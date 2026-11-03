// Flex OS Ultra · kit de componentes compartidos de las apps (ver flex_kit.h).
#include "flex_kit.h"
#include <stdlib.h>
#include <string.h>
#include "flex_frame.h"
#include "flex_icons.h"
#include "flex_theme.h"

#define TOAST_MS 1800

static lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h, int r)
{
    lv_obj_t *o = flex_box(p);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    return o;
}

static void fill(lv_obj_t *o, lv_color_t c, lv_opa_t a)
{
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, a, 0);
}

static lv_obj_t *text(lv_obj_t *p, const char *s, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = flex_label(p, s, f, c);
    lv_obj_set_clickable(l, false);
    return l;
}

// Cada componente lleva su estado en el propio objeto y lo libera al borrarse: nada global, nada compartido
// entre apps
static void ctx_free_cb(lv_event_t *e)
{
    free(lv_event_get_user_data(e));
}

static void *ctx_new(lv_obj_t *o, size_t n)
{
    void *c = calloc(1, n);
    if (c) {
        lv_obj_set_user_data(o, c);
        lv_obj_add_event_cb(o, ctx_free_cb, LV_EVENT_DELETE, c);
    }
    return c;
}

const lv_font_t *flex_ui_font(int fs)
{
    return fs >= 5 ? FLEX_FONT_S5 : fs == 4 ? FLEX_FONT_S4 : fs == 3 ? FLEX_FONT_S3 : fs == 2 ? FLEX_FONT_S2 : FLEX_FONT_S1;
}

int flex_ui_text_w(const char *t, int fs, void *ctx)
{
    (void)ctx;
    return flex_text_width(t, flex_ui_font(fs));
}

lv_obj_t *flex_kit_text(lv_obj_t *parent, const char *s, int fs, lv_color_t color, int cx_or_x, int y, bool centered)
{
    lv_obj_t *l = text(parent, s, flex_ui_font(fs), color);
    if (centered) {
        flex_label_cap_center(l, cx_or_x, y);
    } else {
        lv_obj_set_x(l, cx_or_x);
        flex_label_cap_y(l, y);
    }
    return l;
}

lv_obj_t *flex_kit_text_vc(lv_obj_t *parent, const char *s, int fs, lv_color_t color, int cx, int cy)
{
    const lv_font_t *f = flex_ui_font(fs);
    return flex_kit_text(parent, s, fs, color, cx, cy - (int)flex_font_cap_h(f) / 2, true);
}

// ===== boton ==================================================================================
typedef struct {
    flex_btn_style_t style;
    flex_kit_btn_cb_t cb;
    void *user;
    lv_obj_t *label;
} btn_t;

static void btn_apply(lv_obj_t *b, btn_t *c)
{
    const flex_palette_t *t = flex_th();
    lv_color_t bg = t->surf2, fg = t->txt;
    switch (c->style) {
    case FLEX_BTN_PRIMARY: bg = t->primary; fg = t->on_acc; break;
    case FLEX_BTN_DANGER: fg = t->danger; break;
    case FLEX_BTN_DISABLED: bg = t->track; fg = t->disabled; break;
    default: break;
    }
    fill(b, bg, LV_OPA_COVER);
    lv_obj_set_style_text_color(c->label, fg, 0);
    lv_obj_set_clickable(b, c->style != FLEX_BTN_DISABLED);   // el deshabilitado no registra toque
}

static void btn_click_cb(lv_event_t *e)
{
    lv_obj_t *b = lv_event_get_current_target_obj(e);
    btn_t *c = lv_obj_get_user_data(b);
    if (c && c->style != FLEX_BTN_DISABLED && c->cb) {
        c->cb(b, c->user);
    }
}

