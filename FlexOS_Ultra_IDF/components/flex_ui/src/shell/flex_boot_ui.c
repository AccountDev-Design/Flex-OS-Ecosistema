// Flex OS Ultra · arranque: banda forense de reinicio anormal y splash (docs/spec/01a §2.3-2.5).
//
// Negro absoluto en las dos apariencias (el backlight sube desde 0: un fondo claro daria un fogonazo).
// Banda forense (solo tras un reinicio anormal): "FlexOS Ultra", "ultimo reinicio: %s" y el modo offline,
// 2200 ms. Splash: titulo y "ESP32-P4" con la curva de opacidad de Arduino (600 ms sube, 1400 fijo,
// 600 baja) y tres puntos de carga que cambian cada 320 ms. Nada bloquea la tarea de UI (en Arduino
// eran delay()): todo va por lv_timer. Al terminar, el destino lo decide el shell (§2.5).
#include <stdio.h>
#include <string.h>
#ifndef FLEX_SIM
#include "esp_system.h"
#endif
#include "flex_boot_model.h"
#include "flex_frame.h"
#include "flex_shell.h"
#include "flex_system.h"
#include "flex_theme.h"
#include "flex_touch_lvgl.h"

#define TITLE_Y   360
#define SUB_Y     422
#define DOTS_Y    672

static struct {
    lv_obj_t *root, *title, *sub, *dots[3], *l1, *l2, *l3;
    lv_timer_t *timer;
    uint32_t t0;
    void (*done)(void);
    bool splash, enabled;
    int reason;
} B;

void flex_shell_boot_destination(void);   // flex_shell.c (§2.5)
bool flex_safe_mode(void);

#ifdef FLEX_SIM
static int s_sim_reason = 1;   // POWERON: sin banda
void flex_boot_sim_set(bool enabled, int reason)
{
    B.enabled = enabled;
    s_sim_reason = reason;
}
static int reset_reason(void) { return s_sim_reason; }
#else
static int reset_reason(void) { return (int)esp_reset_reason(); }
#endif

static void end(void)
{
    if (B.timer) {
        lv_timer_delete(B.timer);
        B.timer = NULL;
    }
    if (B.root) {
        lv_obj_delete(B.root);
    }
    B.root = NULL;
    void (*cb)(void) = B.done;
    B.done = NULL;
    flex_touch_drop_all();
    if (cb) {
        cb();
    }
}

static lv_obj_t *text(lv_obj_t *p, const char *s, const lv_font_t *f, lv_color_t c, int y)
{
    lv_obj_t *l = flex_label(p, s, f, c);
    flex_label_cap_center(l, 240, y);
    return l;
}

static void screen_new(void)
{
    B.root = flex_box(flex_shell_screen());
    lv_obj_set_size(B.root, 480, 800);
    lv_obj_set_style_bg_color(B.root, lv_color_black(), 0);   // negro sobre negro a proposito
    lv_obj_set_style_bg_opa(B.root, LV_OPA_COVER, 0);
    lv_obj_set_clickable(B.root, true);                        // se traga los toques
    lv_obj_move_foreground(B.root);
    flex_navbar_set_ctx(FLEX_NAV_HIDDEN);
}

static void splash_tick(lv_timer_t *t)
{
    (void)t;
    uint32_t e = lv_tick_elaps(B.t0);
    int a = flex_boot_alpha(e);
    if (a < 0) {
        end();
        return;
    }
    lv_obj_set_style_text_opa(B.title, (lv_opa_t)a, 0);
    lv_obj_set_style_text_opa(B.sub, (lv_opa_t)(a * 7 / 10), 0);
    int phase = flex_boot_dot(lv_tick_get());
    for (int i = 0; i < 3; i++) {
        lv_obj_set_style_bg_color(B.dots[i], i == phase ? lv_color_white() : lv_color_make(70, 74, 82), 0);
    }
}

static void splash_start(void)
{
    lv_obj_clean(B.root);
    B.splash = true;
    B.title = text(B.root, "FlexOS Ultra", FLEX_FONT_S6, lv_color_white(), TITLE_Y);
    B.sub = text(B.root, "ESP32-P4", FLEX_FONT_S2, lv_color_make(170, 182, 200), SUB_Y);
    for (int i = 0; i < 3; i++) {
        lv_obj_t *d = flex_box(B.root);
        lv_obj_set_size(d, 8, 8);
        lv_obj_set_pos(d, 240 - 16 + i * 16 - 4, DOTS_Y - 4);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        B.dots[i] = d;
    }
    B.t0 = lv_tick_get();
    splash_tick(NULL);
    B.timer = lv_timer_create(splash_tick, 16, NULL);
}

static void banner_done(lv_timer_t *t)
{
    (void)t;
    lv_timer_delete(B.timer);
    B.timer = NULL;
    if (flex_safe_mode()) {
        end();   // Modo seguro: la pantalla de Modo seguro es la primera y unica del arranque (paso 21)
        return;
    }
    splash_start();
}

// Arranque normal: [banda forense si el reinicio fue anormal] -> splash -> destino. Con Modo seguro,
// tras la banda va directo a su pantalla (sin splash). Sin la interfaz de arranque (simulador) el
// destino es inmediato.
void flex_boot_begin(void (*done)(void))
{
#ifdef FLEX_SIM
    if (!B.enabled) {
        done();
        return;
    }
#endif
    B.done = done;
    int reason = reset_reason();
    screen_new();
    if (flex_boot_banner_wanted(reason)) {
        char b[72];
        text(B.root, "FlexOS Ultra", FLEX_FONT_S3, lv_color_make(235, 238, 245), 376);
        snprintf(b, sizeof(b), "ultimo reinicio: %s", flex_boot_reason_str(reason));
        text(B.root, b, FLEX_FONT_S1, lv_color_make(240, 185, 90), 422);
        text(B.root, "P4 480x800 - modo offline", FLEX_FONT_S1, lv_color_make(140, 150, 170), 442);
        B.timer = lv_timer_create(banner_done, FLEX_BOOT_BANNER_MS, NULL);
        lv_timer_set_repeat_count(B.timer, 1);
        return;
    }
    if (flex_safe_mode()) {
        end();
        return;
    }
    splash_start();
}

bool flex_boot_active(void)
{
    return B.root != NULL;
}
