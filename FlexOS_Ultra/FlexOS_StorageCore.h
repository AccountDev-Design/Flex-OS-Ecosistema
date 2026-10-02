#ifndef FLEXOS_STORAGECORE_H
#define FLEXOS_STORAGECORE_H

// #############################################################
//  FLEX STORAGE · NUCLEO PORTABLE (lado P4)
//  ------------------------------------------------------------
//  Lo que Flex Storage DECIDE en el P4 sin tocar la red, la NVS ni la
//  pantalla: el telefono emparejado (y como se guarda), las ofertas de
//  un solo uso que se entregan al navegador, el emparejamiento con
//  ECDH P-256 + codigo de verificacion + aprobacion en pantalla, y el
//  reto-respuesta de las sesiones con el telefono.
//
//  Es el gemelo de `android/FlexPhone/storage/.../StorageCrypto.kt` y de
//  `AttachClient.kt`: lo que entra en cada HMAC tiene que coincidir byte a
//  byte, y las dos baterias (tests/host/test_storagecore.cpp y CoreTest.kt)
//  fijan LOS MISMOS vectores dorados (ECDH: RFC 5903).
//
//  Se compila y se ejecuta entero en el PC con sanitizers: lo que analiza
//  llega por la red local (de cualquiera que este en la misma Wi-Fi).
//  Sin Arduino, sin reservas dinamicas salvo las del backend de ECDH.
//
//  Ver docs/FLEX-STORAGE.md (secciones 4 y 5).
// #############################################################
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define FST_ID_MAX        40      // id de un extremo ("flexos-...", "a55-...")
#define FST_NAME_MAX      48      // nombre visible (UTF-8, recortado sin partir caracteres)
#define FST_MODEL_MAX     24
#define FST_KEY_SIZE      32
#define FST_POINT_SIZE    65      // 0x04 || X(32) || Y(32)
#define FST_HEX32         33      // 16 bytes en hexadecimal + 0 (oferta, id de emparejamiento, reto)
#define FST_TOKEN_MAX     49      // 24 bytes en hexadecimal + 0 (token de sesion del telefono)
#define FST_IP_MAX        16
#define FST_OFFERS         2      // ofertas vivas a la vez
#define FST_OFFER_TTL_MS  180000u // 3 min
#define FST_PAIR_TTL_MS   120000u // 2 min para aprobarlo en la pantalla
#define FST_PROOF_FAILS    3      // pruebas incorrectas antes de cancelar un emparejamiento
#define FST_TRIES          5      // fallos antes de la espera creciente
#define FST_DEFAULT_PORT  47830

// =====================================================================
//  El telefono emparejado (lo que se guarda en la NVS)
// =====================================================================
typedef struct {
  uint8_t  valid;
  uint8_t  enabled;                 // 0 = el usuario lo desconecto (sin olvidarlo)
  char     id[FST_ID_MAX];
  char     name[FST_NAME_MAX];
  char     model[FST_MODEL_MAX];
  uint8_t  key[FST_KEY_SIZE];       // clave del emparejamiento: NUNCA sale del P4
  char     ip[FST_IP_MAX];          // donde sirve Flex Cloud
  uint16_t port;
  uint32_t pairedEpoch;             // UTC (s); 0 = no se sabia la hora
} FstPhone;

#define FST_PHONE_BLOB_MAX 192
// Binario con firma, version y CRC32: un blob danado se descarta entero.
size_t fstPhoneEncode(const FstPhone* p, uint8_t* buf, size_t cap);
bool   fstPhoneDecode(FstPhone* p, const uint8_t* buf, size_t len);
// "http://<ip>:<puerto>/api/cloud" (false si el registro no vale).
bool   fstPhoneBaseUrl(const FstPhone* p, char* out, size_t cap);
// Borra la clave de la memoria y deja el registro vacio.
void   fstPhoneWipe(FstPhone* p);

