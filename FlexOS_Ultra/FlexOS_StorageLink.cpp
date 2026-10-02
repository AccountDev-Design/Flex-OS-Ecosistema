// #############################################################
//  FLEX STORAGE · ENLACE CON EL TELEFONO · implementacion
//  Ver FlexOS_StorageLink.h y docs/FLEX-STORAGE.md (secciones 4 a 7).
// #############################################################
#include "FlexOS_StorageLink.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <string.h>
#include <time.h>

#include "FlexOS_Cloud.h"
#include "FlexOS_CloudCore.h"
#include "FlexOS_HttpSink.h"
#include "FlexOS_OTA.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

const char* const NVS_NS    = "flexstor";
const char* const NVS_PHONE = "phone";
const uint32_t HS_CONNECT_MS   = 4000;              // red local: si no conecta en 4 s, no esta
const uint32_t HS_TIMEOUT_MS   = 6000;
const uint32_t HS_WAIT_MAX_MS  = 15000;             // otra tarea abriendo la sesion: como mucho esto
const uint32_t TOKEN_MARGIN_S  = 60;                // se renueva un minuto antes de que caduque
const uint32_t TOKEN_MAX_AGE_MS = 11u * 3600u * 1000u;   // el telefono las corta a las 12 h
const uint32_t FRESH_TOKEN_MS  = 10000;             // un token recien abierto que se rechaza es sospechoso

// gMx protege TODO lo de abajo salvo gState (publicado) y gSnap (su propio cerrojo).
SemaphoreHandle_t gMx = nullptr;
SemaphoreHandle_t gSnapMx = nullptr;
FstCore gCore;
volatile uint8_t gState = FSP_NONE;
uint32_t gGen = 1;
uint32_t gPhoneGen = 1;                              // cambia con cualquier cambio del telefono emparejado
FlexStorageInfo gSnap;

// Sesion con el telefono: SOLO en RAM (un reinicio abre otra).
char     gToken[FST_TOKEN_MAX] = "";
uint32_t gTokenAtMs = 0, gTokenUseMs = 0, gTokenTtlS = 0;
bool     gHandshaking = false;
uint8_t  gHsFails = 0;                               // aperturas fallidas seguidas
uint32_t gHsRetryAt = 0;                             // 0 = sin espera
uint8_t  gRejects = 0;                               // tokens recien abiertos que el telefono rechazo
uint8_t  g401s = 0;                                  // aperturas rechazadas con 401 (la clave no cuadra)
bool     gRejected = false;                          // el telefono ya no reconoce este Flex OS
uint8_t  gReach = 0;
uint32_t gLastOkMs = 0;
bool     gEverOk = false;
char     gHsBuf[1024];                               // respuestas del saludo (de uno en uno: gHandshaking)

void lockMx(){ if(gMx) xSemaphoreTake(gMx, portMAX_DELAY); }
void unlockMx(){ if(gMx) xSemaphoreGive(gMx); }

void rnd(void*, uint8_t* out, size_t n){ esp_fill_random(out, n); }

uint32_t epochNow(){
  time_t t = time(nullptr);
  return t > (time_t)1700000000 ? (uint32_t)t : 0u;
}

// ---- estado (todas con gMx tomado) -------------------------------------
void publish(){
  uint8_t s = !gCore.phone.valid ? FSP_NONE : !gCore.phone.enabled ? FSP_OFF : gRejected ? FSP_REJECTED : FSP_READY;
  if(s != gState){ gState = s; gGen++; }
}

void dropSession(){
  memset(gToken, 0, sizeof(gToken));
  gTokenAtMs = gTokenUseMs = 0; gTokenTtlS = 0;
}

void resetLink(){
  dropSession();
  gRejected = false; gHsFails = 0; gHsRetryAt = 0; gRejects = 0; g401s = 0;
  gReach = 0; gLastOkMs = 0; gEverOk = false;
}

void backoff(){
  if(gHsFails < 250) gHsFails++;
  gHsRetryAt = millis() + fclBackoffMs(gHsFails);
  if(!gHsRetryAt) gHsRetryAt = 1;
}

void setReach(bool ok){
  uint8_t r = ok ? 1 : 0;
  if(ok){ gLastOkMs = millis(); gEverOk = true; }
  if(r != gReach){ gReach = r; gGen++; }
}

