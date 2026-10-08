// Flex OS Ultra · arbitraje del tactil ANTES de LVGL (docs/spec/01a §14).
//
// Logica pura (sin LVGL ni FreeRTOS), port de flexPollTouch/suspGestureUpdate
// de FlexOS_Ultra_Touch.h. Decide, cuadro a cuadro, que ve la interfaz:
//   1. zona excluida del kiosco: el punto se convierte en "sin dato";
//   2. apoyo / movimiento / suelta (frame de 0 dedos o 90 ms sin frames);
//   3. tragado del episodio heredado de la pantalla anterior (touchDropAll);
//   4. doble toque de 2 dedos (suspender) y de 1 dedo (despertar);
//   5. lo que se traga el gesto: suspendido, todo; despierto, los episodios
//      confirmados de dos dedos (salvo tecleando o si una app es duena de los
//      dos dedos).
// El resultado (dedo apoyado o no + punto) es lo que lee el indev de LVGL.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constantes de Arduino (Types.h:325-330, Touch.h:337-374)
#define FLEX_ARB_TAP_WINDOW_MS 450
#define FLEX_ARB_TAP_GAP_MS    45
#define FLEX_ARB_TAP_MAX_MS    600
#define FLEX_ARB_TAP_FRAMES    2
#define FLEX_ARB_SILENCE_MS    90
// Candado de salida del kiosco (Touch.h:72-75): siempre gana a la zona excluida
#define FLEX_ARB_KIOSK_BADGE_X 448
#define FLEX_ARB_KIOSK_BADGE_Y 44
#define FLEX_ARB_KIOSK_BADGE_S 24
#define FLEX_ARB_KIOSK_PAD     14

enum {
    FLEX_ARB_EV_NONE = 0,
    FLEX_ARB_EV_SUSPEND,      // doble toque de dos dedos (pantalla encendida, sin veto)
    FLEX_ARB_EV_WAKE,         // doble toque de un dedo con la pantalla suspendida
};

// Lo que tiene que ver la interfaz (el "Touch T" de Arduino)
typedef struct {
    bool down, pressed, released, tap, moved;
    bool swipe_up, swipe_down, swipe_left, swipe_right;
    int16_t x, y, start_x, start_y, dx, dy;
    uint32_t down_ms, last_ms;
} flex_arb_touch_t;

typedef struct {
    // ---- entradas que mantiene la UI ----
    bool kiosk_filter;        // kiosco activo Y una app en primer plano
    int16_t kx, ky, kw, kh;   // zona excluida (kw/kh <= 0: ninguna)
    bool kiosk_on;            // veta el gesto de suspension
    bool typing;              // dedos sobre el teclado (veta y no traga)
    bool owns_two;            // una app es duena de los dos dedos (pellizco)
    bool pinch_used;          // la app consumio este episodio (se borra al empezar cada uno)
    bool suspended;           // pantalla suspendida (gSuspOn)

    // ---- estado ----
    flex_arb_touch_t t;
    bool swallow;             // touchDropAll armado
    uint32_t swallow_seen_ms;
    uint8_t fingers;
    uint32_t fingers_ms;
    bool ep_act, ep_had2, ep_had3;
    uint32_t ep_t0;
    uint8_t ep_run2;
    uint32_t tap2_ms, tap1_ms;
    bool susp_swallow;
} flex_arb_t;

void flex_arb_init(flex_arb_t *a);
// Anula el episodio en curso hasta que el dedo se levante de verdad.
void flex_arb_drop_all(flex_arb_t *a, uint32_t now_ms);

// Una vuelta. ev: 1 = cuadro nuevo con dedo en (x, y), 0 = cuadro nuevo sin
// dedos, -1 = sin cuadro nuevo. fingers = dedos del cuadro nuevo (si ev != -1).
// Devuelve FLEX_ARB_EV_*; a->t queda con lo que debe ver la interfaz.
int flex_arb_poll(flex_arb_t *a, int ev, int x, int y, int fingers, uint32_t now_ms);

bool flex_arb_kiosk_in_exit(int x, int y);

#ifdef __cplusplus
}
#endif
