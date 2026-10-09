// Flex OS Ultra · Recientes (docs/spec/01a §12; Arduino:
// FlexOS_Ultra_AppSwitcher.h).
//
// Carrusel horizontal de tarjetas 260x480 (una por app viva, la mas reciente
// primero) sobre el escritorio velado. Arrastrar en horizontal mueve el
// carrusel (con inercia y enganche al centro); arrastrar una tarjeta hacia
// arriba la levanta y, pasados 110 px, cierra la app de verdad; mantenerla
// 480 ms abre su ficha. La tarjeta central se toca para volver a la app.
// Miniatura: captura de la app al suspenderla (lv_snapshot) reducida a 150x250
// por el vecino mas cercano, como captureThumb; solo las 4 mas recientes.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flex_app.h"
#include "flex_frame.h"
#include "flex_glass.h"
#include "flex_i18n.h"
#include "flex_icons.h"
#include "flex_recents_model.h"
#include "flex_shell.h"
#include "flex_theme.h"
#include "flex_touch_lvgl.h"
#include "flex_wallmgr.h"
#ifndef FLEX_SIM
#include "esp_heap_caps.h"
#endif

#define CW        260
#define CH        480
#define STEP      288
#define TOP       92
#define LIFT_MAX  116
#define LIFT_CLOSE 110
#define LONG_MS   480
#define TOAST_MS  1800
#define THUMB_BYTES (FLEX_RC_TH_W * FLEX_RC_TH_H * 2)
#define MEM_CRIT_BYTES (6u * 1024u * 1024u)   // margen de seguridad de PSRAM (FLEXMEM_CRIT)

static void free_thumb(uint16_t *p)
{
    free(p);
}

static void terminate_app(int app)
{
    flex_app_terminate(app);
}

static flex_rc_list_t L = {.free_thumb = free_thumb, .terminate = terminate_app};

static struct {
    lv_obj_t *root, *car, *closeall, *closeall_lbl, *toast, *modal, *empty;
    lv_obj_t *cards[FLEX_RC_MAX];
    lv_image_dsc_t th[FLEX_RC_MAX];
    lv_timer_t *toast_t;
    // gesto sobre una tarjeta
    int press_card;
    lv_point_t p0;
    uint32_t t0;
    int gesture;   // 0 sin decidir, 1 horizontal, 2 vertical
    int32_t lift;
    bool long_done;
    bool opening;
} R = {.press_card = -1};

// ---- captura al suspender (lo llama el marco de apps) ------------------------------
static uint16_t *alloc_thumb(void)
{
    int keep = flex_look()->eff_mode ? 1 : FLEX_RC_THUMB_MAX;
    flex_rc_thumb_trim(&L, keep - 1);   // hueco para la que se toma ahora (el pico no sube)
#ifndef FLEX_SIM
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < MEM_CRIT_BYTES + THUMB_BYTES) {
        return NULL;   // apurado: la miniatura es lo primero que sobra
    }
#endif
    return malloc(THUMB_BYTES);
}

void flex_recents_note_suspend(int app, lv_obj_t *root, bool capture)
{
    if (app < 0 || app >= FLEX_APP_N) {
        return;
    }
    flex_rc_push(&L, (uint8_t)app);
    if (!capture || !root) {
        if (L.c[0].thumb) {
            free(L.c[0].thumb);   // horizontal / protegida: sin miniatura (swPushNoThumb)
            L.c[0].thumb = NULL;
        }
        return;
    }
    if (!L.c[0].thumb) {
        L.c[0].thumb = alloc_thumb();
    }
    if (!L.c[0].thumb) {
        return;
    }
    lv_draw_buf_t *snap = lv_snapshot_take(root, LV_COLOR_FORMAT_RGB565);
    if (!snap) {
        free(L.c[0].thumb);
        L.c[0].thumb = NULL;
        return;
    }
    if (snap->header.w >= 480 && snap->header.h >= 800) {
        flex_rc_downscale((const uint16_t *)snap->data, (int)(snap->header.stride / 2), L.c[0].thumb);
    } else {
        free(L.c[0].thumb);
        L.c[0].thumb = NULL;
    }
    lv_draw_buf_destroy(snap);
}

