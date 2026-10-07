// Flex OS Ultra · interfaz (LVGL). La tarea "ui" (nucleo 1) es la UNICA que
// llama a LVGL: arranca la pantalla, crea la entrada tactil y atiende los
// timers de LVGL.
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Crea la tarea de UI. Requiere flex_i2c_init() y flex_touch_start() antes
// si se quiere tactil (sin ellos la UI arranca igual, sin entrada).
esp_err_t flex_ui_start(void);

#ifdef __cplusplus
}
#endif
