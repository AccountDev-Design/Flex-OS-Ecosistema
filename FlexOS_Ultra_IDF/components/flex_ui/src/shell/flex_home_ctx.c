// Flex OS Ultra · menu contextual del escritorio (docs/spec/01a §9; AppDrawer.h:30-291).
//
// Pulsacion larga (> 1 s) sobre un icono de la rejilla: una tarjeta junto al
// icono con las acciones de esa app. Texto a la izquierda, glifo a la derecha,
// filas separadas por una linea; sin "Cancelar" (se cierra tocando fuera). Abre
// y cierra en 150 ms (escala 0,88 -> 1 y fundido); la accion se ejecuta al
// terminar de cerrarse. Una fila inactiva se ve atenuada y no hace nada.
#include <string.h>
#include "flex_app.h"
#include "flex_auth.h"
#include "flex_frame.h"
#include "flex_glass.h"
#include "flex_home_model.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_touch_lvgl.h"

#define CTX_ROWS_MAX 5
#define CTX_ROW_H    58
#define CTX_W        244
#define CTX_RAD      20
#define CTX_GLYPH_S  26
#define CTX_PAD_L    18
#define CTX_PAD_R    14
#define CTX_MARGIN   8
#define CTX_GAPX     12
#define CTX_ANIM_MS  150

enum { K_LOCK = 0, K_EDIT, K_KIOSK, K_FS, K_FS_LAND };

// Pantalla completa llega con el marco de apps inmersivas: mientras no este, sus
// filas se ven pero inactivas.
__attribute__((weak)) bool flex_app_immersive_available(void)
{
    return false;
}
__attribute__((weak)) void flex_app_immersive_open(int app, bool land)
{
    (void)app;
    (void)land;
}

static struct {
    lv_obj_t *scrim, *panel;
    int app, n, action;
    uint8_t kind[CTX_ROWS_MAX];
    bool closing;
} C = {.app = -1, .action = -1};

static bool app_locked(int app)
{
    return app >= 0 && app < 32 && ((g_home.lock >> app) & 1u);
}

static bool row_enabled(int i)
{
    switch (C.kind[i]) {
    case K_LOCK:
    case K_KIOSK: return flex_auth_required();   // sin clave no hay con que verificar
    case K_EDIT: return true;
    default: return flex_app_immersive_available();
    }
}

static const char *row_label(int i)
{
    switch (C.kind[i]) {
    case K_LOCK: return app_locked(C.app) ? "Desbloquear app" : "Bloquear app";
    case K_EDIT: return "Modo edici\xC3\xB3n";
    case K_FS: return "Pantalla completa";
    case K_FS_LAND: return "Pantalla completa horizontal";
    default: return "Modo kiosko";
    }
}

// ---- glifos de 26 px (ctxGlyph) ---------------------------------------------------------
static lv_obj_t *part(lv_obj_t *p, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, lv_color_t c)
{
    lv_obj_t *o = flex_box(p);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_clickable(o, false);
    return o;
}

static void glyph(lv_obj_t *g, int kind, lv_color_t hole)
{
    const flex_palette_t *t = flex_th();
    const int s = CTX_GLYPH_S;
    if (kind == K_LOCK) {   // candado: accion delicada
        lv_color_t c = t->danger;
        part(g, s / 2 - s / 4, s / 3 - s / 4, 2 * (s / 4) + 1, 2 * (s / 4) + 1, LV_RADIUS_CIRCLE, c);
        part(g, s / 2 - s / 6, s / 3 - s / 6, 2 * (s / 6) + 1, 2 * (s / 6) + 1, LV_RADIUS_CIRCLE, hole);
        part(g, 3, s / 2 - 2, s - 6, s / 2 + 1, 3, c);
    } else if (kind == K_EDIT) {   // rejilla de iconos
        int q = (s - 5) / 2;
        for (int k = 0; k < 4; k++) {
            part(g, (k % 2) * (q + 5), (k / 2) * (q + 5), q, q, 2, t->txt2);
        }
    } else if (kind == K_FS || kind == K_FS_LAND) {   // cuatro esquinas de un marco alto o ancho
        bool land = kind == K_FS_LAND;
        int bw = land ? s : s * 2 / 3, bh = land ? s * 2 / 3 : s, bx = (s - bw) / 2, by = (s - bh) / 2;
        const int L = 8, w = 3;
        lv_color_t c = t->primary;
        part(g, bx, by, L, w, 0, c);
        part(g, bx, by, w, L, 0, c);
        part(g, bx + bw - L, by, L, w, 0, c);
        part(g, bx + bw - w, by, w, L, 0, c);
        part(g, bx, by + bh - w, L, w, 0, c);
        part(g, bx, by + bh - L, w, L, 0, c);
        part(g, bx + bw - L, by + bh - w, L, w, 0, c);
        part(g, bx + bw - w, by + bh - L, w, L, 0, c);
    } else {   // pantalla con candado (kiosco)
        lv_color_t c = t->ok;
        part(g, 0, 1, s, s - 7, 3, c);
        part(g, 3, 4, s - 6, s - 13, 2, hole);
        part(g, s / 2 - 3, s / 2 - 5, 6, 7, 1, c);
        part(g, s / 3, s - 5, s / 3, 3, 1, c);
    }
}

