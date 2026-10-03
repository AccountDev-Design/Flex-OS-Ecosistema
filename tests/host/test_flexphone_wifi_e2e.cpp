// #############################################################
//  test_flexphone_wifi_e2e.cpp  ·  EL ENLACE DE FLEX PHONE, DE PUNTA A PUNTA
//  ------------------------------------------------------------
//  QUE ES
//  ------
//  La tarea de red REAL del P4 (fpwTask, FlexOS_FlexPhone_WiFi.h) y la
//  maquina de estados REAL (FlexOS_FlexPhone_Link.cpp), con HILOS y SOCKETS
//  de verdad, hablando con el WifiLinkServer REAL del APK (Kotlin, en la
//  JVM; ver tests/link/LinkDevServer.kt). Lo arranca link_e2e.sh.
//
//  POR QUE EXISTE
//  --------------
//  Hasta aqui el enlace se probaba por mitades: tests/host/ con un telefono
//  simulado en C++ y tests/link/ con un reloj simulado en Kotlin. Ninguna
//  ejecutaba fpwTask, que es justo donde se vio el ciclo
//  CONECTADO -> DESCONECTADO cada pocos segundos. Aqui las dos mitades
//  REALES se encuentran, y se mide lo que el usuario ve: cuanto dura cada
//  estado.
//
//  QUE NO CUBRE (y esta dicho en docs/DIAGNOSTICO-ENLACE.md)
//  ---------------------------------------------------------
//  La radio, el transporte SDIO P4<->C6, Android (servicio, Doze, ahorro de
//  bateria) ni el descubrimiento UDP. Eso solo se prueba con el P4 y el A55.
// #############################################################
#include "linkstub/Arduino.h"
#include "linkstub/WiFi.h"
#include "linkstub/WiFiUdp.h"
#include "linkstub/freertos/FreeRTOS.h"
#include "linkstub/freertos/task.h"
#include "linkstub/freertos/semphr.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
#include <string>
#include <condition_variable>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#define FLEXOS_ENABLE_WIFI 1
static bool gAirplane = false;

#include "../../FlexOS_Ultra/FlexOS_FlexPhone.h"
#include "../../FlexOS_Ultra/FlexOS_FlexPhone_Link.h"
#include "../../FlexOS_Ultra/FlexOS_FlexAuth.h"
#include "../../FlexOS_Ultra/FlexOS_FlexPhone_WiFi.h"

// =============================================================
//  Core de arduino-esp32 simulado
// =============================================================
static const auto g_t0 = std::chrono::steady_clock::now();
unsigned long millis(){
  return (unsigned long)std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - g_t0).count();
}
void delay(unsigned long ms){ std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
__FlexSerial Serial;
static bool gVerbose = false;
int __FlexSerial::printf(const char* fmt, ...){
  if(!gVerbose) return 0;
  va_list ap; va_start(ap, fmt); int n = vfprintf(stderr, fmt, ap); va_end(ap); return n;
}
void __FlexSerial::println(const char* s){ if(gVerbose) fprintf(stderr, "%s\n", s); }

static std::atomic<bool> gWifiUp{true};
__FlexWiFi WiFi;
wl_status_t __FlexWiFi::status(){ return gWifiUp ? WL_CONNECTED : WL_DISCONNECTED; }

// ---- FreeRTOS: hilos y mutex de verdad ----
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char*, uint32_t, void* arg,
                                   UBaseType_t, TaskHandle_t* handle, BaseType_t){
  std::thread* t = new std::thread([fn, arg]{ fn(arg); });
  t->detach();
  if(handle) *handle = (TaskHandle_t)t;
  return pdPASS;
}
void vTaskDelete(TaskHandle_t){}
void vTaskDelay(TickType_t ticks){ std::this_thread::sleep_for(std::chrono::milliseconds(ticks ? ticks : 1)); }
struct RealMutex { std::mutex m; };
SemaphoreHandle_t xSemaphoreCreateMutex(){ return new RealMutex(); }
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t){ ((RealMutex*)s)->m.lock(); return pdTRUE; }
BaseType_t xSemaphoreGive(SemaphoreHandle_t s){ ((RealMutex*)s)->m.unlock(); return pdTRUE; }

