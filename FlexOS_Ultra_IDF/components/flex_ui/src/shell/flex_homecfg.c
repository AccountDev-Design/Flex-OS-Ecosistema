// Flex OS Ultra · Personalizar inicio (docs/spec/01a §10; Arduino: FlexOS_Ultra_HomeCfg.h).
//
// Administrador de paginas, fondo, tema, widgets y ajustes del escritorio. Se abre con la
// pulsacion larga de 650 ms en un hueco vacio (flex_home.c) o con el pellizco de dos dedos
// hacia dentro (hook del tactil, antes de LVGL). El escritorio real se reduce hasta la
// tarjeta central (216x360 en y = 150, 180 ms) sobre el fondo velado; la salida es la inversa.
// Toda la logica (paginas, rejilla, reset, reordenar, pellizco) esta en flex_home_model y se
// prueba contra el codigo Arduino; aqui solo hay LVGL.
//
// Sin hacer todavia (depende del JPEG por hardware, Fase 9): usar una imagen propia como
// fondo. "Mis imagenes" lista los JPEG pero "Usar" avisa de que no se puede.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flex_app.h"
#include "flex_frame.h"
#include "flex_glass.h"
#include "flex_home_model.h"
#include "flex_home_wg.h"
#include "flex_icons.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_touch.h"
#include "flex_touch_lvgl.h"
#include "flex_wallmgr.h"
#include "flex_wallpaper.h"

#define CARD_W   216
#define CARD_H   360
#define CARD_X   ((480 - CARD_W) / 2)
#define CARD_Y   150
#define STEP     (CARD_W + 24)
#define DOTS_Y   (CARD_Y + CARD_H + 42)
#define BAR_Y    590
#define BAR_H    112
#define LIST_TOP 128
#define LIST_BOT 700
#define ANIM_MS  180
#define PV_W     100
#define PV_H     166
#define WSHEET_Y 506
#define LP_MS    650

enum { V_PAGES = 0, V_WALL, V_THEME, V_WIDGETS, V_SETTINGS, V_PICK };
enum { M_NONE = 0, M_DELPAGE, M_RESET, M_INFO };
enum { G_NONE = 0, G_HOUSE_FILL, G_HOUSE_LINE, G_TRASH, G_PLUS, G_WALL, G_THEME, G_WIDGETS };

static struct {
    lv_obj_t *root, *bg, *content, *modal, *anim_img, *layer;   // layer: capa de toque de Paginas
    lv_obj_t *card[FLEX_HOME_PAGES_MAX + 1];
    lv_obj_t *list;                                               // lista con scroll (vistas de lista)
    uint8_t view, modal_kind;
    int page_view;                  // pagina centrada (page_n = tarjeta "+")
    float slide;
    bool drag, drag_moved, active, ignore;
    int drag_x0;
    float drag_base;
    int reorder;
    uint8_t anim;                   // 0 quieto · 1 entrando · 2 saliendo
    lv_draw_buf_t *snap;
    char msg[72];
    int wall_sel, theme_sel, wg_sel, pick_sel;
    int pick_n;
    char pick_path[FLEX_HC_PICK_MAX][FLEX_HC_PATH_MAX];
    lv_point_t p0;
    uint32_t t0;
    bool lp_armed;
    uint16_t *wall_thumb;           // fondo de Inicio a 216x360
    uint16_t *page_thumb[FLEX_HOME_PAGES_MAX];
    lv_image_dsc_t thumb_dsc[FLEX_HOME_PAGES_MAX];
    lv_image_dsc_t wall_dsc;
    bool thumbs_ok;
    uint16_t *pv[FLEX_WALL_N];      // miniaturas del selector de fondos
    lv_image_dsc_t pv_dsc[FLEX_WALL_N];
    int pv_next;
    lv_timer_t *pv_timer;
    lv_obj_t *pv_img[FLEX_WALL_N];
    flex_pz_t pz;
} H = {.reorder = -1, .wall_sel = -1, .theme_sel = -1, .wg_sel = -1, .pick_sel = -1};

static void show_view(int v);
static void open_view(int v);
static void begin_exit(void);
static void build_modal(void);

// ---- ayudas de maquetacion ------------------------------------------------------------------
static lv_obj_t *part(lv_obj_t *p, int x, int y, int w, int h, int r, lv_color_t c, lv_opa_t a)
{
    lv_obj_t *o = flex_box(p);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, a, 0);
    lv_obj_set_clickable(o, false);
    return o;
}

static lv_obj_t *panel(lv_obj_t *p, int x, int y, int w, int h, int r, lv_opa_t a)
{
    lv_obj_t *o = part(p, x, y, w, h, r, flex_th()->surf, a);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, flex_th()->border, 0);
    return o;
}

static lv_obj_t *label_c(lv_obj_t *p, const char *s, const lv_font_t *f, lv_color_t c, int cx, int y)
{
    lv_obj_t *l = flex_label(p, s, f, c);
    flex_label_cap_center(l, cx, y);
    return l;
}

static lv_obj_t *label_at(lv_obj_t *p, const char *s, const lv_font_t *f, lv_color_t c, int x, int y, int clip_w)
{
    lv_obj_t *l = flex_label(p, s, f, c);
    lv_obj_set_x(l, x);
    if (clip_w > 0) {
        flex_label_one_line(l, clip_w);
    }
    flex_label_cap_y(l, y);
    return l;
}

static lv_obj_t *hit(lv_obj_t *p, int x, int y, int w, int h, lv_event_cb_t cb, intptr_t ud)
{
    lv_obj_t *o = flex_box(p);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_clickable(o, true);
    lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, (void *)ud);
    return o;
}

// Boton en capsula (hcBtn): primario = acento del fondo, secundario = TH_SURF2
static lv_obj_t *btn(lv_obj_t *p, int x, int y, int w, int h, const char *s, bool primary, lv_event_cb_t cb, intptr_t ud)
{
    lv_color_t bg = primary ? flex_accent() : flex_th()->surf2;
    lv_obj_t *b = part(p, x, y, w, h, h / 2, bg, LV_OPA_COVER);
    lv_obj_set_clickable(b, true);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)ud);
    label_c(b, s, FLEX_FONT_S2, flex_on_color_lv(bg), w / 2, (h - 20) / 2 + 3);
    return b;
}

// Fila de lista (hcRow): 0 = valor + chevron, 1 = interruptor, 2 = accion
static lv_obj_t *row(lv_obj_t *p, int y, int h, const char *title, const char *value, int kind, bool on, lv_event_cb_t cb,
                     intptr_t ud)
{
    const flex_palette_t *t = flex_th();
    lv_obj_t *r = panel(p, 16, y, 448, h, 16, 235);
    lv_obj_set_clickable(r, cb != NULL);
    if (cb) {
        lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, (void *)ud);
    }
    label_at(r, title, FLEX_FONT_S2, t->txt, 16, (h - 20) / 2 + 3, 360);
    if (kind == 1) {
        int sw = 46, sx = 448 - 16 - sw, sy = (h - 26) / 2;
        lv_obj_t *tr = part(r, sx, sy, sw, 26, 13, on ? flex_accent() : t->track, LV_OPA_COVER);
        part(tr, on ? sw - 23 : 3, 3, 20, 20, 10, lv_color_white(), LV_OPA_COVER);
    } else if (value && *value) {
        lv_obj_t *v = flex_label(r, value, FLEX_FONT_S2, t->txt2);
        lv_obj_set_pos(v, 448 - 38 - flex_text_width(value, FLEX_FONT_S2), (h - 18) / 2);
        if (kind == 0) {
            lv_obj_t *g = flex_glyph_create(r, FLEX_GLYPH_CHEVRON_RIGHT, 14, 14);
            lv_obj_set_pos(g, 448 - 16 - 14, (h - 14) / 2);
            lv_obj_set_style_text_color(g, t->txt2, 0);
        }
    }
    return r;
}

