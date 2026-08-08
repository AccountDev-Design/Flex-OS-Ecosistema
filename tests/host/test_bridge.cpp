// #############################################################
//  test_bridge.cpp  ·  comprobacion de compilacion del PUENTE
//  (FlexOS_Browser_Bridge.h) contra un .ino simulado
// #############################################################
//
//  POR QUE
//  -------
//  FlexOS_Browser_Bridge.h es el unico fichero del navegador que no se
//  puede probar de ninguna otra forma: por definicion habla con las
//  funciones `static` de un .ino de 20.000 lineas que aqui no se puede
//  compilar (necesita el core de arduino-esp32, la pantalla MIPI, el
//  GT911...). Y es justo donde mas facil es colar un nombre mal
//  escrito o una firma cambiada, porque el compilador de Arduino solo
//  lo descubriria en la placa del usuario.
//
//  Este fichero declara UN MINIMO de las mismas funciones y variables
//  que el .ino expone al puente -- con las MISMAS FIRMAS -- e incluye
//  el puente de verdad. Si algo no cuadra, falla aqui.
//
//  Lo que NO prueba: que el dibujo se vea bien (eso es hardware) ni que
//  las firmas simuladas coincidan con las de arduino-esp32 (para eso se
//  han copiado literalmente de los .ino; ver tests/host/stub/README.md).

#include "Arduino.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "Preferences.h"
#include "WiFi.h"
#include "WiFiClientSecure.h"
#include "HTTPClient.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <cstdio>
#include <cstring>
#include <vector>

// =============================================================
//  Runtime simulado (igual que en test_app.cpp)
// =============================================================
static unsigned long g_ms = 1000;
unsigned long millis(){ return g_ms; }
void delay(unsigned long ms){ g_ms += ms; }
uint32_t esp_random(){ return 0x12345678u; }
uint32_t esp_get_free_heap_size(){ return 200000; }
void* heap_caps_malloc(size_t n, uint32_t){ return malloc(n); }
size_t heap_caps_get_free_size(uint32_t){ return 200000; }
size_t heap_caps_get_total_size(uint32_t){ return 4u * 1024 * 1024; }
int  WiFiClient::connect(const char*, uint16_t){ return 0; }
void WiFiClient::stop(){}
uint8_t WiFiClient::connected(){ return 0; }
int  WiFiClient::available(){ return 0; }
int  WiFiClient::read(uint8_t*, size_t){ return -1; }
size_t WiFiClient::write(const uint8_t*, size_t n){ return n; }
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char*, uint32_t, void*,
                                   UBaseType_t, TaskHandle_t* h, BaseType_t){
  if(h) *h = nullptr;
  return pdFALSE;
}
void vTaskDelete(TaskHandle_t){}
void vTaskDelay(TickType_t t){ g_ms += t; }
struct StubSem { int count; };
SemaphoreHandle_t xSemaphoreCreateMutex(){ return new StubSem{1}; }
SemaphoreHandle_t xSemaphoreCreateBinary(){ return new StubSem{0}; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t){
  StubSem* p = (StubSem*)s; if(!p) return pdFALSE;
  if(p->count > 0){ p->count--; return pdTRUE; } return pdFALSE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t s){ if(s) ((StubSem*)s)->count++; return pdTRUE; }
void vSemaphoreDelete(SemaphoreHandle_t s){ delete (StubSem*)s; }
struct StubQueue { size_t item; std::vector<std::vector<uint8_t>> items; size_t cap; };
QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t itemSize){
  StubQueue* q = new StubQueue(); q->item = itemSize; q->cap = len; return q;
}
BaseType_t xQueueSend(QueueHandle_t h, const void* item, TickType_t){
  StubQueue* q = (StubQueue*)h; if(!q || q->items.size() >= q->cap) return pdFALSE;
  q->items.emplace_back((const uint8_t*)item, (const uint8_t*)item + q->item);
  return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t h, void* item, TickType_t){
  StubQueue* q = (StubQueue*)h; if(!q || q->items.empty()) return pdFALSE;
  memcpy(item, q->items.front().data(), q->item);
  q->items.erase(q->items.begin());
  return pdTRUE;
}
void vQueueDelete(QueueHandle_t h){ delete (StubQueue*)h; }

// =============================================================
//  El ".ino" simulado
//  -------------------------------------------------------------
//  Cada declaracion de aqui esta copiada de FlexOS_Ultra.ino. Si en el
//  .ino cambia una firma y aqui no, esta prueba deja de representar la
//  realidad: por eso se copian tal cual y no se "adaptan".
// =============================================================
#define SCR_W   480
#define SCR_H   800

