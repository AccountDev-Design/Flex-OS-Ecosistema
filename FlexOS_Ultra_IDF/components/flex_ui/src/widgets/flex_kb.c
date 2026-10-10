// Flex OS Ultra · teclado del sistema en LVGL (ver flex_kb.h).
//
// Estructura de objetos (todo LVGL, ningun buffer propio):
//   panel (flex_surface: Liquid Glass o Plano, recibe TODOS los toques)
//     └ keys  (contenedor sin fondo: lo que se mueve en la sacudida)
//         ├ 30 teclas (flex_surface forzada a Plano, como kbPaintKey) + etiqueta
//         └ 6 teclas de funcion (kbFKey) + etiqueta
//   popup de acentos (hermano del panel, solo mientras se ve)
// El toque se resuelve con el hit-test de Arduino (flex_kb_cell_at /
// flex_kb_frow_hit) sobre el punto en coordenadas de pantalla: lo que se ve y
// lo que se toca salen de la misma geometria.
#include "flex_kb.h"

#include <stdlib.h>
#include <string.h>
#include "flex_frame.h"
#include "flex_theme.h"
#include "flex_touch_lvgl.h"
#include "flex_wallpaper.h"

typedef enum {
    SCHEME_SYSTEM = 0,   // Notas, Wi-Fi, Ajustes del teclado: kbCol*
    SCHEME_PAGE,         // clave al crearla: PAGE_BG / SET_CARD_BG
    SCHEME_WALL,         // clave al verificar: TH_WALL*
} scheme_t;

typedef struct {
    lv_color_t panel;        // tinte del vidrio y relleno plano del panel
    lv_color_t panel_flat;   // relleno plano (difiere del tinte en SCHEME_PAGE)
    lv_opa_t panel_opa;      // < 255: relleno plano con alfa (kbopa < 100)
    lv_color_t key, key_txt;
    lv_color_t fn, fn_txt, fn_on, fn_on_txt;
    lv_color_t press, edge;
} kb_colors_t;

typedef struct kb_s {
    lv_obj_t *obj, *keys;
    lv_obj_t *key[FLEX_KB_CELLS], *klbl[FLEX_KB_CELLS];
    lv_obj_t *fkey[FLEX_KB_FKEYS], *flbl[FLEX_KB_FKEYS];
    lv_obj_t *pop;
    flex_kb_cfg_t cfg;
    scheme_t scheme;
    flex_kb_state_t st;
    flex_kb_geom_t g;
    flex_kb_prefs_t pr;
    kb_colors_t col;
    flex_kb_fx_t fx;
    lv_timer_t *fx_timer;
    // episodio tactil en curso
    bool down, ignore, fired;
    int held;               // celda con el dedo encima (escritura rapida)
    int lp_cell;            // vocal candidata a acentos
    int px0, py0;           // punto de apoyo (coordenadas de pantalla en reposo)
    uint32_t t0;
    // ventana de acentos
    int pop_x, pop_y, pop_n;
    const char *pop_var[4];
    bool anim, input_off;
    struct kb_s *next;
} kb_t;

#define POP_W   40
#define POP_H   46
#define POP_GAP 4

static kb_t *s_live;
static bool s_listening;

static bool fast_on(const kb_t *k)
{
    return k->pr.fast && !(k->cfg.flags & FLEX_KB_F_NO_FAST);
}

static const lv_font_t *font_of(int size)
{
    return size <= 1 ? FLEX_FONT_S1 : size == 2 ? FLEX_FONT_S2 : FLEX_FONT_S3;
}