// ---- iconos propios (primitivas, nada importado) ----------------------------------------------
static void tri(lv_layer_t *l, lv_color_t c, int x0, int y0, int x1, int y1, int x2, int y2)
{
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = c;
    d.opa = LV_OPA_COVER;
    d.p[0].x = x0;
    d.p[0].y = y0;
    d.p[1].x = x1;
    d.p[1].y = y1;
    d.p[2].x = x2;
    d.p[2].y = y2;
    lv_draw_triangle(l, &d);
}

static void rect(lv_layer_t *l, lv_color_t c, int x, int y, int w, int h, int r, int bw)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = r;
    if (bw) {
        d.bg_opa = LV_OPA_TRANSP;
        d.border_color = c;
        d.border_width = bw;
        d.border_opa = LV_OPA_COVER;
    } else {
        d.bg_color = c;
        d.bg_opa = LV_OPA_COVER;
    }
    lv_area_t a = {x, y, x + w - 1, y + h - 1};
    lv_draw_rect(l, &d, &a);
}

static void glyph_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    lv_layer_t *l = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    int cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
    lv_color_t c = lv_obj_get_style_text_color(o, 0);
    switch ((int)(intptr_t)lv_event_get_user_data(e)) {
    case G_HOUSE_FILL:
    case G_HOUSE_LINE: {   // casita de 15 px: tejado + cuerpo (hcIcoHome)
        int s = 15, h = s / 2, mid = cy - s / 6, bh = cy + h - mid;
        tri(l, c, cx, cy - h, cx - h - 2, mid, cx + h + 2, mid);
        if ((int)(intptr_t)lv_event_get_user_data(e) == G_HOUSE_FILL) {
            rect(l, c, cx - h + 2, mid, s - 4, bh, 0, 0);
        } else {
            rect(l, c, cx - h + 2, mid, s - 4, bh, 0, 2);
        }
        break;
    }
    case G_TRASH:
        rect(l, c, cx - 8, cy - 9, 16, 2, 0, 0);
        rect(l, c, cx - 3, cy - 12, 6, 2, 0, 0);
        rect(l, c, cx - 6, cy - 6, 12, 15, 0, 1);
        rect(l, c, cx - 2, cy - 3, 1, 9, 0, 0);
        rect(l, c, cx + 2, cy - 3, 1, 9, 0, 0);
        break;
    case G_PLUS:
        rect(l, c, cx - 28, cy - 3, 56, 6, 3, 0);
        rect(l, c, cx - 3, cy - 28, 6, 56, 3, 0);
        break;
    case G_WALL:
        rect(l, c, cx - 15, cy - 12, 30, 24, 5, 2);
        rect(l, c, cx - 10, cy - 8, 6, 6, 3, 0);
        tri(l, c, cx - 12, cy + 10, cx - 1, cy - 3, cx + 9, cy + 10);
        tri(l, c, cx + 1, cy + 10, cx + 8, cy + 1, cx + 14, cy + 10);
        break;
    case G_THEME:
        rect(l, c, cx - 13, cy - 13, 26, 26, 13, 2);
        rect(l, c, cx - 7, cy - 7, 4, 4, 2, 0);
        rect(l, c, cx + 2, cy - 8, 4, 4, 2, 0);
        rect(l, c, cx + 5, cy, 4, 4, 2, 0);
        rect(l, c, cx - 8, cy + 2, 4, 4, 2, 0);
        break;
    case G_WIDGETS:
        rect(l, c, cx - 13, cy - 13, 12, 12, 3, 2);
        rect(l, c, cx + 1, cy - 13, 12, 12, 3, 2);
        rect(l, c, cx - 13, cy + 1, 12, 12, 3, 2);
        rect(l, c, cx + 1, cy + 1, 12, 12, 3, 0);
        break;
    default:
        break;
    }
}

static lv_obj_t *glyph(lv_obj_t *p, int kind, int cx, int cy, int size, lv_color_t c)
{
    lv_obj_t *o = flex_box(p);
    lv_obj_set_pos(o, cx - size / 2, cy - size / 2);
    lv_obj_set_size(o, size, size);
    lv_obj_set_style_text_color(o, c, 0);
    lv_obj_add_event_cb(o, glyph_cb, LV_EVENT_DRAW_MAIN_END, (void *)(intptr_t)kind);
    return o;
}

// ---- fondo velado y miniaturas ------------------------------------------------------------------
static void free_thumbs(void)
{
    free(H.wall_thumb);
    H.wall_thumb = NULL;
    for (int i = 0; i < FLEX_HOME_PAGES_MAX; i++) {
        free(H.page_thumb[i]);
        H.page_thumb[i] = NULL;
    }
    H.thumbs_ok = false;
}

static void init_dsc(lv_image_dsc_t *d, const uint16_t *data, int w, int h)
{
    memset(d, 0, sizeof(*d));
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_RGB565;
    d->header.w = (uint32_t)w;
    d->header.h = (uint32_t)h;
    d->header.stride = (uint32_t)w * 2;
    d->data = (const uint8_t *)data;
    d->data_size = (uint32_t)(w * h * 2);
}

// Pagina p a 216x360: fondo de Inicio escalado al vecino mas cercano + el contenido REAL de la
// pagina (iconos y widgets de la pantalla) reducido de la misma forma (hcDrawThumb).
static void build_thumbs(void)
{
    const lv_image_dsc_t *wall = flex_wallmgr_image(FLEX_WALL_HOME);
    if (!wall) {
        return;
    }
    free_thumbs();
    size_t n = (size_t)CARD_W * CARD_H;
    H.wall_thumb = malloc(n * 2);
    if (!H.wall_thumb) {
        return;
    }
    const uint16_t *src = (const uint16_t *)wall->data;
    for (int y = 0; y < CARD_H; y++) {
        const uint16_t *srow = src + (size_t)((y * 800) / CARD_H) * 480;
        for (int x = 0; x < CARD_W; x++) {
            H.wall_thumb[(size_t)y * CARD_W + x] = srow[(x * 480) / CARD_W];
        }
    }
    init_dsc(&H.wall_dsc, H.wall_thumb, CARD_W, CARD_H);
    flex_home_rebuild();   // el modelo puede haber cambiado: el escritorio real se reconstruye
    for (int p = 0; p < g_home.page_n; p++) {
        H.page_thumb[p] = malloc(n * 2);
        if (!H.page_thumb[p]) {
            continue;
        }
        memcpy(H.page_thumb[p], H.wall_thumb, n * 2);
        lv_obj_t *pg = flex_home_page_obj(p);
        lv_draw_buf_t *snap = pg ? lv_snapshot_take(pg, LV_COLOR_FORMAT_ARGB8888) : NULL;
        if (snap) {
            int sw = (int)snap->header.w, sh = (int)snap->header.h, stride = (int)snap->header.stride;
            lv_area_t ca;
            lv_obj_get_coords(pg, &ca);
            int y0 = ca.y1 * CARD_H / 800;
            for (int y = 0; y < sh * CARD_H / 800 && y0 + y < CARD_H; y++) {
                const uint8_t *srow = snap->data + (size_t)((y * 800) / CARD_H) * (size_t)stride;
                for (int x = 0; x < CARD_W; x++) {
                    const uint8_t *px = srow + (size_t)((x * sw) / CARD_W) * 4;   // B G R A
                    uint8_t a = px[3];
                    if (!a) {
                        continue;
                    }
                    uint16_t *d = &H.page_thumb[p][(size_t)(y0 + y) * CARD_W + x];
                    uint16_t s565 = (uint16_t)(((px[2] & 0xF8) << 8) | ((px[1] & 0xFC) << 3) | (px[0] >> 3));
                    *d = a == 255 ? s565 : flex_mix565(*d, s565, a);
                }
            }
            lv_draw_buf_destroy(snap);
        }
        init_dsc(&H.thumb_dsc[p], H.page_thumb[p], CARD_W, CARD_H);
    }
    H.thumbs_ok = true;
}

// ---- tarjetas de pagina -----------------------------------------------------------------------
static int total_cards(void)
{
    return g_home.page_n + ((g_home.page_n < FLEX_HOME_PAGES_MAX && !g_home.locked) ? 1 : 0);
}

