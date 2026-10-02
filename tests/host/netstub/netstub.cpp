#include "netstub.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <random>
#include <algorithm>

std::function<NetResponse(const NetRequest&)> gNetHandler;
bool gNetDnsOk = true;
std::vector<NetRequest> gNetLog;
bool gNetWifi = false;
unsigned long gNetNowMs = 0;
unsigned gNetTaskCreates = 0;

__FlexSerial Serial;
EspClass ESP;
__FlexWiFi WiFi;

unsigned long millis(){ return gNetNowMs; }
unsigned long micros(){ return gNetNowMs * 1000ul; }
void delay(unsigned long ms){ gNetNowMs += ms; }
void yield() {}
void netstubAdvance(unsigned long ms){ gNetNowMs += ms; }

int __FlexSerial::printf(const char* fmt, ...){
  if(!getenv("NETSTUB_VERBOSE")) return 0;
  va_list ap; va_start(ap, fmt); int n = vfprintf(stderr, fmt, ap); va_end(ap); return n;
}
void __FlexSerial::emit(const char* s){ if(getenv("NETSTUB_VERBOSE")) fprintf(stderr, "%s\n", s); }

wl_status_t __FlexWiFi::status(){ return gNetWifi ? WL_CONNECTED : WL_DISCONNECTED; }

static std::mt19937& rng(){ static std::mt19937 g(12345); return g; }
void esp_fill_random(void* buf, size_t len){ uint8_t* p = (uint8_t*)buf; for(size_t i = 0; i < len; i++) p[i] = (uint8_t)rng()(); }
uint32_t esp_random(){ return rng()(); }
uint32_t esp_get_free_heap_size(){ return 300u * 1024u; }
// PSRAM con contabilidad: la prueba mira el PICO de memoria (gNetPsPeak) para
// demostrar que un archivo de 160 MB no se carga entero en ningun momento.
static std::map<void*, size_t>& psMap(){ static std::map<void*, size_t> m; return m; }
size_t gNetPsNow = 0, gNetPsPeak = 0;
size_t gNetPsFailAbove = 0;                         // >0: falla toda reserva mayor que esto
void* heap_caps_malloc(size_t n, uint32_t){
  if(gNetPsFailAbove && n > gNetPsFailAbove) return nullptr;
  void* p = malloc(n);
  if(p){ psMap()[p] = n; gNetPsNow += n; if(gNetPsNow > gNetPsPeak) gNetPsPeak = gNetPsNow; }
  return p;
}
void heap_caps_free(void* p){
  if(!p) return;
  auto it = psMap().find(p);
  if(it != psMap().end()){ gNetPsNow -= it->second; psMap().erase(it); }
  free(p);
}
// La SRAM INTERNA es la escasa en el P4 (la PSRAM son 32 MB): la prueba la
// programa. Cualquier otra pregunta (PSRAM / por defecto) sigue siendo holgada.
size_t gNetInternalFree = 200u << 10, gNetInternalBlock = 100u << 10;
size_t heap_caps_get_free_size(uint32_t caps){ return (caps & MALLOC_CAP_INTERNAL) ? gNetInternalFree : (8u << 20); }
size_t heap_caps_get_largest_free_block(uint32_t caps){ return (caps & MALLOC_CAP_INTERNAL) ? gNetInternalBlock : (4u << 20); }

std::vector<NetTaskRec> gNetTasks;
unsigned gNetTaskFail = 0;
static BaseType_t netTaskMake(const char* n, uint32_t s, TaskHandle_t* h, int core){
  if(gNetTaskFail){ gNetTaskFail--; if(h) *h = nullptr; return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY; }
  gNetTaskCreates++; gNetTasks.push_back({ n ? n : "", core, s });
  if(h) *h = (TaskHandle_t)(uintptr_t)gNetTaskCreates;
  return pdPASS;
}
BaseType_t xTaskCreate(TaskFunction_t, const char* n, uint32_t s, void*, UBaseType_t, TaskHandle_t* h){
  return netTaskMake(n, s, h, -1);
}
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char* n, uint32_t s, void*, UBaseType_t, TaskHandle_t* h, BaseType_t core){
  return netTaskMake(n, s, h, (int)core);
}
void vTaskDelete(TaskHandle_t) {}
unsigned gNetTaskNotifies = 0;
BaseType_t xTaskNotifyGive(TaskHandle_t){ gNetTaskNotifies++; return pdPASS; }
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t t){ gNetNowMs += t ? t : 1; return 0; }
std::function<void()> gNetOnDelay;
void vTaskDelay(TickType_t t){ gNetNowMs += t ? t : 1; if(gNetOnDelay) gNetOnDelay(); }
// Mutex que DETECTA anidamiento (en la placa es no recursivo: un doble
// lock() seria un bloqueo eterno).
struct NetMutex { bool held = false; };
SemaphoreHandle_t xSemaphoreCreateMutex(){ return new NetMutex(); }
SemaphoreHandle_t xSemaphoreCreateBinary(){ return new NetMutex(); }
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t){
  NetMutex* m = (NetMutex*)s;
  if(m->held){ fprintf(stderr, "netstub: mutex tomado dos veces (bloqueo en la placa)\n"); abort(); }
  m->held = true; return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t s){ ((NetMutex*)s)->held = false; return pdTRUE; }
