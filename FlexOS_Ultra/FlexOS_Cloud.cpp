// #############################################################
//  FLEX CLOUD MANAGER · implementacion (ver FlexOS_Cloud.h)
// #############################################################
#include "FlexOS_Cloud.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <string.h>

#include "FlexOS_Account.h"
#include "FlexOS_CloudTLS.h"
#include "FlexOS_FS.h"
#include "FlexOS_HttpSink.h"
#include "FlexOS_JPEG.h"
#include "FlexOS_MediaLib.h"
#include "FlexOS_OTA.h"
#include "FlexOS_StorageLink.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

// ---------------------------------------------------------------- constantes
static const char*    CLOUD_DIR    = "/System/Cloud";
static const char*    JOURNAL_PATH = "/System/Cloud/jobs.bin";
static const char*    JOURNAL_PHONE = "/System/Cloud/phone.bin";  // destino telefono (Flex Storage)
static const char*    DL_DIR       = "/System/Cloud/dl";
static const char*    DL_PHONE     = "/System/Cloud/pdl";       // descargas del telefono a medias
static const uint32_t PHONE_IDS    = 0x40000000u;  // numeros de trabajo del telefono: nunca los de Internet
static const char*    VIEW_DIR     = "/System/Cloud/view";   // OBSOLETO: la foto ya no pasa por la flash (solo se limpia lo que dejo un firmware anterior)
static const size_t   JSON_CAP     = 48u * 1024u;       // pagina de 40 elementos con holgura
static const size_t   IO_CAP       = 16u * 1024u;       // lectura/escritura de bytes, reutilizado
static const int      PAGE         = 40;
static const uint32_t HTTP_TIMEOUT = 15000;
static const uint32_t ME_EVERY_MS  = 60000;             // cuota al dia mientras la nube esta a la vista
static const uint32_t ME_PHONE_MS  = 15000;             // con el telefono, ademas, dice si SU lista cambio (rev): una consulta diminuta cada 15 s, solo a la vista
static const uint64_t LOCAL_RESERVE = 512u * 1024u;     // = FML_RESERVE_BYTES: nunca se llena LittleFS
static const uint32_t VIEW_MAX     = 6u * 1024u * 1024u;// foto para el visor (el limite del visor: se trae a la RAM, no a la flash)
static const uint32_t THUMB_MAX    = 96u * 1024u;       // miniatura remota que se acepta
static const int      EVENTS       = 12;
static const int      CMDS         = 10;

// --------------------------------------------------------------- memoria
// Todo lo grande va a PSRAM y se reserva UNA vez: nada por cuadro ni por paquete.
static void* psAlloc(size_t n){
  void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!p) p = malloc(n);
  return p;
}
static void psFree(void* p){ if(p) heap_caps_free(p); }

// ----------------------------------------------------------------- estado
static SemaphoreHandle_t gLock = nullptr;     // estado publico, cola, eventos, diario, miniaturas
static SemaphoreHandle_t gStLock = nullptr;   // cache del streaming (la lee el reproductor)
static TaskHandle_t gTask = nullptr, gStTask = nullptr;
static void lock(){ if(gLock) xSemaphoreTake(gLock, portMAX_DELAY); }
static void unlock(){ if(gLock) xSemaphoreGive(gLock); }
static void stLock(){ if(gStLock) xSemaphoreTake(gStLock, portMAX_DELAY); }
static void stUnlock(){ if(gStLock) xSemaphoreGive(gStLock); }

static char gBase[160] = FLEX_CLOUD_BASE_URL;
// DESTINO (FlexOS_Cloud.h): el que esta en uso y el pedido. Solo la tarea los
// cambia (applyDest); el resto solo lee gDest (un byte).
static volatile uint8_t gDest = FCD_INTERNET, gDestWant = FCD_INTERNET;
static volatile bool gPhoneForgot = false;     // vaciar el diario del telefono (lo hace la tarea)
static volatile bool gInetUnlinked = false;    // cuenta desvinculada con el destino en el telefono
static bool phoneDest(){ return gDest == FCD_PHONE; }
static const char* journalPath(){ return phoneDest() ? JOURNAL_PHONE : JOURNAL_PATH; }
static const char* dlDir(){ return phoneDest() ? DL_PHONE : DL_DIR; }
// Los textos de siempre; con el destino en el telefono, los suyos.
static const char* errText(const char* code){ return phoneDest() ? fclPhoneErrorText(code) : fclErrorText(code); }
static FlexCloudStatus gStatus;
static volatile bool gActive = false;
static uint32_t gNextMeMs = 0, gNetRetryAt = 0;
static uint8_t gNetFails = 0;
static volatile bool gMeDue = true;
// La lista que se ensena quedo VIEJA (se acaba de subir algo, un archivo cambio o ya no existe): la tarea la vuelve a pedir UNA vez
// en cuanto esta libre y la nube se ve. Es el "terminar de subir -> verlo en la Galeria" sin reabrirla (docs/FLEX-MEDIA-ECOSYSTEM.md §16).
static volatile bool gListStale = false;
static uint32_t gSeenRev = 0, gListFreshMs = 0;          // ultimo "rev" que dijo el telefono y cuando se publico la lista que se ensena
// La nube rechazo la credencial: no se vuelve a probar hasta que Flex Account
// la haya comprobado DESPUES del rechazo (o pasen 5 min). Sin esto, "la nube
// dice 401 / Flex Account aun no lo ha mirado" seria un bucle de peticiones.
static bool gAuthWait = false;
static uint32_t gAuthRejectMs = 0;
static const uint32_t AUTH_RETRY_MIN_MS = 30000, AUTH_RETRY_MAX_MS = 300000;

// Errores del servicio que NO son "el servicio no esta": el servidor contesto y
// dijo que no. Reintentarlos no cambia nada.
static bool permanentCode(const char* code){
  static const char* const P[] = { "quota_exceeded", "file_too_large", "payload_too_large", "name_invalid", "name_conflict",
                                   "invalid_request", "not_found", "part_out_of_range", "part_size_mismatch", "folder_cycle",
                                   "csrf_failed", "link_invalid", "range_not_satisfiable" };
  if(!code || !*code) return false;
  for(const char* p : P) if(!strcmp(code, p)) return true;
  return false;
}

// Vista
static FclItem* gList = nullptr;              // FLEX_CLOUD_LIST_MAX (PSRAM)
static FlexCloudListInfo gListInfo;
static char gCursor[FCL_CURSOR_MAX] = "";
static char gQuery[FCL_NAME_MAX] = "";

// Buffers de la tarea principal (PSRAM, reutilizados)
static char* gJson = nullptr;
static uint8_t* gIo = nullptr;

// Cola de ordenes de la interfaz
enum { CMD_NONE = 0, CMD_LIST, CMD_MORE, CMD_REFRESH, CMD_MKDIR, CMD_RENAME, CMD_TRASH, CMD_RESTORE, CMD_DELETE, CMD_VIEW };
struct Cmd {
  uint8_t  type, view;
  bool     folder;
  bool     play;                // leer por /files/<id>/playable (la version del perfil o el original ya compatible)
  uint32_t opId;
  uint64_t size;
  char     id[FCL_ID_MAX];
  char     sha[FCL_SHA_HEX];
  char     text[FCL_NAME_MAX];
};
static Cmd* gCmds = nullptr; static int gCmdHead = 0, gCmdN = 0;
static uint32_t gNextOp = 1;

// Eventos para la interfaz
static FlexCloudEvent* gEvents = nullptr; static int gEvHead = 0, gEvN = 0;

// Diario y su estado en RAM
static FclJournal* gJ = nullptr;
struct JobRt { uint8_t phase; uint64_t done; uint32_t rate; uint32_t retryAt; uint8_t fails; uint32_t rateMs; uint64_t rateBytes; };
static JobRt gRt[FCL_JOBS_MAX];
static bool gJournalDirty = false;

// Miniaturas
enum { TH_EMPTY = 0, TH_WANTED, TH_READY, TH_FAILED };
struct Thumb { char id[FCL_ID_MAX]; uint16_t* px; uint32_t lru, failMs; uint8_t state; };
static Thumb gThumbs[FLEX_CLOUD_THUMBS];
static uint32_t gThumbTick = 0, gThumbGen = 0;

// Streaming
struct Stream_ {
  bool     open;
  uint32_t gen;
  uint8_t  state;
  char     err[96];
  char     fileId[FCL_ID_MAX];
  bool     play;                // por /files/<id>/playable (el telefono ya preparo lo multimedia)
  uint32_t size;
  uint8_t* arena;
  FclCache cache;
  volatile bool busy;           // el fetcher esta escribiendo en un hueco
  uint32_t lastBlk;             // bloque donde leyo el reproductor por ultima vez (al cambiar, hay hueco nuevo por delante: se avisa a la tarea)
  uint32_t bps;                 // bajada reciente (bytes/s, media movil)
  uint32_t conns;               // conexiones abiertas en este flujo
} gSt;

// LAS TAREAS SE CREAN CUANDO HACEN FALTA (ver ensureMainTask / ensureStreamTask).
// Cada pila sale de la SRAM INTERNA (16 + 10 KB): tenerlas creadas siempre, con
// o sin cuenta y con o sin nube a la vista, le quitaba al sistema 26 KB seguidos
// que Flex Store necesita para su propia tarea (24 KB) y que mbedTLS necesita
// para sus buffers. Todo aviso a la tarea pasa por aqui, asi que pedir algo a la
// nube la crea si aun no existe (y si no hay memoria lo dice y se reintenta).
static bool ensureMainTask();
static void stNotify(){ if(gStTask) xTaskNotifyGive(gStTask); }
static void notifyTask(){ if(!gTask) ensureMainTask(); if(gTask) xTaskNotifyGive(gTask); }

// ======================================================================
//  Eventos y estado publico
// ======================================================================
static bool xferEvent(uint8_t k){ return k == FCE_UPLOAD_DONE || k == FCE_UPLOAD_FAILED || k == FCE_DOWNLOAD_DONE || k == FCE_DOWNLOAD_FAILED; }
static void pushEvent(const FlexCloudEvent& e){
  lock();
  if(gEvents){
    if(gEvN == EVENTS){
      // Cola llena (la interfaz no la vacia): se pierde el aviso MENOS importante,
      // el mas viejo que no sea de una transferencia. Los de transferencias se
      // repiten en el arranque si no se confirmaron (FCL_JF_DELIVERED).
      int drop = 0;
      for(int k = 0; k < gEvN; k++) if(!xferEvent(gEvents[(gEvHead + k) % EVENTS].kind)){ drop = k; break; }
      for(int k = drop; k > 0; k--) gEvents[(gEvHead + k) % EVENTS] = gEvents[(gEvHead + k - 1) % EVENTS];
      gEvHead = (gEvHead + 1) % EVENTS; gEvN--;
    }
    gEvents[(gEvHead + gEvN) % EVENTS] = e;
    gEvN++;
  }
  unlock();
}

static void setNet(uint8_t net, const char* text){
  lock();
  if(gStatus.net != net || strcmp(gStatus.netText, text ? text : "")){
    gStatus.net = net;
    snprintf(gStatus.netText, sizeof(gStatus.netText), "%s", text ? text : "");
    gStatus.gen++;
  }
  unlock();
}


// ======================================================================
//  Diario
// ======================================================================
static uint32_t gJournalSaves = 0;       // cuantas veces se escribio (las pruebas vigilan el desgaste)
static void saveJournal(){
  if(!gJ || !flexFsReady()) return;
  gJournalSaves++;
  size_t cap = fclJournalMaxBytes();
  uint8_t* buf = (uint8_t*)psAlloc(cap);
  if(!buf) return;
  lock();
  size_t n = fclJournalEncode(gJ, buf, cap);
  gJournalDirty = false;
  unlock();
  if(n){
    flexFsMkdir("/System");
    flexFsMkdir(CLOUD_DIR);
    if(!flexFsWriteBinAtomic(journalPath(), buf, n)) Serial.println(F("[CLOUD] no se pudo guardar el diario"));
  }
  psFree(buf);
}

// Tambien al cambiar de destino, con la interfaz leyendo: el diario se
// sustituye bajo el cerrojo (la lectura del archivo va fuera).
static void loadJournal(){
  size_t cap = fclJournalMaxBytes();
  uint8_t* buf = flexFsReady() ? (uint8_t*)psAlloc(cap) : nullptr;
  int n = buf ? flexFsReadBin(journalPath(), buf, cap) : 0;
  bool damaged = false;
  lock();
  fclJournalInit(gJ);
  if(n > 0) fclJournalDecode(gJ, buf, (size_t)n, &damaged);
  // La interfaz cancela y reintenta POR NUMERO: los del telefono no pueden
  // coincidir nunca con los de Internet.
  if(phoneDest() && gJ->nextId < PHONE_IDS) gJ->nextId = PHONE_IDS;
  gJournalDirty = false;
  unlock();
  if(damaged) Serial.println(F("[CLOUD] diario con registros danados: se descartaron"));
  psFree(buf);
}

static int jobIndex(const FclJob* j){ return (int)(j - gJ->jobs); }

// ======================================================================
//  HTTP
// ======================================================================
// Una parte de un archivo local como cuerpo de un PUT, sin cargarla en RAM.
// OJO: HTTPClient::sendRequest(Stream*) da vueltas mientras available() > -1
// y faltan bytes; un error de lectura DEBE devolver -1 o se quedaria ahi.
class PartStream : public Stream {
 public:
  PartStream(FlexFsStream* f, size_t len) : f_(f), left_(len), err_(false) {}
  int available() override { return err_ ? -1 : (int)(left_ > 0x7FFFFFFF ? 0x7FFFFFFF : left_); }
  int read() override { uint8_t c; return readBytes(&c, 1) == 1 ? c : -1; }
  int peek() override { return -1; }
  size_t readBytes(char* buf, size_t n) override { return readBytes((uint8_t*)buf, n); }
  size_t readBytes(uint8_t* buf, size_t n) override {
    if(n > left_) n = left_;
    if(!n) return 0;
    int r = flexFsStreamRead(f_, buf, n);
    if(r <= 0){ err_ = true; left_ = 0; return 0; }
    left_ -= (size_t)r;
    return (size_t)r;
  }
  size_t write(uint8_t) override { return 0; }
  void flush() override {}
  bool failed() const { return err_; }
 private:
  FlexFsStream* f_; size_t left_; bool err_;
};

struct Api { WiFiClientSecure sec; WiFiClient plain; HTTPClient http; };
static Api* gApi = nullptr;                  // conexion reutilizable de la tarea principal

// ---- Lo que cambia con el destino (el resto del gestor es el mismo) ----
// Internet: Flex Account, TLS verificado. Telefono: FlexOS_StorageLink, red
// local sin TLS (no gasta la SRAM interna de mbedTLS) y token de sesion.
static bool destUsable(){ return phoneDest() ? flexStoragePhoneUsable() : flexAccountUsable(); }
static bool netUsable(){ return destUsable() && WiFi.status() == WL_CONNECTED; }
static bool destBase(char* out, size_t cap){
  if(phoneDest()) return flexStoragePhoneBase(out, cap);
  snprintf(out, cap, "%s", gBase);
  return true;
}
// La credencial del destino; si no la hay, `code` dice por que.
static bool destBearer(char* out, size_t cap, char* code, size_t codeCap){
  if(phoneDest()) return flexStorageCopyBearer(out, cap, code, codeCap);
  if(flexAccountCopyBearer(out, cap)) return true;
  if(code && codeCap) snprintf(code, codeCap, "no_account");
  return false;
}
// El destino contesto 401 con una credencial que creiamos buena.
static void destRejected(){ if(phoneDest()) flexStorageSessionRejected(); else flexAccountReportRejected(); }
// Por que no se puede usar el destino (codigo para errText).
static const char* destWhy(){
  if(phoneDest()) return flexStoragePhoneState() == FSP_REJECTED ? "phone_rejected" : "no_phone";
  return flexAccountLinked() ? "auth_required" : "no_account";
}
// Codigo del motivo por el que la red "no esta lista" (taskStep, transferencias).
static const char* netWhy(uint8_t net){
  if(net == FCN_OFFLINE) return "no_wifi";
  if(net == FCN_AUTH) return phoneDest() ? "phone_rejected" : "auth_required";
  if(net == FCN_NO_ACCOUNT) return phoneDest() ? "no_phone" : "no_account";
  return "network";
}

// Por que fallo la ultima conexion TLS (lo dice mbedTLS); vacio si no fue un fallo de transporte.
static char gTlsWhy[48] = "";

// Sitio en la SRAM INTERNA para abrir una conexion TLS (FlexOS_CloudTLS). La
// guarda de antes miraba esp_get_free_heap_size(), que suma los 32 MB de PSRAM
// y nunca saltaba. Sin sitio no se intenta: el llamante lo trata como "sin
// memoria" (se reintenta con espera) en vez de dejar fallar a mbedTLS a medias.
static bool tlsRoomOk(){
  size_t f = 0, b = 0;
  if(flexTlsRoom(&f, &b)) return true;
  static uint32_t lastLogMs = 0;
  if(!lastLogMs || millis() - lastLogMs > 10000u){
    lastLogMs = millis() | 1u;
    Serial.printf("[CLOUD] TLS aplazado: SRAM interna %u KB (mayor bloque %u KB), hacen falta %u y %u KB\n",
                  (unsigned)(f / 1024u), (unsigned)(b / 1024u),
                  (unsigned)(FLEX_TLS_MIN_INTERNAL / 1024u), (unsigned)(FLEX_TLS_MIN_BLOCK / 1024u));
  }
  return false;
}

// Sin TLS (telefono) no hace falta sitio en la SRAM interna para mbedTLS.
static bool roomForConn(){ return phoneDest() || tlsRoomOk(); }
// Cliente para una conexion propia (descarga, visor, streaming): TLS verificado
// con Internet (aqui viaja la credencial); TCP normal con el telefono.
static WiFiClient* newClient(){
  if(phoneDest()) return new WiFiClient();
  WiFiClientSecure* s = new WiFiClientSecure();
  s->setCACert(flexCloudRootCA());
  s->setHandshakeTimeout(12);
  return s;
}

// Telefono: no hay "cuenta" que revisar. 401 = la sesion caduco (StorageLink
// abre otra en la siguiente peticion, con su propia espera si insiste); que el
// telefono ya no reconozca este Flex OS es el unico motivo para no insistir.
static void classifyPhoneFail(int status, const char* code){
  if(code && (!strcmp(code, "phone_rejected") || !strcmp(code, "device_revoked"))){
    setNet(FCN_AUTH, errText("phone_rejected"));
    return;
  }
  if(status == 401){ flexStorageSessionRejected(); return; }
  if(status < 0 || (status >= 500 && !permanentCode(code))){
    if(gNetFails < 250) gNetFails++;
    gNetRetryAt = millis() + fclBackoffMs(gNetFails);
    setNet(FCN_UNAVAILABLE, errText(status < 0 ? "network" : "server"));
  }
}