static int card_x(int i)
{
    int base = CARD_X + (i - H.page_view) * STEP;
    if (H.reorder >= 0) {
        return base - (i == H.reorder ? (int)H.slide : 0);
    }
    return base - (int)H.slide;
}

static void layout_cards(void)
{
    for (int i = 0; i < FLEX_HOME_PAGES_MAX + 1; i++) {
        if (!H.card[i]) {
            continue;
        }
        int x = card_x(i);
        lv_obj_set_x(H.card[i], x);
        lv_obj_set_hidden(H.card[i], x > 480 || x + CARD_W < 0);
        bool center = i == H.page_view;
        if (i < g_home.page_n) {
            lv_obj_t *frame = lv_obj_get_child(H.card[i], 2);
            lv_obj_set_style_border_color(frame, center ? flex_accent() : flex_th()->border, 0);
            lv_obj_set_style_border_width(frame, center ? 2 : 1, 0);
        }
    }
}

static void dots_build(lv_obj_t *p)
{
    int n = g_home.page_n, step = 26, x0 = 240 - (n - 1) * step / 2;
    for (int i = 0; i < n; i++) {
        int x = x0 + i * step;
        lv_color_t c = i == H.page_view ? flex_accent() : FLEX_ONWALL;
        if (i == g_home.main) {
            glyph(p, G_HOUSE_FILL, x, DOTS_Y, 24, c);
        } else {
            part(p, x - 5, DOTS_Y - 5, 10, 10, 5, c, i == H.page_view ? 255 : 140);
        }
    }
}

static void bar_build(lv_obj_t *p)
{
    static const char *const LBL[4] = {"Fondo", "Temas", "Widgets", "Ajustes"};
    static const int G[4] = {G_WALL, G_THEME, G_WIDGETS, 0};
    lv_obj_t *b = panel(p, 12, BAR_Y, 456, BAR_H, 26, 225);
    int iw = (480 - 24) / 4;
    for (int i = 0; i < 4; i++) {
        int cx = iw * i + iw / 2;
        lv_color_t c = flex_th()->txt;
        if (G[i]) {
            glyph(b, G[i], cx, 40, 34, c);
        } else {
            lv_obj_t *g = flex_glyph_create(b, FLEX_GLYPH_GEAR, 32, 32);
            lv_obj_set_pos(g, cx - 16, 24);
            lv_obj_set_style_text_color(g, c, 0);
        }
        label_c(b, LBL[i], FLEX_FONT_S2, c, cx, 66);
    }
}

static void build_pages(void)
{
    lv_obj_clean(H.content);
    memset(H.card, 0, sizeof(H.card));
    if (!H.thumbs_ok) {
        build_thumbs();
    }
    char sub[40];
    snprintf(sub, sizeof(sub), "P\xC3\xA1gina %d de %d", (H.page_view < g_home.page_n ? H.page_view : g_home.page_n - 1) + 1,
             g_home.page_n);
    label_c(H.content, "Personalizar inicio", FLEX_FONT_S3, FLEX_ONWALL, 240, 66);
    label_c(H.content, sub, FLEX_FONT_S2, FLEX_ONWALL2, 240, 98);
    int total = total_cards();
    for (int i = 0; i < total; i++) {
        lv_obj_t *c = flex_box(H.content);
        lv_obj_set_pos(c, card_x(i), CARD_Y);
        lv_obj_set_size(c, CARD_W, CARD_H);
        lv_obj_set_style_radius(c, 26, 0);
        lv_obj_set_style_clip_corner(c, true, 0);
        H.card[i] = c;
        if (i < g_home.page_n) {
            lv_obj_t *img = lv_image_create(c);
            lv_obj_set_pos(img, 0, 0);
            lv_image_set_src(img, H.page_thumb[i] ? &H.thumb_dsc[i] : (H.wall_thumb ? &H.wall_dsc : NULL));
            part(c, 24 * CARD_W / 480, (800 - 176) * CARD_H / 800, 432 * CARD_W / 480, 96 * CARD_H / 800, 8,
                 lv_color_white(), 60);
            lv_obj_t *frame = part(c, 0, 0, CARD_W, CARD_H, 26, lv_color_black(), LV_OPA_TRANSP);
            lv_obj_set_style_border_width(frame, 1, 0);
            lv_obj_t *chip = part(c, CARD_W - 26 - 16, 26 - 16, 32, 32, 16, flex_th()->scrim, 140);
            bool main = g_home.main == i;
            glyph(chip, main ? G_HOUSE_FILL : G_HOUSE_LINE, 16, 19, 24, main ? lv_color_make(255, 214, 80) : FLEX_ONWALL);
            if (g_home.page_n > 1 && !g_home.locked) {
                lv_obj_t *tc = part(c, 26 - 16, 26 - 16, 32, 32, 16, flex_th()->scrim, 140);
                glyph(tc, G_TRASH, 16, 18, 24, lv_color_make(252, 214, 214));
            }
            char nb[24];
            snprintf(nb, sizeof(nb), "P\xC3\xA1gina %d", i + 1);
            lv_obj_t *l = flex_label(H.content, nb, FLEX_FONT_S2, FLEX_ONWALL);
            flex_label_cap_center(l, card_x(i) + CARD_W / 2, CARD_Y + CARD_H + 6);
            lv_obj_set_user_data(c, l);
        } else {   // tarjeta "+"
            part(c, 0, 0, CARD_W, CARD_H, 26, lv_color_white(), 34);
            lv_obj_t *frame = part(c, 0, 0, CARD_W, CARD_H, 26, lv_color_black(), LV_OPA_TRANSP);
            lv_obj_set_style_border_width(frame, 1, 0);
            lv_obj_set_style_border_color(frame, FLEX_ONWALL2, 0);
            glyph(c, G_PLUS, CARD_W / 2, CARD_H / 2, 64, FLEX_ONWALL);
            lv_obj_t *l = flex_label(H.content, "Nueva p\xC3\xA1gina", FLEX_FONT_S2, FLEX_ONWALL);
            flex_label_cap_center(l, card_x(i) + CARD_W / 2, CARD_Y + CARD_H + 6);
            lv_obj_set_user_data(c, l);
        }
    }
    layout_cards();
    dots_build(H.content);
    bar_build(H.content);
    // la capa de toque va la ultima: ve todo y decide por geometria, como hcPagesTouch
    H.layer = flex_box(H.content);
    lv_obj_set_size(H.layer, 480, 720);
    lv_obj_set_clickable(H.layer, true);
}