// ---- colores: misma correspondencia que Arduino ----------------------------------
// kbColKey/KeyTxt/Fn/FnOn/FnOnTxt/Panel/Edge/Press (Keyboard.h:225-235) y los de
// la pantalla de clave (Lock.h:64-72). Las teclas de funcion pasan SIEMPRE por
// kbFKey, que usa los colores del sistema tambien en la clave sobre el fondo.
static void compute_colors(kb_t *k)
{
    const flex_palette_t *t = flex_th();
    kb_colors_t *c = &k->col;
    bool hc = k->pr.hicon;
    lv_color_t amber = lv_color_make(255, 210, 0);
    c->fn = hc ? lv_color_make(24, 24, 24) : t->key_alt;
    c->fn_txt = hc ? lv_color_white() : t->txt;
    c->fn_on = hc ? amber : flex_accent();
    c->fn_on_txt = hc ? lv_color_black() : t->on_acc;
    c->press = hc ? amber : flex_c565(flex_mix565(flex_lv_to_565(flex_accent()), flex_lv_to_565(t->key_face), 60));
    c->edge = hc ? lv_color_white() : t->border;
    c->panel_opa = LV_OPA_COVER;
    switch (k->scheme) {
    case SCHEME_WALL:
        c->panel = c->panel_flat = FLEX_WALLPANEL;
        c->key = FLEX_WALLSURF;
        c->key_txt = FLEX_ONWALL;
        break;
    case SCHEME_PAGE:
        c->panel = t->glass;        // SET_CARD_GLASS
        c->panel_flat = t->page;    // PAGE_BG
        c->key = t->surf;           // SET_CARD_BG
        c->key_txt = t->txt;        // SET_TXT_HI
        break;
    default:
        // kbColPanel: con Liquid Glass el tinte del tema, plano el fondo de teclado.
        // kbPaintPanel: por debajo del 100 % de opacidad, relleno plano con alfa.
        c->panel = c->panel_flat = hc ? lv_color_black() : (flex_look()->glass ? t->glass : t->key_panel);
        c->panel_opa = (lv_opa_t)(k->pr.opacity * 255 / 100);
        c->key = hc ? lv_color_black() : t->key_face;
        c->key_txt = hc ? lv_color_white() : t->txt;
        break;
    }
}

// ---- pintado ------------------------------------------------------------------------
// kbPaintKey: relleno (o solo contorno en el estilo "Contorno") y destello.
static void paint_key(const kb_t *k, lv_obj_t *key, lv_obj_t *lbl, lv_color_t bg, lv_color_t txt, bool pressed)
{
    bool contour = k->pr.style == 2;
    lv_opa_t opa = (contour && !pressed) ? LV_OPA_TRANSP : LV_OPA_COVER;
    flex_surface_set_flat(key, pressed ? k->col.press : bg, opa);
    lv_obj_set_style_text_color(lbl, txt, 0);
}

static void paint_cell(kb_t *k, int cell)
{
    if (cell < 0 || cell >= FLEX_KB_CELLS) {
        return;
    }
    bool hot = cell == k->held || flex_kb_fx_level(&k->fx, cell, lv_tick_get(), k->pr.fx_ms) > 0;
    paint_key(k, k->key[cell], k->klbl[cell], k->col.key, k->col.key_txt, hot);
}

static void paint_fkey(kb_t *k, int i)
{
    bool on = i == FLEX_KB_FN_SHIFT && k->st.shift;
    paint_key(k, k->fkey[i], k->flbl[i], on ? k->col.fn_on : k->col.fn, on ? k->col.fn_on_txt : k->col.fn_txt, false);
}

// Etiquetas segun capa y shift (kbResolveKey sin consumir) y fila de funciones.
static void refresh_labels(kb_t *k)
{
    for (int i = 0; i < FLEX_KB_CELLS; i++) {
        char out[6];
        const char *s = flex_kb_resolve(&k->st, flex_kb_key_base(&k->st, i), out, false);
        lv_label_set_text(k->klbl[i], s);
    }
    const char *lb[FLEX_KB_FKEYS] = {"shift", flex_kb_layer_label(&k->st), flex_kb_lang_label(&k->st), "espacio", "<-",
                                     k->cfg.enter_label ? k->cfg.enter_label : "OK"};
    for (int i = 0; i < FLEX_KB_FKEYS; i++) {
        lv_label_set_text(k->flbl[i], lb[i]);
        paint_fkey(k, i);
    }
}

static void paint_all(kb_t *k)
{
    for (int i = 0; i < FLEX_KB_CELLS; i++) {
        paint_cell(k, i);
    }
    for (int i = 0; i < FLEX_KB_FKEYS; i++) {
        paint_fkey(k, i);
    }
}

// Coloca una tecla y su etiqueta: el tope de las mayusculas en h/2 - kbFontDy,
// como drawTextC(x + w/2, y + h/2 - kbFontDy(), ...).
static void place_key(lv_obj_t *key, lv_obj_t *lbl, int x, int y, int w, int h, int radius, const lv_font_t *f, int dy)
{
    lv_obj_set_pos(key, x, y);
    lv_obj_set_size(key, w, h);
    lv_obj_set_style_radius(key, radius, 0);
    lv_obj_set_style_text_font(lbl, f, 0);
    lv_obj_set_width(lbl, w);
    lv_obj_set_x(lbl, 0);
    flex_label_cap_y(lbl, h / 2 - dy);
}