// ---- candado (LSU_AFTER_LOCKAPP / UNLOCKAPP) ---------------------------------------------
static void lock_ok(void *ctx)
{
    int app = (int)(intptr_t)ctx;
    if (app >= 0 && app < 32) {
        g_home.lock ^= 1u << app;
        flex_home_save();   // "applockm", 32 bits
    }
    flex_shell_show_home();
    flex_home_rebuild();
}

static void lock_cancel(void *ctx)
{
    (void)ctx;
    flex_shell_show_home();
}

// ---- abrir / cerrar ---------------------------------------------------------------------
static void anim_exec(void *obj, int32_t v)
{
    // v 0..256: escala 0,88 -> 1 y fundido, ease-out cuadratica (la da el camino)
    lv_obj_set_style_transform_scale(obj, 225 + (31 * v) / 256, 0);
    lv_obj_set_style_opa(obj, (lv_opa_t)(v >= 256 ? 255 : v), 0);
}

static void finish(void)
{
    int a = C.action, app = C.app;
    int k = a >= 0 && a < C.n ? C.kind[a] : -1;
    bool en = k >= 0 && row_enabled(a);
    if (C.scrim) {
        lv_obj_delete(C.scrim);   // borra tambien el panel
    }
    C.scrim = C.panel = NULL;
    C.app = C.action = -1;
    C.closing = false;
    flex_shell_overlay_end();   // escritorio limpio
    if (!en) {
        return;   // cancelado (toque fuera, Atras)
    }
    if (k == K_LOCK) {
        // Poner Y quitar el candado exigen la clave: nadie desbloquea la app de otro
        flex_auth_req_t r = {.on_ok = lock_ok, .on_cancel = lock_cancel, .ctx = (void *)(intptr_t)app};
        flex_auth_verify(&r);
    } else if (k == K_EDIT) {
        flex_home_edit_enter();
    } else if (k == K_KIOSK) {
        flex_kiosk_set_open(app);
    } else if (k == K_FS || k == K_FS_LAND) {
        flex_app_immersive_open(app, k == K_FS_LAND);
    }
}

static void anim_done(lv_anim_t *a)
{
    (void)a;
    if (C.closing) {
        finish();
    }
}

static void animate(bool open)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, C.panel);
    lv_anim_set_exec_cb(&a, anim_exec);
    lv_anim_set_values(&a, open ? 0 : 256, open ? 256 : 0);
    lv_anim_set_duration(&a, CTX_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, anim_done);
    lv_anim_start(&a);
}

static void close_menu(int action)
{
    if (!C.panel || C.closing) {
        return;
    }
    C.action = action;
    C.closing = true;
    lv_anim_delete(C.panel, anim_exec);
    animate(false);
}

static void ov_cancel(void)
{
    close_menu(-1);
}

static const flex_overlay_ops_t s_ops = {ov_cancel, ov_cancel, ov_cancel};

static void row_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (C.closing || !row_enabled(i)) {
        return;   // inerte: ni siquiera cierra el menu
    }
    close_menu(i);
}

static void scrim_cb(lv_event_t *e)
{
    (void)e;
    close_menu(-1);   // toque fuera: cancelar
}

bool flex_home_ctx_open(void)
{
    return C.panel != NULL;
}

