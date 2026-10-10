#include "flex_frame.h"

#include <stdlib.h>
#include <string.h>
#include "flex_app.h"
#include "flex_clock.h"
#include "flex_i18n.h"
#include "flex_icons.h"
#include "flex_theme.h"

// ---- etiquetas con la y de la version Arduino (tope de mayusculas) ----------
static int32_t cap_offset(const lv_font_t *f)
{
    lv_font_glyph_dsc_t g;
    if (!f || !lv_font_get_glyph_dsc(f, &g, 'H', 0)) {
        return 0;
    }
    // desde el tope de la caja de linea hasta el tope de la 'H'
    return (f->line_height - f->base_line) - (g.ofs_y + g.box_h);
}

void flex_label_cap_y(lv_obj_t *label, int32_t y)
{
    const lv_font_t *f = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    lv_obj_set_y(label, y - cap_offset(f));
}

void flex_label_one_line(lv_obj_t *label, int32_t w)
{
    const lv_font_t *f = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(label, w, lv_font_get_line_height(f));
}

void flex_label_cap_center(lv_obj_t *label, int32_t cx, int32_t y)
{
    lv_obj_set_width(label, 480);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_x(label, cx - 240);
    flex_label_cap_y(label, y);
}

// ---- barra de estado ----------------------------------------------------------
typedef struct {
    flex_sb_kind_t kind;
    lv_obj_t *obj, *time, *date, *wifi, *batt;
} sb_t;

#define SB_MAX 24
static sb_t *s_sb[SB_MAX];

static void sb_fill(sb_t *sb)
{
    const flex_palette_t *t = flex_th();
    lv_color_t fg = sb->kind == FLEX_SB_APP ? t->nav : FLEX_ONWALL;
    if (sb->kind == FLEX_SB_APP) {
        lv_obj_set_style_bg_color(sb->obj, t->win, 0);
    }
    if (sb->time) {
        char s[16];
        flex_clock_str_bar(s, sizeof(s));
        lv_label_set_text(sb->time, s);
        lv_obj_set_style_text_color(sb->time, fg, 0);
    }
    if (sb->date) {
        struct tm tm;
        char d[48];
        flex_clock_now(&tm);
        flex_date_short(d, sizeof(d), tm.tm_wday, tm.tm_mday, tm.tm_mon + 1);
        lv_label_set_text(sb->date, d);
    }
    // Wi-Fi: hasta que exista el servicio de red (Fase 6) se muestra sin conexion.
    // Bateria: la placa no mide la bateria (spec 02 §6.2): contorno sin nivel,
    // nunca el 82 % fijo de la version Arduino.
    lv_obj_set_style_text_color(sb->wifi, fg, 0);
    lv_obj_set_style_text_color(sb->batt, fg, 0);
}

static void sb_delete_cb(lv_event_t *e)
{
    sb_t *sb = lv_event_get_user_data(e);
    for (int i = 0; i < SB_MAX; i++) {
        if (s_sb[i] == sb) {
            s_sb[i] = NULL;
        }
    }
    free(sb);
}

lv_obj_t *flex_statusbar_create(lv_obj_t *parent, flex_sb_kind_t kind)
{
    sb_t *sb = calloc(1, sizeof(*sb));
    lv_obj_t *o = flex_box(parent);
    lv_obj_set_size(o, 480, 46);
    lv_obj_set_pos(o, 0, 0);
    lv_obj_set_clickable(o, false);
    if (!sb) {
        return o;
    }
    sb->kind = kind;
    sb->obj = o;
    if (kind == FLEX_SB_APP) {
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    }
    if (kind != FLEX_SB_LOCK) {
        sb->time = flex_label(o, "", FLEX_FONT_S2, FLEX_ONWALL);
        lv_obj_set_x(sb->time, 20);
        flex_label_cap_y(sb->time, 16);
    }
    if (kind == FLEX_SB_WALL) {
        sb->date = flex_label(o, "", FLEX_FONT_S1, FLEX_ONWALL2);
        lv_obj_set_x(sb->date, 20);
        flex_label_cap_y(sb->date, 40);
    }
    // drawWifi(414, 28, 11) / bloqueo (414, 40, 12); drawBattery(434, 20|31, 30x15)
    bool lock = kind == FLEX_SB_LOCK;
    sb->wifi = flex_glyph_create(o, FLEX_GLYPH_WIFI, lock ? 26 : 24, lock ? 26 : 24);
    lv_obj_set_pos(sb->wifi, lock ? 401 : 402, lock ? 22 : 11);
    flex_glyph_set(sb->wifi, FLEX_GLYPH_WIFI, -1);
    sb->batt = flex_glyph_create(o, FLEX_GLYPH_BATTERY, 33, 30);
    lv_obj_set_pos(sb->batt, 434, lock ? 24 : 13);
    flex_glyph_set(sb->batt, FLEX_GLYPH_BATTERY, -1);
    lv_obj_add_event_cb(o, sb_delete_cb, LV_EVENT_DELETE, sb);
    for (int i = 0; i < SB_MAX; i++) {
        if (!s_sb[i]) {
            s_sb[i] = sb;
            break;
        }
    }
    sb_fill(sb);
    return o;
}

