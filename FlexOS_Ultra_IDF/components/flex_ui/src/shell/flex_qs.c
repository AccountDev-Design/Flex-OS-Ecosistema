// Flex OS Ultra · panel rapido (docs/spec/01b §5; Arduino: FlexOS_Ultra_QuickPanel.h,
// QuickPanelGlass.h y QuickPanelEdit.h).
//
// Capa GLOBAL (escritorio o app) en lv_layer_top. La cortina descubre el panel
// desde arriba: el contenido esta fijo en la pantalla y el borde movil decide
// cuanto se ve (la raiz mide 480 x panel_y y recorta a sus hijos). Todo el tacto
// del panel llega por flex_touch_add_sys_hook, ANTES que LVGL: la maquina de
// gestos de Arduino (qpPanelTouch, qpEditTouch, qpCatTouch) se conserva tal
// cual, con la propiedad del gesto decidida al apoyar y sin scroll de LVGL. El
// modelo (catalogo, disposicion, maquetacion) es flex_qs_model.c, probado
// contra el de Arduino.
//
// NADA FALSO: solo se ven y responden los controles cuyo backend existe ya en
// ESP-IDF (qs_shown). Los demas se CONSERVAN en la configuracion (qs_keep) y
// aparecen cuando su modulo se migre.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_auth.h"
#include "flex_clock.h"
#include "flex_display.h"
#include "flex_frame.h"
#include "flex_glass.h"
#include "flex_i18n.h"
#include "flex_qs_icons.h"
#include "flex_qs_model.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_touch_lvgl.h"
#include "flex_wallmgr.h"
#include "flex_wallpaper.h"

#define SCR_W 480
#define SCR_H 800
#define QS_EDGE_H     30   // SYS_EDGE_TOP_H
#define QS_EDGE_W     26   // SYS_EDGE_RIGHT_W
#define QS_EDGE_SLOP  28
#define QS_OPEN_PCT   40
#define QS_SHADOW_H   18
#define QS_VEL_TAU    45.0f
#define QS_FLICK      0.45f
#define QP_DRAG_TH    8
#define QP_LONG_MS    480
#define QP_EDLONG_MS  320
#define QP_RUBBER     0.42f
#define QP_FLASH_MS   200
#define QP_REJECT_MS  350
#define QP_EDH_H      96
#define QP_EDH_BTN_Y0 6
#define QP_EDH_BTN_Y1 50
#define QP_EDH_RST_Y0 50
#define QP_CAT_HDR    96
#define QP_CAT_ROW    112
#define QP_CAT_VIEWH  (SCR_H - QP_CAT_HDR)
#define QP_TOUCH_MIN  44
#define QP_HBTN_R     22
#define QP_HBTN_CY    50
#define GLASS_LVL_DEF 50
#define GLASS_LVL_STEP 5
#define TICK_MS       8
// "Apagar": boton de la cabecera y control (flex_poweroff_ui.c)
#define QS_HAS_POWEROFF 1

static const int HBTN_CX[3] = {338, 390, 442};

enum { QG_NONE = 0, QG_PENDING, QG_CURTAIN, QG_SCROLL, QG_GSCROLL, QG_RESIZE, QG_SLIDER, QG_EDDRAG, QG_CATSCROLL };
enum { QPM_PANEL = 0, QPM_EDIT, QPM_CAT };

typedef struct {
    lv_obj_t *root;
    lv_obj_t *fill, *val, *icon, *reset_icon;   // deslizadores
    int track_w, th;
} blk_ui_t;

static struct {
    // ---- modelo
    flex_qs_cfg_t cfg, ed;
    bool loaded;
    flex_qs_layout_t L;
    int lay_px;
    uint8_t mode;
    // ---- cortina
    int panel_y;
    bool dragging, drag_moved;
    int drag_base, drag_y0, prev_y;
    uint32_t prev_ms;
    float vel;
    bool anim_on;
    int anim_from, anim_dest;
    uint32_t anim_t0, anim_dur;
    bool over_app;
    // ---- gesto del panel
    uint8_t g;
    int gx0, gy0, g_prev_y;
    uint32_t g_prev_ms;
    float g_base;
    int tgt_blk, tgt_tile, tgt_hdr;
    bool g_long;
    // ---- scroll y tarjeta
    float scroll, scroll_vel, gh, gscroll, gscroll_vel;
    bool ganim;
    float gfrom, gto;
    uint32_t gt0, gdur;
    bool save_panel;
    // ---- editor y catalogo
    int ed_drag, ed_drag_x, ed_drag_y, ed_resize, ed_res_x0, ed_reject_id, ed_pend_idx, ed_pend_blk;
    uint32_t ed_reject_ms;
    uint8_t cat_ids[FLEX_QS_COUNT];
    int cat_n, cat_sel;
    float cat_scroll;
    int glassfx_drag;
    // ---- destello
    int flash_kind, flash_id;
    uint32_t flash_ms;
    lv_obj_t *flash_obj, *reject_obj;
    // ---- LVGL
    lv_obj_t *root, *bg, *edge, *body, *view, *content, *tiles_box, *group_inner, *gbar, *gpill, *ghost, *cat_grid;
    lv_obj_t *time_l, *date_l;
    blk_ui_t blk[FLEX_QS_MAX_ITEMS + 2];
    lv_timer_t *tick;
    uint32_t tick_ms;
    int32_t last_min;
} Q = {.ed_drag = -1, .ed_resize = -1, .ed_reject_id = -1, .glassfx_drag = -1, .flash_kind = -1, .cat_sel = -1};

// ---- disponibilidad ---------------------------------------------------------------
// Se conserva en la configuracion todo lo que puede existir en esta placa: el
// P4 no tiene Bluetooth y las dos ranuras retiradas no se ofrecen nunca.
static bool qs_keep(int id)
{
    return id >= 0 && id < FLEX_QS_COUNT && id != FLEX_QS_BLE && id != FLEX_QS_RETIRED_10 &&
           id != FLEX_QS_RETIRED_13;
}

static bool fs_mounted(void)
{
    flex_storage_status_t st;
    flex_storage_get_status(&st);
    return st.fs_mounted;
}

// Lo que funciona AHORA en ESP-IDF. Pendientes: Wi-Fi y Sincronizar hora (Fase 6),
// Ahorro (gestion de energia, Fase 17), Actualizaciones (Fase 14), Volumen y
// Silencio (Fase 11) y Cronometro (app).
static bool qs_shown(int id)
{
    switch (id) {
    case FLEX_QS_AIRPLANE:
    case FLEX_QS_BRIGHT:
    case FLEX_QS_THEME:
    case FLEX_QS_GLASS:
    case FLEX_QS_SETTINGS:
    case FLEX_QS_CONN:
    case FLEX_QS_DEX:
    case FLEX_QS_CAMERA:
    case FLEX_QS_GALLERY:
    case FLEX_QS_DND:   // politica del sistema (flex_notif.c), no un periferico
    case FLEX_QS_POWEROFF:
        return true;
    case FLEX_QS_GLASSFX:
        return flex_look()->glass;   // con el estilo Plano la intensidad no cambia nada
    case FLEX_QS_FILES:
        return fs_mounted();
    case FLEX_QS_LOCK:
        return flex_auth_required();   // sin clave, "Bloquear" no bloquearia nada
    default:
        return false;
    }
}

// ---- estado real de cada control ---------------------------------------------------
static bool airplane(void)
{
    return flex_cfg_get_bool("airpl", false);   // Conn.h:180, misma clave y tipo
}

static bool ctl_on(int id)
{
    switch (id) {
    case FLEX_QS_AIRPLANE: return airplane();
    case FLEX_QS_THEME: return flex_look()->dark;   // ON = oscuro (Arduino lo tenia al reves)
    case FLEX_QS_GLASS: return flex_look()->glass;
    case FLEX_QS_DND: return flex_notif_dnd();
    default: return false;
    }
}

static int bright(void)
{
    return flex_display_get_brightness();
}

static int glass_lvl(void)
{
    return Q.glassfx_drag >= 0 ? Q.glassfx_drag : flex_look()->glass_level;
}

static void ctl_sub(int id, char *o, size_t n)
{
    o[0] = 0;
    switch (id) {
    case FLEX_QS_AIRPLANE: snprintf(o, n, "%s", airplane() ? "Activado" : "Desactivado"); break;
    case FLEX_QS_THEME: snprintf(o, n, "%s", flex_look()->dark ? "Oscuro" : "Claro"); break;
    case FLEX_QS_GLASS: snprintf(o, n, "%s", flex_look()->glass ? "Liquid Glass" : "Plano"); break;
    case FLEX_QS_BRIGHT: snprintf(o, n, "%d%%", bright()); break;
    // Lo que de verdad pasa. "Avisos, sin sonido" llegara con el audio (Fase 11).
    case FLEX_QS_DND: snprintf(o, n, "%s", flex_notif_dnd() ? "Sin avisos" : "Avisos normales"); break;
    case FLEX_QS_GLASSFX: {
        int v = glass_lvl();
        snprintf(o, n, "%s %d%%", v < 35 ? "Sutil" : v > 65 ? "Intenso" : "Normal", v);
        break;
    }
    default: break;
    }
}

// Accion secundaria (pulsacion larga): la pantalla de Ajustes del control.
static bool ctl_has_detail(int id)
{
    return id == FLEX_QS_AIRPLANE || id == FLEX_QS_BRIGHT || id == FLEX_QS_THEME || id == FLEX_QS_GLASS ||
           id == FLEX_QS_DND;
}

// ---- colores (QuickPanelGlass.h:146-155, QuickPanel.h:975) -----------------------------
static lv_color_t mixc(lv_color_t a, lv_color_t b, uint8_t t)
{
    return lv_color_mix(b, a, t);   // mix565(a, b, t): t = peso de b
}
static lv_color_t c_card(void)
{
    return flex_look()->glass ? flex_th()->glass2 : flex_th()->surf2;
}
static lv_color_t c_tile_off(void)
{
    return mixc(c_card(), flex_th()->txt, 34);
}
static lv_color_t c_cap_on(void)
{
    return mixc(c_card(), flex_th()->primary, 70);
}
static int mix_adj(int base)
{
    if (!flex_look()->glass) {
        return 255;
    }
    int d = (int)flex_look()->glass_level - GLASS_LVL_DEF;
    int v = base + (d < 0 ? (d * 12) / 50 : (d * 24) / 50);
    return v < 0 ? 0 : v > 255 ? 255 : v;
}
static uint8_t veil_alpha(void)
{
    if (!flex_look()->glass) {
        return 255;
    }
    int d = (int)flex_look()->glass_level - GLASS_LVL_DEF;
    return (uint8_t)(152 + (d < 0 ? (d * 16) / 50 : (d * 32) / 50));   // 136 .. 184
}

// ---- construccion ---------------------------------------------------------------
static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = flex_box(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_clickable(o, false);
    return o;
}

// Superficie del panel: vidrio sobre el fondo velado de la cortina (o Plano).
static lv_obj_t *surf(lv_obj_t *parent, int x, int y, int w, int h, int r, lv_color_t tint, int mix_base)
{
    lv_obj_t *o = box(parent, x, y, w, h);
    lv_obj_set_style_radius(o, r, 0);
    bool over_wall = flex_look()->glass && !Q.over_app;
    flex_surface(o, FLEX_SURF_TINT, over_wall ? FLEX_BD_HOME : FLEX_BD_FLAT);
    flex_surface_set_tint(o, tint);
    if (over_wall) {
        flex_surface_set_veil(o, flex_th()->page, veil_alpha());
    }
    int mm = mix_adj(mix_base) - 12;
    flex_surface_set_min_mix(o, (uint8_t)(mm < 0 ? 0 : mm));
    flex_surface_set_flat(o, tint, LV_OPA_COVER);
    return o;
}

