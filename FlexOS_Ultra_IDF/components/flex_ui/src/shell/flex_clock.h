// Flex OS Ultra · hora del sistema para la interfaz.
//
// Como la version Arduino (FlexOS_Ultra_Clock.h): huso fijo de Lima (UTC-5) y,
// mientras no haya hora de red (NTP llega con el Wi-Fi), la semilla de fabrica
// sabado 4 de julio de 2026, 13:23 local, contando desde el arranque.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_TZ_OFFSET_SEC (-5 * 3600)

void flex_clock_init(void);
// Hora local desglosada (tm_wday 0 = domingo, tm_mon 0..11).
void flex_clock_now(struct tm *out);
// true si la hora vino de la red (NTP) y no de la semilla.
bool flex_clock_synced(void);
void flex_clock_set_utc(uint32_t utc);   // NTP / ajuste manual
// "13:23" o "1:23" (reloj grande, sin AM/PM) y "13:23" / "1:23 PM" (barras).
void flex_clock_str_big(char *out, int cap);
void flex_clock_str_bar(char *out, int cap);
// Minuto absoluto actual (para detectar el cambio de minuto).
int32_t flex_clock_minute(void);

#ifdef __cplusplus
}
#endif
