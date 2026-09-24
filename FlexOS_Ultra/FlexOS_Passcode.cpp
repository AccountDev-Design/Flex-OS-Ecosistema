// #############################################################
// ##  FlexOS · CLAVE DEL SISTEMA  ·  implementacion
// ##  Sal, PBKDF2-HMAC-SHA256, comparacion en tiempo constante y
// ##  verificacion a plazos. Ver FlexOS_Passcode.h para el porque.
// #############################################################
//
//  REGLA DE ORO DE ESTE FICHERO
//  ----------------------------
//  1. Ninguna clave se imprime NUNCA por el puerto serie. Ni en
//     depuracion. Los errores se cuentan como codigo, jamas con
//     datos.
//  2. Todo secreto que entre en RAM se BORRA (flexLockWipe) en
//     todos los caminos de salida, tambien en los de error.
//  3. Una escritura que no se pueda completar no deja al usuario
//     sin clave: se comprueba lo escrito ANTES de borrar lo viejo.

#include <Arduino.h>
#include "FlexOS_Passcode.h"
#include <string.h>
#include <Preferences.h>

// -------------------------------------------------------------
//  BACKEND CRIPTOGRAFICO
//  ------------------------------------------------------------
//  En la placa: mbedTLS, tal como viene en el core de arduino-esp32.
//  Es la implementacion del fabricante y usa el ACELERADOR SHA del
//  ESP32-P4.
//
//  En las pruebas de host: OpenSSL. Lo que las pruebas verifican de
//  verdad es la LOGICA de esta capa -- la sal, el formato en NVS, la
//  migracion y que la verificacion a plazos de EXACTAMENTE el mismo
//  hash que la de una sentada. La primitiva HMAC la firma una
//  biblioteca auditada en los dos casos.
//
//  mbedtls_md_hmac tiene el MISMO nombre y la misma firma en mbedTLS
//  2.x y 3.x, a proposito: asi el modulo no se rompe con una
//  actualizacion del core. Las variantes con sufijo _ret y las de
//  pkcs5 cambiaron entre versiones y no se usan.
// -------------------------------------------------------------
#if defined(__has_include)
#  if __has_include(<mbedtls/md.h>)
#    define FLEXLOCK_BACKEND_MBEDTLS 1
#  elif __has_include(<openssl/evp.h>)
#    define FLEXLOCK_BACKEND_OPENSSL 1
#  endif
#endif

#if !defined(FLEXLOCK_BACKEND_MBEDTLS) && !defined(FLEXLOCK_BACKEND_OPENSSL)
#  error "La clave del sistema necesita mbedTLS (placa) u OpenSSL (pruebas de host)"
#endif

#if FLEXLOCK_BACKEND_MBEDTLS
#  include <mbedtls/md.h>
#  include <esp_random.h>
#else
#  include <openssl/hmac.h>
#  include <openssl/evp.h>
#  include <openssl/rand.h>
#endif

#define FLEXLOCK_KEY_LEN  32          // PBKDF2-HMAC-SHA256 de 32 bytes
#define FLEXLOCK_SALT_LEN 16

// Namespace de NVS: el MISMO que el resto de los ajustes del sistema
// ("flexos"), porque locktype ya vive ahi y el sketch lo lee en su
// arranque. Las claves son:
//    lockslt  sal de 16 bytes
//    lockhsh  PBKDF2-HMAC-SHA256 de 32 bytes
//    lockitr  iteraciones con las que se calculo
//    locklen  longitud del PIN (para autoconfirmar en pantalla)
// Y las que desaparecen: lockpin, lockpass.
#define FLEXLOCK_NS "flexos"

// Iteraciones del bloqueo de pantalla. Se evalua al desbloquear el
// aparato (varias veces al dia) y no protege un almacen cifrado, solo
// la entrada a la interfaz. 12.000 con el acelerador SHA del P4 son
// unas pocas decenas de milisegundos, y desde que la verificacion va
// a plazos ese tiempo ya no se paga de una sentada.
#define FLEXLOCK_ITERS 12000

// -------------------------------------------------------------
//  PRIMITIVAS
// -------------------------------------------------------------
void flexLockWipe(void* p, size_t n){
  // volatile para que el compilador NO pueda eliminar el borrado por
  // "nadie lee esto despues". Es el fallo clasico de un memset de
  // limpieza: se optimiza y la clave se queda en la pila.
  if(!p) return;
  volatile uint8_t* q = (volatile uint8_t*)p;
  while(n--) *q++ = 0;
}