static void apply_style(kb_t *k)
{
    compute_colors(k);
    flex_surface_set_tint(k->obj, k->col.panel);
    if (k->col.panel_opa < LV_OPA_COVER) {
        flex_surface_force_material(k->obj, 0);
    } else {
        flex_surface_force_material(k->obj, -1);
    }
    flex_surface_set_flat(k->obj, k->col.panel_flat, k->col.panel_opa);
    bool contour = k->pr.style == 2;
    for (int i = 0; i < FLEX_KB_CELLS + FLEX_KB_FKEYS; i++) {
        lv_obj_t *o = i < FLEX_KB_CELLS ? k->key[i] : k->fkey[i - FLEX_KB_CELLS];
        lv_obj_set_style_border_width(o, contour ? 1 : 0, 0);
        lv_obj_set_style_border_color(o, k->col.edge, 0);
        lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
    }
    paint_all(k);
}

static void apply_geometry(kb_t *k)
{
    flex_kb_geom_init(&k->g, k->pr.size, k->cfg.bottom_reserve, 0);
    int top = flex_kb_panel_top(&k->g);
    lv_obj_set_pos(k->obj, 0, top);
    lv_obj_set_size(k->obj, FLEX_KB_SCR_W, FLEX_KB_SCR_H - top);
    lv_obj_set_size(k->keys, FLEX_KB_SCR_W, FLEX_KB_SCR_H - top);
    int rad = flex_kb_radius(&k->pr);
    int fs = flex_kb_font_size(&k->pr), dy = flex_kb_font_dy(&k->pr);
    for (int i = 0; i < FLEX_KB_CELLS; i++) {
        int x, y;
        flex_kb_cell_xy(&k->g, i, &x, &y);
        place_key(k->key[i], k->klbl[i], x, y - top, k->g.kw, k->g.kh, rad, font_of(fs), dy);
    }
    // kbFKey: talla de letra min(kbFontSize, 2) y el mismo desfase de la talla de teclas
    int fy = flex_kb_func_y(&k->g) - top;
    for (int i = 0; i < FLEX_KB_FKEYS; i++) {
        place_key(k->fkey[i], k->flbl[i], flex_kb_fkey_x(&k->g, i), fy, flex_kb_fkey_w(&k->g, i), k->g.kh, rad,
                  font_of(fs > 2 ? 2 : fs), dy);
    }
}

// ---- destello por tiempo (kbFxStart / kbFxTick) ----------------------------------
static void fx_timer_cb(lv_timer_t *t)
{
    kb_t *k = lv_timer_get_user_data(t);
    int c = flex_kb_fx_tick(&k->fx, lv_tick_get(), k->pr.fx_ms);
    if (c >= 0) {
        paint_cell(k, c);
    }
    if (k->fx.cell < 0) {
        lv_timer_pause(t);
    }
}

static void fx_start(kb_t *k, int cell)
{
    if (cell < 0) {
        return;
    }
    int old = k->fx.cell;
    flex_kb_fx_start(&k->fx, cell, lv_tick_get());
    if (old >= 0 && old != cell) {
        paint_cell(k, old);
    }
    paint_cell(k, cell);
    lv_timer_set_period(k->fx_timer, (uint32_t)k->pr.fx_ms);
    lv_timer_reset(k->fx_timer);
    lv_timer_resume(k->fx_timer);
}

// ---- ventana de acentos (kbRenderPopup / kbPopupHit) ------------------------------
static void popup_close(kb_t *k)
{
    if (k->pop) {
        lv_obj_delete(k->pop);
        k->pop = NULL;
    }
    k->pop_n = 0;
}

