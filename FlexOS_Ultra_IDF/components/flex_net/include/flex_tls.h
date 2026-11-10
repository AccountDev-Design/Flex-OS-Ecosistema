// Flex OS Ultra · TLS: lo que dice el sistema cuando una conexion segura falla (referencia: FlexOS_CloudTLS.h/.cpp). C puro: los textos y los umbrales se prueban
// en el host contra las funciones de Arduino; el transporte de la placa (esp_http_client, esp-tls) lo pone cada servicio (flex_account_port.c, la nube).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// mbedTLS reserva sus buffers de registro en la SRAM INTERNA (la PSRAM no cuenta): sin sitio no se abre la conexion. Los suelos son los del sistema (por debajo
// de 40 KB de interna entra el modo de proteccion).
#define FLEX_TLS_MIN_INTERNAL (40u * 1024u)
#define FLEX_TLS_MIN_BLOCK    (16u * 1024u)

// true si con esta SRAM interna libre y este mayor bloque cabe abrir ahora una conexion TLS (flexTlsRoom sin la medida: la mide quien llama)
bool flex_tls_room_ok(size_t internal_free, size_t largest_block);

// Motivo corto y legible de un fallo de conexion a partir del codigo de la conexion (mbedTLS, negativo; -1 = sin DNS ni TCP o tiempo agotado; 0 o mas = no fue
// un error de la conexion). Siempre escribe algo en `out`.
const char *flex_tls_reason(int tls_error, char *out, size_t cap);

// El -1 no dice que fallo (DNS, TCP o el saludo TLS): lo distinguen el DNS y lo que tardo el intento. Es una pista, no una prueba: el texto lleva los segundos.
const char *flex_tls_phase(bool dns_ok, uint32_t elapsed_ms, uint32_t handshake_ms, uint32_t connect_ms, char *out, size_t cap);

// "https://host[:puerto]/ruta" -> "host"
bool flex_url_host(const char *url, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