void vSemaphoreDelete(SemaphoreHandle_t s){ delete (NetMutex*)s; }

// ------------------------------- NVS -------------------------------------
std::map<std::string, NetNvsValue>& netstubNvs(){ static std::map<std::string, NetNvsValue> m; return m; }
unsigned& netstubNvsWriteCount(){ static unsigned n = 0; return n; }
bool& netstubNvsBroken(){ static bool b = false; return b; }
void netstubNvsWipe(){ netstubNvs().clear(); netstubNvsWriteCount() = 0; netstubNvsBroken() = false; }

bool Preferences::begin(const char* name, bool readOnly){
  if(netstubNvsBroken() || !name || strlen(name) > 15) return false;   // NVS_KEY_NAME_MAX_SIZE - 1
  ns_ = name; ro_ = readOnly; started_ = true; return true;
}
bool Preferences::valid(const char* key) const { return started_ && key && strlen(key) <= 15; }
bool Preferences::clear(){
  if(!started_ || ro_) return false;
  auto& m = netstubNvs(); std::string pre = ns_ + "\x1f";
  for(auto it = m.begin(); it != m.end();) it = it->first.compare(0, pre.size(), pre) ? std::next(it) : m.erase(it);
  netstubNvsWriteCount()++; return true;
}
bool Preferences::remove(const char* key){
  if(!valid(key) || ro_) return false;
  netstubNvsWriteCount()++; return netstubNvs().erase(full(key)) > 0;
}
bool Preferences::isKey(const char* key){ return valid(key) && netstubNvs().count(full(key)); }
size_t Preferences::putNum(const char* key, unsigned long long v, size_t width){
  if(!valid(key) || ro_) return 0;
  NetNvsValue& e = netstubNvs()[full(key)]; e = NetNvsValue(); e.type = 1; e.num = v;
  netstubNvsWriteCount()++; return width;
}
unsigned long long Preferences::getNum(const char* key, unsigned long long d){
  if(!valid(key)) return d;
  auto it = netstubNvs().find(full(key));
  return (it != netstubNvs().end() && it->second.type == 1) ? it->second.num : d;
}
size_t Preferences::putString(const char* key, const char* value){
  if(!valid(key) || !value || ro_) return 0;
  NetNvsValue& e = netstubNvs()[full(key)]; e = NetNvsValue(); e.type = 2; e.str = value;
  netstubNvsWriteCount()++; return strlen(value);
}
size_t Preferences::getString(const char* key, char* value, size_t maxLen){
  if(!valid(key) || !value || !maxLen) return 0;
  auto it = netstubNvs().find(full(key));
  if(it == netstubNvs().end() || it->second.type != 2) return 0;
  size_t len = it->second.str.size() + 1;                  // nvs_get_str: CON terminador
  if(len > maxLen) return 0;
  memcpy(value, it->second.str.c_str(), len);
  return len;
}
String Preferences::getString(const char* key, const String defaultValue){
  if(!valid(key)) return defaultValue;
  auto it = netstubNvs().find(full(key));
  return (it != netstubNvs().end() && it->second.type == 2) ? String(it->second.str) : defaultValue;
}
size_t Preferences::putBytes(const char* key, const void* value, size_t len){
  if(!valid(key) || !value || !len || ro_) return 0;
  NetNvsValue& e = netstubNvs()[full(key)]; e = NetNvsValue(); e.type = 3;
  e.blob.assign((const uint8_t*)value, (const uint8_t*)value + len);
  netstubNvsWriteCount()++; return len;
}
size_t Preferences::getBytesLength(const char* key){
  if(!valid(key)) return 0;
  auto it = netstubNvs().find(full(key));
  return (it != netstubNvs().end() && it->second.type == 3) ? it->second.blob.size() : 0;
}
size_t Preferences::getBytes(const char* key, void* buf, size_t maxLen){
  size_t n = getBytesLength(key);
  if(!n || !buf || n > maxLen) return 0;
  memcpy(buf, netstubNvs()[full(key)].blob.data(), n);
  return n;
}

// ------------------------------- HTTP ------------------------------------
static std::string lower(std::string s){ for(auto& c : s) c = (char)tolower((unsigned char)c); return s; }

