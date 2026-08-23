#include "FlexOS_Account.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cJSON.h>
#include <string.h>

#include "FlexOS_OTA.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/bignum.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/sha256.h"
#include "mbedtls/version.h"

#ifndef FLEX_ACCOUNT_CODE_URL
#define FLEX_ACCOUNT_CODE_URL "https://flex-developer-studio.ralvarezsantos980.chatgpt.site/api/devices/code"
#endif

namespace {

static const char* ACCOUNT_ACTIVATE_PREFIX = "https://flex-developer-studio.ralvarezsantos980.chatgpt.site/activate?code=";
static const char* ACCOUNT_KEY_ID = "69b91cf7a9246cc2084dda73ccef77b2dc1a372b18fec19b49cf980efd68af69";
static const uint32_t HTTP_TIMEOUT_MS = 15000;
static const uint32_t LINK_TIMEOUT_MS = 10UL * 60UL * 1000UL;
static const size_t ENVELOPE_MAX = 8192;

// Misma clave publica anclada que Flex Store. La privada existe solamente en
// Flex Developer Studio. Por eso el flujo de enlace sigue siendo autentico
// incluso si el certificado del hosting rota: el P4 no acepta un codigo ni un
// estado de cuenta que no lleven una firma ES256 valida.
static const uint8_t ACCOUNT_PUBLIC_KEY[65] = {
  0x04,0x70,0x9d,0xf4,0x80,0xde,0x8d,0x66,0x05,0x38,0x6b,0x01,0xb3,0xf8,0x9f,0x20,
  0xf7,0x29,0x08,0x7f,0x76,0xff,0x76,0xfd,0x43,0x9d,0x50,0xa8,0x30,0x9e,0xac,0xc7,
  0x60,0xf5,0x4d,0x60,0xbb,0x85,0xde,0xa2,0xd3,0x79,0x8e,0x15,0xab,0xad,0x89,0xd5,
  0x97,0xa4,0xc0,0x7c,0xfa,0x66,0xc1,0x70,0x6d,0xe6,0x48,0xb3,0x8e,0x40,0x28,0x2c,
  0x05
};

static SemaphoreHandle_t gMutex = nullptr;
static TaskHandle_t gTask = nullptr;
static volatile bool gStartRequested = false;
static volatile bool gCancelRequested = false;
static FlexAccountSnapshot gSnapshot;
static char gRequestedLabel[49] = "FlexOS Ultra";
static char gBearer[48] = "";          // 32 bytes codificados base64url = 43 caracteres

static void lock(){ if(gMutex) xSemaphoreTake(gMutex, portMAX_DELAY); }
static void unlock(){ if(gMutex) xSemaphoreGive(gMutex); }

static void setStatus(FlexAccountState state, uint8_t progress, const char* stage, const char* error = nullptr){
  lock();
  gSnapshot.state = state;
  gSnapshot.progress = progress;
  gSnapshot.linked = (state == FLEX_ACCOUNT_LINKED);
  snprintf(gSnapshot.stage, sizeof(gSnapshot.stage), "%s", stage ? stage : "");
  if(error) snprintf(gSnapshot.error, sizeof(gSnapshot.error), "%s", error);
  else if(state != FLEX_ACCOUNT_ERROR) gSnapshot.error[0] = 0;
  unlock();
}

static bool sha256(const uint8_t* data, size_t len, uint8_t out[32]){
  if(!data || !out) return false;
#if MBEDTLS_VERSION_NUMBER >= 0x03000000
  return mbedtls_sha256(data, len, out, 0) == 0;
#else
  return mbedtls_sha256_ret(data, len, out, 0) == 0;
#endif
}

static void hex32(const uint8_t data[32], char out[65]){
  static const char h[] = "0123456789abcdef";
  for(int i = 0; i < 32; i++){ out[i * 2] = h[data[i] >> 4]; out[i * 2 + 1] = h[data[i] & 15]; }
  out[64] = 0;
}

static bool makeBearer(char out[48], char hashHex[65]){
  uint8_t randomBytes[32];
  esp_fill_random(randomBytes, sizeof(randomBytes));
  static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  size_t used = 0;
  uint32_t acc = 0; int bits = 0;
  for(size_t i = 0; i < sizeof(randomBytes); i++){
    acc = (acc << 8) | randomBytes[i]; bits += 8;
    while(bits >= 6){ bits -= 6; out[used++] = table[(acc >> bits) & 63u]; }
  }
  if(bits) out[used++] = table[(acc << (6 - bits)) & 63u];
  out[used] = 0;
  uint8_t digest[32];
  bool ok = used == 43 && sha256((const uint8_t*)out, used, digest);
  if(ok) hex32(digest, hashHex);
  memset(randomBytes, 0, sizeof(randomBytes));
  memset(digest, 0, sizeof(digest));
  return ok;
}

static void hardwareId(char out[40]){
  uint64_t mac = ESP.getEfuseMac();
  snprintf(out, 40, "P4-%04lX%08lX", (unsigned long)((mac >> 32) & 0xFFFFu), (unsigned long)(mac & 0xFFFFFFFFu));
}

static bool jsonEscape(const char* in, char* out, size_t cap){
  if(!in || !out || cap < 2) return false;
  size_t used = 0;
  for(const unsigned char* p = (const unsigned char*)in; *p; ++p){
    const char* esc = nullptr;
    if(*p == '\"') esc = "\\\"";
    else if(*p == '\\') esc = "\\\\";
    else if(*p == '\n') esc = "\\n";
    else if(*p == '\r') esc = "\\r";
    else if(*p == '\t') esc = "\\t";
    if(esc){
      if(used + 2 >= cap) return false;
      out[used++] = esc[0]; out[used++] = esc[1];
    } else {
      if(*p < 0x20 || used + 1 >= cap) return false;
      out[used++] = (char)*p;
    }
  }
  out[used] = 0;
  return true;
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
  size_t n = strlen(text), cap = (n * 3u) / 4u + 3u;
  if(cap > maxOut) return false;
  uint8_t* data = (uint8_t*)malloc(cap + 1);
  if(!data) return false;
  uint32_t acc = 0; int bits = 0; size_t used = 0;
  for(size_t i = 0; i < n; i++){
    if(text[i] == '=') break;
    int value = b64Value(text[i]);
    if(value < 0){ free(data); return false; }
    acc = (acc << 6) | (uint32_t)value; bits += 6;
    if(bits >= 8){ bits -= 8; if(used >= cap){ free(data); return false; } data[used++] = (uint8_t)(acc >> bits); }
  }
  data[used] = 0; *out = data; *outLen = used;
  return true;
}

static bool verifySignature(const uint8_t* payload, size_t payloadLen, const uint8_t signature[64]){
  uint8_t digest[32];
  if(!sha256(payload, payloadLen, digest)) return false;
  mbedtls_ecp_group group; mbedtls_ecp_group_init(&group);
  mbedtls_ecp_point key; mbedtls_ecp_point_init(&key);
  mbedtls_mpi r, s; mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
  int rc = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1);
  if(rc == 0) rc = mbedtls_ecp_point_read_binary(&group, &key, ACCOUNT_PUBLIC_KEY, sizeof(ACCOUNT_PUBLIC_KEY));
  if(rc == 0) rc = mbedtls_ecp_check_pubkey(&group, &key);
  if(rc == 0) rc = mbedtls_mpi_read_binary(&r, signature, 32);
  if(rc == 0) rc = mbedtls_mpi_read_binary(&s, signature + 32, 32);
  if(rc == 0) rc = mbedtls_ecdsa_verify(&group, digest, sizeof(digest), &key, &r, &s);
  mbedtls_mpi_free(&s); mbedtls_mpi_free(&r); mbedtls_ecp_point_free(&key); mbedtls_ecp_group_free(&group);
  memset(digest, 0, sizeof(digest));
  return rc == 0;
}

