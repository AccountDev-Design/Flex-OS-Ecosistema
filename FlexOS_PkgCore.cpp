// #############################################################
//  FLEX OS · NUCLEO DE VALIDACION DE .flexpkg  ·  implementación
//  ------------------------------------------------------------
//  Ver FlexOS_PkgCore.h. Este archivo es el MISMO código que corría
//  antes dentro de FlexOS_Package.cpp: las reglas no se han relajado
//  ni una. Lo que ha cambiado es de dónde salen los bytes (un lector
//  abstracto) y a dónde van (un sumidero abstracto), que es lo que
//  permite ejercitarlo entero en el PC con sanitizers.
//
//  AMPLIACIONES COMPATIBLES respecto a la versión anterior:
//    · manifest.runtime acepta "flex-app-v1" además de "flex-ui-1";
//    · límites nuevos y opcionales para el runtime nuevo;
//    · systemPermissions: lo que el manifest DECLARA pedir (declarar
//      no concede: quien concede es el grant firmado);
//    · trailer de grant, declarado en los bytes 56..59 de la cabecera
//      (antes reservados a cero, así que un paquete viejo sigue valiendo
//      y un firmware viejo rechaza uno nuevo -- falla cerrado).
// #############################################################
#include "FlexOS_PkgCore.h"
#include "FlexOS_AppGrant.h"

#include <cJSON.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__has_include)
#  if __has_include(<mbedtls/ecdsa.h>)
#    define FXP_BACKEND_MBEDTLS 1
#  elif __has_include(<openssl/evp.h>)
#    define FXP_BACKEND_OPENSSL 1
#  endif
#endif

#if FXP_BACKEND_MBEDTLS
#  include "mbedtls/bignum.h"
#  include "mbedtls/ecdsa.h"
#  include "mbedtls/ecp.h"
#  include "mbedtls/sha256.h"
#  include "mbedtls/version.h"
#  if MBEDTLS_VERSION_NUMBER >= 0x03000000
#    define FXP_SHA_START(c)      mbedtls_sha256_starts((c), 0)
#    define FXP_SHA_UPDATE(c,p,n) mbedtls_sha256_update((c),(p),(n))
#    define FXP_SHA_FINISH(c,o)   mbedtls_sha256_finish((c),(o))
#  else
#    define FXP_SHA_START(c)      mbedtls_sha256_starts_ret((c), 0)
#    define FXP_SHA_UPDATE(c,p,n) mbedtls_sha256_update_ret((c),(p),(n))
#    define FXP_SHA_FINISH(c,o)   mbedtls_sha256_finish_ret((c),(o))
#  endif
   typedef mbedtls_sha256_context FxpSha;
#else
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#  include <openssl/bn.h>
#  include <openssl/ec.h>
#  include <openssl/ecdsa.h>
#  include <openssl/evp.h>
#  include <openssl/obj_mac.h>
   typedef EVP_MD_CTX* FxpSha;
#endif

