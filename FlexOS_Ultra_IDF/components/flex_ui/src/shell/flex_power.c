// Flex OS Ultra · energia del shell: suspension de pantalla por doble toque y
// bloqueo automatico por inactividad (docs/spec/01a §5.11, §5.12 y §14.5;
// Arduino: FlexOS_Ultra_Touch.h:190-230, Power.h:633-660, Lock.h:338-396).
//
// La suspension NO duerme el chip ni cambia el estado del shell: funde el
// retroiluminado a 0 (6 puntos cada 10 ms), manda DISPOFF y deja todo vivo.
// Despertar con clave compone el bloqueo A OSCURAS antes de encender y, al
// acertar, vuelve a la app donde estaba. "A oscuras" de verdad: el panel no se
// enciende hasta que el cuadro con el bloqueo ha salido entero por el DPI (dos
// fines de cuadro despues de dibujarlo); encender en el mismo instante dejaria
// ver uno o dos cuadros de la app que habia.
#include <stdbool.h>
#include "flex_app.h"
#include "flex_auth.h"
#include "flex_display.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_touch_lvgl.h"
#include "lvgl.h"

#define FADE_STEP_MS 10
#define FADE_STEP    6
#define AUTOLOCK_DEF_MS 60000u
#define AUTOLOCK_TICK_MS 250
#define CLOSE_WAIT_MS 200   // el cierre de la app (190 ms) termina antes de que caiga el bloqueo
#define WAKE_VSYNCS 2       // 1: el DMA pasa al framebuffer nuevo; 2: ese cuadro ya salio entero
#define WAKE_MAX_MS 250     // red de seguridad: sin fines de cuadro (driver parado) se enciende igual
#define WAKE_POLL_MS 4

static const uint32_t AUTOLOCK_OPTS[6] = {30000u, 60000u, 300000u, 600000u, 1800000u, 0u};

static struct {
    bool on;           // gSuspOn: desde el gesto hasta que termina el fundido de vuelta
    bool dark;         // retroiluminado en 0 y DISPOFF enviado
    uint8_t bright;    // brillo del usuario al suspender
    int cur;           // lo que hay escrito en el PWM ahora (0..100, gBlPct)
    int fade_to;
    lv_timer_t *timer; // fundido en curso
} S;

static lv_timer_t *s_autolock;
static lv_timer_t *s_close_wait;
static struct {
    lv_timer_t *timer;   // encendido pendiente
    bool drawn;
    uint32_t vs;         // fines de cuadro al dibujar
    uint32_t t0;
} W;

bool flex_power_suspended(void)
{
    return S.on;
}

static void fade_cb(lv_timer_t *t)
{
    (void)t;
    if (S.cur > S.fade_to) {
        S.cur -= FADE_STEP;
        if (S.cur < S.fade_to) {
            S.cur = S.fade_to;
        }
    } else if (S.cur < S.fade_to) {
        S.cur += FADE_STEP;
        if (S.cur > S.fade_to) {
            S.cur = S.fade_to;
        }
    }
    flex_display_backlight_raw((uint8_t)S.cur);
    if (S.cur != S.fade_to) {
        return;
    }
    lv_timer_delete(S.timer);
    S.timer = NULL;
    if (S.fade_to == 0) {
        if (S.on && !S.dark) {
            S.dark = true;
            flex_display_panel_on(false);   // DCS despues del negro
        }
    } else {
        flex_display_backlight_enable(true);   // brillo exacto del usuario, con su curva
        S.on = false;                          // a partir de aqui el tactil vuelve a fluir
        flex_touch_arb()->suspended = false;
        lv_display_trigger_activity(lv_display_get_default());
    }
}

// Sigue desde el valor REAL del PWM (S.cur): al despertar esta a 0 aunque el
// brillo elegido siga siendo, por ejemplo, 80 (si no, se encenderia de golpe);
// a mitad de un fundido, sigue desde donde iba.
static void fade_to(int to)
{
    S.fade_to = to;
    if (!S.timer) {
        S.timer = lv_timer_create(fade_cb, FADE_STEP_MS, NULL);
    }
}

void flex_power_suspend(void)
{
    if (S.on) {
        return;
    }
    flex_qs_close_now();   // qsForceClose: la cortina no sobrevive a apagar la pantalla
    S.bright = flex_display_get_brightness();
    if (!S.timer) {
        S.cur = S.bright;
    }
    S.on = true;
    S.dark = false;
    flex_touch_arb()->suspended = true;
    fade_to(0);
}

// suspWakeLockScreen (Power.h:633-660)
static void wake_lock_screen(void)
{
    if (!flex_auth_required()) {
        return;   // sin clave se despierta donde estaba
    }
    flex_shell_state_t st = flex_shell_state();
    if (st == FLEX_SH_LOCK) {
        flex_lock_reset();   // ya bloqueado: entero, nunca a medias de una caida
        return;
    }
    if (st == FLEX_SH_AUTH && flex_auth_from_lock()) {
        return;   // tecleando la clave del bloqueo
    }
    // Un candado de app o la clave de Ajustes a medias NO cuentan como bloqueo
    // (Arduino si los contaba: "atras" dejaba el escritorio sin pedir la clave).
    int app = flex_app_current();   // la app NO se cierra: se vuelve a ella al acertar
    flex_shell_lock();
    flex_lock_set_return_app(app);
}