bool flexLockEqualCT(const void* a, const void* b, size_t n){
  const uint8_t* x = (const uint8_t*)a;
  const uint8_t* y = (const uint8_t*)b;
  uint8_t d = 0;
  for(size_t i = 0; i < n; i++) d |= (uint8_t)(x[i] ^ y[i]);
  return d == 0;
}

void flexLockRandomBytes(void* out, size_t n){
#if FLEXLOCK_BACKEND_MBEDTLS
  // esp_fill_random usa el generador de hardware del chip: ruido
  // fisico, no un PRNG con semilla fija.
  esp_fill_random(out, n);
#else
  RAND_bytes((unsigned char*)out, (int)n);
#endif
}

static void flexLockHmac(const uint8_t* key, size_t keyLen,
                         const uint8_t* in, size_t inLen, uint8_t out[32]){
#if FLEXLOCK_BACKEND_MBEDTLS
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                  key, keyLen, in, inLen, out);
#else
  unsigned int n = 32;
  HMAC(EVP_sha256(), key, (int)keyLen, in, inLen, out, &n);
#endif
}

// PBKDF2-HMAC-SHA256 con dkLen <= 32 (un solo bloque). Se implementa
// aqui, sobre HMAC, en vez de llamar a la API pkcs5 de mbedTLS: esa
// cambio de nombre entre la 2.x y la 3.x y no merece la pena atar el
// arranque del sistema a eso. Son doce lineas y el resultado es
// identico, comprobado contra vectores en las pruebas.
void flexLockKdf(const char* secret, const uint8_t* salt, size_t saltLen,
                 uint32_t iters, uint8_t* out, size_t outLen){
  if(!out || outLen == 0) return;
  if(outLen > 32) outLen = 32;
  if(iters == 0) iters = 1;
  size_t sl = secret ? strlen(secret) : 0;

  // U1 = HMAC(clave, sal || 0x00000001)
  uint8_t blk[FLEXLOCK_SALT_LEN + 4 + 32];
  size_t  bl = saltLen > sizeof(blk) - 4 ? sizeof(blk) - 4 : saltLen;
  memcpy(blk, salt, bl);
  blk[bl + 0] = 0; blk[bl + 1] = 0; blk[bl + 2] = 0; blk[bl + 3] = 1;

  uint8_t u[32], acc[32];
  flexLockHmac((const uint8_t*)secret, sl, blk, bl + 4, u);
  memcpy(acc, u, 32);
  for(uint32_t i = 1; i < iters; i++){
    flexLockHmac((const uint8_t*)secret, sl, u, 32, u);
    for(int k = 0; k < 32; k++) acc[k] ^= u[k];
  }
  memcpy(out, acc, outLen);
  flexLockWipe(u, sizeof(u));
  flexLockWipe(acc, sizeof(acc));
  flexLockWipe(blk, sizeof(blk));
}

// -------------------------------------------------------------
//  LECTURA DE LOS AJUSTES DE BLOQUEO
// -------------------------------------------------------------
int flexLockType(){
  Preferences p;
  p.begin(FLEXLOCK_NS, true);
  int t = p.getInt("locktype", 0);
  p.end();
  return (t == FLEXLOCK_PIN || t == FLEXLOCK_PASS) ? t : FLEXLOCK_NONE;
}

int flexLockLen(){
  Preferences p;
  p.begin(FLEXLOCK_NS, true);
  int t = p.getInt("locktype", 0);
  int l = p.getInt("locklen", 0);
  p.end();
  return t == FLEXLOCK_PIN ? l : 0;
}

// Escribe sal + hash. `dropPlain` decide si tambien se borra la clave
// en texto legible de la version antigua. La migracion lo hace en DOS
// pasos (escribir, comprobar, borrar) para no dejar nunca al usuario
// sin ninguna de las dos: si la escritura del hash falla a medias, la
// clave antigua sigue ahi y el aparato se sigue abriendo.
static bool flexLockStore(const char* secret, int type, bool dropPlain){
  uint8_t salt[FLEXLOCK_SALT_LEN], hash[FLEXLOCK_KEY_LEN];
  flexLockRandomBytes(salt, sizeof(salt));
  flexLockKdf(secret, salt, sizeof(salt), FLEXLOCK_ITERS, hash, sizeof(hash));

  Preferences p;
  p.begin(FLEXLOCK_NS, false);
  bool ok = p.putBytes("lockslt", salt, sizeof(salt)) == sizeof(salt);
  if(ok) ok = p.putBytes("lockhsh", hash, sizeof(hash)) == sizeof(hash);
  if(ok) p.putUInt("lockitr", FLEXLOCK_ITERS);
  if(ok) p.putInt("locklen", (int)strlen(secret));
  if(ok) p.putInt("locktype", type);
  if(ok && dropPlain){
    p.remove("lockpin");
    p.remove("lockpass");
  }
  p.end();
  flexLockWipe(hash, sizeof(hash));
  flexLockWipe(salt, sizeof(salt));
  return ok;
}

