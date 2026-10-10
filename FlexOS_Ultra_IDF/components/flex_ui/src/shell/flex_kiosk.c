// Flex OS Ultra · Modo kiosco (docs/spec/01a §5.14, 01c §12; Lock.h:398-622).
//
// Prestar el aparato con UNA app abierta y, si se quiere, una zona de la
// pantalla sin tactil. Necesita la clave del sistema: es la unica salida
// (mantener el candado de la esquina > 1 s y escribirla). Persiste al
// reiniciar. Los vetos (Atras, Inicio, Recientes, cortina, Centro, caja,
// otra app, crear la clave, autobloqueo, suspension) los consultan sus
// modulos con flex_kiosk_active().
//
// En Modo seguro el kiosco no se aplica en ese arranque (la NVS no cambia):
// el Modo seguro ya pide la clave para dar acceso y "Reiniciar normalmente"
// vuelve a la app clavada.
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_auth.h"
#include "flex_frame.h"
#include "flex_glass.h"
#include "flex_i18n.h"
#include "flex_kiosk_model.h"
#include "flex_safeboot.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_touch_lvgl.h"

#define SCR_W 480
#define SCR_H 800
#define BTN_Y (SCR_H - 96)   // KS_BTN_Y
#define BTN_W 180
#define BTN_H 64

static flex_kiosk_t K;
static lv_obj_t *s_badge;
static bool s_exit_fired;   // ya se disparo con ESTE contacto

// ---- persistencia (kioskSave) ----------------------------------------------------------
static void save(void)
{
    flex_cfg_set_bool("kioskon", K.on);
    flex_cfg_set_i32("kioskapp", K.app);
    flex_cfg_set_i32("kioskx", K.x);
    flex_cfg_set_i32("kiosky", K.y);
    flex_cfg_set_i32("kioskw", K.w);
    flex_cfg_set_i32("kioskh", K.h);
}

bool flex_kiosk_active(void)
{
    return K.on;
}

int flex_kiosk_app(void)
{
    return K.on ? K.app : -1;
}

