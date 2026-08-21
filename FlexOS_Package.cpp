#include "FlexOS_Package.h"

#include <FS.h>
#include <LittleFS.h>
#include <cJSON.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "FlexOS_OTA.h"
#include "mbedtls/bignum.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/sha256.h"
#include "mbedtls/version.h"

namespace {

static const uint32_t HDR_SIZE = 64;
static const uint32_t MAX_MANIFEST = 32u * 1024u;
static const uint32_t MAX_INDEX = 128u * 1024u;
static const char* ROOT_DIR = "/FlexApps";
static const char* MANIFEST_FILE = "/.manifest.json";
static const char* INDEX_FILE = "/.index.json";
static const char* STAGE_NAME = "/.stage";
static const char* ACTIVE_NAME = "/active";
static const char* OLD_NAME = "/.old";

static volatile bool gBusy = false;
static volatile bool gCancel = false;
static FlexPkgErrorCode gErr = FLEXPKG_OK;
static char gErrText[128] = "Correcto";

static void setError(FlexPkgErrorCode code, const char* text){
  gErr = code;
  snprintf(gErrText, sizeof(gErrText), "%s", text ? text : "Error de paquete");
}

static uint16_t rd16(const uint8_t* p){
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t* p){
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool readExact(File& f, void* dst, size_t n){
  uint8_t* p = static_cast<uint8_t*>(dst);
  size_t done = 0;
  while(done < n){
    size_t got = f.read(p + done, n - done);
    if(got == 0) return false;
    done += got;
  }
  return true;
}

static bool progress(FlexPkgProgressFn cb, void* user, uint8_t pct, const char* stage){
  if(gCancel){ setError(FLEXPKG_ERR_CANCELLED, "Instalacion cancelada"); return false; }
  if(cb && !cb(pct, stage, user)){
    gCancel = true;
    setError(FLEXPKG_ERR_CANCELLED, "Instalacion cancelada");
    return false;
  }
  return true;
}

static bool mkdirOne(const char* path){
  return LittleFS.exists(path) || LittleFS.mkdir(path);
}

static bool mkdirs(const char* path){
  if(!path || path[0] != '/') return false;
  char tmp[FLEXPKG_PATH_MAX + FLEXPKG_ID_MAX + 40];
  size_t n = strlen(path);
  if(n >= sizeof(tmp)) return false;
  memcpy(tmp, path, n + 1);
  for(size_t i = 1; i < n; i++){
    if(tmp[i] == '/'){
      tmp[i] = 0;
      if(!mkdirOne(tmp)) return false;
      tmp[i] = '/';
    }
  }
  return mkdirOne(tmp);
}

static const char* baseName(const char* path){
  const char* p = path ? strrchr(path, '/') : nullptr;
  return p ? p + 1 : (path ? path : "");
}

static bool removeTree(const char* path, uint8_t depth = 0){
  if(!path || !path[0] || !strcmp(path, "/") || depth > 12) return false;
  File f = LittleFS.open(path, "r");
  if(!f) return !LittleFS.exists(path);
  if(!f.isDirectory()){
    f.close();
    return LittleFS.remove(path);
  }
  File e = f.openNextFile();
  while(e){
    char child[FLEXPKG_PATH_MAX + FLEXPKG_ID_MAX + 48];
    snprintf(child, sizeof(child), "%s/%s", path, baseName(e.name()));
    bool dir = e.isDirectory();
    e.close();
    if(dir) removeTree(child, depth + 1);
    else LittleFS.remove(child);
    e = f.openNextFile();
  }
  f.close();
  return LittleFS.rmdir(path);
}

static uint32_t dirBytes(const char* path, uint8_t depth = 0){
  if(depth > 12) return 0;
  File f = LittleFS.open(path, "r");
  if(!f) return 0;
  if(!f.isDirectory()){
    uint32_t n = (uint32_t)f.size();
    f.close();
    return n;
  }
  uint32_t total = 0;
  File e = f.openNextFile();
  while(e){
    char child[FLEXPKG_PATH_MAX + FLEXPKG_ID_MAX + 48];
    snprintf(child, sizeof(child), "%s/%s", path, baseName(e.name()));
    bool dir = e.isDirectory();
    uint32_t n = dir ? 0 : (uint32_t)e.size();
    e.close();
    total += dir ? dirBytes(child, depth + 1) : n;
    e = f.openNextFile();
  }
  f.close();
  return total;
}

static bool safeId(const char* s){
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

static bool safePath(const char* s){
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

static bool semver(const char* s){
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

static int semverCompare(const char* a, const char* b){
  uint32_t av[3], bv[3]; semverParts(a, av); semverParts(b, bv);
  for(int i = 0; i < 3; i++) if(av[i] != bv[i]) return av[i] < bv[i] ? -1 : 1;
  return 0;
}

static bool hexToBytes(const char* hex, uint8_t* out, size_t n){
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

static void bytesToHex(const uint8_t* in, size_t n, char* out){
  static const char h[] = "0123456789abcdef";
  for(size_t i = 0; i < n; i++){ out[i * 2] = h[in[i] >> 4]; out[i * 2 + 1] = h[in[i] & 15]; }
  out[n * 2] = 0;
}

#if MBEDTLS_VERSION_NUMBER >= 0x03000000
  #define SHA_START(ctx)  mbedtls_sha256_starts((ctx), 0)
  #define SHA_UPDATE(ctx,p,n) mbedtls_sha256_update((ctx),(p),(n))
  #define SHA_FINISH(ctx,o) mbedtls_sha256_finish((ctx),(o))
#else
  #define SHA_START(ctx)  mbedtls_sha256_starts_ret((ctx), 0)
  #define SHA_UPDATE(ctx,p,n) mbedtls_sha256_update_ret((ctx),(p),(n))
  #define SHA_FINISH(ctx,o) mbedtls_sha256_finish_ret((ctx),(o))
#endif

static bool shaBuffer(const uint8_t* data, size_t n, uint8_t out[32]){
  mbedtls_sha256_context c; mbedtls_sha256_init(&c);
  bool ok = SHA_START(&c) == 0 && SHA_UPDATE(&c, data, n) == 0 && SHA_FINISH(&c, out) == 0;
  mbedtls_sha256_free(&c);
  return ok;
}

static bool verifyP256(const uint8_t pub[65], const uint8_t hash[32], const uint8_t sig[64]){
  if(pub[0] != 0x04) return false;
  mbedtls_ecp_group grp; mbedtls_ecp_group_init(&grp);
  mbedtls_ecp_point q; mbedtls_ecp_point_init(&q);
  mbedtls_mpi r, s; mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
  int rc = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
  if(rc == 0) rc = mbedtls_mpi_read_binary(&q.X, pub + 1, 32);
  if(rc == 0) rc = mbedtls_mpi_read_binary(&q.Y, pub + 33, 32);
  if(rc == 0) rc = mbedtls_mpi_lset(&q.Z, 1);
  if(rc == 0) rc = mbedtls_ecp_check_pubkey(&grp, &q);
  if(rc == 0) rc = mbedtls_mpi_read_binary(&r, sig, 32);
  if(rc == 0) rc = mbedtls_mpi_read_binary(&s, sig + 32, 32);
  if(rc == 0) rc = mbedtls_ecdsa_verify(&grp, hash, 32, &q, &r, &s);
  mbedtls_mpi_free(&s); mbedtls_mpi_free(&r);
  mbedtls_ecp_point_free(&q); mbedtls_ecp_group_free(&grp);
  return rc == 0;
}

static bool jsonCanonical(const char* raw, cJSON* root){
  char* printed = cJSON_PrintUnformatted(root);
  if(!printed) return false;
  bool ok = strlen(raw) == strlen(printed) && memcmp(raw, printed, strlen(raw)) == 0;
  cJSON_free(printed);
  return ok;
}

static bool jsonString(cJSON* obj, const char* key, char* out, size_t cap, bool required = true){
  cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
  if(!v){ if(!required){ out[0] = 0; return true; } return false; }
  if(!cJSON_IsString(v) || !v->valuestring || strlen(v->valuestring) >= cap) return false;
  memcpy(out, v->valuestring, strlen(v->valuestring) + 1);
  return true;
}

static uint16_t permissionBit(const char* s){
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

static bool parseManifest(cJSON* root, FlexPkgInfo* out){
  if(!cJSON_IsObject(root) || !out) return false;
  memset(out, 0, sizeof(*out));
  cJSON* schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
  cJSON* ver = cJSON_GetObjectItemCaseSensitive(root, "version");
  cJSON* limits = cJSON_GetObjectItemCaseSensitive(root, "limits");
  cJSON* perms = cJSON_GetObjectItemCaseSensitive(root, "permissions");
  char runtime[24];
  if(!cJSON_IsNumber(schema) || schema->valuedouble != 1.0 || !cJSON_IsObject(ver) ||
     !cJSON_IsObject(limits) || !cJSON_IsArray(perms)) return false;
  if(!jsonString(root, "id", out->id, sizeof(out->id)) || !safeId(out->id)) return false;
  if(!jsonString(root, "name", out->name, sizeof(out->name)) || strlen(out->name) < 2) return false;
  if(!jsonString(root, "minFlexOS", out->minFlexOS, sizeof(out->minFlexOS)) || !semver(out->minFlexOS)) return false;
  if(!jsonString(root, "runtime", runtime, sizeof(runtime)) || strcmp(runtime, "flex-ui-1")) return false;
  if(!jsonString(root, "entry", out->entry, sizeof(out->entry)) || !safePath(out->entry)) return false;
  if(!jsonString(root, "developerKeySha256", out->developerKeySha256, sizeof(out->developerKeySha256)) || strlen(out->developerKeySha256) != 64) return false;
  if(!jsonString(root, "summary", out->summary, sizeof(out->summary), false)) return false;
  if(!jsonString(root, "category", out->category, sizeof(out->category), false)) return false;
  if(!jsonString(ver, "name", out->versionName, sizeof(out->versionName)) || !semver(out->versionName)) return false;
  cJSON* code = cJSON_GetObjectItemCaseSensitive(ver, "code");
  if(!cJSON_IsNumber(code) || code->valuedouble < 1 || code->valuedouble > 4294967295.0 || code->valuedouble != (double)(uint32_t)code->valuedouble) return false;
  out->versionCode = (uint32_t)code->valuedouble;
  cJSON* mem = cJSON_GetObjectItemCaseSensitive(limits, "memoryKB");
  cJSON* sto = cJSON_GetObjectItemCaseSensitive(limits, "storageKB");
  if(!cJSON_IsNumber(mem) || !cJSON_IsNumber(sto) || mem->valuedouble < 64 || mem->valuedouble > 4096 ||
     sto->valuedouble < 0 || sto->valuedouble > 8192) return false;
  out->memoryKB = (uint16_t)mem->valuedouble;
  out->storageKB = (uint16_t)sto->valuedouble;
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
  return true;
}

static bool writeHidden(const char* root, const char* suffix, const char* data, size_t n){
  char path[FLEXPKG_PATH_MAX + FLEXPKG_ID_MAX + 48];
  snprintf(path, sizeof(path), "%s%s", root, suffix);
  File f = LittleFS.open(path, "w");
  if(!f) return false;
  bool ok = f.write((const uint8_t*)data, n) == n;
  f.close();
  if(!ok) LittleFS.remove(path);
  return ok;
}

static bool readManifestAt(const char* activeRoot, FlexPkgInfo* out){
  char path[FLEXPKG_PATH_MAX + FLEXPKG_ID_MAX + 48];
  snprintf(path, sizeof(path), "%s%s", activeRoot, MANIFEST_FILE);
  File f = LittleFS.open(path, "r");
  if(!f || f.size() < 2 || f.size() > MAX_MANIFEST){ if(f) f.close(); return false; }
  size_t n = f.size();
  char* raw = (char*)malloc(n + 1);
  if(!raw){ f.close(); return false; }
  bool ok = readExact(f, raw, n); f.close(); raw[n] = 0;
  cJSON* root = ok ? cJSON_ParseWithLength(raw, n) : nullptr;
  ok = root && parseManifest(root, out);
  if(root) cJSON_Delete(root);
  free(raw);
  if(ok) out->installedBytes = dirBytes(activeRoot);
  return ok;
}

static bool smokeEntrypoint(const char* stage, const FlexPkgInfo& info){
  char path[FLEXPKG_PATH_MAX + FLEXPKG_ID_MAX + 48];
  snprintf(path, sizeof(path), "%s/%s", stage, info.entry);
  File f = LittleFS.open(path, "r");
  if(!f || f.size() < 2 || f.size() > (size_t)info.memoryKB * 1024u){ if(f) f.close(); return false; }
  size_t n = f.size();
  char* raw = (char*)malloc(n + 1);
  if(!raw){ f.close(); return false; }
  bool ok = readExact(f, raw, n); f.close(); raw[n] = 0;
  cJSON* root = ok ? cJSON_ParseWithLength(raw, n) : nullptr;
  if(!root || !cJSON_IsObject(root)) ok = false;
  if(ok){
    cJSON* schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
    cJSON* start = cJSON_GetObjectItemCaseSensitive(root, "startScreen");
    cJSON* screens = cJSON_GetObjectItemCaseSensitive(root, "screens");
    ok = cJSON_IsNumber(schema) && schema->valuedouble == 1.0 && cJSON_IsString(start) && cJSON_IsArray(screens) && cJSON_GetArraySize(screens) > 0;
  }
  if(root) cJSON_Delete(root);
  free(raw);
  return ok;
}

struct ParsedPackage {
  File file;
  uint8_t header[HDR_SIZE] = {0};
  uint32_t manifestLen = 0, indexLen = 0, payloadLen = 0;
  uint32_t payloadStart = 0, payloadEnd = 0;
  char* manifestRaw = nullptr;
  char* indexRaw = nullptr;
  cJSON* manifest = nullptr;
  cJSON* index = nullptr;
  uint8_t publicKey[65] = {0};
  uint8_t signature[64] = {0};
  FlexPkgInfo info = {};
};

static void parsedClose(ParsedPackage& p){
  if(p.file) p.file.close();
  if(p.manifest) cJSON_Delete(p.manifest);
  if(p.index) cJSON_Delete(p.index);
  free(p.manifestRaw); free(p.indexRaw);
  p.manifest = p.index = nullptr;
  p.manifestRaw = p.indexRaw = nullptr;
}

static bool parseOpen(const char* packagePath, ParsedPackage& p){
  p.file = LittleFS.open(packagePath, "r");
  if(!p.file){ setError(FLEXPKG_ERR_OPEN, "No se pudo abrir el .flexpkg"); return false; }
  uint32_t total = (uint32_t)p.file.size();
  if(total < HDR_SIZE + 65 + 64 || total > FLEXPKG_MAX_PACKAGE_BYTES){ setError(FLEXPKG_ERR_SIZE, "Tamano de paquete no permitido"); return false; }
  if(!readExact(p.file, p.header, HDR_SIZE)){ setError(FLEXPKG_ERR_HEADER, "Encabezado incompleto"); return false; }
  if(memcmp(p.header, "FLXP", 4) || rd16(p.header + 4) != FLEXPKG_FORMAT_VERSION || rd16(p.header + 6) != 0){
    setError(FLEXPKG_ERR_HEADER, "No es un Flex Package v1"); return false;
  }
  for(int i = 56; i < 64; i++) if(p.header[i] != 0){ setError(FLEXPKG_ERR_HEADER, "Bytes reservados invalidos"); return false; }
  p.manifestLen = rd32(p.header + 8); p.indexLen = rd32(p.header + 12); p.payloadLen = rd32(p.header + 16);
  if(p.manifestLen < 2 || p.manifestLen > MAX_MANIFEST || p.indexLen < 2 || p.indexLen > MAX_INDEX ||
     p.payloadLen > FLEXPKG_MAX_PACKAGE_BYTES || rd16(p.header + 20) != 65 || rd16(p.header + 22) != 64){
    setError(FLEXPKG_ERR_HEADER, "Longitudes internas invalidas"); return false;
  }
  uint64_t declared = (uint64_t)HDR_SIZE + p.manifestLen + p.indexLen + p.payloadLen + 65u + 64u;
  if(declared != total){ setError(FLEXPKG_ERR_SIZE, "El tamano declarado no coincide"); return false; }
  p.payloadStart = HDR_SIZE + p.manifestLen + p.indexLen;
  p.payloadEnd = p.payloadStart + p.payloadLen;
  p.manifestRaw = (char*)malloc(p.manifestLen + 1);
  p.indexRaw = (char*)malloc(p.indexLen + 1);
  if(!p.manifestRaw || !p.indexRaw){ setError(FLEXPKG_ERR_MEMORY, "Sin memoria para validar el paquete"); return false; }
  if(!readExact(p.file, p.manifestRaw, p.manifestLen) || !readExact(p.file, p.indexRaw, p.indexLen)){
    setError(FLEXPKG_ERR_JSON, "Manifiesto o indice truncado"); return false;
  }
  p.manifestRaw[p.manifestLen] = 0; p.indexRaw[p.indexLen] = 0;
  p.manifest = cJSON_ParseWithLength(p.manifestRaw, p.manifestLen);
  p.index = cJSON_ParseWithLength(p.indexRaw, p.indexLen);
  if(!p.manifest || !p.index || !jsonCanonical(p.manifestRaw, p.manifest) || !jsonCanonical(p.indexRaw, p.index)){
    setError(FLEXPKG_ERR_JSON, "JSON interno invalido o no canonico"); return false;
  }
  if(!parseManifest(p.manifest, &p.info)){
    setError(FLEXPKG_ERR_MANIFEST, "Manifest de la app invalido"); return false;
  }
  if(semverCompare(FLEXOS_FW_VERSION, p.info.minFlexOS) < 0){
    setError(FLEXPKG_ERR_VERSION, "La app requiere una version mas reciente de FlexOS"); return false;
  }
  int files = cJSON_IsArray(p.index) ? cJSON_GetArraySize(p.index) : -1;
  if(files < 1 || files > FLEXPKG_MAX_FILES){ setError(FLEXPKG_ERR_INDEX, "Cantidad de archivos invalida"); return false; }
  p.info.fileCount = (uint16_t)files; p.info.payloadBytes = p.payloadLen;
  if(!p.file.seek(p.payloadEnd, SeekSet) || !readExact(p.file, p.publicKey, 65) || !readExact(p.file, p.signature, 64)){
    setError(FLEXPKG_ERR_SIGNATURE, "Firma o clave truncada"); return false;
  }
  uint8_t fp[32]; char fpHex[65];
  if(!shaBuffer(p.publicKey, 65, fp)){ setError(FLEXPKG_ERR_HASH, "No se pudo calcular SHA-256"); return false; }
  bytesToHex(fp, 32, fpHex);
  if(strcmp(fpHex, p.info.developerKeySha256)){
    setError(FLEXPKG_ERR_DEVELOPER, "La clave no coincide con el desarrollador"); return false;
  }
  return true;
}

static bool ensureParent(const char* path){
  char dir[FLEXPKG_PATH_MAX + FLEXPKG_ID_MAX + 48];
  size_t n = strlen(path);
  if(n >= sizeof(dir)) return false;
  memcpy(dir, path, n + 1);
  char* slash = strrchr(dir, '/');
  if(!slash || slash == dir) return true;
  *slash = 0;
  return mkdirs(dir);
}

static bool validateAndExtract(ParsedPackage& p, const char* stage, bool writeFiles,
                               FlexPkgProgressFn cb, void* user){
  if(writeFiles){ removeTree(stage); if(!mkdirs(stage)){ setError(FLEXPKG_ERR_STORAGE, "No se pudo crear el slot temporal"); return false; } }
  mbedtls_sha256_context totalHash; mbedtls_sha256_init(&totalHash);
  if(SHA_START(&totalHash) != 0 || SHA_UPDATE(&totalHash, (const uint8_t*)p.manifestRaw, p.manifestLen) != 0 ||
     SHA_UPDATE(&totalHash, (const uint8_t*)p.indexRaw, p.indexLen) != 0){
    mbedtls_sha256_free(&totalHash); setError(FLEXPKG_ERR_HASH, "No se pudo iniciar SHA-256"); return false;
  }
  if(!p.file.seek(p.payloadStart, SeekSet)){ mbedtls_sha256_free(&totalHash); setError(FLEXPKG_ERR_OPEN, "No se pudo leer el payload"); return false; }
  uint32_t expectedOffset = 0;
  bool entryFound = false;
  uint8_t buffer[4096];
  for(int i = 0; i < p.info.fileCount; i++){
    cJSON* e = cJSON_GetArrayItem(p.index, i);
    cJSON* path = e ? cJSON_GetObjectItemCaseSensitive(e, "path") : nullptr;
    cJSON* off = e ? cJSON_GetObjectItemCaseSensitive(e, "offset") : nullptr;
    cJSON* size = e ? cJSON_GetObjectItemCaseSensitive(e, "size") : nullptr;
    cJSON* hash = e ? cJSON_GetObjectItemCaseSensitive(e, "sha256") : nullptr;
    if(!cJSON_IsObject(e) || !cJSON_IsString(path) || !path->valuestring || !safePath(path->valuestring) ||
       !cJSON_IsNumber(off) || !cJSON_IsNumber(size) || !cJSON_IsString(hash) || !hash->valuestring ||
       off->valuedouble < 0 || off->valuedouble > 4294967295.0 ||
       size->valuedouble < 0 || size->valuedouble > 4294967295.0 ||
       off->valuedouble != (double)(uint32_t)off->valuedouble || size->valuedouble != (double)(uint32_t)size->valuedouble){
      mbedtls_sha256_free(&totalHash); setError(FLEXPKG_ERR_INDEX, "Entrada del indice invalida"); return false;
    }
    uint32_t offset = (uint32_t)off->valuedouble, bytes = (uint32_t)size->valuedouble;
    if(offset != expectedOffset || bytes > FLEXPKG_MAX_FILE_BYTES || offset + bytes > p.payloadLen){
      mbedtls_sha256_free(&totalHash); setError(FLEXPKG_ERR_INDEX, "Offsets del payload invalidos"); return false;
    }
    for(int j = 0; j < i; j++){
      cJSON* prev = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(p.index, j), "path");
      if(prev && cJSON_IsString(prev) && !strcmp(prev->valuestring, path->valuestring)){
        mbedtls_sha256_free(&totalHash); setError(FLEXPKG_ERR_PATH, "Ruta duplicada en el paquete"); return false;
      }
    }
    uint8_t expectedFileHash[32];
    if(!hexToBytes(hash->valuestring, expectedFileHash, 32)){
      mbedtls_sha256_free(&totalHash); setError(FLEXPKG_ERR_INDEX, "SHA-256 de archivo invalido"); return false;
    }
    char dst[FLEXPKG_PATH_MAX + FLEXPKG_ID_MAX + 48];
    File out;
    if(writeFiles){
      snprintf(dst, sizeof(dst), "%s/%s", stage, path->valuestring);
      if(!ensureParent(dst)){ mbedtls_sha256_free(&totalHash); setError(FLEXPKG_ERR_STORAGE, "No se pudo crear una carpeta de la app"); return false; }
      out = LittleFS.open(dst, "w");
      if(!out){ mbedtls_sha256_free(&totalHash); setError(FLEXPKG_ERR_STORAGE, "No se pudo escribir un archivo de la app"); return false; }
    }
    mbedtls_sha256_context fileHash; mbedtls_sha256_init(&fileHash); SHA_START(&fileHash);
    uint32_t remain = bytes;
    while(remain){
      size_t want = remain > sizeof(buffer) ? sizeof(buffer) : remain;
      if(!readExact(p.file, buffer, want)){
        if(out) out.close(); mbedtls_sha256_free(&fileHash); mbedtls_sha256_free(&totalHash);
        setError(FLEXPKG_ERR_SIZE, "Payload truncado"); return false;
      }
      if(SHA_UPDATE(&totalHash, buffer, want) != 0 || SHA_UPDATE(&fileHash, buffer, want) != 0 ||
         (writeFiles && out.write(buffer, want) != want)){
        if(out) out.close(); mbedtls_sha256_free(&fileHash); mbedtls_sha256_free(&totalHash);
        setError(FLEXPKG_ERR_STORAGE, "Fallo al verificar o escribir el payload"); return false;
      }
      remain -= want;
      uint8_t pct = (uint8_t)(10u + ((uint64_t)(offset + bytes - remain) * 75u / (p.payloadLen ? p.payloadLen : 1u)));
      if(!progress(cb, user, pct, writeFiles ? "Verificando e instalando" : "Verificando")){
        if(out) out.close(); mbedtls_sha256_free(&fileHash); mbedtls_sha256_free(&totalHash); return false;
      }
    }
    if(out) out.close();
    uint8_t actual[32]; SHA_FINISH(&fileHash, actual); mbedtls_sha256_free(&fileHash);
    if(memcmp(actual, expectedFileHash, 32)){
      mbedtls_sha256_free(&totalHash); setError(FLEXPKG_ERR_HASH, "SHA-256 de un archivo no coincide"); return false;
    }
    if(!strcmp(path->valuestring, p.info.entry)) entryFound = true;
    expectedOffset = offset + bytes;
  }
  if(expectedOffset != p.payloadLen || !entryFound){
    mbedtls_sha256_free(&totalHash); setError(FLEXPKG_ERR_INDEX, "Payload no declarado o entry ausente"); return false;
  }
  uint8_t signedHash[32]; SHA_FINISH(&totalHash, signedHash); mbedtls_sha256_free(&totalHash);
  if(memcmp(signedHash, p.header + 24, 32)){
    setError(FLEXPKG_ERR_HASH, "El paquete fue alterado"); return false;
  }
  if(!verifyP256(p.publicKey, signedHash, p.signature)){
    setError(FLEXPKG_ERR_SIGNATURE, "Firma ECDSA P-256 invalida"); return false;
  }
  if(writeFiles){
    if(!writeHidden(stage, MANIFEST_FILE, p.manifestRaw, p.manifestLen) ||
       !writeHidden(stage, INDEX_FILE, p.indexRaw, p.indexLen) || !smokeEntrypoint(stage, p.info)){
      setError(FLEXPKG_ERR_RUNTIME, "El entrypoint flex-ui-1 no puede iniciarse"); return false;
    }
    p.info.installedBytes = dirBytes(stage);
  }
  return progress(cb, user, 90, "Firma valida");
}

static bool appPaths(const char* id, char* root, char* active, char* stage, char* old, size_t cap){
  if(!safeId(id)) return false;
  snprintf(root, cap, "%s/%s", ROOT_DIR, id);
  snprintf(active, cap, "%s%s", root, ACTIVE_NAME);
  snprintf(stage, cap, "%s%s", root, STAGE_NAME);
  snprintf(old, cap, "%s%s", root, OLD_NAME);
  return true;
}

static bool runPackage(const char* packagePath, FlexPkgInfo* out, bool install,
                       FlexPkgProgressFn cb, void* user){
  if(gBusy){ setError(FLEXPKG_ERR_BUSY, "El instalador ya esta ocupado"); return false; }
  gBusy = true; gCancel = false; setError(FLEXPKG_OK, "Correcto");
  ParsedPackage p;
  bool ok = progress(cb, user, 1, "Abriendo paquete") && parseOpen(packagePath, p);
  char root[360] = {0}, active[360] = {0}, stage[360] = {0}, old[360] = {0};
  if(ok && install){
    appPaths(p.info.id, root, active, stage, old, sizeof(root));
    FlexPkgInfo current;
    if(readManifestAt(active, &current)){
      if(strcmp(current.developerKeySha256, p.info.developerKeySha256)){
        setError(FLEXPKG_ERR_DEVELOPER, "La actualizacion usa otra clave de desarrollador"); ok = false;
      } else if(p.info.versionCode <= current.versionCode){
        setError(FLEXPKG_ERR_VERSION, "La version instalada es igual o mas reciente"); ok = false;
      }
    }
    uint64_t needed = (uint64_t)p.payloadLen + p.manifestLen + p.indexLen + 4096u;
    uint64_t freeBytes = (uint64_t)LittleFS.totalBytes() - LittleFS.usedBytes();
    if(ok && needed > freeBytes){ setError(FLEXPKG_ERR_STORAGE, "No hay espacio para validar la version nueva"); ok = false; }
    if(ok && !mkdirs(root)){ setError(FLEXPKG_ERR_STORAGE, "No se pudo crear la carpeta de la app"); ok = false; }
  }
  if(ok) ok = validateAndExtract(p, stage, install, cb, user);
  if(ok && install){
    removeTree(old);
    bool hadActive = LittleFS.exists(active);
    if(hadActive && !LittleFS.rename(active, old)){
      setError(FLEXPKG_ERR_COMMIT, "No se pudo preparar la actualizacion"); ok = false;
    }
    if(ok && !LittleFS.rename(stage, active)){
      if(hadActive) LittleFS.rename(old, active);
      setError(FLEXPKG_ERR_COMMIT, "No se pudo activar la version nueva"); ok = false;
    }
    if(ok){
      removeTree(old);                         // solo queda la version mas reciente
      progress(cb, user, 100, "Aplicacion instalada");
    }
  }
  if(!ok && install) removeTree(stage);
  if(out && ok) *out = p.info;
  parsedClose(p);
  gBusy = false;
  return ok;
}

} // namespace

bool flexPkgBegin(){
  setError(FLEXPKG_OK, "Correcto");
  // FlexOS_FS ya monto LittleFS con la etiqueta real de la particion
  // (spiffs/littlefs/ffat/storage). Volver a llamar begin() aqui podria
  // intentar la etiqueta por defecto y desmontar una particion valida.
  if(LittleFS.totalBytes() == 0){ setError(FLEXPKG_ERR_FS, "LittleFS no esta disponible"); return false; }
  if(!mkdirOne(ROOT_DIR)){ setError(FLEXPKG_ERR_FS, "No se pudo crear /FlexApps"); return false; }
  File root = LittleFS.open(ROOT_DIR, "r");
  if(!root || !root.isDirectory()){ if(root) root.close(); return false; }
  File e = root.openNextFile();
  while(e){
    if(e.isDirectory()){
      char id[FLEXPKG_ID_MAX + 1];
      snprintf(id, sizeof(id), "%s", baseName(e.name()));
      char appRoot[360], active[360], stage[360], old[360];
      appPaths(id, appRoot, active, stage, old, sizeof(appRoot));
      e.close();
      removeTree(stage);
      if(!LittleFS.exists(active) && LittleFS.exists(old)) LittleFS.rename(old, active);
      if(LittleFS.exists(active)) removeTree(old);
    } else e.close();
    e = root.openNextFile();
  }
  root.close();
  return true;
}

bool flexPkgInspect(const char* packagePath, FlexPkgInfo* out, FlexPkgProgressFn cb, void* user){
  return runPackage(packagePath, out, false, cb, user);
}

bool flexPkgInstall(const char* packagePath, FlexPkgInfo* out, FlexPkgProgressFn cb, void* user){
  return runPackage(packagePath, out, true, cb, user);
}

bool flexPkgUninstall(const char* packageId){
  if(gBusy){ setError(FLEXPKG_ERR_BUSY, "El instalador ya esta ocupado"); return false; }
  char root[360], active[360], stage[360], old[360];
  if(!appPaths(packageId, root, active, stage, old, sizeof(root)) || !LittleFS.exists(root)){
    setError(FLEXPKG_ERR_NOT_FOUND, "Aplicacion no encontrada"); return false;
  }
  if(!removeTree(root)){ setError(FLEXPKG_ERR_STORAGE, "No se pudo eliminar la aplicacion"); return false; }
  setError(FLEXPKG_OK, "Correcto");
  return true;
}

int flexPkgList(FlexPkgInfo* out, int maxItems){
  if(!out || maxItems <= 0 || !LittleFS.exists(ROOT_DIR)) return 0;
  int n = 0;
  File root = LittleFS.open(ROOT_DIR, "r");
  if(!root || !root.isDirectory()){ if(root) root.close(); return 0; }
  File e = root.openNextFile();
  while(e && n < maxItems){
    if(e.isDirectory()){
      char active[360]; snprintf(active, sizeof(active), "%s/%s%s", ROOT_DIR, baseName(e.name()), ACTIVE_NAME);
      e.close();
      if(readManifestAt(active, &out[n])) n++;
    } else e.close();
    e = root.openNextFile();
  }
  if(e) e.close(); root.close();
  return n;
}

bool flexPkgGet(const char* packageId, FlexPkgInfo* out){
  if(!out || !safeId(packageId)) return false;
  char active[360]; snprintf(active, sizeof(active), "%s/%s%s", ROOT_DIR, packageId, ACTIVE_NAME);
  return readManifestAt(active, out);
}

bool flexPkgActiveRoot(const char* packageId, char* out, size_t outSize){
  if(!out || outSize == 0 || !safeId(packageId)) return false;
  int n = snprintf(out, outSize, "%s/%s%s", ROOT_DIR, packageId, ACTIVE_NAME);
  return n > 0 && (size_t)n < outSize && LittleFS.exists(out);
}

bool flexPkgEntryPath(const char* packageId, char* out, size_t outSize){
  FlexPkgInfo info; char root[360];
  if(!out || !flexPkgGet(packageId, &info) || !flexPkgActiveRoot(packageId, root, sizeof(root))) return false;
  int n = snprintf(out, outSize, "%s/%s", root, info.entry);
  return n > 0 && (size_t)n < outSize && LittleFS.exists(out);
}

FlexPkgErrorCode flexPkgErrorCode(){ return gErr; }
const char* flexPkgError(){ return gErrText; }
bool flexPkgBusy(){ return gBusy; }
void flexPkgCancel(){ gCancel = true; }
