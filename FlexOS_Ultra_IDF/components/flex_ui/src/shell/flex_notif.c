// Flex OS Ultra · avisos: banner flotante, Centro de notificaciones y No molestar
// (docs/spec/01c §6; Arduino: FlexOS_Ultra_Notif.h y FlexOS_FlexPhone_Overlay.h).
//
// Un modelo (flex_notif_model.c, probado contra Arduino) y un unico presentador:
// el banner. Los avisos llegan desde cualquier tarea por el buzon de la UI.
// El banner y el Centro son capas en lv_layer_top y su tacto llega por
// flex_touch_add_sys_hook, antes que LVGL: el banner se queda el episodio que
// empieza DENTRO de la tarjeta (no modal); el Centro entra por el borde izquierdo.
//
// Del telefono vinculado (Flex Phone) aun no llega nada: el Centro tiene los
// avisos del sistema. No molestar es real y persistente (flexphone/dnd) y lo
// respetara el puente del telefono; los avisos del sistema no le afectan.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_frame.h"
#include "flex_glass.h"
#include "flex_i18n.h"
#include "flex_icons.h"
#include "flex_inbox.h"
#include "flex_notif_model.h"
#include "flex_safeboot.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_touch_lvgl.h"

#define SCR_W 480
#define SCR_H 800

// ---- banner (Overlay.h:144-171) ------------------------------------------------------
#define FPB_X        14
#define FPB_Y        18
#define FPB_W        452
#define FPB_H        72
#define FPB_RAD      18
#define FPB_DROP_MAX (FPB_Y + FPB_H + 10)
#define FPB_IN_MS    220
#define FPB_OUT_MS   180
#define FPB_SPRING_MS 140
#define FPB_DRAG_PX  10
#define FPB_SHADOW_A 60
#define FPB_MIN_MIX  150
#define TICK_MS      16

enum { FPB_HIDDEN = 0, FPB_IN, FPB_SHOWN, FPB_OUT };

// ---- Centro (Overlay.h:868-1196) ------------------------------------------------------
#define FPC_EDGE_W    26    // SYS_EDGE_LEFT_W
#define FPC_INTENT_PX 12
#define FPC_ANIM_MS   190
#define FPC_VIEW_Y0   96
#define FPC_VIEW_Y1   792
#define FPC_ROW       78
#define FG_DRAG_SLOP  8

static flex_ntf_hist_t s_hist;
static flex_bq_t s_bq;

static struct {
    int state;
    uint32_t t0;
    float slide, drop, out_from;
    int out_dir;
    bool spring;
    float spring_from;
    uint32_t spring_t0;
    // gesto
    bool gesture, moved;
    int gx0, glx;
    uint32_t gms;
    float gvx;
    // LVGL
    lv_obj_t *card;
    lv_timer_t *tick;
    bool over_app;
} B;

static struct {
    bool open, dragging, anim_on, moved;
    float x;             // -480 (fuera) .. 0 (a la vista)
    int start_x;
    float anim_from, anim_to;
    uint32_t anim_t0;
    float scroll;
    int content_h;
    int g;               // 0 nada, 1 apoyado (puede ser toque), 2 desplazando
    int gy0;
    float off0;
    lv_obj_t *root, *list;
    lv_timer_t *tick;
} C = {.x = -SCR_W};

static bool s_init;

// ---- No molestar (Overlay.h:44-82) ----------------------------------------------------
bool flex_notif_dnd(void)
{
    return flex_kvs_get_bool("flexphone", "dnd", false);
}

static void center_build(void);

void flex_notif_set_dnd(bool on)
{
    if (on != flex_notif_dnd()) {
        flex_kvs_set_bool("flexphone", "dnd", on);   // misma clave y tipo que Arduino
    }
    if (C.root) {
        center_build();
    }
}

// ---- cuando se puede dibujar el banner (fpbScreenAllows) ---------------------------------
static bool center_busy(void);

