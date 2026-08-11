// #############################################################
// ##  FlexOS · FLEX VAULT  ·  implementacion
// ##  Cifrado autenticado, claves, almacen y registro.
// ##  Ver FlexOS_Vault.h para el porque de cada decision.
// #############################################################
//
//  REGLA DE ORO DE ESTE FICHERO
//  ----------------------------
//  1. Ninguna clave, contrasena, nombre privado ni byte de
//     contenido se imprime NUNCA por el puerto serie. Ni en
//     depuracion. Los errores se cuentan como codigo y como texto
//     generico ("clave incorrecta"), jamas con datos.
//  2. Todo lo que se descifra se libera y se BORRA (flexVaultWipe)
//     en todos los caminos de salida, tambien en los de error.
//  3. Si una operacion no se puede completar, no se deja el almacen
//     a medias: se limpia lo escrito y se devuelve false. Un
//     "elemento a medio cifrar" seria un fichero perdido.

#include "FlexOS_Vault.h"
#include "FlexOS_FS.h"
#include <Preferences.h>

// -------------------------------------------------------------
//  BACKEND CRIPTOGRAFICO
//  ------------------------------------------------------------
//  En la placa: mbedTLS, tal como viene en el core de arduino-esp32.
//  Es la implementacion del fabricante y usa el ACELERADOR AES y SHA
//  del ESP32-P4, asi que ni se reimplementa AES a mano ni se pierde
//  el hardware.
//
//  En las pruebas de host: OpenSSL. No es el codigo que corre en la
//  placa, y eso hay que decirlo: lo que las pruebas verifican de
//  verdad es la LOGICA de esta capa -- el esquema de sobre, la
//  derivacion por fichero, el troceado, el formato en disco, la
//  migracion, el contador de intentos y que un byte cambiado se
//  detecte. La primitiva AES-GCM la firma una biblioteca auditada en
//  los dos casos.
//
//  Las tres funciones de mbedTLS que se usan (mbedtls_md,
//  mbedtls_md_hmac, mbedtls_gcm_*) tienen el MISMO nombre y la misma
//  firma en mbedTLS 2.x y 3.x, a proposito: asi el modulo no se rompe
//  con una actualizacion del core. Las variantes con sufijo _ret y
//  las de pkcs5 cambiaron entre versiones y no se usan.
// -------------------------------------------------------------
#if defined(__has_include)
#  if __has_include(<mbedtls/gcm.h>)
#    define FXV_BACKEND_MBEDTLS 1
#  elif __has_include(<openssl/evp.h>)
#    define FXV_BACKEND_OPENSSL 1
#  endif
#endif

#if !defined(FXV_BACKEND_MBEDTLS) && !defined(FXV_BACKEND_OPENSSL)
#  error "Flex Vault necesita mbedTLS (placa) u OpenSSL (pruebas de host)"
#endif

#if FXV_BACKEND_MBEDTLS
#  include <mbedtls/gcm.h>
#  include <mbedtls/md.h>
#  include <esp_random.h>
#else
#  include <openssl/evp.h>
#  include <openssl/hmac.h>
#  include <openssl/sha.h>
#  include <openssl/rand.h>
#  include <strings.h>          // strcasecmp (en la placa lo trae Arduino.h)
#endif

#define FXV_KEY_LEN   32          // AES-256
#define FXV_NONCE_LEN 12          // el tamano recomendado para GCM
#define FXV_TAG_LEN   16
#define FXV_SALT_LEN  16
#define FXV_WSALT_LEN 8           // sal de escritura (una por reescritura)
#define FXV_HDR_LEN   24          // cabecera de un blob cifrado

// Sobre de la clave maestra guardado en NVS: nonce + envuelto + tag.
#define FXV_ENV_LEN   (FXV_NONCE_LEN + FXV_KEY_LEN + FXV_TAG_LEN)   // 60

// Marcas de clase para los contenedores internos (no son contenido
// del usuario, por eso van fuera del rango de FXV_KIND_*).
#define FXV_KIND_INDEX 0xFE
#define FXV_KIND_LOG   0xFD

// -------------------------------------------------------------
//  PRIMITIVAS
// -------------------------------------------------------------
void flexVaultWipe(void* p, size_t n){
  // volatile para que el compilador NO pueda eliminar el borrado por
  // "nadie lee esto despues". Es el fallo clasico de un memset de
  // limpieza: se optimiza y la clave se queda en la pila.
  if(!p) return;
  volatile uint8_t* q = (volatile uint8_t*)p;
  while(n--) *q++ = 0;
}

bool flexVaultEqualCT(const void* a, const void* b, size_t n){
  const uint8_t* x = (const uint8_t*)a;
  const uint8_t* y = (const uint8_t*)b;
  uint8_t d = 0;
  for(size_t i = 0; i < n; i++) d |= (uint8_t)(x[i] ^ y[i]);
  return d == 0;
}

void flexVaultRandomBytes(void* out, size_t n){
#if FXV_BACKEND_MBEDTLS
  // esp_fill_random usa el generador de hardware del chip. En el
  // ESP32-P4 se alimenta de ruido fisico, no de un PRNG con semilla
  // fija, que es la condicion para que una clave maestra sea
  // realmente impredecible.
  esp_fill_random(out, n);
#else
  RAND_bytes((unsigned char*)out, (int)n);
#endif
}

static void fxvHmac(const uint8_t* key, size_t keyLen,
                    const uint8_t* in, size_t inLen, uint8_t out[32]){
#if FXV_BACKEND_MBEDTLS
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                  key, keyLen, in, inLen, out);
#else
  unsigned int n = 32;
  HMAC(EVP_sha256(), key, (int)keyLen, in, inLen, out, &n);
#endif
}

// PBKDF2-HMAC-SHA256 con dkLen <= 32 (un solo bloque). Se
// implementa aqui, sobre HMAC, en vez de llamar a la API pkcs5 de
// mbedTLS: esa cambio de nombre entre la 2.x y la 3.x y no merece la
// pena atar el arranque del sistema a eso. Son doce lineas y el
// resultado es identico, comprobado contra vectores en las pruebas.
void flexVaultKdf(const char* secret, const uint8_t* salt, size_t saltLen,
                  uint32_t iters, uint8_t* out, size_t outLen){
  if(!out || outLen == 0) return;
  if(outLen > 32) outLen = 32;
  if(iters == 0) iters = 1;
  size_t sl = secret ? strlen(secret) : 0;

  // U1 = HMAC(clave, sal || 0x00000001)
  uint8_t blk[FXV_SALT_LEN + 4 + 32];
  size_t  bl = saltLen > sizeof(blk) - 4 ? sizeof(blk) - 4 : saltLen;
  memcpy(blk, salt, bl);
  blk[bl + 0] = 0; blk[bl + 1] = 0; blk[bl + 2] = 0; blk[bl + 3] = 1;

  uint8_t u[32], acc[32];
  fxvHmac((const uint8_t*)secret, sl, blk, bl + 4, u);
  memcpy(acc, u, 32);
  for(uint32_t i = 1; i < iters; i++){
    fxvHmac((const uint8_t*)secret, sl, u, 32, u);
    for(int k = 0; k < 32; k++) acc[k] ^= u[k];
  }
  memcpy(out, acc, outLen);
  flexVaultWipe(u, sizeof(u));
  flexVaultWipe(acc, sizeof(acc));
  flexVaultWipe(blk, sizeof(blk));
}

// AES-256-GCM. `aad` se autentica pero no se cifra: es donde va la
// cabecera del bloque, para que nadie pueda recolocar bloques de un
// fichero en otro ni recortar el final sin que se note.
static bool fxvGcmEncrypt(const uint8_t key[FXV_KEY_LEN], const uint8_t* nonce,
                          const uint8_t* aad, size_t aadLen,
                          const uint8_t* in, size_t len,
                          uint8_t* out, uint8_t tag[FXV_TAG_LEN]){
#if FXV_BACKEND_MBEDTLS
  mbedtls_gcm_context g;
  mbedtls_gcm_init(&g);
  bool ok = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, FXV_KEY_LEN * 8) == 0;
  if(ok) ok = mbedtls_gcm_crypt_and_tag(&g, MBEDTLS_GCM_ENCRYPT, len,
                                        nonce, FXV_NONCE_LEN, aad, aadLen,
                                        in, out, FXV_TAG_LEN, tag) == 0;
  mbedtls_gcm_free(&g);
  return ok;
