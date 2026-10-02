#include "FlexOS_Account.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cJSON.h>
#include <string.h>

#include "FlexOS_CloudTLS.h"
#include "FlexOS_OTA.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// BACKEND CRIPTOGRAFICO. Mismo patron que FlexOS_AppGrant y FlexOS_Passcode:
// en la placa mbedTLS (con el acelerador SHA del P4 por debajo), en el PC
// OpenSSL. Las dos rutas comprueban EXACTAMENTE lo mismo sobre los mismos
// bytes: asi este modulo, que antes no compilaba ninguna prueba, se ejecuta
// entero en tests/host/test_account.cpp.
#if defined(__has_include)
#  if __has_include(<mbedtls/ecdsa.h>)
#    define FXA_BACKEND_MBEDTLS 1
#  elif __has_include(<openssl/evp.h>)
#    define FXA_BACKEND_OPENSSL 1
#  endif
#endif
#if !defined(FXA_BACKEND_MBEDTLS) && !defined(FXA_BACKEND_OPENSSL)
#  error "FlexOS_Account: falta un backend criptografico (mbedTLS u OpenSSL)"
#endif
#if FXA_BACKEND_MBEDTLS
#  include "mbedtls/bignum.h"
#  include "mbedtls/ecdsa.h"
#  include "mbedtls/ecp.h"
#  include "mbedtls/sha256.h"
#  include "mbedtls/version.h"
#else
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#  include <openssl/bn.h>
#  include <openssl/ec.h>
#  include <openssl/ecdsa.h>
#  include <openssl/evp.h>
#  include <openssl/obj_mac.h>
#  include <openssl/sha.h>
#endif

#ifndef FLEX_ACCOUNT_CODE_URL
#define FLEX_ACCOUNT_CODE_URL "https://flex-developer-studio.ralvarezsantos980.chatgpt.site/api/devices/code"
#endif
// Validacion de la sesion: un GET autenticado con la credencial del aparato.
// Lo atiende Flex Cloud (mismo dominio que Flex Developer Studio), que a su vez
// pregunta a Flex Account: no hay un segundo sistema de identidad.
#ifndef FLEX_ACCOUNT_SESSION_URL
#define FLEX_ACCOUNT_SESSION_URL "https://flex-developer-studio.ralvarezsantos980.chatgpt.site/api/cloud/me"
#endif

