// Flex OS Ultra · interfaz de las actualizaciones OTA (N5b; Arduino: FlexOS_OTA.cpp, capas 1-4). Cuatro capas sobre el escritorio, con la paleta y las medidas
// de Arduino: el aviso flotante (abajo, con el icono de actualizacion), el modal del changelog (Cancelar / Actualizar ahora), la pantalla de instalacion (porcentaje
// gigante, barra, fase, bytes y velocidad; "Cancelar" solo mientras se puede abortar) y la pagina de Ajustes > Actualizaciones. Lo que muestra sale del servicio
// (flex_ota_get): esta capa no descarga ni instala nada.
//
// Reglas de Arduino que se conservan: la instalacion es MODAL mientras trabaja (Atras, Inicio y Recientes no hacen nada; a la pagina se le vuelve a poner el aviso
// de error con "Entendido"); el aviso solo sale en el escritorio, se va solo a los 9 s y un deslizamiento hacia abajo lo descarta; una capa a pantalla completa se
// queda TODO lo que le llegue.
#include <stdio.h>
#include <string.h>
#include "flex_frame.h"
#include "flex_glass.h"
#include "flex_icons.h"
#include "flex_kit.h"
#include "flex_ota.h"
#include "flex_ota_ui.h"
#include "flex_shell.h"
#include "flex_theme.h"
#include "flex_touch_lvgl.h"

#define OTA_M 16
#define OTA_RAD 28
#define OTA_TOAST_H 96
#define OTA_TOAST_GAP 80
#define OTA_MODAL_H 540
#define OTA_BTN_H 56
#define OTA_ICON 44
#define OTA_ANIM_IN 300
#define OTA_ANIM_OUT 240
#define OTA_TOAST_MS 9000
#define OTA_PLAT_NAME "Flex OS Ultra"
#define W 480
#define H 800

typedef enum { OV_HIDDEN = 0, OV_TOAST, OV_CHANGELOG, OV_PROGRESS, OV_SETTINGS } ov_t;
enum { BT_CHECK = FLEX_OTA_BT_CHECK, BT_INSTALL = FLEX_OTA_BT_INSTALL, BT_CANCEL = FLEX_OTA_BT_CANCEL, BT_CLOSE = FLEX_OTA_BT_CLOSE, BT_BACK = FLEX_OTA_BT_BACK,
       BT_TOAST = FLEX_OTA_BT_TOAST };

static struct {
    lv_obj_t *root, *card, *pct_l, *bar_fill, *phase_l, *bytes_l, *bps_l;
    ov_t ov;
    uint32_t t0, close_t0;
    bool closing;
    lv_timer_t *tmr;
    flex_ota_state_t shown_st;
    uint8_t shown_pct;
    uint32_t shown_done, shown_bps;
    char toast_ver[FLEX_OTA_VER_MAX];
    bool tdrag;
    int ty0, builds;
    int btn_xy[8][2];
    bool btn_has[8];
} U;

static lv_obj_t *s_logview;   // el area con scroll del changelog (solo una a la vez)

static bool dark(void) { return flex_look()->dark; }
static lv_color_t rgb(int r, int g, int b) { return lv_color_make((uint8_t)r, (uint8_t)g, (uint8_t)b); }
static lv_color_t c_bg(void) { return dark() ? rgb(16, 18, 26) : rgb(244, 247, 251); }
static lv_color_t c_card(void) { return dark() ? rgb(34, 38, 50) : rgb(255, 255, 255); }
static lv_color_t c_hi(void) { return dark() ? rgb(240, 242, 248) : rgb(20, 22, 30); }
static lv_color_t c_lo(void) { return dark() ? rgb(160, 166, 182) : rgb(120, 126, 140); }
static lv_color_t c_accent(void) { return rgb(60, 140, 235); }
static lv_color_t c_ok(void) { return rgb(60, 190, 130); }
static lv_color_t c_err(void) { return rgb(230, 90, 90); }
static lv_color_t c_line(void) { return dark() ? rgb(58, 64, 82) : rgb(224, 229, 238); }
static lv_color_t c_white(void) { return rgb(255, 255, 255); }
static lv_color_t c_btn2(void) { return dark() ? rgb(52, 58, 76) : rgb(232, 236, 244); }
static lv_color_t c_track(void) { return dark() ? rgb(52, 58, 76) : rgb(224, 229, 238); }

static float ease_out(float p)
{
    float q = 1.0f - p;
    return 1.0f - q * q * q;
}

