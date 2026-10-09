// Flex OS Ultra · pantalla de bloqueo (docs/spec/01a §4, Home.h:502-545 y 1415-1500).
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "flex_app.h"
#include "flex_auth.h"
#include "flex_clock.h"
#include "flex_frame.h"
#include "flex_glass.h"
#include "flex_i18n.h"
#include "flex_icons.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"

#define LW_CLOCK   0x01
#define LW_WEATHER 0x02
#define LW_CAL     0x04
#define LW_NOTIF   0x08

#define UNLOCK_PX     266   // SCR_H / 3
#define VERIFY_PX     60
#define SWIPE_PX      55
#define ANIM_MS       200

static lv_obj_t *s_root, *s_wall, *s_glass, *s_clock, *s_date, *s_cards;
static int32_t s_y0, s_off;
static bool s_verify_started;

static int32_t ease_out_quad(const lv_anim_t *a)
{
    float p = a->duration ? (float)(a->act_time < 0 ? 0 : a->act_time) / (float)a->duration : 1.0f;
    p = p > 1.0f ? 1.0f : p;
    float e = 1.0f - (1.0f - p) * (1.0f - p);
    return a->start_value + (int32_t)((float)(a->end_value - a->start_value) * e);
}

static void set_off(void *obj, int32_t off)
{
    (void)obj;
    s_off = off;
    lv_obj_set_y(s_root, -off);
}

static void unlocked_done(lv_anim_t *a)
{
    (void)a;
    flex_shell_unlocked();
}

static void animate_to(int32_t target, bool unlock)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_root);
    lv_anim_set_exec_cb(&a, set_off);
    lv_anim_set_values(&a, s_off, target);
    lv_anim_set_duration(&a, ANIM_MS);
    lv_anim_set_path_cb(&a, ease_out_quad);
    if (unlock) {
        lv_anim_set_completed_cb(&a, unlocked_done);
    }
    lv_anim_start(&a);
}

static int s_ret_app = -1;   // gSuspRetApp: app a la que volver tras despertar y acertar

static void verify_ok(void *ctx)
{
    (void)ctx;
    flex_shell_unlocked();
}

static void verify_ok_app(void *ctx)
{
    // LSU_AFTER_OPENAPP: escritorio y la app, con su animacion de apertura
    flex_shell_unlocked();
    flex_app_open((int)(intptr_t)ctx, NULL);
}

void flex_lock_set_return_app(int app)
{
    s_ret_app = app;
}

static void verify_cancel(void *ctx)
{
    (void)ctx;
    flex_shell_lock();   // cancelar SIEMPRE vuelve al bloqueo, nunca al escritorio
}

static void verify_open(void)
{
    // Con clave el escritorio nunca se revela sin verificar. Si el bloqueo lo
    // puso el despertar con una app abierta, al acertar se vuelve a ella (sin
    // revelado); el destino se consume: cancelar y reintentar va al escritorio.
    int app = s_ret_app;
    s_ret_app = -1;
    flex_auth_req_t req = {.from_lock = true, .reveal = true, .on_ok = verify_ok, .on_cancel = verify_cancel};
    if (app >= 0) {
        req.reveal = false;
        req.on_ok = verify_ok_app;
        req.ctx = (void *)(intptr_t)app;
    }
    flex_auth_verify(&req);
}

static void touch_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    bool need_pin = flex_auth_required();
    if (code == LV_EVENT_PRESSED) {
        lv_anim_delete(s_root, set_off);
        if (need_pin && s_off != 0) {
            // Con clave el bloqueo no se queda a medias por un toque durante su
            // caida (en Arduino la caida no lee el tactil): termina en el acto.
            set_off(NULL, 0);
        }
        s_y0 = p.y + s_off;
        s_verify_started = false;
    } else if (code == LV_EVENT_PRESSING) {
        int32_t off = s_y0 - p.y;
        off = off < 0 ? 0 : off > 800 ? 800 : off;
        if (need_pin) {
            // Con clave el escritorio nunca se revela: pasado 60 px se pide la clave.
            if (off > VERIFY_PX && !s_verify_started) {
                s_verify_started = true;
                verify_open();
            }
            return;
        }
        set_off(NULL, off);   // el bloqueo sube como un telon y deja ver el escritorio
    } else if (code == LV_EVENT_RELEASED) {
        int32_t dy = s_y0 - p.y;
        if (need_pin) {
            if (dy > SWIPE_PX && !s_verify_started) {
                s_verify_started = true;
                verify_open();
            }
            return;
        }
        if (s_off > UNLOCK_PX || dy > SWIPE_PX) {
            animate_to(800, true);
        } else {
            animate_to(0, false);
        }
    }
}