#else
  EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
  if(!c) return false;
  bool ok = EVP_EncryptInit_ex(c, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1 &&
            EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, FXV_NONCE_LEN, NULL) == 1 &&
            EVP_EncryptInit_ex(c, NULL, NULL, key, nonce) == 1;
  int outl = 0;
  if(ok && aadLen) ok = EVP_EncryptUpdate(c, NULL, &outl, aad, (int)aadLen) == 1;
  if(ok && len)    ok = EVP_EncryptUpdate(c, out, &outl, in, (int)len) == 1;
  int fl = 0;
  if(ok) ok = EVP_EncryptFinal_ex(c, out + outl, &fl) == 1;
  if(ok) ok = EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_GET_TAG, FXV_TAG_LEN, tag) == 1;
  EVP_CIPHER_CTX_free(c);
  return ok;
#endif
}

static bool fxvGcmDecrypt(const uint8_t key[FXV_KEY_LEN], const uint8_t* nonce,
                          const uint8_t* aad, size_t aadLen,
                          const uint8_t* in, size_t len,
                          const uint8_t tag[FXV_TAG_LEN], uint8_t* out){
#if FXV_BACKEND_MBEDTLS
  mbedtls_gcm_context g;
  mbedtls_gcm_init(&g);
  bool ok = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, FXV_KEY_LEN * 8) == 0;
  if(ok) ok = mbedtls_gcm_auth_decrypt(&g, len, nonce, FXV_NONCE_LEN,
                                       aad, aadLen, tag, FXV_TAG_LEN,
                                       in, out) == 0;
  mbedtls_gcm_free(&g);
  return ok;
#else
  EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
  if(!c) return false;
  bool ok = EVP_DecryptInit_ex(c, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1 &&
            EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, FXV_NONCE_LEN, NULL) == 1 &&
            EVP_DecryptInit_ex(c, NULL, NULL, key, nonce) == 1;
  int outl = 0;
  if(ok && aadLen) ok = EVP_DecryptUpdate(c, NULL, &outl, aad, (int)aadLen) == 1;
  if(ok && len)    ok = EVP_DecryptUpdate(c, out, &outl, in, (int)len) == 1;
  if(ok) ok = EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_TAG, FXV_TAG_LEN, (void*)tag) == 1;
  int fl = 0;
  if(ok) ok = EVP_DecryptFinal_ex(c, out + outl, &fl) == 1;   // aqui falla si el tag no cuadra
  EVP_CIPHER_CTX_free(c);
  return ok;
#endif
}

// -------------------------------------------------------------
//  ESTADO DEL MODULO
// -------------------------------------------------------------
#define FXV_NS "fxvault"

static Preferences vprefs;

// --- sobre y ajustes (viven en NVS) ---
static bool     vExists   = false;
static int      vLockType = FLEXVAULT_LOCK_NONE;
static int      vSecretLen= 0;
static uint32_t vIters    = FLEXVAULT_KDF_ITERS;
static uint8_t  vSalt[FXV_SALT_LEN];
static uint8_t  vEnv[FXV_ENV_LEN];
static int      vFails    = 0;
static uint32_t vLastAcc  = 0;
static uint32_t vAutoLock = 60000;              // 1 min por defecto
static uint16_t vAppMask  = 0;                  // apps anadidas
static uint16_t vAppLock  = 0;                  // apps con candado propio
static uint32_t vAppTouch[16];

// --- clave maestra: SOLO en RAM y SOLO mientras esta abierta ---
static uint8_t  vMaster[FXV_KEY_LEN];
static bool     vOpen    = false;

// --- espera por intentos fallidos ---
// Mismo criterio que el bloqueo del sistema: el CONTADOR vive en NVS
// (reiniciar no perdona nada) y la espera se cobra al abrir la
// pantalla. vPenaltyServed arranca en false en cada arranque, asi que
// tras un reinicio el castigo se aplica una vez y no se repite cada
// vez que se entra y se sale.
static bool     vPenServed = false;
static uint32_t vPenUntil  = 0;

static uint32_t vClock = 0;                     // epoch que da el sketch
static const char* vErr = "";

// Indice en RAM. Solo existe mientras la boveda esta abierta.
struct FxvRec {
  uint16_t id;
  int8_t   kind;
  int8_t   appId;
  uint32_t size;
  uint32_t added;
  uint32_t lastAcc;
  char     name[FLEXVAULT_NAME_MAX];
  char     origin[FLEXVAULT_ORIGIN_MAX];
};
static FxvRec*  vIdx    = NULL;
static int      vIdxN   = 0;
static uint16_t vNextId = 1;

// Registro de seguridad en RAM (anillo). Se escribe cifrado en cada
// cambio: son 512 bytes, no compensa acumular.
static FlexVaultLog vLog[FLEXVAULT_LOG_MAX];
static int          vLogN = 0;

// Eventos ocurridos CON LA BOVEDA CERRADA (intentos fallidos, sobre
// todo). Sin clave no se pueden cifrar, asi que esperan aqui, en
// orden de llegada, hasta la proxima apertura correcta -- que es el
// primer momento en que se pueden escribir. Tirarlos seria perder
// justo la informacion que mas importa: cuantas veces intento entrar
// alguien mientras el duenno no estaba.
#define FXV_PEND_MAX 8
static FlexVaultLog vPend[FXV_PEND_MAX];
static int          vPendN = 0;

const char* flexVaultError(){ return vErr; }
void        flexVaultSetClock(uint32_t epoch){ vClock = epoch; }
uint32_t    flexVaultNow(){ return vClock; }

bool     flexVaultExists(){ return vExists; }
bool     flexVaultUnlocked(){ return vOpen; }
int      flexVaultLockType(){ return vExists ? vLockType : FLEXVAULT_LOCK_NONE; }
int      flexVaultSecretLen(){ return vLockType == FLEXVAULT_LOCK_PIN ? vSecretLen : 0; }
uint32_t flexVaultAutoLockMs(){ return vAutoLock; }
uint32_t flexVaultLastAccess(){ return vLastAcc; }
int      flexVaultFails(){ return vFails; }

// -------------------------------------------------------------
//  RUTAS DEL ALMACEN
//  ------------------------------------------------------------
//  Los nombres de fichero son NUMEROS. No es pereza: el nombre de un
//  fichero no se puede cifrar (el sistema de archivos lo guarda tal
//  cual), asi que un "foto-dni.jpg.fxv" delataria el contenido con
//  solo volcar la flash. El nombre real vive cifrado en el indice.
// -------------------------------------------------------------
static void vaultBlobPath(uint16_t id, char* out, size_t n){
  snprintf(out, n, "%s/%u.b", FLEXFS_DIR_VAULTD, (unsigned)id);
}
#define FXV_IDX_PATH FLEXFS_DIR_VAULT "/i"
#define FXV_LOG_PATH FLEXFS_DIR_VAULT "/g"

// -------------------------------------------------------------
//  DERIVACION DE CLAVES DE TRABAJO
//  ------------------------------------------------------------
//  Cada contenedor tiene su propia clave, derivada de la maestra con
//  una etiqueta de proposito, su id y la SAL DE ESCRITURA. La sal de
//  escritura se sortea en CADA reescritura: por eso la clave cambia
//  con cada guardado y los nonces (que empiezan en 0) nunca se
//  repiten con la misma clave. El reuso de nonce es LA forma de
//  romper GCM, y asi no puede ocurrir por construccion.
// -------------------------------------------------------------
static void fxvDeriveKey(uint8_t kind, uint16_t id, const uint8_t wsalt[FXV_WSALT_LEN],
                         uint8_t out[FXV_KEY_LEN]){
  uint8_t info[11 + 1 + 2 + FXV_WSALT_LEN];
  memcpy(info, "fxv-key-v1", 10);
  info[10] = kind;
  info[11] = (uint8_t)(id & 0xFF);
  info[12] = (uint8_t)(id >> 8);
  memcpy(info + 13, wsalt, FXV_WSALT_LEN);
  uint8_t h[32];
  fxvHmac(vMaster, FXV_KEY_LEN, info, 13 + FXV_WSALT_LEN, h);
  memcpy(out, h, FXV_KEY_LEN);
  flexVaultWipe(h, sizeof(h));
}

static void fxvNonce(uint32_t chunk, uint8_t out[FXV_NONCE_LEN]){
  memset(out, 0, FXV_NONCE_LEN);
  out[4] = (uint8_t)(chunk & 0xFF);
  out[5] = (uint8_t)((chunk >> 8) & 0xFF);
  out[6] = (uint8_t)((chunk >> 16) & 0xFF);
  out[7] = (uint8_t)((chunk >> 24) & 0xFF);
}

