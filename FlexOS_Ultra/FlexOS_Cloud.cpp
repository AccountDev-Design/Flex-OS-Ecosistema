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
#include "FlexOS_JPEG.h"
#include "FlexOS_MediaLib.h"
#include "FlexOS_OTA.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

// ---------------------------------------------------------------- constantes
static const char*    CLOUD_DIR    = "/System/Cloud";
static const char*    JOURNAL_PATH = "/System/Cloud/jobs.bin";
static const char*    DL_DIR       = "/System/Cloud/dl";
static const char*    VIEW_PATH    = "/System/Cloud/view.bin";
static const size_t   JSON_CAP     = 48u * 1024u;       // pagina de 40 elementos con holgura
static const size_t   IO_CAP       = 16u * 1024u;       // lectura/escritura de bytes, reutilizado
static const int      PAGE         = 40;
static const uint32_t HTTP_TIMEOUT = 15000;
static const uint32_t ME_EVERY_MS  = 60000;             // cuota al dia mientras la nube esta a la vista
static const uint32_t MIN_HEAP     = 48u * 1024u;       // TLS necesita su sitio: si no lo hay, se espera
static const uint64_t LOCAL_RESERVE = 512u * 1024u;     // = FML_RESERVE_BYTES: nunca se llena LittleFS
static const uint32_t VIEW_MAX     = 8u * 1024u * 1024u;// foto para el visor (el visor admite 6 MB)
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
static FlexCloudStatus gStatus;
static volatile bool gActive = false;
static uint32_t gNextMeMs = 0, gNetRetryAt = 0;
static uint8_t gNetFails = 0;
static volatile bool gMeDue = true;
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
  uint32_t size;
  uint8_t* arena;
  FclCache cache;
  volatile bool busy;           // el fetcher esta escribiendo en un hueco
} gSt;

static void stNotify(){ if(gStTask) xTaskNotifyGive(gStTask); }
static void notifyTask(){ if(gTask) xTaskNotifyGive(gTask); }

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
    if(!flexFsWriteBinAtomic(JOURNAL_PATH, buf, n)) Serial.println(F("[CLOUD] no se pudo guardar el diario"));
  }
  psFree(buf);
}

static void loadJournal(){
  fclJournalInit(gJ);
  if(!flexFsReady()) return;
  size_t cap = fclJournalMaxBytes();
  uint8_t* buf = (uint8_t*)psAlloc(cap);
  if(!buf) return;
  int n = flexFsReadBin(JOURNAL_PATH, buf, cap);
  bool damaged = false;
  if(n > 0) fclJournalDecode(gJ, buf, (size_t)n, &damaged);
  if(damaged) Serial.println(F("[CLOUD] diario con registros danados: se descartaron"));
  psFree(buf);
}

static int jobIndex(const FclJob* j){ return (int)(j - gJ->jobs); }

// ======================================================================
//  HTTP
// ======================================================================
// Cuerpo de respuesta a un buffer fijo (writeToStream descodifica chunked).
class BufSink : public Stream {
 public:
  BufSink(char* b, size_t cap) : b_(b), cap_(cap), n_(0), over_(false) {}
  size_t write(uint8_t c) override { if(n_ + 1 >= cap_){ over_ = true; return 0; } b_[n_++] = (char)c; return 1; }
  size_t write(const uint8_t* p, size_t n) override {
    if(n_ + n >= cap_){ over_ = true; n = n_ + 1 < cap_ ? cap_ - 1 - n_ : 0; }
    memcpy(b_ + n_, p, n); n_ += n; return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  size_t len() const { return n_; }
  bool overflow() const { return over_; }
  void terminate(){ b_[n_ < cap_ ? n_ : cap_ - 1] = 0; }
 private:
  char* b_; size_t cap_, n_; bool over_;
};

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

struct Api { WiFiClientSecure sec; HTTPClient http; };
static Api* gApi = nullptr;                  // conexion reutilizable de la tarea principal

static bool netUsable(){ return flexAccountUsable() && WiFi.status() == WL_CONNECTED; }

static void classifyFail(int status, const char* code){
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
    setNet(FCN_UNAVAILABLE, status < 0 ? "Sin respuesta segura de Flex Cloud" : fclErrorText("server"));
  }
}

