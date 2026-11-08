// Flex OS Ultra · servicio del Cronometro (docs/spec/03 §2; Arduino: FlexOS_Ultra_AppChrono.h).
//
// El modelo (flex_chrono_model) decide; aqui se dibuja. Tres piezas:
//   · estado global + acciones: una sola fuente del tiempo, con `now` = lv_tick_get().
//   · capsula de la barra de estado: la aloja flex_frame.c (flex_statusbar_enable_crono); aqui se decide si hay,
//     donde y con que texto, y se mantiene al dia SIN repintar por cuadro (cronoCapsuleTick).
//   · tarjeta expandida: overlay propio en lv_layer_top (no es una notificacion, no caduca sola), modal: el gancho de
//     tacto se queda con TODO mientras esta a la vista (cronoOverlayTouch).
#include "flex_crono.h"

#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_frame.h"
#include "flex_glass.h"
#include "flex_icons.h"
#include "flex_i18n.h"
#include "flex_kit.h"
#include "flex_shell.h"
#include "flex_theme.h"
#include "flex_touch_lvgl.h"

static flex_crono_t G;
static flex_crono_cap_t K;
static flex_crono_ov_t OV;
static int s_cap_x = 20;     // x de la capsula tal como la dejo la ultima barra rehecha
static bool s_bar_hidden;    // una app cubre la barra de estado
static lv_timer_t *s_tmr;
static bool s_init;

static struct {
    int phase;
    uint32_t t0, last;
    lv_obj_t *card, *ov, *hold, *icon, *glyph, *sep, *lbl_l, *lbl_r, *time, *lap;
    int time_fs;
    bool static_dirty;
} C;

// ---- estado y acciones ----------------------------------------------------------------------------------------------------
const flex_crono_t *flex_crono_svc(void)
{
    return &G;
}

uint32_t flex_crono_svc_now(void)
{
    return lv_tick_get();
}

uint32_t flex_crono_elapsed_ms(void)
{
    return flex_crono_elapsed(&G, lv_tick_get());
}

bool flex_crono_running(void)
{
    return G.st == FLEX_CRONO_RUN;
}

void flex_crono_state_text(char *out, size_t n)
{
    snprintf(out, n, "%s", G.st == FLEX_CRONO_RUN ? "En marcha" : G.st == FLEX_CRONO_PAUSE ? "En pausa" : "Detenido");
}

static void kick(void)
{
    if (s_tmr) {
        lv_timer_ready(s_tmr);   // la capsula aparece / desaparece / cambia en el siguiente cuadro
    }
}

void flex_crono_svc_left(void)
{
    flex_crono_action_left(&G, lv_tick_get());
    kick();
}

void flex_crono_svc_right(void)
{
    flex_crono_action_right(&G, lv_tick_get());
    kick();
}

void flex_crono_svc_toggle(void)
{
    if (G.st == FLEX_CRONO_RUN) {
        flex_crono_pause(&G, lv_tick_get());
    } else {
        flex_crono_start(&G, lv_tick_get());
    }
    kick();
}

bool flex_crono_svc_take(int what)
{
    bool *f = what == FLEX_CRONO_D_LAPS ? &G.laps_dirty : what == FLEX_CRONO_D_BTN ? &G.btn_dirty : &G.dial_dirty;
    bool v = *f;
    *f = false;
    return v;
}

void flex_crono_svc_hide_bar(bool hidden)
{
    s_bar_hidden = hidden;
    kick();
}

// ---- superficie de la barra de estado (cronoBarSurface) --------------------------------------------------------------------
static int surface(void)
{
    flex_crono_surface_in_t in;
    memset(&in, 0, sizeof(in));
    flex_shell_state_t st = flex_shell_state();
    in.suspended = flex_touch_arb()->suspended;
    in.kiosk = flex_kiosk_active();
    in.home = st == FLEX_SH_HOME;
    in.qs_closed = !flex_qs_is_open();
    in.edit_mode = flex_home_edit_active();
    in.app = st == FLEX_SH_APP;
    if (in.app) {
        const flex_app_def_t *d = flex_app_def(flex_shell_fg_app());
        in.app_custom_header = d && (d->flags & FLEX_APP_CUSTOM_HEADER);
        in.app_hides_status_bar = s_bar_hidden;
    }
    // Endurecimiento sobre Arduino: con la cortina o el Centro de notificaciones abiertos la barra no es la del escritorio
    // ni la de la app (la tapan): ni capsula ni toque (Arduino dejaba el toque vivo en el marco de app).
    if (flex_qs_is_open() || flex_notif_center_open()) {
        return 0;
    }
    return flex_crono_surface(&in);
}