static void fmt_bytes(uint32_t b, char *out, size_t cap)
{
    if (b >= 1048576u) {
        snprintf(out, cap, "%u,%01u MB", (unsigned)(b / 1048576u), (unsigned)((b % 1048576u) * 10u / 1048576u));
    } else if (b >= 1024u) {
        snprintf(out, cap, "%u KB", (unsigned)(b / 1024u));
    } else {
        snprintf(out, cap, "%u B", (unsigned)b);
    }
}

// ---- piezas ----
static lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h)
{
    lv_obj_t *o = flex_box(p);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_clickable(o, false);
    return o;
}
static void fill(lv_obj_t *o, lv_color_t c, int radius)
{
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
}
static lv_obj_t *txt(lv_obj_t *p, const char *s, int fs, lv_color_t c, int x, int y, bool centered)
{
    lv_obj_t *l;
    if (fs >= 6) {   // el porcentaje gigante de la instalacion (SZ_BIG): la fuente grande del sistema
        l = flex_label(p, s, FLEX_FONT_S6, c);
        if (centered) {
            flex_label_cap_center(l, x, y);
        } else {
            lv_obj_set_x(l, x);
            flex_label_cap_y(l, y);
        }
    } else {
        l = flex_kit_text(p, s, fs, c, x, y, centered);
    }
    lv_obj_set_clickable(l, false);
    return l;
}

// Los tres glifos de Arduino (circulo + flecha de descarga / marca / aspa), con trazos de extremos redondos
typedef struct { int kind, s; lv_color_t col, fg; } glyph_t;
static void glyph_draw(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    glyph_t *g = lv_event_get_user_data(e);
    lv_layer_t *L = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    int s = g->s, cx = a.x1 + s / 2, cy = a.y1 + s / 2;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_opa = LV_OPA_COVER;
    d.bg_color = g->col;
    lv_area_t ca = {cx - s / 2, cy - s / 2, cx - s / 2 + s - 1, cy - s / 2 + s - 1};
    lv_draw_rect(L, &d, &ca);
    float t = g->kind == 0 ? s * 0.055f : s * 0.075f;
    if (t < (g->kind == 0 ? 1.6f : 1.8f)) {
        t = g->kind == 0 ? 1.6f : 1.8f;
    }
    float seg[4][4];
    int n = 0;
    float f = (float)s;
    if (g->kind == 0) {   // actualizar: flecha hacia abajo y base
        float v[4][4] = {{0, -0.24f, 0, 0.16f}, {-0.16f, -0.02f, 0, 0.17f}, {0.16f, -0.02f, 0, 0.17f}, {-0.20f, 0.28f, 0.20f, 0.28f}};
        memcpy(seg, v, sizeof(v));
        n = 4;
    } else if (g->kind == 1) {   // al dia: marca
        float v[2][4] = {{-0.20f, 0.02f, -0.04f, 0.17f}, {-0.04f, 0.17f, 0.21f, -0.16f}};
        memcpy(seg, v, sizeof(v));
        n = 2;
    } else {   // error: aspa
        float v[2][4] = {{-0.17f, -0.17f, 0.17f, 0.17f}, {-0.17f, 0.17f, 0.17f, -0.17f}};
        memcpy(seg, v, sizeof(v));
        n = 2;
    }
    for (int i = 0; i < n; i++) {
        lv_draw_line_dsc_t l;
        lv_draw_line_dsc_init(&l);
        l.color = g->fg;
        l.width = (int32_t)(2.0f * t + 0.5f);
        l.round_start = 1;
        l.round_end = 1;
        l.p1.x = cx + (int32_t)(seg[i][0] * f + (seg[i][0] < 0 ? -0.5f : 0.5f));
        l.p1.y = cy + (int32_t)(seg[i][1] * f + (seg[i][1] < 0 ? -0.5f : 0.5f));
        l.p2.x = cx + (int32_t)(seg[i][2] * f + (seg[i][2] < 0 ? -0.5f : 0.5f));
        l.p2.y = cy + (int32_t)(seg[i][3] * f + (seg[i][3] < 0 ? -0.5f : 0.5f));
        lv_draw_line(L, &l);
    }
}
static void glyph_free(lv_event_t *e) { lv_free(lv_event_get_user_data(e)); }
static lv_obj_t *glyph(lv_obj_t *p, int kind, int x, int y, int s, lv_color_t col)
{
    lv_obj_t *o = box(p, x, y, s, s);
    glyph_t *g = lv_malloc(sizeof(*g));
    g->kind = kind;
    g->s = s;
    g->col = col;
    g->fg = c_white();
    lv_obj_add_event_cb(o, glyph_draw, LV_EVENT_DRAW_MAIN, g);
    lv_obj_add_event_cb(o, glyph_free, LV_EVENT_DELETE, g);
    return o;
}

