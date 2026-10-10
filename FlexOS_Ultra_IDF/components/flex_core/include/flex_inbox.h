// Flex OS Ultra · buzon de la tarea de UI.
//
// Cualquier tarea entrega trabajo a la UI sin tocar LVGL: publica una funcion y
// su argumento, y la tarea de UI la ejecuta en su siguiente vuelta (alli si se
// puede usar LVGL). Publicar nunca bloquea y despierta a la UI al momento.
//
// Uso tipico: un servicio termina una operacion y entrega el resultado a la
// pantalla que lo pidio (la funcion la escribe la UI; el servicio solo la llama
// por puntero, sin ver LVGL).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*flex_inbox_fn_t)(void *arg);

// La crea la tarea de UI al arrancar, con su propio handle para despertarla.
esp_err_t flex_inbox_init(size_t depth, TaskHandle_t ui_task);

// Desde cualquier tarea (no ISR). false si la cola esta llena: el llamante
// decide (normalmente liberar arg y contar el descarte).
bool flex_inbox_post(flex_inbox_fn_t fn, void *arg);

// Solo la tarea de UI. Ejecuta hasta max entregas pendientes.
size_t flex_inbox_drain(size_t max);

uint32_t flex_inbox_dropped(void);
// true si quien llama es la tarea de UI (puede leer el estado de la interfaz ya).
bool flex_inbox_in_ui(void);

#ifdef __cplusplus
}
#endif
