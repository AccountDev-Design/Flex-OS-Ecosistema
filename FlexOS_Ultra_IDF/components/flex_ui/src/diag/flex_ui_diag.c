#include "flex_ui_diag.h"

#include <stdio.h>
#include <string.h>
#include "../theme/flex_ui_theme.h"

// Textos sin tildes a proposito: las fuentes integradas de LVGL (Montserrat)
// solo traen ASCII. La tipografia con alfabeto latino completo llega en la
// Fase 3 con el sistema de diseno.

#define ROW_H           22
#define CARD_PAD        12
#define CIRCLE_D        56
#define TOUCH_PERIOD_MS 8
#define STATS_PERIOD_MS 500
#define TEXT_CACHE      48

enum {
    ROW_PANEL, ROW_FPS, ROW_RENDER, ROW_VSYNC, ROW_CPU, ROW_TOUCH_SHOWN, ROW_TOUCH_USED,
    ROW_RAM, ROW_PSRAM, ROW_CHIP, ROW_GT911, ROW_I2C, ROW_COUNT
};

static const char *const k_row_names[ROW_COUNT] = {
    "Panel (Hz reales)",
    "Cuadros nuevos / s",
    "Render (media / max)",
    "Espera de fin de cuadro",
    "CPU de la UI",
    "Toque > pantalla",
    "Toque > LVGL",
    "RAM interna libre",
    "PSRAM libre",
    "Chip / binario",
    "GT911",
    "I2C errores / recup.",
};

static const flex_diag_ops_t *s_ops;
static lv_obj_t *s_vals[ROW_COUNT];
static char s_val_text[ROW_COUNT][TEXT_CACHE];
static lv_obj_t *s_circles[FLEX_DIAG_MAX_POINTS];
static lv_obj_t *s_coord;
static char s_coord_text[TEXT_CACHE];
static lv_obj_t *s_bar;
static lv_obj_t *s_bright_val;
static lv_obj_t *s_stress_sw;
static lv_timer_t *s_stress_timer;
static bool s_touch_ok_shown = true;

static const uint32_t k_finger_colors[FLEX_DIAG_MAX_POINTS] = {
    0x4DA3FF, 0x46D18C, 0xFFB547, 0xFF5D5D, 0xB98CFF,
};

static lv_obj_t *plain_rect(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t c)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_clickable(o, false);
    lv_obj_set_scrollable(o, false);
    return o;
}

static lv_obj_t *text(lv_obj_t *parent, const char *txt, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

static lv_obj_t *card(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, FLEX_UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_90, 0);
    lv_obj_set_style_border_color(c, FLEX_UI_COLOR_OUTLINE, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_radius(c, FLEX_UI_RADIUS_M, 0);
    lv_obj_set_style_pad_all(c, CARD_PAD, 0);
    lv_obj_set_scrollable(c, false);
    return c;
}

// Solo se toca el label si el texto cambia: una pantalla quieta no debe
// generar ni un cuadro nuevo.
static void set_cached(lv_obj_t *label, char *cache, const char *txt)
{
    if (strncmp(cache, txt, TEXT_CACHE) != 0) {
        snprintf(cache, TEXT_CACHE, "%s", txt);
        lv_label_set_text(label, cache);
    }
}

static void build_grid(lv_obj_t *scr)
{
    for (int32_t x = 0; x < FLEX_UI_W; x += 40) {
        plain_rect(scr, x, 0, 1, FLEX_UI_H, x % 80 ? FLEX_UI_COLOR_GRID : FLEX_UI_COLOR_OUTLINE);
    }
    plain_rect(scr, FLEX_UI_W - 1, 0, 1, FLEX_UI_H, FLEX_UI_COLOR_OUTLINE);
    for (int32_t y = 0; y < FLEX_UI_H; y += 40) {
        plain_rect(scr, 0, y, FLEX_UI_W, 1, y % 80 ? FLEX_UI_COLOR_GRID : FLEX_UI_COLOR_OUTLINE);
    }
    plain_rect(scr, 0, FLEX_UI_H - 1, FLEX_UI_W, 1, FLEX_UI_COLOR_OUTLINE);
}

static void corner(lv_obj_t *scr, int32_t x, int32_t y, bool right, bool bottom, const char *label)
{
    const int32_t len = 24, th = 3;
    int32_t hx = right ? x - len + 1 : x;
    int32_t hy = bottom ? y - th + 1 : y;
    int32_t vx = right ? x - th + 1 : x;
    int32_t vy = bottom ? y - len + 1 : y;
    plain_rect(scr, hx, hy, len, th, FLEX_UI_COLOR_ACCENT);
    plain_rect(scr, vx, vy, th, len, FLEX_UI_COLOR_ACCENT);
    lv_obj_t *l = text(scr, label, &lv_font_montserrat_12, FLEX_UI_COLOR_ACCENT);
    lv_align_t a = bottom ? (right ? LV_ALIGN_BOTTOM_RIGHT : LV_ALIGN_BOTTOM_LEFT)
                          : (right ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT);
    lv_obj_align(l, a, right ? -8 : 8, bottom ? -6 : 6);
}

static void build_color_bars(lv_obj_t *scr, int32_t y0)
{
    static const struct {
        uint32_t color;
        const char *name;
    } bars[] = {{0xFF0000, "R"}, {0x00FF00, "G"}, {0x0000FF, "B"}, {0xFFFFFF, "W"}};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = plain_rect(scr, FLEX_UI_MARGIN, y0 + i * 22, FLEX_UI_W - 2 * FLEX_UI_MARGIN, 18,
                                 lv_color_black());
        lv_obj_set_style_bg_grad_color(b, lv_color_hex(bars[i].color), 0);
        lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_HOR, 0);
        lv_obj_t *l = text(b, bars[i].name, &lv_font_montserrat_12, lv_color_white());
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 6, 0);
    }
}