static lv_obj_t *fillr(lv_obj_t *parent, int x, int y, int w, int h, int r, lv_color_t c, lv_opa_t a)
{
    lv_obj_t *o = box(parent, x, y, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, a, 0);
    return o;
}

static lv_obj_t *icon(lv_obj_t *parent, flex_qi_t ic, int s, int cx, int cy, lv_color_t col)
{
    lv_obj_t *o = flex_qi_create(parent, ic, s);
    lv_obj_set_pos(o, cx - (s + 8) / 2, cy - (s + 8) / 2);
    lv_obj_set_style_text_color(o, col, 0);
    return o;
}

static int32_t text_w(const char *s, const lv_font_t *f)
{
    lv_point_t sz;
    lv_text_get_size(&sz, s, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return sz.x;
}

// Texto como drawText/drawTextC/drawTextR/drawTextClip de Arduino: y es el tope
// de las mayusculas; align 0 izquierda en x, 1 centrado en x, 2 termina en x.
// maxw > 0 recorta (centrado que no cabe: empieza en x - maxw/2, como Arduino).
static lv_obj_t *text(lv_obj_t *parent, const char *s, const lv_font_t *f, lv_color_t c, int x, int y, int align,
                      int maxw)
{
    lv_obj_t *l = flex_label(parent, s, f, c);
    int32_t tw = text_w(s, f);
    if (maxw > 0 && tw > maxw) {
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_width(l, maxw);
        lv_obj_set_x(l, align == 1 ? x - maxw / 2 : align == 2 ? x - maxw : x);
    } else {
        lv_obj_set_x(l, align == 1 ? x - tw / 2 : align == 2 ? x - tw : x);
    }
    flex_label_cap_y(l, y);
    return l;
}

// ---- fondo de la cortina (qpGlassBuild) -------------------------------------------------
static lv_image_dsc_t s_bd_view;

static void bg_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t c;
    lv_obj_get_coords(obj, &c);
    const flex_palette_t *t = flex_th();
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    if (!flex_look()->glass) {
        rd.bg_color = t->page;   // PLANO ES PLANO: superficie solida de la paleta
        rd.bg_opa = LV_OPA_COVER;
        lv_draw_rect(layer, &rd, &c);
        return;
    }
    // Lo de debajo ya desenfocado: el fondo del escritorio (backdrop de
    // flex_wallmgr) o, con una app delante, su color de ventana (el desenfoque
    // de una pantalla de app es practicamente su color).
    const uint16_t *bd = Q.over_app ? NULL : flex_wallmgr_backdrop(FLEX_WALL_HOME);
    if (bd) {
        memset(&s_bd_view, 0, sizeof(s_bd_view));
        s_bd_view.header.magic = LV_IMAGE_HEADER_MAGIC;
        s_bd_view.header.cf = LV_COLOR_FORMAT_RGB565;
        s_bd_view.header.w = SCR_W;
        s_bd_view.header.h = SCR_H;
        s_bd_view.header.stride = FLEX_WALL_W * 2;
        s_bd_view.data = (const uint8_t *)bd;
        s_bd_view.data_size = FLEX_WALL_W * 2 * SCR_H;
        lv_draw_image_dsc_t id;
        lv_draw_image_dsc_init(&id);
        id.src = &s_bd_view;
        id.image_area = c;
        lv_draw_image(layer, &id, &c);
    } else {
        rd.bg_color = Q.over_app ? t->win : t->page;
        rd.bg_opa = LV_OPA_COVER;
        lv_draw_rect(layer, &rd, &c);
    }
    rd.bg_color = t->glass2;   // tinte de vidrio 40/255
    rd.bg_opa = 40;
    lv_draw_rect(layer, &rd, &c);
    rd.bg_color = t->page;     // velo
    rd.bg_opa = veil_alpha();
    lv_draw_rect(layer, &rd, &c);
    // brillo muy suave en el 20 % de arriba (TH_SURF2, 18 -> 0)
    lv_area_t top = {c.x1, c.y1, c.x2, c.y1 + SCR_H / 5 - 1};
    lv_draw_rect_dsc_init(&rd);
    rd.bg_grad.dir = LV_GRAD_DIR_VER;
    rd.bg_grad.stops_count = 2;
    rd.bg_grad.stops[0].color = t->surf2;
    rd.bg_grad.stops[0].opa = 18;
    rd.bg_grad.stops[0].frac = 0;
    rd.bg_grad.stops[1].color = t->surf2;
    rd.bg_grad.stops[1].opa = 0;
    rd.bg_grad.stops[1].frac = 255;
    rd.bg_opa = LV_OPA_COVER;
    lv_draw_rect(layer, &rd, &top);
}

// Sombra (18 filas, alfa 70 -> 0) y asa 56x5 del borde movil (qsRender)
static void edge_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t c;
    lv_obj_get_coords(obj, &c);
    const flex_palette_t *t = flex_th();
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    lv_area_t sh = {c.x1, c.y1 + 22, c.x2, c.y1 + 22 + QS_SHADOW_H - 1};
    rd.bg_grad.dir = LV_GRAD_DIR_VER;
    rd.bg_grad.stops_count = 2;
    rd.bg_grad.stops[0].color = t->shadow;
    rd.bg_grad.stops[0].opa = 70;
    rd.bg_grad.stops[0].frac = 0;
    rd.bg_grad.stops[1].color = t->shadow;
    rd.bg_grad.stops[1].opa = 0;
    rd.bg_grad.stops[1].frac = 255;
    rd.bg_opa = LV_OPA_COVER;
    lv_draw_rect(layer, &rd, &sh);
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color = t->mute;
    rd.bg_opa = LV_OPA_COVER;
    rd.radius = 2;
    lv_area_t pill = {c.x1 + SCR_W / 2 - 28, c.y1 + 22 - 14, c.x1 + SCR_W / 2 + 27, c.y1 + 22 - 10};
    lv_draw_rect(layer, &rd, &pill);
}

// ---- maquetacion viva (qpRelayout) --------------------------------------------------------
static flex_qs_cfg_t *lay_cfg(void)
{
    return Q.mode == QPM_PANEL ? &Q.cfg : &Q.ed;
}

static void relayout(void)
{
    bool edit = Q.mode != QPM_PANEL;
    flex_qs_cfg_t *c = lay_cfg();
    int want;
    if (edit) {
        want = flex_qp_group_h(Q.ed.grows);
    } else {
        if (Q.gh <= 0) {
            Q.gh = (float)flex_qp_group_h(Q.cfg.grows);
        }
        want = (int)(Q.gh + 0.5f);
    }
    int px = flex_qs_relayout(&Q.L, c->it, c->n, want, edit, qs_shown);
    if (px != want && !edit) {
        Q.gh = (float)px;
    }
    Q.lay_px = px;
    int mg = flex_qs_gscroll_max(&Q.L, Q.lay_px);
    if (Q.gscroll > mg) {
        Q.gscroll = (float)mg;
    }
    if (Q.gscroll < 0) {
        Q.gscroll = 0;
    }
}

static int view_top(void)
{
    return FLEX_QP_VIEW_Y0 - (int)(Q.scroll + 0.5f);
}

// ---- contenido ------------------------------------------------------------------
static void build_body(void);

static void add_flash(lv_obj_t *parent, int x, int y, int w, int h, int r)
{
    Q.flash_obj = fillr(parent, x, y, w, h, r, flex_th()->txt, 110);
}

static void build_slider(blk_ui_t *u, lv_obj_t *parent, int x, int y, int w, int h, int id)
{
    const flex_palette_t *t = flex_th();
    int th = h - 20 < 40 ? 40 : h - 20;
    int ty = (h - th) / 2;
    int pct = id == FLEX_QS_BRIGHT ? bright() : glass_lvl();
    int tw = id == FLEX_QS_GLASSFX ? w - th - 8 : w;
    lv_obj_t *r = box(parent, x, y, w, h);
    u->root = r;
    u->th = th;
    u->track_w = tw;
    if (id == FLEX_QS_GLASSFX) {
        int rx = w - th;
        surf(r, rx, ty, th, th, th / 2, t->track, 128);
        u->reset_icon = icon(r, FLEX_QI_RESET, 26, rx + th / 2, ty + th / 2, pct == GLASS_LVL_DEF ? t->mute : t->txt);
    }
    surf(r, 0, ty, tw, th, th / 2, t->track, 128);
    int fw = th + (tw - th) * pct / 100;
    if (fw > tw) {
        fw = tw;
    }
    u->fill = surf(r, 0, ty, fw, th, th / 2, flex_accent(), 178);
    u->icon = icon(r, flex_qs_ctl_icon(id), 30, th / 2, ty + th / 2, t->on_acc);
    char v[24];
    ctl_sub(id, v, sizeof(v));
    u->val = text(r, v, FLEX_FONT_S2, fw > tw - 70 ? t->on_acc : t->txt2, tw - 20, ty + th / 2 - 8, 2, 0);
}

static void slider_update(int b, int id)
{
    blk_ui_t *u = &Q.blk[b];
    if (!u->fill) {
        return;
    }
    const flex_palette_t *t = flex_th();
    int pct = id == FLEX_QS_BRIGHT ? bright() : glass_lvl();
    int fw = u->th + (u->track_w - u->th) * pct / 100;
    if (fw > u->track_w) {
        fw = u->track_w;
    }
    lv_obj_set_width(u->fill, fw);
    char v[24];
    ctl_sub(id, v, sizeof(v));
    lv_label_set_text(u->val, v);
    lv_obj_set_x(u->val, u->track_w - 20 - text_w(v, FLEX_FONT_S2));
    lv_obj_set_style_text_color(u->val, fw > u->track_w - 70 ? t->on_acc : t->txt2, 0);
    if (u->reset_icon) {
        lv_obj_set_style_text_color(u->reset_icon, pct == GLASS_LVL_DEF ? t->mute : t->txt, 0);
    }
}

static void build_module(blk_ui_t *u, lv_obj_t *parent, const flex_qs_block_t *k, const flex_qs_item_t *it)
{
    const flex_qs_meta_t *m = flex_qs_meta(it->id);
    const flex_palette_t *t = flex_th();
    if (m->type == FLEX_QT_SLIDER) {
        build_slider(u, parent, k->x, k->y, k->w, k->h, it->id);
        return;
    }
    bool on = m->type == FLEX_QT_TOGGLE && ctl_on(it->id);
    lv_obj_t *r = surf(parent, k->x, k->y, k->w, k->h, FLEX_QP_RAD_S, on ? c_cap_on() : c_card(), 128);
    u->root = r;
    const int ir = 22;
    lv_color_t ic_face = on ? t->primary : c_tile_off();
    lv_color_t ic_col = on ? t->on_acc : t->txt2;
    char sub[64];
    ctl_sub(it->id, sub, sizeof(sub));
    int w = k->w, h = k->h;
    if (it->ori == FLEX_QOR_V) {
        int icx = w / 2, icy = 22 + ir - 8;
        surf(r, icx - ir, icy - ir, 2 * ir, 2 * ir, ir, ic_face, on ? 178 : 112);
        icon(r, flex_qs_ctl_icon(it->id), 30, icx, icy, ic_col);
        text(r, m->title, FLEX_FONT_S1, t->txt, icx, icy + ir + 8, 1, w - 8);
        if (sub[0]) {
            text(r, sub, FLEX_FONT_S1, t->txt2, icx, icy + ir + 22, 1, w - 8);
        }
    } else {
        int icx = 14 + ir, icy = h / 2;
        surf(r, icx - ir, icy - ir, 2 * ir, 2 * ir, ir, ic_face, on ? 178 : 112);
        icon(r, flex_qs_ctl_icon(it->id), 30, icx, icy, ic_col);
        int tx = icx + ir + 12, maxw = (w - 12) - tx;
        if (sub[0]) {
            text(r, m->title, FLEX_FONT_S2, t->txt, tx, icy - 17, 0, maxw);
            text(r, sub, FLEX_FONT_S1, t->txt2, tx, icy + 5, 0, maxw);
        } else {
            text(r, m->title, FLEX_FONT_S2, t->txt, tx, icy - 8, 0, maxw);
        }
    }
    if (Q.flash_kind == 1 && Q.flash_id == it->id) {
        add_flash(r, 0, 0, w, h, FLEX_QP_RAD_S);
    }
}

