#include "flex_theme.h"

#include <string.h>
#include "flex_storage.h"
#include "flex_wallpaper.h"

#define HEX(x) LV_COLOR_MAKE(((x) >> 16) & 0xFF, ((x) >> 8) & 0xFF, (x) & 0xFF)

// Theme.h:75-174 (oscuro / claro). Los tres ultimos: AppFramework.h:1015-1017.
static const flex_palette_t k_dark = {
    .page = HEX(0x12141C), .win = HEX(0x12141C), .surf = HEX(0x222632), .surf2 = HEX(0x303648),
    .glass = HEX(0x303648), .glass2 = HEX(0x28325A), .txt = HEX(0xF0F2F8), .txt2 = HEX(0xA0A6B6),
    .mute = HEX(0x787E8E), .nav = HEX(0xE8ECF6), .border = HEX(0x424A5E), .divider = HEX(0x343A4A),
    .disabled = HEX(0x7C8292), .track = HEX(0x383E56), .sel = HEX(0x305CA8), .scrim = HEX(0x080A12),
    .shadow = HEX(0x000000), .on_acc = HEX(0xFFFFFF), .primary = HEX(0x3C6EEB), .danger = HEX(0xC84646),
    .ok = HEX(0x5AC88C), .warn = HEX(0xF0B45A), .err = HEX(0xEB5050), .acc_soft = HEX(0x8CB4FA),
    .key_panel = HEX(0x1E222E), .key_face = HEX(0x343846), .key_alt = HEX(0x424656),
    .navbar_bg = HEX(0x0D0F16), .navbar_fg = HEX(0xE8ECF5), .navbar_line = HEX(0x1E222E),
};
static const flex_palette_t k_light = {
    .page = HEX(0xF4F7FB), .win = HEX(0xF4F7FB), .surf = HEX(0xFFFFFF), .surf2 = HEX(0xECF0F8),
    .glass = HEX(0xF6F8FC), .glass2 = HEX(0xE0E8F6), .txt = HEX(0x14161E), .txt2 = HEX(0x606674),
    .mute = HEX(0x848A98), .nav = HEX(0x222632), .border = HEX(0xCAD2E0), .divider = HEX(0xE0E5EE),
    .disabled = HEX(0x848A96), .track = HEX(0xCAD0DC), .sel = HEX(0xB2CEF6), .scrim = HEX(0x222836),
    .shadow = HEX(0x465064), .on_acc = HEX(0xFFFFFF), .primary = HEX(0x2D5FE1), .danger = HEX(0xC43434),
    .ok = HEX(0x168C58), .warn = HEX(0xB0740C), .err = HEX(0xCC2E2E), .acc_soft = HEX(0x285CBE),
    .key_panel = HEX(0xDEE2EC), .key_face = HEX(0xFFFFFF), .key_alt = HEX(0xD6DBE6),
    .navbar_bg = HEX(0xEEF1F7), .navbar_fg = HEX(0x2C303C), .navbar_line = HEX(0xD6DBE4),
};

#define MAX_LISTENERS 24

static flex_look_prefs_t s_prefs;
static flex_glass_params_t s_glass;
static bool s_wall_acc_ok;
static lv_color_t s_wall_acc, s_wall_acc2;
static struct {
    flex_theme_listener_t cb;
    void *ctx;
} s_listeners[MAX_LISTENERS];
static int s_nlisteners;

const flex_palette_t *flex_th(void)
{
    return s_prefs.dark ? &k_dark : &k_light;
}

const flex_look_prefs_t *flex_look(void)
{
    return &s_prefs;
}

const flex_glass_params_t *flex_glass_params(void)
{
    return &s_glass;
}

// Acento: la paleta del fondo si esta encendida y calculada; si no, el del tema
// integrado. (En Arduino el acento del tema solo llegaba a la muestra del
// selector por un fallo de logica, docs/spec/02 §2.6; aqui si es el del sistema.)
lv_color_t flex_accent(void)
{
    if (s_prefs.wall_pal && s_wall_acc_ok) {
        return s_wall_acc;
    }
    if (s_prefs.look > 0 && s_prefs.look < FLEX_LOOK_N) {
        const flex_look_t *l = &flex_looks[s_prefs.look];
        return lv_color_make(l->ar, l->ag, l->ab);
    }
    return flex_th()->primary;
}