static void classifyFail(int status, const char* code){
  if(phoneDest()){ classifyPhoneFail(status, code); return; }
  // 401 con un codigo del servicio: la credencial no vale. Se avisa a Flex
  // Account para que lo compruebe (no se desvincula por una respuesta).
  if(status == 401 && code && (!strcmp(code, "auth_required") || !strcmp(code, "device_revoked") || !strcmp(code, "token_expired"))){
    flexAccountReportRejected();
    gAuthWait = true; gAuthRejectMs = millis();
    setNet(FCN_AUTH, fclErrorText(code));
    return;
  }
  // Servicio caido: transporte, 5xx sin una respuesta de negocio (un 507 de
  // cuota llena es una respuesta, no una caida) o un 401 que no es del servicio
  // (la pagina de error de un proxy).
  if(status < 0 || (status >= 500 && !permanentCode(code)) || (status == 401 && (!code || !*code))){
    if(gNetFails < 250) gNetFails++;
    gNetRetryAt = millis() + fclBackoffMs(gNetFails);
    char txt[96];
    if(status < 0) snprintf(txt, sizeof(txt), "Sin respuesta segura de Flex Cloud%s%s%s", gTlsWhy[0] ? " (" : "", gTlsWhy, gTlsWhy[0] ? ")" : "");
    else snprintf(txt, sizeof(txt), "%s", fclErrorText("server"));
    setNet(FCN_UNAVAILABLE, txt);
  }
}

// Una peticion de la API. `json` (cuerpo JSON) o `body` (flujo) o nada.
// Devuelve el estado HTTP (<0 = transporte). El cuerpo de la respuesta queda
// en gJson (terminado en 0) y su error.code en `code`.
static int apiCallOnce(const char* method, const char* path, const char* json, Stream* body, size_t bodyLen,
                       const char* hName, const char* hVal, char* code, size_t codeCap, size_t* outLen){
  if(code && codeCap) code[0] = 0;
  if(outLen) *outLen = 0;
  if(!gApi || !gJson) return HTTPC_ERROR_TOO_LESS_RAM;
  if(!netUsable()) return HTTPC_ERROR_NOT_CONNECTED;
  const bool phone = phoneDest();
  if(!roomForConn()){ if(code) snprintf(code, codeCap, "no_memory"); return HTTPC_ERROR_TOO_LESS_RAM; }
  char bearer[64];
  if(!destBearer(bearer, sizeof(bearer), code, codeCap)){
    // Telefono sin sesion: no contesta (o espera su reintento) o ya no reconoce
    // este Flex OS. Es su estado de red, como un fallo de transporte.
    if(phone) classifyFail(code && !strcmp(code, "phone_rejected") ? 403 : -1, code);
    return HTTPC_ERROR_NOT_CONNECTED;
  }
  char base[160];
  if(!destBase(base, sizeof(base))){ memset(bearer, 0, sizeof(bearer)); if(code) snprintf(code, codeCap, "no_phone"); return HTTPC_ERROR_NOT_CONNECTED; }
  char url[384];
  snprintf(url, sizeof(url), "%s%s", base, path);
  HTTPClient& h = gApi->http;
  if(!phone){
    gApi->sec.setCACert(flexCloudRootCA());   // TLS SIEMPRE verificado: aqui viaja la credencial
    gApi->sec.setHandshakeTimeout(12);
  }
  h.setReuse(true);
  h.setTimeout(HTTP_TIMEOUT);
  h.setConnectTimeout(HTTP_TIMEOUT);
  h.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  h.useHTTP10(false);
  if(!(phone ? h.begin(gApi->plain, url) : h.begin(gApi->sec, url))){ memset(bearer, 0, sizeof(bearer)); return HTTPC_ERROR_CONNECTION_REFUSED; }
  char auth[80];
  snprintf(auth, sizeof(auth), "Bearer %s", bearer);
  memset(bearer, 0, sizeof(bearer));
  h.addHeader("Authorization", auth);
  memset(auth, 0, sizeof(auth));
  h.addHeader("Accept", "application/json");
  h.addHeader("Accept-Encoding", "identity");
  h.setUserAgent("FlexOS-Ultra/" FLEXOS_FW_VERSION " ESP32-P4 FlexCloud");
  if(hName && hVal) h.addHeader(hName, hVal);
  int st;
  const uint32_t t0 = millis();
  if(json){ h.addHeader("Content-Type", "application/json"); st = h.sendRequest(method, (uint8_t*)json, strlen(json)); }
  else if(body){ if(!hName || strcasecmp(hName, "Content-Type")) h.addHeader("Content-Type", "application/octet-stream"); st = h.sendRequest(method, body, bodyLen); }
  else st = h.sendRequest(method);
  const uint32_t connMs = millis() - t0;                    // si no hubo conexion, lo que tardo en fallar
  size_t n = 0;
  if(st > 0){
    FlexBufSink sink(gJson, JSON_CAP);
    int w = h.writeToStream(&sink);
    sink.terminate();
    n = sink.len();
    if(sink.overflow()){
      // Mas grande de lo que cabe: no es un fallo de red, es una respuesta que no se acepta.
      if(st >= 200 && st < 300){ st = HTTPC_ERROR_TOO_LESS_RAM; if(code) snprintf(code, codeCap, "bad_response"); }
    } else if(w < 0 && st >= 200 && st < 300) st = w;         // cuerpo cortado: no cuenta como exito
  } else gJson[0] = 0;
  gTlsWhy[0] = 0;
  if(st < 0 && !phone){
    // El codigo de mbedTLS de ESTA conexion, antes de soltarla.
    char raw[64]; int e = gApi->sec.lastError(raw, sizeof(raw));
    if(e == -1 && st == HTTPC_ERROR_CONNECTION_REFUSED){
      // -1 = DNS, TCP o saludo TLS: se distingue por el DNS y por lo que tardo (flexTlsPhase).
      char host[80]; bool dns = true;
      if(flexUrlHost(gBase, host, sizeof(host))) dns = flexTlsDnsOk(host);
      flexTlsPhase(dns, connMs, 12000u, HTTP_TIMEOUT, gTlsWhy, sizeof(gTlsWhy));
    } else flexTlsReason(e, gTlsWhy, sizeof(gTlsWhy));
  }
  h.end();
  if(st < 0){ if(phone) gApi->plain.stop(); else gApi->sec.stop(); }   // conexion en estado desconocido: fuera
  if(phone) flexStorageNoteResult(st);
  if(outLen) *outLen = n;
  if(st >= 400 && code && !code[0]) fclParseError(gJson, n, code, codeCap, nullptr, 0);
  if(st < 0 && code && !code[0]) snprintf(code, codeCap, "network");
  if(st >= 200 && st < 300){
    gNetFails = 0;
    lock(); bool was = gStatus.net != FCN_ONLINE; unlock();
    if(was) setNet(FCN_ONLINE, phone ? "Conectado al tel\xC3\xA9" "fono" : "Conectado a Flex Cloud");
  } else classifyFail(st, code);
  return st;
}

static int apiCall(const char* method, const char* path, const char* json, Stream* body, size_t bodyLen,
                   const char* hName, const char* hVal, char* code, size_t codeCap, size_t* outLen = nullptr){
  int st = apiCallOnce(method, path, json, body, bodyLen, hName, hVal, code, codeCap, outLen);
  // Telefono: un 401 es un token que caduco (el telefono reinicio sus sesiones,
  // o se cambio de Wi-Fi). classifyFail ya lo solto: se abre otra sesion y la
  // peticion se repite UNA vez, si se puede repetir (un flujo de bytes ya se
  // consumio: esa la reintenta su trabajo). Si el token recien abierto tambien
  // se rechaza, StorageLink impone su espera: nunca un bucle.
  if(st == 401 && phoneDest() && !body) st = apiCallOnce(method, path, json, body, bodyLen, hName, hVal, code, codeCap, outLen);
  return st;
}

// ======================================================================
//  Cuenta, cuota y estado de la red
// ======================================================================
// La cuota y la direccion que se ensenaban eran de una cuenta que ya no sirve (o
// que ya no esta): se sueltan. Si se vincula OTRA cuenta, su tarjeta no puede
// empezar ensenando el espacio de la anterior.
// Tambien la lista de archivos que se ensenaba: era de esa cuenta. Queda en ERROR
// (no en "sin pedir"), asi que al volver la conexion la interfaz la pide otra vez
// (CloudKit: "vuelve la conexion con la lista en error").
static void forgetIdentity(const char* why){
  lock();
  if(gStatus.quotaValid || gStatus.address[0] || gListInfo.count > 0 || gListInfo.state == FCL_LIST_READY){
    gStatus.quotaValid = false;
    memset(&gStatus.quota, 0, sizeof(gStatus.quota));
    gStatus.address[0] = 0;
    gStatus.gen++;
    gListInfo.count = 0; gListInfo.more = false; gListInfo.nCrumbs = 0;
    gListInfo.state = FCL_LIST_ERROR;
    snprintf(gListInfo.error, sizeof(gListInfo.error), "%s", errText(why));
    gListInfo.gen++;
  }
  unlock();
}

// Telefono: FlexOS_StorageLink sabe si hay uno emparejado, conectado y que no
// haya rechazado a este Flex OS. Si responde lo dira la siguiente peticion.
static void refreshNetPhone(){
  uint8_t ps = flexStoragePhoneState();
  if(ps == FSP_REJECTED){ forgetIdentity("phone_rejected"); setNet(FCN_AUTH, errText("phone_rejected")); return; }
  if(ps != FSP_READY){ forgetIdentity("no_phone"); setNet(FCN_NO_ACCOUNT, errText("no_phone")); return; }
  if(WiFi.status() != WL_CONNECTED){ setNet(FCN_OFFLINE, errText("no_wifi")); return; }
  lock(); uint8_t net = gStatus.net; unlock();
  if(net == FCN_NO_ACCOUNT || net == FCN_OFFLINE || net == FCN_AUTH){
    setNet(FCN_CONNECTING, "Conectando con el tel\xC3\xA9" "fono");
    if(gActive) gMeDue = true;
    gNetRetryAt = 0;
  }
}

static void refreshNet(){
  if(phoneDest()){ refreshNetPhone(); return; }
  if(!flexAccountLinked()){ gAuthWait = false; forgetIdentity("no_account"); setNet(FCN_NO_ACCOUNT, fclErrorText("no_account")); return; }
  if(!flexAccountUsable()){
    // Flex Account ya lo sabe y lo ensena: de aqui se sale revinculando.
    gAuthWait = false;
    forgetIdentity("auth_required");
    FlexAccountLink l = flexAccountLinkState();
    setNet(FCN_AUTH, fclErrorText(l == FLEX_LINK_TOKEN_EXPIRED ? "token_expired" : "device_revoked"));
    return;
  }
  if(WiFi.status() != WL_CONNECTED){ setNet(FCN_OFFLINE, fclErrorText("no_wifi")); return; }
  lock(); uint8_t net = gStatus.net; unlock();
  if(net == FCN_AUTH && gAuthWait){
    uint32_t since = millis() - gAuthRejectMs;
    FlexAccountSnapshot snap; flexAccountSnapshot(&snap);
    bool checkedAfter = snap.verifiedAgeS != 0xFFFFFFFFu && (uint64_t)snap.verifiedAgeS * 1000u + 1000u <= since;
    if(!((checkedAfter && since >= AUTH_RETRY_MIN_MS) || since >= AUTH_RETRY_MAX_MS)) return;
    gAuthWait = false;
  }
  if(net == FCN_NO_ACCOUNT || net == FCN_OFFLINE || net == FCN_AUTH){
    setNet(FCN_CONNECTING, "Conectando con Flex Cloud");
    // La cuota se pide con la nube a la vista (flexCloudSetActive), no cada vez
    // que vuelve la red: al arrancar Flex Account ya esta validando la misma
    // credencial contra el mismo servidor y dos handshakes a la vez agotaban la
    // SRAM interna justo cuando entra el Wi-Fi.
    if(gActive) gMeDue = true;
    gNetRetryAt = 0;
  }
}

static bool netReady(){
  lock(); uint8_t net = gStatus.net; unlock();
  if(net == FCN_NO_ACCOUNT || net == FCN_OFFLINE || net == FCN_AUTH) return false;
  // gNetRetryAt == 0 es "sin espera". Comparar millis() con 0 COMO INSTANTE da la
  // vuelta a los 24,8 dias encendido ((int32_t)(millis() - 0) < 0): tras volver el
  // Wi-Fi o revincular, la nube se quedaba en "Conectando" hasta reiniciar.
  return !gNetRetryAt || (int32_t)(millis() - gNetRetryAt) >= 0 || net == FCN_ONLINE;
}

static void fetchMe(){
  char code[FCL_CODE_MAX]; size_t n = 0;
  int st = apiCall("GET", "/me", nullptr, nullptr, 0, nullptr, nullptr, code, sizeof(code), &n);
  gNextMeMs = millis() + (phoneDest() ? ME_PHONE_MS : ME_EVERY_MS);
  gMeDue = false;
  if(st != 200) return;
  char addr[48] = "", name[64] = "";
  FclQuota q;
  if(fclParseMe(gJson, n, addr, sizeof(addr), name, sizeof(name), &q)){
    lock();
    gStatus.quota = q; gStatus.quotaValid = q.totalBytes > 0;
    snprintf(gStatus.address, sizeof(gStatus.address), "%s", addr);
    gStatus.gen++;
    unlock();
    // El telefono cuenta los cambios de SU lista (subidas desde la web o desde el propio telefono, renombrados, el estado de preparacion).
    // Si cambio desde la ultima vez y la lista que se ensena no se acaba de pedir, esta vieja: se vuelve a pedir SOLA. Lo propio del P4 (una
    // subida o una orden suya) ya refresco la lista justo antes: eso no cuenta como un cambio ajeno.
    if(q.rev){
      if(gSeenRev && q.rev != gSeenRev && (uint32_t)(millis() - gListFreshMs) > 2500u) gListStale = true;
      gSeenRev = q.rev;
    }
  }
}

// ======================================================================
//  Listados y operaciones
// ======================================================================
static void listError(const char* code){
  lock();
  gListInfo.state = FCL_LIST_ERROR;
  snprintf(gListInfo.error, sizeof(gListInfo.error), "%s", errText(code));
  gListInfo.gen++;
  unlock();
}

static void doList(bool more){
  char path[512], enc[3 * FCL_NAME_MAX];
  char cursor[FCL_CURSOR_MAX];
  lock();
  uint8_t view = gListInfo.view;
  char folder[FCL_ID_MAX]; snprintf(folder, sizeof(folder), "%s", gListInfo.folderId);
  snprintf(cursor, sizeof(cursor), "%s", more ? gCursor : "");
  int have = more ? gListInfo.count : 0;
  uint32_t gen0 = gListInfo.gen;
  unlock();
  int room = FLEX_CLOUD_LIST_MAX - have;
  if(room <= 0) return;
  int lim = room < PAGE ? room : PAGE;
  switch(view){
    case FCL_VIEW_RECENT: snprintf(path, sizeof(path), "/files?view=recent&limit=%d", lim); break;
    case FCL_VIEW_MEDIA:  snprintf(path, sizeof(path), "/files?view=recent&kind=media&limit=%d", lim); break;
    case FCL_VIEW_VIDEO:  snprintf(path, sizeof(path), "/files?view=recent&kind=video&limit=%d", lim); break;
    case FCL_VIEW_TRASH:  snprintf(path, sizeof(path), "/files?view=trash&limit=%d", lim); break;
    case FCL_VIEW_SEARCH:
      fclUrlEncode(gQuery, enc, sizeof(enc));
      snprintf(path, sizeof(path), "/files?view=search&q=%s&limit=%d", enc, lim); break;
    default: snprintf(path, sizeof(path), "/files?parentId=%s&sort=name&limit=%d", folder[0] ? folder : "root", lim); break;
  }
  if(cursor[0]){ size_t L = strlen(path); snprintf(path + L, sizeof(path) - L, "&cursor=%s", cursor); }
  char code[FCL_CODE_MAX]; size_t n = 0;
  int st = apiCall("GET", path, nullptr, nullptr, 0, nullptr, nullptr, code, sizeof(code), &n);
  if(st != 200){ listError(code[0] ? code : "server"); return; }
  // Se lee a un hueco temporal y se publica de una vez, bajo el cerrojo.
  FclItem* tmp = (FclItem*)psAlloc(sizeof(FclItem) * (size_t)lim);
  if(!tmp){ listError("no_memory"); return; }
  bool hasMore = false; char next[FCL_CURSOR_MAX]; FclCrumb crumbs[FCL_CRUMBS]; int nc = 0;
  int got = fclParseList(gJson, n, tmp, lim, next, sizeof(next), &hasMore, crumbs, FCL_CRUMBS, &nc);
  if(got < 0){ psFree(tmp); listError("bad_response"); return; }
  lock();
  if(gListInfo.gen == gen0 && gListInfo.view == view){          // nadie pidio otra vista mientras tanto
    memcpy(gList + have, tmp, sizeof(FclItem) * (size_t)got);
    gListInfo.count = have + got;
    gListInfo.more = hasMore && gListInfo.count < FLEX_CLOUD_LIST_MAX;
    snprintf(gCursor, sizeof(gCursor), "%s", next);
    if(!more){ memcpy(gListInfo.crumbs, crumbs, sizeof(crumbs)); gListInfo.nCrumbs = nc; }
    gListInfo.state = FCL_LIST_READY;
    gListInfo.error[0] = 0;
    gListInfo.gen++;
    gListFreshMs = millis();
  }
  unlock();
  psFree(tmp);
}

static void opDone(uint32_t opId, int st, const char* code, const char* okText){
  FlexCloudEvent e; memset(&e, 0, sizeof(e));
  e.kind = FCE_OP_DONE; e.opId = opId; e.ok = st >= 200 && st < 300;
  snprintf(e.code, sizeof(e.code), "%s", e.ok ? "" : (code && code[0] ? code : "server"));
  snprintf(e.text, sizeof(e.text), "%s", e.ok ? okText : errText(e.code));
  pushEvent(e);
  if(e.ok){ gMeDue = true; lock(); gListInfo.state = FCL_LIST_LOADING; gListInfo.gen++; unlock(); doList(false); }
}

