// Flex OS Ultra · memoria de las apps: soltar, admitir, desalojar y avisar (C portable, sin LVGL).
//
// La parte "decidir" (niveles, histeresis, veredicto de apertura, avisos con enfriamiento) es
// FlexOS_Mem.cpp de Arduino, copiado tal cual en flex_portable. Esta es la capa de arriba de
// FlexOS_Ultra_Core.h:213-495: huella medida por app, soltar en orden LRU, admision al abrir,
// alivio automatico y desalojo por presupuesto. La plataforma (medir la PSRAM, soltar caches,
// cerrar una app...) entra por flex_mem_ops_t, asi tests/host la compara con el codigo Arduino.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "FlexOS_Mem.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_MEM_APPS 32
enum { FLEX_ALIFE_CLOSED = 0, FLEX_ALIFE_SUSPENDED, FLEX_ALIFE_RUNNING };

typedef struct {
    // medir
    uint32_t (*free_psram)(void *ctx);                 // memFreePsram
    void (*sample_now)(void *ctx);                     // memSampleNow
    const FlexMemSnap *(*snap)(void *ctx);             // memSnap
    // soltar
    uint32_t (*shed_system)(void *ctx);                // memShedSystem: bytes de PSRAM medidos
    bool (*bg_busy)(void *ctx, int app);               // appBgBusy: lista blanca Y trabajo real
    bool (*has_shed)(void *ctx, int app);              // la app declara shed()
    size_t (*app_shed)(void *ctx, int app);            // shed(): devuelve la pista
    // desalojar
    bool (*radio_busy)(void *ctx);                     // wifiRadioBusy
    bool (*terminate)(void *ctx, int app);             // appTerminate(app, false): false = no se pudo guardar
    void (*drop_card)(void *ctx, int app);             // quita su tarjeta de Recientes
    void (*notify)(void *ctx, const char *title, const char *sub);   // sysNotify
    void *ctx;
} flex_mem_ops_t;

typedef struct {
    uint32_t mem[FLEX_MEM_APPS];        // huella medida de cada app
    bool known[FLEX_MEM_APPS];
    bool shed[FLEX_MEM_APPS];           // "Estado guardado" (se le solto lo pesado) en vez de "Pausada"
    uint8_t state[FLEX_MEM_APPS];       // FLEX_ALIFE_*
    uint32_t seen_ms[FLEX_MEM_APPS];    // ultima vez en primer plano (LRU)
    FlexMemAlerts alerts;
    int napps;
} flex_mem_t;

void flex_mem_init(flex_mem_t *m, int napps);
// Huella medida (appMemCommit/Released/Forget): sin medida la interfaz escribe "--", nunca un numero inventado
void flex_mem_commit(flex_mem_t *m, const flex_mem_ops_t *o, int app, uint32_t free_before);
void flex_mem_released(flex_mem_t *m, int app, uint32_t bytes);
void flex_mem_forget(flex_mem_t *m, int app);
bool flex_mem_has(const flex_mem_t *m, int app);
uint32_t flex_mem_bytes(const flex_mem_t *m, int app);
// memShedApp: solo apps SUSPENDIDAS y sin trabajo real; mide lo liberado
uint32_t flex_mem_shed_app(flex_mem_t *m, const flex_mem_ops_t *o, int app);
// memShedAll: caches del sistema y luego apps suspendidas de la menos usada a la mas; para en stop_at
uint32_t flex_mem_shed_all(flex_mem_t *m, const flex_mem_ops_t *o, uint32_t stop_at);
// memAdmitApp: FLEXMEM_OK o el motivo de la negativa (suelta una vez y vuelve a preguntar)
int flex_mem_admit(flex_mem_t *m, const flex_mem_ops_t *o, int weight);
const char *flex_mem_deny_text(int verdict);
// memAlertTick. blocked = Modo seguro o restablecimiento (ni el nivel se actualiza); act_ok = Inicio o una app,
// sin transicion, sin Optimizar a la vista y sin la radio levantandose (se actua y se avisa solo asi)
void flex_mem_alert_tick(flex_mem_t *m, const flex_mem_ops_t *o, uint32_t now, bool blocked, bool act_ok);
// appEnforceMemoryBudget: solo con nivel CRITICAL; suelta y, si sigue, cierra la suspendida menos reciente
// SOLO si su sesion se pudo guardar. Devuelve cuantas apps cerro
int flex_mem_enforce_budget(flex_mem_t *m, const flex_mem_ops_t *o);

#ifdef __cplusplus
}
#endif