static bool can_resize(const flex_qs_item_t *it)
{
    uint8_t nw, nh;
    return flex_qs_next_size(it->id, it->w, it->h, +1, &nw, &nh) || flex_qs_next_size(it->id, it->w, it->h, -1, &nw, &nh);
}

// Adornos del editor sobre un modulo (qpDrawEditChrome)
static void edit_chrome(lv_obj_t *parent, const flex_qs_block_t *k, const flex_qs_item_t *it)
{
    const flex_palette_t *t = flex_th();
    int x = k->x, y = k->y, w = k->w, h = k->h;
    fillr(parent, x + 4 - 14, y + 4 - 14, 28, 28, 14, t->danger, LV_OPA_COVER);
    icon(parent, FLEX_QI_MINUS, 22, x + 4, y + 4, t->on_acc);
    if (can_resize(it)) {
        int hx = x + w - 7, hy = y + h / 2;
        fillr(parent, hx - 3, hy - 16, 6, 32, 3, t->primary, LV_OPA_COVER);
        fillr(parent, hx - 9, hy - 8, 4, 16, 2, mixc(t->primary, t->txt, 90), LV_OPA_COVER);
    }
    if (flex_qs_meta(it->id)->oris == (FLEX_QOR_H | FLEX_QOR_V)) {
        int rx = x + 6, ry = y + h - 6;
        fillr(parent, rx - 13, ry - 13, 26, 26, 13, mixc(c_card(), t->txt, 70), LV_OPA_COVER);
        icon(parent, FLEX_QI_ROTATE, 20, rx, ry, t->txt);
    }
    if (Q.ed_reject_id == it->id) {
        Q.reject_obj = fillr(parent, x, y, w, h, FLEX_QP_RAD_S, t->danger, 120);
    }
}

static void build_group(lv_obj_t *parent, int b, const flex_qs_block_t *k)
{
    const flex_palette_t *t = flex_th();
    flex_qs_cfg_t *c = lay_cfg();
    lv_obj_t *g = surf(parent, k->x, k->y, k->w, k->h, FLEX_QP_RAD, c_card(), 128);
    Q.blk[b].root = g;
    int inner_h = k->h - FLEX_QP_GPAD - FLEX_QP_HANDLE_H;
    Q.group_inner = box(g, 2, FLEX_QP_GPAD, k->w - 5, inner_h > 0 ? inner_h : 1);
    int rows = flex_qs_total_rows(&Q.L);
    Q.tiles_box = box(Q.group_inner, 0, -(int)(Q.gscroll + 0.5f), k->w - 5, rows * FLEX_QP_TROW + 1);
    const int r = FLEX_QP_TCIRC / 2;
    for (int i = 0; i < Q.L.tile_n; i++) {
        const flex_qs_item_t *it = &c->it[Q.L.tiles[i]];
        const flex_qs_meta_t *m = flex_qs_meta(it->id);
        int cx = FLEX_QP_GPAD - 2 + (i % 4) * FLEX_QP_TCOLW + FLEX_QP_TCOLW / 2;   // en tiles_box
        int cy = (i / 4) * FLEX_QP_TROW + r;
        if (Q.mode == QPM_EDIT && (int)Q.L.tiles[i] == Q.ed_drag) {   // hueco de insercion
            lv_obj_t *gap = fillr(Q.tiles_box, cx - r, cy - r, 2 * r, 2 * r, r, t->primary, 60);
            lv_obj_set_style_border_color(gap, t->primary, 0);
            lv_obj_set_style_border_width(gap, 1, 0);
            continue;
        }
        bool on = m->type == FLEX_QT_TOGGLE && ctl_on(it->id);
        lv_obj_t *circ = surf(Q.tiles_box, cx - r, cy - r, 2 * r, 2 * r, r, on ? t->primary : c_tile_off(),
                              on ? 178 : 112);
        icon(Q.tiles_box, flex_qs_ctl_icon(it->id), 32, cx, cy, on ? t->on_acc : t->txt2);
        if (Q.flash_kind == 0 && Q.flash_id == it->id) {
            add_flash(circ, 0, 0, 2 * r, 2 * r, r);
        }
        int lw = FLEX_QP_TCOLW - 6;
        text(Q.tiles_box, m->name, FLEX_FONT_S1, on ? t->txt : t->txt2, cx, cy + r + 8, 1, lw);
        char sub[48];
        ctl_sub(it->id, sub, sizeof(sub));
        if (sub[0]) {
            text(Q.tiles_box, sub, FLEX_FONT_S1, t->txt2, cx, cy + r + 22, 1, lw);
        }
        if (Q.mode == QPM_EDIT) {
            fillr(Q.tiles_box, cx - r + 4 - 12, cy - r + 4 - 12, 24, 24, 12, t->danger, LV_OPA_COVER);
            icon(Q.tiles_box, FLEX_QI_MINUS, 20, cx - r + 4, cy - r + 4, t->on_acc);
            if (can_resize(it)) {
                int hx = cx + FLEX_QP_TCOLW / 2 - 5;
                fillr(Q.tiles_box, hx - 3, cy - 12, 6, 24, 3, t->primary, LV_OPA_COVER);
            }
            if (Q.ed_reject_id == it->id) {
                Q.reject_obj = fillr(Q.tiles_box, cx - r, cy - r, 2 * r, 2 * r, r, t->danger, 120);
            }
        }
    }
    Q.gpill = fillr(g, k->w / 2 - 28, k->h - FLEX_QP_HANDLE_H / 2 - 3, 56, 6, 3, t->mute, LV_OPA_COVER);
    Q.gbar = fillr(g, k->w - 8, FLEX_QP_GPAD, 3, 24, 2, t->mute, LV_OPA_COVER);
    lv_obj_set_hidden(Q.gbar, true);
}

static void build_add(lv_obj_t *parent, int b, const flex_qs_block_t *k)
{
    const flex_palette_t *t = flex_th();
    lv_obj_t *a = surf(parent, k->x, k->y, k->w, k->h, FLEX_QP_RAD_S, mixc(c_card(), t->primary, 70), 128);
    lv_obj_set_style_border_color(a, mixc(t->border, t->primary, 150), 0);
    lv_obj_set_style_border_width(a, 1, 0);
    Q.blk[b].root = a;
    int icx = k->w / 2 - 92, icy = k->h / 2;
    icon(a, FLEX_QI_PLUS, 28, icx, icy, t->txt);
    text(a, "A\xC3\xB1" "adir un control", FLEX_FONT_S2, t->txt, icx + 22, icy - 8, 0, 0);
}

// Geometria que cambia sin cambiar de configuracion: alto de la tarjeta (y lo
// que va debajo), scroll del contenido y de la tarjeta, indicador de scroll.
static void place(void)
{
    if (!Q.content) {
        return;
    }
    lv_obj_set_y(Q.content, Q.mode == QPM_PANEL ? -(int)(Q.scroll + 0.5f) : 20 - (int)(Q.scroll + 0.5f));
    for (int b = 0; b < Q.L.blk_n; b++) {
        const flex_qs_block_t *k = &Q.L.blk[b];
        if (!Q.blk[b].root) {
            continue;
        }
        lv_obj_set_pos(Q.blk[b].root, k->x, k->y);
        if (k->kind == FLEX_QB_GROUP) {
            lv_obj_set_height(Q.blk[b].root, k->h);
            int inner = flex_qs_group_inner_h(k->h);
            if (Q.group_inner) {
                lv_obj_set_height(Q.group_inner, inner > 0 ? inner : 1);
            }
            if (Q.tiles_box) {
                lv_obj_set_y(Q.tiles_box, -(int)(Q.gscroll + 0.5f));
            }
            if (Q.gpill) {
                lv_obj_set_y(Q.gpill, k->h - FLEX_QP_HANDLE_H / 2 - 3);
            }
            if (Q.gbar) {
                int total = flex_qs_total_rows(&Q.L) * FLEX_QP_TROW;
                bool show = total > inner + 2 && inner > 0;
                lv_obj_set_hidden(Q.gbar, !show);
                if (show) {
                    int bar_h = inner * inner / total;
                    bar_h = bar_h < 24 ? 24 : bar_h > inner ? inner : bar_h;
                    int max_s = total - inner, sc = (int)Q.gscroll;
                    sc = sc < 0 ? 0 : sc > max_s ? max_s : sc;
                    int off = max_s > 0 ? sc * (inner - bar_h) / max_s : 0;
                    lv_obj_set_pos(Q.gbar, k->w - 8, FLEX_QP_GPAD + off);
                    lv_obj_set_height(Q.gbar, bar_h);
                }
            }
        }
    }
    if (Q.cat_grid) {
        lv_obj_set_y(Q.cat_grid, -(int)(Q.cat_scroll + 0.5f));
    }
}

// Cabecera fija del panel (qpDrawHeader)
static void build_header(lv_obj_t *p)
{
    const flex_palette_t *t = flex_th();
    char buf[40];
    flex_clock_str_bar(buf, sizeof(buf));
    Q.time_l = text(p, buf, FLEX_FONT_S6, t->txt, FLEX_QP_MX, 22, 0, 0);
    int right = HBTN_CX[0] - QP_HBTN_R - 12;   // 304
    int tw = text_w(buf, FLEX_FONT_S6);
    struct tm tm;
    flex_clock_now(&tm);
    char d[48];
    flex_date_short(d, sizeof(d), tm.tm_wday, tm.tm_mday, tm.tm_mon + 1);
    int dx = FLEX_QP_MX + tw + 14;
    Q.date_l = text(p, d, FLEX_FONT_S2, t->txt2, dx, 54, 0, right - dx);
    // Estado REAL de red: sin Wi-Fi todavia (Fase 6), lo que diria connWifiSub
    // con el Wi-Fi sin compilar.
    const char *ns = airplane() ? "Modo avi\xC3\xB3n activo" : "(No disponible)";
    text(p, ns, FLEX_FONT_S1, t->txt2, FLEX_QP_MX, 84, 0, right - FLEX_QP_MX);
    for (int i = 0; i < 3; i++) {
        if (i == 1 && !QS_HAS_POWEROFF) {
            continue;
        }
        int cx = HBTN_CX[i];
        lv_obj_t *b = surf(p, cx - QP_HBTN_R, QP_HBTN_CY - QP_HBTN_R, 2 * QP_HBTN_R, 2 * QP_HBTN_R, QP_HBTN_R,
                           c_card(), 128);
        if (i == 1) {
            lv_obj_set_style_border_color(b, mixc(t->border, t->danger, 140), 0);
            lv_obj_set_style_border_width(b, 1, 0);
        }
        icon(p, i == 0 ? FLEX_QI_PENCIL : i == 1 ? FLEX_QI_POWER : FLEX_QI_GEAR, 26, cx, QP_HBTN_CY,
             i == 1 ? t->danger : t->txt);
    }
    // Asa de cierre (franja inferior fija)
    fillr(p, SCR_W / 2 - 44, SCR_H - FLEX_QP_FOOT_H / 2 - 3, 88, 6, 3, t->mute, LV_OPA_COVER);
}