// Una peticion de la API. `json` (cuerpo JSON) o `body` (flujo) o nada.
// Devuelve el estado HTTP (<0 = transporte). El cuerpo de la respuesta queda
// en gJson (terminado en 0) y su error.code en `code`.
static int apiCall(const char* method, const char* path, const char* json, Stream* body, size_t bodyLen,
                   const char* hName, const char* hVal, char* code, size_t codeCap, size_t* outLen = nullptr){
  if(code && codeCap) code[0] = 0;
  if(outLen) *outLen = 0;
  if(!gApi || !gJson) return HTTPC_ERROR_TOO_LESS_RAM;
  if(!netUsable()) return HTTPC_ERROR_NOT_CONNECTED;
  if(esp_get_free_heap_size() < MIN_HEAP){ if(code) snprintf(code, codeCap, "no_memory"); return HTTPC_ERROR_TOO_LESS_RAM; }
  char bearer[64];
  if(!flexAccountCopyBearer(bearer, sizeof(bearer))){ if(code) snprintf(code, codeCap, "no_account"); return HTTPC_ERROR_NOT_CONNECTED; }
  char url[384];
  snprintf(url, sizeof(url), "%s%s", gBase, path);
  HTTPClient& h = gApi->http;
  gApi->sec.setCACert(flexCloudRootCA());     // TLS SIEMPRE verificado: aqui viaja la credencial
  gApi->sec.setHandshakeTimeout(12);
  h.setReuse(true);
  h.setTimeout(HTTP_TIMEOUT);
  h.setConnectTimeout(HTTP_TIMEOUT);
  h.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  h.useHTTP10(false);
  if(!h.begin(gApi->sec, url)){ memset(bearer, 0, sizeof(bearer)); return HTTPC_ERROR_CONNECTION_REFUSED; }
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
  if(json){ h.addHeader("Content-Type", "application/json"); st = h.sendRequest(method, (uint8_t*)json, strlen(json)); }
  else if(body){ if(!hName || strcasecmp(hName, "Content-Type")) h.addHeader("Content-Type", "application/octet-stream"); st = h.sendRequest(method, body, bodyLen); }
  else st = h.sendRequest(method);
  size_t n = 0;
  if(st > 0){
    BufSink sink(gJson, JSON_CAP);
    int w = h.writeToStream(&sink);
    sink.terminate();
    n = sink.len();
    if(sink.overflow()){
      // Mas grande de lo que cabe: no es un fallo de red, es una respuesta que no se acepta.
      if(st >= 200 && st < 300){ st = HTTPC_ERROR_TOO_LESS_RAM; if(code) snprintf(code, codeCap, "bad_response"); }
    } else if(w < 0 && st >= 200 && st < 300) st = w;         // cuerpo cortado: no cuenta como exito
  } else gJson[0] = 0;
  h.end();
  if(st < 0) gApi->sec.stop();                               // conexion en estado desconocido: fuera
  if(outLen) *outLen = n;
  if(st >= 400 && code && !code[0]) fclParseError(gJson, n, code, codeCap, nullptr, 0);
  if(st < 0 && code && !code[0]) snprintf(code, codeCap, "network");
  if(st >= 200 && st < 300){
    gNetFails = 0;
    lock(); bool was = gStatus.net != FCN_ONLINE; unlock();
    if(was) setNet(FCN_ONLINE, "Conectado a Flex Cloud");
  } else classifyFail(st, code);
  return st;
}