static int cap_w_now(uint32_t el)
{
    return flex_crono_cap_w(flex_text_width(flex_crono_cap_template(el), FLEX_FONT_S2));
}

void flex_crono_svc_bar_view(int clock_w, flex_crono_bar_t *bar, int *cap_w, char *text, size_t n)
{
    uint32_t el = flex_crono_elapsed(&G, lv_tick_get());
    int cw = cap_w_now(el);
    bool want = flex_crono_active(&G) && surface() != 0;
    flex_crono_bar_layout(want, clock_w, cw, bar);
    if (cap_w) {
        *cap_w = cw;
    }
    if (want) {
        flex_crono_fmt(text, n, el, false);
        s_cap_x = bar->x;
    } else if (n) {
        text[0] = 0;
    }
}

bool flex_crono_cap_shown(lv_area_t *out, char *text, size_t n)
{
    return flex_statusbar_crono_shown(out, text, n);
}

// ---- abrir la app --------------------------------------------------------------------------------------------------------------
void flex_crono_svc_open_app(void)
{
    flex_crono_card_close_now();   // la tarjeta no puede sobrevivir a un cambio de pantalla
    flex_app_reloj_goto_tab(1);
    if (flex_shell_state() == FLEX_SH_APP && flex_shell_fg_app() == IC_RELOJ) {
        return;   // ya esta delante: solo cambia de pestana
    }
    // Este atajo NO se salta el candado de la app: misma via que el escritorio (flex_app_launch lo verifica)
    flex_app_launch(IC_RELOJ, NULL);
}

// ---- tarjeta expandida ---------------------------------------------------------------------------------------------------------------
bool flex_crono_card_active(void)
{
    return C.phase != FLEX_CC_HIDDEN;
}

int flex_crono_card_phase(void)
{
    return C.phase;
}

static const lv_font_t *big_font(int fs)
{
    return fs >= 6 ? FLEX_FONT_S6 : flex_ui_font(fs);
}

static int big_tw(const char *s, int fs, void *ctx)
{
    (void)ctx;
    return flex_text_width(s, big_font(fs));
}

static void card_free(void)
{
    if (C.card) {
        lv_obj_delete(C.card);
    }
    if (C.hold) {
        lv_obj_delete(C.hold);
    }
    C.card = C.ov = C.hold = C.icon = C.glyph = C.sep = C.lbl_l = C.lbl_r = C.time = C.lap = NULL;
}

static void content_opa(uint8_t a)
{
    lv_obj_t *all[] = {C.icon, C.sep, C.lbl_l, C.lbl_r, C.time, C.lap};
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        if (all[i]) {
            lv_obj_set_style_opa(all[i], a, 0);
        }
    }
    // El glifo es opaco (no toma alfa): entra solo cuando el cuadrado ya es casi solido
    if (C.glyph) {
        lv_obj_set_hidden(C.glyph, a <= 200);
    }
}

// Etiquetas y colores que dependen del estado (cronoCardStatic)
static void card_static(void)
{
    const flex_palette_t *t = flex_th();
    flex_crono_card_lay_t l;
    flex_crono_card_layout(&l);
    lv_label_set_text(C.lbl_l, flex_crono_label_left(&G) == FLEX_CRONO_LL_RESET ? flex_t(FLEX_S_CRN_BRESET) : flex_t(FLEX_S_CRN_BLAP));
    int lr = flex_crono_label_right(&G);
    lv_label_set_text(C.lbl_r, lr == FLEX_CRONO_LR_STOP ? flex_t(FLEX_S_CRN_BSTOP) : lr == FLEX_CRONO_LR_CONTINUE ? flex_t(FLEX_S_CONTINUE) : flex_t(FLEX_S_CRN_BSTART));
    lv_obj_set_style_text_color(C.lbl_l, G.st == FLEX_CRONO_IDLE ? t->disabled : t->txt, 0);
    lv_obj_set_style_text_color(C.lbl_r, G.st == FLEX_CRONO_RUN ? t->danger : flex_accent(), 0);
    lv_obj_set_style_bg_color(C.icon, flex_accent(), 0);
    (void)l;
}