static void runCmd(const Cmd& c){
  char body[3 * FCL_NAME_MAX + 96], esc[2 * FCL_NAME_MAX], path[160], code[FCL_CODE_MAX];
  const char* coll = c.folder ? "folders" : "files";
  int st = 0;
  switch(c.type){
    case CMD_LIST: case CMD_REFRESH: doList(false); if(c.type == CMD_REFRESH) gMeDue = true; return;
    case CMD_MORE: doList(true); return;
    case CMD_MKDIR:
      if(!fclJsonEscape(c.text, esc, sizeof(esc))){ opDone(c.opId, 400, "name_invalid", ""); return; }
      if(c.id[0]) snprintf(body, sizeof(body), "{\"name\":\"%s\",\"parentId\":\"%s\"}", esc, c.id);
      else snprintf(body, sizeof(body), "{\"name\":\"%s\"}", esc);
      st = apiCall("POST", "/folders", body, nullptr, 0, nullptr, nullptr, code, sizeof(code));
      opDone(c.opId, st, code, "Carpeta creada");
      return;
    case CMD_RENAME:
      if(!fclJsonEscape(c.text, esc, sizeof(esc))){ opDone(c.opId, 400, "name_invalid", ""); return; }
      snprintf(body, sizeof(body), "{\"name\":\"%s\"}", esc);
      snprintf(path, sizeof(path), "/%s/%s", coll, c.id);
      st = apiCall("PATCH", path, body, nullptr, 0, nullptr, nullptr, code, sizeof(code));
      opDone(c.opId, st, code, "Nombre cambiado");
      return;
    case CMD_TRASH:
      snprintf(path, sizeof(path), "/%s/%s", coll, c.id);
      st = apiCall("DELETE", path, nullptr, nullptr, 0, nullptr, nullptr, code, sizeof(code));
      opDone(c.opId, st, code, "Movido a la papelera de Flex Cloud");
      return;
    case CMD_RESTORE:
      snprintf(path, sizeof(path), "/%s/%s/restore", coll, c.id);
      st = apiCall("POST", path, "{}", nullptr, 0, nullptr, nullptr, code, sizeof(code));
      opDone(c.opId, st, code, "Restaurado");
      return;
    case CMD_DELETE:
      snprintf(path, sizeof(path), "/%s/%s/permanent-delete", coll, c.id);
      st = apiCall("POST", path, "{}", nullptr, 0, nullptr, nullptr, code, sizeof(code));
      opDone(c.opId, st, code, "Eliminado de Flex Cloud");
      return;
    default: return;
  }
}

static bool pushCmd(const Cmd& c){
  bool ok = false;
  lock();
  if(gCmds){
    // Pedir otra vista sustituye a una peticion de vista que aun no salio.
    if(c.type == CMD_LIST || c.type == CMD_REFRESH)
      for(int i = 0; i < gCmdN; i++){ Cmd& x = gCmds[(gCmdHead + i) % CMDS]; if(x.type == CMD_LIST || x.type == CMD_MORE || x.type == CMD_REFRESH) x.type = CMD_NONE; }
    // Otra pagina ya esperando turno: no se pide dos veces (el final de la lista la pide en cada repintado).
    if(c.type == CMD_MORE)
      for(int i = 0; i < gCmdN; i++) if(gCmds[(gCmdHead + i) % CMDS].type == CMD_MORE){ unlock(); return true; }
    // Igual con la foto del visor: solo hay una viva, y la anterior ya no la quiere nadie.
    if(c.type == CMD_VIEW)
      for(int i = 0; i < gCmdN; i++){ Cmd& x = gCmds[(gCmdHead + i) % CMDS]; if(x.type == CMD_VIEW) x.type = CMD_NONE; }
    // Una orden que se sustituyo ya no ocupa hueco: la cola no se llena de CMD_NONE que nadie va a atender.
    { int w = 0;
      for(int i = 0; i < gCmdN; i++){ Cmd x = gCmds[(gCmdHead + i) % CMDS]; if(x.type != CMD_NONE) gCmds[(gCmdHead + w++) % CMDS] = x; }
      gCmdN = w; }
    if(gCmdN < CMDS){ gCmds[(gCmdHead + gCmdN) % CMDS] = c; gCmdN++; ok = true; }
  }
  unlock();
  if(ok) notifyTask();
  return ok;
}
static bool popCmd(Cmd& c){
  bool ok = false;
  lock();
  while(gCmds && gCmdN){
    c = gCmds[gCmdHead]; gCmdHead = (gCmdHead + 1) % CMDS; gCmdN--;
    if(c.type != CMD_NONE){ ok = true; break; }
  }
  unlock();
  return ok;
}

// ======================================================================
//  Transferencias (un trabajo a la vez, un paso por vuelta)
// ======================================================================
struct Runner {
  FclJob*       job;
  uint8_t       stage;
  bool          resumedSession; // la sesion de la nube venia de antes (no la acabamos de crear)
  uint8_t*      partSha;        // 32 bytes por parte (PSRAM), solo durante la subida
  uint32_t      parts, chunk, next;
  uint32_t      hashPart;       // huellas: parte en curso
  uint64_t      hashLeft;       //          bytes que le faltan
  FclUpload     up;
  FlexFsStream* f;
  FclSha        sha, partCtx;
  uint64_t      done;
  HTTPClient*   dl;             // descarga en curso (HTTP/1.0, cuerpo sin trocear)
  WiFiClient*   dlCli;          // su conexion (TLS con Internet, TCP con el telefono)
  uint64_t      dlLeft;
  bool          verifyOpen;     // "liberar espacio": comprobando el original otra vez
  uint64_t      verifyLeft;
  bool          localChanged;   // el original ya no es lo que se subio: NO se libera
};
static Runner gRun;
enum { RS_IDLE = 0, RS_PREP, RS_HASH, RS_CREATE, RS_PARTS, RS_COMPLETE, RS_VERIFY_LOCAL, RS_THUMB, RS_DL_PREP, RS_DL_GET, RS_DL_VERIFY };

static const uint32_t HASH_STEP   = 1024u * 1024u;  // bytes de huella por vuelta (~0,1 s en el P4)
static const uint8_t  MAX_SERVER_FAILS = 12;        // fallos seguidos del SERVIDOR antes de rendirse (~8 min)

static void rtPhase(FclJob* j, uint8_t phase){
  int i = jobIndex(j);
  lock(); if(gRt[i].phase != phase){ gRt[i].phase = phase; gStatus.xferGen++; } unlock();
}
static void rtDone(FclJob* j, uint64_t done){
  int i = jobIndex(j);
  uint32_t now = millis();
  lock();
  gRt[i].done = done;
  if(!gRt[i].rateMs || now - gRt[i].rateMs >= 1000){
    if(gRt[i].rateMs && now != gRt[i].rateMs) gRt[i].rate = (uint32_t)((done > gRt[i].rateBytes ? done - gRt[i].rateBytes : 0) * 1000u / (now - gRt[i].rateMs));
    gRt[i].rateMs = now; gRt[i].rateBytes = done;
  }
  gStatus.xferGen++;
  unlock();
}

// Suelta lo que el paso tenia abierto. Lo imprescindible para reanudar vive
// en el diario y en el servidor, no aqui.
static void runnerDropIo(){
  if(gRun.f){ flexFsStreamClose(gRun.f); gRun.f = nullptr; }
  if(gRun.dl){ gRun.dl->end(); delete gRun.dl; gRun.dl = nullptr; }
  if(gRun.dlCli){ gRun.dlCli->stop(); delete gRun.dlCli; gRun.dlCli = nullptr; }
}
static void runnerClose(){
  runnerDropIo();
  psFree(gRun.partSha); gRun.partSha = nullptr;
  lock(); gRun.job = nullptr; unlock();
  gRun.stage = RS_IDLE;
}

static void dlTempPath(const FclJob* j, char* out, size_t cap){ snprintf(out, cap, "%s/%lu.part", dlDir(), (unsigned long)j->id); }

static void jobFinish(FclJob* j, uint8_t state, const char* code){
  int i = jobIndex(j);
  lock();
  // Cancelado por el usuario mientras corria el ultimo paso: gana la cancelacion.
  bool cancelled = j->state == FCL_JOB_CANCELLED;
  if(cancelled) state = FCL_JOB_CANCELLED;
  j->state = state;
  if(state == FCL_JOB_FAILED) snprintf(j->error, sizeof(j->error), "%s", errText(code));
  if(state == FCL_JOB_DONE) j->flags &= (uint8_t)~FCL_JF_ABORT;
  gRt[i].phase = state == FCL_JOB_DONE ? FCX_DONE : state == FCL_JOB_CANCELLED ? FCX_CANCELLED : FCX_FAILED;
  gRt[i].retryAt = 0;
  gStatus.xferGen++;
  unlock();
  bool up = j->type == FCL_JOB_UPLOAD;
  if(cancelled && !up){ char tp[FCL_PATH_MAX]; dlTempPath(j, tp, sizeof(tp)); flexFsDelete(tp); }
  saveJournal();
  FlexCloudEvent e; memset(&e, 0, sizeof(e));
  e.kind = state == FCL_JOB_DONE ? (up ? FCE_UPLOAD_DONE : FCE_DOWNLOAD_DONE) : (up ? FCE_UPLOAD_FAILED : FCE_DOWNLOAD_FAILED);
  e.opId = j->id; e.ok = state == FCL_JOB_DONE; e.mlId = j->mlId; e.flags = j->flags; e.size = j->size;
  snprintf(e.code, sizeof(e.code), "%s", code ? code : "");
  snprintf(e.text, sizeof(e.text), "%s", e.ok ? (up ? "Subido a Flex Cloud" : "Descargado") : errText(code));
  if(e.ok && up && (j->flags & FCL_JF_FREE_LOCAL) && gRun.localChanged){
    // Subido, pero el original cambio despues (o no se pudo releer): se conserva.
    lock(); j->flags &= (uint8_t)~FCL_JF_FREE_LOCAL; unlock();
    e.flags = j->flags;
    snprintf(e.code, sizeof(e.code), "local_changed");
    snprintf(e.text, sizeof(e.text), "Subido. El original cambi\xC3\xB3 y se conserva");
    saveJournal();
  }
  snprintf(e.localPath, sizeof(e.localPath), "%s", j->localPath);
  snprintf(e.name, sizeof(e.name), "%s", j->name);
  snprintf(e.fileId, sizeof(e.fileId), "%s", up ? j->fileId : j->remoteId);
  snprintf(e.sha256, sizeof(e.sha256), "%s", j->sha256);
  if(state != FCL_JOB_CANCELLED) pushEvent(e);
  Serial.printf("[CLOUD] %s #%lu %s %s\n", up ? "subida" : "descarga", (unsigned long)j->id,
                state == FCL_JOB_DONE ? "terminada" : state == FCL_JOB_CANCELLED ? "cancelada" : "fallida", code ? code : "");
  runnerClose();
  if(e.ok){ gMeDue = true; if(up) gListStale = true; }
}

// Fallo de un paso.
//  · Sin red o sin respuesta: se espera (cada vez mas, hasta 1 min) y se
//    reanuda solo; no se da por perdido.
//  · El servidor contesta mal (5xx, 429, huella de una parte alterada): igual,
//    pero solo MAX_SERVER_FAILS veces seguidas; luego queda "fallida" y el
//    usuario puede reintentarla (se reanuda donde iba).
//  · Credencial rechazada: no se insiste; espera a que Flex Account la valide.
//  · Lo demas (cuota, nombre, archivo demasiado grande...): fallo definitivo.
static void stepFail(FclJob* j, int st, const char* code){
  bool network = st < 0 || (code && (!strcmp(code, "network") || !strcmp(code, "no_memory")));
  bool server = !network && !permanentCode(code) &&
                (st >= 500 || st == 429 || (code && (!strcmp(code, "checksum_mismatch") || !strcmp(code, "account_unavailable") ||
                                                     !strcmp(code, "upload_busy") || !strcmp(code, "bad_response"))));
  int i = jobIndex(j);
  runnerDropIo();
  // Se reanuda preguntando al servidor lo que ya tiene: nada en RAM es imprescindible.
  uint8_t back = j->type == FCL_JOB_UPLOAD ? (gRun.stage >= RS_CREATE ? RS_CREATE : RS_PREP) : RS_DL_PREP;
  if(st == 401){
    // Internet: espera a que Flex Account la valide. Telefono: la sesion se
    // renueva sola en la siguiente peticion (con su propia espera).
    uint32_t wait = phoneDest() ? 2000u : 30000u;
    lock(); gRt[i].phase = FCX_WAITING_NET; gRt[i].retryAt = millis() + wait; gStatus.xferGen++; unlock();
    gRun.stage = back;
    return;
  }
  if(network || server){
    lock();
    if(gRt[i].fails < 250) gRt[i].fails++;
    uint8_t fails = gRt[i].fails;
    bool giveUp = server && fails >= MAX_SERVER_FAILS;
    if(!giveUp){
      gRt[i].retryAt = millis() + fclBackoffMs(fails);
      gRt[i].phase = WiFi.status() == WL_CONNECTED ? FCX_RETRYING : FCX_WAITING_NET;
      gStatus.xferGen++;
    }
    unlock();
    if(!giveUp){
      gRun.stage = back;
      Serial.printf("[CLOUD] #%lu paso fallido (%d %s): reintento en %lu ms\n", (unsigned long)j->id, st, code ? code : "",
                    (unsigned long)fclBackoffMs(fails));
      return;
    }
  }
  jobFinish(j, FCL_JOB_FAILED, code && code[0] ? code : "server");
}

static void hexToBytes(const char* hx, uint8_t* out){
  for(int k = 0; k < 32; k++){
    auto nib = [](char c){ return (uint8_t)(c <= '9' ? c - '0' : c - 'a' + 10); };
    out[k] = (uint8_t)((nib(hx[2 * k]) << 4) | nib(hx[2 * k + 1]));
  }
}

// ---- Subida ------------------------------------------------------------
// Huellas: SHA-256 del archivo entero y de cada parte, en UNA pasada y por
// tramos (HASH_STEP por vuelta: la tarea sigue atendiendo a la interfaz).
// Se rehacen en cada arranque: asi tambien se comprueba que el original no
// cambio mientras la subida estaba a medias.
static void upPrepare(FclJob* j){
  rtPhase(j, FCX_PREPARING);
  FlexFsStream* f = flexFsOpenRead(j->localPath);
  if(!f){ jobFinish(j, FCL_JOB_FAILED, "local_missing"); return; }
  uint64_t size = flexFsStreamSize(f);
  if(j->size && size != j->size){ flexFsStreamClose(f); jobFinish(j, FCL_JOB_FAILED, "local_changed"); return; }
  lock(); j->size = size; unlock();
  gRun.chunk = fclChunkFor(size);
  gRun.parts = size ? (uint32_t)((size + gRun.chunk - 1) / gRun.chunk) : 0;
  psFree(gRun.partSha);
  gRun.partSha = (uint8_t*)psAlloc((size_t)(gRun.parts ? gRun.parts : 1) * 32u);
  if(!gRun.partSha){ flexFsStreamClose(f); stepFail(j, -1, "no_memory"); return; }
  gRun.f = f;
  fclShaStart(&gRun.sha); fclShaStart(&gRun.partCtx);
  gRun.hashPart = 0;
  gRun.hashLeft = gRun.parts ? (gRun.parts > 1 ? gRun.chunk : size) : 0;
  gRun.stage = RS_HASH;
}

static void upHash(FclJob* j){
  uint32_t budget = HASH_STEP;
  while(budget && gRun.hashPart < gRun.parts){
    if(!gRun.hashLeft){
      char hx[FCL_SHA_HEX]; fclShaFinishHex(&gRun.partCtx, hx);
      hexToBytes(hx, gRun.partSha + (size_t)gRun.hashPart * 32u);
      gRun.hashPart++;
      if(gRun.hashPart < gRun.parts){
        fclShaStart(&gRun.partCtx);
        gRun.hashLeft = gRun.hashPart + 1 < gRun.parts ? gRun.chunk : j->size - (uint64_t)gRun.hashPart * gRun.chunk;
      }
      continue;
    }
    size_t want = gRun.hashLeft < IO_CAP ? (size_t)gRun.hashLeft : IO_CAP;
    if(want > budget) want = budget;
    int r = flexFsStreamRead(gRun.f, gIo, want);
    if(r <= 0){ jobFinish(j, FCL_JOB_FAILED, "local_io"); return; }
    fclShaUpdate(&gRun.sha, gIo, (size_t)r); fclShaUpdate(&gRun.partCtx, gIo, (size_t)r);
    gRun.hashLeft -= (uint64_t)r;
    budget = budget > (uint32_t)r ? budget - (uint32_t)r : 0;
  }
  if(gRun.hashPart < gRun.parts) return;                       // sigue en la proxima vuelta
  char hex[FCL_SHA_HEX]; fclShaFinishHex(&gRun.sha, hex);
  flexFsStreamClose(gRun.f); gRun.f = nullptr;
  if(j->sha256[0] && strcmp(j->sha256, hex)){ jobFinish(j, FCL_JOB_FAILED, "local_changed"); return; }
  if(!j->sha256[0]){
    lock(); snprintf(j->sha256, sizeof(j->sha256), "%s", hex); gJournalDirty = true; unlock();
    saveJournal();
  }
  gRun.stage = RS_CREATE;
}

static void upCreate(FclJob* j){
  rtPhase(j, FCX_PREPARING);
  char path[96], code[FCL_CODE_MAX]; size_t n = 0; int st;
  gRun.resumedSession = false;
  // Si ya habia sesion, se pregunta por ella; si no (o caduco), se crea. La
  // clave del cliente es el CONTENIDO: tras un reinicio se recupera la misma.
  if(j->remoteId[0]){
    snprintf(path, sizeof(path), "/uploads/%s", j->remoteId);
    st = apiCall("GET", path, nullptr, nullptr, 0, nullptr, nullptr, code, sizeof(code), &n);
    bool parsed = st == 200 && fclParseUpload(gJson, n, &gRun.up);
    if(parsed && !strcmp(gRun.up.state, "active")){ gRun.resumedSession = true; goto have; }
    if(parsed && !strcmp(gRun.up.state, "completed") && gRun.up.fileId[0]){ gRun.stage = RS_COMPLETE; return; }
    if(parsed && !strcmp(gRun.up.state, "completing")){ stepFail(j, 503, "upload_busy"); return; }   // la nube esta uniendo las partes
    if(st != 200 && st != 404 && st != 410){ stepFail(j, st, code); return; }
    // Fallida, abortada, caducada o desconocida: sesion nueva.
    lock(); j->remoteId[0] = 0; unlock();
  }
  {
    char esc[2 * FCL_NAME_MAX], body[3 * FCL_NAME_MAX + 320];
    if(!fclJsonEscape(j->name, esc, sizeof(esc))){ jobFinish(j, FCL_JOB_FAILED, "name_invalid"); return; }
    int L = snprintf(body, sizeof(body), "{\"name\":\"%s\",\"size\":%llu,\"sha256\":\"%s\",\"chunkSize\":%lu,\"clientKey\":\"p4:%s:%llu\"",
                     esc, (unsigned long long)j->size, j->sha256, (unsigned long)gRun.chunk, j->sha256, (unsigned long long)j->size);
    if(j->parentId[0]) L += snprintf(body + L, sizeof(body) - (size_t)L, ",\"parentId\":\"%s\"", j->parentId);
    snprintf(body + L, sizeof(body) - (size_t)L, "}");
    st = apiCall("POST", "/uploads", body, nullptr, 0, nullptr, nullptr, code, sizeof(code), &n);
    if(st != 200 && st != 201){ stepFail(j, st, code); return; }
    if(!fclParseUpload(gJson, n, &gRun.up)){ stepFail(j, 500, "bad_response"); return; }
    lock(); snprintf(j->remoteId, sizeof(j->remoteId), "%s", gRun.up.uploadId); gJournalDirty = true; unlock();
    saveJournal();
    Serial.printf("[CLOUD] subida #%lu: sesion %s (%lu de %lu partes ya en la nube)\n", (unsigned long)j->id, gRun.up.uploadId,
                  (unsigned long)gRun.up.receivedCount, (unsigned long)gRun.up.totalParts);
  }
have:
  if(gRun.up.chunkSize != gRun.chunk || gRun.up.totalParts != gRun.parts || gRun.up.size != j->size){
    // Otro plan de partes que el de las huellas: una sesion vieja se descarta y
    // se pide otra; si la recien creada tampoco cuadra, el servidor no es fiable.
    Serial.printf("[CLOUD] subida #%lu: la sesion %s no cuadra con el archivo\n", (unsigned long)j->id, gRun.up.uploadId);
    if(gRun.resumedSession){ lock(); j->remoteId[0] = 0; gJournalDirty = true; unlock(); gRun.stage = RS_CREATE; return; }
    jobFinish(j, FCL_JOB_FAILED, "bad_response");
    return;
  }
  gRun.next = 1;
  gRun.done = gRun.up.receivedBytes;
  rtDone(j, gRun.done);
  gRun.stage = RS_PARTS;
}