// ======================================================================
//  Cuenta, cuota y estado de la red
// ======================================================================
static void refreshNet(){
  if(!flexAccountLinked()){ gAuthWait = false; setNet(FCN_NO_ACCOUNT, fclErrorText("no_account")); return; }
  if(!flexAccountUsable()){
    // Flex Account ya lo sabe y lo ensena: de aqui se sale revinculando.
    gAuthWait = false;
    FlexAccountLink l = flexAccountLinkState();
    setNet(FCN_AUTH, flexAccountLinkLabel(l));
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
    gMeDue = true; gNetRetryAt = 0;
  }
}

static bool netReady(){
  lock(); uint8_t net = gStatus.net; unlock();
  if(net == FCN_NO_ACCOUNT || net == FCN_OFFLINE || net == FCN_AUTH) return false;
  return (int32_t)(millis() - gNetRetryAt) >= 0 || net == FCN_ONLINE;
}

static void fetchMe(){
  char code[FCL_CODE_MAX]; size_t n = 0;
  int st = apiCall("GET", "/me", nullptr, nullptr, 0, nullptr, nullptr, code, sizeof(code), &n);
  gNextMeMs = millis() + ME_EVERY_MS;
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
  }
}

// ======================================================================
//  Listados y operaciones
// ======================================================================
static void listError(const char* code){
  lock();
  gListInfo.state = FCL_LIST_ERROR;
  snprintf(gListInfo.error, sizeof(gListInfo.error), "%s", fclErrorText(code));
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
  }
  unlock();
  psFree(tmp);
}

static void opDone(uint32_t opId, int st, const char* code, const char* okText){
  FlexCloudEvent e; memset(&e, 0, sizeof(e));
  e.kind = FCE_OP_DONE; e.opId = opId; e.ok = st >= 200 && st < 300;
  snprintf(e.code, sizeof(e.code), "%s", e.ok ? "" : (code && code[0] ? code : "server"));
  snprintf(e.text, sizeof(e.text), "%s", e.ok ? okText : fclErrorText(e.code));
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
  WiFiClientSecure* dlSec;
  uint64_t      dlLeft;
};
static Runner gRun;
enum { RS_IDLE = 0, RS_PREP, RS_HASH, RS_CREATE, RS_PARTS, RS_COMPLETE, RS_THUMB, RS_DL_PREP, RS_DL_GET, RS_DL_VERIFY };

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
  if(gRun.dlSec){ gRun.dlSec->stop(); delete gRun.dlSec; gRun.dlSec = nullptr; }
}
static void runnerClose(){
  runnerDropIo();
  psFree(gRun.partSha); gRun.partSha = nullptr;
  lock(); gRun.job = nullptr; unlock();
  gRun.stage = RS_IDLE;
}

static void dlTempPath(const FclJob* j, char* out, size_t cap){ snprintf(out, cap, "%s/%lu.part", DL_DIR, (unsigned long)j->id); }