static void build_metrics_card(lv_obj_t *scr, int32_t y0)
{
    lv_obj_t *c = card(scr, FLEX_UI_MARGIN, y0, FLEX_UI_W - 2 * FLEX_UI_MARGIN, ROW_COUNT * ROW_H + 2 * CARD_PAD);
    for (int i = 0; i < ROW_COUNT; i++) {
        lv_obj_t *n = text(c, k_row_names[i], &lv_font_montserrat_14, FLEX_UI_COLOR_TEXT_DIM);
        lv_obj_align(n, LV_ALIGN_TOP_LEFT, 0, i * ROW_H);
        s_vals[i] = text(c, "--", &lv_font_montserrat_14, FLEX_UI_COLOR_TEXT);
        lv_obj_align(s_vals[i], LV_ALIGN_TOP_RIGHT, 0, i * ROW_H);
        snprintf(s_val_text[i], TEXT_CACHE, "--");
    }
}

static void anim_x_cb(void *var, int32_t v)
{
    lv_obj_set_x((lv_obj_t *)var, v);
}

static void tearing_anim_set(bool on)
{
    lv_anim_delete(s_bar, anim_x_cb);
    if (!on) {
        return;
    }
    lv_obj_t *track = lv_obj_get_parent(s_bar);
    // Recien creada la pantalla, el layout aun no esta calculado y los anchos
    // valen 0: se calcula antes de medir el recorrido.
    lv_obj_update_layout(track);
    int32_t span = lv_obj_get_content_width(track) - lv_obj_get_width(s_bar);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_bar);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_set_values(&a, 0, span);
    lv_anim_set_duration(&a, 1200);
    lv_anim_set_reverse_duration(&a, 1200);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
}

static void brightness_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target_obj(e);
    int32_t v = lv_slider_get_value(slider);
    lv_label_set_text_fmt(s_bright_val, "%d %%", (int)v);
    if (s_ops && s_ops->set_brightness) {
        s_ops->set_brightness((uint8_t)v);
    }
}

static void anim_sw_cb(lv_event_t *e)
{
    tearing_anim_set(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));
}

static void stress_sw_cb(lv_event_t *e)
{
    flex_ui_diag_set_stress(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));
}