// =====================================================================
//  Criptografia (gemela de StorageCrypto.kt)
// =====================================================================
void fstDeriveKey(const uint8_t z[32], const char* offer, const char* p4Id, const char* phoneId, uint8_t out[FST_KEY_SIZE]);
void fstSas(const uint8_t k[FST_KEY_SIZE], char out[7]);
void fstPhoneProof(const uint8_t k[FST_KEY_SIZE], const char* pairId, uint8_t out[32]);
void fstP4Proof(const uint8_t k[FST_KEY_SIZE], const char* pairId, uint8_t out[32]);
void fstKnownProof(const uint8_t k[FST_KEY_SIZE], const char* offer, uint8_t out[32]);
void fstSessionMac(const uint8_t k[FST_KEY_SIZE], const char* nonce, const char* p4Id, uint8_t out[32]);
void fstSessionOk(const uint8_t k[FST_KEY_SIZE], const char* nonce, const char* token, uint8_t out[32]);

void fstHex(const uint8_t* b, size_t n, char* out);                    // out: 2n+1
bool fstUnhex(const char* s, uint8_t* out, size_t n);                  // exactamente 2n cifras hex

// Fuente de azar del anfitrion (esp_fill_random en la placa).
typedef void (*FstRandFn)(void* ctx, uint8_t* out, size_t n);

// ECDH P-256. Backend: mbedTLS en la placa, OpenSSL en el PC (el mismo
// criterio que el SHA-256 de FlexOS_CloudCore).
typedef struct { uint8_t priv[32]; uint8_t pub[FST_POINT_SIZE]; } FstEcdh;
bool fstEcdhGenerate(FstEcdh* e, FstRandFn rnd, void* rctx);
// Solo para las pruebas (vectores del RFC 5903): clave privada fija.
bool fstEcdhFromPrivate(FstEcdh* e, const uint8_t priv[32]);
// Secreto compartido con la clave publica del otro lado, COMPROBANDO que el
// punto esta en la curva (un punto invalido es un ataque, no un error).
bool fstEcdhShared(const FstEcdh* e, const uint8_t peer[FST_POINT_SIZE], uint8_t z[32], FstRandFn rnd, void* rctx);
void fstEcdhWipe(FstEcdh* e);
// Nombre del backend que se compilo ("mbedtls" / "openssl").
const char* fstEcdhBackend();

// =====================================================================
//  Ofertas y emparejamiento (el lado P4)
// =====================================================================
enum { FSTP_NONE = 0, FSTP_PENDING, FSTP_APPROVED, FSTP_DENIED, FSTP_DONE };

typedef struct {
  char     offer[FST_HEX32];
  uint32_t expMs;
  uint8_t  live;
} FstOffer;

typedef struct {
  uint8_t  state;                   // FSTP_*
  uint8_t  needsApproval;           // 0 = telefono conocido que demostro su clave
  uint8_t  fails;                   // pruebas incorrectas
  char     pairId[FST_HEX32];
  char     phoneId[FST_ID_MAX];
  char     name[FST_NAME_MAX];
  char     model[FST_MODEL_MAX];
  char     ip[FST_IP_MAX];
  uint16_t port;
  uint8_t  key[FST_KEY_SIZE];
  char     sas[7];
  uint32_t startMs;
} FstPairing;

typedef struct {
  char       p4Id[FST_ID_MAX];
  char       p4Name[FST_NAME_MAX];
  FstOffer   offers[FST_OFFERS];
  FstPairing pair;                  // como mucho UNO en curso
  FstPhone   phone;                 // el emparejado (lo persiste el anfitrion)
  uint8_t    fails;                 // ofertas falsas seguidas (limitador)
  uint32_t   blockUntilMs;
} FstCore;

void fstInit(FstCore* c, const char* p4Id, const char* p4Name);

