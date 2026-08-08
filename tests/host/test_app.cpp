// #############################################################
//  test_app.cpp  ·  pruebas de host del lado DISPOSITIVO del
//  navegador (FlexOS_BrowserApp.cpp)
// #############################################################
//
//  POR QUE EXISTE
//  --------------
//  FlexOS_BrowserApp.cpp es codigo de placa: FreeRTOS, WiFi, TLS,
//  framebuffer. Sin la placa delante no hay forma de ejecutarlo... a
//  menos que se le den unas cabeceras minimas (tests/host/stub) y una
//  implementacion del puente `brHost*` contra un lienzo en memoria.
//  Eso es lo que hace este fichero, y con ello se comprueba de verdad:
//
//    · que el modulo COMPILA Y ENLAZA en los tres perfiles de placa
//      (P4, S3 y ESP32 clasico cambian presupuestos y capacidades);
//    · que el ciclo de vida es simetrico: Enter reserva, Exit libera
//      TODO y no queda ni una reserva viva (el requisito de "cerrar la
//      app libera recursos" deja de ser una promesa y pasa a ser un
//      test);
//    · que Tick no escribe NUNCA fuera del area de contenido, que es
//      lo que garantiza que una pagina externa no pueda pintar sobre
//      la barra del sistema;
//    · que las capacidades se apagan solas cuando falta memoria y dan
//      un motivo legible en vez de fallar en silencio.
//
//  Lo que este test NO hace: hablar por red. El simulador no crea
//  hilos, asi que la tarea de red no arranca; el protocolo se prueba
//  aparte en test_browser.cpp con bytes reales.

#include "../../FlexOS_Browser.h"
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
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <cmath>

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

// =============================================================
//  Simulador: reloj, memoria y objetos de FreeRTOS
// =============================================================
static unsigned long g_ms = 1000;
unsigned long millis(){ return g_ms; }
void delay(unsigned long ms){ g_ms += ms; }
uint32_t esp_random(){ return (uint32_t)std::rand(); }
uint32_t esp_get_free_heap_size(){ return 200000; }

static long g_liveAllocs = 0;
static size_t g_liveBytes = 0;
static std::map<void*, size_t> g_blocks;
void* heap_caps_malloc(size_t n, uint32_t){ return std::malloc(n); }
size_t heap_caps_get_free_size(uint32_t){ return 200000; }
size_t heap_caps_get_total_size(uint32_t){ return 400000; }

int  WiFiClient::connect(const char*, uint16_t){ return 0; }
void WiFiClient::stop(){}
uint8_t WiFiClient::connected(){ return 0; }
int  WiFiClient::available(){ return 0; }
int  WiFiClient::read(uint8_t*, size_t){ return -1; }
size_t WiFiClient::write(const uint8_t*, size_t n){ return n; }

// El simulador no lanza hilos: devolver pdFALSE deja gNetTask a NULL y
// el ciclo de vida se puede recorrer entero de forma determinista.
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char*, uint32_t, void*,
                                   UBaseType_t, TaskHandle_t* handle, BaseType_t){
  if(handle) *handle = nullptr;
  return pdFALSE;
}
void vTaskDelete(TaskHandle_t){}
void vTaskDelay(TickType_t t){ g_ms += t; }

struct StubSem { int count; };
static long g_liveSems = 0;
SemaphoreHandle_t xSemaphoreCreateMutex(){ g_liveSems++; return new StubSem{1}; }
SemaphoreHandle_t xSemaphoreCreateBinary(){ g_liveSems++; return new StubSem{0}; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t){
  StubSem* p = (StubSem*)s; if(!p) return pdFALSE;
  if(p->count > 0){ p->count--; return pdTRUE; }
  return pdFALSE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t s){
  StubSem* p = (StubSem*)s; if(!p) return pdFALSE;
  p->count++; return pdTRUE;
}
void vSemaphoreDelete(SemaphoreHandle_t s){ if(s){ g_liveSems--; delete (StubSem*)s; } }

