// Flex OS Ultra · arranque: banda forense y splash (C portable, sin LVGL). docs/spec/01a §2.3-2.5.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_BOOT_BANNER_MS 2200   // la banda forense (showBootBanner: delay(2200))
#define FLEX_BOOT_SPLASH_MS 2600   // el splash entero: 600 sube, 1400 fijo, 600 baja
#define FLEX_BOOT_DOT_MS    320    // cada punto de carga

// resetReasonStr (Home.h:246): "POWERON", "SW", "PANIC (crash)", "INT_WDT", "TASK_WDT", "WDT",
// "BROWNOUT (voltaje)", "DEEPSLEEP" y "OTRO" para cualquier otro. reason = esp_reset_reason_t.
const char *flex_boot_reason_str(int reason);
// Reinicio ANORMAL a efectos de la banda: cualquiera que no sea POWERON, SW o DEEPSLEEP
// (FlexOS_Ultra.ino, paso 20). No es el criterio del Modo seguro (flex_safe_abnormal).
bool flex_boot_banner_wanted(int reason);
// Opacidad del texto del splash 0..255 con e ms desde que empezo; -1 cuando termino (splashTick)
int flex_boot_alpha(uint32_t e);
// Punto de carga activo (millis()/320) % 3
int flex_boot_dot(uint32_t ms);

#ifdef __cplusplus
}
#endif
