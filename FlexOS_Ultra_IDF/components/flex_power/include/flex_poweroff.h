// Flex OS Ultra · apagado completo (deep sleep real) y filtro de encendido.
//
// docs/spec/01c §10 (Arduino: FlexOS_Ultra_Power.h:892-1083). La pantalla de
// confirmacion y la animacion final estan en flex_ui; aqui va lo que no es UI:
//   · el modelo del deslizador y del filtro de 3 s (C puro, pruebas en host),
//   · dormir (reset del GT911 retenido en alto, despertar por ext1 o temporizador),
//   · el filtro de encendido, en app_main ANTES de encender la pantalla.
//
// SALIDA DE EMERGENCIA: solo un despertar de deep sleep pasa por el filtro. Cortar
// la alimentacion (o el boton de reset) es un arranque normal SIEMPRE: un filtro
// equivocado nunca puede dejar la placa sin arrancar.
//
// NO PROBADO EN HARDWARE REAL.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- deslizador "desliza para apagar" (poffTick, Power.h:834-878) -------------------
#define FLEX_POFF_TRACK_X   40
#define FLEX_POFF_TRACK_Y   352
#define FLEX_POFF_TRACK_W   400
#define FLEX_POFF_TRACK_H   96
#define FLEX_POFF_KNOB_PAD  6
#define FLEX_POFF_KNOB_D    84
#define FLEX_POFF_RUN       (FLEX_POFF_TRACK_W - 2 * FLEX_POFF_KNOB_PAD - FLEX_POFF_KNOB_D)   // 304
#define FLEX_POFF_DONE_PCT  92

// El apoyo agarra el pomo (todo el alto de la pista y 24 px a cada lado del pomo).
bool flex_poff_grab(int knob, int x, int y);
// Pomo que sigue al dedo 1:1 conservando el desfase del agarre (grab = x - kx).
int  flex_poff_knob_at(int x, int grab);
// Soltar aqui apaga (>= 92 % del recorrido).
bool flex_poff_done(int knob);
// Un paso de la vuelta a 0 (acercamiento proporcional con enganche final).
int  flex_poff_spring_step(int knob, int target);

// ---- filtro de encendido (poffWakeGate, Power.h:1056-1083) --------------------------
#define FLEX_WG_HOLD_MS      3000   // dedo sostenido para arrancar
#define FLEX_WG_WINDOW_MS    4200   // ventana total antes de volver a dormir
#define FLEX_WG_NOFINGER_MS  300    // sin dedo este tiempo seguido: a dormir YA
#define FLEX_WG_STALE_MS     120    // un cuadro con dedos mas viejo ya no cuenta
#define FLEX_WG_POLL_MS      10

typedef enum { FLEX_WG_WAIT = 0, FLEX_WG_BOOT, FLEX_WG_SLEEP } flex_wg_result_t;

typedef struct {
    uint32_t t0;
    uint32_t last_finger;   // ultima vez con dedo (o el inicio)
    uint32_t held_from;
    bool held;
} flex_wakegate_t;

void flex_wakegate_init(flex_wakegate_t *g, uint32_t now_ms);
// fingers: dedos del ultimo cuadro valido (0 si es de hace mas de FLEX_WG_STALE_MS).
// Arduino esperaba siempre la ventana entera sin dedo (el chip despierto ~91 % del
// tiempo con el despertar por temporizador): aqui, sin dedo FLEX_WG_NOFINGER_MS
// seguidos, se vuelve a dormir enseguida. Levantar el dedo menos que eso solo
// reinicia la cuenta de los 3 s.
flex_wg_result_t flex_wakegate_step(flex_wakegate_t *g, uint32_t now_ms, int fingers);

// ---- hardware (solo firmware) -------------------------------------------------------
// true si este arranque viene de un deep sleep.
bool flex_poweroff_woke_from_sleep(void);
// Filtro de encendido. Llamar al principio de app_main, antes de la pantalla.
// Vuelve si hay que arrancar (no se viene de deep sleep, no hay tactil, o el dedo
// se sostuvo 3 s); si no, vuelve a dormir y no retorna.
void flex_poweroff_wake_gate(void);
// Tras montar el almacenamiento: lee y borra flexos/cleanoff (solo informa).
void flex_poweroff_boot_note(void);
// Guarda lo que hay que guardar (flexos/cleanoff, flexos/bright) de forma
// sincronica, retiene el reset del tactil, arma el despertar y duerme. No retorna.
// La pantalla ya debe estar apagada (retroiluminacion a 0 y SLPIN).
// (En el simulador de la interfaz se registra y vuelve: FLEX_SIM.)
#ifdef FLEX_SIM
void flex_poweroff_deep_sleep(void);
#else
void flex_poweroff_deep_sleep(void) __attribute__((noreturn));
#endif

#ifdef __cplusplus
}
#endif
