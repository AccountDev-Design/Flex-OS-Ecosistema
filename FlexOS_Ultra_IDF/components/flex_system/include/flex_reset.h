// Flex OS Ultra · restablecimiento de datos de fabrica: marcador transaccional y
// etapas (docs/spec/01c §13; Arduino: FlexOS_Ultra_Recovery.h:234-415 y
// FlexOS_Ultra_Session.h:212-298).
//
// El marcador vive en NVS "flexreset" (pending u8, ver i32, stage i32, err i32,
// mismas claves y tipos que Arduino) y se escribe Y SE RELEE antes de tocar un
// solo dato y despues de cada etapa: si se corta la corriente, el arranque
// retoma la etapa anotada (todas son idempotentes). Nunca se borra el marcador
// con los datos.
//
// Solo lo pone en marcha el asistente de la pantalla, con confirmacion del
// usuario (clave, o escribir RESTABLECER, y deslizar). NO PROBADO EN HARDWARE REAL.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    FLEX_FR_NONE = 0, FLEX_FR_ARMED = 1, FLEX_FR_TOKENS, FLEX_FR_REMOTE, FLEX_FR_APPDATA, FLEX_FR_FILES,
    FLEX_FR_NVS, FLEX_FR_DEFAULTS, FLEX_FR_DONE, FLEX_FR_FAIL,
};
#define FLEX_FR_VER     1
#define FLEX_FR_STEPS   7                 // ARMED..DEFAULTS
#define FLEX_FR_NS      "flexreset"

typedef struct {
    bool pending;
    int ver, stage, err;
} flex_fr_marker_t;

typedef enum {
    FLEX_FR_BOOT_NORMAL = 0,   // sin marcador
    FLEX_FR_BOOT_RESUME,       // borrado a medias (o fallido): se retoma antes que nada
    FLEX_FR_BOOT_DONE,         // terminado: arranque de aparato nuevo; el marcador se borra luego
    FLEX_FR_BOOT_DISCARD,      // marcador de otra version: se descarta
} flex_fr_boot_t;

// ---- modelo (C puro, pruebas en host) -----------------------------------------------
typedef struct {
    bool (*save)(void *ctx, const flex_fr_marker_t *m);   // escribir Y releer; false si no cuadra
    bool (*run)(void *ctx, int stage);                    // ejecutar una etapa (idempotente)
    void *ctx;
} flex_fr_ops_t;

flex_fr_boot_t flex_fr_boot_decision(const flex_fr_marker_t *m);
// Arma el marcador (pending, ARMED) ANTES de borrar nada. false: no se pudo
// guardar y no se ha tocado ningun dato (m->stage = FAIL, m->err = 0).
bool flex_fr_arm(const flex_fr_ops_t *ops, flex_fr_marker_t *m);
// Ejecuta la etapa anotada; exito -> siguiente (guardada antes de seguir);
// fallo -> FAIL con err = etapa. Devuelve la etapa nueva.
int  flex_fr_step(const flex_fr_ops_t *ops, flex_fr_marker_t *m);
// Desde FAIL: con err = 0 (no se llego a armar) vuelve a armar; si no, retoma la
// etapa que fallo. false si no se pudo guardar.
bool flex_fr_retry(const flex_fr_ops_t *ops, flex_fr_marker_t *m);
// Numero de etapa para "Paso %d de %d" (1..7) y su nombre en pantalla.
int flex_fr_step_index(int stage);
const char *flex_fr_stage_name(int stage);

// ---- firmware (flex_reset.c) -----------------------------------------------------------
// En el arranque, tras montar el almacenamiento: lee el marcador. true si hay un
// borrado a medias (o fallido): el arranque no debe iniciar servicios y la
// interfaz entra directa al asistente.
bool flex_reset_boot_check(void);
flex_fr_boot_t flex_reset_boot_state(void);
const flex_fr_marker_t *flex_reset_marker(void);
// Progreso hacia la UI (por el buzon): etapa en curso, o fin (stage DONE o FAIL).
typedef void (*flex_reset_cb_t)(int stage, int err, void *user);
// Ficha de confirmacion (la pide la UI al mostrar el ultimo aviso).
uint32_t flex_reset_token(void);
// Arranca (o retoma) el borrado en su tarea. La UI ya cerro las apps (etapa ARMED).
bool flex_reset_start(uint32_t token, flex_reset_cb_t cb, void *user);
bool flex_reset_resume(flex_reset_cb_t cb, void *user);   // el arranque encontro uno a medias
bool flex_reset_retry(flex_reset_cb_t cb, void *user);    // "Reintentar" en la pantalla de fallo
// Primer arranque tras terminar: borra el marcador (Arduino lo hace al entrar al OOBE).
void flex_reset_confirm_clean_boot(void);
void flex_reset_reboot(void);

#ifdef __cplusplus
}
#endif
