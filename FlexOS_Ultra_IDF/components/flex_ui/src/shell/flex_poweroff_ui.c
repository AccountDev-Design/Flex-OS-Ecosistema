// Flex OS Ultra · "¿Apagar FlexOS?" y animacion final (docs/spec/01c §10;
// Arduino: FlexOS_Ultra_Power.h:658-997).
//
// Se entra desde el panel rapido (boton de la cabecera o control "Apagar"). Es
// una capa en lv_layer_top que se queda el tacto entero (estado POWEROFF: ni
// panel rapido, ni Centro, ni barra de navegacion). Cancelar vuelve a donde se
// estaba (Arduino volvia siempre a Inicio aunque se viniera de una app). Con
// "Apagado seguro" (flexos/poffpin) y clave puesta, la clave va antes; cancelarla
// vuelve al deslizador, nunca apaga. La animacion final no se puede interrumpir:
// fundido a negro, "Flex OS", retroiluminacion a 0, SLPIN y deep sleep
// (flex_power). El modelo del deslizador esta en flex_poweroff_model.c.
#include <stdio.h>
#include "flex_auth.h"
#include "flex_display.h"
#include "flex_frame.h"
#include "flex_glass.h"
#include "flex_poweroff.h"
#include "flex_qs_icons.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "flex_wallpaper.h"

#define SCR_W 480
#define SCR_H 800
#define TICK_MS 16

#define PANEL_X 28
#define PANEL_Y 232
#define PANEL_W 424
#define PANEL_H 424
#define PANEL_R 44
#define CAN_X   130
#define CAN_Y   560
#define CAN_W   220
#define CAN_H   72
#define KNOB_R  (FLEX_POFF_KNOB_D / 2)

// Fases de la animacion final (Power.h:681-688)
#define FADE_MS 520
#define TIN_MS  260
#define HOLD_MS 700
#define TOUT_MS 620
#define BL_MS   150
#define TXT_Y   (SCR_H / 2 - 26)

enum { PH_CONFIRM = 0, PH_FADE, PH_TIN, PH_HOLD, PH_TOUT, PH_BL, PH_DONE };

static struct {
    lv_obj_t *root, *trail, *hint, *knob, *knob_sh, *black, *txt;
    lv_timer_t *tick;
    int knob_x, target, grab;
    bool drag;
    int phase;
    uint32_t t0;
    uint8_t bl0;
    lv_image_dsc_t bd;   // vista del fondo desenfocado de flex_wallmgr (sin copia)
} P;

static bool pin_required(void)
{
    return flex_cfg_get_bool("poffpin", false) && flex_auth_required();   // poffPinRequired
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, int r)
{
    lv_obj_t *o = flex_box(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_clickable(o, false);
    return o;
}

static lv_obj_t *fill(lv_obj_t *parent, int x, int y, int w, int h, int r, lv_color_t c, lv_opa_t a)
{
    lv_obj_t *o = box(parent, x, y, w, h, r);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, a, 0);
    return o;
}

// uiWallSurface: vidrio sobre el fondo velado (o el color liso en Plano).
static lv_obj_t *wall_surface(lv_obj_t *parent, int x, int y, int w, int h, int r, lv_color_t tint)
{
    lv_obj_t *o = box(parent, x, y, w, h, r);
    flex_surface(o, FLEX_SURF_TINT, FLEX_BD_HOME);
    flex_surface_set_tint(o, tint);
    flex_surface_set_veil(o, flex_th()->scrim, 150);
    flex_surface_set_flat(o, tint, LV_OPA_COVER);
    return o;
}