static void upPart(FclJob* j){
  while(gRun.next <= gRun.parts && fclUploadHasPart(&gRun.up, gRun.next)) gRun.next++;
  if(gRun.next > gRun.parts){ gRun.stage = RS_COMPLETE; return; }
  rtPhase(j, FCX_RUNNING);
  uint32_t n = gRun.next;
  uint64_t off = (uint64_t)(n - 1) * gRun.chunk;
  size_t len = (size_t)((n < gRun.parts) ? gRun.chunk : j->size - off);
  if(!gRun.f) gRun.f = flexFsOpenRead(j->localPath);
  if(!gRun.f || !flexFsStreamSeek(gRun.f, (uint32_t)off)){ jobFinish(j, FCL_JOB_FAILED, "local_missing"); return; }
  char hx[FCL_SHA_HEX];
  static const char H[] = "0123456789abcdef";
  for(int k = 0; k < 32; k++){ uint8_t b = gRun.partSha[(size_t)(n - 1) * 32u + k]; hx[2 * k] = H[b >> 4]; hx[2 * k + 1] = H[b & 15]; }
  hx[64] = 0;
  char path[96], code[FCL_CODE_MAX];
  snprintf(path, sizeof(path), "/uploads/%s/parts/%lu", j->remoteId, (unsigned long)n);
  PartStream ps(gRun.f, len);
  int st = apiCall("PUT", path, nullptr, &ps, len, "X-Part-SHA256", hx, code, sizeof(code));
  if(ps.failed()){ jobFinish(j, FCL_JOB_FAILED, "local_io"); return; }
  if(st == 200){
    gRun.up.parts[(n - 1) / 32] |= 1u << ((n - 1) % 32);
    gRun.done += len; gRun.next++;
    lock(); gRt[jobIndex(j)].fails = 0; unlock();
    rtDone(j, gRun.done);
    return;
  }
  if(code[0] && (!strcmp(code, "upload_expired") || !strcmp(code, "upload_not_found"))){
    lock(); j->remoteId[0] = 0; gJournalDirty = true; unlock();
    runnerDropIo(); gRun.stage = RS_CREATE; return;
  }
  if(code[0] && !strcmp(code, "upload_state")){ stepFail(j, 503, "upload_busy"); return; }
  if(code[0] && !strcmp(code, "part_conflict")){ jobFinish(j, FCL_JOB_FAILED, "local_changed"); return; }
  stepFail(j, st, code);
}

static void upComplete(FclJob* j){
  rtPhase(j, FCX_VERIFYING);
  runnerDropIo();
  char path[96], body[96], code[FCL_CODE_MAX]; size_t n = 0;
  snprintf(path, sizeof(path), "/uploads/%s/complete", j->remoteId);
  snprintf(body, sizeof(body), "{\"sha256\":\"%s\"}", j->sha256);
  int st = apiCall("POST", path, body, nullptr, 0, nullptr, nullptr, code, sizeof(code), &n);
  if(st == 409 && !strcmp(code, "incomplete_upload")){ gRun.stage = RS_CREATE; return; }   // se vuelve a preguntar
  if(st == 409 && !strcmp(code, "upload_state")){ stepFail(j, 503, "upload_busy"); return; }
  if(st == 422 && !strcmp(code, "checksum_mismatch")){
    // Cada parte llego con su huella correcta y aun asi el total no cuadra: el
    // original cambio a mitad. La nube ya descarto esa sesion.
    lock(); j->remoteId[0] = 0; j->sha256[0] = 0; unlock();
    jobFinish(j, FCL_JOB_FAILED, "local_changed");
    return;
  }
  if(st != 200){ stepFail(j, st, code); return; }
  FclUpload u;
  if(!fclParseUpload(gJson, n, &u) || !u.fileId[0]){ stepFail(j, 500, "bad_response"); return; }
  // LA CONDICION PARA BORRAR LO LOCAL: la nube confirma el archivo y su
  // SHA-256 es el mismo que se calculo aqui sobre el original.
  if(strcmp(u.sha256, j->sha256)){ jobFinish(j, FCL_JOB_FAILED, "checksum_mismatch"); return; }
  lock(); snprintf(j->fileId, sizeof(j->fileId), "%s", u.fileId); unlock();
  rtDone(j, j->size);
  gRun.verifyOpen = false; gRun.localChanged = false;
  gRun.stage = (j->flags & FCL_JF_FREE_LOCAL) ? RS_VERIFY_LOCAL : RS_THUMB;
}

// "Subir y liberar espacio": ANTES de avisar a la interfaz (que borrara el
// original) se vuelve a leer el original y su SHA-256 tiene que seguir siendo
// el que la nube acaba de confirmar. Si alguien lo cambio entre medias, o no se
// puede leer, se conserva. Por tramos, como las huellas.
static void upVerifyLocal(FclJob* j){
  rtPhase(j, FCX_VERIFYING);
  if(!gRun.verifyOpen){
    gRun.f = flexFsOpenRead(j->localPath);
    if(!gRun.f || flexFsStreamSize(gRun.f) != j->size){ gRun.localChanged = true; runnerDropIo(); gRun.stage = RS_THUMB; return; }
    fclShaStart(&gRun.sha);
    gRun.verifyLeft = j->size; gRun.verifyOpen = true;
  }
  uint32_t budget = HASH_STEP;
  while(budget && gRun.verifyLeft){
    size_t want = gRun.verifyLeft < IO_CAP ? (size_t)gRun.verifyLeft : IO_CAP;
    if(want > budget) want = budget;
    int r = flexFsStreamRead(gRun.f, gIo, want);
    if(r <= 0){ gRun.localChanged = true; gRun.verifyLeft = 0; break; }
    fclShaUpdate(&gRun.sha, gIo, (size_t)r);
    gRun.verifyLeft -= (uint64_t)r;
    budget = budget > (uint32_t)r ? budget - (uint32_t)r : 0;
  }
  if(gRun.verifyLeft) return;                                   // sigue en la proxima vuelta
  char hex[FCL_SHA_HEX]; fclShaFinishHex(&gRun.sha, hex);
  if(strcmp(hex, j->sha256)) gRun.localChanged = true;
  runnerDropIo();
  gRun.verifyOpen = false;
  gRun.stage = RS_THUMB;
}

// La miniatura que ya hizo la biblioteca (JPEG de 132 px) viaja como objeto
// aparte. Opcional: si falla, el archivo ya esta subido igual.
static void upThumb(FclJob* j){
  if(j->mlId && (j->flags & FCL_JF_FROM_LIBRARY)){
    char tp[FCL_PATH_MAX];
    snprintf(tp, sizeof(tp), FML_DIR_THUMB "/%lu.jpg", (unsigned long)j->mlId);
    uint32_t sz = flexFsSize(tp);
    if(sz > 0 && sz <= THUMB_MAX){
      FlexFsStream* f = flexFsOpenRead(tp);
      if(f){
        char path[96], code[FCL_CODE_MAX];
        snprintf(path, sizeof(path), "/files/%s/thumbnail", j->fileId);
        PartStream ps(f, sz);
        apiCall("PUT", path, nullptr, &ps, sz, "Content-Type", "image/jpeg", code, sizeof(code));
        flexFsStreamClose(f);
      }
    }
  }
  jobFinish(j, FCL_JOB_DONE, nullptr);
}

// ---- Descarga ----------------------------------------------------------
static void dlPrepare(FclJob* j){
  rtPhase(j, FCX_PREPARING);
  char tp[FCL_PATH_MAX]; dlTempPath(j, tp, sizeof(tp));
  if(!j->localPath[0]){ lock(); snprintf(j->localPath, sizeof(j->localPath), "%s", tp); gJournalDirty = true; unlock(); saveJournal(); }
  uint64_t have = flexFsExists(tp) ? flexFsSize(tp) : 0;
  if(have > j->size){ flexFsDelete(tp); have = 0; }
  uint64_t total = flexFsTotalBytes(), used = flexFsUsedBytes();
  uint64_t freeB = total > used ? total - used : 0;
  if(j->size - have + LOCAL_RESERVE > freeB){ jobFinish(j, FCL_JOB_FAILED, "no_space_local"); return; }
  // Lo ya bajado entra en la huella: se reanuda sin volver a pedirlo.
  fclShaStart(&gRun.sha);
  if(have){
    FlexFsStream* f = flexFsOpenRead(tp);
    uint64_t left = have;
    while(f && left){
      int r = flexFsStreamRead(f, gIo, left < IO_CAP ? (size_t)left : IO_CAP);
      if(r <= 0) break;
      fclShaUpdate(&gRun.sha, gIo, (size_t)r); left -= (uint64_t)r;
    }
    flexFsStreamClose(f);
    if(left){ flexFsDelete(tp); have = 0; fclShaStart(&gRun.sha); }
  }
  gRun.done = have;
  rtDone(j, have);
  if(have == j->size){ gRun.stage = RS_DL_VERIFY; return; }
  if(!netUsable()){ stepFail(j, -1, "network"); return; }
  if(!roomForConn()){ stepFail(j, -1, "no_memory"); return; }
  char bearer[64], why[FCL_CODE_MAX] = "", base[160];
  if(!destBearer(bearer, sizeof(bearer), why, sizeof(why)) || !destBase(base, sizeof(base))){
    memset(bearer, 0, sizeof(bearer));
    if(phoneDest()) classifyFail(!strcmp(why, "phone_rejected") ? 403 : -1, why);
    stepFail(j, -1, "network"); return;
  }
  gRun.f = have ? flexFsOpenAppend(tp) : flexFsOpenWrite(tp);
  if(!gRun.f){ memset(bearer, 0, sizeof(bearer)); jobFinish(j, FCL_JOB_FAILED, "no_space_local"); return; }
  // Peticion con rango. HTTP/1.0: cuerpo sin trocear, lectura directa.
  gRun.dlCli = newClient();                        // con Internet, TLS verificado: aqui tambien viaja la credencial
  gRun.dl = new HTTPClient();
  gRun.dl->setTimeout(HTTP_TIMEOUT);
  gRun.dl->setConnectTimeout(HTTP_TIMEOUT);
  gRun.dl->setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  gRun.dl->useHTTP10(true);
  char url[256];
  snprintf(url, sizeof(url), "%s/download/%s", base, j->remoteId);
  if(!gRun.dl->begin(*gRun.dlCli, url)){ memset(bearer, 0, sizeof(bearer)); stepFail(j, -1, "network"); return; }
  char hdr[80];
  snprintf(hdr, sizeof(hdr), "Bearer %s", bearer); memset(bearer, 0, sizeof(bearer));
  gRun.dl->addHeader("Authorization", hdr); memset(hdr, 0, sizeof(hdr));
  gRun.dl->addHeader("Accept-Encoding", "identity");
  if(have){
    snprintf(hdr, sizeof(hdr), "bytes=%llu-", (unsigned long long)have);
    gRun.dl->addHeader("Range", hdr);
    // Si el archivo cambio en la nube, el servidor manda el nuevo entero (200).
    if(j->sha256[0]){ char ir[72]; snprintf(ir, sizeof(ir), "\"%s\"", j->sha256); gRun.dl->addHeader("If-Range", ir); }
  }
  int st = gRun.dl->GET();
  if(st == 200 && have){
    // Nunca se mezclan dos versiones: se empieza de cero.
    flexFsStreamClose(gRun.f); gRun.f = flexFsOpenWrite(tp);
    fclShaStart(&gRun.sha); gRun.done = have = 0;
    if(!gRun.f){ jobFinish(j, FCL_JOB_FAILED, "no_space_local"); return; }
  }
  if(phoneDest()) flexStorageNoteResult(st);
  if(st != 200 && st != 206){
    if(st == 401) destRejected();
    stepFail(j, st, st == 404 ? "not_found" : st == 401 ? "auth_required" : st < 0 ? "network" : "server");
    return;
  }
  int cl = gRun.dl->getSize();
  gRun.dlLeft = j->size - have;
  if(cl >= 0 && (uint64_t)cl != gRun.dlLeft){
    // Otro tamano que el de la lista: el archivo cambio. Se pide la lista de nuevo.
    stepFail(j, 409, "file_changed");
    return;
  }
  rtPhase(j, FCX_RUNNING);
  gRun.stage = RS_DL_GET;
}

static void dlChunk(FclJob* j){
  // Hasta 256 KB por vuelta: la tarea vuelve a atender a la interfaz entre medias.
  WiFiClient* s = gRun.dl ? gRun.dl->getStreamPtr() : nullptr;
  if(!s){ stepFail(j, -1, "network"); return; }
  uint32_t budget = 256u * 1024u, waited = 0;
  while(budget && gRun.dlLeft){
    int av = s->available();
    if(av <= 0){
      if(!s->connected() || waited > HTTP_TIMEOUT){ rtDone(j, gRun.done); stepFail(j, -1, "network"); return; }
      vTaskDelay(pdMS_TO_TICKS(5)); waited += 5;
      continue;
    }
    size_t want = (size_t)av;
    if(want > IO_CAP) want = IO_CAP;
    if(want > gRun.dlLeft) want = (size_t)gRun.dlLeft;
    int r = s->read(gIo, want);
    if(r <= 0){ rtDone(j, gRun.done); stepFail(j, -1, "network"); return; }
    if(!flexFsStreamWrite(gRun.f, gIo, (size_t)r)){ jobFinish(j, FCL_JOB_FAILED, "no_space_local"); return; }
    fclShaUpdate(&gRun.sha, gIo, (size_t)r);
    gRun.done += (uint64_t)r; gRun.dlLeft -= (uint64_t)r;
    budget = budget > (uint32_t)r ? budget - (uint32_t)r : 0;
    waited = 0;
  }
  lock(); gRt[jobIndex(j)].fails = 0; unlock();
  rtDone(j, gRun.done);
  if(!gRun.dlLeft) gRun.stage = RS_DL_VERIFY;
}

static void dlVerify(FclJob* j){
  rtPhase(j, FCX_VERIFYING);
  runnerDropIo();
  char hex[FCL_SHA_HEX]; fclShaFinishHex(&gRun.sha, hex);
  char tp[FCL_PATH_MAX]; dlTempPath(j, tp, sizeof(tp));
  if(!j->sha256[0] || strcmp(hex, j->sha256)){
    // Sin huella de la nube no hay forma de saber que llego entero: no se da por bueno.
    flexFsDelete(tp);
    jobFinish(j, FCL_JOB_FAILED, "checksum_mismatch");
    return;
  }
  // Se queda con la extension del nombre en la nube ("<id>.jpg"): la
  // biblioteca de medios y el visor deciden por ella. Si no se puede mover, el
  // .part verificado vale igual.
  char local[64], fin[FCL_PATH_MAX];
  fclLocalName(j->name, local, sizeof(local));
  const char* dot = strrchr(local, '.');
  snprintf(fin, sizeof(fin), "%s/%lu%s", dlDir(), (unsigned long)j->id, dot ? dot : "");
  if(strcmp(fin, tp)){ flexFsDelete(fin); if(!flexFsMove(tp, fin)) snprintf(fin, sizeof(fin), "%s", tp); }
  lock(); snprintf(j->localPath, sizeof(j->localPath), "%s", fin); unlock();
  jobFinish(j, FCL_JOB_DONE, nullptr);
}

// Lo que dejo pendiente una cancelacion: avisar a la nube de que suelte la
// reserva de una subida (DELETE /uploads/:id) o borrar el temporal de una
// descarga. Va antes que el trabajo siguiente. true = hizo algo de red.
static bool cancelHousekeeping(){
  if(!gJ) return false;
  FclJob* pick = nullptr;
  lock();
  for(int i = 0; i < FCL_JOBS_MAX && !pick; i++){
    FclJob* x = &gJ->jobs[i];
    if(x->state != FCL_JOB_CANCELLED || !(x->flags & FCL_JF_ABORT) || x == gRun.job) continue;
    if(gRt[i].retryAt && (int32_t)(millis() - gRt[i].retryAt) < 0) continue;
    pick = x;
  }
  unlock();
  if(!pick) return false;
  int i = jobIndex(pick);
  bool done = true, net = false;
  if(pick->type == FCL_JOB_DOWNLOAD){
    char tp[FCL_PATH_MAX]; dlTempPath(pick, tp, sizeof(tp)); flexFsDelete(tp);
  } else if(pick->remoteId[0]){
    char path[96], code[FCL_CODE_MAX];
    snprintf(path, sizeof(path), "/uploads/%s", pick->remoteId);
    int st = apiCall("DELETE", path, nullptr, nullptr, 0, nullptr, nullptr, code, sizeof(code));
    net = true;
    // Ya no existe, ya termino o ya caduco: la reserva no esta. Otra cosa: luego.
    done = st == 200 || st == 404 || st == 410 || (st == 409 && !strcmp(code, "upload_state"));
    if(!done){ lock(); if(gRt[i].fails < 250) gRt[i].fails++; gRt[i].retryAt = millis() + fclBackoffMs(gRt[i].fails); unlock(); }
    else Serial.printf("[CLOUD] subida #%lu cancelada: la nube solto su reserva\n", (unsigned long)pick->id);
  }
  if(done){
    lock();
    pick->flags &= (uint8_t)~FCL_JF_ABORT;
    pick->remoteId[0] = 0;
    if(pick->flags & FCL_JF_CLEARED){ memset(pick, 0, sizeof(*pick)); }
    memset(&gRt[i], 0, sizeof(gRt[i])); gRt[i].phase = FCX_CANCELLED;
    gJournalDirty = true; gStatus.xferGen++;
    unlock();
    saveJournal();
  }
  return net || done;
}

