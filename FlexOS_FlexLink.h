// #############################################################
// ##  FLEX LINK  -- protocolo de enlace Flex OS <-> Flex Phone
// ##  ---------------------------------------------------------
// ##  Capa de TRAMA, versionada, sobre un transporte no fiable y
// ##  orientado a paquetes pequenos (BLE GATT). Este fichero es
// ##  NUCLEO PURO: no incluye Arduino, no reserva memoria, no
// ##  bloquea y no habla con ninguna radio. Por eso se compila y
// ##  se prueba EN EL PC (tests/host/test_flexlink.cpp) con los
// ##  mismos bytes que van al aire.
// ##
// ##  Lo que este modulo SI hace:
// ##    - Enmarcado con magia, version, tipo, sesion y longitud.
// ##    - CRC16-CCITT sobre cabecera + carga: detecta corrupcion.
// ##    - Fragmentacion y reensamblaje con limites ESTRICTOS.
// ##    - Contador monotono por sentido: descarta repetidos y
// ##      paquetes fuera de orden (anti-replay).
// ##    - Validacion de TODOS los tamanos antes de copiar.
// ##
// ##  Lo que este modulo NO hace (a proposito):
// ##    - No cifra. El emparejamiento y el cifrado del enlace los
// ##      da el bonding BLE (LE Secure Connections) mas la clave
// ##      de sesion que negocia FlexOS_FlexPhone_Link. Aqui solo
// ##      se transporta y se valida.
// ##    - No envia frames del navegador. BLE es control; los
// ##      frames van por Wi-Fi (ver FLEX-PHONE.md).
// #############################################################
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// ---- Version del protocolo ------------------------------------
// Se envia en cada trama. Un extremo que reciba una version MAYOR
// que la suya responde FLNK_T_ERR con FLNK_E_VERSION y NO intenta
// interpretar la carga: asi una version futura nunca hace que un
// dispositivo viejo lea campos que no entiende.
#define FLNK_VERSION      1
#define FLNK_VERSION_MIN  1      // la mas antigua que este extremo acepta

// ---- Limites de tamano (ESTRICTOS) ----------------------------
// FLNK_MAX_FRAME es lo que cabe en una notificacion GATT con el MTU
// que se negocia de verdad (247 - 3 de cabecera ATT = 244). Si el
// MTU real es menor, flexLinkFragment() lo respeta: nunca se emite
// una trama mayor que el MTU negociado.
#define FLNK_HDR_SIZE     18
#define FLNK_MAX_FRAME    244
#define FLNK_MIN_FRAME    FLNK_HDR_SIZE
#define FLNK_MAX_PAYLOAD  (FLNK_MAX_FRAME - FLNK_HDR_SIZE)   // 226
// Mensaje completo (tras reensamblar). 2 KB sobra para una
// notificacion con titulo, cuerpo recortado y acciones; poner mas
// solo agranda el buffer de reensamblaje en RAM del P4.
#define FLNK_MAX_MESSAGE  2048
#define FLNK_MAX_FRAGS    16
// MTU minimo utilizable. Por debajo de esto no cabe cabecera + un
// byte de carga, asi que el enlace se declara inservible en vez de
// emitir tramas que el otro extremo no puede reensamblar.
#define FLNK_MIN_MTU      (FLNK_HDR_SIZE + 8)

// ---- Tipos de mensaje -----------------------------------------
// Numerados a mano y NUNCA reordenados: el numero va al aire. Un
// hueco es preferible a mover un valor existente.
enum {
  // -- enlace y sesion --
  FLNK_T_HELLO        = 0x01,  // telefono -> Flex OS: me presento
  FLNK_T_WELCOME      = 0x02,  // Flex OS -> telefono: sesion abierta
  FLNK_T_PING         = 0x03,
  FLNK_T_PONG         = 0x04,
  FLNK_T_BYE          = 0x05,  // desconexion limpia
  FLNK_T_ACK          = 0x06,
  FLNK_T_ERR          = 0x07,
  // -- emparejamiento --
  FLNK_T_PAIR_REQ     = 0x10,  // telefono pide emparejar
  FLNK_T_PAIR_CODE    = 0x11,  // Flex OS -> telefono: muestro este codigo
  FLNK_T_PAIR_CONFIRM = 0x12,  // ambos confirman
  FLNK_T_UNPAIR       = 0x13,  // olvidar y borrar claves
  // -- notificaciones --
  FLNK_T_NOTIF_ADD    = 0x20,
  FLNK_T_NOTIF_UPDATE = 0x21,
  FLNK_T_NOTIF_REMOVE = 0x22,
  FLNK_T_NOTIF_CLEAR  = 0x23,
  // -- respuestas y acciones --
  FLNK_T_REPLY_REQ    = 0x30,  // Flex OS -> telefono: responde esto
  FLNK_T_REPLY_RESULT = 0x31,  // telefono -> Flex OS: exito o error REAL
  FLNK_T_ACTION_REQ   = 0x32,
  FLNK_T_ACTION_RESULT= 0x33,
  // -- estado del telefono --
  FLNK_T_PHONE_STATE  = 0x40,  // bateria, carga, nombre, red
  FLNK_T_TIME_SYNC    = 0x41,
  FLNK_T_FIND_START   = 0x42,
  FLNK_T_FIND_STOP    = 0x43,
  // -- multimedia --
  FLNK_T_MEDIA_STATE  = 0x50,
  FLNK_T_MEDIA_CMD    = 0x51,
  // -- Browser Relay --
  FLNK_T_RELAY_START  = 0x60,  // Flex OS pide al telefono levantar el relay
  FLNK_T_RELAY_STOP   = 0x61,
  FLNK_T_RELAY_INFO   = 0x62,  // telefono -> Flex OS: ip, puerto, version, caps
};