// Oferta nueva de un solo uso para la sesion web que la pide. Como mucho
// FST_OFFERS vivas (la mas vieja cede su sitio).
bool fstOfferNew(FstCore* c, uint32_t nowMs, FstRandFn rnd, void* rctx, char out[FST_HEX32]);

// Lo que manda la app al empezar (formulario de POST /api/fs/phone/pair).
typedef struct {
  const char* offer;
  const char* pid;
  const char* name;
  const char* model;
  const char* port;
  const char* pub;                  // 130 cifras hex
  const char* kp4;                  // opcional: el P4 con el que ya estaba emparejado
  const char* known;                // opcional: HMAC(clave anterior, "known" || oferta)
} FstPairReq;

typedef struct {
  char     pairId[FST_HEX32];
  char     pub[2 * FST_POINT_SIZE + 1];
  uint8_t  approve;                 // 1 = hay que aprobarlo en la pantalla
  uint32_t expiresS;
} FstPairResp;

// Empieza un emparejamiento. Devuelve el estado HTTP que hay que contestar:
// 202 (resp relleno), 400 (peticion mal formada o clave publica fuera de la
// curva), 403 (oferta que no vale o IP que no es de la red local), 429
// (demasiadas ofertas falsas: *waitS dice cuanto; una oferta buena pasa
// siempre) o 500 (ECDH sin memoria). `err` es el texto para la persona. La
// oferta se gasta al usarla y un emparejamiento anterior en curso se cancela.
int  fstPairBegin(FstCore* c, uint32_t nowMs, const FstPairReq* req, const char* peerIp,
                  FstRandFn rnd, void* rctx, FstPairResp* resp, char* err, size_t errCap, uint32_t* waitS);

// Sondeo de la app con su prueba. Devuelve 200 (estado en *state: PENDING o
// DONE; con DONE, proofOut = la prueba del P4), 403 (rechazado o prueba
// incorrecta), 404 (no existe) o 410 (caduco). *persist = true UNA vez: el
// telefono quedo emparejado y hay que guardar c->phone en la NVS.
int  fstPairPoll(FstCore* c, uint32_t nowMs, const char* pairId, const char* proofHex,
                 uint8_t* state, char proofOut[65], bool* persist, char* err, size_t errCap);

// La persona decide en la pantalla del P4.
bool fstPairDecide(FstCore* c, bool allow);
// Hay un emparejamiento esperando a que se apruebe (para pintarlo).
bool fstPairWaiting(FstCore* c, uint32_t nowMs);
// Cancela lo que este en curso (la hoja se cierra, el servidor se apaga).
void fstPairCancel(FstCore* c);

// =====================================================================
//  Sesion P4 -> telefono (reto-respuesta)
// =====================================================================
// {"nonce":"<32 hex>",...} de GET /api/fs/challenge.
bool   fstParseChallenge(const char* body, size_t len, char nonce[FST_HEX32]);
// Cuerpo JSON de POST /api/fs/session. 0 si no cabe.
size_t fstSessionBody(const FstCore* c, const char* nonce, char* out, size_t cap);
// Respuesta de POST /api/fs/session: token + PRUEBA del telefono, que se
// comprueba aqui (autenticacion mutua). false si no cuadra.
bool   fstParseSession(const char* body, size_t len, const uint8_t key[FST_KEY_SIZE], const char* nonce,
                       char token[FST_TOKEN_MAX], uint32_t* expiresS, char* name, size_t nameCap);

// =====================================================================
//  Utilidades
// =====================================================================
// Copia UTF-8 recortando sin partir un caracter y cambiando los bytes de
// control por espacios (lo que llega de la red se ensena en pantalla).
void fstCleanText(char* out, size_t cap, const char* in);
// "a.b.c.d" de la red LOCAL (10/8, 172.16/12, 192.168/16, 169.254/16,
// 100.64/10 y 127/8). Nada de nombres ni de IPs publicas.
bool fstLocalIpv4(const char* ip);

#endif