static void jobFinish(FclJob* j, uint8_t state, const char* code){
  int i = jobIndex(j);
  lock();
  // Cancelado por el usuario mientras corria el ultimo paso: gana la cancelacion.
  bool cancelled = j->state == FCL_JOB_CANCELLED;
  if(cancelled) state = FCL_JOB_CANCELLED;
  j->state = state;
  if(state == FCL_JOB_FAILED) snprintf(j->error, sizeof(j->error), "%s", fclErrorText(code));
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
  snprintf(e.text, sizeof(e.text), "%s", e.ok ? (up ? "Subido a Flex Cloud" : "Descargado") : fclErrorText(code));
  snprintf(e.localPath, sizeof(e.localPath), "%s", j->localPath);
  snprintf(e.name, sizeof(e.name), "%s", j->name);
  snprintf(e.fileId, sizeof(e.fileId), "%s", up ? j->fileId : j->remoteId);
  snprintf(e.sha256, sizeof(e.sha256), "%s", j->sha256);
  if(state != FCL_JOB_CANCELLED) pushEvent(e);
  Serial.printf("[CLOUD] %s #%lu %s %s\n", up ? "subida" : "descarga", (unsigned long)j->id,
                state == FCL_JOB_DONE ? "terminada" : state == FCL_JOB_CANCELLED ? "cancelada" : "fallida", code ? code : "");
  runnerClose();
  if(e.ok) gMeDue = true;
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
    lock(); gRt[i].phase = FCX_WAITING_NET; gRt[i].retryAt = millis() + 30000; gStatus.xferGen++; unlock();
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
  if(esp_get_free_heap_size() < MIN_HEAP){ stepFail(j, -1, "no_memory"); return; }
  char bearer[64];
  if(!flexAccountCopyBearer(bearer, sizeof(bearer))){ stepFail(j, -1, "network"); return; }
  gRun.f = have ? flexFsOpenAppend(tp) : flexFsOpenWrite(tp);
  if(!gRun.f){ memset(bearer, 0, sizeof(bearer)); jobFinish(j, FCL_JOB_FAILED, "no_space_local"); return; }
  // Peticion con rango. HTTP/1.0: cuerpo sin trocear, lectura directa.
  gRun.dlSec = new WiFiClientSecure();
  gRun.dl = new HTTPClient();
  gRun.dlSec->setCACert(flexCloudRootCA());        // TLS verificado: aqui tambien viaja la credencial
  gRun.dlSec->setHandshakeTimeout(12);
  gRun.dl->setTimeout(HTTP_TIMEOUT);
  gRun.dl->setConnectTimeout(HTTP_TIMEOUT);
  gRun.dl->setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  gRun.dl->useHTTP10(true);
  char url[256];
  snprintf(url, sizeof(url), "%s/download/%s", gBase, j->remoteId);
  if(!gRun.dl->begin(*gRun.dlSec, url)){ memset(bearer, 0, sizeof(bearer)); stepFail(j, -1, "network"); return; }
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
  if(st != 200 && st != 206){
    if(st == 401) flexAccountReportRejected();
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
  lock(); snprintf(j->localPath, sizeof(j->localPath), "%s", tp); unlock();
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
    case RS_THUMB:     upThumb(j); break;
    case RS_DL_PREP:   dlPrepare(j); break;
    case RS_DL_GET:    dlChunk(j); break;
    case RS_DL_VERIFY: dlVerify(j); break;
    default: runnerClose(); break;
  }
  return true;
}

