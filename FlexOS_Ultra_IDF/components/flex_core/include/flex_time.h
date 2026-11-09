// Flex OS Ultra · hora del sistema: la base unica que comparten la interfaz (flex_clock) y el servicio de hora por red (flex_ntp).
//
// En la placa es el reloj POSIX (gettimeofday / settimeofday), que sigue contando sin red. "Sincronizada" es solo la hora que vino de la red
// o de un ajuste manual; la semilla de fabrica y la hora recuperada de la NVS (aproximada) no lo son.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t flex_time_utc(void);                               // epoca UTC actual
void flex_time_set_utc(uint32_t utc, bool from_network);    // fija la hora; from_network = true: pasa a "sincronizada"
bool flex_time_synced(void);
#ifdef FLEX_SIM
void flex_time_sim_unsync(void);   // solo el simulador: un arranque en frio
#endif

#ifdef __cplusplus
}
#endif