static bool screen_allows(void)
{
    flex_shell_state_t st = flex_shell_state();
    if (st != FLEX_SH_HOME && st != FLEX_SH_APP && st != FLEX_SH_OVERLAY) {
        // bloqueo, clave, apagado, Modo seguro, restablecimiento, primera
        // configuracion: el aviso espera (fpbScreenAllows, FlexPhone_Overlay.h:316).
        // Cuando existan, tambien: Personalizar inicio, Optimizar, OTA y Modo PC.
        return false;
    }
    if (flex_power_suspended() || flex_shell_transition_active()) {
        return false;
    }
    if (flex_qs_is_open() || center_busy()) {
        return false;
    }
    return true;
}

// ---- banner: dibujo ------------------------------------------------------------------
static lv_obj_t *label_dots(lv_obj_t *p, const char *s, const lv_font_t *f, lv_color_t c, int x, int y, int w)
{
    lv_obj_t *l = flex_label(p, s, f, c);
    flex_label_one_line(l, w);   // "..." como fgTextEllipsis
    lv_obj_set_x(l, x);
    flex_label_cap_y(l, y);
    return l;
}

// Solo la tarjeta: el temporizador se apaga solo cuando no queda nada en la cola.
static void banner_destroy(void)
{
    if (B.card) {
        lv_obj_delete(B.card);
        B.card = NULL;
    }
}

static void banner_build(void)
{
    if (B.card) {
        lv_obj_delete(B.card);
    }
    const flex_palette_t *t = flex_th();
    const flex_bq_msg_t *m = &s_bq.cur;
    B.over_app = flex_shell_state() == FLEX_SH_APP;
    lv_obj_t *c = flex_box(lv_layer_top());
    B.card = c;
    lv_obj_set_size(c, FPB_W, FPB_H);
    lv_obj_set_clickable(c, false);
    lv_obj_set_style_radius(c, FPB_RAD, 0);
    flex_surface(c, FLEX_SURF_ELEVATED, B.over_app ? FLEX_BD_FLAT : FLEX_BD_HOME);
    flex_surface_set_min_mix(c, FPB_MIN_MIX);   // el texto se lee sobre cualquier fondo
    lv_obj_set_style_shadow_color(c, t->shadow, 0);
    lv_obj_set_style_shadow_opa(c, FPB_SHADOW_A, 0);
    lv_obj_set_style_shadow_offset_x(c, 3, 0);
    lv_obj_set_style_shadow_offset_y(c, 4, 0);
    lv_obj_set_style_shadow_width(c, 2, 0);
    // barra de prioridad
    lv_obj_t *bar = flex_box(c);
    lv_obj_set_pos(bar, 8, 12);
    lv_obj_set_size(bar, 4, FPB_H - 24);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, m->pri >= FLEX_PRI_HIGH ? t->primary : t->divider, 0);
    int tx = 22;
    if (m->icon) {
        int is = FPB_H - 28 < 40 ? FPB_H - 28 : 40;
        lv_obj_t *ic = flex_app_icon_create(c, m->icon - 1 == FLEX_NTF_MEDIA ? IC_MULTIMEDIA : IC_AJUSTES, is,
                                            FLEX_BD_FLAT);
        lv_obj_set_pos(ic, 20, (FPB_H - is) / 2);
        tx = 20 + is + 12;
    }
    int tw = FPB_W - tx - 18;
    label_dots(c, m->app[0] ? m->app : "Flex OS", FLEX_FONT_S1, t->mute, tx, 10, tw);
    label_dots(c, m->title, FLEX_FONT_S2, t->txt, tx, 28, tw);
    if (m->body[0]) {
        label_dots(c, m->body, FLEX_FONT_S1, t->txt2, tx, 50, tw);
    }
    uint32_t more = (uint32_t)s_bq.n + s_bq.more;
    if (more) {
        char b[16];
        snprintf(b, sizeof(b), "+%u", (unsigned)more);
        lv_obj_t *l = flex_label(c, b, FLEX_FONT_S1, t->mute);
        lv_obj_update_layout(l);
        lv_obj_set_x(l, FPB_W - 14 - lv_obj_get_width(l));
        flex_label_cap_y(l, 10);
    }
}

static void banner_place(void)
{
    if (B.card) {
        lv_obj_set_pos(B.card, FPB_X + (int)B.slide, FPB_Y - (int)B.drop);
    }
}

static float ease(float p)
{
    return 1.0f - (1.0f - p) * (1.0f - p);
}

