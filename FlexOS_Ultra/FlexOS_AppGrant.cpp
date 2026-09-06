// #############################################################
//  FLEX OS · GRANT DE PERMISOS FLXG v1  ·  implementación
//  ------------------------------------------------------------
//  Ver FlexOS_AppGrant.h para el contrato y la disposición del bloque.
//
//  BACKEND CRIPTOGRAFICO. Mismo patrón que FlexOS_Vault.cpp: en la
//  placa mbedTLS (con el acelerador del P4 por debajo), en el PC
//  OpenSSL. Las dos rutas comprueban EXACTAMENTE lo mismo sobre los
//  mismos bytes; lo único que cambia es quién hace la aritmética.
// #############################################################
#include "FlexOS_AppGrant.h"

#include <string.h>

#if defined(__has_include)
#  if __has_include(<mbedtls/ecdsa.h>)
#    define FXG_BACKEND_MBEDTLS 1
#  elif __has_include(<openssl/evp.h>)
#    define FXG_BACKEND_OPENSSL 1
#  endif
#endif
#if !defined(FXG_BACKEND_MBEDTLS) && !defined(FXG_BACKEND_OPENSSL)
#  error "FlexOS_AppGrant: falta un backend criptografico (mbedTLS u OpenSSL)"
#endif

#if FXG_BACKEND_MBEDTLS
#  include "mbedtls/bignum.h"
#  include "mbedtls/ecdsa.h"
#  include "mbedtls/ecp.h"
#  include "mbedtls/sha256.h"
#  include "mbedtls/version.h"
#  if MBEDTLS_VERSION_NUMBER >= 0x03000000
#    define FXG_SHA_START(c)      mbedtls_sha256_starts((c), 0)
#    define FXG_SHA_UPDATE(c,p,n) mbedtls_sha256_update((c),(p),(n))
#    define FXG_SHA_FINISH(c,o)   mbedtls_sha256_finish((c),(o))
#  else
#    define FXG_SHA_START(c)      mbedtls_sha256_starts_ret((c), 0)
#    define FXG_SHA_UPDATE(c,p,n) mbedtls_sha256_update_ret((c),(p),(n))
#    define FXG_SHA_FINISH(c,o)   mbedtls_sha256_finish_ret((c),(o))
#  endif
#else
// Ruta SOLO DE PC (las pruebas de host). OpenSSL 3.0 marca la API EC_KEY como
// obsoleta pero sigue siendo la equivalente exacta de lo que hace mbedTLS en la
// placa, que es lo que interesa aqui: el mismo calculo sobre los mismos bytes.
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#  include <openssl/bn.h>
#  include <openssl/ec.h>
#  include <openssl/ecdsa.h>
#  include <openssl/evp.h>
#  include <openssl/obj_mac.h>
#endif