static bool jobStep(){
  if(!gJ) return false;
  FclJob* j = gRun.job;
  if(!j){
    memset(&gRun, 0, sizeof(gRun));                          // libre: no queda nada abierto
    lock();
    // El primero de la cola que no este esperando un reintento.
    FclJob* best = nullptr;
    for(int i = 0; i < FCL_JOBS_MAX; i++){
      FclJob* x = &gJ->jobs[i];
      if(x->state != FCL_JOB_QUEUED && x->state != FCL_JOB_ACTIVE) continue;
      if(gRt[i].retryAt && (int32_t)(millis() - gRt[i].retryAt) < 0) continue;
      if(!best || x->id < best->id) best = x;
    }
    if(best){ best->state = FCL_JOB_ACTIVE; gStatus.xferGen++; gRun.job = best; }
    unlock();
    if(!best) return false;
    j = best;
    gRun.stage = j->type == FCL_JOB_UPLOAD ? RS_PREP : RS_DL_PREP;
    Serial.printf("[CLOUD] %s #%lu: %s\n", j->type == FCL_JOB_UPLOAD ? "subida" : "descarga", (unsigned long)j->id, j->name);
  }
  int i = jobIndex(j);
  lock(); uint8_t state = j->state; uint32_t retryAt = gRt[i].retryAt; unlock();
  if(state != FCL_JOB_ACTIVE){
    // Cancelado (o cualquier cosa que no sea "en curso"): se suelta todo. Lo que
    // haya que avisar a la nube lo hace cancelHousekeeping.
    if(j->type == FCL_JOB_DOWNLOAD){ char tp[FCL_PATH_MAX]; dlTempPath(j, tp, sizeof(tp)); flexFsDelete(tp); }
    runnerClose();
    lock();
    if((j->flags & FCL_JF_CLEARED) && !(j->flags & FCL_JF_ABORT)){ memset(j, 0, sizeof(*j)); memset(&gRt[i], 0, sizeof(gRt[i])); }
    gJournalDirty = true;
    unlock();
    saveJournal(); return true;
  }
  // Esperando un reintento: el trabajo conserva su turno y sus huellas (rehacer
  // las de 160 MB en cada corte de red seria mucho peor que esperar).
  if(retryAt && (int32_t)(millis() - retryAt) < 0) return false;
  switch(gRun.stage){
    case RS_PREP:      upPrepare(j); break;
    case RS_HASH:      upHash(j); break;
    case RS_CREATE:    upCreate(j); break;
    case RS_PARTS:     upPart(j); break;
    case RS_COMPLETE:  upComplete(j); break;
    case RS_VERIFY_LOCAL: upVerifyLocal(j); break;
    case RS_THUMB:     upThumb(j); break;
    case RS_DL_PREP:   dlPrepare(j); break;
    case RS_DL_GET:    dlChunk(j); break;
    case RS_DL_VERIFY: dlVerify(j); break;
    default: runnerClose(); break;
  }
  return true;
}

// ---- Foto para el visor: a la RAM, nunca a la flash ----------------------
// Antes se bajaba entera a LittleFS (/System/Cloud/view): un borrado de sector por cada 4 KB, y cada borrado
// APAGA LA CACHE mientras dura. Con el panel DSI refrescandose desde la PSRAM, eso era el parpadeo cian de
// 10-15 s al abrir una foto (docs/FLEX-MEDIA-ECOSYSTEM.md §11), mas desgaste de la flash en cada foto vista.
// Ahora la foto va a un buffer de PSRAM con UN duenyo en cada momento:
//   · la tarea de la nube lo reserva, lo llena y verifica su SHA-256 (fetchView);
//   · queda en la ranura hasta que quien la pidio la TOMA (flexCloudViewTake): desde ahi es suyo;
//   · si nadie la toma (se cerro el visor, se pidio otra) se suelta aqui, nunca queda colgando.
// Hay UNA peticion viva (gViewWant): pedir otra anula la anterior, y la descarga en curso lo ve en cada
// lectura y se corta. Nada de esto toca la flash.
static const uint32_t VIEW_DEADLINE_MS = 60000;       // plazo TOTAL de una foto: no puede bloquear la nube para siempre
static const uint32_t VIEW_HEADROOM    = FLEX_CLOUD_VIEW_HEADROOM;   // PSRAM que debe quedar libre ademas del buffer (lo que decodifica el visor)
struct ViewSlot { uint32_t op; uint8_t state; uint32_t got, total; uint8_t* buf; char err[96]; };
static ViewSlot gView;                                // bajo lock()
static volatile uint32_t gViewWant = 0;               // la peticion que el visor quiere AHORA (0 = ninguna)

// Suelta lo que hubiera en la ranura (con lock() tomado).
static void viewDropLocked(){
  if(gView.buf){ psFree(gView.buf); gView.buf = nullptr; }
  memset(&gView, 0, sizeof(gView));
}
static void viewFail(uint32_t op, const char* code){
  lock();
  if(gView.op == op && gViewWant == op){
    if(gView.buf){ psFree(gView.buf); gView.buf = nullptr; }
    gView.state = FCV_FAILED;
    snprintf(gView.err, sizeof(gView.err), "%s", errText(code));
  }
  unlock();
}
static void viewProgress(uint32_t op, uint32_t got){
  lock(); if(gView.op == op && gView.state == FCV_FETCHING) gView.got = got; unlock();
}
// El SHA-256 que el servidor dice de lo que ENVIA, de su cabecera ETag ("<64 hex>" entre comillas). "" si no hay.
static void viewEtagSha(const String& et, char* out, size_t cap){
  out[0] = 0;
  const char* p = et.c_str();
  if(*p == 'W' && p[1] == '/') p += 2;
  if(*p == '"') p++;
  size_t n = 0;
  while(p[n] && p[n] != '"' && n < cap) n++;
  if(n != FCL_SHA_HEX - 1) return;
  for(size_t i = 0; i < n; i++){ char ch = p[i]; if(!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return; }
  memcpy(out, p, n); out[n] = 0;
}

static void fetchView(const Cmd& c){
  const uint32_t op = c.opId;
  if(gViewWant != op) return;                                   // ya no se quiere: ni red ni memoria
  if(c.size == 0 || c.size > VIEW_MAX){ viewFail(op, "file_too_large"); return; }
  // Sin sitio para decodificarla DESPUES: se dice ya, sin gastar red (si el servidor la cambio, se vuelve a medir con lo que envia).
  if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < (size_t)c.size + VIEW_HEADROOM){ viewFail(op, "no_memory"); return; }
  char bearer[64], why[FCL_CODE_MAX] = "", base[160];
  if(!netUsable() || !destBearer(bearer, sizeof(bearer), why, sizeof(why)) || !destBase(base, sizeof(base))){
    memset(bearer, 0, sizeof(bearer));
    if(phoneDest() && why[0]) classifyFail(!strcmp(why, "phone_rejected") ? 403 : -1, why);
    viewFail(op, "network");
    return;
  }
  if(!roomForConn()){ memset(bearer, 0, sizeof(bearer)); viewFail(op, "no_memory"); return; }
  WiFiClient* cli = newClient(); HTTPClient h;
  h.setTimeout(HTTP_TIMEOUT); h.useHTTP10(true);
  static const char* KEYS[] = { "ETag" };               // (collectHeaders pide const char*[], no const char* const[])
  h.collectHeaders(KEYS, 1);
  char url[256], hdr[80];
  if(c.play) snprintf(url, sizeof(url), "%s/files/%s/playable", base, c.id);   // preview o version del perfil, no el original
  else       snprintf(url, sizeof(url), "%s/download/%s", base, c.id);
  bool ok = h.begin(*cli, url);
  snprintf(hdr, sizeof(hdr), "Bearer %s", bearer); memset(bearer, 0, sizeof(bearer));
  if(ok){ h.addHeader("Authorization", hdr); h.addHeader("Accept-Encoding", "identity"); }
  memset(hdr, 0, sizeof(hdr));
  const int st = ok ? h.GET() : -1;

  // Lo que el servidor ENVIA manda sobre lo que la lista recordaba: una foto grande puede haber ganado una vista
  // ligera (otro tamano y otro SHA-256) DESPUES de que la lista se pidiera. Se comprueba ANTES de reservar nada.
  uint64_t total = c.size;
  char wantSha[FCL_SHA_HEX]; snprintf(wantSha, sizeof(wantSha), "%s", c.sha);
  const char* code = nullptr;
  if(st == 200){
    const int cl = h.getSize();
    if(cl > 0 && (uint64_t)cl != c.size){
      char es[FCL_SHA_HEX]; viewEtagSha(h.header("ETag"), es, sizeof(es));
      if(es[0] && (uint64_t)cl <= VIEW_MAX){ total = (uint64_t)cl; snprintf(wantSha, sizeof(wantSha), "%s", es); }
      else code = "file_changed";                              // no se puede verificar lo distinto: se refresca la lista
    }
  } else code = st == 401 ? "auth_required" : st == 404 || st == 410 ? "not_found" : st == 409 ? "not_ready" : st > 0 ? "server" : "network";
  uint8_t* buf = nullptr;
  if(!code){
    const uint32_t need = (uint32_t)total;
    if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < (size_t)need + VIEW_HEADROOM) code = "no_memory";
    else { buf = (uint8_t*)heap_caps_malloc(need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); if(!buf) code = "no_memory"; }
  }
  FclSha sha; fclShaStart(&sha);
  uint64_t got = 0;
  bool cancelled = false, timedOut = false;
  if(!code){
    lock(); if(gView.op == op){ gView.total = (uint32_t)total; } unlock();
    WiFiClient* s = h.getStreamPtr();
    const uint32_t t0 = millis();
    uint32_t waited = 0, lastPub = 0;
    while(got < total && s){
      if(gViewWant != op){ cancelled = true; break; }           // el visor lo cerro / pidio otra: se corta YA
      if(millis() - t0 > VIEW_DEADLINE_MS){ timedOut = true; break; }
      int av = s->available();
      if(av <= 0){ if(!s->connected() || waited > HTTP_TIMEOUT) break; vTaskDelay(pdMS_TO_TICKS(2)); waited += 2; continue; }
      uint64_t left = total - got;
      size_t want = (size_t)(av > (int)IO_CAP ? IO_CAP : (size_t)av);
      if((uint64_t)want > left) want = (size_t)left;
      int r = s->read(buf + got, want);
      if(r <= 0) break;
      fclShaUpdate(&sha, buf + got, (size_t)r); got += (uint64_t)r; waited = 0;
      if(got - lastPub >= 32768u){ lastPub = (uint32_t)got; viewProgress(op, (uint32_t)got); }
    }
  }
  h.end();
  cli->stop(); delete cli;
  if(phoneDest()) flexStorageNoteResult(st);
  if(st == 401) destRejected();
  if(cancelled){ psFree(buf); return; }                         // nadie espera ya el resultado
  if(!code){
    char hex[FCL_SHA_HEX]; fclShaFinishHex(&sha, hex);
    if(timedOut) code = "view_timeout";
    else if(got != total) code = "network";
    else if(wantSha[0] && strcmp(hex, wantSha)) code = "view_checksum";     // (la foto no se reintenta sola: el usuario la vuelve a abrir)
  }
  if(code){
    psFree(buf); viewFail(op, code);
    if(!strcmp(code, "not_found") || !strcmp(code, "file_changed") || !strcmp(code, "not_ready")) gListStale = true;   // lo que la lista decia ya no es cierto
    return;
  }
  bool published = false;
  lock();
  if(gView.op == op && gViewWant == op && gView.state == FCV_FETCHING){
    gView.buf = buf; gView.got = (uint32_t)got; gView.total = (uint32_t)total; gView.state = FCV_READY;
    published = true;
  }
  unlock();
  if(!published) psFree(buf);                                   // lo anularon justo ahora
}

// ======================================================================
//  Miniaturas
// ======================================================================
struct ThumbDec { uint16_t* dst; FlexJpegInfo* info; int s, x0, y0, nextT; bool init; };
static bool thumbRow(void* u, int y, int w, const uint16_t* rgb){
  ThumbDec* d = (ThumbDec*)u;
  const int S = FLEX_CLOUD_THUMB_SIDE;
  if(!d->init){
    // Las medidas REALES de salida (ya escalada) las dice el decodificador.
    int ow = d->info->outWidth, oh = d->info->outHeight;
    if(ow <= 0 || oh <= 0) return false;
    d->s = ow < oh ? ow : oh; d->x0 = (ow - d->s) / 2; d->y0 = (oh - d->s) / 2; d->init = true;
  }
  // Recorte cuadrado centrado, como las de la biblioteca local.
  while(d->nextT < S){
    int sy = d->y0 + (int)((int64_t)d->nextT * d->s / S);
    if(sy > y) break;
    if(sy == y){
      uint16_t* row = d->dst + (size_t)d->nextT * S;
      for(int t = 0; t < S; t++){
        int sx = d->x0 + (int)((int64_t)t * d->s / S);
        row[t] = rgb[sx < w ? sx : w - 1];
      }
    }
    d->nextT++;
  }
  return d->nextT < S;
}

static void fetchThumb(){
  char id[FCL_ID_MAX] = "";
  int slot = -1;
  lock();
  uint32_t best = 0;
  for(int i = 0; i < FLEX_CLOUD_THUMBS; i++){
    Thumb& t = gThumbs[i];
    bool retry = t.state == TH_FAILED && (millis() - t.failMs) > 60000u;
    if((t.state == TH_WANTED || retry) && t.lru >= best){ best = t.lru; slot = i; }
  }
  if(slot >= 0){ snprintf(id, sizeof(id), "%s", gThumbs[slot].id); gThumbs[slot].state = TH_WANTED; }
  unlock();
  if(slot < 0) return;
  char path[96], code[FCL_CODE_MAX]; size_t n = 0;
  snprintf(path, sizeof(path), "/files/%s/thumbnail", id);
  int st = apiCall("GET", path, nullptr, nullptr, 0, nullptr, nullptr, code, sizeof(code), &n);
  uint16_t* px = nullptr;
  bool ok = false;
  if(st == 200 && n > 4 && n <= THUMB_MAX){
    FlexJpegInfo info;
    if(flexJpegProbe((const uint8_t*)gJson, n, &info) == FLEXJPG_OK && info.width > 0 && info.height > 0 && !info.progressive){
      // El mayor divisor que deja el lado corto en >= 132 px: menos trabajo, mismo resultado.
      int mn = info.width < info.height ? info.width : info.height, d = 1;
      for(int c = 8; c >= 2; c >>= 1) if(mn / c >= FLEX_CLOUD_THUMB_SIDE){ d = c; break; }
      int ow = (info.width + d - 1) / d, oh = (info.height + d - 1) / d;
      px = (uint16_t*)psAlloc((size_t)FLEX_CLOUD_THUMB_SIDE * FLEX_CLOUD_THUMB_SIDE * 2);
      if(px){
        FlexJpegInfo out;
        ThumbDec dec = { px, &out, 0, 0, 0, 0, false };
        int r = flexJpegDecode((const uint8_t*)gJson, n, ow, oh, 0, &out, thumbRow, &dec, psAlloc, psFree);
        ok = (r == FLEXJPG_OK || r == FLEXJPG_ERR_ABORTED) && dec.nextT >= FLEX_CLOUD_THUMB_SIDE;
      }
    }
  }
  lock();
  Thumb& t = gThumbs[slot];
  if(!strcmp(t.id, id)){
    if(ok){ psFree(t.px); t.px = px; px = nullptr; t.state = TH_READY; }
    else { t.state = TH_FAILED; t.failMs = millis(); }
    gThumbGen++;
  }
  unlock();
  psFree(px);
}

static bool thumbWanted(){
  bool w = false;
  lock();
  for(int i = 0; i < FLEX_CLOUD_THUMBS && !w; i++) w = gThumbs[i].state == TH_WANTED;
  unlock();
  return w;
}

// ======================================================================
//  Cambio de destino (SOLO en la tarea: nadie mas toca el diario ni el runner)
// ======================================================================
// Una orden que no se va a ejecutar se CONTESTA (la interfaz la esperaba): la
// lista se pide de nuevo sola; las operaciones y el visor dicen que no.
static void failCmd(const Cmd& c, const char* why){
  if(c.type == CMD_LIST || c.type == CMD_REFRESH || c.type == CMD_MORE){ listError(why); return; }
  if(c.type == CMD_VIEW){ viewFail(c.opId, why); return; }
  opDone(c.opId, -1, why, "");
}

// El diario de un destino que NO esta cargado: se lee, se cambia y se guarda.
static void editJournalFile(const char* path, void (*fn)(FclJournal*)){
  if(!flexFsReady()) return;
  size_t cap = fclJournalMaxBytes();
  uint8_t* buf = (uint8_t*)psAlloc(cap);
  FclJournal* j = (FclJournal*)psAlloc(sizeof(FclJournal));
  int n = buf ? flexFsReadBin(path, buf, cap) : 0;
  if(j && n > 0){
    bool damaged = false;
    fclJournalInit(j);
    fclJournalDecode(j, buf, (size_t)n, &damaged);
    fn(j);
    size_t m = fclJournalEncode(j, buf, cap);
    if(m && !flexFsWriteBinAtomic(path, buf, m)) Serial.println(F("[CLOUD] no se pudo guardar el diario"));
  }
  psFree(j); psFree(buf);
}
// La cuenta de Internet se desvinculo: lo suyo se cancela como en
// flexCloudCancel() (una subida con sesion deja pendiente soltar su reserva)
// y lo terminado se quita de la lista, como en flexCloudClearFinished().
static void cancelForUnlink(FclJournal* j){
  for(int i = 0; i < FCL_JOBS_MAX; i++){
    FclJob& x = j->jobs[i];
    if(x.state == FCL_JOB_QUEUED || x.state == FCL_JOB_ACTIVE){
      x.state = FCL_JOB_CANCELLED;
      if(x.type == FCL_JOB_DOWNLOAD || x.remoteId[0]) x.flags |= FCL_JF_ABORT;
    }
    bool finished = x.state == FCL_JOB_FAILED || x.state == FCL_JOB_CANCELLED || (x.state == FCL_JOB_DONE && (x.flags & FCL_JF_DELIVERED));
    if(!finished) continue;
    if(x.flags & FCL_JF_ABORT) x.flags |= FCL_JF_CLEARED; else memset(&x, 0, sizeof(x));
  }
}
// El telefono se olvido: con el ya no se puede hablar (nada que soltar en el
// otro lado). Se vacia el diario; el numero siguiente se conserva.
static void wipeJobs(FclJournal* j){ for(int i = 0; i < FCL_JOBS_MAX; i++) memset(&j->jobs[i], 0, sizeof(j->jobs[i])); }