void flex_recents_note_closed(int app)
{
    flex_rc_drop(&L, flex_rc_find(&L, app));
}

// ---- pantalla --------------------------------------------------------------------------
static void rebuild(void);
static void close_recents(void);

static void toast_hide(lv_timer_t *t)
{
    (void)t;
    R.toast_t = NULL;
    if (R.toast) {
        lv_obj_set_hidden(R.toast, true);
    }
}

static void toast(const char *msg)
{
    if (!R.root) {
        return;
    }
    if (R.toast) {
        lv_obj_delete(R.toast);
    }
    lv_point_t sz;
    lv_text_get_size(&sz, msg, FLEX_FONT_S2, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int32_t tw = sz.x + 34;
    R.toast = flex_box(R.root);
    lv_obj_set_pos(R.toast, (480 - tw) / 2, TOP + CH - 60);
    lv_obj_set_size(R.toast, tw, 34);
    lv_obj_set_style_radius(R.toast, 17, 0);
    lv_obj_set_style_bg_opa(R.toast, 235, 0);
    lv_obj_set_style_bg_color(R.toast, FLEX_WALLSURF, 0);
    lv_obj_set_clickable(R.toast, false);
    lv_obj_t *l = flex_label(R.toast, msg, FLEX_FONT_S2, FLEX_ONWALL);
    flex_label_cap_center(l, tw / 2, 9);
    lv_obj_set_x(l, 0);
    lv_obj_set_width(l, tw);
    if (R.toast_t) {
        lv_timer_delete(R.toast_t);
    }
    R.toast_t = lv_timer_create(toast_hide, TOAST_MS, NULL);
    lv_timer_set_repeat_count(R.toast_t, 1);
}

static const char *state_name(int app)
{
    (void)app;
    return "Pausada";   // "Estado guardado" llega con el soltado de recursos (memoria)
}

static bool app_dirty(int app)
{
    const flex_app_def_t *d = flex_app_def(app);
    return d && d->ops && d->ops->dirty && d->ops->dirty();
}

static bool app_bg_busy(int app)
{
    const flex_app_def_t *d = flex_app_def(app);
    return d && d->ops && d->ops->bg_work && d->ops->bg_work();
}

// swCloseCard: false si se niega (trabajo esencial en curso)
static bool close_card(int idx)
{
    if (idx < 0 || idx >= L.n) {
        return false;
    }
    int app = L.c[idx].app;
    if (app_bg_busy(app)) {
        toast("Tarea en curso: no se cierra");
        return false;
    }
    flex_app_terminate(app);   // tambien quita la tarjeta (flex_recents_note_closed)
    return true;
}

static void close_all(void)
{
    int kept = 0;
    for (int i = L.n - 1; i >= 0; i--) {
        int app = L.c[i].app;
        if (app_bg_busy(app)) {
            kept++;
            continue;
        }
        flex_app_terminate(app);
    }
    rebuild();
    if (kept) {
        toast("Tarea en curso: no se cierra");
    }
}

static int centered_card(void)
{
    int32_t sx = lv_obj_get_scroll_x(R.car);
    int i = (int)((sx + STEP / 2) / STEP);
    return i < 0 ? 0 : i >= L.n ? L.n - 1 : i;
}

// ---- hoja modal (ficha y confirmacion) ------------------------------------------------
static void modal_close(void)
{
    if (R.modal) {
        lv_obj_delete_async(R.modal);
        R.modal = NULL;
    }
}

static void modal_scrim_cb(lv_event_t *e)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    (void)e;
    if (p.y < 250 || p.y > 550) {
        modal_close();   // tocar fuera de la hoja la cierra
    }
}