static void popup_open(kb_t *k, int cell)
{
    const char *base = flex_kb_key_base(&k->st, cell);
    int n = flex_kb_variants(base[0], k->pop_var);
    if (n == 0) {
        return;
    }
    int kx, ky;
    flex_kb_cell_xy(&k->g, cell, &kx, &ky);
    int totw = n * POP_W + (n - 1) * POP_GAP;
    int px0 = kx + k->g.kw / 2 - totw / 2;
    if (px0 < 4) {
        px0 = 4;
    }
    if (px0 + totw > FLEX_KB_SCR_W - 4) {
        px0 = FLEX_KB_SCR_W - 4 - totw;
    }
    int py0 = ky - POP_H - 10;
    k->pop_x = px0;
    k->pop_y = py0;
    k->pop_n = n;
    // uiSurface(px0 - 6, py0 - 6, totw + 12, ph + 12, 10, UIS_ELEVATED)
    lv_obj_t *p = flex_box(lv_obj_get_parent(k->obj));
    lv_obj_set_pos(p, px0 - 6, py0 - 6);
    lv_obj_set_size(p, totw + 12, POP_H + 12);
    lv_obj_set_style_radius(p, 10, 0);
    lv_obj_set_clickable(p, false);
    flex_surface(p, FLEX_SURF_ELEVATED, k->cfg.backdrop);
    for (int i = 0; i < n; i++) {
        lv_obj_t *b = flex_box(p);
        lv_obj_set_pos(b, 6 + i * (POP_W + POP_GAP), 6);
        lv_obj_set_size(b, POP_W, POP_H);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, k->col.key, 0);
        lv_obj_t *l = flex_label(b, k->pop_var[i], FLEX_FONT_S3, k->col.key_txt);
        lv_obj_set_width(l, POP_W);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        flex_label_cap_y(l, POP_H / 2 - 12);
    }
    k->pop = p;
}

static int popup_hit(const kb_t *k, int px, int py)
{
    for (int i = 0; i < k->pop_n; i++) {
        int x = k->pop_x + i * (POP_W + POP_GAP);
        if (px >= x && px <= x + POP_W && py >= k->pop_y && py <= k->pop_y + POP_H) {
            return i;
        }
    }
    return -1;
}

// ---- salida: los callbacks van SIEMPRE al final (pueden borrar el teclado) --------
static void emit_text(kb_t *k, const char *s)
{
    if (k->cfg.on_text) {
        k->cfg.on_text(k->obj, s, k->cfg.user);
    }
}

// kbPressChar: resuelve con shift (y lo consume) y escribe.
static void press_char(kb_t *k, int cell)
{
    char out[6], txt[8];
    bool shift = k->st.shift;
    const char *s = flex_kb_resolve(&k->st, flex_kb_key_base(&k->st, cell), out, true);
    strncpy(txt, s, sizeof(txt) - 1);
    txt[sizeof(txt) - 1] = 0;
    if (shift != k->st.shift) {
        refresh_labels(k);   // el shift se apago: las etiquetas vuelven a minusculas
    }
    emit_text(k, txt);
}

static void fn_key(kb_t *k, int fn)
{
    switch (fn) {
    case FLEX_KB_FN_SHIFT:
    case FLEX_KB_FN_LAYER:
    case FLEX_KB_FN_LANG:
        flex_kb_fn_apply(&k->st, fn);
        refresh_labels(k);
        break;
    case FLEX_KB_FN_SPACE:
        emit_text(k, " ");
        break;
    case FLEX_KB_FN_BACKSPACE:
        if (k->cfg.on_backspace) {
            k->cfg.on_backspace(k->obj, k->cfg.user);
        }
        break;
    case FLEX_KB_FN_ENTER:
        if (k->cfg.on_enter) {
            k->cfg.on_enter(k->obj, k->cfg.user);
        }
        break;
    default:
        break;
    }
}

// ---- entrada ------------------------------------------------------------------------
static void release_held(kb_t *k)
{
    int h = k->held;
    k->held = -1;
    paint_cell(k, h);
}