// ---- WiFiClient con la semantica de NetworkClient 3.2.1 ----
int WiFiClient::connect(const char* host, uint16_t port, int32_t timeoutMs){
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0) return 0;
  int fl = ::fcntl(fd, F_GETFL, 0);
  ::fcntl(fd, F_SETFL, fl | O_NONBLOCK);
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port);
  a.sin_addr.s_addr = inet_addr(host);
  int rc = ::connect(fd, (sockaddr*)&a, sizeof(a));
  if(rc != 0 && errno == EINPROGRESS){
    pollfd p{fd, POLLOUT, 0};
    if(::poll(&p, 1, timeoutMs) <= 0){ ::close(fd); return 0; }
    int err = 0; socklen_t l = sizeof(err);
    ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &l);
    if(err){ ::close(fd); return 0; }
  } else if(rc != 0){ ::close(fd); return 0; }
  int one = 1; ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  m_fd = fd; m_connected = true;
  return 1;
}
void WiFiClient::stop(){ if(m_fd >= 0){ ::close(m_fd); m_fd = -1; } m_connected = false; }
uint8_t WiFiClient::connected(){
  if(m_fd == -1 && m_connected) stop();
  if(m_connected){
    uint8_t dummy;
    // lwIP deja errno = 0 en un recv() que termina bien, y un FIN (EOF) termina
    // bien: recv() devuelve 0. Con errno = 0 el `switch` de abajo cae en
    // `default` y connected() sigue diciendo "si". Linux no toca errno en ese
    // caso, asi que se pone a 0 a mano para reproducir lo que hace la placa.
    errno = 0;
    int res = ::recv(m_fd, &dummy, 1, MSG_DONTWAIT | MSG_PEEK);
    if(res <= 0){
      switch(errno){
        case EWOULDBLOCK: case ENOENT: m_connected = true; break;
        case ENOTCONN: case EPIPE: case ECONNRESET: case ECONNREFUSED: case ECONNABORTED:
          m_connected = false; break;
        default: m_connected = true; break;
      }
    } else m_connected = true;
  }
  return m_connected;
}
int WiFiClient::available(){
  if(m_fd < 0) return 0;
  int n = 0;
  if(::ioctl(m_fd, FIONREAD, &n) < 0) return 0;
  return n;
}
int WiFiClient::read(uint8_t* buf, size_t size){
  if(m_fd < 0) return -1;
  ssize_t r = ::recv(m_fd, buf, size, MSG_DONTWAIT);
  if(r < 0) return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
  return (int)r;
}
// Copia de NetworkClient::write (3.2.1): select() de 1 s, 10 intentos, puede
// devolver MENOS de lo pedido, y un error que no sea EAGAIN cierra el socket.
size_t WiFiClient::write(const uint8_t* buf, size_t size){
  if(!m_connected || m_fd < 0) return 0;
  int retry = 10; size_t total = 0, remaining = size;
  while(retry){
    fd_set set; FD_ZERO(&set); FD_SET(m_fd, &set);
    timeval tv{0, 1000000};
    retry--;
    if(::select(m_fd + 1, NULL, &set, NULL, &tv) < 0) return 0;
    if(FD_ISSET(m_fd, &set)){
      ssize_t res = ::send(m_fd, buf, remaining, MSG_DONTWAIT | MSG_NOSIGNAL);
      if(res > 0){
        total += (size_t)res;
        if(total >= size) retry = 0; else { buf += res; remaining -= (size_t)res; retry = 10; }
      } else if(res < 0){
        if(errno != EAGAIN){ stop(); retry = 0; }
      }
    }
  }
  return total;
}

