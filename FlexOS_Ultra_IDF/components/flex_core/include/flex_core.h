// Flex OS Ultra · nucleo comun: reparto de tareas y datos de arranque.
#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

// Reparto de tareas (plan, seccion 5.3). El nucleo 1 es de la interfaz y el
// video; el nucleo 0, de la radio, la red, el almacenamiento y los sensores.
// Las pilas son el punto de partida: se ajustan midiendo el high-water mark.
#define FLEX_TASK_UI_NAME        "ui"
#define FLEX_TASK_UI_CORE        1
#define FLEX_TASK_UI_PRIO        5
#define FLEX_TASK_UI_STACK       (16 * 1024)

#define FLEX_TASK_TOUCH_NAME     "touch"
#define FLEX_TASK_TOUCH_CORE     0
#define FLEX_TASK_TOUCH_PRIO     6
#define FLEX_TASK_TOUCH_STACK    (4 * 1024)

typedef struct {
    uint16_t chip_rev;          // revision real del silicio: mayor*100 + menor
    uint16_t build_rev_min;     // revision minima para la que se compilo
    uint16_t build_rev_max;     // revision maxima para la que se compilo
    uint32_t cpu_mhz;
    uint32_t psram_bytes;
    uint32_t flash_bytes;
    const char *idf_version;
    const char *app_version;
    const char *reset_reason;
} flex_boot_info_t;

// Rellena y deja en el log (una vez, al arrancar) la revision del chip, la
// ventana de revisiones del binario y el motivo del ultimo reinicio.
void flex_core_boot_report(void);
const flex_boot_info_t *flex_core_boot_info(void);

#ifdef __cplusplus
}
#endif