// La copia que lee la interfaz. Con gMx tomado; gSnap tiene su propio cerrojo
// para que leerla no tenga que esperar a un ECDH.
void refreshSnap(uint32_t now){
  FlexStorageInfo o; memset(&o, 0, sizeof(o));
  bool wasPending = gCore.pair.state == FSTP_PENDING;
  bool waiting = fstPairWaiting(&gCore, now);         // cancela el que caduco
  if(wasPending && !waiting) gGen++;
  o.state = gState;
  if(gCore.phone.valid){
    memcpy(o.name, gCore.phone.name, sizeof(o.name));
    memcpy(o.model, gCore.phone.model, sizeof(o.model));
    memcpy(o.ip, gCore.phone.ip, sizeof(o.ip));
    o.port = gCore.phone.port;
  }
  o.reachable = gReach;
  o.lastOkAgeS = gEverOk ? (now - gLastOkMs) / 1000u : 0xFFFFFFFFu;
  if(waiting){
    o.pairWaiting = 1;
    memcpy(o.pairName, gCore.pair.name, sizeof(o.pairName));
    memcpy(o.pairModel, gCore.pair.model, sizeof(o.pairModel));
    memcpy(o.pairIp, gCore.pair.ip, sizeof(o.pairIp));
    memcpy(o.sas, gCore.pair.sas, sizeof(o.sas));
    uint32_t used = now - gCore.pair.startMs;
    uint32_t left = used < FST_PAIR_TTL_MS ? (FST_PAIR_TTL_MS - used + 999u) / 1000u : 0u;
    o.pairLeftS = (uint8_t)(left > 255u ? 255u : left);
  }
  o.gen = gGen;
  if(gSnapMx) xSemaphoreTake(gSnapMx, portMAX_DELAY);
  gSnap = o;
  if(gSnapMx) xSemaphoreGive(gSnapMx);
}

// ---- NVS ------------------------------------------------------------------
bool nvsSave(const FstPhone* p){
  uint8_t buf[FST_PHONE_BLOB_MAX];
  size_t n = fstPhoneEncode(p, buf, sizeof(buf));
  bool ok = false;
  if(n){
    Preferences pr;
    if(pr.begin(NVS_NS, false)){ ok = pr.putBytes(NVS_PHONE, buf, n) == n; pr.end(); }
  }
  memset(buf, 0, sizeof(buf));                        // lleva la clave
  return ok;
}

void nvsLoad(FstPhone* p){
  fstPhoneWipe(p);
  uint8_t buf[FST_PHONE_BLOB_MAX];
  // Lectura-escritura a proposito: abrir en solo lectura un espacio que aun no
  // existe falla con un error en el registro en cada arranque.
  Preferences pr;
  if(!pr.begin(NVS_NS, false)) return;
  size_t n = pr.getBytesLength(NVS_PHONE);
  if(n && n <= sizeof(buf) && pr.getBytes(NVS_PHONE, buf, sizeof(buf)) == n && !fstPhoneDecode(p, buf, n))
    Serial.println(F("[STORAGE] el registro del telefono estaba danado: se descarta"));
  pr.end();
  memset(buf, 0, sizeof(buf));
}

bool nvsErase(){
  Preferences pr;
  if(!pr.begin(NVS_NS, false)) return false;
  bool ok = !pr.isKey(NVS_PHONE) || pr.remove(NVS_PHONE);
  pr.end();
  return ok;
}

// ---- JSON para el servidor web ------------------------------------------
void errJson(char* json, size_t cap, const char* msg){
  char esc[2 * 160];
  if(!fclJsonEscape(msg && *msg ? msg : "Error", esc, sizeof(esc))) snprintf(esc, sizeof(esc), "Error");
  snprintf(json, cap, "{\"error\":\"%s\"}", esc);
}

