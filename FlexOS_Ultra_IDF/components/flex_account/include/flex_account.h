// Flex OS Ultra · Flex Account (N3). Referencia: FlexOS_Account.h/.cpp (servicio) y FlexOS_Account_Bridge.h (pantalla) de Arduino.
//
// Tres capas, como la OTA y la hora por red:
//  * NUCLEO (flex_account_model.c, C puro + cJSON + SHA-256/ECDSA): clasificar la respuesta de la sesion, espera progresiva, lectura de cadenas de la NVS, base64url,
//    firma ES256 del sobre con la clave anclada, cargas de codigo y de estado, credencial con su SHA-256. Comparado con las funciones de Arduino compiladas tal cual.
//  * SERVICIO (flex_account.c): el enlace por codigo, la validacion de la sesion y la persistencia, sobre un juego de operaciones (flex_account_ops_t: red, NVS, tiempo,
//    aleatorio) que la placa implementa en flex_account_port.c y las pruebas con dobles. Nunca corre en la tarea de la interfaz.
//  * INTERFAZ (flex_ui): la pantalla Cuenta.
//
// "No hay red" NUNCA significa "desvinculado": una cuenta guardada en NVS sigue vinculada aunque el equipo arranque sin Wi-Fi, el servidor no conteste o el certificado
// no valide. Solo la desvincula el usuario (o restablecer de fabrica) o que no haya credencial guardada.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FLEX_ACCOUNT_CODE_URL
#define FLEX_ACCOUNT_CODE_URL "https://flex-developer-studio.ralvarezsantos980.chatgpt.site/api/devices/code"
#endif
// La validacion de la sesion es un GET autenticado con la credencial del aparato; lo atiende Flex Cloud, que pregunta a Flex Account
#ifndef FLEX_ACCOUNT_SESSION_URL
#define FLEX_ACCOUNT_SESSION_URL "https://flex-developer-studio.ralvarezsantos980.chatgpt.site/api/cloud/me"
#endif
#define FLEX_ACCOUNT_ACTIVATE_PREFIX "https://flex-developer-studio.ralvarezsantos980.chatgpt.site/activate?code="
#define FLEX_ACCOUNT_KEY_ID "69b91cf7a9246cc2084dda73ccef77b2dc1a372b18fec19b49cf980efd68af69"
#define FLEX_ACCOUNT_ENVELOPE_MAX 8192u
#define FLEX_ACCOUNT_HTTP_TIMEOUT_MS 15000u
#define FLEX_ACCOUNT_LINK_TIMEOUT_MS (10UL * 60UL * 1000UL)
#define FLEX_ACCOUNT_REVALIDATE_MS (6UL * 60UL * 60UL * 1000UL)   // con la sesion ya validada: barata y rara
#define FLEX_ACCOUNT_SETTLE_MS 3000u                              // tras conectar el Wi-Fi: deja que DHCP, DNS y NTP terminen
#define FLEX_ACCOUNT_REJECT_RATE_MS 60000u                        // un 401 de otro modulo pide revalidar, como mucho una vez por minuto
#define FLEX_ACCOUNT_MIN_ATTEMPT_GAP_MS 30000u                    // un Wi-Fi que entra y sale no se convierte en una validacion cada pocos segundos

// Estado observable del ENLACE (el proceso de vincular)
typedef enum {
    FLEX_ACCOUNT_UNLINKED = 0,
    FLEX_ACCOUNT_REQUESTING,
    FLEX_ACCOUNT_CODE_READY,
    FLEX_ACCOUNT_LINKED,   // hay una credencial guardada en NVS (ver flex_account_link_t)
    FLEX_ACCOUNT_EXPIRED,  // el CODIGO de vinculacion caduco (no la sesion)
    FLEX_ACCOUNT_CANCELLED,
    FLEX_ACCOUNT_ERROR
} flex_account_state_t;