static void banner_finish(void)
{
    B.state = FPB_HIDDEN;
    s_bq.cur_live = false;
    s_bq.more = 0;
    B.slide = B.drop = 0;
    B.gesture = false;
    banner_destroy();
}

// Algo le quito la pantalla a medias: vuelve al frente con lo que estuvo a la vista.
static void banner_requeue(void)
{
    bool again = B.state == FPB_IN || B.state == FPB_SHOWN;
    flex_bq_msg_t m = s_bq.cur;
    if (B.state == FPB_SHOWN) {
        m.shown_ms += lv_tick_get() - B.t0;
    }
    bool finger = B.gesture && flex_touch_arb()->t.down;
    B.state = FPB_HIDDEN;
    s_bq.cur_live = false;
    s_bq.refresh = false;
    B.gesture = false;
    B.slide = B.drop = 0;
    if (B.card) {
        lv_obj_delete(B.card);
        B.card = NULL;
    }
    if (again) {
        flex_bq_enqueue(&s_bq, &m, true);
    }
    if (finger) {
        flex_touch_drop_all();   // lo que quede del episodio no es de nadie
    }
}

static void begin_out(int dir)
{
    if (B.state == FPB_HIDDEN || B.state == FPB_OUT) {
        return;
    }
    B.state = FPB_OUT;
    s_bq.cur_live = false;
    B.t0 = lv_tick_get();
    B.out_dir = dir;
    B.out_from = B.slide;
    B.spring = false;
}

static void banner_tick(lv_timer_t *tm);

static void ensure_tick(void)
{
    if (!B.tick) {
        B.tick = lv_timer_create(banner_tick, TICK_MS, NULL);
    }
}

static void banner_tick(lv_timer_t *tm)
{
    (void)tm;
    if (B.state == FPB_HIDDEN && s_bq.n == 0) {
        lv_timer_delete(B.tick);   // nada que hacer: cero coste sin avisos
        B.tick = NULL;
        return;
    }
    if (B.state == FPB_HIDDEN) {
        if (!screen_allows()) {
            return;   // se espera: la cola no se pierde
        }
        flex_bq_pop(&s_bq, &s_bq.cur);
        s_bq.cur_live = true;
        s_bq.refresh = false;
        B.slide = 0;
        B.drop = FPB_DROP_MAX;
        B.state = FPB_IN;
        B.t0 = lv_tick_get();
        banner_build();
        banner_place();
        return;
    }
    if (!screen_allows()) {
        if (B.state == FPB_OUT) {
            banner_finish();   // fpbAbandon: el que ya se iba no se queda encima del bloqueo
        } else {
            banner_requeue();
        }
        return;
    }
    uint32_t now = lv_tick_get(), e = now - B.t0;
    if (s_bq.refresh) {
        if (B.state == FPB_SHOWN) {
            B.t0 = now;   // el texto cambio: vuelve a contar desde cero
            s_bq.cur.shown_ms = 0;
        }
        s_bq.refresh = false;
        banner_build();
    }
    switch (B.state) {
    case FPB_IN: {
        float p = e >= FPB_IN_MS ? 1.0f : (float)e / FPB_IN_MS;
        B.drop = (1.0f - ease(p)) * FPB_DROP_MAX;
        if (e >= FPB_IN_MS) {
            B.drop = 0;
            B.state = FPB_SHOWN;
            B.t0 = now;
        }
        break;
    }
    case FPB_SHOWN:
        if (B.spring) {
            float p = now - B.spring_t0 >= FPB_SPRING_MS ? 1.0f : (float)(now - B.spring_t0) / FPB_SPRING_MS;
            B.slide = B.spring_from * (1.0f - ease(p));
            if (p >= 1.0f) {
                B.slide = 0;
                B.spring = false;
            }
        }
        if (B.gesture || B.spring || B.slide != 0) {
            B.t0 = now;   // con el dedo encima no caduca
            break;
        }
        if (e >= flex_bq_hold_ms(s_bq.cur.shown_ms)) {
            begin_out(0);
        }
        break;
    case FPB_OUT: {
        float p = e >= FPB_OUT_MS ? 1.0f : (float)e / FPB_OUT_MS;
        if (B.out_dir == 0) {
            B.drop = ease(p) * FPB_DROP_MAX;   // sube por donde entro
        } else {
            B.slide = B.out_from + ((float)B.out_dir * (FPB_W + 40) - B.out_from) * ease(p);
        }
        if (e >= FPB_OUT_MS) {
            banner_finish();
            return;
        }
        break;
    }
    default:
        break;
    }
    banner_place();
}

