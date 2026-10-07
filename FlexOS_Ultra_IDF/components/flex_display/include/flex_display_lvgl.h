// Flex OS Ultra · puerto de pantalla de LVGL. Solo tarea de UI.
//
// LVGL dibuja en modo DIRECT sobre los dos framebuffers del panel DPI. Al
// terminar un cuadro, el framebuffer dibujado pasa a ser el que se muestra
// desde el siguiente fin de cuadro (sin copia) y LVGL no vuelve a tocar el otro
// hasta que el DMA ha terminado de leerlo: sin tearing.
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Requiere lv_init() y flex_display_init(). Devuelve la pantalla por defecto.
lv_display_t *flex_display_lvgl_create(void);

#ifdef __cplusplus
}
#endif