static void build_edit_header(lv_obj_t *p)
{
    const flex_palette_t *t = flex_th();
    surf(p, 0, 0, SCR_W, QP_EDH_H, 0, t->glass2, 168);
    text(p, "Cancelar", FLEX_FONT_S2, t->txt2, FLEX_QP_MX, 20, 0, 0);
    text(p, "Editar panel", FLEX_FONT_S2, t->txt, SCR_W / 2, 20, 1, 0);
    text(p, "Listo", FLEX_FONT_S2, t->primary, SCR_W - FLEX_QP_MX, 20, 2, 0);
    text(p, "Restablecer dise\xC3\xB1o", FLEX_FONT_S1, t->txt2, SCR_W / 2, 56, 1, 0);
    fillr(p, 0, QP_EDH_H - 1, SCR_W, 1, 0, t->divider, LV_OPA_COVER);
}

static void build_catalog(lv_obj_t *p)
{
    const flex_palette_t *t = flex_th();
    surf(p, 0, QP_CAT_HDR, SCR_W, SCR_H - QP_CAT_HDR, 0, t->glass2, 128);
    lv_obj_t *view = box(p, 0, QP_CAT_HDR, SCR_W, QP_CAT_VIEWH);
    int rows = (Q.cat_n + 3) / 4;
    Q.cat_grid = box(view, 0, -(int)(Q.cat_scroll + 0.5f), SCR_W, rows * QP_CAT_ROW + 24);
    const int r = FLEX_QP_TCIRC / 2;
    for (int k = 0; k < Q.cat_n; k++) {
        const flex_qs_meta_t *m = flex_qs_meta(Q.cat_ids[k]);
        int cx = flex_qp_col_x(k % 4) + FLEX_QP_CW / 2, cy = 12 + (k / 4) * QP_CAT_ROW + r;
        bool sel = Q.cat_sel == k;
        surf(Q.cat_grid, cx - r, cy - r, 2 * r, 2 * r, r, sel ? t->primary : c_tile_off(), sel ? 178 : 112);
        icon(Q.cat_grid, flex_qs_ctl_icon(Q.cat_ids[k]), 32, cx, cy, sel ? t->on_acc : t->txt);
        int lw = FLEX_QP_CW - 2;
        text(Q.cat_grid, m->name, FLEX_FONT_S1, t->txt, cx, cy + r + 8, 1, lw);
        text(Q.cat_grid, FLEX_QS_CAT_NAME[m->cat], FLEX_FONT_S1, t->txt2, cx, cy + r + 22, 1, lw);
    }
    if (Q.cat_n == 0) {
        text(p, "No queda ning\xC3\xBAn control disponible", FLEX_FONT_S2, t->mute, SCR_W / 2, QP_CAT_HDR + 60, 1, 0);
    }
    surf(p, 0, 0, SCR_W, QP_CAT_HDR, 0, t->glass2, 168);
    text(p, "Atr\xC3\xA1s", FLEX_FONT_S2, t->txt2, FLEX_QP_MX, 22, 0, 0);
    text(p, "A\xC3\xB1" "adir un control", FLEX_FONT_S2, t->txt, SCR_W / 2, 22, 1, 0);
    text(p, "Solo se listan controles con funci\xC3\xB3n real", FLEX_FONT_S1, t->txt2, SCR_W / 2, 56, 1, 0);
    fillr(p, 0, QP_CAT_HDR - 1, SCR_W, 1, 0, t->divider, LV_OPA_COVER);
}

// Fantasma del elemento que se mueve en el editor (qpDrawGhost)
static void build_ghost(void)
{
    if (Q.ghost) {
        lv_obj_delete(Q.ghost);
        Q.ghost = NULL;
    }
    if (Q.mode != QPM_EDIT || Q.ed_drag < 0 || Q.ed_drag >= Q.ed.n) {
        return;
    }
    const flex_palette_t *t = flex_th();
    const flex_qs_item_t *it = &Q.ed.it[Q.ed_drag];
    int w = it->w == 1 ? FLEX_QP_TCIRC + 20 : flex_qp_span_w(it->w);
    int h = it->w == 1 ? FLEX_QP_TROW - 12 : it->h >= 2 ? FLEX_QP_RH2 : FLEX_QP_RH1;
    Q.ghost = box(Q.root, 0, 0, w, h + 4);
    for (int k = 1; k <= 4; k++) {
        fillr(Q.ghost, 8, h + k - 1, w - 16, 1, 0, t->shadow, (lv_opa_t)(96 - k * 20));
    }
    if (it->w == 1) {
        const int r = FLEX_QP_TCIRC / 2;
        int cx = w / 2, cy = r + 4;
        fillr(Q.ghost, cx - r, cy - r, 2 * r, 2 * r, r, mixc(c_card(), t->primary, 90), LV_OPA_COVER);
        icon(Q.ghost, flex_qs_ctl_icon(it->id), 32, cx, cy, t->txt);
        text(Q.ghost, flex_qs_meta(it->id)->name, FLEX_FONT_S1, t->txt, cx, cy + r + 6, 1, 0);
    } else {
        blk_ui_t tmp = {0};
        flex_qs_block_t k = {FLEX_QB_ITEM, 0, 0, 0, (int16_t)w, (int16_t)h};
        build_module(&tmp, Q.ghost, &k, it);
        lv_obj_t *o = fillr(Q.ghost, 0, 0, w, h, FLEX_QP_RAD_S, t->primary, LV_OPA_TRANSP);
        lv_obj_set_style_border_color(o, t->primary, 0);
        lv_obj_set_style_border_width(o, 1, 0);
    }
}

static void place_ghost(void)
{
    if (!Q.ghost) {
        return;
    }
    int w = lv_obj_get_width(Q.ghost), h = lv_obj_get_height(Q.ghost) - 4;
    int x = Q.ed_drag_x - w / 2, y = Q.ed_drag_y - h / 2;
    x = x < 4 ? 4 : x + w > SCR_W - 4 ? SCR_W - 4 - w : x;
    lv_obj_set_pos(Q.ghost, x, y);
}

// Reconstruye el cuerpo segun el modo (lo llama todo cambio de configuracion,
// de estado de un control o de tema).
static void build_body(void)
{
    if (!Q.root) {
        return;
    }
    if (Q.body) {
        lv_obj_delete(Q.body);
    }
    if (Q.ghost) {
        lv_obj_delete(Q.ghost);   // hijo de root, no del cuerpo: se rehace abajo
        Q.ghost = NULL;
    }
    Q.body = box(Q.root, 0, 0, SCR_W, SCR_H);
    Q.view = Q.content = Q.tiles_box = Q.group_inner = Q.gbar = Q.gpill = Q.cat_grid = NULL;
    Q.flash_obj = Q.reject_obj = NULL;
    Q.time_l = Q.date_l = NULL;
    memset(Q.blk, 0, sizeof(Q.blk));
    if (Q.mode == QPM_CAT) {
        build_catalog(Q.body);
        place();
        return;
    }
    relayout();
    bool edit = Q.mode == QPM_EDIT;
    Q.view = edit ? box(Q.body, 0, QP_EDH_H, SCR_W, SCR_H - QP_EDH_H)
                  : box(Q.body, 0, FLEX_QP_VIEW_Y0, SCR_W, FLEX_QP_VIEW_H);
    Q.content = box(Q.view, 0, 0, SCR_W, Q.L.content_h + 8 > 1 ? Q.L.content_h + 8 : 1);
    // el "-" del editor asoma por encima y a la izquierda de su bloque (como en Arduino)
    lv_obj_set_overflow_visible(Q.content, true);
    flex_qs_cfg_t *c = lay_cfg();
    for (int b = 0; b < Q.L.blk_n; b++) {
        const flex_qs_block_t *k = &Q.L.blk[b];
        if (k->kind == FLEX_QB_GROUP) {
            build_group(Q.content, b, k);
        } else if (k->kind == FLEX_QB_ADD) {
            build_add(Q.content, b, k);
        } else if (edit && k->item == Q.ed_drag) {
            lv_obj_t *gap = fillr(Q.content, k->x, k->y, k->w, k->h, FLEX_QP_RAD_S, flex_th()->primary, 60);
            lv_obj_set_style_border_color(gap, flex_th()->primary, 0);
            lv_obj_set_style_border_width(gap, 1, 0);
            Q.blk[b].root = gap;
        } else {
            build_module(&Q.blk[b], Q.content, k, &c->it[k->item]);
            if (edit) {
                edit_chrome(Q.content, k, &c->it[k->item]);
            }
        }
    }
    lv_obj_set_height(Q.content, Q.L.content_h + 40);
    if (edit) {
        build_edit_header(Q.body);
    } else {
        build_header(Q.body);
    }
    place();
    build_ghost();
    place_ghost();
}

static void sync_curtain(void)
{
    if (!Q.root) {
        return;
    }
    int py = Q.panel_y < 0 ? 0 : Q.panel_y > SCR_H ? SCR_H : Q.panel_y;
    lv_obj_set_hidden(Q.root, py <= 0);
    lv_obj_set_height(Q.root, py > 0 ? py : 1);
    bool show_edge = py > 0 && py < SCR_H;
    lv_obj_set_hidden(Q.edge, !show_edge);
    if (show_edge) {
        lv_obj_set_y(Q.edge, py - 22);
    }
}

// ---- abrir / cerrar ------------------------------------------------------------
static void load_cfg(void)
{
    if (Q.loaded) {
        return;
    }
    Q.loaded = true;
    uint8_t b[FLEX_QS_BLOB_N];
    size_t rd = flex_kvs_get_blob(FLEX_QS_NVS_NS, FLEX_QS_NVS_KEY, b, sizeof(b));
    if (!flex_qs_load(&Q.cfg, b, rd, qs_keep)) {
        LV_LOG_USER("[QP] configuracion ausente o invalida: valores de fabrica");
    }
}

static void save_cfg(void)
{
    uint8_t b[FLEX_QS_BLOB_N];
    flex_qs_serialize(&Q.cfg, b);
    // A la cache; la tarea de almacenamiento lo graba despues (nunca la UI).
    flex_kvs_set_blob(FLEX_QS_NVS_NS, FLEX_QS_NVS_KEY, b, sizeof(b));
}

static void tick_cb(lv_timer_t *t);