// Estado del VINCULO
typedef enum {
    FLEX_LINK_UNLINKED = 0,
    FLEX_LINK_LINKED,
    FLEX_LINK_LINKED_OFFLINE,
    FLEX_LINK_NETWORK_UNAVAILABLE,
    FLEX_LINK_AUTH_REQUIRED,
    FLEX_LINK_TOKEN_EXPIRED,
    FLEX_LINK_ERROR
} flex_account_link_t;

typedef struct {
    flex_account_state_t state;
    uint8_t progress;
    bool linked;   // == hay credencial guardada (no depende de la red)
    char stage[72];
    char error[144];
    char code[9];
    char activation_url[192];
    char flex_address[48];
    char display_name[64];
    flex_account_link_t link;
    char link_detail[96];
    uint32_t verified_age_s;   // segundos desde la ultima validacion (0xFFFFFFFF = nunca en esta sesion)
} flex_account_snapshot_t;

typedef enum { FLEX_SESSION_OK = 0, FLEX_SESSION_AUTH_REQUIRED, FLEX_SESSION_TOKEN_EXPIRED, FLEX_SESSION_UNAVAILABLE } flex_session_verdict_t;
typedef enum { FLEX_POLL_PENDING = 0, FLEX_POLL_APPROVED, FLEX_POLL_EXPIRED, FLEX_POLL_INVALID } flex_poll_t;

// ---------------------------------------------------------------------------------------------------------------------------------------------------
//  Nucleo puro
// ---------------------------------------------------------------------------------------------------------------------------------------------------
// Clasifica la respuesta del servicio de sesion. `http_status` < 0 es un fallo de transporte; `error_code` es error.code del JSON (puede ser NULL)
flex_session_verdict_t flex_account_classify(int http_status, const char *error_code);
// Espera antes del reintento numero `failures` (1, 2, ...): 30 s, 60 s, 2 min, 4 min, 8 min y despues 15 min como mucho
uint32_t flex_account_backoff_ms(uint8_t failures);
// Longitud util de una cadena leida de la NVS segun lo que devolvio la lectura (nvs_get_str cuenta el terminador, putString no)
size_t flex_nvs_str_len(size_t nvs_returned, const char *buf, size_t cap);
bool flex_account_json_escape(const char *in, char *out, size_t cap);
int flex_account_b64_value(char c);
// Decodifica base64url (y base64 con '+' '/'; admite '=' al final) a un bloque de malloc (liberar con free); falla si pasa de max_out
bool flex_account_b64url_decode(const char *text, uint8_t **out, size_t *out_len, size_t max_out);
bool flex_account_valid_address(const char *address);   // "algo@flex" de 6 o mas caracteres
// Verifica una firma ES256 (r || s, 64 bytes) sobre SHA-256(payload) con la clave publica anclada (65 bytes, sin comprimir)
bool flex_account_verify_signature(const uint8_t *payload, size_t payload_len, const uint8_t signature[64]);
// Abre el sobre {"schema":2,"algorithm":"ES256","keyId":...,"payload":b64url,"signature":b64url}: el payload solo se entrega si la firma es valida
bool flex_account_signed_payload(const uint8_t *envelope, size_t envelope_len, uint8_t **payload, size_t *payload_len);
bool flex_account_parse_code(const uint8_t *payload, size_t len, const char *expected_hash, char code[9], char activation_url[192]);
flex_poll_t flex_account_parse_status(const uint8_t *payload, size_t len, char address[48], char display_name[64]);
// Credencial nueva a partir de 32 bytes aleatorios: 43 caracteres base64url y su SHA-256 en hexadecimal
bool flex_account_make_bearer(const uint8_t random32[32], char out[48], char hash_hex[65]);
bool flex_account_sha256(const uint8_t *data, size_t len, uint8_t out[32]);
void flex_account_hex32(const uint8_t data[32], char out[65]);
const char *flex_account_link_label(flex_account_link_t link);
const char *flex_account_verdict_detail(flex_session_verdict_t v);

#ifdef FLEX_HOST_TEST
// Las pruebas firman con una clave EFIMERA (la privada de produccion solo existe en Flex Developer Studio)
void flex_account_test_set_key(const uint8_t pub[65]);
#endif

#ifdef __cplusplus
}
#endif