// ---- banner: tacto (fpbTouch) ------------------------------------------------------------
static bool banner_hook(const flex_arb_touch_t *t)
{
    bool targetable = B.card && (B.state == FPB_IN || B.state == FPB_SHOWN);
    if (!B.gesture) {
        if (!targetable || !t->pressed) {
            return false;   // el episodio es de quien este debajo
        }
        int cx = FPB_X + (int)B.slide, cy = FPB_Y - (int)B.drop;
        if (t->x < cx || t->x >= cx + FPB_W || t->y < cy || t->y >= cy + FPB_H) {
            return false;
        }
        B.gesture = true;
        B.moved = false;
        B.spring = false;
        B.gx0 = B.glx = t->x;
        B.gms = lv_tick_get();
        B.gvx = 0;
    }
    if (!targetable) {
        B.gesture = false;   // desaparecio a mitad: el resto del episodio no es de nadie
        if (t->down) {
            flex_touch_drop_all();
        }
        return true;
    }
    if (t->down) {
        int dx = t->x - B.gx0;
        if (!B.moved && (dx > FPB_DRAG_PX || dx < -FPB_DRAG_PX)) {
            B.moved = true;
        }
        if (B.moved) {
            float lim = FPB_W + 40, sl = (float)dx;
            B.slide = sl < -lim ? -lim : sl > lim ? lim : sl;
            uint32_t now = lv_tick_get();
            if (now - B.gms >= 16u) {
                B.gvx = (float)(t->x - B.glx) / (float)(now - B.gms);
                B.glx = t->x;
                B.gms = now;
            }
            banner_place();
        }
        return true;
    }
    // soltado
    B.gesture = false;
    if (lv_tick_get() - B.gms > 100u) {
        B.gvx = 0;   // se paro antes de soltar: no es un lanzamiento
    }
    if (flex_bq_dismiss(B.slide, B.gvx, B.moved, FPB_W)) {
        begin_out(B.slide < 0 ? -1 : 1);   // del sistema: no hay nada que borrar en otro lado
    } else if (B.moved || B.slide != 0) {
        B.spring = true;
        B.spring_from = B.slide;
        B.spring_t0 = lv_tick_get();
    } else if (t->tap) {
        begin_out(0);   // un aviso del sistema no navega a ningun sitio
    }
    return true;
}

// ---- servicio --------------------------------------------------------------------------
typedef struct {
    int kind;   // 0 historial, 1 solo banner (sysSay dentro de una app), 2 sysSay: se decide al entregar
    uint8_t type;
    char app[FLEX_BQ_APP_MAX];
    char title[FLEX_NTF_TITLE_MAX];
    char sub[FLEX_BQ_BODY_MAX];
} post_t;

static void lock_refresh_if_visible(void)
{
    if (flex_shell_state() == FLEX_SH_LOCK) {
        flex_lock_refresh_widgets();
    }
}

static void post_ui(void *arg)
{
    post_t *p = arg;
    if (flex_safe_mode()) {
        // Modo seguro: sin banner (Recovery.h). Lo del sistema queda en el historial.
        if (p->kind != 1 && (p->kind != 2 || flex_shell_state() != FLEX_SH_APP)) {
            flex_ntf_push(&s_hist, p->type, p->title, p->sub, lv_tick_get());
            lock_refresh_if_visible();
        }
        free(p);
        return;
    }
    if (p->kind == 2) {
        p->kind = flex_shell_state() == FLEX_SH_APP ? 1 : 0;   // llamado desde otra tarea
    }
    if (p->kind == 1) {
        flex_bq_msg_t m;
        flex_bq_msg_init(&m, FLEX_BQ_SRC_SYSTEM, 0, p->app, p->title, p->sub, FLEX_PRI_DEFAULT);
        flex_bq_offer(&s_bq, &m);
    } else {
        char sub[FLEX_NTF_SUB_MAX];
        flex_utf8_copy(sub, sizeof(sub), p->sub);   // el historial guarda 39 bytes: sin partir un caracter
        flex_ntf_post(&s_hist, &s_bq, p->type, p->title, sub, lv_tick_get());
        if (C.root) {
            center_build();
        }
        lock_refresh_if_visible();
    }
    free(p);
    ensure_tick();
}