namespace {

static const char* ACCOUNT_ACTIVATE_PREFIX = "https://flex-developer-studio.ralvarezsantos980.chatgpt.site/activate?code=";
static const char* ACCOUNT_KEY_ID = "69b91cf7a9246cc2084dda73ccef77b2dc1a372b18fec19b49cf980efd68af69";
static const uint32_t HTTP_TIMEOUT_MS = 15000;
static const uint32_t LINK_TIMEOUT_MS = 10UL * 60UL * 1000UL;
static const size_t ENVELOPE_MAX = 8192;
// Revalidacion periodica con la sesion ya validada: barata y rara. La que
// importa es la del flanco "vuelve el Wi-Fi", que es inmediata.
static const uint32_t REVALIDATE_MS = 6UL * 60UL * 60UL * 1000UL;
// Espera tras conectar el Wi-Fi antes de la primera validacion: deja que DHCP,
// DNS y NTP terminen y que el arranque no compita por la radio.
static const uint32_t SETTLE_MS = 3000;
// Un 401 de otro modulo pide revalidar, como mucho una vez por minuto.
static const uint32_t REJECT_RATE_MS = 60000;

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

#ifdef FLEXOS_HOST_TEST
// Las pruebas firman con una clave EFIMERA (la privada de produccion solo
// existe en Flex Developer Studio). En la placa esto no existe.
static uint8_t gTestKey[65];
static bool gTestKeySet = false;
#endif
static const uint8_t* accountKey(){
#ifdef FLEXOS_HOST_TEST
  if(gTestKeySet) return gTestKey;
#endif
  return ACCOUNT_PUBLIC_KEY;
}

// Valores de la clave "authst" (solo se escribe en las TRANSICIONES).
enum : uint8_t { AUTHST_OK = 0, AUTHST_REQUIRED = 1, AUTHST_EXPIRED = 2 };

static SemaphoreHandle_t gMutex = nullptr;
static TaskHandle_t gTask = nullptr;
static volatile bool gStartRequested = false;
static volatile bool gCancelRequested = false;
static FlexAccountSnapshot gSnapshot;
static char gRequestedLabel[49] = "FlexOS Ultra";
static char gBearer[48] = "";          // 32 bytes codificados base64url = 43 caracteres
static bool gHaveCredential = false;   // hay credencial guardada y cargada (independiente de la red)
static char gRequestError[128] = "";
static uint32_t gInstallationId = 0;    // distingue reinstalaciones del mismo P4
static int gLastHttpStatus = 0;
static uint8_t gAuthSt = AUTHST_OK;     // copia en RAM de "authst"
// Cambia cada vez que el USUARIO desvincula (flexAccountForgetLocal, desde el hilo
// de la interfaz). La validacion de la sesion corre en la tarea de fondo y tarda
// segundos: si mientras tanto se desvinculo, su resultado era de una credencial
// que ya no existe y no puede escribir nada en la NVS (un "address" suelto sin
// token dejaba la cuenta "incompleta" al reiniciar).
static volatile uint32_t gEpoch = 0;

// Estado de la validacion (solo lo toca la tarea de fondo, salvo las banderas).
static bool gWasOnline = false;
static volatile bool gValidateWanted = false;
// Espera hasta el proximo intento como "desde cuando" + "cuanto": comparar
// el TIEMPO TRANSCURRIDO sin signo sigue siendo correcto cuando millis() da
// la vuelta. Antes se guardaba un instante absoluto y se comparaba con
// (int32_t)(ahora - instante): a los 24,8 dias encendido esa resta cambiaba de
// signo y la revalidacion quedaba bloqueada hasta la vuelta de los 49 dias.
static volatile uint32_t gWaitFromMs = 0;
static volatile uint32_t gWaitMs = 0;
static uint32_t gLastAttemptMs = 0;
static bool gAttempted = false;
// Un Wi-Fi que entra y sale cada pocos segundos no puede convertirse en una
// validacion cada pocos segundos: entre dos intentos pasan al menos 30 s.
static const uint32_t MIN_ATTEMPT_GAP_MS = 30000;
static uint8_t gFailures = 0;
static uint32_t gLastVerifiedMs = 0;
static bool gVerifiedThisBoot = false;
static uint32_t gLastRejectMs = 0;
static bool gRejectSeen = false;

static void lock(){ if(gMutex) xSemaphoreTake(gMutex, portMAX_DELAY); }
static void unlock(){ if(gMutex) xSemaphoreGive(gMutex); }

static void setStatus(FlexAccountState state, uint8_t progress, const char* stage, const char* error = nullptr){
  lock();
  gSnapshot.state = state;
  gSnapshot.progress = progress;
  gSnapshot.linked = gHaveCredential;
  snprintf(gSnapshot.stage, sizeof(gSnapshot.stage), "%s", stage ? stage : "");
  if(error) snprintf(gSnapshot.error, sizeof(gSnapshot.error), "%s", error);
  else if(state != FLEX_ACCOUNT_ERROR) gSnapshot.error[0] = 0;
  unlock();
}

static void setLink(FlexAccountLink link, const char* detail){
  lock();
  gSnapshot.link = link;
  snprintf(gSnapshot.linkDetail, sizeof(gSnapshot.linkDetail), "%s", detail ? detail : "");
  unlock();
}

// ---------------------------------------------------------------------------
//  Criptografia (backend comun)
// ---------------------------------------------------------------------------
static bool sha256(const uint8_t* data, size_t len, uint8_t out[32]){
  if(!data || !out) return false;
#if FXA_BACKEND_MBEDTLS
#  if MBEDTLS_VERSION_NUMBER >= 0x03000000
  return mbedtls_sha256(data, len, out, 0) == 0;
#  else
  return mbedtls_sha256_ret(data, len, out, 0) == 0;
#  endif
#else
  return SHA256(data, len, out) != nullptr;
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
  snprintf(out, 40, "P4-%04lX%08lX-%08lX", (unsigned long)((mac >> 32) & 0xFFFFu),
           (unsigned long)(mac & 0xFFFFFFFFu), (unsigned long)gInstallationId);
}

static bool rotateInstallationId(){
  uint32_t next = 0;
  do { esp_fill_random(&next, sizeof(next)); } while(next == 0 || next == gInstallationId);
  Preferences preferences;
  if(!preferences.begin("flexacct", false)) return false;
  bool ok = preferences.putUInt("installId", next) == sizeof(next);
  preferences.end();
  if(ok) gInstallationId = next;
  return ok;
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
  size_t n = strlen(text);
  size_t encoded = n;
  while(encoded > 0 && text[encoded - 1] == '=') encoded--;
  size_t remainder = encoded & 3u;
  if(remainder == 1u) return false;
  size_t expected = (encoded / 4u) * 3u + (remainder ? remainder - 1u : 0u);
  if(expected > maxOut) return false;
  uint8_t* data = (uint8_t*)malloc(expected + 1u);
  if(!data) return false;
  uint32_t acc = 0; int bits = 0; size_t used = 0;
  for(size_t i = 0; i < n; i++){
    if(text[i] == '='){
      for(size_t j = i; j < n; j++) if(text[j] != '='){ free(data); return false; }
      break;
    }
    int value = b64Value(text[i]);
    if(value < 0){ free(data); return false; }
    acc = (acc << 6) | (uint32_t)value; bits += 6;
    if(bits >= 8){ bits -= 8; if(used >= expected){ free(data); return false; } data[used++] = (uint8_t)(acc >> bits); }
  }
  if(used != expected){ free(data); return false; }
  data[used] = 0; *out = data; *outLen = used;
  return true;
}

#if FXA_BACKEND_MBEDTLS
static bool verifySignature(const uint8_t* payload, size_t payloadLen, const uint8_t signature[64]){
  static const uint8_t P256_ORDER[32] = {
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xbc,0xe6,0xfa,0xad,0xa7,0x17,0x9e,0x84,0xf3,0xb9,0xca,0xc2,0xfc,0x63,0x25,0x51
  };
  static const uint8_t P256_HALF_ORDER[32] = {
    0x7f,0xff,0xff,0xff,0x80,0x00,0x00,0x00,0x7f,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xde,0x73,0x7d,0x56,0xd3,0x8b,0xcf,0x42,0x79,0xdc,0xe5,0x61,0x7e,0x31,0x92,0xa8
  };
  uint8_t digest[32];
  if(!sha256(payload, payloadLen, digest)) return false;
  mbedtls_ecp_group group; mbedtls_ecp_group_init(&group);
  mbedtls_ecp_point key; mbedtls_ecp_point_init(&key);
  mbedtls_mpi r, s, order, halfOrder;
  mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
  mbedtls_mpi_init(&order); mbedtls_mpi_init(&halfOrder);
  int rc = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1);
  if(rc == 0) rc = mbedtls_ecp_point_read_binary(&group, &key, accountKey(), sizeof(ACCOUNT_PUBLIC_KEY));
  if(rc == 0) rc = mbedtls_ecp_check_pubkey(&group, &key);
  if(rc == 0) rc = mbedtls_mpi_read_binary(&r, signature, 32);
  if(rc == 0) rc = mbedtls_mpi_read_binary(&s, signature + 32, 32);
  if(rc == 0) rc = mbedtls_mpi_read_binary(&order, P256_ORDER, sizeof(P256_ORDER));
  if(rc == 0) rc = mbedtls_mpi_read_binary(&halfOrder, P256_HALF_ORDER, sizeof(P256_HALF_ORDER));
  if(rc == 0 && mbedtls_mpi_cmp_mpi(&s, &halfOrder) > 0) rc = mbedtls_mpi_sub_mpi(&s, &order, &s);
  if(rc == 0) rc = mbedtls_ecdsa_verify(&group, digest, sizeof(digest), &key, &r, &s);
  mbedtls_mpi_free(&halfOrder); mbedtls_mpi_free(&order);
  mbedtls_mpi_free(&s); mbedtls_mpi_free(&r); mbedtls_ecp_point_free(&key); mbedtls_ecp_group_free(&group);
  memset(digest, 0, sizeof(digest));
  return rc == 0;
}
#else
// Ruta SOLO DE PC. Misma comprobacion sobre los mismos bytes (OpenSSL acepta
// tambien S alto, que es la misma firma).
static bool verifySignature(const uint8_t* payload, size_t payloadLen, const uint8_t signature[64]){
  uint8_t digest[32];
  if(!sha256(payload, payloadLen, digest)) return false;
  bool ok = false;
  EC_KEY* key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
  EC_POINT* pt = key ? EC_POINT_new(EC_KEY_get0_group(key)) : nullptr;
  ECDSA_SIG* sig = ECDSA_SIG_new();
  BIGNUM* r = BN_bin2bn(signature, 32, nullptr);
  BIGNUM* s = BN_bin2bn(signature + 32, 32, nullptr);
  if(key && pt && sig && r && s &&
     EC_POINT_oct2point(EC_KEY_get0_group(key), pt, accountKey(), sizeof(ACCOUNT_PUBLIC_KEY), nullptr) == 1 &&
     EC_KEY_set_public_key(key, pt) == 1 && ECDSA_SIG_set0(sig, r, s) == 1){
    r = s = nullptr;                        // ahora son de sig
    ok = ECDSA_do_verify(digest, 32, sig, key) == 1;
  }
  BN_free(r); BN_free(s);
  ECDSA_SIG_free(sig); EC_POINT_free(pt); EC_KEY_free(key);
  return ok;
}
#endif

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

// `honorCancel`: el flujo de enlace se puede cancelar; la validacion de la
// sesion no depende de esa bandera.
static bool readHttpBody(HTTPClient& http, uint8_t** out, size_t* outLen, bool honorCancel = true){
  *out = nullptr; *outLen = 0;
  int declared = http.getSize();
  if(declared > 0 && (size_t)declared > ENVELOPE_MAX) return false;
  WiFiClient* stream = http.getStreamPtr();
  if(!stream) return false;
  uint8_t* data = (uint8_t*)malloc(ENVELOPE_MAX + 1);
  if(!data) return false;
  size_t used = 0; uint32_t last = millis();
  while(http.connected() || stream->available()){
    if(honorCancel && gCancelRequested){ free(data); return false; }
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

static bool validAddress(const char* address){
  size_t len = address ? strlen(address) : 0;
  return len >= 6 && strstr(address, "@flex") == address + len - 5;
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
      if(copyJsonString(root, "flexAddress", address, 48) &&
         copyJsonString(root, "displayName", displayName, 64, false) &&
         validAddress(address)) result = POLL_APPROVED;
    }
  }
  cJSON_Delete(root);
  return result;
}

