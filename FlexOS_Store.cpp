#include "FlexOS_Store.h"

#include <FS.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cJSON.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/bignum.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/sha256.h"
#include "mbedtls/version.h"

#ifndef FLEXSTORE_CATALOG_URL
#define FLEXSTORE_CATALOG_URL "https://flex-developer-studio.ralvarezsantos980.chatgpt.site/api/catalog"
#endif

namespace {

static const char* STORE_ORIGIN = "https://flex-developer-studio.ralvarezsantos980.chatgpt.site/";
static const char* TEMP_PACKAGE = "/FlexApps/.download.flexpkg";
static const uint32_t CATALOG_MAX = 160u * 1024u;
static const uint32_t HTTP_TIMEOUT_MS = 15000;

// Clave publica estable del catalogo oficial. La clave privada vive solamente
// como secreto del backend de Flex Developer Studio. Cada respuesta /api/catalog
// esta firmada con ES256 antes de que el P4 confie en sus URLs o hashes.
static const uint8_t CATALOG_PUBLIC_KEY[65] = {
  0x04,0x70,0x9d,0xf4,0x80,0xde,0x8d,0x66,0x05,0x38,0x6b,0x01,0xb3,0xf8,0x9f,0x20,
  0xf7,0x29,0x08,0x7f,0x76,0xff,0x76,0xfd,0x43,0x9d,0x50,0xa8,0x30,0x9e,0xac,0xc7,
  0x60,0xf5,0x4d,0x60,0xbb,0x85,0xde,0xa2,0xd3,0x79,0x8e,0x15,0xab,0xad,0x89,0xd5,
  0x97,0xa4,0xc0,0x7c,0xfa,0x66,0xc1,0x70,0x6d,0xe6,0x48,0xb3,0x8e,0x40,0x28,0x2c,
  0x05
};
static const char* CATALOG_KEY_ID = "69b91cf7a9246cc2084dda73ccef77b2dc1a372b18fec19b49cf980efd68af69";

static SemaphoreHandle_t gMutex = nullptr;
static TaskHandle_t gTask = nullptr;
static volatile FlexStoreState gState = FLEXSTORE_IDLE;
static volatile uint8_t gProgress = 0;
static volatile bool gCancel = false;
static char gStage[64] = "Listo";
static char gError[128] = "";
static FlexStoreItem gCatalog[FLEXSTORE_MAX_CATALOG];
static int gCatalogN = 0;
static FlexStoreItem gRequest;
enum Work : uint8_t { WORK_REFRESH = 1, WORK_INSTALL = 2 };
static Work gWork = WORK_REFRESH;

static void lock(){ if(gMutex) xSemaphoreTake(gMutex, portMAX_DELAY); }
static void unlock(){ if(gMutex) xSemaphoreGive(gMutex); }

static void status(FlexStoreState state, uint8_t pct, const char* stage, const char* error = nullptr){
  lock();
  gState = state; gProgress = pct;
  snprintf(gStage, sizeof(gStage), "%s", stage ? stage : "");
  if(error) snprintf(gError, sizeof(gError), "%s", error); else if(state != FLEXSTORE_ERROR) gError[0] = 0;
  unlock();
}

static bool allowedUrl(const char* url){ return url && !strncmp(url, STORE_ORIGIN, strlen(STORE_ORIGIN)); }

static bool readHttpBody(HTTPClient& http, uint8_t** out, size_t* outLen, size_t maxBytes){
  *out = nullptr; *outLen = 0;
  int declared = http.getSize();
  if(declared > 0 && (size_t)declared > maxBytes) return false;
  uint8_t* data = (uint8_t*)malloc(maxBytes + 1);
  if(!data) return false;
  WiFiClient* stream = http.getStreamPtr();
  size_t used = 0;
  uint32_t last = millis();
  while(http.connected() || stream->available()){
    if(gCancel){ free(data); return false; }
    int avail = stream->available();
    if(avail > 0){
      size_t want = (size_t)avail;
      if(want > maxBytes - used) want = maxBytes - used;
      if(!want){ free(data); return false; }
      int got = stream->readBytes(data + used, want);
      if(got <= 0){ free(data); return false; }
      used += (size_t)got; last = millis();
      if(declared > 0 && used >= (size_t)declared) break;
    } else {
      if(millis() - last > HTTP_TIMEOUT_MS){ free(data); return false; }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
  if(declared > 0 && used != (size_t)declared){ free(data); return false; }
  data[used] = 0; *out = data; *outLen = used;
  return used > 0;
}

static int b64Value(char c){
  if(c >= 'A' && c <= 'Z') return c - 'A';
  if(c >= 'a' && c <= 'z') return c - 'a' + 26;
  if(c >= '0' && c <= '9') return c - '0' + 52;
  if(c == '-' || c == '+') return 62;
  if(c == '_' || c == '/') return 63;
  return -1;
}

static bool base64UrlDecode(const char* text, uint8_t** out, size_t* outLen, size_t maxOut){
  if(!text || !out || !outLen) return false;
  size_t n = strlen(text);
  size_t cap = (n * 3u) / 4u + 3u;
  if(cap > maxOut) return false;
  uint8_t* data = (uint8_t*)malloc(cap + 1);
  if(!data) return false;
  uint32_t acc = 0; int bits = 0; size_t used = 0;
  for(size_t i = 0; i < n; i++){
    if(text[i] == '=') break;
    int v = b64Value(text[i]);
    if(v < 0){ free(data); return false; }
    acc = (acc << 6) | (uint32_t)v; bits += 6;
    if(bits >= 8){ bits -= 8; if(used >= cap){ free(data); return false; } data[used++] = (uint8_t)(acc >> bits); }
  }
  data[used] = 0; *out = data; *outLen = used;
  return true;
}

#if MBEDTLS_VERSION_NUMBER >= 0x03000000
  #define SHA_START(ctx) mbedtls_sha256_starts((ctx), 0)
  #define SHA_UPDATE(ctx,p,n) mbedtls_sha256_update((ctx),(p),(n))
  #define SHA_FINISH(ctx,o) mbedtls_sha256_finish((ctx),(o))
#else
  #define SHA_START(ctx) mbedtls_sha256_starts_ret((ctx), 0)
  #define SHA_UPDATE(ctx,p,n) mbedtls_sha256_update_ret((ctx),(p),(n))
  #define SHA_FINISH(ctx,o) mbedtls_sha256_finish_ret((ctx),(o))
#endif

static bool verifyCatalogSignature(const uint8_t* payload, size_t payloadLen, const uint8_t sig[64]){
  uint8_t hash[32];
  mbedtls_sha256_context hc; mbedtls_sha256_init(&hc);
  int rc = SHA_START(&hc);
  if(rc == 0) rc = SHA_UPDATE(&hc, payload, payloadLen);
  if(rc == 0) rc = SHA_FINISH(&hc, hash);
  mbedtls_sha256_free(&hc);
  if(rc != 0) return false;
  mbedtls_ecp_group grp; mbedtls_ecp_group_init(&grp);
  mbedtls_ecp_point q; mbedtls_ecp_point_init(&q);
  mbedtls_mpi r, s; mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
  rc = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
  if(rc == 0) rc = mbedtls_mpi_read_binary(&q.X, CATALOG_PUBLIC_KEY + 1, 32);
  if(rc == 0) rc = mbedtls_mpi_read_binary(&q.Y, CATALOG_PUBLIC_KEY + 33, 32);
  if(rc == 0) rc = mbedtls_mpi_lset(&q.Z, 1);
  if(rc == 0) rc = mbedtls_ecp_check_pubkey(&grp, &q);
  if(rc == 0) rc = mbedtls_mpi_read_binary(&r, sig, 32);
  if(rc == 0) rc = mbedtls_mpi_read_binary(&s, sig + 32, 32);
  if(rc == 0) rc = mbedtls_ecdsa_verify(&grp, hash, 32, &q, &r, &s);
  mbedtls_mpi_free(&s); mbedtls_mpi_free(&r); mbedtls_ecp_point_free(&q); mbedtls_ecp_group_free(&grp);
  return rc == 0;
}

static bool copyString(cJSON* obj, const char* key, char* out, size_t cap, bool required = true){
  cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
  if(!v){ if(!required){ out[0] = 0; return true; } return false; }
  if(!cJSON_IsString(v) || !v->valuestring || strlen(v->valuestring) >= cap) return false;
  memcpy(out, v->valuestring, strlen(v->valuestring) + 1);
  return true;
}

static bool parseCatalogPayload(const uint8_t* payload, size_t payloadLen){
  cJSON* root = cJSON_ParseWithLength((const char*)payload, payloadLen);
  if(!root) return false;
  cJSON* schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
  cJSON* platform = cJSON_GetObjectItemCaseSensitive(root, "platform");
  cJSON* apps = cJSON_GetObjectItemCaseSensitive(root, "apps");
  bool ok = cJSON_IsNumber(schema) && schema->valuedouble == 1.0 && cJSON_IsString(platform) &&
            !strcmp(platform->valuestring, "flex_os_ultra_p4") && cJSON_IsArray(apps);
  FlexStoreItem* next = (FlexStoreItem*)calloc(FLEXSTORE_MAX_CATALOG, sizeof(FlexStoreItem));
  if(!next){ cJSON_Delete(root); return false; }
  int count = ok ? cJSON_GetArraySize(apps) : 0;
  if(count < 0) ok = false;
  if(count > FLEXSTORE_MAX_CATALOG) count = FLEXSTORE_MAX_CATALOG;
  int used = 0;
  for(int i = 0; ok && i < count; i++){
    cJSON* a = cJSON_GetArrayItem(apps, i);
    cJSON* v = a ? cJSON_GetObjectItemCaseSensitive(a, "version") : nullptr;
    if(!cJSON_IsObject(a) || !cJSON_IsObject(v) ||
       !copyString(a, "id", next[used].recordId, sizeof(next[used].recordId)) ||
       !copyString(a, "packageId", next[used].packageId, sizeof(next[used].packageId)) ||
       !copyString(a, "name", next[used].name, sizeof(next[used].name)) ||
       !copyString(a, "summary", next[used].summary, sizeof(next[used].summary), false) ||
       !copyString(a, "category", next[used].category, sizeof(next[used].category), false) ||
       !copyString(v, "name", next[used].versionName, sizeof(next[used].versionName)) ||
       !copyString(v, "minFlexVersion", next[used].minFlexOS, sizeof(next[used].minFlexOS)) ||
       !copyString(v, "sha256", next[used].packageSha256, sizeof(next[used].packageSha256)) ||
       !copyString(v, "downloadUrl", next[used].downloadUrl, sizeof(next[used].downloadUrl)) ||
       !allowedUrl(next[used].downloadUrl)) { ok = false; break; }
    cJSON* code = cJSON_GetObjectItemCaseSensitive(v, "code");
    cJSON* size = cJSON_GetObjectItemCaseSensitive(v, "size");
    cJSON* rating = cJSON_GetObjectItemCaseSensitive(a, "rating");
    cJSON* ratingCount = cJSON_GetObjectItemCaseSensitive(a, "ratingCount");
    if(!cJSON_IsNumber(code) || code->valuedouble < 1 || code->valuedouble > 4294967295.0 ||
       !cJSON_IsNumber(size) || size->valuedouble < 1 || size->valuedouble > FLEXPKG_MAX_PACKAGE_BYTES ||
       !cJSON_IsNumber(rating) || rating->valuedouble < 0 || rating->valuedouble > 5 ||
       !cJSON_IsNumber(ratingCount) || ratingCount->valuedouble < 0 || ratingCount->valuedouble > 4294967295.0 ||
       strlen(next[used].packageSha256) != 64){ ok = false; break; }
    next[used].versionCode = (uint32_t)code->valuedouble;
    next[used].packageBytes = (uint32_t)size->valuedouble;
    next[used].ratingX100 = (uint16_t)(rating->valuedouble * 100.0 + 0.5);
    next[used].ratingCount = (uint32_t)ratingCount->valuedouble;
    used++;
  }
  cJSON_Delete(root);
  if(!ok){ free(next); return false; }
  lock(); memcpy(gCatalog, next, sizeof(FlexStoreItem) * used); gCatalogN = used; unlock();
  free(next);
  return true;
}

static bool refreshCatalog(){
  status(FLEXSTORE_LOADING, 5, "Conectando al catalogo");
  if(WiFi.status() != WL_CONNECTED){ status(FLEXSTORE_ERROR, 0, "Sin conexion", "Conecta Wi-Fi para abrir Flex Store"); return false; }
  WiFiClientSecure sec;
  // La autenticidad NO depende de este canal: el envelope del catalogo esta
  // firmado por CATALOG_PUBLIC_KEY y cada paquete vuelve a verificarse con
  // SHA-256 + ECDSA. Esto permite sobrevivir a rotaciones del certificado del
  // hosting sin aceptar un catalogo o binario modificado.
  sec.setInsecure();
  sec.setHandshakeTimeout(12);
  HTTPClient http; http.setTimeout(HTTP_TIMEOUT_MS); http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if(!http.begin(sec, FLEXSTORE_CATALOG_URL)){ status(FLEXSTORE_ERROR, 0, "Error HTTPS", "No se pudo abrir el catalogo"); return false; }
  int code = http.GET();
  if(code != HTTP_CODE_OK){ http.end(); status(FLEXSTORE_ERROR, 0, "Error del servidor", "El catalogo no respondio correctamente"); return false; }
  uint8_t* envelope = nullptr; size_t envelopeLen = 0;
  bool ok = readHttpBody(http, &envelope, &envelopeLen, CATALOG_MAX); http.end();
  if(!ok){ status(gCancel ? FLEXSTORE_CANCELLED : FLEXSTORE_ERROR, 0, "Descarga cancelada", gCancel ? nullptr : "Respuesta del catalogo incompleta"); return false; }
  status(FLEXSTORE_LOADING, 45, "Verificando firma del catalogo");
  cJSON* root = cJSON_ParseWithLength((const char*)envelope, envelopeLen);
  free(envelope);
  if(!root){ status(FLEXSTORE_ERROR, 0, "Catalogo invalido", "JSON del catalogo invalido"); return false; }
  cJSON* schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
  cJSON* alg = cJSON_GetObjectItemCaseSensitive(root, "algorithm");
  cJSON* key = cJSON_GetObjectItemCaseSensitive(root, "keyId");
  cJSON* payloadNode = cJSON_GetObjectItemCaseSensitive(root, "payload");
  cJSON* sigNode = cJSON_GetObjectItemCaseSensitive(root, "signature");
  ok = cJSON_IsNumber(schema) && schema->valuedouble == 2.0 && cJSON_IsString(alg) && !strcmp(alg->valuestring, "ES256") &&
       cJSON_IsString(key) && !strcmp(key->valuestring, CATALOG_KEY_ID) && cJSON_IsString(payloadNode) && cJSON_IsString(sigNode);
  uint8_t *payload = nullptr, *sig = nullptr; size_t payloadLen = 0, sigLen = 0;
  if(ok) ok = base64UrlDecode(payloadNode->valuestring, &payload, &payloadLen, CATALOG_MAX) &&
              base64UrlDecode(sigNode->valuestring, &sig, &sigLen, 64) && sigLen == 64;
  cJSON_Delete(root);
  if(ok) ok = verifyCatalogSignature(payload, payloadLen, sig);
  if(ok) ok = parseCatalogPayload(payload, payloadLen);
  free(sig); free(payload);
  if(!ok){ status(FLEXSTORE_ERROR, 0, "Firma invalida", "Flex Store rechazo un catalogo no autentico"); return false; }
  status(FLEXSTORE_READY, 100, "Catalogo actualizado");
  return true;
}

static bool hexHash(const char* hex, const uint8_t hash[32]){
  static const char h[] = "0123456789abcdef";
  if(!hex || strlen(hex) != 64) return false;
  for(int i = 0; i < 32; i++) if(hex[i * 2] != h[hash[i] >> 4] || hex[i * 2 + 1] != h[hash[i] & 15]) return false;
  return true;
}

static bool pkgProgress(uint8_t pct, const char* stageText, void*){
  if(gCancel) return false;
  uint8_t mapped = (uint8_t)(58u + ((uint16_t)pct * 42u / 100u));
  status(FLEXSTORE_INSTALLING, mapped, stageText);
  return true;
}

static bool downloadAndInstall(const FlexStoreItem& item){
  status(FLEXSTORE_DOWNLOADING, 3, "Conectando a la descarga");
  if(WiFi.status() != WL_CONNECTED || !allowedUrl(item.downloadUrl)){
    status(FLEXSTORE_ERROR, 0, "Sin conexion", "No se puede descargar esta app"); return false;
  }
  WiFiClientSecure sec; sec.setInsecure(); sec.setHandshakeTimeout(12);
  HTTPClient http; http.setTimeout(HTTP_TIMEOUT_MS); http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if(!http.begin(sec, item.downloadUrl)){ status(FLEXSTORE_ERROR, 0, "Error HTTPS", "No se pudo abrir la descarga"); return false; }
  int code = http.GET();
  if(code != HTTP_CODE_OK){ http.end(); status(FLEXSTORE_ERROR, 0, "Error del servidor", "La descarga no esta disponible"); return false; }
  int declared = http.getSize();
  if(declared <= 0 || (uint32_t)declared != item.packageBytes || declared > (int)FLEXPKG_MAX_PACKAGE_BYTES){
    http.end(); status(FLEXSTORE_ERROR, 0, "Tamano invalido", "El paquete no coincide con el catalogo firmado"); return false;
  }
  LittleFS.remove(TEMP_PACKAGE);
  File out = LittleFS.open(TEMP_PACKAGE, "w");
  if(!out){ http.end(); status(FLEXSTORE_ERROR, 0, "Sin almacenamiento", "No se pudo crear la descarga temporal"); return false; }
  mbedtls_sha256_context hc; mbedtls_sha256_init(&hc); SHA_START(&hc);
  WiFiClient* stream = http.getStreamPtr();
  uint8_t buffer[4096]; uint32_t received = 0, last = millis(); bool ok = true;
  while(received < (uint32_t)declared){
    if(gCancel){ ok = false; break; }
    int avail = stream->available();
    if(avail > 0){
      size_t want = (size_t)avail;
      if(want > sizeof(buffer)) want = sizeof(buffer);
      if(want > (uint32_t)declared - received) want = (uint32_t)declared - received;
      int got = stream->readBytes(buffer, want);
      if(got <= 0 || out.write(buffer, got) != (size_t)got || SHA_UPDATE(&hc, buffer, got) != 0){ ok = false; break; }
      received += (uint32_t)got; last = millis();
      status(FLEXSTORE_DOWNLOADING, (uint8_t)(5u + ((uint64_t)received * 50u / declared)), "Descargando paquete firmado");
    } else {
      if(!http.connected() || millis() - last > HTTP_TIMEOUT_MS){ ok = false; break; }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
  out.close(); http.end();
  uint8_t digest[32]; if(SHA_FINISH(&hc, digest) != 0) ok = false; mbedtls_sha256_free(&hc);
  if(ok && !hexHash(item.packageSha256, digest)) ok = false;
  if(!ok){
    LittleFS.remove(TEMP_PACKAGE);
    status(gCancel ? FLEXSTORE_CANCELLED : FLEXSTORE_ERROR, 0, gCancel ? "Cancelado" : "Descarga rechazada",
           gCancel ? nullptr : "SHA-256 no coincide con el catalogo firmado");
    return false;
  }
  status(FLEXSTORE_INSTALLING, 58, "Verificando Flex Package");
  FlexPkgInfo installed;
  ok = flexPkgInstall(TEMP_PACKAGE, &installed, pkgProgress, nullptr);
  LittleFS.remove(TEMP_PACKAGE);
  if(!ok){
    status(flexPkgErrorCode() == FLEXPKG_ERR_CANCELLED ? FLEXSTORE_CANCELLED : FLEXSTORE_ERROR, 0,
           "Paquete rechazado", flexPkgError());
    return false;
  }
  status(FLEXSTORE_SUCCESS, 100, "Aplicacion instalada");
  return true;
}

static void worker(void*){
  Work work = gWork;
  if(work == WORK_REFRESH) refreshCatalog();
  else downloadAndInstall(gRequest);
  lock(); gTask = nullptr; unlock();
  vTaskDelete(nullptr);
}

static bool start(Work work){
  lock();
  if(gTask){ unlock(); return false; }
  gCancel = false; gWork = work;
  BaseType_t rc = xTaskCreate(worker, "flex_store", 24576, nullptr, 1, &gTask);
  if(rc != pdPASS) gTask = nullptr;
  unlock();
  if(rc != pdPASS){ status(FLEXSTORE_ERROR, 0, "Sin memoria", "No se pudo iniciar la tarea de Flex Store"); return false; }
  return true;
}

} // namespace

void flexStoreBegin(){
  if(!gMutex) gMutex = xSemaphoreCreateMutex();
  gState = FLEXSTORE_IDLE; gProgress = 0; gCatalogN = 0; gCancel = false;
  snprintf(gStage, sizeof(gStage), "Listo"); gError[0] = 0;
}

bool flexStoreRefresh(){ return start(WORK_REFRESH); }

bool flexStoreInstall(int catalogIndex){
  lock();
  bool ok = catalogIndex >= 0 && catalogIndex < gCatalogN && !gTask;
  if(ok) gRequest = gCatalog[catalogIndex];
  unlock();
  return ok && start(WORK_INSTALL);
}

void flexStoreCancel(){ gCancel = true; flexPkgCancel(); }

FlexStoreState flexStoreState(){ return gState; }
uint8_t flexStoreProgress(){ return gProgress; }
const char* flexStoreStage(){ return gStage; }
const char* flexStoreError(){ return gError; }

int flexStoreCatalogCount(){ lock(); int n = gCatalogN; unlock(); return n; }

bool flexStoreCatalogItem(int index, FlexStoreItem* out){
  if(!out) return false;
  lock(); bool ok = index >= 0 && index < gCatalogN; if(ok) *out = gCatalog[index]; unlock();
  return ok;
}

bool flexStoreHasUpdate(const FlexStoreItem* item, FlexPkgInfo* installed){
  if(!item || !item->packageId[0]) return false;
  FlexPkgInfo local;
  if(!flexPkgGet(item->packageId, &local)) return false;
  if(installed) *installed = local;
  return item->versionCode > local.versionCode;
}
