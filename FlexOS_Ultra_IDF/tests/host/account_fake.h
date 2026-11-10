// Un mundo en memoria para Flex Account (operaciones del servicio sobre una NVS de mentira y un servidor que firma sus respuestas con una clave P-256 efimera). Lo usan la
// prueba con hilos (test_account_thr.c) y el simulador de la interfaz (sim/scenes/scene_account.c). Es seguro entre hilos: la NVS y la peticion en curso estan
// serializadas, el resto son atomicos.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "flex_account_svc.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { AF_SESS_OK = 0, AF_SESS_DOWN, AF_SESS_REVOKED, AF_SESS_EXPIRED };
enum { AF_POSTS = 0, AF_POLLS, AF_SESSIONS, AF_NVS_WRITES };

void account_fake_reset(void);   // mundo nuevo: NVS de fabrica, servidor sin aprobar nada, Wi-Fi puesto, reloj en 1000. La clave se genera una vez y se entrega al servicio
const flex_account_ops_t *account_fake_ops(void);
void account_fake_set_wifi(bool on);
void account_fake_set_session(int mode);
void account_fake_set_polls(int pending_answers);   // consultas "pendiente" antes de aprobar
void account_fake_set_account(const char *address, const char *display);
void account_fake_set_latency(uint32_t ms);          // lo que tarda cada peticion (5 ms por omision)
void account_fake_set_mem(bool ok);                  // hay SRAM interna para abrir TLS
void account_fake_set_post_fail(int n);              // las n proximas peticiones de codigo fallan por transporte
void account_fake_set_post_status(int status);       // != 0: el POST del codigo contesta este codigo sin cuerpo
void account_fake_set_ensure_task(bool (*fn)(void)); // quien crea la tarea de fondo (NULL: se da por creada y el que prueba llama a flex_account_step)
void account_fake_set_delay_hook(void (*fn)(uint32_t ms));   // se llama en cada espera del servicio (los hilos duermen un poco)
void account_fake_advance(uint32_t ms);
uint32_t account_fake_now(void);
int account_fake_count(int what);
bool account_fake_nvs_linked(void);   // hay una cuenta completa en la NVS
void account_fake_nvs_save(const char *token43, const char *address, const char *display, int authst);   // una cuenta ya guardada (arranque con cuenta)
void account_fake_server_register(const char *token43);
// El servidor sin transporte: contesta una peticion (codigo HTTP, o -1 si falla el transporte) y deja el cuerpo en `reply`
int account_fake_serve(const char *method, const char *url, const char *auth, const char *body, char *reply, size_t cap);   // el servidor conoce esta credencial (la validacion la acepta)

#ifdef __cplusplus
}
#endif