// Escribe la cuenta entera. La marca "linked" va la ULTIMA: si el corte de
// energia llega a mitad, el arranque siguiente ve un registro incompleto (y lo
// dice como ERROR, no como "sin cuenta") en vez de uno que parece valido.
static bool persistLinked(const char* bearer, const char* address, const char* displayName){
  Preferences preferences;
  if(!preferences.begin("flexacct", false)) return false;
  size_t a = preferences.putString("token", bearer);
  size_t b = preferences.putString("address", address);
  preferences.putString("display", displayName ? displayName : "");
  // Un enlace nuevo deja la sesion limpia (solo se escribe si hacia falta).
  if(preferences.getUChar("authst", AUTHST_OK) != AUTHST_OK) preferences.putUChar("authst", AUTHST_OK);
  bool c = preferences.putBool("linked", true);
  preferences.end();
  // putString devuelve strlen (sin terminador); getString, con terminador.
  return a == strlen(bearer) && b == strlen(address) && c;
}

static void persistAuthState(uint8_t st){
  if(st == gAuthSt) return;                       // sin cambios: ni una escritura de flash
  Preferences preferences;
  if(!preferences.begin("flexacct", false)) return;
  preferences.putUChar("authst", st);
  preferences.end();
  gAuthSt = st;
}

static void persistProfile(const char* address, const char* displayName){
  Preferences preferences;
  if(!preferences.begin("flexacct", false)) return;
  preferences.putString("address", address);
  preferences.putString("display", displayName ? displayName : "");
  preferences.end();
}

// Cliente HTTPS con la credencial: SIEMPRE con certificado validado.
static void secureClient(WiFiClientSecure& secure){
  secure.setCACert(flexCloudRootCA());
  secure.setHandshakeTimeout(12);
}

static bool requestDeviceCode(const char* label, const char* tokenHash, char code[9], char activationUrl[192]){
  gRequestError[0] = 0;
  gLastHttpStatus = 0;
  if(WiFi.status() != WL_CONNECTED){ snprintf(gRequestError, sizeof(gRequestError), "Wi-Fi se desconecto durante el enlace"); return false; }
  char escaped[104];
  if(!jsonEscape(label, escaped, sizeof(escaped))){ snprintf(gRequestError, sizeof(gRequestError), "Nombre del dispositivo no valido"); return false; }
  char id[40]; hardwareId(id);
  char body[420];
  int written = snprintf(body, sizeof(body),
    "{\"hardwareId\":\"%s\",\"label\":\"%s\",\"model\":\"ESP32-P4\",\"flexVersion\":\"%s\",\"tokenHash\":\"%s\"}",
    id, escaped, FLEXOS_FW_VERSION, tokenHash);
  if(written <= 0 || written >= (int)sizeof(body)){ snprintf(gRequestError, sizeof(gRequestError), "Solicitud de cuenta demasiado grande"); return false; }
  // Sin SRAM interna para el handshake no se intenta: se dice QUE hacer (cerrar
  // una app) en vez de un "Fallo HTTPS -1" sin explicacion.
  size_t inFree = 0, inBlock = 0;
  if(!flexTlsRoom(&inFree, &inBlock)){
    snprintf(gRequestError, sizeof(gRequestError), "Poca memoria interna (%u KB libres). Cierra una app y reintenta", (unsigned)(inFree / 1024u));
    Serial.printf("[ACCOUNT] enlace aplazado: SRAM interna %u KB (mayor bloque %u KB)\n", (unsigned)(inFree / 1024u), (unsigned)(inBlock / 1024u));
    return false;
  }
  WiFiClientSecure secure;
  // No secreto viaja en esta operacion: solo una huella SHA-256. La respuesta
  // se acepta unicamente despues de validar la firma P-256 anclada arriba.
  // (Ruta existente: se conserva tal cual para no romper el enlace si el
  // proveedor rota su certificado; la credencial NUNCA viaja por aqui.)
  secure.setInsecure();
  secure.setHandshakeTimeout(12);
  HTTPClient http; http.setTimeout(HTTP_TIMEOUT_MS); http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  // Cloudflare puede responder el POST con cuerpo chunked y mantener viva la
  // conexion. HTTP/1.0 + Connection: close da al lector un final inequivoco.
  http.useHTTP10(true);
  http.setUserAgent("FlexOS-Ultra/1.0 ESP32-P4");
  if(!http.begin(secure, FLEX_ACCOUNT_CODE_URL)){ snprintf(gRequestError, sizeof(gRequestError), "No se pudo abrir el servicio de Flex Account"); return false; }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  http.addHeader("Connection", "close");
  int statusCode = http.POST((uint8_t*)body, (size_t)written);
  gLastHttpStatus = statusCode;
  int tlsErr = 0;
  if(statusCode < 0){ char raw[64]; tlsErr = secure.lastError(raw, sizeof(raw)); }
  if(statusCode != HTTP_CODE_CREATED && statusCode != HTTP_CODE_OK){
    if(statusCode == HTTP_CODE_CONFLICT)
      snprintf(gRequestError, sizeof(gRequestError), "Este registro del dispositivo ya estaba vinculado");
    else if(statusCode == 429)
      snprintf(gRequestError, sizeof(gRequestError), "Demasiados intentos; espera un minuto");
    else if(statusCode > 0)
      snprintf(gRequestError, sizeof(gRequestError), "Flex Account respondio HTTP %d", statusCode);
    else {
      char why[48]; flexTlsReason(tlsErr, why, sizeof(why));
      snprintf(gRequestError, sizeof(gRequestError), "Fallo HTTPS %d (%s)", statusCode, why);
      Serial.printf("[ACCOUNT] enlace: HTTP %d, %s (mbedTLS %d), SRAM interna %u KB (bloque %u KB)\n",
                    statusCode, why, tlsErr, (unsigned)(inFree / 1024u), (unsigned)(inBlock / 1024u));
    }
    http.end(); return false;
  }
  uint8_t* envelope = nullptr; size_t envelopeLen = 0;
  bool ok = readHttpBody(http, &envelope, &envelopeLen); http.end();
  if(!ok) snprintf(gRequestError, sizeof(gRequestError), "Respuesta de Flex Account incompleta");
  uint8_t* payload = nullptr; size_t payloadLen = 0;
  if(ok && !signedPayload(envelope, envelopeLen, &payload, &payloadLen)){
    ok = false; snprintf(gRequestError, sizeof(gRequestError), "Firma ES256 de Flex Account no valida");
  }
  if(ok && !parseCodePayload(payload, payloadLen, tokenHash, code, activationUrl)){
    ok = false; snprintf(gRequestError, sizeof(gRequestError), "Datos de vinculacion incompatibles");
  }
  free(payload); free(envelope);
  return ok;
}

