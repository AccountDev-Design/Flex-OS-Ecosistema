// #############################################################
// ##  FLEX AUTH  -- emparejamiento y sesion de Flex Phone
// ##  ---------------------------------------------------------
// ##  Existe por un motivo concreto: al pasar el enlace de BLE a
// ##  Wi-Fi se pierde el bonding. BLE cifraba y autenticaba el
// ##  enlace por debajo; un socket TCP en la red local NO hace
// ##  nada de eso. Sin esta capa, cualquiera que conozca la IP del
// ##  telefono podria pedirle las notificaciones.
// ##
// ##  QUE HACE
// ##    · SHA-256 y HMAC-SHA256 en C puro, sin mbedtls ni
// ##      OpenSSL, para que el MISMO codigo se compile y se pruebe
// ##      en el PC y en la placa.
// ##    · Deriva la clave de vinculo a partir del codigo de 6
// ##      digitos. El codigo NUNCA viaja por la red.
// ##    · Reto-respuesta MUTUO por sesion: los dos extremos
// ##      demuestran que tienen la clave sin enviarla.
// ##
// ##  QUE **NO** HACE, y hay que decirlo claro
// ##    · NO cifra la carga. Las notificaciones viajan en claro por
// ##      la red local, igual que ya hacian los fotogramas del
// ##      Browser Relay. Lo que esta capa impide es que un
// ##      dispositivo NO emparejado abra sesion; no protege frente
// ##      a alguien que ya este escuchando el mismo cable.
// ##    · NO sustituye a TLS. La interfaz lo dice tal cual.
// ##
// ##  NUCLEO PURO: sin Arduino, sin reservas dinamicas, sin
// ##  bloqueos. Se prueba entero en el PC.
// #############################################################
#pragma once
#include <stdint.h>
#include <stddef.h>

// =============================================================
//  SHA-256
// =============================================================
#define FLXA_SHA256_SIZE  32
#define FLXA_BLOCK_SIZE   64

typedef struct {
  uint32_t state[8];
  uint64_t bits;
  uint8_t  buf[FLXA_BLOCK_SIZE];
  size_t   at;
} FlexSha256;

void flexSha256Init(FlexSha256* c);
void flexSha256Update(FlexSha256* c, const void* data, size_t n);
// Deja 32 bytes en `out`. El contexto queda inservible despues.
void flexSha256Final(FlexSha256* c, uint8_t out[FLXA_SHA256_SIZE]);
// Atajo de una sola llamada.
void flexSha256(const void* data, size_t n, uint8_t out[FLXA_SHA256_SIZE]);

// =============================================================
//  HMAC-SHA256  (RFC 2104)
// =============================================================
void flexHmacSha256(const uint8_t* key, size_t keyN,
                    const void* msg, size_t msgN,
                    uint8_t out[FLXA_SHA256_SIZE]);

// Comparacion en TIEMPO CONSTANTE. Un memcmp normal sale en el
// primer byte distinto, y ese tiempo se puede medir para adivinar
// un MAC byte a byte. Aqui no.
bool flexAuthEqual(const uint8_t* a, const uint8_t* b, size_t n);

// =============================================================
//  Material del vinculo
// =============================================================
#define FLXA_KEY_SIZE     32     // clave de vinculo persistente
#define FLXA_NONCE_SIZE   16     // reto de sesion
#define FLXA_SALT_SIZE    16     // sal del emparejamiento
#define FLXA_PROOF_SIZE   32     // respuesta al reto
#define FLXA_ID_MAX       32     // identificador de un extremo

// Identidad de un extremo del enlace. Es un texto corto y estable
// (Flex OS usa su id de dispositivo; Android, uno propio que
// genera al instalarse). NO es un dato sensible: entra en la
// derivacion para que dos emparejamientos distintos con el mismo
// codigo no den la misma clave.
typedef struct {
  char id[FLXA_ID_MAX];
} FlexAuthPeer;

// #############################################################
// ##  DERIVACION DE LA CLAVE DE VINCULO
// ##  ------------------------------------------------------
// ##  clave = HMAC( codigo , "flexphone-pair-v2" || sal ||
// ##                         idFlexOS || idTelefono )
// ##
// ##  Los DOS extremos la calculan por su cuenta. Por la red solo
// ##  viajan la sal (publica) y los identificadores (publicos): el
// ##  codigo de 6 digitos lo teclea el usuario en un lado y lo lee
// ##  en el otro, y nunca se transmite.
// ##
// ##  Seis digitos es poca entropia, y por eso NO basta con la
// ##  derivacion: el codigo caduca a los dos minutos y cada
// ##  emparejamiento genera una sal nueva, asi que no hay ventana
// ##  practica para probarlos todos contra un extremo vivo.
// #############################################################
void flexAuthDeriveKey(const char* code,
                       const uint8_t salt[FLXA_SALT_SIZE],
                       const char* flexosId, const char* phoneId,
                       uint8_t out[FLXA_KEY_SIZE]);

// #############################################################
// ##  RETO-RESPUESTA DE SESION (MUTUO)
// ##  ------------------------------------------------------
// ##  El telefono demuestra que tiene la clave, y Flex OS tambien.
// ##  Las dos pruebas usan etiquetas DISTINTAS sobre el mismo
// ##  reto, asi que reenviar la prueba ajena no sirve de nada:
// ##
// ##      pruebaTelefono = HMAC(clave, "flexphone-peer-v2"  || reto || sesion)
// ##      pruebaFlexOS   = HMAC(clave, "flexphone-host-v2"  || reto || sesion)
// ##
// ##  Sin la parte de Flex OS, un equipo cualquiera de la red
// ##  podria hacerse pasar por el reloj y quedarse con todas las
// ##  notificaciones del usuario. Autenticar en un solo sentido
// ##  seria justo la mitad util.
// #############################################################
enum { FLXA_ROLE_PHONE = 0, FLXA_ROLE_HOST = 1 };

void flexAuthProof(const uint8_t key[FLXA_KEY_SIZE], uint8_t role,
                   const uint8_t nonce[FLXA_NONCE_SIZE], uint16_t session,
                   uint8_t out[FLXA_PROOF_SIZE]);

// Verifica una prueba recibida. Tiempo constante.
bool flexAuthVerify(const uint8_t key[FLXA_KEY_SIZE], uint8_t role,
                    const uint8_t nonce[FLXA_NONCE_SIZE], uint16_t session,
                    const uint8_t got[FLXA_PROOF_SIZE]);

// #############################################################
// ##  CODIGO DE EMPAREJAMIENTO
// ##  ------------------------------------------------------
// ##  Seis digitos a partir de una fuente de aleatoriedad del
// ##  llamador (esp_random en la placa, SecureRandom en Android).
// ##  Se pide un uint32 entero y se reduce por modulo; el sesgo
// ##  que introduce el modulo sobre 10^6 es despreciable frente a
// ##  2^32 y este codigo caduca en dos minutos.
// #############################################################
void flexAuthFormatCode(uint32_t rnd, char out[7]);

// Rellena `out` con bytes a partir de una funcion de aleatoriedad
// que da 32 bits por llamada. Existe para que el nucleo no dependa
// de ninguna API concreta de aleatoriedad.
typedef uint32_t (*FlexAuthRandFn)(void);
void flexAuthRandomBytes(FlexAuthRandFn rnd, uint8_t* out, size_t n);
