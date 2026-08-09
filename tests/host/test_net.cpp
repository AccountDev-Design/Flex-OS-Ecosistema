// #############################################################
//  test_net.cpp  ·  EL TRANSPORTE DE VERDAD, SOBRE SOCKETS DE VERDAD
//  ------------------------------------------------------------
//  QUE PRUEBA ESTO Y POR QUE EXISTE
//  --------------------------------
//  Las otras pruebas de host sustituyen WiFiClient por un doble que no
//  hace nada: sirven para el ciclo de vida y para el dibujo, pero NO
//  ejercitan ni una linea del transporte. Y el fallo que se vio en la
//  ESP32-P4 -- "se conecta, autentica y despues se queda cargando para
//  siempre, sin un solo frame" -- estaba justo ahi.
//
//  Aqui, en cambio:
//
//   · WiFiClient es un socket TCP POSIX de verdad.
//   · xTaskCreatePinnedToCore lanza un HILO de verdad, asi que la
//     tarea de red del firmware (brNetTask) corre en paralelo con el
//     hilo grafico, igual que en la placa.
//   · Los semaforos, mutex y colas son los de verdad (std::mutex /
//     std::condition_variable), no contadores de juguete.
//   · Al otro lado hay un servidor WebSocket que habla FBP/1 y que
//     REPRODUCE EL MODO DE FALLO: manda el primer FRAME -- decenas de
//     KB -- troceado y con una PAUSA de 400 ms en mitad del mensaje,
//     que es exactamente lo que hace una Wi-Fi domestica con un JPEG.
//
//  Con el codigo anterior (un unico plazo de 120 ms para sondear y
//  para leer) esa pausa abandonaba la lectura A MITAD DE MENSAJE; el
//  socket seguia "conectado", asi que no se reconectaba, y los bytes
//  que quedaban en el flujo se interpretaban como una cabecera
//  WebSocket. Desincronizacion permanente: nunca mas llegaba un frame.
//  La comprobacion de abajo -- "la imagen llega, se decodifica y se
//  dibuja" -- falla con aquel codigo y pasa con este.
//
//  No sustituye a la prueba en la placa; cubre la parte que en la
//  placa no se puede instrumentar sin un analizador de red.
// #############################################################

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <deque>

#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "../../FlexOS_Browser.h"
#include "../../FlexOS_JPEG.h"
// Las mismas cabeceras simuladas que ve el modulo bajo prueba: aqui se
// IMPLEMENTAN las funciones que alli solo se declaran.
#include "Arduino.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "WiFi.h"
#include "WiFiClientSecure.h"
#include "HTTPClient.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

// =============================================================
//  Arduino / ESP-IDF minimo
// =============================================================
static std::chrono::steady_clock::time_point g_t0 = std::chrono::steady_clock::now();
unsigned long millis(){
  return (unsigned long)std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - g_t0).count();
}
void delay(unsigned long ms){ std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
uint32_t esp_random(){ return (uint32_t)std::rand(); }
uint32_t esp_get_free_heap_size(){ return 300000; }
__FlexSerial Serial;

void* heap_caps_malloc(size_t n, uint32_t){ return std::malloc(n); }
size_t heap_caps_get_free_size(uint32_t){ return 300000; }
size_t heap_caps_get_total_size(uint32_t){ return 4u * 1024 * 1024; }

// =============================================================
//  WiFiClient SOBRE UN SOCKET TCP DE VERDAD
// =============================================================
static uint16_t g_serverPort = 0;

int WiFiClient::connect(const char* host, uint16_t port){
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0) return 0;
  int one = 1;
  ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = inet_addr(host && host[0] ? host : "127.0.0.1");
  if(::connect(fd, (sockaddr*)&a, sizeof(a)) != 0){ ::close(fd); return 0; }
  // No bloqueante: el firmware sondea con available() y cede CPU, que es
  // exactamente lo que hace en la placa.
  int fl = ::fcntl(fd, F_GETFL, 0);
  ::fcntl(fd, F_SETFL, fl | O_NONBLOCK);
  m_fd = fd;
  return 1;
}
void WiFiClient::stop(){ if(m_fd >= 0){ ::close(m_fd); m_fd = -1; } }
uint8_t WiFiClient::connected(){ return m_fd >= 0 ? 1 : 0; }
int WiFiClient::available(){
  if(m_fd < 0) return 0;
  int n = 0;
  if(::ioctl(m_fd, FIONREAD, &n) != 0) return 0;
  return n;
}
int WiFiClient::read(uint8_t* buf, size_t size){
  if(m_fd < 0) return -1;
  ssize_t r = ::recv(m_fd, buf, size, 0);
  if(r == 0){ stop(); return -1; }              // el otro lado cerro
  if(r < 0) return 0;                            // aun no hay nada
  return (int)r;
}
size_t WiFiClient::write(const uint8_t* buf, size_t size){
  if(m_fd < 0) return 0;
  size_t sent = 0;
  while(sent < size){
    ssize_t w = ::send(m_fd, buf + sent, size - sent, MSG_NOSIGNAL);
    if(w > 0){ sent += (size_t)w; continue; }
    if(w < 0){
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    break;
  }
  return sent;
}

// =============================================================
//  FreeRTOS DE VERDAD: hilos, mutex, semaforos y colas
// =============================================================
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char*, uint32_t, void* arg,
                                   UBaseType_t, TaskHandle_t* handle, BaseType_t){
  std::thread* t = new std::thread([fn, arg]{ fn(arg); });
  t->detach();
  if(handle) *handle = (TaskHandle_t)t;
  return pdTRUE;
}
void vTaskDelete(TaskHandle_t){}
void vTaskDelay(TickType_t ticks){
  std::this_thread::sleep_for(std::chrono::milliseconds(ticks ? ticks : 1));
}

