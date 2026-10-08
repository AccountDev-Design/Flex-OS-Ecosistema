// Flex OS Ultra · puerto de entrada de LVGL para el GT911. Solo tarea de UI.
#pragma once

#include "flex_touch_arb.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Crea el indev (puntero) asociado a la pantalla. El primer dedo es el puntero
// de LVGL; el resto de dedos estan en flex_touch_get_frame() para la UI que
// los necesite (teclado, pantalla de prueba).
lv_indev_t *flex_touch_lvgl_create(lv_display_t *disp);

// ---- arbitraje previo a LVGL (flex_touch_arb.h) ----------------------------------
// Estado del arbitraje: la UI pone aqui sus entradas (suspendida, kiosco,
// tecleando, duena de dos dedos).
flex_arb_t *flex_touch_arb(void);
// Gestos del sistema (FLEX_ARB_EV_SUSPEND / FLEX_ARB_EV_WAKE), en la tarea de UI.
typedef void (*flex_touch_gesture_cb_t)(int ev);
void flex_touch_set_gesture_cb(flex_touch_gesture_cb_t cb);
// touchDropAll: al cambiar de pantalla, el dedo que sigue apoyado no toca nada
// de la pantalla nueva hasta que se levante de verdad.
void flex_touch_drop_all(void);
// Una lectura del indev pasada por el arbitraje (la usan el puerto y el
// simulador). ev: 1 cuadro nuevo con dedo, 0 cuadro nuevo sin dedos, -1 nada nuevo.
void flex_touch_feed(lv_indev_t *indev, lv_indev_data_t *data, int ev, int x, int y, int fingers);

#ifdef __cplusplus
}
#endif