// =============================================================
//  Pruebas
// =============================================================
static int gChecks = 0, gFails = 0;
#define CHECK(cond, ...) do { gChecks++; if(!(cond)){ gFails++; \
  printf("  FALLO %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while(0)

static FlexPhoneLink  L;
static FlexPhoneModel M;
static std::mutex     gLm;                       // protege L y M entre el hilo de tick y el de prueba
static std::atomic<bool> gRun{true};
static std::atomic<int>  gReadyCount{0};          // veces que el enlace PASO a READY
static std::atomic<int>  gLostCount{0};           // veces que SALIO de READY
static std::atomic<unsigned long> gReadySince{0}; // cuando entro en READY la ultima vez
static std::vector<unsigned long> gUp;            // cuanto duro cada tramo en READY (ms)
static std::string gCtrl;                         // fifo de control del telefono

static uint32_t rnd(){ static uint32_t s = 0x1234567u; s = s * 1664525u + 1013904223u; return s; }

static void ctrl(const char* cmd){
  if(gCtrl.empty()) return;
  FILE* f = fopen(gCtrl.c_str(), "a");
  if(f){ fprintf(f, "%s\n", cmd); fclose(f); }
}

// El bucle principal de Flex OS: un tick por frame, 60 Hz.
static void tickLoop(){
  uint8_t last = 0xFF;
  while(gRun){
    {
      std::lock_guard<std::mutex> lk(gLm);
      const uint32_t now = (uint32_t)millis();
      flexPhoneLinkTick(&L, &M, now);
      if(L.state != last){
        if(L.state == FLP_LS_READY){ gReadyCount++; gReadySince = now; }
        else if(last == FLP_LS_READY){ gLostCount++; gUp.push_back(now - gReadySince); }
        if(gVerbose) fprintf(stderr, "[%6u] P4 estado %s -> %s\n", now,
                             last == 0xFF ? "-" : flexPhoneLinkStateName(last), flexPhoneLinkStateName(L.state));
        last = L.state;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
}

static bool waitReady(unsigned long maxMs){
  const unsigned long t0 = millis();
  while(millis() - t0 < maxMs){
    { std::lock_guard<std::mutex> lk(gLm); if(L.state == FLP_LS_READY) return true; }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}
// Mantiene la vista de READY durante `ms` y devuelve cuantas veces se perdio.
static int holdAndCountLosses(unsigned long ms){
  const int before = gLostCount;
  const unsigned long t0 = millis();
  while(millis() - t0 < ms) std::this_thread::sleep_for(std::chrono::milliseconds(20));
  return gLostCount - before;
}

int main(int argc, char** argv){
  (void)argc; (void)argv;
  setvbuf(stdout, NULL, _IOLBF, 0);
  gVerbose = getenv("E2E_VERBOSE") != NULL;
  const char* keyHex  = getenv("E2E_KEY");
  const char* phoneId = getenv("E2E_PHONEID") ? getenv("E2E_PHONEID") : "phone-prueba";
  const int   port    = getenv("E2E_PORT") ? atoi(getenv("E2E_PORT")) : FLPW_TCP_PORT;
  const bool  quick   = getenv("E2E_QUICK") != NULL;
  if(getenv("E2E_CTRL")) gCtrl = getenv("E2E_CTRL");
  if(!keyHex || strlen(keyHex) != FLXA_KEY_SIZE * 2){ fprintf(stderr, "falta E2E_KEY (hex de %d bytes)\n", FLXA_KEY_SIZE); return 2; }

  printf("=== FlexOS · enlace de Flex Phone: tarea de red REAL del P4 <-> WifiLinkServer REAL ===\n");

  // ---- Un P4 ya vinculado con ese telefono ----
  flexPhoneModelInit(&M);
  flexPhoneLinkInit(&L);
  flexPhoneLinkSetIdentity(&L, "flexos-a1b2c3d4e5f6");
  flexPhoneLinkSetRandom(&L, rnd);
  flexPhoneLinkSetTransport(&L, flexPhoneWifiTransport());
  FlexPhoneBond b; memset(&b, 0, sizeof(b));
  for(int i = 0; i < FLXA_KEY_SIZE; i++){ unsigned v; sscanf(keyHex + 2 * i, "%2x", &v); b.key[i] = (uint8_t)v; }
  snprintf(b.peerId, sizeof(b.peerId), "%s", phoneId);
  b.valid = true;
  flexPhoneLinkSetBond(&L, &b);
  const uint8_t lo[4] = {127, 0, 0, 1};
  flexPhoneWifiSetHost(lo, (uint16_t)port);          // direccion fija: el UDP no se prueba aqui
  CHECK(flexPhoneLinkStart(&L), "el enlace no arranco");
  std::thread ticker(tickLoop);

  // ---------------------------------------------------------
  printf("[enlace] conecta y llega a READY con el telefono real\n");
  CHECK(waitReady(15000), "no llego a READY en 15 s");
  { std::lock_guard<std::mutex> lk(gLm);
    CHECK(L.bond.valid, "el vinculo se perdio"); }

  // ---------------------------------------------------------
  const unsigned long steady = quick ? 12000 : 30000;
  printf("[enlace] sesion en reposo: %lu s sin una sola caida (latidos de 8 s incluidos)\n", steady / 1000);
  {
    const int lost = holdAndCountLosses(steady);
    CHECK(lost == 0, "el enlace SALIO de READY %d vez/veces estando en reposo (ciclo)", lost);
    std::lock_guard<std::mutex> lk(gLm);
    CHECK(L.state == FLP_LS_READY, "no esta en READY tras el reposo (estado %s)", flexPhoneLinkStateName(L.state));
    CHECK(M.caps.valid, "no llegaron las capacidades del telefono");
    CHECK(M.phone.battery == 37, "no llego el estado del telefono (bateria=%d)", (int)M.phone.battery);
    uint16_t ms = 0;
    CHECK(quick || flexPhoneLinkLatency(&L, &ms), "tras >8 s no hay latencia medida (no llega el PONG)");
    printf("   tramos en READY: %zu  recon=%u  latencia=%s\n", gUp.size() + 1, (unsigned)L.nReconnects,
           flexPhoneLinkLatency(&L, &ms) ? "medida" : "--");
  }

  // ---------------------------------------------------------
  printf("[enlace] ordenes con acuse (RELAY_START, FIND_START): llegan UNA vez\n");
  {
    std::lock_guard<std::mutex> lk(gLm);
    CHECK(flexPhoneLinkSend(&L, FLNK_T_RELAY_START, NULL, 0, true), "no se encolo RELAY_START");
    CHECK(flexPhoneLinkSend(&L, FLNK_T_FIND_START,  NULL, 0, true), "no se encolo FIND_START");
  }
  const int lost = holdAndCountLosses(quick ? 9500 : 20000);       // mas que el plazo completo de reintentos (7,5 s)
  CHECK(lost == 0, "el enlace se cayo al mandar ordenes (%d)", lost);
  ctrl("dump");
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  printf("   ENVIADO relay_start=1 find_start=1\n");

  // ---------------------------------------------------------
  printf("[enlace] el telefono cierra el socket (FIN): el P4 lo ve en seguida, no \"conectado\" sobre un socket muerto\n");
  if(!gCtrl.empty()){
    // Con el canal en reposo: ningun latido ni orden pendiente que pueda delatar
    // el cierre por escritura. Solo ve el FIN quien lo mira de verdad.
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    const int lostBefore = gLostCount;
    const unsigned long t0 = millis();
    ctrl("stop");
    while(gLostCount == lostBefore && millis() - t0 < 20000) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const unsigned long seen = millis() - t0;
    printf("   el P4 dejo de decir CONECTADO %lu ms despues del cierre del telefono\n", seen);
    CHECK(gLostCount > lostBefore, "el P4 sigue diciendo CONECTADO sobre un socket que el telefono cerro");
    CHECK(seen <= 2000, "el P4 tardo %lu ms en enterarse del cierre (limite 2000 ms)", seen);
  }

  // ---------------------------------------------------------
  printf("[enlace] el servicio del telefono se reinicia: el P4 vuelve solo y se queda\n");
  if(!gCtrl.empty()){
    const int readyBefore = gReadyCount;
    // El cierre ya ocurrio arriba; aqui solo se vuelve a levantar el servicio.
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    ctrl("start");
    CHECK(waitReady(45000), "no volvio a READY tras reiniciar el servicio del telefono");
    CHECK(gReadyCount > readyBefore, "no se registro la reconexion");
    const int l2 = holdAndCountLosses(quick ? 8000 : 15000);
    CHECK(l2 == 0, "tras reconectar, el enlace volvio a caerse %d vez/veces", l2);
    std::lock_guard<std::mutex> lk(gLm);
    CHECK(M.caps.valid && M.phone.battery == 37, "tras reconectar no volvieron las capacidades/estado");
  } else printf("   (sin E2E_CTRL: omitido)\n");

  // ---------------------------------------------------------
  printf("[enlace] la Wi-Fi del P4 se va 3 s y vuelve\n");
  {
    const int readyBefore = gReadyCount;
    gWifiUp = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    gWifiUp = true;
    CHECK(waitReady(45000), "no volvio a READY tras volver la Wi-Fi");
    CHECK(gReadyCount > readyBefore, "no se registro la reconexion tras la Wi-Fi");
    const int l3 = holdAndCountLosses(quick ? 8000 : 15000);
    CHECK(l3 == 0, "tras la Wi-Fi, el enlace volvio a caerse %d vez/veces", l3);
  }

  // ---------------------------------------------------------
  printf("[enlace] el registro en RAM cuenta la verdad (aperturas, cierres y su motivo)\n");
  {
    const uint8_t n = flexPhoneWifiLogCount();
    CHECK(n >= 6, "el registro tiene %u entradas; se esperaban >= 6 (3 aperturas y 2-3 cierres)", (unsigned)n);
    int opens = 0, closes = 0, peer = 0, wifi = 0;
    for(uint8_t i = 0; i < n; i++){
      FpwEvent e; flexPhoneWifiLogGet(i, &e);
      char line[96]; flexPhoneWifiLogLine(e, line, sizeof(line));
      if(gVerbose) fprintf(stderr, "   log[%u] %s\n", (unsigned)i, line);
      if(e.kind == FPWE_OPEN) opens++;
      if(e.kind == FPWE_CLOSE){ closes++; if(e.reason == FPWR_PEER_CLOSED) peer++; if(e.reason == FPWR_WIFI) wifi++; }
    }
    CHECK(opens >= 3, "faltan aperturas en el registro (%d)", opens);
    CHECK(closes >= 2, "faltan cierres en el registro (%d)", closes);
    CHECK(peer >= 1, "el cierre del telefono no quedo apuntado como tal");
    CHECK(wifi >= 1, "el corte de Wi-Fi no quedo apuntado como tal");
  }

  gRun = false;
  ticker.join();
  flexPhoneWifiTransport()->stop(flexPhoneWifiTransport());
  ctrl("dump");
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  printf("=== %d comprobaciones, %d fallos ===\n", gChecks, gFails);
  return gFails ? 1 : 0;
}