static void applyPending(){
  if(gInetUnlinked){
    gInetUnlinked = false;
    // Con el destino en Internet ya lo hizo flexCloudAccountUnlinked() al momento.
    if(phoneDest()) editJournalFile(JOURNAL_PATH, cancelForUnlink);
  }
  if(gPhoneForgot){
    gPhoneForgot = false;
    if(phoneDest()){
      runnerClose();
      flexCloudStreamClose();
      lock(); wipeJobs(gJ); memset(gRt, 0, sizeof(gRt)); gStatus.xferGen++; unlock();
      saveJournal();
    } else editJournalFile(JOURNAL_PHONE, wipeJobs);
    if(flexFsReady()) flexFsDelete(DL_PHONE);
    Serial.println(F("[CLOUD] telefono olvidado: sus transferencias pendientes se cancelaron"));
  }
}

static void reemitPending();
static void applyDest(){
  uint8_t want = gDestWant;
  if(want == gDest) return;
  Serial.printf("[CLOUD] destino: %s\n", want == FCD_PHONE ? "telefono (Flex Storage)" : "Internet");
  // 1. Lo que estaba en marcha se suelta SIN perderse: vive en el diario de
  //    su destino y en su servidor, y sigue cuando ese destino vuelva.
  runnerClose();
  if(gApi){ gApi->http.end(); gApi->sec.stop(); gApi->plain.stop(); }
  flexCloudStreamClose();
  if(gJournalDirty) saveJournal();
  // 2. Avisos y ordenes del destino anterior. Los avisos de transferencias no
  //    se marcaron entregados: se repiten cuando ese destino vuelva.
  lock(); gEvHead = gEvN = 0; unlock();
  Cmd c;
  while(popCmd(c)) failCmd(c, "dest_changed");
  // 3. El otro destino, desde cero: nada de lo que se ensenaba era suyo.
  lock();
  gDest = want;
  gStatus.net = FCN_CONNECTING;
  snprintf(gStatus.netText, sizeof(gStatus.netText), "%s", want == FCD_PHONE ? "Conectando con el tel\xC3\xA9" "fono" : "Conectando con Flex Cloud");
  gStatus.quotaValid = false; memset(&gStatus.quota, 0, sizeof(gStatus.quota)); gStatus.address[0] = 0;
  gStatus.gen++;
  gListInfo.count = 0; gListInfo.more = false; gListInfo.nCrumbs = 0; gListInfo.folderId[0] = 0;
  gListInfo.state = FCL_LIST_ERROR;                 // la interfaz la pide otra vez al volver la conexion
  snprintf(gListInfo.error, sizeof(gListInfo.error), "%s", gStatus.netText);
  gListInfo.gen++;
  gCursor[0] = 0;
  for(int i = 0; i < FLEX_CLOUD_THUMBS; i++){ psFree(gThumbs[i].px); memset(&gThumbs[i], 0, sizeof(gThumbs[i])); }
  gThumbGen++;
  memset(gRt, 0, sizeof(gRt));
  gStatus.xferGen++;
  unlock();
  gNetFails = 0; gNetRetryAt = 0; gAuthWait = false; gNextMeMs = 0;
  gMeDue = gActive;
  loadJournal();
  reemitPending();
  gViewWant = 0;
  lock(); viewDropLocked(); unlock();                       // la foto que se traia era del destino anterior
}

// ======================================================================
//  Vigilar lo que el telefono esta preparando (ver flexCloudWatch en FlexOS_Cloud.h)
// ======================================================================
// La lista en pantalla es una FOTO de un instante. El telefono tarda segundos -- o minutos -- en analizar y preparar un video para el P4
// (sin analizar -> en cola -> preparando N % -> listo/fallido), y la miniatura y la duracion llegan despues. Sin esto el estado de cada celda
// no se movia hasta reabrir la Galeria. La interfaz dice QUE ids de lo que ve estan sin acabar y la tarea pregunta por UNO cada vez
// (GET /files/<id>): con cambios, pronto; sin ellos, cada vez mas espaciado; y con un tope de tiempo. Nada que vigilar = nada que pedir.
static const uint32_t WATCH_FIRST_MS = 3000, WATCH_MAX_GAP_MS = 15000, WATCH_TOTAL_MS = 300000, WATCH_PROGRESS_MS = 5000;
static char     gWatch[FLEX_CLOUD_WATCH_MAX][FCL_ID_MAX];
static int      gWatchN = 0, gWatchRR = 0;
static bool     gWatchDone = false;                     // se agoto el tiempo para este conjunto (no se reanuda hasta que cambie)
static uint32_t gWatchNextMs = 0, gWatchSinceMs = 0, gWatchGapMs = WATCH_FIRST_MS;
static char     gWatchKick[FCL_ID_MAX] = "";
static FclItem  gWatchItem;                             // de la tarea (nadie mas lo toca): 672 B fuera de su pila

static bool watchDiffers(const FclItem& a, const FclItem& b){
  return a.playState != b.playState || a.playProgress != b.playProgress || a.hasThumb != b.hasThumb || a.durationMs != b.durationMs ||
         a.width != b.width || a.height != b.height || a.playSize != b.playSize || a.size != b.size || a.updatedAt != b.updatedAt ||
         a.kind != b.kind || strcmp(a.name, b.name) || strcmp(a.playSha, b.playSha) || strcmp(a.sha256, b.sha256) ||
         strcmp(a.playReason, b.playReason) || strcmp(a.mime, b.mime);
}

// Una consulta. true = salio a la red (la tarea vuelve a mirar sus ordenes antes de la siguiente).
static bool watchStep(){
  char id[FCL_ID_MAX] = "";
  const uint32_t now = millis();
  lock();
  if(gWatchKick[0]){ snprintf(id, sizeof(id), "%s", gWatchKick); gWatchKick[0] = 0; }
  else if(gWatchN > 0 && !gWatchDone && (int32_t)(now - gWatchNextMs) >= 0){
    if(now - gWatchSinceMs > WATCH_TOTAL_MS) gWatchDone = true;
    else { gWatchRR = (gWatchRR + 1) % gWatchN; snprintf(id, sizeof(id), "%s", gWatch[gWatchRR]); }
  }
  unlock();
  if(!id[0]) return false;
  char path[96], code[FCL_CODE_MAX] = ""; size_t n = 0;
  snprintf(path, sizeof(path), "/files/%s", id);
  const int st = apiCall("GET", path, nullptr, nullptr, 0, nullptr, nullptr, code, sizeof(code), &n);
  bool changed = false, stateMoved = false;
  if(st == 200){
    memset(&gWatchItem, 0, sizeof(gWatchItem));
    if(fclParseItem(gJson, n, &gWatchItem) && !strcmp(gWatchItem.id, id)){
      lock();
      for(int i = 0; i < gListInfo.count; i++){
        if(strcmp(gList[i].id, id)) continue;
        if(watchDiffers(gList[i], gWatchItem)){
          stateMoved = gList[i].playState != gWatchItem.playState;
          gList[i] = gWatchItem; gListInfo.gen++; changed = true;
        }
        break;
      }
      unlock();
      if(gWatchItem.deletedAt && gListInfo.view != FCL_VIEW_TRASH) gListStale = true;     // a la papelera desde otro sitio
    }
  } else if(st == 404 || st == 410) gListStale = true;                                  // ya no existe: la lista ya no es esta
  // El ritmo: una transicion de estado vuelve a empezar; un simple avance, cada 5 s; sin cambios, cada vez mas espaciado.
  lock();
  if(stateMoved){ gWatchGapMs = WATCH_FIRST_MS; gWatchSinceMs = now; }
  else if(changed) gWatchGapMs = WATCH_PROGRESS_MS;
  else { gWatchGapMs = gWatchGapMs * 3u / 2u; if(gWatchGapMs > WATCH_MAX_GAP_MS) gWatchGapMs = WATCH_MAX_GAP_MS; }
  gWatchNextMs = now + gWatchGapMs;
  unlock();
  return true;
}

// ======================================================================
//  Tarea principal
// ======================================================================
static void taskStep(){
  applyPending();
  applyDest();
  refreshNet();
  Cmd c;
  if(popCmd(c)){
    if(netReady()){ if(c.type == CMD_VIEW) fetchView(c); else runCmd(c); }
    else {
      lock(); uint8_t net = gStatus.net; unlock();
      failCmd(c, netWhy(net));
    }
    return;
  }
  if(gJournalDirty) saveJournal();
  if(!netReady()){
    // Los trabajos esperan sin consumir nada; se marca el motivo.
    lock();
    for(int i = 0; i < FCL_JOBS_MAX; i++){
      FclJob& x = gJ->jobs[i];
      if((x.state == FCL_JOB_QUEUED || x.state == FCL_JOB_ACTIVE) && gRt[i].phase != FCX_WAITING_NET){ gRt[i].phase = FCX_WAITING_NET; gStatus.xferGen++; }
    }
    unlock();
    return;
  }
  if(gMeDue || (gActive && (int32_t)(millis() - gNextMeMs) >= 0)){ fetchMe(); return; }
  // Lo que la interfaz ensena quedo viejo (acaba de subirse algo, un archivo cambio): se vuelve a pedir, UNA vez y solo con la nube a la vista.
  if(gListStale && gActive){ gListStale = false; flexCloudRefresh(); return; }
  // Las miniaturas solo se piden con algo en pantalla: no hace falta mirar gActive.
  if(thumbWanted()){ fetchThumb(); return; }
  if(gActive && watchStep()) return;
  if(cancelHousekeeping()) return;
  jobStep();
}

static bool busyNow(){
  lock();
  bool waiting = gRun.job && gRt[jobIndex(gRun.job)].retryAt && (int32_t)(millis() - gRt[jobIndex(gRun.job)].retryAt) < 0;
  bool b = gCmdN > 0 || (gRun.job != nullptr && !waiting);
  for(int i = 0; i < FLEX_CLOUD_THUMBS && !b; i++) b = gThumbs[i].state == TH_WANTED;
  unlock();
  return b && netReady();
}

static void cloudTask(void*){
  for(;;){
    taskStep();
    // Con trabajo pendiente solo se cede el procesador; en reposo se duerme
    // hasta que la interfaz pida algo (o cada medio segundo, para la red).
    if(busyNow()) vTaskDelay(pdMS_TO_TICKS(2));
    else ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
  }
}

// ======================================================================
//  Streaming (tarea propia: un video no espera a una subida)
// ======================================================================
// El streaming NO se da por perdido a la primera: con la Wi-Fi o el telefono fuera un rato (se apaga la pantalla, cambia de punto de
// acceso) se queda "Reconectando..." con una espera acotada y, al volver, sigue donde estaba (la cache y la posicion se conservan). Solo
// tras STREAM_PATIENCE_MS seguidos sin ningun contacto se declara el fallo y el reproductor lo dice.
#define STREAM_PATIENCE_MS  90000u
#define STREAM_BACKOFF_MAX  5000u
struct StNet { WiFiClient* cli; HTTPClient* http; uint32_t pos; uint32_t left; uint32_t gen; uint8_t fails; uint32_t retryAt; uint32_t idleMs; uint32_t firstFailMs; int lastSt; };
static StNet gStNet;

static void stConnClose(){
  if(gStNet.http){ gStNet.http->end(); delete gStNet.http; gStNet.http = nullptr; }
  if(gStNet.cli){ gStNet.cli->stop(); delete gStNet.cli; gStNet.cli = nullptr; }
  gStNet.left = 0;
}

static void stSetState(uint8_t state, const char* err){
  stLock(); gSt.state = state; snprintf(gSt.err, sizeof(gSt.err), "%s", err ? err : ""); stUnlock();
}

static bool stConnOpen(const char* fileId, uint32_t off, uint32_t size, bool play){
  stConnClose();
  gStNet.lastSt = 0;
  if(!roomForConn()) return false;
  char bearer[64], why[FCL_CODE_MAX] = "", base[160];
  if(!destBearer(bearer, sizeof(bearer), why, sizeof(why)) || !destBase(base, sizeof(base))){ memset(bearer, 0, sizeof(bearer)); return false; }
  gStNet.cli = newClient();                     // con Internet, TLS verificado
  gStNet.http = new HTTPClient();
  gStNet.http->setTimeout(HTTP_TIMEOUT);
  gStNet.http->setConnectTimeout(HTTP_TIMEOUT);
  gStNet.http->useHTTP10(true);                 // cuerpo sin trocear: se lee tal cual
  char url[256], hdr[80];
  if(play) snprintf(url, sizeof(url), "%s/files/%s/playable", base, fileId);
  else     snprintf(url, sizeof(url), "%s/download/%s", base, fileId);
  if(!gStNet.http->begin(*gStNet.cli, url)){ memset(bearer, 0, sizeof(bearer)); stConnClose(); return false; }
  snprintf(hdr, sizeof(hdr), "Bearer %s", bearer); memset(bearer, 0, sizeof(bearer));
  gStNet.http->addHeader("Authorization", hdr); memset(hdr, 0, sizeof(hdr));
  gStNet.http->addHeader("Accept-Encoding", "identity");
  snprintf(hdr, sizeof(hdr), "bytes=%lu-", (unsigned long)off);
  gStNet.http->addHeader("Range", hdr);
  int st = gStNet.http->GET();
  gStNet.lastSt = st;
  if(phoneDest()) flexStorageNoteResult(st);
  if(st == 401) destRejected();
  bool ok = st == 206 || (st == 200 && off == 0);
  int cl = ok ? gStNet.http->getSize() : -1;
  if(!ok || (cl >= 0 && (uint32_t)cl != size - off)){
    Serial.printf("[CLOUD] streaming: rango %lu rechazado (HTTP %d)\n", (unsigned long)off, st);
    stConnClose();
    return false;
  }
  gStNet.pos = off; gStNet.left = size - off;
  stLock(); gSt.conns++; stUnlock();
  return true;
}

// Un paso de la tarea de streaming. Devuelve 0 si ha hecho trabajo (un bloque traido: puede haber mas, se sigue ya) o los ms que la
// tarea debe DORMIR hasta que alguien le avise (una lectura nueva, un salto, un rango fijado, el cierre): ya no se sondea cada
// milisegundo -- con la ventana llena no hay nada que traer y la tarea no consume nada hasta que el reproductor avance.
#define STREAM_IDLE_MS    100u      // nada que traer: tope de la espera (red de seguridad; todo cambio de estado avisa)
#define STREAM_CLOSED_MS  1000u     // sin flujo abierto
static uint32_t streamStep(){
  stLock();
  bool open = gSt.open;
  uint32_t gen = gSt.gen, size = gSt.size, off = 0, len = 0;
  bool play = gSt.play;
  char fileId[FCL_ID_MAX]; snprintf(fileId, sizeof(fileId), "%s", gSt.fileId);
  int slot = open ? fclCacheNextFetch(&gSt.cache, &off, &len) : -1;
  uint8_t* dst = slot >= 0 ? fclCacheSlot(&gSt.cache, slot) : nullptr;
  if(slot >= 0) gSt.busy = true;
  stUnlock();
  if(!open){ if(gStNet.http) stConnClose(); return STREAM_CLOSED_MS; }
  if(gStNet.gen != gen){ stConnClose(); gStNet.gen = gen; gStNet.fails = 0; gStNet.retryAt = 0; }
  if(slot < 0){
    // Nada que traer ahora (la ventana por delante esta llena). Una conexion
    // parada mucho rato se cierra; se vuelve a abrir al seguir.
    if(gStNet.http && millis() - gStNet.idleMs > 20000u) stConnClose();
    return STREAM_IDLE_MS;
  }
  auto giveBack = [&](){ stLock(); if(gSt.gen == gen) fclCacheAbort(&gSt.cache, slot); gSt.busy = false; stUnlock(); };
  if(gStNet.retryAt && (int32_t)(millis() - gStNet.retryAt) < 0){
    giveBack();
    const uint32_t left = gStNet.retryAt - millis();                  // lo que falta de la espera (un aviso no se la salta)
    return left < 1u ? 1u : (left > 200u ? 200u : left);
  }
  if(!netUsable()){
    giveBack();
    // Sin Wi-Fi se espera; sin una credencial que sirva NO: esperar "conexion"
    // para siempre era mentir. El reproductor lo dice (FCS_ERROR) y se detiene.
    if(!destUsable()) stSetState(FCS_ERROR, errText(destWhy()));
    else stSetState(FCS_WAITING_NET, errText("no_wifi"));
    return 200u;
  }
  if(!gStNet.http || gStNet.pos != off){
    if(!stConnOpen(fileId, off, size, play)){
      giveBack();
      // El servidor CONTESTO que ya no esta (404/410) o que aun no esta listo (409): reintentar no lo cambia. Se dice YA (antes el
      // reproductor ensenaba "Cargando" 45 s y acababa con "Sin conexion", que no era verdad) y la lista se vuelve a pedir.
      if(gStNet.lastSt == 404 || gStNet.lastSt == 410 || gStNet.lastSt == 409){
        stSetState(FCS_ERROR, errText(gStNet.lastSt == 409 ? "not_ready" : "not_found"));
        gListStale = true;
        gStNet.retryAt = millis() + 2000u;                      // si nadie cierra el flujo, no se martillea al servidor
        return 200u;
      }
      if(!gStNet.fails) gStNet.firstFailMs = millis();
      if(gStNet.fails < 250) gStNet.fails++;
      uint32_t back = gStNet.fails < 4 ? 500u * gStNet.fails : fclBackoffMs(gStNet.fails - 3);
      if(back > STREAM_BACKOFF_MAX) back = STREAM_BACKOFF_MAX;
      gStNet.retryAt = millis() + back;
      bool giveUp = gStNet.fails >= 6 && millis() - gStNet.firstFailMs > STREAM_PATIENCE_MS;
      stSetState(giveUp ? FCS_ERROR : FCS_WAITING_NET, errText(giveUp ? "server" : "network"));
      return back < 200u ? back : 200u;
    }
  }
  // Se rellena el hueco SIN el cerrojo: esta en LOADING y nadie mas lo toca.
  WiFiClient* s = gStNet.http->getStreamPtr();
  uint32_t got = 0, waited = 0;
  const uint32_t blkT0 = millis();                              // el reloj de ESTE bloque (sin contar el conectar)
  bool fail = false;
  while(got < len){
    stLock(); bool same = gSt.gen == gen && gSt.open; stUnlock();
    if(!same){ fail = true; break; }                          // se cerro o se cambio de video
    int av = s ? s->available() : -1;
    if(av <= 0){
      if(!s || !s->connected() || waited > HTTP_TIMEOUT){ fail = true; break; }
      vTaskDelay(pdMS_TO_TICKS(2)); waited += 2; continue;
    }
    uint32_t want = len - got;
    if((uint32_t)av < want) want = (uint32_t)av;
    if(want > 16384u) want = 16384u;
    int r = s->read(dst + got, want);
    if(r <= 0){ fail = true; break; }
    got += (uint32_t)r; waited = 0;
  }
  if(fail){
    giveBack(); stConnClose();
    stLock(); bool same = gSt.gen == gen; stUnlock();
    if(same){
      if(!gStNet.fails) gStNet.firstFailMs = millis();
      if(gStNet.fails < 250) gStNet.fails++;
      uint32_t back = 300u * gStNet.fails; if(back > STREAM_BACKOFF_MAX) back = STREAM_BACKOFF_MAX;
      gStNet.retryAt = millis() + back;
      stSetState(FCS_WAITING_NET, "Reconectando...");
      return back < 200u ? back : 200u;
    }
    return 0;                                                  // se cerro o cambio el video: el siguiente paso lo ve
  }
  gStNet.pos += len; gStNet.left -= len; gStNet.fails = 0; gStNet.idleMs = millis();
  const uint32_t blkMs = millis() - blkT0;
  stLock();
  if(gSt.gen == gen){
    fclCacheCommit(&gSt.cache, slot);
    gSt.bps = fclThroughput(gSt.bps, len, blkMs);
    if(gSt.state != FCS_STREAMING){ gSt.state = FCS_STREAMING; gSt.err[0] = 0; }
  }
  gSt.busy = false;
  stUnlock();
  return 0;                                                    // un bloque mas: puede haber otro por traer
}