// ---- Foto para el visor (un solo hueco, sin diario) ----------------------
static void fetchView(const Cmd& c){
  FlexCloudEvent e; memset(&e, 0, sizeof(e));
  e.kind = FCE_VIEW_FAILED; e.opId = c.opId;
  snprintf(e.fileId, sizeof(e.fileId), "%s", c.id);
  snprintf(e.name, sizeof(e.name), "%s", c.text);
  uint64_t total = flexFsTotalBytes(), used = flexFsUsedBytes();
  uint64_t freeB = total > used ? total - used : 0;
  if(c.size > VIEW_MAX){ snprintf(e.code, sizeof(e.code), "file_too_large"); snprintf(e.text, sizeof(e.text), "Demasiado grande para abrirla aqu\xC3\xAD"); pushEvent(e); return; }
  if(c.size + LOCAL_RESERVE > freeB + (flexFsExists(VIEW_PATH) ? flexFsSize(VIEW_PATH) : 0)){
    snprintf(e.code, sizeof(e.code), "no_space_local"); snprintf(e.text, sizeof(e.text), "%s", fclErrorText(e.code)); pushEvent(e); return;
  }
  char bearer[64];
  if(!netUsable() || !flexAccountCopyBearer(bearer, sizeof(bearer))){ snprintf(e.code, sizeof(e.code), "network"); snprintf(e.text, sizeof(e.text), "%s", fclErrorText("network")); pushEvent(e); return; }
  WiFiClientSecure sec; HTTPClient h;
  sec.setCACert(flexCloudRootCA()); sec.setHandshakeTimeout(12);
  h.setTimeout(HTTP_TIMEOUT); h.useHTTP10(true);
  char url[256], hdr[80];
  snprintf(url, sizeof(url), "%s/download/%s", gBase, c.id);
  bool ok = h.begin(sec, url);
  snprintf(hdr, sizeof(hdr), "Bearer %s", bearer); memset(bearer, 0, sizeof(bearer));
  if(ok){ h.addHeader("Authorization", hdr); h.addHeader("Accept-Encoding", "identity"); }
  memset(hdr, 0, sizeof(hdr));
  int st = ok ? h.GET() : -1;
  FlexFsStream* f = nullptr;
  FclSha sha; fclShaStart(&sha);
  uint64_t got = 0;
  if(st == 200 && (f = flexFsOpenWrite(VIEW_PATH)) != nullptr){
    WiFiClient* s = h.getStreamPtr();
    uint32_t waited = 0;
    while(got < c.size && s){
      int av = s->available();
      if(av <= 0){ if(!s->connected() || waited > HTTP_TIMEOUT) break; vTaskDelay(pdMS_TO_TICKS(5)); waited += 5; continue; }
      int r = s->read(gIo, (size_t)(av > (int)IO_CAP ? IO_CAP : (size_t)av));
      if(r <= 0 || !flexFsStreamWrite(f, gIo, (size_t)r)) break;
      fclShaUpdate(&sha, gIo, (size_t)r); got += (uint64_t)r; waited = 0;
    }
    flexFsStreamClose(f);
  }
  h.end();
  char hex[FCL_SHA_HEX]; fclShaFinishHex(&sha, hex);
  if(st == 200 && got == c.size && (!c.sha[0] || !strcmp(hex, c.sha))){
    e.kind = FCE_VIEW_READY; e.ok = true;
    snprintf(e.localPath, sizeof(e.localPath), "%s", VIEW_PATH);
  } else {
    flexFsDelete(VIEW_PATH);
    const char* code = st == 401 ? "auth_required" : st == 404 ? "not_found" : got != c.size ? "network" : "checksum_mismatch";
    snprintf(e.code, sizeof(e.code), "%s", code);
    snprintf(e.text, sizeof(e.text), "%s", fclErrorText(code));
    if(st == 401) flexAccountReportRejected();
  }
  pushEvent(e);
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
//  Tarea principal
// ======================================================================
static void taskStep(){
  refreshNet();
  Cmd c;
  if(popCmd(c)){
    if(netReady()){ if(c.type == CMD_VIEW) fetchView(c); else runCmd(c); }
    else {
      lock(); uint8_t net = gStatus.net; unlock();
      const char* why = net == FCN_OFFLINE ? "no_wifi" : net == FCN_AUTH ? "auth_required" : net == FCN_NO_ACCOUNT ? "no_account" : "network";
      if(c.type == CMD_LIST || c.type == CMD_REFRESH || c.type == CMD_MORE) listError(why);
      else if(c.type == CMD_VIEW){
        FlexCloudEvent e; memset(&e, 0, sizeof(e));
        e.kind = FCE_VIEW_FAILED; e.opId = c.opId;
        snprintf(e.code, sizeof(e.code), "%s", why); snprintf(e.text, sizeof(e.text), "%s", fclErrorText(why));
        pushEvent(e);
      }
      else opDone(c.opId, -1, why, "");
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
  // Las miniaturas solo se piden con algo en pantalla: no hace falta mirar gActive.
  if(thumbWanted()){ fetchThumb(); return; }
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
struct StNet { WiFiClientSecure* sec; HTTPClient* http; uint32_t pos; uint32_t left; uint32_t gen; uint8_t fails; uint32_t retryAt; uint32_t idleMs; };
static StNet gStNet;

static void stConnClose(){
  if(gStNet.http){ gStNet.http->end(); delete gStNet.http; gStNet.http = nullptr; }
  if(gStNet.sec){ gStNet.sec->stop(); delete gStNet.sec; gStNet.sec = nullptr; }
  gStNet.left = 0;
}

static void stSetState(uint8_t state, const char* err){
  stLock(); gSt.state = state; snprintf(gSt.err, sizeof(gSt.err), "%s", err ? err : ""); stUnlock();
}

static bool stConnOpen(const char* fileId, uint32_t off, uint32_t size){
  stConnClose();
  char bearer[64];
  if(!flexAccountCopyBearer(bearer, sizeof(bearer))) return false;
  gStNet.sec = new WiFiClientSecure();
  gStNet.http = new HTTPClient();
  gStNet.sec->setCACert(flexCloudRootCA());
  gStNet.sec->setHandshakeTimeout(12);
  gStNet.http->setTimeout(HTTP_TIMEOUT);
  gStNet.http->setConnectTimeout(HTTP_TIMEOUT);
  gStNet.http->useHTTP10(true);                 // cuerpo sin trocear: se lee tal cual
  char url[256], hdr[80];
  snprintf(url, sizeof(url), "%s/download/%s", gBase, fileId);
  if(!gStNet.http->begin(*gStNet.sec, url)){ memset(bearer, 0, sizeof(bearer)); stConnClose(); return false; }
  snprintf(hdr, sizeof(hdr), "Bearer %s", bearer); memset(bearer, 0, sizeof(bearer));
  gStNet.http->addHeader("Authorization", hdr); memset(hdr, 0, sizeof(hdr));
  gStNet.http->addHeader("Accept-Encoding", "identity");
  snprintf(hdr, sizeof(hdr), "bytes=%lu-", (unsigned long)off);
  gStNet.http->addHeader("Range", hdr);
  int st = gStNet.http->GET();
  if(st == 401) flexAccountReportRejected();
  bool ok = st == 206 || (st == 200 && off == 0);
  int cl = ok ? gStNet.http->getSize() : -1;
  if(!ok || (cl >= 0 && (uint32_t)cl != size - off)){
    Serial.printf("[CLOUD] streaming: rango %lu rechazado (HTTP %d)\n", (unsigned long)off, st);
    stConnClose();
    return false;
  }
  gStNet.pos = off; gStNet.left = size - off;
  return true;
}

static void streamStep(){
  stLock();
  bool open = gSt.open;
  uint32_t gen = gSt.gen, size = gSt.size, off = 0, len = 0;
  char fileId[FCL_ID_MAX]; snprintf(fileId, sizeof(fileId), "%s", gSt.fileId);
  int slot = open ? fclCacheNextFetch(&gSt.cache, &off, &len) : -1;
  uint8_t* dst = slot >= 0 ? fclCacheSlot(&gSt.cache, slot) : nullptr;
  if(slot >= 0) gSt.busy = true;
  stUnlock();
  if(!open){ if(gStNet.http) stConnClose(); return; }
  if(gStNet.gen != gen){ stConnClose(); gStNet.gen = gen; gStNet.fails = 0; gStNet.retryAt = 0; }
  if(slot < 0){
    // Nada que traer ahora (la ventana por delante esta llena). Una conexion
    // parada mucho rato se cierra; se vuelve a abrir al seguir.
    if(gStNet.http && millis() - gStNet.idleMs > 20000u) stConnClose();
    return;
  }
  auto giveBack = [&](){ stLock(); if(gSt.gen == gen) fclCacheAbort(&gSt.cache, slot); gSt.busy = false; stUnlock(); };
  if(gStNet.retryAt && (int32_t)(millis() - gStNet.retryAt) < 0){ giveBack(); vTaskDelay(pdMS_TO_TICKS(20)); return; }
  if(!netUsable()){ giveBack(); stSetState(FCS_WAITING_NET, fclErrorText("no_wifi")); vTaskDelay(pdMS_TO_TICKS(200)); return; }
  if(!gStNet.http || gStNet.pos != off){
    if(!stConnOpen(fileId, off, size)){
      giveBack();
      if(gStNet.fails < 250) gStNet.fails++;
      gStNet.retryAt = millis() + (gStNet.fails < 4 ? 500u * gStNet.fails : fclBackoffMs(gStNet.fails - 3));
      stSetState(gStNet.fails >= 6 ? FCS_ERROR : FCS_WAITING_NET, fclErrorText(gStNet.fails >= 6 ? "server" : "network"));
      return;
    }
  }
  // Se rellena el hueco SIN el cerrojo: esta en LOADING y nadie mas lo toca.
  WiFiClient* s = gStNet.http->getStreamPtr();
  uint32_t got = 0, waited = 0;
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
    if(same){ if(gStNet.fails < 250) gStNet.fails++; gStNet.retryAt = millis() + 300u * gStNet.fails; stSetState(FCS_WAITING_NET, "Reconectando..."); }
    return;
  }
  gStNet.pos += len; gStNet.left -= len; gStNet.fails = 0; gStNet.idleMs = millis();
  stLock();
  if(gSt.gen == gen){ fclCacheCommit(&gSt.cache, slot); if(gSt.state != FCS_STREAMING){ gSt.state = FCS_STREAMING; gSt.err[0] = 0; } }
  gSt.busy = false;
  stUnlock();
}

static void streamTask(void*){
  for(;;){
    streamStep();
    stLock(); bool open = gSt.open; stUnlock();
    if(open) vTaskDelay(pdMS_TO_TICKS(1));
    else ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
  }
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
  // medias): se vuelven a anunciar hasta que la interfaz los confirme.
  for(int i = 0; i < FCL_JOBS_MAX; i++){
    FclJob& x = gJ->jobs[i];
    gRt[i].phase = x.state == FCL_JOB_DONE ? FCX_DONE : x.state == FCL_JOB_FAILED ? FCX_FAILED : FCX_QUEUED;
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
void flexCloudBegin(){
  if(!gLock) gLock = xSemaphoreCreateMutex();
  if(!gStLock) gStLock = xSemaphoreCreateMutex();
  if(!allocState()){ Serial.println(F("[CLOUD] sin memoria para Flex Cloud")); return; }
  memset(&gStatus, 0, sizeof(gStatus));
  memset(&gListInfo, 0, sizeof(gListInfo));
  memset(gRt, 0, sizeof(gRt));
  gStatus.net = FCN_NO_ACCOUNT;
  snprintf(gStatus.netText, sizeof(gStatus.netText), "%s", fclErrorText("no_account"));
  gCmdHead = gCmdN = 0; gEvHead = gEvN = 0;
  loadJournal();
  reemitPending();
  gMeDue = true;
  if(flexFsReady()) flexFsDelete(VIEW_PATH);              // la copia del visor no sobrevive a un reinicio
  if(!gTask) xTaskCreate(cloudTask, "flex-cloud", 16384, nullptr, 1, &gTask);
  if(!gStTask) xTaskCreate(streamTask, "flex-cloud-st", 10240, nullptr, 1, &gStTask);
  int pend = fclJournalCount(gJ, FCL_JOB_QUEUED);
  if(pend) Serial.printf("[CLOUD] %d transferencia(s) pendientes del arranque anterior\n", pend);
}

void flexCloudSetActive(bool active){
  bool was = gActive;
  gActive = active;
  if(active && !was){ gMeDue = true; notifyTask(); }
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

const char* flexCloudNetText(uint8_t net){
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
  Cmd c; memset(&c, 0, sizeof(c)); c.type = CMD_LIST;
  return pushCmd(c);
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
int flexCloudListCopy(FclItem* dst, int start, int cap){
  if(!dst || cap <= 0 || !gList) return 0;
  lock();
  int n = gListInfo.count - start;
  if(n > cap) n = cap;
  if(n > 0) memcpy(dst, gList + start, sizeof(FclItem) * (size_t)n);
  unlock();
  return n > 0 ? n : 0;
}

static uint32_t opFor(uint8_t type, const FclItem* it, const char* text, const char* id = nullptr){
  if(!gLock) return 0;
  Cmd c; memset(&c, 0, sizeof(c));
  c.type = type;
  lock(); c.opId = gNextOp++; unlock();
  if(it){ snprintf(c.id, sizeof(c.id), "%s", it->id); c.folder = it->isFolder; c.size = it->size; snprintf(c.sha, sizeof(c.sha), "%s", it->sha256); }
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
uint32_t flexCloudFetchForView(const FclItem* it){ return it && !it->isFolder ? opFor(CMD_VIEW, it, it->name) : 0; }

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
  if(!localPath || !localPath[0] || !flexFsExists(localPath)) return 0;
  const char* base = strrchr(localPath, '/');
  const char* nm = name && name[0] ? name : (base ? base + 1 : localPath);
  flags &= FCL_JF_FREE_LOCAL | FCL_JF_TO_LIBRARY | FCL_JF_FROM_LIBRARY;    // las internas no se piden
  return addJob(FCL_JOB_UPLOAD, localPath, nm, parentId, nullptr, 0, nullptr, mlId,
                (uint8_t)(flags | (mlId ? FCL_JF_FROM_LIBRARY : 0)));
}

uint32_t flexCloudDownload(const FclItem* it, uint8_t flags){
  if(!it || it->isFolder || !it->sha256[0]) return 0;           // sin huella no se puede verificar
  flags &= FCL_JF_TO_LIBRARY;
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
  if(!gJ) return false;
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
  if(!it || it->isFolder || !gStLock || it->size == 0 || it->size > 0xFFFFFFF0ull) return false;
  stLock();
  if(!gSt.arena && !gSt.busy){
    // UNA reserva, la primera vez: se reutiliza en todos los videos.
    gSt.arena = (uint8_t*)heap_caps_malloc((size_t)FLEX_CLOUD_STREAM_BLOCK * FLEX_CLOUD_STREAM_BLOCKS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  if(!gSt.arena){ stUnlock(); return false; }
  gSt.open = true;
  gSt.gen++;
  gSt.size = (uint32_t)it->size;
  snprintf(gSt.fileId, sizeof(gSt.fileId), "%s", it->id);
  fclCacheInit(&gSt.cache, gSt.arena, FLEX_CLOUD_STREAM_BLOCK, FLEX_CLOUD_STREAM_BLOCKS, gSt.size);
  gSt.state = FCS_OPENING; gSt.err[0] = 0;
  stUnlock();
  Serial.printf("[CLOUD] streaming: %s (%lu bytes)\n", it->id, (unsigned long)it->size);
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
  stUnlock();
  if(r < 0) stNotify();
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
void flexCloudTestStreamStep(){ streamStep(); }
void flexCloudTestPowerCycle(){
  // Se pierde la RAM (los objetos de red tambien); el diario sigue en "flash".
  runnerClose();
  stConnClose();
  stLock(); gSt.open = false; gSt.gen++; gSt.busy = false; stUnlock();
  for(int i = 0; i < FLEX_CLOUD_THUMBS; i++){ psFree(gThumbs[i].px); memset(&gThumbs[i], 0, sizeof(gThumbs[i])); }
  if(gApi){ gApi->http.end(); gApi->sec.stop(); }
  gActive = false; gNetFails = 0; gNetRetryAt = 0; gNextMeMs = 0; gAuthWait = false;
  flexCloudBegin();
}
#endif