struct RealSem {
  std::mutex m; std::condition_variable cv; int count;
  explicit RealSem(int c) : count(c) {}
};
SemaphoreHandle_t xSemaphoreCreateMutex(){ return new RealSem(1); }
SemaphoreHandle_t xSemaphoreCreateBinary(){ return new RealSem(0); }
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks){
  RealSem* p = (RealSem*)s; if(!p) return pdFALSE;
  std::unique_lock<std::mutex> lk(p->m);
  if(ticks == portMAX_DELAY){
    p->cv.wait(lk, [&]{ return p->count > 0; });
  } else if(!p->cv.wait_for(lk, std::chrono::milliseconds(ticks), [&]{ return p->count > 0; })){
    return pdFALSE;
  }
  p->count--;
  return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t s){
  RealSem* p = (RealSem*)s; if(!p) return pdFALSE;
  { std::lock_guard<std::mutex> lk(p->m); p->count++; }
  p->cv.notify_one();
  return pdTRUE;
}
void vSemaphoreDelete(SemaphoreHandle_t s){ delete (RealSem*)s; }

struct RealQueue {
  std::mutex m;
  size_t item, cap;
  std::deque<std::vector<uint8_t>> items;
};
QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t itemSize){
  RealQueue* q = new RealQueue();
  q->item = itemSize; q->cap = len;
  return q;
}
BaseType_t xQueueSend(QueueHandle_t h, const void* item, TickType_t){
  RealQueue* q = (RealQueue*)h; if(!q) return pdFALSE;
  std::lock_guard<std::mutex> lk(q->m);
  if(q->items.size() >= q->cap) return pdFALSE;
  q->items.emplace_back((const uint8_t*)item, (const uint8_t*)item + q->item);
  return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t h, void* item, TickType_t){
  RealQueue* q = (RealQueue*)h; if(!q) return pdFALSE;
  std::lock_guard<std::mutex> lk(q->m);
  if(q->items.empty()) return pdFALSE;
  std::memcpy(item, q->items.front().data(), q->item);
  q->items.pop_front();
  return pdTRUE;
}
void vQueueDelete(QueueHandle_t h){ delete (RealQueue*)h; }

// =============================================================
//  Puente brHost*: lienzo en memoria
// =============================================================
#define SCRW 480
#define SCRH 800
static uint16_t g_canvas[SCRW * SCRH];
static std::atomic<long> g_blitted{0};