static void outline(lv_obj_t *o)
{
    lv_obj_set_style_border_color(o, FLEX_ONWALL2, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_post(o, true, 0);
}

static void label_c(lv_obj_t *parent, const char *s, const lv_font_t *f, lv_color_t c, int cx, int y)
{
    lv_obj_t *l = flex_label(parent, s, f, c);
    flex_label_cap_center(l, cx, y);
}

// Fondo: el escritorio desenfocado (o TH_SCRIM liso si no hay) y el velo a150.
// El puntero del backdrop se pide al dibujar: flex_wallmgr puede rehacerlo o soltarlo.
static void bg_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t c;
    lv_obj_get_coords(lv_event_get_target_obj(e), &c);
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    const uint16_t *bd = flex_wallmgr_backdrop(FLEX_WALL_HOME);
    if (bd) {
        P.bd = (lv_image_dsc_t){0};
        P.bd.header.magic = LV_IMAGE_HEADER_MAGIC;
        P.bd.header.cf = LV_COLOR_FORMAT_RGB565;
        P.bd.header.w = SCR_W;
        P.bd.header.h = SCR_H;
        P.bd.header.stride = FLEX_WALL_W * 2;
        P.bd.data = (const uint8_t *)bd;
        P.bd.data_size = FLEX_WALL_W * 2 * SCR_H;
        lv_draw_image_dsc_t id;
        lv_draw_image_dsc_init(&id);
        id.src = &P.bd;
        id.image_area = c;
        lv_draw_image(layer, &id, &c);
    } else {
        rd.bg_color = flex_th()->scrim;
        rd.bg_opa = LV_OPA_COVER;
        lv_draw_rect(layer, &rd, &c);
    }
    rd.bg_color = flex_th()->scrim;
    rd.bg_opa = 150;
    lv_draw_rect(layer, &rd, &c);
}

// ---- deslizador ---------------------------------------------------------------------------
static void knob_place(void)
{
    int k = P.knob_x;
    int p = k * 100 / FLEX_POFF_RUN;
    int kx = FLEX_POFF_TRACK_X + FLEX_POFF_KNOB_PAD + k;
    int fw = k + FLEX_POFF_KNOB_D + 2 * FLEX_POFF_KNOB_PAD;
    fw = fw > FLEX_POFF_TRACK_W ? FLEX_POFF_TRACK_W : fw;
    lv_obj_set_hidden(P.trail, k <= 0);
    lv_obj_set_width(P.trail, fw);
    lv_obj_set_style_bg_opa(P.trail, (lv_opa_t)(40 + p * 130 / 100), 0);   // apagar: accion destructiva
    int a = 235 - p * 2;
    lv_obj_set_style_text_opa(P.hint, (lv_opa_t)(a > 0 ? a : 0), 0);
    lv_obj_set_pos(P.knob_sh, kx + 1, FLEX_POFF_TRACK_Y + FLEX_POFF_KNOB_PAD + 2);
    lv_obj_set_pos(P.knob, kx, FLEX_POFF_TRACK_Y + FLEX_POFF_KNOB_PAD);
}

static void destroy(void)
{
    if (P.tick) {
        lv_timer_delete(P.tick);
        P.tick = NULL;
    }
    if (P.root) {
        lv_obj_delete(P.root);
    }
    P.root = P.trail = P.hint = P.knob = P.knob_sh = P.black = P.txt = NULL;
    P.drag = false;
}

static void begin_anim(void);

static void after_pin_ok(void *ctx)
{
    (void)ctx;
    begin_anim();
}

static void after_pin_cancel(void *ctx)
{
    (void)ctx;
    flex_poweroff_open();   // al deslizador en reposo: nunca apaga ni va al escritorio
}