// Textos que cambian por cuadro: tiempo grande con centesimas y "Vuelta N" (cronoCardDynText)
static void card_dyn(void)
{
    flex_crono_card_lay_t l;
    flex_crono_card_layout(&l);
    char b[16];
    flex_crono_fmt(b, sizeof(b), flex_crono_elapsed(&G, lv_tick_get()), true);
    int fs = flex_crono_fit_big(b, l.time_maxw, 7, big_tw, NULL);
    if (fs != C.time_fs) {
        C.time_fs = fs;
        lv_obj_set_style_text_font(C.time, big_font(fs), 0);
        lv_obj_set_width(C.time, l.time_maxw + 20);
        flex_label_cap_y(C.time, l.time_y);
    }
    if (strcmp(lv_label_get_text(C.time), b) != 0) {
        lv_label_set_text(C.time, b);
    }
    char lap[32];
    snprintf(lap, sizeof(lap), "%s %d", flex_t(FLEX_S_CRN_LAP), flex_crono_cur_lap_no(&G));
    if (strcmp(lv_label_get_text(C.lap), lap) != 0) {
        lv_label_set_text(C.lap, lap);
    }
}

// Un cuadro: p = 0 -> pildora, p = 1 -> tarjeta (p ya con el ease)
static void card_frame(float p)
{
    flex_crono_card_frame_t f;
    int cw = K.cap_w_drawn > 0 ? K.cap_w_drawn : cap_w_now(flex_crono_elapsed(&G, lv_tick_get()));
    flex_crono_card_frame(p, s_cap_x, cw, &f);
    if (p >= 1.0f) {
        f.x = flex_crono_card_x_rest();   // Arduino cae un pixel corrido en el ultimo cuadro (ver el modelo)
    }
    lv_obj_set_pos(C.card, f.x, f.y);
    lv_obj_set_size(C.card, f.w, f.h);
    lv_obj_set_style_radius(C.card, f.r, 0);
    // El morfeo de color desde el acento de la capsula hasta la superficie: funcion de p
    lv_obj_set_size(C.ov, f.w, f.h);
    lv_obj_set_style_radius(C.ov, f.r, 0);
    lv_obj_set_style_bg_opa(C.ov, f.accent_a, 0);
    lv_obj_set_hidden(C.ov, f.accent_a == 0);
    content_opa(f.content_a);
    lv_obj_invalidate(C.card);
}

static lv_obj_t *card_label(const char *s, const lv_font_t *f, lv_color_t col, int cx, int y, bool centered)
{
    lv_obj_t *l = flex_label(C.hold, s, f, col);
    lv_obj_set_clickable(l, false);
    if (centered) {
        flex_label_cap_center(l, cx, y);
    } else {
        lv_obj_set_x(l, cx);
        flex_label_cap_y(l, y);
    }
    return l;
}

static void card_build(void)
{
    const flex_palette_t *t = flex_th();
    flex_crono_card_lay_t l;
    flex_crono_card_layout(&l);
    lv_obj_t *top = lv_layer_top();
    C.card = flex_box(top);
    lv_obj_set_clickable(C.card, false);
    // Sobre el escritorio el vidrio muestra el fondo ya desenfocado; sobre una app, el color liso de la ventana
    flex_surface(C.card, FLEX_SURF_ELEVATED, flex_shell_state() == FLEX_SH_HOME ? FLEX_BD_HOME : FLEX_BD_FLAT);
    // Misma superficie en los dos estados y en la transicion (uiSurfaceA: 240 plano). Con vidrio Arduino usa 200 sobre la banda
    // desenfocada ENTERA; LVGL solo desenfoca el fondo (no los widgets ni la app de debajo), asi que el vidrio va opaco para que
    // lo de debajo no reste legibilidad al tiempo (diferencia documentada).
    flex_surface_set_opa(C.card, flex_look()->glass ? 255 : 240);
    C.ov = flex_box(C.card);
    lv_obj_set_pos(C.ov, 0, 0);
    lv_obj_set_clickable(C.ov, false);
    lv_obj_set_style_bg_color(C.ov, flex_accent(), 0);
    lv_obj_set_style_bg_opa(C.ov, LV_OPA_COVER, 0);
    // El contenido va en coordenadas de pantalla (como en Arduino) y entra en la segunda mitad de la animacion
    C.hold = flex_box(top);
    lv_obj_set_pos(C.hold, 0, 0);
    lv_obj_set_size(C.hold, 480, FLEX_CRONO_BAND_B);
    lv_obj_set_clickable(C.hold, false);
    C.icon = flex_box(C.hold);
    lv_obj_set_pos(C.icon, l.icon_x, l.icon_y);
    lv_obj_set_size(C.icon, l.icon_s, l.icon_s);
    lv_obj_set_style_radius(C.icon, l.icon_r, 0);
    lv_obj_set_style_bg_color(C.icon, flex_accent(), 0);
    lv_obj_set_style_bg_opa(C.icon, LV_OPA_COVER, 0);
    lv_obj_set_clickable(C.icon, false);
    C.glyph = flex_glyph_create(C.hold, FLEX_GLYPH_STOPWATCH, l.icon_s, l.icon_s);
    flex_glyph_set(C.glyph, FLEX_GLYPH_STOPWATCH, l.glyph_r | (22 << 8));   // cronoGlyph(cx, cy, is / 4, ONACC, 2.2)
    lv_obj_set_pos(C.glyph, l.glyph_cx - l.icon_s / 2, l.glyph_cy - l.icon_s / 2);
    lv_obj_set_style_text_color(C.glyph, t->on_acc, 0);
    C.sep = flex_box(C.hold);
    lv_obj_set_pos(C.sep, l.sep_x, l.sep_y);
    lv_obj_set_size(C.sep, l.sep_w, l.sep_h);
    lv_obj_set_style_radius(C.sep, 1, 0);
    lv_obj_set_style_bg_color(C.sep, t->divider, 0);
    lv_obj_set_style_bg_opa(C.sep, LV_OPA_COVER, 0);
    lv_obj_set_clickable(C.sep, false);
    C.lbl_l = card_label("", FLEX_FONT_S3, t->txt, l.lbl_lx, l.lbl_y, true);
    C.lbl_r = card_label("", FLEX_FONT_S3, t->txt, l.lbl_rx, l.lbl_y, true);
    C.time = card_label("", big_font(7), t->txt, l.time_x, l.time_y, false);
    C.time_fs = -1;
    lv_label_set_long_mode(C.time, LV_LABEL_LONG_MODE_CLIP);
    C.lap = card_label("", FLEX_FONT_S2, t->txt2, l.lap_x, l.lap_y, false);
    card_static();
    card_dyn();
}

