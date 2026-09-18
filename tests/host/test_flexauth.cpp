// #############################################################
//  test_flexauth.cpp  ·  emparejamiento y sesion de Flex Phone
// #############################################################
//
//  POR QUE EXISTE
//  --------------
//  Al pasar el enlace de BLE a Wi-Fi se pierde el bonding, que era
//  quien autenticaba. Si la derivacion de la clave o el HMAC estan
//  mal, NO se nota: el emparejamiento "funciona" igual en las dos
//  puntas porque las dos hacen el mismo error. Lo que se rompe es
//  la garantia, en silencio.
//
//  Por eso SHA-256 y HMAC-SHA256 se comparan contra los vectores
//  publicados (FIPS 180-4 y RFC 4231), no contra lo que produzca
//  esta implementacion.
//
//  Y el mismo material derivado se fija en Kotlin:
//      android/.../protocol/src/test/kotlin/.../FlexLinkTest.kt
//  para que el telefono y el reloj no puedan derivar claves
//  distintas sin que salte una de las dos baterias.

#include "../../FlexOS_Ultra/FlexOS_FlexAuth.h"
#include <cstdio>
#include <cstring>

static int g_fail = 0, g_run = 0;

static void hex(const uint8_t* b, size_t n, char* out){
  static const char* D = "0123456789abcdef";
  size_t at = 0;
  for(size_t i = 0; i < n; i++){ out[at++] = D[b[i] >> 4]; out[at++] = D[b[i] & 0xF]; }
  out[at] = 0;
}

static void eq(const char* name, const uint8_t* got, size_t n, const char* want){
  g_run++;
  char h[160];
  hex(got, n, h);
  if(std::strcmp(h, want) != 0){
    g_fail++;
    std::printf("  FALLO  %s\n         obtenido %s\n         esperado %s\n", name, h, want);
  } else {
    std::printf("   %-34s ok\n", name);
  }
}

static void ok(const char* name, bool cond){
  g_run++;
  if(!cond){ g_fail++; std::printf("  FALLO  %s\n", name); }
  else std::printf("   %-34s ok\n", name);
}