// `problem` recibe, si la consulta NO llego a contestarse, un motivo corto que la
// pantalla ensena mientras se reintenta. Antes quedaba mudo: con el codigo ya
// aprobado en el celular el aparato podia seguir diciendo "Esperando aprobacion"
// diez minutos, sin poder consultarlo, hasta que el codigo caducaba.
static PollResult pollDeviceCode(const char* code, const char* tokenHash, char address[48], char displayName[64],
                                 char* problem, size_t problemCap){
  if(problem && problemCap) problem[0] = 0;
  if(WiFi.status() != WL_CONNECTED) return POLL_PENDING;
  char url[320];
  int written = snprintf(url, sizeof(url), "%s?code=%s&tokenHash=%s", FLEX_ACCOUNT_CODE_URL, code, tokenHash);
  if(written <= 0 || written >= (int)sizeof(url)) return POLL_INVALID;
  size_t inFree = 0;
  if(!flexTlsRoom(&inFree, nullptr)){
    if(problem) snprintf(problem, problemCap, "Poca memoria interna (%u KB). Reintentando", (unsigned)(inFree / 1024u));
    return POLL_PENDING;
  }
  WiFiClientSecure secure; secure.setInsecure(); secure.setHandshakeTimeout(12);
  HTTPClient http; http.setTimeout(HTTP_TIMEOUT_MS); http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.useHTTP10(true);
  http.setUserAgent("FlexOS-Ultra/1.0 ESP32-P4");
  if(!http.begin(secure, url)){
    if(problem) snprintf(problem, problemCap, "No se pudo abrir la conexion. Reintentando");
    return POLL_PENDING;
  }
  http.addHeader("Accept", "application/json");
  http.addHeader("Connection", "close");
  int statusCode = http.GET();
  if(problem && statusCode < 0){
    char raw[64], why[48]; flexTlsReason(secure.lastError(raw, sizeof(raw)), why, sizeof(why));
    snprintf(problem, problemCap, "Sin conexion (%s). Reintentando", why);
  } else if(problem && statusCode != HTTP_CODE_OK && statusCode != HTTP_CODE_NOT_FOUND){
    snprintf(problem, problemCap, "Flex Account respondio HTTP %d. Reintentando", statusCode);
  }
  if(statusCode != HTTP_CODE_OK){ http.end(); return statusCode == HTTP_CODE_NOT_FOUND ? POLL_INVALID : POLL_PENDING; }
  uint8_t* envelope = nullptr; size_t envelopeLen = 0;
  bool ok = readHttpBody(http, &envelope, &envelopeLen); http.end();
  if(!ok){ free(envelope); return POLL_PENDING; }
  uint8_t* payload = nullptr; size_t payloadLen = 0;
  ok = signedPayload(envelope, envelopeLen, &payload, &payloadLen);
  PollResult result = ok ? parseStatusPayload(payload, payloadLen, address, displayName) : POLL_INVALID;
  free(payload); free(envelope);
  return result;
}

// ---------------------------------------------------------------------------
//  Validacion de la sesion
// ---------------------------------------------------------------------------
static const char* verdictDetail(FlexSessionVerdict v){
  switch(v){
    case FLEX_SESSION_OK:            return "Sesion validada";
    case FLEX_SESSION_AUTH_REQUIRED: return "Flex Account ya no reconoce este aparato: vuelve a vincularlo";
    case FLEX_SESSION_TOKEN_EXPIRED: return "La sesion de este aparato caduco: vuelve a vincularlo";
    default:                         return "Flex Account no responde; se reintentara";
  }
}

// GET autenticado. Rellena el perfil si el servidor lo devuelve. Nunca
// imprime la credencial. `detail` recibe un motivo legible en caso de fallo.
static FlexSessionVerdict validateSession(const char* bearer, char address[48], char displayName[64],
                                          char* detail, size_t detailCap){
  address[0] = 0; displayName[0] = 0; detail[0] = 0;
  if(WiFi.status() != WL_CONNECTED){ snprintf(detail, detailCap, "Sin Wi-Fi"); return FLEX_SESSION_UNAVAILABLE; }
  // SRAM INTERNA, no la memoria total (que suma la PSRAM): sin sitio para el
  // handshake no se intenta. Sale como "servicio no disponible" con el motivo y
  // se reintenta con la espera de siempre, en vez de un "-1" mudo o algo peor.
  size_t inFree = 0, inBlock = 0;
  if(!flexTlsRoom(&inFree, &inBlock)){
    snprintf(detail, detailCap, "Poca memoria interna (%u KB libres): se reintentara", (unsigned)(inFree / 1024u));
    Serial.printf("[ACCOUNT] validacion aplazada: SRAM interna %u KB (mayor bloque %u KB), hacen falta %u y %u KB\n",
                  (unsigned)(inFree / 1024u), (unsigned)(inBlock / 1024u),
                  (unsigned)(FLEX_TLS_MIN_INTERNAL / 1024u), (unsigned)(FLEX_TLS_MIN_BLOCK / 1024u));
    return FLEX_SESSION_UNAVAILABLE;
  }
  WiFiClientSecure secure; secureClient(secure);
  HTTPClient http; http.setTimeout(HTTP_TIMEOUT_MS); http.setConnectTimeout(HTTP_TIMEOUT_MS);   // sin esto la conexion TCP se corta a los 5 s del valor por defecto
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.useHTTP10(true);
  http.setUserAgent("FlexOS-Ultra/1.0 ESP32-P4");
  if(!http.begin(secure, FLEX_ACCOUNT_SESSION_URL)){
    snprintf(detail, detailCap, "No se pudo abrir la conexion segura");
    return FLEX_SESSION_UNAVAILABLE;
  }
  char auth[64];
  snprintf(auth, sizeof(auth), "Bearer %s", bearer);
  http.addHeader("Authorization", auth);
  http.addHeader("Accept", "application/json");
  http.addHeader("Connection", "close");
  memset(auth, 0, sizeof(auth));
  const uint32_t t0 = millis();
  int status = http.GET();
  const uint32_t tookMs = millis() - t0;
  // Por que no hubo conexion: el codigo de mbedTLS de esta misma conexion.
  int tlsErr = 0;
  if(status < 0){ char raw[64]; tlsErr = secure.lastError(raw, sizeof(raw)); }
  char code[40] = "";
  uint8_t* body = nullptr; size_t bodyLen = 0;
  if(status > 0) readHttpBody(http, &body, &bodyLen, false);
  http.end();
  cJSON* root = body ? cJSON_ParseWithLength((const char*)body, bodyLen) : nullptr;
  if(root && status != HTTP_CODE_OK){
    cJSON* err = cJSON_GetObjectItemCaseSensitive(root, "error");
    if(cJSON_IsObject(err)) copyJsonString(err, "code", code, sizeof(code), false);
  }
  FlexSessionVerdict v = flexAccountClassify(status, code);
  if(v == FLEX_SESSION_OK){
    // Cuerpo esperado: {"account":{"flexAddress":"...","displayName":"..."}}.
    // Un 200 sin cuerpo valido no es una validacion: se trata como servicio
    // no disponible (nunca como "desvinculado").
    cJSON* acc = root ? cJSON_GetObjectItemCaseSensitive(root, "account") : nullptr;
    if(!cJSON_IsObject(acc) || !copyJsonString(acc, "flexAddress", address, 48) || !validAddress(address)){
      address[0] = 0;
      v = FLEX_SESSION_UNAVAILABLE;
      snprintf(detail, detailCap, "Respuesta de Flex Account incompleta");
    } else copyJsonString(acc, "displayName", displayName, 64, false);
  } else if(v == FLEX_SESSION_UNAVAILABLE){
    if(status < 0){
      char why[56];
      if(tlsErr == -1){
        // -1 = DNS, TCP o saludo TLS: se distingue por el DNS y por lo que tardo (ver flexTlsPhase).
        char host[80]; bool dns = true;
        if(flexUrlHost(FLEX_ACCOUNT_SESSION_URL, host, sizeof(host))) dns = flexTlsDnsOk(host);
        flexTlsPhase(dns, tookMs, 12000u, HTTP_TIMEOUT_MS, why, sizeof(why));
      } else flexTlsReason(tlsErr, why, sizeof(why));
      snprintf(detail, detailCap, "Sin respuesta segura (%d: %s)", status, why);
      Serial.printf("[ACCOUNT] sin conexion TLS verificada: HTTP %d, %s, tardo %lu ms (mbedTLS %d), SRAM interna %u KB (bloque %u KB)\n",
                    status, why, (unsigned long)tookMs, tlsErr, (unsigned)(inFree / 1024u), (unsigned)(inBlock / 1024u));
    } else snprintf(detail, detailCap, "Flex Account respondio HTTP %d; se reintentara", status);
  }
  if(root) cJSON_Delete(root);
  free(body);
  if(!detail[0]) snprintf(detail, detailCap, "%s", verdictDetail(v));
  return v;
}

