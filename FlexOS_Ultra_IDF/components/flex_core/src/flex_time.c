// Flex OS Ultra · hora del sistema (ver flex_time.h).
#include "flex_time.h"

#ifndef FLEX_SIM
#include <sys/time.h>
#endif

static volatile bool s_synced;

#ifdef FLEX_SIM
void flex_time_sim_unsync(void)   // solo el simulador: un arranque en frio (la hora aun no es de la red)
{
    s_synced = false;
}
// En el simulador la hora es la que fija la escena (flex_clock_set_utc) mas un desplazamiento que mueve el reloj sin marcarlo como sincronizado
static uint32_t s_sim_utc = 1791481500u;   // jue 8 oct 2026, 12:45 en Lima (17:45 UTC)
uint32_t flex_sim_utc_offset;
#endif

uint32_t flex_time_utc(void)
{
#ifdef FLEX_SIM
    return s_sim_utc + flex_sim_utc_offset;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)tv.tv_sec;
#endif
}

void flex_time_set_utc(uint32_t utc, bool from_network)
{
#ifdef FLEX_SIM
    s_sim_utc = utc;
#else
    struct timeval tv = {.tv_sec = (time_t)utc, .tv_usec = 0};
    settimeofday(&tv, NULL);
#endif
    if (from_network) {
        s_synced = true;
    }
}

bool flex_time_synced(void)
{
    return s_synced;
}