namespace {

struct PermName { uint32_t bit; const char* name; };
const PermName kPerms[] = {
  { FLEXPERM_SYS_BENCHMARK_RUN,       "benchmark.run" },
  { FLEXPERM_SYS_CPU_STRESS,          "system.cpu.stress" },
  { FLEXPERM_SYS_PSRAM_MEASURE,       "system.psram.measure" },
  { FLEXPERM_SYS_TEMPERATURE_READ,    "system.temperature.read" },
  { FLEXPERM_SYS_PERF_METRICS, "system.performance.metrics" },
  { FLEXPERM_SYS_DISPLAY_LANDSCAPE,   "display.landscape" },
  { FLEXPERM_SYS_DISPLAY_EXCLUSIVE,   "display.exclusive" },
  { FLEXPERM_SYS_STORAGE_APP,         "storage.app" }
};
const size_t kPermCount = sizeof(kPerms) / sizeof(kPerms[0]);

inline uint16_t rd16(const uint8_t* p){ return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
inline uint32_t rd32(const uint8_t* p){
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline uint64_t rd64(const uint8_t* p){
  return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

// Compara un campo ASCII de ancho fijo, relleno a cero, con una cadena C.
// Exige relleno LIMPIO: un solo byte distinto de cero después del final
// invalida el grant. Así no hay dos codificaciones del mismo nombre.
bool fixedEquals(const uint8_t* field, uint32_t width, const char* text){
  if(!text) return false;
  size_t n = strlen(text);
  if(n > width) return false;
  if(memcmp(field, text, n) != 0) return false;
  for(uint32_t i = (uint32_t)n; i < width; i++) if(field[i] != 0) return false;
  return true;
}

uint32_t popcount32(uint32_t v){
  uint32_t c = 0;
  while(v){ c += (v & 1u); v >>= 1; }
  return c;
}

// Comparación en tiempo constante: un hash no se compara con memcmp para no
// filtrar por dónde deja de coincidir.
bool sameHash(const uint8_t* a, const uint8_t* b){
  uint8_t diff = 0;
  for(int i = 0; i < 32; i++) diff = (uint8_t)(diff | (a[i] ^ b[i]));
  return diff == 0;
}

#if FXG_BACKEND_MBEDTLS
bool verifyP256(const uint8_t pub[65], const uint8_t hash[32], const uint8_t sig[64]){
  // WebCrypto puede emitir firmas con S alto. (r, N-s) es LA MISMA firma, y
  // algunos backends sólo aceptan la forma canónica: normalizar aquí no
  // relaja nada. Mismo criterio que verifyCatalogSignature en FlexOS_Store.
  static const uint8_t ORDER[32] = {
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xbc,0xe6,0xfa,0xad,0xa7,0x17,0x9e,0x84,0xf3,0xb9,0xca,0xc2,0xfc,0x63,0x25,0x51
  };
  static const uint8_t HALF[32] = {
    0x7f,0xff,0xff,0xff,0x80,0x00,0x00,0x00,0x7f,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xde,0x73,0x7d,0x56,0xd3,0x8b,0xcf,0x42,0x79,0xdc,0xe5,0x61,0x7e,0x31,0x92,0xa8
  };
  if(pub[0] != 0x04) return false;
  mbedtls_ecp_group grp; mbedtls_ecp_group_init(&grp);
  mbedtls_ecp_point q; mbedtls_ecp_point_init(&q);
  mbedtls_mpi r, s, order, half;
  mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
  mbedtls_mpi_init(&order); mbedtls_mpi_init(&half);
  int rc = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
  if(rc == 0) rc = mbedtls_ecp_point_read_binary(&grp, &q, pub, 65);
  if(rc == 0) rc = mbedtls_ecp_check_pubkey(&grp, &q);
  if(rc == 0) rc = mbedtls_mpi_read_binary(&r, sig, 32);
  if(rc == 0) rc = mbedtls_mpi_read_binary(&s, sig + 32, 32);
  if(rc == 0) rc = mbedtls_mpi_read_binary(&order, ORDER, sizeof(ORDER));
  if(rc == 0) rc = mbedtls_mpi_read_binary(&half, HALF, sizeof(HALF));
  if(rc == 0 && mbedtls_mpi_cmp_mpi(&s, &half) > 0) rc = mbedtls_mpi_sub_mpi(&s, &order, &s);
  if(rc == 0) rc = mbedtls_ecdsa_verify(&grp, hash, 32, &q, &r, &s);
  mbedtls_mpi_free(&half); mbedtls_mpi_free(&order);
  mbedtls_mpi_free(&s); mbedtls_mpi_free(&r);
  mbedtls_ecp_point_free(&q); mbedtls_ecp_group_free(&grp);
  return rc == 0;
}
#else
bool verifyP256(const uint8_t pub[65], const uint8_t hash[32], const uint8_t sig[64]){
  if(pub[0] != 0x04) return false;
  bool ok = false;
  EC_KEY* key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
  ECDSA_SIG* es = nullptr;
  BIGNUM* r = nullptr; BIGNUM* s = nullptr;
  if(!key) goto done;
  if(EC_KEY_oct2key(key, pub, 65, nullptr) != 1) goto done;
  if(EC_KEY_check_key(key) != 1) goto done;
  r = BN_bin2bn(sig, 32, nullptr);
  s = BN_bin2bn(sig + 32, 32, nullptr);
  if(!r || !s) goto done;
  es = ECDSA_SIG_new();
  if(!es) goto done;
  if(ECDSA_SIG_set0(es, r, s) != 1) goto done;
  r = nullptr; s = nullptr;      // ECDSA_SIG se queda con ellos
  ok = (ECDSA_do_verify(hash, 32, es, key) == 1);
done:
  if(es) ECDSA_SIG_free(es);
  if(r) BN_free(r);
  if(s) BN_free(s);
  if(key) EC_KEY_free(key);
  return ok;
}
#  pragma GCC diagnostic pop
#endif

} // namespace

// -------------------------------------------------------------------------
bool flexGrantSha256(const uint8_t* data, size_t len, uint8_t out[32]){
  if((!data && len) || !out) return false;
#if FXG_BACKEND_MBEDTLS
  mbedtls_sha256_context c; mbedtls_sha256_init(&c);
  bool ok = FXG_SHA_START(&c) == 0 && FXG_SHA_UPDATE(&c, data, len) == 0 &&
            FXG_SHA_FINISH(&c, out) == 0;
  mbedtls_sha256_free(&c);
  return ok;
#else
  unsigned int n = 0;
  EVP_MD_CTX* c = EVP_MD_CTX_new();
  if(!c) return false;
  bool ok = EVP_DigestInit_ex(c, EVP_sha256(), nullptr) == 1 &&
            EVP_DigestUpdate(c, data, len) == 1 &&
            EVP_DigestFinal_ex(c, out, &n) == 1 && n == 32;
  EVP_MD_CTX_free(c);
  return ok;
#endif
}

uint32_t flexSysPermissionBit(const char* name){
  if(!name) return 0;
  for(size_t i = 0; i < kPermCount; i++)
    if(!strcmp(name, kPerms[i].name)) return kPerms[i].bit;
  return 0;
}

const char* flexSysPermissionName(uint32_t bit){
  for(size_t i = 0; i < kPermCount; i++)
    if(kPerms[i].bit == bit) return kPerms[i].name;
  return "";
}

FlexGrantStatus flexGrantCheck(const uint8_t* blob, uint32_t len,
                               const FlexGrantExpect* expect,
                               const uint8_t trustedPub[65],
                               FlexGrantResult* out){
  if(out){ out->granted = 0; out->notBefore = 0; out->notAfter = 0; out->windowChecked = 0; }
  if(!expect || !trustedPub) return FLEXGRANT_ERR_SIZE;
  if(!blob || len == 0) return FLEXGRANT_ABSENT;
  if(len != FLEXGRANT_BYTES) return FLEXGRANT_ERR_SIZE;

  // 1) Forma del bloque -----------------------------------------------------
  if(blob[0] != 'F' || blob[1] != 'L' || blob[2] != 'X' || blob[3] != 'G') return FLEXGRANT_ERR_MAGIC;
  if(rd16(blob + 4) != FLEXGRANT_FORMAT_VERSION || rd16(blob + 6) != 0) return FLEXGRANT_ERR_MAGIC;
  if(rd32(blob + 228) != 0) return FLEXGRANT_ERR_MAGIC;

  // 2) Propósito ------------------------------------------------------------
  if(rd16(blob + 8) != FLEXGRANT_PURPOSE_APP) return FLEXGRANT_ERR_PURPOSE;

  // 3) Identidad del paquete -----------------------------------------------
  if(!fixedEquals(blob + 96, FLEXGRANT_ID_BYTES, expect->packageId)) return FLEXGRANT_ERR_PACKAGE;

  // 4) Versión (nombre Y código; los dos, no uno) ---------------------------
  if(!fixedEquals(blob + 192, FLEXGRANT_VERNAME_BYTES, expect->versionName)) return FLEXGRANT_ERR_VERSION;
  if(rd32(blob + 12) != expect->versionCode) return FLEXGRANT_ERR_VERSION;

  // 5) SHA-256 del paquete REALMENTE instalado ------------------------------
  if(!sameHash(blob + 32, expect->packageSha256)) return FLEXGRANT_ERR_HASH;

  // 6) SHA-256 de la clave del desarrollador --------------------------------
  if(!sameHash(blob + 64, expect->developerKeySha256)) return FLEXGRANT_ERR_DEVELOPER;

  // 7) Permisos: conocidos, coherentes y no mayores que lo que pide el
  //    manifest. Un grant no puede conceder algo que la app ni siquiera
  //    declaró: si el manifest y el grant no cuadran, algo se manipuló.
  uint32_t mask = rd32(blob + 224);
  if(mask == 0 || (mask & ~FLEXPERM_SYS_ALL) != 0) return FLEXGRANT_ERR_PERMS;
  if(popcount32(mask) != (uint32_t)rd16(blob + 10)) return FLEXGRANT_ERR_PERMS;
  if((mask & ~expect->manifestRequested) != 0) return FLEXGRANT_ERR_MANIFEST;

  // 8) Ventana de validez ---------------------------------------------------
  uint64_t nb = rd64(blob + 16), na = rd64(blob + 24);
  if(nb && na && nb > na) return FLEXGRANT_ERR_WINDOW;
  bool windowChecked = false;
  if(nb || na){
    if(expect->nowEpoch == 0){
      // El reloj todavía no es fiable (sin NTP tras un arranque en frío).
      // NO se concede de más por eso: sólo se OMITE la comprobación temporal
      // y se deja constancia para que la interfaz pueda decirlo.
      windowChecked = false;
    } else {
      if(nb && expect->nowEpoch < nb) return FLEXGRANT_ERR_WINDOW;
      if(na && expect->nowEpoch > na) return FLEXGRANT_ERR_WINDOW;
      windowChecked = true;
    }
  } else {
    windowChecked = true;   // sin ventana declarada: nada que comprobar
  }

  // 9) Y AL FINAL la firma --------------------------------------------------
  uint8_t hash[32];
  if(!flexGrantSha256(blob, FLEXGRANT_SIGNED_BYTES, hash)) return FLEXGRANT_ERR_SIGNATURE;
  if(!verifyP256(trustedPub, hash, blob + FLEXGRANT_SIGNED_BYTES)) return FLEXGRANT_ERR_SIGNATURE;

  if(out){
    out->granted = mask;
    out->notBefore = nb;
    out->notAfter = na;
    out->windowChecked = windowChecked ? 1 : 0;
  }
  return FLEXGRANT_OK;
}

const char* flexGrantStatusText(uint8_t status){
  switch(status){
    case FLEXGRANT_OK:            return "Permisos concedidos";
    case FLEXGRANT_ABSENT:        return "La app no trae permisos de sistema";
    case FLEXGRANT_ERR_SIZE:      return "El permiso firmado tiene un tamano invalido";
    case FLEXGRANT_ERR_MAGIC:     return "El permiso firmado no es un grant de Flex Store";
    case FLEXGRANT_ERR_PURPOSE:   return "El grant no es para permisos de aplicacion";
    case FLEXGRANT_ERR_PACKAGE:   return "El grant es de otra aplicacion";
    case FLEXGRANT_ERR_VERSION:   return "El grant es de otra version";
    case FLEXGRANT_ERR_HASH:      return "El grant no corresponde al paquete instalado";
    case FLEXGRANT_ERR_DEVELOPER: return "El grant es de otro desarrollador";
    case FLEXGRANT_ERR_PERMS:     return "El grant declara permisos invalidos";
    case FLEXGRANT_ERR_MANIFEST:  return "El grant concede mas de lo que pide la app";
    case FLEXGRANT_ERR_WINDOW:    return "El grant esta fuera de su periodo de validez";
    case FLEXGRANT_ERR_SIGNATURE: return "La firma del grant no es de Flex Store";
    default:                      return "Permiso denegado";
  }
}