// -------------------------------------------------------------
//  CONTENEDOR CIFRADO
//  ------------------------------------------------------------
//  Formato (todo little-endian):
//     0  'F','X','V','1'
//     4  version
//     5  clase
//     6  banderas (reservado, 0)
//     7  reservado
//     8  id (u16)
//    10  bytes de plano por bloque (u16)
//    12  bytes de plano totales (u32)
//    16  sal de escritura (8 B)
//    24  bloques: [tag 16][cifrado n] ...
//
//  La cabecera entera entra como datos autenticados de CADA bloque,
//  junto con el numero de bloque y si es el ultimo. Consecuencias:
//    · Cambiar un byte de la cabecera invalida todos los bloques.
//    · Mover el bloque 5 al sitio del 2 no valida.
//    · Recortar el fichero por el final no valida (el bloque que
//      pasa a ser ultimo se autentico como "no ultimo").
//  Es decir: no basta con que los bloques sean autenticos, tienen
//  que estar en su sitio y estar todos.
// -------------------------------------------------------------
static void fxvHdrBuild(uint8_t* h, uint8_t kind, uint16_t id,
                        uint32_t plainSize, const uint8_t wsalt[FXV_WSALT_LEN]){
  h[0] = 'F'; h[1] = 'X'; h[2] = 'V'; h[3] = '1';
  h[4] = FLEXVAULT_FORMAT_VERSION;
  h[5] = kind;
  h[6] = 0; h[7] = 0;
  h[8] = (uint8_t)(id & 0xFF); h[9] = (uint8_t)(id >> 8);
  h[10] = (uint8_t)(FLEXVAULT_CHUNK & 0xFF);
  h[11] = (uint8_t)(FLEXVAULT_CHUNK >> 8);
  h[12] = (uint8_t)(plainSize & 0xFF);
  h[13] = (uint8_t)((plainSize >> 8) & 0xFF);
  h[14] = (uint8_t)((plainSize >> 16) & 0xFF);
  h[15] = (uint8_t)((plainSize >> 24) & 0xFF);
  memcpy(h + 16, wsalt, FXV_WSALT_LEN);
}

static bool fxvHdrCheck(const uint8_t* h, uint8_t kind, uint16_t id,
                        uint32_t* plainSize, uint16_t* chunkLen){
  if(h[0] != 'F' || h[1] != 'X' || h[2] != 'V' || h[3] != '1') return false;
  if(h[4] != FLEXVAULT_FORMAT_VERSION) return false;
  if(h[5] != kind) return false;
  uint16_t hid = (uint16_t)h[8] | ((uint16_t)h[9] << 8);
  if(hid != id) return false;
  *chunkLen  = (uint16_t)h[10] | ((uint16_t)h[11] << 8);
  if(*chunkLen == 0 || *chunkLen > FLEXVAULT_CHUNK) return false;
  *plainSize = (uint32_t)h[12] | ((uint32_t)h[13] << 8) |
               ((uint32_t)h[14] << 16) | ((uint32_t)h[15] << 24);
  return true;
}

static void fxvAad(const uint8_t* hdr, uint32_t chunk, bool last, uint8_t* out){
  memcpy(out, hdr, FXV_HDR_LEN);
  out[FXV_HDR_LEN + 0] = (uint8_t)(chunk & 0xFF);
  out[FXV_HDR_LEN + 1] = (uint8_t)((chunk >> 8) & 0xFF);
  out[FXV_HDR_LEN + 2] = (uint8_t)((chunk >> 16) & 0xFF);
  out[FXV_HDR_LEN + 3] = (uint8_t)((chunk >> 24) & 0xFF);
  out[FXV_HDR_LEN + 4] = last ? 1 : 0;
}
#define FXV_AAD_LEN (FXV_HDR_LEN + 5)

// ---- escritor incremental ----
// Existe para poder cifrar un fichero grande sin tenerlo entero en
// RAM: se le van dando bloques de plano y el va anadiendo bloques
// cifrados al almacen.
struct FxvWriter {
  char     path[FLEXFS_PATH_MAX];
  uint8_t  hdr[FXV_HDR_LEN];
  uint8_t  key[FXV_KEY_LEN];
  uint32_t chunk;
  uint32_t left;          // plano que queda por entregar
  bool     bad;
};

static bool fxvwBegin(FxvWriter* w, const char* path, uint8_t kind, uint16_t id,
                      uint32_t plainSize){
  memset(w, 0, sizeof(*w));
  snprintf(w->path, sizeof(w->path), "%s", path);
  uint8_t wsalt[FXV_WSALT_LEN];
  flexVaultRandomBytes(wsalt, sizeof(wsalt));
  fxvHdrBuild(w->hdr, kind, id, plainSize, wsalt);
  fxvDeriveKey(kind, id, wsalt, w->key);
  flexVaultWipe(wsalt, sizeof(wsalt));
  w->left = plainSize;
  // "w": reemplaza. Si habia una version anterior, deja de existir en
  // el momento en que empieza la nueva, no despues.
  if(!flexFsPrivWrite(w->path, w->hdr, FXV_HDR_LEN)){ w->bad = true; return false; }
  return true;
}

static bool fxvwChunk(FxvWriter* w, const uint8_t* data, size_t n){
  if(w->bad || n == 0 || n > FLEXVAULT_CHUNK) return false;
  bool last = (w->left <= n);
  uint8_t aad[FXV_AAD_LEN];
  fxvAad(w->hdr, w->chunk, last, aad);

  uint8_t  tag[FXV_TAG_LEN];
  uint8_t* ct = (uint8_t*)malloc(n);
  if(!ct){ w->bad = true; vErr = "sin memoria para cifrar"; return false; }
  uint8_t nonce[FXV_NONCE_LEN];
  fxvNonce(w->chunk, nonce);
  bool ok = fxvGcmEncrypt(w->key, nonce, aad, FXV_AAD_LEN, data, n, ct, tag);
  if(ok) ok = flexFsPrivAppend(w->path, tag, FXV_TAG_LEN);
  if(ok) ok = flexFsPrivAppend(w->path, ct, n);
  flexVaultWipe(ct, n);
  free(ct);
  if(!ok){ w->bad = true; vErr = "no se pudo escribir en el almacen"; return false; }
  w->chunk++;
  w->left = (w->left > n) ? (w->left - n) : 0;
  return true;
}

static void fxvwEnd(FxvWriter* w, bool keep){
  if(!keep || w->bad || w->left != 0){
    flexFsPrivDelete(w->path);          // nada a medias en el almacen
  }
  flexVaultWipe(w->key, sizeof(w->key));
}

// ---- lectura por bloques ----
static bool fxvReadContainer(const char* path, uint8_t kind, uint16_t id,
                             FlexVaultChunkCb cb, void* user, uint32_t* outSize){
  uint8_t hdr[FXV_HDR_LEN];
  if(flexFsPrivRead(path, 0, hdr, FXV_HDR_LEN) != FXV_HDR_LEN){
    vErr = "el almacen no responde"; return false;
  }
  uint32_t plainSize; uint16_t chunkLen;
  if(!fxvHdrCheck(hdr, kind, id, &plainSize, &chunkLen)){
    vErr = "formato no reconocido"; return false;
  }
  if(outSize) *outSize = plainSize;

  uint8_t key[FXV_KEY_LEN];
  fxvDeriveKey(kind, id, hdr + 16, key);

  uint8_t* ct = (uint8_t*)malloc(chunkLen);
  uint8_t* pt = (uint8_t*)malloc(chunkLen);
  if(!ct || !pt){
    free(ct); free(pt);
    flexVaultWipe(key, sizeof(key));
    vErr = "sin memoria para descifrar";
    return false;
  }

  bool     ok   = true;
  uint32_t off  = FXV_HDR_LEN;
  uint32_t done = 0, chunk = 0;
  while(done < plainSize){
    uint32_t want = plainSize - done;
    if(want > chunkLen) want = chunkLen;
    bool last = (done + want >= plainSize);

    uint8_t tag[FXV_TAG_LEN];
    if(flexFsPrivRead(path, off, tag, FXV_TAG_LEN) != FXV_TAG_LEN){ ok = false; vErr = "fichero incompleto"; break; }
    off += FXV_TAG_LEN;
    if(flexFsPrivRead(path, off, ct, want) != (int)want){ ok = false; vErr = "fichero incompleto"; break; }
    off += want;

    uint8_t aad[FXV_AAD_LEN];
    fxvAad(hdr, chunk, last, aad);
    uint8_t nonce[FXV_NONCE_LEN];
    fxvNonce(chunk, nonce);
    if(!fxvGcmDecrypt(key, nonce, aad, FXV_AAD_LEN, ct, want, tag, pt)){
      // Autenticacion fallida: o la clave no es la correcta, o
      // alguien toco los bytes. En los dos casos se para aqui y NO se
      // entrega nada: entregar "lo que se pudo descifrar" seria
      // entregar datos manipulados.
      ok = false; vErr = "los datos no pasan la comprobacion de integridad"; break;
    }
    if(cb && !cb(user, done, pt, want)){ ok = false; break; }
    done  += want;
    chunk++;
  }

  flexVaultWipe(ct, chunkLen);
  flexVaultWipe(pt, chunkLen);
  free(ct); free(pt);
  flexVaultWipe(key, sizeof(key));
  return ok;
}