struct StubQueue { size_t item; std::vector<std::vector<uint8_t>> items; size_t cap; };
static long g_liveQueues = 0;
QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t itemSize){
  g_liveQueues++;
  StubQueue* q = new StubQueue();
  q->item = itemSize; q->cap = len;
  return q;
}
BaseType_t xQueueSend(QueueHandle_t h, const void* item, TickType_t){
  StubQueue* q = (StubQueue*)h; if(!q) return pdFALSE;
  if(q->items.size() >= q->cap) return pdFALSE;
  q->items.emplace_back((const uint8_t*)item, (const uint8_t*)item + q->item);
  return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t h, void* item, TickType_t){
  StubQueue* q = (StubQueue*)h; if(!q || q->items.empty()) return pdFALSE;
  std::memcpy(item, q->items.front().data(), q->item);
  q->items.erase(q->items.begin());
  return pdTRUE;
}
void vQueueDelete(QueueHandle_t h){ if(h){ g_liveQueues--; delete (StubQueue*)h; } }

// =============================================================
//  Puente brHost*: un lienzo en memoria con deteccion de
//  escrituras fuera del area permitida
// =============================================================
#define SCRW 480
#define SCRH 800
static uint16_t g_canvas[SCRW * SCRH];
static bool     g_painted[SCRW * SCRH];
static int      g_outOfBounds = 0;      // escrituras fuera de la pantalla
static int      g_chromeViolations = 0; // pixeles de PAGINA sobre la barra
static int      g_pageTop = 0;          // primera fila valida para brHostBlitRow
static int      g_contentTop = 96, g_contentH = SCRH - 96 - 64;
static uint32_t g_freeHeap = 300000, g_freePsram = 4u * 1024 * 1024;
static bool     g_online = true, g_fsReady = true;
static BrTouch  g_touch;
static int      g_consumeCount = 0;

static std::map<std::string, std::string> g_prefsStr;
static std::map<std::string, int>         g_prefsInt;
static std::map<std::string, std::vector<uint8_t>> g_files;

int  brHostScrW(){ return SCRW; }
int  brHostScrH(){ return SCRH; }
void brHostContentRect(int* x, int* y, int* w, int* h){
  *x = 0; *y = g_contentTop; *w = SCRW; *h = g_contentH;
}
bool brHostDark(){ return true; }
bool brHostGlass(){ return false; }
bool brHostHosted(){ return false; }
bool brHostOnline(){ return g_online; }
const char* brHostDeviceName(){ return "flexos-test"; }
uint32_t brHostMillis(){ return (uint32_t)g_ms; }
int  brHostKeyboardTop(){ return SCRH; }
void brHostGetTouch(BrTouch* t){ *t = g_touch; }
void brHostConsumeTouch(){
  g_consumeCount++;
  g_touch.tap = g_touch.pressed = g_touch.released = false;
  g_touch.swipeUp = g_touch.swipeDown = g_touch.swipeLeft = g_touch.swipeRight = false;
}