static void touch_cb(lv_event_t *e)
{
    lv_event_code_t tc = lv_event_get_code(e);
    if (tc == LV_EVENT_PRESSED || tc == LV_EVENT_PRESSING) {
        flex_touch_typing_mark();   // veto del gesto de suspender mientras se teclea
    }
    kb_t *k = lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *in = lv_indev_active();
    if (!k || !in) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(in, &p);
    // Punto en coordenadas de pantalla con el panel en reposo
    lv_area_t a;
    lv_obj_get_coords(k->obj, &a);
    int px = p.x - a.x1, py = p.y - a.y1 + flex_kb_panel_top(&k->g);

    if (code == LV_EVENT_PRESSED) {
        k->down = true;
        k->fired = false;
        k->ignore = k->anim || k->input_off;   // durante la entrada no se lee nada
        k->px0 = px;
        k->py0 = py;
        k->t0 = lv_tick_get();
        k->lp_cell = -1;
        if (k->ignore) {
            return;
        }
        int cell = flex_kb_cell_at(&k->g, px, py);
        int fn = flex_kb_frow_hit(&k->g, px, py);
        if ((k->cfg.flags & FLEX_KB_F_ACCENTS) && flex_kb_is_vowel_cell(&k->st, cell)) {
            k->lp_cell = cell;
        }
        if (fast_on(k)) {
            // Via rapida (Fase B): la tecla se escribe al TOCAR. Como en kbMtPoll,
            // si el punto cae en una tecla de letra gana la letra.
            if (cell >= 0) {
                k->fired = true;
                k->held = cell;
                fx_start(k, cell);
                press_char(k, cell);
                return;
            }
            if (fn >= 0 && (k->cfg.flags & FLEX_KB_F_FN_ON_PRESS)) {
                k->fired = true;
                fn_key(k, fn);
                return;
            }
        } else {
            // Sin escritura rapida la tecla se escribe al soltar: el destello al
            // apoyar es la unica senal de que el toque entro (kbFxPress).
            fx_start(k, cell);
        }
        return;
    }

    if (code == LV_EVENT_PRESSING) {
        if (k->ignore || k->lp_cell < 0 || k->pop) {
            return;
        }
        if (lv_tick_elaps(k->t0) > (uint32_t)k->pr.lp_ms) {
            popup_open(k, k->lp_cell);
        }
        return;
    }

    if (code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) {
        return;
    }
    bool was_down = k->down;
    k->down = false;
    if (k->ignore || !was_down) {
        k->ignore = false;
        return;
    }
    release_held(k);
    if (k->pop) {
        // Soltar sobre un acento lo escribe (si la via rapida ya escribio la
        // vocal, primero se borra); soltar fuera escribe la vocal si aun no estaba.
        int v = code == LV_EVENT_RELEASED ? popup_hit(k, px, py) : -1;
        int cell = k->lp_cell;
        bool base_written = k->fired;
        char var[8] = "";
        if (v >= 0) {
            strncpy(var, k->pop_var[v], sizeof(var) - 1);
        }
        popup_close(k);
        k->lp_cell = -1;
        if (v >= 0) {
            if (base_written && k->cfg.on_backspace) {
                k->cfg.on_backspace(k->obj, k->cfg.user);
            }
            emit_text(k, var);
        } else if (!base_written && code == LV_EVENT_RELEASED) {
            fx_start(k, cell);
            press_char(k, cell);
        }
        return;
    }
    k->lp_cell = -1;
    if (code != LV_EVENT_RELEASED || k->fired) {
        return;
    }
    // Toque (Touch.h: |dx|,|dy| < 16 y < 550 ms), localizado donde se apoyo
    if (!flex_kb_is_tap(px - k->px0, py - k->py0, lv_tick_elaps(k->t0))) {
        return;
    }
    int fn = flex_kb_frow_hit(&k->g, k->px0, k->py0);
    if (fn >= 0) {
        fn_key(k, fn);
        return;
    }
    if (!fast_on(k)) {
        int cell = flex_kb_cell_at(&k->g, k->px0, k->py0);
        if (cell >= 0) {
            fx_start(k, cell);
            press_char(k, cell);
        }
    }
}

// ---- ciclo de vida y tema ------------------------------------------------------------
static void theme_cb(void *ctx)
{
    (void)ctx;
    for (kb_t *k = s_live; k; k = k->next) {
        apply_style(k);
    }
}

static void slide_exec(void *var, int32_t v)
{
    kb_t *k = lv_obj_get_user_data(var);
    if (k) {
        lv_obj_set_y(k->obj, flex_kb_panel_top(&k->g) + v);
    }
}

static void delete_cb(lv_event_t *e)
{
    kb_t *k = lv_event_get_user_data(e);
    if (!k) {
        return;
    }
    lv_anim_delete(k->obj, slide_exec);
    if (k->fx_timer) {
        lv_timer_delete(k->fx_timer);
    }
    if (k->pop) {
        lv_obj_delete(k->pop);
    }
    for (kb_t **pp = &s_live; *pp; pp = &(*pp)->next) {
        if (*pp == k) {
            *pp = k->next;
            break;
        }
    }
    lv_obj_set_user_data(k->obj, NULL);
    free(k);
}