lv_obj_t *flex_kit_button(lv_obj_t *p, int x, int y, int w, int h, const char *label, flex_btn_style_t style,
                          flex_kit_btn_cb_t cb, void *user)
{
    lv_obj_t *b = box(p, x, y, w, h, h / 2 < 16 ? h / 2 : 16);
    btn_t *c = ctx_new(b, sizeof(*c));
    if (!c) {
        return b;
    }
    c->style = style;
    c->cb = cb;
    c->user = user;
    c->label = text(b, label ? label : "", h >= 40 ? FLEX_FONT_S2 : FLEX_FONT_S1, flex_th()->txt);
    lv_obj_add_event_cb(b, btn_click_cb, LV_EVENT_CLICKED, NULL);
    flex_label_one_line(c->label, w - 12);
    lv_obj_set_style_text_align(c->label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_x(c->label, 6);
    flex_label_cap_y(c->label, h >= 40 ? (h - 10) / 2 : (h - 8) / 2);
    btn_apply(b, c);
    return b;
}

void flex_kit_button_set(lv_obj_t *b, flex_btn_style_t style, const char *label)
{
    btn_t *c = lv_obj_get_user_data(b);
    if (!c) {
        return;
    }
    c->style = style;
    if (label) {
        lv_label_set_text(c->label, label);
    }
    btn_apply(b, c);
}

flex_btn_style_t flex_kit_button_style(const lv_obj_t *b)
{
    const btn_t *c = lv_obj_get_user_data((lv_obj_t *)b);
    return c ? c->style : FLEX_BTN_DISABLED;
}

// ===== interruptor ===============================================================================
typedef struct {
    bool on, enabled;
    flex_kit_switch_cb_t cb;
    void *user;
    lv_obj_t *knob, *cross;
} sw_t;

static void sw_apply(lv_obj_t *s, sw_t *c)
{
    const flex_palette_t *t = flex_th();
    int w = lv_obj_get_style_width(s, 0), h = lv_obj_get_style_height(s, 0), kd = h - 8;   // el tamano pedido (las coordenadas aun no estan maquetadas)
    fill(s, !c->enabled ? t->disabled : c->on ? t->ok : t->track, LV_OPA_COVER);
    lv_obj_set_pos(c->knob, c->on ? w - 4 - kd : 4, 4);
    lv_obj_set_hidden(c->cross, c->enabled);
    lv_obj_set_clickable(s, c->enabled);
}

static void sw_toggle(lv_obj_t *s)
{
    sw_t *c = lv_obj_get_user_data(s);
    if (!c || !c->enabled) {
        return;
    }
    c->on = !c->on;
    sw_apply(s, c);
    if (c->cb) {
        c->cb(s, c->on, c->user);
    }
}

static void sw_click_cb(lv_event_t *e)
{
    sw_toggle(lv_event_get_current_target_obj(e));
}

lv_obj_t *flex_kit_switch(lv_obj_t *p, int x, int y, int w, int h, bool on, flex_kit_switch_cb_t cb, void *user)
{
    lv_obj_t *s = box(p, x, y, w, h, h / 2);
    sw_t *c = ctx_new(s, sizeof(*c));
    if (!c) {
        return s;
    }
    c->on = on;
    c->enabled = true;
    c->cb = cb;
    c->user = user;
    int kd = h - 8;
    c->knob = box(s, 4, 4, kd, kd, kd / 2);   // mando de radio h/2 - 4
    fill(c->knob, flex_th()->on_acc, LV_OPA_COVER);
    lv_obj_set_clickable(c->knob, false);
    c->cross = text(c->knob, "x", FLEX_FONT_S1, flex_th()->mute);   // aspa del deshabilitado
    lv_obj_center(c->cross);
    lv_obj_add_event_cb(s, sw_click_cb, LV_EVENT_CLICKED, NULL);
    sw_apply(s, c);
    return s;
}

void flex_kit_switch_set(lv_obj_t *s, bool on)
{
    sw_t *c = lv_obj_get_user_data(s);
    if (c) {
        c->on = on;
        sw_apply(s, c);
    }
}

bool flex_kit_switch_get(const lv_obj_t *s)
{
    const sw_t *c = lv_obj_get_user_data((lv_obj_t *)s);
    return c && c->on;
}

void flex_kit_switch_enable(lv_obj_t *s, bool enabled)
{
    sw_t *c = lv_obj_get_user_data(s);
    if (c) {
        c->enabled = enabled;
        sw_apply(s, c);
    }
}

// ===== fila-tarjeta ===============================================================================
typedef struct {
    flex_kit_btn_cb_t cb;
    void *user;
    lv_obj_t *tile, *sw, *value;
} row_t;

static void row_click_cb(lv_event_t *e)
{
    lv_obj_t *r = lv_event_get_current_target_obj(e);
    row_t *c = lv_obj_get_user_data(r);
    if (!c) {
        return;
    }
    if (c->sw) {
        sw_toggle(c->sw);   // en una fila con interruptor, tocar la fila lo conmuta
    } else if (c->cb) {
        c->cb(r, c->user);
    }
}

lv_obj_t *flex_kit_row(lv_obj_t *p, int y, const flex_kit_row_t *r)
{
    const flex_palette_t *t = flex_th();
    lv_obj_t *card = box(p, 14, y, 452, 56, 14);
    flex_surface(card, FLEX_SURF_CARD, FLEX_BD_FLAT);
    row_t *c = ctx_new(card, sizeof(*c));
    if (!c) {
        return card;
    }
    c->cb = r->cb;
    c->user = r->user;
    int tx = 24;
    if (r->tile) {
        c->tile = box(card, 24, 12, 32, 32, 9);   // glifo centrado en x = 40
        fill(c->tile, r->accent, LV_OPA_COVER);
        lv_obj_set_clickable(c->tile, false);
        tx = 66;
    }
    int right = r->chevron ? 436 : 452 - 12;
    if (r->sw >= 0) {
        right = 452 - 12 - 46 - 8;
    }
    lv_obj_t *title = text(card, r->title ? r->title : "", FLEX_FONT_S2, t->txt);
    lv_obj_set_x(title, tx);
    flex_label_one_line(title, right - tx);
    flex_label_cap_y(title, r->value ? 10 : 19);
    if (r->value) {
        c->value = text(card, r->value, FLEX_FONT_S1, t->txt2);
        lv_obj_set_x(c->value, tx);
        flex_label_one_line(c->value, right - tx);
        flex_label_cap_y(c->value, 34);
    }
    if (r->chevron) {
        lv_obj_t *g = flex_glyph_create(card, FLEX_GLYPH_CHEVRON_RIGHT, 14, 14);
        lv_obj_set_pos(g, 446 - 7 - 14, 21);
        lv_obj_set_style_text_color(g, t->txt2, 0);
        lv_obj_set_clickable(g, false);
    }
    if (r->sw >= 0) {
        c->sw = flex_kit_switch(card, 452 - 12 - 46, 15, 46, 26, r->sw > 0, r->sw_cb, r->user);
        lv_obj_set_clickable(c->sw, false);   // la fila entera recoge el toque
    }
    if (r->cb || r->sw >= 0) {
        lv_obj_set_clickable(card, true);
        lv_obj_add_event_cb(card, row_click_cb, LV_EVENT_CLICKED, NULL);
    } else {
        lv_obj_set_clickable(card, false);
    }
    return card;
}

lv_obj_t *flex_kit_row_tile(lv_obj_t *row)
{
    row_t *c = lv_obj_get_user_data(row);
    return c ? c->tile : NULL;
}

lv_obj_t *flex_kit_row_switch(lv_obj_t *row)
{
    row_t *c = lv_obj_get_user_data(row);
    return c ? c->sw : NULL;
}

void flex_kit_row_set_value(lv_obj_t *row, const char *value)
{
    row_t *c = lv_obj_get_user_data(row);
    if (c && c->value && value) {
        lv_label_set_text(c->value, value);
    }
}

lv_obj_t *flex_kit_info_line(lv_obj_t *p, int y, const char *label, const char *value)
{
    lv_obj_t *row = box(p, 0, y, 480, FLEX_KIT_INFO_STEP, 0);
    lv_obj_set_clickable(row, false);
    lv_obj_t *l = text(row, label ? label : "", FLEX_FONT_S1, flex_th()->txt2);
    lv_obj_set_x(l, 24);
    flex_label_cap_y(l, 6);
    lv_obj_t *v = text(row, value ? value : "", FLEX_FONT_S1, flex_th()->txt);
    lv_obj_set_width(v, 300);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_x(v, 468 - 300);
    flex_label_cap_y(v, 6);
    return row;
}

// ===== barra y deslizador ==========================================================================
lv_obj_t *flex_kit_bar(lv_obj_t *p, int x, int y, int w, int pct)
{
    lv_obj_t *b = box(p, x, y, w, 6, 3);
    fill(b, flex_th()->track, LV_OPA_COVER);
    lv_obj_set_clickable(b, false);
    lv_obj_t *f = box(b, 0, 0, 0, 6, 3);
    fill(f, flex_th()->primary, LV_OPA_COVER);
    lv_obj_set_clickable(f, false);
    flex_kit_bar_set(b, pct);
    return b;
}

void flex_kit_bar_set(lv_obj_t *b, int pct)
{
    lv_obj_t *f = lv_obj_get_child(b, 0);
    if (!f) {
        return;
    }
    pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
    int w = lv_obj_get_style_width(b, 0) * pct / 100;
    lv_obj_set_width(f, w);
    lv_obj_set_hidden(f, w <= 0);
}

typedef struct {
    flex_kit_slider_cb_t cb;
    void *user;
} slider_t;

static void slider_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_current_target_obj(e);
    slider_t *c = lv_obj_get_user_data(s);
    if (c && c->cb) {
        c->cb(s, (int)lv_slider_get_value(s), c->user);
    }
}