static void slider_cb(lv_event_t *e)
{
    if (P.phase != PH_CONFIRM) {
        return;   // la animacion final no se interrumpe
    }
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t pt;
    lv_indev_get_point(lv_indev_active(), &pt);
    if (code == LV_EVENT_PRESSED) {
        if (!P.drag && flex_poff_grab(P.knob_x, pt.x, pt.y)) {
            P.drag = true;
            P.grab = pt.x - (FLEX_POFF_TRACK_X + FLEX_POFF_KNOB_PAD + P.knob_x);
        }
    } else if (code == LV_EVENT_PRESSING) {
        if (P.drag) {
            P.knob_x = P.target = flex_poff_knob_at(pt.x, P.grab);   // 1:1 con el dedo
            knob_place();
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (!P.drag) {
            return;
        }
        P.drag = false;
        if (code == LV_EVENT_RELEASED && flex_poff_done(P.knob_x)) {
            P.knob_x = P.target = FLEX_POFF_RUN;
            knob_place();
            if (pin_required()) {
                // La clave va en la pantalla (debajo de esta capa): la capa se esconde
                lv_obj_set_hidden(P.root, true);
                flex_auth_req_t req = {.on_ok = after_pin_ok, .on_cancel = after_pin_cancel};
                if (flex_auth_verify(&req)) {
                    return;
                }
                lv_obj_set_hidden(P.root, false);   // sin clave al final: se apaga sin ella
            }
            begin_anim();
            return;
        }
        P.target = 0;   // no llego: vuelve solo
    }
}

static void cancel_cb(lv_event_t *e)
{
    (void)e;
    if (P.phase == PH_CONFIRM && !P.drag) {
        flex_poweroff_close_now();
    }
}

// ---- animacion final -------------------------------------------------------------------
static void tick_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t e = lv_tick_get() - P.t0;
    switch (P.phase) {
    case PH_CONFIRM:
        if (!P.drag && P.knob_x != P.target) {
            P.knob_x = flex_poff_spring_step(P.knob_x, P.target);
            knob_place();
        }
        return;
    case PH_FADE: {
        uint32_t c = e > FADE_MS ? FADE_MS : e;
        lv_obj_set_style_bg_opa(P.black, (lv_opa_t)(c * 255 / FADE_MS), 0);   // lineal
        if (e >= FADE_MS) {
            P.phase = PH_TIN;
            P.t0 = lv_tick_get();
        }
        return;
    }
    case PH_TIN:
    case PH_HOLD:
    case PH_TOUT: {
        uint32_t dur = P.phase == PH_TIN ? TIN_MS : P.phase == PH_HOLD ? HOLD_MS : TOUT_MS;
        uint32_t c = e > dur ? dur : e;
        lv_opa_t a = P.phase == PH_TIN ? (lv_opa_t)(c * 255 / TIN_MS)
                   : P.phase == PH_HOLD ? LV_OPA_COVER
                                        : (lv_opa_t)(255 - c * 255 / TOUT_MS);
        lv_obj_set_style_text_opa(P.txt, a, 0);
        if (e >= dur) {
            P.phase++;
            P.t0 = lv_tick_get();
            if (P.phase == PH_BL) {
                P.bl0 = flex_display_get_brightness();
            }
        }
        return;
    }
    case PH_BL: {
        uint32_t c = e > BL_MS ? BL_MS : e;
        flex_display_backlight_raw((uint8_t)(P.bl0 - P.bl0 * c / BL_MS));
        if (e >= BL_MS) {
            P.phase = PH_DONE;
            flex_display_backlight_raw(0);
            flex_display_sleep_in();
            flex_poweroff_deep_sleep();   // no retorna en el firmware
        }
        return;
    }
    default:
        return;
    }
}

static void begin_anim(void)
{
    if (!P.root) {
        flex_poweroff_open();   // tras la clave la capa ya estaba: por si acaso
    }
    lv_obj_set_hidden(P.root, false);
    flex_shell_poweroff_begin();
    P.drag = false;
    P.phase = PH_FADE;
    P.t0 = lv_tick_get();
    // Sobre lo que haya en pantalla (el deslizador al final, o la clave recien
    // aceptada): velo negro y el texto encima.
    P.black = fill(P.root, 0, 0, SCR_W, SCR_H, 0, lv_color_black(), LV_OPA_TRANSP);
    P.txt = flex_label(P.root, "Flex OS", FLEX_FONT_S5, FLEX_ONWALL);
    flex_label_cap_center(P.txt, SCR_W / 2, TXT_Y);
    lv_obj_set_style_text_opa(P.txt, LV_OPA_TRANSP, 0);
    lv_obj_move_foreground(P.root);
}