// ---- Codigos de error -----------------------------------------
enum {
  FLNK_E_NONE      = 0,
  FLNK_E_VERSION   = 1,   // version de protocolo incompatible
  FLNK_E_CRC       = 2,
  FLNK_E_TOOBIG    = 3,
  FLNK_E_BADFRAG   = 4,
  FLNK_E_TIMEOUT   = 5,
  FLNK_E_NOTPAIRED = 6,
  FLNK_E_NOREPLY   = 7,   // la notificacion no admite RemoteInput
  FLNK_E_GONE      = 8,   // la notificacion ya no existe / accion caducada
  FLNK_E_DENIED    = 9,   // permiso revocado en Android
  FLNK_E_BUSY      = 10,
  FLNK_E_INTERNAL  = 11,
};
const char* flexLinkErrName(uint8_t code);

// ---- Resultado de parseo --------------------------------------
enum {
  FLNK_OK          = 0,
  FLNK_ERR_SHORT   = -1,   // no llega ni a cabecera
  FLNK_ERR_MAGIC   = -2,
  FLNK_ERR_VERSION = -3,
  FLNK_ERR_LEN     = -4,   // longitud declarada imposible
  FLNK_ERR_CRC     = -5,
  FLNK_ERR_FRAG    = -6,
};

// Cabecera ya decodificada. Los campos estan en orden de aire.
typedef struct {
  uint8_t  version;
  uint8_t  type;
  uint16_t session;
  uint16_t packet;    // identifica el MENSAJE (todos sus fragmentos lo comparten)
  uint8_t  frag;      // indice de fragmento, 0..fragCount-1
  uint8_t  fragCount; // total de fragmentos del mensaje (>=1)
  uint16_t len;       // bytes de carga EN ESTA trama
  uint32_t counter;   // monotono por sentido: anti-repeticion
} FlexLinkHeader;

// =============================================================
//  CRC y enmarcado
// =============================================================

// CRC16-CCITT (poly 0x1021, init 0xFFFF). Detecta la corrupcion
// que el CRC del propio BLE deja pasar cuando el fallo ocurre por
// encima del enlace (buffer reutilizado, fragmento mezclado).
uint16_t flexLinkCrc16(const uint8_t* data, size_t n);

// Escribe UNA trama completa en `out`. Devuelve los bytes escritos
// o <0 si no cabe o los argumentos son imposibles.
// `payload` puede ser NULL si payloadLen es 0.
int flexLinkWriteFrame(uint8_t* out, size_t outN,
                       const FlexLinkHeader* h,
                       const uint8_t* payload, size_t payloadLen);

// Lee y VALIDA una trama. Comprueba magia, version, longitudes y
// CRC antes de tocar nada. `out` solo se rellena si devuelve FLNK_OK.
// `payloadOut` apunta DENTRO de `in` (no copia): valido mientras
// `in` lo sea.
int flexLinkReadFrame(const uint8_t* in, size_t n,
                      FlexLinkHeader* out,
                      const uint8_t** payloadOut, size_t* payloadLenOut);

// Cuantos fragmentos hacen falta para `msgLen` bytes con `mtu`.
// Devuelve 0 si no es representable (mtu demasiado pequeno o
// mensaje demasiado grande).
int flexLinkFragCount(size_t msgLen, size_t mtu);

// =============================================================
//  Reensamblador
// =============================================================
// Un solo mensaje en vuelo por sentido: es lo que necesita este
// enlace y evita una tabla de parciales que un emisor hostil
// podria llenar. Un packet id distinto ABANDONA el parcial
// anterior (y lo cuenta), no lo mezcla.
typedef struct {
  uint8_t  buf[FLNK_MAX_MESSAGE];
  uint32_t seenMask;      // bit i = fragmento i recibido (FLNK_MAX_FRAGS <= 32)
  uint16_t packet;
  uint16_t len;           // bytes utiles acumulados
  uint8_t  fragCount;
  bool     active;
  uint32_t startedMs;
  // -- contadores de diagnostico (nunca contenido) --
  uint32_t nBadCrc, nTooBig, nDup, nOutOfOrder, nAbandoned, nOk;
} FlexLinkReasm;