lv_obj_t *flex_kit_slider(lv_obj_t *p, int x, int y, int w, int min, int max, int value, flex_kit_slider_cb_t cb, void *user)
{
    const flex_palette_t *t = flex_th();
    lv_obj_t *s = lv_slider_create(p);
    lv_obj_set_pos(s, x, y);
    lv_obj_set_size(s, w, 26);   // pista de 26 px r13 (brillo de Modo PC)
    lv_slider_set_range(s, min, max);
    lv_slider_set_value(s, value, LV_ANIM_OFF);
    lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, t->track, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, t->primary, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_bg_color(s, t->on_acc, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s, -4, LV_PART_KNOB);
    slider_t *c = ctx_new(s, sizeof(*c));
    if (c) {
        c->cb = cb;
        c->user = user;
        lv_obj_add_event_cb(s, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }
    return s;
}

// ===== pestanas segmentadas ==========================================================================
#define SEG_MAX 6
typedef struct {
    int n, active;
    flex_kit_seg_cb_t cb;
    void *user;
    lv_obj_t *seg[SEG_MAX];
} seg_t;

static void seg_apply(seg_t *c)
{
    const flex_palette_t *t = flex_th();
    for (int i = 0; i < c->n; i++) {
        bool on = i == c->active;
        fill(c->seg[i], t->primary, on ? LV_OPA_COVER : LV_OPA_TRANSP);
        lv_obj_set_style_text_color(lv_obj_get_child(c->seg[i], 0), on ? t->on_acc : t->txt2, 0);
    }
}

static void seg_click_cb(lv_event_t *e)
{
    lv_obj_t *item = lv_event_get_current_target_obj(e);
    lv_obj_t *bar = lv_obj_get_parent(item);
    seg_t *c = lv_obj_get_user_data(bar);
    if (!c) {
        return;
    }
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i == c->active) {
        return;
    }
    c->active = i;
    seg_apply(c);
    if (c->cb) {
        c->cb(bar, i, c->user);
    }
}