// La tarea NO sondea: trabaja mientras haya algo que traer y, si no, duerme hasta que se le avise (stNotify: una lectura que
// cambia de bloque, un salto, un rango fijado, abrir o cerrar). El tope de cada espera es solo una red de seguridad.
static void streamTask(void*){
  for(;;){
    const uint32_t wait = streamStep();
    if(wait) ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait));
  }
}

// ======================================================================
//  Las tareas: se crean al hacer falta, ancladas al core 1 y COMPROBANDO el resultado
// ======================================================================
// Core 1 como el resto de tareas de red del sistema: el 0 es del presentador
// grafico (ver FlexOS_OTA.cpp) y xTaskCreate "a secas" las dejaba migrar a el.
// El resultado SE COMPRUEBA: la pila sale de la SRAM interna y, con la memoria
// justa, la creacion falla; antes ese fallo pasaba en silencio y la nube se
// quedaba sin hilo para siempre. Ahora se dice y se reintenta en la siguiente
// peticion (notifyTask / flexCloudStreamOpen).
static bool gReady = false;                    // flexCloudBegin() termino de reservar su estado
static void logTaskFail(const char* name, uint32_t stack){
  Serial.printf("[CLOUD] no se pudo crear la tarea %s (pila %u KB): SRAM interna %u KB, mayor bloque %u KB\n", name,
                (unsigned)(stack / 1024u),
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024u),
                (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024u));
}
static bool ensureMainTask(){
  if(gTask) return true;
  if(!gReady) return false;
  BaseType_t rc = xTaskCreatePinnedToCore(cloudTask, "flex-cloud", 16384, nullptr, 1, &gTask, 1);
  if(rc != pdPASS){
    gTask = nullptr;
    static uint32_t lastLogMs = 0;
    if(!lastLogMs || millis() - lastLogMs > 10000u){ lastLogMs = millis() | 1u; logTaskFail("flex-cloud", 16384); }
    // Lo que se pidio no lo va a atender nadie: la lista no se queda en "Cargando"
    // para siempre, dice que falta memoria y ofrece "Reintentar"; y la tarjeta lo
    // dice tambien (si no, "Conectando..." o, peor, "sin cuenta" hasta el reintento).
    lock(); bool loading = gListInfo.state == FCL_LIST_LOADING; unlock();
    if(loading) listError("no_memory");
    setNet(FCN_UNAVAILABLE, errText("no_memory"));
    return false;
  }
  return true;
}
static bool ensureStreamTask(){
  if(gStTask) return true;
  if(!gReady) return false;
  BaseType_t rc = xTaskCreatePinnedToCore(streamTask, "flex-cloud-st", 10240, nullptr, 1, &gStTask, 1);
  if(rc != pdPASS){ gStTask = nullptr; logTaskFail("flex-cloud-st", 10240); return false; }
  return true;
}

// Trabajo que no puede esperar a que alguien abra la nube: transferencias a
// medias del arranque anterior, o una cancelacion que aun debe soltar su
// reserva en el servidor.
static bool journalHasWork(){
  if(!gJ) return false;
  for(int i = 0; i < FCL_JOBS_MAX; i++){
    const FclJob& x = gJ->jobs[i];
    if(x.state == FCL_JOB_QUEUED || x.state == FCL_JOB_ACTIVE) return true;
    if(x.state == FCL_JOB_CANCELLED && (x.flags & FCL_JF_ABORT)) return true;
  }
  return false;
}

static bool allocState(){
  if(!gList) gList = (FclItem*)psAlloc(sizeof(FclItem) * FLEX_CLOUD_LIST_MAX);
  if(!gJson) gJson = (char*)psAlloc(JSON_CAP);
  if(!gIo) gIo = (uint8_t*)psAlloc(IO_CAP);
  if(!gCmds) gCmds = (Cmd*)psAlloc(sizeof(Cmd) * CMDS);
  if(!gEvents) gEvents = (FlexCloudEvent*)psAlloc(sizeof(FlexCloudEvent) * EVENTS);
  if(!gJ) gJ = (FclJournal*)psAlloc(sizeof(FclJournal));
  if(!gApi) gApi = new Api();
  return gList && gJson && gIo && gCmds && gEvents && gJ && gApi;
}

static void reemitPending(){
  // Trabajos terminados cuyo aviso quiza no llego a procesarse (apagado entre
  // medias, o el destino cambio antes): se vuelven a anunciar hasta que la
  // interfaz los confirme. Tambien corre con la interfaz leyendo (cambio de
  // destino): el estado se toca bajo el cerrojo y se avisa fuera de el.
  for(int i = 0; i < FCL_JOBS_MAX; i++){
    lock();
    FclJob x = gJ->jobs[i];
    gRt[i].phase = x.state == FCL_JOB_DONE ? FCX_DONE : x.state == FCL_JOB_FAILED ? FCX_FAILED : FCX_QUEUED;
    unlock();
    if(x.state != FCL_JOB_DONE || (x.flags & FCL_JF_DELIVERED)) continue;
    FlexCloudEvent e; memset(&e, 0, sizeof(e));
    e.kind = x.type == FCL_JOB_UPLOAD ? FCE_UPLOAD_DONE : FCE_DOWNLOAD_DONE;
    e.opId = x.id; e.ok = true; e.mlId = x.mlId; e.flags = x.flags; e.size = x.size;
    snprintf(e.localPath, sizeof(e.localPath), "%s", x.localPath);
    snprintf(e.name, sizeof(e.name), "%s", x.name);
    snprintf(e.fileId, sizeof(e.fileId), "%s", x.type == FCL_JOB_UPLOAD ? x.fileId : x.remoteId);
    snprintf(e.sha256, sizeof(e.sha256), "%s", x.sha256);
    snprintf(e.text, sizeof(e.text), "%s", x.type == FCL_JOB_UPLOAD ? "Subido a Flex Cloud" : "Descargado");
    pushEvent(e);
  }
}

} // namespace

// ======================================================================
//  API publica
// ======================================================================
void flexCloudSetDest(uint8_t dest){
  dest = dest == FCD_PHONE ? (uint8_t)FCD_PHONE : (uint8_t)FCD_INTERNET;
  // Antes de flexCloudBegin() no hay tarea ni diario cargado: solo se elige cual.
  if(!gReady){ gDest = gDestWant = dest; return; }
  if(gDestWant == dest) return;
  gDestWant = dest;
  notifyTask();
}
uint8_t flexCloudDest(){ return gDest; }
void flexCloudPhoneForgotten(){
  gPhoneForgot = true;
  if(gReady) notifyTask();
}

void flexCloudBegin(){
  if(!gLock) gLock = xSemaphoreCreateMutex();
  if(!gStLock) gStLock = xSemaphoreCreateMutex();
  if(!allocState()){ Serial.println(F("[CLOUD] sin memoria para Flex Cloud")); return; }
  memset(&gStatus, 0, sizeof(gStatus));
  memset(&gListInfo, 0, sizeof(gListInfo));
  memset(gRt, 0, sizeof(gRt));
  // Hasta que la tarea da su primera vuelta (refreshNet) NO se sabe si hay cuenta,
  // si esta rechazada o si falta el Wi-Fi: la tarea ya no nace al arrancar sino con
  // la primera peticion, asi que la primera pintada de la tarjeta llega ANTES de
  // ella. Con "sin cuenta" por defecto, una cuenta vinculada ensenaba un instante
  // "Vincular cuenta" (y, si la tarea no podia nacer, para siempre).
  gStatus.net = FCN_CONNECTING;
  snprintf(gStatus.netText, sizeof(gStatus.netText), "%s", phoneDest() ? "Conectando con el tel\xC3\xA9" "fono" : "Conectando con Flex Cloud");
  gCmdHead = gCmdN = 0; gEvHead = gEvN = 0;
  loadJournal();
  reemitPending();
  gMeDue = false;                                         // la cuota se pide cuando la nube se ve (flexCloudSetActive)
  if(flexFsReady() && flexFsExists(VIEW_DIR)) flexFsDelete(VIEW_DIR);   // lo que dejo un firmware anterior (la foto ya no pasa por la flash)
  memset(&gView, 0, sizeof(gView)); gViewWant = 0;
  gReady = true;
  // Sin trabajo pendiente NO se crea ninguna tarea: nace con la primera peticion
  // (notifyTask) y la de streaming con el primer video (flexCloudStreamOpen).
  if(journalHasWork()) ensureMainTask();
  int pend = fclJournalCount(gJ, FCL_JOB_QUEUED);
  if(pend) Serial.printf("[CLOUD] %d transferencia(s) pendientes del arranque anterior\n", pend);
}

void flexCloudSetActive(bool active){
  bool was = gActive;
  gActive = active;
  if(active && !was){ gMeDue = true; notifyTask(); return; }
  // La interfaz llama a esto en cada vuelta. Si la tarea no pudo nacer (sin
  // memoria para su pila) y hay algo que atender -- la nube a la vista o un
  // trabajo pendiente --, se reintenta sola cada 2 s hasta que haya sitio.
  if(!gTask && gReady){
    static uint32_t lastTryMs = 0;
    if(!lastTryMs || millis() - lastTryMs >= 2000u){
      lock(); bool work = journalHasWork(); unlock();
      if(active || work){ lastTryMs = millis() | 1u; notifyTask(); }
    }
  }
}

void flexCloudStatus(FlexCloudStatus* out){
  if(!out) return;
  lock();
  *out = gStatus;
  uint8_t act = 0;
  if(gJ) for(int i = 0; i < FCL_JOBS_MAX; i++) if(gJ->jobs[i].state == FCL_JOB_QUEUED || gJ->jobs[i].state == FCL_JOB_ACTIVE) act++;
  out->activeXfers = act;
  unlock();
}

// El usuario DESVINCULO la cuenta en este aparato (FlexOS_Account_Bridge): lo que
// estaba en cola o en marcha era de ELLA y no puede seguir hacia otra cuenta que se
// vincule despues. Se cancelan las transferencias (con lo de siempre al cancelar:
// los originales no se tocan y la reserva en la nube la suelta la tarea cuando haya
// cuenta) y se sueltan la cuota, la direccion y la lista de esa cuenta.
void flexCloudAccountUnlinked(){
  if(!gLock || !gJ) return;
  // Con el destino en el telefono, el diario de Internet no esta cargado: lo
  // cancela la tarea sobre el archivo (applyPending), antes de cualquier cambio
  // de destino. Lo que se ensena es del telefono: no hay identidad que soltar.
  if(phoneDest()){ gInetUnlinked = true; notifyTask(); return; }
  uint32_t ids[FCL_JOBS_MAX]; int n = 0;
  lock();
  for(int i = 0; i < FCL_JOBS_MAX; i++){
    const FclJob& x = gJ->jobs[i];
    if(x.state == FCL_JOB_QUEUED || x.state == FCL_JOB_ACTIVE) ids[n++] = x.id;
  }
  unlock();
  for(int i = 0; i < n; i++) flexCloudCancel(ids[i]);
  flexCloudClearFinished();
  forgetIdentity("no_account");
}

const char* flexCloudNetText(uint8_t net){
  if(phoneDest()){
    switch(net){
      case FCN_ONLINE:      return "Conectado";
      case FCN_CONNECTING:  return "Conectando...";
      case FCN_OFFLINE:     return "Sin Wi-Fi";
      case FCN_AUTH:        return "Vuelve a emparejar el tel\xC3\xA9" "fono";
      case FCN_UNAVAILABLE: return "Tel\xC3\xA9" "fono desconectado";
      default:              return "Sin tel\xC3\xA9" "fono";
    }
  }
  switch(net){
    case FCN_ONLINE:      return "Conectado";
    case FCN_CONNECTING:  return "Conectando...";
    case FCN_OFFLINE:     return "Sin Wi-Fi";
    case FCN_AUTH:        return "Vuelve a vincular tu cuenta";
    case FCN_UNAVAILABLE: return "Flex Cloud no responde";
    default:              return "Sin Flex Account";
  }
}

bool flexCloudRequestList(uint8_t view, const char* folderId, const char* query){
  if(!gLock || !gList) return false;
  lock();
  gListInfo.view = view;
  snprintf(gListInfo.folderId, sizeof(gListInfo.folderId), "%s", folderId && strcmp(folderId, "root") ? folderId : "");
  snprintf(gQuery, sizeof(gQuery), "%s", query ? query : "");
  gListInfo.state = FCL_LIST_LOADING;
  gListInfo.count = 0; gListInfo.more = false; gListInfo.error[0] = 0; gListInfo.nCrumbs = 0;
  gListInfo.gen++;
  unlock();
  gListStale = false;                                        // una lista nueva ya es fresca
  Cmd c; memset(&c, 0, sizeof(c)); c.type = CMD_LIST;
  if(pushCmd(c)) return true;
  listError("busy");                                         // la cola de ordenes esta llena: se dice, con "Reintentar"
  return false;
}
bool flexCloudRequestMore(){
  lock(); bool more = gListInfo.more && gListInfo.state == FCL_LIST_READY; unlock();
  if(!more) return false;
  Cmd c; memset(&c, 0, sizeof(c)); c.type = CMD_MORE;
  return pushCmd(c);
}
void flexCloudRefresh(){
  lock(); gListInfo.state = FCL_LIST_LOADING; gListInfo.gen++; unlock();
  Cmd c; memset(&c, 0, sizeof(c)); c.type = CMD_REFRESH;
  pushCmd(c);
}
void flexCloudListInfo(FlexCloudListInfo* out){ if(!out) return; lock(); *out = gListInfo; unlock(); }
void flexCloudWatch(const char* const* ids, int n){
  if(!gLock) return;
  if(!ids || n < 0) n = 0;
  if(n > FLEX_CLOUD_WATCH_MAX) n = FLEX_CLOUD_WATCH_MAX;
  lock();
  bool same = n == gWatchN;
  for(int i = 0; same && i < n; i++) same = ids[i] && !strcmp(gWatch[i], ids[i]);
  if(!same){                                                 // el mismo conjunto en cada repintado NO reinicia nada
    for(int i = 0; i < n; i++) snprintf(gWatch[i], sizeof(gWatch[i]), "%s", ids[i] ? ids[i] : "");
    gWatchN = n; gWatchRR = n ? n - 1 : 0; gWatchDone = false;
    gWatchSinceMs = millis(); gWatchGapMs = WATCH_FIRST_MS; gWatchNextMs = millis() + WATCH_FIRST_MS;
  }
  unlock();
}
void flexCloudWatchKick(const char* id){
  if(!gLock || !id || !id[0]) return;
  lock(); snprintf(gWatchKick, sizeof(gWatchKick), "%s", id); unlock();
  notifyTask();
}
int flexCloudListCopy(FclItem* dst, int start, int cap){
  if(!dst || cap <= 0 || !gList) return 0;
  lock();
  int n = gListInfo.count - start;
  if(n > cap) n = cap;
  if(n > 0) memcpy(dst, gList + start, sizeof(FclItem) * (size_t)n);
  unlock();
  return n > 0 ? n : 0;
}

// SIN CREDENCIAL QUE SIRVA NO ENTRA NADA NUEVO. Con la cuenta desvinculada o
// rechazada por Flex Account (flexAccountUsable() == false) una peticion nueva se
// quedaria esperando "conexion" para siempre y la interfaz diria que se hizo:
// se rechaza aqui y quien pregunta (CloudKit) dice por que.
static uint32_t opFor(uint8_t type, const FclItem* it, const char* text, const char* id = nullptr){
  if(!gLock || !destUsable()) return 0;
  Cmd c; memset(&c, 0, sizeof(c));
  c.type = type;
  lock(); c.opId = gNextOp++; unlock();
  if(it){
    snprintf(c.id, sizeof(c.id), "%s", it->id); c.folder = it->isFolder; c.size = it->size; snprintf(c.sha, sizeof(c.sha), "%s", it->sha256);
  }
  if(id) snprintf(c.id, sizeof(c.id), "%s", id);
  if(text) fclCopyUtf8(c.text, sizeof(c.text), text);
  return pushCmd(c) ? c.opId : 0;
}
uint32_t flexCloudMkdir(const char* parentId, const char* name){
  if(!name || !name[0]) return 0;
  return opFor(CMD_MKDIR, nullptr, name, parentId && strcmp(parentId, "root") ? parentId : "");
}
uint32_t flexCloudRename(const FclItem* it, const char* newName){ return it && newName && newName[0] ? opFor(CMD_RENAME, it, newName) : 0; }
uint32_t flexCloudTrash(const FclItem* it){ return it ? opFor(CMD_TRASH, it, nullptr) : 0; }
uint32_t flexCloudRestore(const FclItem* it){ return it ? opFor(CMD_RESTORE, it, nullptr) : 0; }
uint32_t flexCloudDeleteForever(const FclItem* it){ return it ? opFor(CMD_DELETE, it, nullptr) : 0; }
// ---- Foto para el visor (ver fetchView) ----------------------------------
uint32_t flexCloudViewStart(const FclItem* it){
  if(!it || it->isFolder || !gLock || !destUsable()) return 0;
  const uint64_t size = fclPlaySize(it);
  Cmd c; memset(&c, 0, sizeof(c));
  c.type = CMD_VIEW;
  snprintf(c.id, sizeof(c.id), "%s", it->id);
  // Para VER una foto se pide la vista ligera que preparo el telefono (con SU tamano y SU SHA-256), no el original.
  c.play = fclPlayable(it);
  c.size = size;
  snprintf(c.sha, sizeof(c.sha), "%s", fclPlaySha(it));
  lock();
  c.opId = gNextOp++;
  viewDropLocked();                                          // la peticion anterior (y su buffer) ya no se quieren
  gView.op = c.opId; gView.state = FCV_FETCHING; gView.total = size > 0xFFFFFFF0ull ? 0 : (uint32_t)size;
  gViewWant = c.opId;                                        // ANTES de encolar: la tarea ya ve que esta vigente
  unlock();
  if(pushCmd(c)) return c.opId;
  lock(); if(gView.op == c.opId){ viewDropLocked(); } unlock();
  gViewWant = 0;
  return 0;
}
uint8_t flexCloudViewState(uint32_t op, uint32_t* got, uint32_t* total, char* err, size_t cap){
  if(!gLock || !op) return FCV_NONE;
  uint8_t st = FCV_NONE;
  lock();
  if(gView.op == op){
    st = gView.state;
    if(got) *got = gView.got;
    if(total) *total = gView.total;
    if(err && cap) snprintf(err, cap, "%s", st == FCV_FAILED ? gView.err : "");
  }
  unlock();
  return st;
}
uint8_t* flexCloudViewTake(uint32_t op, uint32_t* len){
  if(!gLock || !op) return nullptr;
  uint8_t* p = nullptr;
  lock();
  if(gView.op == op && gView.state == FCV_READY && gView.buf){
    p = gView.buf; if(len) *len = gView.got;
    gView.buf = nullptr;                                     // desde aqui es de quien la toma
    memset(&gView, 0, sizeof(gView));
    gViewWant = 0;
  }
  unlock();
  return p;
}
void flexCloudViewFree(uint8_t* p){ psFree(p); }
void flexCloudViewCancel(uint32_t op){
  if(!gLock || !op) return;
  lock();
  if(gView.op == op){ viewDropLocked(); if(gViewWant == op) gViewWant = 0; }
  unlock();
}