// ---- tarjetas de widgets ---------------------------------------------------------
static void card(lv_obj_t *parent, int32_t y, flex_glyph_t glyph, lv_color_t acc, const char *title, const char *val)
{
    // lockWidgetCard (Home.h:490): 28, y, 424x50 r16; Vidrio GLASS2 / Plano SURF a215
    const flex_palette_t *t = flex_th();
    lv_obj_t *c = flex_box(parent);
    lv_obj_set_pos(c, 28, y);
    lv_obj_set_size(c, 424, 50);
    lv_obj_set_style_radius(c, 16, 0);
    flex_surface(c, FLEX_SURF_ELEVATED, FLEX_BD_LOCK);
    flex_surface_set_flat(c, t->surf, 215);
    lv_obj_t *g = flex_glyph_create(c, glyph, 18, 18);
    lv_obj_set_pos(g, 58 - 28 - 9, 25 - 9);
    lv_obj_set_style_text_color(g, acc, 0);
    lv_obj_t *l1 = flex_label(c, title, FLEX_FONT_S2, t->txt);
    lv_obj_set_x(l1, 82 - 28);
    flex_label_cap_y(l1, 9);
    lv_obj_t *l2 = flex_label(c, val, FLEX_FONT_S1, t->txt2);
    lv_obj_set_x(l2, 82 - 28);
    flex_label_cap_y(l2, 30);
}

static void build_content(void)
{
    const flex_palette_t *t = flex_th();
    uint8_t w = (uint8_t)flex_cfg_get_i32("lockwidgets", LW_CLOCK);
    if (s_cards) {
        lv_obj_delete(s_cards);
    }
    s_cards = flex_box(s_root);
    lv_obj_set_size(s_cards, 480, 800);
    lv_obj_set_clickable(s_cards, false);
    bool clock = w & LW_CLOCK;
    lv_obj_set_hidden(s_glass, !(clock && flex_look()->glass));
    lv_obj_set_hidden(s_clock, !clock);
    lv_obj_set_hidden(s_date, !clock);
    int32_t y = clock ? 462 : 200;
    if (w & LW_WEATHER) {
        // wxLockCard sin datos (AppWeather.h:1365): el servicio de clima llega con el Wi-Fi
        lv_obj_t *c = flex_box(s_cards);
        lv_obj_set_pos(c, 28, y);
        lv_obj_set_size(c, 424, 50);
        lv_obj_set_style_radius(c, 16, 0);
        flex_surface(c, FLEX_SURF_TINT, FLEX_BD_LOCK);
        flex_surface_set_tint(c, lv_color_hex(0x28325A));
        flex_surface_set_flat(c, FLEX_ONWALL, 45);
        lv_obj_t *g = flex_glyph_create(c, FLEX_GLYPH_SUN, 24, 24);
        lv_obj_set_pos(g, 58 - 28 - 12, 25 - 12);
        lv_obj_set_style_text_color(g, FLEX_ONWALL, 0);
        lv_obj_t *l1 = flex_label(c, flex_t(FLEX_S_WEATHER), FLEX_FONT_S2, FLEX_ONWALL);
        lv_obj_set_x(l1, 86 - 28);
        flex_label_cap_y(l1, 9);
        lv_obj_t *l2 = flex_label(c, "Sin datos meteorol\xC3\xB3gicos", FLEX_FONT_S1, FLEX_ONWALL2);
        lv_obj_set_x(l2, 86 - 28);
        flex_label_cap_y(l2, 30);
        y += 60;
    }
    if (w & LW_CAL) {
        card(s_cards, y, FLEX_GLYPH_MENU, lv_color_hex(0xEB6E5A), flex_app_name(IC_CALEND), flex_t(FLEX_S_NOEVENTS));
        y += 60;
    }
    if (w & LW_NOTIF) {
        card(s_cards, y, FLEX_GLYPH_BELL, lv_color_hex(0xE6B45A), flex_t(FLEX_S_NOTIFS), flex_t(FLEX_S_NONOTIFS));
        y += 60;
    }
    (void)t;
}