static uint16_t g_fb[SCR_W * SCR_H];
static uint16_t* fb   = g_fb;
static uint16_t* gBuf = g_fb;
static int gClipY0 = 0, gClipY1 = SCR_H - 1;
static int gClipX0 = 0, gClipX1 = SCR_W - 1;
static bool gLand = false, gHosted = false, gDark = true, uiGlass = false;
static volatile bool gNetOnline = true;
static char cfgName[24] = "FlexOS Ultra";
static bool gRelayout = false;
static int  gNavMode = 0;
static int  gAppW = SCR_W, gAppH = SCR_H;

#define WIN_TOP (gHosted ? 0 : 96)
#define WIN_BOT (gHosted ? gAppH : (SCR_H - 64))
#define WIN_BG  rgb565(18, 20, 28)

struct Touch {
  bool down=false, pressed=false, released=false, tap=false, moved=false;
  bool swipeUp=false, swipeDown=false, swipeLeft=false, swipeRight=false;
  int  x=0, y=0, startX=0, startY=0, dx=0, dy=0;
  unsigned long downMs=0, lastMs=0;
};
static Touch T;

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b){
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
static inline void setBuf(uint16_t* b){ gBuf = b; }
static inline void px(int x, int y, uint16_t c){
  if((unsigned)x >= SCR_W || (unsigned)y >= SCR_H) return;
  if(y < gClipY0 || y > gClipY1 || x < gClipX0 || x > gClipX1) return;
  gBuf[(size_t)y * SCR_W + x] = c;
}
static void fillRect(int x, int y, int w, int h, uint16_t c){
  for(int j = 0; j < h; j++) for(int i = 0; i < w; i++) px(x + i, y + j, c);
}
static void fillRoundRect(int x, int y, int w, int h, int, uint16_t c){ fillRect(x, y, w, h, c); }
static void drawRoundRect(int x, int y, int w, int h, int, uint16_t c){ fillRect(x, y, w, 1, c); }
static void fillCircle(int cx, int cy, int r, uint16_t c){ fillRect(cx - r, cy - r, 2 * r, 2 * r, c); }
static void drawCircle(int cx, int cy, int r, uint16_t c){ px(cx + r, cy, c); }
static void fillTriangle(int x0,int y0,int,int,int,int,uint16_t c){ px(x0, y0, c); }
static void strokeSegAA(float x0, float y0, float, float, float, uint16_t c){ px((int)x0, (int)y0, c); }
static int  textW(const char* s, int size){
  int n = 0; for(const char* p = s; *p; p++) if((*p & 0xC0) != 0x80) n++;
  return size <= 1 ? n * 6 : n * size * 5;
}
static int  drawText(int x, int y, const char* s, int size, uint16_t c){ fillRect(x, y, textW(s, size), size * 9, c); return x; }
static void drawTextC(int cx, int y, const char* s, int size, uint16_t c){ drawText(cx - textW(s, size) / 2, y, s, size, c); }
static void drawTextR(int rx, int y, const char* s, int size, uint16_t c){ drawText(rx - textW(s, size), y, s, size, c); }
static int  drawTextClip(int x, int y, const char* s, int size, uint16_t c, int mr){ (void)mr; return drawText(x, y, s, size, c); }
static void drawLiquidGlassPanel(int x, int y, int w, int h, int, uint16_t t){ fillRect(x, y, w, h, t); }
static void flxFlush(int, int){}
static void uiBox(int &x, int &y, int &w, int &h){
  x = 0; y = WIN_TOP; w = gAppW; h = WIN_BOT - WIN_TOP;
  if(w < 32) w = 32;
  if(h < 32) h = 32;
}
static bool g_appClosed = false;
static void appClose(){ g_appClosed = true; }

// ---- teclado (mismas firmas que en los tres .ino) ----
#define KB_COLS 10
#define KB_ROWS 3
#define KB_FKEYS 6
static const char* LAYOUT_ES[KB_ROWS][KB_COLS] = {
  {"q","w","e","r","t","y","u","i","o","p"},
  {"a","s","d","f","g","h","j","k","l","\xC3\xB1"},
  {"z","x","c","v","b","n","m",",",".","?"} };
static const char* LAYOUT_EN[KB_ROWS][KB_COLS] = {
  {"q","w","e","r","t","y","u","i","o","p"},
  {"a","s","d","f","g","h","j","k","l",";"},
  {"z","x","c","v","b","n","m",",",".","?"} };
static const char* LAYOUT_NUM[KB_ROWS][KB_COLS] = {
  {"1","2","3","4","5","6","7","8","9","0"},
  {"@","#","$","%","&","-","_","(",")","/"},
  {"*","\"","'",":",";","!","?","+","=","."} };
static const char* LAYOUT_EMOJI[KB_ROWS][KB_COLS] = {
  {":)",":D",":(",";)",":P","xD",":o",":|","<3",":3"},
  {"^^","o_o",">:(",":'(","B)","-_-","=)","D:",":v",":c"},
  {"uwu",":*","<_<",">_>","(y)","!!",":]","[:","T_T","o/"} };
static const char* (*mapaActivo)[KB_COLS] = LAYOUT_ES;
static bool kbShift = false, kbLangEs = true;
static int  kbKW = 45, kbKH = 60, kbGap = 2, kbX = 6;
#define KB_KW   kbKW
#define KB_KH   kbKH
#define KB_GAP  kbGap
#define KB_X    kbX
static void kbApplySize(){}
static bool kbExtrasOn = false;
static int  kbRowsTop(){ return SCR_H - 4 * (KB_KH + KB_GAP) - 6; }
#define KB_Y kbRowsTop()
static int  kbPanelTop(){ return kbRowsTop() - 4; }
// La fila de funciones tiene la barra espaciadora ANCHA, asi que las X
// son acumulativas: con posiciones de paso fijo, "espacio" solaparia a
// "borrar" y kbFRowHit devolveria siempre la primera. (Los .ino reales
// ya lo calculan asi; aqui se reproduce para que la prueba mida lo
// mismo que la placa.)
static int  kbFKeyW(int i){ return i == 3 ? 180 : 60; }
static int  kbFKeyX(int i){
  int x = 6;
  for(int k = 0; k < i; k++) x += kbFKeyW(k) + 4;
  return x;
}
static int  kbFRowHit(int px_, int py_){
  int fy = KB_Y + 3 * (KB_KH + KB_GAP);
  if(py_ < fy || py_ > fy + KB_KH) return -1;
  for(int i = 0; i < KB_FKEYS; i++)
    if(px_ >= kbFKeyX(i) && px_ < kbFKeyX(i) + kbFKeyW(i)) return i;
  return -1;
}
static int  kbCellAt(int px_, int py_){
  int r = (py_ - KB_Y) / (KB_KH + KB_GAP);
  int c = (px_ - KB_X) / (KB_KW + KB_GAP);
  if(r < 0 || r >= KB_ROWS || c < 0 || c >= KB_COLS) return -1;
  return r * KB_COLS + c;
}
static uint16_t kbColKey(){    return rgb565(52, 56, 70); }
static uint16_t kbColKeyTxt(){ return rgb565(240, 242, 248); }
static uint16_t kbColPanel(){  return rgb565(36, 40, 58); }
static int  kbFontSize(){ return 2; }
static void kbPaintKey(int x, int y, int w, int h, const char* label, int fontSize,
                       uint16_t bg, uint16_t txt, bool pressed){
  (void)pressed;
  fillRoundRect(x, y, w, h, 6, bg);
  drawTextC(x + w / 2, y + h / 2 - 6, label, fontSize, txt);
}
static void kbFKey(int x, int fy, int w, const char* label, bool on){
  kbPaintKey(x, fy, w, KB_KH, label, 2, on ? rgb565(60,120,235) : kbColKey(), kbColKeyTxt(), false);
}
static const char* kbLayerLabel(){ return mapaActivo == LAYOUT_NUM ? "?123" : "abc"; }

// ---- sistema de archivos (se stubea: LittleFS no existe en el PC) ----
bool flexFsReady(){ return false; }
int  flexFsReadBin(const char*, void*, size_t){ return -1; }
bool flexFsWriteBin(const char*, const void*, size_t){ return false; }
bool flexFsDelete(const char*){ return false; }

// =============================================================
//  EL PUENTE DE VERDAD
// =============================================================
#include "../../FlexOS_Browser_Bridge.h"

// =============================================================
//  Comprobaciones
// =============================================================
static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

int main(){
  std::printf("=== FlexOS · puente del navegador ===\n");

  flexBrowserBegin();
  navEnter();
  CHECK(!kbExtrasOn, "el omnibox no debe habilitar el portapapeles del teclado");

  // Sin teclado, el area de contenido es la ventana entera.
  int x, y, w, h;
  brHostContentRect(&x, &y, &w, &h);
  CHECK(y == 96 && h == SCR_H - 96 - 64, "area de contenido %d..%d", y, y + h);
  CHECK(brHostKeyboardTop() == SCR_H, "sin teclado el tope debe ser el borde inferior");

  // Abrir el omnibox encoge el area de contenido por encima del teclado.
  T.tap = true; T.released = true; T.x = SCR_W / 2; T.y = 96 + 40;
  navTick();
  T.tap = false; T.released = false;
  if(!flexBrowserKeyboardOpen()){
    T.tap = true; T.released = true; T.y = 96 + 60;
    navTick();
    T.tap = false; T.released = false;
  }
  CHECK(flexBrowserKeyboardOpen(), "tocar el omnibox no abrio el teclado");
  int kt = brHostKeyboardTop();
  CHECK(kt < SCR_H && kt > 96, "tope del teclado fuera de rango: %d", kt);
  brHostContentRect(&x, &y, &w, &h);
  CHECK(y + h <= kt, "el contenido (%d..%d) invade el teclado (%d)", y, y + h, kt);

  // Escribir con el teclado real del sistema: se pulsa la tecla 'a'.
  {
    int cell = 1 * KB_COLS + 0;                    // fila 2, columna 1 -> 'a'
    int kx = KB_X + 0 * (KB_KW + KB_GAP) + KB_KW / 2;
    int ky = KB_Y + 1 * (KB_KH + KB_GAP) + KB_KH / 2;
    CHECK(kbCellAt(kx, ky) == cell, "la celda calculada no coincide");
    T.tap = true; T.released = true; T.x = kx; T.y = ky;
    navTick();
    T.tap = false; T.released = false;
    CHECK(!std::strcmp(flexBrowserEditText(), "a"),
          "tras pulsar 'a' el texto es \"%s\"", flexBrowserEditText());
  }
  // Retroceso con la tecla de funcion.
  {
    int fy = KB_Y + 3 * (KB_KH + KB_GAP) + KB_KH / 2;
    T.tap = true; T.released = true; T.x = kbFKeyX(4) + 4; T.y = fy;
    navTick();
    T.tap = false; T.released = false;
    CHECK(flexBrowserEditText()[0] == 0, "el retroceso no borro: \"%s\"", flexBrowserEditText());
  }

  // Un toque en el area del teclado NUNCA debe llegar a la pagina.
  {
    T.tap = true; T.released = true; T.x = 10; T.y = SCR_H - 10;
    navTick();
    CHECK(!T.tap, "el teclado no consumio el toque");
    T.tap = false; T.released = false;
  }

  // brHostBlitRow recorta contra el area de contenido: una fila fuera
  // no debe escribir ni un pixel (es lo que impide que una pagina
  // externa pinte sobre la barra del sistema).
  {
    uint16_t row[SCR_W];
    for(int i = 0; i < SCR_W; i++) row[i] = 0xF800;
    std::memset(g_fb, 0, sizeof(g_fb));
    brHostBlitRow(0, 10, SCR_W, row);              // muy por encima del contenido
    brHostBlitRow(0, SCR_H - 5, SCR_W, row);       // por debajo
    long painted = 0;
    for(int i = 0; i < SCR_W * SCR_H; i++) if(g_fb[i]) painted++;
    CHECK(painted == 0, "brHostBlitRow pinto %ld pixeles fuera del contenido", painted);

    brHostContentRect(&x, &y, &w, &h);
    brHostBlitRow(x, y + 5, w + 200, row);         // ancho excesivo: se recorta
    long inside = 0, outside = 0;
    for(int yy = 0; yy < SCR_H; yy++)
      for(int xx = 0; xx < SCR_W; xx++)
        if(g_fb[yy * SCR_W + xx]){
          if(yy >= y && yy < y + h && xx >= x && xx < x + w) inside++;
          else outside++;
        }
    CHECK(inside > 0, "brHostBlitRow no pinto nada dentro del contenido");
    CHECK(outside == 0, "brHostBlitRow se salio en %ld pixeles", outside);
  }

  // Cancelar y cerrar.
  flexBrowserKeyCancel();
  navTick();
  CHECK(!flexBrowserKeyboardOpen(), "cancelar no cerro el teclado");
  CHECK(brHostKeyboardTop() == SCR_H, "el tope no volvio al borde inferior");

  flexBrowserExit();
  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