int main(){
  std::printf("\n=== FlexOS · emparejamiento y sesion de Flex Phone ===\n");

  // ---------------------------------------------------------
  //  1) SHA-256 contra los vectores del FIPS 180-4
  // ---------------------------------------------------------
  {
    uint8_t d[FLXA_SHA256_SIZE];
    flexSha256("", 0, d);
    eq("sha256(\"\")", d, sizeof(d),
       "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    flexSha256("abc", 3, d);
    eq("sha256(\"abc\")", d, sizeof(d),
       "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    const char* m2 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    flexSha256(m2, std::strlen(m2), d);
    eq("sha256(56 bytes)", d, sizeof(d),
       "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    // Un millon de 'a': ejercita el troceado en bloques y el contador
    // de bits de 64 bits, que es donde un desbordamiento pasaria
    // inadvertido con mensajes cortos.
    FlexSha256 c;
    flexSha256Init(&c);
    char chunk[1000];
    std::memset(chunk, 'a', sizeof(chunk));
    for(int i = 0; i < 1000; i++) flexSha256Update(&c, chunk, sizeof(chunk));
    flexSha256Final(&c, d);
    eq("sha256(1e6 x 'a')", d, sizeof(d),
       "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  }

  // ---------------------------------------------------------
  //  2) HMAC-SHA256 contra el RFC 4231
  // ---------------------------------------------------------
  {
    uint8_t d[FLXA_SHA256_SIZE];
    uint8_t k1[20];
    std::memset(k1, 0x0b, sizeof(k1));
    flexHmacSha256(k1, sizeof(k1), "Hi There", 8, d);
    eq("hmac rfc4231 caso 1", d, sizeof(d),
       "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");

    flexHmacSha256((const uint8_t*)"Jefe", 4,
                   "what do ya want for nothing?", 28, d);
    eq("hmac rfc4231 caso 2", d, sizeof(d),
       "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");

    // Caso 6: clave de 131 bytes -- mas larga que el bloque, asi que
    // entra por el camino que primero la resume.
    uint8_t k6[131];
    std::memset(k6, 0xaa, sizeof(k6));
    const char* m6 = "Test Using Larger Than Block-Size Key - Hash Key First";
    flexHmacSha256(k6, sizeof(k6), m6, std::strlen(m6), d);
    eq("hmac rfc4231 caso 6 (clave larga)", d, sizeof(d),
       "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
  }

  // ---------------------------------------------------------
  //  3) Comparacion en tiempo constante
  // ---------------------------------------------------------
  {
    uint8_t a[8] = {1,2,3,4,5,6,7,8};
    uint8_t b[8] = {1,2,3,4,5,6,7,8};
    ok("iguales", flexAuthEqual(a, b, 8));
    b[7] = 9;
    ok("distintos en el ultimo byte", !flexAuthEqual(a, b, 8));
    b[7] = 8; b[0] = 9;
    ok("distintos en el primero", !flexAuthEqual(a, b, 8));
  }

  // ---------------------------------------------------------
  //  4) Derivacion de la clave del vinculo
  // ---------------------------------------------------------
  uint8_t salt[FLXA_SALT_SIZE];
  for(size_t i = 0; i < sizeof(salt); i++) salt[i] = (uint8_t)i;

  uint8_t key[FLXA_KEY_SIZE], key2[FLXA_KEY_SIZE];
  flexAuthDeriveKey("012345", salt, "flexos-1", "phone-1", key);
  // VECTOR DORADO. El mismo valor esta fijado en FlexLinkTest.kt: si
  // alguien cambia la derivacion en un solo lado, el telefono y el
  // reloj derivan claves distintas y el emparejamiento falla sin que
  // ninguno de los dos pueda decir por que.
  eq("clave del vinculo (vector dorado)", key, sizeof(key),
     "66840f7c66ababb9f042d87c18e1f6b28d1bf08db58316823bb99edb38954176");

  {
    // Un codigo distinto da una clave distinta. Es lo que hace que
    // teclear mal el codigo no empareje.
    flexAuthDeriveKey("012346", salt, "flexos-1", "phone-1", key2);
    ok("otro codigo -> otra clave", !flexAuthEqual(key, key2, FLXA_KEY_SIZE));

    // Una sal distinta tambien. Cada emparejamiento genera la suya,
    // asi que repetir el mismo codigo no reconstruye el vinculo viejo.
    uint8_t salt2[FLXA_SALT_SIZE];
    std::memcpy(salt2, salt, sizeof(salt2));
    salt2[0] ^= 0xFF;
    flexAuthDeriveKey("012345", salt2, "flexos-1", "phone-1", key2);
    ok("otra sal -> otra clave", !flexAuthEqual(key, key2, FLXA_KEY_SIZE));

    // Y los identificadores entran con su longitud delante, asi que
    // ("ab","c") y ("a","bc") NO pueden colisionar.
    uint8_t ka[FLXA_KEY_SIZE], kb[FLXA_KEY_SIZE];
    flexAuthDeriveKey("012345", salt, "ab", "c",  ka);
    flexAuthDeriveKey("012345", salt, "a",  "bc", kb);
    ok("los ids no se pueden confundir", !flexAuthEqual(ka, kb, FLXA_KEY_SIZE));
  }

  // ---------------------------------------------------------
  //  5) Reto-respuesta mutuo
  // ---------------------------------------------------------
  {
    uint8_t nonce[FLXA_NONCE_SIZE];
    for(size_t i = 0; i < sizeof(nonce); i++) nonce[i] = (uint8_t)(0xA0 + i);

    uint8_t pPhone[FLXA_PROOF_SIZE], pHost[FLXA_PROOF_SIZE];
    flexAuthProof(key, FLXA_ROLE_PHONE, nonce, 0x1234, pPhone);
    flexAuthProof(key, FLXA_ROLE_HOST,  nonce, 0x1234, pHost);

    ok("la prueba del telefono vale",
       flexAuthVerify(key, FLXA_ROLE_PHONE, nonce, 0x1234, pPhone));
    ok("la prueba de Flex OS vale",
       flexAuthVerify(key, FLXA_ROLE_HOST, nonce, 0x1234, pHost));

    // LO IMPORTANTE: las dos pruebas son distintas. Si fueran iguales,
    // quien escuche la del telefono podria hacerse pasar por Flex OS
    // devolviendosela, y el usuario acabaria mandando sus
    // notificaciones a un equipo cualquiera de la red.
    ok("reenviar la prueba ajena no cuela",
       !flexAuthVerify(key, FLXA_ROLE_HOST, nonce, 0x1234, pPhone));

    // Otro reto, otra sesion u otra clave: no vale.
    uint8_t other[FLXA_NONCE_SIZE];
    std::memcpy(other, nonce, sizeof(other));
    other[0] ^= 0x01;
    ok("otro reto no vale",
       !flexAuthVerify(key, FLXA_ROLE_PHONE, other, 0x1234, pPhone));
    ok("otra sesion no vale",
       !flexAuthVerify(key, FLXA_ROLE_PHONE, nonce, 0x1235, pPhone));
    ok("otra clave no vale",
       !flexAuthVerify(key2, FLXA_ROLE_PHONE, nonce, 0x1234, pPhone));
  }

  // ---------------------------------------------------------
  //  6) Codigo de emparejamiento
  // ---------------------------------------------------------
  {
    char c[7];
    flexAuthFormatCode(0, c);
    ok("el codigo lleva siempre 6 digitos", std::strcmp(c, "000000") == 0);
    flexAuthFormatCode(42, c);
    ok("ceros a la izquierda", std::strcmp(c, "000042") == 0);
    flexAuthFormatCode(1234567u, c);           // se reduce por modulo 10^6
    ok("reduccion a 6 digitos", std::strcmp(c, "234567") == 0);
    flexAuthFormatCode(0xFFFFFFFFu, c);
    g_run++;
    bool digits = std::strlen(c) == 6;
    for(int i = 0; i < 6 && digits; i++) digits = c[i] >= '0' && c[i] <= '9';
    if(!digits){ g_fail++; std::printf("  FALLO  codigo fuera de rango: %s\n", c); }
    else std::printf("   %-34s ok\n", "cualquier semilla da digitos");
  }

  // ---------------------------------------------------------
  //  7) Bytes aleatorios sin fuente: cero, no basura de pila
  // ---------------------------------------------------------
  {
    uint8_t b[20];
    std::memset(b, 0xAB, sizeof(b));
    flexAuthRandomBytes(NULL, b, sizeof(b));
    bool zero = true;
    for(size_t i = 0; i < sizeof(b); i++) if(b[i]) zero = false;
    ok("sin fuente de azar se rellena a cero", zero);

    // Con una fuente que da 32 bits por llamada, un tamano que no es
    // multiplo de 4 tiene que rellenarse entero igualmente.
    static uint32_t seq = 0;
    seq = 0;
    struct F { static uint32_t next(){ return ++seq; } };
    uint8_t c[7];
    std::memset(c, 0, sizeof(c));
    flexAuthRandomBytes(&F::next, c, sizeof(c));
    bool any = false;
    for(size_t i = 0; i < sizeof(c); i++) if(c[i]) any = true;
    ok("tamano no multiplo de 4", any);
  }

  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