void flex_lock_refresh(void)
{
    if (!s_root) {
        return;
    }
    char cs[8], ds[64];
    struct tm tm;
    flex_clock_str_big(cs, sizeof(cs));
    flex_clock_now(&tm);
    flex_date_long(ds, sizeof(ds), tm.tm_wday, tm.tm_mday, tm.tm_mon + 1);
    lv_label_set_text(s_clock, cs);
    lv_label_set_text(s_date, ds);
    lv_image_set_src(s_wall, flex_wallmgr_image(FLEX_WALL_LOCK));
}

void flex_lock_drop_in(void)
{
    // autoLockNow: el bloqueo baja desde arriba (animateTo(SCR_H, 0))
    if (!s_root) {
        return;
    }
    set_off(NULL, 800);
    animate_to(0, false);
}

void flex_lock_reset(void)
{
    if (!s_root) {
        return;
    }
    lv_anim_delete(s_root, set_off);
    set_off(NULL, 0);
    build_content();
    flex_lock_refresh();
}

static void theme_cb(void *ctx)
{
    (void)ctx;
    if (flex_shell_state() == FLEX_SH_LOCK) {
        flex_lock_reset();
    }
}

lv_obj_t *flex_lock_create(lv_obj_t *parent)
{
    s_root = flex_box(parent);
    lv_obj_set_size(s_root, 480, 800);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
    lv_obj_set_clickable(s_root, true);
    lv_obj_add_event_cb(s_root, touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_root, touch_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_root, touch_cb, LV_EVENT_RELEASED, NULL);

    s_wall = lv_image_create(s_root);
    lv_obj_set_pos(s_wall, 0, 0);
    flex_statusbar_create(s_root, FLEX_SB_LOCK);

    // Panel de vidrio tras el reloj (28, 198, 424x252) r28, solo con Liquid Glass
    s_glass = flex_box(s_root);
    lv_obj_set_pos(s_glass, 28, 198);
    lv_obj_set_size(s_glass, 424, 252);
    lv_obj_set_style_radius(s_glass, 28, 0);
    flex_surface(s_glass, FLEX_SURF_ELEVATED, FLEX_BD_LOCK);
    lv_obj_set_clickable(s_glass, false);

    s_clock = flex_label(s_root, "", FLEX_FONT_CLOCK, FLEX_ONWALL);
    flex_label_cap_center(s_clock, 240, 242);
    s_date = flex_label(s_root, "", FLEX_FONT_S3, FLEX_ONWALL);
    flex_label_cap_center(s_date, 240, 242 + 140 + 36);

    // Asa (170, 650, 140x10) r5 y "Desliza arriba para desbloquear" (y = 682)
    lv_obj_t *handle = flex_box(s_root);
    lv_obj_set_pos(handle, 170, 650);
    lv_obj_set_size(handle, 140, 10);
    lv_obj_set_style_radius(handle, 5, 0);
    lv_obj_set_style_bg_opa(handle, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(handle, FLEX_ONWALL, 0);
    lv_obj_t *hint = flex_label(s_root, flex_t(FLEX_S_SWIPE), FLEX_FONT_S2, FLEX_ONWALL);
    flex_label_cap_center(hint, 240, 682);

    flex_theme_listen(theme_cb, NULL);
    flex_lock_reset();
    return s_root;
}
