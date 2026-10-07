// Flex OS Ultra · servicio del puerto de pantalla para el bucle de la UI.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Duerme hasta max_ms. Si hay un cambio de framebuffer pendiente, duerme en el
// aviso de fin de cuadro y lo completa en cuanto llega (asi una pantalla quieta
// tambien termina su ultimo cuadro y enciende el retroiluminado).
void flex_display_lvgl_idle(uint32_t max_ms);

// Tiempo que la tarea de UI ha pasado bloqueada esperando fin de cuadro dentro
// de LVGL desde la ultima llamada (para descontarlo del uso de CPU).
int64_t flex_display_lvgl_take_wait_us(void);

#ifdef __cplusplus
}
#endif