static void ui_create(void)
{
    if (Q.root) {
        return;
    }
    Q.root = box(lv_layer_top(), 0, 0, SCR_W, 1);
    Q.bg = box(Q.root, 0, 0, SCR_W, SCR_H);
    lv_obj_add_event_cb(Q.bg, bg_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    Q.edge = box(lv_layer_top(), 0, 0, SCR_W, 22 + QS_SHADOW_H);
    lv_obj_add_event_cb(Q.edge, edge_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_set_hidden(Q.edge, true);
    build_body();
    Q.last_min = flex_clock_minute();
    Q.tick_ms = lv_tick_get();
    Q.tick = lv_timer_create(tick_cb, TICK_MS, NULL);
}

static void ui_destroy(void)
{
    if (Q.tick) {
        lv_timer_delete(Q.tick);
        Q.tick = NULL;
    }
    if (Q.edge) {
        lv_obj_delete(Q.edge);
        Q.edge = NULL;
    }
    if (Q.root) {
        lv_obj_delete(Q.root);
    }
    Q.root = Q.bg = Q.body = Q.view = Q.content = Q.tiles_box = Q.group_inner = Q.gbar = Q.gpill = Q.ghost = NULL;
    Q.cat_grid = Q.flash_obj = Q.reject_obj = Q.time_l = Q.date_l = NULL;
    memset(Q.blk, 0, sizeof(Q.blk));
}

static void reset_gesture_state(void)
{
    Q.anim_on = Q.dragging = Q.drag_moved = false;
    Q.vel = 0;
    Q.mode = QPM_PANEL;   // una edicion a medias se descarta ENTERA
    Q.ed_drag = Q.ed_resize = Q.ed_reject_id = -1;
    Q.g = QG_NONE;
    Q.ganim = false;
    Q.scroll = Q.scroll_vel = Q.gscroll_vel = 0;
    Q.flash_kind = Q.flash_id = -1;
    Q.glassfx_drag = -1;
}

static bool is_open(void)
{
    return Q.panel_y != 0 || Q.dragging || Q.anim_on;
}

// qsSettleClosed: cerrada del todo, se suelta todo lo temporal
static void settle_closed(void)
{
    Q.panel_y = 0;
    reset_gesture_state();
    if (Q.save_panel) {
        Q.save_panel = false;
        save_cfg();
    }
    ui_destroy();
}

void flex_qs_close_now(void)
{
    if (!is_open() && !Q.root) {
        return;
    }
    bool finger = flex_touch_arb()->t.down;
    settle_closed();
    if (finger) {
        flex_touch_drop_all();   // el dedo que sigue apoyado no toca lo de debajo
    }
}

static void anim_to(int target)
{
    target = target < 0 ? 0 : target > SCR_H ? SCR_H : target;
    int dist = abs(target - Q.panel_y);
    if (dist == 0) {
        Q.anim_on = false;
        Q.panel_y = target;
        if (target <= 0) {
            settle_closed();
        } else {
            sync_curtain();
        }
        return;
    }
    Q.anim_from = Q.panel_y;
    Q.anim_dest = target;
    Q.anim_t0 = lv_tick_get();
    Q.anim_dur = 150 + (uint32_t)dist * 150u / SCR_H;
    Q.anim_on = true;
}

static void begin_open(void)
{
    load_cfg();
    Q.over_app = flex_shell_state() == FLEX_SH_APP;
    reset_gesture_state();
    Q.gh = (float)flex_qp_group_h(Q.cfg.grows);
    Q.gscroll = 0;
    ui_create();
    lv_obj_move_foreground(Q.root);
    lv_obj_move_foreground(Q.edge);
}

static void grab_curtain(int y)
{
    Q.anim_on = false;
    Q.dragging = true;
    Q.drag_moved = false;
    Q.drag_base = Q.panel_y;
    Q.drag_y0 = y;
    Q.prev_y = y;
    Q.prev_ms = lv_tick_get();
    Q.vel = 0;
    Q.g = QG_CURTAIN;
}

static bool can_open(void)
{
    flex_shell_state_t st = flex_shell_state();
    // kiosco: la cortina no se abre (QuickPanelEdit.h:492)
    return (st == FLEX_SH_HOME || st == FLEX_SH_APP) && !flex_power_suspended() && !flex_kiosk_active();
}

void flex_qs_open(void)
{
    if (is_open()) {
        return;
    }
    if (!can_open()) {
        return;
    }
    begin_open();
    anim_to(SCR_H);
}

bool flex_qs_is_open(void)
{
    return is_open();
}

int flex_qs_panel_y(void)
{
    return Q.panel_y;
}

// ---- acciones de los controles (qpExecCtl) ----------------------------------------
static void leave_to_app(int app)
{
    flex_qs_close_now();
    flex_app_launch(app, NULL);
}

static void flash_set(int kind, int id)
{
    Q.flash_kind = kind;
    Q.flash_id = id;
    Q.flash_ms = lv_tick_get();
}

static bool exec_ctl(int id, bool detail)
{
    const flex_qs_meta_t *m = flex_qs_meta(id);
    if (!m || !qs_shown(id)) {
        return false;
    }
    if (detail) {
        if (!ctl_has_detail(id)) {
            return false;
        }
        leave_to_app(IC_AJUSTES);   // Ajustes (y su pagina) cuando la app exista
        return true;
    }
    switch (id) {
    case FLEX_QS_SETTINGS:
    case FLEX_QS_CONN: leave_to_app(IC_AJUSTES); return true;
    case FLEX_QS_DEX: leave_to_app(IC_MODOPC); return true;
    case FLEX_QS_CAMERA: leave_to_app(IC_CAMARA); return true;
    case FLEX_QS_GALLERY: leave_to_app(IC_GALERIA); return true;
    case FLEX_QS_FILES: leave_to_app(IC_ALMACEN); return true;
    case FLEX_QS_LOCK:
        flex_qs_close_now();
        flex_power_suspend();   // al despertar sale el bloqueo
        return true;
    case FLEX_QS_POWEROFF:
        flex_qs_close_now();
        flex_poweroff_open();
        return true;
    case FLEX_QS_AIRPLANE:
        // Sin radios todavia en ESP-IDF: el estado es real (lo leera el Wi-Fi).
        flex_cfg_set_bool("airpl", !airplane());
        break;
    case FLEX_QS_DND: flex_notif_set_dnd(!flex_notif_dnd()); break;
    case FLEX_QS_THEME: flex_theme_set_dark(!flex_look()->dark); return false;   // el aviso de tema rehace el panel
    case FLEX_QS_GLASS: flex_theme_set_glass(!flex_look()->glass); return false;
    case FLEX_QS_GLASSFX:
        if (flex_look()->glass_level != GLASS_LVL_DEF) {
            flex_theme_set_glass_level(GLASS_LVL_DEF);   // restablecer
        }
        return false;
    default: return false;
    }
    build_body();
    return false;
}

// Quitar en el editor. La configuracion conserva controles ocultos (sin backend
// todavia), asi que "el ultimo" es el ultimo que SE VE: el panel nunca queda vacio.
static bool ed_remove(int idx)
{
    if (idx < 0 || idx >= Q.ed.n) {
        return false;
    }
    const flex_qs_item_t *it = &Q.ed.it[idx];
    if (it->vis && qs_shown(it->id)) {
        int shown = 0;
        for (int i = 0; i < Q.ed.n; i++) {
            shown += Q.ed.it[i].vis && qs_shown(Q.ed.it[i].id);
        }
        if (shown <= 1) {
            return false;
        }
    }
    return flex_qs_edit_remove(&Q.ed, idx);
}

// ---- hit-test (QuickPanelGlass.h:1348-1404) ------------------------------------------
static int hdr_btn_at(int px, int py)
{
    if (py < 0 || py > FLEX_QP_HDR_H) {
        return -1;
    }
    for (int i = 0; i < 3; i++) {
        if (i == 1 && !QS_HAS_POWEROFF) {
            continue;
        }
        int cx = HBTN_CX[i], cy = QP_HBTN_CY, r = QP_TOUCH_MIN / 2 + 2;
        if (px >= cx - r && px <= cx + r && py >= cy - r && py <= cy + r) {
            return i;
        }
    }
    return -1;
}

static int block_at(int px, int py)
{
    int top = view_top();
    for (int b = 0; b < Q.L.blk_n; b++) {
        const flex_qs_block_t *k = &Q.L.blk[b];
        int by = top + k->y;
        if (px >= k->x && px < k->x + k->w && py >= by && py < by + k->h) {
            return b;
        }
    }
    return -1;
}

static bool on_group_handle(int px, int py)
{
    if (Q.L.group_blk < 0) {
        return false;
    }
    const flex_qs_block_t *k = &Q.L.blk[Q.L.group_blk];
    int gy = view_top() + k->y, hy = gy + k->h - FLEX_QP_HANDLE_H;
    return px >= FLEX_QP_MX && px <= FLEX_QP_MX + FLEX_QP_CONT_W && py >= hy - 10 && py <= gy + k->h + 14;
}

static int tile_at(int px, int py)
{
    if (Q.L.group_blk < 0) {
        return -1;
    }
    const flex_qs_block_t *k = &Q.L.blk[Q.L.group_blk];
    int gy = view_top() + k->y;
    int inner_top = gy + FLEX_QP_GPAD, inner_bot = gy + k->h - FLEX_QP_HANDLE_H;
    if (py < inner_top || py >= inner_bot) {
        return -1;
    }
    int gy_top = inner_top - (int)(Q.gscroll + 0.5f);
    for (int i = 0; i < Q.L.tile_n; i++) {
        int cx, cy;
        flex_qs_tile_center(i, gy_top, &cx, &cy);
        int hw = FLEX_QP_TCOLW / 2, hh = FLEX_QP_TROW / 2;
        hw = hw < QP_TOUCH_MIN / 2 ? QP_TOUCH_MIN / 2 : hw;
        if (px >= cx - hw && px <= cx + hw && py >= cy - FLEX_QP_TCIRC / 2 - 6 && py <= cy + hh) {
            return i;
        }
    }
    return -1;
}

static bool group_can_scroll(void)
{
    return flex_qs_total_rows(&Q.L) * FLEX_QP_TROW > flex_qs_group_inner_h(Q.lay_px) + 2;
}

static float rubber(float over)
{
    return over * QP_RUBBER;
}

static void clamp_scroll(bool elastic)
{
    int mx = flex_qs_scroll_max(&Q.L);
    if (Q.scroll < 0) {
        Q.scroll = elastic ? rubber(Q.scroll) : 0;
    }
    if (Q.scroll > mx) {
        Q.scroll = elastic ? mx + rubber(Q.scroll - mx) : (float)mx;
    }
}

static void clamp_gscroll(bool elastic)
{
    int mx = flex_qs_gscroll_max(&Q.L, Q.lay_px);
    if (Q.gscroll < 0) {
        Q.gscroll = elastic ? rubber(Q.gscroll) : 0;
    }
    if (Q.gscroll > mx) {
        Q.gscroll = elastic ? mx + rubber(Q.gscroll - mx) : (float)mx;
    }
}

static int slider_value(int b, int x)
{
    const flex_qs_block_t *k = &Q.L.blk[b];
    int id = Q.cfg.it[k->item].id;
    int th = k->h - 20 < 40 ? 40 : k->h - 20;
    int w = id == FLEX_QS_GLASSFX ? k->w - th - 8 : k->w;
    int run = w - th < 1 ? 1 : w - th;
    int v = (x - k->x - th / 2) * 100 / run;
    return v < 0 ? 0 : v > 100 ? 100 : v;
}

static bool glassfx_reset_hit(const flex_qs_block_t *k, int px)
{
    int th = k->h - 20 < 40 ? 40 : k->h - 20;
    return px >= k->x + (k->w - th - 8) + 4;
}

// qsTapTile: circulos primero, modulos despues
static void tap_tile(int px, int py)
{
    int k = tile_at(px, py);
    if (k >= 0 && k < Q.L.tile_n) {
        int id = Q.cfg.it[Q.L.tiles[k]].id;
        flash_set(0, id);
        if (!exec_ctl(id, false) && Q.root) {
            build_body();
        }
        return;
    }
    int b = block_at(px, py);
    if (b >= 0 && Q.L.blk[b].kind == FLEX_QB_ITEM) {
        int i = Q.L.blk[b].item;
        if (i >= 0 && i < Q.cfg.n) {
            int id = Q.cfg.it[i].id;
            const flex_qs_block_t *k2 = &Q.L.blk[b];
            if (id == FLEX_QS_GLASSFX && !glassfx_reset_hit(k2, px)) {
                return;   // el toque en la pista no restablece
            }
            flash_set(1, id);
            if (!exec_ctl(id, false) && Q.root) {
                build_body();
            }
        }
    }
}

// ---- editor (QuickPanelEdit.h) ------------------------------------------------------
static void edit_enter(void)
{
    Q.ed = Q.cfg;
    Q.ed_drag = Q.ed_resize = Q.ed_reject_id = -1;
    Q.scroll = Q.scroll_vel = 0;
    Q.g = QG_NONE;
    Q.mode = QPM_EDIT;
    build_body();
}

static void edit_cancel(void)
{
    Q.mode = QPM_PANEL;
    Q.ed_drag = Q.ed_resize = -1;
    Q.g = QG_NONE;
    Q.scroll = Q.scroll_vel = 0;
    build_body();
}

static void edit_commit(void)
{
    Q.ed.n = flex_qs_normalize(Q.ed.it, Q.ed.n, &Q.ed.grows, qs_keep);
    if (Q.ed.n == 0) {
        flex_qs_factory(&Q.cfg, qs_keep);
        Q.cfg.n = flex_qs_normalize(Q.cfg.it, Q.cfg.n, &Q.cfg.grows, qs_keep);
    } else {
        Q.cfg = Q.ed;
    }
    save_cfg();
    Q.gh = (float)flex_qp_group_h(Q.cfg.grows);
    Q.gscroll = Q.gscroll_vel = 0;
    Q.mode = QPM_PANEL;
    Q.ed_drag = Q.ed_resize = -1;
    Q.g = QG_NONE;
    Q.scroll = Q.scroll_vel = 0;
    build_body();
}

static void edit_reset(void)
{
    flex_qs_factory(&Q.ed, qs_keep);
    Q.ed.n = flex_qs_normalize(Q.ed.it, Q.ed.n, &Q.ed.grows, qs_keep);   // lo vivo no cambia hasta "Listo"
    Q.ed_drag = Q.ed_resize = -1;
    build_body();
}

static void reject(int id)
{
    Q.ed_reject_id = id;
    Q.ed_reject_ms = lv_tick_get();
    build_body();
}

static int ed_item_at(int px, int py, int *blk_out)
{
    *blk_out = -1;
    int b = block_at(px, py);
    if (b < 0) {
        return -1;
    }
    *blk_out = b;
    if (Q.L.blk[b].kind == FLEX_QB_ITEM) {
        return Q.L.blk[b].item;
    }
    if (Q.L.blk[b].kind == FLEX_QB_GROUP) {
        int k = tile_at(px, py);
        if (k >= 0 && k < Q.L.tile_n) {
            return Q.L.tiles[k];
        }
    }
    return -1;
}

static void cat_build(void)
{
    Q.cat_n = flex_qs_catalog(&Q.ed, Q.cat_ids, qs_shown);
}

static bool edit_touch(const flex_arb_touch_t *t)
{
    uint32_t now = lv_tick_get();
    if (t->pressed && Q.g == QG_NONE) {
        Q.gx0 = t->x;
        Q.gy0 = t->y;
        Q.g_prev_y = t->y;
        Q.g_prev_ms = now;
        Q.g_long = false;
        Q.ed_pend_idx = Q.ed_pend_blk = -1;
        Q.scroll_vel = 0;
        if (t->y < QP_EDH_H) {
            Q.g = QG_PENDING;
            return true;
        }
        int blk;
        int idx = ed_item_at(t->x, t->y, &blk);
        if (blk >= 0 && Q.L.blk[blk].kind == FLEX_QB_ADD) {
            Q.g = QG_PENDING;
            Q.ed_pend_blk = blk;
            return true;
        }
        if (idx >= 0 && blk >= 0) {
            const flex_qs_block_t *k = &Q.L.blk[blk];
            int top = view_top();
            int bx = k->x, by = top + k->y, bw = k->w, bh = k->h;
            flex_qs_item_t *it = &Q.ed.it[idx];
            uint8_t nw, nh;
            bool resizable = flex_qs_next_size(it->id, it->w, it->h, +1, &nw, &nh) ||
                             flex_qs_next_size(it->id, it->w, it->h, -1, &nw, &nh);
            if (k->kind == FLEX_QB_ITEM) {
                if (t->x <= bx + 26 && t->y <= by + 26) {   // "-"
                    if (!ed_remove(idx)) {
                        reject(it->id);
                    } else {
                        build_body();
                    }
                    Q.g = QG_NONE;
                    return true;
                }
                if (t->x >= bx + bw - 26) {   // asa de redimension
                    if (resizable) {
                        Q.g = QG_EDDRAG;
                        Q.ed_resize = idx;
                        Q.ed_res_x0 = t->x;
                        Q.ed_drag = -1;
                        return true;
                    }
                    reject(it->id);
                    Q.g = QG_NONE;
                    return true;
                }
                if (flex_qs_meta(it->id)->oris == (FLEX_QOR_H | FLEX_QOR_V) && t->x <= bx + 30 && t->y >= by + bh - 30) {
                    it->ori = it->ori == FLEX_QOR_H ? FLEX_QOR_V : FLEX_QOR_H;
                    build_body();
                    Q.g = QG_NONE;
                    return true;
                }
            } else {
                int kt = tile_at(t->x, t->y);
                if (kt >= 0) {
                    int gy_top = by + FLEX_QP_GPAD - (int)(Q.gscroll + 0.5f);
                    int cx, cy;
                    flex_qs_tile_center(kt, gy_top, &cx, &cy);
                    if (t->x <= cx - FLEX_QP_TCIRC / 2 + 20 && t->y <= cy - FLEX_QP_TCIRC / 2 + 20) {
                        if (!ed_remove(idx)) {
                            reject(it->id);
                        } else {
                            build_body();
                        }
                        Q.g = QG_NONE;
                        return true;
                    }
                    if (t->x >= cx + FLEX_QP_TCOLW / 2 - 22 && t->y >= cy - 24 && t->y <= cy + 24) {
                        if (resizable) {
                            Q.g = QG_EDDRAG;
                            Q.ed_resize = idx;
                            Q.ed_res_x0 = t->x;
                            Q.ed_drag = -1;
                            return true;
                        }
                        reject(it->id);
                        Q.g = QG_NONE;
                        return true;
                    }
                }
            }
            Q.ed_pend_idx = idx;
            Q.ed_pend_blk = blk;
        }
        Q.g = QG_PENDING;
        return true;
    }
    if (Q.g == QG_EDDRAG) {
        if (t->down) {
            int d = t->x - Q.ed_res_x0;
            if (abs(d) >= 46 && Q.ed_resize >= 0 && Q.ed_resize < Q.ed.n) {
                flex_qs_item_t *it = &Q.ed.it[Q.ed_resize];
                uint8_t nw, nh;
                Q.ed_res_x0 = t->x;
                if (flex_qs_next_size(it->id, it->w, it->h, d > 0 ? +1 : -1, &nw, &nh)) {
                    it->w = nw;
                    it->h = nh;
                    build_body();
                } else {
                    reject(it->id);
                }
            }
            return true;
        }
        Q.g = QG_NONE;
        Q.ed_resize = -1;
        return true;
    }
    if (Q.g == QG_SCROLL && Q.ed_drag >= 0) {   // mover un elemento
        if (t->down) {
            Q.ed_drag_x = t->x;
            Q.ed_drag_y = t->y;
            uint32_t dt = now - Q.g_prev_ms;
            dt = dt < 1 ? 1 : dt > 100 ? 100 : dt;
            Q.g_prev_ms = now;
            const float AUTO = 0.45f;
            if (t->y < QP_EDH_H + 60) {
                Q.scroll -= AUTO * (float)dt;
                clamp_scroll(false);
            } else if (t->y > SCR_H - 70) {
                Q.scroll += AUTO * (float)dt;
                clamp_scroll(false);
            }
            int blk;
            int tgt = ed_item_at(t->x, t->y, &blk);
            if (tgt >= 0 && tgt != Q.ed_drag) {
                flex_qs_edit_move(&Q.ed, Q.ed_drag, tgt);   // reordenamiento en tiempo real
                Q.ed_drag = tgt;
                build_body();
            } else {
                place();
                place_ghost();
            }
            return true;
        }
        Q.ed_drag = -1;
        Q.g = QG_NONE;
        build_body();
        return true;
    }
    if (Q.g == QG_SCROLL) {
        if (t->down) {
            uint32_t dt = now - Q.g_prev_ms;
            dt = dt < 1 ? 1 : dt > 100 ? 100 : dt;
            int d = Q.g_prev_y - t->y;
            Q.scroll += d;
            clamp_scroll(true);
            Q.scroll_vel += ((float)d / (float)dt - Q.scroll_vel) * ((float)dt / ((float)dt + 45.0f));
            Q.g_prev_y = t->y;
            Q.g_prev_ms = now;
            place();
            return true;
        }
        Q.g = QG_NONE;
        return true;
    }
    if (Q.g == QG_PENDING) {
        if (t->down) {
            if (Q.ed_pend_idx >= 0 && !Q.g_long && now - t->down_ms > QP_EDLONG_MS) {
                Q.g_long = true;   // mantener -> mover
                Q.ed_drag = Q.ed_pend_idx;
                Q.ed_drag_x = t->x;
                Q.ed_drag_y = t->y;
                Q.g = QG_SCROLL;
                build_body();
                return true;
            }
            if (abs(t->y - Q.gy0) > QP_DRAG_TH) {
                Q.g = QG_SCROLL;
                Q.ed_drag = -1;
                Q.g_prev_y = t->y;
                Q.g_prev_ms = now;
            }
            return true;
        }
        Q.g = QG_NONE;
        if (Q.g_long) {
            return true;
        }
        if (Q.gy0 < QP_EDH_H) {
            if (Q.gy0 >= QP_EDH_BTN_Y0 && Q.gy0 <= QP_EDH_BTN_Y1) {
                if (Q.gx0 < 150) {
                    edit_cancel();
                } else if (Q.gx0 > SCR_W - 150) {
                    edit_commit();
                }
                return true;
            }
            if (Q.gy0 >= QP_EDH_RST_Y0 && Q.gx0 > SCR_W / 2 - 110 && Q.gx0 < SCR_W / 2 + 110) {
                edit_reset();
            }
            return true;
        }
        if (Q.ed_pend_blk >= 0 && Q.ed_pend_blk < Q.L.blk_n && Q.L.blk[Q.ed_pend_blk].kind == FLEX_QB_ADD) {
            cat_build();
            Q.cat_sel = -1;
            Q.cat_scroll = 0;
            Q.mode = QPM_CAT;
            build_body();
        }
        return true;
    }
    if (!t->down && Q.g != QG_NONE) {
        Q.g = QG_NONE;
    }
    return true;
}

static int cat_scroll_max(void)
{
    int m = ((Q.cat_n + 3) / 4) * QP_CAT_ROW + 24 - QP_CAT_VIEWH;
    return m > 0 ? m : 0;
}

static int cat_at(int px, int py)
{
    if (py < QP_CAT_HDR) {
        return -1;
    }
    int top = QP_CAT_HDR - (int)(Q.cat_scroll + 0.5f) + 12;
    for (int k = 0; k < Q.cat_n; k++) {
        int cx = flex_qp_col_x(k % 4) + FLEX_QP_CW / 2, cy = top + (k / 4) * QP_CAT_ROW + FLEX_QP_TCIRC / 2;
        int hw = FLEX_QP_CW / 2 < QP_TOUCH_MIN / 2 ? QP_TOUCH_MIN / 2 : FLEX_QP_CW / 2;
        if (px >= cx - hw && px <= cx + hw && py >= cy - FLEX_QP_TCIRC / 2 - 6 && py <= cy + QP_CAT_ROW / 2) {
            return k;
        }
    }
    return -1;
}

static bool cat_touch(const flex_arb_touch_t *t)
{
    uint32_t now = lv_tick_get();
    if (t->pressed && Q.g == QG_NONE) {
        Q.gx0 = t->x;
        Q.gy0 = t->y;
        Q.g_prev_y = t->y;
        Q.g_prev_ms = now;
        Q.g_long = false;
        Q.g = QG_PENDING;
        return true;
    }
    if (Q.g == QG_CATSCROLL) {
        int mx = cat_scroll_max();
        if (t->down) {
            Q.cat_scroll += Q.g_prev_y - t->y;
            if (Q.cat_scroll < 0) {
                Q.cat_scroll = rubber(Q.cat_scroll);
            }
            if (Q.cat_scroll > mx) {
                Q.cat_scroll = mx + rubber(Q.cat_scroll - mx);
            }
            Q.g_prev_y = t->y;
            Q.g_prev_ms = now;
            place();
            return true;
        }
        Q.cat_scroll = Q.cat_scroll < 0 ? 0 : Q.cat_scroll > mx ? (float)mx : Q.cat_scroll;
        Q.g = QG_NONE;
        place();
        return true;
    }
    if (Q.g == QG_PENDING) {
        if (t->down) {
            if (abs(t->y - Q.gy0) > QP_DRAG_TH) {
                Q.g = QG_CATSCROLL;
                Q.g_prev_y = t->y;
                Q.g_prev_ms = now;
            }
            return true;
        }
        Q.g = QG_NONE;
        if (Q.gy0 < QP_CAT_HDR) {
            if (Q.gx0 < 150) {   // "Atras": al editor sin perder lo editado
                Q.mode = QPM_EDIT;
                build_body();
            }
            return true;
        }
        int k = cat_at(Q.gx0, Q.gy0);
        if (k >= 0 && k < Q.cat_n) {
            if (flex_qs_edit_add(&Q.ed, Q.cat_ids[k], qs_shown)) {
                Q.cat_sel = -1;
                Q.mode = QPM_EDIT;
                relayout();
                Q.scroll = (float)flex_qs_scroll_max(&Q.L);
                build_body();
            } else {
                Q.cat_sel = k;
                build_body();
            }
        }
        return true;
    }
    if (!t->down && Q.g != QG_NONE) {
        Q.g = QG_NONE;
    }
    return true;
}

// ---- panel abierto (qpPanelTouch) ---------------------------------------------------
static void group_snap(void)
{
    uint8_t rows = Q.cfg.grows;
    int best = flex_qs_group_snap(&Q.L, Q.gh, &rows);
    Q.cfg.grows = rows;
    Q.gfrom = Q.gh;
    Q.gto = (float)best;
    Q.gt0 = lv_tick_get();
    Q.gdur = flex_qs_group_snap_ms(Q.gfrom, Q.gto);
    Q.ganim = true;
}

static bool panel_touch(const flex_arb_touch_t *t)
{
    uint32_t now = lv_tick_get();
    uint32_t dt = now - Q.g_prev_ms;
    dt = dt < 1 ? 1 : dt > 100 ? 100 : dt;
    if (t->pressed && Q.g == QG_NONE) {
        Q.gx0 = t->x;
        Q.gy0 = t->y;
        Q.g_prev_y = t->y;
        Q.g_prev_ms = now;
        Q.g_long = false;
        Q.tgt_blk = Q.tgt_tile = Q.tgt_hdr = -1;
        Q.scroll_vel = Q.gscroll_vel = 0;
        Q.ganim = false;
        if (Q.panel_y < SCR_H) {
            Q.g = QG_CURTAIN;   // a medio abrir no hay controles: manda la cortina
        } else if (t->y < FLEX_QP_HDR_H) {
            Q.tgt_hdr = hdr_btn_at(t->x, t->y);
            Q.g = Q.tgt_hdr >= 0 ? QG_PENDING : QG_CURTAIN;
        } else if (t->y > SCR_H - FLEX_QP_FOOT_H) {
            Q.g = QG_CURTAIN;
        } else if (on_group_handle(t->x, t->y)) {
            Q.g = QG_RESIZE;
            Q.g_base = Q.gh;
        } else {
            int b = block_at(t->x, t->y);
            Q.tgt_blk = b;
            Q.g = QG_PENDING;
            if (b >= 0 && Q.L.blk[b].kind == FLEX_QB_ITEM) {
                int i = Q.L.blk[b].item, id = Q.cfg.it[i].id;
                if (flex_qs_meta(id)->type == FLEX_QT_SLIDER &&
                    !(id == FLEX_QS_GLASSFX && glassfx_reset_hit(&Q.L.blk[b], t->x))) {
                    Q.g = QG_SLIDER;
                }
            } else if (b >= 0 && Q.L.blk[b].kind == FLEX_QB_GROUP) {
                Q.tgt_tile = tile_at(t->x, t->y);
            }
            Q.g_base = Q.scroll;
        }
        if (Q.g == QG_CURTAIN) {
            grab_curtain(t->y);
        }
    }

    if (Q.g == QG_SLIDER) {
        int b = Q.tgt_blk;
        if (t->down) {
            if (b >= 0 && b < Q.L.blk_n && Q.L.blk[b].kind == FLEX_QB_ITEM) {
                int id = Q.cfg.it[Q.L.blk[b].item].id;
                int v = slider_value(b, t->x);
                if (id == FLEX_QS_GLASSFX) {
                    // solo el indicador, en pasos de 5; el material al soltar
                    int q = ((v + GLASS_LVL_STEP / 2) / GLASS_LVL_STEP) * GLASS_LVL_STEP;
                    q = q > 100 ? 100 : q;
                    if (q != glass_lvl()) {
                        Q.glassfx_drag = q;
                        slider_update(b, id);
                    }
                } else if (id == FLEX_QS_BRIGHT) {
                    int nv = v < 5 ? 5 : v;   // setBacklight: suelo del 5 %
                    if (nv != bright()) {
                        flex_display_set_brightness((uint8_t)nv);
                        flex_cfg_set_i32("bright", nv);   // cache: la flash, despues y fuera de la UI
                        slider_update(b, id);
                    }
                }
            }
            return true;
        }
        Q.g = QG_NONE;
        if (Q.glassfx_drag >= 0) {
            int v = Q.glassfx_drag;
            Q.glassfx_drag = -1;
            if (v != flex_look()->glass_level) {
                flex_theme_set_glass_level((uint8_t)v);   // rehace vidrio y fondos; el aviso rehace el panel
            } else if (b >= 0 && b < Q.L.blk_n) {
                slider_update(b, FLEX_QS_GLASSFX);
            }
        }
        return true;
    }

    if (Q.g == QG_CURTAIN) {
        if (t->down) {
            if (!Q.drag_moved) {
                if (abs(t->y - Q.drag_y0) <= 6) {
                    Q.prev_y = t->y;
                    Q.prev_ms = now;
                    return true;
                }
                Q.drag_moved = true;
            }
            float d = (float)(now - Q.prev_ms);
            d = d < 1 ? 1 : d > 100 ? 100 : d;
            int target = Q.drag_base + (t->y - Q.drag_y0);   // 1:1 con el dedo
            target = target < 0 ? 0 : target > SCR_H ? SCR_H : target;
            float inst = (float)(t->y - Q.prev_y) / d;
            Q.vel += (inst - Q.vel) * (d / (d + QS_VEL_TAU));
            Q.prev_y = t->y;
            Q.prev_ms = now;
            if (target != Q.panel_y) {
                Q.panel_y = target;
                sync_curtain();
            }
            return true;
        }
        Q.dragging = false;
        Q.g = QG_NONE;
        if (!Q.drag_moved) {
            Q.panel_y = Q.drag_base;
            if (Q.panel_y >= SCR_H && t->tap && Q.gy0 > SCR_H - FLEX_QP_FOOT_H) {
                anim_to(0);   // un toque cierra solo desde el asa inferior
            } else if (Q.panel_y > 0) {
                anim_to(Q.vel > QS_FLICK ? SCR_H : Q.vel < -QS_FLICK ? 0
                        : Q.panel_y >= SCR_H * QS_OPEN_PCT / 100 ? SCR_H : 0);
            } else {
                settle_closed();
            }
            return true;
        }
        anim_to(Q.vel > QS_FLICK ? SCR_H : Q.vel < -QS_FLICK ? 0 : Q.panel_y >= SCR_H * QS_OPEN_PCT / 100 ? SCR_H : 0);
        return true;
    }

    if (Q.g == QG_RESIZE) {
        if (t->down) {
            float lo = (float)flex_qs_group_min_px(), hi = (float)flex_qs_group_max_px(&Q.L);
            float h = Q.g_base + (float)(t->y - Q.gy0);
            if (h < lo) {
                h = lo + rubber(h - lo);
            }
            if (h > hi) {
                h = hi + rubber(h - hi);
            }
            h = h < lo - 40 ? lo - 40 : h > hi + 40 ? hi + 40 : h;
            if ((int)h != (int)Q.gh) {
                Q.gh = h;
                relayout();
                place();
            }
            return true;
        }
        Q.g = QG_NONE;
        group_snap();
        Q.save_panel = true;
        return true;
    }

    if (Q.g == QG_SCROLL || Q.g == QG_GSCROLL) {
        if (t->down) {
            int d = Q.g_prev_y - t->y;
            float inst = (float)d / (float)dt;
            if (Q.g == QG_SCROLL) {
                Q.scroll += d;
                clamp_scroll(true);
                Q.scroll_vel += (inst - Q.scroll_vel) * ((float)dt / ((float)dt + 45.0f));
            } else {
                Q.gscroll += d;
                clamp_gscroll(true);
                Q.gscroll_vel += (inst - Q.gscroll_vel) * ((float)dt / ((float)dt + 45.0f));
            }
            Q.g_prev_y = t->y;
            Q.g_prev_ms = now;
            place();
            return true;
        }
        Q.g = QG_NONE;
        return true;
    }

    if (Q.g == QG_PENDING) {
        if (t->down) {
            if (abs(t->y - Q.gy0) > QP_DRAG_TH || abs(t->x - Q.gx0) > QP_DRAG_TH * 2) {
                if (Q.tgt_hdr >= 0 || Q.gy0 < FLEX_QP_HDR_H) {   // nacido en la cabecera: la cortina
                    Q.tgt_hdr = -1;
                    grab_curtain(Q.gy0);
                    Q.prev_y = t->y;
                    return true;
                }
                bool in_group = Q.tgt_blk >= 0 && Q.tgt_blk < Q.L.blk_n && Q.L.blk[Q.tgt_blk].kind == FLEX_QB_GROUP;
                Q.g = in_group && group_can_scroll() ? QG_GSCROLL : QG_SCROLL;
                Q.g_prev_y = t->y;
                Q.g_prev_ms = now;
                Q.scroll_vel = Q.gscroll_vel = 0;
                return true;
            }
            if (!Q.g_long && now - t->down_ms > QP_LONG_MS) {
                Q.g_long = true;
                int id = -1;
                if (Q.tgt_tile >= 0 && Q.tgt_tile < Q.L.tile_n) {
                    id = Q.cfg.it[Q.L.tiles[Q.tgt_tile]].id;
                } else if (Q.tgt_blk >= 0 && Q.L.blk[Q.tgt_blk].kind == FLEX_QB_ITEM) {
                    id = Q.cfg.it[Q.L.blk[Q.tgt_blk].item].id;
                }
                if (id >= 0 && ctl_has_detail(id)) {
                    exec_ctl(id, true);
                }
            }
            return true;
        }
        Q.g = QG_NONE;
        if (Q.g_long) {
            return true;
        }
        if (Q.tgt_hdr >= 0) {
            int h = Q.tgt_hdr;
            Q.tgt_hdr = -1;
            if (h == 0) {
                edit_enter();
            } else if (h == 1) {
                flex_qs_close_now();
                flex_poweroff_open();
            } else if (h == 2) {
                leave_to_app(IC_AJUSTES);
            }
            return true;
        }
        tap_tile(t->x, t->y);   // un toque en el vacio NO cierra
        return true;
    }
    if (!t->down && Q.g != QG_NONE) {
        Q.g = QG_NONE;
    }
    return true;
}

// ---- punto de entrada global (qsGlobalHandle) ---------------------------------------

static bool hook(const flex_arb_touch_t *t)
{
    if (!can_open()) {
        if (is_open() || Q.root) {
            flex_qs_close_now();
        }
        return false;
    }
    if (Q.anim_on) {
        if (!t->pressed) {
            return true;
        }
        // tocar durante la animacion la cancela y devuelve el control al dedo
        grab_curtain(t->y);
        return true;
    }
    if (Q.panel_y > 0 || Q.dragging) {
        if (Q.panel_y < SCR_H && Q.g != QG_CURTAIN && Q.g != QG_NONE) {
            Q.g = QG_NONE;
        }
        // Episodio anulado (segundo dedo, touchDropAll): el dedo no se levanto,
        // el arbitraje se lo trago. Lo que iba a ser un toque NO lo es (Arduino
        // lo ejecutaba: el doble toque con dos dedos cambiaba el tema o abria
        // la camara antes de suspender).
        if (Q.g == QG_PENDING && !t->down && !t->released) {
            Q.g = QG_NONE;
            Q.g_long = false;
            return true;
        }
        if (Q.panel_y >= SCR_H && Q.mode == QPM_EDIT) {
            return edit_touch(t);
        }
        if (Q.panel_y >= SCR_H && Q.mode == QPM_CAT) {
            return cat_touch(t);
        }
        return panel_touch(t);
    }
    // Dos bordes: el superior (se agarra en el cuadro del apoyo y sigue al dedo)
    // y el derecho (con intencion: hacia dentro mas que en vertical; Arduino lo
    // evaluaba solo en el cuadro del apoyo y no se disparaba nunca).
    bool from_top = t->pressed && t->start_y < QS_EDGE_H;
    bool from_right = t->down && !t->pressed && t->start_x > SCR_W - QS_EDGE_W && t->start_y > QS_EDGE_H &&
                      (t->start_x - t->x) > QS_EDGE_SLOP && (t->start_x - t->x) > abs(t->y - t->start_y);
    if (!from_top && !from_right) {
        return false;
    }
    begin_open();
    grab_curtain(t->y);
    if (from_right) {
        // Abre sola: el dedo va en horizontal y la cortina en vertical. El resto
        // del episodio lo traga la animacion; NO queda un arrastre de cortina
        // pendiente (al acabar la animacion se leeria como "soltar sin mover"
        // y cerraria lo que acaba de abrir).
        Q.dragging = false;
        Q.g = QG_NONE;
        anim_to(SCR_H);
    }
    return true;
}

// ---- reloj del panel (qsTick) ------------------------------------------------------------
static void scroll_step(uint32_t dt)
{
    bool moved = false;
    if (Q.g != QG_SCROLL && fabsf(Q.scroll_vel) > 0.01f) {
        Q.scroll += Q.scroll_vel * (float)dt;
        Q.scroll_vel *= expf(-(float)dt / 190.0f);
        if (fabsf(Q.scroll_vel) < 0.01f) {
            Q.scroll_vel = 0;
        }
        moved = true;
    }
    if (Q.g != QG_SCROLL) {
        int mx = Q.mode == QPM_CAT ? 0 : flex_qs_scroll_max(&Q.L);
        if (Q.scroll < 0 || Q.scroll > mx) {
            float tgt = Q.scroll < 0 ? 0.0f : (float)mx;
            Q.scroll += (tgt - Q.scroll) * (1.0f - expf(-(float)dt / 90.0f));
            if (fabsf(tgt - Q.scroll) < 0.5f) {
                Q.scroll = tgt;
                Q.scroll_vel = 0;
            }
            moved = true;
        }
    }
    if (Q.g != QG_GSCROLL && fabsf(Q.gscroll_vel) > 0.01f) {
        Q.gscroll += Q.gscroll_vel * (float)dt;
        Q.gscroll_vel *= expf(-(float)dt / 190.0f);
        if (fabsf(Q.gscroll_vel) < 0.01f) {
            Q.gscroll_vel = 0;
        }
        moved = true;
    }
    if (Q.g != QG_GSCROLL) {
        int mx = flex_qs_gscroll_max(&Q.L, Q.lay_px);
        if (Q.gscroll < 0 || Q.gscroll > mx) {
            float tgt = Q.gscroll < 0 ? 0.0f : (float)mx;
            Q.gscroll += (tgt - Q.gscroll) * (1.0f - expf(-(float)dt / 90.0f));
            if (fabsf(tgt - Q.gscroll) < 0.5f) {
                Q.gscroll = tgt;
                Q.gscroll_vel = 0;
            }
            moved = true;
        }
    }
    if (moved) {
        place();
    }
}

static void tick_cb(lv_timer_t *tm)
{
    (void)tm;
    uint32_t now = lv_tick_get();
    uint32_t dt = now - Q.tick_ms;
    dt = dt < 1 ? 1 : dt > 100 ? 100 : dt;
    Q.tick_ms = now;
    if (Q.anim_on) {
        uint32_t e = now - Q.anim_t0;
        e = e > Q.anim_dur ? Q.anim_dur : e;
        float p = (float)e / (float)Q.anim_dur, ip = 1.0f - p;
        p = p < 0.5f ? 4.0f * p * p * p : 1.0f - 4.0f * ip * ip * ip;
        int ny = Q.anim_from + (int)((Q.anim_dest - Q.anim_from) * p + (Q.anim_dest > Q.anim_from ? 0.5f : -0.5f));
        ny = ny < 0 ? 0 : ny > SCR_H ? SCR_H : ny;
        Q.panel_y = ny;
        if (e >= Q.anim_dur) {
            Q.anim_on = false;
            Q.panel_y = Q.anim_dest;
            if (Q.anim_dest <= 0) {
                settle_closed();   // borra este temporizador: no tocar nada despues
                return;
            }
        }
        sync_curtain();
    } else if (Q.panel_y > 0) {
        if (Q.ganim) {
            uint32_t e = now - Q.gt0;
            e = e > Q.gdur ? Q.gdur : e;
            float p = (float)e / (float)Q.gdur, u = p - 1.0f;
            const float c1 = 0.55f, c3 = c1 + 1.0f;
            p = 1.0f + c3 * u * u * u + c1 * u * u;   // ease-out con un rebote muy pequeno
            Q.gh = Q.gfrom + (Q.gto - Q.gfrom) * p;
            float lo = (float)flex_qs_group_min_px(), hi = (float)flex_qs_group_max_px(&Q.L);
            Q.gh = Q.gh < lo ? lo : Q.gh > hi ? hi : Q.gh;
            if (e >= Q.gdur) {
                Q.ganim = false;
                Q.gh = Q.gto;
            }
            relayout();
            place();
        }
        scroll_step(dt);
    }
    // destellos que se apagan
    if (Q.flash_obj) {
        uint32_t e = now - Q.flash_ms;
        if (e >= QP_FLASH_MS) {
            lv_obj_delete(Q.flash_obj);
            Q.flash_obj = NULL;
            Q.flash_kind = Q.flash_id = -1;
        } else {
            lv_obj_set_style_bg_opa(Q.flash_obj, (lv_opa_t)(110 * (1.0f - (float)e / QP_FLASH_MS)), 0);
        }
    } else if (Q.flash_kind >= 0 && now - Q.flash_ms >= QP_FLASH_MS) {
        Q.flash_kind = Q.flash_id = -1;
    }
    if (Q.reject_obj) {
        uint32_t e = now - Q.ed_reject_ms;
        if (e >= QP_REJECT_MS) {
            lv_obj_delete(Q.reject_obj);
            Q.reject_obj = NULL;
            Q.ed_reject_id = -1;
        } else {
            lv_obj_set_style_bg_opa(Q.reject_obj, (lv_opa_t)(120 * (1.0f - (float)e / QP_REJECT_MS)), 0);
        }
    }
    // cambio de minuto: hora y fecha de la cabecera
    int32_t m = flex_clock_minute();
    if (m != Q.last_min) {
        Q.last_min = m;
        if (Q.time_l && Q.mode == QPM_PANEL) {
            build_body();
        }
    }
    // el alto elegido para la tarjeta es configuracion (a la cache, al terminar el ajuste)
    if (Q.save_panel && !Q.ganim) {
        Q.save_panel = false;
        save_cfg();
    }
}

static void theme_cb(void *ctx)
{
    (void)ctx;
    if (!Q.root) {
        return;
    }
    if (Q.mode == QPM_CAT) {
        cat_build();
    }
    lv_obj_invalidate(Q.bg);
    build_body();
}

void flex_qs_init(void)
{
    flex_touch_add_sys_hook(hook, 2);
    flex_theme_listen(theme_cb, NULL);
}

// ---- introspeccion para el simulador y las pruebas ----------------------------------------
int flex_qs_visible_ids(uint8_t *out, int cap)
{
    if (!Q.loaded) {
        return 0;
    }
    flex_qs_layout_t L;
    flex_qs_layout(&L, Q.cfg.it, Q.cfg.n, flex_qp_group_h(Q.cfg.grows), false, qs_shown);
    int n = 0;
    for (int b = 0; b < L.blk_n && n < cap; b++) {
        if (L.blk[b].kind == FLEX_QB_ITEM) {
            out[n++] = Q.cfg.it[L.blk[b].item].id;
        }
    }
    for (int k = 0; k < L.tile_n && n < cap; k++) {
        out[n++] = Q.cfg.it[L.tiles[k]].id;
    }
    return n;
}

int flex_qs_mode(void)
{
    return Q.mode;
}

uint8_t flex_qs_grows(void)
{
    return Q.cfg.grows;
}

// Rectangulo en pantalla del control id (modulo o circulo de la tarjeta) en el
// modo actual. false si no se ve.
bool flex_qs_ctl_rect(int id, lv_area_t *out)
{
    if (!Q.root || Q.mode == QPM_CAT) {
        return false;
    }
    flex_qs_cfg_t *c = lay_cfg();
    int top = view_top();
    for (int b = 0; b < Q.L.blk_n; b++) {
        const flex_qs_block_t *k = &Q.L.blk[b];
        if (k->kind == FLEX_QB_ITEM && c->it[k->item].id == id) {
            *out = (lv_area_t){k->x, top + k->y, k->x + k->w - 1, top + k->y + k->h - 1};
            return true;
        }
    }
    if (Q.L.group_blk < 0) {
        return false;
    }
    int gy_top = top + Q.L.blk[Q.L.group_blk].y + FLEX_QP_GPAD - (int)(Q.gscroll + 0.5f);
    for (int i = 0; i < Q.L.tile_n; i++) {
        if (c->it[Q.L.tiles[i]].id == id) {
            int cx, cy;
            flex_qs_tile_center(i, gy_top, &cx, &cy);
            int r = FLEX_QP_TCIRC / 2;
            *out = (lv_area_t){cx - r, cy - r, cx + r - 1, cy + r - 1};
            return true;
        }
    }
    return false;
}

bool flex_qs_group_rect(lv_area_t *out)
{
    if (!Q.root || Q.mode == QPM_CAT || Q.L.group_blk < 0) {
        return false;
    }
    const flex_qs_block_t *k = &Q.L.blk[Q.L.group_blk];
    int top = view_top();
    *out = (lv_area_t){k->x, top + k->y, k->x + k->w - 1, top + k->y + k->h - 1};
    return true;
}

bool flex_qs_catalog_has(int id)
{
    for (int k = 0; k < Q.cat_n; k++) {
        if (Q.cat_ids[k] == id) {
            return true;
        }
    }
    return false;
}

int flex_qs_catalog_index(int id)
{
    for (int k = 0; k < Q.cat_n; k++) {
        if (Q.cat_ids[k] == id) {
            return k;
        }
    }
    return -1;
}
