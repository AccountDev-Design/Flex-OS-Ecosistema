// Flex OS Ultra · Flex Account: el SERVICIO (ver flex_account.h) sobre un juego de operaciones inyectadas. La placa las implementa en flex_account_port.c; las pruebas de host,
// sobre el mundo simulado del propio Arduino (tests/host/netstub). Nada de aqui bloquea la tarea de la interfaz: la red y la NVS corren en la tarea de fondo.
#pragma once

#include "flex_account.h"

#ifdef __cplusplus
extern "C" {
#endif

// Una peticion HTTPS (HTTP/1.0 con Connection: close: el final del cuerpo es inequivoco). `auth` = "Bearer ..." (la credencial nunca va en la URL ni se imprime)
typedef struct {
    const char *method;   // "POST" | "GET"
    const char *url;
    const char *auth;     // NULL = sin cabecera Authorization
    const uint8_t *body;  // POST: JSON
    size_t body_len;
    bool insecure;        // Arduino: el enlace por codigo no valida el certificado (la respuesta lleva firma ES256); la sesion SI. La placa valida SIEMPRE (mas estricto)
    uint32_t timeout_ms;
    bool set_connect_timeout;   // la validacion fija tambien el de conexion (sin esto TCP se corta a los 5 s del valor por defecto)
} flex_account_req_t;

typedef struct {
    const char *fw_version;   // "1.0.0": va en la peticion del codigo
} flex_account_cfg_t;

typedef struct {
    uint32_t (*millis)(void);
    void (*delay_ms)(uint32_t ms);
    bool (*wifi_connected)(void);
    void (*fill_random)(void *buf, size_t n);
    uint64_t (*efuse_mac)(void);
    // NVS "flexacct" con la semantica de Preferences / nvs: get_str devuelve la longitud CON el terminador (0 si no existe o no cabe), put_str devuelve strlen
    bool (*nvs_begin)(void);   // false: el almacenamiento seguro no responde
    void (*nvs_end)(void);
    uint32_t (*nvs_get_u32)(const char *key, uint32_t def);
    size_t (*nvs_put_u32)(const char *key, uint32_t v);   // bytes escritos (4) o 0
    size_t (*nvs_get_str)(const char *key, char *out, size_t cap);
    size_t (*nvs_put_str)(const char *key, const char *v);
    bool (*nvs_get_bool)(const char *key, bool def);
    bool (*nvs_put_bool)(const char *key, bool v);
    uint8_t (*nvs_get_u8)(const char *key, uint8_t def);
    size_t (*nvs_put_u8)(const char *key, uint8_t v);
    bool (*nvs_is_key)(const char *key);
    void (*nvs_clear)(void);
    // red
    bool (*tls_room)(size_t *internal_free, size_t *largest_block);
    bool (*tls_dns_ok)(const char *host);
    bool (*http_begin)(const flex_account_req_t *r);   // false: no se pudo abrir
    int (*http_send)(void);                            // codigo HTTP, o negativo si fallo el transporte
    int (*http_tls_error)(void);                       // el codigo de la conexion (mbedTLS, negativo; -1 = sin DNS/TCP/tiempo)
    int (*http_size)(void);                            // Content-Length (<= 0: no lo hay)
    bool (*http_has_stream)(void);
    int (*http_available)(void);
    int (*http_read)(uint8_t *buf, size_t n);
    bool (*http_connected)(void);
    void (*http_end)(void);
    // tarea de fondo y cerrojo
    bool (*ensure_task)(void);   // crea (si falta) la tarea que llama a flex_account_step cada 100 ms; false: no hubo memoria
    void (*lock)(void);          // NULL = una sola tarea
    void (*unlock)(void);
    void (*log)(const char *msg);   // NULL permitido (nunca se le pasa la credencial)
} flex_account_ops_t;

void flex_account_set(const flex_account_cfg_t *cfg, const flex_account_ops_t *ops);   // NULL: lo de la placa (flex_account_port_*)
const flex_account_ops_t *flex_account_ops(void);

// ---- API (flexAccount* de Arduino) ----
void flex_account_begin(void);   // recupera SIEMPRE primero el estado persistente: con cuenta arranca LINKED_OFFLINE y se valida en cuanto haya Wi-Fi
bool flex_account_request_code(const char *device_label);
void flex_account_cancel(void);
bool flex_account_linked(void);   // hay credencial guardada (no depende de la red)
bool flex_account_usable(void);   // ... y el servidor no la ha rechazado
flex_account_state_t flex_account_state(void);
flex_account_link_t flex_account_link_state(void);
void flex_account_snapshot(flex_account_snapshot_t *out);
void flex_account_request_validation(void);
void flex_account_report_rejected(void);   // Flex Cloud recibio un 401: se revalida ya (como mucho una vez por minuto), sin desvincular por una respuesta
bool flex_account_copy_bearer(char *out, size_t capacity);   // copia para APIs autenticadas: nunca un puntero a la memoria interna
void flex_account_forget_local(void);   // desvincula ESTE aparato: borra NVS y RAM, id de instalacion nuevo; escribe una sola vez y solo si habia algo
void flex_account_step(void);           // una vuelta de la tarea de fondo (la tarea de la placa la llama cada 100 ms)

#ifdef FLEX_HOST_TEST
void flex_account_test_power_cycle(void);   // se pierde la RAM, se conserva la NVS
#endif

#ifdef __cplusplus
}
#endif