// ---- saludo de la sesion --------------------------------------------------
// Una peticion corta al telefono. Estado HTTP (<0 = no contesto) y el cuerpo,
// ACOTADO, en gHsBuf. Una respuesta mas grande de lo que cabe no se acepta.
int hsCall(const char* method, const char* url, const char* body, size_t* outLen){
  *outLen = 0; gHsBuf[0] = 0;
  WiFiClient cli;
  HTTPClient h;
  h.setReuse(false);
  h.setConnectTimeout(HS_CONNECT_MS);
  h.setTimeout(HS_TIMEOUT_MS);
  h.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  h.useHTTP10(true);
  if(!h.begin(cli, url)) return HTTPC_ERROR_CONNECTION_REFUSED;
  h.addHeader("Accept", "application/json");
  h.addHeader("Accept-Encoding", "identity");
  h.setUserAgent("FlexOS-Ultra/" FLEXOS_FW_VERSION " ESP32-P4 FlexStorage");
  int st;
  if(body){ h.addHeader("Content-Type", "application/json"); st = h.sendRequest(method, (uint8_t*)body, strlen(body)); }
  else st = h.sendRequest(method);
  if(st > 0){
    FlexBufSink sink(gHsBuf, sizeof(gHsBuf));
    int w = h.writeToStream(&sink);
    sink.terminate();
    *outLen = sink.len();
    if((sink.overflow() || w < 0) && st >= 200 && st < 300) st = HTTPC_ERROR_TOO_LESS_RAM;
  }
  h.end();
  cli.stop();
  return st;
}

} // namespace

// ======================================================================
//  Arranque y estado
// ======================================================================
void flexStorageBegin(){
  if(!gMx) gMx = xSemaphoreCreateMutex();
  if(!gSnapMx) gSnapMx = xSemaphoreCreateMutex();
  uint64_t mac = ESP.getEfuseMac();
  char id[FST_ID_MAX], name[FST_NAME_MAX];
  snprintf(id, sizeof(id), "flexos-%04lx%08lx", (unsigned long)((mac >> 32) & 0xFFFFu), (unsigned long)(mac & 0xFFFFFFFFu));
  snprintf(name, sizeof(name), "Flex OS Ultra (%04lX)", (unsigned long)(mac & 0xFFFFu));
  lockMx();
  fstInit(&gCore, id, name);
  nvsLoad(&gCore.phone);
  resetLink();
  gPhoneGen++;
  publish();
  gGen++;
  refreshSnap(millis());
  bool usePhone = gState == FSP_READY;
  char pn[FST_NAME_MAX]; memcpy(pn, gCore.phone.name, sizeof(pn));
  char pip[FST_IP_MAX]; memcpy(pip, gCore.phone.ip, sizeof(pip));
  bool valid = gCore.phone.valid, enabled = gCore.phone.enabled;
  unlockMx();
  // El destino se fija ANTES de flexCloudBegin(): decide que diario se carga.
  flexCloudSetDest(usePhone ? FCD_PHONE : FCD_INTERNET);
  if(valid) Serial.printf("[STORAGE] telefono emparejado: %s (%s)%s\n", pn, pip, enabled ? "" : " - desconectado por el usuario");
}

void flexStorageInfo(FlexStorageInfo* out){
  if(!out) return;
  if(!gMx || !gSnapMx){ memset(out, 0, sizeof(*out)); return; }
  // Si el cerrojo esta ocupado (un emparejamiento calculando su ECDH, unas
  // decenas de ms), la interfaz pinta la copia anterior en vez de esperar.
  if(xSemaphoreTake(gMx, 0) == pdTRUE){ refreshSnap(millis()); xSemaphoreGive(gMx); }
  xSemaphoreTake(gSnapMx, portMAX_DELAY);
  *out = gSnap;
  xSemaphoreGive(gSnapMx);
}

const char* flexStorageP4Id(){ return gCore.p4Id; }

// ======================================================================
//  Servidor web
// ======================================================================
bool flexStorageOffer(char out[FST_HEX32]){
  if(!gMx || !out) return false;
  lockMx();
  bool ok = fstOfferNew(&gCore, millis(), rnd, nullptr, out);
  unlockMx();
  return ok;
}