static void btn_click(lv_event_t *e);
static lv_obj_t *button(lv_obj_t *p, int id, int x, int y, int w, int h, const char *label, bool filled)
{
    lv_obj_t *b = box(p, x, y, w, h);
    fill(b, filled ? c_accent() : c_btn2(), h / 2);
    lv_obj_set_clickable(b, true);
    lv_obj_set_user_data(b, (void *)(intptr_t)id);
    lv_obj_add_event_cb(b, btn_click, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = txt(b, label, 2, filled ? c_white() : c_hi(), w / 2, (h - 16) / 2, true);
    (void)l;
    U.btn_xy[id][0] = x + w / 2;
    U.btn_xy[id][1] = y + h / 2;
    U.btn_has[id] = true;
    return b;
}

static void clear_ui(void)
{
    if (U.root) {
        lv_obj_delete(U.root);
    }
    U.root = U.card = U.pct_l = U.bar_fill = U.phase_l = U.bytes_l = U.bps_l = NULL;
    s_logview = NULL;
    memset(U.btn_has, 0, sizeof(U.btn_has));
}

static void show(ov_t ov);
static void close_overlay(void);

// El gesto "atras" del sistema (deslizar hacia la derecha) sale del changelog y de la pagina de ajustes; la instalacion se queda fuera a proposito: mientras se
// escribe en la particion el usuario debe ver lo que pasa y no perder la pantalla por un roce
static void root_gesture_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *in = lv_indev_active();
    if (in && (U.ov == OV_CHANGELOG || U.ov == OV_SETTINGS) && lv_indev_get_gesture_dir(in) == LV_DIR_RIGHT) {
        lv_indev_wait_release(in);
        close_overlay();
    }
}

// ---- capa 1: el aviso flotante ----
static void toast_press_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    lv_indev_t *in = lv_indev_active();
    lv_point_t p = {0, 0};
    if (in) {
        lv_indev_get_point(in, &p);
    }
    if (c == LV_EVENT_PRESSED) {
        U.tdrag = true;
        U.ty0 = p.y;
    } else if (c == LV_EVENT_PRESSING && U.tdrag && p.y - U.ty0 > 36 && !U.closing) {   // un deslizamiento hacia abajo lo descarta
        U.tdrag = false;
        U.closing = true;
        U.close_t0 = lv_tick_get();
    } else if (c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) {
        U.tdrag = false;
    }
}

static void build_toast(void)
{
    flex_ota_info_t i;
    flex_ota_get(&i);
    U.root = box(flex_shell_screen(), OTA_M, H - OTA_TOAST_GAP - OTA_TOAST_H, W - 2 * OTA_M, OTA_TOAST_H);
    lv_obj_move_foreground(U.root);
    U.card = U.root;
    if (flex_look()->glass) {
        flex_surface(U.card, FLEX_SURF_ELEVATED, FLEX_BD_FLAT);
    } else {
        fill(U.card, c_card(), OTA_RAD);
    }
    lv_obj_set_style_radius(U.card, OTA_RAD, 0);
    lv_obj_set_style_border_width(U.card, 1, 0);
    lv_obj_set_style_border_color(U.card, dark() ? rgb(96, 104, 128) : rgb(206, 214, 228), 0);
    lv_obj_set_style_shadow_width(U.card, 14, 0);
    lv_obj_set_style_shadow_opa(U.card, 40, 0);
    lv_obj_set_style_shadow_ofs_y(U.card, 3, 0);
    lv_obj_set_clickable(U.card, true);
    lv_obj_add_event_cb(U.card, btn_click, LV_EVENT_CLICKED, NULL);
    lv_obj_set_user_data(U.card, (void *)(intptr_t)BT_TOAST);
    lv_obj_add_event_cb(U.card, toast_press_cb, LV_EVENT_ALL, NULL);
    glyph(U.card, 0, 18, (OTA_TOAST_H - OTA_ICON) / 2, OTA_ICON, c_accent());
    int tx = 18 + OTA_ICON + 16;
    lv_obj_t *l = txt(U.card, "Nueva actualizacion disponible", 2, c_hi(), tx, 20, false);
    flex_label_one_line(l, W - 2 * OTA_M - tx - 20);
    char l2[56];
    snprintf(l2, sizeof(l2), "%s %s", OTA_PLAT_NAME, i.remote);
    l = txt(U.card, l2, 1, c_lo(), tx, 20 + 20 + 6, false);
    flex_label_one_line(l, W - 2 * OTA_M - tx - 20);
    U.btn_xy[BT_TOAST][0] = OTA_M + (W - 2 * OTA_M) / 2;
    U.btn_xy[BT_TOAST][1] = H - OTA_TOAST_GAP - OTA_TOAST_H / 2;
    U.btn_has[BT_TOAST] = true;
}