namespace {

const uint32_t HDR_SIZE = 64;
const uint32_t MAX_MANIFEST = 32u * 1024u;
const uint32_t MAX_INDEX = 128u * 1024u;

// ---- SHA-256 incremental, con el backend disponible --------------------
struct Sha {
#if FXP_BACKEND_MBEDTLS
  mbedtls_sha256_context c;
  bool ok = false;
  void start(){ mbedtls_sha256_init(&c); ok = (FXP_SHA_START(&c) == 0); }
  void update(const uint8_t* p, size_t n){ if(ok && FXP_SHA_UPDATE(&c, p, n) != 0) ok = false; }
  bool finish(uint8_t out[32]){ bool r = ok && FXP_SHA_FINISH(&c, out) == 0; mbedtls_sha256_free(&c); ok = false; return r; }
  void discard(){ if(ok){ mbedtls_sha256_free(&c); ok = false; } }
#else
  EVP_MD_CTX* c = nullptr;
  bool ok = false;
  void start(){ c = EVP_MD_CTX_new(); ok = c && EVP_DigestInit_ex(c, EVP_sha256(), nullptr) == 1; }
  void update(const uint8_t* p, size_t n){ if(ok && EVP_DigestUpdate(c, p, n) != 1) ok = false; }
  bool finish(uint8_t out[32]){
    unsigned int len = 0;
    bool r = ok && EVP_DigestFinal_ex(c, out, &len) == 1 && len == 32;
    if(c){ EVP_MD_CTX_free(c); c = nullptr; }
    ok = false;
    return r;
  }
  void discard(){ if(c){ EVP_MD_CTX_free(c); c = nullptr; } ok = false; }
#endif
};

#if FXP_BACKEND_MBEDTLS
bool verifyP256(const uint8_t pub[65], const uint8_t hash[32], const uint8_t sig[64]){
  if(pub[0] != 0x04) return false;
  mbedtls_ecp_group grp; mbedtls_ecp_group_init(&grp);
  mbedtls_ecp_point q; mbedtls_ecp_point_init(&q);
  mbedtls_mpi r, s; mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
  int rc = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
  if(rc == 0) rc = mbedtls_ecp_point_read_binary(&grp, &q, pub, 65);
  if(rc == 0) rc = mbedtls_ecp_check_pubkey(&grp, &q);
  if(rc == 0) rc = mbedtls_mpi_read_binary(&r, sig, 32);
  if(rc == 0) rc = mbedtls_mpi_read_binary(&s, sig + 32, 32);
  if(rc == 0) rc = mbedtls_ecdsa_verify(&grp, hash, 32, &q, &r, &s);
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
  r = nullptr; s = nullptr;
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

uint16_t rd16(const uint8_t* p){ return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
uint32_t rd32(const uint8_t* p){
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

struct Ctx {
  const FlexPkgReader* rd;
  char* err;
  size_t errCap;
  FlexPkgErrorCode code;
};

FlexPkgErrorCode fail(Ctx& c, FlexPkgErrorCode code, const char* text){
  c.code = code;
  if(c.err && c.errCap) snprintf(c.err, c.errCap, "%s", text ? text : "Error de paquete");
  return code;
}

bool jsonCanonical(const char* raw, uint32_t len, cJSON* root){
  char* printed = cJSON_PrintUnformatted(root);
  if(!printed) return false;
  size_t n = strlen(printed);
  bool ok = (n == (size_t)len) && memcmp(raw, printed, n) == 0;
  cJSON_free(printed);
  return ok;
}

bool jsonString(cJSON* obj, const char* key, char* out, size_t cap, bool required = true){
  cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
  if(!v){ if(!required){ out[0] = 0; return true; } return false; }
  if(!cJSON_IsString(v) || !v->valuestring || strlen(v->valuestring) >= cap) return false;
  memcpy(out, v->valuestring, strlen(v->valuestring) + 1);
  return true;
}

// Entero exacto dentro de un rango. cJSON guarda todo como double, así que
// "exacto" se comprueba de verdad y no por aproximación.
bool jsonUint(cJSON* obj, const char* key, uint32_t& out, uint32_t lo, uint32_t hi, bool required){
  cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
  if(!v){ if(!required){ out = 0; return true; } return false; }
  if(!cJSON_IsNumber(v)) return false;
  double d = v->valuedouble;
  if(d < (double)lo || d > (double)hi) return false;
  if(d != (double)(uint32_t)d) return false;
  out = (uint32_t)d;
  return true;
}

uint16_t permissionBit(const char* s){
  if(!strcmp(s, "network")) return FLEXPERM_NETWORK;
  if(!strcmp(s, "storage.read")) return FLEXPERM_STORAGE_READ;
  if(!strcmp(s, "storage.write")) return FLEXPERM_STORAGE_WRITE;
  if(!strcmp(s, "notifications")) return FLEXPERM_NOTIFICATIONS;
  if(!strcmp(s, "camera")) return FLEXPERM_CAMERA;
  if(!strcmp(s, "microphone")) return FLEXPERM_MICROPHONE;
  if(!strcmp(s, "location")) return FLEXPERM_LOCATION;
  if(!strcmp(s, "clipboard")) return FLEXPERM_CLIPBOARD;
  return 0;
}

} // namespace

// -------------------------------------------------------------------------
//  Reglas sueltas (idénticas a las que ya aplicaba el instalador)
// -------------------------------------------------------------------------
bool flexPkgCoreSafeId(const char* s){
  if(!s) return false;
  size_t n = strlen(s);
  if(n < 5 || n > FLEXPKG_ID_MAX || s[0] < 'a' || s[0] > 'z') return false;
  int dots = 0;
  bool segmentStart = true;
  for(size_t i = 0; i < n; i++){
    char c = s[i];
    if(c == '.'){
      if(segmentStart || i + 1 == n) return false;
      dots++;
      segmentStart = true;
      continue;
    }
    if(segmentStart && (c < 'a' || c > 'z')) return false;
    if(!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    segmentStart = false;
  }
  return dots >= 2 && dots <= 7;
}

bool flexPkgCoreSafePath(const char* s){
  if(!s) return false;
  size_t n = strlen(s);
  if(n == 0 || n > FLEXPKG_PATH_MAX || s[0] == '/' || s[0] == '.' || strchr(s, '\\')) return false;
  bool start = true;
  char part[4] = {0};
  size_t pi = 0;
  for(size_t i = 0; i <= n; i++){
    char c = s[i];
    if(c == '/' || c == 0){
      if(start || !strcmp(part, ".") || !strcmp(part, "..")) return false;
      start = true; pi = 0; memset(part, 0, sizeof(part));
      continue;
    }
    bool ok = isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-';
    if(!ok) return false;
    if(pi < sizeof(part) - 1) part[pi++] = c;
    start = false;
  }
  return true;
}

bool flexPkgCoreSemver(const char* s){
  if(!s || !isdigit((unsigned char)*s)) return false;
  int dots = 0;
  bool digit = false;
  for(const char* p = s; *p; ++p){
    if(isdigit((unsigned char)*p)){ digit = true; continue; }
    if(*p == '.' && dots < 2 && digit){ dots++; digit = false; continue; }
    if(*p == '-' && dots == 2 && digit){
      ++p;
      if(!*p) return false;
      for(; *p; ++p) if(!(islower((unsigned char)*p) || isdigit((unsigned char)*p) || *p == '.' || *p == '-')) return false;
      return true;
    }
    return false;
  }
  return dots == 2 && digit;
}

static void semverParts(const char* s, uint32_t out[3]){
  out[0] = out[1] = out[2] = 0;
  int p = 0;
  while(*s && p < 3){
    if(isdigit((unsigned char)*s)) out[p] = out[p] * 10u + (uint32_t)(*s - '0');
    else if(*s == '.') p++;
    else break;
    s++;
  }
}

int flexPkgCoreSemverCompare(const char* a, const char* b){
  uint32_t av[3], bv[3]; semverParts(a, av); semverParts(b, bv);
  for(int i = 0; i < 3; i++) if(av[i] != bv[i]) return av[i] < bv[i] ? -1 : 1;
  return 0;
}

bool flexPkgCoreHexToBytes(const char* hex, uint8_t* out, size_t n){
  if(!hex || strlen(hex) != n * 2) return false;
  for(size_t i = 0; i < n; i++){
    int hi = hex[i * 2], lo = hex[i * 2 + 1];
    hi = (hi >= '0' && hi <= '9') ? hi - '0' : (hi >= 'a' && hi <= 'f') ? hi - 'a' + 10 : -1;
    lo = (lo >= '0' && lo <= '9') ? lo - '0' : (lo >= 'a' && lo <= 'f') ? lo - 'a' + 10 : -1;
    if(hi < 0 || lo < 0) return false;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

void flexPkgCoreBytesToHex(const uint8_t* in, size_t n, char* out){
  static const char h[] = "0123456789abcdef";
  for(size_t i = 0; i < n; i++){ out[i * 2] = h[in[i] >> 4]; out[i * 2 + 1] = h[in[i] & 15]; }
  out[n * 2] = 0;
}

// -------------------------------------------------------------------------
//  Manifest
// -------------------------------------------------------------------------
static bool parseManifestJson(cJSON* root, FlexPkgInfo* out){
  if(!cJSON_IsObject(root) || !out) return false;
  memset(out, 0, sizeof(*out));
  cJSON* schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
  cJSON* ver = cJSON_GetObjectItemCaseSensitive(root, "version");
  cJSON* limits = cJSON_GetObjectItemCaseSensitive(root, "limits");
  cJSON* perms = cJSON_GetObjectItemCaseSensitive(root, "permissions");
  char runtime[FLEXPKG_RUNTIME_MAX + 1];
  if(!cJSON_IsNumber(schema) || schema->valuedouble != 1.0 || !cJSON_IsObject(ver) ||
     !cJSON_IsObject(limits) || !cJSON_IsArray(perms)) return false;
  if(!jsonString(root, "id", out->id, sizeof(out->id)) || !flexPkgCoreSafeId(out->id)) return false;
  if(!jsonString(root, "name", out->name, sizeof(out->name)) || strlen(out->name) < 2) return false;
  if(!jsonString(root, "minFlexOS", out->minFlexOS, sizeof(out->minFlexOS)) || !flexPkgCoreSemver(out->minFlexOS)) return false;
  if(!jsonString(root, "runtime", runtime, sizeof(runtime))) return false;
  if(!strcmp(runtime, "flex-ui-1")) out->runtime = FLEXPKG_RT_UI1;
  else if(!strcmp(runtime, "flex-app-v1")) out->runtime = FLEXPKG_RT_APP1;
  else return false;
  if(!jsonString(root, "entry", out->entry, sizeof(out->entry)) || !flexPkgCoreSafePath(out->entry)) return false;
  if(!jsonString(root, "developerKeySha256", out->developerKeySha256, sizeof(out->developerKeySha256)) ||
     strlen(out->developerKeySha256) != 64) return false;
  for(int i = 0; i < 64; i++){
    char c = out->developerKeySha256[i];
    if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  if(!jsonString(root, "summary", out->summary, sizeof(out->summary), false)) return false;
  if(!jsonString(root, "category", out->category, sizeof(out->category), false)) return false;
  if(!jsonString(ver, "name", out->versionName, sizeof(out->versionName)) || !flexPkgCoreSemver(out->versionName)) return false;
  uint32_t code = 0;
  if(!jsonUint(ver, "code", code, 1u, 0xFFFFFFFFu, true)) return false;
  out->versionCode = code;

  uint32_t mem = 0, sto = 0;
  if(!jsonUint(limits, "memoryKB", mem, 64u, 4096u, true)) return false;
  if(!jsonUint(limits, "storageKB", sto, 0u, 8192u, true)) return false;
  out->memoryKB = (uint16_t)mem;
  out->storageKB = (uint16_t)sto;
  // Límites del runtime nuevo. OPCIONALES: un manifest flex-ui-1 de los que
  // ya están instalados no los lleva y sigue siendo válido.
  if(!jsonUint(limits, "instrPerTick", out->instrPerTick, 1000u, 400000u, false)) return false;
  if(!jsonUint(limits, "usPerTick", out->usPerTick, 500u, 12000u, false)) return false;
  if(!jsonUint(limits, "drawPerFrame", out->drawPerFrame, 32u, 4096u, false)) return false;

  int pn = cJSON_GetArraySize(perms);
  if(pn < 0 || pn > 16) return false;
  uint16_t mask = 0;
  for(int i = 0; i < pn; i++){
    cJSON* p = cJSON_GetArrayItem(perms, i);
    if(!cJSON_IsString(p) || !p->valuestring) return false;
    uint16_t bit = permissionBit(p->valuestring);
    if(!bit || (mask & bit)) return false;
    mask |= bit;
  }
  out->permissions = mask;

  // systemPermissions: OPCIONAL y, sobre todo, DECLARATIVO. Estar aquí no
  // concede nada: sólo dice qué grant tendría sentido para esta app. El
  // firmware lo usa para rechazar un grant que conceda MÁS de lo declarado.
  cJSON* sysp = cJSON_GetObjectItemCaseSensitive(root, "systemPermissions");
  uint32_t smask = 0;
  if(sysp){
    if(!cJSON_IsArray(sysp)) return false;
    int sn = cJSON_GetArraySize(sysp);
    if(sn < 0 || sn > 16) return false;
    for(int i = 0; i < sn; i++){
      cJSON* p = cJSON_GetArrayItem(sysp, i);
      if(!cJSON_IsString(p) || !p->valuestring) return false;
      uint32_t bit = flexSysPermissionBit(p->valuestring);
      if(!bit || (smask & bit)) return false;
      smask |= bit;
    }
    // Un manifest flex-ui-1 no puede pedir permisos de sistema: ese runtime
    // no tiene forma de llamarlos, así que declararlos sólo puede ser un
    // error o un intento de confundir a quien revise el paquete.
    if(smask && out->runtime != FLEXPKG_RT_APP1) return false;
  }
  out->systemPermissions = smask;
  return true;
}

bool flexPkgCoreParseManifestBuffer(const char* raw, uint32_t len, FlexPkgInfo* out){
  if(!raw || len < 2 || len > MAX_MANIFEST || !out) return false;
  cJSON* root = cJSON_ParseWithLength(raw, len);
  if(!root) return false;
  bool ok = parseManifestJson(root, out);
  cJSON_Delete(root);
  return ok;
}

// -------------------------------------------------------------------------
//  Vistazo rapido: cabecera + manifest, sin hashes ni firma
// -------------------------------------------------------------------------
FlexPkgErrorCode flexPkgCorePeek(const FlexPkgReader* rd, FlexPkgInfo* out,
                                 char* errText, size_t errCap,
                                 const char* firmwareVersion){
  Ctx c{ rd, errText, errCap, FLEXPKG_OK };
  if(!rd || !rd->read || !out) return fail(c, FLEXPKG_ERR_OPEN, "Lector de paquete invalido");
  memset(out, 0, sizeof(*out));
  uint32_t total = rd->size;
  if(total < HDR_SIZE + 65 + 64 || total > FLEXPKG_MAX_PACKAGE_BYTES)
    return fail(c, FLEXPKG_ERR_SIZE, "Tamano de paquete no permitido");
  uint8_t header[HDR_SIZE];
  if(!rd->read(rd->user, 0, header, HDR_SIZE)) return fail(c, FLEXPKG_ERR_HEADER, "Encabezado incompleto");
  if(memcmp(header, "FLXP", 4) || rd16(header + 4) != FLEXPKG_FORMAT_VERSION || rd16(header + 6) != 0)
    return fail(c, FLEXPKG_ERR_HEADER, "No es un Flex Package v1");
  uint32_t manifestLen = rd32(header + 8);
  if(manifestLen < 2 || manifestLen > MAX_MANIFEST)
    return fail(c, FLEXPKG_ERR_HEADER, "Longitudes internas invalidas");
  char* raw = (char*)malloc(manifestLen + 1);
  if(!raw) return fail(c, FLEXPKG_ERR_MEMORY, "Sin memoria para leer el manifest");
  bool ok = rd->read(rd->user, HDR_SIZE, raw, manifestLen);
  raw[manifestLen] = 0;
  cJSON* root = ok ? cJSON_ParseWithLength(raw, manifestLen) : nullptr;
  ok = root && jsonCanonical(raw, manifestLen, root) && parseManifestJson(root, out);
  if(root) cJSON_Delete(root);
  free(raw);
  if(!ok) return fail(c, FLEXPKG_ERR_MANIFEST, "Manifest de la app invalido");
  if(firmwareVersion && flexPkgCoreSemverCompare(firmwareVersion, out->minFlexOS) < 0)
    return fail(c, FLEXPKG_ERR_VERSION, "La app requiere una version mas reciente de FlexOS");
  return FLEXPKG_OK;
}

// -------------------------------------------------------------------------
//  Validación completa
// -------------------------------------------------------------------------
FlexPkgErrorCode flexPkgCoreRun(const FlexPkgReader* rd, const FlexPkgSink* sink,
                                FlexPkgCoreOut* out,
                                FlexPkgProgressFn progress, void* progressUser,
                                char* errText, size_t errCap,
                                const char* firmwareVersion){
  Ctx c{ rd, errText, errCap, FLEXPKG_OK };
  if(!rd || !rd->read || !out) return fail(c, FLEXPKG_ERR_OPEN, "Lector de paquete invalido");
  memset(out, 0, sizeof(*out));

  auto step = [&](uint8_t pct, const char* stage) -> bool {
    if(!progress) return true;
    if(!progress(pct, stage, progressUser)){ fail(c, FLEXPKG_ERR_CANCELLED, "Instalacion cancelada"); return false; }
    return true;
  };
  if(!step(1, "Abriendo paquete")) return c.code;

  // ---- Cabecera ---------------------------------------------------------
  uint32_t total = rd->size;
  if(total < HDR_SIZE + 65 + 64 || total > FLEXPKG_MAX_PACKAGE_BYTES)
    return fail(c, FLEXPKG_ERR_SIZE, "Tamano de paquete no permitido");
  uint8_t header[HDR_SIZE];
  if(!rd->read(rd->user, 0, header, HDR_SIZE)) return fail(c, FLEXPKG_ERR_HEADER, "Encabezado incompleto");
  if(memcmp(header, "FLXP", 4) || rd16(header + 4) != FLEXPKG_FORMAT_VERSION || rd16(header + 6) != 0)
    return fail(c, FLEXPKG_ERR_HEADER, "No es un Flex Package v1");
  // 56..59 = longitud del grant (antes reservado a cero); 60..63 siguen a cero.
  uint32_t grantLen = rd32(header + 56);
  for(int i = 60; i < 64; i++) if(header[i] != 0)
    return fail(c, FLEXPKG_ERR_HEADER, "Bytes reservados invalidos");
  if(grantLen != 0 && grantLen != FLEXGRANT_BYTES)
    return fail(c, FLEXPKG_ERR_HEADER, "Bloque de permisos con tamano invalido");

  uint32_t manifestLen = rd32(header + 8);
  uint32_t indexLen = rd32(header + 12);
  uint32_t payloadLen = rd32(header + 16);
  if(manifestLen < 2 || manifestLen > MAX_MANIFEST || indexLen < 2 || indexLen > MAX_INDEX ||
     payloadLen > FLEXPKG_MAX_PACKAGE_BYTES || rd16(header + 20) != 65 || rd16(header + 22) != 64)
    return fail(c, FLEXPKG_ERR_HEADER, "Longitudes internas invalidas");

  uint64_t declared = (uint64_t)HDR_SIZE + manifestLen + indexLen + payloadLen + 65u + 64u + grantLen;
  if(declared != (uint64_t)total) return fail(c, FLEXPKG_ERR_SIZE, "El tamano declarado no coincide");

  uint32_t payloadStart = HDR_SIZE + manifestLen + indexLen;
  uint32_t payloadEnd = payloadStart + payloadLen;

  // ---- Manifest e índice -------------------------------------------------
  char* manifestRaw = (char*)malloc(manifestLen + 1);
  char* indexRaw = (char*)malloc(indexLen + 1);
  cJSON* manifest = nullptr;
  cJSON* index = nullptr;
  FlexPkgErrorCode rc = FLEXPKG_OK;
  uint8_t publicKey[65], signature[64];

  auto cleanup = [&](){
    if(manifest) cJSON_Delete(manifest);
    if(index) cJSON_Delete(index);
    free(manifestRaw); free(indexRaw);
  };

  if(!manifestRaw || !indexRaw){ cleanup(); return fail(c, FLEXPKG_ERR_MEMORY, "Sin memoria para validar el paquete"); }
  if(!rd->read(rd->user, HDR_SIZE, manifestRaw, manifestLen) ||
     !rd->read(rd->user, HDR_SIZE + manifestLen, indexRaw, indexLen)){
    cleanup(); return fail(c, FLEXPKG_ERR_JSON, "Manifiesto o indice truncado");
  }
  manifestRaw[manifestLen] = 0; indexRaw[indexLen] = 0;
  manifest = cJSON_ParseWithLength(manifestRaw, manifestLen);
  index = cJSON_ParseWithLength(indexRaw, indexLen);
  if(!manifest || !index || !jsonCanonical(manifestRaw, manifestLen, manifest) ||
     !jsonCanonical(indexRaw, indexLen, index)){
    cleanup(); return fail(c, FLEXPKG_ERR_JSON, "JSON interno invalido o no canonico");
  }
  if(!parseManifestJson(manifest, &out->info)){
    cleanup(); return fail(c, FLEXPKG_ERR_MANIFEST, "Manifest de la app invalido");
  }
  if(firmwareVersion && flexPkgCoreSemverCompare(firmwareVersion, out->info.minFlexOS) < 0){
    cleanup(); return fail(c, FLEXPKG_ERR_VERSION, "La app requiere una version mas reciente de FlexOS");
  }
  int files = cJSON_IsArray(index) ? cJSON_GetArraySize(index) : -1;
  if(files < 1 || files > FLEXPKG_MAX_FILES){
    cleanup(); return fail(c, FLEXPKG_ERR_INDEX, "Cantidad de archivos invalida");
  }
  out->info.fileCount = (uint16_t)files;
  out->info.payloadBytes = payloadLen;
  out->manifestLen = manifestLen; out->indexLen = indexLen; out->payloadLen = payloadLen;

  // ---- Clave y firma -----------------------------------------------------
  if(!rd->read(rd->user, payloadEnd, publicKey, 65) ||
     !rd->read(rd->user, payloadEnd + 65, signature, 64)){
    cleanup(); return fail(c, FLEXPKG_ERR_SIGNATURE, "Firma o clave truncada");
  }
  {
    uint8_t fp[32]; char fpHex[65];
    if(!flexGrantSha256(publicKey, 65, fp)){
      cleanup(); return fail(c, FLEXPKG_ERR_HASH, "No se pudo calcular SHA-256");
    }
    flexPkgCoreBytesToHex(fp, 32, fpHex);
    if(strcmp(fpHex, out->info.developerKeySha256)){
      cleanup(); return fail(c, FLEXPKG_ERR_DEVELOPER, "La clave no coincide con el desarrollador");
    }
  }

  // ---- Recorrido del payload --------------------------------------------
  Sha totalHash; totalHash.start();
  totalHash.update((const uint8_t*)manifestRaw, manifestLen);
  totalHash.update((const uint8_t*)indexRaw, indexLen);

  uint32_t expectedOffset = 0;
  bool entryFound = false;
  uint8_t buffer[2048];
  bool writing = (sink && sink->begin);

  for(int i = 0; i < files && rc == FLEXPKG_OK; i++){
    cJSON* e = cJSON_GetArrayItem(index, i);
    cJSON* pathV = e ? cJSON_GetObjectItemCaseSensitive(e, "path") : nullptr;
    cJSON* off = e ? cJSON_GetObjectItemCaseSensitive(e, "offset") : nullptr;
    cJSON* size = e ? cJSON_GetObjectItemCaseSensitive(e, "size") : nullptr;
    cJSON* hash = e ? cJSON_GetObjectItemCaseSensitive(e, "sha256") : nullptr;
    if(!cJSON_IsObject(e) || !cJSON_IsString(pathV) || !pathV->valuestring ||
       !flexPkgCoreSafePath(pathV->valuestring) ||
       !cJSON_IsNumber(off) || !cJSON_IsNumber(size) || !cJSON_IsString(hash) || !hash->valuestring ||
       off->valuedouble < 0 || off->valuedouble > 4294967295.0 ||
       size->valuedouble < 0 || size->valuedouble > 4294967295.0 ||
       off->valuedouble != (double)(uint32_t)off->valuedouble ||
       size->valuedouble != (double)(uint32_t)size->valuedouble){
      rc = fail(c, FLEXPKG_ERR_INDEX, "Entrada del indice invalida"); break;
    }
    uint32_t offset = (uint32_t)off->valuedouble, bytes = (uint32_t)size->valuedouble;
    if(offset != expectedOffset || bytes > FLEXPKG_MAX_FILE_BYTES ||
       (uint64_t)offset + bytes > (uint64_t)payloadLen){
      rc = fail(c, FLEXPKG_ERR_INDEX, "Offsets del payload invalidos"); break;
    }
    for(int j = 0; j < i; j++){
      cJSON* prev = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(index, j), "path");
      if(prev && cJSON_IsString(prev) && !strcmp(prev->valuestring, pathV->valuestring)){
        rc = fail(c, FLEXPKG_ERR_PATH, "Ruta duplicada en el paquete"); break;
      }
    }
    if(rc != FLEXPKG_OK) break;
    uint8_t expectedFileHash[32];
    if(!flexPkgCoreHexToBytes(hash->valuestring, expectedFileHash, 32)){
      rc = fail(c, FLEXPKG_ERR_INDEX, "SHA-256 de archivo invalido"); break;
    }
    if(writing && !sink->begin(sink->user, pathV->valuestring)){
      rc = fail(c, FLEXPKG_ERR_STORAGE, "No se pudo escribir un archivo de la app"); break;
    }
    Sha fileHash; fileHash.start();
    uint32_t remain = bytes, done = 0;
    while(remain){
      uint32_t want = remain > sizeof(buffer) ? (uint32_t)sizeof(buffer) : remain;
      if(!rd->read(rd->user, payloadStart + offset + done, buffer, want)){
        rc = fail(c, FLEXPKG_ERR_SIZE, "Payload truncado"); break;
      }
      totalHash.update(buffer, want);
      fileHash.update(buffer, want);
      if(writing && !sink->write(sink->user, buffer, want)){
        rc = fail(c, FLEXPKG_ERR_STORAGE, "Fallo al escribir el payload"); break;
      }
      remain -= want; done += want;
      uint8_t pct = (uint8_t)(10u + ((uint64_t)(offset + done) * 75u / (payloadLen ? payloadLen : 1u)));
      if(!step(pct, writing ? "Verificando e instalando" : "Verificando")){ rc = c.code; break; }
    }
    uint8_t actual[32];
    bool digestOk = fileHash.finish(actual);
    if(writing) sink->end(sink->user, rc == FLEXPKG_OK);
    if(rc != FLEXPKG_OK) break;
    if(!digestOk){ rc = fail(c, FLEXPKG_ERR_HASH, "No se pudo calcular SHA-256"); break; }
    if(memcmp(actual, expectedFileHash, 32)){
      rc = fail(c, FLEXPKG_ERR_HASH, "SHA-256 de un archivo no coincide"); break;
    }
    if(!strcmp(pathV->valuestring, out->info.entry)) entryFound = true;
    expectedOffset = offset + bytes;
  }

  if(rc != FLEXPKG_OK){ totalHash.discard(); cleanup(); return rc; }
  if(expectedOffset != payloadLen || !entryFound){
    totalHash.discard(); cleanup();
    return fail(c, FLEXPKG_ERR_INDEX, "Payload no declarado o entry ausente");
  }

  uint8_t signedHash[32];
  if(!totalHash.finish(signedHash)){ cleanup(); return fail(c, FLEXPKG_ERR_HASH, "No se pudo calcular SHA-256"); }
  if(memcmp(signedHash, header + 24, 32)){ cleanup(); return fail(c, FLEXPKG_ERR_HASH, "El paquete fue alterado"); }
  if(!verifyP256(publicKey, signedHash, signature)){
    cleanup(); return fail(c, FLEXPKG_ERR_SIGNATURE, "Firma ECDSA P-256 invalida");
  }

  memcpy(out->signedHash, signedHash, 32);
  flexPkgCoreBytesToHex(signedHash, 32, out->info.packageSha256);

  // ---- Trailer de permisos ----------------------------------------------
  if(grantLen){
    if(!rd->read(rd->user, payloadEnd + 65 + 64, out->grant, grantLen)){
      cleanup(); return fail(c, FLEXPKG_ERR_SIGNATURE, "Bloque de permisos truncado");
    }
    out->grantLen = grantLen;
    out->info.grantLen = grantLen;
  }

  cleanup();
  if(!step(90, "Firma valida")) return c.code;
  return FLEXPKG_OK;
}
