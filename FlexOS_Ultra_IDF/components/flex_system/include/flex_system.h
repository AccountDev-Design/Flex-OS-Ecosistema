// Flex OS Ultra · sistema: datos para Device Care.
//
// La tarea "system" (nucleo 0, prioridad baja) mide cada 2 s y publica una
// copia; la UI la lee sin bloquear. Todo sale de APIs reales de ESP-IDF:
// heap_caps_*, uxTaskGetSystemState (con tiempos de ejecucion), el sensor de
// temperatura del P4, esp_reset_reason y el volcado de la particion coredump.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_SYS_MAX_TASKS 28

typedef struct {
    char     name[16];
    uint32_t stack_free;   // bytes que nunca se han usado de la pila (high-water mark)
    uint8_t  prio;
    int8_t   core;         // -1 = sin nucleo fijo
    float    cpu_pct;      // del total de los dos nucleos en la ultima ventana
} flex_sys_task_t;

typedef struct {
    uint32_t uptime_s;
    uint32_t heap_int_free;
    uint32_t heap_int_min;
    uint32_t heap_int_largest;
    uint32_t heap_psram_free;
    uint32_t heap_psram_min;
    uint32_t heap_psram_largest;
    uint32_t psram_total;
    bool     temp_ok;
    float    temp_c;
    float    cpu_load[2];          // % ocupado de cada nucleo en la ultima ventana
    uint8_t  ntasks;
    flex_sys_task_t tasks[FLEX_SYS_MAX_TASKS];
    char     reset_reason[40];
    uint32_t boot_count;
    bool     crash_present;        // la particion coredump guarda un fallo
    char     crash_task[16];
    uint32_t crash_pc;
    uint32_t crash_cause;          // mcause (RISC-V)
    char     crash_reason[80];
    uint32_t samples;
} flex_sys_snapshot_t;

// Requiere flex_storage_init() (cuenta de arranques) y flex_bus_init().
esp_err_t flex_system_start(void);
// Copia la ultima muestra (estructura grande: que el llamante la tenga estatica).
void flex_system_get(flex_sys_snapshot_t *out);

// Eventos de FLEX_EV_SYSTEM
enum {
    FLEX_SYS_EV_LOW_INTERNAL_RAM = 1,   // RAM interna libre por debajo del umbral (dato: uint32_t bytes)
    FLEX_SYS_EV_HOT,                    // temperatura alta (dato: float grados)
    FLEX_SYS_EV_CRASH_FOUND,            // al arrancar habia un volcado de un fallo
};

#ifdef __cplusplus
}
#endif