// ---- candado de la esquina (kioskBadgePaint) ---------------------------------------------
static lv_obj_t *part(lv_obj_t *p, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, lv_color_t c, lv_opa_t a)
{
    lv_obj_t *o = flex_box(p);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_opa(o, a, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_clickable(o, false);
    return o;
}

static void badge_build(void)
{
    if (s_badge) {
        lv_obj_delete(s_badge);
    }
    // Sobre CUALQUIER app: pastilla con el fondo y el glifo del tema. En la capa
    // del sistema (encima de la app y de las capas del shell) y sin tocar: los
    // toques siguen llegando a la app, como en Arduino.
    const flex_palette_t *t = flex_th();
    const int bx = FLEX_ARB_KIOSK_BADGE_X, by = FLEX_ARB_KIOSK_BADGE_Y, bs = FLEX_ARB_KIOSK_BADGE_S;
    s_badge = part(lv_layer_sys(), bx, by, bs, bs, 7, t->page, 200);
    part(s_badge, bs / 2 - 5, 9 - 5, 11, 11, LV_RADIUS_CIRCLE, t->txt, LV_OPA_COVER);   // arco
    part(s_badge, bs / 2 - 3, 9 - 3, 7, 7, LV_RADIUS_CIRCLE, t->page, LV_OPA_COVER);
    part(s_badge, 5, 11, bs - 10, 9, 2, t->txt, LV_OPA_COVER);                          // cuerpo
    lv_obj_set_hidden(s_badge, true);
}

static bool on_pinned_app(void)
{
    return K.on && flex_shell_state() == FLEX_SH_APP && flex_app_current() == K.app;
}

bool flex_kiosk_badge_visible(void)
{
    return s_badge && !lv_obj_is_hidden(s_badge);
}

// ---- salida (kioskTick + LSU_AFTER_KIOSKOUT) -----------------------------------------------
static void exit_ok(void *ctx)
{
    (void)ctx;
    flex_kiosk_exit_now();
}

static void exit_cancel(void *ctx)
{
    (void)ctx;
    flex_shell_return_to_app();   // cancelar vuelve a la app clavada, nunca al escritorio
}

static void exit_verify(void *arg)
{
    (void)arg;
    if (!on_pinned_app()) {
        return;
    }
    flex_touch_drop_all();   // el dedo que mantuvo el candado no teclea en la clave
    flex_auth_req_t r = {.on_ok = exit_ok, .on_cancel = exit_cancel};
    flex_auth_verify(&r);
}

// Cada lectura del tactil (tarea de UI): filtro de la zona excluida, veto de la
// suspension, candado a la vista y gesto de salida.
static void poll_hook(flex_arb_t *a, bool after)
{
    const flex_arb_touch_t *t = &a->t;
    bool pinned = on_pinned_app();
    if (!after) {
        a->kiosk_on = K.on;
        // la zona excluida SOLO con la app clavada delante: nunca sobre la clave
        // ni los menus del sistema (con ella encima del teclado no habria salida)
        a->kiosk_filter = pinned;
        a->kx = (int16_t)K.x;
        a->ky = (int16_t)K.y;
        a->kw = (int16_t)K.w;
        a->kh = (int16_t)K.h;
        if (s_badge && lv_obj_is_hidden(s_badge) == pinned) {
            lv_obj_set_hidden(s_badge, !pinned);
        }
        return;
    }
    if (!t->down) {
        s_exit_fired = false;   // el rearme va antes del filtro de estado (reintentar tras cancelar)
    }
    if (!pinned || s_exit_fired) {
        return;
    }
    if (flex_kiosk_exit_due(t->down, flex_arb_kiosk_in_exit(t->start_x, t->start_y), t->down_ms, lv_tick_get(),
                            t->x - t->start_x, t->y - t->start_y)) {
        s_exit_fired = true;
        lv_async_call(exit_verify, NULL);   // no se crea UI dentro de la lectura del tactil
    }
}

// ---- entrar / salir ---------------------------------------------------------------------
void flex_kiosk_load(void)
{
    flex_kiosk_t saved = {
        flex_cfg_get_bool("kioskon", false), (int)flex_cfg_get_i32("kioskapp", -1),
        (int)flex_cfg_get_i32("kioskx", 0),  (int)flex_cfg_get_i32("kiosky", 0),
        (int)flex_cfg_get_i32("kioskw", 0),  (int)flex_cfg_get_i32("kioskh", 0),
    };
    K = flex_kiosk_effective(&saved, FLEX_APP_N, flex_auth_required());
    if (flex_safe_mode()) {
        K = (flex_kiosk_t){false, -1, 0, 0, 0, 0};   // este arranque no (la NVS sigue igual)
    }
    flex_touch_set_poll_hook(poll_hook);
    badge_build();
}

void flex_kiosk_boot(void)
{
    // la vista del bloqueo nace visible encima de las apps: fuera, sin pasar por ella
    flex_shell_unlocked();
    flex_app_open(K.app, NULL);
}

static void kiosk_start(int app, int x, int y, int w, int h)
{
    if (!flex_auth_required() || app < 0 || app >= FLEX_APP_N) {
        return;   // sin clave no habria salida: no se activa
    }
    K = (flex_kiosk_t){true, app, x, y, w, h};
    save();   // ANTES de abrir: un corte ya arranca en la app
    badge_build();
    flex_app_open(app, NULL);   // apertura con la animacion normal
}

void flex_kiosk_exit_now(void)
{
    K = (flex_kiosk_t){false, -1, 0, 0, 0, 0};
    save();
    if (s_badge) {
        lv_obj_set_hidden(s_badge, true);
    }
    // a Inicio por el camino normal (la app se suspende, no queda viva delante)
    flex_shell_return_to_app();
    flex_sys_home();
    lv_display_trigger_activity(NULL);   // el autobloqueo cuenta desde ahora
}

// ---- pantalla "definir area excluida" (ST_KIOSKSET) ---------------------------------------
static struct {
    lv_obj_t *root, *rect, *rect_l;
    int app;
    int x0, y0, x1, y1;
    bool has, drag;
} S;

static void set_close(void)
{
    if (S.root) {
        lv_obj_delete(S.root);
        S.root = NULL;
    }
}

static void set_cancel(void)
{
    set_close();
    flex_shell_overlay_end();   // Inicio
}

static const flex_overlay_ops_t s_set_ops = {set_cancel, set_cancel, set_cancel};

bool flex_kiosk_set_active(void)
{
    return S.root != NULL;
}

static void cancel_async(void *arg)
{
    (void)arg;
    if (S.root) {
        set_cancel();
    }
}

static void start_async(void *arg)
{
    (void)arg;
    if (!S.root) {
        return;
    }
    int x = 0, y = 0, w = 0, h = 0;
    if (S.has) {
        flex_kiosk_drag_rect(S.x0, S.y0, S.x1, S.y1, &x, &y, &w, &h);
    }
    int app = S.app;
    set_close();
    flex_shell_overlay_end();
    kiosk_start(app, x, y, w, h);
}

static void btn_cb(lv_event_t *e)
{
    bool start = (bool)(intptr_t)lv_event_get_user_data(e);
    lv_async_call(start ? start_async : cancel_async, NULL);   // borra la pantalla: fuera del evento
}

static void rect_show(void)
{
    int x, y, w, h;
    flex_kiosk_drag_rect(S.x0, S.y0, S.x1, S.y1, &x, &y, &w, &h);
    lv_obj_set_hidden(S.rect, !S.has);
    if (!S.has) {
        return;
    }
    lv_obj_set_pos(S.rect, x, y);
    lv_obj_set_size(S.rect, w, h);
    char b[24];
    snprintf(b, sizeof(b), "%d x %d", w, h);
    lv_label_set_text(S.rect_l, b);
    flex_label_cap_center(S.rect_l, w / 2, h / 2 - 9);
}

static void drag_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (code == LV_EVENT_PRESSED) {
        // solo cuenta si EMPIEZA por encima de los botones: pulsarlos nunca dibuja
        S.drag = p.y < BTN_Y - 8;
        if (S.drag) {
            S.x0 = S.x1 = p.x;
            S.y0 = S.y1 = p.y;
            S.has = false;
            rect_show();
        }
    } else if (code == LV_EVENT_PRESSING && S.drag) {
        S.x1 = p.x;
        S.y1 = p.y;
        int x, y, w, h;
        S.has = flex_kiosk_drag_rect(S.x0, S.y0, S.x1, S.y1, &x, &y, &w, &h);
        rect_show();
    } else if (code == LV_EVENT_RELEASED) {
        S.drag = false;   // el rectangulo queda fijado
    }
}

