// Flex OS Ultra · app Reloj (docs/spec/03 §1-2; Arduino: FlexOS_Ultra_AppFramework.h:488-630 y FlexOS_Ultra_AppChrono.h §4).
//
// Contenedor con tres pestanas: Hora (reloj grande, fecha larga, tarjetas de fecha y formato, pie), Cronometro (esfera de 60
// marcas con dos agujas, tabla de vueltas y dos botones circulares) y Temporizador (anunciado como "Proximamente": no hay
// motor propio y no se finge). El contenedor es el UNICO que pinta la barra de pestanas; cada pestana construye SU cuerpo, asi
// ninguna deja restos de otra. Toda la logica y la maqueta salen de flex_chrono_model (probado contra Arduino); el estado del
// cronometro es el del servicio (flex_crono.c), que sigue contando con la app cerrada.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_chrono_model.h"
#include "flex_clock.h"
#include "flex_crono.h"
#include "flex_frame.h"
#include "flex_glass.h"
#include "flex_i18n.h"
#include "flex_icons.h"
#include "flex_kit.h"
#include "flex_layout.h"
#include "flex_theme.h"

#define NROWS 4
static uint8_t s_tab;   // 0 Hora · 1 Cronometro · 2 Temporizador. Persiste mientras el sistema corre (gRelojTab)

static struct {
    lv_obj_t *root, *seg, *body;
    lv_timer_t *tmr;
    int w, h, pad, shown;
    bool susp;
    // Hora
    lv_obj_t *h_clock, *h_date, *h_card_val[2], *h_footer;
    // Cronometro
    flex_crono_layout_t L;
    lv_obj_t *dial, *d_time, *tbl, *circ_l, *circ_r, *lbl_l, *lbl_r, *row[NROWS][3], *nolaps, *hdr[3];
    int d_fs, frames;   // frames: cuadros de la esfera repintados (pruebas: solo corre a ~30 fps mientras el cronometro corre)
    uint32_t last;
    // Temporizador
    lv_obj_t *t_title, *t_sub;
} R;

static const lv_font_t *big_font(int fs)
{
    return fs >= 6 ? FLEX_FONT_S6 : flex_ui_font(fs);
}

static int big_tw(const char *s, int fs, void *ctx)
{
    (void)ctx;
    return flex_text_width(s, big_font(fs));
}

static lv_obj_t *mk(lv_obj_t *p, int x, int y, int w, int h)
{
    lv_obj_t *o = flex_box(p);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_clickable(o, false);
    return o;
}

static lv_obj_t *txt(lv_obj_t *p, const char *s, int fs, lv_color_t col, int cx, int y)
{
    lv_obj_t *l = flex_kit_text(p, s, fs, col, cx, y, true);
    lv_obj_set_clickable(l, false);
    return l;
}

// ---- pestana Hora (appRelojHoraRender) ---------------------------------------------------------------------------------------
typedef struct {
    const char *ds;
    int maxw;
} datectx_t;

static int date_lh(int hint, void *ctx)
{
    datectx_t *d = ctx;
    int fs = flex_ui_font_fit(d->ds, d->maxw, flex_ui_font_h(hint), flex_ui_text_w, NULL);
    return flex_ui_line_h(fs);
}

// Reloj grande: la fuente del bloqueo (Outfit, 200 px) escalada para que la altura de las cifras sea capH. El trazo vectorial
// de Arduino ("thick") no existe aqui: la fuente ya trae su propio peso (diferencia documentada).
static lv_obj_t *big_clock(lv_obj_t *parent, const char *cs, int cx, int cap_top, int cap_h, lv_color_t col)
{
    const lv_font_t *f = FLEX_FONT_CLOCK;
    lv_obj_t *l = flex_label(parent, cs, f, col);
    lv_obj_set_clickable(l, false);
    int tw = flex_text_width(cs, f), lh = lv_font_get_line_height(f);
    // La fuente del reloj solo trae cifras y ':' (sin 'H'): las metricas salen del '0'
    lv_font_glyph_dsc_t g;
    int fcap = lh * 7 / 10, foff = 0;
    if (lv_font_get_glyph_dsc(f, &g, '0', 0)) {
        fcap = g.box_h;
        foff = (f->line_height - f->base_line) - (g.ofs_y + g.box_h);
    }
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_size(l, tw, lh);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    int px = tw / 2, py = foff + fcap / 2;
    int scale = cap_h * 256 / (fcap > 0 ? fcap : 1);
    if (scale != 256) {
        lv_obj_set_style_transform_pivot_x(l, px, 0);
        lv_obj_set_style_transform_pivot_y(l, py, 0);
        lv_obj_set_style_transform_scale(l, scale, 0);
    }
    // el centro de las cifras queda fijo en el pivote al escalar
    lv_obj_set_pos(l, cx - px, cap_top + cap_h / 2 - py);
    return l;
}