static lv_obj_t *switch_with_label(lv_obj_t *parent, const char *label, int32_t x, int32_t y, bool on,
                                   lv_event_cb_t cb)
{
    lv_obj_t *sw = lv_switch_create(parent);
    lv_obj_set_size(sw, 64, 32);
    lv_obj_set_pos(sw, x, y);
    lv_obj_set_ext_click_area(sw, 12);
    if (on) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *l = text(parent, label, &lv_font_montserrat_14, FLEX_UI_COLOR_TEXT);
    lv_obj_align_to(l, sw, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    return sw;
}

static void build_controls(lv_obj_t *scr, int32_t y0)
{
    lv_obj_t *c = card(scr, FLEX_UI_MARGIN, y0, FLEX_UI_W - 2 * FLEX_UI_MARGIN, 132);
    lv_obj_t *l = text(c, "Brillo", &lv_font_montserrat_14, FLEX_UI_COLOR_TEXT);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 0);
    uint8_t initial = s_ops && s_ops->initial_brightness ? s_ops->initial_brightness : 80;
    s_bright_val = text(c, "", &lv_font_montserrat_14, FLEX_UI_COLOR_TEXT);
    lv_label_set_text_fmt(s_bright_val, "%d %%", (int)initial);
    lv_obj_align(s_bright_val, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_t *slider = lv_slider_create(c);
    lv_obj_set_width(slider, lv_pct(100));
    lv_obj_set_height(slider, 14);
    lv_obj_align(slider, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_ext_click_area(slider, 16);
    lv_slider_set_range(slider, 5, 100);
    lv_slider_set_value(slider, initial, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);

    switch_with_label(c, "Animacion", 0, 68, true, anim_sw_cb);
    s_stress_sw = switch_with_label(c, "Estres", 220, 68, false, stress_sw_cb);
}

static void build_tearing_strip(lv_obj_t *scr, int32_t y0)
{
    lv_obj_t *c = card(scr, FLEX_UI_MARGIN, y0, FLEX_UI_W - 2 * FLEX_UI_MARGIN, 104);
    lv_obj_t *l = text(c, "Tearing: la barra debe verse siempre entera", &lv_font_montserrat_12,
                       FLEX_UI_COLOR_TEXT_DIM);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *track = lv_obj_create(c);
    lv_obj_remove_style_all(track);
    lv_obj_set_size(track, lv_pct(100), 58);
    lv_obj_align(track, LV_ALIGN_TOP_LEFT, 0, 20);
    lv_obj_set_scrollable(track, false);
    lv_obj_set_clickable(track, false);
    s_bar = plain_rect(track, 0, 0, 12, 58, lv_color_white());
}

static void build_circles(lv_obj_t *scr)
{
    for (int i = 0; i < FLEX_DIAG_MAX_POINTS; i++) {
        lv_obj_t *o = lv_obj_create(scr);
        lv_obj_remove_style_all(o);
        lv_obj_set_size(o, CIRCLE_D, CIRCLE_D);
        lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(o, lv_color_hex(k_finger_colors[i]), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_40, 0);
        lv_obj_set_style_border_color(o, lv_color_hex(k_finger_colors[i]), 0);
        lv_obj_set_style_border_width(o, 3, 0);
        lv_obj_set_clickable(o, false);
        lv_obj_set_scrollable(o, false);
        lv_obj_set_ignore_layout(o, true);
        lv_obj_set_hidden(o, true);
        lv_obj_t *n = text(o, "", &lv_font_montserrat_16, lv_color_white());
        lv_label_set_text_fmt(n, "%d", i + 1);
        lv_obj_center(n);
        s_circles[i] = o;
    }
}

static void touch_timer_cb(lv_timer_t *t)
{
    (void)t;
    flex_diag_touch_t tf;
    memset(&tf, 0, sizeof(tf));
    bool have = s_ops && s_ops->get_touch && s_ops->get_touch(&tf);
    uint8_t count = have ? tf.count : 0;
    if (count > FLEX_DIAG_MAX_POINTS) {
        count = FLEX_DIAG_MAX_POINTS;
    }
    for (int i = 0; i < FLEX_DIAG_MAX_POINTS; i++) {
        lv_obj_t *o = s_circles[i];
        if (i < count) {
            lv_obj_set_pos(o, tf.pts[i].x - CIRCLE_D / 2, tf.pts[i].y - CIRCLE_D / 2);
            if (lv_obj_is_hidden(o)) {
                lv_obj_set_hidden(o, false);
            }
        } else if (!lv_obj_is_hidden(o)) {
            lv_obj_set_hidden(o, true);
        }
    }
    char buf[TEXT_CACHE];
    if (count) {
        snprintf(buf, sizeof(buf), "Toque: x=%d  y=%d  dedos=%u", tf.pts[0].x, tf.pts[0].y, (unsigned)count);
    } else {
        snprintf(buf, sizeof(buf), "Toque: sin dedos");
    }
    set_cached(s_coord, s_coord_text, buf);
}

static void fmt_rev(char *out, size_t n, uint16_t rev)
{
    snprintf(out, n, "v%u.%u", rev / 100, rev % 100);
}

static void stats_timer_cb(lv_timer_t *t)
{
    (void)t;
    flex_metrics_t m;
    memset(&m, 0, sizeof(m));
    if (s_ops && s_ops->get_metrics) {
        s_ops->get_metrics(&m);
    }
    flex_diag_sys_t sys;
    memset(&sys, 0, sizeof(sys));
    if (s_ops && s_ops->get_sys) {
        s_ops->get_sys(&sys);
    }
    char buf[TEXT_CACHE];

    snprintf(buf, sizeof(buf), "%.1f Hz", (double)m.panel_hz);
    set_cached(s_vals[ROW_PANEL], s_val_text[ROW_PANEL], buf);
    snprintf(buf, sizeof(buf), "%.1f", (double)m.fps);
    set_cached(s_vals[ROW_FPS], s_val_text[ROW_FPS], buf);
    snprintf(buf, sizeof(buf), "%.1f / %.1f ms", (double)m.render_ms_avg, (double)m.render_ms_max);
    set_cached(s_vals[ROW_RENDER], s_val_text[ROW_RENDER], buf);
    snprintf(buf, sizeof(buf), "%.1f ms  (vencidas %u)", (double)m.vsync_wait_ms_avg, (unsigned)m.vsync_timeouts);
    set_cached(s_vals[ROW_VSYNC], s_val_text[ROW_VSYNC], buf);
    snprintf(buf, sizeof(buf), "%.0f %%", (double)m.ui_cpu_pct);
    set_cached(s_vals[ROW_CPU], s_val_text[ROW_CPU], buf);
    if (m.touch_samples) {
        snprintf(buf, sizeof(buf), "%.1f / %.1f ms", (double)m.touch_shown_ms_avg, (double)m.touch_shown_ms_max);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    set_cached(s_vals[ROW_TOUCH_SHOWN], s_val_text[ROW_TOUCH_SHOWN], buf);
    if (m.touch_samples) {
        snprintf(buf, sizeof(buf), "%.1f ms", (double)m.touch_used_ms_avg);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    set_cached(s_vals[ROW_TOUCH_USED], s_val_text[ROW_TOUCH_USED], buf);
    snprintf(buf, sizeof(buf), "%u KB (min %u)", (unsigned)sys.heap_internal_free_kb, (unsigned)sys.heap_internal_min_kb);
    set_cached(s_vals[ROW_RAM], s_val_text[ROW_RAM], buf);
    snprintf(buf, sizeof(buf), "%u KB", (unsigned)sys.psram_free_kb);
    set_cached(s_vals[ROW_PSRAM], s_val_text[ROW_PSRAM], buf);

    char r0[12], r1[12], r2[12];
    fmt_rev(r0, sizeof(r0), sys.chip_rev);
    fmt_rev(r1, sizeof(r1), sys.build_rev_min);
    fmt_rev(r2, sizeof(r2), sys.build_rev_max);
    snprintf(buf, sizeof(buf), "%s / %s..%s", r0, r1, r2);
    set_cached(s_vals[ROW_CHIP], s_val_text[ROW_CHIP], buf);

    if (sys.touch_present) {
        snprintf(buf, sizeof(buf), "0x%02X  %s  %ux%u  err %u", sys.touch_addr, sys.touch_id[0] ? sys.touch_id : "?",
                 sys.touch_res_x, sys.touch_res_y, (unsigned)sys.touch_errors);
    } else {
        snprintf(buf, sizeof(buf), "NO DETECTADO");
    }
    set_cached(s_vals[ROW_GT911], s_val_text[ROW_GT911], buf);
    if (sys.touch_present != s_touch_ok_shown) {
        s_touch_ok_shown = sys.touch_present;
        lv_obj_set_style_text_color(s_vals[ROW_GT911], sys.touch_present ? FLEX_UI_COLOR_TEXT : FLEX_UI_COLOR_ERROR, 0);
    }

    snprintf(buf, sizeof(buf), "%u / %u%s", (unsigned)sys.i2c_errors, (unsigned)sys.i2c_recoveries,
             sys.i2c_wedged ? "  TRABADO" : "");
    set_cached(s_vals[ROW_I2C], s_val_text[ROW_I2C], buf);
}

static void stress_timer_cb(lv_timer_t *t)
{
    (void)t;
    lv_obj_invalidate(lv_screen_active());
}

void flex_ui_diag_set_stress(bool on)
{
    if (on && !s_stress_timer) {
        s_stress_timer = lv_timer_create(stress_timer_cb, 1, NULL);
    } else if (!on && s_stress_timer) {
        lv_timer_delete(s_stress_timer);
        s_stress_timer = NULL;
    }
    if (s_stress_sw && lv_obj_has_state(s_stress_sw, LV_STATE_CHECKED) != on) {
        if (on) {
            lv_obj_add_state(s_stress_sw, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(s_stress_sw, LV_STATE_CHECKED);
        }
    }
}

lv_obj_t *flex_ui_diag_create(const flex_diag_ops_t *ops)
{
    s_ops = ops;
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, FLEX_UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);

    build_grid(scr);

    lv_obj_t *title = text(scr, "Flex OS Ultra  |  prueba 480x800", &lv_font_montserrat_20, FLEX_UI_COLOR_TEXT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    build_color_bars(scr, 44);
    build_metrics_card(scr, 136);
    build_controls(scr, 136 + ROW_COUNT * ROW_H + 2 * CARD_PAD + 12);
    build_tearing_strip(scr, 576);

    s_coord = text(scr, "Toque: sin dedos", &lv_font_montserrat_16, FLEX_UI_COLOR_TEXT);
    snprintf(s_coord_text, TEXT_CACHE, "Toque: sin dedos");
    lv_obj_align(s_coord, LV_ALIGN_TOP_MID, 0, 692);

    lv_obj_t *f1 = text(scr, "LVGL 9.6  |  DIRECT con 2 framebuffers  |  ESP-IDF 5.5", &lv_font_montserrat_12,
                        FLEX_UI_COLOR_TEXT_DIM);
    lv_obj_align(f1, LV_ALIGN_TOP_MID, 0, 726);
    lv_obj_t *f2 = text(scr, "NO PROBADO EN HARDWARE REAL", &lv_font_montserrat_12, FLEX_UI_COLOR_WARN);
    lv_obj_align(f2, LV_ALIGN_TOP_MID, 0, 744);

    // Marcas encima de todo lo demas: esquinas exactas y centro (240,400).
    corner(scr, 0, 0, false, false, "0,0");
    corner(scr, FLEX_UI_W - 1, 0, true, false, "479,0");
    corner(scr, 0, FLEX_UI_H - 1, false, true, "0,799");
    corner(scr, FLEX_UI_W - 1, FLEX_UI_H - 1, true, true, "479,799");
    plain_rect(scr, FLEX_UI_W / 2 - 12, FLEX_UI_H / 2, 25, 1, FLEX_UI_COLOR_WARN);
    plain_rect(scr, FLEX_UI_W / 2, FLEX_UI_H / 2 - 12, 1, 25, FLEX_UI_COLOR_WARN);

    build_circles(scr);

    lv_screen_load(scr);
    tearing_anim_set(true);
    lv_timer_create(touch_timer_cb, TOUCH_PERIOD_MS, NULL);
    lv_timer_t *st = lv_timer_create(stats_timer_cb, STATS_PERIOD_MS, NULL);
    lv_timer_ready(st);
    return scr;
}
