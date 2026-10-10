// Flex OS Ultra · escritorio (docs/spec/01a §6-7, Home.h y Widgets.h).
//
// El fondo de pantalla queda FIJO detras; lo que se desliza es la franja de
// pagina (y 72..bandBot): un contenedor con desplazamiento horizontal, una
// pagina de 480 px por hijo, acomodo de una pagina por gesto. El vidrio de los
// widgets y de los iconos se dibuja leyendo el backdrop en su posicion ACTUAL,
// asi que al deslizar nunca arrastra el fondo de donde se compuso.
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_frame.h"
#include "flex_glass.h"
#include "flex_home_model.h"
#include "flex_home_wg.h"
#include "flex_i18n.h"
#include "flex_safeboot.h"
#include "flex_icons.h"
#include "flex_shell.h"
#include "flex_theme.h"
#include "flex_touch_lvgl.h"
#include "flex_wallmgr.h"

#define LONGPRESS_ICON_MS  1000
#define LONGPRESS_EMPTY_MS 650

static lv_obj_t *s_root, *s_wall, *s_pages, *s_dots, *s_dock, *s_page[FLEX_HOME_PAGES_MAX];
static lv_obj_t *s_dock_icon[FLEX_HOME_DOCK_N];
static lv_obj_t *s_safe_pill;
static int32_t s_band_top, s_band_bot;

// Ganchos opcionales (otros modulos del shell)
__attribute__((weak)) void flex_home_ctx_menu(int app_id, const lv_area_t *icon) { (void)app_id; (void)icon; }
__attribute__((weak)) void flex_home_customize_open(void) {}

// ---- iconos ----------------------------------------------------------------------
typedef struct {
    int app;
    uint32_t t0;
    lv_point_t p0;
    bool long_done;
} press_t;
static press_t s_press;

static void icon_cb(lv_event_t *e)
{
    if (flex_shell_state() != FLEX_SH_HOME) {
        return;   // durante la apertura de una app o con la caja subiendo, el escritorio no manda
    }
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *o = lv_event_get_current_target_obj(e);
    int app = (int)(intptr_t)lv_event_get_user_data(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (code == LV_EVENT_PRESSED) {
        s_press.app = app;
        s_press.t0 = lv_tick_get();
        s_press.p0 = p;
        s_press.long_done = false;
    } else if (code == LV_EVENT_PRESSING) {
        if (!s_press.long_done && lv_tick_elaps(s_press.t0) > LONGPRESS_ICON_MS && LV_ABS(p.x - s_press.p0.x) < 12 &&
            LV_ABS(p.y - s_press.p0.y) < 12) {
            s_press.long_done = true;
            lv_area_t a;
            lv_obj_get_coords(o, &a);
            lv_indev_wait_release(lv_indev_active());
            flex_home_ctx_menu(app, &a);
        }
    } else if (code == LV_EVENT_SHORT_CLICKED && !s_press.long_done) {
        lv_area_t a;
        lv_obj_get_coords(o, &a);
        flex_app_launch(app, &a);   // con candado pide la clave antes
    }
}

static lv_obj_t *add_icon(lv_obj_t *parent, int app, int32_t x, int32_t y, int32_t S, bool label, int32_t lbl_w,
                          const lv_font_t *lf)
{
    lv_obj_t *ic = flex_app_icon_create(parent, app, S, FLEX_BD_HOME);
    lv_obj_set_pos(ic, x, y);
    lv_obj_set_clickable(ic, true);
    lv_obj_set_ext_click_area(ic, 6);
    lv_obj_add_event_cb(ic, icon_cb, LV_EVENT_PRESSED, (void *)(intptr_t)app);
    lv_obj_add_event_cb(ic, icon_cb, LV_EVENT_PRESSING, (void *)(intptr_t)app);
    lv_obj_add_event_cb(ic, icon_cb, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)app);
    if (label) {
        lv_obj_t *l = flex_label(parent, flex_app_name(app), lf, FLEX_ONWALL);
        flex_label_one_line(l, lbl_w);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_x(l, x + S / 2 - lbl_w / 2);
        flex_label_cap_y(l, y + S + 6);
    }
    return ic;
}

