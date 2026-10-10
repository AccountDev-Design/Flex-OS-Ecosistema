// Flex OS Ultra · Modo edicion del escritorio (docs/spec/01a §8; Home.h:1501-2197).
//
// Se entra desde el menu contextual ("Modo edicion"). Una capa a pantalla
// completa sobre el escritorio recibe todo el tactil: los iconos de la pagina
// tiemblan al 89 % de su tamano y vuelven a su celda con un resorte; se
// arrastran y, tras 400 ms sobre otra celda, se reinsertan desplazando los
// demas; sostenidos 700 ms contra un borde pasan a la pagina vecina. Los
// widgets se seleccionan, se mueven por celdas, se quitan con su insignia y
// cambian de tamano con su asa. Tocar en vacio suelta la seleccion o sale.
// Al salir se normaliza y se guarda. Con el diseno bloqueado no hay arrastre,
// insignias ni asa (si se puede seleccionar).
//
// La logica (celdas, reinsercion, bordes, widgets) es la del modelo, probada
// contra Arduino (tests/host/test_home.c).
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_frame.h"
#include "flex_glass.h"
#include "flex_home_model.h"
#include "flex_home_wg.h"
#include "flex_shell.h"
#include "flex_theme.h"
#include "flex_touch_lvgl.h"

#define ED_HINT_Y   (FLEX_HOME_DOCK_Y - 22)
#define ED_MSG_MS   1600
#define ED_DWELL_MS 400
#define ED_TICK_MS  38   // ~26 cuadros/s, la cadencia del bucle de Arduino (resorte y temblor)

static struct {
    lv_obj_t *root, *band, *hint;
    lv_obj_t *icon[FLEX_HOME_STRIDE];
    lv_obj_t *drag_panel, *drag_icon;
    lv_timer_t *tick;
    float cx[FLEX_HOME_STRIDE], cy[FLEX_HOME_STRIDE];
    int drag, hover;
    uint32_t hover_ms;
    float dx, dy;
    int wdrag, wsel, wresize, grab_c, grab_r;
    flex_home_edge_t edge;
    char msg[56];
    uint32_t msg_ms;
    bool used;   // este contacto ya hizo algo (quitar un widget): soltar no es "toque en vacio"
} E = {.drag = -1, .hover = -1, .wdrag = -1, .wsel = -1, .wresize = -1};

static void build(void);

bool flex_home_edit_active(void)
{
    return E.root != NULL;
}

static int page(void)
{
    return g_home.page;
}

static uint8_t *slot(int local)
{
    return &g_home.order[flex_home_idx(page(), local)];
}

static void notice(const char *m)
{
    snprintf(E.msg, sizeof(E.msg), "%s", m ? m : "");
    E.msg_ms = lv_tick_get();
}

static void reset_springs(void)
{
    for (int i = 0; i < FLEX_HOME_STRIDE; i++) {
        int x, y;
        flex_home_slot_xy(i, &x, &y);
        E.cx[i] = (float)x;
        E.cy[i] = (float)y;
    }
}

// edSetDrag: el icono arrastrado, acotado a la zona de la rejilla
static void set_drag(int tx, int ty)
{
    int S, gx0, gy0, cs, rs, cols, rows;
    flex_home_grid(&S, &gx0, &gy0, &cs, &rs, &cols, &rows);
    float dx = (float)(tx - S / 2), dy = (float)(ty - S / 2);
    if (dx < 8) dx = 8;
    if (dx > 480 - S - 8) dx = (float)(480 - S - 8);
    if (dy < FLEX_HOME_GY0 - 72) dy = (float)(FLEX_HOME_GY0 - 72);
    if (dy > flex_home_band_bot() - S - 10) dy = (float)(flex_home_band_bot() - S - 10);
    E.dx = dx;
    E.dy = dy;
}