// ---- pagina: gestos de la capa de toque ------------------------------------------------------
static bool in(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

static void refresh_pages(void)
{
    H.thumbs_ok = false;
    show_view(V_PAGES);
}

static void info(const char *m)
{
    snprintf(H.msg, sizeof(H.msg), "%s", m);
    H.modal_kind = M_INFO;
    build_modal();
}

static void pages_tap(int x, int y)
{
    int total = total_cards();
    if (y >= BAR_Y && y <= BAR_Y + BAR_H && x >= 12 && x <= 468) {
        int b = (x - 12) / ((480 - 24) / 4);
        open_view(b == 0 ? V_WALL : b == 1 ? V_THEME : b == 2 ? V_WIDGETS : V_SETTINGS);
        return;
    }
    int n = g_home.page_n, step = 26, x0 = 240 - (n - 1) * step / 2;
    if (y >= DOTS_Y - 18 && y <= DOTS_Y + 18) {
        for (int i = 0; i < n; i++) {
            if (LV_ABS(x - (x0 + i * step)) <= 13) {
                H.page_view = i;
                H.slide = 0;
                show_view(V_PAGES);
                return;
            }
        }
    }
    int hy = CARD_Y + 26;
    for (int i = H.page_view - 1; i <= H.page_view + 1; i++) {   // casita -> pagina principal
        if (i < 0 || i >= g_home.page_n) {
            continue;
        }
        if (LV_ABS(x - (card_x(i) + CARD_W - 26)) <= 20 && LV_ABS(y - hy) <= 20) {
            flex_hc_set_main(i);
            refresh_pages();
            return;
        }
    }
    if (H.page_view < g_home.page_n && g_home.page_n > 1 && !g_home.locked &&
        LV_ABS(x - (card_x(H.page_view) + 26)) <= 20 && LV_ABS(y - hy) <= 20) {
        H.modal_kind = M_DELPAGE;
        build_modal();
        return;
    }
    if (total > g_home.page_n && in(x, y, card_x(g_home.page_n), CARD_Y, CARD_W, CARD_H)) {   // tarjeta "+"
        if (flex_hc_page_add() == FLEX_HC_OK) {
            H.page_view = g_home.page_n - 1;
            H.slide = 0;
        } else {
            info("Maximo de 5 paginas");
            return;
        }
        refresh_pages();
        return;
    }
    for (int i = H.page_view - 1; i <= H.page_view + 1; i += 2) {   // una vecina: se centra
        if (i >= 0 && i < total && in(x, y, card_x(i), CARD_Y, CARD_W, CARD_H)) {
            H.page_view = i;
            H.slide = 0;
            layout_cards();
            show_view(V_PAGES);
            return;
        }
    }
}

static void layer_cb(lv_event_t *e)
{
    if (H.ignore || H.anim || H.modal_kind != M_NONE) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    int total = total_cards();
    if (code == LV_EVENT_PRESSED) {
        H.p0 = p;
        H.t0 = lv_tick_get();
        H.lp_armed = true;
        if (p.y >= CARD_Y - 40 && p.y <= CARD_Y + CARD_H + 40) {
            H.drag = true;
            H.drag_moved = false;
            H.drag_x0 = p.x;
            H.drag_base = H.slide;
        }
    } else if (code == LV_EVENT_PRESSING) {
        // reordenar: pulsacion larga sobre la tarjeta centrada (no bloqueado)
        if (!g_home.locked && H.reorder < 0 && H.lp_armed && !H.drag_moved && H.page_view < g_home.page_n &&
            lv_tick_elaps(H.t0) > LP_MS && LV_ABS(p.x - H.p0.x) <= 12 && LV_ABS(p.y - H.p0.y) <= 12 &&
            in(H.p0.x, H.p0.y, card_x(H.page_view), CARD_Y, CARD_W, CARD_H)) {
            H.reorder = H.page_view;
            H.drag_x0 = p.x;
            H.drag = false;
            H.slide = 0;
            H.lp_armed = false;
            return;
        }
        if (H.reorder >= 0) {
            H.slide = (float)(H.drag_x0 - p.x);
            layout_cards();
            return;
        }
        if (H.drag) {
            int dx = p.x - H.drag_x0;
            if (!H.drag_moved) {
                if (LV_ABS(dx) < 14) {
                    return;
                }
                H.drag_moved = true;
                H.lp_armed = false;
            }
            float sv = H.drag_base - dx, lim = (float)STEP * 0.42f;
            if (H.page_view == 0 && sv < -lim) {
                sv = -lim;
            }
            if (H.page_view >= total - 1 && sv > lim) {
                sv = lim;
            }
            H.slide = sv;
            layout_cards();
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        H.lp_armed = false;
        if (H.reorder >= 0) {
            int to = flex_hc_reorder_apply(H.reorder, H.slide, STEP);
            H.page_view = to;
            H.reorder = -1;
            H.slide = 0;
            refresh_pages();
            return;
        }
        bool moved = H.drag_moved;
        if (H.drag && moved) {
            if (H.slide > STEP / 3 && H.page_view < total - 1) {
                H.page_view++;
            } else if (H.slide < -(STEP / 3) && H.page_view > 0) {
                H.page_view--;
            }
        }
        H.drag = false;
        H.slide = 0;
        if (moved) {
            show_view(V_PAGES);
        } else if (code == LV_EVENT_RELEASED && lv_tick_elaps(H.t0) < FLEX_TOUCH_TAP_MS && LV_ABS(p.x - H.p0.x) < FLEX_TOUCH_TAP_PX &&
                   LV_ABS(p.y - H.p0.y) < FLEX_TOUCH_TAP_PX) {
            pages_tap(p.x, p.y);
        }
    }
}

// ---- modales ---------------------------------------------------------------------------------
static void modal_close(void)
{
    if (H.modal) {
        lv_obj_delete(H.modal);
        H.modal = NULL;
    }
    H.modal_kind = M_NONE;
}

static void modal_ok_cb(lv_event_t *e)
{
    (void)e;
    uint8_t m = H.modal_kind;
    modal_close();
    if (m == M_DELPAGE) {
        if (flex_hc_page_delete(H.page_view) == FLEX_HC_OK && H.page_view >= g_home.page_n) {
            H.page_view = g_home.page_n - 1;
        }
        refresh_pages();
    } else if (m == M_RESET) {
        flex_hc_reset_layout();
        H.page_view = 0;
        refresh_pages();
    }
}

static void modal_cancel_cb(lv_event_t *e)
{
    (void)e;
    modal_close();
}

static void build_modal(void)
{
    if (H.modal) {
        lv_obj_delete(H.modal);
    }
    if (H.modal_kind == M_NONE) {
        H.modal = NULL;
        return;
    }
    const flex_palette_t *t = flex_th();
    H.modal = part(H.root, 0, 0, 480, 800, 0, t->scrim, 150);
    lv_obj_set_clickable(H.modal, true);
    int w = 480 - 64, h = 200, x = 32, y = (800 - h) / 2;
    lv_obj_t *d = flex_box(H.modal);
    lv_obj_set_pos(d, x, y);
    lv_obj_set_size(d, w, h);
    lv_obj_set_style_radius(d, 26, 0);
    flex_surface(d, FLEX_SURF_ELEVATED, FLEX_BD_HOME);
    lv_obj_set_style_border_width(d, 1, 0);
    lv_obj_set_style_border_color(d, t->border, 0);
    const char *ttl = H.modal_kind == M_DELPAGE ? "Eliminar esta p\xC3\xA1gina" : H.modal_kind == M_RESET ? "Restablecer el inicio" : "Aviso";
    label_c(d, ttl, FLEX_FONT_S3, t->txt, w / 2, 26);
    if (H.modal_kind == M_INFO) {
        label_c(d, H.msg, FLEX_FONT_S2, t->txt2, w / 2, 80);
        btn(d, (w - 140) / 2, h - 62, 140, 46, "Entendido", true, modal_cancel_cb, 0);
        return;
    }
    if (H.modal_kind == M_RESET) {
        label_c(d, "Vuelve al reparto de fabrica:", FLEX_FONT_S2, t->txt2, w / 2, 70);
        label_c(d, "tres p\xC3\xA1ginas y rejilla 4x3", FLEX_FONT_S2, t->txt2, w / 2, 92);
    } else {
        label_c(d, "Sus iconos se recolocan solos", FLEX_FONT_S2, t->txt2, w / 2, 70);
        label_c(d, "en las paginas que queden", FLEX_FONT_S2, t->txt2, w / 2, 92);
    }
    int bw = (w - 3 * 16) / 2;
    btn(d, 16, h - 62, bw, 46, "Cancelar", false, modal_cancel_cb, 0);
    btn(d, 32 + bw, h - 62, bw, 46, H.modal_kind == M_RESET ? "Restablecer" : "Eliminar", true, modal_ok_cb, 0);
}

// ---- cabecera comun de las vistas secundarias -------------------------------------------------
static void back_chip_cb(lv_event_t *e);

static void chrome(const char *title, const char *sub, bool back)
{
    label_c(H.content, title, FLEX_FONT_S3, FLEX_ONWALL, 240, 66);
    if (sub && *sub) {
        label_c(H.content, sub, FLEX_FONT_S2, FLEX_ONWALL2, 240, 98);
    }
    if (back) {
        lv_obj_t *c = part(H.content, 14, 52, 96, 36, 18, flex_th()->scrim, 130);
        lv_obj_set_clickable(c, true);
        lv_obj_add_event_cb(c, back_chip_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *g = flex_glyph_create(c, FLEX_GLYPH_CHEVRON_LEFT, 18, 18);
        lv_obj_set_pos(g, 12, 9);
        lv_obj_set_style_text_color(g, FLEX_ONWALL, 0);
        label_at(c, "Atr\xC3\xA1s", FLEX_FONT_S2, FLEX_ONWALL, 38, 12, 0);
    }
}

static lv_obj_t *list_create(void)
{
    lv_obj_t *l = flex_box(H.content);
    lv_obj_set_pos(l, 0, LIST_TOP);
    lv_obj_set_size(l, 480, LIST_BOT - LIST_TOP);
    lv_obj_set_clickable(l, true);
    lv_obj_set_scrollable(l, true);
    lv_obj_set_scroll_dir(l, LV_DIR_VER);
    lv_obj_set_scroll_elastic(l, false);
    lv_obj_set_scroll_momentum(l, false);   // sin inercia
    lv_obj_set_scrollbar_mode(l, LV_SCROLLBAR_MODE_OFF);
    H.list = l;
    return l;
}

// ---- Fondo ---------------------------------------------------------------------------------
static void wall_apply(int id, int dest)   // bit0 = inicio, bit1 = bloqueo (hcApplyWall)
{
    if (id < 0 || id >= FLEX_WALL_N) {
        info("Ese fondo no esta disponible");
        return;
    }
    const flex_look_prefs_t *lp = flex_look();
    flex_theme_set_wallpapers((dest & 1) ? (uint8_t)id : lp->wall_home, (dest & 2) ? (uint8_t)id : lp->wall_lock);
    H.thumbs_ok = false;
    show_view(V_WALL);
}

static void wall_cb(lv_event_t *e)
{
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    if (k < 8) {
        H.wall_sel = k;
        show_view(V_WALL);
    } else if (k >= 20 && k <= 22) {
        wall_apply(H.wall_sel, k == 20 ? 1 : k == 21 ? 2 : 3);
    } else if (k == 30) {
        open_view(V_PICK);
    } else if (k == 31) {
        H.wall_sel = 0;
        wall_apply(0, 3);
    } else if (k == 32) {
        flex_theme_set_wall_palette(!flex_look()->wall_pal);
        H.thumbs_ok = false;
        show_view(V_WALL);
    }
}

static void pv_tick(lv_timer_t *t)
{
    (void)t;
    if (H.view != V_WALL || H.pv_next >= FLEX_WALL_N) {
        lv_timer_pause(H.pv_timer);
        return;
    }
    int i = H.pv_next++;
    uint16_t *buf = malloc((size_t)FLEX_WALL_W * FLEX_WALL_H * 2);
    if (!buf) {
        return;
    }
    if (!H.pv[i]) {
        H.pv[i] = malloc((size_t)PV_W * PV_H * 2);
    }
    if (H.pv[i]) {
        flex_wall_render(buf, i, true, 0, FLEX_WALL_H - 1);
        for (int y = 0; y < PV_H; y++) {
            const uint16_t *srow = buf + (size_t)((y * FLEX_WALL_H) / PV_H) * FLEX_WALL_W;
            for (int x = 0; x < PV_W; x++) {
                H.pv[i][(size_t)y * PV_W + x] = srow[(x * FLEX_WALL_W) / PV_W];
            }
        }
        init_dsc(&H.pv_dsc[i], H.pv[i], PV_W, PV_H);
        if (H.pv_img[i]) {
            lv_image_set_src(H.pv_img[i], &H.pv_dsc[i]);
        }
    }
    free(buf);
}

static void build_wall(void)
{
    const flex_palette_t *t = flex_th();
    const flex_look_prefs_t *lp = flex_look();
    chrome("Fondo de pantalla", "Integrados de Flex OS e im\xC3\xA1genes tuyas", true);
    for (int i = 0; i < FLEX_WALL_N; i++) {
        int x = 18 + (i % 4) * 116, y = 130 + (i / 4) * 186;
        lv_obj_t *c = hit(H.content, x, y, PV_W, PV_H, wall_cb, i);
        lv_obj_set_style_radius(c, 12, 0);
        lv_obj_set_style_clip_corner(c, true, 0);
        lv_obj_set_style_bg_color(c, t->surf, 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        lv_obj_t *img = lv_image_create(c);
        if (H.pv[i]) {
            lv_image_set_src(img, &H.pv_dsc[i]);
        }
        H.pv_img[i] = img;
        bool sel = H.wall_sel == i;
        lv_obj_t *fr = part(c, 0, 0, PV_W, PV_H, 12, lv_color_black(), LV_OPA_TRANSP);
        lv_obj_set_style_border_width(fr, sel ? 2 : 1, 0);
        lv_obj_set_style_border_color(fr, sel ? flex_accent() : t->border, 0);
        if (sel) {
            lv_obj_t *ck = part(c, PV_W - 23, 5, 18, 18, 9, flex_accent(), LV_OPA_COVER);
            lv_obj_t *g = flex_glyph_create(ck, FLEX_GLYPH_CHECK, 12, 12);
            lv_obj_set_pos(g, 3, 3);
            lv_obj_set_style_text_color(g, flex_on_color_lv(flex_accent()), 0);
        }
        if (lp->wall_home == i) {
            part(c, 7, PV_H - 17, 10, 10, 5, flex_accent(), LV_OPA_COVER);
        }
        label_c(H.content, flex_wall_names[i], FLEX_FONT_S1, FLEX_ONWALL, x + PV_W / 2, y + PV_H + 5);
    }
    lv_obj_t *sh = panel(H.content, 12, WSHEET_Y, 456, 148, 22, 235);
    if (H.wall_sel < 0) {
        label_c(sh, "Elige un fondo para ver las opciones", FLEX_FONT_S2, t->txt2, 228, 18);
    } else {
        char tb[56];
        snprintf(tb, sizeof(tb), "Aplicar \"%s\" a:", flex_wall_names[H.wall_sel]);
        label_at(sh, tb, FLEX_FONT_S2, t->txt, 16, 12, 400);
        int bw = (480 - 24 - 4 * 12) / 3;
        btn(sh, 12, 38, bw, 42, "Inicio", true, wall_cb, 20);
        btn(sh, 12 + bw + 12, 38, bw, 42, "Bloqueo", true, wall_cb, 21);
        btn(sh, 12 + 2 * (bw + 12), 38, bw, 42, "Ambos", true, wall_cb, 22);
    }
    int hw = (480 - 60) / 2;
    btn(sh, 12, 88, hw, 42, "Mis im\xC3\xA1genes", false, wall_cb, 30);
    btn(sh, 24 + hw, 88, hw, 42, "Restaurar", false, wall_cb, 31);
    row(H.content, WSHEET_Y + 158, 44, "Aplicar paleta al sistema", NULL, 1, lp->wall_pal, wall_cb, 32);
    if (!H.pv_timer) {
        H.pv_timer = lv_timer_create(pv_tick, 30, NULL);
    }
    if (H.pv_next < FLEX_WALL_N) {
        lv_timer_resume(H.pv_timer);
    }
}

// ---- Mis imagenes ---------------------------------------------------------------------------
// El listado lo hace la tarea de almacenamiento (nunca la flash desde la UI); las tres carpetas se
// piden una tras otra y, con las tres, flex_hc_scan_images arma la lista como hcScanImages.
static struct {
    int n[3];
    flex_hc_ent_t e[3][FLEX_HC_ENT_MAX];
    uint32_t gen;
    bool loading;
} PK;
static const char *const PK_DIRS[3] = {"/Imagenes", "/Camara", "/Descargas"};

static int pk_ls(void *ctx, const char *dir, flex_hc_ent_t *ents, int cap)
{
    (void)ctx;
    int d = !strcmp(dir, PK_DIRS[0]) ? 0 : !strcmp(dir, PK_DIRS[1]) ? 1 : 2;
    int n = PK.n[d] < cap ? PK.n[d] : cap;
    memcpy(ents, PK.e[d], (size_t)n * sizeof(flex_hc_ent_t));
    return n;
}

static void pk_cb(esp_err_t err, const flex_fs_ent_t *ents, size_t n, void *user)
{
    uint32_t tag = (uint32_t)(uintptr_t)user;
    int d = (int)(tag & 3);
    if ((tag >> 2) != PK.gen || !H.active) {
        return;   // la vista se cerro o se pidio otra vez
    }
    PK.n[d] = 0;
    for (size_t i = 0; err == ESP_OK && i < n && i < FLEX_HC_ENT_MAX; i++) {
        snprintf(PK.e[d][PK.n[d]].name, sizeof(PK.e[d][0].name), "%s", ents[i].name);
        PK.e[d][PK.n[d]].dir = ents[i].dir;
        PK.n[d]++;
    }
    if (d < 2) {
        flex_fs_list_async(PK_DIRS[d + 1], FLEX_HC_ENT_MAX, pk_cb, (void *)(uintptr_t)((PK.gen << 2) | (uint32_t)(d + 1)));
        return;
    }
    PK.loading = false;
    H.pick_n = flex_hc_scan_images(pk_ls, NULL, H.pick_path);
    if (H.view == V_PICK) {
        show_view(V_PICK);
    }
}

static void pick_scan_start(void)
{
    PK.gen++;
    PK.loading = true;
    H.pick_n = 0;
    if (flex_fs_list_async(PK_DIRS[0], FLEX_HC_ENT_MAX, pk_cb, (void *)(uintptr_t)(PK.gen << 2)) != ESP_OK) {
        PK.loading = false;
    }
}

static void pick_cb(lv_event_t *e)
{
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    if (k >= 1000) {   // "Usar": el JPEG por hardware llega en la Fase 9
        info("No se pudo usar esa imagen");
        return;
    }
    H.pick_sel = k;
    show_view(V_PICK);
}

static void build_pick(void)
{
    const flex_palette_t *t = flex_th();
    chrome("Mis im\xC3\xA1genes", "JPEG de /Imagenes, /Camara y /Descargas", true);
    if (PK.loading) {
        label_c(H.content, "Buscando im\xC3\xA1genes...", FLEX_FONT_S2, FLEX_ONWALL2, 240, 230);
        return;
    }
    if (H.pick_n == 0) {
        label_c(H.content, "No hay imagenes JPEG guardadas", FLEX_FONT_S2, FLEX_ONWALL2, 240, 230);
        label_c(H.content, "Guarda alguna desde Camara o Archivos", FLEX_FONT_S1, FLEX_ONWALL2, 240, 256);
        return;
    }
    lv_obj_t *l = list_create();
    for (int i = 0; i < H.pick_n; i++) {
        bool sel = H.pick_sel == i;
        lv_obj_t *r = panel(l, 16, 4 + i * 58, 448, 52, 14, sel ? 250 : 225);
        lv_obj_set_clickable(r, true);
        lv_obj_add_event_cb(r, pick_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        if (sel) {
            lv_obj_set_style_border_color(r, flex_accent(), 0);
        }
        label_at(r, H.pick_path[i], FLEX_FONT_S2, t->txt, 16, 16, 330);
        if (sel) {
            btn(r, 448 - 126 + 16 - 16, 8, 100, 36, "Usar", true, pick_cb, 1000 + i);
        }
    }
}

// ---- Temas ---------------------------------------------------------------------------------
static void theme_cb(lv_event_t *e)
{
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    if (k < FLEX_LOOK_N) {
        H.theme_sel = k;
        show_view(V_THEME);
    } else if (k == 100 || k == 102) {   // Aplicar / Restaurar tema predeterminado
        int want = k == 100 ? H.theme_sel : 0;
        H.theme_sel = want;
        if (!flex_theme_apply_look((uint8_t)want)) {
            info("No se pudo aplicar el tema");
            return;
        }
        H.thumbs_ok = false;
        show_view(V_THEME);
    } else if (k == 101) {
        H.theme_sel = -1;
        show_view(V_THEME);
    }
}

static void build_theme(void)
{
    const flex_palette_t *t = flex_th();
    chrome("Temas", "Temas integrados de Flex OS", true);
    for (int i = 0; i < FLEX_LOOK_N; i++) {
        const flex_look_t *lk = &flex_looks[i];
        bool sel = H.theme_sel == i;
        lv_obj_t *r = panel(H.content, 16, 130 + i * 54, 448, 48, 16, sel ? 250 : 225);
        lv_obj_set_clickable(r, true);
        lv_obj_add_event_cb(r, theme_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        if (sel) {
            lv_obj_set_style_border_color(r, flex_accent(), 0);
        }
        part(r, 12, 10, 28, 28, 8, lv_color_make(lk->ar, lk->ag, lk->ab), LV_OPA_COVER);
        part(r, 44, 10, 14, 28, 6, lv_color_make(lk->sr, lk->sg, lk->sb), LV_OPA_COVER);
        part(r, 62, 10, 14, 28, 6, lk->dark ? lv_color_make(30, 34, 46) : lv_color_make(240, 243, 249), LV_OPA_COVER);
        label_at(r, lk->name, FLEX_FONT_S2, t->txt, 88, 15, 0);
        if (flex_look()->look == i) {
            lv_obj_t *a = flex_label(r, "Activo", FLEX_FONT_S1, flex_accent());
            lv_obj_set_pos(a, 448 - 16 - flex_text_width("Activo", FLEX_FONT_S1), 16);
        }
    }
    int by = 130 + FLEX_LOOK_N * 54 + 8, bw = (480 - 60) / 2;
    if (H.theme_sel >= 0) {
        btn(H.content, 24, by, bw, 44, "Aplicar", true, theme_cb, 100);
        btn(H.content, 36 + bw, by, bw, 44, "Cancelar", false, theme_cb, 101);
    } else {
        label_c(H.content, "Elige un tema para verlo y aplicarlo", FLEX_FONT_S2, FLEX_ONWALL2, 240, by + 12);
    }
    btn(H.content, 24, by + 54, 480 - 48, 42, "Restaurar tema predeterminado", false, theme_cb, 102);
}

// ---- Widgets -------------------------------------------------------------------------------
static void wg_cb(lv_event_t *e)
{
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    if (k >= 1000) {
        int pg = H.page_view < g_home.page_n ? H.page_view : g_home.main;
        int r = flex_hc_wg_add(pg, k - 1000);
        if (r == 1) {
            char m[48];
            snprintf(m, sizeof(m), "Esta pagina ya tiene %d widgets", FLEX_HOME_WG_MAX);
            info(m);
        } else if (r == 2) {
            info("Sin espacio: elige otra pagina");
        } else {
            flex_home_normalize();
            flex_home_save();
            H.wg_sel = -1;
            refresh_pages();
        }
        return;
    }
    H.wg_sel = k;
    show_view(V_WIDGETS);
}

static void build_widgets(void)
{
    const flex_palette_t *t = flex_th();
    char sb[56];
    snprintf(sb, sizeof(sb), "Se colocan en la p\xC3\xA1gina %d", (H.page_view < g_home.page_n ? H.page_view : g_home.main) + 1);
    chrome("Widgets", sb, true);
    lv_obj_t *l = list_create();
    int y = 4;
    const char *last = NULL;
    for (int i = 1; i < FLEX_WG_COUNT; i++) {
        if (i == FLEX_WG_RETIRED_7) {
            continue;
        }
        const flex_home_wg_desc_t *d = &flex_home_wg_reg[i];
        if (!last || strcmp(last, d->cat)) {
            last = d->cat;
            label_at(l, d->cat, FLEX_FONT_S2, FLEX_ONWALL2, 24, y + 4, 0);
            y += 26;
        }
        bool sel = H.wg_sel == i;
        lv_obj_t *r = panel(l, 16, y, 448, 76, 16, sel ? 250 : 225);
        lv_obj_set_clickable(r, true);
        lv_obj_add_event_cb(r, wg_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        if (sel) {
            lv_obj_set_style_border_color(r, flex_accent(), 0);
        }
        // vista previa REAL del widget (misma construccion que en el escritorio) recortada a su caja
        lv_obj_t *pv = flex_box(r);
        lv_obj_set_pos(pv, 10, 8);
        lv_obj_set_size(pv, 108, 60);
        lv_obj_set_style_clip_corner(pv, true, 0);
        lv_obj_set_style_radius(pv, 8, 0);
        lv_obj_set_scrollable(pv, false);
        flex_home_wg_t w = {.type = (uint8_t)i, .col = 0, .row = 0, .w = d->w, .h = d->h};
        flex_home_wg_build_preview(pv, &w, 108, 60);
        label_at(r, d->name, FLEX_FONT_S2, t->txt, 132, 18, 300);
        char sz[24];
        snprintf(sz, sizeof(sz), "%dx%d celdas", d->w, d->h);
        label_at(r, sz, FLEX_FONT_S1, t->txt2, 132, 42, 0);
        if (sel) {
            btn(r, 448 - 122 + 16 - 16, 20, 96, 36, "A\xC3\xB1" "adir", true, wg_cb, 1000 + i);
        }
        y += 82;
    }
}

// ---- Ajustes de inicio -------------------------------------------------------------------------
static void settings_cb(lv_event_t *e)
{
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    switch (k) {
    case 0: {
        int g = (flex_hc_grid_idx() + 1) % FLEX_HC_GRID_OPTS;
        flex_hc_set_grid(flex_hc_grid_c[g], flex_hc_grid_r[g]);
        H.thumbs_ok = false;
        break;
    }
    case 1: g_home.icon_sz = (uint8_t)((g_home.icon_sz + 1) % 3); flex_home_save(); H.thumbs_ok = false; break;
    case 2: g_home.labels = !g_home.labels; flex_home_save(); H.thumbs_ok = false; break;
    case 3: g_home.locked = !g_home.locked; flex_home_save(); break;
    case 4: g_home.dots = !g_home.dots; flex_home_save(); break;
    case 5:
        H.page_view = g_home.main;
        H.slide = 0;
        show_view(V_PAGES);
        return;
    case 6: g_home.pinch = !g_home.pinch; flex_home_save(); break;
    case 7: g_home.reduce = !g_home.reduce; flex_home_save(); break;
    case 8: flex_theme_set_wall_fit((uint8_t)((flex_look()->wall_fit + 1) % 3)); break;
    case 9:
        H.modal_kind = M_RESET;
        build_modal();
        return;
    default: return;
    }
    int keep = H.list ? lv_obj_get_scroll_y(H.list) : 0;
    show_view(V_SETTINGS);
    if (H.list) {
        lv_obj_scroll_to_y(H.list, keep, LV_ANIM_OFF);
    }
}

static void build_settings(void)
{
    chrome("Ajustes de inicio", NULL, true);
    lv_obj_t *l = list_create();
    char v[32];
    int y = 4;
    snprintf(v, sizeof(v), "%dx%d", g_home.cols, g_home.rows);
    static const char *const SZN[3] = {"Peque\xC3\xB1o", "Normal", "Grande"};
    const char *fit = flex_look()->wall_fit == 1 ? "Ajustar" : flex_look()->wall_fit == 2 ? "Centrar" : "Rellenar";
    char pg[32];
    snprintf(pg, sizeof(pg), "P\xC3\xA1gina %d", g_home.main + 1);
    row(l, y, 52, "Cuadr\xC3\xAD" "cula del inicio", v, 0, false, settings_cb, 0); y += 58;
    row(l, y, 52, "Tama\xC3\xB1o de iconos", SZN[g_home.icon_sz > 2 ? 1 : g_home.icon_sz], 0, false, settings_cb, 1); y += 58;
    row(l, y, 52, "Nombres de aplicaciones", NULL, 1, g_home.labels, settings_cb, 2); y += 58;
    row(l, y, 52, "Bloquear dise\xC3\xB1o del inicio", NULL, 1, g_home.locked, settings_cb, 3); y += 58;
    row(l, y, 52, "Indicadores de p\xC3\xA1gina", NULL, 1, g_home.dots, settings_cb, 4); y += 58;
    row(l, y, 52, "P\xC3\xA1gina principal", pg, 0, false, settings_cb, 5); y += 58;
    row(l, y, 52, "Gesto de pellizco", NULL, 1, g_home.pinch, settings_cb, 6); y += 58;
    row(l, y, 52, "Reducir animaciones", NULL, 1, g_home.reduce, settings_cb, 7); y += 58;
    row(l, y, 52, "Encuadre de la imagen", fit, 0, false, settings_cb, 8); y += 58;
    row(l, y, 52, "Restablecer dise\xC3\xB1o del inicio", NULL, 2, false, settings_cb, 9);
}

// ---- vistas ---------------------------------------------------------------------------------
static void back_chip_cb(lv_event_t *e)
{
    (void)e;
    if (H.modal_kind != M_NONE) {
        modal_close();
    } else if (H.view == V_PICK) {
        open_view(V_WALL);
    } else {
        show_view(V_PAGES);
    }
}

static void pick_scan_start(void);

// hcOpenView: entrar en una vista la deja sin seleccion
static void open_view(int v)
{
    if (v == V_WALL) {
        H.wall_sel = -1;
    } else if (v == V_THEME) {
        H.theme_sel = -1;
    } else if (v == V_WIDGETS) {
        H.wg_sel = -1;
    } else if (v == V_PICK) {
        H.pick_sel = -1;
        pick_scan_start();
    }
    show_view(v);
}

static void show_view(int v)
{
    H.view = (uint8_t)v;
    H.list = NULL;
    memset(H.pv_img, 0, sizeof(H.pv_img));
    lv_obj_invalidate(H.bg);   // el fondo puede haber cambiado (Fondo, Temas, paleta)
    lv_obj_clean(H.content);
    switch (v) {
    case V_WALL: build_wall(); break;
    case V_PICK: build_pick(); break;
    case V_THEME: build_theme(); break;
    case V_WIDGETS: build_widgets(); break;
    case V_SETTINGS: build_settings(); break;
    default:
        build_pages();
        lv_obj_add_event_cb(H.layer, layer_cb, LV_EVENT_ALL, NULL);
        break;
    }
    if (H.modal) {
        lv_obj_move_foreground(H.modal);
    }
}

// ---- entrada, salida y transicion ----------------------------------------------------------
static void snap_free(void)
{
    if (H.snap) {
        lv_draw_buf_destroy(H.snap);
        H.snap = NULL;
    }
}

static void destroy(void)
{
    if (H.pv_timer) {
        lv_timer_delete(H.pv_timer);
        H.pv_timer = NULL;
    }
    if (H.anim_img) {
        lv_anim_delete(H.anim_img, NULL);
    }
    if (H.root) {
        lv_obj_delete(H.root);
    }
    H.root = H.bg = H.content = H.modal = H.anim_img = H.layer = H.list = NULL;
    snap_free();
    free_thumbs();
    for (int i = 0; i < FLEX_WALL_N; i++) {
        free(H.pv[i]);
        H.pv[i] = NULL;
    }
    H.pv_next = 0;
    H.active = false;
    H.anim = 0;
    H.modal_kind = M_NONE;
}

static void finish_exit(void)
{
    destroy();
    flex_shell_overlay_end();   // estado Inicio
    flex_home_rebuild();
}

static void anim_exec(void *var, int32_t v)
{
    (void)var;
    // v: 0..1000 (0 = escritorio a pantalla completa, 1000 = tarjeta central)
    float p = (float)v / 1000.0f;
    int w = (int)(480 + (CARD_W - 480) * p), h = (int)(800 + (CARD_H - 800) * p);
    int x = (480 - w) / 2, y = (int)(CARD_Y * p);
    lv_obj_set_pos(H.anim_img, x, y);
    lv_obj_set_size(H.anim_img, w, h);
    lv_obj_set_style_radius(H.anim_img, (int)(26 * p), 0);
}

static int32_t ease_out_quad(const lv_anim_t *a)
{
    int32_t t = lv_map(a->act_time, 0, a->duration, 0, 1024);
    int32_t e = 1024 - ((1024 - t) * (1024 - t)) / 1024;
    return lv_map(e, 0, 1024, a->start_value, a->end_value);
}

static void anim_done(lv_anim_t *a)
{
    (void)a;
    if (H.anim == 2) {
        finish_exit();
        return;
    }
    H.anim = 0;
    if (H.anim_img) {
        lv_obj_delete(H.anim_img);
        H.anim_img = NULL;
    }
    snap_free();
    lv_obj_set_hidden(H.content, false);
    H.ignore = true;
}

static void anim_start(bool entering)
{
    H.anim_img = lv_image_create(H.root);
    lv_image_set_src(H.anim_img, H.snap);
    lv_image_set_inner_align(H.anim_img, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_set_style_clip_corner(H.anim_img, true, 0);
    lv_obj_set_clickable(H.anim_img, false);
    lv_obj_set_hidden(H.content, true);
    H.anim = entering ? 1 : 2;
    anim_exec(NULL, entering ? 0 : 1000);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, H.anim_img);
    lv_anim_set_exec_cb(&a, anim_exec);
    lv_anim_set_values(&a, entering ? 0 : 1000, entering ? 1000 : 0);
    lv_anim_set_duration(&a, ANIM_MS);
    lv_anim_set_path_cb(&a, ease_out_quad);
    lv_anim_set_completed_cb(&a, anim_done);
    lv_anim_start(&a);
}

static void ov_back(void)
{
    if (H.anim) {
        return;
    }
    if (H.modal_kind != M_NONE) {
        modal_close();
    } else if (H.view == V_PICK) {
        open_view(V_WALL);
    } else if (H.view != V_PAGES) {
        show_view(V_PAGES);
    } else {
        begin_exit();
    }
}

static void ov_home(void)
{
    if (!H.anim) {
        begin_exit();
    }
}

static void ov_recents(void) {}   // en Personalizar inicio el boton Recientes no hace nada

static const flex_overlay_ops_t s_ops = {ov_back, ov_home, ov_recents};

static void begin_exit(void)
{
    if (!H.active || H.anim) {
        return;
    }
    modal_close();
    if (H.pz.state) {
        flex_pz_init(&H.pz);
    }
    g_home.page = H.page_view < g_home.page_n ? H.page_view : g_home.main;
    flex_home_normalize();
    flex_home_save();
    flex_home_rebuild();                 // el escritorio refleja ya lo configurado
    lv_obj_update_layout(flex_home_root_obj());
    if (g_home.reduce) {
        finish_exit();
        return;
    }
    snap_free();
    H.snap = lv_snapshot_take(flex_home_root_obj(), LV_COLOR_FORMAT_RGB565);
    if (!H.snap) {
        finish_exit();
        return;
    }
    anim_start(false);
}

static void enter_now(void *unused)
{
    (void)unused;
    if (H.active || flex_shell_state() != FLEX_SH_HOME || !flex_wallmgr_image(FLEX_WALL_HOME)) {
        return;
    }
    const flex_palette_t *t = flex_th();
    lv_obj_t *home = flex_home_root_obj();
    lv_obj_update_layout(home);
    snap_free();
    H.snap = g_home.reduce ? NULL : lv_snapshot_take(home, LV_COLOR_FORMAT_RGB565);
    H.root = flex_box(flex_shell_screen());
    lv_obj_set_size(H.root, 480, 800);
    lv_obj_set_clickable(H.root, true);
    lv_obj_move_foreground(H.root);
    // fondo velado (blurBgVeil: rgb(8,10,18) a70) con el fondo de Inicio
    H.bg = lv_image_create(H.root);
    lv_image_set_src(H.bg, flex_wallmgr_image(FLEX_WALL_HOME));
    part(H.root, 0, 0, 480, 800, 0, lv_color_make(8, 10, 18), 70);
    (void)t;
    H.content = flex_box(H.root);
    lv_obj_set_size(H.content, 480, 800);
    flex_statusbar_create(H.root, FLEX_SB_WALL);
    H.view = V_PAGES;
    H.page_view = g_home.page;
    H.slide = 0;
    H.drag = H.drag_moved = false;
    H.reorder = -1;
    H.modal_kind = M_NONE;
    H.wall_sel = H.theme_sel = H.wg_sel = H.pick_sel = -1;
    H.ignore = true;
    H.active = true;
    H.thumbs_ok = false;
    flex_shell_overlay_begin(&s_ops);
    show_view(V_PAGES);
    if (H.snap) {
        anim_start(true);
    }
    flex_touch_drop_all();   // el dedo que abrio esto no toca nada hasta que se levante
}

// "Ignorar toques hasta que el dedo se levante" tras entrar (hcIgnore)
static void ignore_tick(lv_timer_t *t)
{
    if (H.ignore && !H.anim && flex_touch_arb()->t.down == false) {
        H.ignore = false;
    }
    if (!H.active) {
        lv_timer_delete(t);
    }
}

void flex_home_customize_open(void)
{
    if (H.active) {
        return;
    }
    lv_async_call(enter_now, NULL);   // se llama desde un evento del escritorio: la UI se crea fuera de el
    lv_timer_create(ignore_tick, 40, NULL);
}

bool flex_home_customize_active(void)
{
    return H.active;
}
int flex_home_customize_view(void)
{
    return H.active ? H.view : -1;
}
int flex_home_customize_page_view(void)
{
    return H.page_view;
}
bool flex_home_customize_animating(void)
{
    return H.anim != 0;
}
void flex_home_customize_close_now(void)
{
    if (H.active) {
        g_home.page = H.page_view < g_home.page_n ? H.page_view : g_home.page;
        flex_home_normalize();
        flex_home_save();
        destroy();
        flex_shell_overlay_end();
    }
}

// ---- pellizco de dos dedos: consumidor previo a LVGL (hpzUpdate) -----------------------------
static int poll_frame(void *ctx, flex_pz_pt_t out[4])
{
    (void)ctx;
    flex_touch_frame_t fr;
    if (!flex_touch_get_frame(&fr)) {
        return 0;
    }
    int n = fr.count > 4 ? 4 : fr.count;
    for (int i = 0; i < n; i++) {
        out[i].x = fr.pts[i].x;
        out[i].y = fr.pts[i].y;
        out[i].id = fr.pts[i].id;
    }
    return fr.count;
}

static int pinch_mode(void)
{
    if (H.active) {   // vista Paginas: sin modal, animacion, arrastre ni reordenacion
        return (H.view == V_PAGES && H.modal_kind == M_NONE && !H.anim && !H.drag && H.reorder < 0) ? 2 : 0;
    }
    // escritorio libre (drawerCanOpen, sin gesto de pagina, sin suspension, con el gesto activo)
    if (!g_home.pinch || flex_shell_state() != FLEX_SH_HOME || flex_home_edit_active() || flex_kiosk_active() ||
        flex_qs_is_open() || flex_notif_center_open() || flex_power_suspended() || flex_home_ctx_open() ||
        flex_drawer_is_open() || flex_home_scroll_x() % 480 != 0) {
        return 0;
    }
    return 1;
}

static void pinch_act(void *p)
{
    if ((int)(intptr_t)p == FLEX_PZ_OPEN_CFG) {
        enter_now(NULL);
        lv_timer_create(ignore_tick, 40, NULL);
    } else {
        begin_exit();
    }
}

static bool pinch_hook(const flex_arb_touch_t *t)
{
    (void)t;
    flex_arb_t *a = flex_touch_arb();
    int act = flex_pz_update(&H.pz, pinch_mode(), a->fingers, a->fingers_ms, lv_tick_get(), poll_frame, NULL);
    if (act != FLEX_PZ_NONE) {
        lv_async_call(pinch_act, (void *)(intptr_t)act);   // el hook corre dentro de la lectura de LVGL
    }
    return H.pz.swallow;
}

void flex_homecfg_init(void)
{
    flex_pz_init(&H.pz);
    flex_touch_add_sys_hook(pinch_hook, 3);
}