typedef struct {
    int kind;   // 0 ficha, 1 confirmar cerrar todas
    int idx;
} modal_t;
static modal_t s_modal;

static void modal_btn_cb(lv_event_t *e)
{
    int right = (int)(intptr_t)lv_event_get_user_data(e);
    modal_t m = s_modal;
    modal_close();
    if (!right) {
        return;   // "Volver" / "Cancelar"
    }
    if (m.kind == 0) {
        if (close_card(m.idx)) {
            rebuild();
        }
    } else {
        close_all();
    }
}

static lv_obj_t *modal_open(int kind, int idx)
{
    modal_close();
    s_modal.kind = kind;
    s_modal.idx = idx;
    const flex_palette_t *t = flex_th();
    R.modal = flex_box(R.root);
    lv_obj_set_size(R.modal, 480, 800);
    lv_obj_set_clickable(R.modal, true);
    lv_obj_set_style_bg_opa(R.modal, 150, 0);   // velo TH_SCRIM a150 sobre el fondo
    lv_obj_set_style_bg_color(R.modal, t->scrim, 0);
    lv_obj_add_event_cb(R.modal, modal_scrim_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sh = flex_box(R.modal);
    lv_obj_set_pos(sh, 28, 250);
    lv_obj_set_size(sh, 424, 300);
    lv_obj_set_style_radius(sh, 24, 0);
    flex_surface(sh, FLEX_SURF_WALL, FLEX_BD_HOME);
    flex_surface_set_veil(sh, lv_color_make(8, 10, 18), 70);
    lv_obj_set_clickable(sh, true);
    static const char *const lbl[2][2] = {{"Volver", "Cerrar"}, {"Cancelar", "Cerrar todas"}};
    for (int b = 0; b < 2; b++) {
        lv_obj_t *bt = flex_box(sh);
        lv_obj_set_pos(bt, (b ? 248 : 44) - 28, 488 - 250);
        lv_obj_set_size(bt, 188, 46);
        lv_obj_set_style_radius(bt, 23, 0);
        lv_obj_set_style_bg_opa(bt, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bt, b ? t->danger : t->surf2, 0);
        lv_obj_set_clickable(bt, true);
        lv_obj_add_event_cb(bt, modal_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)b);
        lv_obj_t *l = flex_label(bt, lbl[kind][b], FLEX_FONT_S2, b ? t->on_acc : t->txt);
        flex_label_cap_center(l, 94, 23 - 8);
    }
    return sh;
}

static void info_row(lv_obj_t *sh, int k, const char *label, const char *value, lv_color_t vc)
{
    lv_obj_t *l = flex_label(sh, label, FLEX_FONT_S1, FLEX_ONWALL2);
    lv_obj_set_x(l, 48 - 28);
    flex_label_cap_y(l, 334 + k * 26 - 250 + 2);
    lv_obj_t *v = flex_label(sh, value, FLEX_FONT_S2, vc);
    lv_obj_set_width(v, 200);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_x(v, 432 - 28 - 200);
    flex_label_cap_y(v, 334 + k * 26 - 250);
}

