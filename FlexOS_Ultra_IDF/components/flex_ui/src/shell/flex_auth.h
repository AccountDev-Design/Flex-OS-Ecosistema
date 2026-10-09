// Flex OS Ultra · pantallas de la clave del sistema (docs/spec/01a §5).
//
// UNA sola pantalla de verificacion para todo (lsuStartVerify de Arduino):
// desbloquear, abrir una app con candado, poner/quitar candados, salir del
// kiosco, apagado seguro, restablecer, medios protegidos. Quien la pide dice
// que hacer al acertar y al cancelar; la clave nunca pasa por la UI mas que el
// tiempo de teclearla (se copia a la tarea de verificacion y se borra).
#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool from_lock;              // salio del bloqueo: cancelar vuelve SIEMPRE al bloqueo
    bool reveal;                 // al acertar, revelado del escritorio (400 ms)
    void (*on_ok)(void *ctx);    // tras el revelado (si lo hay)
    void (*on_cancel)(void *ctx);
    void *ctx;
} flex_auth_req_t;

// Hay clave puesta (NVS "locktype" != 0, como gLockType de Arduino).
bool flex_auth_required(void);
// Abre la verificacion. false si no hay clave (no se abre nada: el llamante sigue).
bool flex_auth_verify(const flex_auth_req_t *req);
// Crear la clave (Ajustes -> Seguridad -> Bloqueo): selector PIN/Contrasena.
// on_done(ctx) al guardar o al salir.
void flex_auth_setup(void (*on_done)(void *ctx), void *ctx);
bool flex_auth_active(void);
// La verificacion abierta salio del bloqueo (no de un candado de app ni de Ajustes).
bool flex_auth_from_lock(void);
// Corta sin avisar a nadie (bloquear, suspender, apagar): borra lo tecleado.
void flex_auth_abort(void);

// Estado de la espera progresiva (para pruebas y Device Care)
uint32_t flex_auth_wait_left_ms(void);

#ifdef __cplusplus
}
#endif
