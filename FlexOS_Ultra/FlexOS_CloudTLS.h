#ifndef FLEXOS_CLOUD_TLS_H
#define FLEXOS_CLOUD_TLS_H

// #############################################################
//  FLEX OS · RAICES TLS DE FLEX CLOUD Y FLEX ACCOUNT
//  ------------------------------------------------------------
//  Toda peticion que lleve la credencial de Flex Account (validar la sesion,
//  Flex Cloud) valida el certificado del servidor contra ESTAS raices. Nunca
//  setInsecure(): quien controlase la red veria la credencial en claro y
//  podria suplantar al usuario.
//
//  Mismo criterio que el OTA (FLEXOS_OTA_ROOT_CA) y el clima, con una
//  diferencia: aqui el valor por defecto NO esta vacio. El servicio se aloja
//  detras de un proveedor cuya autoridad puede rotar (Cloudflare usa Google
//  Trust Services, Let's Encrypt y SSL.com), asi que se confia en las raices
//  de esas autoridades en lugar de fijar una sola.
//
//  Para fijar UNA raiz concreta en una version publicada:
//      -DFLEX_CLOUD_ROOT_CA="\"-----BEGIN CERTIFICATE-----\\n...\""
//
//  Coste: ~16 KB de flash para las once raices. mbedTLS las analiza al abrir
//  cada conexion y las libera al cerrarla; no quedan en RAM.
// #############################################################
const char* flexCloudRootCA();

// #############################################################
//  SITIO PARA ABRIR UNA CONEXION TLS, Y POR QUE FALLO
//  ------------------------------------------------------------
//  mbedTLS reserva sus buffers de registro y analiza las raices de arriba en
//  la SRAM INTERNA. esp_get_free_heap_size() no sirve para decidir si caben:
//  suma los 32 MB de PSRAM del P4, asi que siempre dice "de sobra" aunque la
//  interna este en las ultimas (la guarda que tenia Flex Cloud nunca saltaba).
//  Aqui se mira lo que de verdad hace falta: la SRAM INTERNA libre y su mayor
//  bloque. Sin sitio no se abre la conexion: se dice y se reintenta, en vez de
//  dejar que mbedTLS falle a medias (un "-1" mudo) en el peor momento.
//
//  Los suelos son los del propio sistema (FlexOS_Ultra_Core: por debajo de
//  40 KB de interna entra el modo de proteccion) y se pueden cambiar con
//  -DFLEX_TLS_MIN_INTERNAL / -DFLEX_TLS_MIN_BLOCK.
// #############################################################
#ifndef FLEX_TLS_MIN_INTERNAL
#define FLEX_TLS_MIN_INTERNAL (40u * 1024u)
#endif
#ifndef FLEX_TLS_MIN_BLOCK
#define FLEX_TLS_MIN_BLOCK    (16u * 1024u)
#endif

#include <stddef.h>
#include <stdint.h>

// true si hay SRAM interna para abrir ahora una conexion TLS. Devuelve lo que
// midio (cualquiera de los dos punteros puede ser NULL).
bool flexTlsRoom(size_t* internalFree, size_t* largestBlock);

// Motivo corto y legible de un fallo de conexion TLS a partir de lo que
// WiFiClientSecure::lastError() devuelve (codigo de mbedTLS, negativo, o -1 =
// no hubo DNS, TCP o se agoto el tiempo; 0 o positivo = no fue un error de la
// conexion). Siempre escribe algo en `out`.
const char* flexTlsReason(int mbedtlsError, char* out, size_t cap);

// El codigo -1 de lastError() NO dice que fallo: WiFiClientSecure lo devuelve cuando no
// resuelve el nombre (DNS), cuando no abre el socket TCP (rechazo, sin ruta o tiempo) y
// cuando el saludo TLS no termina a tiempo. Lo que SI los distingue es el DNS (se
// comprueba aparte, una vez, tras el fallo) y cuanto tardo el intento:
//    < 3 s        el servidor rechaza la conexion o no hay ruta
//    ~ handshake  el saludo TLS no termino (la conexion TCP si se abrio)
//    ~ connect    el servidor no contesta por TCP
// Es una pista, no una prueba: por eso el texto lleva los segundos.
// `host` = el nombre del servidor (flexUrlHost); devuelve `out`.
bool flexTlsDnsOk(const char* host);
bool flexUrlHost(const char* url, char* out, size_t cap);
const char* flexTlsPhase(bool dnsOk, uint32_t elapsedMs, uint32_t handshakeMs, uint32_t connectMs, char* out, size_t cap);

#endif