static void sessionTick(){
  lock();
  bool have = gHaveCredential && gBearer[0];
  bool busyFlow = gSnapshot.state == FLEX_ACCOUNT_REQUESTING || gSnapshot.state == FLEX_ACCOUNT_CODE_READY;
  FlexAccountLink link = gSnapshot.link;
  unlock();
  if(!have || busyFlow) return;

  const uint32_t now = millis();
  if(WiFi.status() != WL_CONNECTED){
    gWasOnline = false;
    // Sin red la cuenta SIGUE vinculada: solo se dice que no se puede
    // comprobar. Los estados que vienen del servidor (credencial rechazada
    // o caducada) se conservan: no son un problema de red.
    if(link == FLEX_LINK_LINKED || link == FLEX_LINK_NETWORK_UNAVAILABLE)
      setLink(FLEX_LINK_LINKED_OFFLINE, "Sin Wi-Fi: se comprobara al volver la conexion");
    return;
  }
  if(!gWasOnline){
    // Flanco: acaba de volver el Wi-Fi (o es el primero de este arranque).
    gWasOnline = true;
    gValidateWanted = true;
    // Lo que quede de la espera en curso (backoff tras un fallo de red) no se
    // acorta por volver a conectar; si no hay ninguna, se deja asentar la red.
    uint32_t left = 0;
    if(gFailures){ uint32_t el = now - gWaitFromMs; left = el < gWaitMs ? gWaitMs - el : 0; }
    if(left < SETTLE_MS) left = SETTLE_MS;
    if(gAttempted){
      uint32_t since = now - gLastAttemptMs;
      if(since < MIN_ATTEMPT_GAP_MS && MIN_ATTEMPT_GAP_MS - since > left) left = MIN_ATTEMPT_GAP_MS - since;
    }
    gWaitFromMs = now; gWaitMs = left;
  }
  if(link == FLEX_LINK_LINKED && gVerifiedThisBoot && !gValidateWanted && (uint32_t)(now - gLastVerifiedMs) >= REVALIDATE_MS){
    gValidateWanted = true;
    gWaitFromMs = now; gWaitMs = 0;
  }
  if(!gValidateWanted || (uint32_t)(now - gWaitFromMs) < gWaitMs) return;

  char bearer[48];
  const uint32_t epoch = gEpoch;                      // ANTES de copiar la credencial: si se desvincula despues, el resultado se descarta
  lock(); snprintf(bearer, sizeof(bearer), "%s", gBearer); unlock();
  gAttempted = true; gLastAttemptMs = now;
  char address[48], displayName[64], detail[96];
  FlexSessionVerdict v = validateSession(bearer, address, displayName, detail, sizeof(detail));
  memset(bearer, 0, sizeof(bearer));
  if(epoch != gEpoch) return;                         // se desvinculo mientras esperaba al servidor: el resultado ya no vale
  const uint32_t done = millis();
  if(v == FLEX_SESSION_OK){
    gValidateWanted = false; gFailures = 0;
    gLastVerifiedMs = done; gVerifiedThisBoot = true;
    persistAuthState(AUTHST_OK);
    bool changed = false;
    lock();
    changed = strcmp(address, gSnapshot.flexAddress) || strcmp(displayName, gSnapshot.displayName);
    if(changed){
      snprintf(gSnapshot.flexAddress, sizeof(gSnapshot.flexAddress), "%s", address);
      snprintf(gSnapshot.displayName, sizeof(gSnapshot.displayName), "%s", displayName);
    }
    unlock();
    if(changed) persistProfile(address, displayName);   // solo si el servidor cambio el perfil
    setLink(FLEX_LINK_LINKED, "Conectada con Flex Account");
    Serial.println(F("[ACCOUNT] sesion validada"));
  } else if(v == FLEX_SESSION_AUTH_REQUIRED || v == FLEX_SESSION_TOKEN_EXPIRED){
    gValidateWanted = false; gFailures = 0;
    persistAuthState(v == FLEX_SESSION_TOKEN_EXPIRED ? AUTHST_EXPIRED : AUTHST_REQUIRED);
    setLink(v == FLEX_SESSION_TOKEN_EXPIRED ? FLEX_LINK_TOKEN_EXPIRED : FLEX_LINK_AUTH_REQUIRED, detail);
    Serial.printf("[ACCOUNT] el servidor rechazo la credencial (%s)\n",
                  v == FLEX_SESSION_TOKEN_EXPIRED ? "caducada" : "revocada");
  } else {
    if(gFailures < 250) gFailures++;
    gWaitFromMs = done; gWaitMs = flexAccountBackoffMs(gFailures);
    // Si el servidor ya habia rechazado la credencial, un fallo de red no lo
    // "arregla": se conserva ese estado.
    if(link != FLEX_LINK_AUTH_REQUIRED && link != FLEX_LINK_TOKEN_EXPIRED)
      setLink(FLEX_LINK_NETWORK_UNAVAILABLE, detail);
    Serial.printf("[ACCOUNT] validacion aplazada (intento %u): %s\n", (unsigned)gFailures, detail);
  }
}

// ---------------------------------------------------------------------------
//  Flujo de enlace
// ---------------------------------------------------------------------------
// Un enlace nuevo que no termina bien NO toca la cuenta que ya estaba
// guardada: se vuelve a ella con el motivo del fallo a la vista.
static void finishFailedFlow(FlexAccountState state, const char* stage, const char* error){
  if(gHaveCredential){
    setStatus(FLEX_ACCOUNT_LINKED, 100, "Cuenta vinculada", nullptr);
    if(error){ lock(); snprintf(gSnapshot.error, sizeof(gSnapshot.error), "%s", error); unlock(); }
    return;
  }
  setStatus(state, 0, stage, error);
}