void flexLinkReasmInit(FlexLinkReasm* r);

// Resultado de alimentar una trama al reensamblador.
enum {
  FLNK_R_NEED_MORE = 0,   // aceptada, faltan fragmentos
  FLNK_R_DONE      = 1,   // mensaje completo en r->buf / r->len
  FLNK_R_DROP      = -1,  // descartada (dup, fuera de orden, corrupta)
};

// Alimenta UNA trama ya validada por flexLinkReadFrame.
// `nowMs` sirve para caducar un parcial a medias.
int flexLinkReasmFeed(FlexLinkReasm* r, const FlexLinkHeader* h,
                      const uint8_t* payload, size_t payloadLen,
                      uint32_t nowMs);

// Caduca el parcial si lleva mas de `timeoutMs` sin completarse.
// Devuelve true si ha abandonado algo.
bool flexLinkReasmExpire(FlexLinkReasm* r, uint32_t nowMs, uint32_t timeoutMs);

// =============================================================
//  Ventana anti-repeticion
// =============================================================
// El contador es monotono creciente por sentido. Se acepta un
// contador MAYOR que el ultimo visto, o dentro de una ventana
// hacia atras que NO se haya usado ya (BLE puede reordenar).
typedef struct {
  uint32_t highest;
  uint32_t mask;      // bit i = (highest - 1 - i) ya visto
  bool     primed;
} FlexLinkAntiReplay;

void flexLinkAntiReplayInit(FlexLinkAntiReplay* a);
// true = contador nuevo y aceptable (y queda marcado como visto).
// false = repetido o demasiado viejo -> descartar la trama.
bool flexLinkAntiReplayCheck(FlexLinkAntiReplay* a, uint32_t counter);

// =============================================================
//  Reintentos con espera progresiva
// =============================================================
// Politica compartida por el transporte y por el relay: sin
// delay(), solo devuelve CUANDO toca el siguiente intento.
#define FLNK_RETRY_MAX      5
#define FLNK_RETRY_BASE_MS  500u
#define FLNK_RETRY_CAP_MS   30000u
// Devuelve los ms a esperar antes del intento `attempt` (0-based),
// con tope. attempt >= FLNK_RETRY_MAX -> 0 (rendirse).
uint32_t flexLinkRetryDelayMs(uint8_t attempt);

// =============================================================
//  Ayudas de carga: escritura/lectura segura de campos
// =============================================================
// Un escritor con cursor que NUNCA se pasa del final. Todas las
// funciones comprueban antes de copiar; si algo no cabe, marca
// `ovf` y las siguientes son no-op. Asi un mensaje truncado se
// detecta con UNA comprobacion al final, no en cada campo.
typedef struct { uint8_t* p; size_t n, at; bool ovf; } FlexLinkWr;
void flexLinkWrInit(FlexLinkWr* w, uint8_t* buf, size_t n);
void flexLinkWrU8 (FlexLinkWr* w, uint8_t v);
void flexLinkWrU16(FlexLinkWr* w, uint16_t v);
void flexLinkWrU32(FlexLinkWr* w, uint32_t v);
void flexLinkWrBytes(FlexLinkWr* w, const void* src, size_t n);
// Cadena con prefijo de longitud de 1 byte (max 255). El texto se
// TRUNCA de forma segura en frontera UTF-8 si no cabe.
void flexLinkWrStr(FlexLinkWr* w, const char* s, size_t maxLen);
bool flexLinkWrOk(const FlexLinkWr* w);

typedef struct { const uint8_t* p; size_t n, at; bool ovf; } FlexLinkRd;
void     flexLinkRdInit(FlexLinkRd* r, const uint8_t* buf, size_t n);
uint8_t  flexLinkRdU8 (FlexLinkRd* r);
uint16_t flexLinkRdU16(FlexLinkRd* r);
uint32_t flexLinkRdU32(FlexLinkRd* r);
void     flexLinkRdBytes(FlexLinkRd* r, void* dst, size_t n);
// Lee una cadena con prefijo de longitud a un buffer de tamano fijo.
// SIEMPRE deja `out` terminada en 0. Trunca en frontera UTF-8.
void     flexLinkRdStr(FlexLinkRd* r, char* out, size_t outN);
bool     flexLinkRdOk(const FlexLinkRd* r);

// =============================================================
//  UTF-8
// =============================================================
// Copia `src` en `out` (buffer de outN bytes, incluido el 0 final)
// truncando SIN partir un caracter multibyte por la mitad. Devuelve
// los bytes escritos (sin contar el 0). Una secuencia invalida se
// corta ahi: nunca se propaga UTF-8 roto al resto del sistema.
size_t flexLinkUtf8Copy(char* out, size_t outN, const char* src);
// Longitud en bytes de un prefijo valido de `src` que no pase de
// `maxBytes` y no parta un caracter.
size_t flexLinkUtf8Trunc(const char* src, size_t maxBytes);