static void build_hora(void)
{
    const flex_palette_t *t = flex_th();
    flex_reloj_box_t b;
    flex_reloj_body(0, 0, R.w, R.h, &b);
    int pad = R.pad, gap = flex_ui_gap(pad);
    char cs[8], ds[64], sd[40];
    struct tm tm;
    flex_clock_str_big(cs, sizeof(cs));
    flex_clock_now(&tm);
    flex_date_long(ds, sizeof(ds), tm.tm_wday, tm.tm_mday, tm.tm_mon + 1);
    flex_date_short(sd, sizeof(sd), tm.tm_wday, tm.tm_mday, tm.tm_mon + 1);
    datectx_t dc = {ds, b.bw - 2 * pad};
    flex_reloj_hora_t H;
    flex_reloj_hora_layout(&b, pad, gap, (int)strlen(cs), flex_ui_line_h(2), date_lh, &dc, &H);
    R.h_clock = big_clock(R.body, cs, b.bx + b.bw / 2, H.cy, H.cap_h, t->txt);
    if (H.show_date) {
        int fs = flex_ui_font_fit(ds, dc.maxw, flex_ui_font_h(H.date_hint), flex_ui_text_w, NULL);
        R.h_date = txt(R.body, ds, fs, t->txt2, b.bx + b.bw / 2, H.date_y);
    }
    if (H.show_cards) {
        const char *lbl[2] = {"Fecha", "Formato"};
        const char *val[2] = {sd, flex_look()->h24 ? "24 h" : "12 h"};
        for (int i = 0; i < 2; i++) {
            int x = b.bx + pad + i * (H.card_w + H.gap);
            lv_obj_t *c = mk(R.body, x, H.cards_y, H.card_w, H.card_h);
            lv_obj_set_style_radius(c, pad, 0);
            flex_surface(c, FLEX_SURF_CARD, FLEX_BD_FLAT);
            int fl = flex_ui_font_fit(lbl[i], H.card_w - 16, 2, flex_ui_text_w, NULL);
            txt(R.body, lbl[i], fl, t->txt2, x + H.card_w / 2, H.cards_y + H.card_h / 2 - flex_ui_line_h(fl) - 6);
            int fv = flex_ui_font_fit(val[i], H.card_w - 16, 3, flex_ui_text_w, NULL);
            R.h_card_val[i] = txt(R.body, val[i], fv, t->txt, x + H.card_w / 2, H.cards_y + H.card_h / 2 + 2);
        }
    }
    if (H.show_footer) {
        const char *s = "Reloj de FlexOS";
        int fs = flex_ui_font_fit(s, b.bw - 2 * pad, 2, flex_ui_text_w, NULL);
        R.h_footer = txt(R.body, s, fs, t->mute, b.bx + b.bw / 2, H.footer_y);
    }
}

// ---- pestana Temporizador (appRelojTempRender) ----------------------------------------------------------------------------------
static void build_timer(void)
{
    const flex_palette_t *t = flex_th();
    flex_reloj_box_t b;
    flex_reloj_body(0, 0, R.w, R.h, &b);
    int cy = b.by + b.bh / 2;
    int f1 = flex_ui_font_fit(flex_t(FLEX_S_CRN_TIMER), b.bw - 40, 4, flex_ui_text_w, NULL);
    R.t_title = txt(R.body, flex_t(FLEX_S_CRN_TIMER), f1, t->txt, b.bx + b.bw / 2, cy - 24);
    int f2 = flex_ui_font_fit(flex_t(FLEX_S_SOON), b.bw - 40, 2, flex_ui_text_w, NULL);
    R.t_sub = txt(R.body, flex_t(FLEX_S_SOON), f2, t->txt2, b.bx + b.bw / 2, cy + 16);
}