static bool signedPayload(const uint8_t* envelope, size_t envelopeLen, uint8_t** payload, size_t* payloadLen){
  *payload = nullptr; *payloadLen = 0;
  cJSON* root = cJSON_ParseWithLength((const char*)envelope, envelopeLen);
  if(!root) return false;
  cJSON* schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
  cJSON* algorithm = cJSON_GetObjectItemCaseSensitive(root, "algorithm");
  cJSON* keyId = cJSON_GetObjectItemCaseSensitive(root, "keyId");
  cJSON* encoded = cJSON_GetObjectItemCaseSensitive(root, "payload");
  cJSON* signature = cJSON_GetObjectItemCaseSensitive(root, "signature");
  bool ok = cJSON_IsNumber(schema) && schema->valuedouble == 2.0 &&
            cJSON_IsString(algorithm) && !strcmp(algorithm->valuestring, "ES256") &&
            cJSON_IsString(keyId) && !strcmp(keyId->valuestring, ACCOUNT_KEY_ID) &&
            cJSON_IsString(encoded) && cJSON_IsString(signature);
  uint8_t* sig = nullptr; size_t sigLen = 0;
  if(ok) ok = base64UrlDecode(encoded->valuestring, payload, payloadLen, ENVELOPE_MAX) &&
              base64UrlDecode(signature->valuestring, &sig, &sigLen, 64) && sigLen == 64;
  cJSON_Delete(root);
  if(ok) ok = verifySignature(*payload, *payloadLen, sig);
  free(sig);
  if(!ok){ free(*payload); *payload = nullptr; *payloadLen = 0; }
  return ok;
}