// Lo llama el escritorio con el rectangulo del icono pulsado
void flex_home_ctx_menu(int app, const lv_area_t *icon)
{
    if (C.panel || app < 0 || app >= FLEX_APP_N || flex_shell_state() != FLEX_SH_HOME) {
        return;
    }
    C.app = app;
    C.n = 0;
    C.kind[C.n++] = K_LOCK;
    C.kind[C.n++] = K_EDIT;
    C.kind[C.n++] = K_KIOSK;
    const flex_app_def_t *d = flex_app_def(app);
    if (d && (d->flags & FLEX_APP_IMMERSIVE)) {
        C.kind[C.n++] = K_FS;
        C.kind[C.n++] = K_FS_LAND;
    }
    int32_t ph = C.n * CTX_ROW_H, ix = icon->x1, iy = icon->y1, S = lv_area_get_width(icon);
    // a la derecha si cabe entero; si no a la izquierda; si no, el lado con mas sitio
    int32_t room_r = (480 - CTX_MARGIN) - (ix + S + CTX_GAPX), room_l = (ix - CTX_GAPX) - CTX_MARGIN;
    bool right = room_r >= CTX_W ? true : room_l >= CTX_W ? false : room_r >= room_l;
    int32_t px = right ? ix + S + CTX_GAPX : ix - CTX_GAPX - CTX_W, py = iy;
    px = px < CTX_MARGIN ? CTX_MARGIN : px > 480 - CTX_MARGIN - CTX_W ? 480 - CTX_MARGIN - CTX_W : px;
    py = py < CTX_MARGIN ? CTX_MARGIN : py > 800 - CTX_MARGIN - ph ? 800 - CTX_MARGIN - ph : py;

    const flex_palette_t *t = flex_th();
    C.scrim = flex_box(lv_layer_top());
    lv_obj_set_size(C.scrim, 480, 800);
    lv_obj_set_clickable(C.scrim, true);
    lv_obj_set_gesture_bubble(C.scrim, false);
    lv_obj_add_event_cb(C.scrim, scrim_cb, LV_EVENT_CLICKED, NULL);

    C.panel = flex_box(C.scrim);
    lv_obj_set_pos(C.panel, px, py);
    lv_obj_set_size(C.panel, CTX_W, ph);
    lv_obj_set_style_radius(C.panel, CTX_RAD, 0);
    flex_surface(C.panel, FLEX_SURF_ELEVATED, FLEX_BD_HOME);
    flex_surface_set_flat(C.panel, t->surf2, 238);
    lv_obj_set_style_transform_pivot_x(C.panel, CTX_W / 2, 0);
    lv_obj_set_style_transform_pivot_y(C.panel, ph / 2, 0);
    lv_obj_set_clickable(C.panel, true);   // un toque en el panel fuera de una fila no cancela
    lv_color_t hole = flex_look()->glass ? t->glass2 : t->surf2;
    int text_max = CTX_W - CTX_PAD_L - CTX_GLYPH_S - CTX_PAD_R - 10;
    for (int i = 0; i < C.n; i++) {
        lv_obj_t *row = flex_box(C.panel);
        lv_obj_set_pos(row, 0, i * CTX_ROW_H);
        lv_obj_set_size(row, CTX_W, CTX_ROW_H);
        lv_obj_set_clickable(row, true);
        lv_obj_add_event_cb(row, row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        if (i > 0) {
            lv_obj_t *sep = part(row, 14, 0, CTX_W - 28, 1, 0, t->mute);
            lv_obj_set_style_bg_opa(sep, 95, 0);
        }
        bool en = row_enabled(i);
        const char *lb = row_label(i);
        // uiFontFit(texto, 176, 3)
        const lv_font_t *f = FLEX_FONT_S3;
        if (flex_text_width(lb, f) > text_max) {
            f = FLEX_FONT_S2;
        }
        if (flex_text_width(lb, f) > text_max) {
            f = FLEX_FONT_S1;
        }
        lv_obj_t *l = flex_label(row, lb, f, en ? t->txt : t->mute);
        lv_obj_set_x(l, CTX_PAD_L);
        lv_obj_set_y(l, CTX_ROW_H / 2 - lv_font_get_line_height(f) / 2);
        lv_obj_t *g = flex_box(row);
        lv_obj_set_pos(g, CTX_W - CTX_PAD_R - CTX_GLYPH_S, CTX_ROW_H / 2 - CTX_GLYPH_S / 2);
        lv_obj_set_size(g, CTX_GLYPH_S, CTX_GLYPH_S);
        lv_obj_set_clickable(g, false);
        lv_obj_set_style_opa(g, en ? LV_OPA_COVER : 110, 0);
        glyph(g, C.kind[i], hole);
    }
    anim_exec(C.panel, 0);
    flex_shell_overlay_begin(&s_ops);
    flex_touch_drop_all();   // el dedo de la pulsacion larga no toca el menu al soltar
    animate(true);
}