bool HTTPClient::begin(WiFiClient& client, const char* url){
  if(!url) return false;
  client_ = &client; url_ = url; reqHeaders_.clear(); respHeaders_.clear(); active_ = false; size_ = -1;
  return !strncmp(url, "https://", 8) || !strncmp(url, "http://", 7);
}
void HTTPClient::end(){ active_ = false; body_.reset("", 0); }
void HTTPClient::addHeader(const String& name, const String& value, bool, bool replace){
  std::string k = name.str();
  if(replace) for(auto& h : reqHeaders_) if(lower(h.first) == lower(k)){ h.second = value.str(); return; }
  reqHeaders_.push_back({k, value.str()});
}
String HTTPClient::header(const char* name){
  auto it = respHeaders_.find(lower(name ? name : ""));
  return it == respHeaders_.end() ? String() : String(it->second);
}
bool HTTPClient::hasHeader(const char* name){ return respHeaders_.count(lower(name ? name : "")) > 0; }
int HTTPClient::sendRequest(const char* type, uint8_t* payload, size_t size){
  return dispatch(type, payload ? std::string((const char*)payload, size) : std::string());
}
// Mismo bucle que arduino-esp32 3.2.1: lee mientras available() > -1 y falten
// bytes; con available() == 0 la placa espera (delay(1)) MIENTRAS la conexion
// siga viva, es decir, para siempre. Aqui eso es un fallo de la prueba.
int HTTPClient::sendRequest(const char* type, Stream* stream, size_t size){
  if(!stream) return HTTPC_ERROR_NO_STREAM;
  if(!gNetWifi) return HTTPC_ERROR_CONNECTION_REFUSED;
  std::string b;
  uint8_t tmp[1460];
  long len = size ? (long)size : -1;
  while(stream->available() > -1 && (len > 0 || len == -1)){
    int av = stream->available();
    if(!av){
      if(len == -1) break;
      fprintf(stderr, "netstub: sendRequest(Stream) se quedaria esperando para siempre en la placa (available()==0 con %ld bytes pendientes)\n", len);
      abort();
    }
    size_t want = (size_t)av;
    if(len > 0 && want > (size_t)len) want = (size_t)len;
    if(want > sizeof(tmp)) want = sizeof(tmp);
    size_t got = stream->readBytes(tmp, want);
    b.append((const char*)tmp, got);
    if(len > 0) len -= (long)want;
  }
  if(size && b.size() != size) return HTTPC_ERROR_SEND_PAYLOAD_FAILED;
  return dispatch(type, b);
}
int HTTPClient::dispatch(const char* type, const std::string& body){
  if(!gNetWifi) return HTTPC_ERROR_CONNECTION_REFUSED;
  NetRequest rq; rq.method = type ? type : "GET"; rq.url = url_; rq.body = body;
  for(auto& h : reqHeaders_) rq.headers[lower(h.first)] = h.second;
  rq.https = !url_.compare(0, 8, "https://");
  rq.at = gNetNowMs;
  WiFiClientSecure* sec = dynamic_cast<WiFiClientSecure*>(client_);
  if(sec){ rq.tlsInsecure = sec->insecure; rq.tlsCa = sec->caCert; }
  gNetLog.push_back(rq);
  NetResponse rs;
  if(gNetHandler) rs = gNetHandler(rq); else rs.status = HTTPC_ERROR_CONNECTION_REFUSED;
  gNetNowMs += rs.latencyMs;                                   // el servidor tarda (o el intento se agota)
  if(sec) sec->lastErr = rs.status < 0 ? rs.tlsError : 0;
  if(rs.status < 0){ active_ = false; return rs.status; }
  respHeaders_.clear();
  for(auto& h : rs.headers) respHeaders_[lower(h.first)] = h.second;
  body_.reset(rs.body, rs.cutAt);
  size_ = rs.chunked ? -1 : (int)rs.body.size();
  active_ = true;
  return rs.status;
}
String HTTPClient::getString(){
  std::string s; uint8_t tmp[512]; size_t n;
  while((n = body_.readBytes(tmp, sizeof(tmp))) > 0) s.append((const char*)tmp, n);
  return String(s);
}

int HTTPClient::writeToStream(Stream* stream){
  if(!stream) return HTTPC_ERROR_NO_STREAM;
  if(!active_) return HTTPC_ERROR_NOT_CONNECTED;
  uint8_t tmp[1460]; size_t n; int total = 0;
  while((n = body_.readBytes(tmp, sizeof(tmp))) > 0){
    size_t w = stream->write(tmp, n);
    total += (int)w;
    if(w != n) return HTTPC_ERROR_STREAM_WRITE;
  }
  if(body_.cut()) return size_ < 0 ? HTTPC_ERROR_READ_TIMEOUT : HTTPC_ERROR_STREAM_WRITE;
  return total;
}

void netstubReset(){
  gNetHandler = nullptr; gNetLog.clear(); gNetWifi = false; gNetNowMs = 1000; gNetDnsOk = true;
  gNetInternalFree = 200u << 10; gNetInternalBlock = 100u << 10;
  gNetTasks.clear(); gNetTaskFail = 0; gNetOnDelay = nullptr;
}