// ---- entrada / salida ------------------------------------------------------------------
void flex_poweroff_open(void)
{
    if (P.phase != PH_CONFIRM) {
        return;
    }
    destroy();
    flex_shell_poweroff_begin();
    const flex_palette_t *t = flex_th();
    P.knob_x = P.target = 0;
    P.root = flex_box(lv_layer_top());
    lv_obj_set_size(P.root, SCR_W, SCR_H);
    lv_obj_set_clickable(P.root, true);   // nada de lo de debajo recibe toques
    lv_obj_add_event_cb(P.root, slider_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(P.root, slider_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(P.root, slider_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(P.root, slider_cb, LV_EVENT_PRESS_LOST, NULL);

    lv_obj_add_event_cb(P.root, bg_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    wall_surface(P.root, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, PANEL_R, FLEX_WALLPANEL);
    label_c(P.root, "\xC2\xBF" "Apagar FlexOS?", FLEX_FONT_S3, FLEX_ONWALL, SCR_W / 2, 268);
    label_c(P.root, "El sistema entrar\xC3\xA1 en reposo profundo", FLEX_FONT_S1, FLEX_ONWALL2, SCR_W / 2, 306);

    // pista
    fill(P.root, FLEX_POFF_TRACK_X + 2, FLEX_POFF_TRACK_Y + 4, FLEX_POFF_TRACK_W, FLEX_POFF_TRACK_H,
         FLEX_POFF_TRACK_H / 2, t->shadow, 60);
    lv_obj_t *tr = wall_surface(P.root, FLEX_POFF_TRACK_X, FLEX_POFF_TRACK_Y, FLEX_POFF_TRACK_W, FLEX_POFF_TRACK_H,
                                FLEX_POFF_TRACK_H / 2, FLEX_WALLSURF);
    outline(tr);
    P.trail = fill(P.root, FLEX_POFF_TRACK_X, FLEX_POFF_TRACK_Y, FLEX_POFF_TRACK_W, FLEX_POFF_TRACK_H,
                   FLEX_POFF_TRACK_H / 2, t->danger, 40);
    P.hint = flex_label(P.root, "desliza para apagar", FLEX_FONT_S2, FLEX_ONWALL);
    flex_label_cap_center(P.hint, FLEX_POFF_TRACK_X + FLEX_POFF_TRACK_W / 2 + 18,
                          FLEX_POFF_TRACK_Y + FLEX_POFF_TRACK_H / 2 - 9);
    P.knob_sh = fill(P.root, 0, 0, FLEX_POFF_KNOB_D, FLEX_POFF_KNOB_D, KNOB_R, t->shadow, 70);
    P.knob = fill(P.root, 0, 0, FLEX_POFF_KNOB_D, FLEX_POFF_KNOB_D, KNOB_R, FLEX_ONWALL, LV_OPA_COVER);
    lv_obj_t *ic = flex_qi_create(P.knob, FLEX_QI_POWER, 30);
    lv_obj_set_style_text_color(ic, t->danger, 0);
    lv_obj_set_pos(ic, KNOB_R - (30 + 8) / 2, KNOB_R - (30 + 8) / 2);

    // Cancelar
    fill(P.root, CAN_X + 2, CAN_Y + 4, CAN_W, CAN_H, CAN_H / 2, t->shadow, 60);
    lv_obj_t *cb = wall_surface(P.root, CAN_X, CAN_Y, CAN_W, CAN_H, CAN_H / 2, FLEX_WALLSURF);
    outline(cb);
    lv_obj_set_clickable(cb, true);
    lv_obj_add_event_cb(cb, cancel_cb, LV_EVENT_CLICKED, NULL);
    label_c(P.root, "Cancelar", FLEX_FONT_S2, FLEX_ONWALL, SCR_W / 2, CAN_Y + CAN_H / 2 - 9);

    knob_place();
    P.tick = lv_timer_create(tick_cb, TICK_MS, NULL);
    lv_obj_move_foreground(P.root);
}

void flex_poweroff_close_now(void)
{
    if (P.phase != PH_CONFIRM || !P.root) {
        return;   // la animacion final ya no tiene vuelta
    }
    destroy();
    flex_shell_poweroff_end();
}

bool flex_poweroff_active(void)
{
    return P.root != NULL;
}

bool flex_poweroff_running(void)
{
    return P.phase != PH_CONFIRM;
}

int flex_poweroff_knob(void)
{
    return P.knob_x;
}

#ifdef FLEX_SIM
// Simulador: tras "dormir" (el stub vuelve) la sesion sigue con otras escenas.
void flex_poweroff_sim_reset(void)
{
    destroy();
    P.phase = PH_CONFIRM;
    flex_shell_poweroff_end();
}
#endif