int flexStoragePairBegin(const FstPairReq* req, const char* peerIp, char* json, size_t cap, uint32_t* retryS){
  if(retryS) *retryS = 0;
  if(!json || cap < 400) return 500;
  if(!gMx){ errJson(json, cap, "Flex Storage a\xC3\xBAn no est\xC3\xA1 listo"); return 503; }
  FstPairResp r; memset(&r, 0, sizeof(r));
  char err[160] = "", p4id[FST_ID_MAX], p4name[FST_NAME_MAX];
  uint32_t waitS = 0;
  lockMx();
  int st = fstPairBegin(&gCore, millis(), req, peerIp, rnd, nullptr, &r, err, sizeof(err), &waitS);
  memcpy(p4id, gCore.p4Id, sizeof(p4id));
  memcpy(p4name, gCore.p4Name, sizeof(p4name));
  if(st == 202) gGen++;
  refreshSnap(millis());
  unlockMx();
  if(st != 202){
    if(retryS) *retryS = waitS;
    errJson(json, cap, err);
    return st;
  }
  char en[2 * FST_NAME_MAX];
  if(!fclJsonEscape(p4name, en, sizeof(en))) snprintf(en, sizeof(en), "Flex OS");
  snprintf(json, cap, "{\"ok\":1,\"pairId\":\"%s\",\"pub\":\"%s\",\"p4id\":\"%s\",\"p4name\":\"%s\",\"approve\":%u,\"expiresIn\":%lu}",
           r.pairId, r.pub, p4id, en, (unsigned)r.approve, (unsigned long)r.expiresS);
  Serial.printf("[STORAGE] emparejamiento pedido desde %s%s\n", peerIp ? peerIp : "?",
                r.approve ? ": esperando la aprobacion en pantalla" : " (telefono ya emparejado)");
  return 202;
}

int flexStoragePairPoll(const char* pairId, const char* proofHex, char* json, size_t cap){
  if(!json || cap < 160) return 500;
  if(!gMx){ errJson(json, cap, "Flex Storage a\xC3\xBAn no est\xC3\xA1 listo"); return 503; }
  uint8_t state = 0; bool persist = false;
  char proof[65] = "", err[160] = "";
  FstPhone old, rec;
  lockMx();
  old = gCore.phone;
  int st = fstPairPoll(&gCore, millis(), pairId, proofHex, &state, proof, &persist, err, sizeof(err));
  if(persist){
    gCore.phone.pairedEpoch = epochNow();
    rec = gCore.phone;
    resetLink();
    gPhoneGen++;
    publish();
  }
  if(st != 200 || persist) gGen++;
  refreshSnap(millis());
  unlockMx();
  if(persist){
    // A la NVS ANTES de contestar "aprobado": si no se puede guardar, el
    // telefono no debe creer que quedo emparejado (se deshace todo).
    if(!nvsSave(&rec)){
      lockMx();
      gCore.phone = old;
      fstPairCancel(&gCore);
      resetLink();
      gPhoneGen++;
      publish(); gGen++;
      refreshSnap(millis());
      unlockMx();
      fstPhoneWipe(&rec); fstPhoneWipe(&old);
      Serial.println(F("[STORAGE] no se pudo guardar el emparejamiento en la NVS"));
      errJson(json, cap, "Flex OS no pudo guardar el emparejamiento. Vuelve a intentarlo.");
      return 500;
    }
    bool other = old.valid && strcmp(old.id, rec.id) != 0;
    Serial.printf("[STORAGE] telefono emparejado: %s (%s:%u)\n", rec.name, rec.ip, (unsigned)rec.port);
    fstPhoneWipe(&rec); fstPhoneWipe(&old);
    // Otro telefono distinto del que habia: lo que quedaba para el anterior
    // no puede terminar. El MISMO telefono (nueva IP o nueva clave) lo conserva.
    if(other) flexCloudPhoneForgotten();
    flexCloudSetDest(FCD_PHONE);
  } else {
    fstPhoneWipe(&old);
  }
  if(st == 200 && state == FSTP_DONE) snprintf(json, cap, "{\"ok\":1,\"state\":\"approved\",\"proof\":\"%s\"}", proof);
  else if(st == 200) snprintf(json, cap, "{\"ok\":1,\"state\":\"pending\"}");
  else errJson(json, cap, err);
  return st;
}

// ======================================================================
//  Interfaz
// ======================================================================
bool flexStoragePairDecide(bool allow){
  if(!gMx) return false;
  lockMx();
  bool ok = fstPairDecide(&gCore, allow);
  if(ok) gGen++;
  refreshSnap(millis());
  unlockMx();
  if(ok) Serial.printf("[STORAGE] emparejamiento %s en pantalla\n", allow ? "aprobado" : "rechazado");
  return ok;
}