void flex_crono_card_open(void)
{
    if (C.phase != FLEX_CC_HIDDEN) {
        return;
    }
    card_build();
    C.phase = FLEX_CC_OPENING;
    C.t0 = lv_tick_get();
    C.last = C.t0;
    card_frame(0.0f);
    kick();
}

void flex_crono_card_close(void)
{
    if (C.phase != FLEX_CC_OPEN && C.phase != FLEX_CC_OPENING) {
        return;
    }
    C.phase = FLEX_CC_CLOSING;
    C.t0 = lv_tick_get();
}

void flex_crono_card_close_now(void)
{
    if (C.phase == FLEX_CC_HIDDEN) {
        return;
    }
    card_free();
    C.phase = FLEX_CC_HIDDEN;
    G.bar_dirty = true;   // la barra de debajo se rehace al volver
    kick();
}

const char *flex_crono_card_text(int which)
{
    lv_obj_t *o = which == 0 ? C.time : which == 1 ? C.lap : which == 2 ? C.lbl_l : C.lbl_r;
    return o ? lv_label_get_text(o) : "";
}

bool flex_crono_card_rect(lv_area_t *out)
{
    if (!C.card) {
        return false;
    }
    lv_obj_get_coords(C.card, out);
    return true;
}

// cronoCardTick
static void card_tick(void)
{
    if (C.phase == FLEX_CC_HIDDEN) {
        return;
    }
    flex_shell_state_t st = flex_shell_state();
    if (st != FLEX_SH_HOME && st != FLEX_SH_APP) {
        flex_crono_card_close_now();   // bloqueo, apagado, Modo seguro...: otra pantalla se adueno
        return;
    }
    uint32_t now = lv_tick_get();
    uint32_t e = (uint32_t)(now - C.t0);
    if (C.phase == FLEX_CC_OPENING) {
        float p = flex_crono_card_progress(e);
        if (p >= 1.0f) {
            card_frame(1.0f);
            C.phase = FLEX_CC_OPEN;
        } else {
            card_frame(flex_crono_ease(p));
        }
        card_dyn();
        return;
    }
    if (C.phase == FLEX_CC_CLOSING) {
        float p = flex_crono_card_progress(e);
        if (p >= 1.0f) {
            flex_crono_card_close_now();
        } else {
            card_frame(flex_crono_ease(1.0f - p));
            card_dyn();
        }
        return;
    }
    // CC_OPEN: ~30 fps y SOLO los textos que cambian
    if (!flex_crono_frame_due(now, C.last)) {
        return;
    }
    C.last = now;
    if (C.static_dirty) {
        card_static();
        C.static_dirty = false;
    }
    card_dyn();
}