static void light_up(void)
{
    if (S.dark) {
        flex_display_panel_on(true);   // DISPON antes de subir el retroiluminado
        S.dark = false;
    }
    fade_to(S.bright);
}

static void wake_cb(lv_timer_t *t)
{
    if (!W.drawn) {
        // Ya fuera de la lectura del tactil: lo que cambio (el bloqueo) se dibuja
        // ahora mismo, sin esperar al siguiente refresco.
        lv_refr_now(NULL);
        W.drawn = true;
        W.vs = flex_display_vsync_count();
        W.t0 = lv_tick_get();   // la red de seguridad cuenta desde el dibujo, no desde el gesto
        return;
    }
    if ((uint32_t)(flex_display_vsync_count() - W.vs) < WAKE_VSYNCS && lv_tick_elaps(W.t0) < WAKE_MAX_MS) {
        return;
    }
    lv_timer_delete(t);
    W.timer = NULL;
    light_up();
}

void flex_power_wake(void)
{
    if (!S.on || W.timer) {
        return;
    }
    wake_lock_screen();   // a oscuras: lo de antes no llega a verse
    if (!S.dark) {
        light_up();   // aun no se habia apagado (a mitad del fundido): se ve el cambio, sin mas
        return;
    }
    W.drawn = false;
    W.t0 = lv_tick_get();
    W.timer = lv_timer_create(wake_cb, WAKE_POLL_MS, NULL);
}

static void gesture_cb(int ev)
{
    if (ev == FLEX_ARB_EV_SUSPEND) {
        if (flex_poweroff_running() || flex_factory_active()) {
            return;   // apagando o en el asistente de restablecimiento (§9.6: alli el tactil se perdia)
        }
        flex_power_suspend();
    } else if (ev == FLEX_ARB_EV_WAKE) {
        flex_power_wake();
    }
}

// ---- bloqueo por inactividad (autoLockTick, Lock.h:357-396) -----------------------
uint32_t flex_power_autolock_ms(void)
{
    uint32_t v = (uint32_t)flex_cfg_get_i32("autolockms", (int32_t)AUTOLOCK_DEF_MS);
    for (int i = 0; i < 6; i++) {
        if (AUTOLOCK_OPTS[i] == v) {
            return v;
        }
    }
    return AUTOLOCK_DEF_MS;   // un valor fuera de la lista (p. ej. los 2 min antiguos) se normaliza
}

static void drop_lock(lv_timer_t *t)
{
    (void)t;
    s_close_wait = NULL;
    flex_shell_lock();
    flex_lock_drop_in();   // el bloqueo baja desde arriba (200 ms ease-out)
    lv_display_trigger_activity(lv_display_get_default());
}

static void autolock_now(void)
{
    if (flex_shell_state() == FLEX_SH_APP) {
        flex_app_close();   // su animacion de cierre; despues cae el bloqueo
        s_close_wait = lv_timer_create(drop_lock, CLOSE_WAIT_MS, NULL);
        lv_timer_set_repeat_count(s_close_wait, 1);
        return;
    }
    drop_lock(NULL);
}

// Lo que impide bloquear ahora mismo (kiosco, OTA, cortina abierta...): los
// modulos que lo necesiten lo aportan aqui cuando existan.
__attribute__((weak)) bool flex_power_autolock_vetoed(void)
{
    return false;
}

static void autolock_cb(lv_timer_t *t)
{
    (void)t;
    if (S.on || s_close_wait) {
        return;   // suspendido: del bloqueo se encarga el despertar
    }
    if (flex_power_autolock_vetoed()) {
        lv_display_trigger_activity(lv_display_get_default());
        return;
    }
    if (flex_qs_is_open()) {
        return;   // cortina abierta: no bloquear a media interaccion (Lock.h:392)
    }
    uint32_t win = flex_power_autolock_ms();
    if (!win) {
        return;   // "Nunca"
    }
    flex_shell_state_t st = flex_shell_state();
    bool app_auth = st == FLEX_SH_AUTH && !flex_auth_from_lock();   // candado de app: si aplica
    if (st != FLEX_SH_HOME && st != FLEX_SH_APP && st != FLEX_SH_OVERLAY && !app_auth) {
        return;   // bloqueo, clave del bloqueo: no aplica
    }
    if (lv_display_get_inactive_time(lv_display_get_default()) < win) {
        return;
    }
    autolock_now();   // se aplica aunque no haya clave (cae en el de deslizar)
}

void flex_power_init(void)
{
    flex_touch_set_gesture_cb(gesture_cb);
    if (!s_autolock) {
        s_autolock = lv_timer_create(autolock_cb, AUTOLOCK_TICK_MS, NULL);
    }
}