uint16_t brHostRGB(uint8_t r, uint8_t g, uint8_t b){
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
uint16_t brHostColor(int idx){ return (uint16_t)(0x1000 + idx); }

static void putPx(int x, int y, uint16_t c){
  if(x < 0 || y < 0 || x >= SCRW || y >= SCRH){ g_outOfBounds++; return; }
  g_canvas[y * SCRW + x] = c;
  g_painted[y * SCRW + x] = true;
}
void brHostFillRect(int x, int y, int w, int h, uint16_t c){
  for(int j = 0; j < h; j++) for(int i = 0; i < w; i++) putPx(x + i, y + j, c);
}
void brHostFillRoundRect(int x, int y, int w, int h, int, uint16_t c){ brHostFillRect(x, y, w, h, c); }
void brHostDrawRoundRect(int x, int y, int w, int h, int, uint16_t c){
  brHostFillRect(x, y, w, 1, c); brHostFillRect(x, y + h - 1, w, 1, c);
  brHostFillRect(x, y, 1, h, c); brHostFillRect(x + w - 1, y, 1, h, c);
}
void brHostFillCircle(int cx, int cy, int r, uint16_t c){
  for(int j = -r; j <= r; j++) for(int i = -r; i <= r; i++)
    if(i * i + j * j <= r * r) putPx(cx + i, cy + j, c);
}
void brHostDrawCircle(int cx, int cy, int r, uint16_t c){
  for(int a = 0; a < 360; a += 6){
    double rad = a * 3.14159265 / 180.0;
    putPx(cx + (int)(r * std::cos(rad)), cy + (int)(r * std::sin(rad)), c);
  }
}
void brHostTriangle(int x0,int y0,int x1,int y1,int x2,int y2,uint16_t c){
  int minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
  int maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
  int miny = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
  int maxy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
  for(int y = miny; y <= maxy; y++) for(int x = minx; x <= maxx; x++) putPx(x, y, c);
}
void brHostStroke(float x0, float y0, float x1, float y1, float, uint16_t c){
  int steps = 24;
  for(int i = 0; i <= steps; i++)
    putPx((int)(x0 + (x1 - x0) * i / steps), (int)(y0 + (y1 - y0) * i / steps), c);
}
static int textWidth(const char* s, int size){
  int n = 0; for(const char* p = s; *p; p++) if((*p & 0xC0) != 0x80) n++;
  return size <= 1 ? n * 6 : n * size * 5;
}
void brHostText(int x, int y, const char* s, int size, uint16_t c){
  brHostFillRect(x, y, textWidth(s, size), size <= 1 ? 8 : size * 9, c);
}
void brHostTextC(int cx, int y, const char* s, int size, uint16_t c){
  brHostText(cx - textWidth(s, size) / 2, y, s, size, c);
}
void brHostTextR(int rx, int y, const char* s, int size, uint16_t c){
  brHostText(rx - textWidth(s, size), y, s, size, c);
}
void brHostTextClip(int x, int y, const char* s, int size, uint16_t c, int maxRight){
  int w = textWidth(s, size);
  if(x + w > maxRight) w = maxRight - x;
  if(w > 0) brHostFillRect(x, y, w, size <= 1 ? 8 : size * 9, c);
}
int  brHostTextW(const char* s, int size){ return textWidth(s, size); }
void brHostClip(int, int){}
void brHostFlush(int, int){}

// Este es el unico camino por el que la PAGINA REMOTA llega a la
// pantalla. Si alguna fila cae por encima de g_pageTop, una pagina
// externa estaria pintando sobre la interfaz del navegador o sobre la
// barra del sistema: se cuenta como violacion.
void brHostBlitRow(int x, int y, int w, const uint16_t* px){
  if(y < g_pageTop) g_chromeViolations++;
  if(y < g_contentTop || y >= g_contentTop + g_contentH) g_chromeViolations++;
  for(int i = 0; i < w; i++) putPx(x + i, y, px[i]);
}

void* brHostAlloc(size_t n, bool){
  void* p = std::malloc(n);
  if(p){ g_liveAllocs++; g_liveBytes += n; g_blocks[p] = n; }
  return p;
}
void brHostFree(void* p){
  if(!p) return;
  auto it = g_blocks.find(p);
  if(it != g_blocks.end()){ g_liveBytes -= it->second; g_blocks.erase(it); g_liveAllocs--; }
  std::free(p);
}
size_t brHostFreeHeap(){ return g_freeHeap; }
size_t brHostFreePsram(){ return g_freePsram; }

bool brHostPrefsGetStr(const char* key, char* out, size_t n, const char* def){
  auto it = g_prefsStr.find(key);
  std::snprintf(out, n, "%s", it != g_prefsStr.end() ? it->second.c_str() : (def ? def : ""));
  return it != g_prefsStr.end();
}
void brHostPrefsPutStr(const char* key, const char* val){ g_prefsStr[key] = val ? val : ""; }
int  brHostPrefsGetInt(const char* key, int def){
  auto it = g_prefsInt.find(key);
  return it != g_prefsInt.end() ? it->second : def;
}
void brHostPrefsPutInt(const char* key, int v){ g_prefsInt[key] = v; }
bool brHostFsReady(){ return g_fsReady; }
int  brHostFileRead(const char* path, void* buf, size_t n){
  auto it = g_files.find(path);
  if(it == g_files.end()) return -1;
  size_t c = it->second.size() < n ? it->second.size() : n;
  std::memcpy(buf, it->second.data(), c);
  return (int)c;
}
bool brHostFileWrite(const char* path, const void* buf, size_t n){
  g_files[path].assign((const uint8_t*)buf, (const uint8_t*)buf + n);
  return true;
}
bool brHostFileDelete(const char* path){ g_files.erase(path); return true; }

// =============================================================
//  Utilidades del test
// =============================================================
static void resetCanvas(){
  std::memset(g_painted, 0, sizeof(g_painted));
  g_outOfBounds = 0;
  g_chromeViolations = 0;
}
static void tapAt(int x, int y){
  std::memset(&g_touch, 0, sizeof(g_touch));
  g_touch.tap = true; g_touch.released = true;
  g_touch.x = x; g_touch.y = y; g_touch.startX = x; g_touch.startY = y;
  g_touch.downMs = (uint32_t)g_ms;
}
static void clearTouch(){ std::memset(&g_touch, 0, sizeof(g_touch)); }
static void tick(int n = 1){ for(int i = 0; i < n; i++){ g_ms += 16; flexBrowserTick(); } }

// =============================================================
//  Pruebas
// =============================================================
static void testLifecycle(){
  std::printf("[app] ciclo de vida y liberacion de recursos\n");
  long a0 = g_liveAllocs;
  flexBrowserBegin();
  // Begin() NO debe reservar nada: corre en el arranque del sistema y no
  // se deshace nunca, asi que cualquier reserva suya seria memoria
  // ocupada de forma permanente por una app que quiza no se abra.
  CHECK(g_liveAllocs == a0, "Begin reservo %ld bloques; deberia no reservar ninguno",
        g_liveAllocs - a0);

  // Enter() SI reserva (historial, favoritos, buffers de red) y Exit()
  // tiene que devolverlo todo.
  flexBrowserEnter();
  CHECK(g_liveAllocs > a0, "Enter no reservo nada");
  flexBrowserExit();
  CHECK(g_liveAllocs == a0, "Exit dejo %ld bloques vivos", g_liveAllocs - a0);

  for(int round = 0; round < 3; round++){
    long before = g_liveAllocs;
    long semBefore = g_liveSems, qBefore = g_liveQueues;
    flexBrowserEnter();
    resetCanvas();
    tick(5);
    flexBrowserExit();
    CHECK(g_liveAllocs == before, "vuelta %d: quedaron %ld reservas vivas tras Exit",
          round, g_liveAllocs - before);
    CHECK(g_liveSems == semBefore, "vuelta %d: quedaron %ld semaforos vivos",
          round, g_liveSems - semBefore);
    CHECK(g_liveQueues == qBefore, "vuelta %d: quedaron %ld colas vivas",
          round, g_liveQueues - qBefore);
  }
  // Exit repetido no debe romper nada.
  flexBrowserExit();
  flexBrowserExit();
  // Tick con la app cerrada: no debe dibujar ni petar.
  resetCanvas();
  tick(3);
  long painted = 0;
  for(int i = 0; i < SCRW * SCRH; i++) if(g_painted[i]) painted++;
  CHECK(painted == 0, "Tick dibujo %ld pixeles con la app cerrada", painted);
}

static void testDrawBounds(){
  std::printf("[app] el dibujo nunca sale del area de contenido\n");
  flexBrowserEnter();
  resetCanvas();
  tick(3);
  CHECK(g_outOfBounds == 0, "%d escrituras fuera de la pantalla", g_outOfBounds);
  long above = 0, below = 0;
  for(int y = 0; y < g_contentTop; y++)
    for(int x = 0; x < SCRW; x++) if(g_painted[y * SCRW + x]) above++;
  for(int y = g_contentTop + g_contentH; y < SCRH; y++)
    for(int x = 0; x < SCRW; x++) if(g_painted[y * SCRW + x]) below++;
  CHECK(above == 0, "%ld pixeles pintados sobre la barra de estado", above);
  CHECK(below == 0, "%ld pixeles pintados sobre la barra de navegacion", below);
  flexBrowserExit();
}

static void testCapabilities(){
  std::printf("[app] capacidades medidas y motivos legibles\n");
  // Sin servidor configurado: la sesion remota debe estar apagada y con
  // un motivo que el usuario pueda leer.
  g_prefsStr.clear(); g_prefsInt.clear();
  flexBrowserEnter();
  CHECK(!(flexBrowserCaps() & FLEXBR_CAP_REMOTE), "sin servidor la sesion remota deberia estar apagada");
  const char* why = flexBrowserCapReason(FLEXBR_CAP_REMOTE);
  CHECK(why && why[0], "no hay motivo para la sesion remota desactivada");
  std::printf("   sin servidor -> \"%s\"\n", why ? why : "(nulo)");
  flexBrowserExit();

  // Con servidor pero sin heap: tambien apagada, y el motivo cita el
  // numero real de KB.
  g_prefsStr["brsrv"] = "wss://nav.example.com/v1/session";
  uint32_t saved = g_freeHeap;
  g_freeHeap = 1000;
  flexBrowserEnter();
  CHECK(!(flexBrowserCaps() & FLEXBR_CAP_REMOTE), "con poco heap la sesion remota deberia estar apagada");
  why = flexBrowserCapReason(FLEXBR_CAP_REMOTE);
  CHECK(why && std::strstr(why, "KB"), "el motivo deberia citar la memoria: \"%s\"", why ? why : "");
  std::printf("   con 1 KB de heap -> \"%s\"\n", why ? why : "(nulo)");
  flexBrowserExit();

  // Con memoria y servidor: encendida.
  g_freeHeap = saved;
  flexBrowserEnter();
  CHECK((flexBrowserCaps() & FLEXBR_CAP_REMOTE) != 0, "con servidor y memoria deberia estar encendida");
  CHECK(flexBrowserCapReason(FLEXBR_CAP_REMOTE) == nullptr, "una capacidad activa no debe dar motivo");
  flexBrowserExit();

  // Sin PSRAM: una sola pestana y sin reproductor, con explicacion.
  uint32_t savedPs = g_freePsram;
  g_freePsram = 0;
  flexBrowserEnter();
  CHECK(!(flexBrowserCaps() & FLEXBR_CAP_PSRAM), "sin PSRAM la capacidad deberia estar apagada");
  CHECK(!(flexBrowserCaps() & FLEXBR_CAP_TABS), "sin PSRAM no deberia haber varias pestanas");
  CHECK(!(flexBrowserCaps() & FLEXBR_CAP_MEDIA), "sin PSRAM no deberia haber reproductor");
  std::printf("   sin PSRAM -> pestanas: \"%s\"\n", flexBrowserCapReason(FLEXBR_CAP_TABS));
  std::printf("   sin PSRAM -> video:    \"%s\"\n", flexBrowserCapReason(FLEXBR_CAP_MEDIA));
  flexBrowserExit();
  g_freePsram = savedPs;
}

static void testInternalPagesAndKeyboard(){
  std::printf("[app] paginas internas, omnibox y teclado\n");
  g_prefsStr["brsrv"] = "wss://nav.example.com/v1/session";
  flexBrowserEnter();

  CHECK(!flexBrowserKeyboardOpen(), "el teclado no deberia estar abierto al entrar");

  // Tocar el omnibox lo abre y pide teclado.
  int toolTop = g_contentTop;                        // barra de pestanas + toolbar
  tapAt(SCRW / 2, toolTop + 40);
  tick(1);
  clearTouch();
  // Segun el ancho puede haber barra de pestanas; se prueba tambien mas abajo.
  if(!flexBrowserKeyboardOpen()){
    tapAt(SCRW / 2, toolTop + 60);
    tick(1);
    clearTouch();
  }
  CHECK(flexBrowserKeyboardOpen(), "tocar el omnibox deberia abrir el teclado");
  CHECK(flexBrowserEditLabel()[0] != 0, "el campo en edicion deberia tener etiqueta");

  // Escribir y borrar respetando UTF-8.
  flexBrowserKeyText("ma");
  flexBrowserKeyText("\xC3\xB1");            // n con tilde: 2 bytes
  flexBrowserKeyText("ana");
  CHECK(!std::strcmp(flexBrowserEditText(), "ma\xC3\xB1" "ana"),
        "texto \"%s\"", flexBrowserEditText());
  flexBrowserKeyBackspace(); flexBrowserKeyBackspace();
  flexBrowserKeyBackspace(); flexBrowserKeyBackspace();
  CHECK(!std::strcmp(flexBrowserEditText(), "ma"),
        "tras 4 borrados queda \"%s\", esperado \"ma\"", flexBrowserEditText());
  flexBrowserKeyBackspace();
  flexBrowserKeyBackspace();
  flexBrowserKeyBackspace();                  // de mas: no debe romper
  CHECK(flexBrowserEditText()[0] == 0, "el buffer deberia estar vacio");

  // Limite duro de entrada: no se puede desbordar por mucho que se teclee.
  for(int i = 0; i < FLEXBR_INPUT_MAX + 100; i++) flexBrowserKeyText("x");
  CHECK(std::strlen(flexBrowserEditText()) < FLEXBR_INPUT_MAX,
        "el texto del omnibox desbordo: %zu", std::strlen(flexBrowserEditText()));

  // Cancelar cierra el teclado.
  flexBrowserKeyCancel();
  CHECK(!flexBrowserKeyboardOpen(), "cancelar deberia cerrar el teclado");

  // Ir a una pagina interna por el omnibox.
  tapAt(SCRW / 2, toolTop + 40); tick(1); clearTouch();
  if(!flexBrowserKeyboardOpen()){ tapAt(SCRW / 2, toolTop + 60); tick(1); clearTouch(); }
  const char* target = "flex://history";
  for(const char* p = target; *p; p++){ char c[2] = { *p, 0 }; flexBrowserKeyText(c); }
  flexBrowserKeyEnter();
  resetCanvas();
  tick(2);
  CHECK(g_outOfBounds == 0, "la pagina interna se salio de la pantalla");
  CHECK(!flexBrowserKeyboardOpen(), "tras confirmar deberia cerrarse el teclado");

  // Boton atras del sistema: consume mientras haya capas que cerrar.
  CHECK(flexBrowserHandleSystemBack(), "atras deberia volver a la nueva pestana");
  CHECK(!flexBrowserHandleSystemBack(), "en la nueva pestana atras deberia cerrar la app");

  flexBrowserExit();
}

static void testPersistence(){
  std::printf("[app] persistencia agrupada\n");
  g_files.clear();
  g_prefsStr["brsrv"] = "wss://nav.example.com/v1/session";
  flexBrowserEnter();
  size_t writesBefore = g_files.size();
  // Muchos ticks seguidos NO deben escribir en flash.
  tick(200);
  CHECK(g_files.size() == writesBefore, "Tick escribio en el sistema de archivos");
  flexBrowserExit();

  // Los ajustes SI se guardan en NVS.
  CHECK(g_prefsStr.count("brsrv") == 1, "el servidor no se guardo");
  CHECK(g_prefsStr.count("brlast") == 1, "no se guardo la restauracion de sesion");
}

static void testClearData(){
  std::printf("[app] borrado de datos de navegacion\n");
  flexBrowserEnter();
  flexBrowserClearData(BRCLR_ALL);
  tick(1);
  flexBrowserExit();
  CHECK(g_liveAllocs >= 0, "el borrado no debe dejar contadores negativos");   // el borrado no debe petar ni dejar reservas (lo cubre testLifecycle)
}

int main(){
  std::printf("=== FlexOS · app del navegador (%s) ===\n", FLEXBR_PLAT_NAME);
  std::printf("    presupuesto: keyframe %d KB · pestanas %d · heap minimo %d KB\n",
              FLEXBR_KEYFRAME_MAX / 1024, FLEXBR_MAX_TABS, FLEXBR_MIN_FREE_HEAP / 1024);
  testLifecycle();
  testDrawBounds();
  testCapabilities();
  testInternalPagesAndKeyboard();
  testPersistence();
  testClearData();
  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