void flex_statusbar_refresh_all(void)
{
    for (int i = 0; i < SB_MAX; i++) {
        if (s_sb[i]) {
            sb_fill(s_sb[i]);
        }
    }
}

// ---- barra de navegacion ----------------------------------------------------------
#define NAV_PRESS_MS 130
#define GB_STRIP_H   44
#define GB_HOME_DY   30
#define GB_CLAIM_DY  12
#define GB_RECENTS_MS 300

static lv_obj_t *s_nav, *s_btn[3], *s_glyph[3], *s_flash, *s_pill, *s_strip;
static flex_nav_ctx_t s_ctx = FLEX_NAV_HIDDEN;
static lv_timer_t *s_flash_tmr;
static struct {
    int32_t y0;
    uint32_t t0;
    bool claimed, done;
} s_gb;

static void flash_off(lv_timer_t *t)
{
    (void)t;
    lv_obj_set_hidden(s_flash, true);
    s_flash_tmr = NULL;
}

bool flex_navbar_flash_visible(void)
{
    return s_flash && !lv_obj_is_hidden(s_flash);
}

static void nav_btn_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    intptr_t which = (intptr_t)lv_event_get_user_data(e);
    if (code == LV_EVENT_PRESSED) {
        // destello r=24 a46 del color de los glifos en el centro del tercio
        lv_obj_set_pos(s_flash, 80 + (int32_t)which * 160 - 24, 756 - 736 - 24);
        lv_obj_set_hidden(s_flash, false);
        if (s_flash_tmr) {
            lv_timer_delete(s_flash_tmr);
            s_flash_tmr = NULL;
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (!s_flash_tmr) {
            s_flash_tmr = lv_timer_create(flash_off, NAV_PRESS_MS, NULL);
            lv_timer_set_repeat_count(s_flash_tmr, 1);
        }
    } else if (code == LV_EVENT_CLICKED) {
        if (which == 0) {
            flex_sys_back();
        } else if (which == 1) {
            flex_sys_home();
        } else {
            flex_sys_recents();
        }
    }
}

// Barra de gestos (Core.h:1063): deslizar desde la franja inferior.
static void strip_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *in = lv_indev_active();
    lv_point_t p = {0, 0};
    if (in) {
        lv_indev_get_point(in, &p);
    }
    uint32_t now = lv_tick_get();
    if (code == LV_EVENT_PRESSED) {
        s_gb.y0 = p.y;
        s_gb.t0 = now;
        s_gb.claimed = false;
        s_gb.done = false;
    } else if (code == LV_EVENT_PRESSING && !s_gb.done) {
        int32_t dy = s_gb.y0 - p.y;
        if (dy > GB_CLAIM_DY) {
            s_gb.claimed = true;
        }
        uint32_t dt = now - s_gb.t0;
        // golpe rapido hacia arriba: Inicio con el dedo todavia apoyado
        if (dy > GB_HOME_DY && dt > 0 && dt < GB_RECENTS_MS && (float)dy / (float)dt >= 0.35f) {
            s_gb.done = true;
            flex_sys_home();
        }
    } else if (code == LV_EVENT_RELEASED && !s_gb.done) {
        int32_t dy = s_gb.y0 - p.y;
        if (dy > GB_HOME_DY) {
            if (now - s_gb.t0 >= GB_RECENTS_MS) {
                flex_sys_recents();
            } else {
                flex_sys_home();
            }
        }
    }
}