lv_color_t flex_accent2(void)
{
    if (s_prefs.wall_pal && s_wall_acc_ok) {
        return s_wall_acc2;
    }
    if (s_prefs.look > 0 && s_prefs.look < FLEX_LOOK_N) {
        const flex_look_t *l = &flex_looks[s_prefs.look];
        return lv_color_make(l->sr, l->sg, l->sb);
    }
    return flex_th()->acc_soft;
}

lv_color_t flex_on_color_lv(lv_color_t bg)
{
    return flex_c565(flex_on_color(flex_lv_to_565(bg)));
}

static void recompute(void)
{
    uint8_t lvl = s_prefs.glass_level;
    flex_glass_level_apply(lvl, &s_glass);
    s_prefs.glass_level = s_glass.level;
    if (s_prefs.eff_mode && s_glass.blur_r > FLEX_GLASS_BLUR_R_EFF) {
        s_glass.blur_r = FLEX_GLASS_BLUR_R_EFF;
    }
}

static void notify(void)
{
    recompute();
    for (int i = 0; i < s_nlisteners; i++) {
        s_listeners[i].cb(s_listeners[i].ctx);
    }
    lv_obj_report_style_change(NULL);
    lv_display_t *d = lv_display_get_default();
    if (d) {
        lv_obj_invalidate(lv_display_get_screen_active(d));
        lv_obj_invalidate(lv_display_get_layer_top(d));
    }
}

static uint8_t clamp_u8(int32_t v, int32_t lo, int32_t hi, uint8_t def)
{
    return (v < lo || v > hi) ? def : (uint8_t)v;
}

void flex_theme_init(void)
{
    s_prefs.dark = flex_cfg_get_bool("dark", true);
    s_prefs.glass = flex_cfg_get_bool("glass", false);
    s_prefs.icon_style = clamp_u8(flex_cfg_get_i32("iconstyle", 0), 0, 1, 0);
    int32_t lv = flex_cfg_get_i32("glasslv", FLEX_GLASS_LVL_DEF);
    s_prefs.glass_level = (uint8_t)(lv < 0 ? 0 : lv > 100 ? 100 : lv);   // Prefs.h:260
    s_prefs.wall_pal = flex_cfg_get_bool("wallpal", false);
    s_prefs.look = clamp_u8(flex_cfg_get_i32("hlook", 0), 0, FLEX_LOOK_N - 1, 0);
    int32_t wh = flex_cfg_get_i32("wallh", 0), wl = flex_cfg_get_i32("walll", 0);
    // Fuera de rango -> 0 (Home.h:1588); 200 = imagen del almacenamiento.
    s_prefs.wall_home = (wh == FLEX_WALL_IMG || (wh >= 0 && wh < FLEX_WALL_N)) ? (uint8_t)wh : 0;
    s_prefs.wall_lock = (wl == FLEX_WALL_IMG || (wl >= 0 && wl < FLEX_WALL_N)) ? (uint8_t)wl : 0;
    s_prefs.wall_fit = clamp_u8(flex_cfg_get_i32("wallfit", 0), 0, 2, 0);
    s_prefs.nav_mode = clamp_u8(flex_cfg_get_i32("navmode", 0), 0, 1, 0);
    s_prefs.anim_style = clamp_u8(flex_cfg_get_i32("animstyle", 0), 0, 2, 0);
    s_prefs.h24 = flex_cfg_get_bool("h24", false);
    s_prefs.eff_mode = false;
    recompute();
}

void flex_theme_listen(flex_theme_listener_t cb, void *ctx)
{
    if (cb && s_nlisteners < MAX_LISTENERS) {
        s_listeners[s_nlisteners].cb = cb;
        s_listeners[s_nlisteners].ctx = ctx;
        s_nlisteners++;
    }
}