static void linkFlow(const char* label){
  setStatus(FLEX_ACCOUNT_REQUESTING, 8, "Creando enlace seguro");
  char bearer[48] = "", tokenHash[65] = "", code[9] = "", activationUrl[192] = "";
  if(!makeBearer(bearer, tokenHash)){
    finishFailedFlow(FLEX_ACCOUNT_ERROR, "No se pudo iniciar", "No se pudo crear la credencial segura"); return;
  }
  if(gCancelRequested){ finishFailedFlow(FLEX_ACCOUNT_CANCELLED, "Cancelado", nullptr); return; }
  if(WiFi.status() != WL_CONNECTED){
    finishFailedFlow(FLEX_ACCOUNT_ERROR, "Sin Wi-Fi", "Conecta el dispositivo a Wi-Fi y vuelve a intentar"); return;
  }
  setStatus(FLEX_ACCOUNT_REQUESTING, 28, "Contactando Flex Account");
  bool requested = requestDeviceCode(label, tokenHash, code, activationUrl);
  if(!requested && gLastHttpStatus == HTTP_CODE_CONFLICT){
    // La placa puede haber sido vinculada antes de que la credencial alcanzara
    // a guardarse (reinicio, reflasheo o corte de energia). Una nueva identidad
    // de instalacion permite recuperar el MISMO hardware sin reutilizar el
    // token perdido ni debilitar la autenticacion del servidor.
    setStatus(FLEX_ACCOUNT_REQUESTING, 34, "Recuperando vinculacion anterior");
    if(rotateInstallationId()) requested = requestDeviceCode(label, tokenHash, code, activationUrl);
    else snprintf(gRequestError, sizeof(gRequestError), "No se pudo guardar la nueva identidad del dispositivo");
  }
  if(!requested){
    if(gCancelRequested) finishFailedFlow(FLEX_ACCOUNT_CANCELLED, "Cancelado", nullptr);
    else finishFailedFlow(FLEX_ACCOUNT_ERROR, "Enlace no disponible",
                          gRequestError[0] ? gRequestError : "No se recibio una respuesta autentica de Flex Account");
    memset(bearer, 0, sizeof(bearer)); return;
  }
  lock();
  snprintf(gSnapshot.code, sizeof(gSnapshot.code), "%s", code);
  snprintf(gSnapshot.activationUrl, sizeof(gSnapshot.activationUrl), "%s", activationUrl);
  unlock();
  setStatus(FLEX_ACCOUNT_CODE_READY, 55, "Esperando aprobacion en tu celular");
  uint32_t started = millis(), lastPoll = 0;
  while(!gCancelRequested && millis() - started < LINK_TIMEOUT_MS){
    if(lastPoll && millis() - lastPoll < 3000){ vTaskDelay(pdMS_TO_TICKS(100)); continue; }
    lastPoll = millis();
    if(WiFi.status() != WL_CONNECTED){ setStatus(FLEX_ACCOUNT_CODE_READY, 55, "Esperando que vuelva el Wi-Fi", "Sin Wi-Fi. Se reintenta al volver la conexion"); continue; }
    char address[48] = "", displayName[64] = "", problem[96] = "";
    PollResult result = pollDeviceCode(code, tokenHash, address, displayName, problem, sizeof(problem));
    if(result == POLL_PENDING){
      // Si la consulta no se pudo contestar, el motivo va en `error` (la pantalla lo ensena junto al codigo).
      setStatus(FLEX_ACCOUNT_CODE_READY, 55, "Esperando aprobacion en tu celular", problem[0] ? problem : nullptr);
    } else if(result == POLL_EXPIRED){
      finishFailedFlow(FLEX_ACCOUNT_EXPIRED, "El codigo expiro", gHaveCredential ? "El codigo de vinculacion expiro" : nullptr);
      memset(bearer, 0, sizeof(bearer)); return;
    } else if(result == POLL_INVALID){
      finishFailedFlow(FLEX_ACCOUNT_ERROR, "Respuesta rechazada", "FlexOS rechazo un estado sin firma valida");
      memset(bearer, 0, sizeof(bearer)); return;
    } else {
      setStatus(FLEX_ACCOUNT_REQUESTING, 88, "Guardando tu Flex Account");
      if(!persistLinked(bearer, address, displayName)){
        finishFailedFlow(FLEX_ACCOUNT_ERROR, "No se pudo guardar", "El almacenamiento seguro no respondio");
        memset(bearer, 0, sizeof(bearer)); return;
      }
      lock();
      snprintf(gBearer, sizeof(gBearer), "%s", bearer);
      snprintf(gSnapshot.flexAddress, sizeof(gSnapshot.flexAddress), "%s", address);
      snprintf(gSnapshot.displayName, sizeof(gSnapshot.displayName), "%s", displayName);
      gHaveCredential = true;
      unlock();
      gAuthSt = AUTHST_OK;
      // El servidor ACABA de aprobar esta credencial: cuenta como validada.
      gLastVerifiedMs = millis(); gVerifiedThisBoot = true;
      gValidateWanted = false; gFailures = 0; gWasOnline = true;
      setLink(FLEX_LINK_LINKED, "Conectada con Flex Account");
      setStatus(FLEX_ACCOUNT_LINKED, 100, "Cuenta vinculada");
      Serial.println(F("[ACCOUNT] cuenta vinculada y guardada"));
      memset(bearer, 0, sizeof(bearer)); return;
    }
  }
  memset(bearer, 0, sizeof(bearer));
  if(gCancelRequested) finishFailedFlow(FLEX_ACCOUNT_CANCELLED, "Cancelado", nullptr);
  else finishFailedFlow(FLEX_ACCOUNT_EXPIRED, "El codigo expiro", gHaveCredential ? "El codigo de vinculacion expiro" : nullptr);
}

static void accountTaskStep(){
  if(gStartRequested){
    char label[49];
    lock(); snprintf(label, sizeof(label), "%s", gRequestedLabel); gStartRequested = false; unlock();
    linkFlow(label);
    return;
  }
  sessionTick();
}