void flexStoragePairCancel(){
  if(!gMx) return;
  lockMx();
  bool had = gCore.pair.state != FSTP_NONE;
  fstPairCancel(&gCore);
  if(had) gGen++;
  refreshSnap(millis());
  unlockMx();
}

bool flexStorageSetEnabled(bool on){
  if(!gMx) return false;
  lockMx();
  if(!gCore.phone.valid){ unlockMx(); return false; }
  bool changed = (gCore.phone.enabled != 0) != on;
  gCore.phone.enabled = on ? 1 : 0;
  // Volver a conectar tambien es "reintentar ahora": sin esperas ni rechazos de antes.
  resetLink();
  gPhoneGen++;
  publish(); gGen++;
  refreshSnap(millis());
  FstPhone rec = gCore.phone;
  unlockMx();
  bool ok = !changed || nvsSave(&rec);
  fstPhoneWipe(&rec);
  flexCloudSetDest(on ? FCD_PHONE : FCD_INTERNET);
  return ok;
}

void flexStorageForget(){
  if(!gMx) return;
  lockMx();
  bool had = gCore.phone.valid;
  fstPhoneWipe(&gCore.phone);
  fstPairCancel(&gCore);
  resetLink();
  gPhoneGen++;
  publish(); gGen++;
  refreshSnap(millis());
  unlockMx();
  if(!nvsErase()) Serial.println(F("[STORAGE] no se pudo borrar el telefono de la NVS"));
  if(had){
    Serial.println(F("[STORAGE] telefono olvidado"));
    flexCloudPhoneForgotten();
    flexCloudSetDest(FCD_INTERNET);
  }
}

// ======================================================================
//  Flex Cloud
// ======================================================================
bool flexStoragePhoneUsable(){ return gState == FSP_READY; }
uint8_t flexStoragePhoneState(){ return gState; }

bool flexStoragePhoneBase(char* out, size_t cap){
  if(!gMx || !out || !cap) return false;
  lockMx();
  bool ok = gState == FSP_READY && fstPhoneBaseUrl(&gCore.phone, out, cap);
  unlockMx();
  return ok;
}

