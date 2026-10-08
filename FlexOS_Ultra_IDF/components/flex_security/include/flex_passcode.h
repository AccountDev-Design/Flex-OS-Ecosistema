// Flex OS Ultra · clave del sistema (PIN o contrasena de la pantalla de bloqueo).
//
// Port de FlexOS_Passcode.cpp de la version Arduino con el MISMO formato en la
// NVS ("flexos"): sal de 16 B ("lockslt"), PBKDF2-HMAC-SHA256 de 32 B
// ("lockhsh"), iteraciones ("lockitr", 12 000), longitud del PIN ("locklen") y
// tipo ("locktype": 0 nada, 1 PIN, 2 contrasena). Una placa que cambia de
// firmware sigue abriendose con su clave de siempre. Las claves antiguas en
// texto claro ("lockpin"/"lockpass") se migran a hash y se borran.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_LOCK_NONE 0
#define FLEX_LOCK_PIN  1
#define FLEX_LOCK_PASS 2
#define FLEX_LOCK_SECRET_MAX 64
#define FLEX_LOCK_ITERS 12000
#define FLEX_LOCK_STEP_ITERS 1500

#define FLEX_LOCK_BUSY 0
#define FLEX_LOCK_OK   1
#define FLEX_LOCK_FAIL 2

// Primitivas (publicas para las pruebas de host contra vectores conocidos)
void flex_lock_wipe(void *p, size_t n);
bool flex_lock_equal_ct(const void *a, const void *b, size_t n);
void flex_lock_kdf(const char *secret, const uint8_t *salt, size_t salt_len, uint32_t iters, uint8_t *out,
                   size_t out_len);

int  flex_lock_type(void);
int  flex_lock_len(void);           // longitud del PIN (autoconfirmar); 0 si contrasena o nada
bool flex_lock_set(const char *secret, int type);
bool flex_lock_clear(void);
int  flex_lock_migrate(void);       // 1 migro/limpio, 0 nada que hacer, -1 no se pudo (todo queda como estaba)

// Verificacion de una sentada (no desde la tarea de UI: tarda decenas de ms)
bool flex_lock_verify(const char *secret);
// A plazos (estado del modulo, una en vuelo)
bool flex_lock_verify_begin(const char *secret);
int  flex_lock_verify_step(uint32_t budget_iters);
void flex_lock_verify_cancel(void);
bool flex_lock_verify_active(void);

// Una sentada con variables locales (no toca la de plazos): para otra tarea.
// Si la migracion del arranque no pudo completarse, acepta la clave antigua en
// texto claro (comparada en tiempo constante) para no dejar fuera al usuario.
bool flex_lock_verify_alone(const char *secret);

// Verificacion en segundo plano: el resultado llega a la tarea de UI (buzon).
// El secreto se copia y se borra. Devuelve false si no se pudo lanzar.
typedef void (*flex_lock_result_cb_t)(bool ok, void *user);
bool flex_lock_verify_async(const char *secret, flex_lock_result_cb_t cb, void *user);

// Fallos seguidos (NVS "lockfails", tope 9999) y espera progresiva:
// 1-3 sin espera, 4-5 -> 30 s, >= 6 -> 5 min (Lock.h:189-336).
int      flex_lock_fails(void);
void     flex_lock_set_fails(int n);
uint32_t flex_lock_penalty_ms(int fails);

#ifdef __cplusplus
}
#endif