static void post(int kind, uint8_t type, const char *app, const char *title, const char *sub)
{
    post_t *p = calloc(1, sizeof(*p));
    if (!p) {
        return;
    }
    p->kind = kind;
    p->type = type;
    // sysNotify cortaba en bytes y podia partir una letra con tilde; aqui se corta en caracteres
    flex_utf8_copy(p->app, sizeof(p->app), app);
    flex_utf8_copy(p->title, sizeof(p->title), title);
    flex_utf8_copy(p->sub, sizeof(p->sub), sub);
    if (!flex_inbox_post(post_ui, p)) {
        free(p);   // buzon lleno: el aviso se pierde (no bloquea a quien avisa)
    }
}

void flex_notify(const char *title, const char *sub)
{
    post(0, FLEX_NTF_SYSTEM, NULL, title, sub);
}

void flex_notify_media(const char *title, const char *sub)
{
    post(0, FLEX_NTF_MEDIA, NULL, title, sub);
}

void flex_say(const char *app, const char *title, const char *sub)
{
    // sysSay mira gState AL LLAMARLO (Media.h:83): desde la UI se decide ya (una
    // app que avisa y se cierra en la misma vuelta sigue siendo "dentro de la app");
    // desde otra tarea, al entregarlo.
    int kind = 2;
    if (flex_inbox_in_ui()) {
        kind = flex_shell_state() == FLEX_SH_APP ? 1 : 0;
    }
    post(kind, FLEX_NTF_SYSTEM, app, title, sub);
}

int flex_notif_count(void)
{
    return s_hist.n;
}

const char *flex_notif_latest_title(void)
{
    return s_hist.n ? s_hist.e[s_hist.n - 1].title : NULL;
}

bool flex_notif_banner_visible(void)
{
    return B.state != FPB_HIDDEN;
}

const char *flex_notif_banner_title(void)
{
    return B.state != FPB_HIDDEN ? s_bq.cur.title : NULL;
}

int flex_notif_queue_len(void)
{
    return s_bq.n;
}

// ---- Centro de notificaciones (fpc*) ------------------------------------------------------
static bool center_busy(void)
{
    return C.open || C.dragging || C.anim_on;
}

static bool center_can_open(void)
{
    flex_shell_state_t st = flex_shell_state();
    return (st == FLEX_SH_HOME || st == FLEX_SH_APP) && !flex_qs_is_open() && !flex_power_suspended();
}

static bool es(void)
{
    return flex_li() != 1;   // el Centro solo tiene ES/EN, como Arduino
}

static void center_tick(lv_timer_t *tm);

static void center_destroy(void)
{
    if (C.tick) {
        lv_timer_delete(C.tick);
        C.tick = NULL;
    }
    if (C.root) {
        lv_obj_delete(C.root);
        C.root = C.list = NULL;
    }
}

static void center_place(void)
{
    if (C.root) {
        lv_obj_set_x(C.root, (int)C.x);
    }
    if (C.list) {
        lv_obj_set_y(C.list, -(int)(C.scroll + 0.5f));
    }
}