// ---- piezas ------------------------------------------------------------------------------
static lv_obj_t *disc(lv_obj_t *p, int32_t cx, int32_t cy, int32_t r, lv_color_t c, lv_opa_t a)
{
    lv_obj_t *o = flex_box(p);
    lv_obj_set_pos(o, cx - r, cy - r);
    lv_obj_set_size(o, 2 * r + 1, 2 * r + 1);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(o, a, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_clickable(o, false);
    return o;
}

static void stroke(lv_obj_t *p, const lv_point_precise_t *pts, lv_color_t c)
{
    lv_obj_t *l = lv_line_create(p);
    lv_line_set_points(l, pts, 2);
    lv_obj_set_style_line_width(l, 2, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_set_style_line_color(l, c, 0);
    lv_obj_set_clickable(l, false);
}

static void handle_xy(int k, int *hx, int *hy)
{
    int wx, wy, ww, wh;
    flex_home_wg_rect(&g_home.wg[page()][k], &wx, &wy, &ww, &wh);
    *hx = wx + ww - 12;
    *hy = wy + wh - 12;
}

static bool hit_resize(int k, int px, int py)
{
    if (k < 0 || k >= g_home.wg_n[page()] || !flex_home_wg_can_resize(g_home.wg[page()][k].type)) {
        return false;
    }
    int hx, hy;
    handle_xy(k, &hx, &hy);
    return LV_ABS(px - hx) <= 22 && LV_ABS(py - hy) <= 22;   // 44 px de zona tactil
}

// ---- edRender ------------------------------------------------------------------------------
static void place_icons(void)
{
    int S, gx0, gy0, cs, rs, cols, rows;
    flex_home_grid(&S, &gx0, &gy0, &cs, &rs, &cols, &rows);
    uint32_t t = lv_tick_get();
    int s = S * 8 / 9, off = (S - s) / 2;
    for (int i = 0; i < flex_home_slot_count(); i++) {
        if (!E.icon[i]) {
            continue;
        }
        int tx, ty;
        flex_home_slot_xy(i, &tx, &ty);
        E.cx[i] += ((float)tx - E.cx[i]) * 0.2f;   // resorte
        E.cy[i] += ((float)ty - E.cy[i]) * 0.2f;
        float ph = (float)i * 0.6f;
        int ox = (int)(2.0f * sinf((float)t * 0.02f + ph)), oy = (int)(2.0f * cosf((float)t * 0.017f + ph));
        lv_obj_set_pos(E.icon[i], (int32_t)E.cx[i] + off + ox, (int32_t)E.cy[i] + off + oy);
    }
    if (E.drag_panel) {
        lv_obj_set_pos(E.drag_panel, (int32_t)E.dx - 6, (int32_t)E.dy - 6);
    }
}

static void hint_refresh(void)
{
    bool msg = E.msg[0] && lv_tick_get() - E.msg_ms < ED_MSG_MS;
    const char *txt = msg ? E.msg : "Arrastra iconos y widgets - Inicio para salir";
    if (strcmp(lv_label_get_text(E.hint), txt) != 0) {
        lv_label_set_text(E.hint, txt);
        lv_obj_set_style_text_color(E.hint, msg ? FLEX_ONWALL : FLEX_ONWALL2, 0);
        flex_label_cap_center(E.hint, 240, ED_HINT_Y);
    }
}

static void build(void)
{
    lv_obj_clean(E.band);
    memset(E.icon, 0, sizeof(E.icon));
    E.drag_panel = E.drag_icon = NULL;
    const flex_palette_t *t = flex_th();
    int p = page();
    // Widgets bajo los iconos: el seleccionado con su resalte; insignia de quitar y,
    // si su tipo admite otros tamanos, asa
    for (int k = 0; k < g_home.wg_n[p] && k < FLEX_HOME_WG_MAX; k++) {
        const flex_home_wg_t *w = &g_home.wg[p][k];
        if (w->type == FLEX_WG_NONE) {
            continue;
        }
        int wx, wy, ww, wh;
        flex_home_wg_rect(w, &wx, &wy, &ww, &wh);
        bool sel = k == E.wdrag || k == E.wsel || k == E.wresize;
        if (sel) {
            lv_obj_t *h = flex_box(E.band);
            lv_obj_set_pos(h, wx - 4, wy - 4);
            lv_obj_set_size(h, ww + 8, wh + 8);
            lv_obj_set_style_radius(h, 22, 0);
            lv_obj_set_style_bg_opa(h, 120, 0);
            lv_obj_set_style_bg_color(h, flex_accent(), 0);
            lv_obj_set_clickable(h, false);
        }
        flex_home_wg_build(E.band, w, 0);
        lv_obj_set_clickable(lv_obj_get_child(E.band, -1), false);   // la capa recibe el tactil
        if (g_home.locked) {
            continue;
        }
        static const lv_point_precise_t X1[2] = {{7, 7}, {15, 15}}, X2[2] = {{15, 7}, {7, 15}};
        lv_obj_t *b = disc(E.band, wx + 12, wy + 12, 11, t->danger, LV_OPA_COVER);
        stroke(b, X1, t->on_acc);
        stroke(b, X2, t->on_acc);
        if (sel && flex_home_wg_can_resize(w->type)) {
            int hx, hy;
            handle_xy(k, &hx, &hy);
            lv_color_t ac = flex_accent(), on = flex_on_color_lv(ac);
            lv_obj_t *hd = disc(E.band, hx, hy, 12, ac, LV_OPA_COVER);
            // flecha diagonal doble, relativa al centro (12, 12)
            static const lv_point_precise_t A[5][2] = {{{7, 17}, {17, 7}}, {{17, 7}, {13, 7}}, {{17, 7}, {17, 11}},
                                                       {{7, 17}, {11, 17}}, {{7, 17}, {7, 13}}};
            for (int a = 0; a < 5; a++) {
                stroke(hd, A[a], on);
            }
        }
    }
    // Iconos al 89 %, sin nombre; los huecos no tiemblan
    int S, gx0, gy0, cs, rs, cols, rows;
    flex_home_grid(&S, &gx0, &gy0, &cs, &rs, &cols, &rows);
    int s = S * 8 / 9;
    for (int i = 0; i < flex_home_slot_count(); i++) {
        uint8_t v = *slot(i);
        if (i == E.drag || v == FLEX_HOME_EMPTY || flex_home_is_pkg(v) || v >= FLEX_APP_N) {
            continue;   // las descargadas se pintan cuando exista Flex Store
        }
        E.icon[i] = flex_app_icon_create(E.band, v, s, FLEX_BD_HOME);
        lv_obj_set_clickable(E.icon[i], false);
    }
    // El arrastrado, a tamano completo sobre un panel, por encima de todo
    if (E.drag >= 0 && *slot(E.drag) < FLEX_APP_N) {
        E.drag_panel = flex_box(E.band);
        lv_obj_set_size(E.drag_panel, S + 12, S + 12);
        lv_obj_set_style_radius(E.drag_panel, 16, 0);
        flex_surface(E.drag_panel, FLEX_SURF_ELEVATED, FLEX_BD_HOME);   // TH_GLASS2 / TH_SEL a150
        flex_surface_set_flat(E.drag_panel, t->sel, 150);
        lv_obj_set_clickable(E.drag_panel, false);
        E.drag_icon = flex_app_icon_create(E.drag_panel, *slot(E.drag), S, FLEX_BD_HOME);
        lv_obj_set_pos(E.drag_icon, 6, 6);
        lv_obj_set_clickable(E.drag_icon, false);
    }
    place_icons();
    hint_refresh();
}

static void tick_cb(lv_timer_t *tm)
{
    (void)tm;
    if (E.root) {
        place_icons();
        hint_refresh();
    }
}

// ---- salir ---------------------------------------------------------------------------------
static void finish(bool save)
{
    if (!E.root) {
        return;
    }
    if (E.tick) {
        lv_timer_delete(E.tick);
        E.tick = NULL;
    }
    lv_obj_delete(E.root);
    E.root = E.band = E.hint = NULL;
    E.drag = E.hover = E.wdrag = E.wsel = E.wresize = -1;
    if (save) {
        flex_home_normalize();
        flex_home_save();
    }
    flex_home_pages_show(true);
}

static void exit_edit(void)
{
    finish(true);
    flex_shell_overlay_end();   // Inicio
    flex_home_rebuild();        // la pagina en la que se quedo, sin temblor
}

void flex_home_edit_close_now(void)
{
    if (E.root) {
        finish(true);   // bloquear o suspender: se guarda lo hecho (edExit)
        flex_home_rebuild();
    }
}

static void exit_async(void *arg)
{
    (void)arg;
    if (E.root) {
        exit_edit();
    }
}

static const flex_overlay_ops_t s_ops = {exit_edit, exit_edit, exit_edit};

// ---- edTick --------------------------------------------------------------------------------
static void on_pressed(int x, int y)
{
    int p = page();
    E.used = false;
    // 1) el asa del widget seleccionado va primero
    if (E.wsel >= 0 && !g_home.locked && hit_resize(E.wsel, x, y)) {
        E.wresize = E.wsel;
        E.wdrag = E.drag = -1;
        build();
        return;
    }
    // 2) widgets: su insignia de quitar y luego el agarre para moverlo
    int wi = flex_home_wg_at(p, x, y);
    if (wi >= 0) {
        int wx, wy, ww, wh;
        flex_home_wg_rect(&g_home.wg[p][wi], &wx, &wy, &ww, &wh);
        if (!g_home.locked && LV_ABS(x - (wx + 12)) <= 14 && LV_ABS(y - (wy + 12)) <= 14) {
            flex_home_wg_remove(p, wi);
            E.wsel = -1;
            E.used = true;   // (Arduino salia del Modo edicion al soltar este toque)
            flex_home_normalize();
            flex_home_save();
            build();
            return;
        }
        E.wsel = wi;
        E.used = true;   // con el diseno bloqueado, soltar este toque no la deselecciona
        if (!g_home.locked) {
            E.wdrag = wi;
            int c, r;   // se agarra por la celda tocada: esa celda sigue al dedo
            if (flex_home_layout_cell_at(x, y, &c, &r)) {
                E.grab_c = c - g_home.wg[p][wi].col;
                E.grab_r = r - g_home.wg[p][wi].row;
            } else {
                E.grab_c = E.grab_r = 0;
            }
        }
        E.drag = -1;
        build();
        return;
    }
    // 3) un icono se agarra (suelta la seleccion de widget); un hueco no
    E.drag = flex_home_slot_at(x, y);
    if (E.drag >= 0 && *slot(E.drag) == FLEX_HOME_EMPTY) {
        E.drag = -1;
    }
    if (g_home.locked) {
        E.drag = -1;
    }
    if (E.drag >= 0) {
        E.wsel = -1;
    }
    set_drag(x, y);
    E.hover = -1;
    build();
}

static void resize_to(int x, int y)
{
    flex_home_wg_t *w = &g_home.wg[page()][E.wresize];
    int c, r;
    if (!flex_home_layout_cell_at(x, y, &c, &r)) {
        return;
    }
    int mn_w, mx_w, mn_h, mx_h;
    flex_home_wg_limits(w->type, &mn_w, &mx_w, &mn_h, &mx_h);
    int nw = c - w->col + 1, nh = r - w->row + 1;
    nw = nw < mn_w ? mn_w : nw > mx_w ? mx_w : nw;
    nh = nh < mn_h ? mn_h : nh > mx_h ? mx_h : nh;
    if (nw == w->w && nh == w->h) {
        return;
    }
    if (flex_home_wg_place_ok(page(), w->type, w->col, w->row, nw, nh, E.wresize)) {
        w->w = (uint8_t)nw;
        w->h = (uint8_t)nh;
        build();
    } else {
        notice("Ese tama\xC3\xB1o no cabe ah\xC3\xAD");
    }
}

static void wdrag_to(int x, int y)
{
    int p = page();
    flex_home_wg_t *w = &g_home.wg[p][E.wdrag];
    int c, r;
    if (flex_home_layout_cell_at(x, y, &c, &r)) {
        c -= E.grab_c;
        r -= E.grab_r;
        c = c < 0 ? 0 : c;
        r = r < 0 ? 0 : r;
        if ((c != w->col || r != w->row) && flex_home_wg_place_ok(p, w->type, c, r, w->w, w->h, E.wdrag)) {
            w->col = (uint8_t)c;
            w->row = (uint8_t)r;
            build();
        }
    }
    // contra un borde: a la pagina vecina (homeWgToPage); si no cabe se dice por que
    int dir = flex_home_ed_edge(&E.edge, x, p, g_home.page_n, lv_tick_get());
    if (!dir) {
        return;
    }
    int dst = p + dir;
    int ni = flex_home_wg_to_page(p, E.wdrag, dst);
    char m[56];
    if (ni >= 0) {
        g_home.page = dst;
        E.wdrag = E.wsel = ni;
        E.edge.dir = 0;
        int cc, rr;
        if (flex_home_layout_cell_at(x, y, &cc, &rr)) {
            E.grab_c = cc - g_home.wg[dst][ni].col;
            E.grab_r = rr - g_home.wg[dst][ni].row;
        } else {
            E.grab_c = E.grab_r = 0;
        }
        flex_home_normalize();
        flex_home_save();
        reset_springs();
        snprintf(m, sizeof(m), "Widget movido a la p\xC3\xA1gina %d", dst + 1);
        notice(m);
        build();
    } else {
        if (g_home.wg_n[dst] >= FLEX_HOME_WG_MAX) {
            snprintf(m, sizeof(m), "La p\xC3\xA1gina %d ya tiene %d widgets", dst + 1, FLEX_HOME_WG_MAX);
        } else {
            snprintf(m, sizeof(m), "Sin espacio en la p\xC3\xA1gina %d", dst + 1);
        }
        notice(m);
        E.edge.ms = lv_tick_get();   // no reintentar en cada vuelta
    }
}

static void drag_to(int x, int y)
{
    set_drag(x, y);
    int p = page();
    // sostenido en el borde: a la primera celda libre de la vecina, si la hay
    int dir = flex_home_ed_edge(&E.edge, x, p, g_home.page_n, lv_tick_get());
    if (dir) {
        int dst = flex_home_first_free_cell(p + dir);
        if (dst >= 0) {
            uint8_t v = *slot(E.drag);
            *slot(E.drag) = FLEX_HOME_EMPTY;
            g_home.page += dir;
            *slot(dst) = v;
            E.drag = dst;
            E.hover = -1;
            E.edge.dir = 0;
            flex_home_save();
            reset_springs();
            build();
        } else {
            char m[56];
            snprintf(m, sizeof(m), "Sin espacio en la p\xC3\xA1gina %d", p + dir + 1);
            notice(m);
            E.edge.ms = lv_tick_get();
        }
    }
    int S, gx0, gy0, cs, rs, cols, rows;
    flex_home_grid(&S, &gx0, &gy0, &cs, &rs, &cols, &rows);
    int over = flex_home_slot_at((int)E.dx + S / 2, (int)E.dy + S / 2);   // celda bajo el centro
    if (over >= 0 && over != E.drag) {
        if (over != E.hover) {
            E.hover = over;
            E.hover_ms = lv_tick_get();
        } else if (lv_tick_get() - E.hover_ms > ED_DWELL_MS) {
            flex_home_ed_move(page(), E.drag, over);
            E.drag = over;
            E.hover = -1;
            build();
        }
    } else {
        E.hover = -1;
    }
    place_icons();
}

static void on_released(bool tap)
{
    E.edge.dir = 0;
    if (E.wresize >= 0 || E.wdrag >= 0) {
        E.wresize = E.wdrag = -1;
        flex_home_normalize();
        flex_home_save();
        build();
        return;
    }
    if (E.drag >= 0) {
        // normalizar tambien al soltar un icono: desplazar puede meter uno bajo un widget
        E.drag = -1;
        flex_home_normalize();
        flex_home_save();
        build();
    } else if (tap && !E.used) {
        if (E.wsel >= 0) {
            E.wsel = -1;   // primero suelta la seleccion
            build();
        } else {
            lv_async_call(exit_async, NULL);   // borra la capa: fuera del evento
        }
    }
}

static void touch_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t pt;
    lv_indev_get_point(lv_indev_active(), &pt);
    if (code == LV_EVENT_PRESSED) {
        on_pressed(pt.x, pt.y);
    } else if (code == LV_EVENT_PRESSING) {
        if (E.wresize >= 0) {
            resize_to(pt.x, pt.y);
        } else if (E.wdrag >= 0) {
            wdrag_to(pt.x, pt.y);
        } else if (E.drag >= 0) {
            drag_to(pt.x, pt.y);
        }
    } else if (code == LV_EVENT_RELEASED) {
        on_released(flex_touch_arb()->t.tap);
    }
}

// ---- entrar --------------------------------------------------------------------------------
void flex_home_edit_enter(void)
{
    if (E.root || flex_shell_state() != FLEX_SH_HOME) {
        return;
    }
    E.drag = E.hover = E.wdrag = E.wsel = E.wresize = -1;
    E.edge = (flex_home_edge_t){0, 0};
    E.msg[0] = 0;
    reset_springs();
    flex_home_pages_show(false);   // la pagina la pinta la capa de edicion
    E.root = flex_box(flex_home_root_obj());
    lv_obj_set_size(E.root, 480, 800);
    lv_obj_set_clickable(E.root, true);
    lv_obj_set_gesture_bubble(E.root, false);
    lv_obj_set_scrollable(E.root, false);
    lv_obj_add_event_cb(E.root, touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(E.root, touch_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(E.root, touch_cb, LV_EVENT_RELEASED, NULL);
    E.band = flex_box(E.root);
    lv_obj_set_size(E.band, 480, 800);
    lv_obj_set_clickable(E.band, false);
    E.hint = flex_label(E.root, "", FLEX_FONT_S1, FLEX_ONWALL2);
    E.tick = lv_timer_create(tick_cb, ED_TICK_MS, NULL);
    flex_shell_overlay_begin(&s_ops);
    build();
}

// Pruebas
int flex_home_edit_drag(void)
{
    return E.drag;
}

int flex_home_edit_wsel(void)
{
    return E.wsel;
}

const char *flex_home_edit_hint(void)
{
    return E.hint ? lv_label_get_text(E.hint) : "";
}
