// Flex OS Ultra · Modo seguro: deteccion de reinicios anormales en el arranque.
//
// docs/spec/01c §11.1 (Arduino: FlexOS_Ultra_Session.h:115-210). Un contador
// persistente de reinicios anormales SEGUIDOS (NVS flexsafe/fails, i32) y la causa
// que disparo la cadena (flexsafe/cause, i32 = esp_reset_reason_t). Con 3 o mas se
// arranca en Modo seguro. Encender, reiniciar a proposito (y el reinicio del OTA)
// o despertar de deep sleep NO cuentan. A los 60 s de arranque estable (fuera del
// Modo seguro) el contador vuelve a 0.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Valores de esp_reset_reason_t (esp_system.h; flex_safeboot.c comprueba que coinciden)
enum {
    FLEX_RST_UNKNOWN = 0, FLEX_RST_POWERON = 1, FLEX_RST_EXT = 2, FLEX_RST_SW = 3, FLEX_RST_PANIC = 4,
    FLEX_RST_INT_WDT = 5, FLEX_RST_TASK_WDT = 6, FLEX_RST_WDT = 7, FLEX_RST_DEEPSLEEP = 8, FLEX_RST_BROWNOUT = 9,
    FLEX_RST_PWR_GLITCH = 14, FLEX_RST_CPU_LOCKUP = 15,
};

#define FLEX_SAFE_FAIL_MAX   3
#define FLEX_SAFE_FAILS_CAP  250
#define FLEX_SAFE_STABLE_MS  60000u

typedef struct {
    int fails;     // reinicios anormales seguidos (0..250)
    int cause;     // motivo que se ensena (el de la cadena)
    bool abnormal; // este arranque lo es
    bool safe;     // arrancar en Modo seguro
    bool write;    // hay que guardar fails/cause
} flex_safe_eval_t;

// ---- modelo (C puro, pruebas en host) -----------------------------------------------
bool flex_safe_abnormal(int reason);
// saved_fails/saved_cause: lo guardado (have_cause = false si no habia "cause").
flex_safe_eval_t flex_safe_eval(int reason, int saved_fails, int saved_cause, bool have_cause);
// A los 60 s de arranque estable: true si hay que poner el contador a 0 (una vez).
bool flex_safe_stable_clear(bool safe, int fails, uint32_t uptime_ms);
// Texto de la causa para la pantalla (safeCauseText).
const char *flex_safe_cause_text(int cause);

// ---- firmware ------------------------------------------------------------------------
// Tras montar el almacenamiento: evalua este arranque, guarda y arma el reloj
// de arranque estable.
void flex_safeboot_eval(void);
bool flex_safe_mode(void);
int  flex_safe_fails(void);
int  flex_safe_cause(void);
// "Reiniciar normalmente": contador a 0, grabado y reinicio, en una tarea propia
// (puede esperar en la cola del escritor a "Limpiar caches"). Si no se puede
// grabar NO reinicia y llama a on_fail en la tarea de UI. false si no se pudo
// lanzar (on_fail no se llama).
bool flex_safe_exit_and_reboot(void (*on_fail)(void));

#ifdef __cplusplus
}
#endif