static void accountTask(void*){
  for(;;){
    accountTaskStep();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

static void clearMemory(){
  lock();
  memset(gBearer, 0, sizeof(gBearer));
  memset(&gSnapshot, 0, sizeof(gSnapshot));
  gHaveCredential = false;
  gSnapshot.state = FLEX_ACCOUNT_UNLINKED;
  gSnapshot.link = FLEX_LINK_UNLINKED;
  snprintf(gSnapshot.stage, sizeof(gSnapshot.stage), "Sin cuenta vinculada");
  snprintf(gSnapshot.linkDetail, sizeof(gSnapshot.linkDetail), "Sin cuenta vinculada");
  unlock();
  gAuthSt = AUTHST_OK;
  gValidateWanted = false; gFailures = 0; gVerifiedThisBoot = false;
}

// Carga la cuenta guardada. Va SIEMPRE antes de mirar la red: el estado de la
// vinculacion es el de la NVS, no el de la ultima respuesta del servidor.
static void loadPersisted(){
  memset(&gSnapshot, 0, sizeof(gSnapshot));
  memset(gBearer, 0, sizeof(gBearer));
  gHaveCredential = false;
  gSnapshot.state = FLEX_ACCOUNT_UNLINKED;
  gSnapshot.link = FLEX_LINK_UNLINKED;
  snprintf(gSnapshot.stage, sizeof(gSnapshot.stage), "Sin cuenta vinculada");
  snprintf(gSnapshot.linkDetail, sizeof(gSnapshot.linkDetail), "Sin cuenta vinculada");

  Preferences preferences;
  if(!preferences.begin("flexacct", false)){
    gSnapshot.link = FLEX_LINK_ERROR;
    snprintf(gSnapshot.linkDetail, sizeof(gSnapshot.linkDetail), "El almacenamiento seguro no respondio");
    Serial.println(F("[ACCOUNT] NVS no disponible: no se pudo leer la cuenta"));
    return;
  }
  gInstallationId = preferences.getUInt("installId", 0);
  if(gInstallationId == 0){
    do { esp_fill_random(&gInstallationId, sizeof(gInstallationId)); } while(gInstallationId == 0);
    preferences.putUInt("installId", gInstallationId);
  }
  bool linked = preferences.getBool("linked", false);
  // EL FALLO QUE DESVINCULABA EN CADA ARRANQUE: getString(clave, char*, cap)
  // devuelve la longitud CON el terminador (44 para un token de 43), y aqui se
  // exigia tokenLen == 43. La cuenta se guardaba bien y se descartaba al leer.
  size_t tokenLen = flexNvsStrLen(preferences.getString("token", gBearer, sizeof(gBearer)), gBearer, sizeof(gBearer));
  size_t addressLen = flexNvsStrLen(preferences.getString("address", gSnapshot.flexAddress, sizeof(gSnapshot.flexAddress)),
                                    gSnapshot.flexAddress, sizeof(gSnapshot.flexAddress));
  if(!flexNvsStrLen(preferences.getString("display", gSnapshot.displayName, sizeof(gSnapshot.displayName)),
                    gSnapshot.displayName, sizeof(gSnapshot.displayName))) gSnapshot.displayName[0] = 0;
  gAuthSt = preferences.getUChar("authst", AUTHST_OK);
  bool anyKey = linked || preferences.isKey("token") || preferences.isKey("address");
  preferences.end();

  bool valid = linked && tokenLen == 43 && addressLen >= 6 && validAddress(gSnapshot.flexAddress);
  if(valid){
    gHaveCredential = true;
    gSnapshot.state = FLEX_ACCOUNT_LINKED; gSnapshot.linked = true; gSnapshot.progress = 100;
    snprintf(gSnapshot.stage, sizeof(gSnapshot.stage), "Cuenta vinculada");
    if(gAuthSt == AUTHST_EXPIRED){
      gSnapshot.link = FLEX_LINK_TOKEN_EXPIRED;
      snprintf(gSnapshot.linkDetail, sizeof(gSnapshot.linkDetail), "%s", verdictDetail(FLEX_SESSION_TOKEN_EXPIRED));
    } else if(gAuthSt == AUTHST_REQUIRED){
      gSnapshot.link = FLEX_LINK_AUTH_REQUIRED;
      snprintf(gSnapshot.linkDetail, sizeof(gSnapshot.linkDetail), "%s", verdictDetail(FLEX_SESSION_AUTH_REQUIRED));
    } else {
      gSnapshot.link = FLEX_LINK_LINKED_OFFLINE;
      snprintf(gSnapshot.linkDetail, sizeof(gSnapshot.linkDetail), "Vinculada; se comprobara al conectar el Wi-Fi");
    }
    Serial.println(F("[ACCOUNT] cuenta guardada recuperada"));
  } else {
    memset(gBearer, 0, sizeof(gBearer));
    gSnapshot.flexAddress[0] = 0; gSnapshot.displayName[0] = 0;
    if(anyKey){
      // Habia algo, pero incompleto (corte de energia a mitad de guardar, o
      // una version anterior danada): se dice, no se finge "sin cuenta".
      gSnapshot.link = FLEX_LINK_ERROR;
      snprintf(gSnapshot.linkDetail, sizeof(gSnapshot.linkDetail), "La cuenta guardada esta incompleta: vuelve a vincular");
      Serial.println(F("[ACCOUNT] registro de cuenta incompleto en NVS"));
    }
  }
}

} // namespace

// ---------------------------------------------------------------------------
//  Nucleo puro
// ---------------------------------------------------------------------------
FlexSessionVerdict flexAccountClassify(int httpStatus, const char* errorCode){
  if(httpStatus >= 200 && httpStatus < 300) return FLEX_SESSION_OK;
  if(httpStatus == 401 || httpStatus == 403){
    if(errorCode && !strcmp(errorCode, "token_expired")) return FLEX_SESSION_TOKEN_EXPIRED;
    // 401/403 sin un codigo reconocible del servicio puede venir de un proxy o
    // de una pagina de error del alojamiento: no se desvincula por eso.
    if(errorCode && (!strcmp(errorCode, "device_revoked") || !strcmp(errorCode, "auth_required") ||
                     !strcmp(errorCode, "invalid_token") || !strcmp(errorCode, "device_unknown")))
      return FLEX_SESSION_AUTH_REQUIRED;
    return FLEX_SESSION_UNAVAILABLE;
  }
  return FLEX_SESSION_UNAVAILABLE;
}

uint32_t flexAccountBackoffMs(uint8_t failures){
  if(failures == 0) return 0;
  uint32_t ms = 30000u;
  for(uint8_t i = 1; i < failures && ms < 15u * 60u * 1000u; i++) ms *= 2u;
  if(ms > 15u * 60u * 1000u) ms = 15u * 60u * 1000u;
  return ms;
}

size_t flexNvsStrLen(size_t nvsReturned, const char* buf, size_t cap){
  if(nvsReturned == 0 || !buf || cap == 0) return 0;   // 0 = no existe o no cabia: buf no es fiable
  size_t n = 0;
  while(n < cap && buf[n]) n++;
  if(n >= cap) return 0;                                // sin terminador: no se acepta
  // nvs_get_str cuenta el terminador; un valor coherente cumple n + 1 == devuelto.
  // Si una version futura del core devolviera strlen, n == devuelto tambien vale.
  if(n + 1 != nvsReturned && n != nvsReturned) return 0;
  return n;
}

// ---------------------------------------------------------------------------
//  API publica
// ---------------------------------------------------------------------------
// La tarea de Flex Account. Va al NUCLEO 1 como el resto de tareas de red (el 0
// es del presentador grafico) y su creacion SE COMPRUEBA: la pila (12 KB) sale
// de la SRAM interna y, con la memoria justa, xTaskCreate falla. Antes ese fallo
// pasaba en silencio y quedaba un modulo sin hilo: "Iniciar sesion" se quedaba
// en "Creando enlace seguro" para siempre y la cuenta nunca se validaba.
// Se llama al arrancar y otra vez al pedir un enlace, asi que un fallo del
// arranque se recupera sin reiniciar el aparato.
static bool ensureTask(){
  if(gTask) return true;
  BaseType_t rc = xTaskCreatePinnedToCore(accountTask, "flex-account", 12288, nullptr, 1, &gTask, 1);
  if(rc == pdPASS) return true;
  gTask = nullptr;
  Serial.printf("[ACCOUNT] no se pudo crear la tarea: SRAM interna %u KB, mayor bloque %u KB (la pila pide 12 KB seguidos)\n",
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024u),
                (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024u));
  return false;
}