static void info_open(int idx)
{
    if (idx < 0 || idx >= L.n) {
        return;
    }
    int app = L.c[idx].app;
    lv_obj_t *sh = modal_open(0, idx);
    lv_obj_t *ic = flex_app_icon_create(sh, app, 48, FLEX_BD_HOME);
    lv_obj_set_pos(ic, 48 - 28, 268 - 250);
    lv_obj_set_clickable(ic, false);
    lv_obj_t *nm = flex_label(sh, flex_app_name(app), FLEX_FONT_S3, FLEX_ONWALL);
    lv_obj_set_x(nm, 112 - 28);
    flex_label_cap_y(nm, 270 - 250);
    lv_obj_t *st = flex_label(sh, state_name(app), FLEX_FONT_S1, FLEX_ONWALL2);
    lv_obj_set_x(st, 112 - 28);
    flex_label_cap_y(st, 298 - 250);
    const flex_app_def_t *d = flex_app_def(app);
    static const char *const cls[3] = {"Ligera", "Media", "Pesada"};
    char buf[32];
    info_row(sh, 0, "Consumo estimado", "No disponible", FLEX_ONWALL);
    info_row(sh, 1, "Clase", cls[d && d->weight < 3 ? d->weight : 0], FLEX_ONWALL);
    info_row(sh, 2, "Miniatura", L.c[idx].thumb ? "73 KB" : "Sin captura", FLEX_ONWALL);
    uint32_t used = flex_app_last_used(app);
    if (!used) {
        snprintf(buf, sizeof(buf), "No disponible");
    } else {
        uint32_t s = lv_tick_elaps(used) / 1000;
        if (s < 60) {
            snprintf(buf, sizeof(buf), "hace %u s", (unsigned)s);
        } else if (s < 3600) {
            snprintf(buf, sizeof(buf), "hace %u min", (unsigned)(s / 60));
        } else {
            snprintf(buf, sizeof(buf), "hace %u h", (unsigned)(s / 3600));
        }
    }
    info_row(sh, 3, "\xC3\x9Altima actividad", buf, FLEX_ONWALL);
    bool dirty = app_dirty(app);
    info_row(sh, 4, "Cambios sin guardar", dirty ? "S\xC3\xAD" : "No", dirty ? flex_th()->warn : FLEX_ONWALL);
}

static void confirm_close_all(int unsaved)
{
    lv_obj_t *sh = modal_open(1, -1);
    lv_obj_t *t1 = flex_label(sh, "Cerrar todas las apps", FLEX_FONT_S2, FLEX_ONWALL);
    flex_label_cap_center(t1, 212, 30);
    char buf[64];
    snprintf(buf, sizeof(buf), "%d app%s tiene%s cambios sin guardar.", unsaved, unsaved == 1 ? "" : "s",
             unsaved == 1 ? "" : "n");
    lv_obj_t *t2 = flex_label(sh, buf, FLEX_FONT_S2, FLEX_ONWALL);
    flex_label_cap_center(t2, 212, 80);
    lv_obj_t *t3 = flex_label(sh, "Flex OS intentar\xC3\xA1 guardarlos antes de cerrar;", FLEX_FONT_S1, FLEX_ONWALL2);
    flex_label_cap_center(t3, 212, 124);
    lv_obj_t *t4 = flex_label(sh, "la que no pueda guardarse se queda abierta.", FLEX_FONT_S1, FLEX_ONWALL2);
    flex_label_cap_center(t4, 212, 146);
}

// ---- tarjetas ---------------------------------------------------------------------------
static void lift_exec(void *obj, int32_t v)
{
    lv_obj_set_style_translate_y(obj, -v, 0);
}