bool flexLockSet(const char* secret, int type){
  if(!secret || !secret[0]) return false;
  if(type != FLEXLOCK_PIN && type != FLEXLOCK_PASS) return false;
  // Al poner una clave NUEVA se borra de NVS cualquier resto en texto
  // legible de la version anterior: aqui, no "algun dia".
  return flexLockStore(secret, type, true);
}

// -------------------------------------------------------------
//  VERIFICACION A PLAZOS
//  ------------------------------------------------------------
//  El estado vive en el modulo, no en la pantalla: asi el secreto no
//  se queda en una global del sketch y se puede borrar de verdad al
//  terminar. Solo hay UNA verificacion en vuelo -- no tiene sentido
//  comprobar dos claves a la vez -- y empezar otra cancela la
//  anterior.
// -------------------------------------------------------------
static bool     lkvOn      = false;              // hay verificacion en curso
static char     lkvSecret[FLEXLOCK_SECRET_MAX];  // copia del secreto (se borra al acabar)
static uint8_t  lkvU[32], lkvAcc[32], lkvWant[FLEXLOCK_KEY_LEN];
static uint32_t lkvIters   = 0;                  // iteraciones totales
static uint32_t lkvDone    = 0;                  // iteraciones ya hechas

void flexLockVerifyCancel(){
  lkvOn = false;
  lkvIters = lkvDone = 0;
  flexLockWipe(lkvSecret, sizeof(lkvSecret));
  flexLockWipe(lkvU,    sizeof(lkvU));
  flexLockWipe(lkvAcc,  sizeof(lkvAcc));
  flexLockWipe(lkvWant, sizeof(lkvWant));
}

bool flexLockVerifyActive(){ return lkvOn; }

bool flexLockVerifyBegin(const char* secret){
  flexLockVerifyCancel();
  if(!secret) return false;
  if(strlen(secret) >= sizeof(lkvSecret)) return false;   // no cabe: no es una clave valida

  uint8_t salt[FLEXLOCK_SALT_LEN];
  uint32_t iters;
  Preferences p;
  p.begin(FLEXLOCK_NS, true);
  size_t gs = p.getBytes("lockslt", salt, sizeof(salt));
  size_t gh = p.getBytes("lockhsh", lkvWant, sizeof(lkvWant));
  iters = p.getUInt("lockitr", FLEXLOCK_ITERS);
  p.end();
  if(gs != sizeof(salt) || gh != sizeof(lkvWant)){ flexLockVerifyCancel(); return false; }
  if(iters == 0 || iters > 1000000u){ flexLockVerifyCancel(); return false; }

  memcpy(lkvSecret, secret, strlen(secret) + 1);
  // U1 = HMAC(clave, sal || 0x00000001). Es la primera iteracion: se
  // hace aqui para que el paso de abajo sea siempre "una mas de la
  // cadena" y no tenga dos casos.
  uint8_t blk[FLEXLOCK_SALT_LEN + 4];
  memcpy(blk, salt, sizeof(salt));
  blk[sizeof(salt) + 0] = 0; blk[sizeof(salt) + 1] = 0;
  blk[sizeof(salt) + 2] = 0; blk[sizeof(salt) + 3] = 1;
  flexLockHmac((const uint8_t*)lkvSecret, strlen(lkvSecret), blk, sizeof(blk), lkvU);
  memcpy(lkvAcc, lkvU, 32);
  flexLockWipe(blk,  sizeof(blk));
  flexLockWipe(salt, sizeof(salt));
  lkvIters = iters;
  lkvDone  = 1;                         // U1 ya hecha
  lkvOn    = true;
  return true;
}

int flexLockVerifyStep(uint32_t budgetIters){
  if(!lkvOn) return FLEXLOCK_FAIL;
  if(budgetIters == 0) budgetIters = 1;
  size_t sl = strlen(lkvSecret);
  uint32_t n = 0;
  while(lkvDone < lkvIters && n < budgetIters){
    flexLockHmac((const uint8_t*)lkvSecret, sl, lkvU, 32, lkvU);
    for(int k = 0; k < 32; k++) lkvAcc[k] ^= lkvU[k];
    lkvDone++; n++;
  }
  if(lkvDone < lkvIters) return FLEXLOCK_BUSY;
  // Tiempo constante: comparar con memcmp filtraria, por lo que tarda
  // en volver, cuantos bytes del hash coincidian.
  bool ok = flexLockEqualCT(lkvAcc, lkvWant, sizeof(lkvWant));
  flexLockVerifyCancel();
  return ok ? FLEXLOCK_OK : FLEXLOCK_FAIL;
}