// ---- capa 2: el modal del changelog ----
static lv_obj_t *changelog_view(lv_obj_t *p, int x, int y, int w, int h, const char *text)
{
    lv_obj_t *sv = box(p, x, y, w, h);
    s_logview = sv;
    lv_obj_set_clickable(sv, true);
    lv_obj_set_scrollable(sv, true);
    lv_obj_set_scroll_dir(sv, LV_DIR_VER);
    lv_obj_set_scroll_elastic(sv, false);
    lv_obj_set_scroll_momentum(sv, false);
    lv_obj_set_scrollbar_mode(sv, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_t *l = flex_kit_text(sv, text, 2, c_lo(), 0, 0, false);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(l, w - 12);
    lv_obj_set_style_text_line_space(l, 24 - 19, 0);
    return sv;
}

static void build_modal(void)
{
    flex_ota_info_t i;
    flex_ota_get(&i);
    U.root = box(flex_shell_screen(), 0, 0, W, H);
    lv_obj_move_foreground(U.root);
    fill(U.root, dark() ? rgb(8, 10, 16) : rgb(228, 232, 240), 0);   // velo OPACO, como en Arduino
    lv_obj_set_clickable(U.root, true);   // una capa a pantalla completa se queda TODO lo que le llegue
    lv_obj_add_event_cb(U.root, root_gesture_cb, LV_EVENT_GESTURE, NULL);
    int cw = W - 2 * OTA_M * 2;
    int ch = OTA_MODAL_H > H - 80 ? H - 80 : OTA_MODAL_H;
    int cx = (W - cw) / 2, cy = (H - ch) / 2;
    U.card = box(U.root, cx, cy, cw, ch);
    fill(U.card, c_card(), OTA_RAD);
    int y = 26;
    int is = OTA_ICON + 8;
    glyph(U.card, 0, (cw - is) / 2, y, is, c_accent());
    y += is + 16;
    txt(U.card, "Actualizacion de software", 3, c_hi(), cw / 2, y, true);
    y += 3 * 10 + 14;
    char row[72];
    snprintf(row, sizeof(row), "%s  %s", OTA_PLAT_NAME, i.remote);
    txt(U.card, row, 2, c_accent(), cw / 2, y, true);
    y += 2 * 10 + 8;
    snprintf(row, sizeof(row), "Version actual: %s", flex_ota_local_version());
    txt(U.card, row, 1, c_lo(), cw / 2, y, true);
    y += 1 * 10 + 6;
    if (i.total > 0) {
        char sz[24];
        fmt_bytes(i.total, sz, sizeof(sz));
        snprintf(row, sizeof(row), "Tamano de la descarga: %s", sz);
        txt(U.card, row, 1, c_lo(), cw / 2, y, true);
    }
    y += 1 * 10 + 12;
    lv_obj_t *ln = box(U.card, 20, y, cw - 40, 1);
    fill(ln, c_line(), 0);
    y += 14;
    int btn_y = ch - OTA_BTN_H - 20;
    changelog_view(U.card, 20, y, cw - 40, btn_y - 14 - y, i.changelog[0] ? i.changelog : "Sin notas de la version.");
    int bw = (cw - 20 * 2 - 12) / 2;
    button(U.card, BT_CANCEL, 20, btn_y, bw, OTA_BTN_H, "Cancelar", false);
    button(U.card, BT_INSTALL, 20 + bw + 12, btn_y, bw, OTA_BTN_H, "Actualizar ahora", true);
    for (int b = BT_CANCEL; b <= BT_INSTALL; b++) {   // las coordenadas guardadas son de la tarjeta: se pasan a la pantalla
        U.btn_xy[b][0] += cx;
        U.btn_xy[b][1] += cy;
    }
}

// ---- capa 3: la pantalla de instalacion ----
static void progress_dynamic(const flex_ota_info_t *i)
{
    char l[72];
    if (U.pct_l) {
        snprintf(l, sizeof(l), "%u%%", (unsigned)i->percent);
        lv_label_set_text(U.pct_l, l);
    }
    if (U.bar_fill) {
        int w = W - 2 * (OTA_M + 24);
        int fw = (int)((uint32_t)w * i->percent / 100u);
        if (fw < 10) {
            fw = i->percent > 0 ? 10 : 0;   // que se vea aunque sea 1 %
        }
        lv_obj_set_width(U.bar_fill, fw > 0 ? fw : 1);
        lv_obj_set_hidden(U.bar_fill, fw == 0);
    }
    if (U.bytes_l) {
        if (i->total > 0) {
            char a[24], b[24];
            fmt_bytes(i->done, a, sizeof(a));
            fmt_bytes(i->total, b, sizeof(b));
            snprintf(l, sizeof(l), "%s / %s", a, b);
            lv_label_set_text(U.bytes_l, l);
        }
    }
    if (U.bps_l) {
        if (i->bps > 0 && i->state == FLEX_OTA_DOWNLOADING) {
            char v[24];
            fmt_bytes(i->bps, v, sizeof(v));
            snprintf(l, sizeof(l), "%s/s", v);
            lv_label_set_text(U.bps_l, l);
        } else {
            lv_label_set_text(U.bps_l, "");
        }
    }
}

static void build_progress(void)
{
    flex_ota_info_t i;
    flex_ota_get(&i);
    bool bad = i.state == FLEX_OTA_ERROR;
    U.root = box(flex_shell_screen(), 0, 0, W, H);
    lv_obj_move_foreground(U.root);
    fill(U.root, c_bg(), 0);
    lv_obj_set_clickable(U.root, true);
    int y_top = H / 2 - OTA_MODAL_H / 2;
    if (y_top < 60) {
        y_top = 60;
    }
    int is = OTA_ICON + 20;
    int y_head = y_top + is + 24;
    int y_ver = y_head + 3 * 10 + 10;
    int y_dyn = y_ver + 1 * 10 + 30;
    int dyn_h = 6 * 10 + 22 + 10 + 26 + 2 * 10 + 12 + (1 * 10 + 6) * 2 + 8;
    if (y_dyn + dyn_h > H - 1) {
        dyn_h = H - 1 - y_dyn;
    }
    glyph(U.root, bad ? 2 : 0, (W - is) / 2, y_top, is, bad ? c_err() : c_accent());
    txt(U.root, bad ? "No se pudo actualizar" : "Actualizando FlexOS", 3, c_hi(), W / 2, y_head, true);
    char l[72];
    snprintf(l, sizeof(l), "%s %s", OTA_PLAT_NAME, i.remote);
    txt(U.root, l, 1, c_lo(), W / 2, y_ver, true);
    if (bad) {
        int y = y_dyn;
        txt(U.root, flex_ota_err_text(i.error), 2, c_err(), W / 2, y, true);
        y += 2 * 10 + 16;
        txt(U.root, "El equipo no se ha modificado.", 1, c_lo(), W / 2, y, true);
        y += 1 * 10 + 34;
        int bw = W - 2 * (OTA_M + 40);
        button(U.root, BT_CLOSE, (W - bw) / 2, y, bw, OTA_BTN_H, "Entendido", true);
        return;
    }
    int y = y_dyn;
    U.pct_l = txt(U.root, "0%", 6, c_hi(), W / 2, y, true);
    lv_obj_set_width(U.pct_l, W);   // ancho fijo y centrado: el texto cambia con cada avance
    lv_obj_set_x(U.pct_l, 0);
    lv_obj_set_style_text_align(U.pct_l, LV_TEXT_ALIGN_CENTER, 0);
    y += 6 * 10 + 22;
    lv_obj_t *track = box(U.root, OTA_M + 24, y, W - 2 * (OTA_M + 24), 10);
    fill(track, c_track(), 5);
    U.bar_fill = box(track, 0, 0, 10, 10);
    fill(U.bar_fill, c_accent(), 5);
    y += 26;
    U.phase_l = txt(U.root, flex_ota_phase_text(i.state), 2, c_accent(), W / 2, y, true);
    y += 2 * 10 + 12;
    U.bytes_l = txt(U.root, "", 1, c_lo(), W / 2, y, true);
    lv_obj_set_width(U.bytes_l, W);
    lv_obj_set_style_text_align(U.bytes_l, LV_TEXT_ALIGN_CENTER, 0);
    y += 1 * 10 + 6;
    U.bps_l = txt(U.root, "", 1, c_lo(), W / 2, y, true);
    lv_obj_set_width(U.bps_l, W);
    lv_obj_set_style_text_align(U.bps_l, LV_TEXT_ALIGN_CENTER, 0);
    int y_btn = y_dyn + dyn_h + 16;
    // Cancelar SOLO mientras se puede abortar sin dejar nada a medias; a partir de INSTALLING ya no se ofrece
    if (i.state == FLEX_OTA_CONNECTING || i.state == FLEX_OTA_DOWNLOADING) {
        int bw = W - 2 * (OTA_M + 40);
        button(U.root, BT_CANCEL, (W - bw) / 2, y_btn, bw, OTA_BTN_H, "Cancelar", false);
    } else if (i.state == FLEX_OTA_REBOOTING) {
        txt(U.root, "El equipo se reiniciara solo", 1, c_lo(), W / 2, y_btn + 8, true);
    }
    progress_dynamic(&i);
}

// ---- capa 4: Ajustes > Actualizaciones ----
static void build_settings(void)
{
    flex_ota_info_t i;
    flex_ota_get(&i);
    flex_ota_state_t s = i.state;
    U.root = box(flex_shell_screen(), 0, 0, W, H);
    lv_obj_move_foreground(U.root);
    fill(U.root, c_bg(), 0);
    lv_obj_set_clickable(U.root, true);
    lv_obj_add_event_cb(U.root, root_gesture_cb, LV_EVENT_GESTURE, NULL);
    // cabecera con flecha atras
    lv_obj_t *back = box(U.root, 4, 6, 60, 56);
    lv_obj_set_clickable(back, true);
    lv_obj_set_user_data(back, (void *)(intptr_t)BT_BACK);
    lv_obj_add_event_cb(back, btn_click, LV_EVENT_CLICKED, NULL);
    lv_obj_t *g = flex_glyph_create(back, FLEX_GLYPH_CHEVRON_LEFT, 28, 28);
    lv_obj_set_pos(g, 14, 14);
    lv_obj_set_style_text_color(g, c_hi(), 0);
    U.btn_xy[BT_BACK][0] = 34;
    U.btn_xy[BT_BACK][1] = 34;
    U.btn_has[BT_BACK] = true;
    txt(U.root, "Actualizacion de software", 2, c_hi(), W / 2, 26, true);
    int y = 92;
    int card_h = OTA_ICON + 116;
    lv_obj_t *card = box(U.root, OTA_M, y, W - 2 * OTA_M, card_h);
    fill(card, c_card(), OTA_RAD);
    int is = OTA_ICON + 12;
    bool upd = s == FLEX_OTA_AVAILABLE || i.has_update;
    if (s == FLEX_OTA_ERROR) {
        glyph(U.root, 2, (W - is) / 2, y + 20, is, c_err());
    } else if (upd) {
        glyph(U.root, 0, (W - is) / 2, y + 20, is, c_accent());
    } else {
        glyph(U.root, 1, (W - is) / 2, y + 20, is, c_ok());
    }
    int ty = y + 20 + is + 16;
    const char *head = upd ? "Actualizacion disponible" : (s == FLEX_OTA_ERROR ? "No se pudo comprobar" : (s == FLEX_OTA_CHECKING ? "Buscando..." : "El sistema esta al dia"));
    txt(U.root, head, 2, c_hi(), W / 2, ty, true);
    ty += 2 * 10 + 10;
    char l[72];
    snprintf(l, sizeof(l), "%s %s", OTA_PLAT_NAME, flex_ota_local_version());
    txt(U.root, l, 1, c_lo(), W / 2, ty, true);
    y += card_h + 14;
    if (upd && i.remote[0]) {
        int dh = 78;
        lv_obj_t *d = box(U.root, OTA_M, y, W - 2 * OTA_M, dh);
        fill(d, c_card(), OTA_RAD);
        txt(U.root, "Version disponible", 1, c_lo(), OTA_M + 20, y + 16, false);
        lv_obj_t *r = txt(U.root, i.remote, 1, c_accent(), 0, y + 16, false);
        lv_obj_set_x(r, W - OTA_M - 20 - (int)flex_text_width(i.remote, flex_ui_font(1)));
        if (i.total > 0) {
            char sz[24];
            fmt_bytes(i.total, sz, sizeof(sz));
            txt(U.root, "Tamano", 1, c_lo(), OTA_M + 20, y + 44, false);
            r = txt(U.root, sz, 1, c_hi(), 0, y + 44, false);
            lv_obj_set_x(r, W - OTA_M - 20 - (int)flex_text_width(sz, flex_ui_font(1)));
        }
        y += dh + 14;
    } else if (s == FLEX_OTA_ERROR) {
        int dh = 56;
        lv_obj_t *d = box(U.root, OTA_M, y, W - 2 * OTA_M, dh);
        fill(d, c_card(), OTA_RAD);
        txt(U.root, flex_ota_err_text(i.error), 1, c_err(), W / 2, y + 20, true);
        y += dh + 14;
    }
    int btn_y = H - OTA_BTN_H - 40;
    if (upd && i.changelog[0]) {
        int log_top = y + 30, log_bot = btn_y - 18;
        if (log_bot > log_top + 24) {
            lv_obj_t *c = box(U.root, OTA_M, y, W - 2 * OTA_M, log_bot - y + 8);
            fill(c, c_card(), OTA_RAD);
            txt(U.root, "Novedades", 1, c_hi(), OTA_M + 20, y + 14, false);
            changelog_view(U.root, OTA_M + 20, log_top, W - 2 * OTA_M - 40, log_bot - log_top, i.changelog);
        }
    }
    bool busy = flex_ota_busy();
    if (upd && !busy) {
        int bw = (W - 2 * OTA_M - 12) / 2;
        button(U.root, BT_CHECK, OTA_M, btn_y, bw, OTA_BTN_H, "Buscar", false);
        button(U.root, BT_INSTALL, OTA_M + bw + 12, btn_y, bw, OTA_BTN_H, "Actualizar ahora", true);
    } else {
        int bw = W - 2 * OTA_M;
        if (busy) {
            txt(U.root, flex_ota_status_text(), 2, c_lo(), W / 2, btn_y + (OTA_BTN_H - 16) / 2, true);
        } else {
            button(U.root, BT_CHECK, OTA_M, btn_y, bw, OTA_BTN_H, "Buscar actualizacion", true);
        }
    }
}

// ---- control de capas ----
static void show(ov_t ov)
{
    clear_ui();
    U.ov = ov;
    U.t0 = lv_tick_get();
    U.closing = false;
    U.shown_pct = 255;
    U.shown_st = FLEX_OTA_IDLE;
    U.builds++;
    flex_touch_drop_all();   // el toque que abrio la capa no se filtra
    switch (ov) {
    case OV_TOAST: build_toast(); break;
    case OV_CHANGELOG: build_modal(); break;
    case OV_PROGRESS: build_progress(); break;
    case OV_SETTINGS: build_settings(); break;
    default: break;
    }
    flex_ota_info_t i;
    flex_ota_get(&i);
    U.shown_st = i.state;
}

static void close_overlay(void)
{
    if (U.ov == OV_HIDDEN) {
        return;
    }
    clear_ui();
    U.ov = OV_HIDDEN;
    U.closing = false;
    flex_touch_drop_all();
    flex_app_settings_refresh();   // la fila de Ajustes > Actualizaciones ya dice otra cosa
}

static void btn_click(lv_event_t *e)
{
    int id = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
    switch (id) {
    case BT_TOAST:
        show(OV_CHANGELOG);   // pulsar el aviso abre el modal
        break;
    case BT_INSTALL:
        flex_ota_request_install();   // el servicio avisa a esta capa (flex_ota_ui_install_started) y se abre la pantalla de instalacion
        break;
    case BT_CHECK: {
        flex_ota_request_check();
        if (U.ov == OV_SETTINGS) {
            show(OV_SETTINGS);
        }
        break;
    }
    case BT_CANCEL:
        if (U.ov == OV_PROGRESS) {
            flex_ota_cancel();
        } else {
            close_overlay();
        }
        break;
    case BT_CLOSE:
    case BT_BACK:
        close_overlay();
        break;
    default:
        break;
    }
}

// el servicio pide una instalacion: pantalla de progreso
void flex_ota_ui_install_started(void)
{
    show(OV_PROGRESS);
}

static bool home_now(void)
{
    return flex_shell_state() == FLEX_SH_HOME;
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t now = lv_tick_get();
    flex_ota_auto_check_tick();   // la comprobacion automatica: ~20 s tras el arranque y una sola vez
    flex_ota_info_t i;
    flex_ota_get(&i);
    // el aviso aparece solo al detectarse una version nueva, y solo en el escritorio
    if (U.ov == OV_HIDDEN && i.state == FLEX_OTA_AVAILABLE && i.has_update && home_now() && strcmp(U.toast_ver, i.remote) != 0) {
        snprintf(U.toast_ver, sizeof(U.toast_ver), "%s", i.remote);
        show(OV_TOAST);
        return;
    }
    switch (U.ov) {
    case OV_TOAST: {
        if (!home_now()) {   // sin escritorio ya no hay donde se vea bien
            close_overlay();
            return;
        }
        if (!U.closing && now - U.t0 > OTA_TOAST_MS) {
            U.closing = true;
            U.close_t0 = now;
        }
        float p;
        if (U.closing) {
            if (now - U.close_t0 > OTA_ANIM_OUT) {
                close_overlay();
                return;
            }
            p = 1.0f - (float)(now - U.close_t0) / (float)OTA_ANIM_OUT;
        } else {
            p = (float)(now - U.t0) / (float)OTA_ANIM_IN;
        }
        p = p < 0 ? 0 : (p > 1 ? 1 : p);
        float e = ease_out(p);
        int y_end = H - OTA_TOAST_GAP - OTA_TOAST_H;
        if (U.card) {
            lv_obj_set_y(U.card, y_end + (int)((1.0f - e) * (OTA_TOAST_H + OTA_TOAST_GAP)));   // entra deslizando desde abajo
        }
        break;
    }
    case OV_CHANGELOG: {
        if (U.card && now - U.t0 < OTA_ANIM_IN + 40) {
            float p = (float)(now - U.t0) / (float)OTA_ANIM_IN;
            p = p > 1 ? 1 : p;
            int ch = OTA_MODAL_H > H - 80 ? H - 80 : OTA_MODAL_H;
            lv_obj_set_y(U.card, (H - ch) / 2 + (int)((1.0f - ease_out(p)) * 40));   // sube 40 px al aparecer
        }
        if (i.state != U.shown_st) {   // cambio de estado (p. ej. una comprobacion que termina): se rehace
            show(OV_CHANGELOG);
        }
        break;
    }
    case OV_PROGRESS:
        // la pantalla de instalacion sigue al estado del motor: un cambio de fase rehace el marco; un avance solo cambia la banda dinamica
        if (i.state != U.shown_st) {
            show(OV_PROGRESS);
            flex_ota_get(&i);
        } else if (i.percent != U.shown_pct || i.done != U.shown_done || i.bps != U.shown_bps) {
            U.shown_pct = i.percent;
            U.shown_done = i.done;
            U.shown_bps = i.bps;
            progress_dynamic(&i);
        }
        break;
    case OV_SETTINGS:
        if (i.state != U.shown_st) {
            show(OV_SETTINGS);
        }
        break;
    default:
        break;
    }
}

// ---- lo que ve el resto del sistema ----
void flex_ota_open_settings(void)
{
    show(OV_SETTINGS);
}

bool flex_ota_ui_owns_screen(void)
{
    return U.ov != OV_HIDDEN && U.ov != OV_TOAST;   // la tarjeta es pequena y vive sobre el escritorio: no cuenta
}

bool flex_ota_ui_overlay_active(void)
{
    return U.ov != OV_HIDDEN;
}

// El bloqueo automatico no actua con una actualizacion en marcha ni con una capa OTA a pantalla completa (Lock.h:381): una descarga no genera toques
bool flex_power_autolock_vetoed(void)
{
    return flex_ota_busy() || flex_ota_ui_owns_screen();
}

bool flex_ota_ui_busy(void)   // la instalacion trabajando: modal (Atras, Inicio y Recientes no hacen nada)
{
    if (U.ov != OV_PROGRESS) {
        return false;
    }
    flex_ota_info_t i;
    flex_ota_get(&i);
    return i.state != FLEX_OTA_ERROR;
}

void flex_ota_ui_back(void)
{
    if (U.ov == OV_HIDDEN || U.ov == OV_TOAST || flex_ota_ui_busy()) {
        return;
    }
    close_overlay();   // modal / ajustes / error: Atras sale
}

void flex_ota_ui_close_now(void)
{
    if (flex_ota_ui_busy()) {
        return;   // una instalacion en marcha no se cierra por bloquear o suspender: se queda y sigue
    }
    close_overlay();
}

void flex_ota_ui_start(void)
{
    if (!U.tmr) {
        U.tmr = lv_timer_create(tick_cb, 33, NULL);
    }
}

// ---- pruebas ----
int flex_ota_ui_overlay(void) { return (int)U.ov; }
int flex_ota_ui_builds(void) { return U.builds; }
bool flex_ota_ui_btn_xy(int id, int *x, int *y)
{
    if (id < 0 || id >= 8 || !U.btn_has[id]) {
        return false;
    }
    *x = U.btn_xy[id][0];
    *y = U.btn_xy[id][1];
    return true;
}
static bool find_text(lv_obj_t *o, const char *t)
{
    if (lv_obj_check_type(o, &lv_label_class) && !strcmp(lv_label_get_text(o), t)) {
        return true;
    }
    for (uint32_t k = 0; k < lv_obj_get_child_count(o); k++) {
        if (find_text(lv_obj_get_child(o, (int32_t)k), t)) {
            return true;
        }
    }
    return false;
}
bool flex_ota_ui_shown(const char *t) { return U.root && find_text(U.root, t); }
int flex_ota_ui_card_y(void) { return U.card ? (int)lv_obj_get_y(U.card) : -1; }
int flex_ota_ui_log_scroll(void) { return s_logview ? (int)lv_obj_get_scroll_y(s_logview) : -1; }
int flex_ota_ui_log_max(void) { return s_logview ? (int)lv_obj_get_scroll_bottom(s_logview) + (int)lv_obj_get_scroll_y(s_logview) : -1; }