// Contenedor completo a un buffer de RAM (para el indice, el registro
// y los elementos pequenos).
struct FxvGrab { uint8_t* buf; size_t cap; size_t got; };
static bool fxvGrabCb(void* user, uint32_t off, const uint8_t* data, size_t n){
  FxvGrab* g = (FxvGrab*)user;
  if(off + n > g->cap) return false;
  memcpy(g->buf + off, data, n);
  g->got = off + n;
  return true;
}

static bool fxvWriteWhole(const char* path, uint8_t kind, uint16_t id,
                          const void* data, size_t n){
  FxvWriter w;
  if(!fxvwBegin(&w, path, kind, id, (uint32_t)n)){ fxvwEnd(&w, false); return false; }
  const uint8_t* p = (const uint8_t*)data;
  size_t left = n;
  bool ok = true;
  while(ok && left){
    size_t c = left > FLEXVAULT_CHUNK ? FLEXVAULT_CHUNK : left;
    ok = fxvwChunk(&w, p, c);
    p += c; left -= c;
  }
  if(n == 0){
    // Un contenedor vacio es legitimo (una nota nueva). Se queda solo
    // con la cabecera y no hay bloques que autenticar.
    ok = true;
  }
  fxvwEnd(&w, ok);
  return ok;
}

// -------------------------------------------------------------
//  INDICE (nombres, rutas, tamanos: todo cifrado)
// -------------------------------------------------------------
static size_t fxvIdxBytes(){ return (size_t)FLEXVAULT_MAX_ITEMS * sizeof(FxvRec) + 8; }

static bool fxvIdxAlloc(){
  if(vIdx) return true;
  vIdx = (FxvRec*)calloc(FLEXVAULT_MAX_ITEMS, sizeof(FxvRec));
  if(!vIdx){ vErr = "sin memoria para el indice"; return false; }
  vIdxN = 0;
  return true;
}

static void fxvIdxFree(){
  if(vIdx){
    // El indice tiene los NOMBRES en claro: se borra, no solo se
    // libera. Un free() deja los nombres en el heap hasta que otro
    // los pise.
    flexVaultWipe(vIdx, (size_t)FLEXVAULT_MAX_ITEMS * sizeof(FxvRec));
    free(vIdx);
    vIdx = NULL;
  }
  vIdxN = 0;
  vNextId = 1;
}

static bool fxvIdxSave(){
  if(!vOpen || !vIdx) return false;
  size_t n = 4 + (size_t)vIdxN * sizeof(FxvRec);
  uint8_t* buf = (uint8_t*)malloc(n);
  if(!buf){ vErr = "sin memoria para el indice"; return false; }
  buf[0] = (uint8_t)(vIdxN & 0xFF); buf[1] = (uint8_t)(vIdxN >> 8);
  buf[2] = (uint8_t)(vNextId & 0xFF); buf[3] = (uint8_t)(vNextId >> 8);
  memcpy(buf + 4, vIdx, (size_t)vIdxN * sizeof(FxvRec));
  bool ok = fxvWriteWhole(FXV_IDX_PATH, FXV_KIND_INDEX, 0, buf, n);
  flexVaultWipe(buf, n);
  free(buf);
  return ok;
}