static void card_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *card = lv_event_get_current_target_obj(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (R.modal || R.opening) {
        return;
    }
    if (code == LV_EVENT_PRESSED) {
        R.press_card = idx;
        R.p0 = p;
        R.t0 = lv_tick_get();
        R.gesture = 0;
        R.lift = 0;
        R.long_done = false;
        lv_anim_delete(card, lift_exec);
        return;
    }
    if (R.press_card != idx) {
        return;
    }
    int32_t dx = p.x - R.p0.x, dy = R.p0.y - p.y;
    if (code == LV_EVENT_PRESSING) {
        if (R.gesture == 0) {
            if (LV_ABS(dx) > 12 || lv_obj_is_scrolling(R.car)) {
                R.gesture = 1;   // horizontal: el carrusel
            } else if (LV_ABS(dy) > 14) {
                R.gesture = 2;   // vertical: levanta la tarjeta
            } else if (!R.long_done && lv_tick_elaps(R.t0) >= LONG_MS) {
                R.long_done = true;   // mantener 480 ms sin moverse -> ficha
                lv_indev_wait_release(lv_indev_active());
                info_open(idx);
                return;
            }
        }
        if (R.gesture == 2) {
            R.lift = dy < 0 ? 0 : dy > LIFT_MAX ? LIFT_MAX : dy;
            lift_exec(card, R.lift);
        }
        return;
    }
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (R.gesture == 2) {
            if (R.lift > LIFT_CLOSE && close_card(idx)) {
                R.press_card = -1;
                lv_async_call((lv_async_cb_t)rebuild, NULL);
                return;
            }
            lv_anim_t a;   // vuelve a su sitio
            lv_anim_init(&a);
            lv_anim_set_var(&a, card);
            lv_anim_set_exec_cb(&a, lift_exec);
            lv_anim_set_values(&a, R.lift, 0);
            lv_anim_set_duration(&a, 150);
            lv_anim_start(&a);
        }
        return;
    }
    if (code == LV_EVENT_SHORT_CLICKED && R.gesture == 0 && !R.long_done) {
        if (idx == centered_card()) {
            // reanuda la app donde estaba
            int app = L.c[idx].app;
            lv_area_t a;
            lv_obj_get_coords(card, &a);
            R.opening = true;
            close_recents();
            flex_app_open(app, &a);
        } else {
            lv_obj_scroll_to_x(R.car, idx * STEP, LV_ANIM_ON);   // lateral: la centra
        }
    }
}