// ---- tacto (cronoOverlayTouch) ----------------------------------------------------------------------------------------------------------
static bool crono_hook(const flex_arb_touch_t *t)
{
    flex_crono_touch_t in;
    memset(&in, 0, sizeof(in));
    in.down = t->down;
    in.tap = t->tap;
    in.pressed = t->pressed;
    in.released = t->released;
    in.swipe_up = t->swipe_up;
    in.swipe_left = t->swipe_left;
    in.swipe_right = t->swipe_right;
    in.swipe_down = t->swipe_down;
    in.x = t->x;
    in.y = t->y;
    in.start_x = t->start_x;
    in.start_y = t->start_y;
    in.down_ms = t->down_ms;
    flex_crono_world_t w;
    memset(&w, 0, sizeof(w));
    w.susp = flex_touch_arb()->suspended;
    w.card = C.phase;
    w.cap_on = K.cap_on;
    w.surface = K.cap_on || C.phase == FLEX_CC_HIDDEN ? surface() : 0;
    w.cap_x = s_cap_x;
    w.cap_w_drawn = K.cap_w_drawn;
    bool card_was = C.phase != FLEX_CC_HIDDEN;
    flex_crono_touch_out_t out;
    flex_crono_overlay_touch(&G, &OV, &w, &in, lv_tick_get(), &out);
    for (int i = 0; i < out.n; i++) {
        switch (out.act[i]) {
        case FLEX_CRONO_ACT_DROP: flex_crono_card_close_now(); break;
        case FLEX_CRONO_ACT_CLOSE: flex_crono_card_close(); break;
        case FLEX_CRONO_ACT_OPEN_APP: flex_crono_svc_open_app(); break;
        case FLEX_CRONO_ACT_CARD_OPEN: flex_crono_card_open(); break;
        case FLEX_CRONO_ACT_BG_INVAL: C.static_dirty = true; break;
        default: break;   // LEFT / RIGHT: el modelo ya cambio el estado
        }
    }
    if (out.n > 0) {
        kick();
    }
    // la tarjeta es modal: se queda con todo mientras esta a la vista; la capsula solo con el toque que consume
    return card_was || out.n > 0 || out.clr != 0;
}

// ---- cronoCapsuleTick + animacion de la tarjeta ---------------------------------------------------------------------------------------------
static void svc_tick(lv_timer_t *tm)
{
    (void)tm;
    flex_crono_svc_tick();
}

void flex_crono_svc_tick(void)
{
    uint32_t now = lv_tick_get();
    card_tick();
    uint32_t el = flex_crono_elapsed(&G, now);
    int cw = cap_w_now(el);
    int act = flex_crono_cap_tick(&G, &K, now, surface(), cw, true);
    if (act == FLEX_CRONO_CT_REBUILD) {
        // La capsula aparece, desaparece o cambia de ancho (al pasar de 1 h): la barra se rehace entera
        flex_statusbar_refresh_all();
        flex_crono_cap_apply_bar(&G, &K, now, flex_crono_active(&G), cw);
    } else if (act == FLEX_CRONO_CT_STAMP) {
        flex_statusbar_crono_update();   // solo la pildora, solo el texto
    }
    if (s_tmr) {
        lv_timer_set_period(s_tmr, C.phase != FLEX_CC_HIDDEN ? 16 : 100);
    }
}

static void svc_theme_cb(void *ctx)
{
    (void)ctx;
    flex_app_reloj_theme_changed();
}

void flex_crono_svc_init(void)
{
    // Antes que la isla de avisos (prio 0): la tarjeta y la capsula se quedan el toque. Ambos registros se repiten en cada arranque
    // del shell: el gancho no se duplica y la lista de oyentes del tema se vacia con flex_theme_init()
    flex_touch_add_sys_hook(crono_hook, -1);
    flex_theme_listen(svc_theme_cb, NULL);
    if (s_init) {
        return;
    }
    s_init = true;
    flex_crono_init(&G);
    flex_crono_cap_reset(&K);
    s_tmr = lv_timer_create(svc_tick, 100, NULL);
}

// Pruebas: el cronometro en un estado conocido (running: lleva elapsed_ms corriendo; si no, parado con ese tiempo, o inactivo si es 0)
void flex_crono_svc_test_set(uint32_t elapsed_ms, bool running)
{
    flex_crono_reset(&G);
    uint32_t now = lv_tick_get();
    if (running) {
        flex_crono_start(&G, now - elapsed_ms);
    } else if (elapsed_ms) {
        flex_crono_start(&G, now - elapsed_ms);
        flex_crono_pause(&G, now);
    }
    kick();
}

void flex_crono_svc_reset(void)
{
    flex_crono_card_close_now();
    flex_crono_init(&G);
    flex_crono_cap_reset(&K);
    memset(&OV, 0, sizeof(OV));
    s_cap_x = 20;
    s_bar_hidden = false;
    flex_statusbar_refresh_all();
}