static uint32_t addJob(uint8_t type, const char* localPath, const char* name, const char* parentId, const char* remoteId,
                       uint64_t size, const char* sha, uint32_t mlId, uint8_t flags){
  if(!gJ) return 0;
  lock();
  // La misma subida dos veces seguidas (doble toque) es una sola.
  for(int i = 0; i < FCL_JOBS_MAX; i++){
    FclJob& x = gJ->jobs[i];
    bool live = x.state == FCL_JOB_QUEUED || x.state == FCL_JOB_ACTIVE;
    if(live && x.type == type && ((type == FCL_JOB_UPLOAD && !strcmp(x.localPath, localPath ? localPath : "")) ||
                                  (type == FCL_JOB_DOWNLOAD && !strcmp(x.remoteId, remoteId ? remoteId : "")))){
      uint32_t id = x.id; unlock(); return id;
    }
  }
  FclJob* j = fclJournalAlloc(gJ);
  if(!j){ unlock(); return 0; }
  j->state = FCL_JOB_QUEUED; j->type = type; j->flags = flags; j->mlId = mlId; j->size = size;
  if(localPath) snprintf(j->localPath, sizeof(j->localPath), "%s", localPath);
  fclCopyUtf8(j->name, sizeof(j->name), name ? name : "");
  if(parentId && strcmp(parentId, "root")) snprintf(j->parentId, sizeof(j->parentId), "%s", parentId);
  if(remoteId) snprintf(j->remoteId, sizeof(j->remoteId), "%s", remoteId);
  if(sha) snprintf(j->sha256, sizeof(j->sha256), "%s", sha);
  int i = jobIndex(j);
  memset(&gRt[i], 0, sizeof(gRt[i]));
  gRt[i].phase = FCX_QUEUED;
  gStatus.xferGen++;
  uint32_t id = j->id;
  gJournalDirty = true;
  unlock();
  notifyTask();
  return id;
}

uint32_t flexCloudUpload(const char* localPath, const char* name, const char* parentId, uint32_t mlId, uint8_t flags){
  if(!destUsable()) return 0;                                   // ver opFor
  if(!localPath || !localPath[0] || !flexFsExists(localPath)) return 0;
  const char* base = strrchr(localPath, '/');
  const char* nm = name && name[0] ? name : (base ? base + 1 : localPath);
  flags &= FCL_JF_FREE_LOCAL | FCL_JF_TO_LIBRARY | FCL_JF_FROM_LIBRARY;    // las internas no se piden
  return addJob(FCL_JOB_UPLOAD, localPath, nm, parentId, nullptr, 0, nullptr, mlId,
                (uint8_t)(flags | (mlId ? FCL_JF_FROM_LIBRARY : 0)));
}

uint32_t flexCloudDownload(const FclItem* it, uint8_t flags){
  if(!destUsable()) return 0;                                   // ver opFor
  if(!it || it->isFolder || !it->sha256[0]) return 0;           // sin huella no se puede verificar
  flags &= FCL_JF_TO_LIBRARY | FCL_JF_MOVE_REMOTE;
  return addJob(FCL_JOB_DOWNLOAD, nullptr, it->name, it->parentId, it->id, it->size, it->sha256, 0, flags);
}

bool flexCloudCancel(uint32_t jobId){
  if(!gJ) return false;
  bool ok = false;
  lock();
  FclJob* j = fclJournalFind(gJ, jobId);
  if(j && (j->state == FCL_JOB_QUEUED || j->state == FCL_JOB_ACTIVE)){
    j->state = FCL_JOB_CANCELLED;
    // Pendiente: la subida suelta su reserva en la nube (si no, el espacio
    // quedaria apartado hasta que caduque la sesion) y la descarga borra su
    // temporal. Lo hace la tarea en cuanto puede; la cancelacion ya es firme.
    if(j->type == FCL_JOB_DOWNLOAD || j->remoteId[0]) j->flags |= FCL_JF_ABORT;
    gRt[jobIndex(j)].phase = FCX_CANCELLED;
    gRt[jobIndex(j)].retryAt = 0; gRt[jobIndex(j)].fails = 0;
    gStatus.xferGen++; gJournalDirty = true; ok = true;
  }
  unlock();
  notifyTask();
  return ok;
}

bool flexCloudRetry(uint32_t jobId){
  if(!gJ || !destUsable()) return false;                        // ver opFor
  bool ok = false;
  lock();
  FclJob* j = fclJournalFind(gJ, jobId);
  if(j && j->state == FCL_JOB_FAILED){
    j->state = FCL_JOB_QUEUED; j->error[0] = 0;
    int i = jobIndex(j);
    memset(&gRt[i], 0, sizeof(gRt[i])); gRt[i].phase = FCX_QUEUED;
    gStatus.xferGen++; gJournalDirty = true; ok = true;
  }
  unlock();
  notifyTask();
  return ok;
}

void flexCloudClearFinished(){
  if(!gJ) return;
  lock();
  for(int i = 0; i < FCL_JOBS_MAX; i++){
    FclJob& x = gJ->jobs[i];
    // El que la tarea esta soltando ahora mismo (recien cancelado) se oculta y
    // ella misma libera el hueco.
    if(&x == gRun.job){ if(x.state == FCL_JOB_CANCELLED) x.flags |= FCL_JF_CLEARED; continue; }
    bool finished = x.state == FCL_JOB_FAILED || x.state == FCL_JOB_CANCELLED || (x.state == FCL_JOB_DONE && (x.flags & FCL_JF_DELIVERED));
    if(!finished) continue;
    // Una cancelacion que aun debe soltar la reserva en la nube se oculta y
    // se borra cuando lo haya hecho.
    if(x.flags & FCL_JF_ABORT){ x.flags |= FCL_JF_CLEARED; continue; }
    memset(&x, 0, sizeof(x)); memset(&gRt[i], 0, sizeof(gRt[i]));
  }
  gStatus.xferGen++; gJournalDirty = true;
  unlock();
  notifyTask();
}

int flexCloudXfers(FlexCloudXfer* out, int cap){
  if(!out || !gJ) return 0;
  int n = 0;
  lock();
  // Si esperan por la CUENTA (o por el telefono) y no por el Wi-Fi, la fila lo dice (fclXferLine).
  const char* waitWhy = gStatus.net == FCN_AUTH || gStatus.net == FCN_NO_ACCOUNT ? netWhy(gStatus.net)
                      : phoneDest() && gStatus.net == FCN_UNAVAILABLE ? "network" : nullptr;
  // En orden de alta (el diario no esta ordenado).
  uint32_t last = 0;
  for(;;){
    int pick = -1; uint32_t best = 0xFFFFFFFFu;
    for(int i = 0; i < FCL_JOBS_MAX; i++){
      const FclJob& x = gJ->jobs[i];
      if(x.state == FCL_JOB_FREE || (x.flags & FCL_JF_CLEARED)) continue;
      if(x.id > last && x.id < best){ best = x.id; pick = i; }
    }
    if(pick < 0 || n >= cap) break;
    last = best;
    const FclJob& x = gJ->jobs[pick];
    FlexCloudXfer& o = out[n++];
    memset(&o, 0, sizeof(o));
    o.id = x.id; o.type = x.type; o.flags = x.flags; o.size = x.size;
    o.phase = x.state == FCL_JOB_DONE ? (uint8_t)FCX_DONE : x.state == FCL_JOB_FAILED ? (uint8_t)FCX_FAILED
            : x.state == FCL_JOB_CANCELLED ? (uint8_t)FCX_CANCELLED : gRt[pick].phase;
    o.done = x.state == FCL_JOB_DONE ? x.size : gRt[pick].done;
    o.bytesPerSec = gRt[pick].rate;
    uint32_t now = millis();
    o.retryInMs = gRt[pick].retryAt && (int32_t)(gRt[pick].retryAt - now) > 0 ? gRt[pick].retryAt - now : 0;
    fclCopyUtf8(o.name, sizeof(o.name), x.name);
    snprintf(o.error, sizeof(o.error), "%s", x.error);
    if(waitWhy && o.phase == FCX_WAITING_NET) snprintf(o.error, sizeof(o.error), "%s", errText(waitWhy));
  }
  unlock();
  return n;
}

bool flexCloudPollEvent(FlexCloudEvent* ev){
  if(!ev || !gEvents) return false;
  bool ok = false;
  lock();
  if(gEvN){
    *ev = gEvents[gEvHead]; gEvHead = (gEvHead + 1) % EVENTS; gEvN--; ok = true;
    // Un trabajo terminado se da por entregado al sacarlo de la cola: la
    // interfaz lo procesa en esta misma vuelta del bucle.
    if(gJ && (ev->kind == FCE_UPLOAD_DONE || ev->kind == FCE_DOWNLOAD_DONE)){
      FclJob* j = fclJournalFind(gJ, ev->opId);
      if(j){ j->flags |= FCL_JF_DELIVERED; gJournalDirty = true; }
    }
  }
  unlock();
  if(ok && (ev->kind == FCE_UPLOAD_DONE || ev->kind == FCE_DOWNLOAD_DONE)) notifyTask();
  return ok;
}

void flexCloudWantThumb(const char* fileId){
  if(!fileId || !fileId[0] || !gLock) return;
  lock();
  int slot = -1;
  for(int i = 0; i < FLEX_CLOUD_THUMBS; i++) if(gThumbs[i].state != TH_EMPTY && !strcmp(gThumbs[i].id, fileId)){ slot = i; break; }
  if(slot >= 0){ gThumbs[slot].lru = ++gThumbTick; unlock(); return; }
  // Hueco: uno vacio o el usado hace mas tiempo (su miniatura se suelta).
  uint32_t oldest = 0xFFFFFFFFu;
  for(int i = 0; i < FLEX_CLOUD_THUMBS; i++){
    if(gThumbs[i].state == TH_EMPTY){ slot = i; break; }
    if(gThumbs[i].lru < oldest){ oldest = gThumbs[i].lru; slot = i; }
  }
  Thumb& t = gThumbs[slot];
  psFree(t.px); t.px = nullptr;
  snprintf(t.id, sizeof(t.id), "%s", fileId);
  t.state = TH_WANTED; t.lru = ++gThumbTick;
  unlock();
  notifyTask();
}

bool flexCloudThumbDraw(const char* fileId, void (*draw)(const uint16_t* px, int side, void* user), void* user){
  if(!fileId || !draw || !gLock) return false;
  bool ok = false;
  lock();
  for(int i = 0; i < FLEX_CLOUD_THUMBS; i++){
    Thumb& t = gThumbs[i];
    if(t.state == TH_READY && t.px && !strcmp(t.id, fileId)){ t.lru = ++gThumbTick; draw(t.px, FLEX_CLOUD_THUMB_SIDE, user); ok = true; break; }
  }
  unlock();
  return ok;
}
uint32_t flexCloudThumbGen(){ lock(); uint32_t g = gThumbGen; unlock(); return g; }

bool flexCloudStreamOpen(const FclItem* it){
  if(!destUsable()) return false;                               // ver opFor
  const uint64_t playSz = fclPlaySize(it);           // el tamano de lo que se va a LEER (version del perfil o original)
  if(!it || it->isFolder || !gStLock || playSz == 0 || playSz > 0xFFFFFFF0ull) return false;
  stLock();
  if(!gSt.arena && !gSt.busy){
    // UNA reserva, la primera vez: se reutiliza en todos los videos.
    gSt.arena = (uint8_t*)heap_caps_malloc((size_t)FLEX_CLOUD_STREAM_BLOCK * FLEX_CLOUD_STREAM_BLOCKS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  if(!gSt.arena){ stUnlock(); return false; }
  if(!ensureStreamTask()){ stUnlock(); return false; }     // sin hilo no hay streaming: el visor lo dice
  gSt.open = true;
  gSt.gen++;
  gSt.size = (uint32_t)playSz;
  gSt.play = fclPlayable(it);
  snprintf(gSt.fileId, sizeof(gSt.fileId), "%s", it->id);
  fclCacheInit(&gSt.cache, gSt.arena, FLEX_CLOUD_STREAM_BLOCK, FLEX_CLOUD_STREAM_BLOCKS, gSt.size);
  gSt.state = FCS_OPENING; gSt.err[0] = 0;
  gSt.lastBlk = 0xFFFFFFFFu; gSt.bps = 0; gSt.conns = 0;
  stUnlock();
  Serial.printf("[CLOUD] streaming: %s (%lu bytes%s)\n", it->id, (unsigned long)playSz, fclPlayable(it) ? ", version del perfil" : "");
  stNotify();
  return true;
}
void flexCloudStreamClose(){
  if(!gStLock) return;
  stLock();
  if(gSt.open){ gSt.open = false; gSt.gen++; gSt.state = FCS_CLOSED; }
  stUnlock();
  stNotify();
}
bool flexCloudStreamIs(const char* fileId){
  if(!gStLock || !fileId) return false;
  stLock(); bool r = gSt.open && !strcmp(gSt.fileId, fileId); stUnlock();
  return r;
}
uint8_t flexCloudStreamState(char* err, size_t cap){
  if(!gStLock) return FCS_CLOSED;
  stLock(); uint8_t s = gSt.state; if(err && cap) snprintf(err, cap, "%s", gSt.err); stUnlock();
  return s;
}
uint32_t flexCloudStreamSize(){ if(!gStLock) return 0; stLock(); uint32_t s = gSt.open ? gSt.size : 0; stUnlock(); return s; }
int flexCloudStreamRead(uint32_t off, void* buf, uint32_t n){
  if(!gStLock) return -1;
  stLock();
  int r = gSt.open ? fclCacheRead(&gSt.cache, off, buf, n) : -1;
  // Se avisa a la tarea de dos cosas: de que falta algo (r < 0), y de que la lectura entro en OTRO bloque (la ventana de por
  // delante se desliza y hay hueco nuevo que llenar). Una vez por bloque -- no por lectura --: ahi esta toda la diferencia
  // con sondear.
  bool wake = r < 0;
  if(gSt.open && r >= 0){
    const uint32_t blk = off / gSt.cache.blockSize;
    if(blk != gSt.lastBlk){ gSt.lastBlk = blk; wake = true; }
  }
  stUnlock();
  if(wake) stNotify();
  return r;
}
bool flexCloudStreamReady(uint32_t off, uint32_t len){
  if(!gStLock) return false;
  stLock(); bool r = gSt.open && fclCacheReady(&gSt.cache, off, len); stUnlock();
  return r;
}
void flexCloudStreamPin(uint32_t off, uint32_t len){ if(!gStLock) return; stLock(); if(gSt.open) fclCachePin(&gSt.cache, off, len); stUnlock(); stNotify(); }
void flexCloudStreamUnpin(){ if(!gStLock) return; stLock(); if(gSt.open) fclCacheUnpin(&gSt.cache); stUnlock(); }
void flexCloudStreamSeek(uint32_t pos){ if(!gStLock) return; stLock(); if(gSt.open) fclCacheSeek(&gSt.cache, pos); stUnlock(); stNotify(); }
uint32_t flexCloudStreamBuffered(uint32_t pos){
  if(!gStLock) return 0;
  stLock(); uint32_t r = gSt.open ? fclCacheContiguous(&gSt.cache, pos) : 0; stUnlock();
  return r;
}
bool flexCloudStreamStats(FlexCloudStreamStats* out){
  if(!gStLock || !out) return false;
  stLock();
  const bool open = gSt.open;
  if(open){ out->bps = gSt.bps; out->blocks = gSt.cache.fetched; out->misses = gSt.cache.misses; out->conns = gSt.conns; }
  stUnlock();
  return open;
}

size_t flexCloudShed(){
  size_t freed = 0;
  if(gLock){
    lock();
    for(int i = 0; i < FLEX_CLOUD_THUMBS; i++)
      if(gThumbs[i].px){ psFree(gThumbs[i].px); gThumbs[i].px = nullptr; gThumbs[i].state = TH_EMPTY; freed += (size_t)FLEX_CLOUD_THUMB_SIDE * FLEX_CLOUD_THUMB_SIDE * 2; }
    gThumbGen++;
    unlock();
  }
  if(gStLock){
    stLock();
    if(!gSt.open && !gSt.busy && gSt.arena){
      heap_caps_free(gSt.arena); gSt.arena = nullptr;
      freed += (size_t)FLEX_CLOUD_STREAM_BLOCK * FLEX_CLOUD_STREAM_BLOCKS;
    }
    stUnlock();
  }
  return freed;
}

#ifdef FLEXOS_HOST_TEST
void flexCloudTestSetBase(const char* base){ snprintf(gBase, sizeof(gBase), "%s", base); }
uint32_t flexCloudTestJournalSaves(){ return gJournalSaves; }
void flexCloudTestStep(){ taskStep(); }
uint32_t flexCloudTestStreamStep(){ return streamStep(); }
void flexCloudTestPowerCycle(){ flexCloudTestPowerOff(); flexCloudBegin(); }
void flexCloudTestPowerOff(){
  // Se pierde la RAM (los objetos de red tambien); el diario sigue en "flash".
  // El destino vuelve al de arranque: lo fija flexStorageBegin() otra vez.
  gDest = gDestWant = FCD_INTERNET; gPhoneForgot = false; gInetUnlinked = false;
  runnerClose();
  stConnClose();
  stLock(); gSt.open = false; gSt.gen++; gSt.busy = false; stUnlock();
  for(int i = 0; i < FLEX_CLOUD_THUMBS; i++){ psFree(gThumbs[i].px); memset(&gThumbs[i], 0, sizeof(gThumbs[i])); }
  if(gApi){ gApi->http.end(); gApi->sec.stop(); }
  gActive = false; gNetFails = 0; gNetRetryAt = 0; gNextMeMs = 0; gAuthWait = false;
  gTask = nullptr; gStTask = nullptr; gReady = false;     // las tareas se pierden con la RAM
}
#endif