// ---- pestana Cronometro --------------------------------------------------------------------------------------------------------------
static void line(lv_layer_t *ly, float x0, float y0, float x1, float y1, int w, lv_color_t c)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = c;
    d.width = w < 1 ? 1 : w;
    d.round_start = 1;
    d.round_end = 1;
    d.p1.x = (lv_value_precise_t)x0;
    d.p1.y = (lv_value_precise_t)y0;
    d.p2.x = (lv_value_precise_t)x1;
    d.p2.y = (lv_value_precise_t)y1;
    lv_draw_line(ly, &d);
}

static void disc(lv_layer_t *ly, float cx, float cy, int r, lv_color_t c)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = LV_OPA_COVER;
    d.radius = LV_RADIUS_CIRCLE;
    lv_area_t a = {(int32_t)cx - r, (int32_t)cy - r, (int32_t)cx + r, (int32_t)cy + r};
    lv_draw_rect(ly, &d, &a);
}

// ESFERA (cronoDrawDial): 60 marcas (las de cada 5 mas largas, gruesas y claras), disco central y dos agujas con cola
// (la violeta marca el TOTAL, una vuelta por minuto; la gris, la vuelta en curso) y el cubo.
static void dial_draw_cb(lv_event_t *e)
{
    lv_layer_t *ly = lv_event_get_layer(e);
    lv_obj_t *o = lv_event_get_target_obj(e);
    lv_area_t oa;
    lv_obj_get_coords(o, &oa);
    float ox = (float)(oa.x1 - lv_obj_get_x(o)), oy = (float)(oa.y1 - lv_obj_get_y(o));   // origen del cuerpo en pantalla
    const flex_palette_t *t = flex_th();
    const flex_crono_t *c = flex_crono_svc();
    uint32_t now = lv_tick_get();
    for (int i = 0; i < 60; i++) {
        flex_crono_tick_t k;
        flex_crono_dial_tick(&R.L, i, &k);
        line(ly, ox + k.x0, oy + k.y0, ox + k.x1, oy + k.y1, (int)(k.w + 0.5f), k.major ? t->txt2 : t->mute);
    }
    disc(ly, ox + (float)R.L.cx, oy + (float)R.L.cy, R.L.r_disc, t->surf2);
    flex_crono_hands_t h;
    flex_crono_dial_hands(&R.L, flex_crono_elapsed(c, now), flex_crono_lap_elapsed(c, now), &h);
    line(ly, ox + h.lap[0], oy + h.lap[1], ox + h.lap[2], oy + h.lap[3], (int)(h.lap_w + 0.5f), t->txt2);
    line(ly, ox + h.tot[0], oy + h.tot[1], ox + h.tot[2], oy + h.tot[3], (int)(h.tot_w + 0.5f), flex_accent());
    disc(ly, ox + (float)R.L.cx, oy + (float)R.L.cy, (int)h.hub, flex_accent());
}

// Tiempo central MM:SS.cc, con el tamano mas grande que cabe en el disco (cronoFitBig)
static void dial_text(void)
{
    char b[16];
    flex_crono_fmt(b, sizeof(b), flex_crono_elapsed(flex_crono_svc(), lv_tick_get()), true);
    int fs = flex_crono_fit_big(b, R.L.r_disc * 2 - 18, 7, big_tw, NULL);
    if (fs != R.d_fs) {
        R.d_fs = fs;
        const lv_font_t *f = big_font(fs);
        lv_obj_set_style_text_font(R.d_time, f, 0);
        // fs equivalente de Arduino por altura de cifras (7 filas por tamano): y = cy - 2.3 * fs
        int eq = flex_font_cap_h(f) / 7;
        lv_obj_set_width(R.d_time, R.L.r_disc * 2);
        lv_obj_set_x(R.d_time, R.L.cx - R.L.r_disc);
        flex_label_cap_y(R.d_time, flex_crono_dial_text_y(&R.L, eq < 1 ? 1 : eq));
    }
    if (strcmp(lv_label_get_text(R.d_time), b) != 0) {
        lv_label_set_text(R.d_time, b);
    }
}