lv_obj_t *flex_kit_segmented(lv_obj_t *p, int y, const char *const *labels, int n, int active, flex_kit_seg_cb_t cb, void *user)
{
    if (n < 1 || n > SEG_MAX) {
        return NULL;
    }
    const int h = 36, pad = 16, w = 480 - 2 * pad;
    lv_obj_t *bar = box(p, pad, y, w, h, h / 2);
    flex_surface(bar, FLEX_SURF_CARD, FLEX_BD_FLAT);
    seg_t *c = ctx_new(bar, sizeof(*c));
    if (!c) {
        return bar;
    }
    c->n = n;
    c->active = active;
    c->cb = cb;
    c->user = user;
    int seg = (w - 4) / n;
    for (int i = 0; i < n; i++) {
        lv_obj_t *s = box(bar, 2 + i * seg, 2, seg, h - 4, (h - 4) / 2);
        lv_obj_set_clickable(s, true);
        lv_obj_add_event_cb(s, seg_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *l = text(s, labels[i], FLEX_FONT_S2, flex_th()->txt2);
        flex_label_one_line(l, seg - 12);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_x(l, 6);
        flex_label_cap_y(l, 11);
        c->seg[i] = s;
    }
    seg_apply(c);
    return bar;
}

void flex_kit_segmented_set(lv_obj_t *bar, int active)
{
    seg_t *c = lv_obj_get_user_data(bar);
    if (c && active >= 0 && active < c->n) {
        c->active = active;
        seg_apply(c);
    }
}

int flex_kit_segmented_get(const lv_obj_t *bar)
{
    const seg_t *c = lv_obj_get_user_data((lv_obj_t *)bar);
    return c ? c->active : -1;
}

// ===== dialogo ========================================================================================
typedef struct {
    void (*on_ok)(void *);
    void (*on_cancel)(void *);
    void *user;
    bool has_cancel;
} dlg_t;

void flex_kit_dialog_close(lv_obj_t *dlg)
{
    if (dlg) {
        lv_obj_delete(dlg);
    }
}

static void dlg_ok_cb(lv_obj_t *b, void *user)
{
    (void)user;
    lv_obj_t *scrim = lv_obj_get_parent(lv_obj_get_parent(b));
    dlg_t *c = lv_obj_get_user_data(scrim);
    void (*cb)(void *) = c ? c->on_ok : NULL;
    void *u = c ? c->user : NULL;
    flex_kit_dialog_close(scrim);   // se cierra ANTES: el callback puede abrir otro dialogo
    if (cb) {
        cb(u);
    }
}

static void dlg_cancel_cb(lv_obj_t *b, void *user)
{
    (void)user;
    lv_obj_t *scrim = b ? lv_obj_get_parent(lv_obj_get_parent(b)) : NULL;
    dlg_t *c = scrim ? lv_obj_get_user_data(scrim) : NULL;
    void (*cb)(void *) = c ? c->on_cancel : NULL;
    void *u = c ? c->user : NULL;
    flex_kit_dialog_close(scrim);
    if (cb) {
        cb(u);
    }
}

static void scrim_click_cb(lv_event_t *e)
{
    lv_obj_t *scrim = lv_event_get_current_target_obj(e);
    dlg_t *c = lv_obj_get_user_data(scrim);
    if (!c || !c->has_cancel) {
        return;   // sin boton de cancelar el dialogo no se cierra tocando fuera
    }
    void (*cb)(void *) = c->on_cancel;
    void *u = c->user;
    flex_kit_dialog_close(scrim);
    if (cb) {
        cb(u);
    }
}

lv_obj_t *flex_kit_dialog(lv_obj_t *parent, const flex_kit_dialog_t *d)
{
    const flex_palette_t *t = flex_th();
    lv_obj_t *scrim = box(parent, 0, 0, 480, 800, 0);
    fill(scrim, t->scrim, 150);
    lv_obj_set_clickable(scrim, true);
    lv_obj_move_foreground(scrim);
    dlg_t *c = ctx_new(scrim, sizeof(*c));
    if (!c) {
        return scrim;
    }
    c->on_ok = d->on_ok;
    c->on_cancel = d->on_cancel;
    c->user = d->user;
    c->has_cancel = d->cancel != NULL;
    lv_obj_add_event_cb(scrim, scrim_click_cb, LV_EVENT_CLICKED, NULL);
    int nl = 0;
    for (int i = 0; i < 3; i++) nl += d->line[i] != NULL;
    int w = 416, h = 200 + (nl > 2 ? 22 : 0), x = 32, y = (800 - h) / 2;
    lv_obj_t *card = box(scrim, x, y, w, h, 26);
    flex_surface(card, FLEX_SURF_ELEVATED, FLEX_BD_FLAT);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, t->border, 0);
    lv_obj_set_clickable(card, true);   // el toque dentro de la tarjeta no cierra
    lv_obj_t *ttl = text(card, d->title ? d->title : "", FLEX_FONT_S3, t->txt);
    flex_label_cap_center(ttl, w / 2, 26);
    for (int i = 0, k = 0; i < 3; i++) {
        if (d->line[i]) {
            lv_obj_t *l = text(card, d->line[i], FLEX_FONT_S2, t->txt2);
            flex_label_cap_center(l, w / 2, 70 + 22 * k++);
        }
    }
    int by = h - 62;
    if (d->cancel && d->ok) {
        int bw = (w - 3 * 16) / 2;
        flex_kit_button(card, 16, by, bw, 46, d->cancel, FLEX_BTN_NORMAL, dlg_cancel_cb, NULL);
        flex_kit_button(card, 32 + bw, by, bw, 46, d->ok, d->danger ? FLEX_BTN_DANGER : FLEX_BTN_PRIMARY, dlg_ok_cb, NULL);
    } else if (d->ok) {
        flex_kit_button(card, (w - 140) / 2, by, 140, 46, d->ok, d->danger ? FLEX_BTN_DANGER : FLEX_BTN_PRIMARY, dlg_ok_cb, NULL);
    } else if (d->cancel) {
        flex_kit_button(card, (w - 140) / 2, by, 140, 46, d->cancel, FLEX_BTN_NORMAL, dlg_cancel_cb, NULL);
    }
    return scrim;
}