static void nav_apply(void)
{
    if (!s_nav) {
        return;
    }
    const flex_palette_t *t = flex_th();
    bool gestures = flex_look()->nav_mode == 1;
    bool visible = s_ctx != FLEX_NAV_HIDDEN;
    lv_obj_set_hidden(s_nav, !visible || gestures);
    lv_obj_set_hidden(s_pill, !visible || !gestures);
    lv_obj_set_hidden(s_strip, !visible || !gestures);
    if (!visible) {
        return;
    }
    bool app = s_ctx == FLEX_NAV_APP;
    lv_color_t fg = app ? t->navbar_fg : FLEX_ONWALL;
    lv_obj_set_style_bg_opa(s_nav, app ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(s_nav, t->navbar_bg, 0);
    lv_obj_set_style_border_side(s_nav, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(s_nav, app ? 1 : 0, 0);
    lv_obj_set_style_border_color(s_nav, t->navbar_line, 0);
    for (int i = 0; i < 3; i++) {
        lv_obj_set_style_text_color(s_glyph[i], fg, 0);
    }
    lv_obj_set_style_bg_color(s_flash, fg, 0);
    // Indicador de inicio: blanco sobre el fondo; TH_NAV en el marco de app (en
    // claro el blanco de Arduino era casi invisible, spec 02 §7.3)
    lv_obj_set_style_bg_color(s_pill, app ? t->nav : FLEX_ONWALL, 0);
    lv_obj_set_style_bg_opa(s_pill, app ? 180 : 220, 0);
}

static void nav_theme_cb(void *ctx)
{
    (void)ctx;
    nav_apply();
}

void flex_navbar_init(void)
{
    lv_obj_t *top = lv_layer_top();
    s_nav = flex_box(top);
    lv_obj_set_size(s_nav, 480, FLEX_NAV_H);
    lv_obj_set_pos(s_nav, 0, 800 - FLEX_NAV_H);
    s_flash = flex_box(s_nav);
    lv_obj_set_size(s_flash, 48, 48);
    lv_obj_set_style_radius(s_flash, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_flash, 46, 0);
    lv_obj_set_hidden(s_flash, true);
    lv_obj_set_clickable(s_flash, false);
    static const flex_glyph_t g[3] = {FLEX_GLYPH_BACK, FLEX_GLYPH_HOME, FLEX_GLYPH_RECENTS};
    for (int i = 0; i < 3; i++) {
        // tercios: x < 160 atras, < 320 inicio, resto recientes
        s_btn[i] = flex_box(s_nav);
        lv_obj_set_size(s_btn[i], 160, FLEX_NAV_H);
        lv_obj_set_pos(s_btn[i], i * 160, 0);
        lv_obj_set_clickable(s_btn[i], true);
        lv_obj_add_event_cb(s_btn[i], nav_btn_cb, LV_EVENT_ALL, (void *)(intptr_t)i);
        s_glyph[i] = flex_glyph_create(s_btn[i], g[i], 40, 40);
        lv_obj_set_pos(s_glyph[i], 60, 756 - 736 - 20);
    }
    s_pill = flex_box(top);
    lv_obj_set_size(s_pill, 130, 5);
    lv_obj_set_pos(s_pill, 175, 775);
    lv_obj_set_style_radius(s_pill, 2, 0);
    lv_obj_set_clickable(s_pill, false);
    s_strip = flex_box(top);
    lv_obj_set_size(s_strip, 480, GB_STRIP_H);
    lv_obj_set_pos(s_strip, 0, 800 - GB_STRIP_H);
    lv_obj_set_clickable(s_strip, true);
    lv_obj_add_event_cb(s_strip, strip_cb, LV_EVENT_ALL, NULL);
    flex_theme_listen(nav_theme_cb, NULL);
    nav_apply();
}

void flex_navbar_set_ctx(flex_nav_ctx_t ctx)
{
    s_ctx = ctx;
    nav_apply();
}

flex_nav_ctx_t flex_navbar_ctx(void)
{
    return s_ctx;
}