static void dial_refresh(void)
{
    if (!R.dial) {
        return;
    }
    R.frames++;
    dial_text();
    lv_obj_invalidate(R.dial);
}

// TABLA DE VUELTAS (cronoDrawLaps): se rehace SOLO cuando cambia la lista. Las mas recientes arriba.
static void build_laps(void)
{
    const flex_palette_t *t = flex_th();
    const flex_crono_t *c = flex_crono_svc();
    memset(R.row, 0, sizeof(R.row));
    R.nolaps = NULL;
    if (!R.tbl) {
        return;
    }
    lv_obj_clean(R.tbl);
    const flex_crono_layout_t *L = &R.L;
    int fsh = flex_ui_font_fit(flex_t(FLEX_S_CRN_SPLIT), L->bw / 3 - 8, 2, flex_ui_text_w, NULL);
    const int cols[3] = {L->col_lap - L->bx, L->col_split - L->bx, L->col_tot - L->bx};
    const char *hd[3] = {flex_t(FLEX_S_CRN_LAP), flex_t(FLEX_S_CRN_SPLIT), flex_t(FLEX_S_CRN_TOTAL)};
    for (int i = 0; i < 3; i++) {
        R.hdr[i] = txt(R.tbl, hd[i], fsh, t->txt2, cols[i], 0);
    }
    int yl = flex_ui_line_h(fsh) + 6;
    lv_obj_t *dv = mk(R.tbl, R.pad, yl, L->bw - 2 * R.pad, 1);
    lv_obj_set_style_bg_color(dv, t->divider, 0);
    lv_obj_set_style_bg_opa(dv, LV_OPA_COVER, 0);
    int y = yl + 8;
    if (c->nlaps == 0) {
        R.nolaps = txt(R.tbl, flex_t(FLEX_S_CRN_NOLAPS), 2, t->mute, L->bw / 2, y + 6);
        return;
    }
    for (int r = 0; r < L->rows && r < NROWS; r++) {
        int kind;
        char no[8], sp[16], to[16];
        if (!flex_crono_lap_row(c, r, &kind, no, sp, to)) {
            break;
        }
        lv_color_t col = kind == FLEX_CRONO_ROW_BEST ? flex_accent() : kind == FLEX_CRONO_ROW_WORST ? t->danger : t->txt;   // mejor / peor
        R.row[r][0] = txt(R.tbl, no, 2, col, cols[0], y);
        R.row[r][1] = txt(R.tbl, sp, 2, col, cols[1], y);
        R.row[r][2] = txt(R.tbl, to, 2, col, cols[2], y);
        y += L->row_h;
    }
}

// Dos botones circulares grandes (cronoDrawButtons): izquierdo gris (Parcial / Reinic.), derecho rojo al detener o acento
static void btn_visual(void)
{
    if (!R.circ_l) {
        return;
    }
    const flex_palette_t *t = flex_th();
    const flex_crono_t *c = flex_crono_svc();
    const flex_crono_layout_t *L = &R.L;
    int r = L->btn_r, cy = L->btn_cy;
    bool left_on = c->st != FLEX_CRONO_IDLE, stop = c->st == FLEX_CRONO_RUN;
    lv_label_set_text(R.lbl_l, flex_crono_label_left(c) == FLEX_CRONO_LL_RESET ? flex_t(FLEX_S_CRN_BRESET) : flex_t(FLEX_S_CRN_BLAP));
    int lr = flex_crono_label_right(c);
    lv_label_set_text(R.lbl_r, lr == FLEX_CRONO_LR_STOP ? flex_t(FLEX_S_CRN_BSTOP) : lr == FLEX_CRONO_LR_CONTINUE ? flex_t(FLEX_S_CONTINUE) : flex_t(FLEX_S_CRN_BSTART));
    lv_obj_set_style_text_color(R.lbl_l, left_on ? t->txt : t->disabled, 0);
    lv_color_t rc = stop ? t->danger : flex_accent();
    lv_obj_set_style_text_color(R.lbl_r, rc, 0);
    lv_obj_t *vis = lv_obj_get_child(R.circ_r, 0);
    lv_obj_set_style_bg_color(vis, rc, 0);
    lv_obj_set_style_border_color(vis, rc, 0);
    // el rotulo se ajusta a 2r - 12 con el tamano mas grande (hasta 3) que cabe
    int fsl = flex_ui_font_fit(lv_label_get_text(R.lbl_l), 2 * r - 12, 3, flex_ui_text_w, NULL);
    int fsr = flex_ui_font_fit(lv_label_get_text(R.lbl_r), 2 * r - 12, 3, flex_ui_text_w, NULL);
    lv_obj_set_style_text_font(R.lbl_l, flex_ui_font(fsl), 0);
    lv_obj_set_style_text_font(R.lbl_r, flex_ui_font(fsr), 0);
    flex_label_cap_center(R.lbl_l, L->btn_lx, cy - flex_ui_line_h(fsl) / 2);
    flex_label_cap_center(R.lbl_r, L->btn_rx, cy - flex_ui_line_h(fsr) / 2);
}

