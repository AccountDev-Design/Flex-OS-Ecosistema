// Flex OS Ultra · puerto de entrada de LVGL para el GT911. Solo tarea de UI.
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Crea el indev (puntero) asociado a la pantalla. El primer dedo es el puntero
// de LVGL; el resto de dedos estan en flex_touch_get_frame() para la UI que
// los necesite (teclado, pantalla de prueba).
lv_indev_t *flex_touch_lvgl_create(lv_display_t *disp);

#ifdef __cplusplus
}
#endif