static void build_card(int i)
{
    const flex_palette_t *t = flex_th();
    int app = L.c[i].app;
    lv_obj_t *c = flex_box(R.car);
    R.cards[i] = c;
    lv_obj_set_pos(c, i * STEP, TOP);
    lv_obj_set_size(c, CW, CH);
    lv_obj_set_style_radius(c, 22, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(c, t->surf, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, t->border, 0);
    lv_obj_set_clickable(c, true);
    lv_obj_set_scroll_chain(c, true);
    static const lv_event_code_t codes[] = {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED,
                                            LV_EVENT_PRESS_LOST, LV_EVENT_SHORT_CLICKED};
    for (size_t k = 0; k < sizeof(codes) / sizeof(codes[0]); k++) {
        lv_obj_add_event_cb(c, card_cb, codes[k], (void *)(intptr_t)i);
    }
    if (L.c[i].thumb) {
        lv_image_dsc_t *d = &R.th[i];
        memset(d, 0, sizeof(*d));
        d->header.magic = LV_IMAGE_HEADER_MAGIC;
        d->header.cf = LV_COLOR_FORMAT_RGB565;
        d->header.w = FLEX_RC_TH_W;
        d->header.h = FLEX_RC_TH_H;
        d->header.stride = FLEX_RC_TH_W * 2;
        d->data = (const uint8_t *)L.c[i].thumb;
        d->data_size = THUMB_BYTES;
        lv_obj_t *img = lv_image_create(c);
        lv_image_set_src(img, d);
        lv_obj_set_pos(img, 8, 8);
        lv_obj_set_size(img, CW - 16, CH - 76);   // 244x404: la proporcion de la miniatura
        lv_image_set_inner_align(img, LV_IMAGE_ALIGN_STRETCH);
        lv_image_set_antialias(img, false);   // vecino mas cercano, como blitThumbScaled
        lv_obj_set_clickable(img, false);
    } else {
        lv_obj_t *ph = flex_box(c);   // sin miniatura: marco con el icono
        lv_obj_set_pos(ph, 8, 8);
        lv_obj_set_size(ph, CW - 16, CH - 76);
        lv_obj_set_style_radius(ph, 14, 0);
        lv_obj_set_style_bg_opa(ph, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(ph, t->surf2, 0);
        lv_obj_set_clickable(ph, false);
        lv_obj_t *ic = flex_app_icon_create(c, app, 60, FLEX_BD_FLAT);
        lv_obj_set_pos(ic, CW / 2 - 30, 180);
        lv_obj_set_clickable(ic, false);
    }
    lv_obj_t *nm = flex_label(c, flex_app_name(app), FLEX_FONT_S2, t->txt);
    flex_label_cap_center(nm, CW / 2, 428);
    if (app_dirty(app)) {
        lv_point_t sz;
        lv_text_get_size(&sz, flex_app_name(app), FLEX_FONT_S2, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        lv_obj_t *dot = flex_box(c);   // cambios sin guardar
        lv_obj_set_pos(dot, CW / 2 + sz.x / 2 + 8 - 4, 428 + 8 - 4);
        lv_obj_set_size(dot, 9, 9);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, t->warn, 0);
    }
    lv_obj_t *st = flex_label(c, state_name(app), FLEX_FONT_S1, t->txt2);
    flex_label_cap_center(st, CW / 2, 452);
}

static void closeall_cb(lv_event_t *e)
{
    (void)e;
    if (L.n == 0 || R.modal) {
        return;
    }
    int unsaved = 0;
    for (int i = 0; i < L.n; i++) {
        unsaved += app_dirty(L.c[i].app);
    }
    if (unsaved) {
        confirm_close_all(unsaved);   // cerrar sin avisar una nota a medias es perder trabajo
    } else {
        close_all();
    }
}

static void rebuild(void)
{
    if (!R.root) {
        return;
    }
    lv_obj_clean(R.car);
    memset(R.cards, 0, sizeof(R.cards));
    for (int i = 0; i < L.n; i++) {
        build_card(i);
    }
    lv_obj_set_hidden(R.empty, L.n > 0);
    const flex_palette_t *t = flex_th();
    bool on = L.n > 0;
    lv_obj_set_style_bg_color(R.closeall, on ? t->surf2 : t->surf, 0);
    lv_obj_set_style_border_color(R.closeall, on ? t->border : t->divider, 0);
    lv_obj_set_style_text_color(R.closeall_lbl, on ? FLEX_ONWALL : t->disabled, 0);
}

static void root_cb(lv_event_t *e)
{
    (void)e;
    if (R.modal || R.opening) {
        return;
    }
    close_recents();   // cualquier otro sitio (y > 740 incluido) -> Inicio
}

static int32_t back_path(const lv_anim_t *a)
{
    // ease-out-back de Arduino: 1 + 2.6 (p-1)^3 + 1.6 (p-1)^2
    float p = a->duration ? (float)(a->act_time < 0 ? 0 : a->act_time) / (float)a->duration : 1.0f;
    p = p > 1.0f ? 1.0f : p;
    float q = p - 1.0f;
    float e = 1.0f + 2.6f * q * q * q + 1.6f * q * q;
    return a->start_value + (int32_t)((float)(a->end_value - a->start_value) * e);
}

static void scale_exec(void *obj, int32_t v)
{
    lv_obj_set_style_transform_scale(obj, v, 0);
}

static void ov_close(void)
{
    close_recents();
}

static const flex_overlay_ops_t s_ops = {ov_close, ov_close, ov_close};

static void close_recents(void)
{
    if (!R.root) {
        return;
    }
    if (R.toast_t) {
        lv_timer_delete(R.toast_t);
        R.toast_t = NULL;
    }
    lv_obj_delete_async(R.root);
    R.root = R.car = R.closeall = R.closeall_lbl = R.toast = R.modal = R.empty = NULL;
    flex_shell_overlay_end();
    flex_touch_drop_all();
    R.opening = false;
}

void flex_recents_open(void)
{
    if (R.root || flex_shell_state() != FLEX_SH_HOME) {
        return;
    }
    R.press_card = -1;
    R.opening = false;
    R.root = flex_box(flex_shell_screen());
    lv_obj_set_size(R.root, 480, 800);
    lv_obj_set_clickable(R.root, true);
    lv_obj_add_event_cb(R.root, root_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_move_foreground(R.root);
    // fondo: el escritorio velado (blurBg)
    const lv_image_dsc_t *wall = flex_wallmgr_image(FLEX_WALL_HOME);
    lv_obj_set_style_bg_opa(R.root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(R.root, flex_th()->page, 0);
    if (wall) {
        lv_obj_t *img = lv_image_create(R.root);
        lv_image_set_src(img, wall);
        lv_obj_set_clickable(img, false);
        lv_obj_t *veil = flex_box(R.root);
        lv_obj_set_size(veil, 480, 800);
        lv_obj_set_style_bg_opa(veil, 70, 0);
        lv_obj_set_style_bg_color(veil, lv_color_make(8, 10, 18), 0);
        lv_obj_set_clickable(veil, false);
    }
    lv_obj_t *title = flex_label(R.root, "Recientes", FLEX_FONT_S3, FLEX_ONWALL);
    flex_label_cap_center(title, 240, 30);

    R.car = flex_box(R.root);
    lv_obj_set_size(R.car, 480, 600);
    lv_obj_set_clickable(R.car, false);
    lv_obj_set_scrollable(R.car, true);
    lv_obj_set_scroll_dir(R.car, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(R.car, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(R.car, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_left(R.car, 240 - CW / 2, 0);
    lv_obj_set_style_pad_right(R.car, 240 - CW / 2, 0);

    R.empty = flex_label(R.root, "Sin apps recientes", FLEX_FONT_S2, FLEX_ONWALL2);
    flex_label_cap_center(R.empty, 240, 332);

    R.closeall = flex_box(R.root);
    lv_obj_set_pos(R.closeall, 140, 600);
    lv_obj_set_size(R.closeall, 200, 46);
    lv_obj_set_style_radius(R.closeall, 23, 0);
    lv_obj_set_style_bg_opa(R.closeall, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(R.closeall, 1, 0);
    lv_obj_set_clickable(R.closeall, true);
    lv_obj_add_event_cb(R.closeall, closeall_cb, LV_EVENT_CLICKED, NULL);
    R.closeall_lbl = flex_label(R.closeall, "Cerrar todas", FLEX_FONT_S2, FLEX_ONWALL);
    flex_label_cap_center(R.closeall_lbl, 100, 14);
    lv_obj_t *foot = flex_label(R.root, "Desliza una tarjeta arriba para cerrar", FLEX_FONT_S1, FLEX_ONWALL2);
    flex_label_cap_center(foot, 240, 772);
    rebuild();

    flex_shell_overlay_begin(&s_ops);
    flex_navbar_set_ctx(FLEX_NAV_HIDDEN);
    // entrada: escala 0.6 -> 1 en 150 ms con rebote
    lv_obj_set_style_transform_pivot_x(R.car, 240, 0);
    lv_obj_set_style_transform_pivot_y(R.car, TOP + CH / 2, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, R.car);
    lv_anim_set_exec_cb(&a, scale_exec);
    lv_anim_set_values(&a, 154, 256);
    lv_anim_set_duration(&a, 150);
    lv_anim_set_path_cb(&a, back_path);
    lv_anim_start(&a);
}

bool flex_recents_is_open(void)
{
    return R.root != NULL;
}

int flex_recents_count(void)
{
    return L.n;
}

int flex_recents_app_at(int i)
{
    return i >= 0 && i < L.n ? L.c[i].app : -1;
}

bool flex_recents_has_thumb(int i)
{
    return i >= 0 && i < L.n && L.c[i].thumb != NULL;
}

void flex_recents_close_now(void)
{
    if (R.root) {
        lv_obj_delete(R.root);
        if (R.toast_t) {
            lv_timer_delete(R.toast_t);
            R.toast_t = NULL;
        }
        R.root = R.car = R.closeall = R.closeall_lbl = R.toast = R.modal = R.empty = NULL;
        flex_shell_overlay_end();
    }
}