void flex_theme_set_dark(bool dark)
{
    if (s_prefs.dark != dark) {
        s_prefs.dark = dark;
        flex_cfg_set_bool("dark", dark);
        notify();
    }
}

void flex_theme_set_glass(bool glass)
{
    if (s_prefs.glass != glass) {
        s_prefs.glass = glass;
        flex_cfg_set_bool("glass", glass);
        notify();
    }
}

void flex_theme_set_icon_style(uint8_t style)
{
    style = style ? 1 : 0;
    if (s_prefs.icon_style != style) {
        s_prefs.icon_style = style;
        flex_cfg_set_i32("iconstyle", style);
        notify();
    }
}

void flex_theme_set_glass_level(uint8_t level)
{
    flex_glass_params_t p;
    flex_glass_level_apply(level, &p);
    if (s_prefs.glass_level != p.level) {
        s_prefs.glass_level = p.level;
        flex_cfg_set_i32("glasslv", p.level);
        notify();
    }
}

void flex_theme_set_nav_mode(uint8_t mode)
{
    mode = mode ? 1 : 0;
    if (s_prefs.nav_mode != mode) {
        s_prefs.nav_mode = mode;
        flex_cfg_set_i32("navmode", mode);
        notify();
    }
}

void flex_theme_set_anim_style(uint8_t style)
{
    style = style > 2 ? 0 : style;
    if (s_prefs.anim_style != style) {
        s_prefs.anim_style = style;
        flex_cfg_set_i32("animstyle", style);
    }
}

void flex_theme_set_h24(bool h24)
{
    if (s_prefs.h24 != h24) {
        s_prefs.h24 = h24;
        flex_cfg_set_bool("h24", h24);
        notify();
    }
}

void flex_theme_set_wallpapers(uint8_t home, uint8_t lock)
{
    if (s_prefs.wall_home != home || s_prefs.wall_lock != lock) {
        s_prefs.wall_home = home;
        s_prefs.wall_lock = lock;
        flex_cfg_set_i32("wallh", home);
        flex_cfg_set_i32("walll", lock);
        notify();
    }
}

void flex_theme_set_wall_palette(bool on)
{
    if (s_prefs.wall_pal != on) {
        s_prefs.wall_pal = on;
        flex_cfg_set_bool("wallpal", on);
        notify();
    }
}

bool flex_theme_apply_look(uint8_t look)
{
    if (look >= FLEX_LOOK_N) {
        return false;
    }
    const flex_look_t *l = &flex_looks[look];
    if (l->wall >= FLEX_WALL_N || l->icon_style > 1) {
        return false;   // tabla corrupta: no se toca nada (HomeCfg.h:401)
    }
    s_prefs.look = look;
    s_prefs.dark = l->dark;
    s_prefs.glass = l->glass;
    s_prefs.icon_style = l->icon_style;
    s_prefs.wall_home = l->wall;
    s_prefs.wall_lock = l->wall;
    s_prefs.wall_pal = l->palette;
    flex_cfg_set_i32("hlook", look);
    flex_cfg_set_bool("dark", l->dark);
    flex_cfg_set_bool("glass", l->glass);
    flex_cfg_set_i32("iconstyle", l->icon_style);
    flex_cfg_set_i32("wallh", l->wall);
    flex_cfg_set_i32("walll", l->wall);
    flex_cfg_set_bool("wallpal", l->palette);
    notify();
    return true;
}

void flex_theme_set_eff_mode(bool on)
{
    if (s_prefs.eff_mode != on) {
        s_prefs.eff_mode = on;
        notify();
    }
}

void flex_theme_set_wall_accent(uint16_t acc565, uint16_t acc2_565)
{
    s_wall_acc = flex_c565(acc565);
    s_wall_acc2 = flex_c565(acc2_565);
    s_wall_acc_ok = true;
}

lv_obj_t *flex_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text ? text : "");
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

lv_obj_t *flex_box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_scrollable(o, false);
    return o;
}