bool flexStorageCopyBearer(char* out, size_t cap, char* why, size_t whyCap){
  auto say = [&](const char* w){ if(why && whyCap) snprintf(why, whyCap, "%s", w); };
  if(out && cap) out[0] = 0;
  if(!gMx || !out || cap < FST_TOKEN_MAX){ say("no_phone"); return false; }
  uint32_t waited = 0;
  for(;;){
    lockMx();
    if(gState != FSP_READY){ uint8_t s = gState; unlockMx(); say(s == FSP_REJECTED ? "phone_rejected" : "no_phone"); return false; }
    uint32_t now = millis();
    if(gToken[0]){
      bool idle = gTokenTtlS > TOKEN_MARGIN_S && (now - gTokenUseMs) >= (gTokenTtlS - TOKEN_MARGIN_S) * 1000u;
      bool old = (now - gTokenAtMs) >= TOKEN_MAX_AGE_MS;
      if(!idle && !old){ memcpy(out, gToken, FST_TOKEN_MAX); gTokenUseMs = now; unlockMx(); return true; }
      dropSession();                                   // caducada o a punto: se abre otra
    }
    if(!gHandshaking) break;
    unlockMx();
    // Otra tarea de la nube esta abriendo la sesion: se espera a la suya.
    if(waited >= HS_WAIT_MAX_MS){ say("network"); return false; }
    vTaskDelay(pdMS_TO_TICKS(20)); waited += 20;
  }
  // --- con el cerrojo ---
  if(gHsRetryAt && (int32_t)(millis() - gHsRetryAt) < 0){ unlockMx(); say("network"); return false; }
  gHandshaking = true;
  const uint32_t gen0 = gPhoneGen;
  char base[48];
  snprintf(base, sizeof(base), "http://%s:%u", gCore.phone.ip, (unsigned)gCore.phone.port);
  unlockMx();

  // 1) Reto (sin cerrojo: es red).
  char url[96], nonce[FST_HEX32] = "";
  size_t n = 0;
  snprintf(url, sizeof(url), "%s/api/fs/challenge", base);
  int st = hsCall("GET", url, nullptr, &n);
  bool gotNonce = st == 200 && fstParseChallenge(gHsBuf, n, nonce);

  // 2) Cuerpo de la sesion: HMAC con la clave, que no sale del cerrojo.
  char body[256]; size_t bl = 0;
  if(gotNonce){
    lockMx();
    if(gPhoneGen == gen0) bl = fstSessionBody(&gCore, nonce, body, sizeof(body));
    unlockMx();
  }

  // 3) Sesion (red).
  int st2 = 0;
  if(bl){
    snprintf(url, sizeof(url), "%s/api/fs/session", base);
    st2 = hsCall("POST", url, body, &n);
  }
  memset(body, 0, sizeof(body));

  // 4) Resultado.
  lockMx();
  gHandshaking = false;
  bool ok = false;
  const char* res = "network";
  if(gPhoneGen != gen0){
    res = "no_phone";                                  // el telefono cambio mientras tanto: no vale nada de esto
  } else if(!gotNonce){
    setReach(st > 0);
    backoff();
    if(st > 0) Serial.printf("[STORAGE] el telefono contesto %d al pedir el reto\n", st);
  } else if(st2 == 200){
    char token[FST_TOKEN_MAX]; uint32_t ttl = 0;
    char nm[FST_NAME_MAX];
    if(fstParseSession(gHsBuf, n, gCore.phone.key, nonce, token, &ttl, nm, sizeof(nm))){
      memcpy(gToken, token, sizeof(gToken));
      memset(token, 0, sizeof(token));
      uint32_t now = millis();
      gTokenAtMs = gTokenUseMs = now; gTokenTtlS = ttl;
      gHsFails = 0; gHsRetryAt = 0; g401s = 0;
      setReach(true);
      memcpy(out, gToken, FST_TOKEN_MAX);
      ok = true;
    } else {
      // Contesto alguien que NO demostro tener la clave: no se le manda nada.
      Serial.println(F("[STORAGE] la respuesta de la sesion no demuestra la clave del telefono: se ignora"));
      setReach(true);
      backoff();
    }
  } else if(st2 == 403){
    // El telefono no conoce este Flex OS (lo olvido o se emparejo con otro):
    // no se insiste. Se sale emparejando de nuevo (o "Volver a conectar").
    gRejected = true; dropSession();
    setReach(true);
    publish(); gGen++;
    res = "phone_rejected";
    Serial.println(F("[STORAGE] el telefono ya no reconoce este Flex OS"));
  } else if(st2 == 401){
    // La clave no cuadra (o el reto caduco por el camino). Dos seguidas: rechazado.
    setReach(true);
    if(++g401s >= 2){ gRejected = true; publish(); gGen++; res = "phone_rejected"; }
    else backoff();
  } else {
    setReach(st2 > 0);
    backoff();
  }
  refreshSnap(millis());
  unlockMx();
  if(!ok) say(res);
  return ok;
}

void flexStorageSessionRejected(){
  if(!gMx) return;
  lockMx();
  uint32_t now = millis();
  bool fresh = gToken[0] && (now - gTokenAtMs) < FRESH_TOKEN_MS;
  dropSession();
  // Un token recien abierto que se rechaza otra vez y otra: algo no cuadra
  // (otra IP, el telefono reiniciando sus sesiones...). Sin bucle: espera.
  if(fresh && ++gRejects >= 3){ gRejects = 0; backoff(); }
  unlockMx();
}

void flexStorageNoteResult(int httpStatus){
  if(!gMx) return;
  lockMx();
  setReach(httpStatus > 0);
  if(httpStatus >= 200 && httpStatus < 300) gRejects = 0;
  unlockMx();
}

#ifdef FLEXOS_HOST_TEST
void flexStorageTestReset(){
  lockMx();
  fstPhoneWipe(&gCore.phone);
  memset(&gCore, 0, sizeof(gCore));
  resetLink();
  gHandshaking = false;
  gState = FSP_NONE;
  gGen++; gPhoneGen++;
  unlockMx();
}
#endif