static bool readHttpBody(HTTPClient& http, uint8_t** out, size_t* outLen){
  *out = nullptr; *outLen = 0;
  int declared = http.getSize();
  if(declared > 0 && (size_t)declared > ENVELOPE_MAX) return false;
  uint8_t* data = (uint8_t*)malloc(ENVELOPE_MAX + 1);
  if(!data) return false;
  WiFiClient* stream = http.getStreamPtr();
  size_t used = 0; uint32_t last = millis();
  while(http.connected() || stream->available()){
    if(gCancelRequested){ free(data); return false; }
    int available = stream->available();
    if(available > 0){
      size_t wanted = (size_t)available;
      if(wanted > ENVELOPE_MAX - used) wanted = ENVELOPE_MAX - used;
      if(!wanted){ free(data); return false; }
      int got = stream->readBytes(data + used, wanted);
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

static bool copyJsonString(cJSON* object, const char* key, char* out, size_t cap, bool required = true){
  cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
  if(cJSON_IsNull(value) && !required){ out[0] = 0; return true; }
  if(!cJSON_IsString(value) || !value->valuestring || strlen(value->valuestring) >= cap){
    if(!required) out[0] = 0;
    return !required;
  }
  memcpy(out, value->valuestring, strlen(value->valuestring) + 1);
  return true;
}

static bool parseCodePayload(const uint8_t* payload, size_t len, const char* expectedHash,
                             char code[9], char activationUrl[192]){
  cJSON* root = cJSON_ParseWithLength((const char*)payload, len);
  if(!root) return false;
  cJSON* schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
  cJSON* purpose = cJSON_GetObjectItemCaseSensitive(root, "purpose");
  char tokenHash[65] = "";
  bool ok = cJSON_IsNumber(schema) && schema->valuedouble == 1.0 &&
            cJSON_IsString(purpose) && !strcmp(purpose->valuestring, "flex-device-code") &&
            copyJsonString(root, "code", code, 9) &&
            copyJsonString(root, "tokenHash", tokenHash, sizeof(tokenHash)) &&
            copyJsonString(root, "activationUrl", activationUrl, 192) &&
            !strcmp(tokenHash, expectedHash) && strlen(code) >= 5 &&
            !strncmp(activationUrl, ACCOUNT_ACTIVATE_PREFIX, strlen(ACCOUNT_ACTIVATE_PREFIX));
  for(size_t i = 0; ok && code[i]; i++){
    char c = code[i]; ok = (c >= 'A' && c <= 'Z') || (c >= '2' && c <= '9');
  }
  cJSON_Delete(root);
  return ok;
}

enum PollResult : uint8_t { POLL_PENDING = 0, POLL_APPROVED, POLL_EXPIRED, POLL_INVALID };

static PollResult parseStatusPayload(const uint8_t* payload, size_t len, char address[48], char displayName[64]){
  cJSON* root = cJSON_ParseWithLength((const char*)payload, len);
  if(!root) return POLL_INVALID;
  cJSON* schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
  cJSON* purpose = cJSON_GetObjectItemCaseSensitive(root, "purpose");
  cJSON* state = cJSON_GetObjectItemCaseSensitive(root, "state");
  PollResult result = POLL_INVALID;
  if(cJSON_IsNumber(schema) && schema->valuedouble == 1.0 && cJSON_IsString(purpose) &&
     !strcmp(purpose->valuestring, "flex-device-status") && cJSON_IsString(state)){
    if(!strcmp(state->valuestring, "pending")) result = POLL_PENDING;
    else if(!strcmp(state->valuestring, "expired")) result = POLL_EXPIRED;
    else if(!strcmp(state->valuestring, "approved")){
      size_t addressLen = 0;
      if(copyJsonString(root, "flexAddress", address, 48) &&
         copyJsonString(root, "displayName", displayName, 64, false) &&
         (addressLen = strlen(address)) >= 6 &&
         strstr(address, "@flex") == address + addressLen - 5) result = POLL_APPROVED;
    }
  }
  cJSON_Delete(root);
  return result;
}

static bool persistLinked(const char* bearer, const char* address, const char* displayName){
  Preferences preferences;
  if(!preferences.begin("flexacct", false)) return false;
  size_t a = preferences.putString("token", bearer);
  size_t b = preferences.putString("address", address);
  preferences.putString("display", displayName ? displayName : "");
  bool c = preferences.putBool("linked", true);
  preferences.end();
  return a == strlen(bearer) && b == strlen(address) && c;
}

static bool requestDeviceCode(const char* label, const char* tokenHash, char code[9], char activationUrl[192]){
  if(WiFi.status() != WL_CONNECTED) return false;
  char escaped[104]; if(!jsonEscape(label, escaped, sizeof(escaped))) return false;
  char id[40]; hardwareId(id);
  char body[420];
  int written = snprintf(body, sizeof(body),
    "{\"hardwareId\":\"%s\",\"label\":\"%s\",\"model\":\"ESP32-P4\",\"flexVersion\":\"%s\",\"tokenHash\":\"%s\"}",
    id, escaped, FLEXOS_FW_VERSION, tokenHash);
  if(written <= 0 || written >= (int)sizeof(body)) return false;
  WiFiClientSecure secure;
  // No secreto viaja en esta operacion: solo una huella SHA-256. La respuesta
  // se acepta unicamente despues de validar la firma P-256 anclada arriba.
  secure.setInsecure();
  secure.setHandshakeTimeout(12);
  HTTPClient http; http.setTimeout(HTTP_TIMEOUT_MS); http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if(!http.begin(secure, FLEX_ACCOUNT_CODE_URL)) return false;
  http.addHeader("Content-Type", "application/json");
  int statusCode = http.POST((uint8_t*)body, (size_t)written);
  if(statusCode != HTTP_CODE_CREATED){ http.end(); return false; }
  uint8_t* envelope = nullptr; size_t envelopeLen = 0;
  bool ok = readHttpBody(http, &envelope, &envelopeLen); http.end();
  uint8_t* payload = nullptr; size_t payloadLen = 0;
  if(ok) ok = signedPayload(envelope, envelopeLen, &payload, &payloadLen);
  if(ok) ok = parseCodePayload(payload, payloadLen, tokenHash, code, activationUrl);
  free(payload); free(envelope);
  return ok;
}

static PollResult pollDeviceCode(const char* code, const char* tokenHash, char address[48], char displayName[64]){
  if(WiFi.status() != WL_CONNECTED) return POLL_PENDING;
  char url[320];
  int written = snprintf(url, sizeof(url), "%s?code=%s&tokenHash=%s", FLEX_ACCOUNT_CODE_URL, code, tokenHash);
  if(written <= 0 || written >= (int)sizeof(url)) return POLL_INVALID;
  WiFiClientSecure secure; secure.setInsecure(); secure.setHandshakeTimeout(12);
  HTTPClient http; http.setTimeout(HTTP_TIMEOUT_MS); http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if(!http.begin(secure, url)) return POLL_PENDING;
  int statusCode = http.GET();
  if(statusCode != HTTP_CODE_OK){ http.end(); return statusCode == HTTP_CODE_NOT_FOUND ? POLL_INVALID : POLL_PENDING; }
  uint8_t* envelope = nullptr; size_t envelopeLen = 0;
  bool ok = readHttpBody(http, &envelope, &envelopeLen); http.end();
  uint8_t* payload = nullptr; size_t payloadLen = 0;
  if(ok) ok = signedPayload(envelope, envelopeLen, &payload, &payloadLen);
  PollResult result = ok ? parseStatusPayload(payload, payloadLen, address, displayName) : POLL_INVALID;
  free(payload); free(envelope);
  return result;
}

static void linkFlow(const char* label){
  setStatus(FLEX_ACCOUNT_REQUESTING, 8, "Creando enlace seguro");
  char bearer[48] = "", tokenHash[65] = "", code[9] = "", activationUrl[192] = "";
  if(!makeBearer(bearer, tokenHash)){
    setStatus(FLEX_ACCOUNT_ERROR, 0, "No se pudo iniciar", "No se pudo crear la credencial segura"); return;
  }
  if(gCancelRequested){ setStatus(FLEX_ACCOUNT_CANCELLED, 0, "Cancelado"); return; }
  if(WiFi.status() != WL_CONNECTED){
    setStatus(FLEX_ACCOUNT_ERROR, 0, "Sin Wi-Fi", "Conecta el dispositivo a Wi-Fi y vuelve a intentar"); return;
  }
  setStatus(FLEX_ACCOUNT_REQUESTING, 28, "Contactando Flex Account");
  if(!requestDeviceCode(label, tokenHash, code, activationUrl)){
    if(gCancelRequested) setStatus(FLEX_ACCOUNT_CANCELLED, 0, "Cancelado");
    else setStatus(FLEX_ACCOUNT_ERROR, 0, "Enlace no disponible", "No se recibio una respuesta autentica de Flex Account");
    memset(bearer, 0, sizeof(bearer)); return;
  }
  lock();
  snprintf(gSnapshot.code, sizeof(gSnapshot.code), "%s", code);
  snprintf(gSnapshot.activationUrl, sizeof(gSnapshot.activationUrl), "%s", activationUrl);
  unlock();
  setStatus(FLEX_ACCOUNT_CODE_READY, 55, "Esperando aprobacion en tu celular");
  uint32_t started = millis(), lastPoll = 0; uint8_t transientFailures = 0;
  while(!gCancelRequested && millis() - started < LINK_TIMEOUT_MS){
    if(lastPoll && millis() - lastPoll < 3000){ vTaskDelay(pdMS_TO_TICKS(100)); continue; }
    lastPoll = millis();
    if(WiFi.status() != WL_CONNECTED){ setStatus(FLEX_ACCOUNT_CODE_READY, 55, "Esperando que vuelva el Wi-Fi"); continue; }
    char address[48] = "", displayName[64] = "";
    PollResult result = pollDeviceCode(code, tokenHash, address, displayName);
    if(result == POLL_PENDING){
      if(transientFailures < 8) transientFailures++;
      setStatus(FLEX_ACCOUNT_CODE_READY, 55, "Esperando aprobacion en tu celular");
    } else if(result == POLL_EXPIRED){
      setStatus(FLEX_ACCOUNT_EXPIRED, 0, "El codigo expiro"); memset(bearer, 0, sizeof(bearer)); return;
    } else if(result == POLL_INVALID){
      setStatus(FLEX_ACCOUNT_ERROR, 0, "Respuesta rechazada", "FlexOS rechazo un estado sin firma valida");
      memset(bearer, 0, sizeof(bearer)); return;
    } else {
      setStatus(FLEX_ACCOUNT_REQUESTING, 88, "Guardando tu Flex Account");
      if(!persistLinked(bearer, address, displayName)){
        setStatus(FLEX_ACCOUNT_ERROR, 0, "No se pudo guardar", "El almacenamiento seguro no respondio");
        memset(bearer, 0, sizeof(bearer)); return;
      }
      lock();
      snprintf(gBearer, sizeof(gBearer), "%s", bearer);
      snprintf(gSnapshot.flexAddress, sizeof(gSnapshot.flexAddress), "%s", address);
      snprintf(gSnapshot.displayName, sizeof(gSnapshot.displayName), "%s", displayName);
      unlock();
      setStatus(FLEX_ACCOUNT_LINKED, 100, "Cuenta vinculada");
      memset(bearer, 0, sizeof(bearer)); return;
    }
  }
  memset(bearer, 0, sizeof(bearer));
  if(gCancelRequested) setStatus(FLEX_ACCOUNT_CANCELLED, 0, "Cancelado");
  else setStatus(FLEX_ACCOUNT_EXPIRED, 0, "El codigo expiro");
}

static void accountTask(void*){
  for(;;){
    if(gStartRequested){
      char label[49];
      lock(); snprintf(label, sizeof(label), "%s", gRequestedLabel); gStartRequested = false; unlock();
      linkFlow(label);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

static void clearMemory(){
  lock();
  memset(gBearer, 0, sizeof(gBearer));
  memset(&gSnapshot, 0, sizeof(gSnapshot));
  gSnapshot.state = FLEX_ACCOUNT_UNLINKED;
  snprintf(gSnapshot.stage, sizeof(gSnapshot.stage), "Sin cuenta vinculada");
  unlock();
}

} // namespace

void flexAccountBegin(){
  if(!gMutex) gMutex = xSemaphoreCreateMutex();
  memset(&gSnapshot, 0, sizeof(gSnapshot));
  gSnapshot.state = FLEX_ACCOUNT_UNLINKED;
  snprintf(gSnapshot.stage, sizeof(gSnapshot.stage), "Sin cuenta vinculada");
  Preferences preferences;
  bool valid = false;
  if(preferences.begin("flexacct", true)){
    bool linked = preferences.getBool("linked", false);
    size_t tokenLen = preferences.getString("token", gBearer, sizeof(gBearer));
    size_t addressLen = preferences.getString("address", gSnapshot.flexAddress, sizeof(gSnapshot.flexAddress));
    preferences.getString("display", gSnapshot.displayName, sizeof(gSnapshot.displayName));
    preferences.end();
    valid = linked && tokenLen == 43 && addressLen >= 6 &&
            strstr(gSnapshot.flexAddress, "@flex") == gSnapshot.flexAddress + addressLen - 5;
  }
  if(valid){
    gSnapshot.state = FLEX_ACCOUNT_LINKED; gSnapshot.linked = true; gSnapshot.progress = 100;
    snprintf(gSnapshot.stage, sizeof(gSnapshot.stage), "Cuenta vinculada");
  } else {
    memset(gBearer, 0, sizeof(gBearer));
    gSnapshot.flexAddress[0] = 0; gSnapshot.displayName[0] = 0;
  }
  if(!gTask) xTaskCreate(accountTask, "flex-account", 12288, nullptr, 1, &gTask);
}

bool flexAccountRequestCode(const char* deviceLabel){
  if(!gMutex || !gTask) return false;
  lock();
  bool busy = gStartRequested || gSnapshot.state == FLEX_ACCOUNT_REQUESTING || gSnapshot.state == FLEX_ACCOUNT_CODE_READY;
  if(!busy){
    snprintf(gRequestedLabel, sizeof(gRequestedLabel), "%s", (deviceLabel && deviceLabel[0]) ? deviceLabel : "FlexOS Ultra");
    gSnapshot.code[0] = 0; gSnapshot.activationUrl[0] = 0; gSnapshot.error[0] = 0;
    gSnapshot.state = FLEX_ACCOUNT_REQUESTING; gSnapshot.progress = 1; gSnapshot.linked = false;
    snprintf(gSnapshot.stage, sizeof(gSnapshot.stage), "Preparando enlace");
    gCancelRequested = false; gStartRequested = true;
  }
  unlock();
  return !busy;
}

void flexAccountCancel(){ gCancelRequested = true; }

bool flexAccountLinked(){
  if(!gMutex) return false;
  lock(); bool result = gSnapshot.linked; unlock(); return result;
}

FlexAccountState flexAccountState(){
  if(!gMutex) return FLEX_ACCOUNT_UNLINKED;
  lock(); FlexAccountState result = gSnapshot.state; unlock(); return result;
}

void flexAccountSnapshot(FlexAccountSnapshot* out){
  if(!out) return;
  if(!gMutex){ memset(out, 0, sizeof(*out)); out->state = FLEX_ACCOUNT_UNLINKED; return; }
  lock(); memcpy(out, &gSnapshot, sizeof(*out)); unlock();
}

bool flexAccountCopyBearer(char* out, size_t capacity){
  if(!out || capacity == 0 || !gMutex) return false;
  lock();
  bool ok = gSnapshot.linked && gBearer[0] && strlen(gBearer) + 1 <= capacity;
  if(ok) memcpy(out, gBearer, strlen(gBearer) + 1); else out[0] = 0;
  unlock();
  return ok;
}

void flexAccountForgetLocal(){
  gCancelRequested = true;
  Preferences preferences;
  if(preferences.begin("flexacct", false)){ preferences.clear(); preferences.end(); }
  clearMemory();
}