void flexAccountBegin(){
  if(!gMutex) gMutex = xSemaphoreCreateMutex();
  lock();
  loadPersisted();
  unlock();
  gWasOnline = false; gValidateWanted = false; gFailures = 0;
  gVerifiedThisBoot = false; gLastVerifiedMs = 0; gWaitFromMs = 0; gWaitMs = 0; gAttempted = false;
  ensureTask();
}

bool flexAccountRequestCode(const char* deviceLabel){
  if(!gMutex) return false;
  // Sin tarea no hay quien haga el enlace: se reintenta crearla ahora y, si
  // tampoco hay memoria, se dice en pantalla (la cuenta guardada, si la hay, se
  // conserva) en vez de dejar "Creando enlace seguro" para siempre.
  if(!ensureTask()){
    finishFailedFlow(FLEX_ACCOUNT_ERROR, "No se pudo iniciar",
                     "Sin memoria interna para Flex Account. Cierra una app y reintenta");
    return false;
  }
  lock();
  bool busy = gStartRequested || gSnapshot.state == FLEX_ACCOUNT_REQUESTING || gSnapshot.state == FLEX_ACCOUNT_CODE_READY;
  if(!busy){
    snprintf(gRequestedLabel, sizeof(gRequestedLabel), "%s", (deviceLabel && deviceLabel[0]) ? deviceLabel : "FlexOS Ultra");
    gSnapshot.code[0] = 0; gSnapshot.activationUrl[0] = 0; gSnapshot.error[0] = 0;
    gSnapshot.state = FLEX_ACCOUNT_REQUESTING; gSnapshot.progress = 1;
    gSnapshot.linked = gHaveCredential;            // la cuenta guardada sigue ahi mientras tanto
    snprintf(gSnapshot.stage, sizeof(gSnapshot.stage), "Preparando enlace");
    gCancelRequested = false; gStartRequested = true;
  }
  unlock();
  return !busy;
}

void flexAccountCancel(){ gCancelRequested = true; }

bool flexAccountLinked(){
  if(!gMutex) return false;
  lock(); bool result = gHaveCredential; unlock(); return result;
}

bool flexAccountUsable(){
  if(!gMutex) return false;
  lock();
  bool result = gHaveCredential && gSnapshot.link != FLEX_LINK_AUTH_REQUIRED && gSnapshot.link != FLEX_LINK_TOKEN_EXPIRED;
  unlock();
  return result;
}

FlexAccountState flexAccountState(){
  if(!gMutex) return FLEX_ACCOUNT_UNLINKED;
  lock(); FlexAccountState result = gSnapshot.state; unlock(); return result;
}

FlexAccountLink flexAccountLinkState(){
  if(!gMutex) return FLEX_LINK_UNLINKED;
  lock(); FlexAccountLink result = gSnapshot.link; unlock(); return result;
}

void flexAccountSnapshot(FlexAccountSnapshot* out){
  if(!out) return;
  if(!gMutex){ memset(out, 0, sizeof(*out)); out->state = FLEX_ACCOUNT_UNLINKED; out->link = FLEX_LINK_UNLINKED; return; }
  lock();
  memcpy(out, &gSnapshot, sizeof(*out));
  out->linked = gHaveCredential;
  out->verifiedAgeS = gVerifiedThisBoot ? (uint32_t)(millis() - gLastVerifiedMs) / 1000u : 0xFFFFFFFFu;
  unlock();
}

const char* flexAccountLinkLabel(FlexAccountLink link){
  switch(link){
    case FLEX_LINK_LINKED:              return "Conectada";
    case FLEX_LINK_LINKED_OFFLINE:      return "Vinculada \xC2\xB7 sin conexi\xC3\xB3n";
    case FLEX_LINK_NETWORK_UNAVAILABLE: return "Vinculada \xC2\xB7 servicio no disponible";
    case FLEX_LINK_AUTH_REQUIRED:       return "Vuelve a iniciar sesi\xC3\xB3n";
    case FLEX_LINK_TOKEN_EXPIRED:       return "Sesi\xC3\xB3n caducada";
    case FLEX_LINK_ERROR:               return "No se pudo leer la cuenta";
    default:                            return "Sin cuenta vinculada";
  }
}

void flexAccountRequestValidation(){
  gWaitMs = 0;
  gWaitFromMs = millis();
  gValidateWanted = true;
  if(gMutex) ensureTask();           // sin tarea nadie validaria: si no se pudo crear al arrancar, se reintenta aqui
}

void flexAccountReportRejected(){
  uint32_t now = millis();
  if(gRejectSeen && (uint32_t)(now - gLastRejectMs) < REJECT_RATE_MS) return;
  gRejectSeen = true; gLastRejectMs = now;
  flexAccountRequestValidation();
}

bool flexAccountCopyBearer(char* out, size_t capacity){
  if(!out || capacity == 0 || !gMutex) return false;
  lock();
  bool ok = gHaveCredential && gBearer[0] && strlen(gBearer) + 1 <= capacity;
  if(ok) memcpy(out, gBearer, strlen(gBearer) + 1); else out[0] = 0;
  unlock();
  return ok;
}

// DESVINCULAR ESTE APARATO (accion explicita del usuario, o restablecer de
// fabrica). Deja la cuenta en FLEX_ACCOUNT_UNLINKED / FLEX_LINK_UNLINKED: sin
// credencial en la NVS ni en RAM, y con un id de instalacion nuevo para que el
// servidor no reconozca a este aparato como el anterior. Se escribe en la NVS
// UNA vez y solo si hay algo que borrar: llamarla sin cuenta no toca la flash.
void flexAccountForgetLocal(){
  gCancelRequested = true;                          // un enlace en curso se cancela
  gEpoch = gEpoch + 1;                              // y una validacion en vuelo no escribira nada despues
  lock(); bool had = gHaveCredential; unlock();
  Preferences preferences;
  if(preferences.begin("flexacct", false)){
    bool any = had || preferences.isKey("linked") || preferences.isKey("token") || preferences.isKey("address") ||
               preferences.isKey("display") || preferences.isKey("authst");
    if(any){
      preferences.clear();
      preferences.end();
      rotateInstallationId();
      Serial.println(F("[ACCOUNT] cuenta desvinculada de este aparato"));
    } else preferences.end();
  }
  clearMemory();
}

#ifdef FLEXOS_HOST_TEST
void flexAccountTestSetKey(const uint8_t pub[65]){ memcpy(gTestKey, pub, 65); gTestKeySet = true; }
void flexAccountTestStep(){ accountTaskStep(); }
void flexAccountTestPowerCycle(){
  // Se pierde TODO lo de RAM (la NVS es de la prueba y sobrevive).
  lock();
  memset(&gSnapshot, 0, sizeof(gSnapshot));
  memset(gBearer, 0, sizeof(gBearer));
  gHaveCredential = false;
  unlock();
  gStartRequested = false; gCancelRequested = false;
  gWasOnline = false; gValidateWanted = false; gWaitFromMs = 0; gWaitMs = 0; gFailures = 0;
  gLastVerifiedMs = 0; gVerifiedThisBoot = false; gRejectSeen = false; gAuthSt = AUTHST_OK;
  gInstallationId = 0; gAttempted = false; gLastAttemptMs = 0;
  gTask = nullptr;                                  // las tareas se pierden con la RAM
  flexAccountBegin();
}
#endif

#if FXA_BACKEND_OPENSSL
#  pragma GCC diagnostic pop
#endif