static lv_obj_t *make_key(kb_t *k, lv_obj_t **lbl)
{
    lv_obj_t *o = flex_box(k->keys);
    lv_obj_set_clickable(o, false);
    // La tecla es una superficie del sistema, pero el material es SIEMPRE plano:
    // en Arduino las teclas son fillRoundRect aun con Liquid Glass (el vidrio es
    // el panel). El color lo pone paint_key con flex_surface_set_flat.
    flex_surface(o, FLEX_SURF_TINT, FLEX_BD_FLAT);
    flex_surface_force_material(o, 0);
    *lbl = flex_label(o, "", FLEX_FONT_S2, lv_color_white());
    lv_label_set_long_mode(*lbl, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(*lbl, LV_TEXT_ALIGN_CENTER, 0);
    return o;
}

lv_obj_t *flex_kb_create(lv_obj_t *parent, const flex_kb_cfg_t *cfg)
{
    kb_t *k = calloc(1, sizeof(*k));
    if (!k) {
        return NULL;
    }
    if (cfg) {
        k->cfg = *cfg;
    }
    k->scheme = (k->cfg.flags & FLEX_KB_F_WALLPAPER) ? SCHEME_WALL
                : (k->cfg.flags & FLEX_KB_F_PAGE)    ? SCHEME_PAGE
                                                      : SCHEME_SYSTEM;
    flex_kb_state_init(&k->st, !(k->cfg.flags & FLEX_KB_F_LANG_EN));
    if (k->cfg.layout == FLEX_KB_LAYOUT_NUM || k->cfg.layout == FLEX_KB_LAYOUT_EMOJI) {
        k->st.layout = (uint8_t)k->cfg.layout;
    }
    flex_kb_prefs_load(&k->pr);
    flex_kb_fx_reset(&k->fx);
    k->held = -1;
    k->lp_cell = -1;

    k->obj = flex_box(parent);
    lv_obj_set_user_data(k->obj, k);
    lv_obj_set_clickable(k->obj, true);
    lv_obj_set_press_lock(k->obj, true);
    lv_obj_set_scroll_chain(k->obj, false);   // teclear nunca desplaza lo de debajo
    lv_obj_set_style_radius(k->obj, 0, 0);
    flex_surface(k->obj, FLEX_SURF_TINT, k->cfg.backdrop);
    k->keys = flex_box(k->obj);
    lv_obj_set_clickable(k->keys, false);
    lv_obj_set_pos(k->keys, 0, 0);
    for (int i = 0; i < FLEX_KB_CELLS; i++) {
        k->key[i] = make_key(k, &k->klbl[i]);
    }
    for (int i = 0; i < FLEX_KB_FKEYS; i++) {
        k->fkey[i] = make_key(k, &k->flbl[i]);
    }
    k->fx_timer = lv_timer_create(fx_timer_cb, (uint32_t)k->pr.fx_ms, k);
    lv_timer_pause(k->fx_timer);

    lv_obj_add_event_cb(k->obj, touch_cb, LV_EVENT_PRESSED, k);
    lv_obj_add_event_cb(k->obj, touch_cb, LV_EVENT_PRESSING, k);
    lv_obj_add_event_cb(k->obj, touch_cb, LV_EVENT_RELEASED, k);
    lv_obj_add_event_cb(k->obj, touch_cb, LV_EVENT_PRESS_LOST, k);
    lv_obj_add_event_cb(k->obj, delete_cb, LV_EVENT_DELETE, k);

    apply_geometry(k);
    refresh_labels(k);
    apply_style(k);

    k->next = s_live;
    s_live = k;
    if (!s_listening) {
        flex_theme_listen(theme_cb, NULL);
        s_listening = true;
    }
    return k->obj;
}

static kb_t *get(const lv_obj_t *kb)
{
    return kb ? lv_obj_get_user_data((lv_obj_t *)kb) : NULL;
}

void flex_kb_set_layout(lv_obj_t *kb, flex_kb_layout_t layout)
{
    kb_t *k = get(kb);
    if (k) {
        flex_kb_set_layout_state(&k->st, layout);
        refresh_labels(k);
    }
}

flex_kb_layout_t flex_kb_get_layout(const lv_obj_t *kb)
{
    kb_t *k = get(kb);
    return k ? (flex_kb_layout_t)k->st.layout : FLEX_KB_LAYOUT_ES;
}

void flex_kb_set_shift(lv_obj_t *kb, bool on)
{
    kb_t *k = get(kb);
    if (k && k->st.shift != on) {
        k->st.shift = on;
        refresh_labels(k);
    }
}

bool flex_kb_get_shift(const lv_obj_t *kb)
{
    kb_t *k = get(kb);
    return k && k->st.shift;
}

void flex_kb_set_lang_es(lv_obj_t *kb, bool es)
{
    kb_t *k = get(kb);
    if (k && k->st.lang_es != es) {
        flex_kb_fn_apply(&k->st, FLEX_KB_FN_LANG);
        refresh_labels(k);
    }
}

bool flex_kb_get_lang_es(const lv_obj_t *kb)
{
    kb_t *k = get(kb);
    return !k || k->st.lang_es;
}

int32_t flex_kb_height(const lv_obj_t *kb)
{
    kb_t *k = get(kb);
    if (k) {
        return FLEX_KB_SCR_H - flex_kb_panel_top(&k->g);
    }
    flex_kb_prefs_t pr;
    flex_kb_geom_t g;
    flex_kb_prefs_load(&pr);
    flex_kb_geom_init(&g, pr.size, 0, 0);
    return FLEX_KB_SCR_H - flex_kb_panel_top(&g);
}

int32_t flex_kb_top(const lv_obj_t *kb)
{
    kb_t *k = get(kb);
    return k ? flex_kb_panel_top(&k->g) : FLEX_KB_SCR_H - flex_kb_height(NULL);
}

int32_t flex_kb_keys_y(const lv_obj_t *kb)
{
    kb_t *k = get(kb);
    return k ? flex_kb_rows_top(&k->g) : flex_kb_top(NULL) + 4;
}

const flex_kb_geom_t *flex_kb_geom(const lv_obj_t *kb)
{
    kb_t *k = get(kb);
    return k ? &k->g : NULL;
}

// ---- entrada deslizando: 0.3 s lineal ---------------------------------------------
static int32_t slide_path(const lv_anim_t *a)
{
    int32_t t = a->act_time < 0 ? 0 : a->act_time;
    return flex_kb_slide_off(a->start_value, (uint32_t)t);
}

static void slide_done(lv_anim_t *a)
{
    kb_t *k = lv_obj_get_user_data(a->var);
    if (k) {
        k->anim = false;
    }
}

void flex_kb_slide_in(lv_obj_t *kb)
{
    kb_t *k = get(kb);
    if (!k) {
        return;
    }
    // Clave, Wi-Fi: kbh = SCR_H - KB_Y (lsuTick). Notas (con extras): todo el panel.
    int top = flex_kb_panel_top(&k->g);
    int kbh = (k->cfg.flags & FLEX_KB_F_EXTRAS) ? FLEX_KB_SCR_H - top : FLEX_KB_SCR_H - flex_kb_rows_top(&k->g);
    lv_anim_delete(k->obj, slide_exec);
    k->anim = true;
    lv_obj_set_y(k->obj, top + kbh);
    lv_anim_t an;
    lv_anim_init(&an);
    lv_anim_set_var(&an, k->obj);
    lv_anim_set_exec_cb(&an, slide_exec);
    lv_anim_set_values(&an, kbh, 0);
    lv_anim_set_duration(&an, FLEX_KB_SLIDE_MS);
    lv_anim_set_path_cb(&an, slide_path);
    lv_anim_set_completed_cb(&an, slide_done);
    lv_anim_start(&an);
}

bool flex_kb_is_animating(const lv_obj_t *kb)
{
    kb_t *k = get(kb);
    return k && k->anim;
}

void flex_kb_set_shift_x(lv_obj_t *kb, int32_t dx)
{
    kb_t *k = get(kb);
    if (k) {
        lv_obj_set_x(k->keys, dx);
    }
}

lv_obj_t *flex_kb_keys_obj(lv_obj_t *kb)
{
    kb_t *k = get(kb);
    return k ? k->keys : NULL;
}

void flex_kb_set_input_enabled(lv_obj_t *kb, bool en)
{
    kb_t *k = get(kb);
    if (k) {
        k->input_off = !en;
        if (!en) {
            popup_close(k);
            release_held(k);
        }
    }
}

void flex_kb_reload_prefs(lv_obj_t *kb)
{
    kb_t *k = get(kb);
    if (!k) {
        return;
    }
    flex_kb_prefs_load(&k->pr);
    popup_close(k);
    apply_geometry(k);
    refresh_labels(k);
    apply_style(k);
}
