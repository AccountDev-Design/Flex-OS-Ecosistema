// Flex OS Ultra · servicio del puerto de pantalla para el bucle de la UI.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Duerme hasta max_ms en la notificacion de la tarea de UI (la dan el fin de
// cuadro y el buzon). Si habia un cambio de framebuffer pendiente y ya se
// estreno, lo completa (asi una pantalla quieta tambien termina su ultimo
// cuadro y enciende el retroiluminado).
void flex_display_lvgl_idle(uint32_t max_ms);

// Tiempo que la tarea de UI ha pasado bloqueada esperando fin de cuadro dentro
// de LVGL desde la ultima llamada (para descontarlo del uso de CPU).
int64_t flex_display_lvgl_take_wait_us(void);

#ifdef __cplusplus
}
#endif