static void btn_cb(lv_event_t *e)
{
    if ((intptr_t)lv_event_get_user_data(e) == 0) {
        flex_crono_svc_left();
    } else {
        flex_crono_svc_right();
    }
    // lo que cambia con la accion se repinta en el acto (no espera al siguiente cuadro)
    if (flex_crono_svc_take(FLEX_CRONO_D_BTN)) {
        btn_visual();
    }
    if (flex_crono_svc_take(FLEX_CRONO_D_LAPS)) {
        build_laps();
    }
    if (flex_crono_svc_take(FLEX_CRONO_D_DIAL)) {
        dial_refresh();
    }
}

static lv_obj_t *circle_btn(int cx, int cy, int r, intptr_t side)
{
    // zona sensible: circulo de r + 6 (cronoTabTouch); el circulo visible va dentro
    lv_obj_t *hit = mk(R.body, cx - r - 6, cy - r - 6, 2 * (r + 6), 2 * (r + 6));
    lv_obj_set_style_radius(hit, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_clickable(hit, true);
    lv_obj_set_adv_hittest(hit, true);
    lv_obj_add_event_cb(hit, btn_cb, LV_EVENT_CLICKED, (void *)side);
    lv_obj_t *vis = mk(hit, 6, 6, 2 * r, 2 * r);
    lv_obj_set_style_radius(vis, LV_RADIUS_CIRCLE, 0);
    return hit;
}

static void build_crono(void)
{
    const flex_palette_t *t = flex_th();
    flex_reloj_box_t b;
    flex_reloj_body(0, 0, R.w, R.h, &b);
    flex_crono_layout(b.bx, b.by, b.bw, b.bh, R.pad, &R.L);
    const flex_crono_layout_t *L = &R.L;
    // esfera: banda vertical recortada al cuerpo
    int y0 = L->dial_y0 < L->by ? L->by : L->dial_y0;
    int y1 = L->dial_y1 > L->by + L->bh - 1 ? L->by + L->bh - 1 : L->dial_y1;
    if (y1 > y0) {
        R.dial = mk(R.body, L->bx, y0, L->bw, y1 - y0 + 1);
        lv_obj_add_event_cb(R.dial, dial_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
        R.d_time = flex_label(R.body, "", flex_ui_font(4), t->txt);
        lv_obj_set_clickable(R.d_time, false);
        lv_obj_set_style_text_align(R.d_time, LV_TEXT_ALIGN_CENTER, 0);
        R.d_fs = -1;
    }
    if (L->has_table) {
        R.tbl = mk(R.body, L->bx, L->tbl_y, L->bw, 26 + L->rows * L->row_h);
        build_laps();
    }
    // botones
    R.circ_l = circle_btn(L->btn_lx, L->btn_cy, L->btn_r, 0);
    R.circ_r = circle_btn(L->btn_rx, L->btn_cy, L->btn_r, 1);
    lv_obj_t *vl = lv_obj_get_child(R.circ_l, 0);
    flex_surface(vl, FLEX_SURF_CARD, FLEX_BD_FLAT);
    lv_obj_t *vr = lv_obj_get_child(R.circ_r, 0);
    lv_obj_set_style_bg_opa(vr, 70, 0);
    lv_obj_set_style_border_width(vr, 1, 0);
    lv_obj_set_style_border_opa(vr, LV_OPA_COVER, 0);
    R.lbl_l = flex_label(R.body, "", FLEX_FONT_S3, t->txt);
    R.lbl_r = flex_label(R.body, "", FLEX_FONT_S3, t->txt);
    lv_obj_set_clickable(R.lbl_l, false);
    lv_obj_set_clickable(R.lbl_r, false);
    btn_visual();
    // lo pendiente de repintar ya esta repintado (Arduino lo baja tras el render completo)
    flex_crono_svc_take(FLEX_CRONO_D_LAPS);
    flex_crono_svc_take(FLEX_CRONO_D_BTN);
    flex_crono_svc_take(FLEX_CRONO_D_DIAL);
    dial_refresh();
}

// Tick de la pestana (cronoTabTick): cada zona se repinta por su cuenta y solo cuando le toca. La esfera, ~30 fps mientras
// corre; parada o inactiva solo si alguien la invalido (cambio de estado) y entonces una vez basta.
static void tab_tick(void)
{
    if (flex_crono_svc_take(FLEX_CRONO_D_BTN)) {
        btn_visual();
    }
    if (flex_crono_svc_take(FLEX_CRONO_D_LAPS)) {
        build_laps();
    }
    bool dirty = flex_crono_svc_take(FLEX_CRONO_D_DIAL);
    if (flex_crono_svc()->st != FLEX_CRONO_RUN && !dirty) {
        return;
    }
    uint32_t now = lv_tick_get();
    if (!dirty && !flex_crono_frame_due(now, R.last)) {
        return;
    }
    R.last = now;
    dial_refresh();
}

static void tmr_cb(lv_timer_t *tm)
{
    (void)tm;
    if (R.shown == 1 && !R.susp) {
        tab_tick();
    }
}

static void timer_sync(void)
{
    if (!R.tmr) {
        return;
    }
    if (R.shown == 1 && !R.susp) {
        lv_timer_resume(R.tmr);
    } else {
        lv_timer_pause(R.tmr);
    }
}

// ---- contenedor ----------------------------------------------------------------------------------------------------------------------------
static void build_body(void)
{
    if (R.body) {
        lv_obj_delete(R.body);
    }
    R.body = mk(R.root, 0, 0, R.w, R.h);
    R.h_clock = R.h_date = R.h_footer = R.h_card_val[0] = R.h_card_val[1] = NULL;
    R.dial = R.d_time = R.tbl = R.circ_l = R.circ_r = R.lbl_l = R.lbl_r = R.nolaps = NULL;
    R.t_title = R.t_sub = NULL;
    memset(R.row, 0, sizeof(R.row));
    memset(R.hdr, 0, sizeof(R.hdr));
    R.shown = s_tab;
    switch (s_tab) {
    case 1: build_crono(); break;
    case 2: build_timer(); break;
    default: build_hora(); break;
    }
    timer_sync();
}

static void seg_cb(lv_obj_t *seg, int index, void *user)
{
    (void)seg;
    (void)user;
    s_tab = (uint8_t)index;
    build_body();
}

// Barra de pestanas (relojDrawTabs) y cuerpo de la pestana activa. Sin barra en lienzos diminutos (ventana de DeX).
static void build_all(void)
{
    if (R.seg) {
        lv_obj_delete(R.seg);
        R.seg = NULL;
    }
    build_body();
    if (flex_reloj_tabs_visible(R.h)) {
        flex_reloj_tabs_t tg;
        flex_reloj_tabs_geom(0, 0, R.w, R.pad, &tg);
        const char *labels[3] = {flex_t(FLEX_S_CRN_HOUR), flex_t(FLEX_S_CRN_STOPW), flex_t(FLEX_S_CRN_TIMER)};
        R.seg = flex_kit_segmented_at(R.root, tg.x, tg.y, tg.w, labels, 3, s_tab, seg_cb, NULL);
    }
}

// Cambio de tema con la app delante (el interruptor Oscuro / Claro del panel rapido): los colores se leen al crear cada objeto.
// Lo avisa el servicio (se registra en el arranque: la lista de oyentes del tema es finita y no se puede depender de que quede sitio
// cuando se abre la app)
void flex_app_reloj_theme_changed(void)
{
    if (R.root) {
        build_all();
    }
}

static void on_create(lv_obj_t *root, const lv_area_t *area)
{
    memset(&R, 0, sizeof(R));
    R.root = root;
    R.w = lv_area_get_width(area);
    R.h = lv_area_get_height(area);
    R.pad = flex_ui_pad(R.w, R.h);
    build_all();
    R.tmr = lv_timer_create(tmr_cb, 33, NULL);
    timer_sync();
}

static void on_resume(void)
{
    R.susp = false;
    if (!R.root) {
        return;
    }
    // Todo se rehace al volver: la hora pudo cambiar, la capsula o la tarjeta pudieron pedir otra pestana mientras estaba atras, y
    // el idioma o el formato de hora se cambian en Ajustes
    build_all();
    timer_sync();
}

static void on_suspend(void)
{
    R.susp = true;
    timer_sync();
}

static void on_close(void)
{
    if (R.tmr) {
        lv_timer_delete(R.tmr);
    }
    memset(&R, 0, sizeof(R));
}

static void on_minute(void)
{
    if (R.root && R.shown == 0 && !R.susp) {
        build_body();   // appRelojRender: la pestana Hora se rehace cada minuto
    }
}

void flex_app_reloj_goto_tab(int tab)
{
    if (tab < 0 || tab > 2) {
        return;
    }
    s_tab = (uint8_t)tab;
    if (R.root && !R.susp) {
        if (R.seg) {
            flex_kit_segmented_set(R.seg, tab);
        }
        if (R.shown != tab) {
            build_body();
        }
    }
}

int flex_app_reloj_tab(void)
{
    return s_tab;
}

// Pruebas: los textos que de verdad se ven. 0 tiempo de la esfera · 1 / 2 botones · 3 reloj grande · 4 fecha larga ·
// 5 / 6 valores de las tarjetas · 7 / 8 temporizador · 9 sin vueltas · 10 + 3 * fila + columna: tabla
static lv_obj_t *text_obj(int which)
{
    lv_obj_t *o = NULL;
    switch (which) {
    case 0: o = R.d_time; break;
    case 1: o = R.lbl_l; break;
    case 2: o = R.lbl_r; break;
    case 3: o = R.h_clock; break;
    case 4: o = R.h_date; break;
    case 5: o = R.h_card_val[0]; break;
    case 6: o = R.h_card_val[1]; break;
    case 7: o = R.t_title; break;
    case 8: o = R.t_sub; break;
    case 9: o = R.nolaps; break;
    default:
        if (which >= 10 && which < 10 + 3 * NROWS) {
            o = R.row[(which - 10) / 3][(which - 10) % 3];
        }
        break;
    }
    return o;
}

const char *flex_app_reloj_text(int which)
{
    lv_obj_t *o = text_obj(which);
    return o ? lv_label_get_text(o) : "";
}

// Pruebas: el color con que se ve ese texto (RGB de 24 bits) y la pestana marcada en la barra
uint32_t flex_app_reloj_color(int which)
{
    lv_obj_t *o = text_obj(which);
    return o ? lv_color_to_u32(lv_obj_get_style_text_color(o, LV_PART_MAIN)) & 0xFFFFFF : 0;
}

int flex_app_reloj_frames(void)
{
    return R.frames;
}

lv_obj_t *flex_app_reloj_seg_obj(void)
{
    return R.seg;
}

int flex_app_reloj_seg_active(void)
{
    return R.seg ? flex_kit_segmented_get(R.seg) : -1;
}

const flex_app_ops_t flex_app_reloj_ops = {
    .on_create = on_create,
    .on_resume = on_resume,
    .on_suspend = on_suspend,
    .on_close = on_close,
    .on_minute = on_minute,
};