bool flexLockVerifyAlone(const char* secret){
  if(!secret || strlen(secret) >= FLEXLOCK_SECRET_MAX) return false;
  uint8_t salt[FLEXLOCK_SALT_LEN], want[FLEXLOCK_KEY_LEN], got[FLEXLOCK_KEY_LEN];
  Preferences p;
  p.begin(FLEXLOCK_NS, true);
  size_t gs = p.getBytes("lockslt", salt, sizeof(salt));
  size_t gh = p.getBytes("lockhsh", want, sizeof(want));
  uint32_t iters = p.getUInt("lockitr", FLEXLOCK_ITERS);
  p.end();
  bool ok = false;
  if(gs == sizeof(salt) && gh == sizeof(want) && iters != 0 && iters <= 1000000u){
    flexLockKdf(secret, salt, sizeof(salt), iters, got, sizeof(got));
    ok = flexLockEqualCT(got, want, sizeof(want));
  }
  flexLockWipe(salt, sizeof(salt));
  flexLockWipe(want, sizeof(want));
  flexLockWipe(got, sizeof(got));
  return ok;
}

bool flexLockVerify(const char* secret){
  // La de una sentada ES la de plazos con un presupuesto infinito: asi
  // no hay dos implementaciones del mismo hash que puedan divergir.
  if(!flexLockVerifyBegin(secret)) return false;
  return flexLockVerifyStep(0xFFFFFFFFu) == FLEXLOCK_OK;
}

bool flexLockClear(){
  flexLockVerifyCancel();               // una verificacion en vuelo ya no tiene contra que comparar
  Preferences p;
  p.begin(FLEXLOCK_NS, false);
  p.remove("lockslt");
  p.remove("lockhsh");
  p.remove("lockitr");
  p.remove("locklen");
  p.remove("lockpin");
  p.remove("lockpass");
  p.putInt("locktype", 0);
  p.end();
  return true;
}

int flexLockMigrate(){
  Preferences p;
  p.begin(FLEXLOCK_NS, true);
  int  type = p.getInt("locktype", 0);
  bool haveHash = false;
  {
    uint8_t probe[FLEXLOCK_KEY_LEN];
    haveHash = p.getBytes("lockhsh", probe, sizeof(probe)) == sizeof(probe);
    flexLockWipe(probe, sizeof(probe));
  }
  String old = (type == FLEXLOCK_PIN) ? p.getString("lockpin", "") : p.getString("lockpass", "");
  p.end();

  if(haveHash){
    // Ya esta migrado. Aun asi se limpia cualquier resto en texto
    // legible: si una version intermedia dejo las dos cosas, el texto
    // legible tiene que irse igual.
    if(old.length() > 0){
      Preferences q;
      q.begin(FLEXLOCK_NS, false);
      q.remove("lockpin");
      q.remove("lockpass");
      q.end();
      return 1;
    }
    return 0;
  }
  if(type == 0 || old.length() == 0) return 0;      // no hay clave que migrar

  char buf[FLEXLOCK_SECRET_MAX];
  old.toCharArray(buf, sizeof(buf));

  // Paso 1: escribir el hash SIN tocar todavia la clave antigua.
  bool ok = flexLockStore(buf, type, false);
  // Paso 2: comprobar que el hash recien escrito valida esa misma
  // clave. Es la parte que convierte esto en una migracion segura:
  // solo si el usuario va a poder entrar con su PIN de siempre se
  // borra el texto legible.
  if(ok) ok = flexLockVerify(buf);
  flexLockWipe(buf, sizeof(buf));
  if(!ok){
    // No se pudo: se deja TODO como estaba (la clave antigua sigue
    // siendo la valida) y se avisa. Preferible a un aparato que no se
    // abre.
    Preferences q;
    q.begin(FLEXLOCK_NS, false);
    q.remove("lockslt");
    q.remove("lockhsh");
    q.remove("lockitr");
    q.end();
    return -1;
  }
  // Paso 3: ahora si, fuera la clave en texto legible.
  Preferences q;
  q.begin(FLEXLOCK_NS, false);
  q.remove("lockpin");
  q.remove("lockpass");
  q.end();
  return 1;
}