static void build_page(int p)
{
    lv_obj_t *pg = s_page[p];
    lv_obj_clean(pg);
    for (int k = 0; k < g_home.wg_n[p]; k++) {
        if (g_home.wg[p][k].type != FLEX_WG_NONE) {
            flex_home_wg_build(pg, &g_home.wg[p][k], s_band_top);
        }
    }
    int S, gx0, gy0, cs, rs, cols, rows;
    flex_home_grid(&S, &gx0, &gy0, &cs, &rs, &cols, &rows);
    const lv_font_t *lf = cs >= 110 ? FLEX_FONT_S2 : FLEX_FONT_S1;
    for (int i = 0; i < flex_home_slot_count(); i++) {
        uint8_t v = g_home.order[flex_home_idx(p, i)];
        if (v == FLEX_HOME_EMPTY || flex_home_is_pkg(v) || v >= FLEX_APP_N) {
            continue;   // las apps descargadas se pintan cuando exista Flex Store (Fase 8+)
        }
        int x, y;
        flex_home_slot_xy(i, &x, &y);
        add_icon(pg, v, x, y - s_band_top, S, g_home.labels, cs - 14, lf);
    }
}

// ---- indicadores de pagina --------------------------------------------------------------
static void dots_update(float pos)
{
    lv_obj_clean(s_dots);
    if (!g_home.dots) {
        return;
    }
    int n = g_home.page_n;
    int32_t x0 = 240 - (n - 1) * 9;
    for (int i = 0; i < n; i++) {
        int32_t x = x0 + i * 18;
        if (i == g_home.main) {
            // casita: la pagina principal
            lv_obj_t *b = flex_box(s_dots);
            lv_obj_set_pos(b, x - 4, 9 - 1);
            lv_obj_set_size(b, 8, 6);
            lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(b, FLEX_ONWALL, 0);
            lv_obj_t *r = flex_box(s_dots);
            lv_obj_set_pos(r, x - 6, 9 - 7);
            lv_obj_set_size(r, 12, 6);
            lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(r, FLEX_ONWALL, 0);
            lv_obj_set_style_radius(r, 3, 0);
        } else {
            lv_obj_t *d = flex_box(s_dots);
            lv_obj_set_pos(d, x - 4, 9 - 4);
            lv_obj_set_size(d, 9, 9);
            lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(d, 110, 0);
            lv_obj_set_style_bg_color(d, FLEX_ONWALL, 0);
        }
    }
    // el punto activo se desplaza con el dedo; quieto en la principal no se pinta
    if (!(pos == (float)g_home.main)) {
        int32_t cx = (int32_t)(x0 + pos * 18.0f + 0.5f);
        lv_obj_t *a = flex_box(s_dots);
        lv_obj_set_pos(a, cx - 5, 9 - 5);
        lv_obj_set_size(a, 11, 11);
        lv_obj_set_style_radius(a, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(a, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(a, FLEX_ONWALL, 0);
    }
}

static void pages_scroll_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int32_t sx = lv_obj_get_scroll_x(s_pages);
    float pos = (float)sx / 480.0f;
    if (code == LV_EVENT_SCROLL_END) {
        int p = (int)((sx + 240) / 480);
        g_home.page = p < 0 ? 0 : p >= g_home.page_n ? g_home.page_n - 1 : p;
        pos = (float)g_home.page;
    }
    dots_update(pos);
}

// ---- gestos del escritorio --------------------------------------------------------
static void root_gesture_cb(lv_event_t *e)
{
    if (flex_shell_state() != FLEX_SH_HOME) {
        return;   // durante la apertura de una app o con la caja subiendo, el escritorio no manda
    }
    lv_indev_t *in = lv_indev_active();
    if (!in) {
        return;
    }
    lv_dir_t dir = lv_indev_get_gesture_dir(in);
    if (dir == LV_DIR_TOP) {
        // swipeUp que empezo en y > 96 -> Caja de aplicaciones (HomeCfg.h:1298);
        // desde la barra de estado no
        if (flex_touch_arb()->t.start_y <= 96) {
            return;
        }
        lv_indev_wait_release(in);
        flex_drawer_open();
    }
    (void)e;
}

static void empty_cb(lv_event_t *e)
{
    if (flex_shell_state() != FLEX_SH_HOME) {
        return;   // durante la apertura de una app o con la caja subiendo, el escritorio no manda
    }
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (code == LV_EVENT_PRESSED) {
        s_press.app = -1;
        s_press.t0 = lv_tick_get();
        s_press.p0 = p;
        s_press.long_done = false;
    } else if (code == LV_EVENT_PRESSING) {
        if (!s_press.long_done && lv_tick_elaps(s_press.t0) > LONGPRESS_EMPTY_MS && LV_ABS(p.x - s_press.p0.x) <= 12 &&
            LV_ABS(p.y - s_press.p0.y) <= 12 && lv_obj_get_scroll_x(s_pages) % 480 == 0) {
            s_press.long_done = true;
            lv_indev_wait_release(lv_indev_active());
            flex_home_customize_open();
        }
    } else if (code == LV_EVENT_SHORT_CLICKED) {
        if (p.x > 320 && p.y > 728) {
            flex_sys_recents();   // toque abajo a la derecha: Recientes (los dos modos)
        }
    }
}

// ---- construccion --------------------------------------------------------------------
static void build_dock(void)
{
    // (24, 624, 432x96) r28; Vidrio GLASS2 / Plano SURF a90; iconos 64 en x = 40, 152, 264, 376
    const flex_palette_t *t = flex_th();
    if (s_dock) {
        lv_obj_delete(s_dock);
    }
    s_dock = flex_box(s_root);
    lv_obj_set_pos(s_dock, 24, FLEX_HOME_DOCK_Y);
    lv_obj_set_size(s_dock, 432, 96);
    lv_obj_set_style_radius(s_dock, 28, 0);
    flex_surface(s_dock, FLEX_SURF_ELEVATED, FLEX_BD_HOME);
    flex_surface_set_flat(s_dock, t->surf, 90);
    int dS = 64, inner = 432 - 32, dgap = (inner - 4 * dS) / 3;
    for (int i = 0; i < FLEX_HOME_DOCK_N; i++) {
        s_dock_icon[i] = add_icon(s_dock, flex_home_dock[i], 16 + i * (dS + dgap), (96 - dS) / 2, dS, false, 0, NULL);
    }
}

static void rebuild(void)
{
    int32_t bot = flex_home_dots_y() + 18;
    s_band_bot = bot > 596 ? 596 : bot;
    s_band_top = FLEX_HOME_HDR_Y;
    lv_obj_set_pos(s_pages, 0, s_band_top);
    lv_obj_set_size(s_pages, 480, s_band_bot - s_band_top);
    for (int p = 0; p < FLEX_HOME_PAGES_MAX; p++) {
        if (s_page[p]) {
            lv_obj_delete(s_page[p]);
            s_page[p] = NULL;
        }
    }
    for (int p = 0; p < g_home.page_n; p++) {
        s_page[p] = flex_box(s_pages);
        lv_obj_set_pos(s_page[p], p * 480, 0);
        lv_obj_set_size(s_page[p], 480, s_band_bot - s_band_top);
        lv_obj_set_clickable(s_page[p], true);
        lv_obj_add_event_cb(s_page[p], empty_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(s_page[p], empty_cb, LV_EVENT_PRESSING, NULL);
        lv_obj_add_event_cb(s_page[p], empty_cb, LV_EVENT_SHORT_CLICKED, NULL);
        build_page(p);
    }
    lv_obj_set_pos(s_dots, 0, flex_home_dots_y() - 9);
    build_dock();
    lv_image_set_src(s_wall, flex_wallmgr_image(FLEX_WALL_HOME));
    lv_obj_scroll_to_x(s_pages, g_home.page * 480, LV_ANIM_OFF);
    dots_update((float)g_home.page);
}

// El minuto (y el tema) rehacen las paginas con widgets: con un dedo encima o
// las paginas aun deslizandose se cortaria el gesto y la pagina quedaria entre
// dos encajes. Se aplaza hasta que el escritorio este quieto.
static lv_timer_t *s_refresh_retry;

static void refresh_retry_cb(lv_timer_t *t)
{
    (void)t;
    s_refresh_retry = NULL;   // repeat_count 1: LVGL lo borra al volver
    flex_home_refresh();
}

static bool home_busy(void)
{
    return flex_touch_arb()->t.down || lv_anim_get(s_pages, NULL) != NULL;
}

void flex_home_refresh(void)
{
    if (!s_root) {
        return;
    }
    if (home_busy()) {
        if (!s_refresh_retry) {
            s_refresh_retry = lv_timer_create(refresh_retry_cb, 250, NULL);
            lv_timer_set_repeat_count(s_refresh_retry, 1);
        }
        return;
    }
    if (s_safe_pill) {
        lv_obj_set_hidden(s_safe_pill, !flex_safe_mode());   // el Modo seguro es de este arranque
    }
    // relojes y calendario de los widgets
    for (int p = 0; p < g_home.page_n; p++) {
        if (g_home.wg_n[p]) {
            build_page(p);
        }
    }
}

void flex_home_rebuild(void)
{
    if (s_root) {
        rebuild();   // el modelo cambio (favoritas, ocultas, orden)
    }
}

int32_t flex_home_scroll_x(void)
{
    return s_pages ? lv_obj_get_scroll_x(s_pages) : 0;
}

bool flex_home_icon_area(int app_id, lv_area_t *out)
{
    if (!s_root) {
        return false;
    }
    for (int i = 0; i < FLEX_HOME_DOCK_N; i++) {
        if (flex_home_dock[i] == app_id && s_dock_icon[i]) {
            lv_obj_get_coords(s_dock_icon[i], out);
            return true;
        }
    }
    int p = g_home.page;
    int S, gx0, gy0, cs, rs, cols, rows;
    flex_home_grid(&S, &gx0, &gy0, &cs, &rs, &cols, &rows);
    for (int i = 0; i < flex_home_slot_count(); i++) {
        if (g_home.order[flex_home_idx(p, i)] == app_id) {
            int x, y;
            flex_home_slot_xy(i, &x, &y);
            *out = (lv_area_t){x, y, x + S - 1, y + S - 1};
            return true;
        }
    }
    return false;
}

static void theme_cb(void *ctx)
{
    (void)ctx;
    rebuild();
}

// Pildora "Modo seguro" (HomeCfg.h:1240): vuelve a la pantalla del Modo seguro.
static void safe_pill_cb(lv_event_t *e)
{
    (void)e;
    flex_safe_open();
}

static void safe_pill_create(void)
{
    lv_obj_t *p = flex_box(s_root);
    s_safe_pill = p;
    lv_obj_set_pos(p, 146, 56);
    lv_obj_set_size(p, 188, 38);
    lv_obj_set_style_radius(p, 19, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(0xBA7030), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_clickable(p, true);
    lv_obj_set_ext_click_area(p, 10);   // (136..344, 48..104) como Arduino
    lv_obj_add_event_cb(p, safe_pill_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = flex_label(p, "Modo seguro", FLEX_FONT_S2, lv_color_white());
    flex_label_cap_center(l, 94, 66 - 56);
}

lv_obj_t *flex_home_create(lv_obj_t *parent)
{
    s_root = flex_box(parent);
    lv_obj_set_size(s_root, 480, 800);
    lv_obj_set_clickable(s_root, true);
    // En LVGL 9.6 el gesto sube por todos los padres con gesture_bubble (el
    // valor por defecto) hasta la pantalla: se corta aqui para recibirlo.
    lv_obj_set_gesture_bubble(s_root, false);
    lv_obj_add_event_cb(s_root, root_gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(s_root, empty_cb, LV_EVENT_SHORT_CLICKED, NULL);
    s_wall = lv_image_create(s_root);
    lv_obj_set_pos(s_wall, 0, 0);
    flex_statusbar_create(s_root, FLEX_SB_WALL);
    s_pages = flex_box(s_root);
    lv_obj_set_scrollable(s_pages, true);
    lv_obj_set_scroll_dir(s_pages, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(s_pages, LV_SCROLL_SNAP_START);
    lv_obj_set_scroll_one(s_pages, true);
    lv_obj_set_scroll_elastic(s_pages, false);
    lv_obj_set_scroll_momentum(s_pages, false);
    lv_obj_set_scrollbar_mode(s_pages, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_pages, pages_scroll_cb, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(s_pages, pages_scroll_cb, LV_EVENT_SCROLL_END, NULL);
    s_dots = flex_box(s_root);
    lv_obj_set_size(s_dots, 480, 18);
    lv_obj_set_clickable(s_dots, false);
    flex_theme_listen(theme_cb, NULL);
    rebuild();
    safe_pill_create();
    lv_obj_set_hidden(s_safe_pill, !flex_safe_mode());
    return s_root;
}