static bool fxvIdxLoad(){
  if(!fxvIdxAlloc()) return false;
  vIdxN = 0; vNextId = 1;
  if(!flexFsPrivExists(FXV_IDX_PATH)) return true;        // boveda vacia

  size_t cap = fxvIdxBytes();
  uint8_t* buf = (uint8_t*)malloc(cap);
  if(!buf){ vErr = "sin memoria para el indice"; return false; }
  FxvGrab g = { buf, cap, 0 };
  bool ok = fxvReadContainer(FXV_IDX_PATH, FXV_KIND_INDEX, 0, fxvGrabCb, &g, NULL);
  if(ok && g.got >= 4){
    int n = (int)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
    vNextId = (uint16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
    if(n < 0) n = 0;
    if(n > FLEXVAULT_MAX_ITEMS) n = FLEXVAULT_MAX_ITEMS;
    if(4 + (size_t)n * sizeof(FxvRec) <= g.got){
      memcpy(vIdx, buf + 4, (size_t)n * sizeof(FxvRec));
      vIdxN = n;
    } else ok = false;
  } else ok = false;
  if(vNextId == 0) vNextId = 1;
  flexVaultWipe(buf, cap);
  free(buf);
  if(!ok) vErr = "el indice de la boveda no se pudo leer";
  return ok;
}

static FxvRec* fxvFind(uint16_t id){
  for(int i = 0; i < vIdxN; i++) if(vIdx[i].id == id) return &vIdx[i];
  return NULL;
}

// -------------------------------------------------------------
//  REGISTRO DE SEGURIDAD
//  ------------------------------------------------------------
//  Guarda CODIGOS, no texto: apertura, cierre (con motivo), fallo,
//  cambio de clave y movimientos de elementos. Nunca el nombre ni el
//  tipo de lo que se movio -- "entro un elemento" es todo lo que se
//  puede decir sin convertir el registro en una lista de lo que hay
//  dentro. Y aun asi va cifrado.
// -------------------------------------------------------------
static bool fxvLogSave(){
  if(!vOpen) return false;
  uint8_t buf[2 + FLEXVAULT_LOG_MAX * 8];
  buf[0] = (uint8_t)(vLogN & 0xFF); buf[1] = (uint8_t)(vLogN >> 8);
  for(int i = 0; i < vLogN; i++){
    uint8_t* p = buf + 2 + i * 8;
    p[0] = (uint8_t)(vLog[i].ts & 0xFF);
    p[1] = (uint8_t)((vLog[i].ts >> 8) & 0xFF);
    p[2] = (uint8_t)((vLog[i].ts >> 16) & 0xFF);
    p[3] = (uint8_t)((vLog[i].ts >> 24) & 0xFF);
    p[4] = vLog[i].ev;
    p[5] = (uint8_t)vLog[i].aux;
    p[6] = 0; p[7] = 0;
  }
  return fxvWriteWhole(FXV_LOG_PATH, FXV_KIND_LOG, 0, buf, 2 + (size_t)vLogN * 8);
}

static void fxvLogLoad(){
  vLogN = 0;
  if(!flexFsPrivExists(FXV_LOG_PATH)) return;
  uint8_t buf[2 + FLEXVAULT_LOG_MAX * 8];
  FxvGrab g = { buf, sizeof(buf), 0 };
  if(!fxvReadContainer(FXV_LOG_PATH, FXV_KIND_LOG, 0, fxvGrabCb, &g, NULL)) return;
  if(g.got < 2) return;
  int n = (int)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
  if(n < 0) n = 0;
  if(n > FLEXVAULT_LOG_MAX) n = FLEXVAULT_LOG_MAX;
  if(2 + (size_t)n * 8 > g.got) return;
  for(int i = 0; i < n; i++){
    const uint8_t* p = buf + 2 + i * 8;
    vLog[i].ts = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                 ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    vLog[i].ev  = p[4];
    vLog[i].aux = (int8_t)p[5];
  }
  vLogN = n;
}

// Inserta al frente del anillo (el mas reciente primero). Si esta
// lleno se pierde el mas viejo, que es lo correcto cuando el registro
// sirve para "que ha pasado ultimamente".
static void fxvRingPush(FlexVaultLog* ring, int* n, int cap,
                        uint32_t ts, uint8_t ev, int8_t aux){
  if(*n < cap) (*n)++;
  for(int i = *n - 1; i > 0; i--) ring[i] = ring[i - 1];
  ring[0].ts = ts; ring[0].ev = ev; ring[0].aux = aux;
}

void flexVaultLogAdd(uint8_t ev, int8_t aux){
  if(!vOpen){
    fxvRingPush(vPend, &vPendN, FXV_PEND_MAX, vClock, ev, aux);
    return;
  }
  fxvRingPush(vLog, &vLogN, FLEXVAULT_LOG_MAX, vClock, ev, aux);
  fxvLogSave();
}

// Vuelca los eventos que estaban esperando (del mas antiguo al mas
// reciente, para que el orden del registro siga siendo el real).
static void fxvLogFlushPending(){
  for(int i = vPendN - 1; i >= 0; i--)
    fxvRingPush(vLog, &vLogN, FLEXVAULT_LOG_MAX, vPend[i].ts, vPend[i].ev, vPend[i].aux);
  vPendN = 0;
}

int flexVaultLogRead(FlexVaultLog* out, int maxn){
  // El registro es contenido privado: cerrada la boveda no se lee.
  if(!vOpen || !out || maxn <= 0) return 0;
  int n = vLogN < maxn ? vLogN : maxn;
  for(int i = 0; i < n; i++) out[i] = vLog[i];
  return n;
}

// -------------------------------------------------------------
//  PERSISTENCIA DE AJUSTES Y CONTADORES (NVS)
// -------------------------------------------------------------
static void fxvSaveEnvelope(){
  vprefs.begin(FXV_NS, false);
  vprefs.putInt("ver",   FLEXVAULT_FORMAT_VERSION);
  vprefs.putInt("type",  vLockType);
  vprefs.putInt("plen",  vSecretLen);
  vprefs.putUInt("iters", vIters);
  vprefs.putBytes("salt", vSalt, sizeof(vSalt));
  vprefs.putBytes("env",  vEnv,  sizeof(vEnv));
  vprefs.end();
}

static void fxvSaveFails(){
  vprefs.begin(FXV_NS, false);
  vprefs.putInt("fails", vFails);
  vprefs.end();
}

static void fxvSaveSettings(){
  vprefs.begin(FXV_NS, false);
  vprefs.putUInt("alock", vAutoLock);
  vprefs.putUInt("last",  vLastAcc);
  vprefs.putInt("apps",   (int)vAppMask);
  vprefs.putInt("applk",  (int)vAppLock);
  vprefs.putBytes("atch", vAppTouch, sizeof(vAppTouch));
  vprefs.end();
}

bool flexVaultBegin(){
  memset(vSalt, 0, sizeof(vSalt));
  memset(vEnv, 0, sizeof(vEnv));
  memset(vAppTouch, 0, sizeof(vAppTouch));
  vOpen = false;
  flexVaultWipe(vMaster, sizeof(vMaster));

  vprefs.begin(FXV_NS, true);
  int ver = vprefs.getInt("ver", 0);
  vLockType  = vprefs.getInt("type", FLEXVAULT_LOCK_NONE);
  vSecretLen = vprefs.getInt("plen", 0);
  vIters     = vprefs.getUInt("iters", FLEXVAULT_KDF_ITERS);
  size_t gs  = vprefs.getBytes("salt", vSalt, sizeof(vSalt));
  size_t ge  = vprefs.getBytes("env",  vEnv,  sizeof(vEnv));
  vFails     = vprefs.getInt("fails", 0);
  vAutoLock  = vprefs.getUInt("alock", 60000);
  vLastAcc   = vprefs.getUInt("last", 0);
  vAppMask   = (uint16_t)vprefs.getInt("apps", 0);
  vAppLock   = (uint16_t)vprefs.getInt("applk", 0);
  vprefs.getBytes("atch", vAppTouch, sizeof(vAppTouch));
  vprefs.end();

  if(vFails < 0) vFails = 0;                                  // NVS corrupta
  if(vIters == 0 || vIters > 1000000u) vIters = FLEXVAULT_KDF_ITERS;
  vExists = (ver == FLEXVAULT_FORMAT_VERSION &&
             (vLockType == FLEXVAULT_LOCK_PIN || vLockType == FLEXVAULT_LOCK_PASS) &&
             gs == sizeof(vSalt) && ge == sizeof(vEnv));
  if(!vExists){ vLockType = FLEXVAULT_LOCK_NONE; vSecretLen = 0; }

  // El arranque es, por definicion, un cierre: la boveda nunca queda
  // abierta al encender. Se anota en el registro la proxima vez que
  // haya clave para poder escribirlo (aqui aun no la hay).
  vPenServed = false;
  vPenUntil  = 0;
  vLogN      = 0;                // el registro se relee de disco al abrir
  vPendN     = 0;
  if(vExists) flexVaultLogAdd(FXV_EV_LOCK, FXV_LOCK_BOOT);

  if(!flexFsReady()){ vErr = "sin almacenamiento"; return false; }
  vErr = "";
  return flexFsVaultInit();
}

uint32_t flexVaultUsedBytes(){
  return flexFsPrivDirSize(FLEXFS_DIR_VAULT) + flexFsPrivDirSize(FLEXFS_DIR_VAULTD);
}

// -------------------------------------------------------------
//  ESPERA PROGRESIVA POR INTENTOS FALLIDOS
//  ------------------------------------------------------------
//  Los tramos son los mismos que ya usa el bloqueo del sistema, para
//  que el usuario no tenga que aprender dos comportamientos: 1-3
//  fallos sin espera, 4-5 medio minuto, 6+ cinco minutos. El
//  contador vive en NVS, asi que quitar la corriente no lo reinicia.
// -------------------------------------------------------------
static uint32_t fxvPenaltyFor(int fails){
  if(fails >= 6) return 300000u;
  if(fails >= 4) return 30000u;
  return 0;
}

uint32_t flexVaultWaitMs(){
  // Castigo pendiente tras un reinicio: se cobra la primera vez que
  // se pregunta, no en cada entrada y salida de la pantalla.
  if(!vPenUntil && !vPenServed){
    uint32_t pen = fxvPenaltyFor(vFails);
    if(pen){ vPenUntil = millis() + pen; }
    else    { vPenServed = true; }
  }
  if(!vPenUntil) return 0;
  int32_t left = (int32_t)(vPenUntil - millis());       // con signo: aguanta el vuelco de millis()
  if(left <= 0){ vPenUntil = 0; vPenServed = true; return 0; }
  return (uint32_t)left;
}

// -------------------------------------------------------------
//  CREACION, APERTURA, CIERRE
// -------------------------------------------------------------
static bool fxvWrapMaster(const char* secret){
  uint8_t kek[FXV_KEY_LEN];
  flexVaultKdf(secret, vSalt, sizeof(vSalt), vIters, kek, sizeof(kek));
  uint8_t* nonce = vEnv;
  uint8_t* wrap  = vEnv + FXV_NONCE_LEN;
  uint8_t* tag   = vEnv + FXV_NONCE_LEN + FXV_KEY_LEN;
  flexVaultRandomBytes(nonce, FXV_NONCE_LEN);
  // La sal entra como datos autenticados: asi el sobre esta atado a
  // ESTA boveda y no se puede pegar el sobre de otra.
  bool ok = fxvGcmEncrypt(kek, nonce, vSalt, sizeof(vSalt),
                          vMaster, FXV_KEY_LEN, wrap, tag);
  flexVaultWipe(kek, sizeof(kek));
  return ok;
}

static bool fxvUnwrapMaster(const char* secret){
  uint8_t kek[FXV_KEY_LEN];
  flexVaultKdf(secret, vSalt, sizeof(vSalt), vIters, kek, sizeof(kek));
  const uint8_t* nonce = vEnv;
  const uint8_t* wrap  = vEnv + FXV_NONCE_LEN;
  const uint8_t* tag   = vEnv + FXV_NONCE_LEN + FXV_KEY_LEN;
  bool ok = fxvGcmDecrypt(kek, nonce, vSalt, sizeof(vSalt),
                          wrap, FXV_KEY_LEN, tag, vMaster);
  flexVaultWipe(kek, sizeof(kek));
  if(!ok) flexVaultWipe(vMaster, sizeof(vMaster));
  return ok;
}

int flexVaultCreate(const char* secret, int lockType){
  if(vExists) return FXV_ERR_STATE;
  if(!secret || strlen(secret) < 4 || strlen(secret) >= FLEXVAULT_SECRET_MAX) return FXV_ERR_ARG;
  if(lockType != FLEXVAULT_LOCK_PIN && lockType != FLEXVAULT_LOCK_PASS) return FXV_ERR_ARG;
  if(!flexFsReady() || !flexFsVaultInit()){ vErr = "sin almacenamiento"; return FXV_ERR_IO; }

  vLockType  = lockType;
  vSecretLen = (int)strlen(secret);
  vIters     = FLEXVAULT_KDF_ITERS;
  flexVaultRandomBytes(vSalt, sizeof(vSalt));
  // Clave maestra ALEATORIA, una por boveda. No se deriva de nada que
  // el usuario sepa: por eso cambiar la contrasena no obliga a
  // recifrar y por eso dos bovedas con la misma contrasena no
  // comparten claves de fichero.
  flexVaultRandomBytes(vMaster, sizeof(vMaster));
  if(!fxvWrapMaster(secret)){
    flexVaultWipe(vMaster, sizeof(vMaster));
    vErr = "no se pudo proteger la clave";
    return FXV_ERR_IO;
  }
  vOpen   = true;
  vExists = true;
  vFails  = 0;
  vPenServed = true; vPenUntil = 0;
  vLastAcc = vClock;
  fxvSaveEnvelope();
  fxvSaveFails();
  fxvSaveSettings();

  if(!fxvIdxAlloc()){ flexVaultLock(FXV_LOCK_MANUAL); return FXV_ERR_IO; }
  vIdxN = 0; vNextId = 1; vLogN = 0;
  if(!fxvIdxSave()){ flexVaultLock(FXV_LOCK_MANUAL); return FXV_ERR_IO; }
  flexVaultLogAdd(FXV_EV_CREATE, 0);
  vErr = "";
  return FXV_OK;
}

int flexVaultUnlock(const char* secret){
  if(!vExists) return FXV_ERR_STATE;
  if(vOpen) return FXV_OK;
  if(flexVaultWaitMs() > 0) return FXV_ERR_WAIT;
  if(!secret || !secret[0]) return FXV_ERR_ARG;
  if(!flexFsReady()){ vErr = "sin almacenamiento"; return FXV_ERR_IO; }

  if(!fxvUnwrapMaster(secret)){
    // Clave incorrecta: el tag GCM no valida. Se suma el fallo y se
    // PERSISTE antes de devolver, para que un reinicio inmediato no
    // borre el intento.
    if(vFails < 9999) vFails++;
    fxvSaveFails();
    vPenServed = false; vPenUntil = 0;
    uint32_t pen = fxvPenaltyFor(vFails);
    if(pen) vPenUntil = millis() + pen;
    else    vPenServed = true;
    // El fallo se anota, pero sin clave NO se puede escribir en un
    // registro cifrado: queda en RAM y se persiste con la proxima
    // apertura correcta. Guardarlo en claro para no perderlo seria
    // cambiar una limitacion por una fuga.
    flexVaultLogAdd(FXV_EV_FAIL, 0);
    vErr = "clave incorrecta";
    return FXV_ERR_WRONG;
  }

  vOpen = true;
  if(vFails){ vFails = 0; fxvSaveFails(); }
  vPenServed = true; vPenUntil = 0;

  fxvLogLoad();
  fxvLogFlushPending();          // los fallos de mientras estaba cerrada
  if(!fxvIdxLoad()){
    // La clave era correcta (el sobre valido), pero el indice no se
    // pudo leer. No se finge que la boveda esta vacia: se cierra y se
    // dice lo que pasa, que es un almacen danado.
    flexVaultLock(FXV_LOCK_MANUAL);
    return FXV_ERR_IO;
  }
  vLastAcc = vClock;
  fxvSaveSettings();
  flexVaultLogAdd(FXV_EV_UNLOCK, 0);
  vErr = "";
  return FXV_OK;
}

void flexVaultLock(int reason){
  if(vOpen){
    flexVaultLogAdd(FXV_EV_LOCK, (int8_t)reason);
  }
  // Orden importante: primero el indice (tiene nombres en claro),
  // luego la clave maestra. Y los dos con borrado real.
  fxvIdxFree();
  flexVaultWipe(vMaster, sizeof(vMaster));
  vOpen = false;
}

int flexVaultChangeSecret(const char* oldSecret, const char* newSecret, int lockType){
  if(!vExists) return FXV_ERR_STATE;
  if(!newSecret || strlen(newSecret) < 4 || strlen(newSecret) >= FLEXVAULT_SECRET_MAX) return FXV_ERR_ARG;
  if(lockType != FLEXVAULT_LOCK_PIN && lockType != FLEXVAULT_LOCK_PASS) return FXV_ERR_ARG;
  if(flexVaultWaitMs() > 0) return FXV_ERR_WAIT;

  // Se exige la clave actual incluso con la boveda abierta: cambiar la
  // llave es justo la operacion que no puede aprovechar una sesion
  // que alguien dejo abierta.
  bool wasOpen = vOpen;
  uint8_t keep[FXV_KEY_LEN];
  if(wasOpen) memcpy(keep, vMaster, FXV_KEY_LEN);
  if(!fxvUnwrapMaster(oldSecret)){
    if(wasOpen) memcpy(vMaster, keep, FXV_KEY_LEN);
    flexVaultWipe(keep, sizeof(keep));
    if(vFails < 9999) vFails++;
    fxvSaveFails();
    vPenServed = false;
    uint32_t pen = fxvPenaltyFor(vFails);
    vPenUntil = pen ? (millis() + pen) : 0;
    if(!pen) vPenServed = true;
    vErr = "clave incorrecta";
    return FXV_ERR_WRONG;
  }
  flexVaultWipe(keep, sizeof(keep));

  // Sal NUEVA con la clave nueva: si se reutilizara la vieja, dos
  // sobres distintos compartirian derivacion.
  uint8_t oldSalt[FXV_SALT_LEN];
  memcpy(oldSalt, vSalt, sizeof(oldSalt));
  flexVaultRandomBytes(vSalt, sizeof(vSalt));
  int oldType = vLockType, oldLen = vSecretLen;
  vLockType  = lockType;
  vSecretLen = (int)strlen(newSecret);
  if(!fxvWrapMaster(newSecret)){
    memcpy(vSalt, oldSalt, sizeof(oldSalt));           // se deja como estaba
    vLockType = oldType; vSecretLen = oldLen;
    fxvWrapMaster(oldSecret);
    if(!wasOpen) flexVaultWipe(vMaster, sizeof(vMaster));
    vErr = "no se pudo guardar la clave nueva";
    return FXV_ERR_IO;
  }
  fxvSaveEnvelope();
  if(vFails){ vFails = 0; fxvSaveFails(); }

  // Los FICHEROS NO SE TOCAN: siguen cifrados con claves derivadas de
  // la clave maestra, que es la misma. Eso es lo que permite cambiar
  // la contrasena sin perder nada.
  if(wasOpen){ flexVaultLogAdd(FXV_EV_CHANGEKEY, 0); }
  else       { flexVaultWipe(vMaster, sizeof(vMaster)); }
  vErr = "";
  return FXV_OK;
}

// -------------------------------------------------------------
//  CONTENIDO
// -------------------------------------------------------------
int flexVaultCount(int kind){
  if(!vOpen || !vIdx) return 0;
  int n = 0;
  for(int i = 0; i < vIdxN; i++){
    if(vIdx[i].kind == FXV_KIND_APP) continue;          // datos de app: no son contenido listable
    if(kind == FXV_KIND_ANY || vIdx[i].kind == kind) n++;
  }
  return n;
}

int flexVaultList(int kind, FlexVaultItem* out, int maxn){
  if(!vOpen || !vIdx || !out || maxn <= 0) return 0;
  int n = 0;
  // El indice se mantiene con el mas reciente al final, asi que se
  // recorre al reves para entregar "lo ultimo que entro primero".
  for(int i = vIdxN - 1; i >= 0 && n < maxn; i--){
    if(kind != FXV_KIND_ANY && vIdx[i].kind != kind) continue;
    if(kind == FXV_KIND_ANY && vIdx[i].kind == FXV_KIND_APP) continue;
    out[n].id      = vIdx[i].id;
    out[n].kind    = vIdx[i].kind;
    out[n].appId   = vIdx[i].appId;
    out[n].size    = vIdx[i].size;
    out[n].added   = vIdx[i].added;
    out[n].lastAcc = vIdx[i].lastAcc;
    snprintf(out[n].name, sizeof(out[n].name), "%s", vIdx[i].name);
    n++;
  }
  return n;
}

bool flexVaultGet(uint16_t id, FlexVaultItem* out){
  if(!vOpen || !out) return false;
  FxvRec* r = fxvFind(id);
  if(!r) return false;
  out->id = r->id; out->kind = r->kind; out->appId = r->appId;
  out->size = r->size; out->added = r->added; out->lastAcc = r->lastAcc;
  snprintf(out->name, sizeof(out->name), "%s", r->name);
  return true;
}

// Deduce la clase por la extension. Solo se usa como sugerencia: la
// interfaz puede imponer otra.
static int fxvKindFromName(const char* name){
  const char* d = strrchr(name, '.');
  if(!d) return FXV_KIND_FILE;
  if(!strcasecmp(d, ".txt")) return FXV_KIND_NOTE;
  if(!strcasecmp(d, ".jpg") || !strcasecmp(d, ".jpeg") ||
     !strcasecmp(d, ".fxp")) return FXV_KIND_PHOTO;
  return FXV_KIND_FILE;
}

bool flexVaultImport(const char* path, int kind, int appId){
  if(!vOpen || !vIdx){ vErr = "la boveda esta cerrada"; return false; }
  if(!path || !path[0]) return false;
  if(vIdxN >= FLEXVAULT_MAX_ITEMS){ vErr = "la boveda esta llena"; return false; }
  if(flexFsIsVaultPath(path)) return false;              // ya esta dentro
  if(!flexFsExists(path) || flexFsIsDir(path)){ vErr = "solo se pueden mover ficheros"; return false; }

  uint32_t size = flexFsSize(path);
  uint16_t id   = vNextId;
  char blob[FLEXFS_PATH_MAX];
  vaultBlobPath(id, blob, sizeof(blob));

  const char* base = strrchr(path, '/');
  base = base ? base + 1 : path;
  if(kind < 0) kind = fxvKindFromName(base);

  FxvWriter w;
  if(!fxvwBegin(&w, blob, (uint8_t)kind, id, size)){ fxvwEnd(&w, false); return false; }

  uint8_t* buf = (uint8_t*)malloc(FLEXVAULT_CHUNK);
  if(!buf){ fxvwEnd(&w, false); vErr = "sin memoria"; return false; }
  bool ok = true;
  uint32_t off = 0;
  while(ok && off < size){
    size_t want = size - off;
    if(want > FLEXVAULT_CHUNK) want = FLEXVAULT_CHUNK;
    int rd = flexFsReadAt(path, off, buf, want);
    if(rd != (int)want){ ok = false; vErr = "no se pudo leer el original"; break; }
    ok = fxvwChunk(&w, buf, want);
    off += want;
  }
  flexVaultWipe(buf, FLEXVAULT_CHUNK);
  free(buf);
  fxvwEnd(&w, ok);
  if(!ok) return false;

  FxvRec* r = &vIdx[vIdxN];
  memset(r, 0, sizeof(*r));
  r->id      = id;
  r->kind    = (int8_t)kind;
  r->appId   = (int8_t)appId;
  r->size    = size;
  r->added   = vClock;
  r->lastAcc = vClock;
  snprintf(r->name,   sizeof(r->name),   "%s", base);
  snprintf(r->origin, sizeof(r->origin), "%s", path);
  vIdxN++;
  vNextId = (uint16_t)(id + 1);
  if(!fxvIdxSave()){
    vIdxN--;                                 // el indice manda: si no se guardo, el elemento no existe
    flexFsPrivDelete(blob);
    return false;
  }

  // ORDEN DELIBERADO: el original se borra AL FINAL, cuando el blob y
  // el indice ya estan en disco. Si se corta la corriente antes, el
  // fichero sigue en su sitio (el usuario ve un duplicado, molesto
  // pero recuperable). Al contrario -borrar primero- un corte
  // significaria perder el fichero.
  if(!flexFsDelete(path)){
    vErr = "el elemento se copio cifrado, pero el original no se pudo borrar";
    flexVaultLogAdd(FXV_EV_IN, 0);
    return false;
  }
  flexVaultLogAdd(FXV_EV_IN, 0);
  vErr = "";
  return true;
}

// Escritura por bloques al devolver un elemento a la app normal.
struct FxvExp { char path[FLEXFS_PATH_MAX]; bool ok; };
static bool fxvExpCb(void* user, uint32_t off, const uint8_t* data, size_t n){
  FxvExp* e = (FxvExp*)user;
  (void)off;
  if(!flexFsAppendBin(e->path, data, n)){ e->ok = false; return false; }
  return true;
}

bool flexVaultExport(uint16_t id, char* outPath, size_t n){
  if(!vOpen || !vIdx){ vErr = "la boveda esta cerrada"; return false; }
  FxvRec* r = fxvFind(id);
  if(!r){ vErr = "el elemento ya no esta"; return false; }

  // Destino: su ruta original. Si ya hay algo ahi, se busca un nombre
  // libre en vez de pisarlo.
  char dst[FLEXFS_PATH_MAX];
  snprintf(dst, sizeof(dst), "%s", r->origin[0] ? r->origin : "");
  if(!dst[0] || flexFsIsVaultPath(dst)){
    snprintf(dst, sizeof(dst), "%s/%s", FLEXFS_DIR_DOCS, r->name);
  }
  if(flexFsExists(dst)){
    char dir[FLEXFS_PATH_MAX]; snprintf(dir, sizeof(dir), "%s", dst);
    char* s = strrchr(dir, '/');
    if(s) *s = 0;
    char stem[FLEXFS_NAME_MAX]; flexFsStem(r->name, stem, sizeof(stem));
    const char* ext = strrchr(r->name, '.');
    char cand[FLEXFS_PATH_MAX];
    bool found = false;
    for(int k = 2; k < 100 && !found; k++){
      snprintf(cand, sizeof(cand), "%s/%s (%d)%s", dir[0] ? dir : "", stem, k, ext ? ext : "");
      if(!flexFsExists(cand)){ snprintf(dst, sizeof(dst), "%s", cand); found = true; }
    }
    if(!found){ vErr = "no hay un nombre libre en la carpeta de destino"; return false; }
  }

  FxvExp e;
  snprintf(e.path, sizeof(e.path), "%s", dst);
  e.ok = true;
  if(!flexFsWriteBin(e.path, NULL, 0)){ vErr = "no se pudo crear el fichero"; return false; }

  char blob[FLEXFS_PATH_MAX];
  vaultBlobPath(id, blob, sizeof(blob));
  bool ok = fxvReadContainer(blob, (uint8_t)r->kind, id, fxvExpCb, &e, NULL) && e.ok;
  if(!ok){
    flexFsDelete(e.path);            // no se deja medio fichero fuera
    return false;
  }
  if(outPath && n) snprintf(outPath, n, "%s", e.path);

  // Ya esta fuera y completo: se retira de la boveda.
  flexFsPrivDelete(blob);
  int at = (int)(r - vIdx);
  for(int i = at; i < vIdxN - 1; i++) vIdx[i] = vIdx[i + 1];
  vIdxN--;
  fxvIdxSave();
  flexVaultLogAdd(FXV_EV_OUT, 0);
  vErr = "";
  return true;
}

bool flexVaultDelete(uint16_t id){
  if(!vOpen || !vIdx) return false;
  FxvRec* r = fxvFind(id);
  if(!r) return false;
  char blob[FLEXFS_PATH_MAX];
  vaultBlobPath(id, blob, sizeof(blob));
  flexFsPrivDelete(blob);
  int at = (int)(r - vIdx);
  for(int i = at; i < vIdxN - 1; i++) vIdx[i] = vIdx[i + 1];
  vIdxN--;
  bool ok = fxvIdxSave();
  flexVaultLogAdd(FXV_EV_DEL, 0);
  return ok;
}

bool flexVaultRename(uint16_t id, const char* name){
  if(!vOpen || !vIdx || !name || !name[0]) return false;
  if(strchr(name, '/')) return false;
  FxvRec* r = fxvFind(id);
  if(!r) return false;
  snprintf(r->name, sizeof(r->name), "%s", name);
  return fxvIdxSave();
}

bool flexVaultCreateItem(const char* name, int kind, int appId, uint16_t* outId){
  if(!vOpen || !vIdx){ vErr = "la boveda esta cerrada"; return false; }
  if(vIdxN >= FLEXVAULT_MAX_ITEMS){ vErr = "la boveda esta llena"; return false; }
  if(!name || !name[0]) return false;

  uint16_t id = vNextId;
  char blob[FLEXFS_PATH_MAX];
  vaultBlobPath(id, blob, sizeof(blob));
  if(!fxvWriteWhole(blob, (uint8_t)kind, id, NULL, 0)) return false;

  FxvRec* r = &vIdx[vIdxN];
  memset(r, 0, sizeof(*r));
  r->id = id; r->kind = (int8_t)kind; r->appId = (int8_t)appId;
  r->size = 0; r->added = vClock; r->lastAcc = vClock;
  snprintf(r->name, sizeof(r->name), "%s", name);
  vIdxN++;
  vNextId = (uint16_t)(id + 1);
  if(!fxvIdxSave()){ vIdxN--; flexFsPrivDelete(blob); return false; }
  if(outId) *outId = id;
  vErr = "";
  return true;
}

bool flexVaultWrite(uint16_t id, const void* buf, size_t n){
  if(!vOpen || !vIdx){ vErr = "la boveda esta cerrada"; return false; }
  FxvRec* r = fxvFind(id);
  if(!r) return false;
  char blob[FLEXFS_PATH_MAX];
  vaultBlobPath(id, blob, sizeof(blob));
  if(!fxvWriteWhole(blob, (uint8_t)r->kind, id, buf, n)) return false;
  r->size    = (uint32_t)n;
  r->lastAcc = vClock;
  return fxvIdxSave();
}

int flexVaultRead(uint16_t id, void* buf, size_t n){
  if(!vOpen || !vIdx || !buf) return -1;
  FxvRec* r = fxvFind(id);
  if(!r) return -1;
  char blob[FLEXFS_PATH_MAX];
  vaultBlobPath(id, blob, sizeof(blob));
  FxvGrab g = { (uint8_t*)buf, n, 0 };
  uint32_t sz = 0;
  if(!fxvReadContainer(blob, (uint8_t)r->kind, id, fxvGrabCb, &g, &sz)) return -1;
  r->lastAcc = vClock;
  return (int)g.got;
}

bool flexVaultReadStream(uint16_t id, FlexVaultChunkCb cb, void* user){
  if(!vOpen || !vIdx || !cb) return false;
  FxvRec* r = fxvFind(id);
  if(!r) return false;
  char blob[FLEXFS_PATH_MAX];
  vaultBlobPath(id, blob, sizeof(blob));
  bool ok = fxvReadContainer(blob, (uint8_t)r->kind, id, cb, user, NULL);
  if(ok) r->lastAcc = vClock;
  return ok;
}

// -------------------------------------------------------------
//  APPS PRIVADAS
//  ------------------------------------------------------------
//  Aqui esta la decision mas delicada de todo el modulo: QUE APP
//  PUEDE de verdad tener una version privada. El criterio es uno y
//  no se negocia:
//
//     una app es compatible si TODO su estado persistente puede
//     redirigirse al almacen cifrado de la boveda.
//
//  Si solo se puede separar una parte, la app NO es compatible. Una
//  "version privada" que comparte cache, sesion o historial con la
//  normal es peor que no tenerla: parece privada y no lo es.
//
//  Los identificadores son los del registro de apps del sketch
//  (enum IC_*): 1 Galeria, 3 Almacenamiento/Archivos, 5 Notas,
//  7 Navegador, 10 Paint, 12 Ajustes, 14 Calendario.
// -------------------------------------------------------------
#define FXV_APP_GALERIA  1
#define FXV_APP_ARCHIVOS 3
#define FXV_APP_NOTAS    5
#define FXV_APP_NAV      7
#define FXV_APP_PAINT    10
#define FXV_APP_AJUSTES  12
#define FXV_APP_CALEND   14
#define FXV_APP_MODOPC   4

bool flexVaultAppForbidden(int appId){
  // Nada que administre el sistema entra en la boveda. Ajustes es la
  // puerta de la OTA, del bloqueo, del apagado y de la propia
  // Flex Vault: una copia "privada" de Ajustes seria una segunda
  // consola de administracion detras de la clave. Modo PC hospeda
  // otras apps dentro de sus ventanas, asi que meterlo dentro seria
  // una via para colar cualquier app en la boveda sin pasar por este
  // filtro.
  return appId == FXV_APP_AJUSTES || appId == FXV_APP_MODOPC;
}

bool flexVaultAppSupported(int appId){
  if(flexVaultAppForbidden(appId)) return false;
  return appId == FXV_APP_NOTAS || appId == FXV_APP_GALERIA || appId == FXV_APP_ARCHIVOS;
}

const char* flexVaultAppReason(int appId){
  if(flexVaultAppSupported(appId)) return NULL;
  if(flexVaultAppForbidden(appId))
    return "Las apps del sistema no pueden entrar en la Carpeta segura";
  if(appId == FXV_APP_NAV)
    return "Esta app a\xC3\xBA" "n no es compatible con Carpeta segura: su sesi\xC3\xB3n y su cach\xC3\xA9 viven en el servicio de navegaci\xC3\xB3n, fuera de la placa";
  if(appId == FXV_APP_PAINT)
    return "Esta app a\xC3\xBA" "n no es compatible con Carpeta segura: guarda cada trazo directamente en el fichero mientras dibujas";
  if(appId == FXV_APP_CALEND)
    return "Esta app a\xC3\xBA" "n no es compatible con Carpeta segura: todav\xC3\xAD" "a no guarda datos propios que separar";
  return "Esta app a\xC3\xBA" "n no es compatible con Carpeta segura";
}

bool flexVaultAppAdded(int appId){
  if(appId < 0 || appId > 15) return false;
  return (vAppMask & (uint16_t)(1u << appId)) != 0;
}

bool flexVaultAppAdd(int appId){
  if(appId < 0 || appId > 15) return false;
  if(!flexVaultAppSupported(appId)) return false;
  if(!vOpen) return false;                      // anadir apps exige la boveda abierta
  vAppMask |= (uint16_t)(1u << appId);
  fxvSaveSettings();
  flexVaultLogAdd(FXV_EV_APPADD, (int8_t)appId);
  return true;
}

bool flexVaultAppRemove(int appId, int dataAction){
  if(appId < 0 || appId > 15) return false;
  if(!vOpen) return false;
  if(!flexVaultAppAdded(appId)) return true;

  if(dataAction == FXV_APPDATA_EXPORT){
    // Los datos vuelven a la app normal. Se recorre al reves porque
    // exportar quita elementos del indice.
    for(int i = vIdxN - 1; i >= 0; i--){
      if(vIdx[i].appId != (int8_t)appId) continue;
      uint16_t id = vIdx[i].id;
      if(!flexVaultExport(id, NULL, 0)) return false;
    }
  } else if(dataAction == FXV_APPDATA_WIPE){
    for(int i = vIdxN - 1; i >= 0; i--){
      if(vIdx[i].appId != (int8_t)appId) continue;
      if(!flexVaultDelete(vIdx[i].id)) return false;
    }
    flexVaultLogAdd(FXV_EV_WIPE, (int8_t)appId);
  }
  // FXV_APPDATA_KEEP: no se toca nada. Los elementos siguen cifrados
  // dentro y volveran a aparecer si la app se anade otra vez.

  vAppMask &= (uint16_t)~(1u << appId);
  vAppLock &= (uint16_t)~(1u << appId);
  fxvSaveSettings();
  flexVaultLogAdd(FXV_EV_APPDEL, (int8_t)appId);
  return true;
}

bool flexVaultAppLocked(int appId){
  if(appId < 0 || appId > 15) return false;
  return (vAppLock & (uint16_t)(1u << appId)) != 0;
}

void flexVaultAppSetLocked(int appId, bool on){
  if(appId < 0 || appId > 15) return;
  if(on) vAppLock |=  (uint16_t)(1u << appId);
  else   vAppLock &= (uint16_t)~(1u << appId);
  fxvSaveSettings();
}

uint32_t flexVaultAppBytes(int appId){
  if(!vOpen || !vIdx) return 0;
  uint32_t total = 0;
  for(int i = 0; i < vIdxN; i++){
    if(vIdx[i].appId != (int8_t)appId) continue;
    char blob[FLEXFS_PATH_MAX];
    vaultBlobPath(vIdx[i].id, blob, sizeof(blob));
    total += flexFsPrivSize(blob);        // bytes REALES en la particion
  }
  return total;
}

uint32_t flexVaultAppLast(int appId){
  if(appId < 0 || appId > 15) return 0;
  return vAppTouch[appId];
}

void flexVaultAppTouch(int appId){
  if(appId < 0 || appId > 15) return;
  vAppTouch[appId] = vClock;
  fxvSaveSettings();
}

void flexVaultSetAutoLockMs(uint32_t ms){
  vAutoLock = ms;
  fxvSaveSettings();
}