// ===== aviso local ==========================================================================================
typedef struct {
    lv_timer_t *timer;
} toast_t;

static void toast_free_cb(lv_event_t *e)
{
    toast_t *c = lv_event_get_user_data(e);
    if (c->timer) {
        lv_timer_delete(c->timer);
    }
    free(c);
}

// Un aviso local se reconoce por su callback de borrado (sin flags ni nombres
// de LVGL: ambas APIs estan obsoletas / desactivadas en esta configuracion).
static bool is_toast(lv_obj_t *o)
{
    uint32_t n = lv_obj_get_event_count(o);
    for (uint32_t i = 0; i < n; i++) {
        if (lv_event_dsc_get_cb(lv_obj_get_event_dsc(o, i)) == toast_free_cb) {
            return true;
        }
    }
    return false;
}

static void toast_timer_cb(lv_timer_t *t)
{
    lv_obj_t *o = lv_timer_get_user_data(t);
    toast_t *c = lv_obj_get_user_data(o);
    c->timer = NULL;   // este temporizador (de una sola vez) se borra solo
    lv_obj_delete(o);
}

int flex_kit_toast_count(lv_obj_t *parent)
{
    int n = 0;
    uint32_t cnt = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < cnt; i++) {
        n += is_toast(lv_obj_get_child(parent, (int32_t)i)) ? 1 : 0;
    }
    return n;
}

void flex_kit_toast(lv_obj_t *parent, const char *msg)
{
    uint32_t n = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < n; i++) {   // uno a la vez por padre
        lv_obj_t *c = lv_obj_get_child(parent, (int32_t)i);
        if (is_toast(c)) {
            lv_obj_delete(c);
            break;
        }
    }
    int tw = flex_text_width(msg, FLEX_FONT_S2) + 32;
    tw = tw > 440 ? 440 : tw;
    lv_obj_t *o = box(parent, (480 - tw) / 2, 512, tw, 34, 17);
    fill(o, FLEX_WALLSURF, 235);
    lv_obj_set_clickable(o, false);
    toast_t *c = calloc(1, sizeof(*c));
    if (!c) {
        return;
    }
    lv_obj_set_user_data(o, c);
    lv_obj_add_event_cb(o, toast_free_cb, LV_EVENT_DELETE, c);
    lv_obj_t *l = text(o, msg, FLEX_FONT_S2, FLEX_ONWALL);
    flex_label_one_line(l, tw - 16);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_x(l, 8);
    flex_label_cap_y(l, 9);
    c->timer = lv_timer_create(toast_timer_cb, TOAST_MS, o);
    lv_timer_set_repeat_count(c->timer, 1);
}