static void center_build(void)
{
    if (!C.root) {
        return;
    }
    lv_obj_clean(C.root);
    C.list = NULL;
    const flex_palette_t *t = flex_th();
    lv_obj_t *r = C.root;
    lv_obj_t *l = flex_label(r, es() ? "Notificaciones" : "Notifications", FLEX_FONT_S3, t->txt);
    lv_obj_set_x(l, 20);
    flex_label_cap_y(l, 26);
    char sub[40];
    if (s_hist.n) {
        snprintf(sub, sizeof(sub), es() ? "%d en total" : "%d in total", s_hist.n);
    } else {
        snprintf(sub, sizeof(sub), "%s", es() ? "Nada pendiente" : "Nothing pending");
    }
    l = flex_label(r, sub, FLEX_FONT_S1, t->mute);
    lv_obj_set_x(l, 20);
    flex_label_cap_y(l, 60);
    // pildora No molestar
    bool dnd = flex_notif_dnd();
    lv_obj_t *pill = flex_box(r);
    lv_obj_set_pos(pill, 372, 30);
    lv_obj_set_size(pill, 92, 34);
    lv_obj_set_style_radius(pill, 17, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(pill, dnd ? t->primary : t->surf2, 0);
    lv_obj_t *pl = flex_label(r, dnd ? (es() ? "Silencio" : "DND on") : (es() ? "Avisos" : "DND off"), FLEX_FONT_S1,
                              dnd ? t->on_acc : t->txt2);
    flex_label_cap_center(pl, 418, 39);
    lv_obj_t *div = flex_box(r);
    lv_obj_set_pos(div, 20, 84);
    lv_obj_set_size(div, 440, 1);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(div, t->divider, 0);
    // vista con scroll (recortada: una fila no pisa la cabecera)
    lv_obj_t *view = flex_box(r);
    lv_obj_set_pos(view, 0, FPC_VIEW_Y0);
    lv_obj_set_size(view, SCR_W, FPC_VIEW_Y1 - FPC_VIEW_Y0);
    lv_obj_set_clickable(view, false);
    if (s_hist.n == 0) {
        C.content_h = 0;
        lv_obj_t *dot = flex_box(r);
        lv_obj_set_pos(dot, 240 - 26, 186 - 26);
        lv_obj_set_size(dot, 52, 52);
        lv_obj_set_style_radius(dot, 26, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, t->surf2, 0);
        lv_obj_t *g = flex_glyph_create(dot, FLEX_GLYPH_BELL, 52, 52);
        lv_obj_set_style_text_color(g, t->mute, 0);
        lv_obj_t *tl = flex_label(r, es() ? "Todo al dia" : "All clear", FLEX_FONT_S2, t->txt2);
        flex_label_cap_center(tl, 240, 226);
        const char *msg = dnd ? (es() ? "No molestar esta activado. Lo que llegue se guarda aqui, sin banner y sin sonido."
                                      : "Do not disturb is on. Anything that arrives is kept here, without a banner or a sound.")
                              : (es() ? "Aqui aparecen las notificaciones de Flex OS y las del telefono vinculado."
                                      : "Notifications from Flex OS and from the linked phone show up here.");
        lv_obj_t *pm = flex_label(r, msg, FLEX_FONT_S1, t->mute);
        lv_label_set_long_mode(pm, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(pm, 384);
        lv_obj_set_style_text_align(pm, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_x(pm, 48);
        flex_label_cap_y(pm, 256);
        return;
    }
    C.list = flex_box(view);
    // mas reciente primero (fpcBuild: por bornMs, insercion estable)
    int order[FLEX_NTF_MAX], n = 0;
    for (int i = 0; i < s_hist.n; i++) {
        int at = n;
        while (at > 0 && s_hist.e[order[at - 1]].born_ms < s_hist.e[i].born_ms) {
            order[at] = order[at - 1];
            at--;
        }
        order[at] = i;
        n++;
    }
    for (int k = 0; k < n; k++) {
        const flex_ntf_t *e = &s_hist.e[order[k]];
        int y = k * FPC_ROW;
        lv_obj_t *card = flex_box(C.list);
        lv_obj_set_pos(card, 16, y);
        lv_obj_set_size(card, 448, 70);
        lv_obj_set_style_radius(card, 16, 0);
        flex_surface(card, FLEX_SURF_CARD, FLEX_BD_FLAT);   // fgCard: vidrio sobre la pagina o surf
        lv_obj_t *acc = flex_box(C.list);
        lv_obj_set_pos(acc, 19, y + 10);
        lv_obj_set_size(acc, 4, 50);
        lv_obj_set_style_radius(acc, 2, 0);
        lv_obj_set_style_bg_opa(acc, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(acc, t->acc_soft, 0);   // aviso del sistema
        label_dots(C.list, "Flex OS", FLEX_FONT_S1, t->mute, 38, y + 8, 370);
        label_dots(C.list, e->title, FLEX_FONT_S2, t->txt, 38, y + 26, 404);
        if (e->sub[0]) {
            label_dots(C.list, e->sub, FLEX_FONT_S1, t->txt2, 38, y + 50, 404);
        }
    }
    int by = n * FPC_ROW + 6;
    lv_obj_t *btn = flex_box(C.list);
    lv_obj_set_pos(btn, 16, by);
    lv_obj_set_size(btn, 448, 42);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, t->surf2, 0);
    lv_obj_t *bl = flex_label(btn, es() ? "Borrar todas" : "Clear all", FLEX_FONT_S1, t->danger);
    flex_label_cap_center(bl, 224, 16);
    C.content_h = by + 42;
    lv_obj_set_size(C.list, SCR_W, C.content_h + 1);
    int view_h = FPC_VIEW_Y1 - FPC_VIEW_Y0;
    if (C.content_h > view_h) {
        int bar_h = view_h * view_h / C.content_h;
        bar_h = bar_h < 28 ? 28 : bar_h;
        lv_obj_t *sb = flex_box(r);
        lv_obj_set_pos(sb, 471, FPC_VIEW_Y0);
        lv_obj_set_size(sb, 4, bar_h);
        lv_obj_set_style_radius(sb, 2, 0);
        lv_obj_set_style_bg_opa(sb, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(sb, t->track, 0);
    }
    center_place();
}

static void center_create(void)
{
    if (C.root) {
        return;
    }
    C.root = flex_box(lv_layer_top());
    lv_obj_set_size(C.root, SCR_W, SCR_H);
    lv_obj_set_clickable(C.root, false);
    lv_obj_set_style_bg_opa(C.root, LV_OPA_COVER, 0);   // fondo opaco TH_PAGE, sin vidrio
    lv_obj_set_style_bg_color(C.root, flex_th()->page, 0);
    C.scroll = 0;
    center_build();
    center_place();
    lv_obj_move_foreground(C.root);
    C.tick = lv_timer_create(center_tick, TICK_MS, NULL);
}

void flex_notif_center_close_now(void)
{
    bool was = center_busy() || C.root;
    bool finger = (C.dragging || C.g) && flex_touch_arb()->t.down;
    C.open = C.dragging = C.anim_on = false;
    C.g = 0;
    C.x = -SCR_W;
    center_destroy();
    if (was && finger) {
        flex_touch_drop_all();
    }
}

static void center_anim(float to)
{
    if (C.x == to) {   // fpcAnimTo: ya esta ahi
        C.anim_on = false;
        C.open = to > -SCR_W / 2;
        if (!C.open) {
            flex_notif_center_close_now();
        }
        return;
    }
    C.anim_from = C.x;
    C.anim_to = to;
    C.anim_t0 = lv_tick_get();
    C.anim_on = true;
}

static void center_tick(lv_timer_t *tm)
{
    (void)tm;
    if (!C.anim_on) {
        return;
    }
    uint32_t e = lv_tick_get() - C.anim_t0;
    float p = e >= FPC_ANIM_MS ? 1.0f : (float)e / FPC_ANIM_MS;
    C.x = C.anim_from + (C.anim_to - C.anim_from) * ease(p);
    if (e >= FPC_ANIM_MS) {
        C.anim_on = false;
        C.x = C.anim_to;
        C.open = C.x > -SCR_W / 2;
        if (!C.open) {
            flex_notif_center_close_now();
            return;
        }
    }
    center_place();
}

static int center_scroll_max(void)
{
    int m = C.content_h - (FPC_VIEW_Y1 - FPC_VIEW_Y0);
    return m > 0 ? m : 0;
}

static void clear_all(void)
{
    memset(&s_hist, 0, sizeof(s_hist));
    C.scroll = 0;
    center_build();
    lock_refresh_if_visible();
}

// fpcHandleHit: pildora, "Borrar todas" o una fila (del sistema: se descarta)
static void center_hit(int x, int y)
{
    if (x >= 372 && x < 464 && y >= 30 && y < 64) {
        flex_notif_set_dnd(!flex_notif_dnd());
        return;
    }
    if (y < FPC_VIEW_Y0 || y >= FPC_VIEW_Y1 || s_hist.n == 0) {
        return;
    }
    int cy = y - FPC_VIEW_Y0 + (int)(C.scroll + 0.5f);
    int by = s_hist.n * FPC_ROW + 6;
    if (x >= 16 && x < 464 && cy >= by && cy < by + 42) {
        clear_all();
        return;
    }
    int k = cy / FPC_ROW;
    if (x >= 16 && x < 464 && k >= 0 && k < s_hist.n && cy - k * FPC_ROW < 70) {
        // la fila k es la k-esima mas reciente
        int order[FLEX_NTF_MAX], n = 0;
        for (int i = 0; i < s_hist.n; i++) {
            int at = n;
            while (at > 0 && s_hist.e[order[at - 1]].born_ms < s_hist.e[i].born_ms) {
                order[at] = order[at - 1];
                at--;
            }
            order[at] = i;
            n++;
        }
        flex_ntf_remove(&s_hist, order[k]);
        int mx = center_scroll_max();
        center_build();
        mx = center_scroll_max();
        C.scroll = C.scroll > mx ? (float)mx : C.scroll;
        center_place();
        lock_refresh_if_visible();
    }
}

static bool center_hook(const flex_arb_touch_t *t)
{
    if (!center_can_open() && !center_busy()) {
        return false;
    }
    if (center_busy() && !center_can_open()) {
        flex_notif_center_close_now();
        return false;
    }
    if (!center_busy()) {
        // borde izquierdo: con el dedo apoyado y moviendose hacia dentro
        int dx = t->x - t->start_x, dy = t->y - t->start_y;
        if (!(t->down && t->start_x < FPC_EDGE_W && t->start_y > 40 && dx > FPC_INTENT_PX &&
              dx > (dy < 0 ? -dy : dy))) {
            return false;
        }
        C.dragging = true;
        C.moved = true;
        C.start_x = t->start_x;
        C.x = -SCR_W + dx;
        C.x = C.x > 0 ? 0 : C.x;
        center_create();
        center_place();
        return true;
    }
    if (C.anim_on) {
        C.g = 0;
        return true;   // mientras entra o sale, el tacto es suyo y no hace nada
    }
    if (C.dragging) {
        if (t->down) {
            float x = (float)(-SCR_W + (t->x - C.start_x));
            C.x = x < -SCR_W ? -SCR_W : x > 0 ? 0 : x;
            center_place();
            return true;
        }
        C.dragging = false;
        center_anim(C.x <= -SCR_W / 2 ? -SCR_W : 0);
        return true;
    }
    // abierto. Un arrastre hacia la derecha desde el contenido lo cierra (como Arduino).
    if (t->down && t->x - t->start_x > 60 && abs(t->y - t->start_y) < 40) {
        C.g = 0;
        center_anim(-SCR_W);
        return true;
    }
    if (t->pressed) {
        C.g = 1;
        C.gy0 = t->y;
        C.off0 = C.scroll;
        return true;
    }
    if (C.g == 1 && t->down && abs(t->y - C.gy0) > FG_DRAG_SLOP) {
        C.g = 2;   // desplazamiento: ya no sera un toque
    }
    if (C.g == 2 && t->down) {
        float off = C.off0 + (float)(C.gy0 - t->y);   // fgDragStep: 1:1 desde el apoyo, sin inercia ni goma
        int mx = center_scroll_max();
        C.scroll = off < 0 ? 0 : off > mx ? (float)mx : off;
        center_place();
        return true;
    }
    if (C.g && !t->down) {
        if (C.g == 1 && t->tap) {
            center_hit(t->x, t->y);
        }
        C.g = 0;
        return true;
    }
    return true;
}

bool flex_notif_center_open(void)
{
    return center_busy();
}

void flex_notif_center_show(void)
{
    if (center_busy() || !center_can_open()) {
        return;
    }
    C.x = -SCR_W;
    center_create();
    center_anim(0);
}

// ---- inicio ---------------------------------------------------------------------------
static void theme_cb(void *ctx)
{
    (void)ctx;
    if (C.root) {
        lv_obj_set_style_bg_color(C.root, flex_th()->page, 0);
        center_build();
    }
    if (B.card) {
        banner_build();
        banner_place();
    }
}

void flex_notif_init(void)
{
    if (s_init) {
        return;
    }
    s_init = true;
    flex_touch_add_sys_hook(banner_hook, 0);
    flex_touch_add_sys_hook(center_hook, 1);
    flex_theme_listen(theme_cb, NULL);
}