// uiFontFit(s, 440, 2): tamano 2 si cabe, si no 1
static lv_obj_t *fit_label(lv_obj_t *p, const char *s, lv_color_t c, int32_t y)
{
    const lv_font_t *f = FLEX_FONT_S2;
    if (flex_text_width(s, f) > SCR_W - 40) {
        f = FLEX_FONT_S1;
    }
    lv_obj_t *l = flex_label(p, s, f, c);
    flex_label_cap_center(l, SCR_W / 2, y);
    return l;
}

void flex_kiosk_set_open(int app)
{
    if (S.root || !flex_auth_required() || app < 0 || app >= FLEX_APP_N) {
        return;
    }
    const flex_palette_t *t = flex_th();
    memset(&S, 0, sizeof(S));
    S.app = app;
    // Velo del color de pagina sobre el escritorio + tarjeta con el contenido
    S.root = flex_box(lv_layer_top());
    lv_obj_set_size(S.root, SCR_W, SCR_H);
    lv_obj_set_style_bg_opa(S.root, 208, 0);
    lv_obj_set_style_bg_color(S.root, t->page, 0);
    lv_obj_set_clickable(S.root, true);
    lv_obj_set_gesture_bubble(S.root, false);
    lv_obj_add_event_cb(S.root, drag_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(S.root, drag_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(S.root, drag_cb, LV_EVENT_RELEASED, NULL);

    lv_obj_t *card = flex_box(S.root);
    lv_obj_set_pos(card, 24, 40);
    lv_obj_set_size(card, SCR_W - 48, 350);
    lv_obj_set_style_radius(card, 24, 0);
    // SET_CARD_GLASS / SET_CARD_BG a235. El vidrio desenfoca el escritorio con el
    // mismo velo que tiene encima (si no, se verian los iconos nitidos a traves)
    flex_surface(card, FLEX_SURF_CARD, FLEX_BD_HOME);
    flex_surface_set_veil(card, t->page, 208);
    flex_surface_set_flat(card, t->surf, 235);
    lv_obj_set_clickable(card, false);

    lv_obj_t *l = flex_label(S.root, "Modo Kiosco", FLEX_FONT_S4, t->txt);
    flex_label_cap_center(l, SCR_W / 2, 64);
    fit_label(S.root, "Arrastra para excluir una zona del t\xC3\xA1" "ctil", t->txt2, 122);
    fit_label(S.root, "Si no arrastras, toda la pantalla queda activa", t->mute, 146);
    lv_obj_t *ic = flex_app_icon_create(S.root, app, 72, FLEX_BD_FLAT);
    lv_obj_set_pos(ic, SCR_W / 2 - 36, 196);
    lv_obj_set_clickable(ic, false);
    l = flex_label(S.root, flex_app_name(app), FLEX_FONT_S3, t->txt);
    flex_label_cap_center(l, SCR_W / 2, 282);
    fit_label(S.root, "Para salir: mant\xC3\xA9n pulsado el candado de la", t->txt2, 344);
    fit_label(S.root, "esquina y escribe tu clave del sistema", t->txt2, 366);

    // Zona excluida: rojo de estado ("aqui no se toca") en las dos apariencias
    S.rect = flex_box(S.root);
    lv_obj_set_style_radius(S.rect, 4, 0);
    lv_obj_set_style_bg_opa(S.rect, 95, 0);
    lv_obj_set_style_bg_color(S.rect, t->err, 0);
    lv_obj_set_style_border_width(S.rect, 1, 0);
    lv_obj_set_style_border_color(S.rect, t->err, 0);
    lv_obj_set_clickable(S.rect, false);
    S.rect_l = flex_label(S.rect, "", FLEX_FONT_S2, t->txt);
    lv_obj_set_hidden(S.rect, true);

    // "Cancelar": tarjeta del tema; "Iniciar": accion primaria
    for (int k = 0; k < 2; k++) {
        lv_obj_t *b = flex_box(S.root);
        lv_obj_set_pos(b, k ? SCR_W - 30 - BTN_W : 30, BTN_Y);
        lv_obj_set_size(b, BTN_W, BTN_H);
        lv_obj_set_style_radius(b, 20, 0);
        if (k) {
            lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(b, t->primary, 0);
        } else {
            flex_surface(b, FLEX_SURF_CARD, FLEX_BD_HOME);
            flex_surface_set_veil(b, t->page, 208);
            flex_surface_set_flat(b, t->surf, LV_OPA_COVER);
        }
        lv_obj_set_clickable(b, true);
        lv_obj_add_event_cb(b, btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)k);
        lv_obj_t *bl = flex_label(b, k ? "Iniciar" : "Cancelar", FLEX_FONT_S2, k ? t->on_acc : t->txt);
        flex_label_cap_center(bl, BTN_W / 2, BTN_H / 2 - 9);
    }
    flex_shell_overlay_begin(&s_set_ops);
    flex_navbar_set_ctx(FLEX_NAV_HIDDEN);
    flex_touch_drop_all();   // el dedo que eligio la fila no dibuja una zona
}