int  brHostScrW(){ return SCRW; }
int  brHostScrH(){ return SCRH; }
void brHostContentRect(int* x, int* y, int* w, int* h){
  *x = 0; *y = 96; *w = SCRW; *h = SCRH - 96 - 64;
}
bool brHostDark(){ return true; }
bool brHostGlass(){ return false; }
bool brHostHosted(){ return false; }
bool brHostOnline(){ return true; }
const char* brHostDeviceName(){ return "prueba-transporte"; }
uint32_t brHostMillis(){ return (uint32_t)millis(); }
int brHostKeyboardTop(){ return SCRH; }
static BrTouch g_touch;
void brHostGetTouch(BrTouch* t){ *t = g_touch; }
void brHostConsumeTouch(){
  g_touch.pressed = g_touch.released = g_touch.tap = g_touch.moved = false;
  g_touch.swipeUp = g_touch.swipeDown = g_touch.swipeLeft = g_touch.swipeRight = false;
}
uint16_t brHostRGB(uint8_t r, uint8_t g, uint8_t b){
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
uint16_t brHostColor(int){ return 0x1234; }
void brHostFillRect(int, int, int, int, uint16_t){}
void brHostFillRoundRect(int, int, int, int, int, uint16_t){}
void brHostDrawRoundRect(int, int, int, int, int, uint16_t){}
void brHostFillCircle(int, int, int, uint16_t){}
void brHostDrawCircle(int, int, int, uint16_t){}
void brHostTriangle(int,int,int,int,int,int,uint16_t){}
void brHostStroke(float, float, float, float, float, uint16_t){}
void brHostText(int, int, const char*, int, uint16_t){}
void brHostTextC(int, int, const char*, int, uint16_t){}
void brHostTextR(int, int, const char*, int, uint16_t){}
void brHostTextClip(int, int, const char*, int, uint16_t, int){}
int  brHostTextW(const char* s, int size){ return (int)std::strlen(s ? s : "") * 6 * size; }
void brHostClip(int, int){}
void brHostClipReset(){}
void brHostFlush(int, int){}
void brHostBlitRow(int x, int y, int w, const uint16_t* src){
  if(!src || w <= 0) return;
  if(y < 0 || y >= SCRH) return;
  for(int i = 0; i < w && x + i < SCRW; i++) g_canvas[y * SCRW + x + i] = src[i];
  g_blitted += w;
}
void* brHostAlloc(size_t n, bool){ return std::malloc(n); }
void  brHostFree(void* p){ std::free(p); }
size_t brHostFreeHeap(){ return 300000; }
size_t brHostFreePsram(){ return 4u * 1024 * 1024; }

static std::map<std::string, std::string> g_prefsS;
static std::map<std::string, int>         g_prefsI;
bool brHostPrefsGetStr(const char* k, char* out, size_t n, const char* def){
  auto it = g_prefsS.find(k);
  std::snprintf(out, n, "%s", it != g_prefsS.end() ? it->second.c_str() : (def ? def : ""));
  return out[0] != 0;
}
void brHostPrefsPutStr(const char* k, const char* v){ g_prefsS[k] = v ? v : ""; }
int  brHostPrefsGetInt(const char* k, int def){
  auto it = g_prefsI.find(k);
  return it != g_prefsI.end() ? it->second : def;
}
void brHostPrefsPutInt(const char* k, int v){ g_prefsI[k] = v; }
bool brHostFsReady(){ return false; }
int  brHostFileRead(const char*, void*, size_t){ return -1; }
bool brHostFileWrite(const char*, const void*, size_t){ return false; }
bool brHostFileDelete(const char*){ return false; }

// =============================================================
//  UN SERVIDOR WEBSOCKET / FBP DE VERDAD, CON EL FALLO REPRODUCIDO
// =============================================================
static std::vector<uint8_t> g_jpeg;              // imagen que se manda
static std::atomic<bool> g_srvStop{false};
static std::atomic<bool> g_gotHello{false};
static std::atomic<bool> g_gotNavigate{false};
static std::string       g_navUrl;

static bool sendAll(int fd, const uint8_t* p, size_t n){
  size_t s = 0;
  while(s < n){
    ssize_t w = ::send(fd, p + s, n - s, MSG_NOSIGNAL);
    if(w <= 0) return false;
    s += (size_t)w;
  }
  return true;
}
static bool recvAll(int fd, uint8_t* p, size_t n){
  size_t g = 0;
  while(g < n){
    ssize_t r = ::recv(fd, p + g, n - g, 0);
    if(r <= 0) return false;
    g += (size_t)r;
  }
  return true;
}

// Trama WebSocket del SERVIDOR (sin mascara, RFC 6455).
static std::vector<uint8_t> wsServerFrame(const std::vector<uint8_t>& payload){
  std::vector<uint8_t> out;
  out.push_back(0x82);                               // FIN + binario
  size_t n = payload.size();
  if(n < 126) out.push_back((uint8_t)n);
  else if(n <= 0xFFFF){
    out.push_back(126);
    out.push_back((uint8_t)(n >> 8)); out.push_back((uint8_t)(n & 0xFF));
  } else {
    out.push_back(127);
    for(int i = 7; i >= 0; i--) out.push_back((uint8_t)(((uint64_t)n >> (8 * i)) & 0xFF));
  }
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

// Lee UNA trama de aplicacion del cliente (siempre enmascarada).
static bool wsReadClient(int fd, std::vector<uint8_t>& out){
  uint8_t h[2];
  if(!recvAll(fd, h, 2)) return false;
  uint8_t op = (uint8_t)(h[0] & 0x0F);
  bool masked = (h[1] & 0x80) != 0;
  uint64_t len = (uint64_t)(h[1] & 0x7F);
  if(len == 126){ uint8_t e[2]; if(!recvAll(fd, e, 2)) return false; len = ((uint64_t)e[0] << 8) | e[1]; }
  else if(len == 127){ uint8_t e[8]; if(!recvAll(fd, e, 8)) return false; len = 0; for(int i = 0; i < 8; i++) len = (len << 8) | e[i]; }
  uint8_t mask[4] = {0,0,0,0};
  if(masked && !recvAll(fd, mask, 4)) return false;
  out.resize((size_t)len);
  if(len && !recvAll(fd, out.data(), (size_t)len)) return false;
  if(masked) for(size_t i = 0; i < out.size(); i++) out[i] ^= mask[i & 3];
  if(op == 0x8) return false;                        // CLOSE
  return true;
}

static void putU16(std::vector<uint8_t>& v, uint16_t x){ v.push_back((uint8_t)(x & 0xFF)); v.push_back((uint8_t)(x >> 8)); }
static void putU32(std::vector<uint8_t>& v, uint32_t x){ for(int i = 0; i < 4; i++) v.push_back((uint8_t)((x >> (8 * i)) & 0xFF)); }
static void putStr(std::vector<uint8_t>& v, const std::string& s){
  putU16(v, (uint16_t)s.size());
  v.insert(v.end(), s.begin(), s.end());
}
static std::vector<uint8_t> fbpMsg(uint8_t type, uint8_t ch, uint16_t seq,
                                   const std::vector<uint8_t>& payload){
  std::vector<uint8_t> m(FBP_HDR_SIZE);
  fbpWriteHeader(m.data(), type, 0, ch, seq, (uint32_t)payload.size());
  m.insert(m.end(), payload.begin(), payload.end());
  return m;
}

static void serverThread(int listenFd){
  int fd = ::accept(listenFd, nullptr, nullptr);
  if(fd < 0) return;
  int one = 1;
  ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

  // ---- apreton de manos: se lee la peticion y se responde 101 ----
  std::string req;
  char c;
  while(req.size() < 4096 && req.find("\r\n\r\n") == std::string::npos){
    ssize_t r = ::recv(fd, &c, 1, 0);
    if(r <= 0){ ::close(fd); return; }
    req.push_back(c);
  }
  std::string key;
  {
    const char* k = "Sec-WebSocket-Key:";
    size_t p = req.find(k);
    if(p == std::string::npos){ ::close(fd); return; }
    p += std::strlen(k);
    while(p < req.size() && req[p] == ' ') p++;
    size_t e = req.find("\r\n", p);
    key = req.substr(p, e - p);
  }
  char accept[40];
  if(!flexBrWsAccept(key.c_str(), accept, sizeof(accept))){ ::close(fd); return; }
  std::string resp = "HTTP/1.1 101 Switching Protocols\r\n"
                     "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                     "Sec-WebSocket-Accept: " + std::string(accept) + "\r\n"
                     "Sec-WebSocket-Protocol: fbp.v1\r\n\r\n";
  if(!sendAll(fd, (const uint8_t*)resp.data(), resp.size())){ ::close(fd); return; }

  // ---- HELLO -> WELCOME ----
  std::vector<uint8_t> msg;
  while(!g_srvStop){
    if(!wsReadClient(fd, msg)){ ::close(fd); return; }
    FbpHeader h;
    if(!fbpReadHeader(msg.data(), msg.size(), &h)) continue;
    if(h.type == FBP_C_HELLO){ g_gotHello = true; break; }
  }
  {
    std::vector<uint8_t> p;
    p.push_back(FBP_VERSION);
    putU16(p, SCRW); putU16(p, 600);
    putU32(p, 0xFFFFFFFFu);
    putU16(p, 4);
    putU32(p, FBP_PAYLOAD_MAX);
    putStr(p, "sesion-de-prueba");
    auto f = wsServerFrame(fbpMsg(FBP_S_WELCOME, 0, 1, p));
    if(!sendAll(fd, f.data(), f.size())){ ::close(fd); return; }
  }

  // ---- se espera un NAVIGATE ----
  while(!g_srvStop){
    if(!wsReadClient(fd, msg)){ ::close(fd); return; }
    FbpHeader h;
    if(!fbpReadHeader(msg.data(), msg.size(), &h)) continue;
    if(h.type == FBP_C_NAVIGATE){
      if(h.len >= 2){
        uint16_t l = (uint16_t)(msg[FBP_HDR_SIZE] | (msg[FBP_HDR_SIZE + 1] << 8));
        if((size_t)FBP_HDR_SIZE + 2 + l <= msg.size())
          g_navUrl.assign((const char*)msg.data() + FBP_HDR_SIZE + 2, l);
      }
      g_gotNavigate = true;
      break;
    }
  }

  // ---- FRAME grande, TROCEADO Y CON UNA PAUSA EN MITAD ----
  //
  //  Esto es el nucleo de la prueba. El mensaje se parte en trozos de
  //  512 B y a la mitad se para 400 ms. Un lector con un unico plazo
  //  corto abandona AQUI, deja el flujo desalineado y no vuelve a
  //  entender nada. El lector correcto espera, porque el mensaje ya
  //  habia empezado.
  {
    // La banda se anuncia con el tamano REAL de la imagen, como hace el
    // servicio: asi se dibuja 1:1 y la prueba mide el camino normal.
    FlexJpegInfo info;
    flexJpegProbe(g_jpeg.data(), g_jpeg.size(), &info);
    std::vector<uint8_t> p;
    putU16(p, 0); putU16(p, 0);
    putU16(p, (uint16_t)info.width); putU16(p, (uint16_t)info.height);
    p.push_back(FBP_IMG_JPEG);
    p.push_back(FBP_FR_KEYFRAME);
    putU32(p, 1);
    putU32(p, (uint32_t)g_jpeg.size());
    p.insert(p.end(), g_jpeg.begin(), g_jpeg.end());
    auto f = wsServerFrame(fbpMsg(FBP_S_FRAME, 1, 2, p));

    size_t mitad = f.size() / 2, enviado = 0;
    bool pausado = false;
    while(enviado < f.size()){
      size_t c = f.size() - enviado;
      if(c > 512) c = 512;
      if(!sendAll(fd, f.data() + enviado, c)) { ::close(fd); return; }
      enviado += c;
      if(!pausado && enviado >= mitad){
        pausado = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
  }

  // Se atiende lo que quede (ACK, PING) hasta que la prueba termine.
  while(!g_srvStop){
    if(!wsReadClient(fd, msg)) break;
  }
  ::close(fd);
}

// =============================================================
//  Comprobaciones
// =============================================================
static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

static bool cargaJpeg(const char* dir){
  std::string p = std::string(dir) + "/page480.jpg";
  FILE* f = std::fopen(p.c_str(), "rb");
  if(!f) return false;
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  g_jpeg.resize((size_t)n);
  size_t rd = std::fread(g_jpeg.data(), 1, (size_t)n, f);
  std::fclose(f);
  return rd == (size_t)n && n > 0;
}

int main(int argc, char** argv){
  std::printf("=== FlexOS · transporte del navegador sobre sockets reales ===\n");
  const char* fx = argc > 1 ? argv[1] : "../fixtures";
  if(!cargaJpeg(fx)){
    std::printf("   (omitida: falta %s/page480.jpg -- make -C tests/host fixtures)\n", fx);
    return 0;
  }

  // ---- servidor ----
  int lfd = ::socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  ::setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = inet_addr("127.0.0.1");
  a.sin_port = 0;                                   // puerto libre
  if(::bind(lfd, (sockaddr*)&a, sizeof(a)) != 0 || ::listen(lfd, 1) != 0){
    std::printf("   (omitida: no se pudo abrir un socket de escucha)\n");
    return 0;
  }
  socklen_t al = sizeof(a);
  ::getsockname(lfd, (sockaddr*)&a, &al);
  g_serverPort = ntohs(a.sin_port);
  std::thread srv(serverThread, lfd);

  // ---- dispositivo ----
  char url[128];
  std::snprintf(url, sizeof(url), "ws://127.0.0.1:%u/v1/session", (unsigned)g_serverPort);
  g_prefsS["brsrv"] = url;
  g_prefsS["brtok"] = "credencial-de-prueba-1234567890";
  g_prefsI["brinsec"] = 1;                          // ws:// sin TLS permitido
  g_prefsI["brlocal"] = 1;                          // destino en red local

  flexBrowserBegin();
  flexBrowserEnter();

  // Se espera a que la sesion este lista (WELCOME).
  uint32_t t0 = (uint32_t)millis();
  while(flexBrowserNetState() != BRN_READY && millis() - t0 < 8000){
    flexBrowserTick();
    delay(10);
  }
  CHECK(g_gotHello.load(), "el dispositivo no envio HELLO");
  CHECK(flexBrowserNetState() == BRN_READY,
        "la sesion no llego a READY (estado %d)", flexBrowserNetState());

  // Navegar POR EL CAMINO REAL: se toca la barra de direcciones, se
  // escribe y se pulsa Intro. Nada de atajos internos.
  {
    int intentos = 0;
    while(!flexBrowserKeyboardOpen() && intentos < 14){
      g_touch.tap = true; g_touch.released = true;
      g_touch.x = SCRW / 2; g_touch.y = 96 + 30 + intentos * 12;
      flexBrowserTick();
      g_touch.tap = false; g_touch.released = false;
      flexBrowserTick();
      intentos++;
    }
    CHECK(flexBrowserKeyboardOpen(), "no se pudo abrir la barra de direcciones");
    const char* destino = "http://192.168.50.50/pagina";
    for(const char* p = destino; *p; p++){ char c[2] = { *p, 0 }; flexBrowserKeyText(c); }
    CHECK(!std::strcmp(flexBrowserEditText(), destino),
          "el omnibox tiene \"%s\"", flexBrowserEditText());
    flexBrowserKeyEnter();
    flexBrowserTick();
  }

  // Se espera el NAVIGATE y despues el FRAME. El hilo grafico sigue
  // llamando a tick(), igual que el bucle del sistema.
  t0 = (uint32_t)millis();
  while(!g_gotNavigate.load() && millis() - t0 < 5000){
    flexBrowserTick();
    delay(10);
  }

  t0 = (uint32_t)millis();
  while(flexBrowserStats()->framesOk == 0 && millis() - t0 < 20000){
    flexBrowserTick();
    delay(5);
  }

  const BrStats* st = flexBrowserStats();
  std::printf("   navegacion: \"%s\"\n", g_navUrl.c_str());
  std::printf("   frames ok=%u malos=%u descartados=%u  bytes=%u  reconexiones=%u\n",
              (unsigned)st->framesOk, (unsigned)st->framesBad,
              (unsigned)st->framesDropped, (unsigned)st->bytesIn,
              (unsigned)st->reconnects);

  CHECK(g_gotNavigate.load(), "el servidor no recibio ningun NAVIGATE");
  // ESTA es la comprobacion del fallo: con el lector antiguo, la pausa
  // de 400 ms en mitad del mensaje dejaba framesOk en 0 para siempre.
  CHECK(st->framesOk >= 1,
        "no se recibio/decodifico ninguna imagen pese a que el servidor la envio "
        "(bytes=%u, malos=%u, reconexiones=%u)",
        (unsigned)st->bytesIn, (unsigned)st->framesBad, (unsigned)st->reconnects);
  CHECK(g_blitted.load() > 1000,
        "la imagen no se pinto: solo %ld pixeles", g_blitted.load());
  CHECK(st->reconnects == 0,
        "hubo %u reconexiones: el tramado se perdio", (unsigned)st->reconnects);

  g_srvStop = true;
  flexBrowserExit();
  ::shutdown(lfd, SHUT_RDWR);
  ::close(lfd);
  if(srv.joinable()) srv.detach();

  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
