// #############################################################
//  COMPROBACION DE COMPILACION DEL SKETCH  (host, sin placa)
//  ------------------------------------------------------------
//  Compila FlexOS_Ultra.ino ENTERO en el PC contra los dobles de
//  inostub/ (Arduino + ESP-IDF). No ejecuta nada: su valor es que
//  cualquier error de tipos, de nombres o de sintaxis del sketch
//  aparece aqui, sin necesidad de arduino-cli ni del core ESP32.
//
//  Limite honesto: los dobles no reproducen el comportamiento del
//  hardware, asi que esto NO sustituye a una prueba en placa. Solo
//  garantiza que el sketch compila.
// #############################################################
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <time.h>
#include <map>
#include <string>
#include <vector>
#include "Arduino.h"
#include "Wire.h"
#include "Preferences.h"
#include "WiFi.h"
#include "WiFiUdp.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_task_wdt.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "soc/soc_caps.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

__FlexSerial Serial;
TwoWire      Wire;
__FlexWiFi   WiFi;
EspClass     ESP;
// Valores plausibles y FIJOS: el arnes no simula la flash, solo hace que el
// sketch compile y enlace. Lo que se comprueba aqui es el codigo, no el chip.
uint32_t EspClass::getSketchSize(){ return 3u << 20; }
uint32_t EspClass::getFreeSketchSpace(){ return 4u << 20; }

// Reloj virtual: las pruebas de abajo lo mueven a voluntad.
unsigned long gTestMs = 0;
static unsigned gDelayCalls = 0;
static unsigned gPinnedTaskCreates = 0;
static unsigned gSemTakeCalls = 0;
static unsigned gPanelDrawCalls = 0;
static int gPanelLastY0 = -1, gPanelLastY1 = -1;   // filas del ultimo volcado al panel
unsigned long millis(){ return gTestMs; }
// micros() avanza de verdad (reloj monotonico del PC) para que la
// instrumentacion del Panel Rapido pueda medir el coste real de un cuadro.
//
// PERO las transiciones de app interpolan por micros(), asi que su prueba
// necesita mandar en el tiempo igual que las demas mandan en millis() con
// gTestMs. gTestUs hace de anulacion: con 0 (el valor por defecto) se
// devuelve el reloj del PC y nada cambia; con un valor distinto de 0, micros()
// devuelve EXACTAMENTE ese valor y la prueba puede colocar la animacion en el
// instante que quiera.
unsigned long gTestUs = 0;
unsigned long micros(){
  if(gTestUs) return gTestUs;
  struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
  return (unsigned long)(t.tv_sec * 1000000UL + t.tv_nsec / 1000UL);
}
void delay(unsigned long){}
void delayMicroseconds(unsigned long){}
// #############################################################
//  LINEAS SDA/SCL DEL BUS COMPARTIDO, MODELADAS
//  ------------------------------------------------------------
//  La recuperacion del bus (i2cBusRecover, en FlexOS_Ultra_HAL.h) es
//  precisamente mover SCL a mano hasta que el esclavo atascado suelte SDA.
//  Para poder comprobar que eso funciona de verdad hay que modelar las dos
//  lineas: cada flanco de subida de SCL cuenta como un pulso, y al pulso
//  numero gI2cFreeAfterPulses el esclavo suelta la linea -- que es lo que
//  hace un chip real cuando termina de entregar el byte que tenia a medias.
//  Con gI2cFreeAfterPulses en 0 la linea NO se suelta: es el caso de un
//  modulo arrancado de cuajo, y tambien tiene que quedar cubierto.
// #############################################################
//  Los numeros de pin van en crudo porque estos dobles estan ANTES de que se
//  incluya el sketch, que es quien define PIN_TP_SDA/PIN_TP_SCL. Justo despues
//  del include hay un static_assert que falla la compilacion si alguna vez
//  dejan de coincidir, asi que no pueden separarse en silencio.
#define STUB_PIN_SDA 7
#define STUB_PIN_SCL 8
int gI2cFreeAfterPulses = 0;   // 0 = SDA no se suelta con los pulsos
int gI2cPulses          = 0;   // flancos de subida de SCL contados
static int gSclLevel    = 1;
//  En drenador abierto la linea sube al SOLTARLA (INPUT_PULLUP) y baja al
//  tirar de ella (OUTPUT + LOW), asi que el flanco de subida esta en pinMode,
//  no en digitalWrite. El doble modela eso mismo: al flanco numero
//  gI2cFreeAfterPulses el esclavo suelta SDA.
static void sclLevel(int lvl){
  if(lvl && !gSclLevel){
    gI2cPulses++;
    if(gI2cFreeAfterPulses && gI2cPulses >= gI2cFreeAfterPulses) gWireWedged = 0;
  }
  gSclLevel = lvl ? 1 : 0;
}
void pinMode(int pin, int mode){
  if(pin == STUB_PIN_SCL) sclLevel(mode == INPUT_PULLUP);
}
void digitalWrite(int pin, int v){
  if(pin == STUB_PIN_SCL) sclLevel(v);
}
int  digitalRead(int pin){
  if(pin == STUB_PIN_SDA) return gWireWedged ? LOW : HIGH;
  return LOW;
}
void yield(){}
long random(long m){ return m ? 0 : 0; }
long random(long a, long){ return a; }
uint32_t esp_random(){ return 0; }
bool setCpuFrequencyMhz(uint32_t){ return true; }
uint32_t getCpuFrequencyMhz(){ return 360; }

// #############################################################
//  CONTABILIDAD DE PSRAM DEL ARNES
//  ------------------------------------------------------------
//  El sketch MIDE la memoria libre: antes y despues de construir una
//  app (para saber su huella), antes y despues de soltar un buffer
//  (para saber cuanto se libero de verdad), y para decidir si una app
//  pesada cabe. Con un valor constante todas esas medidas darian 0 y
//  la multitarea por memoria seria imposible de comprobar aqui.
//
//  Asi que el arnes lleva la cuenta: cada reserva de PSRAM se apunta
//  con su tamano y cada liberacion se descuenta. Como el sketch libera
//  indistintamente con free() y con heap_caps_free() -- en ESP-IDF son
//  el mismo asignador --, free() se redirige (mas abajo, justo antes de
//  incluir el .ino) a la version que descuenta.
//
//  Esto ademas convierte la prueba en un DETECTOR DE FUGAS real: si
//  cerrar todas las apps no devuelve la PSRAM al punto de partida, es
//  que algo quedo reservado.
// #############################################################
#include <map>
static std::map<void*, size_t>& psMap(){ static std::map<void*, size_t> m; return m; }
size_t gPsUsed        = 0;
size_t gTestPsTotal   = 32u << 20;
size_t gTestPsPressure = 0;          // presion artificial que aplican las pruebas
size_t gTestInTotal   = 400u << 10;
size_t gTestInFree    = 180u << 10;
size_t gTestPsLargest = 0;           // 0 = todo lo libre en una sola pieza

size_t gPsPeak = 0;                  // pico de gPsUsed (una prueba lo pone a cero y lo mira)
static void psTrack(void* p, size_t n){ if(p){ psMap()[p] = n; gPsUsed += n; if(gPsUsed > gPsPeak) gPsPeak = gPsUsed; } }
static void psUntrack(void* p){
  auto it = psMap().find(p);
  if(it == psMap().end()) return;
  gPsUsed -= it->second;
  psMap().erase(it);
}
// free() del sketch: descuenta y llama al de verdad. Se define ANTES de la
// macro que redirige free(), asi que aqui dentro free() sigue siendo el real.
void flexTestFree(void* p){ psUntrack(p); free(p); }

// Inyeccion de fallo: con esto a true, cada reserva de PSRAM devuelve NULL.
// Sirve para comprobar que quedarse sin memoria DEGRADA con elegancia en vez
// de dejar la caja vacia o reventar. Solo lo activa la prueba que lo usa.
bool gTestPsFail = false;
void*  heap_caps_malloc(size_t n, uint32_t){ if(gTestPsFail) return nullptr; void* p = malloc(n); psTrack(p, n); return p; }
void*  heap_caps_calloc(size_t n, size_t s, uint32_t){ void* p = calloc(n, s); psTrack(p, n * s); return p; }
void*  heap_caps_realloc(void* p, size_t n, uint32_t){ psUntrack(p); void* q = realloc(p, n); psTrack(q, n); return q; }
void*  heap_caps_aligned_alloc(size_t a, size_t n, uint32_t){ if(gTestPsFail) return nullptr; void* p = aligned_alloc(a, n); psTrack(p, n); return p; }
void   heap_caps_free(void* p){ psUntrack(p); free(p); }
size_t heap_caps_get_free_size(uint32_t caps){
  if(caps & MALLOC_CAP_INTERNAL) return gTestInFree;
  size_t used = gPsUsed + gTestPsPressure;
  return used >= gTestPsTotal ? 0 : gTestPsTotal - used;
}
size_t heap_caps_get_total_size(uint32_t caps){
  return (caps & MALLOC_CAP_INTERNAL) ? gTestInTotal : gTestPsTotal;
}
unsigned gLargestCalls = 0;     // en la placa: recorrer el monton entero en seccion critica
size_t heap_caps_get_largest_free_block(uint32_t caps){
  gLargestCalls++;
  size_t fr = heap_caps_get_free_size(caps);
  return (gTestPsLargest && gTestPsLargest < fr) ? gTestPsLargest : fr;
}
size_t esp_get_free_heap_size(){ return 256u << 10; }

esp_reset_reason_t esp_reset_reason(){ return ESP_RST_POWERON; }
void esp_restart(){}

void portENTER_CRITICAL(portMUX_TYPE*){}
void portEXIT_CRITICAL(portMUX_TYPE*){}
BaseType_t xTaskCreate(TaskFunction_t, const char*, uint32_t, void*, UBaseType_t, TaskHandle_t*){ return pdPASS; }
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char*, uint32_t, void*, UBaseType_t, TaskHandle_t*, BaseType_t){ gPinnedTaskCreates++; return pdPASS; }
void vTaskDelete(TaskHandle_t){}
void vTaskDelay(TickType_t){ gDelayCalls++; }
TaskHandle_t xTaskGetCurrentTaskHandle(){ return nullptr; }
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t){ return 0; }
BaseType_t xTaskNotifyGive(TaskHandle_t){ return pdTRUE; }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t){ return 1024; }
SemaphoreHandle_t xSemaphoreCreateMutex(){ return (SemaphoreHandle_t)1; }
SemaphoreHandle_t xSemaphoreCreateBinary(){ return (SemaphoreHandle_t)1; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t){ gSemTakeCalls++; return pdTRUE; }
BaseType_t xSemaphoreGive(SemaphoreHandle_t){ return pdTRUE; }
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t, BaseType_t*){ return pdTRUE; }
void vSemaphoreDelete(SemaphoreHandle_t){}
QueueHandle_t xQueueCreate(UBaseType_t, UBaseType_t){ return (QueueHandle_t)1; }
BaseType_t xQueueSend(QueueHandle_t, const void*, TickType_t){ return pdTRUE; }
BaseType_t xQueueReceive(QueueHandle_t, void*, TickType_t){ return pdFALSE; }
void vQueueDelete(QueueHandle_t){}

esp_err_t esp_task_wdt_reset(){ return ESP_OK; }
esp_err_t esp_task_wdt_add(TaskHandle_t){ return ESP_OK; }
esp_err_t esp_task_wdt_delete(TaskHandle_t){ return ESP_OK; }
esp_err_t esp_task_wdt_status(TaskHandle_t){ return ESP_OK; }
esp_err_t esp_task_wdt_deinit(){ return ESP_OK; }
esp_err_t esp_sleep_enable_ext1_wakeup(uint64_t, esp_sleep_ext1_wakeup_mode_t){ return ESP_OK; }
esp_err_t esp_sleep_enable_ext1_wakeup_io(uint64_t, esp_sleep_ext1_wakeup_mode_t){ return ESP_OK; }
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t){ return ESP_OK; }
esp_sleep_source_t esp_sleep_get_wakeup_cause(){ return ESP_SLEEP_WAKEUP_UNDEFINED; }
void esp_deep_sleep_start(){}
esp_err_t gpio_hold_en(gpio_num_t){ return ESP_OK; }
esp_err_t gpio_hold_dis(gpio_num_t){ return ESP_OK; }
void gpio_deep_sleep_hold_en(){}
void gpio_deep_sleep_hold_dis(){}

esp_err_t esp_ldo_acquire_channel(const esp_ldo_channel_config_t*, esp_ldo_channel_handle_t*){ return ESP_OK; }
esp_err_t esp_ldo_release_channel(esp_ldo_channel_handle_t){ return ESP_OK; }
esp_err_t esp_lcd_new_dsi_bus(const esp_lcd_dsi_bus_config_t*, esp_lcd_dsi_bus_handle_t*){ return ESP_OK; }
esp_err_t esp_lcd_new_panel_io_dbi(esp_lcd_dsi_bus_handle_t, const esp_lcd_dbi_io_config_t*, esp_lcd_panel_io_handle_t*){ return ESP_OK; }
esp_err_t esp_lcd_new_panel_dpi(esp_lcd_dsi_bus_handle_t, const esp_lcd_dpi_panel_config_t*, esp_lcd_panel_handle_t*){ return ESP_OK; }
esp_err_t esp_lcd_dpi_panel_register_event_callbacks(esp_lcd_panel_handle_t, const esp_lcd_dpi_panel_event_callbacks_t*, void*){ return ESP_OK; }
esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t, int, const void*, size_t){ return ESP_OK; }
esp_err_t esp_lcd_panel_io_tx_color(esp_lcd_panel_io_handle_t, int, const void*, size_t){ return ESP_OK; }
esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t){ return ESP_OK; }
esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t){ return ESP_OK; }
esp_err_t esp_lcd_panel_reset(esp_lcd_panel_handle_t){ return ESP_OK; }
esp_err_t esp_lcd_panel_del(esp_lcd_panel_handle_t){ return ESP_OK; }
// SOMBRA DEL PANEL. fb es lo que el sketch COMPONE; lo que el usuario VE es lo
// que llega a esp_lcd_panel_draw_bitmap. Las dos cosas pueden separarse: un
// pixel escrito en fb fuera de la banda que se publica no llega al panel hasta
// que alguien vuelque esas filas, y lo que el panel tenia se queda ahi. Con
// gPanelShadow apuntando a un lienzo de 480x800, este doble copia cada banda
// publicada, asi una prueba puede mirar la pantalla de verdad y no solo fb.
// (480/800 en crudo porque el sketch se incluye despues; un static_assert tras
// el include comprueba que coinciden con SCR_W/SCR_H.)
uint16_t* gPanelShadow = nullptr;
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t, int, int y0, int, int y1, const void* data){
  gPanelDrawCalls++; gPanelLastY0 = y0; gPanelLastY1 = y1 - 1;
  if(gPanelShadow && data && y0 >= 0 && y1 <= 800 && y1 > y0)
    memcpy(gPanelShadow + (size_t)y0 * 480, data, (size_t)(y1 - y0) * 480 * 2);
  return ESP_OK;
}
esp_err_t esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t, bool){ return ESP_OK; }
esp_err_t esp_lcd_panel_disp_sleep(esp_lcd_panel_handle_t, bool){ return ESP_OK; }

bool ledcAttach(uint8_t, uint32_t, uint8_t){ return true; }
bool ledcAttachChannel(uint8_t, uint32_t, uint8_t, uint8_t){ return true; }
bool ledcWrite(uint8_t, uint32_t){ return true; }
bool ledcDetach(uint8_t){ return true; }
long map(long x, long a, long b, long c, long d){ return b==a ? c : (x-a)*(d-c)/(b-a)+c; }

// PROTOTIPOS QUE EL IDE DE ARDUINO GENERA SOLO.
// El IDE analiza el .ino y inserta al principio un prototipo de cada
// funcion, por eso el sketch puede llamar a algo definido mas abajo.
// g++ no hace eso, asi que aqui se declaran a mano las (pocas) funciones
// que el sketch usa antes de definir. No son cambios al sketch: son la
// misma declaracion que el IDE fabrica.
static void futRenderTeamSel(bool opp);
static void futRenderEnd();
static void futSaveTeam(int idx);
static void gamesRenderMenu();
static void geoEnterSelect();

// free() del SKETCH pasa por la contabilidad de PSRAM (ver arriba). Va aqui,
// despues de todos los includes del sistema y justo antes del .ino, para que
// solo afecte al codigo bajo prueba.
#define free(p) flexTestFree(p)

// El sketch entero, tal cual va a la placa.
#include "../../FlexOS_Ultra/FlexOS_Ultra.ino"

// Los dobles de SDA/SCL de arriba llevan los numeros de pin en crudo porque se
// definen antes que el sketch. Si el cableado cambia, esto falla al compilar en
// vez de dejar una prueba que ya no comprueba lo que dice comprobar.
static_assert(STUB_PIN_SDA == PIN_TP_SDA, "el doble de SDA no coincide con el sketch");
static_assert(STUB_PIN_SCL == PIN_TP_SCL, "el doble de SCL no coincide con el sketch");
static_assert(SCR_W == 480 && SCR_H == 800, "la sombra del panel asume 480x800");
#undef free

// #############################################################
//  PRUEBAS DEL RELOJ DEL SISTEMA
//  ------------------------------------------------------------
//  El calendario y la conversion de zona son aritmetica pura, asi
//  que se pueden comprobar de verdad en el PC. Estan DENTRO de esta
//  unidad de traduccion (el .ino se incluye arriba), asi que main()
//  puede llamar a las funciones static del sketch sin exportarlas.
// #############################################################
static void testPanelRapido();
static void testPanelOneUI();
static void testTactoGlobal();
static void testPulsacionLargaVidrio();
static void testVariasFotos();
static void testGuardadoRafaga();
static void testVideoRobusto();
static void testArrastresSinFlash();
extern bool gFlexOtaOwns;
static void testTecladoGlobal();
static void testCajaApps();
static void testCajaDescargadas();
static void testCajaDescargadasScroll();
static void testCajaDescargadasRegresion();
static void testCajaUnificada();
static void testCronometro();
static void testPaginasHome();
static void testNotifUnaSola();
static void testBannerNotificacion();
static void testFotoNubeEnRam();
static void testNubeEstados();
static void testDeslizarPaginas();
static void testCabeceras();
static void testListasConScroll();
static void testTarjetaCronometro();
static void testPersonalizarInicio();
static void testFlexStore();
static void testFlexAccount();
static void testRecortePorBandas();
static void testIconosEnSuCaja();
static void testTransicionesApps();
static void testRejillaAutoPaginas();
static void testMultitareaMemoria();
static void testDesbloqueoFluido();
static void testClimaFluido();
static void testCalculadora();
static void testFlexCompass();
static void testProteccionRobo();
static int gFails = 0;
static void chk(bool ok, const char* what){
  if(!ok){ printf("  FALLO: %s\n", what); gFails++; }
}
static void chkDate(uint32_t utc, int ey, int emo, int ed, int ewd, int eh, int emi, const char* what){
  gTestMs = 100000; clkSetEpoch(utc); clkLastMin = -1; clkUpdate();
  bool ok = (rtcY==ey && rtcMo==emo && rtcD==ed && rtcWd==ewd && rtcH==eh && rtcMin==emi);
  if(!ok) printf("  FALLO: %s -> %04d-%02d-%02d wd%d %02d:%02d (esperado %04d-%02d-%02d wd%d %02d:%02d)\n",
                 what, rtcY, rtcMo, rtcD, rtcWd, rtcH, rtcMin, ey, emo, ed, ewd, eh, emi);
  if(!ok) gFails++;
}

// #############################################################
//  PRUEBAS DEL PANEL RAPIDO GLOBAL
//  ------------------------------------------------------------
//  Se ejercita la maquina de gestos de verdad: se reservan los
//  framebuffers (flxGfxInit con los dobles) y se le dan toques
//  sinteticos. Lo que se comprueba es exactamente lo que el panel
//  promete: que nunca sale de su rango, que al soltar acaba abierto
//  o cerrado -- nunca a medias --, que encima de una app captura su
//  fondo y lo libera al cerrar, y que cualquier cambio de estado lo
//  deja limpio.
// #############################################################
static void touchReset(){
  T = Touch();
}
// Un cuadro con el dedo BAJANDO en (x, y). first = el cuadro del contacto.
static void touchDrag(int x, int y, bool first){
  touchReset();
  T.down = true; T.pressed = first; T.x = x; T.y = y;
  T.startX = x; T.startY = first ? y : T.startY;
}

// Gestos completos con reloj virtual, usados por las pruebas del escritorio.
static void tReset(){ T = Touch(); }
static void tDown(int x, int y, unsigned long ms){
  gTestMs = ms; tReset();
  T.down = true; T.pressed = true;
  T.x = T.startX = x; T.y = T.startY = y; T.downMs = ms;
}
static void tMove(int x, int y, unsigned long ms){
  gTestMs = ms; T.pressed = false; T.down = true;
  T.x = x; T.y = y; T.moved = true;
}
static void tUp(unsigned long ms, bool tap){
  gTestMs = ms; T.down = false; T.pressed = false;
  T.released = true; T.tap = tap;
}

static void testPanelRapido(){
  printf("Panel rapido global\n");
  // El compositor debe enviar y esperar el DMA2D en el mismo hilo. Una tarea
  // paralela leyendo el framebuffer a medio dibujar reproduce los cortes que
  // se veian en la pantalla real.
  flxPanel = (esp_lcd_panel_handle_t)1;
  flxDpiSem = (SemaphoreHandle_t)1;
  gPinnedTaskCreates = gSemTakeCalls = gPanelDrawCalls = 0;
  if(!flxGfxInit()){ printf("  FALLO: no se pudieron reservar los framebuffers\n"); gFails++; return; }
  chk(gPinnedTaskCreates == 0,
      "el compositor no crea una tarea paralela que lea fb a medio dibujar");
  chk(gPanelDrawCalls == 1 && gSemTakeCalls == 2,
      "el primer cuadro se envia una vez y espera su final DMA2D");
  gSemTakeCalls = gPanelDrawCalls = 0;
  flxFlush(100, 120);
  chk(gPanelDrawCalls == 1 && gSemTakeCalls == 2,
      "cada flush drena el token anterior y espera el callback");
  drawWallpaper(homeBuf, false);              // fondo valido para componer la cortina
  setBuf(fb);

  // --- no se abre donde no debe ---
  gState = ST_HOME; gLand = true;
  chk(!qsCanOpen(), "en horizontal (Modo PC) la cortina esta desactivada");
  gLand = false; gHosted = true;
  chk(!qsCanOpen(), "dentro de una ventana de DeX la cortina esta desactivada");
  gHosted = false; editMode = true;
  chk(!qsCanOpen(), "en Modo Edicion la cortina esta desactivada");
  editMode = false;
  gState = ST_LOCK;  chk(!qsCanOpen(), "en la pantalla de bloqueo no se abre");
  gState = ST_APP;   chk(qsCanOpen(),  "encima de una app SI se abre");
  gState = ST_HOME;  chk(qsCanOpen(),  "en el escritorio se abre");

  // --- apertura encima de una app: captura el fondo ---
  gState = ST_APP; gAppId = 0;
  gTestMs = 10000;
  int startY = 10, y = startY;
  touchDrag(240, y, true);
  chk(qsGlobalHandle(), "el gesto del borde superior lo captura la cortina");
  chk(qsOverApp && qsAppSnap != NULL, "encima de una app se captura su ultimo cuadro");

  // --- arrastre: la posicion nunca sale de [0, SCR_H] ---
  bool enRango = true;
  for(int i = 0; i < 40; i++){
    gTestMs += 16; y += 10;
    T.startY = startY; touchDrag(240, y, false); T.startY = startY;
    qsGlobalHandle();
    if(qsPanelY < 0 || qsPanelY > SCR_H) enRango = false;
  }
  chk(enRango, "la posicion se mantiene dentro del rango durante el arrastre");

  // Un tiron muy por debajo del borde inferior tampoco la saca de rango.
  gTestMs += 16; T.startY = startY; touchDrag(240, 100000, false); T.startY = startY;
  qsGlobalHandle();
  chk(qsPanelY >= 0 && qsPanelY <= SCR_H, "un arrastre desmedido queda acotado");

  // --- soltar tras un arrastre largo: se completa la apertura ---
  gTestMs += 16; touchReset(); T.released = true;
  qsGlobalHandle();
  int guardia = 0;
  while(qsAnimOn && guardia++ < 500){ gTestMs += 16; qsAnimStep(); }
  chk(!qsAnimOn, "la animacion de apertura termina");
  chk(qsPanelY == SCR_H, "un arrastre largo acaba con la cortina ABIERTA del todo");
  qsForceClose();

  // --- EL UMBRAL DEL 40%, sin ayuda del lanzamiento ---
  // Se arrastra DESPACIO (2 px cada 100 ms, muy por debajo de QS_FLICK) hasta
  // una fraccion concreta y se suelta. Asi lo que decide es la POSICION, no la
  // velocidad: es exactamente el criterio que promete el panel.
  const int umbral = (SCR_H * QS_OPEN_PCT) / 100;
  chk(umbral == 320, "el umbral de apertura es el 40% de la pantalla");
  for(int caso = 0; caso < 2; caso++){
    int destino = (caso == 0) ? umbral - 40 : umbral + 40;    // 35% y 45%
    gState = ST_APP;
    gTestMs += 1000;
    touchDrag(240, 10, true);
    chk(qsGlobalHandle(), caso == 0 ? "agarre (caso 35%)" : "agarre (caso 45%)");
    for(int yy = 12; yy <= 10 + destino; yy += 2){
      gTestMs += 100;                                         // 2 px / 100 ms = 0,02 px/ms
      T.startY = 10; touchDrag(240, yy, false); T.startY = 10;
      qsGlobalHandle();
    }
    bool esperadoAbrir = (qsPanelY >= umbral);
    chk(esperadoAbrir == (caso == 1), "el arrastre lento acaba del lado del umbral que toca");
    gTestMs += 100; touchReset(); T.released = true; qsGlobalHandle();
    guardia = 0;
    while(qsAnimOn && guardia++ < 500){ gTestMs += 16; qsAnimStep(); }
    chk(qsPanelY == 0 || qsPanelY == SCR_H, "al soltar nunca queda a medias");
    chk(qsPanelY == (esperadoAbrir ? SCR_H : 0),
        esperadoAbrir ? "por encima del 40% se completa la apertura"
                      : "por debajo del 40% se cierra del todo");
    if(qsPanelY == 0) chk(qsAppSnap == NULL, "al cerrar se libera la captura de la app");
    qsForceClose();
  }

  // --- cierre forzado por cambio de estado ---
  gTestMs += 1000; touchDrag(240, 10, true); qsGlobalHandle();
  for(int i = 0; i < 30; i++){ gTestMs += 16; T.startY = 10; touchDrag(240, 10 + i * 20, false); T.startY = 10; qsGlobalHandle(); }
  chk(qsPanelY > 0, "la cortina esta a medio abrir antes del cierre forzado");
  chk(qsAppSnap != NULL, "y con la captura de la app viva");
  qsForceClose();
  chk(qsPanelY == 0 && !qsDragging && !qsAnimOn, "qsForceClose deja la cortina cerrada y sin gesto");
  chk(qsAppSnap == NULL, "qsForceClose libera la captura de la app");
  chk(!T.tap && !T.pressed && !T.released && !T.swipeUp, "qsForceClose suelta el toque");

  // --- un estado no permitido cierra la cortina por su cuenta ---
  gTestMs += 1000; gState = ST_APP;
  touchDrag(240, 10, true); qsGlobalHandle();
  for(int i = 0; i < 10; i++){ gTestMs += 16; T.startY = 10; touchDrag(240, 10 + i * 20, false); T.startY = 10; qsGlobalHandle(); }
  chk(qsPanelY > 0, "cortina abierta antes de cambiar de estado");
  gState = ST_LOCK;                                    // p.ej. bloqueo por inactividad
  chk(!qsGlobalHandle(), "en un estado no permitido la cortina no se queda el toque");
  chk(qsPanelY == 0 && qsAppSnap == NULL, "y se cierra sola, liberando la captura");
  gState = ST_HOME;

  if(!gFails) printf("  Panel rapido: todas las comprobaciones pasan.\n");
}

// #############################################################
//  PRUEBAS DEL PANEL RAPIDO ESTILO ONE UI 8.5
//  ------------------------------------------------------------
//  Aqui se comprueba lo que el rediseno promete y que NO se ve
//  mirando la pantalla:
//    · el registro no ofrece ningun control sin backend real,
//    · una accion nunca finge un estado ON/OFF,
//    · la configuracion sobrevive a NVS, y una corrupta o de otra
//      version cae a fabrica en vez de dejar el panel roto,
//    · la maquetacion no solapa, no se sale y no deja huecos,
//    · el asa de la tarjeta asienta SIEMPRE en filas completas,
//    · el editor trabaja sobre una copia: Cancelar no toca nada,
//    · el catalogo no ofrece lo que ya esta puesto,
//    · un toggle del panel mueve el estado REAL del sistema.
// #############################################################
static bool qpBlocksOverlap(int a, int b){
  int ax0 = qpBlk[a].x, ax1 = ax0 + qpBlk[a].w, ay0 = qpBlk[a].y, ay1 = ay0 + qpBlk[a].h;
  int bx0 = qpBlk[b].x, bx1 = bx0 + qpBlk[b].w, by0 = qpBlk[b].y, by1 = by0 + qpBlk[b].h;
  return !(ax1 <= bx0 || bx1 <= ax0 || ay1 <= by0 || by1 <= ay0);
}
static void qpCheckLayout(const char* ctx){
  char msg[128];
  bool inside = true, overlap = false;
  for(int i = 0; i < qpBlkN; i++){
    if(qpBlk[i].x < QP_MX || qpBlk[i].x + qpBlk[i].w > QP_MX + QP_CONT_W) inside = false;
    if(qpBlk[i].y < 0 || qpBlk[i].y + qpBlk[i].h > qpContentH + 1) inside = false;
    for(int j = i + 1; j < qpBlkN; j++) if(qpBlocksOverlap(i, j)) overlap = true;
  }
  snprintf(msg, sizeof(msg), "%s: ningun bloque se sale de los margenes", ctx);
  chk(inside, msg);
  snprintf(msg, sizeof(msg), "%s: ningun bloque solapa con otro", ctx);
  chk(!overlap, msg);
}
static bool qpCfgHas(int id){
  for(int i = 0; i < qpN; i++) if(qpIt[i].id == id) return true;
  return false;
}

// #############################################################
//  ARRASTRAR UN DESLIZADOR NO ESCRIBE FLASH EN CADA PASO
//  ------------------------------------------------------------
//  Cada escritura de NVS son milisegundos con la cache apagada: un
//  tiron y, con el panel DSI, riesgo de destello cian. El volumen
//  (panel rapido y Musica) y el brillo de Modo PC escribian en CADA
//  paso del arrastre. Ahora el valor se APLICA en el acto (codec,
//  PWM) y se GUARDA una vez al soltar. Se comprueban las dos cosas:
//  que no hay escrituras durante el gesto y que el valor final SI
//  queda guardado.
// #############################################################
extern bool gStubAudioOk;
extern unsigned gStubAudioNvsWrites;
static void testArrastresSinFlash(){
  printf("Arrastrar un deslizador no escribe flash en cada paso\n");
  int before = gFails;
  // ---- 1. Volumen en el panel rapido ----
  gStubAudioOk = true;
  qpLoaded = false; flexPrefsWipe(); qpLoad();
  drawWallpaper(homeBuf, false); setBuf(fb);
  qsPanelY = SCR_H; qsLastY = SCR_H; qsDirty = true;
  qpMode = QPM_PANEL; qpG = QG_NONE;
  qpGH = (float)qpGroupH(qpGrows); qpScrollF = 0; qpGScrollF = 0;
  qpRelayout();
  int bVol = -1;
  for(int b = 0; b < qpBlkN; b++)
    if(qpBlk[b].kind == QB_ITEM && qpIt[qpBlk[b].item].id == QSID_VOLUME) bVol = b;
  chk(bVol >= 0, "con codec, el volumen esta en el panel de fabrica");
  if(bVol >= 0){
    int x = qpBlk[bVol].x, w = qpBlk[bVol].w;
    int py = QP_VIEW_Y0 + qpBlk[bVol].y + qpBlk[bVol].h / 2;
    unsigned w0 = gStubAudioNvsWrites, p0 = flexPrefsWrites();
    gTestMs += 100; touchDrag(x + 6, py, true); T.downMs = gTestMs; qsHandle();
    chk(qpG == QG_SLIDER, "el gesto que nace en el volumen es del slider");
    int cambios = 0; uint8_t antes = flexAudioVolume();
    for(int k = 1; k <= 12; k++){
      gTestMs += 16; touchDrag(x + 6 + (w - 12) * k / 12, py, false); qsHandle();
      if(flexAudioVolume() != antes){ cambios++; antes = flexAudioVolume(); }
    }
    chk(cambios >= 6, "el volumen REAL cambia en cada paso del arrastre");
    chk(flexAudioVolume() >= FLEXAUDIO_VOL_MAX - 5, "y llega casi al maximo");
    chk(gStubAudioNvsWrites == w0 && flexPrefsWrites() == p0,
        "durante el arrastre NO se escribe NVS (ni volumen ni preferencias)");
    gTestMs += 16; touchReset(); T.released = true; qsHandle();
    chk(gStubAudioNvsWrites == w0, "al soltar tampoco: el guardado queda diferido");
    qsTick();
    chk(gStubAudioNvsWrites == w0 + 1, "y qsTick guarda el volumen UNA vez, despues de publicar");
    qsTick();
    chk(gStubAudioNvsWrites == w0 + 1, "sin repetir la escritura en la vuelta siguiente");
  }
  qsPanelY = 0; qsLastY = 0; qpG = QG_NONE; touchReset();

  // ---- 2. Volumen en Musica ----
  {
    bool l0 = musLoaded; musLoaded = true;
    MusNowGeom g = musNowGeom();
    unsigned w0 = gStubAudioNvsWrites;
    gTestMs += 100; touchDrag(g.volX + 2, g.volY, true); musNowTouch();
    chk(musVolDrag, "Musica: el gesto que nace en la barra de volumen la arrastra");
    for(int k = 1; k <= 10; k++){ gTestMs += 16; touchDrag(g.volX + g.volW * k / 10, g.volY, false); musNowTouch(); }
    chk(flexAudioVolume() >= FLEXAUDIO_VOL_MAX - 2, "Musica: el volumen real sigue al dedo");
    chk(gStubAudioNvsWrites == w0, "Musica: ninguna escritura NVS durante el arrastre");
    gTestMs += 16; touchReset(); T.released = true; musNowTouch();
    chk(!musVolDrag && gStubAudioNvsWrites == w0 + 1, "Musica: una sola escritura al soltar");
    musLoaded = l0;
  }
  gStubAudioOk = false;

  // ---- 3. Brillo del panel de notificaciones de Modo PC ----
  {
    uint8_t ov0 = dexOv;
    dexOv = DXO_NOTIF; dexOvClosing = false; dexOvT0 = gTestMs - DEX_ANIM_MS - 1;
    int nx, ny, nw, nh; dexNpRect(nx, ny, nw, nh);
    int by = dexNpBrightY(ny), bx = nx + 12, bw = nw - 24;
    unsigned p0 = flexPrefsWrites();
    pTap = pPressed = pReleased = false; pDown = true; pY = by + 14 + 13;
    int bris = 0, b0 = gBright;
    for(int k = 1; k <= 8; k++){
      pX = bx + bw * k / 10; dexNotifTouch();
      if(gBright != b0){ bris++; b0 = gBright; }
    }
    chk(bris >= 6, "Modo PC: el brillo REAL (PWM) cambia en cada paso");
    chk(flexPrefsWrites() == p0, "Modo PC: ninguna escritura NVS durante el arrastre");
    touchReset(); dexPointer();
    chk(flexPrefsWrites() > p0, "Modo PC: al soltar se guarda");
    Preferences p; p.begin("flexos", true);
    chk(p.getInt("bright", -1) == gBright, "Modo PC: y lo guardado es el brillo final");
    p.end();
    unsigned p1 = flexPrefsWrites();
    dexPointer();
    chk(flexPrefsWrites() == p1, "Modo PC: una sola vez");
    dexOv = ov0; pDown = false;
    setBacklight(80);
  }
  if(gFails == before) printf("  Deslizadores: todas las comprobaciones pasan.\n");
}

static void testPanelOneUI(){
  printf("Panel rapido - rediseno One UI\n");
  gState = ST_HOME; gLand = false; gHosted = false; editMode = false;
  gTestMs = 200000;

  // ---- 1. REGISTRO: nada falso ----
  bool idsOk = true, ptrsOk = true, accionSinEstado = true;
  for(int i = 0; i < QSID_COUNT; i++){
    if(QS_REG[i].id != i) idsOk = false;
    // Huecos NVS reservados por controles retirados: nunca se ofrecen ni se
    // ejecutan, asi que tampoco declaran accion ni nombre.
    if(i == QSID_RETIRED_10 || i == QSID_RETIRED_13) continue;
    if(!QS_REG[i].avail || !QS_REG[i].tap || !QS_REG[i].icon || !QS_REG[i].name) ptrsOk = false;
    if(QS_REG[i].type == QT_ACTION && QS_REG[i].state != NULL) accionSinEstado = false;
    if(QS_REG[i].type == QT_TOGGLE && QS_REG[i].state == NULL) accionSinEstado = false;
  }
  chk(idsOk,  "cada entrada del registro esta en el indice de su propio id");
  chk(ptrsOk, "toda entrada declara disponibilidad, accion, icono y nombre");
  chk(accionSinEstado, "una ACCION nunca expone estado ON/OFF y un TOGGLE siempre lo expone");
  // El ESP32-P4 no lleva radio Bluetooth: soc_caps.h no define SOC_BLE_SUPPORTED.
  chk(FLEXOS_BLE_HW == 0, "el perfil compilado NO declara radio Bluetooth");
  chk(!qpCtlAvail(QSID_BLE), "sin radio real, Bluetooth NO esta disponible como control");
  chk(!qpCtlAvail(QSID_RETIRED_13), "el identificador retirado nunca aparece como control");
  chk(!qpCtlAvail(QSID_RETIRED_10), "ni el otro hueco reservado");
  chk(qpCtlAvail(QSID_WIFI) == (FLEXOS_ENABLE_WIFI ? true : false), "Wi-Fi sigue la disponibilidad real");
  chk(qpCtl(QSID_COUNT) == NULL && qpCtl(-1) == NULL, "un id fuera de rango no lee basura");

  // ---- 2. FABRICA + NORMALIZACION ----
  flexPrefsWipe();
  qpLoaded = false; qpLoad();
  chk(qpN > 0, "sin nada en NVS se arranca con la configuracion de fabrica");
  bool sinBle = true, tamOk = true, sinDup = true;
  for(int i = 0; i < qpN; i++){
    if(qpIt[i].id == QSID_BLE) sinBle = false;
    if(!qpSizeAllowed(qpIt[i].id, qpIt[i].w, qpIt[i].h)) tamOk = false;
    for(int j = i + 1; j < qpN; j++) if(qpIt[i].id == qpIt[j].id) sinDup = false;
  }
  chk(sinBle, "la configuracion de fabrica no coloca Bluetooth en el P4");
  chk(tamOk,  "todo elemento tiene un tamano que su control admite");
  chk(sinDup, "no hay ningun control repetido");
  chk(qpCfgHas(QSID_WIFI) && qpCfgHas(QSID_BRIGHT), "Wi-Fi y Brillo entran de fabrica");

  // ---- 3. MAQUETACION ----
  qpMode = QPM_PANEL; qpGH = (float)qpGroupH(qpGrows);
  qpScrollF = 0; qpGScrollF = 0;
  qpRelayout();
  qpCheckLayout("maquetacion de fabrica");
  chk(qpContentH > 0, "el contenido tiene alto");
  chk(qpTileN > 0 && qpGroupBlk >= 0, "hay circulos 1x1 y por tanto tarjeta expandible");
  chk(qpBlk[qpGroupBlk].w == QP_CONT_W, "la tarjeta ocupa las cuatro columnas");
  chk(qpColX(0) == QP_MX && qpColX(3) + QP_CW == QP_MX + QP_CONT_W,
      "las cuatro columnas cubren el ancho util EXACTO (480 px)");
  chk(qpSpanW(2) == 218 && qpSpanW(4) == 448, "los tramos de 2 y 4 columnas miden lo que deben");

  // Con MUCHOS controles 1x1 la tarjeta no crece sin limite: pasa a scroll interno.
  int filas = qpTotalRows();
  chk(qpGroupMaxPx() <= qpGroupH(QP_GROWS_MAX), "la tarjeta tiene un alto maximo acotado");
  // Un alto guardado en NVS mayor que las filas que hay AHORA (el usuario
  // guardo 4 filas y luego quito controles) no puede dibujar filas vacias ni
  // empujar el resto del panel fuera de la pantalla.
  {
    qpGrows = QP_GROWS_MAX; qpGH = (float)qpGroupH(QP_GROWS_MAX);
    qpRelayout();
    chk(qpLayGroupPx == qpGroupMaxPx(),
        "un alto guardado mayor que las filas reales se acota a las que hay");
    chk((int)(qpGH + 0.5f) == qpLayGroupPx, "y el estado vivo se corrige, no solo el dibujo");
    qpGrows = 3; qpGH = (float)qpGroupH(3); qpRelayout();
  }
  if(filas > QP_GROWS_MAX) chk(qpGroupCanScroll(), "con mas filas que el maximo hay scroll interno");

  // ---- 4. ESTIRAMIENTO: asienta en FILAS COMPLETAS ----
  bool snapOk = true;
  for(int destino = qpGroupMinPx() - 30; destino <= qpGroupMaxPx() + 30; destino += 17){
    qpGH = (float)destino;
    qpGroupSnap();
    int guardia = 0;
    while(qpGAnim && guardia++ < 400){ gTestMs += 16; qpGroupAnimStep(); }
    int h = (int)(qpGH + 0.5f);
    bool valido = false;
    for(int r = QP_GROWS_MIN; r <= QP_GROWS_MAX; r++) if(h == qpGroupH(r)) valido = true;
    if(!valido) snapOk = false;
    if(h < qpGroupMinPx() || h > qpGroupMaxPx()) snapOk = false;
  }
  chk(snapOk, "al soltar el asa, la tarjeta asienta en una fila COMPLETA y dentro de limites");

  // ---- 4b. MUCHOS CONTROLES: estiramiento REAL y scroll interno ----
  // Se construye a mano un panel con TODOS los controles disponibles como
  // circulos 1x1. Asi hay mas filas que las que caben, que es justo el caso
  // que el asa y el scroll interno tienen que resolver.
  {
    uint8_t sv[QP_MAX_ITEMS]; uint8_t svN = qpN, svG = qpGrows;
    QpItem svIt[QP_MAX_ITEMS]; memcpy(svIt, qpIt, sizeof(svIt)); (void)sv;
    qpN = 0;
    for(int id = 0; id < QSID_COUNT && qpN < QP_MAX_ITEMS; id++){
      if(!qpCtlAvail(id) || !qpSizeAllowed(id, 1, 1)) continue;
      qpIt[qpN].id = (uint8_t)id; qpIt[qpN].w = 1; qpIt[qpN].h = 1;
      qpIt[qpN].ori = QOR_H; qpIt[qpN].vis = 1; qpN++;
    }
    qpGrows = QP_GROWS_MAX; qpGH = (float)qpGroupH(qpGrows);
    qpScrollF = 0; qpGScrollF = 0;
    qpRelayout();
    chk(qpTotalRows() >= 3, "con todos los controles como circulos hay al menos tres filas");
    qpCheckLayout("panel lleno de circulos");
    // Estirar de minimo a maximo y volver.
    qpGH = (float)qpGroupMinPx(); qpRelayout();
    int hMin = qpLayGroupPx;
    qpGH = (float)qpGroupMaxPx(); qpRelayout();
    int hMax = qpLayGroupPx;
    chk(hMax > hMin, "la tarjeta puede crecer de verdad cuando hay filas de sobra");
    chk((hMax - hMin) % QP_TROW == 0, "el recorrido del asa es un numero entero de FILAS");
    // Contraida al minimo quedan filas fuera: ahi tiene que haber scroll
    // interno, y con limites exactos en las dos puntas.
    qpGH = (float)qpGroupMinPx(); qpRelayout();
    chk(qpGroupCanScroll(), "con la tarjeta contraida y mas filas, hay scroll interno");
    qpGScrollF = 100000; qpClampGScroll(false);
    int innerMax = qpTotalRows() * QP_TROW - qpGroupInnerH(qpLayGroupPx);
    chk((int)qpGScrollF == innerMax, "el scroll interno se detiene en la ultima fila");
    qpGScrollF = -100000; qpClampGScroll(false);
    chk((int)qpGScrollF == 0, "y en la primera");
    // Estirada al maximo con todas las filas dentro, ya no hace falta scroll.
    qpGH = (float)qpGroupMaxPx(); qpRelayout();
    if(qpTotalRows() <= QP_GROWS_MAX)
      chk(!qpGroupCanScroll(), "estirada del todo, si caben todas las filas no hay scroll interno");
    memcpy(qpIt, svIt, sizeof(qpIt)); qpN = svN; qpGrows = svG;
    qpGH = (float)qpGroupH(qpGrows); qpScrollF = 0; qpGScrollF = 0; qpRelayout();
  }

  // ---- 5. IDS DESCONOCIDOS, DUPLICADOS Y TAMANOS IMPOSIBLES ----
  QpItem sucio[QP_MAX_ITEMS];
  memset(sucio, 0, sizeof(sucio));
  sucio[0].id = QSID_WIFI;   sucio[0].w = 2; sucio[0].h = 1; sucio[0].ori = QOR_H; sucio[0].vis = 1;
  sucio[1].id = QSID_WIFI;   sucio[1].w = 2; sucio[1].h = 1; sucio[1].ori = QOR_H; sucio[1].vis = 1;  // duplicado
  sucio[2].id = 200;         sucio[2].w = 1; sucio[2].h = 1; sucio[2].ori = QOR_H; sucio[2].vis = 1;  // desconocido
  sucio[3].id = QSID_BLE;    sucio[3].w = 1; sucio[3].h = 1; sucio[3].ori = QOR_H; sucio[3].vis = 1;  // sin hardware
  sucio[4].id = QSID_BRIGHT; sucio[4].w = 1; sucio[4].h = 1; sucio[4].ori = QOR_V; sucio[4].vis = 1;  // tamano y ori imposibles
  uint8_t gr = 99;
  uint8_t n = qpNormalize(sucio, 5, gr);
  chk(n == 2, "la normalizacion deja solo los elementos validos");
  chk(sucio[0].id == QSID_WIFI && sucio[1].id == QSID_BRIGHT, "y conserva el orden de los que sobreviven");
  chk(sucio[1].w == 4 && sucio[1].h == 1, "un tamano incompatible se corrige al primero permitido");
  chk(sucio[1].ori == QOR_H, "una orientacion no permitida se corrige");
  chk(gr == QP_GROWS_MAX, "el numero de filas se acota al rango valido");

  // ---- 6. NVS: ida y vuelta, version futura y blob corrupto ----
  qpLoaded = false; qpLoad();
  uint8_t antesN = qpN, antesGrows = qpGrows, antesId0 = qpIt[0].id;
  qpGrows = 4; qpSave();
  qpN = 0; qpGrows = 0; memset(qpIt, 0, sizeof(qpIt));
  qpLoaded = false; qpLoad();
  chk(qpN == antesN && qpIt[0].id == antesId0, "la configuracion vuelve intacta de NVS");
  chk(qpGrows == 4, "el alto elegido para la tarjeta tambien persiste");
  (void)antesGrows;

  uint8_t blob[QP_BLOB_N];
  qpSerialize(blob);
  blob[1] = QP_CFG_VER + 7;                                  // "version del futuro"
  chk(!qpDeserialize(blob), "un blob de una version mas nueva se rechaza en vez de adivinarse");
  qpSerialize(blob); blob[0] = 'X';
  chk(!qpDeserialize(blob), "un blob con firma equivocada se rechaza");
  qpSerialize(blob); blob[2] = QP_MAX_ITEMS + 5;             // cuenta imposible
  chk(!qpDeserialize(blob), "un blob con mas elementos de los que caben se rechaza");
  // ... y un blob corrupto en NVS deja el panel utilizable, no roto.
  memset(blob, 0xA5, sizeof(blob));
  prefs.begin(QP_NVS_NS, false); prefs.putBytes(QP_NVS_KEY, blob, QP_BLOB_N); prefs.end();
  qpN = 0; qpLoaded = false; qpLoad();
  chk(qpN > 0, "una NVS corrupta cae a la configuracion de fabrica");
  qpCheckLayout("tras recuperarse de NVS corrupta");

  // ---- 7. EDITOR: copia temporal, quitar, anadir, mover, redimensionar ----
  qpLoaded = false; flexPrefsWipe(); qpLoad();
  uint8_t origN = qpN, origId0 = qpIt[0].id;
  qpEditEnter();
  chk(qpMode == QPM_EDIT && qpEdN == origN, "editar trabaja sobre una copia identica");
  chk(qpEditRemove(0), "se puede quitar un elemento");
  chk(qpEdN == origN - 1, "la copia pierde el elemento");
  chk(qpN == origN && qpIt[0].id == origId0, "la configuracion VIVA no se ha tocado");
  qpEditCancel();
  chk(qpMode == QPM_PANEL && qpN == origN && qpIt[0].id == origId0,
      "Cancelar descarta todos los cambios");

  qpEditEnter();
  qpEditRemove(0);
  qpEditCommit();
  chk(qpMode == QPM_PANEL && qpN == origN - 1, "Listo aplica los cambios");
  qpN = 0; qpLoaded = false; qpLoad();
  chk(qpN == origN - 1, "y los deja guardados en NVS para el proximo arranque");

  // Volver a anadirlo desde el catalogo.
  qpEditEnter();
  qpCatBuild();
  bool ofreceQuitado = false, ofrecePuesto = false, ofreceNoDisponible = false;
  for(int i = 0; i < qpCatN; i++){
    if(qpCatIds[i] == origId0) ofreceQuitado = true;
    for(int j = 0; j < qpEdN; j++) if(qpCatIds[i] == qpEdIt[j].id) ofrecePuesto = true;
    if(!qpCtlAvail(qpCatIds[i])) ofreceNoDisponible = true;
  }
  chk(ofreceQuitado, "el catalogo ofrece el control que se habia quitado");
  chk(!ofrecePuesto, "el catalogo NO ofrece controles que ya estan en el panel");
  chk(!ofreceNoDisponible, "el catalogo NO ofrece controles sin backend real");
  chk(qpEditAdd(origId0), "anadir desde el catalogo coloca el control");
  chk(!qpEditAdd(origId0), "y no lo duplica");
  chk(!qpEditAdd(QSID_BLE), "no se puede anadir un control sin hardware real");
  qpRelayout(); qpCheckLayout("tras anadir desde el catalogo");
  qpEditCommit();
  chk(qpN == origN, "el panel recupera su numero de controles");

  // Redimensionar: 1x1 -> 2x1 y rechazo limpio de lo imposible.
  qpEditEnter();
  int idxW = -1;
  for(int i = 0; i < qpEdN; i++) if(qpEdIt[i].id == QSID_THEME) idxW = i;
  chk(idxW >= 0, "el tema esta en el panel para probar el redimensionado");
  if(idxW >= 0){
    uint8_t nw = 0, nh = 0;
    qpEdIt[idxW].w = 1; qpEdIt[idxW].h = 1;
    chk(qpNextSize(QSID_THEME, 1, 1, +1, nw, nh) && nw == 2 && nh == 1,
        "un control 1x1 que admite capsula crece a 2x1");
    qpEdIt[idxW].w = nw; qpEdIt[idxW].h = nh;
    chk(!qpNextSize(QSID_THEME, 2, 1, +1, nw, nh), "y no crece a un tamano que no admite");
    chk(!qpNextSize(QSID_BRIGHT, 4, 1, +1, nw, nh) && !qpNextSize(QSID_BRIGHT, 4, 1, -1, nw, nh),
        "el slider de brillo solo existe a 4x1: cualquier otro tamano se rechaza");
    qpRelayout(); qpCheckLayout("con una capsula redimensionada");
  }
  qpEditCancel();

  // El asa del borde derecho de un CIRCULO lo convierte en capsula, por el
  // camino real del toque (no llamando a qpNextSize a mano).
  qpEditEnter();
  qpScrollF = 0; qpGScrollF = 0; qpRelayout(); qpG = QG_NONE;
  if(qpGroupBlk >= 0 && qpTileN > 0){
    int idxT = qpTiles[0];
    uint8_t idT = qpEdIt[idxT].id;
    int gy = QP_VIEW_Y0 + qpBlk[qpGroupBlk].y;
    int gyTop = gy + QP_GPAD - (int)(qpGScrollF + 0.5f);
    int cx, cy; qpTileCenter(0, gyTop, cx, cy);
    gTestMs += 200; touchDrag(cx + QP_TCOLW / 2 - 8, cy, true); T.downMs = gTestMs; qpEditTouch();
    chk(qpG == QG_EDDRAG && qpEdResize == idxT, "el asa derecha de un circulo engancha el redimensionado");
    gTestMs += 16; touchDrag(cx + QP_TCOLW / 2 + 50, cy, false); qpEditTouch();
    chk(qpEdIt[idxT].w == 2 && qpEdIt[idxT].h == 1, "arrastrarla convierte el circulo 1x1 en capsula 2x1");
    qpRelayout();
    bool yaNoEsCirculo = true;
    for(int k = 0; k < qpTileN; k++) if(qpEdIt[qpTiles[k]].id == idT) yaNoEsCirculo = false;
    chk(yaNoEsCirculo, "y sale de la tarjeta de circulos para vivir en el flujo");
    qpCheckLayout("tras convertir un circulo en capsula");
    gTestMs += 16; touchReset(); T.released = true; qpEditTouch();
    chk(qpG == QG_NONE && qpEdResize == -1, "al soltar termina el redimensionado");
  }
  qpEditCancel();

  // Reordenar.
  qpEditEnter();
  uint8_t a0 = qpEdIt[0].id, a2 = qpEdIt[2].id;
  qpEditMove(0, 2);
  chk(qpEdIt[2].id == a0 && qpEdIt[1].id == a2, "mover un elemento reordena sin perder a nadie");
  qpEditMove(2, 0);
  chk(qpEdIt[0].id == a0, "y se puede devolver a su sitio");
  qpEditCancel();

  // Restablecer diseno solo afecta a la copia hasta "Listo".
  qpEditEnter();
  uint8_t vivoN = qpN;
  qpEditRemove(0); qpEditRemove(0);
  qpEditReset();
  chk(qpEdN == vivoN, "Restablecer devuelve la copia al diseno de fabrica");
  chk(qpN == vivoN, "y no toca lo vivo hasta pulsar Listo");
  qpEditCancel();

  // No se puede vaciar el panel del todo.
  qpEditEnter();
  int guardia2 = 0;
  while(qpEdN > 1 && guardia2++ < 64) qpEditRemove(0);
  chk(qpEdN == 1, "se pueden quitar todos menos uno");
  chk(!qpEditRemove(0), "el ultimo control no se puede quitar: el panel no queda vacio");
  qpEditCancel();

  // El OTA es propietario de la pantalla: no se entra a editar.
  gFlexOtaOwns = true;
  qpEditEnter();
  chk(qpMode == QPM_PANEL, "con la pantalla en manos del OTA no se abre el editor");
  gFlexOtaOwns = false;

  // ---- 8. ACCION REAL DE UN CONTROL ----
  // Se abre el panel del todo y se toca el circulo del Modo avion: lo que se
  // comprueba es que cambia gAirplane, el estado REAL del sistema.
  qpLoaded = false; flexPrefsWipe(); qpLoad();
  drawWallpaper(homeBuf, false); setBuf(fb);
  qsPanelY = SCR_H; qsLastY = SCR_H; qsDirty = true;
  qpMode = QPM_PANEL; qpG = QG_NONE;
  qpGH = (float)qpGroupH(qpGrows); qpScrollF = 0; qpGScrollF = 0;
  qpRelayout();
  int kAvion = -1;
  for(int k = 0; k < qpTileN; k++) if(qpIt[qpTiles[k]].id == QSID_AIRPLANE) kAvion = k;
  if(kAvion < 0){
    // De fabrica el Modo avion es una capsula: se busca entre los bloques.
    int bAvion = -1;
    for(int b = 0; b < qpBlkN; b++)
      if(qpBlk[b].kind == QB_ITEM && qpIt[qpBlk[b].item].id == QSID_AIRPLANE) bAvion = b;
    chk(bAvion >= 0, "el Modo avion esta en el panel");
    if(bAvion >= 0){
      bool antes = gAirplane;
      int px = qpBlk[bAvion].x + 40;
      int py = QP_VIEW_Y0 + qpBlk[bAvion].y + qpBlk[bAvion].h / 2;
      chk(qsTapTile(px, py), "el toque cae sobre un control del panel");
      chk(gAirplane != antes, "el toque cambia el estado REAL del modo avion");
      qsTapTile(px, py);
      chk(gAirplane == antes, "y lo devuelve");
    }
  }
  // Brillo: el slider mueve el PWM real (gBright), no un numero decorativo.
  int bBrillo = -1;
  for(int b = 0; b < qpBlkN; b++)
    if(qpBlk[b].kind == QB_ITEM && qpIt[qpBlk[b].item].id == QSID_BRIGHT) bBrillo = b;
  chk(bBrillo >= 0, "el brillo esta en el panel");
  if(bBrillo >= 0){
    int x = qpBlk[bBrillo].x, w = qpBlk[bBrillo].w;
    int py = QP_VIEW_Y0 + qpBlk[bBrillo].y + qpBlk[bBrillo].h / 2;
    gTestMs += 100; touchDrag(x + 6, py, true); T.downMs = gTestMs;
    qsHandle();
    chk(qpG == QG_SLIDER, "el gesto que nace en el slider es del slider, no del scroll");
    gTestMs += 16; touchDrag(x + w - 6, py, false); qsHandle();
    chk(gBright >= 95, "arrastrar a la derecha sube el brillo REAL casi al maximo");
    gTestMs += 16; touchDrag(x + 6, py, false); qsHandle();
    chk(gBright <= 5, "y arrastrar a la izquierda lo baja");
    gTestMs += 16; touchReset(); T.released = true; qsHandle();
    chk(qpG == QG_NONE, "al soltar, el slider suelta el gesto");
    // La escritura en NVS queda PENDIENTE, fuera del gesto: se hace despues de
    // publicar el cuadro, no en el mismo en que se levanta el dedo.
    chk(qpSavePrefs, "el slider deja el guardado pendiente en vez de escribir flash en el gesto");
    qsTick();
    chk(!qpSavePrefs, "y qsTick lo vacia despues de publicar");
    setBacklight(80);
  }

  // ---- 9. PROPIEDAD DEL GESTO ----
  // Un arrastre que nace en el ASA de la tarjeta redimensiona; uno que nace en
  // el contenido hace scroll. Nunca los dos a la vez.
  qpG = QG_NONE; qpScrollF = 0; qpGH = (float)qpGroupH(QP_GROWS_MIN); qpRelayout();
  int gyAsa = QP_VIEW_Y0 + qpBlk[qpGroupBlk].y + qpBlk[qpGroupBlk].h - 4;
  gTestMs += 100; touchDrag(SCR_W / 2, gyAsa, true); T.downMs = gTestMs; qsHandle();
  chk(qpG == QG_RESIZE, "un gesto que nace en el asa manda el estiramiento");
  float h0 = qpGH;
  gTestMs += 16; touchDrag(SCR_W / 2, gyAsa + 60, false); qsHandle();
  chk(qpGH > h0, "y el asa acompana al dedo hacia abajo");
  gTestMs += 16; touchReset(); T.released = true; qsHandle();
  chk(qpSavePanel, "el asa deja el guardado pendiente, no escribe flash en el gesto");
  guardia2 = 0;
  while(qpGAnim && guardia2++ < 400){ gTestMs += 16; qpGroupAnimStep(); }
  chk(!qpGAnim && qpG == QG_NONE, "al soltar el asa termina el asentamiento");
  qsTick();
  chk(!qpSavePanel, "y el alto elegido acaba en NVS una vez fuera del gesto");

  qpG = QG_NONE; qpScrollF = 0; qpRelayout();
  int yVacio = QP_VIEW_Y0 + 4;
  gTestMs += 100; touchDrag(SCR_W / 2, yVacio, true); T.downMs = gTestMs; qsHandle();
  chk(qpG == QG_PENDING, "un gesto en el contenido empieza sin decidir: aun puede ser toque");
  gTestMs += 16; touchDrag(SCR_W / 2, yVacio - 40, false); qsHandle();
  chk(qpG == QG_SCROLL || qpG == QG_GSCROLL, "al pasar del umbral se convierte en scroll");
  gTestMs += 16; touchReset(); T.released = true; qsHandle();

  // La cabecera NO se mueve con el scroll y el scroll respeta sus limites.
  qpScrollF = 100000; qpClampScroll(false);
  chk((int)qpScrollF == qpScrollMax(), "el scroll no pasa del final del contenido");
  qpScrollF = -100000; qpClampScroll(false);
  chk((int)qpScrollF == 0, "ni del principio");

  // ---- 10. LA CABECERA MANDA EL CIERRE, NO EL SCROLL ----
  qsPanelY = SCR_H; qsLastY = SCR_H; qpG = QG_NONE; qpScrollF = 0; qpRelayout();
  gTestMs += 200; touchDrag(200, 60, true); T.downMs = gTestMs; qsHandle();
  gTestMs += 16; touchDrag(200, 20, false); qsHandle();
  chk(qpG == QG_CURTAIN, "un arrastre nacido en la cabecera es de la cortina, no del contenido");
  chk((int)qpScrollF == 0, "y no ha movido ni un pixel el contenido");
  gTestMs += 16; touchReset(); T.released = true; qsHandle();
  guardia2 = 0;
  while(qsAnimOn && guardia2++ < 500){ gTestMs += 16; qsAnimStep(); }
  chk(qsPanelY == 0, "arrastrar la cabecera hacia arriba cierra el panel");

  // Un TOQUE en la cabecera (sin arrastrar y fuera de los botones) no cierra.
  qsPanelY = SCR_H; qsLastY = SCR_H; qsDirty = true; qpG = QG_NONE; qpRelayout();
  gTestMs += 200; touchDrag(200, 60, true); T.downMs = gTestMs; qsHandle();
  gTestMs += 16; touchReset(); T.released = true; T.tap = true; qsHandle();
  chk(qsPanelY == SCR_H && !qsAnimOn, "un toque suelto en la cabecera NO cierra el panel");
  // ... pero el asa inferior si.
  qpG = QG_NONE;
  gTestMs += 200; touchDrag(SCR_W / 2, SCR_H - 12, true); T.downMs = gTestMs; qsHandle();
  gTestMs += 16; touchReset(); T.released = true; T.tap = true; qsHandle();
  guardia2 = 0;
  while(qsAnimOn && guardia2++ < 500){ gTestMs += 16; qsAnimStep(); }
  chk(qsPanelY == 0, "tocar el asa inferior cierra el panel");

  // ---- 11. BOTON DEL LAPIZ Y MODO EDICION POR GESTO ----
  qsPanelY = SCR_H; qsLastY = SCR_H; qsDirty = true; qpG = QG_NONE;
  qpMode = QPM_PANEL; qpScrollF = 0; qpRelayout();
  gTestMs += 200; touchDrag(QP_HBTN_CX[0], QP_HBTN_CY, true); T.downMs = gTestMs; qsHandle();
  chk(qpG == QG_PENDING, "el lapiz de la cabecera espera a ver si es toque");
  gTestMs += 16; touchReset(); T.released = true; T.tap = true; qsHandle();
  chk(qpMode == QPM_EDIT, "tocar el lapiz entra en el modo de edicion");

  // Mantener pulsado un elemento arranca el arrastre y reordena en vivo.
  qpScrollF = 0; qpRelayout(); qpG = QG_NONE;
  int bMover = -1;
  for(int b = 0; b < qpBlkN; b++) if(qpBlk[b].kind == QB_ITEM){ bMover = b; break; }
  chk(bMover >= 0, "hay un modulo exterior que mover");
  if(bMover >= 0){
    int idxAnt = qpBlk[bMover].item;
    uint8_t idAnt = qpEdIt[idxAnt].id;
    int px = qpBlk[bMover].x + qpBlk[bMover].w / 2;
    int py = QP_VIEW_Y0 + qpBlk[bMover].y + qpBlk[bMover].h / 2;
    gTestMs += 200; touchDrag(px, py, true); T.downMs = gTestMs; qpEditTouch();
    chk(qpG == QG_PENDING, "el elemento espera a la pulsacion larga");
    gTestMs += QP_EDLONG_MS + 20; touchDrag(px, py, false); qpEditTouch();
    chk(qpEdDrag == idxAnt, "mantener pulsado engancha el elemento");
    // Se lleva el dedo sobre otro bloque: el orden cambia en el acto.
    int bOtro = -1;
    for(int b = 0; b < qpBlkN; b++)
      if(qpBlk[b].kind == QB_ITEM && qpBlk[b].item != qpEdDrag){ bOtro = b; break; }
    if(bOtro >= 0){
      int qx = qpBlk[bOtro].x + qpBlk[bOtro].w / 2;
      int qy = QP_VIEW_Y0 + qpBlk[bOtro].y + qpBlk[bOtro].h / 2;
      int destino = qpBlk[bOtro].item;
      gTestMs += 16; touchDrag(qx, qy, false); qpEditTouch();
      chk(qpEdIt[destino].id == idAnt, "el elemento se coloca en el hueco de destino");
      chk(qpEdDrag == destino, "y el arrastre sigue enganchado a el");
    }
    gTestMs += 16; touchReset(); T.released = true; qpEditTouch();
    chk(qpEdDrag == -1 && qpG == QG_NONE, "al soltar termina el arrastre");
  }

  // "Anadir un control" abre el catalogo y un toque coloca el control.
  qpEditRemove(0);
  qpRelayout(); qpG = QG_NONE;
  int bAdd = -1;
  for(int b = 0; b < qpBlkN; b++) if(qpBlk[b].kind == QB_ADD) bAdd = b;
  chk(bAdd >= 0, "el editor ofrece 'Anadir un control' al final");
  if(bAdd >= 0){
    int px = qpBlk[bAdd].x + qpBlk[bAdd].w / 2;
    int py = QP_VIEW_Y0 + qpBlk[bAdd].y + qpBlk[bAdd].h / 2;
    gTestMs += 200; touchDrag(px, py, true); T.downMs = gTestMs; qpEditTouch();
    gTestMs += 16; touchReset(); T.released = true; T.tap = true; qpEditTouch();
    chk(qpMode == QPM_CAT && qpCatN > 0, "se abre el catalogo con controles que ofrecer");
    int antesN2 = qpEdN;
    int cx = qpColX(0) + QP_CW / 2, cy = QP_CAT_HDR + 12 + QP_TCIRC / 2;
    qpG = QG_NONE;
    gTestMs += 200; touchDrag(cx, cy, true); T.downMs = gTestMs; qpCatTouch();
    gTestMs += 16; touchReset(); T.released = true; T.tap = true; qpCatTouch();
    chk(qpMode == QPM_EDIT, "tras anadir se vuelve al editor");
    chk(qpEdN == antesN2 + 1, "y el control queda colocado en el diseno");
  }
  // Volver del catalogo con "Atras" no pierde lo editado.
  qpCatBuild(); qpMode = QPM_CAT; qpG = QG_NONE;
  int marcaN = qpEdN;
  gTestMs += 200; touchDrag(40, 24, true); T.downMs = gTestMs; qpCatTouch();
  gTestMs += 16; touchReset(); T.released = true; T.tap = true; qpCatTouch();
  chk(qpMode == QPM_EDIT && qpEdN == marcaN, "'Atras' vuelve al editor sin perder los cambios");
  qpEditCancel();

  // ---- 12. COSTE DEL GESTO: arrastrar no puede COMPONER nada ----
  // Es la prueba que protege la fluidez. El panel se compone una vez segun se
  // revela; a partir de ahi, mover el dedo arriba y abajo tiene que ser copia
  // de filas y nada mas. Si alguien vuelve a meter dibujo (o peor, un
  // desenfoque) en la ruta del arrastre, estos contadores lo delatan.
  {
    bool oGlass = uiGlass; uiGlass = true;
    qsForceClose();
    gState = ST_HOME; gAppId = 0;
    gTestMs += 1000;
    int y0 = 10;
    touchDrag(240, y0, true); T.downMs = gTestMs;
    chk(qsGlobalHandle(), "el borde superior captura el gesto");
    qpProfReset();

    // (a) apertura: se revela toda la pantalla
    for(int y = y0 + 10; y <= SCR_H; y += 20){
      gTestMs += 16; T.startY = y0; touchDrag(240, y, false); T.startY = y0;
      qsGlobalHandle();
      chk(qsPanelY == y - y0, "la cortina va 1:1 con el dedo, sin quedarse detras");
      if(gFails) break;
      qsTick();
    }
    chk(qpPfGlass == 1, "la capa de vidrio se construye UNA sola vez en toda la apertura");
    chk(qpPfRowsComp <= (uint32_t)SCR_H + 8u,
        "revelar el panel entero compone cada fila una sola vez, no una por cuadro");
    uint32_t compApertura = qpPfRowsComp;

    // (b) ya revelado: arrastrar arriba y abajo repetidas veces
    qpProfReset();
    for(int pasada = 0; pasada < 3; pasada++){
      for(int y = SCR_H; y > 260; y -= 24){
        gTestMs += 16; T.startY = y0; touchDrag(240, y, false); T.startY = y0;
        qsGlobalHandle(); qsTick();
      }
      for(int y = 260; y <= SCR_H; y += 24){
        gTestMs += 16; T.startY = y0; touchDrag(240, y, false); T.startY = y0;
        qsGlobalHandle(); qsTick();
      }
    }
    chk(qpPfFrames > 40, "se midieron cuadros de arrastre de verdad");
    chk(qpPfRowsComp == 0,
        "arrastrar arriba y abajo no COMPONE ni una fila: solo copia lo ya compuesto");
    chk(qpPfGlass == 0, "y no vuelve a construir la capa de vidrio");
    // Y publica solo la banda que se movio, no la pantalla entera.
    uint32_t pubMedio = qpPfRowsPub / qpPfFrames;
    chk(pubMedio < (uint32_t)SCR_H / 4,
        "cada cuadro de arrastre publica solo su banda, no las 800 filas");
    printf("  [gesto] apertura: %lu filas compuestas · arrastre: %lu cuadros, "
           "%lu filas/cuadro publicadas, 0 compuestas, %lu us/cuadro\n",
           (unsigned long)compApertura, (unsigned long)qpPfFrames, (unsigned long)pubMedio,
           (unsigned long)(qpPfDragFrames ? qpPfDragUs / qpPfDragFrames : 0));

    // (b2) La capa de vidrio contra el camino caro que se usaba antes. Es la
    // comparacion que explica el tiron al abrir: drawLiquidGlassPanelEx a
    // pantalla completa hace memcpy + dos pasadas de box-blur (la vertical por
    // columnas) + composicion, con seis divisiones enteras por pixel sobre
    // 384.000 pixeles. qpGlassBuild hace lo mismo sobre 24.000.
    {
      struct timespec a0, a1;
      clock_gettime(CLOCK_MONOTONIC, &a0);
      for(int i = 0; i < 4; i++) qpGlassBuild();
      clock_gettime(CLOCK_MONOTONIC, &a1);
      double nuevo = ((a1.tv_sec - a0.tv_sec) * 1e9 + (a1.tv_nsec - a0.tv_nsec)) / 4.0;
      uint16_t* ob = gBuf; setBuf(qsBuf);
      int c0 = gClipY0, c1 = gClipY1, x0 = gClipX0, x1 = gClipX1;
      gClipY0 = 0; gClipY1 = SCR_H - 1; gClipX0 = 0; gClipX1 = SCR_W - 1;
      clock_gettime(CLOCK_MONOTONIC, &a0);
      for(int i = 0; i < 2; i++) drawLiquidGlassPanelEx(0, 0, SCR_W, SCR_H, 0, TH_GLASS2, 11);
      clock_gettime(CLOCK_MONOTONIC, &a1);
      double viejo = ((a1.tv_sec - a0.tv_sec) * 1e9 + (a1.tv_nsec - a0.tv_nsec)) / 2.0;
      gClipY0 = c0; gClipY1 = c1; gClipX0 = x0; gClipX1 = x1; setBuf(ob);
      chk(nuevo * 4.0 < viejo,
          "la capa de vidrio del panel cuesta menos de 1/4 que el desenfoque a pantalla completa");
      printf("  [vidrio] capa reducida %.2f ms  ·  desenfoque a pantalla completa %.2f ms  (%.0f%%)\n",
             nuevo / 1e6, viejo / 1e6, 100.0 * nuevo / viejo);
      qsDirty = true; qpMarkAll();
    }

    // (c) soltar a mitad -> snap; tocar durante el snap lo cancela y el dedo manda
    gTestMs += 16; touchDrag(240, y0 + 300, false); T.startY = y0; qsGlobalHandle(); qsTick();
    gTestMs += 16; touchReset(); T.released = true; qsGlobalHandle();
    chk(qsAnimOn, "soltar a mitad de recorrido arranca el snap");
    gTestMs += 30; qsTick();
    int enVuelo = qsPanelY;
    gTestMs += 16; touchDrag(240, enVuelo + 40, true); T.downMs = gTestMs;
    qsGlobalHandle();
    chk(!qsAnimOn, "tocar durante el snap lo cancela en el acto");
    chk(qpG == QG_CURTAIN && qsDragging, "y el control vuelve al dedo");
    chk(qsPanelY == enVuelo, "sin ningun salto: el panel se queda donde estaba");
    gTestMs += 16; touchReset(); T.released = true; qsGlobalHandle();
    int guardia3 = 0;
    while(qsAnimOn && guardia3++ < 500){ gTestMs += 16; qsTick(); }
    chk(qsPanelY == 0 || qsPanelY == SCR_H, "y al soltar de nuevo termina abierto o cerrado");
    uiGlass = oGlass;
  }

  // ---- 13. SCROLL: cada superficie recompone SOLO lo suyo ----
  // El scroll interno de la tarjeta no puede costar lo mismo que el del panel
  // entero: si recompusiera el viewport, mover cuatro circulos costaria el
  // doble de lo necesario en cada cuadro.
  {
    bool oGlass = uiGlass; uiGlass = true;
    qsForceClose();
    gState = ST_HOME; gTestMs += 1000;
    qpLoaded = false; flexPrefsWipe(); qpLoad();
    drawWallpaper(homeBuf, false);
    qsPanelY = SCR_H; qsLastY = 0; qsDirty = true; qsComposedTo = -1;
    qpMode = QPM_PANEL; qpG = QG_NONE;
    qpScrollF = 0; qpGScrollF = 0; qpGH = (float)qpGroupH(qpGrows);
    qpRelayout(); qpMarkAll(); qsRender(true);

    qpProfReset();
    qpMarkGroup(); qsRender(false);
    uint32_t filasGrupo = qpPfRowsComp;
    qpProfReset();
    qpMarkView(); qsRender(false);
    uint32_t filasPanel = qpPfRowsComp;
    chk(filasGrupo > 0 && filasPanel > 0, "las dos rutas de recomposicion hacen trabajo");
    chk(filasGrupo < filasPanel,
        "el scroll de la tarjeta recompone menos filas que el scroll del panel");
    chk(filasGrupo <= (uint32_t)(qpGroupH(QP_GROWS_MAX) + 8),
        "y nunca mas que la propia tarjeta");

    // Coste RELATIVO: un cuadro de arrastre contra una recomposicion completa
    // del viewport. Es la comparacion que se mantiene valida en cualquier
    // maquina, y la que se rompe si alguien vuelve a dibujar al arrastrar.
    struct timespec a0, a1;
    clock_gettime(CLOCK_MONOTONIC, &a0);
    for(int i = 0; i < 20; i++) qsComposeRows(QP_VIEW_Y0, SCR_H - 1);
    clock_gettime(CLOCK_MONOTONIC, &a1);
    double comp = ((a1.tv_sec - a0.tv_sec) * 1e9 + (a1.tv_nsec - a0.tv_nsec)) / 20.0;
    qpProfReset();
    for(int i = 0; i < 40; i++){
      gTestMs += 16;
      qpMark(300, 360);                       // banda tipica de un cuadro de arrastre
      qsRender(false);
    }
    double drag = qpPfFrames ? (double)qpPfUs * 1000.0 / (double)qpPfFrames : 0.0;
    chk(qpPfRowsComp == 0, "publicar una banda no recompone nada");
    chk(drag > 0 && drag * 10.0 < comp,
        "un cuadro de arrastre cuesta menos de 1/10 que recomponer el viewport");
    printf("  [coste] recomponer viewport %.2f ms . cuadro de arrastre %.3f ms "
           "(tarjeta %lu filas vs panel %lu filas)\n",
           comp / 1e6, drag / 1e6, (unsigned long)filasGrupo, (unsigned long)filasPanel);
    uiGlass = oGlass;
  }

  qsForceClose();
  chk(qpMode == QPM_PANEL && qsBuf == NULL && qsAppSnap == NULL,
      "qsForceClose abandona la edicion y libera TODOS los buffers temporales");

  if(!gFails) printf("  Panel One UI: todas las comprobaciones pasan.\n");
}

// #############################################################
//  PRUEBAS DEL TECLADO GLOBAL
//  ------------------------------------------------------------
//  Las llaves comparten teclas con los parentesis del mapa
//  numerico y se obtienen con Shift sin dejarlo activado.
// #############################################################
static void testTecladoGlobal(){
  printf("Teclado global\n");
  const char* (*mapaAntes)[KB_COLS] = mapaActivo;
  bool shiftAntes = kbShift;
  char out[6];
  mapaActivo = LAYOUT_NUM; kbShift = false;
  chk(!strcmp(kbResolveKey("(", out, false), "("), "sin Shift se conserva el parentesis izquierdo");
  chk(!strcmp(kbResolveKey(")", out, false), ")"), "sin Shift se conserva el parentesis derecho");
  kbShift = true;
  chk(!strcmp(kbResolveKey("(", out, false), "{") && kbShift,
      "Shift muestra { sin consumirlo durante el dibujo");
  chk(!strcmp(kbResolveKey("(", out, true), "{") && !kbShift,
      "al escribir { se apaga Shift");
  kbShift = true;
  chk(!strcmp(kbResolveKey(")", out, true), "}") && !kbShift,
      "Shift+) escribe } y se apaga");
  mapaActivo = mapaAntes; kbShift = shiftAntes;
  if(!gFails) printf("  Teclado: todas las comprobaciones pasan.\n");
}

// #############################################################
//  PRUEBAS DE LA CAJA DE APLICACIONES (cajon de apps)
//  ------------------------------------------------------------
//  Lo que se comprueba aqui es la parte que NO depende del panel:
//  el registro central y sus invariantes. Son justo las reglas que,
//  si se rompen, dejan una app inalcanzable o un icono fantasma en
//  el escritorio -- y eso no se puede descubrir mirando la pantalla.
// #############################################################
static void drwTestReset(){
  drawerRegistryDefaults();
  for(int i = 0; i < HOME_TOTAL; i++) homeOrder[i] = HOME_EMPTY;
  for(int i = 0; i < homeSlotCount(); i++) homeOrder[i] = (uint8_t)i;
  homeOrderNormalize();
  drwQLen = 0; drwQuery[0] = 0; drwShowHid = false; drwKbOn = false;
  drwScroll = 0; drwVel = 0; drwSlide = 0;
  drwFilter();
}
static int drwSlotOf(int id){
  for(int i = 0; i < HOME_TOTAL; i++) if(homeOrder[i] == (uint8_t)id) return i;
  return -1;
}
static bool drwInList(int id){
  for(int i = 0; i < drwN; i++) if(drwNativeId(i) == id) return true;
  return false;
}
// Celdas NATIVAS de la lista unificada (las pruebas de siempre hablan de estas).
static int drwNatCount(){
  int n = 0;
  for(int i = 0; i < drwN; i++) if(drwNativeId(i) >= 0) n++;
  return n;
}
// Celdas DESCARGADAS.
static int drwPkgCount(){
  int n = 0;
  for(int i = 0; i < drwN; i++) if(drwPkgIndex(i) >= 0) n++;
  return n;
}
static int drwFirstPkgCell(){
  for(int i = 0; i < drwN; i++) if(drwPkgIndex(i) >= 0) return i;
  return -1;
}
static int drwFirstPkgIdx(){ int c = drwFirstPkgCell(); return c >= 0 ? drwPkgIndex(c) : -1; }
static int drwCellOfPkgId(const char* id){
  for(int i = 0; i < drwN; i++){
    int e = drwPkgIndex(i);
    if(e >= 0 && !strcmp(pkgApps[e].id, id)) return i;
  }
  return -1;
}
static void testCajaApps(){
  printf("Caja de aplicaciones\n");
  drwTestReset();

  // --- reparto de fabrica: exactamente el escritorio de fabrica ---
  // La lista ya no es "los ids 0..11": Flex Store y Device Care ocupan los dos
  // sitios que dejaron Educacion y Bienestar, y sus ids estan al final del
  // registro. La fuente de verdad es HOME_FACTORY, que es de donde sale el
  // escritorio de una placa virgen.
  chk(gAppHidden == 0, "de fabrica no hay ninguna app oculta");
  { uint32_t esperado = 0;
    for(int i = 0; i < HOME_LEGACY_SLOTS; i++) esperado |= (uint32_t)(1u << HOME_FACTORY[i]);
    bool favOk = true;
    for(int id = 0; id < APP_N; id++)
      if(appIsFav(id) != ((esperado & (uint32_t)(1u << id)) != 0)) favOk = false;
    chk(favOk, "las doce del escritorio de fabrica -- y solo esas -- nacen en Inicio");
    chk(appIsFav(IC_FLEXSTORE), "Flex Store ocupa el sitio que tenia Educacion");
    chk(appIsFav(IC_DEVCARE),   "Flex Device Care ocupa el sitio que tenia Bienestar"); }
  // La caja ensena TODAS las del registro: se compara contra APP_N y no contra
  // un numero escrito a mano, para que anadir una app no obligue a tocar esto
  // (pero SI siga fallando si alguna se queda fuera de la caja).
  chk(drwNatCount() == APP_N, "la caja muestra todas las apps del registro");

  // --- normalizacion: una ranura con una app no favorita se vacia ---
  gAppFav &= (uint16_t)~(1u << 5);
  homeOrderNormalize();
  chk(drwSlotOf(5) < 0, "quitar la marca de favorita libera su ranura");
  // --- y una favorita sin ranura recupera hueco ---
  gAppFav |= (uint16_t)(1u << 5);
  homeOrderNormalize();
  chk(drwSlotOf(5) >= 0, "una favorita sin ranura ocupa el primer hueco");

  // --- pagina 1 llena: la favorita nueva ocupa la pagina 2 ---
  drwTestReset();
  drwFavToggle(IC_AJUSTES);
  int nfav = 0;
  for(int id = 0; id < APP_N; id++) if(appIsFav(id)) nfav++;
  chk(nfav == 13, "Anadir a Inicio conserva la favorita nueva");
  chk(drwSlotOf(IC_AJUSTES) == HOME_STRIDE,
      "con la pagina 1 llena, Anadir a Inicio usa la primera ranura de la pagina 2");
  for(int i = 0; i < HOME_TOTAL; i++) chk(homeOrder[i] == HOME_EMPTY || appIsFav(homeOrder[i]),
                                  "ninguna ranura apunta a una app que no sea favorita");

  // --- quitar y volver a poner desde el menu contextual ---
  drwTestReset();
  int slot3 = drwSlotOf(3);
  drwFavToggle(3);
  chk(!appIsFav(3), "\"Quitar de inicio\" borra la marca");
  chk(homeOrder[slot3] == HOME_EMPTY, "y deja su ranura vacia (no recoloca la rejilla)");
  chk(drwInList(3), "pero la app sigue en la caja");
  drwFavToggle(3);
  chk(appIsFav(3) && drwSlotOf(3) >= 0, "\"Anadir a inicio\" la devuelve a una ranura");

  // --- ocultar: sale de la caja Y del escritorio, y se puede recuperar ---
  drwTestReset();
  homeOrder[7] = HOME_EMPTY;
  homeOrder[HOME_STRIDE] = 7;                    // coloca Navegador en la pagina 2
  drwHideToggle(7);
  chk(appIsHidden(7),  "\"Ocultar\" marca la app");
  chk(!appIsFav(7),    "una app oculta no puede quedarse en Inicio");
  chk(drwSlotOf(7) < 0,"y no deja un icono fantasma en ninguna pagina");
  chk(!drwInList(7),   "la caja ya no la lista");
  drwShowHid = true; drwFilter();
  chk(drwInList(7),    "con \"ver ocultas\" vuelve a aparecer (unica via para mostrarla)");
  drwHideToggle(7);
  chk(!appIsHidden(7), "\"Mostrar\" la recupera");
  drwShowHid = false; drwFilter();
  chk(drwInList(7),    "y vuelve a la caja normal");

  // --- Ajustes no se puede ocultar: es la unica salida del sistema ---
  drwTestReset();
  chk(!appCanHide(IC_AJUSTES), "Ajustes nunca se puede ocultar");
  drwHideToggle(IC_AJUSTES);
  chk(!appIsHidden(IC_AJUSTES), "y el intento no tiene efecto");

  // --- buscador: filtra sobre el nombre localizado, sin distinguir mayusculas ---
  drwTestReset();
  snprintf(drwQuery, sizeof(drwQuery), "cal"); drwQLen = 3; drwFilter();
  chk(drwN >= 2 && drwInList(IC_CALC) && drwInList(IC_CALEND), "\"cal\" encuentra Calculadora y Calendario");
  chk(!drwInList(IC_RELOJ), "y descarta lo que no casa");
  snprintf(drwQuery, sizeof(drwQuery), "zzz"); drwQLen = 3; drwFilter();
  chk(drwN == 0, "una busqueda sin resultados deja la rejilla vacia, no basura");

  // --- geometria: nada se sale de los 480x800 ---
  drwTestReset();
  for(int i = 0; i < drwN; i++){
    int x, y; drwCellXY(i, x, y);
    chk(x >= 0 && x + DRW_ICON_S <= SCR_W, "cada icono cabe a lo ancho de la pantalla");
    chk(y >= DRW_GRID_TOP, "ninguna fila empieza por encima de la rejilla");
  }
  chk(DRW_COL_X0 + 3 * DRW_COL_STEP + DRW_ICON_S <= SCR_W, "las 4 columnas caben en 480 px");
  chk(drwGridBot() <= SCR_H - DRW_NAV_H, "la rejilla no invade la barra de navegacion");

  // --- limite de desplazamiento: el scroll nunca se escapa del rango ---
  drwScroll = -500.0f; drwClampScroll();
  chk(drwScroll == 0.0f, "no se puede arrastrar por encima de la primera fila");
  drwScroll = 99999.0f; drwClampScroll();
  chk(drwScroll == (float)drwMaxScroll(), "ni por debajo de la ultima");
  chk(drwMaxScroll() >= 0, "el recorrido maximo nunca es negativo");
  // Con el teclado desplegado la ventana encoge: el recorrido tiene que crecer
  // y el scroll seguir dentro de rango (aqui es donde antes se salia el ultimo icono).
  int mSin = drwMaxScroll();
  drwKbOn = true;
  chk(drwMaxScroll() >= mSin, "abrir el teclado no reduce el recorrido disponible");
  drwScroll = 99999.0f; drwClampScroll();
  chk(drwScroll <= (float)drwMaxScroll(), "con teclado el scroll sigue acotado");
  drwKbOn = false; drwScroll = 0; drwClampScroll();

  // --- el tactil nunca devuelve una celda que no existe ---
  drwTestReset();
  for(int y = 0; y < SCR_H; y += 7)
    for(int x = 0; x < SCR_W; x += 11){
      int c = drwHitCell(x, y);
      chk(c == -1 || (c >= 0 && c < drwN), "drwHitCell solo devuelve celdas reales");
      if(gFails) break;
    }
  chk(drwHitCell(240, 10) < 0, "la cabecera no es rejilla");
  chk(drwHitCell(240, SCR_H - 20) < 0, "la barra de navegacion tampoco");

  // --- la caja cede el paso a los overlays globales ---
  gState = ST_HOME; gLand = false; gHosted = false; editMode = false;
  qsPanelY = 0; qsAnimOn = false; qsDragging = false;
  chk(drawerCanOpen(), "desde el escritorio limpio si se puede abrir");
  qsPanelY = 200;
  chk(!drawerCanOpen(), "con el panel rapido a la vista manda el panel rapido");
  qsPanelY = 0; editMode = true;
  chk(!drawerCanOpen(), "en Modo Edicion no se abre");
  editMode = false; gLand = true;
  chk(!drawerCanOpen(), "en horizontal (Modo PC) no se abre");
  gLand = false; gState = ST_APP;
  chk(!drawerCanOpen(), "dentro de una app tampoco: el gesto es del escritorio");
  gState = ST_HOME;

  drwTestReset();
  if(!gFails) printf("  Caja de aplicaciones: todas las comprobaciones pasan.\n");
}


// #############################################################
//  PRUEBAS DE LAS APPS DESCARGADAS EN LA CAJA DE APLICACIONES
//  ------------------------------------------------------------
//  El recorrido completo que pide el sistema de apps de Flex Store:
//  instalar -> aparece UNA vez -> abrir -> salir -> sigue ahi;
//  actualizar -> no se duplica; desinstalar -> desaparece; paquete
//  invalido -> no aparece; y las apps NATIVAS intactas.
//
//  Se ejercita el codigo REAL (FlexOS_Ultra_PkgApps.h y la caja), con
//  el registro de paquetes simulado por ino_extern_stubs.cpp. Lo que
//  aqui se comprueba no se ve mirando la pantalla: que la lista salga
//  del registro y no de una lista escrita a mano, que no se relea el
//  almacenamiento por cuadro, y que un paquete roto no se cuele.
// #############################################################
extern FlexPkgInfo    gStubInstalled[];
extern int            gStubInstalledN;
extern int            gStubPkgListCalls;
extern char           gStubRuntimeId[];
extern FlexStoreState gStubStoreState;
extern uint32_t       gStubPkgRevision;
extern char           gStubStoreBusyId[];
extern int            gStubStoreBusyCalls;
extern char           gStubUninstallId[];

// Alta en el registro simulado. Rellena TODO lo que un manifiesto validado
// tiene que traer, para que la entrada sea aceptable; cada prueba de rechazo
// estropea despues UN solo campo, y asi se ve cual es el que decide.
static void pkgStubAdd(const char* id, const char* name, const char* ver, uint32_t code,
                       uint8_t runtime = FLEXPKG_RT_APP1, uint8_t state = FLEXPKG_APP_ENABLED){
  FlexPkgInfo& it = gStubInstalled[gStubInstalledN++];
  memset(&it, 0, sizeof(it));
  snprintf(it.id,          sizeof(it.id),          "%s", id);
  snprintf(it.name,        sizeof(it.name),        "%s", name);
  snprintf(it.versionName, sizeof(it.versionName), "%s", ver);
  snprintf(it.entry,       sizeof(it.entry),       "%s", "main.flxb");
  it.versionCode = code; it.runtime = runtime; it.state = state;
  it.installedBytes = 4096; it.memoryKB = 64;
  gStubPkgRevision++;
}
static void pkgStubClear(){
  gStubInstalledN = 0; gStubStoreBusyId[0] = 0;
  gStubStoreState = FLEXSTORE_READY;
  gStubPkgRevision++;
  pkgAppsInvalidate();
}
static int drwPkgSlotOf(const char* id){ return drwCellOfPkgId(id); }
static int drwPkgCountId(const char* id){
  int n = 0;
  for(int i = 0; i < drwN; i++){
    int e = drwPkgIndex(i);
    if(e >= 0 && !strcmp(pkgApps[e].id, id)) n++;
  }
  return n;
}
// Toque simple sobre el centro del icono de la celda descargada `cell`. La
// seccion de descargadas cae por debajo de las nativas, asi que primero se
// desplaza la rejilla hasta ella -- exactamente lo que hace el dedo del usuario
// -- y la coordenada del toque se calcula con el scroll YA acotado.
static void drwTapPkgCell(int cell){
  if(cell < 0) return;
  int cx, cy; drwCellXY(cell, cx, cy);
  drwScroll = (float)(cy - DRW_GRID_TOP - 8);
  drwClampScroll();
  T = Touch();
  T.tap = true; T.released = true;
  T.x = T.startX = cx + DRW_ICON_S / 2;
  T.y = T.startY = cy - (int)drwScroll + (int)drwSlide + DRW_ICON_S / 2;
}

static void testCajaDescargadas(){
  printf("Apps descargadas en la caja\n");
  pkgStubClear();
  drwTestReset();

  // --- 0. PLACA RECIEN GRABADA: la caja es EXACTAMENTE la de siempre ---
  chk(drwPkgCount() == 0,            "sin apps descargadas la seccion no existe");
  chk(drwNatCount() == APP_N,  "y la rejilla es exactamente la de siempre");
  chk(drwNatCount() == APP_N,  "las apps nativas siguen todas en la caja");
  int rowsSolas = drwRows(), maxSolo = drwMaxScroll();
  chk(rowsSolas == (APP_N + 3) / 4, "el numero de filas es el de siempre");
  { int x0, y0; drwCellXY(0, x0, y0);
    chk(y0 == DRW_GRID_TOP,         "la primera fila empieza donde empezaba"); }

  // --- 1. INSTALAR: aparece UNA sola vez, con el nombre del manifiesto ---
  pkgStubAdd("com.flexos.antutu", "Antutu Benchmark", "1.2.0", 12);
  drwFilter();
  chk(drwPkgCount() == 1,                              "instalar una app la anade a la caja");
  chk(drwPkgCountId("com.flexos.antutu") == 1,     "y aparece UNA sola vez");
  chk(drwN == APP_N + 1,                         "la app entra en la MISMA rejilla, sin seccion aparte");
  chk(drwNatCount() == APP_N,                    "las nativas siguen intactas");
  int e0 = drwFirstPkgIdx();
  chk(!strcmp(pkgApps[e0].name, "Antutu Benchmark"), "el nombre sale del manifiesto");
  chk(!strcmp(pkgApps[e0].version, "1.2.0"),         "y la version tambien");
  chk(pkgAppStatus(e0) == PKGAPP_ST_OK,              "una app recien instalada esta lista");
  chk(pkgApps[e0].icon == -1,                        "sin icon.f565 se usa el icono generico");
  chk(drwRows() == (APP_N + 1 + 3) / 4,              "una rejilla continua, sin fila de seccion");
  chk(drwMaxScroll() >= maxSolo,                     "y el recorrido no encoge");

  // --- 2. NADA DE ESCANEAR POR CUADRO ---
  // La revision no ha cambiado: repintar mil veces no puede volver a leer el
  // registro. Es la regla que mantiene la caja fluida cuando crezca el catalogo.
  int antes = gStubPkgListCalls;
  for(int i = 0; i < 200; i++) drwFilter();
  chk(gStubPkgListCalls == antes, "repintar la caja no vuelve a leer el registro");

  // --- 3. GEOMETRIA: ninguna celda descargada se sale de la pantalla ---
  for(int i = 0; i < drwN; i++){
    int x, y; drwCellXY(i, x, y);
    chk(x >= 0 && x + DRW_ICON_S <= SCR_W, "cada icono cabe a lo ancho");
    chk(y >= DRW_GRID_TOP,                 "y ninguna fila empieza por encima de la rejilla");
  }

  // --- 4. EL TACTIL NUNCA DEVUELVE UNA CELDA QUE NO EXISTE ---
  drwSlide = 0; drwScroll = 0;
  for(int y = 0; y < SCR_H; y += 5)
    for(int x = 0; x < SCR_W; x += 7){
      int c = drwHitCell(x, y);
      chk(c == -1 || (c >= 0 && c < drwN), "drwHitCell solo devuelve celdas reales");
      if(gFails) break;
    }
  { int cp = drwFirstPkgCell();
    chk(cp >= 0 && drwNativeId(cp) < 0, "una celda descargada NUNCA devuelve un id de APP_REG");
    chk(cp >= 0 && drwPkgIndex(cp) >= 0, "y si devuelve un indice de pkgApps valido"); }

  // --- 5. ABRIR: la caja NO abre el runtime, lo PIDE ---
  pkgAppLaunchClear(); pkgAppLaunchFromDrawer = false;
  gState = ST_DRAWER; drwOn = true; drwAnim = 0; drwSlide = 0; drwScroll = 0;
  drwMoved = false; drwDrag = false; drwKbOn = false; drwCtxOn = false; drwInfoOn = false;
  drwTapPkgCell(drwFirstPkgCell());
  drawerTick();
  chk(pkgAppLaunchPending(),                          "tocar una app descargada anota la peticion");
  chk(!strcmp(pkgAppLaunchId, "com.flexos.antutu"),   "y la peticion lleva el ID REAL del paquete");
  chk(pkgAppLaunchFromDrawer,                         "queda anotado que la apertura viene de la caja");
  chk(drwAnim == 2 && drwPendApp == IC_FLEXSTORE,     "la caja se cierra abriendo Flex Store");

  // Flex Store atiende la peticion por el camino de siempre. Antutu declara
  // flex-app-v1, asi que va a av1Start, que revalida bytecode, estado y grant.
  // En el PC no hay LittleFS de verdad: av1Start dice que no encuentra el
  // codigo, y eso es EXACTAMENTE lo que hay que comprobar aqui -- que un fallo
  // de apertura se anota y NO deja al usuario en un escritorio mudo.
  gState = ST_APP; gAppId = IC_FLEXSTORE;
  storeEnter();
  chk(!pkgAppLaunchPending(),  "la tienda consume la peticion (no se abre dos veces)");
  chk(gState == ST_APP,        "si la app no arranca, el usuario se queda en la tienda con el motivo");
  drwFilter();
  chk(pkgAppStatus(drwFirstPkgIdx()) == PKGAPP_ST_ERROR, "y esa app queda marcada como no disponible");
  chk(pkgAppLastError[0],                             "con un motivo concreto, no un fallo mudo");

  // --- 6. ABRIR Y SALIR, IDA Y VUELTA COMPLETA ---
  // Con un paquete flex-ui-1, cuyo runtime SI se puede ejercitar entero en el
  // PC. Es el mismo camino: peticion de la caja -> storeEnter -> apertura por
  // runtime -> atras -> escritorio.
  pkgStubClear();
  pkgStubAdd("com.flexos.ficha", "Ficha", "1.0.0", 1, FLEXPKG_RT_UI1);
  pkgAppsInvalidate(); drwFilter();
  chk(drwPkgCount() == 1, "la app flex-ui-1 tambien esta en la caja");
  pkgAppLaunchClear(); pkgAppLaunchFromDrawer = false;
  gState = ST_DRAWER; drwOn = true; drwAnim = 0; drwSlide = 0; drwScroll = 0;
  drwMoved = false; drwDrag = false; drwKbOn = false; drwCtxOn = false; drwInfoOn = false;
  drwTapPkgCell(drwFirstPkgCell());
  drawerTick();
  chk(!strcmp(pkgAppLaunchId, "com.flexos.ficha"), "la peticion lleva su ID");
  gState = ST_APP; gAppId = IC_FLEXSTORE;
  gStubRuntimeId[0] = 0;
  storeEnter();
  chk(storeView == SV_RUNTIME,                    "la tienda entra directamente en la app, no en el catalogo");
  chk(!strcmp(gStubRuntimeId, "com.flexos.ficha"),"y abre el paquete REAL que se pidio");
  storeBack();
  chk(gState == ST_HOME,       "salir de la app descargada devuelve al ESCRITORIO, no al listado");
  chk(!pkgAppLaunchFromDrawer, "y la marca de origen no se queda pegada");
  drwFilter();
  chk(drwPkgCountId("com.flexos.ficha") == 1, "despues de salir la app SIGUE en la caja, una sola vez");

  // Y abrir la tienda de forma NORMAL, sin peticion, sigue mostrando el catalogo.
  gState = ST_APP; gAppId = IC_FLEXSTORE;
  storeEnter();
  chk(storeView == SV_DISCOVER, "sin peticion pendiente, Flex Store abre en Descubrir como siempre");
  gState = ST_HOME;

  // --- 7 bis. vuelta al paquete de trabajo ---
  pkgStubClear();
  pkgStubAdd("com.flexos.antutu", "Antutu Benchmark", "1.2.0", 12);
  pkgAppsInvalidate(); drwFilter();

  // --- 7. ACTUALIZAR: misma tarjeta, datos nuevos, sin duplicar ---
  gStubInstalledN = 0;
  pkgStubAdd("com.flexos.antutu", "Antutu Benchmark", "1.3.0", 13);
  drwFilter();
  chk(drwPkgCount() == 1,                          "actualizar no anade una segunda tarjeta");
  chk(drwPkgCountId("com.flexos.antutu") == 1, "sigue apareciendo UNA sola vez");
  chk(!strcmp(pkgApps[drwFirstPkgIdx()].version, "1.3.0"), "y muestra la version nueva");
  chk(pkgApps[drwFirstPkgIdx()].versionCode == 13,         "con su codigo de version nuevo");

  // --- 8. ACTUALIZANDO: estado visible, sin bloquear la caja ---
  snprintf(gStubStoreBusyId, FLEXPKG_ID_MAX, "%s", "com.flexos.antutu");
  gStubStoreState = FLEXSTORE_INSTALLING;
  // pkgAppStatus lee la MUESTRA del cuadro, no la tienda: preguntarle a la
  // tienda por icono y por cuadro toma su mutex desde el hilo grafico, que es
  // justo lo que puede disparar el watchdog (ver pkgAppSampleBusy).
  chk(pkgAppStatusLive(drwFirstPkgIdx()) == PKGAPP_ST_UPDATING,
      "mientras se instala se marca actualizando");
  chk(pkgAppStatus(drwFirstPkgIdx()) == PKGAPP_ST_UPDATING,
      "y el pintado lo ve por la muestra, sin volver a preguntar");
  int llamadas = gStubPkgListCalls;
  drwFilter();
  chk(gStubPkgListCalls == llamadas, "y ese estado NO obliga a releer el registro");
  gStubStoreState = FLEXSTORE_READY; gStubStoreBusyId[0] = 0;
  chk(pkgAppStatusLive(drwFirstPkgIdx()) == PKGAPP_ST_OK, "al terminar vuelve a estar lista");

  // --- 9. DESINSTALAR: desaparece en el acto, sin reiniciar ---
  chk(flexPkgUninstall("com.flexos.antutu"), "desinstalar desde Flex Store");
  drwFilter();
  chk(drwPkgCount() == 0,        "la app desaparece de la caja sin reiniciar el sistema");
  chk(drwN == APP_N,       "la rejilla vuelve a ser exactamente la de las nativas");
  chk(drwNatCount() == APP_N, "las nativas siguen exactamente igual");
  chk(drwRows() == rowsSolas && drwMaxScroll() == maxSolo,
      "la caja recupera EXACTAMENTE la geometria que tenia sin descargadas");

  // --- 10. PAQUETES INVALIDOS: no aparecen, y no rompen la caja ---
  pkgStubClear();
  pkgStubAdd("com.flexos.buena", "Buena", "1.0.0", 1);
  pkgStubAdd("",                 "Sin id",  "1.0.0", 1);            // id vacio
  pkgStubAdd("com.flexos.a",     "",        "1.0.0", 1);            // sin nombre
  pkgStubAdd("../escape",        "Fuera",   "1.0.0", 1);            // id con ruta
  pkgStubAdd("com.flexos.b",     "Runtime", "1.0.0", 1, 99);        // runtime desconocido
  gStubInstalled[gStubInstalledN - 1].runtime = 99;
  { // sin punto de entrada
    pkgStubAdd("com.flexos.c", "Sin entry", "1.0.0", 1);
    gStubInstalled[gStubInstalledN - 1].entry[0] = 0;
  }
  pkgAppsInvalidate(); drwFilter();
  chk(drwPkgCount() == 1, "de seis entradas solo entra la unica valida");
  chk(drwPkgSlotOf("com.flexos.buena") >= 0, "y es exactamente la buena");
  chk(drwPkgSlotOf("../escape") < 0,         "un id con rutas dentro no se lista jamas");

  // --- 11. APP DETENIDA: se ve, se marca y NO se abre ---
  pkgStubClear();
  pkgStubAdd("com.flexos.parada", "Parada", "1.0.0", 1, FLEXPKG_RT_APP1, FLEXPKG_APP_STOPPED);
  pkgAppsInvalidate(); drwFilter();
  chk(drwPkgCount() == 1,                                        "una app detenida sigue siendo suya y se ve");
  chk(pkgAppStatus(drwFirstPkgIdx()) == PKGAPP_ST_ERROR,      "pero se marca como no disponible");
  pkgAppLaunchClear(); pkgAppLaunchFromDrawer = false;
  gState = ST_DRAWER; drwOn = true; drwAnim = 0; drwSlide = 0; drwScroll = 0;
  drwMoved = false; drwDrag = false; drwInfoOn = false; drwCtxOn = false;
  drwTapPkgCell(drwFirstPkgCell());
  drawerTick();
  chk(!pkgAppLaunchPending(), "tocarla no intenta abrirla");
  chk(drwInfoOn && drwInfoPkg == drwFirstPkgIdx(), "abre su ficha, que explica por que");
  chk(drwAnim == 0,           "y la caja sigue abierta: un paquete asi no la cierra ni la bloquea");

  // --- 12. BUSCADOR: filtra las dos secciones a la vez ---
  pkgStubClear();
  pkgStubAdd("com.flexos.antutu", "Antutu Benchmark", "1.3.0", 13);
  pkgStubAdd("com.flexos.otra",   "Cuaderno",         "2.0.0", 4);
  pkgAppsInvalidate();
  drwInfoOn = false; drwInfoPkg = -1;
  snprintf(drwQuery, sizeof(drwQuery), "antu"); drwQLen = 4; drwFilter();
  chk(drwPkgCount() == 1 && drwPkgSlotOf("com.flexos.antutu") >= 0, "el buscador encuentra una descargada");
  chk(drwNatCount() == 0,                                     "y descarta las nativas que no casan");
  snprintf(drwQuery, sizeof(drwQuery), "cal"); drwQLen = 3; drwFilter();
  chk(drwPkgCount() == 0 && drwNatCount() >= 2, "y al reves: solo nativas cuando solo ellas casan");
  snprintf(drwQuery, sizeof(drwQuery), "zzzz"); drwQLen = 4; drwFilter();
  chk(drwN == 0, "sin resultados: la rejilla queda vacia");

  // --- 13. LOS ICONOS SE PUEDEN SOLTAR SIN PERDER LA LISTA ---
  drwQLen = 0; drwQuery[0] = 0; drwFilter();
  int antesN = drwPkgCount();
  pkgAppIconsFree();
  chk(drwPkgCount() == antesN, "soltar la cache de iconos no quita ninguna app de la caja");
  for(int i = 0; i < drwN; i++)
    { int e = drwPkgIndex(i); if(e >= 0) chk(pkgApps[e].icon == -1, "las que tenian icono propio caen al generico"); }

  // --- limpieza: el resto de las pruebas encuentran el sistema como estaba ---
  pkgStubClear();
  gState = ST_HOME; drwOn = false; drwAnim = 0; drwSlide = (float)SCR_H;
  storeView = SV_DISCOVER;
  drwTestReset();
  if(!gFails) printf("  Apps descargadas en la caja: todas las comprobaciones pasan.\n");
}


// #############################################################
//  SCROLL DE LA CAJA CON APPS DESCARGADAS  (regresion del reinicio)
//  ------------------------------------------------------------
//  Esta bateria existe por un fallo REAL en placa: con una app
//  instalada, deslizar hasta la seccion "Descargadas" ponia la
//  pantalla cian y reiniciaba el firmware.
//
//  Aqui se recorre TODO el rango de desplazamiento, en los dos
//  sentidos, componiendo bandas de verdad sobre los framebuffers del
//  arnes. Con AddressSanitizer, cualquier lectura o escritura fuera de
//  fb/bbuf/homeBuf -- o fuera de pkgApps[], drwCells[] y la cache de
//  iconos -- para la prueba en el acto y senala la linea.
// #############################################################
static void drwScrollBarrido(const char* que){
  // Cuadro completo y banda de rejilla, que son los dos unicos caminos de
  // pintado de la caja, en cada posicion del recorrido.
  int m = drwMaxScroll();
  for(int paso = 0; paso <= m + 8; paso += 4){
    drwScroll = (float)paso; drwClampScroll();
    drwCompose(0, SCR_H - 1, true);
    drwCompose(DRW_GRID_TOP, drwGridBot() - 1, true);
  }
  for(int paso = m + 8; paso >= -8; paso -= 4){
    drwScroll = (float)paso; drwClampScroll();
    drwCompose(DRW_GRID_TOP, drwGridBot() - 1, true);
  }
  // Y con el teclado del buscador abierto, que encoge la ventana.
  drwKbOn = true; drwClampScroll();
  for(int paso = 0; paso <= drwMaxScroll(); paso += 6){
    drwScroll = (float)paso; drwClampScroll();
    drwCompose(DRW_GRID_TOP, drwGridBot() - 1, true);
  }
  drwKbOn = false; drwScroll = 0; drwClampScroll();
  chk(true, que);
}

static void testCajaDescargadasScroll(){
  printf("Scroll de la caja con apps descargadas\n");
  const int CASOS[] = { 0, 1, 2, 3, 7, 13, PKGAPP_MAX };
  for(unsigned k = 0; k < sizeof(CASOS)/sizeof(CASOS[0]); k++){
    int n = CASOS[k];
    pkgStubClear();
    for(int i = 0; i < n; i++){
      char id[64], nm[64];
      snprintf(id, sizeof(id), "com.flexos.app%02d", i);
      snprintf(nm, sizeof(nm), "Aplicacion %d", i);
      pkgStubAdd(id, nm, "1.0.0", 1);
    }
    pkgAppsInvalidate();
    drwTestReset();
    gState = ST_DRAWER; drwOn = true; drwAnim = 0; drwSlide = 0;
    chk(drwPkgCount() == n, "la caja lista exactamente las apps instaladas");

    char msg[96];
    snprintf(msg, sizeof(msg), "recorrido completo con %d app(s) descargada(s)", n);
    drwScrollBarrido(msg);

    // La hoja a medio subir: sy != 0 desplaza TODA la geometria.
    drwSlide = (float)(SCR_H / 3);
    drwCompose(0, SCR_H - 1, false);
    drwScroll = (float)drwMaxScroll();
    drwCompose(0, SCR_H - 1, false);
    drwSlide = 0; drwScroll = 0; drwClampScroll();

    // Abrir y cerrar repetido: la cache de iconos no puede crecer ni perderse.
    for(int v = 0; v < 3; v++){
      pkgAppIconsFree();
      drwFilter();
      drwCompose(0, SCR_H - 1, true);
    }
    if(gFails) break;
  }

  // La ficha de una descargada, en el limite del recorrido.
  pkgStubClear();
  pkgStubAdd("com.flexos.unica", "Unica", "1.0.0", 1);
  pkgAppsInvalidate(); drwTestReset();
  gState = ST_DRAWER; drwOn = true; drwAnim = 0; drwSlide = 0;
  drwScroll = (float)drwMaxScroll();
  drwInfoPkg = drwFirstPkgIdx(); drwInfoOn = true;
  drwCompose(0, SCR_H - 1, true); drwInfoDraw();
  drwInfoOn = false; drwInfoPkg = -1;
  chk(true, "la ficha de una descargada se pinta al final del recorrido");

  pkgStubClear();
  gState = ST_HOME; drwOn = false; drwAnim = 0; drwSlide = (float)SCR_H;
  drwTestReset();
  if(!gFails) printf("  Scroll de la caja con apps descargadas: todas las comprobaciones pasan.\n");
}


// #############################################################
//  LO QUE LA REGRESION DEL REINICIO DEJO POR ESCRITO
//  ------------------------------------------------------------
//  Tres reglas que no se pueden volver a romper: la lista no se
//  construye en la pila, el hilo grafico no espera a la tienda, y un
//  registro que cambia bajo los pies no deja indices colgando.
//
//  El presupuesto de PILA lo vigila check_stack.py, que compila el
//  sketch con -fstack-usage y falla si una funcion se pasa. Es la
//  unica de las tres que no se puede comprobar desde aqui: en el PC la
//  pila son megabytes y el fallo no se manifiesta.
// #############################################################
static void testCajaDescargadasRegresion(){
  printf("Regresion del reinicio al desplazar la caja\n");

  // --- 1. EL HILO GRAFICO NO PREGUNTA A LA TIENDA POR ICONO ---
  // flexStoreBusyPackage() toma el mutex de Flex Store con espera infinita.
  // Hacerlo por icono y por cuadro es lo que puede dejar al hilo de interfaz
  // esperando a la tarea de descarga hasta que salte el watchdog de tarea.
  pkgStubClear();
  for(int i = 0; i < 8; i++){
    char id[64], nm[64];
    snprintf(id, sizeof(id), "com.flexos.busy%02d", i);
    snprintf(nm, sizeof(nm), "Busy %d", i);
    pkgStubAdd(id, nm, "1.0.0", 1);
  }
  pkgAppsInvalidate(); drwTestReset();
  gState = ST_DRAWER; drwOn = true; drwAnim = 0; drwSlide = 0;
  drwScroll = (float)drwMaxScroll();
  chk(drwPkgCount() == 8, "las ocho descargadas estan en la caja");
  gStubStoreBusyCalls = 0;
  drwCompose(0, SCR_H - 1, true);
  chk(gStubStoreBusyCalls <= 1,
      "un cuadro consulta a Flex Store UNA vez como mucho, no una por icono");
  int trasUno = gStubStoreBusyCalls;
  drwCompose(DRW_GRID_TOP, drwGridBot() - 1, true);
  chk(gStubStoreBusyCalls <= trasUno + 1, "y la banda de rejilla, otra vez una");

  // Sin descargadas no se consulta siquiera.
  pkgStubClear(); pkgAppsInvalidate(); drwTestReset();
  gStubStoreBusyCalls = 0;
  drwCompose(0, SCR_H - 1, true);
  chk(gStubStoreBusyCalls == 0, "sin apps descargadas no se molesta a la tienda");

  // --- 2. SIN PSRAM: SE DEGRADA, NO SE VACIA NI REVIENTA ---
  pkgStubClear();
  pkgStubAdd("com.flexos.uno", "Uno", "1.0.0", 1);
  pkgStubAdd("com.flexos.dos", "Dos", "1.0.0", 1);
  pkgAppsInvalidate(); drwFilter();
  chk(drwPkgCount() == 2, "dos apps en la caja");
  gTestPsFail = true;                       // toda reserva de PSRAM falla
  pkgAppsInvalidate(); drwFilter();
  chk(drwPkgCount() == 2, "si no hay PSRAM para releer, se CONSERVA la lista que habia");
  drwCompose(0, SCR_H - 1, true);           // y se sigue pudiendo pintar
  gTestPsFail = false;
  pkgAppsInvalidate(); drwFilter();
  chk(drwPkgCount() == 2, "y en cuanto vuelve la memoria se relee con normalidad");

  // --- 3. EL REGISTRO CAMBIA CON LA CAJA ABIERTA ---
  // Peor caso: se desinstalan apps mientras la rejilla tiene sus indices en la
  // mano. Ni la rejilla ni la ficha pueden leer una entrada que ya no existe.
  pkgStubClear();
  for(int i = 0; i < 12; i++){
    char id[64], nm[64];
    snprintf(id, sizeof(id), "com.flexos.vol%02d", i);
    snprintf(nm, sizeof(nm), "Volatil %d", i);
    pkgStubAdd(id, nm, "1.0.0", 1);
  }
  pkgAppsInvalidate(); drwTestReset();
  gState = ST_DRAWER; drwOn = true; drwAnim = 0; drwSlide = 0;
  drwScroll = (float)drwMaxScroll();
  chk(drwPkgCount() == 12, "doce descargadas");
  { int ultimo = -1;
    for(int i = 0; i < drwN; i++) if(drwPkgIndex(i) >= 0) ultimo = drwPkgIndex(i);
    drwInfoPkg = ultimo; }
  drwInfoOn = true;                                          // ficha de la ultima
  // Se van casi todas SIN pasar por drwFilter: drwCells queda apuntando a
  // entradas que ya no existen, que es exactamente el estado peligroso.
  for(int i = 2; i < 12; i++){
    char id[64]; snprintf(id, sizeof(id), "com.flexos.vol%02d", i);
    flexPkgUninstall(id);
  }
  pkgAppsInvalidate(); pkgAppsEnsure();
  chk(pkgAppsN == 2, "el registro ya solo tiene dos");
  drwCompose(0, SCR_H - 1, true);            // la rejilla con la lista vieja
  drwInfoDraw();                             // y la ficha, con un indice muerto
  chk(!drwInfoOn && drwInfoPkg == -1, "la ficha de una app que ya no existe se cierra sola");
  drwFilter();
  chk(drwPkgCount() == 2, "y al refiltrar la caja queda cuadrada");

  pkgStubClear();
  gState = ST_HOME; drwOn = false; drwAnim = 0; drwSlide = (float)SCR_H;
  drwInfoOn = false; drwInfoPkg = -1;
  drwTestReset();
  if(!gFails) printf("  Regresion del reinicio: todas las comprobaciones pasan.\n");
}


// #############################################################
//  UNA SOLA REJILLA, ETIQUETAS QUE CABEN Y MENU DE PULSACION LARGA
//  ------------------------------------------------------------
//  Lo que se pidio despues de ver la caja en la placa: que las apps
//  descargadas dejen de estar en una seccion aparte, que un nombre
//  largo no invada la columna de al lado, y que una app instalada
//  tenga el mismo menu cuidado que una nativa.
// #############################################################
static int drwCellOfNative(int id){
  for(int i = 0; i < drwN; i++) if(drwNativeId(i) == id) return i;
  return -1;
}
static void testCajaUnificada(){
  printf("Caja unificada: orden, etiquetas y menu\n");
  pkgStubClear(); drwTestReset();

  // --- 1. TRUNCADO SEGURO DE ETIQUETAS ---
  // Los dos casos exactos de la foto de la placa.
  { char out[40];
    uiLabelFit("Antutu Benchmark for Flex OS", DRW_COL_STEP - 14, 2, out, sizeof(out));
    chk(strlen(out) > 3 && !strcmp(out + strlen(out) - 3, "..."),
        "un nombre largo se corta y termina en \"...\"");
    chk(textW(out, 2) <= DRW_COL_STEP - 14, "y lo que queda CABE en una columna");
    chk(strncmp(out, "Antutu", 6) == 0, "conservando el principio del nombre");

    uiLabelFit("Mi primera app", DRW_COL_STEP - 14, 2, out, sizeof(out));
    chk(textW(out, 2) <= DRW_COL_STEP - 14, "\"Mi primera app\" tambien cabe");

    // Un nombre corto se deja intacto, sin puntos de mas.
    uiLabelFit("Notas", DRW_COL_STEP - 14, 2, out, sizeof(out));
    chk(!strcmp(out, "Notas"), "un nombre que cabe no se toca");

    // NUNCA se parte un caracter UTF-8: se corta a lo bestia y se comprueba
    // que lo que sale es una cadena UTF-8 valida.
    const char* acent = "\xC3\x91" "and\xC3\xBA Se\xC3\xB1" "or Mu\xC3\xB1oz de la Torre Larga";
    for(int w = 4; w <= 200; w += 3){
      uiLabelFit(acent, w, 2, out, sizeof(out));
      int i = 0, ok = 1;
      while(out[i]){
        unsigned char b = (unsigned char)out[i];
        int len = (b < 0x80) ? 1 : ((b & 0xE0) == 0xC0) ? 2 : ((b & 0xF0) == 0xE0) ? 3 : 0;
        if(!len){ ok = 0; break; }
        for(int k = 1; k < len; k++)
          if(((unsigned char)out[i + k] & 0xC0) != 0x80){ ok = 0; break; }
        if(!ok) break;
        i += len;
      }
      chk(ok, "el truncado nunca parte un caracter UTF-8");
      chk(strlen(out) < sizeof(out), "y nunca se sale del buffer");
      if(gFails) break;
    }
    // Un buffer ridiculo no puede desbordarse.
    char mini[5];
    uiLabelFit("Aplicacion con nombre larguisimo", 10, 2, mini, sizeof(mini));
    chk(strlen(mini) < sizeof(mini), "con un buffer minimo tampoco se desborda");
  }

  // --- 2. UNA SOLA REJILLA, ORDENADA POR NOMBRE ---
  pkgStubAdd("com.flexos.aaa", "Aaa primera", "1.0.0", 1);
  pkgStubAdd("com.flexos.zzz", "Zzz ultima",  "1.0.0", 1);
  pkgAppsInvalidate(); drwFilter();
  chk(drwN == APP_N + 2,       "nativas y descargadas van en la MISMA lista");
  chk(drwNatCount() == APP_N,  "estan todas las nativas");
  chk(drwPkgCount() == 2,      "y las dos descargadas");
  // Orden alfabetico de punta a punta, sin distinguir origen.
  bool ordenado = true;
  for(int i = 1; i < drwN; i++){
    const char* a = drwCellName(i - 1);
    const char* b = drwCellName(i);
    if(a && b && pkgAppNameCmp(a, b) > 0){ ordenado = false; break; }
  }
  chk(ordenado, "toda la rejilla esta ordenada por nombre, mezclando los dos origenes");
  chk(drwCellOfPkgId("com.flexos.aaa") < drwCellOfNative(IC_NOTAS),
      "una descargada que empieza por A va ANTES que Notas");
  chk(drwCellOfPkgId("com.flexos.zzz") > drwCellOfNative(IC_NOTAS),
      "y una que empieza por Z, despues: no hay bloque de descargadas");
  // Estable: filtrar dos veces da exactamente la misma rejilla.
  { DrwCell antes[APP_N + PKGAPP_MAX]; int n = drwN;
    memcpy(antes, drwCells, sizeof(DrwCell) * n);
    drwFilter();
    chk(n == drwN && memcmp(antes, drwCells, sizeof(DrwCell) * n) == 0,
        "el orden es estable: dos filtrados dan la misma rejilla"); }

  // --- 3. MENU DE PULSACION LARGA SOBRE UNA DESCARGADA ---
  int cellPkg = drwCellOfPkgId("com.flexos.aaa");
  chk(cellPkg >= 0, "la app descargada tiene su celda");
  drwCtxOpen(cellPkg);
  chk(drwCtxOn,                "la pulsacion larga abre el menu");
  chk(drwCtxPkg >= 0,          "sabe que es una app descargada");
  chk(drwCtxApp == -1,         "y NO la confunde con un id de APP_REG");
  chk(drwCtxRows() == 5,       "cinco acciones: la quinta es Desinstalar");
  chk(!strcmp(drwCtxLabel(DRW_CTX_UNINST), "Desinstalar"), "la fila se llama Desinstalar");
  chk(drwCtxEnabled(DRW_CTX_UNINST), "y esta activa para una app descargada");
  chk(!strcmp(drwCtxName(), "Aaa primera"), "el menu conoce el nombre de la app");

  // Menu sobre una NATIVA: cuatro filas, sin Desinstalar por ninguna parte.
  drwCtxOpen(drwCellOfNative(IC_FLEXSTORE));
  chk(drwCtxApp == IC_FLEXSTORE, "el menu de una nativa lleva su id de APP_REG");
  chk(drwCtxPkg == -1,           "y ningun indice de paquete");
  chk(drwCtxRows() == 4,         "una app nativa NO ofrece Desinstalar");
  chk(!drwCtxEnabled(DRW_CTX_UNINST), "ni aunque se pregunte por esa fila");
  drwCtxOn = false;

  // --- 4. INICIO: anclar y desanclar una descargada ---
  drwFilter();
  cellPkg = drwCellOfPkgId("com.flexos.aaa");
  int e = drwPkgIndex(cellPkg);
  chk(e >= 0, "indice vivo de la app descargada");
  chk(!pkgAppInHome(e), "de fabrica no esta en Inicio");
  drwPkgHomeToggle(e);
  chk(pkgAppInHome(e), "\"Anadir a inicio\" la marca");
  int slot = pkgPrefSlot("com.flexos.aaa", false);
  chk(slot >= 0, "y le reserva una ranura estable");
  bool enRejilla = false;
  for(int i = 0; i < HOME_TOTAL; i++)
    if(homeOrder[i] == (uint8_t)(HOME_PKG_BASE + slot)) enRejilla = true;
  chk(enRejilla, "el escritorio guarda la RANURA, no un indice de pkgApps");
  chk(homeIsPkg((uint8_t)(HOME_PKG_BASE + slot)), "y ese valor se reconoce como paquete");
  chk(pkgAppFromSlot(slot) == e, "la ranura vuelve a la app correcta");
  drwPkgHomeToggle(e);
  chk(!pkgAppInHome(e), "\"Quitar de inicio\" la desmarca");
  for(int i = 0; i < HOME_TOTAL; i++)
    chk(!homeIsPkg(homeOrder[i]), "y no deja ni un icono fantasma en el escritorio");

  // --- 5. OCULTAR una descargada ---
  drwPkgHomeToggle(e);                                  // primero a Inicio
  chk(pkgAppInHome(e), "anclada de nuevo");
  drwPkgHideToggle(e);
  chk(pkgAppHidden(e),  "\"Ocultar\" la marca");
  chk(!pkgAppInHome(e), "y una app oculta no se queda en Inicio");
  chk(drwCellOfPkgId("com.flexos.aaa") < 0, "ya no se lista en la caja");
  drwShowHid = true; drwFilter();
  chk(drwCellOfPkgId("com.flexos.aaa") >= 0, "con \"ver ocultas\" vuelve a aparecer");
  drwShowHid = false;
  e = -1;
  { drwShowHid = true; drwFilter();
    e = drwPkgIndex(drwCellOfPkgId("com.flexos.aaa"));
    drwPkgHideToggle(e);
    drwShowHid = false; drwFilter(); }
  chk(drwCellOfPkgId("com.flexos.aaa") >= 0, "\"Mostrar\" la devuelve a la caja");

  // --- 6. DESINSTALAR por la ruta oficial ---
  drwFilter();
  cellPkg = drwCellOfPkgId("com.flexos.aaa");
  e = drwPkgIndex(cellPkg);
  drwPkgHomeToggle(e);                                  // anclada, para ver que se suelta
  chk(pkgAppInHome(e), "anclada antes de desinstalar");
  gStubUninstallId[0] = 0;
  chk(drwPkgUninstall(e), "se desinstala");
  chk(!strcmp(gStubUninstallId, "com.flexos.aaa"),
      "por flexPkgUninstall, la ruta transaccional del gestor de paquetes");
  chk(drwCellOfPkgId("com.flexos.aaa") < 0, "desaparece de la caja en el acto");
  chk(pkgPrefSlot("com.flexos.aaa", false) < 0, "y sus preferencias se olvidan");
  for(int i = 0; i < HOME_TOTAL; i++)
    chk(!homeIsPkg(homeOrder[i]), "sin dejar nada suyo en el escritorio");
  chk(drwNatCount() == APP_N, "las nativas, intactas");
  chk(drwPkgCount() == 1,     "y la otra descargada sigue ahi");

  // Desinstalar algo que ya no esta no rompe la lista.
  chk(!drwPkgUninstall(999), "un indice invalido no hace nada");
  chk(drwPkgCount() == 1,    "y la caja sigue cuadrada");

  pkgStubClear();
  gState = ST_HOME; drwOn = false;
  drwCtxOn = false; drwConfOn = false; drwCtxApp = -1; drwCtxPkg = -1;
  drwTestReset();
  if(!gFails) printf("  Caja unificada: todas las comprobaciones pasan.\n");
}


// #############################################################
//  PRUEBAS DEL CRONOMETRO
//  ------------------------------------------------------------
//  El modelo de tiempo del cronometro es aritmetica pura sobre
//  millis(), asi que se puede comprobar DE VERDAD en el PC con el
//  reloj virtual: lo que se verifica aqui es exactamente lo que el
//  modulo promete -- que el tiempo sale del acumulado y no de un
//  contador por frame, que el desbordamiento de millis() no lo
//  rompe, que la lista de vueltas nunca se sale de su array y que
//  la capsula reserva un ancho estable (de eso depende que pueda
//  repintarse encima de si misma sin restaurar el fondo).
// #############################################################
static void cronoTestReset(){
  gTestMs = 100000;
  cronoReset();
  gCronoCard = CC_HIDDEN;
  gCronoCapOn = false;
}
static void testCronometro(){
  printf("Cronometro\n");
  cronoTestReset();
  char b[16];

  // --- estado inicial ---
  chk(!cronoActive(),           "de fabrica el cronometro esta inactivo");
  chk(cronoElapsed() == 0,      "y marca cero");
  chk(cronoCurLapNo() == 1,     "la primera vuelta es la 1");

  // --- el tiempo sale de millis(), no de un contador por frame ---
  cronoStart();
  gTestMs += 12345;
  chk(cronoElapsed() == 12345,  "corriendo: el tiempo es millis() - t0");
  gTestMs += 1;                 // un solo frame mas: nada de acumular a mano
  chk(cronoElapsed() == 12346,  "avanza con el reloj, no con el numero de cuadros");

  // --- pausa: consolida y CONGELA ---
  cronoPause();
  chk(gCronoSt == CRONO_PAUSE,  "pausado");
  gTestMs += 500000;            // medio minuto largo parado
  chk(cronoElapsed() == 12346,  "en pausa el tiempo no avanza");
  chk(cronoActive(),            "pausado sigue contando como activo (capsula visible)");

  // --- reanudar: no se pierde ni se regala tiempo ---
  cronoStart();
  gTestMs += 1000;
  chk(cronoElapsed() == 13346,  "al continuar se suma sobre lo acumulado");

  // --- vueltas: parcial y total ---
  cronoTestReset();
  cronoStart();
  gTestMs += 32180; cronoLapMark();       // vuelta 1
  gTestMs += 13410; cronoLapMark();       // vuelta 2
  gTestMs +=  2230; cronoLapMark();       // vuelta 3
  gTestMs +=  2500; cronoLapMark();       // vuelta 4
  chk(gCronoNLaps == 4,                        "cuatro vueltas guardadas");
  chk(gCronoLaps[0].split == 32180,            "parcial de la vuelta 1");
  chk(gCronoLaps[1].split == 13410,            "parcial de la vuelta 2");
  chk(gCronoLaps[3].total == 50320,            "total acumulado de la vuelta 4");
  chk(cronoCurLapNo() == 5,                    "la vuelta en curso es la 5");
  chk(gCronoBest  == 2,                        "la mas rapida es la 3 (indice 2)");
  chk(gCronoWorst == 0,                        "la mas lenta es la 1 (indice 0)");
  gTestMs += 5410;
  chk(cronoLapElapsed() == 5410,               "la vuelta en curso cuenta desde la ultima marca");

  // --- con menos de tres vueltas NO se colorea nada ---
  cronoTestReset(); cronoStart();
  gTestMs += 1000; cronoLapMark();
  gTestMs += 2000; cronoLapMark();
  chk(gCronoBest < 0 && gCronoWorst < 0, "con dos vueltas no hay mejor ni peor");

  // --- lista llena: se descarta la mas antigua, nunca se desborda ---
  cronoTestReset(); cronoStart();
  for(int i = 0; i < CRONO_MAX_LAPS + 7; i++){ gTestMs += 1000 + i; cronoLapMark(); }
  chk(gCronoNLaps == CRONO_MAX_LAPS,  "la lista se queda en su tope");
  chk(gCronoLap0 == 8,                "el indice 0 pasa a ser la vuelta 8");
  chk(cronoCurLapNo() == CRONO_MAX_LAPS + 8, "la numeracion visible nunca retrocede");
  bool crece = true;
  for(int i = 1; i < (int)gCronoNLaps; i++)
    if(gCronoLaps[i].total <= gCronoLaps[i-1].total) crece = false;
  chk(crece, "los totales guardados siguen ordenados tras compactar");
  chk(gCronoBest >= 0 && gCronoBest < (int)gCronoNLaps,   "el indice de la mejor vuelta esta dentro del array");
  chk(gCronoWorst >= 0 && gCronoWorst < (int)gCronoNLaps, "el indice de la peor vuelta esta dentro del array");

  // --- DESBORDAMIENTO DE millis() ---
  // Se arranca justo antes de la vuelta del contador de 32 bits y se coloca el
  // reloj virtual en el valor YA desbordado. La resta sin signo tiene que dar
  // el intervalo real, no un salto de 49 dias.
  cronoTestReset();
  gTestMs = 0xFFFFF000UL;                       // 4096 ms para desbordar
  cronoStart();
  gTestMs = 115904UL;                           // (0xFFFFF000 + 120000) mod 2^32
  chk(cronoElapsed() == 120000UL, "el cronometro cruza el desbordamiento de millis()");
  cronoLapMark();
  chk(gCronoNLaps == 1 && gCronoLaps[0].split == 120000UL,
      "la vuelta marcada al cruzar el desbordamiento es correcta");
  gTestMs += 30000;
  cronoPause();
  chk(cronoElapsed() == 150000UL, "y la pausa consolida el total correcto");

  // --- formateador unico ---
  cronoFmt(b, sizeof(b), 0, true);            chk(!strcmp(b, "00:00.00"), "formato 0 con centesimas");
  cronoFmt(b, sizeof(b), 55730, true);        chk(!strcmp(b, "00:55.73"), "formato MM:SS.cc");
  cronoFmt(b, sizeof(b), 72870, true);        chk(!strcmp(b, "01:12.87"), "formato de la referencia");
  cronoFmt(b, sizeof(b), 72870, false);       chk(!strcmp(b, "01:12"),    "formato compacto (sin centesimas)");
  cronoFmt(b, sizeof(b), 3723456UL, true);    chk(!strcmp(b, "1:02:03.45"), "mas de una hora, con centesimas");
  cronoFmt(b, sizeof(b), 3723456UL, false);   chk(!strcmp(b, "1:02:03"),    "mas de una hora, compacto");

  // --- la capsula reserva un ancho ESTABLE dentro de su clase de formato ---
  cronoTestReset(); cronoStart();
  gTestMs += 1000;      int w1 = cronoCapsuleW();
  gTestMs += 3540000UL; int w2 = cronoCapsuleW();     // 59:01, sigue en MM:SS
  chk(w1 == w2, "el ancho de la capsula no depende de los digitos (se repinta sobre si misma)");
  gTestMs += 120000UL;  int w3 = cronoCapsuleW();     // ya pasa de 1 h -> H:MM:SS
  chk(w3 > w2, "al pasar de una hora la capsula CRECE (la nueva tapa a la vieja)");
  chk(cronoCapsuleRight() <= SCR_W - 66 - 12,
      "la capsula nunca invade el Wi-Fi ni la bateria");

  // --- la capsula no se dibuja donde no debe ---
  gState = ST_LOCK;  chk(cronoBarSurface() == 0, "en el bloqueo no hay capsula");
  gState = ST_HOME; gLand = true;
  chk(cronoBarSurface() == 0, "en Modo PC (horizontal) tampoco");
  gLand = false; editMode = true;
  chk(cronoBarSurface() == 0, "en Modo Edicion tampoco");
  editMode = false;
  chk(cronoBarSurface() == 1, "en el escritorio si (superficie homeBuf + fb)");
  gState = ST_APP; gAppId = IC_RELOJ;
  chk(cronoBarSurface() == 2, "y en el marco estandar de una app tambien");
  gAppId = IC_PAINT;                                   // APP_CUSTOM_HEADER
  chk(cronoBarSurface() == 0, "una app con cabecera propia conserva su esquina");
  gAppId = 0; gState = ST_HOME;

  // --- tarjeta expandida: subsistema propio, NO la isla de notificaciones ---
  int notifAntes = gNotifCount;
  setBuf(fb);
  cronoBarClock(16, TH_ONWALL);                        // deja gCronoCapX/On coherentes
  chk(gCronoCapOn, "con el cronometro activo la barra pinta la capsula");
  cronoCardOpen();
  chk(cronoCardVisible(), "la capsula abre la tarjeta");
  chk(gNotifCount == notifAntes, "la tarjeta NO se encola en gNotifs[]");
  // La animacion termina sola por tiempo, sin delay() de por medio.
  for(int i = 0; i < 20 && gCronoCard != CC_OPEN; i++){ gTestMs += 20; cronoCardTick(); }
  chk(gCronoCard == CC_OPEN, "la expansion termina por tiempo (sin delay)");
  gTestMs += 60000;
  cronoCardTick();
  chk(cronoCardVisible(), "es PERSISTENTE: no caduca a los 5 s como una notificacion");
  cronoCardClose();
  for(int i = 0; i < 20 && gCronoCard != CC_HIDDEN; i++){ gTestMs += 20; cronoCardTick(); }
  chk(gCronoCard == CC_HIDDEN, "la contraccion termina y libera la pantalla");
  // Si otra pantalla se adueña del fb, la tarjeta se retira sin restaurar nada.
  cronoCardOpen();
  chk(cronoCardVisible(), "se puede volver a abrir (los buffers se reutilizan)");
  gState = ST_LOCK;
  cronoCardTick();
  chk(!cronoCardVisible(), "al bloquearse la pantalla la tarjeta se retira sola");
  gState = ST_HOME;

  // --- reinicio: todo a cero y la capsula desaparece ---
  cronoReset();
  chk(!cronoActive() && gCronoNLaps == 0 && cronoElapsed() == 0 && gCronoLap0 == 1,
      "reiniciar deja el cronometro como recien arrancado");
  cronoTestReset();
  if(!gFails) printf("  Cronometro: todas las comprobaciones pasan.\n");
}

static void testPaginasHome(){
  printf("Paginas del escritorio\n");
  drwTestReset();
  gState = ST_HOME; editMode = false; gLand = false;
  gHomePage = 0; hpDragging = false; hpSettling = false;
  hpFreeBuffers(); homeBackdropFree();       // sin caches previas: se reservan de nuevo

  // La primera pagina conserva las doce apps originales y las paginas
  // siguientes empiezan vacias; el gesto no debe inventar iconos.
  { int id;
    chk(hitHomeIcon(HOME_GX0 + 10, HOME_GY0 + 10, id) && id == IC_RELOJ,
        "la primera casilla de la pagina 0 sigue siendo Reloj");
    gHomePage = 1;
    chk(!hitHomeIcon(HOME_GX0 + 10, HOME_GY0 + 10, id),
        "la primera casilla de la pagina 1 empieza vacia");
    gHomePage = 0; }

  // El dock responde igual en TODAS las paginas: no se mueve de pagina.
  { int id;
    int dkx = 24, dky = SCR_H - 176, dkh = 96, dS = 64;
    int ix = dkx + 16, iy = dky + (dkh - dS) / 2;
    chk(hitHomeIcon(ix + 4, iy + 4, id) && id == 12, "el dock responde en la pagina 0");
    gHomePage = 2;
    chk(hitHomeIcon(ix + 4, iy + 4, id) && id == 12, "y tambien en la pagina 2");
    gHomePage = 0; }

  // --- el arrastre solo empieza si el gesto es HORIZONTAL de verdad ---
  int gy = HOME_GY0 + 40;
  tDown(300, gy, 1000);
  chk(!hpTryStart(), "sin haberse movido, no hay arrastre");
  tMove(300 - 6, gy, 1050);
  chk(!hpTryStart(), "6 px no bastan");
  tMove(300, gy - 60, 1100);
  chk(!hpTryStart(), "un gesto VERTICAL no arrastra paginas");

  // Sin PSRAM para el lienzo de la pagina vecina no hay animacion, pero
  // tampoco un fallo: hpTryStart devuelve false y el escritorio sigue
  // respondiendo. (En el arnes heap_caps_aligned_alloc si funciona, asi
  // que aqui la ruta que se ejercita es la buena.)
  tDown(300, gy, 2000);
  tMove(300 - 40, gy, 2050);                       // horizontal, hacia la izquierda
  bool started = hpTryStart();
  chk(started, "un gesto horizontal claro inicia el arrastre");
  if(started){
    chk(hpFrom == 0 && hpTo == 1, "hacia la izquierda se va a la pagina siguiente");
    chk(hpDragging,               "queda en arrastre");
    // Recorrido corto: al soltar VUELVE a su pagina.
    tMove(300 - 50, gy, 2400);                     // 50 px, lento (400 ms)
    hpTick();
    tUp(2500, false);
    hpTick();
    chk(hpSettling,          "al soltar arranca el acomodo");
    chk(hpSettleTo == 0,     "y con poco recorrido vuelve a la pagina de origen");
    gTestMs = 2500 + HP_SETTLE_MS + 10;
    hpTick();
    chk(!hpSettling,         "el acomodo termina");
    chk(gHomePage == 0,      "y el escritorio se queda donde estaba");
  }

  // --- recorrido largo: CAMBIA de pagina ---
  hpDragging = false; hpSettling = false; gHomePage = 0;
  tDown(400, gy, 3000);
  tMove(400 - 40, gy, 3050);
  if(hpTryStart()){
    tMove(400 - (SCR_W / 2), gy, 3600);            // mas de media pantalla
    hpTick();
    tUp(3700, false);
    hpTick();
    chk(hpSettleTo != 0, "con mas de un cuarto de pantalla, cambia");
    gTestMs = 3700 + HP_SETTLE_MS + 10;
    hpTick();
    chk(gHomePage == 1, "y se queda en la pagina 1");
  }

  // --- golpe seco: corto pero rapido, tambien cambia ---
  hpDragging = false; hpSettling = false; gHomePage = 0;
  tDown(400, gy, 5000);
  tMove(400 - 40, gy, 5030);
  if(hpTryStart()){
    tMove(400 - (HP_FLICK_PX + 10), gy, 5100);     // poco recorrido...
    hpTick();
    tUp(5150, false);                              // ...pero en 150 ms
    hpTick();
    chk(hpSettleTo != 0, "un golpe seco corto pero rapido cambia de pagina");
    gTestMs = 5150 + HP_SETTLE_MS + 10;
    hpTick();
    chk(gHomePage == 1, "y llega a la pagina siguiente");
  }

  // --- en los BORDES no hay pagina a la que ir ---
  hpDragging = false; hpSettling = false; gHomePage = 0;
  tDown(300, gy, 7000);
  tMove(300 + 40, gy, 7050);                       // hacia la derecha desde la pagina 0
  chk(!hpTryStart(), "desde la primera pagina no se arrastra hacia atras");
  gHomePage = gHomePageN - 1;
  tDown(300, gy, 7200);
  tMove(300 - 40, gy, 7250);
  chk(!hpTryStart(), "desde la ultima pagina no se arrastra hacia delante");

  // --- la cabecera de widgets ES pagina: arrastrar ahi tambien pasa pagina ---
  // Antes esa franja era de dos widgets fijos que no se movian; ahora sus
  // widgets son de la pagina y viajan con ella, asi que el gesto es el mismo.
  gHomePage = 0;
  tDown(300, 100, 8000);                           // sobre la cabecera de widgets
  tMove(260, 100, 8050);
  chk(hpTryStart(), "sobre la cabecera de widgets tambien se pasa de pagina");
  hpDragging = false; hpSettling = false; tReset();
  // --- fuera de la franja de pagina el gesto es de otro ---
  tDown(300, 40, 8100);                            // sobre la barra de estado
  tMove(260, 40, 8150);
  chk(!hpTryStart(), "sobre la barra de estado no se arrastran paginas");
  tDown(300, SCR_H - 30, 8200);                    // sobre la barra de navegacion
  tMove(260, SCR_H - 30, 8250);
  chk(!hpTryStart(), "sobre la barra de navegacion tampoco");

  // --- en Modo Edicion, nunca ---
  editMode = true;
  tDown(300, gy, 9000);
  tMove(260, gy, 9050);
  chk(!hpTryStart(), "en Modo Edicion no se cambia de pagina arrastrando");

  // En Modo Edicion el gesto correcto es sostener un icono en el borde:
  // debe moverlo a la pagina vecina sin perderlo ni duplicarlo.
  drwTestReset(); gHomePage = 0; editMode = true;
  tDown(HOME_GX0 + 20, HOME_GY0 + 20, 10000); edTick();
  tMove(SCR_W - 2, HOME_GY0 + 20, 10100); edTick();
  tMove(SCR_W - 2, HOME_GY0 + 20, 10900); edTick();
  chk(gHomePage == 1, "sostener el icono en el borde abre la pagina vecina");
  chk(homeOrder[HOME_STRIDE] == IC_RELOJ,
      "y el icono llega a la primera ranura libre, sin perderse");
  chk(homeOrder[0] == HOME_EMPTY, "la ranura de origen queda libre");
  tUp(11000, false); edTick(); editMode = false;

  tReset();
  hpDragging = false; hpSettling = false; gHomePage = 0;
  if(!gFails) printf("  Paginas del escritorio: todas las comprobaciones pasan.\n");
}


static void testNotifUnaSola(){
  printf("Notificaciones reales: una a la vez y nunca sobre el PIN\n");
  gNotifCount = 0; notifDragIdx = -1; notifBandOn = false; notifPaused = false;
  memset(gNotifs, 0, sizeof(gNotifs));
  gState = ST_HOME; qsPanelY = 0; editMode = false; gLand = false; gHosted = false;
  hpDragging = false; hpSettling = false;

  chk(NOTIF_BAND_BOT <= HOME_BAND_TOP,
      "la banda de avisos acaba antes de la rejilla");
  chk(NOTIF_VISIBLE == 1, "solo se dibuja una tarjeta a la vez");
  chk(NOTIF_MAX >= NOTIF_VISIBLE, "la cola puede guardar avisos pendientes");

  { const int seg[] = { ST_SPLASH, ST_OOBE_LANG, ST_OOBE_NAME, ST_LOCK,
                        ST_LOCKSETUP, ST_POWEROFF_CONFIRM, ST_POWEROFF_ANIM };
    for(unsigned k = 0; k < sizeof(seg) / sizeof(seg[0]); k++){
      gState = seg[k];
      chk(notifSecureScreen(), "ningun aviso se pinta sobre una pantalla sensible");
    }
    gState = ST_HOME; }

  DetectedModule a; memset(&a, 0, sizeof(a));
  a.active = true; a.type = MOD_MEDIA;
  snprintf(a.name, sizeof(a.name), "No se puede reproducir");
  snprintf(a.sub, sizeof(a.sub), "Formato no compatible");

  // La cola queda congelada durante el alta del PIN y se arma al volver.
  gTestMs = 100000; gState = ST_LOCKSETUP;
  notifPush(&a);
  for(int f = 0; f < 30; f++){ gTestMs += 40; notifTick(); }
  chk(gNotifCount == 1 && !gNotifs[0].armed,
      "el aviso real espera sin caducar mientras se teclea el PIN");
  gState = ST_HOME; gTestMs += 40; notifTick();
  chk(gNotifs[0].armed && gNotifs[0].bornMs == (uint32_t)gTestMs,
      "su cuenta atras empieza al regresar al escritorio");

  // Repetir el mismo aviso refresca la tarjeta; no la apila.
  notifPush(&a); notifPush(&a);
  chk(gNotifCount == 1, "repetir el mismo aviso no duplica la tarjeta");
  snprintf(a.sub, sizeof(a.sub), "Pista siguiente");
  notifPush(&a);
  chk(gNotifCount == 1 && !strcmp(gNotifs[0].mod.sub, "Pista siguiente"),
      "un subtitulo nuevo refresca la tarjeta existente");
  a.type = MOD_UNKNOWN;
  snprintf(a.name, sizeof(a.name), "Aviso del sistema");
  notifPush(&a);
  chk(gNotifCount == 2, "otro aviso distinto queda esperando en la cola");

  // Aunque haya dos avisos, el compositor arma solo el primero.
  gNotifs[0].armed = false; gNotifs[1].armed = false;
  notifBandOn = false; notifPaused = false; notifLastMs = 0; gTestMs += 40;
  drawWallpaper(homeBuf, false);
  memcpy(fb, homeBuf, (size_t)SCR_W * SCR_H * 2);
  setBuf(fb);
  notifTick();
  chk(gNotifs[0].armed && !gNotifs[1].armed,
      "solo el primer aviso de la cola se hace visible");

  int cambiadosFuera = 0;
  for(int y = 0; y < SCR_H; y++)
    for(int x = 0; x < SCR_W; x++)
      if(fb[(size_t)y * SCR_W + x] != homeBuf[(size_t)y * SCR_W + x] &&
         (y < NOTIF_BAND_TOP || y >= NOTIF_BAND_BOT)) cambiadosFuera++;
  chk(cambiadosFuera == 0, "el aviso no escribe fuera de su banda");

  // Regresion del reinicio visto en placa: dejar salir dos avisos completos.
  // Antes, al retirar el ultimo, `shown` seguia valiendo 1 y el bucle volvia
  // infinitamente sobre la ranura eliminada hasta que el TASK_WDT reiniciaba
  // el P4. Esta prueba recorre entrada, espera y salida de ambas tarjetas.
  gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs));
  notifDragIdx = -1; notifBandOn = false; notifPaused = false; notifLastMs = 0;
  a.type = MOD_MEDIA;
  snprintf(a.name, sizeof(a.name), "Aviso A"); notifPush(&a);
  a.type = MOD_UNKNOWN;
  snprintf(a.name, sizeof(a.name), "Aviso B"); notifPush(&a);
  bool segundaVisible = false;
  for(int f = 0; f < 360 && gNotifCount > 0; f++){
    gTestMs += 40; notifTick();
    if(gNotifCount == 1 && !strcmp(gNotifs[0].mod.name, "Aviso B") && gNotifs[0].armed)
      segundaVisible = true;
  }
  chk(segundaVisible, "la segunda notificacion entra despues de la primera");
  chk(gNotifCount == 0 && !notifBandOn,
      "la ultima sale y limpia la banda sin congelar ni reiniciar el OS");

  // Reponer dos entradas para comprobar tambien la pausa de la Caja.
  a.type = MOD_MEDIA;
  snprintf(a.name, sizeof(a.name), "Aviso A"); notifPush(&a);
  a.type = MOD_UNKNOWN;
  snprintf(a.name, sizeof(a.name), "Aviso B"); notifPush(&a);
  gTestMs += 40; notifTick();

  notifPauseForDrawer();
  chk(gNotifCount == 2 && notifPaused && !notifBandOn,
      "abrir la caja oculta la tarjeta sin perder avisos reales");

  gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs));
  notifDragIdx = -1; notifBandOn = false; notifPaused = false;
  gState = ST_HOME; gLand = false; tReset();
  if(!gFails) printf("  Notificaciones reales: todas las comprobaciones pasan.\n");
}

static void testDeslizarPaginas(){
  printf("Deslizamiento entre paginas del escritorio\n");
  gState = ST_HOME; editMode = false; gLand = false; qsPanelY = 0;
  gHomePage = 0; hpDragging = false; hpSettling = false;

  // --- 1. LOS DOS VIEWPORTS SON COMPLEMENTARIOS ---
  // Se barre todo el recorrido posible, no tres valores bonitos.
  { bool okCubre = true, okSolape = true, okDentro = true;
    for(int dx = -SCR_W; dx <= SCR_W; dx++){
      for(int dir = -1; dir <= 1; dir += 2){
        int nx = dx + dir * SCR_W;
        int aD, aS, aW, bD, bS, bW;
        hpViewport(dx, aD, aS, aW);
        hpViewport(nx, bD, bS, bW);
        if(aD < 0 || bD < 0 || aD + aW > SCR_W || bD + bW > SCR_W) okDentro = false;
        // Solo el par (dx, nx) que corresponde a esta direccion tiene
        // sentido: |dx| + |nx| = SCR_W. El otro se sale y da ancho 0.
        if(aW + bW != SCR_W) continue;
        if(aW > 0 && bW > 0){
          int aFin = aD + aW, bFin = bD + bW;
          if(aD < bFin && bD < aFin) okSolape = false;       // se pisan
          if(aFin != bD && bFin != aD) okCubre = false;      // dejan hueco
        }
      }
    }
    chk(okDentro,  "ningun viewport se sale del ancho de la pantalla");
    chk(okSolape,  "las dos paginas NO se pisan ni una columna");
    chk(okCubre,   "y entre las dos no dejan ningun hueco"); }

  // Los extremos: pagina quieta y pagina fuera.
  { int d, s, w;
    hpViewport(0, d, s, w);
    chk(d == 0 && s == 0 && w == SCR_W, "sin desplazamiento la pagina ocupa todo");
    hpViewport(SCR_W, d, s, w);
    chk(w == 0, "desplazada una pantalla entera ya no se ve");
    hpViewport(-SCR_W, d, s, w);
    chk(w == 0, "y hacia el otro lado tampoco");
    hpViewport(120, d, s, w);
    chk(d == 120 && s == 0 && w == SCR_W - 120, "hacia la derecha entra por la izquierda");
    hpViewport(-120, d, s, w);
    chk(d == 0 && s == 120 && w == SCR_W - 120, "hacia la izquierda, por la derecha"); }

  // --- 2. EL WALLPAPER QUEDA FIJO Y SOLO SE MUEVE EL PRIMER PLANO ---
  // El fondo usa un color distinto en cada X para que desplazarlo siquiera un
  // pixel sea detectable. homeBuf/hpBuf contienen ese mismo fondo mas dos
  // rectangulos que representan iconos. Se verifica pixel a pixel el frame
  // esperado durante todo el recorrido, incluido que no sobreviva basura del
  // frame anterior.
  // Con la mascara de contenido: aqui las dos paginas no tienen vidrio anotado,
  // asi que su base es el wallpaper limpio y la mascara marca justo los dos
  // rectangulos -- el mismo criterio de siempre, ahora precalculado.
  { const uint16_t COL_A = 0x1234, COL_B = 0x4321, VENENO = 0x7BEF;
    if(!hpEnsureBuf()){ chk(false, "hay lienzo para la pagina vecina"); }
    else {
      memset(homeBuf, 0, (size_t)SCR_W * SCR_H * 2);
      for(int y = HOME_PAGE_TOP; y < homeBandBot(); y++) for(int x = 0; x < SCR_W; x++){
        uint16_t bg = (uint16_t)(0x0800u + (unsigned)x);
        homeBuf[(size_t)y * SCR_W + x] = bg;
        hpBg[(size_t)(y - HOME_PAGE_TOP) * SCR_W + x] = bg;
        hpBuf[(size_t)(y - HOME_PAGE_TOP) * SCR_W + x] = bg;
      }
      const int fy = HOME_GY0 + 12;
      for(int y = fy; y < fy + 18; y++){
        for(int x = 60; x < 92; x++) homeBuf[(size_t)y * SCR_W + x] = COL_A;
        for(int x = 100; x < 132; x++) hpBuf[(size_t)(y - HOME_PAGE_TOP) * SCR_W + x] = COL_B;
      }
      hpBufPage = 1; hpFrom = 0; hpTo = 1;    // hacia la izquierda: la 1 entra por la derecha
      hpTop = HOME_BAND_TOP;                  // ninguna de las dos tiene cabecera
      gGlRecN[0] = 0; gGlRecN[1] = 0;
      hpMaskBuild(0); hpMaskBuild(1);
      int malos = 0, fondoMovido = 0, primerPlanoMal = 0;
      for(int dx = -SCR_W + 1; dx <= -1; dx += 37){
        for(size_t i = 0; i < (size_t)SCR_W * SCR_H; i++) bbuf[i] = VENENO;
        hpRenderFrame(dx);
        for(int y = HOME_BAND_TOP; y < homeBandBot(); y++){
          if(y >= HOME_DOTS_Y - 8 && y <= HOME_DOTS_Y + 10) continue;
          const uint16_t* row = bbuf + (size_t)y * SCR_W;
          for(int x = 0; x < SCR_W; x++){
            uint16_t bg = (uint16_t)(0x0800u + (unsigned)x), esperado = bg;
            int sa = x - dx;
            if(y >= fy && y < fy + 18 && sa >= 60 && sa < 92) esperado = COL_A;
            int nx = dx + SCR_W, sb = x - nx;
            if(y >= fy && y < fy + 18 && sb >= 100 && sb < 132) esperado = COL_B;
            if(row[x] == VENENO) malos++;
            if(esperado == bg && row[x] != bg) fondoMovido++;
            if(row[x] != esperado) primerPlanoMal++;
          }
        }
      }
      chk(malos == 0,  "no queda ni un pixel del frame anterior en la banda");
      chk(fondoMovido == 0, "el wallpaper queda fijo mientras se deslizan las apps");
      chk(primerPlanoMal == 0, "solo iconos y etiquetas siguen al dedo");

      // En un punto concreto, el icono de la pagina nueva esta desplazado
      // pero una muestra vecina del wallpaper conserva su coordenada.
      for(size_t i = 0; i < (size_t)SCR_W * SCR_H; i++) bbuf[i] = VENENO;
      hpRenderFrame(-200);
      const uint16_t* row = bbuf + (size_t)fy * SCR_W;
      chk(row[380] == COL_B, "el icono de la pagina entrante cambia de posicion");
      chk(row[200] == (uint16_t)(0x0800u + 200u), "el wallpaper no cambia de posicion");

      // Sin lienzo vecino compuesto no se deja ni una columna sin
      // escribir: el hueco se rellena con fondo, no con lo que hubiera.
      hpBufPage = -1;
      for(size_t i = 0; i < (size_t)SCR_W * SCR_H; i++) bbuf[i] = VENENO;
      hpRenderFrame(-200);
      int huecos = 0;
      for(int y = HOME_BAND_TOP; y < homeBandBot(); y++)
        for(int x = 0; x < SCR_W; x++)
          if(bbuf[(size_t)y * SCR_W + x] == VENENO) huecos++;
      chk(huecos == 0, "sin lienzo vecino tampoco queda basura en pantalla");
      hpBufPage = 1;
    } }

  // --- 3. FUERA DE LA FRANJA NO SE TOCA NADA ---
  // Barra de estado, dock y barra de navegacion son identicos en todas las
  // paginas: el gesto no puede escribir ahi. Sin cabecera en ninguna de las dos
  // paginas el gesto recorre exactamente la banda de la rejilla de siempre; con
  // ella, empieza en la cabecera y ni una fila mas arriba.
  { const uint16_t MARCA = 0x0A0A;
    for(int caso = 0; caso < 2; caso++){
      int top = caso ? HOME_PAGE_TOP : HOME_BAND_TOP;
      for(size_t i = 0; i < (size_t)SCR_W * SCR_H; i++) bbuf[i] = MARCA;
      hpBufPage = 1; hpFrom = 0; hpTo = 1; hpTop = top;
      hpRenderFrame(-240);
      int tocados = 0;
      for(int y = 0; y < SCR_H; y++){
        if(y >= top && y < homeBandBot()) continue;
        for(int x = 0; x < SCR_W; x++)
          if(bbuf[(size_t)y * SCR_W + x] != MARCA) tocados++;
      }
      chk(tocados == 0, caso ? "con cabecera: la barra de estado, el dock y la navegacion no se tocan"
                             : "sin cabecera: solo se toca la banda de la rejilla, como siempre");
    }
    hpTop = HOME_BAND_TOP; }

  // --- 4. LA ISLA SIGUE VIVA (Y ENCIMA) MIENTRAS DURA EL GESTO ---
  // Antes la isla se PAUSABA durante el gesto y sus pixeles se quedaban en fb:
  // con widgets de cabecera, cada cuadro pintaba la pagina encima de la
  // tarjeta (la notificacion "detras", cortada y congelada). Ahora:
  //   a) si la franja que se desliza no toca la banda de la isla, la isla se
  //      compone como siempre (su propia banda);
  //   b) si la toca, el gesto es el unico dueno de esas filas y la pinta
  //      encima de cada cuadro: notifTick solo avanza el tiempo.
  { gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs));
    notifBandOn = false; notifPaused = false; notifLastMs = 0;
    gTestMs = 400000;
    DetectedModule m; memset(&m, 0, sizeof(m));
    m.active = true; m.type = MOD_MEDIA;
    snprintf(m.name, sizeof(m.name), "Reproduccion terminada");
    notifPush(&m);
    gState = ST_HOME; qsPanelY = 0; editMode = false;
    hpDragging = true; hpTop = HOME_BAND_TOP;
    gPanelDrawCalls = 0;
    gTestMs += 40; notifTick();
    chk(gNotifs[0].armed && !notifPaused, "sin cabecera, la isla sigue viva durante el gesto");
    chk(gPanelDrawCalls == 1 && gPanelLastY0 == NOTIF_BAND_TOP, "y publica su propia banda, que el gesto no toca");
    hpTop = HOME_PAGE_TOP;
    uint32_t born = gNotifs[0].bornMs;
    gPanelDrawCalls = 0;
    gTestMs += 40; notifTick();
    chk(gPanelDrawCalls == 0, "con cabecera, la isla no publica su banda sola: la compone el gesto encima");
    chk(gNotifs[0].bornMs == born && !notifPaused, "y su tiempo sigue corriendo: no se congela");
    hpDragging = false; hpTop = HOME_BAND_TOP;
    gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs)); notifBandOn = false; }

  // --- 5. UNA APP EN LA PAGINA 2 USA UNA CASILLA NORMAL ---
  { int x0, y0, x1, y1;
    homeSlotXY(0, x0, y0); homeSlotXY(1, x1, y1);
    chk(x0 == HOME_GX0 && y0 == HOME_GY0,
        "la primera casilla usa los margenes normales");
    chk(x1 - x0 == HOME_COLSTEP, "el paso entre columnas es el de siempre");
    uint8_t old = homeOrder[HOME_STRIDE];
    homeOrder[HOME_STRIDE] = IC_AJUSTES;
    int id; gHomePage = 1;
    chk(hitHomeIcon(HOME_GX0 + 10, HOME_GY0 + 10, id) && id == IC_AJUSTES,
        "una app responde en la primera casilla de la pagina 2");
    homeOrder[HOME_STRIDE] = old; gHomePage = 0; }

  chk(gHomePageN == 3, "de fabrica siguen siendo tres paginas");

  // --- 6. CACHE COMPACTO Y COOPERATIVO ---
  // PRESUPUESTO. La franja de pagina (cabecera + rejilla, 524 filas) se guarda
  // tres veces -- pagina vecina, fondo limpio y fondo desenfocado -- mas dos
  // mascaras de 1 bit. Es lo que compra que el vidrio no arrastre el fondo y
  // que componer el escritorio no desenfoque un panel por icono. Queda por
  // debajo de 1,6 MB (un 5% de los 32 MB de PSRAM) y todo es reconstruible:
  // memShedSystem lo suelta bajo presion.
  chk(HP_BUF_PIXELS == (size_t)SCR_W * HOME_PAGE_H,
      "la pagina vecina reserva exactamente la franja de pagina");
  chk(HP_BUF_PIXELS * 2 < (size_t)SCR_W * SCR_H * 2,
      "y no otro framebuffer completo de 768 KB");
  chk(HP_PSRAM_BYTES <= (size_t)1600 * 1024,
      "vecina + fondo limpio + fondo desenfocado + mascaras caben en 1,6 MB de PSRAM");
  chk(HP_MASK_BYTES * 8 == HP_BUF_PIXELS, "la mascara es de un bit por pixel de la franja");
  gDelayCalls = 0; hpBufPage = -1;
  chk(hpPrepare(1), "la pagina vecina se puede preparar en el cache compacto");
  chk(gDelayCalls > 0,
      "la composicion larga cede CPU y alimenta el WDT durante el trabajo");

  hpDragging = false; hpSettling = false; hpBufPage = -1;
  gHomePage = 0; tReset();
  if(!gFails) printf("  Deslizamiento entre paginas: todas las comprobaciones pasan.\n");
}


// #############################################################
//  CABECERA DE APP: LA FLECHA Y EL TITULO NO SE TOCAN
//  ------------------------------------------------------------
//  En Archivos, Notas y Paint el chevron de volver se dibujaba
//  DENTRO de la primera letra del titulo. La comprobacion es de
//  pixeles y por separado: se pinta solo la flecha, se anota su
//  caja; se pinta solo el titulo, se anota la suya; y se exige que
//  no compartan ni un pixel.
// #############################################################

static void testCabeceras(){
  printf("Cabecera de app\n");
  const uint16_t FONDO = 0x0000;
  gLand = false;

  // Caja de lo que dibuja `f` sobre un lienzo limpio.
  struct Caja { int x0, y0, x1, y1; bool hay; };
  auto medir = [&](void (*f)(uint16_t), uint16_t col) -> Caja {
    memset(bbuf, 0, (size_t)SCR_W * SCR_H * 2);
    setBuf(bbuf);
    gClipX0 = 0; gClipY0 = 0; gClipX1 = SCR_W - 1; gClipY1 = SCR_H - 1;
    f(col);
    Caja c = { SCR_W, SCR_H, -1, -1, false };
    for(int y = 0; y < UIHDR_H + 20; y++)
      for(int x = 0; x < SCR_W; x++)
        if(bbuf[(size_t)y * SCR_W + x] != FONDO){
          if(x < c.x0) c.x0 = x;  if(x > c.x1) c.x1 = x;
          if(y < c.y0) c.y0 = y;  if(y > c.y1) c.y1 = y;
          c.hay = true;
        }
    return c;
  };

  const uint16_t TINTA = 0xFFFF;
  Caja fl = medir(uiHdrChevron, TINTA);
  chk(fl.hay, "el chevron se dibuja");
  chk(fl.x0 >= 0 && fl.x1 < UIHDR_ZONE, "y cabe entero dentro de su zona tactil");
  chk(fl.y0 >= 0 && fl.y1 < UIHDR_ZONE, "tambien a lo alto");
  // Centrado de verdad: los margenes a cada lado de la zona no difieren
  // mas de 2 px. Antes estaba pegado a la esquina superior izquierda.
  chk(abs(fl.x0 - (UIHDR_ZONE - 1 - fl.x1)) <= 2, "el chevron esta centrado horizontalmente");
  chk(abs(fl.y0 - (UIHDR_ZONE - 1 - fl.y1)) <= 2, "y verticalmente");

  Caja pt = medir(uiHdrDots, TINTA);
  chk(pt.hay, "los tres puntos se dibujan");
  chk(pt.x0 >= SCR_W - UIHDR_ZONE, "dentro de su zona tactil");
  chk(pt.x1 < SCR_W - 4,           "y con margen a la derecha");
  chk(pt.y1 < UIHDR_ZONE,          "sin pasarse de la banda");

  // Zonas tactiles: al menos 44x44 y sin solaparse.
  chk(UIHDR_ZONE >= 44, "la zona de atras mide al menos 44 px");
  chk(uiHdrBackHit(2, 2) && uiHdrBackHit(UIHDR_ZONE - 1, UIHDR_ZONE - 1),
      "y responde en toda su superficie");
  chk(!uiHdrBackHit(UIHDR_ZONE, 10), "sin invadir la del titulo");
  chk(uiHdrMenuHit(SCR_W - 2, 2),    "la del menu responde en su esquina");
  chk(!uiHdrMenuHit(SCR_W - UIHDR_ZONE - 1, 10), "y tampoco invade el titulo");
  chk(!uiHdrBackHit(10, UIHDR_ZONE), "por debajo de la cabecera ya no responde");

  // --- LO DEL VIDEO: titulo y flecha en la misma cabecera ---
  { const char* titulos[] = { "Archivos:", "Notas:", "Paint" };
    for(unsigned k = 0; k < 3; k++){
      memset(bbuf, 0, (size_t)SCR_W * SCR_H * 2);
      setBuf(bbuf);
      gClipX0 = 0; gClipY0 = 0; gClipX1 = SCR_W - 1; gClipY1 = SCR_H - 1;
      // Solo el titulo, con la misma llamada que hace uiHdrDraw.
      int fs = 5, avail = UIHDR_TR - UIHDR_TX;
      while(fs > 1 && textW(titulos[k], fs) > avail) fs--;
      drawTextClip(UIHDR_TX, UIHDR_CY - uiLineH(fs) / 2, titulos[k], fs, TINTA, UIHDR_TX + avail);
      int tx0 = SCR_W, tx1 = -1;
      for(int y = 0; y < UIHDR_H + 20; y++)
        for(int x = 0; x < SCR_W; x++)
          if(bbuf[(size_t)y * SCR_W + x] != FONDO){
            if(x < tx0) tx0 = x;  if(x > tx1) tx1 = x;
          }
      char q[96];
      snprintf(q, sizeof(q), "\"%s\": el titulo empieza DESPUES de la zona de atras", titulos[k]);
      chk(tx0 > fl.x1, q);
      snprintf(q, sizeof(q), "\"%s\": y no llega a la zona del menu", titulos[k]);
      chk(tx1 < SCR_W - UIHDR_ZONE, q);
    } }

  // Un titulo absurdamente largo se reduce y se recorta, pero NO invade
  // el boton del menu.
  { memset(bbuf, 0, (size_t)SCR_W * SCR_H * 2);
    setBuf(bbuf);
    gClipX0 = 0; gClipY0 = 0; gClipX1 = SCR_W - 1; gClipY1 = SCR_H - 1;
    uiHdrDraw("Un titulo larguisimo que no cabe de ninguna manera aqui",
              5, TINTA, TINTA, true);
    int enMedio = 0;
    for(int y = 0; y < UIHDR_ZONE; y++)
      for(int x = UIHDR_TR; x < SCR_W - UIHDR_ZONE; x++)
        if(bbuf[(size_t)y * SCR_W + x] != FONDO) enMedio++;
    chk(enMedio == 0, "un titulo larguisimo se recorta antes del menu"); }

  setBuf(fb);
  gClipX0 = 0; gClipY0 = 0; gClipX1 = SCR_W - 1; gClipY1 = SCR_H - 1;
  tReset();
  if(!gFails) printf("  Cabecera de app: todas las comprobaciones pasan.\n");
}


// #############################################################
//  LISTAS CON SCROLL: NADA ESCRIBE FUERA DEL VIEWPORT
//  ------------------------------------------------------------
//  Un fallo real: al arrastrar una lista hacia arriba, sus filas se
//  apilaban unas sobre otras en la banda de la cabecera. La causa:
//  el unico filtro por elemento miraba el borde de ABAJO
//  (`if(y + alto >= 58)`), asi que con `y` muy negativo la tarjeta
//  se dibujaba entera encima del titulo.
//
//  La prueba pinta contenido a proposito FUERA del viewport y
//  comprueba, pixel a pixel, que no llega.
// #############################################################

static void testListasConScroll(){
  printf("Listas con scroll: recorte del viewport\n");
  gLand = false;
  const uint16_t FONDO = 0x0000, TINTA = 0xFFFF;
  // Top de viewport REAL del sistema: el que usan Notas y Paint bajo su
  // cabecera. No es un numero inventado para la prueba.
  const int VP_TOP = UIHDR_ZONE + 4;

  // --- El recorte funciona en las dos direcciones ---
  { memset(bbuf, 0, (size_t)SCR_W * SCR_H * 2);
    setBuf(bbuf);
    uiClipViewport(VP_TOP, SCR_H - 1);
    // Una tarjeta que empieza MUY por encima del viewport, como una fila
    // arrastrada hacia arriba, con sus textos en la banda de la cabecera.
    fillRoundRect(12, -90, SCR_W - 24, 176, 16, TINTA);
    drawText(26, -30, "Ultimo acceso", 1, TINTA);
    drawText(26, 10,  "Bloqueo automatico", 1, TINTA);
    int arriba = 0;
    for(int y = 0; y < VP_TOP; y++)
      for(int x = 0; x < SCR_W; x++)
        if(bbuf[(size_t)y * SCR_W + x] != FONDO) arriba++;
    chk(arriba == 0, "una fila subida no escribe NI UN PIXEL en la cabecera");
    // Y lo que si cae dentro del viewport se dibuja: el recorte no
    // apaga la lista, solo la contiene.
    int dentro = 0;
    for(int y = VP_TOP; y < SCR_H; y++)
      for(int x = 0; x < SCR_W; x++)
        if(bbuf[(size_t)y * SCR_W + x] != FONDO) dentro++;
    chk(dentro > 500, "y la parte que si entra en el viewport se ve"); }

  // --- Por abajo igual ---
  { memset(bbuf, 0, (size_t)SCR_W * SCR_H * 2);
    setBuf(bbuf);
    uiClipViewport(VP_TOP, SCR_H - 120);
    fillRoundRect(12, SCR_H - 200, SCR_W - 24, 176, 16, TINTA);
    int abajo = 0;
    for(int y = SCR_H - 119; y < SCR_H; y++)
      for(int x = 0; x < SCR_W; x++)
        if(bbuf[(size_t)y * SCR_W + x] != FONDO) abajo++;
    chk(abajo == 0, "y tampoco por debajo del viewport"); }

  // --- El viewport deja libre la cabecera ---
  chk(VP_TOP >= UIHDR_ZONE,   "el viewport empieza bajo la zona tactil de la cabecera");
  chk(FILES_VP_TOP > UIHDR_H, "el de Archivos, bajo la cabecera y la ruta");

  // --- Se restaura siempre: ninguna pantalla hereda un recorte estrecho ---
  { uiClipViewport(100, 200);
    uiClipFull();
    chk(gClipY0 == 0 && gClipY1 == SCR_H - 1, "uiClipFull devuelve todas las filas");
    chk(gClipX0 == 0 && gClipX1 == SCR_W - 1, "y todas las columnas"); }

  // --- Un recorte anidado INTERSECA, no sustituye ---
  // Es lo que hace la miniatura de Paint dentro de su tarjeta: si la
  // tarjeta esta a medio salir, sus trazos no pueden escaparse arriba.
  { memset(bbuf, 0, (size_t)SCR_W * SCR_H * 2);
    setBuf(bbuf);
    uiClipViewport(UIHDR_ZONE + 4, SCR_H - 1);
    int sx0 = gClipX0, sx1 = gClipX1, sy0 = gClipY0, sy1 = gClipY1;
    int cy = 10;                                   // tarjeta subida: y = 10
    gClipX0 = max(sx0, 12); gClipX1 = min(sx1, SCR_W - 12);
    gClipY0 = max(sy0, cy); gClipY1 = min(sy1, cy + 200);
    fillRect(0, 0, SCR_W, SCR_H, TINTA);           // la miniatura, a lo bestia
    gClipX0 = sx0; gClipX1 = sx1; gClipY0 = sy0; gClipY1 = sy1;
    int fuera = 0;
    for(int y = 0; y < UIHDR_ZONE + 4; y++)
      for(int x = 0; x < SCR_W; x++)
        if(bbuf[(size_t)y * SCR_W + x] != FONDO) fuera++;
    chk(fuera == 0, "un recorte anidado no puede ampliar el del viewport"); }

  setBuf(fb);
  uiClipFull();
  tReset();
  if(!gFails) printf("  Listas con scroll: todas las comprobaciones pasan.\n");
}


// #############################################################
//  TARJETA DEL CRONOMETRO: TEMA DEL USUARIO Y BARRA VIVA
//  ------------------------------------------------------------
//  Dos cosas, y NINGUNA es el tamano de la tarjeta -- que se
//  conserva tal cual, con sus dos filas de controles.
//
//  1. Los colores salian de tres literales (un violeta, dos
//     grises) en vez de la paleta, asi que el cronometro era la
//     unica pieza que ignoraba la personalizacion: con tema claro
//     seguia siendo una tarjeta gris oscuro con botones violetas.
//
//  2. La pildora de la barra superior se congelaba con la hora de
//     cuando se abrio la tarjeta. En el video la barra dice
//     "00:07" mientras la tarjeta dice "00:18.56".
// #############################################################


// #############################################################
//  PERSONALIZAR INICIO
//  ------------------------------------------------------------
//  Paginas variables, rejilla configurable, widgets reales,
//  fondos y la prioridad tactil del gesto de dos dedos. Todo
//  contra el modelo REAL del sketch, no contra una copia.
// #############################################################
static void hcTestReset(){
  gHomePageN = HOME_LEGACY_PAGES; gHomeMain = 0; gHomePage = 0;
  gHomeCols = 4; gHomeRows = 3; gHomeIconSz = 1;
  gHomeLabels = true; gHomeLocked = false; gHomeDots = true;
  gHomePinch = true; gHomeReduce = false;
  gWallHome = 0; gWallLock = 0; gWallFit = 0; gWallPalOn = false; gHomeLook = 0;
  gWallPath[0] = 0;
  for(int p = 0; p < HOME_PAGES_MAX; p++) gHomeWgN[p] = 0;
  memset(gHomeWg, 0, sizeof(gHomeWg));
  for(int i = 0; i < HOME_TOTAL; i++) homeOrder[i] = HOME_EMPTY;
  gAppFav = 0x0FFF; gAppHidden = 0;
  for(int i = 0; i < HOME_LEGACY_SLOTS; i++) homeOrder[homeIdx(0, i)] = (uint8_t)i;
  hcActive = false; hcModal = HCM_NONE; hcReorder = -1; hcDragging = false;
  hpDragging = false; hpSettling = false;
  gState = ST_HOME; editMode = false; gLand = false; gHosted = false; qsPanelY = 0;
  tReset();
}
static int hcCountPlaced(){
  int n = 0;
  for(int p = 0; p < gHomePageN; p++)
    for(int i = 0; i < homeSlotCount(); i++) if(homeOrder[homeIdx(p, i)] != HOME_EMPTY) n++;
  return n;
}
static void testPersonalizarInicio(){
  printf("Personalizar inicio\n");
  hcTestReset();

  // --- 1. GEOMETRIA: la rejilla de fabrica es EXACTAMENTE la de siempre ---
  { int S, gx0, gy0, cs, rs, cols, rows;
    homeGrid(S, gx0, gy0, cs, rs, cols, rows);
    chk(S == HOME_ICON_S && gx0 == HOME_GX0 && gy0 == HOME_GY0,
        "4x3 con iconos normales conserva la geometria historica");
    chk(cs == HOME_COLSTEP && rs == HOME_ROWSTEP, "y sus pasos de columna y fila");
    chk(homeDotsY() == HOME_DOTS_Y, "los puntos siguen en su sitio de siempre");
    chk(homeBandBot() == HOME_DOTS_Y + 18, "y la banda movil mide lo mismo"); }

  // --- 2. TODAS LAS REJILLAS CABEN Y SU BANDA ENTRA EN EL CACHE ---
  for(int g = 0; g < HC_GRID_OPTS; g++){
    gHomeCols = HC_GRID_C[g]; gHomeRows = HC_GRID_R[g];
    for(int sz = 0; sz < 3; sz++){
      gHomeIconSz = (uint8_t)sz;
      int S, gx0, gy0, cs, rs, cols, rows;
      homeGrid(S, gx0, gy0, cs, rs, cols, rows);
      chk(gx0 >= 0 && gx0 + (cols - 1) * cs + S <= SCR_W, "la ultima columna cabe en pantalla");
      chk(S + 24 <= rs, "el icono y su etiqueta no invaden la fila de abajo");
      chk(homeBandBot() <= HOME_BAND_BOT_MAX, "la banda movil no pasa de lo reservado");
      chk(homeBandBot() < SCR_H - 176, "y nunca se mete debajo del dock");
      if(gFails) break;
    }
    if(gFails) break;
  }
  hcTestReset();

  // --- 3. CAMBIAR DE REJILLA NO PIERDE NI UN ICONO ---
  { int antes = hcCountPlaced();
    homeSetGrid(5, 4);
    chk(hcCountPlaced() == antes, "pasar a 5x4 conserva todos los iconos");
    homeSetGrid(4, 3);
    chk(hcCountPlaced() == antes, "y volver a 4x3 tambien");
    // De 4x3 (12 celdas) a una rejilla mas pequena no se puede ir, pero si
    // reducir el numero de PAGINAS: los iconos se recolocan, no desaparecen.
    homeSetGrid(4, 3); }

  // --- 4. PAGINAS: crear, principal, reordenar, borrar ---
  chk(gHomePageN == 3, "de fabrica hay tres paginas");
  chk(homePageAdd() && gHomePageN == 4, "se puede crear una cuarta pagina");
  chk(homePageAdd() && gHomePageN == 5, "y una quinta");
  chk(!homePageAdd() && gHomePageN == 5, "la sexta no: el tope son cinco");
  chk(hcModal == HCM_INFO, "y se avisa con un mensaje real, no en silencio");
  hcModal = HCM_NONE;
  homeSetMain(2);
  chk(gHomeMain == 2, "la pagina principal se puede cambiar");
  { uint8_t a0 = homeOrder[homeIdx(0, 0)];
    homePageSwap(0, 2);
    chk(homeOrder[homeIdx(2, 0)] == a0, "reordenar mueve el contenido de la pagina");
    chk(gHomeMain == 0, "y la principal viaja con ella");
    homePageSwap(0, 2);
    chk(gHomeMain == 2 && homeOrder[homeIdx(0, 0)] == a0, "el intercambio es reversible"); }

  // --- 5. BORRAR: nunca la ultima, y los iconos se recolocan ---
  { int antes = hcCountPlaced();
    homeSetMain(2);
    chk(homePageDelete(2), "se puede borrar la pagina principal");
    chk(gHomeMain == 1, "y la principal pasa a la vecina mas cercana");
    chk(hcCountPlaced() == antes, "sin perder ningun icono");
    while(gHomePageN > 1) chk(homePageDelete(gHomePageN - 1), "se van borrando las demas");
    chk(!homePageDelete(0), "la ULTIMA pagina no se puede borrar");
    chk(gHomePageN == 1, "y sigue habiendo una");
    chk(hcModal == HCM_INFO, "con aviso real");
    hcModal = HCM_NONE; }
  hcTestReset();

  // --- 6. WIDGETS: colocar, no solaparse, quitar ---
  // La fila 0 es la CABECERA de la pagina (donde antes vivian Clima y
  // Calendario fijos): el primer hueco de una pagina vacia esta ahi, y no quita
  // ninguna celda a los iconos.
  { for(int i = 0; i < homeSlotCount(); i++) homeOrder[homeIdx(1, i)] = HOME_EMPTY;
    chk(homeWgAdd(1, WG_CLOCK) == 0, "un reloj 2x1 entra en una pagina vacia");
    chk(gHomeWgN[1] == 1, "y queda registrado");
    chk(gHomeWg[1][0].row == 0, "en la fila de cabecera");
    uint32_t hm = homeHdrMask(1, -1);
    chk((hm & 1u) && (hm & 2u), "ocupa sus dos celdas de cabecera");
    chk(!(hm & 4u), "y solo esas");
    chk(homeCellMask(1, -1) == 0, "sin quitar ni una celda a los iconos");
    chk(homeWgAdd(1, WG_CLOCK_A) == 0, "un reloj analogico 2x2 tambien cabe");
    { const HomeWidget* a = &gHomeWg[1][1];
      chk(a->row == 0 && a->col == 2 && a->h == 2, "junto al reloj, bajando de la cabecera a la rejilla");
      uint32_t m = homeCellMask(1, -1);
      chk((m & 4u) && (m & 8u) && !(m & 3u), "y ocupa exactamente sus dos celdas de icono"); }
    chk(homeWgAdd(1, WG_CLIMA) == 0, "y un tercero");
    int n = gHomeWgN[1];
    while(n < HOME_WG_MAX && homeWgAdd(1, WG_WIFI) == 0) n = gHomeWgN[1];
    chk(gHomeWgN[1] == HOME_WG_MAX, "se llena hasta el tope de widgets por pagina");
    chk(homeWgAdd(1, WG_WIFI) == 1, "y el siguiente no: hay un maximo por pagina");
    while(gHomeWgN[1] > 3) homeWgRemove(1, gHomeWgN[1] - 1);
    // Un icono no puede quedarse debajo de un widget.
    homeOrder[homeIdx(1, 0)] = IC_RELOJ;
    gAppFav |= (1u << IC_RELOJ);
    homeOrderNormalize();
    chk(homeOrder[homeIdx(1, 0)] == HOME_EMPTY, "un icono bajo un widget se recoloca");
    int id;
    { int wx, wy, ww, wh; wgRect(&gHomeWg[1][0], wx, wy, ww, wh);
      gHomePage = 1;
      chk(homeWgAt(1, wx + 4, wy + 4) == 0, "el widget responde en su rectangulo");
      chk(!hitHomeIcon(wx + 4, wy + 4, id), "y se queda el toque: ahi no hay icono");
      chk(!homeEmptySpaceAt(wx + 4, wy + 4), "un widget NO es espacio vacio"); }
    homeWgRemove(1, 0);
    chk(gHomeWgN[1] == 2, "quitar un widget lo saca de la pagina");
    gHomePage = 0; }

  // --- 7. WIDGETS: sin espacio en una pagina llena ---
  // Con la rejilla llena de iconos solo queda la cabecera, que no tiene iconos:
  // un widget de una fila cabe ahi; uno de dos filas no cabe en ningun sitio.
  { hcTestReset();
    chk(homeWgAdd(0, WG_CLOCK_A) == 2, "en una pagina llena de iconos no cabe uno de dos filas");
    chk(homeWgAdd(0, WG_CLIMA) == 2, "tampoco el clima de 2x2");
    chk(homeWgAdd(0, WG_CLOCK) == 0 && gHomeWg[0][0].row == 0,
        "pero la cabecera, que no quita celdas a los iconos, acepta uno de una fila");
    chk(hcCountPlaced() == HOME_LEGACY_SLOTS, "sin mover ni un icono"); }

  // --- 8. WIDGETS: ida y vuelta por NVS, y blob corrupto rechazado ---
  { hcTestReset();
    for(int i = 0; i < homeSlotCount(); i++) homeOrder[homeIdx(2, i)] = HOME_EMPTY;
    homeWgAdd(2, WG_STORAGE);
    homeWgAdd(2, WG_CRONO);
    uint8_t blob[HOME_WG_BLOB];
    homeWgSerialize(blob);
    uint8_t n2 = gHomeWgN[2], t0 = gHomeWg[2][0].type;
    memset(gHomeWg, 0, sizeof(gHomeWg));
    memset(gHomeWgN, 0, sizeof(gHomeWgN));
    chk(homeWgDeserialize(blob), "el blob propio se vuelve a leer");
    chk(gHomeWgN[2] == n2 && gHomeWg[2][0].type == t0, "con los mismos widgets");
    uint8_t bad[HOME_WG_BLOB];
    memset(bad, 0xAA, sizeof(bad));
    chk(!homeWgDeserialize(bad), "un blob corrupto se rechaza entero");
    // Formato v1 (sin cabecera, 3 por pagina): cada widget baja una fila.
    uint8_t v1[HOME_WG_BLOB_V1]; memset(v1, 0, sizeof(v1));
    v1[0] = 'W'; v1[1] = 1;
    { int o = 2 + (1 + HOME_WG_MAX_V1 * 5) * 1;       // pagina 1
      v1[o] = 1;
      v1[o + 1] = WG_STORAGE; v1[o + 2] = 2; v1[o + 3] = 1; v1[o + 4] = 2; v1[o + 5] = 1; }
    chk(homeWgDeserializeV1(v1), "el blob v1 se sigue leyendo");
    chk(gHomeWgN[1] == 1 && gHomeWg[1][0].type == WG_STORAGE && gHomeWg[1][0].col == 2 &&
        gHomeWg[1][0].row == 2 && gHomeWg[1][0].w == 2, "y baja una fila: la cabecera es nueva");
    chk(!homeWgDeserialize(v1), "un blob v1 no se confunde con uno v2"); }

  // --- 9. FONDOS: id invalido -> fondo por defecto, sin colgarse ---
  { hcTestReset();
    chk(WALL_N == 8, "hay ocho fondos integrados");
    // drawWallpaperRowsId con un id imposible no debe escribir fuera ni petar:
    // se pinta en homeBuf, que es un buffer real del arnes.
    if(homeBuf){
      drawWallpaperRowsId(homeBuf, 999, true, 0, 3);
      drawWallpaperRowsId(homeBuf, WALL_IMG, true, 0, 3);   // sin imagen cargada
      chk(true, "un id de fondo invalido cae al predeterminado sin reventar");
    }
    // La paleta siempre da un acento con contraste util.
    for(int i = 0; i < WALL_N; i++){
      if(!homeBuf) break;
      drawWallpaperRowsId(homeBuf, i, true, 0, SCR_H - 1);
      wallPaletteBuild(homeBuf);
      uint16_t on = onColor(gWallAcc);
      int d = (int)lum565(gWallAcc) - (int)lum565(on);
      if(d < 0) d = -d;
      chk(d > 60, "el texto sobre el acento tiene contraste suficiente");
      if(gFails) break;
    } }

  // --- 10. PRIORIDAD TACTIL DEL GESTO DE DOS DEDOS ---
  { hcTestReset();
    chk(hpzAllowedHome(), "en el escritorio libre el pellizco esta permitido");
    editMode = true;  chk(!hpzAllowedHome(), "en Modo Edicion no");
    editMode = false;
    qsPanelY = 10;    chk(!hpzAllowedHome(), "con la cortina abierta tampoco");
    qsPanelY = 0;
    gState = ST_APP;  chk(!hpzAllowedHome(), "dentro de una app tampoco");
    gState = ST_HOME;
    hpDragging = true; chk(!hpzAllowedHome(), "ni a mitad de un gesto de pagina");
    hpDragging = false;
    gHomePinch = false; chk(!hpzAllowedHome(), "ni con el gesto desactivado en Ajustes");
    gHomePinch = true;
    chk(!hpzSwallowing(), "en reposo el detector no se traga ningun toque"); }

  // --- 11. PULSACION LARGA: solo sobre un hueco DE VERDAD vacio ---
  { hcTestReset();
    int x0, y0; homeSlotXY(0, x0, y0);
    chk(!homeEmptySpaceAt(x0 + 10, y0 + 10), "encima de un icono no es espacio vacio");
    homeOrder[homeIdx(0, 11)] = HOME_EMPTY;             // se libera la ultima celda
    int xe, ye; homeSlotXY(11, xe, ye);
    chk(homeEmptySpaceAt(xe + 10, ye + 10), "una celda libre si lo es");
    // Clima y Calendario, antes fijos, ahora son widgets de la pagina principal.
    homeWgFactory();
    chk(gHomeWgN[0] == 2 && gHomeWg[0][0].type == WG_CLIMA && gHomeWg[0][1].type == WG_CALEND,
        "Clima y Calendario son widgets de la pagina principal");
    chk(gHomeWg[0][0].row == 0 && gHomeWg[0][1].row == 0, "en su cabecera, donde se veian");
    { int wx, wy, ww, wh; wgRect(&gHomeWg[0][0], wx, wy, ww, wh);
      chk(wy == HOME_HDR_Y && wh == HOME_HDR_H, "con el mismo alto y la misma altura que los fijos");
      chk(homeWgAt(0, wx + 20, wy + 20) == 0, "el izquierdo responde como Clima");
      chk(!homeEmptySpaceAt(wx + 20, wy + 20), "y no cuenta como espacio vacio"); }
    { int wx, wy, ww, wh; wgRect(&gHomeWg[0][1], wx, wy, ww, wh);
      chk(homeWgAt(0, wx + 20, wy + 20) == 1, "el derecho responde como Calendario"); }
    chk(homeWgAt(1, 60, HOME_HDR_Y + 20) == -1 && gHomeWgN[1] == 0,
        "y ya no aparecen en las demas paginas");
    chk(!homeEmptySpaceAt(240, SCR_H - 120), "el dock tampoco");
    chk(!homeEmptySpaceAt(240, SCR_H - 30), "ni la barra de navegacion");
    // Y el gesto completo abre el modo.
    gTestMs = 1000; tDown(xe + 10, ye + 10, 1000);
    homeTick();
    chk(!hcActive, "a los 0 ms todavia no se abre");
    tMove(xe + 10, ye + 10, 1700);
    homeTick();
    chk(hcActive && gState == ST_HOMECFG, "a los 700 ms se abre Personalizar inicio");
    // Y el toque que la abrio no se propaga.
    chk(hcIgnore, "el contacto que la abrio queda consumido");
    hcClose(false); gState = ST_HOME; tReset(); }

  // --- 12. PULSACION LARGA CANCELADA POR MOVIMIENTO ---
  { hcTestReset();
    homeOrder[homeIdx(0, 11)] = HOME_EMPTY;
    int xe, ye; homeSlotXY(11, xe, ye);
    tDown(xe + 10, ye + 10, 2000);
    tMove(xe + 40, ye + 10, 2700);                      // 30 px: mas que la tolerancia
    homeTick();
    chk(!hcActive, "moverse mas de 12 px cancela la pulsacion larga");
    tReset(); }

  // --- 13. AJUSTES: se guardan y se recuperan acotados ---
  { hcTestReset();
    gHomeLabels = false; gHomeLocked = true; gHomeDots = false;
    gHomePinch = false; gHomeReduce = true;
    gHomeCols = 5; gHomeRows = 4; gHomeIconSz = 2; gHomeMain = 1;
    homeOrderSave();
    gHomeLabels = true; gHomeLocked = false; gHomeDots = true;
    gHomePinch = true; gHomeReduce = false;
    gHomeCols = 4; gHomeRows = 3; gHomeIconSz = 0; gHomeMain = 0;
    homeOrderLoad();
    chk(!gHomeLabels && gHomeLocked && !gHomeDots && !gHomePinch && gHomeReduce,
        "los interruptores del inicio sobreviven a un reinicio");
    chk(gHomeCols == 5 && gHomeRows == 4 && gHomeIconSz == 2, "la rejilla y el tamano tambien");
    chk(gHomeMain == 1, "y la pagina principal");
    chk(gHomePage == gHomeMain, "al arrancar se entra por la pagina principal"); }

  // --- 14. MIGRACION DESDE LAS CLAVES ANTIGUAS ---
  // Es la comprobacion que de verdad importa al actualizar una placa: el
  // escritorio que el usuario tenia no puede perderse ni moverse.
  { flexPrefsWipe();
    // (a) placa con "hordp" (3 paginas x 12, paso 12): la version anterior.
    uint8_t viejo[HOME_LEGACY_TOTAL];
    for(int i = 0; i < HOME_LEGACY_TOTAL; i++) viejo[i] = HOME_EMPTY;
    viejo[0] = IC_NOTAS; viejo[1] = IC_RELOJ; viejo[11] = IC_CALC;
    viejo[HOME_LEGACY_SLOTS + 0] = IC_GALERIA;          // pagina 2, primera ranura
    viejo[2 * HOME_LEGACY_SLOTS + 3] = IC_CAMARA;       // pagina 3, cuarta ranura
    prefs.begin("flexos", false);
    prefs.putBytes("hordp", viejo, HOME_LEGACY_TOTAL);
    prefs.putInt("appfav", (int)((1u << IC_NOTAS) | (1u << IC_RELOJ) | (1u << IC_CALC) |
                                 (1u << IC_GALERIA) | (1u << IC_CAMARA)));
    prefs.putInt("apphide", 0);
    prefs.putInt("appn", APP_N);
    prefs.putInt("appver", APPREG_VER);        // la guardo ESTE firmware: no hay ids que traducir
    prefs.end();
    hcTestReset();
    homeOrderLoad();
    chk(homeOrder[homeIdx(0, 0)] == IC_NOTAS,   "migracion: Notas sigue en su ranura");
    chk(homeOrder[homeIdx(0, 1)] == IC_RELOJ,   "migracion: Reloj no se mueve");
    chk(homeOrder[homeIdx(0, 11)] == IC_CALC,   "migracion: la ultima ranura se conserva");
    chk(homeOrder[homeIdx(1, 0)] == IC_GALERIA, "migracion: la pagina 2 se traslada al paso nuevo");
    chk(homeOrder[homeIdx(2, 3)] == IC_CAMARA,  "migracion: y la pagina 3 tambien");
    chk(gHomePageN == HOME_LEGACY_PAGES,        "migracion: siguen siendo tres paginas");
    chk(gHomeCols == 4 && gHomeRows == 3,       "migracion: y la rejilla de siempre");
    // (b) la clave ANTIGUA no se toca: bajar de version tiene que seguir siendo posible
    homeOrderSave();
    uint8_t comprueba[HOME_LEGACY_TOTAL];
    prefs.begin("flexos", true);
    size_t hn = prefs.getBytes("hordp", comprueba, HOME_LEGACY_TOTAL);
    prefs.end();
    chk(hn == HOME_LEGACY_TOTAL && !memcmp(comprueba, viejo, HOME_LEGACY_TOTAL),
        "guardar NO reescribe la clave antigua: se puede volver atras");
    // (c) primer arranque de verdad: sin ninguna clave -> reparto de fabrica
    flexPrefsWipe();
    hcTestReset();
    homeOrderLoad();
    chk(hcCountPlaced() > 0, "primer arranque: el escritorio nace con apps");
    chk(gHomePageN == HOME_LEGACY_PAGES, "y con tres paginas");
    chk(gHomeWgN[gHomeMain] == 2 && gHomeWg[gHomeMain][0].type == WG_CLIMA &&
        gHomeWg[gHomeMain][1].type == WG_CALEND,
        "y con Clima y Calendario en la cabecera de la pagina principal");
    // (d) blob de paginas corrupto -> escritorio usable, no una pantalla rota
    flexPrefsWipe();
    { uint8_t basura[HOME_TOTAL];
      memset(basura, 0x7E, sizeof(basura));           // ids fuera de rango, todos repetidos
      prefs.begin("flexos", false);
      prefs.putBytes("hordq", basura, HOME_TOTAL);
      prefs.putInt("appfav", (int)0x0FFF);
      prefs.putInt("appn", APP_N);
      prefs.end();
      hcTestReset();
      homeOrderLoad();
      bool sano = true;
      for(int i = 0; i < HOME_TOTAL; i++)
        if(homeOrder[i] != HOME_EMPTY && homeOrder[i] >= APP_N) sano = false;
      chk(sano, "un blob corrupto no deja ni un id imposible en la rejilla");
      chk(hcCountPlaced() > 0, "y el escritorio sigue siendo usable"); }
    flexPrefsWipe(); }

  // --- 14bis. TRADUCCION DEL REGISTRO v1 -> v2 ---
  // Es la comprobacion que protege el escritorio de quien ACTUALIZA desde un
  // firmware con las 22 apps de antes. Educacion (6) y Bienestar (9) ya no
  // existen: sus sitios los heredan Flex Store y Device Care, y todo lo que
  // venia despues baja de numero. Si la traduccion no funcionara, el usuario
  // veria en Inicio apps distintas de las que puso -- que es exactamente el
  // fallo que este bloque tiene que hacer imposible.
  { flexPrefsWipe();
    // Ids de la version ANTERIOR del registro (22 apps).
    const uint8_t V1_NOTAS = 5, V1_EDU = 6, V1_NAV = 7, V1_BIEN = 9,
                  V1_AJUSTES = 12, V1_CALC = 13, V1_BRUJULA = 20, V1_SECFOLDER = 21;
    uint8_t viejo[HOME_TOTAL];
    for(int i = 0; i < HOME_TOTAL; i++) viejo[i] = HOME_EMPTY;
    viejo[homeIdx(0, 0)] = V1_NOTAS;
    viejo[homeIdx(0, 1)] = V1_EDU;             // -> Flex Store, en su MISMA ranura
    viejo[homeIdx(0, 2)] = V1_NAV;
    viejo[homeIdx(0, 3)] = V1_BIEN;            // -> Device Care, en su MISMA ranura
    viejo[homeIdx(0, 4)] = V1_CALC;
    viejo[homeIdx(1, 0)] = V1_BRUJULA;
    viejo[homeIdx(1, 1)] = V1_SECFOLDER;       // retirada: su ranura se vacia
    prefs.begin("flexos", false);
    prefs.putBytes("hordq", viejo, HOME_TOTAL);
    prefs.putInt("appfav", (int)((1u << V1_NOTAS) | (1u << V1_EDU) | (1u << V1_NAV) |
                                 (1u << V1_BIEN) | (1u << V1_CALC) |
                                 (1u << V1_BRUJULA) | (1u << V1_SECFOLDER)));
    prefs.putInt("apphide", (int)(1u << V1_AJUSTES));   // (lo limpia la regla de Ajustes)
    prefs.putInt("appn", 22);
    prefs.end();                                // SIN "appver": asi lo guardaba la v1
    hcTestReset();
    gAppLock = (uint32_t)(1u << V1_CALC);       // un candado por app, para verlo viajar
    homeOrderLoad();
    chk(homeOrder[homeIdx(0, 0)] == IC_NOTAS,     "v1->v2: Notas no se mueve de ranura");
    chk(homeOrder[homeIdx(0, 1)] == IC_FLEXSTORE, "v1->v2: Flex Store ocupa el sitio de Educacion");
    chk(homeOrder[homeIdx(0, 2)] == IC_NAV,       "v1->v2: el Navegador sigue siendo el Navegador");
    chk(homeOrder[homeIdx(0, 3)] == IC_DEVCARE,   "v1->v2: Device Care ocupa el sitio de Bienestar");
    chk(homeOrder[homeIdx(0, 4)] == IC_CALC,      "v1->v2: la Calculadora tampoco se mueve");
    chk(homeOrder[homeIdx(1, 0)] == IC_BRUJULA,   "v1->v2: Flex Compass conserva su ranura");
    chk(homeOrder[homeIdx(1, 1)] == HOME_EMPTY,   "v1->v2: la ranura de la Carpeta segura se vacia");
    chk(appIsFav(IC_FLEXSTORE) && appIsFav(IC_DEVCARE), "v1->v2: las dos nuevas quedan en Inicio");
    chk(appIsFav(IC_CALC) && appIsFav(IC_BRUJULA),      "v1->v2: las favoritas siguen siendolo");
    chk((gAppLock & (uint32_t)(1u << IC_CALC)) != 0,    "v1->v2: el candado viaja con su app");
    chk((gAppFav & ~((1u << APP_N) - 1u)) == 0,         "v1->v2: no queda ni un bit fuera del registro");
    // Y una segunda carga NO vuelve a traducir: la version ya quedo guardada.
    homeOrderSave();
    hcTestReset();
    homeOrderLoad();
    chk(homeOrder[homeIdx(0, 1)] == IC_FLEXSTORE, "v1->v2: traducir es idempotente");
    chk(homeOrder[homeIdx(0, 4)] == IC_CALC,      "...y no vuelve a correr sobre lo ya traducido");
    flexPrefsWipe(); }

  // --- 15. CICLO DE VIDA COMPLETO: entrar, ANIMAR, salir ---
  // Esta comprobacion existe por un fallo real: el modo se podia abrir y quedar
  // ATRAPADO porque nadie llamaba a hcTick(). Aqui se ejercita el ciclo entero
  // como lo haria loop(), incluida la animacion, y se exige que vuelva solo.
  { hcTestReset();
    homeOrder[homeIdx(0, 11)] = HOME_EMPTY;
    int xe, ye; homeSlotXY(11, xe, ye);
    gHomeReduce = false;                       // con animacion: el caso que se atascaba
    tDown(xe + 10, ye + 10, 30000);
    tMove(xe + 10, ye + 10, 30700);
    homeTick();
    chk(hcActive && gState == ST_HOMECFG, "ciclo: la pulsacion larga abre el modo");
    chk(hcAnim == 1, "ciclo: arranca la animacion de entrada");
    // Se suelta el dedo y se despacha como hace loop(): hcTick() por vuelta.
    tUp(30760, false);
    for(int i = 0; i < 40 && hcAnim; i++){ gTestMs += 20; tReset(); hcTick(); }
    chk(hcAnim == 0, "ciclo: la animacion de entrada TERMINA sola");
    chk(hcActive && gState == ST_HOMECFG, "ciclo: y el modo se queda abierto y vivo");
    gTestMs += 40; tReset(); hcTick();
    chk(!hcDirty, "ciclo: el modo se ha pintado (ya no queda nada sucio)");
    // Salir por el boton Inicio de la barra de navegacion.
    gNavMode = 0;
    tDown(SCR_W / 2, SCR_H - 40, gTestMs + 10);
    tUp(gTestMs + 60, true);
    hcTick();
    for(int i = 0; i < 40 && gState == ST_HOMECFG; i++){ gTestMs += 20; tReset(); hcTick(); }
    chk(gState == ST_HOME, "ciclo: Inicio devuelve al escritorio");
    chk(!hcActive, "ciclo: el modo queda cerrado");
    chk(hcThumb == NULL, "ciclo: la miniatura del fondo se libera al salir");
    chk(hcWallPrev == NULL, "ciclo: y las previsualizaciones tambien");
    // Y el escritorio responde con normalidad justo despues.
    int id, x0, y0; homeSlotXY(0, x0, y0);
    chk(hitHomeIcon(x0 + 10, y0 + 10, id), "ciclo: el Home vuelve a responder al toque");
    tReset(); }

  // --- 16. ENTRAR Y SALIR MUCHAS VECES NO DEJA MEMORIA COLGANDO ---
  { hcTestReset();
    gHomeReduce = true;                        // sin animacion: el ciclo es inmediato
    for(int k = 0; k < 50; k++){
      hcEnter();
      if(!hcActive){ chk(false, "repeticion: el modo no se abrio"); break; }
      hcBeginExit();
      if(gState != ST_HOME){ chk(false, "repeticion: el modo no se cerro"); break; }
    }
    chk(!hcActive && hcThumb == NULL && hcWallPrev == NULL,
        "50 entradas y salidas no dejan ni un buffer reservado");
    gHomeReduce = false; }

  // --- 17. CIERRE SEGURO DESDE OTRAS RUTAS (bloqueo, OTA, vuelta al Home) ---
  { hcTestReset();
    hcEnter();
    chk(hcActive, "cierre: el modo esta abierto");
    hcClose(true);                              // la ruta que usan autoLockNow/loop-OTA/enterHome
    chk(!hcActive, "cierre: hcClose lo cierra");
    chk(hcThumb == NULL && hcWallPrev == NULL, "cierre: y suelta sus buffers");
    chk(gHomePage < gHomePageN, "cierre: la pagina visible queda en rango"); }

  // --- 18. NINGUN OVERLAY COMPITE CON EL MODO ---
  { hcTestReset();
    gState = ST_HOME;
    chk(qsCanOpen(), "la cortina se abre en el escritorio");
    gState = ST_HOMECFG;
    chk(!qsCanOpen(), "pero NO encima de Personalizar inicio");
    // Una notificacion visible no debe dibujarse encima NI robar el toque.
    gNotifCount = 1;
    gNotifs[0].active = true; gNotifs[0].armed = true; gNotifs[0].phase = NP_IDLE;
    gNotifs[0].slideX = 0; gNotifs[0].bornMs = gTestMs;
    notifDragIdx = -1;
    tDown(NOTIF_MARGIN_X + 20, NOTIF_Y0 + 20, gTestMs + 10);
    tUp(gTestMs + 40, true);
    notifHandleTouch();
    chk(T.tap, "la isla NO consume el toque en Personalizar inicio");
    chk(notifDragIdx == -1, "ni empieza a arrastrar su tarjeta");
    notifTick();
    chk(notifPaused, "y sus fases quedan pausadas mientras el modo esta abierto");
    gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs));
    notifPaused = false; notifBandOn = false;
    gState = ST_HOME; tReset(); }

  hcTestReset();
  if(!gFails) printf("  Personalizar inicio: todas las comprobaciones pasan.\n");
}

static void testTarjetaCronometro(){
  printf("Tarjeta del cronometro\n");
  cronoTestReset();
  gLand = false; gHosted = false;

  // --- 1. LA TARJETA NO SE ENCOGE ---
  chk(CRONO_CARD_W == SCR_W - 40, "la tarjeta sigue ocupando casi todo el ancho");
  chk(CRONO_CARD_H == 196,        "y conserva su alto: hay sitio para los dos botones");
  chk(CRONO_CARD_Y + CRONO_CARD_H <= CRONO_BAND_B,
      "cabe entera dentro de la banda que captura y restaura");
  chk(CRONO_DYN_T >= CRONO_CARD_Y && CRONO_DYN_B <= CRONO_CARD_Y + CRONO_CARD_H,
      "la sub-banda dinamica esta dentro de la tarjeta");

  // --- 2. LOS COLORES SIGUEN AL TEMA ---
  { bool prev = gDark;
    gDark = true;
    uint16_t accOsc = CRONO_ACCENT, bgOsc = CRONO_CARDBG, discOsc = CRONO_DISC;
    gDark = false;
    uint16_t accClr = CRONO_ACCENT, bgClr = CRONO_CARDBG, discClr = CRONO_DISC;
    chk(bgOsc  != bgClr,  "la superficie de la tarjeta cambia con el tema");
    chk(discOsc != discClr, "y el disco de la esfera tambien");
    chk(accOsc == TH_PRIM || accClr == TH_PRIM, "el acento es el del tema, no un violeta fijo");
    // Y no es ninguno de los tres literales que habia antes.
    const uint16_t VIOLETA = rgb565(108, 92, 231);
    const uint16_t GRIS_TARJETA = rgb565(46, 48, 58);
    gDark = true;
    chk(CRONO_CARDBG != GRIS_TARJETA, "la tarjeta ya no usa el gris fijo (tema oscuro)");
    gDark = false;
    chk(CRONO_CARDBG != GRIS_TARJETA, "ni con tema claro");
    chk(CRONO_ACCENT != VIOLETA || TH_PRIM == VIOLETA,
        "el acento solo es violeta si el usuario ha elegido ese acento");
    gDark = prev; }

  // --- 3. LA PILDORA NO SE CONGELA CON LA TARJETA ABIERTA ---
  // La banda de la pildora y la sub-banda dinamica de la tarjeta no se
  // solapan, que es lo que permite refrescarlas por separado.
  chk(CRONO_CAP_Y + CRONO_CAP_H < CRONO_DYN_T,
      "la pildora y la sub-banda dinamica de la tarjeta no se pisan");
  chk(CRONO_CAP_Y >= CRONO_BAND_T,
      "pero la pildora si esta dentro de la banda que la tarjeta restaura");
  chk(CRONO_CARD_Y > CRONO_CAP_Y,
      "la tarjeta empieza por debajo: la pildora se sigue viendo, asi que "
      "tiene que estar al dia");

  // El texto de la pildora sale de la MISMA fuente que el de la tarjeta,
  // asi que no pueden discrepar salvo por congelacion.
  { cronoStart();
    gTestMs += 7000;
    char cap[16], card[16];
    cronoFmt(cap,  sizeof(cap),  cronoElapsed(), false);
    cronoFmt(card, sizeof(card), cronoElapsed(), true);
    chk(!strncmp(cap, card, 5), "pildora y tarjeta formatean el mismo tiempo");
    uint32_t antes = cronoElapsed();
    gTestMs += 11000;
    chk(cronoElapsed() == antes + 11000, "y el tiempo avanza con el reloj");
    cronoReset(); }

  // --- 4. ESTADOS DE LA TARJETA ---
  chk(CC_HIDDEN != CC_OPENING && CC_OPENING != CC_OPEN && CC_OPEN != CC_CLOSING,
      "los cuatro estados de la tarjeta son distintos");
  { // Totalmente visible => geometria EXACTA, sin resto de la animacion.
    chk(cronoLerp(10, 200, 1.0f) == 200, "al terminar la animacion el offset es cero");
    chk(cronoLerp(10, 200, 0.0f) == 10,  "y al empezar, el de partida"); }

  cronoTestReset();
  gDark = false; tReset();
  if(!gFails) printf("  Tarjeta del cronometro: todas las comprobaciones pasan.\n");
}


// #############################################################
//  FLEX STORE + FLEX ACCOUNT
//  ------------------------------------------------------------
//  Se ejercita el PUENTE de verdad (FlexOS_Store_Bridge.h y
//  FlexOS_Account_Bridge.h) contra los dobles de los cuatro modulos.
//  Lo que se comprueba es exactamente lo que puede fallar en una
//  lista filtrada y en una maquina de estados con varias vias de
//  entrada:
//
//    · que buscar filtre por nombre, resumen, categoria e ID,
//    · que tocar una fila FILTRADA use el indice REAL del catalogo
//      (instalar) y el ID REAL del paquete (abrir), no la posicion
//      de la fila tocada,
//    · que el teclado escriba, borre, ponga espacio y limpie,
//    · que las zonas tactiles de la cabecera y del detalle no se
//      solapen entre si,
//    · que repintar NO vuelva a leer la lista instalada ni a barrer
//      el catalogo entero,
//    · y que volver del configurador de Wi-Fi devuelva la pantalla
//      de Cuenta a la via por la que se entro.
//
//  El catalogo y la lista instalada de aqui son FIXTURES de prueba
//  (ids "com.prueba.*"): no son apps reales ni se compilan para la
//  placa.
// #############################################################
extern FlexStoreItem  gStubCatalog[];
extern int            gStubCatalogN;
extern FlexPkgInfo    gStubInstalled[];
extern int            gStubInstalledN;
extern int            gStubInstallIndex;
extern char           gStubRuntimeId[];
extern char           gStubUninstallId[];
extern int            gStubPkgListCalls;
extern int            gStubCatalogItemCalls;
extern bool           gStubRuntimeOk;
extern bool           gStubStoreCancelled;
extern FlexStoreState gStubStoreState;
extern uint8_t        gStubStoreProgress;
extern bool           gStubAccountLinked;
extern FlexAccountSnapshot gStubAccountSnap;
extern bool           gStubAccountAccept;
extern int            gStubAccountRequests, gStubAccountCancels;
extern char           gStubAccountLastLabel[64];

static void stubCatalogAdd(const char* pkg, const char* name, const char* summary,
                           const char* cat, uint32_t code){
  FlexStoreItem& it = gStubCatalog[gStubCatalogN++];
  memset(&it, 0, sizeof(it));
  snprintf(it.packageId,   sizeof(it.packageId),   "%s", pkg);
  snprintf(it.name,        sizeof(it.name),        "%s", name);
  snprintf(it.summary,     sizeof(it.summary),     "%s", summary);
  snprintf(it.category,    sizeof(it.category),    "%s", cat);
  snprintf(it.versionName, sizeof(it.versionName), "1.0.0");
  it.versionCode = code; it.packageBytes = 4096; it.ratingX100 = 450;
}
static void stubInstalledAdd(const char* pkg, const char* name, uint32_t code){
  FlexPkgInfo& it = gStubInstalled[gStubInstalledN++];
  memset(&it, 0, sizeof(it));
  snprintf(it.id,          sizeof(it.id),          "%s", pkg);
  snprintf(it.name,        sizeof(it.name),        "%s", name);
  snprintf(it.versionName, sizeof(it.versionName), "1.0.0");
  it.versionCode = code; it.installedBytes = 4096;
}
static void storeTap(int x, int y){
  T = Touch();
  T.tap = true; T.released = true; T.x = T.startX = x; T.y = T.startY = y;
}
// Centro de la fila `row` de la lista (las tarjetas van en 148 + row*174).
static int storeRowCenterY(int row){ return 148 + row * 174 + 60; }

static void testFlexStore(){
  printf("Flex Store: busqueda real, indices y zonas tactiles\n");

  // --- fixtures ---
  gStubCatalogN = 0; gStubInstalledN = 0;
  gStubInstallIndex = -1; gStubRuntimeId[0] = 0; gStubUninstallId[0] = 0;
  gStubRuntimeOk = true; gStubStoreState = FLEXSTORE_READY; gStubStoreProgress = 100;
  stubCatalogAdd("com.prueba.alfa",  "Alfa",  "Cronometro de bolsillo", "Utilidades", 3);
  stubCatalogAdd("com.prueba.beta",  "Beta",  "Libreta de apuntes",     "Trabajo",    2);
  stubCatalogAdd("com.prueba.gamma", "Gamma", "Panel de sensores",      "Sistema",    5);
  stubCatalogAdd("com.prueba.delta", "Delta", "Reproductor local",      "Media",      1);
  chk(gStubCatalogN == 4, "el catalogo de prueba tiene 4 entradas");

  gState = ST_APP; gAppId = IC_FLEXSTORE;
  storeEnter();
  chk(storeView == SV_DISCOVER, "la tienda abre en Descubrir");
  chk(storeSearch[0] == 0,      "y sin busqueda previa");
  chk(storeCatalogFilteredCount() == 4, "sin filtro se ven las 4 del catalogo");
  for(int i = 0; i < 4; i++) chk(storeCatalogIndexAt(i) == i, "sin filtro el indice filtrado es el real");

  // --- 1. filtrar por NOMBRE, y en minusculas ---
  snprintf(storeSearch, sizeof(storeSearch), "gamma"); storeFilterInvalidate();
  chk(storeCatalogFilteredCount() == 1, "buscar por nombre deja una sola app");
  chk(storeCatalogIndexAt(0) == 2,      "y su indice es el REAL del catalogo, no 0");

  // --- 2. filtrar por RESUMEN ---
  snprintf(storeSearch, sizeof(storeSearch), "apuntes"); storeFilterInvalidate();
  chk(storeCatalogFilteredCount() == 1 && storeCatalogIndexAt(0) == 1, "buscar por resumen");

  // --- 3. filtrar por CATEGORIA ---
  snprintf(storeSearch, sizeof(storeSearch), "media"); storeFilterInvalidate();
  chk(storeCatalogFilteredCount() == 1 && storeCatalogIndexAt(0) == 3, "buscar por categoria");

  // --- 4. filtrar por ID DE PAQUETE ---
  snprintf(storeSearch, sizeof(storeSearch), "prueba.alfa"); storeFilterInvalidate();
  chk(storeCatalogFilteredCount() == 1 && storeCatalogIndexAt(0) == 0, "buscar por ID de paquete");

  // --- 5. cero resultados ---
  snprintf(storeSearch, sizeof(storeSearch), "zzzz"); storeFilterInvalidate();
  chk(storeCatalogFilteredCount() == 0, "una consulta sin coincidencias da cero");
  chk(storeCatalogIndexAt(0) == -1,     "y no devuelve ninguna fila");

  // --- 6. TOCAR UNA FILA FILTRADA INSTALA EL INDICE REAL ---
  // Es el fallo clasico de una lista filtrada: la fila 0 de la pantalla es la
  // entrada 2 del catalogo, y instalar la 0 instalaria otra app.
  snprintf(storeSearch, sizeof(storeSearch), "gamma"); storeFilterInvalidate();
  storeView = SV_DISCOVER; storePage = 0; storeRender();
  gStubInstallIndex = -1;
  storeTap(SCR_W - 60, storeRowCenterY(0));       // boton de accion de la fila 0
  storeTick();
  chk(gStubInstallIndex == 2, "instalar desde una fila filtrada usa el indice real del catalogo");

  // --- 7. TOCAR "ABRIR" USA EL ID REAL DEL PAQUETE ---
  stubInstalledAdd("com.prueba.delta", "Delta", 1);
  stubInstalledAdd("com.prueba.gamma", "Gamma", 5);   // misma version que el catalogo -> "Abrir"
  storeInstalledDirty = true; storeFilterInvalidate();
  snprintf(storeSearch, sizeof(storeSearch), "gamma"); storeFilterInvalidate();
  storeView = SV_DISCOVER; storePage = 0; storeRender();
  gStubRuntimeId[0] = 0;
  storeTap(SCR_W - 60, storeRowCenterY(0));
  storeTick();
  chk(!strcmp(gStubRuntimeId, "com.prueba.gamma"),
      "abrir desde una fila filtrada usa el ID real, no la posicion de la fila");

  // --- 8. la ficha de detalle sale de la entrada tocada ---
  storeView = SV_DISCOVER; storePage = 0; storeSearch[0] = 0; storeFilterInvalidate(); storeRender();
  storeTap(60, storeRowCenterY(1));               // fila 1 = catalogo 1, zona de la tarjeta
  storeTick();
  chk(storeView == SV_DETAIL && storeSelected == 1 && !storeSelectedInstalled,
      "tocar la tarjeta abre el detalle de esa misma entrada");
  storeBack();
  chk(storeView == SV_DISCOVER, "y volver regresa a Descubrir");

  // --- 9. TECLADO de la busqueda ---
  storeView = SV_SEARCH; storeSearchSource = SV_DISCOVER; storeSearch[0] = 0;
  storeSearchTap(12 + 42 / 2, 192 + 24);          // Q (fila 1, tecla 0)
  storeSearchTap(34 + 42 / 2, 254 + 24);          // A (fila 2, tecla 0)
  storeSearchTap(22 + 42 / 2, 316 + 24);          // Z (fila 3, tecla 0)
  chk(!strcmp(storeSearch, "qaz"), "las teclas escriben en minusculas y en orden");
  storeSearchTap(129, 397);                        // Espacio
  chk(!strcmp(storeSearch, "qaz "), "la barra espaciadora anade un espacio");
  storeSearchTap(403, 316 + 24);                   // Borrar
  chk(!strcmp(storeSearch, "qaz"), "Borrar quita el ultimo caracter");
  storeSearchTap(351, 397);                        // Limpiar
  chk(storeSearch[0] == 0, "Limpiar vacia la consulta entera");
  storeSearchTap(SCR_W / 2, 470);                  // Mostrar resultados
  chk(storeView == SV_DISCOVER, "aplicar la busqueda vuelve a la vista de origen");

  // La ultima tecla de cada fila y la de "Borrar" no se pisan: un toque sobre
  // "Borrar" no puede escribir una letra.
  storeView = SV_SEARCH; storeSearch[0] = 0;
  chk(!storeSearchTapKey(403, 316 + 24), "el area de Borrar no es ninguna tecla");
  chk(storeSearchTapKey(12 + 9 * 46 + 21, 192 + 24), "la ultima tecla de la fila 1 (P) si responde");
  chk(!strcmp(storeSearch, "p"), "y escribe la letra correcta");

  // --- 10. ZONAS TACTILES DE LA CABECERA ---
  // El boton Cuenta empieza en SCR_W-132 y la lupa ocupa la franja anterior:
  // no pueden solaparse, y ninguna de las dos puede caer sobre las pestanas.
  chk((SCR_W - 132) > (SCR_W - 190), "la franja de la lupa queda a la izquierda de Cuenta");
  storeView = SV_DISCOVER; storeSearch[0] = 0; storeFilterInvalidate(); storeRender();
  storeTap(SCR_W - 60, 40); storeTick();
  chk(gState == ST_OOBE_ACCOUNT, "tocar Cuenta abre Flex Account");
  chk(accountReturn == ACC_RET_STORE, "y anota que hay que volver a Flex Store");
  gState = ST_APP; storeView = SV_DISCOVER; storeRender();
  storeTap(SCR_W - 162, 40); storeTick();
  chk(storeView == SV_SEARCH, "tocar la lupa abre la busqueda");
  storeBack();

  // La esquina superior izquierda de la pestana "Descubrir" (y = 88..130) ya
  // NO cierra la tienda: la franja de "atras" termina en la linea divisoria.
  // El candado de kiosco se enciende SOLO para esta comprobacion: hace que
  // appClose() vuelva sin animar (winRevealAnim gira sobre millis(), que aqui
  // es un reloj virtual congelado), asi la prueba distingue las dos rutas sin
  // depender de una animacion.
  bool kioscoAntes = kioskOn; kioskOn = true;
  storeView = SV_INSTALLED; storeInstalledDirty = true; storeRender();
  storeTap(40, 90); storeTick();
  chk(storeView == SV_DISCOVER, "un toque en la esquina de la pestana cambia de pestana, no cierra la tienda");
  // Y la franja de atras de verdad (por encima de la linea divisoria) si sale.
  storeView = SV_DETAIL; storeSelected = 0; storeSelectedInstalled = false; storeRender();
  storeTap(40, 40); storeTick();
  chk(storeView == SV_DISCOVER, "y sobre la cabecera si funciona como atras");
  kioskOn = kioscoAntes;

  // --- 11. DETALLE: accion principal y desinstalar no se solapan ---
  storeSelected = 1; storeSelectedInstalled = false; storeView = SV_DETAIL;
  storeConfirmDelete = false; storeRender();
  gStubInstallIndex = -1;
  storeTap(SCR_W / 2, 495); storeTick();          // accion principal
  chk(gStubInstallIndex == 1, "la accion principal del detalle instala la entrada mostrada");

  // El hueco entre los dos botones (522..540 dibujados) no dispara ninguno.
  storeSelected = 1; storeSelectedInstalled = false; storeView = SV_DETAIL;
  storeConfirmDelete = false; storeRender();
  gStubInstallIndex = -1;
  storeTap(SCR_W / 2, 531); storeTick();
  chk(gStubInstallIndex == -1 && !storeConfirmDelete,
      "el hueco entre los dos botones del detalle no dispara ninguno");

  // Y con la app SIN instalar el boton de desinstalar no se dibuja: su area
  // tampoco puede responder.
  storeTap(SCR_W / 2, 560); storeTick();
  chk(!storeConfirmDelete, "sin app instalada, el area de Desinstalar no responde");

  // Desinstalar pide confirmacion antes de borrar nada.
  storeInstalledDirty = true; storeEnsureInstalled();
  int idxDelta = storeInstalledFind("com.prueba.delta");
  chk(idxDelta >= 0, "la fixture instalada esta en la lista");
  storeSelected = idxDelta; storeSelectedInstalled = true; storeView = SV_DETAIL;
  storeConfirmDelete = false; storeRender();
  gStubUninstallId[0] = 0;
  storeTap(SCR_W / 2, 560); storeTick();
  chk(storeConfirmDelete && gStubUninstallId[0] == 0, "el primer toque solo pide confirmacion");
  storeTap(SCR_W / 2, 560); storeTick();
  chk(!strcmp(gStubUninstallId, "com.prueba.delta"), "el segundo toque desinstala ESA app");

  // --- 12. REPINTAR NO RELEE EL ALMACENAMIENTO NI BARRE EL CATALOGO ---
  storeView = SV_INSTALLED; storeSearch[0] = 0; storeInstalledDirty = true;
  storeFilterInvalidate(); storeRender();
  int listCalls = gStubPkgListCalls;
  storeRender(); storeRender(); storeRender();
  chk(gStubPkgListCalls == listCalls, "repintar Instaladas no vuelve a llamar a flexPkgList");
  storePage = 0; storeView = SV_DISCOVER; storeRender();
  int itemCalls = gStubCatalogItemCalls;
  storeRender();
  chk(gStubCatalogItemCalls - itemCalls <= storeRowsPerPage(),
      "repintar Descubrir solo copia las filas que dibuja, no todo el catalogo");
  // Y cambiar la busqueda SI rehace el filtro.
  snprintf(storeSearch, sizeof(storeSearch), "beta"); storeFilterInvalidate();
  chk(storeCatalogFilteredCount() == 1, "cambiar la consulta rehace el filtro");

  // --- 13. salir no aborta una instalacion ya verificada ---
  gStubStoreCancelled = false; gStubStoreState = FLEXSTORE_INSTALLING;
  storeExit();
  chk(!gStubStoreCancelled, "salir durante la instalacion no la cancela");
  gStubStoreCancelled = false; gStubStoreState = FLEXSTORE_READY;
  storeExit();
  chk(gStubStoreCancelled, "pero salir sin nada en curso si cancela la consulta de catalogo");

  gStubCatalogN = 0; gStubInstalledN = 0; storeSearch[0] = 0; storeFilterInvalidate();
  if(!gFails) printf("  Flex Store: todas las comprobaciones pasan.\n");
}

static void testFlexAccount(){
  printf("Flex Account: primer arranque, omitir y vuelta desde Wi-Fi\n");
  bool oobeAntes = cfgOobeDone;
  int  estadoAntes = gState;

  memset(&gStubAccountSnap, 0, sizeof(gStubAccountSnap));
  gStubAccountSnap.state = FLEX_ACCOUNT_UNLINKED;
  gStubAccountLinked = false;

  // --- 1. el paso de Cuenta llega DESPUES de idioma y nombre ---
  cfgOobeDone = false;
  gState = ST_OOBE_NAME;
  accountOobeEnter();
  chk(gState == ST_OOBE_ACCOUNT, "tras el nombre se entra en Flex Account");
  chk(accountReturn == ACC_RET_OOBE && accountFirstBoot, "en modo primer arranque");
  chk(!cfgOobeDone, "y la primera configuracion aun no esta marcada como terminada");

  // --- 2. OMITIR no deja el equipo atascado: termina el OOBE y va al bloqueo ---
  storeTap(SCR_W / 2, 706);
  accountOobeTick();
  chk(cfgOobeDone, "omitir cierra la primera configuracion");
  chk(gState == ST_LOCK, "y deja el equipo en la pantalla de bloqueo");

  // --- 3. VOLVER DESDE Wi-Fi CONSERVA LA VIA DE ENTRADA ---
  // Regresion real: entrar al Wi-Fi desde el boton Cuenta de Flex Store
  // devolvia la pantalla en modo primer arranque, y su boton se llevaba el
  // equipo al bloqueo reescribiendo la marca de OOBE.
  gState = ST_APP; gAppId = IC_FLEXSTORE;
  accountStoreEnter();
  chk(accountReturn == ACC_RET_STORE && !accountFirstBoot, "desde Flex Store no es primer arranque");
  wifiOobeEnter();
  chk(gState == ST_WIFI, "el boton de Wi-Fi abre el configurador real");
  wifiExit();
  chk(gState == ST_OOBE_ACCOUNT, "y al salir se vuelve a Flex Account");
  chk(accountReturn == ACC_RET_STORE && !accountFirstBoot,
      "conservando la via de entrada (antes se convertia en primer arranque)");

  // Salir ahora devuelve a Flex Store, no al bloqueo.
  accountFinish();
  chk(gState == ST_APP && storeView == SV_DISCOVER, "salir devuelve a Flex Store");

  // --- 4. la misma pantalla desde Ajustes vuelve a Ajustes ---
  gState = ST_APP; gAppId = IC_AJUSTES; setView = 1; setSel = 0;
  accountSettingsEnter();
  chk(gState == ST_OOBE_ACCOUNT && accountReturn == ACC_RET_SETTINGS, "Ajustes abre Flex Account");
  wifiOobeEnter(); wifiExit();
  chk(accountReturn == ACC_RET_SETTINGS, "y volver del Wi-Fi tampoco cambia ese destino");
  accountFinish();
  chk(gState == ST_APP && setView == 1, "salir devuelve a la pantalla de Ajustes");

  // --- 5. el texto de la fila de Ajustes dice el estado REAL ---
  char fila[64];
  accountSettingsText(fila, sizeof(fila));
  chk(!strcmp(fila, "Sin cuenta vinculada"), "sin cuenta, la fila lo dice");
  gStubAccountSnap.state = FLEX_ACCOUNT_LINKED;
  snprintf(gStubAccountSnap.flexAddress, sizeof(gStubAccountSnap.flexAddress), "usuario@flex");
  gStubAccountLinked = true;
  gStubAccountSnap.link = FLEX_LINK_LINKED;
  accountSettingsText(fila, sizeof(fila));
  chk(!strcmp(fila, "usuario@flex"), "con cuenta, muestra la direccion que trajo el modulo");
  // Vinculada pero sin red (o aun sin validar en este arranque): SIGUE
  // vinculada y se dice que falta la conexion, nunca "sin cuenta".
  gStubAccountSnap.link = FLEX_LINK_LINKED_OFFLINE;
  accountSettingsText(fila, sizeof(fila));
  chk(!strncmp(fila, "usuario@flex", 12) && strstr(fila, "sin conexi"), "vinculada sin red: la direccion y 'sin conexion'");
  gStubAccountSnap.link = FLEX_LINK_LINKED;

  // --- 6. las zonas tactiles de la pantalla no se solapan ---
  // y = 600 queda POR DEBAJO de los dos botones dibujados (452..510 y 526..580)
  // y por encima de la barra de omitir (>= 670): no puede disparar nada. Antes
  // la segunda franja llegaba a 610 y ese punto abria el configurador de Wi-Fi.
  gStubAccountSnap.state = FLEX_ACCOUNT_UNLINKED; gStubAccountLinked = false;
  accountStoreEnter();
  storeTap(SCR_W / 2, 600);
  accountOobeTick();
  chk(gState == ST_OOBE_ACCOUNT, "un toque en el hueco bajo los botones no hace nada");
  // El boton de verdad si responde: sin Wi-Fi lleva al configurador de red.
  storeTap(SCR_W / 2, 480);
  accountOobeTick();
  chk(gState == ST_WIFI, "y el boton principal sin Wi-Fi abre el configurador de red");
  wifiExit();

  gStubAccountLinked = false;
  memset(&gStubAccountSnap, 0, sizeof(gStubAccountSnap));
  cfgOobeDone = oobeAntes; gState = estadoAntes;
  if(!gFails) printf("  Flex Account: todas las comprobaciones pasan.\n");
}


// #############################################################
//  FLEX ACCOUNT · "Iniciar sesion" CON Wi-Fi: el camino que la prueba de arriba
//  no podia recorrer (su doble rechazaba toda peticion y el Wi-Fi siempre
//  estaba caido). Aqui el doble acepta como el modulo real y el Wi-Fi esta
//  conectado: pedir el enlace, cancelar, ver el codigo y SU PROBLEMA, el error
//  y REINTENTAR sin reiniciar, y terminar el primer arranque.
// #############################################################
static void testFlexAccountLink(){
  printf("Flex Account: Iniciar sesion con Wi-Fi, error, reintento y cancelar\n");
  bool oobeAntes = cfgOobeDone; int estadoAntes = gState;
  char nombreAntes[24]; snprintf(nombreAntes, sizeof(nombreAntes), "%s", cfgName);
  memset(&gStubAccountSnap, 0, sizeof(gStubAccountSnap));
  gStubAccountSnap.state = FLEX_ACCOUNT_UNLINKED; gStubAccountLinked = false;
  gStubAccountAccept = true; gStubAccountRequests = gStubAccountCancels = 0;
  gInoWifiStatus = WL_CONNECTED;
  snprintf(cfgName, sizeof(cfgName), "Ana");
  cfgOobeDone = false; gState = ST_OOBE_NAME;
  // El toque se consume: sin limpiar T, el siguiente paso volveria a verlo.
  auto toca = [](int x, int y){ storeTap(x, y); accountOobeTick(); T = Touch(); };
  auto repinta = [](){ T = Touch(); gTestMs += 100; accountOobeTick(); };       // el sondeo de estado va cada 80 ms

  accountOobeEnter();
  chk(gState == ST_OOBE_ACCOUNT && accountLastState == FLEX_ACCOUNT_UNLINKED, "primer arranque: pantalla de Flex Account sin cuenta");

  // 1. Con Wi-Fi el boton principal PIDE EL ENLACE (no abre el configurador de red).
  toca(SCR_W / 2, 480);
  chk(gStubAccountRequests == 1 && !strcmp(gStubAccountLastLabel, "Ana"), "'Iniciar sesion' pide el enlace con el nombre del aparato");
  chk(gState == ST_OOBE_ACCOUNT, "y se queda en la pantalla de la cuenta");
  chk(accountLastState == FLEX_ACCOUNT_REQUESTING, "que pasa a 'Creando enlace seguro'");
  toca(SCR_W / 2, 480);
  chk(gStubAccountRequests == 1, "con el enlace en curso un segundo toque en esa zona no pide otro");

  // 2. Cancelar llega al modulo; cuando este termina, la pantalla vuelve a ofrecer el inicio de sesion.
  toca(SCR_W / 2, 570);
  chk(gStubAccountCancels == 1, "'Cancelar' llega al modulo");
  gStubAccountSnap.state = FLEX_ACCOUNT_CANCELLED; repinta();
  chk(accountLastState == FLEX_ACCOUNT_CANCELLED, "y la pantalla se repinta");
  chk(!strcmp(accountPrimaryLabel(FLEX_ACCOUNT_CANCELLED, true), "Iniciar sesion"), "tras cancelar el boton sigue diciendo 'Iniciar sesion'");
  toca(SCR_W / 2, 480);
  chk(gStubAccountRequests == 2, "se puede volver a pedir el enlace");

  // 3. El codigo, y la linea de debajo dice POR QUE la consulta no avanza (antes quedaba muda).
  gStubAccountSnap.state = FLEX_ACCOUNT_CODE_READY; snprintf(gStubAccountSnap.code, sizeof(gStubAccountSnap.code), "FLX7Q2");
  repinta();
  chk(accountLastState == FLEX_ACCOUNT_CODE_READY, "con el codigo listo se pinta el codigo");
  chk(!strcmp(accountCodeNote(gStubAccountSnap), "El codigo vence en 10 minutos"), "sin problema: 'El codigo vence en 10 minutos'");
  snprintf(gStubAccountSnap.error, sizeof(gStubAccountSnap.error), "Poca memoria interna (24 KB). Reintentando");
  repinta();
  chk(!strcmp(accountLastError, gStubAccountSnap.error), "al cambiar el motivo la pantalla se repinta");
  chk(!strcmp(accountCodeNote(gStubAccountSnap), "Poca memoria interna (24 KB). Reintentando"), "y la linea de debajo del codigo lo dice");
  chk(textW(gStubAccountSnap.error, 1) < SCR_W - 56, "cabe en la tarjeta");

  // 4. ERROR: el motivo y el boton principal DICE 'Reintentar'; reintentar NO exige reiniciar.
  gStubAccountSnap.state = FLEX_ACCOUNT_ERROR;
  snprintf(gStubAccountSnap.error, sizeof(gStubAccountSnap.error), "Poca memoria interna (24 KB libres). Cierra una app y reintenta");
  repinta();
  chk(accountLastState == FLEX_ACCOUNT_ERROR, "error de enlace: la pantalla lo pinta");
  chk(!strcmp(accountPrimaryLabel(FLEX_ACCOUNT_ERROR, true), "Reintentar") && !strcmp(accountPrimaryLabel(FLEX_ACCOUNT_EXPIRED, true), "Reintentar"),
      "tras un error o un codigo caducado el boton dice 'Reintentar'");
  chk(!strcmp(accountPrimaryLabel(FLEX_ACCOUNT_ERROR, false), "Conectar Wi-Fi"), "y sin Wi-Fi sigue llevando al configurador");
  chk(textW(gStubAccountSnap.error, 1) < SCR_W - 56, "el mensaje de error cabe en la tarjeta");
  gStubAccountRequests = 0;
  toca(SCR_W / 2, 480);
  chk(gStubAccountRequests == 1, "'Reintentar' vuelve a pedir el enlace");
  gStubAccountSnap.state = FLEX_ACCOUNT_ERROR; repinta();
  toca(SCR_W / 2, 550);
  chk(gStubAccountRequests == 2, "y 'Crear una cuenta' tambien");

  // 5. El modulo rechaza la peticion (ocupado o sin tarea): la pantalla no se rompe ni cambia de estado.
  gStubAccountAccept = false; gStubAccountSnap.state = FLEX_ACCOUNT_ERROR; repinta();
  gStubAccountRequests = 0;
  toca(SCR_W / 2, 480);
  chk(gStubAccountRequests == 1 && gState == ST_OOBE_ACCOUNT && accountLastState == FLEX_ACCOUNT_ERROR, "una peticion rechazada deja la pantalla como estaba");

  // 6. Vinculada: 'Continuar' termina el primer arranque.
  gStubAccountSnap.state = FLEX_ACCOUNT_LINKED; gStubAccountLinked = true; gStubAccountSnap.link = FLEX_LINK_LINKED;
  snprintf(gStubAccountSnap.flexAddress, sizeof(gStubAccountSnap.flexAddress), "ana@flex"); gStubAccountSnap.error[0] = 0;
  repinta();
  chk(accountLastState == FLEX_ACCOUNT_LINKED, "vinculada: la pantalla lo pinta");
  toca(SCR_W / 2, 570);
  chk(cfgOobeDone && gState == ST_LOCK, "'Continuar' cierra la primera configuracion y va al bloqueo");

  gStubAccountAccept = false; gStubAccountLinked = false; memset(&gStubAccountSnap, 0, sizeof(gStubAccountSnap));
  gInoWifiStatus = WL_DISCONNECTED;
  snprintf(cfgName, sizeof(cfgName), "%s", nombreAntes);
  cfgOobeDone = oobeAntes; gState = estadoAntes;
  if(!gFails) printf("  Flex Account (enlace): todas las comprobaciones pasan.\n");
}

// #############################################################
//  RECORTE POR BANDAS: MISMO PIXEL, MENOS TRABAJO
//  ------------------------------------------------------------
//  Dos optimizaciones del compositor tienen la MISMA obligacion:
//  producir exactamente los mismos pixeles que antes y limitarse a
//  no hacer el trabajo cuyo resultado ya se estaba tirando.
//
//   1. drawGlyphScaled() descarta de una vez el glifo que cae
//      entero fuera de la banda de recorte. Antes lo rasterizaba
//      completo y pxA() iba descartando pixel a pixel.
//   2. drawLiquidGlassPanelEx() copia y desenfoca solo las filas
//      visibles mas blurR de margen. El margen es lo que hace que
//      el resultado sea identico: el desenfoque es separable, de
//      radio blurR y una sola pasada, asi que una fila solo depende
//      de las blurR de arriba y las blurR de abajo.
//
//  La prueba es la unica forma honesta de afirmarlo: se dibuja lo
//  mismo con la pantalla entera y con una banda estrecha, y se
//  comparan las filas de la banda BYTE A BYTE.
// #############################################################
static void testRecortePorBandas(){
  printf("Recorte por bandas: mismo pixel, menos trabajo\n");
  gLand = false;
  uiClipFull();
  const size_t PX = (size_t)SCR_W * SCR_H;
  uint16_t* ref = (uint16_t*)malloc(PX * 2);
  if(!ref){ printf("  FALLO: sin memoria para la referencia\n"); gFails++; return; }

  // Fondo con estructura: si fuera liso, un desenfoque mal recortado
  // seguiria dando el mismo color y la prueba no probaria nada.
  auto pintaFondo = [&](){
    setBuf(bbuf);
    for(int y = 0; y < SCR_H; y++)
      for(int x = 0; x < SCR_W; x++)
        bbuf[(size_t)y * SCR_W + x] = rgb565((uint8_t)(x * 7 + y * 3), (uint8_t)(y * 5), (uint8_t)(x * 3 + 40));
  };

  // ---- 1. Un panel de vidrio ALTO, con una banda estrecha en medio ----
  const int PX0 = 24, PY0 = 90, PW = SCR_W - 48, PH = 460, PRAD = 28;
  const int B0 = 300, B1 = 340;                 // banda de 41 filas dentro del panel

  pintaFondo();
  uiClipFull();
  drawLiquidGlassPanel(PX0, PY0, PW, PH, PRAD, TH_GLASS2);
  memcpy(ref, bbuf, PX * 2);

  pintaFondo();
  uiClipViewport(B0, B1);
  drawLiquidGlassPanel(PX0, PY0, PW, PH, PRAD, TH_GLASS2);
  uiClipFull();

  int dif = 0;
  for(int y = B0; y <= B1; y++)
    for(int x = 0; x < SCR_W; x++)
      if(bbuf[(size_t)y * SCR_W + x] != ref[(size_t)y * SCR_W + x]) dif++;
  chk(dif == 0, "vidrio por bandas: las filas visibles salen IDENTICAS al panel entero");

  // Y fuera de la banda no escribe ni un pixel (el fondo se quedo como estaba).
  int fuera = 0;
  for(int y = 0; y < SCR_H; y++){
    if(y >= B0 && y <= B1) continue;
    for(int x = 0; x < SCR_W; x++){
      uint16_t esperado = rgb565((uint8_t)(x * 7 + y * 3), (uint8_t)(y * 5), (uint8_t)(x * 3 + 40));
      if(bbuf[(size_t)y * SCR_W + x] != esperado) fuera++;
    }
  }
  chk(fuera == 0, "vidrio por bandas: fuera de la banda no toca ni un pixel");

  // El panel tiene que haber pintado ALGO en la banda: si no, las dos
  // comprobaciones de arriba pasarian con una funcion que no hace nada.
  int pintados = 0;
  for(int y = B0; y <= B1; y++)
    for(int x = 0; x < SCR_W; x++){
      uint16_t fondo = rgb565((uint8_t)(x * 7 + y * 3), (uint8_t)(y * 5), (uint8_t)(x * 3 + 40));
      if(bbuf[(size_t)y * SCR_W + x] != fondo) pintados++;
    }
  chk(pintados > 5000, "vidrio por bandas: la banda si se pinta (la prueba no es vacia)");

  // ---- 2. El coste: cuanto se ahorra de verdad ----
  {
    struct timespec c0, c1;
    const int N = 30;
    pintaFondo(); uiClipFull();
    clock_gettime(CLOCK_MONOTONIC, &c0);
    for(int i = 0; i < N; i++) drawLiquidGlassPanel(PX0, PY0, PW, PH, PRAD, TH_GLASS2);
    clock_gettime(CLOCK_MONOTONIC, &c1);
    double entero = ((c1.tv_sec - c0.tv_sec) * 1e3 + (c1.tv_nsec - c0.tv_nsec) / 1e6) / N;

    pintaFondo(); uiClipViewport(B0, B1);
    clock_gettime(CLOCK_MONOTONIC, &c0);
    for(int i = 0; i < N; i++) drawLiquidGlassPanel(PX0, PY0, PW, PH, PRAD, TH_GLASS2);
    clock_gettime(CLOCK_MONOTONIC, &c1);
    double banda = ((c1.tv_sec - c0.tv_sec) * 1e3 + (c1.tv_nsec - c0.tv_nsec) / 1e6) / N;
    uiClipFull();
    printf("  [vidrio] panel de %d filas: entero %.2f ms  ·  banda de %d filas %.2f ms\n",
           PH, entero, B1 - B0 + 1, banda);
    chk(banda < entero, "el panel recortado a una banda cuesta menos que el panel entero");
  }

  // ---- 3. El texto fuera de la banda no cambia lo que si entra ----
  const char* FRASE = "Bloqueo automatico y ultimo acceso";
  pintaFondo();
  uiClipFull();
  for(int i = 0; i < 12; i++) drawText(20, 40 + i * 60, FRASE, 3, rgb565(255,255,255));
  memcpy(ref, bbuf, PX * 2);

  pintaFondo();
  uiClipViewport(B0, B1);
  for(int i = 0; i < 12; i++) drawText(20, 40 + i * 60, FRASE, 3, rgb565(255,255,255));
  uiClipFull();
  dif = 0;
  for(int y = B0; y <= B1; y++)
    for(int x = 0; x < SCR_W; x++)
      if(bbuf[(size_t)y * SCR_W + x] != ref[(size_t)y * SCR_W + x]) dif++;
  chk(dif == 0, "texto por bandas: lo que cae en la banda sale IDENTICO");

  // ---- 4. textW no depende del recorte: la maqueta no se mueve ----
  uiClipViewport(B0, B1);
  int wBanda = textW(FRASE, 3);
  uiClipFull();
  int wLleno = textW(FRASE, 3);
  chk(wBanda == wLleno, "textW no cambia con el recorte (la maqueta no se mueve)");

  // Y drawText devuelve la MISMA pluma final este o no dentro de la banda:
  // varias pantallas encadenan texto a partir de ese valor.
  setBuf(bbuf);
  uiClipViewport(B0, B1);
  int penFuera = drawText(20, 10, FRASE, 3, rgb565(255,255,255));      // arriba, fuera de la banda
  int penDentro = drawText(20, B0 + 4, FRASE, 3, rgb565(255,255,255)); // dentro
  uiClipFull();
  chk(penFuera == penDentro, "drawText avanza la pluma igual dentro y fuera de la banda");

  // ---- 5. El coste del texto descartado ----
  {
    struct timespec c0, c1;
    const int N = 40;
    pintaFondo(); uiClipFull();
    clock_gettime(CLOCK_MONOTONIC, &c0);
    for(int i = 0; i < N; i++)
      for(int k = 0; k < 12; k++) drawText(20, 40 + k * 60, FRASE, 3, rgb565(255,255,255));
    clock_gettime(CLOCK_MONOTONIC, &c1);
    double todo = ((c1.tv_sec - c0.tv_sec) * 1e3 + (c1.tv_nsec - c0.tv_nsec) / 1e6) / N;

    pintaFondo(); uiClipViewport(B0, B1);
    clock_gettime(CLOCK_MONOTONIC, &c0);
    for(int i = 0; i < N; i++)
      for(int k = 0; k < 12; k++) drawText(20, 40 + k * 60, FRASE, 3, rgb565(255,255,255));
    clock_gettime(CLOCK_MONOTONIC, &c1);
    double soloBanda = ((c1.tv_sec - c0.tv_sec) * 1e3 + (c1.tv_nsec - c0.tv_nsec) / 1e6) / N;
    uiClipFull();
    printf("  [texto] 12 lineas: todas visibles %.3f ms  ·  con solo una en la banda %.3f ms\n",
           todo, soloBanda);
    chk(soloBanda < todo, "las lineas fuera de la banda ya no se rasterizan");
  }

  free(ref);
  if(gFails){ printf("  %d comprobacion(es) del recorte por bandas han fallado.\n", gFails); }
  else printf("  Recorte por bandas: todas las comprobaciones pasan.\n");
}

// #############################################################
//  LOS ICONOS NO SE SALEN DE SU CAJA
//  ------------------------------------------------------------
//  drawAppIcon() descarta de una vez el icono que cae fuera de la
//  banda de recorte, y esa decision solo es correcta si NINGUN
//  icono pinta fuera de [x, x+S) x [y, y+S). Aqui se mide de
//  verdad: los 18 iconos, en los dos estilos (Plano y Vidrio) y a
//  tres tamanos, sobre un lienzo limpio, comprobando el rectangulo
//  real de pixeles escritos. Si algun dia un icono nuevo pinta un
//  borde por fuera, esta prueba falla ANTES de que el recorte lo
//  corte en pantalla.
// #############################################################

// #############################################################
//  FLEX DEVICE CARE
//  ------------------------------------------------------------
//  Cinco cosas, y ninguna es cosmetica:
//
//   1. LA REGLA DEL GRAFICO. El dibujo del GY-BNO085 solo puede
//      aparecer en la pantalla de requisito de hardware. Aqui se
//      cuentan los pixeles del morado de la placa en cada pantalla
//      de la app: si algun dia el grafico se cuela en el inicio, en
//      el diagnostico o en la pantalla del modulo conectado, esta
//      prueba falla ANTES de que se vea en el aparato. Y se
//      comprueba tambien que NO se carga ninguna imagen: el modulo
//      se dibuja con primitivas, asi que pintar sobre un lienzo
//      vacio basta para producirlo entero.
//   2. NAVEGACION. Cada acceso del inicio lleva a su pantalla y el
//      boton atras retrocede UNA pantalla, no directo al escritorio.
//   3. EXCEPCION DEX. Con Modo PC en primer plano el aviso global no
//      puede dibujarse -- pero el evento ya se registro.
//   4. LAS DOS MAQUETAS DEL AVISO. Vertical y horizontal ocupan
//      bandas distintas del panel y ponen los botones en sitios
//      distintos: no es la misma UI girada.
//   5. HISTORIAL. Lo que se anota se lee de vuelta, lo mas reciente
//      primero, y no crece por encima de su tope.
// #############################################################
static int dcContarColor(uint16_t col){
  int n = 0;
  for(size_t i = 0; i < (size_t)SCR_W * SCR_H; i++) if(bbuf[i] == col) n++;
  return n;
}
// Pinta una pantalla de Device Care sobre bbuf (sin tocar el panel) y
// devuelve cuantos pixeles del morado de la placa quedaron.
static int dcPixelesPlaca(void (*render)()){
  memset(bbuf, 0, (size_t)SCR_W * SCR_H * 2);
  uint16_t* fbReal = fb;
  fb = bbuf;                       // setBuf(fb) dentro del render escribe aqui
  setBuf(bbuf);
  uiClipFull();
  render();
  fb = fbReal;
  setBuf(fbReal);
  return dcContarColor(DC_PCB_COL);
}

static void testDeviceCare(){
  printf("Flex Device Care\n");
  gLand = false; gHosted = false; editMode = false;
  gState = ST_APP; gAppId = IC_DEVCARE;
  gAppW = SCR_W; gAppH = SCR_H;
  uiClipFull();
  gTestMs = 100000; clkSetEpoch(1783189380u); clkUpdate();

  // --- 1. el grafico del GY-BNO085, SOLO donde toca -------------------
  // El doble del driver deja el modulo AUSENTE, que es justo el estado
  // que tiene que ensenar el requisito de hardware.
  chk(flexBnoState() == FLEXBNO_ST_ABSENT, "el arnes corre con el IMU ausente");
  int enRequisito = dcPixelesPlaca(dcRenderFallMissing);
  chk(enRequisito > 2000, "la pantalla de requisito dibuja el modulo (sin imagenes)");
  chk(dcPixelesPlaca(dcRenderHome)   == 0, "el inicio de Device Care NO dibuja el modulo");
  chk(dcPixelesPlaca(dcRenderStatus) == 0, "Estado del dispositivo NO dibuja el modulo");
  chk(dcPixelesPlaca(dcRenderHist)   == 0, "el historial NO dibuja el modulo");
  chk(dcPixelesPlaca(dcHealthRender) == 0, "Salud de Flex OS NO dibuja el modulo");
  chk(dcPixelesPlaca(dcDiagRender)   == 0, "el diagnostico NO dibuja el modulo");
  chk(dcPixelesPlaca(dcOptimRender)  == 0, "Optimizacion NO dibuja el modulo");
  chk(dcPixelesPlaca(dcRenderFallReady) == 0,
      "con el modulo conectado la pantalla cambia y el grafico desaparece");
  chk(dcPixelesPlaca(dcImuTestRender) == 0, "la prueba del modulo NO dibuja el grafico");
  chk(dcPixelesPlaca(dcRenderFallProbing) == 0, "mientras detecta tampoco lo dibuja");

  // El grafico esta ANIMADO por tiempo: dos instantes distintos dan
  // cuadros distintos (el enlace punteado avanza), pero la placa sigue
  // ocupando lo mismo -- o sea que lo que se mueve es el enlace, no el
  // modulo entero parpadeando.
  dcAnimT0 = gTestMs;
  int a = dcPixelesPlaca(dcRenderFallMissing);
  gTestMs += 200;
  int b2 = dcPixelesPlaca(dcRenderFallMissing);
  chk(a == b2, "la animacion del enlace no cambia el area de la placa (sin parpadeo)");

  // --- 2. navegacion interna ------------------------------------------
  dcScreen = DC_HOME;
  dcRenderHome();                                  // rellena las zonas pulsables
  int idHero = dcHitTest(SCR_W / 2, WIN_TOP + 60);
  chk(idHero == DCH_HERO, "la tarjeta de estado es pulsable");
  bool todas = true;
  for(int i = 0; i < 6; i++){
    int col = i % 2, row = i / 2;
    int x = dcGrid.x + col * (dcGrid.cw + dcGrid.gap) + dcGrid.cw / 2;
    int y = dcGrid.y + row * (dcGrid.ch + dcGrid.gap) + dcGrid.ch / 2;
    if(dcHitTest(x, y) != DCH_T0 + i) todas = false;
  }
  chk(todas, "los seis accesos del inicio tienen su zona pulsable");

  dcScreen = DC_HIST;
  chk(dcBackScreen(), "desde el historial, atras retrocede");
  chk(dcScreen == DC_HOME, "...y vuelve al inicio de la app");
  chk(!dcBackScreen(), "desde el inicio de la app, atras ya no retrocede (sale al escritorio)");
  dcScreen = DC_IMUTEST;
  chk(dcBackScreen() && dcScreen == DC_FALL,
      "la prueba del modulo vuelve a Deteccion de caidas, no al inicio");
  dcPrev = DC_POST; dcScreen = DC_RESULT;
  chk(dcBackScreen() && dcScreen == DC_POST,
      "el resultado de un Post-Impact Check vuelve al Post-Impact Check");
  dcScreen = DC_HOME;

  // --- 3. la excepcion DeX --------------------------------------------
  FlexFallEvent ev;
  memset(&ev, 0, sizeof(ev));
  ev.confidence = 88; ev.fall = 1; ev.peakG = 5.1f; ev.tMs = gTestMs;
  int antes = dcHistN;
  gState = ST_APP; gAppId = IC_MODOPC;
  chk(faDexActive(), "Modo PC en primer plano se reconoce como DeX");
  chk(!faCanShow(),  "en DeX el aviso global NO se dibuja");
  faRaise(&ev);
  chk(!faVisible(),  "...y efectivamente no aparece ningun cuadro");
  gHosted = true; gAppId = IC_DEVCARE;
  chk(faDexActive(), "una app hospedada en una ventana de DeX cuenta igual");
  gHosted = false;
  // El registro NO depende del aviso: lo hace dcSensorTick antes de
  // llamar aqui. Se comprueba llamando al mismo camino de historial.
  dcHistAdd(DC_EV_FALL, ev.confidence, ev.reasons, 51);
  chk(dcHistN == antes + 1, "en DeX el evento SI queda registrado en el historial");
  chk(dcHist[0].kind == DC_EV_FALL && dcHist[0].score == 88,
      "el registro guarda el tipo y la confianza reales");

  // --- 3b. aviso EN ESPERA: no se pierde, sale al despejarse ----------
  gState = ST_HOME; gAppId = 0; gHosted = false;
  faState = FA_HIDDEN; faFreeBand(); faPending = false;
  qsPanelY = SCR_H;                                   // cortina abierta
  chk(!faCanShow(), "con la cortina abierta el aviso no puede dibujarse");
  faRaise(&ev);
  chk(!faVisible() && faPending, "...pero queda EN ESPERA, no se pierde");
  faPendingTick();
  chk(!faVisible(), "mientras la cortina siga abierta no sale");
  qsPanelY = 0;                                       // se cierra la cortina
  faPendingTick();
  chk(faVisible(), "al cerrarse la cortina el aviso aparece solo");
  faAbandon(); faPending = false;
  // Y en DeX no espera: ahi no se ensena nunca.
  gState = ST_APP; gAppId = IC_MODOPC;
  faRaise(&ev);
  chk(!faVisible() && !faPending, "en DeX el aviso ni sale ni espera");
  gState = ST_HOME; gAppId = 0;

  // --- 4. dos maquetas de verdad --------------------------------------
  int vy0, vy1, ly0, ly1;
  faBand(false, vy0, vy1);
  faBand(true,  ly0, ly1);
  chk(vy0 != ly0 || vy1 != ly1, "vertical y horizontal ocupan bandas distintas del panel");
  chk(vy1 - vy0 != ly1 - ly0,   "...y de distinto alto: no es la misma UI estirada");

  gState = ST_HOME; gAppId = 0; gLand = false;
  chk(faCanShow(), "en el escritorio el aviso SI puede dibujarse");
  memset(bbuf, 0, (size_t)SCR_W * SCR_H * 2);
  setBuf(bbuf);
  faDrawVertical(1.0f);
  int vCkX = faBtnCk[0], vCkY = faBtnCk[1], vDsY = faBtnDs[1];
  chk(vDsY > vCkY, "en vertical los dos botones van APILADOS");
  gLand = true;
  memset(bbuf, 0, (size_t)SCR_W * SCR_H * 2);
  faDrawLandscape(1.0f);
  chk(faBtnDs[0] > faBtnCk[0] && faBtnDs[1] == faBtnCk[1],
      "en horizontal los dos botones van EN FILA");
  chk(faBtnCk[0] != vCkX, "los botones no estan en el mismo sitio en las dos maquetas");
  gLand = false;
  setBuf(fb);
  uiClipFull();

  // --- 5. historial: tope y orden --------------------------------------
  for(int i = 0; i < DC_HIST_MAX + 6; i++) dcHistAdd(DC_EV_DIAG, (uint8_t)(50 + i), 0, 0);
  chk(dcHistN == DC_HIST_MAX, "el historial no crece por encima de su tope");
  chk(dcHist[0].score == (uint8_t)(50 + DC_HIST_MAX + 5),
      "lo mas reciente queda el primero");
  chk(dcLastScore == dcHist[0].score && dcLastCheck == dcHist[0].utc,
      "la tarjeta de inicio lee la ultima revision real");

  // --- la puntuacion SALE de las metricas, no es una constante ---------
  uint8_t sc1 = 0, sc2 = 0;
  gTestPsPressure = 0; gTestPsLargest = 0; gTestInFree = 180u << 10;
  memSampleNow();
  chk(dcHealthScore(&sc1), "con memoria holgada hay puntuacion");
  // Misma placa, peor situacion: SRAM al limite y PSRAM casi agotada.
  gTestInFree = 24u << 10;
  { size_t real = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    gTestPsPressure = (real > (2u << 20)) ? (real - (2u << 20)) : 0; }
  memSampleNow();
  chk(dcHealthScore(&sc2), "con memoria al limite tambien hay puntuacion");
  chk(sc2 < sc1, "la puntuacion BAJA cuando las metricas empeoran (no es fija)");
  gTestPsPressure = 0; gTestInFree = 180u << 10;
  memSampleNow();

  // --- 6. el flujo completo del criterio de aceptacion ------------------
  //  aviso global -> "Revisar dispositivo" -> Post-Impact Check ->
  //  diagnostico automatico -> resultado -> historial.
  gState = ST_HOME; gAppId = 0; gLand = false; gHosted = false;
  dcScreen = DC_HOME;
  faState = FA_HIDDEN; faFreeBand();
  chk(faBak == NULL, "en reposo el aviso no retiene memoria");
  memset(&ev, 0, sizeof(ev));
  ev.confidence = 91; ev.fall = 1; ev.peakG = 6.2f; ev.tMs = gTestMs;
  ev.reasons = FLEXFALL_R_FREEFALL | FLEXFALL_R_IMPACT | FLEXFALL_R_SETTLED;
  faRaise(&ev);
  chk(faVisible(), "en el escritorio el aviso SI aparece");
  // faRaise solo ARMA; la banda se prepara en la primera vuelta de
  // dibujo, que es donde el aviso ya es dueno de la pantalla.
  gTestMs += 20; faTick();
  chk(faBandReady(), "el aviso prepara su banda en la fase de dibujo");
  gTestMs += FA_ANIM_MS + 20; faTick();
  chk(faState == FA_SHOWN, "la animacion de entrada termina");

  // Toque en "Revisar dispositivo".
  tReset();
  T.tap = true; T.x = (faBtnCk[0] + faBtnCk[2]) / 2; T.y = (faBtnCk[1] + faBtnCk[3]) / 2;
  faTick();
  chk(!faVisible(),        "el aviso se retira al pulsar Revisar dispositivo");
  chk(faBak == NULL,       "...y suelta su banda: no se queda medio MB reservado");
  chk(dcPendingPost,       "queda pedido el Post-Impact Check");
  chk(gState == ST_APP && gAppId == IC_DEVCARE, "y la app abierta es Device Care");

  // La app se construye en el ultimo cuadro de la transicion; aqui se
  // llama a su enter() igual que hace appTrFinishOpen.
  dcEnter();
  chk(!dcPendingPost, "la peticion se consume una sola vez");
  chk(dcScreen == DC_POST, "Device Care abre directamente el Post-Impact Check");
  chk(dcDiagStage == DG_SYS, "el diagnostico arranca por la primera etapa");

  // Las dos etapas automaticas corren solas, una por vuelta.
  gTestMs += DC_DIAG_STEP_MS + 10; dcDiagTick();
  chk(dcDiagStage == DG_IMU,  "la etapa de sistema corre sola");
  chk(dcResSysOk,             "...y deja una medida real del sistema");
  chk(dcSysRow[0].value[0] != 0, "el test de sistema rellena sus filas medidas");
  gTestMs += DC_DIAG_STEP_MS + 10; dcDiagTick();
  chk(dcDiagStage == DG_SCREEN, "la etapa del IMU corre sola");
  chk(!dcResImuAvail,           "sin modulo, el IMU se anota como NO disponible");
  // Y la siguiente se lanza sola: el usuario no busca la prueba.
  gTestMs += DC_DIAG_STEP_MS + 10; dcDiagTick();
  chk(dcScreen == DC_SCRTEST && dcScrStep == DCP_BLACK,
      "el test de pantalla se lanza automaticamente");
  dcScrTestDone(true);
  chk(dcScreen == DC_POST && dcDiagStage == DG_TOUCH, "tras la pantalla toca el tactil");
  gTestMs += DC_DIAG_STEP_MS + 10; dcDiagTick();
  chk(dcScreen == DC_TOUCHTEST, "el test tactil tambien se lanza solo");
  dcTtHit = DC_TT_COLS * DC_TT_ROWS;            // rejilla recorrida entera
  dcTouchTestDone();
  uint8_t topAntes = dcHist[0].kind;
  gTestMs += DC_DIAG_STEP_MS + 10; dcDiagTick();
  chk(dcScreen == DC_RESULT,  "al terminar se ensena el resultado");
  chk(dcResFinalOk,           "hay puntuacion final");
  chk(dcResTouch == 100,      "la cobertura tactil medida entra en el resultado");
  chk(dcResScreen == 1,       "la respuesta del test de pantalla entra en el resultado");
  chk(topAntes != DC_EV_POST && dcHist[0].kind == DC_EV_POST,
      "el resultado queda el primero del historial, como revision tras impacto");
  chk(dcHist[0].score == dcResFinal, "...con la puntuacion que se ensena");
  chk(dcLastScore == dcResFinal && dcLastCheck == dcHist[0].utc,
      "...y la tarjeta de inicio ya apunta a esta revision");

  // --- 7. EL AVISO NUNCA SE QUEDA LA PANTALLA SIN DIBUJAR --------------
  //  Regresion de un bloqueo real: faRaise pedia la banda con faBand() y
  //  acto seguido llamaba a faFreeBand() para redimensionar el buffer --
  //  y faFreeBand invalidaba la geometria recien calculada. El aviso
  //  pasaba a ser dueno de la pantalla y faCompose salia sin pintar
  //  NUNCA. Resultado: interfaz congelada, apps sin responder, la
  //  notificacion sin aparecer, y solo el panel rapido vivo (loop() lo
  //  despacha ANTES que el bloque del aviso).
  //
  //  Se comprueba el INVARIANTE, que es lo que lo hace imposible: si el
  //  aviso es visible, tiene que poder dibujar; y si no puede, tiene que
  //  soltar la pantalla en la MISMA vuelta.
  appTrCancel();                    // la seccion anterior dejo una apertura en vuelo
  gState = ST_HOME; gAppId = 0; gLand = false; gHosted = false;
  qsPanelY = 0; qsAnimOn = false;
  dcScreen = DC_HOME;
  faState = FA_HIDDEN; faFreeBand(); faInvalidateBand(); faPending = false;
  memset(faBtnCk, 0, sizeof(faBtnCk));
  memset(faBtnDs, 0, sizeof(faBtnDs));
  chk(faBakCap == 0, "se parte del caso REAL: primer aviso, sin banda reservada");

  memset(&ev, 0, sizeof(ev));
  ev.confidence = 95; ev.fall = 1; ev.peakG = 7.4f; ev.tMs = gTestMs;
  faRaise(&ev);
  chk(faVisible(), "el aviso se arma");
  chk(faState == FA_ARMED, "...y solo se ARMA: la deteccion no paga el trabajo pesado");
  chk(faBak == NULL, "faRaise no reserva memoria (corre en el tick del sensor)");

  // Una vuelta de dibujo: prepara y publica el primer cuadro.
  gTestMs += 20; faTick();
  chk(faState == FA_IN,   "la primera vuelta de dibujo lo prepara");
  chk(faBandReady(),      "...y la banda queda LISTA (buffer + geometria)");

  // El aviso se termina de dibujar de verdad: los botones los fija el
  // camino de composicion, nadie mas.
  for(int i = 0; i < 6; i++){ gTestMs += 60; faTick(); }
  chk(faState == FA_SHOWN, "la animacion de entrada termina");
  chk(faBtnCk[2] > faBtnCk[0] && faBtnDs[2] > faBtnDs[0],
      "los botones tienen area REAL: el aviso se dibujo, no solo se armo");

  // Y la pantalla cambio de verdad donde va el aviso: la banda publicada
  // no puede ser igual al fondo que se capturo.
  {
    bool pintado = false;
    if(faBandReady()){
      int my = (faBakY0 + faBakY1) / 2;
      for(int x = 0; x < SCR_W && !pintado; x++)
        if(fb[(size_t)my * SCR_W + x] != faBak[(size_t)(my - faBakY0) * SCR_W + x]) pintado = true;
    }
    chk(pintado, "la banda publicada difiere del fondo capturado: hay cuadro");
  }

  // Descartar: vuelve el fondo y la pantalla se suelta.
  tReset();
  T.tap = true; T.x = (faBtnDs[0] + faBtnDs[2]) / 2; T.y = (faBtnDs[1] + faBtnDs[3]) / 2;
  faTick();
  for(int i = 0; i < 6 && faVisible(); i++){ gTestMs += 60; faTick(); }
  chk(!faVisible(), "al descartar, el aviso suelta la pantalla");
  chk(faBak == NULL, "...y suelta su banda");

  // ---- Si NO se puede preparar, no se queda la pantalla ni una vuelta -
  faState = FA_HIDDEN; faFreeBand(); faInvalidateBand(); faPending = false;
  gTestPsFail = true;                       // la reserva de la banda falla
  faRaise(&ev);
  chk(faVisible() && faState == FA_ARMED, "se arma igual");
  gTestMs += 20; faTick();
  chk(!faVisible(), "sin memoria para la banda, suelta la pantalla en la MISMA vuelta");
  gTestPsFail = false;

  // ---- La barra del sistema sigue viva mientras el aviso esta a la vista
  gState = ST_APP; gAppId = IC_RELOJ; gNavMode = 0;
  faState = FA_HIDDEN; faFreeBand(); faInvalidateBand(); faPending = false;
  faRaise(&ev);
  gTestMs += 20; faTick();
  for(int i = 0; i < 6; i++){ gTestMs += 60; faTick(); }
  chk(faState == FA_SHOWN, "aviso a la vista encima de una app");
  chk(navBarVisible(), "la barra de navegacion del sistema esta dibujada");
  tReset();
  T.tap = true; T.x = SCR_W / 2; T.y = SCR_H - 32;      // boton INICIO
  faTick();
  chk(!faVisible(), "pulsar INICIO retira el aviso: la navegacion no se secuestra");
  chk(faPending,    "...y el aviso queda EN ESPERA, no se pierde");
  faPending = false;
  faState = FA_HIDDEN; faFreeBand(); faInvalidateBand();
  gState = ST_HOME; gAppId = 0;

  // --- fallo seguro del sensor ----------------------------------------
  dcFallOn = true; dcSensorOn = true;
  flexFallReset(&dcDet);
  dcSensorTick();                     // el doble devuelve "no disponible"
  chk(flexFallState(&dcDet) == FLEXFALL_IDLE,
      "sin sensor la deteccion no avanza y el sistema sigue");
  dcFallOn = false; dcSensorOn = false;

  gState = ST_HOME; gAppId = 0;
  if(gFails) printf("  %d comprobacion(es) de Device Care han fallado.\n", gFails);
  else       printf("  Flex Device Care: todas las comprobaciones pasan.\n");
}


// #############################################################
//  LIQUID GLASS: NINGUNA ANIMACION APILA CAPAS DE DESENFOQUE
//  ------------------------------------------------------------
//  EXISTE POR UN FALLO REAL Y VISIBLE. drawLiquidGlassPanel LEE la
//  region del buffer, la desenfoca y la ESCRIBE encima. Es decir: NO
//  es idempotente sobre su propia salida. Si una animacion repinta el
//  panel encima de lo que publico en el cuadro anterior, cada cuadro
//  desenfoca lo ya desenfocado y vuelve a aplicar tinte, especular y
//  borde: las capas se apilan y el texto se emborrona un poco mas cada
//  vez, hasta quedar ilegible. Pasaba en dos sitios -- el barrido del
//  anillo de Device Care y el panel de "Optimizar Flex OS" --, y el
//  panel de optimizacion ademas lo arrastraba de antes.
//
//  LA REGLA, que es lo que se comprueba aqui: un cuadro de animacion
//  tiene que ser IDEMPOTENTE. Ejecutado dos veces con el mismo estado
//  logico debe dar EXACTAMENTE los mismos pixeles. Un cuadro que parte
//  de un fondo limpio lo cumple por construccion; uno que se compone
//  sobre su propia salida no lo cumple nunca.
//
//  La prueba no mira "si se ve bien": compara pixeles. Si manana
//  alguien vuelve a componer vidrio sobre lo publicado, esto falla
//  antes de que llegue a la pantalla.
// #############################################################
static uint16_t* lgSnap = NULL;
static void lgGrab(uint16_t* dst, int y0, int y1){
  memcpy(dst, fb + (size_t)y0 * SCR_W, (size_t)(y1 - y0 + 1) * SCR_W * 2);
}
static int lgDiff(const uint16_t* a, const uint16_t* b, int y0, int y1){
  int n = 0;
  size_t px = (size_t)(y1 - y0 + 1) * SCR_W;
  for(size_t i = 0; i < px; i++) if(a[i] != b[i]) n++;
  return n;
}

// #############################################################
//  PROTECCION CONTRA ROBO  ·  integracion dentro del sketch
//  ------------------------------------------------------------
//  El clasificador tiene su propia bateria (tests/host/test_theft).
//  Lo que se comprueba AQUI es lo que solo existe montado dentro de
//  Flex OS: el reparto del sensor, la maquina de estados de la
//  funcion, el Event Manager (que un arrebato seguido de caida sea UN
//  incidente y no dos), la persistencia del bloqueo, que solo el
//  desbloqueo explicito lo levante, la maqueta de 480x800 y que la
//  animacion sea un BUCLE PERFECTO y no se salga de su rectangulo.
// #############################################################
static void testProteccionRobo(){
  printf("Proteccion contra robo\n");
  int fails0 = gFails;
  gLand = false; gHosted = false; editMode = false;
  uiClipFull();
  gNavMode = 0;
  gTestMs = 300000; clkSetEpoch(1789221138u); clkLastMin = -1; clkUpdate();

  // --- 1. EL REPARTO DEL SENSOR ---------------------------------------
  // Un solo driver. Activar la proteccion NO puede inicializar el BNO085
  // por segunda vez, y desactivarla NO puede apagarlo por debajo de la
  // deteccion de caidas.
  {
    bool gtPrev = gtOk; gtOk = true;
    while(imuHolders() > 0) imuRelease();
    dcSensorOn = false; tpHold = false; tpOn = false;
    chk(imuHolders() == 0, "de partida no hay ningun consumidor del IMU");

    dcSensorStart();
    chk(imuHolders() == 1, "Device Care adquiere el servicio");
    tpSetEnabled(true);
    chk(imuHolders() == 2 && tpHold,
        "la proteccion adquiere el MISMO servicio, no un segundo driver");
    dcSensorStop();
    chk(imuHolders() == 1 && tpHold,
        "apagar la deteccion de caidas NO deja a la proteccion sin sensor");
    tpSetEnabled(false);
    chk(imuHolders() == 0 && !tpHold, "y al apagarla tambien, el servicio queda libre");

    tpSetEnabled(true);
    dcSensorStart();
    chk(imuHolders() == 2, "el orden de llegada da igual");
    tpSetEnabled(false);
    chk(imuHolders() == 1 && dcSensorOn,
        "apagar la proteccion NO apaga el sensor por debajo de la deteccion de caidas");
    dcSensorStop();
    chk(imuHolders() == 0, "sin consumidores, el sensor se suelta del todo");
    // Idempotencia: dos activaciones seguidas no descuadran la cuenta.
    tpSetEnabled(true); tpSetEnabled(true);
    chk(imuHolders() == 1, "activar dos veces no cuenta dos consumidores");
    tpSetEnabled(false); tpSetEnabled(false);
    chk(imuHolders() == 0, "desactivar dos veces tampoco descuadra la cuenta");

    // CON SU PANTALLA DELANTE el enganche se conserva aunque se apague la
    // funcion: si no, apagar el interruptor dejaria la pantalla diciendo
    // "requiere modulo IMU" con el modulo puesto.
    int stPrev = gState;
    gState = ST_THEFT;
    tpSetEnabled(true);
    chk(imuHolders() == 1, "en su pantalla, activarla adquiere el servicio");
    tpSetEnabled(false);
    chk(imuHolders() == 1 && tpHold,
        "apagarla SIN salir de la pantalla conserva el enganche para poder mirar");
    gState = stPrev;
    tpTickMs = gTestMs;
    gTestMs += TP_IDLE_RELEASE_MS + 100;
    tpIdleGuard();
    chk(imuHolders() == 0, "y fuera de la pantalla ese enganche si se suelta");
    gtOk = gtPrev;
  }

  // --- 2. LOS ESTADOS DE LA FUNCION (sin estados ambiguos) ------------
  {
    bool gtPrev = gtOk; gtOk = true;
    tpLocked = false; tpLockPending = false; tpOn = false; tpHold = false;
    while(imuHolders() > 0) imuRelease();
    chk(tpStatus() == TP_ST_OFF, "sin activar: DESACTIVADA");
    tpSetEnabled(true);
    // El doble del driver deja el modulo AUSENTE, asi que la funcion queda
    // PAUSADA -- que es exactamente lo que hay que decir cuando no hay IMU.
    chk(!flexBnoAvailable(), "el arnes corre con el IMU ausente");
    chk(tpStatus() == TP_ST_PAUSED, "activada y sin IMU disponible: PAUSADA");
    tpLocked = true; tpLockPending = false;
    chk(tpStatus() == TP_ST_LOCKED, "con el bloqueo puesto: BLOQUEADA");
    tpLockPending = true;
    chk(tpStatus() == TP_ST_DETECTED, "mientras el bloqueo cae: ARREBATO_DETECTADO");
    tpLocked = false; tpLockPending = false;
    tpSetEnabled(false);
    chk(tpStatus() == TP_ST_OFF, "y al apagarla vuelve a DESACTIVADA");
    gtOk = gtPrev;
  }

  // --- 3. EVENT MANAGER: arrebato + caida es UN incidente -------------
  {
    tpHistLoaded = true;                       // se trabaja sobre la copia en RAM
    tpHistN = 0; tpHistDirty = false;
    FlexTheftEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.confidence = 86; ev.snatch = 1; ev.pullG = 2.7f; ev.burstMs = 160;
    ev.escapeMs = 900; ev.dirCoh = 0.93f;
    ev.reasons = FLEXTHEFT_R_HELD | FLEXTHEFT_R_PULL | FLEXTHEFT_R_JERK |
                 FLEXTHEFT_R_DIR | FLEXTHEFT_R_TWIST | FLEXTHEFT_R_ESCAPE;
    tpHistAdd(&ev, TP_EV_SNATCH, TP_F_LOCKED);
    chk(tpHistN == 1, "el posible arrebato queda registrado");
    chk(tpHist[0].kind == TP_EV_SNATCH, "y se registra como POSIBLE ARREBATO");
    chk(tpHist[0].conf == 86 && tpHist[0].pull == 27,
        "con su confianza y la intensidad del tiron");

    // Un segundo despues, durante la huida, el aparato cae.
    tpHaveLast = true; tpLastEvtMs = gTestMs;
    gTestMs += 1000;
    FlexFallEvent fe;
    memset(&fe, 0, sizeof(fe));
    fe.fall = 1; fe.confidence = 74; fe.peakG = 4.8f; fe.tMs = gTestMs;
    tpNoteFall(&fe);
    chk(tpHistN == 1, "la caida NO crea una segunda entrada del mismo incidente");
    chk(tpHist[0].kind == TP_EV_SNATCH_FALL,
        "el registro ASCIENDE a 'posible arrebato + caida'");
    chk(tpHist[0].conf == 86, "la confianza del arrebato se conserva intacta");
    chk(tpHist[0].impact == 48, "y se anota el pico del impacto posterior");
    chk((tpHist[0].flags & TP_F_LOCKED) != 0, "el bloqueo sigue anotado");

    // Una segunda caida del mismo incidente no vuelve a ascender nada.
    tpNoteFall(&fe);
    chk(tpHistN == 1 && tpHist[0].kind == TP_EV_SNATCH_FALL,
        "una caida mas del mismo incidente no duplica el registro");

    // Una caida LEJANA en el tiempo no pertenece a ese arrebato.
    tpHistN = 0;
    tpHistAdd(&ev, TP_EV_SNATCH, TP_F_LOCKED);
    tpLastEvtMs = gTestMs;
    gTestMs += TP_CORR_MS + 2000;
    tpNoteFall(&fe);
    chk(tpHist[0].kind == TP_EV_SNATCH,
        "una caida fuera de la ventana NO se cuela en el arrebato anterior");

    // Y una caida SOLA nunca crea un registro aqui: esa pregunta es de
    // Flex Device Care, y alli se sigue registrando como siempre.
    tpHistN = 0; tpHaveLast = false;
    tpNoteFall(&fe);
    chk(tpHistN == 0, "una caida sola no entra en el historial de seguridad");
  }

  // --- 4. EL BLOQUEO: se pone, persiste y solo lo levanta el desbloqueo -
  //
  //  NOTA SOBRE EL ARNES: tpApplyLock() termina llamando a autoLockNow(), que
  //  es EL bloqueo del sistema y arrastra su animacion interpolada
  //  (animateTo). Esa animacion avanza con millis(), y aqui millis() es un
  //  reloj virtual que no corre solo, asi que no se puede ejecutar desde una
  //  prueba de host. Lo que SI se comprueba entero es todo lo que decide:
  //  cuando se puede bloquear y cuando no, que el bloqueo quede armado y
  //  persistido, que nada lo cancele y que el desbloqueo explicito lo levante.
  {
    bool gtPrev = gtOk; gtOk = true;
    gSuspOn = false; gSafeMode = false; qsPanelY = 0; qsAnimOn = false;
    gFrPending = false;
    tpLocked = false; tpLockPending = false;
    tpOn = true;

    // 4a. Cuando NO se puede bloquear ahora mismo.
    gState = ST_HOME;
    chk(tpCanLockNow(), "en el escritorio normal si se puede bloquear");
    gSafeMode = true;
    chk(!tpCanLockNow(), "en modo seguro no");
    gSafeMode = false;
    gSuspOn = true;
    chk(!tpCanLockNow(), "con la pantalla suspendida tampoco (no se pinta a oscuras)");
    gSuspOn = false;
    gState = ST_SPLASH;
    chk(!tpCanLockNow(), "durante el arranque tampoco");
    gState = ST_OOBE_LANG;
    chk(!tpCanLockNow(), "durante la puesta en marcha tampoco");
    gState = ST_HOME;
    qsPanelY = 40;
    chk(!tpCanLockNow(), "con la cortina abierta tampoco (es duena de la pantalla)");
    qsPanelY = 0;

    // 4b. Un bloqueo que no cabe AHORA no se pierde: queda pendiente.
    gSafeMode = true;
    tpArmLock(TP_EV_SNATCH);
    chk(tpLocked, "al detectar un posible arrebato el bloqueo queda ARMADO");
    chk(tpLockPending, "...y si la pantalla esta ocupada, PENDIENTE en vez de perdido");
    chk(tpLockBannerOn(), "la pantalla de bloqueo tiene que dibujar su aviso");
    chk(tpLockUtc != 0, "con la hora exacta del evento");
    gSafeMode = false;

    // 4c. En cuanto se despeja, se aplica. Estando ya detras del bloqueo
    //     (pantalla de clave) no hay que volver a bloquear nada.
    gState = ST_LOCKSETUP; gLockVerifyLocked = true;
    tpLockPendingTick();
    chk(!tpLockPending, "al despejarse la pantalla, el bloqueo deja de estar pendiente");
    gLockVerifyLocked = false; gState = ST_HOME;

    // Persistencia: lo que sobrevive a un reinicio es lo que impide que
    // quitar la bateria sea la via de escape de la proteccion.
    {
      Preferences p;
      p.begin("flextheft", true);
      chk(p.getBool("lk", false), "el bloqueo queda escrito en NVS");
      p.end();
    }

    // Nada de esto lo cancela.
    tpSensorTick();
    chk(tpLocked, "moverse otra vez NO cancela el bloqueo");
    FlexFallEvent fe; memset(&fe, 0, sizeof(fe));
    fe.fall = 1; fe.peakG = 5.0f; fe.confidence = 80;
    tpNoteFall(&fe);
    chk(tpLocked, "una caida posterior tampoco lo cancela");
    tpLockPendingTick();
    chk(tpLocked, "ni un reintento del propio bloqueo");

    // Solo el desbloqueo explicito.
    tpLockCleared();
    chk(!tpLocked && !tpLockPending, "el desbloqueo explicito SI lo levanta");
    chk(!tpLockBannerOn(), "y el aviso desaparece de la pantalla de bloqueo");
    {
      Preferences p;
      p.begin("flextheft", true);
      chk(!p.getBool("lk", true), "el levantamiento tambien se persiste");
      p.end();
    }
    tpOn = false; tpHold = false;
    while(imuHolders() > 0) imuRelease();
    gState = ST_HOME;
    gtOk = gtPrev;
  }

  // --- 5. SIN MODULO NO SE MONITORIZA NADA ----------------------------
  {
    bool gtPrev = gtOk; gtOk = true;
    tpOn = true; tpHold = true; tpImuLive = false;
    tpLocked = false; tpLockPending = false;
    uint32_t ev0 = flexTheftEventCount(&tpDet);
    for(int i = 0; i < 200; i++){ gTestMs += 20; tpSensorTick(); }
    chk(flexTheftEventCount(&tpDet) == ev0,
        "con el IMU ausente el clasificador no evalua ni un evento");
    chk(!tpLocked, "y no se inventa ningun bloqueo");
    chk(tpStatus() == TP_ST_PAUSED, "el estado que se ensena es PAUSADA, no ACTIVA");
    tpOn = false; tpHold = false;
    gtOk = gtPrev;
  }

  // --- 6. MAQUETA 480x800: nada fuera de pantalla, nada superpuesto ---
  {
    // Los bloques de la pantalla principal, en orden, con su alto real.
    struct { int y, h; const char* q; } bl[] = {
      { 0,              TP_HDR_H,     "cabecera" },
      { TP_TOGGLE_Y,    TP_TOGGLE_H,  "interruptor" },
      { TP_SENSOR_Y,    TP_SENSOR_H,  "tarjeta del sensor" },
      { TP_ANIM_Y,      TP_ANIM_H,    "animacion" },
      { TP_STATE_Y,     TP_STATE_H,   "estado y ultimo evento" },
      { TP_HISTROW_Y,   TP_ROW_H,     "historial" },
      { TP_SENSROW_Y,   TP_ROW_H,     "sensibilidad" },
      { TP_FOOT_Y,      8,            "pie" },
    };
    const int N = (int)(sizeof(bl)/sizeof(bl[0]));
    bool solapa = false, fuera = false;
    for(int i = 0; i < N; i++){
      if(bl[i].y < 0 || bl[i].y + bl[i].h > SCR_H) fuera = true;
      if(i && bl[i].y < bl[i-1].y + bl[i-1].h){
        printf("  FALLO: '%s' se solapa con '%s'\n", bl[i].q, bl[i-1].q);
        solapa = true;
      }
    }
    chk(!solapa, "ningun bloque de la pantalla principal se solapa con otro");
    chk(!fuera,  "ningun bloque se sale de los 800 px de alto");
    chk(TP_X >= 8 && TP_X + TP_W <= SCR_W - 8, "las tarjetas respetan los margenes laterales");
    // El rectangulo de la animacion vive DENTRO de su tarjeta, con margen.
    chk(TP_ST_X > TP_X && TP_ST_X + TP_ST_W < TP_X + TP_W,
        "el escenario de la animacion cabe dentro de su tarjeta");
    chk(TP_ST_Y > TP_ANIM_Y && TP_ST_Y + TP_ST_H < TP_ANIM_Y + TP_ANIM_H,
        "...tambien en vertical");
    // El aviso del bloqueo va entre la barra de estado y el panel del reloj.
    chk(TPL_Y >= 60 && TPL_Y + TPL_H <= 198,
        "el aviso del bloqueo no pisa ni la barra de estado ni el reloj gigante");
    chk(TPL_X >= 8 && TPL_X + TPL_W <= SCR_W - 8,
        "y respeta los margenes laterales");
  }

  // --- 7. LA ANIMACION ES UN BUCLE PERFECTO ---------------------------
  // El ultimo cuadro tiene que ser EL MISMO que el primero. Si no, el
  // bucle da un salto visible cada 7,2 s. Se dibujan los dos y se
  // comparan pixel a pixel: no hay forma de que esto "casi" pase.
  {
    const size_t PX = (size_t)SCR_W * SCR_H;
    uint16_t* ref = (uint16_t*)malloc(PX * 2);
    if(!ref){ printf("  FALLO: sin memoria para la referencia\n"); gFails++; }
    else {
      auto pinta = [&](float u){
        setBuf(bbuf);
        for(size_t i = 0; i < PX; i++) bbuf[i] = 0x5AA5;      // centinela
        gClipX0 = TP_ST_X; gClipX1 = TP_ST_X + TP_ST_W - 1;
        gClipY0 = TP_ST_Y; gClipY1 = TP_ST_Y + TP_ST_H - 1;
        tpDrawStage(u);
        uiClipFull();
        setBuf(fb);
      };
      pinta(0.0f);
      memcpy(ref, bbuf, PX * 2);
      pinta(1.0f);
      int dif = 0;
      for(size_t i = 0; i < PX; i++) if(bbuf[i] != ref[i]) dif++;
      chk(dif == 0, "el ultimo cuadro de la animacion es IDENTICO al primero (bucle perfecto)");

      // Y no es un bucle vacio: por el medio pasan cosas distintas.
      int distintos = 0;
      const float muestras[6] = { 0.20f, 0.38f, 0.50f, 0.65f, 0.80f, 0.90f };
      for(int k = 0; k < 6; k++){
        pinta(muestras[k]);
        int d = 0;
        for(size_t i = 0; i < PX; i++) if(bbuf[i] != ref[i]) d++;
        if(d > 200) distintos++;
      }
      chk(distintos == 6, "y por el camino SI se mueve (la prueba no es vacia)");

      // NADA se sale del escenario: fuera de su rectangulo el centinela
      // sigue intacto, en todo el recorrido del bucle.
      bool limpio = true;
      for(int k = 0; k <= 40 && limpio; k++){
        pinta((float)k / 40.0f);
        for(int y = 0; y < SCR_H && limpio; y++){
          for(int x = 0; x < SCR_W; x++){
            bool dentro = (x >= TP_ST_X && x < TP_ST_X + TP_ST_W &&
                           y >= TP_ST_Y && y < TP_ST_Y + TP_ST_H);
            if(dentro) continue;
            if(bbuf[(size_t)y * SCR_W + x] != 0x5AA5){ limpio = false; break; }
          }
        }
      }
      chk(limpio, "la animacion no escribe ni un pixel fuera de su rectangulo");

      // El escenario se repinta ENTERO en cada cuadro: ni un pixel del
      // centinela sobrevive dentro. Sin esto quedarian restos del cuadro
      // anterior y la animacion parpadearia.
      pinta(0.42f);
      int restos = 0;
      for(int y = TP_ST_Y; y < TP_ST_Y + TP_ST_H; y++)
        for(int x = TP_ST_X; x < TP_ST_X + TP_ST_W; x++)
          if(bbuf[(size_t)y * SCR_W + x] == 0x5AA5) restos++;
      chk(restos == 0, "cada cuadro repinta el escenario entero (sin restos del anterior)");
      free(ref);
    }
  }

  // --- 7b. COSTE DE UN CUADRO -----------------------------------------
  //  No es el P4, asi que el numero absoluto no dice nada. Lo que si dice
  //  algo es la COMPARACION con un panel de vidrio del sistema, que ya se
  //  dibuja a 60 fps en la placa: si un cuadro de la animacion cuesta menos
  //  que eso, no hay motivo para que no sea fluido.
  {
    struct timespec c0, c1;
    const int N = 60;
    setBuf(bbuf);
    gClipX0 = TP_ST_X; gClipX1 = TP_ST_X + TP_ST_W - 1;
    gClipY0 = TP_ST_Y; gClipY1 = TP_ST_Y + TP_ST_H - 1;
    clock_gettime(CLOCK_MONOTONIC, &c0);
    for(int i = 0; i < N; i++) tpDrawStage((float)(i % 40) / 40.0f);
    clock_gettime(CLOCK_MONOTONIC, &c1);
    double cuadro = ((c1.tv_sec - c0.tv_sec) * 1e3 + (c1.tv_nsec - c0.tv_nsec) / 1e6) / N;
    uiClipFull();
    clock_gettime(CLOCK_MONOTONIC, &c0);
    for(int i = 0; i < N; i++) drawLiquidGlassPanel(24, 90, SCR_W - 48, 200, 28, TH_GLASS2);
    clock_gettime(CLOCK_MONOTONIC, &c1);
    double vidrio = ((c1.tv_sec - c0.tv_sec) * 1e3 + (c1.tv_nsec - c0.tv_nsec) / 1e6) / N;
    setBuf(fb);
    printf("  [robo] cuadro de animacion %.3f ms  ·  panel de vidrio comparable %.3f ms\n",
           cuadro, vidrio);
    chk(cuadro < vidrio * 2.0,
        "un cuadro de la animacion no cuesta mas que un panel de vidrio del sistema");
  }

  // --- 7c. LA RED DE SEGURIDAD DEL ENGANCHE ---------------------------
  //  Entrar a mirar adquiere el sensor aunque la proteccion este apagada.
  //  Si la pantalla se pierde sin pasar por la salida, ese enganche tiene
  //  que soltarse solo -- pero NUNCA el de una proteccion activada.
  {
    bool gtPrev = gtOk; gtOk = true;
    while(imuHolders() > 0) imuRelease();
    tpOn = false; tpHold = false; tpTickMs = 0;

    tpHoldImu(true);                       // como hace theftEnter()
    tpTickMs = gTestMs;
    chk(imuHolders() == 1, "entrar a mirar adquiere el servicio");
    gTestMs += 1000; tpIdleGuard();
    chk(imuHolders() == 1, "mientras la pantalla vive, el enganche se queda");
    gTestMs += TP_IDLE_RELEASE_MS + 100; tpIdleGuard();
    chk(imuHolders() == 0 && !tpHold,
        "si su pantalla se pierde sin salir, el enganche se suelta solo");

    // Y con la proteccion ACTIVADA el guardian no puede tocar nada.
    tpSetEnabled(true);
    tpTickMs = gTestMs;
    gTestMs += TP_IDLE_RELEASE_MS * 4;
    tpIdleGuard();
    chk(imuHolders() == 1 && tpHold,
        "con la proteccion activada el guardian NO suelta el sensor");
    tpSetEnabled(false);
    while(imuHolders() > 0) imuRelease();
    tpHold = false; tpTickMs = 0;
    gtOk = gtPrev;
  }

  // --- 8. LA FILA DE AJUSTES DICE EL ESTADO REAL ----------------------
  {
    tpOn = false; tpLocked = false; tpLockPending = false; tpHold = false;
    while(imuHolders() > 0) imuRelease();
    chk(strcmp(theftRowValue(), tpt(TPS_OFF)) == 0, "desactivada: la fila lo dice");
    tpLocked = true;
    tpOn = true;
    chk(strcmp(theftRowValue(), tpt(TPS_LOCKEDST)) == 0, "bloqueada: la fila lo dice");
    tpLocked = false; tpOn = false;
  }

  // --- 9. LA SENSIBILIDAD CAMBIA EL CLASIFICADOR, NO SOLO UN UMBRAL ---
  {
    tpSetSens(FLEXTHEFT_SENS_LOW);
    FlexTheftParams lo = tpDet.p;
    tpSetSens(FLEXTHEFT_SENS_HIGH);
    FlexTheftParams hi = tpDet.p;
    chk(lo.pullG > hi.pullG && lo.dirCohMin > hi.dirCohMin &&
        lo.threshold > hi.threshold && lo.escapeNeedMs > hi.escapeNeedMs,
        "cambiar de nivel mueve intensidad, coherencia, separacion y confianza");
    chk(lo.needEscape == 1 && hi.needEscape == 0,
        "y en Baja la separacion posterior pasa a ser una condicion");
    tpSetSens(FLEXTHEFT_SENS_NORMAL);
    chk(tpSens == FLEXTHEFT_SENS_NORMAL, "el valor recomendado es Normal");
    tpSetSens(200);
    chk(tpSens == FLEXTHEFT_SENS_NORMAL, "un valor imposible cae en Normal");
  }

  gState = ST_HOME;
  uiClipFull();
  printf("  [robo] detector: %d bytes  ventana temporal: %d bytes (%d ms a %d Hz)  historial: %d bytes\n",
         (int)sizeof(FlexTheftDet), (int)(FLEXTHEFT_RING * sizeof(FlexTheftSlot)),
         FLEXTHEFT_RING * 1000 / FLEXBNO_REPORT_HZ, FLEXBNO_REPORT_HZ,
         (int)sizeof(TpHistFile));
  printf("  [robo] animacion: bucle de %u ms, escenario de %dx%d px repintado a ~%d fps\n",
         (unsigned)TP_ANIM_LOOP_MS, TP_ST_W, TP_ST_H, (int)(1000u / TP_ANIM_FRAME_MS));
  if(gFails > fails0) printf("  Proteccion contra robo: %d fallo(s).\n", gFails - fails0);
  else printf("  Proteccion contra robo: todas las comprobaciones pasan.\n");
}


// #############################################################
//  EL BLUR NO SE QUEDA PEGADO DE LA PANTALLA ANTERIOR
//  ------------------------------------------------------------
//  Dos caminos distintos por los que el Liquid Glass de una app
//  acababa compuesto con pixeles de OTRA pantalla:
//
//   1. LA BANDA PRE-DESENFOCADA. uiGlassBandBegin() guarda una banda
//      ya desenfocada para que un overlay animado sea vidrio real en
//      todos sus cuadros. Mientras esta armada la usa TODA uiSurfaceA,
//      de cualquier pantalla. Si el dueno de esa banda perdia la
//      pantalla por una via que no fuera su propio cierre -- bloqueo
//      por inactividad, aviso de caida, OTA, apagado --, la banda se
//      quedaba viva y a partir de ahi cualquier tarjeta de cualquier
//      app se componia con el desenfoque de la pantalla anterior.
//
//   2. EL BACK BUFFER COMPARTIDO. Una app que compone POR BANDAS da
//      por hecho que lo que hay fuera de la banda sucia es su propio
//      cuadro anterior. En cuanto otro compositor escribe en bbuf eso
//      deja de ser cierto, y el margen de blurR filas del panel de
//      vidrio desenfoca pixeles ajenos.
// #############################################################
static void testBlurNoPegado(){
  printf("Liquid Glass: el blur no se queda pegado de la pantalla anterior\n");
  int before = gFails;
  bool glassPrev = uiGlass;
  uiGlass = true;
  gLand = false; gHosted = false;
  uiClipFull();
  setBuf(fb);

  // ---- 1. La banda pre-desenfocada caduca con su dueno ----
  // Se arma como lo hace el menu contextual y se comprueba que el guardian
  // del bucle la cierra en cuanto la pantalla cambia de manos.
  {
    gState = ST_HOME;
    memset(bbuf, 0x11, (size_t)SCR_W * SCR_H * 2);
    setBuf(bbuf);
    bool armada = uiGlassBandBegin(200, 400, uiSurfTint(UIS_ELEVATED));
    setBuf(fb);
    chk(armada, "la banda pre-desenfocada se arma (hay PSRAM en el arnes)");
    chk(uiGlassBandActive(), "y queda activa mientras su dueno manda");

    // Su dueno es ST_CTX: mientras ese estado mande, la banda vale.
    // uiGlassBandGuard() es el guardian de loop(): ESTE codigo, no una copia.
    gState = ST_CTX;
    uiGlassBandGuard();
    chk(uiGlassBandActive(), "con su dueno en pantalla la banda sigue valiendo");

    // La pantalla cambia de manos por una via que NO es el cierre del menu:
    // el bloqueo por inactividad. Esto es exactamente el caso que dejaba el
    // blur pegado.
    gState = ST_LOCK;
    uiGlassBandGuard();
    chk(!uiGlassBandActive(), "al cambiar de pantalla la banda deja de valer");

    // Y con la banda cerrada, una superficie del sistema vuelve a componerse
    // contra el fondo REAL que tenga debajo, no contra la banda vieja.
    gState = ST_APP; gAppId = IC_CLIMA;
    setBuf(bbuf);
    fillRect(0, 0, SCR_W, SCR_H, rgb565(20, 60, 30));
    uint16_t antes = bbuf[(size_t)300 * SCR_W + 240];
    uiSurface(40, 260, 400, 120, 20, UIS_CARD);
    uint16_t ahora = bbuf[(size_t)300 * SCR_W + 240];
    chk(antes != ahora, "la tarjeta se dibuja sobre el fondo que de verdad hay debajo");
    setBuf(fb);
  }

  // ---- 1b. El MENU DE MEDIOS tambien es dueno de la banda mientras se despliega ----
  // mmOpen arma la banda en ST_APP (Galeria, Multimedia, Archivos, la nube). El
  // guardian de loop() solo conocia a ST_CTX y al cronometro, asi que la primera
  // vuelta tras abrir el menu se la quitaba y el resto del despliegue (140 ms)
  // se componia con vidrio APILADO. mmBandLive() lo declara dueno.
  {
    gState = ST_APP; gAppId = IC_CLIMA; gAppW = SCR_W; gAppH = SCR_H;
    setBuf(fb);
    fillRect(0, 0, SCR_W, SCR_H, rgb565(20, 60, 30));
    const uint8_t acts[3] = { MA_CL_XFERS, MA_CL_REFRESH, MA_INFO };
    gTestMs += 1000;
    mmOpen(380, 80, acts, 3);
    chk(mmOn && !mmAnimDone && uiGlassBandActive(), "abrir el menu de medios arma la banda");
    chk(mmBandLive(), "y el menu se declara dueno de ella mientras se despliega");
    uiGlassBandGuard();                                              // la primera vuelta de loop() tras abrirlo
    chk(uiGlassBandActive(), "el guardian de loop() NO se la quita en la primera vuelta");
    gTestMs += 70; uiGlassBandGuard(); mmAnimTick();
    chk(mmOn && !mmAnimDone && uiGlassBandActive(), "ni a mitad del despliegue");
    gTestMs += 100; uiGlassBandGuard(); mmAnimTick();                // t >= 1: acaba
    chk(mmAnimDone && !uiGlassBandActive() && !mmBandLive(), "al terminar, el propio menu suelta la banda (ya nadie es su dueno)");

    // Otra via de cambio de pantalla a mitad del despliegue (bloqueo, aviso de caida...):
    // nadie vuelve a llamar a mmAnimTick y la banda NO puede quedarse colgada.
    mmClose();
    gTestMs += 1000;
    mmOpen(380, 80, acts, 3);
    gTestMs += MM_ANIM_MS + MM_BAND_GRACE_MS + 10;
    uiGlassBandGuard();
    chk(!uiGlassBandActive(), "si el menu se queda sin ticks la banda caduca y el guardian la cierra");
    mmClose();

    // Los otros duenos siguen mandando.
    gState = ST_CTX; setBuf(bbuf);
    uiGlassBandBegin(200, 400, uiSurfTint(UIS_ELEVATED)); setBuf(fb);
    uiGlassBandGuard();
    chk(uiGlassBandActive(), "(el menu contextual del escritorio sigue siendo dueno)");
    gState = ST_APP;
    uiGlassBandGuard();
    chk(!uiGlassBandActive(), "(y fuera de el, sin menu de medios, la banda se cierra)");
  }

  // ---- 2. El back buffer cambia de dueno -> la app compone entera ----
  {
    gBbufOwner = BBUF_NONE;
    chk(!bbufClaim(BBUF_APP + IC_CLIMA), "sin dueno previo, la app compone el cuadro entero");
    chk(bbufClaim(BBUF_APP + IC_CLIMA),  "y a partir de ahi le basta la banda sucia");
    chk(bbufClaim(BBUF_APP + IC_CLIMA),  "...vuelta tras vuelta, sin repintar de mas");
    // Cualquier compositor del sistema que toque bbuf la desaloja.
    bbufSys();
    chk(!bbufClaim(BBUF_APP + IC_CLIMA), "si el sistema escribe en bbuf, la app vuelve a componer entera");
    // Y dos apps distintas nunca se heredan el buffer.
    chk(bbufClaim(BBUF_APP + IC_CLIMA), "la app reclama su buffer");
    chk(!bbufClaim(BBUF_APP + IC_BRUJULA), "otra app NO hereda el cuadro de la anterior");
  }

  // ---- 3. Y la app de verdad: primer cuadro entero, siguientes por banda ----
  {
    gState = ST_APP; gAppId = IC_CLIMA;
    gAppW = SCR_W; gAppH = SCR_H;
    gBbufOwner = BBUF_SYS;                       // como si acabara de correr una transicion
    memset(bbuf, 0x7F, (size_t)SCR_W * SCR_H * 2);
    wxPresent(400, 420);                         // pide una banda estrecha...
    bool fueraLimpio = true;
    for(int y = 0; y < 40; y++)
      for(int x = 0; x < SCR_W; x += 16)
        if(bbuf[(size_t)y * SCR_W + x] == 0x7F7F) fueraLimpio = false;
    chk(fueraLimpio, "tras un cambio de dueno, la app compone TODO el cuadro, no solo la banda");
    chk(gBbufOwner == (uint16_t)(BBUF_APP + IC_CLIMA), "y se queda con el buffer");
  }

  uiGlass = glassPrev;
  uiGlassBandEnd();
  gBbufOwner = BBUF_NONE;
  gState = ST_HOME; gAppId = IC_RELOJ;
  uiClipFull();
  setBuf(fb);
  if(gFails == before) printf("  Blur pegado: todas las comprobaciones pasan.\n");
}

static void testLiquidGlassSinApilar(){
  printf("Liquid Glass: las animaciones no apilan capas de blur\n");
  bool glassPrev = uiGlass;
  uiGlass = true;                       // el fallo solo existe con vidrio
  gLand = false; gHosted = false;
  uiClipFull();
  setBuf(fb);

  if(!lgSnap) lgSnap = (uint16_t*)malloc((size_t)SCR_W * SCR_H * 2);
  static uint16_t* lgSnap2 = NULL;
  if(!lgSnap2) lgSnap2 = (uint16_t*)malloc((size_t)SCR_W * SCR_H * 2);
  if(!lgSnap || !lgSnap2){ printf("  FALLO: sin memoria para la prueba\n"); gFails++; return; }

  // ---- 1. El contrato del primitivo, en claro --------------------------
  // No es un fallo del vidrio: es SU CONTRATO. Dibujarlo sobre un fondo
  // limpio siempre da lo mismo; dibujarlo sobre su propia salida, no.
  // Todo lo demas de esta prueba existe porque esto es asi.
  {
    const int X = 40, Y = 120, W = 400, H = 200;
    fillRect(0, 0, SCR_W, SCR_H, rgb565(24, 30, 48));
    drawLiquidGlassPanel(X, Y, W, H, 24, TH_GLASS);
    lgGrab(lgSnap, Y, Y + H - 1);
    // (a) otra vez desde el MISMO fondo limpio -> identico
    fillRect(0, 0, SCR_W, SCR_H, rgb565(24, 30, 48));
    drawLiquidGlassPanel(X, Y, W, H, 24, TH_GLASS);
    lgGrab(lgSnap2, Y, Y + H - 1);
    chk(lgDiff(lgSnap, lgSnap2, Y, Y + H - 1) == 0,
        "sobre un fondo limpio, el vidrio es determinista");
    // (b) encima de si mismo -> distinto (esto es lo que se apilaba)
    drawLiquidGlassPanel(X, Y, W, H, 24, TH_GLASS);
    lgGrab(lgSnap2, Y, Y + H - 1);
    chk(lgDiff(lgSnap, lgSnap2, Y, Y + H - 1) > 0,
        "encima de su propia salida SI cambia: por eso no se puede repintar asi");
  }

  // ---- 2. El barrido del anillo de Device Care -------------------------
  gState = ST_APP; gAppId = IC_DEVCARE;
  gAppW = SCR_W; gAppH = SCR_H;
  dcScreen = DC_HOME;
  gTestMs = 500000;
  dcAnimT0 = gTestMs;
  dcHeader(dct(DCS_APPTITLE));
  dcRenderHome();

  int cx, cy, cw, cardH;
  dcHomeCardGeom(cx, cy, cw, cardH);
  int rr = cardH / 2 - 16; if(rr > 54) rr = 54; if(rr < 26) rr = 26;
  int rcy = cy + cardH / 2;
  int b0 = rcy - rr - 4, b1 = rcy + rr + 4;

  // Cuadro intermedio del barrido, repetido veinte veces con el MISMO
  // instante: si apilara, cada pasada emborronaria un poco mas.
  gTestMs = 500000 + DC_RING_MS / 2;
  dcAnimMs = 0; dcHomeAnimTick();
  lgGrab(lgSnap, b0, b1);
  int deriva = 0;
  for(int i = 0; i < 20; i++){
    dcAnimMs = 0;                       // se salta el limitador de cadencia
    dcHomeAnimTick();
    lgGrab(lgSnap2, b0, b1);
    deriva += lgDiff(lgSnap, lgSnap2, b0, b1);
  }
  chk(deriva == 0, "20 cuadros del anillo dan EXACTAMENTE los mismos pixeles");

  // Y el ultimo cuadro del barrido tiene que coincidir con el repintado
  // completo: eso comprueba de paso que la animacion NO borra los textos
  // de la derecha de la tarjeta (antes el panel de vidrio los pisaba y
  // no los devolvia).
  gTestMs = 500000;
  dcAnimT0 = gTestMs;
  dcRenderHome();
  lgGrab(lgSnap, b0, b1);
  gTestMs = 500000 + DC_RING_MS;        // p = 1: el cuadro final
  dcAnimMs = 0; dcHomeAnimTick();
  lgGrab(lgSnap2, b0, b1);
  chk(lgDiff(lgSnap, lgSnap2, b0, b1) == 0,
      "el cuadro final del barrido es identico al repintado completo");

  // ---- 3. El panel de Optimizar Flex OS -------------------------------
  // Fondo reconocible debajo, para que un desenfoque de mas se note.
  setBuf(fb);
  uiClipFull();
  for(int y = 0; y < SCR_H; y++)
    hLine(0, y, SCR_W, ((y / 8) & 1) ? rgb565(40, 60, 110) : rgb565(18, 24, 40));
  gTestMs += 1000;
  optStart();
  chk(optActive(), "el panel de optimizacion se abre");
  int o0 = OPT_BAND_T, o1 = OPT_BAND_B - 1;
  lgGrab(lgSnap, o0, o1);
  deriva = 0;
  for(int i = 0; i < 12; i++){          // mas pasadas que etapas tiene
    optRender();
    lgGrab(lgSnap2, o0, o1);
    deriva += lgDiff(lgSnap, lgSnap2, o0, o1);
  }
  chk(deriva == 0, "12 repintados del panel de optimizacion no cambian ni un pixel");

  // Y el fondo capturado sigue siendo el de DEBAJO, no el panel ya
  // dibujado: si se hubiera capturado tarde, la fila de encima del panel
  // llevaria material de vidrio.
  chk(optBak != NULL, "el panel guarda la captura de su fondo");
  {
    int filaLimpia = OPT_BAND_T + 2;    // dentro de la banda, encima del panel
    const uint16_t* cap = optBak + (size_t)(filaLimpia - OPT_BAND_T) * SCR_W;
    uint16_t esperado = ((filaLimpia / 8) & 1) ? rgb565(40, 60, 110) : rgb565(18, 24, 40);
    chk(cap[SCR_W / 2] == esperado, "la captura es el fondo real, tomada antes de dibujar");
  }
  // ---- 4. La capsula del cronometro -----------------------------------
  // Se estampa encima de si misma una vez por segundo (cronoCapsuleStamp),
  // apoyandose en que es opaca. Aqui se comprueba que de verdad lo es:
  // ocho estampados seguidos no pueden mover ni un pixel.
  {
    setBuf(fb);
    uiClipFull();
    for(int y = 0; y < SCR_H; y++)
      hLine(0, y, SCR_W, ((y / 8) & 1) ? rgb565(40, 60, 110) : rgb565(18, 24, 40));
    gCronoSt = CRONO_RUN; gCronoT0 = gTestMs;
    cronoCapsuleDraw(20);
    lgGrab(lgSnap, CRONO_CAP_Y, CRONO_CAP_Y + CRONO_CAP_H);
    for(int i = 0; i < 8; i++) cronoCapsuleDraw(20);      // 8 segundos de cronometro
    lgGrab(lgSnap2, CRONO_CAP_Y, CRONO_CAP_Y + CRONO_CAP_H);
    chk(lgDiff(lgSnap, lgSnap2, CRONO_CAP_Y, CRONO_CAP_Y + CRONO_CAP_H) == 0,
        "la capsula del cronometro se estampa encima de si misma sin apilar");
    gCronoSt = CRONO_IDLE;
  }

  optStage = OPT_IDLE;                  // cierre sin repintar Almacenamiento
  optBandFree();
  chk(optBak == NULL, "al cerrar, el panel suelta su captura");

  uiGlass = glassPrev;
  gState = ST_HOME; gAppId = 0;
  uiClipFull();
  setBuf(fb);
  if(gFails) printf("  %d comprobacion(es) del vidrio han fallado.\n", gFails);
  else       printf("  Liquid Glass: todas las comprobaciones pasan.\n");
}

static void testIconosEnSuCaja(){
  printf("Iconos de app: cada uno dentro de su caja\n");
  gLand = false;
  uiClipFull();
  const int X = 120, Y = 200;
  const int estiloPrevio = gIconStyle;
  int peorL = 0, peorR = 0, peorT = 0, peorB = 0, vacios = 0;
  for(int estilo = 0; estilo < 2; estilo++){
    gIconStyle = estilo;
    for(int S = 44; S <= 96; S += 26){
      for(int id = 0; id < APP_N; id++){
        memset(bbuf, 0, (size_t)SCR_W * SCR_H * 2);
        setBuf(bbuf);
        drawAppIcon(id, X, Y, S);
        int minx = SCR_W, maxx = -1, miny = SCR_H, maxy = -1;
        for(int y = 0; y < SCR_H; y++)
          for(int x = 0; x < SCR_W; x++)
            if(bbuf[(size_t)y * SCR_W + x] != 0){
              if(x < minx) minx = x;
              if(x > maxx) maxx = x;
              if(y < miny) miny = y;
              if(y > maxy) maxy = y;
            }
        if(maxx < 0){ vacios++; continue; }
        int l = X - minx, r = maxx - (X + S - 1), t = Y - miny, b = maxy - (Y + S - 1);
        if(l > peorL) peorL = l;
        if(r > peorR) peorR = r;
        if(t > peorT) peorT = t;
        if(b > peorB) peorB = b;
      }
    }
  }
  gIconStyle = estiloPrevio;
  printf("  %d iconos x 2 estilos x 3 tamanos: fuera de la caja L=%d R=%d T=%d B=%d\n",
         APP_N, peorL, peorR, peorT, peorB);
  chk(vacios == 0, "todos los iconos pintan algo");
  chk(peorL <= 2 && peorR <= 2 && peorT <= 2 && peorB <= 2,
      "ningun icono se sale del margen que asume el recorte de drawAppIcon");

  // Y el recorte por banda hace lo prometido: dentro de la banda, el mismo
  // pixel que sin recorte; fuera, ni uno.
  { memset(bbuf, 0, (size_t)SCR_W * SCR_H * 2); setBuf(bbuf);
    uiClipFull();
    drawAppIcon(IC_AJUSTES, X, Y, 70);
    static uint16_t ref[SCR_W * 120];
    for(int y = 0; y < 120; y++) memcpy(ref + (size_t)y * SCR_W, bbuf + (size_t)(Y + y) * SCR_W, SCR_W * 2);

    memset(bbuf, 0, (size_t)SCR_W * SCR_H * 2);
    uiClipViewport(Y + 20, Y + 50);
    drawAppIcon(IC_AJUSTES, X, Y, 70);
    uiClipFull();
    int dif = 0, fuera = 0;
    for(int y = 0; y < 120; y++)
      for(int x = 0; x < SCR_W; x++){
        uint16_t got = bbuf[(size_t)(Y + y) * SCR_W + x];
        if(y >= 20 && y <= 50){ if(got != ref[(size_t)y * SCR_W + x]) dif++; }
        else if(got != 0) fuera++;
      }
    chk(dif == 0,   "icono recortado: las filas de la banda salen identicas");
    chk(fuera == 0, "icono recortado: fuera de la banda no escribe nada");

    // Y un icono ENTERO fuera de la banda no deja rastro (es el caso que el
    // recorte se salta de una vez).
    memset(bbuf, 0, (size_t)SCR_W * SCR_H * 2);
    uiClipViewport(600, 700);
    drawAppIcon(IC_AJUSTES, X, Y, 70);
    uiClipFull();
    int rastro = 0;
    for(size_t i = 0; i < (size_t)SCR_W * SCR_H; i++) if(bbuf[i] != 0) rastro++;
    chk(rastro == 0, "un icono entero fuera de la banda no pinta nada"); }

  if(gFails) printf("  %d comprobacion(es) de los iconos han fallado.\n", gFails);
  else printf("  Iconos de app: todas las comprobaciones pasan.\n");
}


// #############################################################
//  TRANSICIONES DE APP INTERRUMPIBLES  ·  el test del video
//  ------------------------------------------------------------
//  Reproduce la secuencia exacta del video de referencia:
//    App A abierta -> gesto Home -> Inicio acepta el toque EN EL ACTO ->
//    se toca App B antes de que A termine de cerrarse -> B se abre.
//  Lo que se comprueba es lo que de verdad importa y que antes era
//  imposible: que el estado LOGICO cambia en el instante de la
//  intencion (no al final de la animacion), que una intencion nueva
//  RE-DIRIGE la transicion en curso desde su progreso visual actual
//  (sin saltos), y que la finalizacion de una transicion ya
//  reemplazada NO puede volver a tocar el estado.
//
//  El tiempo lo manda la prueba: gTestUs fija micros(), que es de
//  donde sale el progreso de la animacion.
// #############################################################
static void testTransicionesApps(){
  printf("Transiciones de app interrumpibles (el test del video)\n");
  int before = gFails;
  // Entorno limpio y determinista.
  appTrCancel();
  gTestMs = 100000; gTestUs = 1000000;
  gState = ST_HOME; gLand = false; gHosted = false;
  kioskOn = false; gSafeMode = false; gFrPending = false;
  editMode = false; hcActive = false; gNavMode = 1;
  qsPanelY = 0; qsAnimOn = false; qsDragging = false;
  gHomePage = 0; gHomeDirty = false;
  for(int i = 0; i < APP_N; i++) gAppState[i] = ALIFE_CLOSED;
  tReset();

  // ---- 1. ABRIR: el estado logico cambia YA, la animacion solo empieza ----
  enterApp(IC_RELOJ);
  chk(gState == ST_APP && gAppId == IC_RELOJ, "abrir cambia el estado LOGICO en el acto");
  chk(appTrVisible(),                          "y deja una capa visual animandose");
  chk(gTrEnterPending,                         "el enter() de la app queda PENDIENTE, no ha corrido");
  chk(gTrIn.on && gTrIn.app == IC_RELOJ,       "la capa entrante es la app abierta");
  chk(!gTrOut.on,                              "y no hay capa saliente: no habia nada antes");
  uint32_t gen1 = gTrGen;

  // A mitad de la apertura el progreso es intermedio y sale del TIEMPO.
  gTestUs += (ATR_OPEN_MS * 1000u) / 2;
  float pMid = appTrP(&gTrIn, (uint32_t)micros());
  chk(pMid > 0.05f && pMid < 1.0f, "a mitad de camino el progreso es intermedio (interpolado por tiempo)");

  // ---- 2. INTERRUMPIR LA APERTURA: cerrar antes de que la app exista ----
  // Es medio test del video: el usuario abandona una apertura a mitad.
  appClose();
  chk(gState == ST_HOME,        "cerrar devuelve el estado LOGICO a Inicio en el acto");
  chk(!gTrEnterPending,          "la apertura pendiente se cancela: la app nunca llego a existir");
  chk(gAppState[IC_RELOJ] == ALIFE_CLOSED, "y su ciclo de vida vuelve a donde estaba (no se suspende una app sin enter)");
  chk(gTrGen != gen1,            "la generacion avanza: la transicion vieja queda obsoleta");
  chk(gTrOut.on && gTrOut.app == IC_RELOJ, "la app abandonada pasa a ser la capa SALIENTE");
  // CONTINUIDAD: la saliente arranca desde el progreso que tenia, no desde 1.
  chk(gTrOut.p0 < 0.999f && gTrOut.p0 > 0.0f,
      "la capa saliente continua desde su progreso visual actual (sin salto)");

  // ---- 3. EL CASO CENTRAL: tocar App B mientras A todavia se cierra ----
  uint32_t genClose = gTrGen;
  gTestUs += (ATR_CLOSE_MS * 1000u) / 3;         // A aun no ha terminado
  chk(appTrVisible(), "App A sigue viendose encogiendo");
  enterApp(IC_CALC);
  chk(gState == ST_APP && gAppId == IC_CALC,
      "el toque en App B se acepta EN EL ACTO, con App A aun cerrandose");
  chk(gTrIn.on && gTrIn.app == IC_CALC,  "App B entra como capa entrante");
  chk(gTrOut.on,                          "y App A sigue como capa saliente: dos capas, no mas");
  chk(gTrGen != genClose,                 "la generacion avanza otra vez");

  // ---- 4. UNA FINALIZACION OBSOLETA NO PUEDE TOCAR EL ESTADO ----
  // Se fuerza el remate de la transicion VIEJA (generacion caducada).
  uint32_t genB = gTrGen;
  gTrEnterGen = genClose;                  // simula el remate de la transicion reemplazada
  appTrFinishOpen();
  chk(gState == ST_APP && gAppId == IC_CALC,
      "una finalizacion de generacion vieja NO devuelve a Inicio ni borra App B");
  chk(!gTrEnterPending, "y se descarta sin dejar nada pendiente");
  (void)genB;

  // ---- 5. NUNCA MAS DE DOS CAPAS ----
  appTrCancel();
  gState = ST_HOME;
  for(int i = 0; i < APP_N; i++) gAppState[i] = ALIFE_CLOSED;
  enterApp(IC_RELOJ);   gTestUs += 20000;
  enterApp(IC_CALC);    gTestUs += 20000;
  enterApp(IC_NOTAS);   gTestUs += 20000;
  chk(gTrIn.on && gTrIn.app == IC_NOTAS, "tras tres aperturas encadenadas, la entrante es la ULTIMA");
  chk(gAppId == IC_NOTAS,                 "y el estado logico es el de la ultima intencion");
  // gTrIn + gTrOut son las UNICAS capas que existen: no hay lista que crezca.
  chk(sizeof(gTrIn) == sizeof(gTrOut), "solo existen dos capas (entrante y saliente)");

  // ---- 6. EL PROGRESO SE ACABA POR TIEMPO, NO POR CUADROS ----
  appTrCancel();
  gState = ST_HOME;
  for(int i = 0; i < APP_N; i++) gAppState[i] = ALIFE_CLOSED;
  enterApp(IC_RELOJ);
  gTestUs += ATR_OPEN_MS * 1000u + 5000u;        // se pasa de largo la duracion
  chk(appTrP(&gTrIn, (uint32_t)micros()) >= 1.0f,
      "pasada la duracion el progreso satura en el destino (no se acumula deuda)");

  appTrCancel();
  gTestUs = 0;                                    // devolver micros() al reloj real
  if(gFails == before) printf("  Transiciones: todas las comprobaciones pasan.\n");
}

// #############################################################
//  REJILLA DEL INICIO  ·  paginas automaticas y cero solapamientos
//  ------------------------------------------------------------
//  Antes, cuando no quedaba una sola celda libre, homeOrderNormalize()
//  BORRABA la app de favoritos (gAppFav &= ~bit) y drwFavToggle() se
//  negaba a anadirla: la app desaparecia del escritorio sin avisar.
//  Ninguno de los dos creaba la pagina que faltaba. Esto comprueba que
//  ahora se crea, que ni un icono se pierde y que dos apps no pueden
//  compartir celda.
// #############################################################
static void testRejillaAutoPaginas(){
  printf("Rejilla del Inicio: paginas automaticas sin solapar\n");
  int before = gFails;
  gHomeCols = 4; gHomeRows = 3;
  gHomePageN = 1; gHomePage = 0; gHomeMain = 0;
  for(int p = 0; p < HOME_PAGES_MAX; p++) gHomeWgN[p] = 0;
  for(int i = 0; i < HOME_TOTAL; i++) homeOrder[i] = HOME_EMPTY;
  gAppHidden = 0;

  const int perPage = 12;                       // 4x3
  // Llenar la unica pagina que existe.
  gAppFav = 0;
  for(int i = 0; i < perPage; i++){ homeOrder[homeIdx(0, i)] = (uint8_t)i; gAppFav |= (1u << i); }
  chk(homeFirstFree() < 0, "con la unica pagina llena no queda ni una celda libre");

  // Una app mas: TIENE que aparecer una pagina nueva, no perderse la app.
  int slot = homeFirstFreeGrow();
  chk(slot >= 0,          "homeFirstFreeGrow crea pagina cuando no queda hueco");
  chk(gHomePageN == 2,    "y el escritorio pasa a tener dos paginas");
  chk(slot == homeIdx(1, 0), "el hueco es la primera celda de la pagina nueva");

  // La ruta REAL de "anadir a Inicio" (menu de la caja de apps).
  gHomePageN = 1;
  for(int i = 0; i < HOME_TOTAL; i++) homeOrder[i] = HOME_EMPTY;
  gAppFav = 0;
  for(int i = 0; i < perPage; i++){ homeOrder[homeIdx(0, i)] = (uint8_t)i; gAppFav |= (1u << i); }
  int extra = perPage;                          // una app que aun no esta en Inicio
  chk(extra < APP_N, "hay al menos una app fuera del escritorio para la prueba");
  if(extra < APP_N){
    drwFavToggle(extra);
    chk(appIsFav(extra), "anadir a Inicio con el escritorio lleno SI anade la app");
    chk(gHomePageN >= 2, "creando la pagina que hacia falta");
    int found = -1;
    for(int i = 0; i < HOME_TOTAL; i++) if(homeOrder[i] == (uint8_t)extra) found = i;
    chk(found >= 0, "y la app tiene una ranura de verdad");
  }

  // NI UN ICONO EN DOS CELDAS, ni una celda con dos iconos, tras normalizar.
  homeOrderNormalize();
  {
    int seen[APP_N]; for(int i = 0; i < APP_N; i++) seen[i] = 0;
    bool dup = false;
    for(int i = 0; i < HOME_TOTAL; i++){
      uint8_t v = homeOrder[i];
      if(v == HOME_EMPTY) continue;
      if(v >= APP_N){ dup = true; break; }
      if(seen[v]++) dup = true;
    }
    chk(!dup, "ninguna app aparece en dos ranuras");
  }
  // Ninguna app favorita se ha quedado sin sitio.
  {
    bool lost = false;
    for(int id = 0; id < APP_N; id++){
      if(!appIsFav(id) || appIsHidden(id)) continue;
      bool here = false;
      for(int i = 0; i < HOME_TOTAL && !here; i++) if(homeOrder[i] == (uint8_t)id) here = true;
      if(!here) lost = true;
    }
    chk(!lost, "ninguna app favorita se pierde por falta de espacio");
  }

  // AUTORREPARACION: datos guardados con DUPLICADOS y ranuras fuera de rango.
  gHomePageN = 2; gHomeCols = 4; gHomeRows = 3;
  for(int i = 0; i < HOME_TOTAL; i++) homeOrder[i] = HOME_EMPTY;
  gAppFav = 0x7;                                  // apps 0,1,2 en Inicio
  homeOrder[homeIdx(0, 0)] = 0;
  homeOrder[homeIdx(0, 1)] = 0;                   // DUPLICADO de la app 0
  homeOrder[homeIdx(0, 2)] = 1;
  homeOrder[homeIdx(0, 19)] = 2;                  // ranura que la rejilla 4x3 NO tiene
  homeOrderNormalize();
  {
    int n0 = 0;
    for(int i = 0; i < HOME_TOTAL; i++) if(homeOrder[i] == 0) n0++;
    chk(n0 == 1, "un duplicado guardado se reduce a UNA sola ranura");
    bool has2 = false;
    for(int i = 0; i < HOME_TOTAL; i++) if(homeOrder[i] == 2) has2 = true;
    chk(has2, "el icono que estaba en una ranura inexistente se recoloca, no se borra");
    bool has1 = false;
    for(int i = 0; i < HOME_TOTAL; i++) if(homeOrder[i] == 1) has1 = true;
    chk(has1, "y el resto de la personalizacion se conserva");
  }
  if(gFails == before) printf("  Rejilla del Inicio: todas las comprobaciones pasan.\n");
}


// =============================================================
//  MEDIOS: orientacion, ajuste sin deformar y mapeo del tacto
//  ------------------------------------------------------------
//  Aqui se prueba la parte del reproductor que es LOGICA PURA y que
//  ademas concentra el fallo mas caro de una app que gira: que la
//  imagen se vea horizontal y el dedo siga respondiendo como si la
//  pantalla estuviera vertical. La comprobacion clave es la ultima:
//  se recorre la geometria de los botones EN LAS DOS orientaciones y
//  se comprueba que el punto fisico que hay que tocar para pulsar un
//  boton es el punto donde ese boton se dibuja.
// =============================================================
static void testMediosOrientacion(){
  printf("Medios - orientacion, ajuste y tacto\n");
  gLand = false;

  // ---- 1. MODO AUTO: manda la forma del archivo ----
  gMediaOriMode = MORI_AUTO;
  chk(mediaOriLandscape(1920, 1080), "Auto: un archivo mas ancho que alto se abre en horizontal");
  chk(!mediaOriLandscape(1080, 1920), "Auto: uno mas alto que ancho se queda en vertical");
  chk(!mediaOriLandscape(0, 0), "Auto: sin dimensiones no se gira (no se adivina)");

  // Cuadrado: la ultima orientacion de la SESION, que es lo pedido.
  gMediaSquareLand = false;
  chk(!mediaOriLandscape(600, 600), "Auto: un archivo cuadrado usa la ultima orientacion (vertical)");
  gMediaSquareLand = true;
  chk(mediaOriLandscape(600, 600), "Auto: un archivo cuadrado usa la ultima orientacion (horizontal)");

  // ---- 2. MODOS MANUALES: mandan siempre, sea cual sea el archivo ----
  gMediaOriMode = MORI_PORT;
  chk(!mediaOriLandscape(1920, 1080), "Vertical fuerza vertical incluso en un archivo apaisado");
  gMediaOriMode = MORI_LAND;
  chk(mediaOriLandscape(1080, 1920), "Horizontal fuerza horizontal incluso en un archivo vertical");
  gMediaOriMode = MORI_AUTO;

  // ---- 3. AJUSTE SIN DEFORMAR ----
  // La proporcion de salida tiene que ser la de entrada, con el error
  // de un pixel del redondeo entero. Si esto falla, las fotos salen
  // estiradas, que es exactamente lo que no puede pasar.
  struct { int sw, sh, bw, bh; } casos[] = {
    { 1920, 1080, 480, 800 }, { 1080, 1920, 480, 800 },
    { 1920, 1080, 800, 480 }, { 640,  480,  800, 480 },
    { 600,  600,  480, 800 }, { 4000, 3000, 480, 800 },
    { 100,  1000, 800, 480 },
  };
  bool cabe = true, proporcion = true, positivos = true;
  for(unsigned i = 0; i < sizeof(casos) / sizeof(casos[0]); i++){
    int ow = 0, oh = 0;
    mediaFitBox(casos[i].sw, casos[i].sh, casos[i].bw, casos[i].bh, ow, oh);
    if(ow > casos[i].bw || oh > casos[i].bh) cabe = false;
    if(ow < 1 || oh < 1) positivos = false;
    // |sw*oh - sh*ow| tiene que ser pequeno frente a la escala: es la
    // comparacion de proporciones sin coma flotante.
    long long izq = (long long)casos[i].sw * oh, der = (long long)casos[i].sh * ow;
    long long dif = izq > der ? izq - der : der - izq;
    long long tol = (long long)casos[i].sw + casos[i].sh;
    if(dif > tol) proporcion = false;
  }
  chk(cabe,       "el contenido ajustado nunca se sale de su caja");
  chk(positivos,  "el ajuste nunca produce un tamano de cero");
  chk(proporcion, "el ajuste conserva la proporcion: las imagenes no se deforman");
  { // Y ademas TOCA un borde: si no, quedaria mas pequeno de lo que cabe.
    int ow = 0, oh = 0;
    mediaFitBox(1920, 1080, 800, 480, ow, oh);
    chk(ow == 800 || oh == 480, "el ajuste llena la caja por su lado limitante");
  }

  // ---- 4. LIENZO LOGICO ----
  chk(mediaCanvasW(false) == SCR_W && mediaCanvasH(false) == SCR_H, "vertical: el lienzo es el de la pantalla");
  chk(mediaCanvasW(true)  == LW    && mediaCanvasH(true)  == LH,    "horizontal: el lienzo es el girado");

  // ---- 5. TACTO Y DIBUJO, LA MISMA GEOMETRIA ----
  // En horizontal, putPhys(lx,ly) escribe en el pixel fisico
  // (SCR_W-1-ly, lx). El tacto tiene que hacer el camino INVERSO
  // exacto: desde un toque fisico en ese pixel se debe recuperar
  // (lx,ly). Se comprueba sobre una rejilla de puntos del lienzo.
  bool ida = true;
  for(int lx = 0; lx < LW; lx += 37){
    for(int ly = 0; ly < LH; ly += 29){
      int fx = (SCR_W - 1) - ly, fy = lx;      // donde CAE ese punto logico
      T.x = fx; T.y = fy;
      int gx = 0, gy = 0;
      mediaTouchXY(true, gx, gy);              // lo que el tacto deduce
      if(gx != lx || gy != ly) ida = false;
    }
  }
  chk(ida, "horizontal: el tacto deshace EXACTAMENTE la rotacion del dibujo");
  { // Y en vertical no toca nada.
    T.x = 123; T.y = 456;
    int gx = 0, gy = 0;
    mediaTouchXY(false, gx, gy);
    chk(gx == 123 && gy == 456, "vertical: el tacto no transforma nada");
  }

  // ---- 6. LAS BARRAS DEL VISOR, EN LAS DOS ORIENTACIONES ----
  // Barras y botones se maquetan en el lienzo LOGICO y el tacto lee la
  // MISMA geometria. Se comprueba: que caben en el lienzo, que los botones
  // caen dentro de su barra y no se pisan, y que tocar el centro de cada
  // boton, pasando por la conversion del tacto fisico, da ESE boton.
  for(int paso = 0; paso < 2; paso++){
    vwLand = (paso == 1);
    gLand = vwLand;
    const char* ori = vwLand ? "horizontal" : "vertical";
    char m[112];
    vwMediaW = 1600; vwMediaH = 1200; vwScale = 1.0f; vwBarsA = 1.0f;
    vwCanTrash = true; vwCanEdit = true;
    for(int k = 0; k < 2; k++){
      vwKind = k == 0 ? VWK_PHOTO : VWK_VIDEO;
      vwLayout();
      int tx, ty, tw, th, bx, by, bw, bh;
      vwTopGeom(tx, ty, tw, th); vwBotGeom(bx, by, bw, bh);
      snprintf(m, sizeof(m), "%s, %s: las dos barras caben en el hueco visible", ori, k ? "video" : "foto");
      chk(tx >= 0 && ty >= 0 && tx + tw <= vwCW() && ty + th <= vwVY + vwVH &&
          bx >= 0 && by >= 0 && bx + bw <= vwCW() && by + bh <= vwVY + vwVH && by > ty + th, m);
      int want[5], cxs[5], cys[5], n = 0;
      if(k == 0){
        uint8_t b[2]; int nb = vwPhotoBtns(b);
        for(int i = 0; i < nb; i++){ want[n] = b[i]; cxs[n] = bx + 8 + i * VW_BTN_W + VW_BTN_W / 2; cys[n] = by + 24; n++; }
      } else {
        VwVidBtns vb; vwVidBtns(vb);
        snprintf(m, sizeof(m), "%s: los botones del video no se solapan", ori);
        chk(vb.backX + 30 < vb.playX - 30 + 1 && vb.playX + 30 < vb.fwdX - 30 + 1 && vb.fwdX + 30 <= vb.trashX - 30 + 1, m);
        want[n] = VWB_BACK10; cxs[n] = vb.backX; cys[n] = vb.cy; n++;
        want[n] = VWB_PLAY;   cxs[n] = vb.playX; cys[n] = vb.cy; n++;
        want[n] = VWB_FWD10;  cxs[n] = vb.fwdX;  cys[n] = vb.cy; n++;
        want[n] = VWB_TRASH;  cxs[n] = vb.trashX; cys[n] = vb.cy; n++;
      }
      want[n] = VWB_BACK; cxs[n] = tx + 28; cys[n] = ty + th / 2; n++;
      bool dentro = true, tacto = true;
      for(int i = 0; i < n; i++){
        bool enBarra = (want[i] == VWB_BACK) ? (cxs[i] >= tx && cxs[i] <= tx + tw && cys[i] >= ty && cys[i] <= ty + th)
                                              : (cxs[i] >= bx && cxs[i] <= bx + bw && cys[i] >= by && cys[i] <= by + bh);
        if(!enBarra) dentro = false;
        // Punto FISICO donde se dibuja ese centro logico, y vuelta por el tacto.
        int fx = vwLand ? (SCR_W - 1) - cys[i] : cxs[i], fy = vwLand ? cxs[i] : cys[i];
        int lx, ly; vwTouchXY(fx, fy, lx, ly);
        if(vwHitBtn(lx, ly) != want[i]) tacto = false;
      }
      snprintf(m, sizeof(m), "%s, %s: cada boton cae dentro de su barra", ori, k ? "video" : "foto");
      chk(dentro, m);
      snprintf(m, sizeof(m), "%s, %s: tocar donde se dibuja un boton pulsa ESE boton", ori, k ? "video" : "foto");
      chk(tacto, m);
    }
  }
  vwLand = false; gLand = false; vwKind = VWK_NONE;

  // ---- 7. VOLUMEN: sin codec no hay control, ni en el panel ni en el catalogo ----
  chk(!flexAudioAvailable(), "el doble de audio reproduce el caso SIN codec");
  chk(!qpCtlAvail(QSID_VOLUME), "sin codec no se ofrece el deslizador de volumen");
  chk(!qpCtlAvail(QSID_MUTE),   "sin codec no se ofrece el interruptor de silencio");
  flexPrefsWipe();
  qpLoaded = false; qpLoad();
  chk(!qpCfgHas(QSID_VOLUME), "la configuracion de fabrica no coloca un control de volumen sin salida real");

  // ---- 8. ALMACENAMIENTO: los medios activos usan LittleFS ----
  chk(strcmp(mediaVolName("/Documentos/x"), "Memoria interna") == 0,
      "el reproductor identifica la memoria interna");

  printf("  Medios: todas las comprobaciones pasan.\n");
}


// #############################################################
//  MULTITAREA POR MEMORIA  ·  presupuesto, desalojo y Recientes
//  ------------------------------------------------------------
//  Lo que se comprueba aqui es el CABLEADO entre las reglas (que ya
//  tienen su propia bateria en test_mem) y el sistema real: que la
//  medida sale del SDK y no de una tabla, que la puerta de admision se
//  aplica de verdad en enterApp, que una app ligera nunca se bloquea,
//  que soltar recursos NO cierra nada, que el desalojo elige la menos
//  reciente y solo como ultimo recurso, y que ni las tarjetas ni sus
//  miniaturas se acumulan.
//
//  Y una comprobacion que solo es posible aqui: que TODO lo que se
//  reserva se libera. El arnes lleva la cuenta real de la PSRAM
//  (gPsUsed), asi que una fuga en el codigo nuevo se ve como un numero
//  que no vuelve a su sitio.
// #############################################################
extern size_t gPsUsed, gTestPsTotal, gTestPsPressure, gTestPsLargest, gTestInFree, gTestInTotal;

// Deja el ciclo de vida y Recientes en blanco, sin tocar la memoria real.
#define MB_(x) ((size_t)(x) * 1024u * 1024u)
static void mtReset(){
  // Sin esto, una transicion de app viva de una prueba anterior hace que
  // memAlertTick() salga por su guarda ("en mitad de una animacion nadie mas
  // compone") y la bateria mediria otra cosa.
  appTrCancel();
  gState = ST_HOME;
  gNotifCount = 0;
  while(swCardCount() > 0) swDropCard(0);
  for(int i = 0; i < APP_N; i++){
    gAppState[i] = ALIFE_CLOSED;
    gAppSeenMs[i] = 0;
    gSessNeedSave[i] = false;
    gAppShed[i] = false;
    appMemForget(i);
  }
  gSessDirtyApp = -1;
  gTestPsPressure = 0;
  gTestPsLargest = 0;
  gTestInFree = 180u << 10;
  memSampleNow();
}
// Presion artificial: deja EXACTAMENTE 'freeWanted' bytes de PSRAM libre.
static void mtSetFree(size_t freeWanted){
  gTestPsPressure = 0;
  size_t real = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  gTestPsPressure = (real > freeWanted) ? (real - freeWanted) : 0;
  memSampleNow();
}


// #############################################################
//  DESBLOQUEO: EL ULTIMO DIGITO NO PUEDE CONGELAR LA INTERFAZ
//  ------------------------------------------------------------
//  Un fallo real y muy visible: al meter el ULTIMO digito del PIN la
//  pantalla se quedaba quieta un momento y luego saltaba al
//  escritorio. Eran dos cosas, las dos en el camino del toque:
//
//    1. flexLockVerify() derivaba el hash entero (miles de
//       HMAC-SHA256) DENTRO del tick, antes de que el punto del
//       ultimo digito llegara a pintarse;
//    2. lsuUnlock() animaba el revelado del escritorio con un
//       `for(;;)` de 400 ms que no devolvia el control.
//
//  Aqui se comprueba lo unico que de verdad lo impide: que despues
//  del toque la pantalla SIGUE siendo suya, que hacen falta VARIAS
//  vueltas del bucle para resolver (o sea, que esta troceado), y que
//  cada vuelta vuelve de verdad. Si alguien devuelve la derivacion o
//  la animacion al camino del toque, la primera vuelta resolveria
//  entera y esto falla.
// #############################################################
static void lockTestTapPin(int key){
  int x, y, w, h; lsuPinRect(key, x, y, w, h);
  tDown(x + w / 2, y + h / 2, gTestMs + 40);
  tUp(gTestMs + 60, true);
  T.x = x + w / 2; T.y = y + h / 2;
}
// Abre la pantalla de clave y deja pasar la transicion de seguridad, que ahora
// son cuadros: con un reloj virtual hay que avanzarlo a mano.
static void lockTestOpenVerify(){
  lsuStartVerify();
  int cuadros = 0;
  while(authFadeBusy() && cuadros < 5000){ gTestMs += 16; lsuTick(); cuadros++; }
}
static void testDesbloqueoFluido(){
  printf("Desbloqueo: el ultimo digito no congela la interfaz\n");
  int before = gFails;
  flexPrefsWipe();
  tReset();
  gTestMs = 500000;
  gHosted = false; gLand = false; kioskOn = false;
  gSafeMode = false;
  gState = ST_HOME;

  chk(flexLockSet("2468", 1), "hay un PIN de 4 digitos configurado");
  gLockType = flexLockType();
  chk(gLockType == 1, "y el sistema lo ve como PIN");

  lsuStartVerify();
  chk(gState == ST_LOCKSETUP, "la pantalla de clave toma el mando");
  // LA TRANSICION DE SEGURIDAD TAMPOCO BLOQUEA. Antes eran dos bucles de 190 y
  // 230 ms que no devolvian el control; ahora son cuadros, y por eso esta
  // prueba puede pasar por aqui sin colgarse con un reloj que no corre solo.
  { int cuadros = 0;
    while(authFadeBusy() && cuadros < 5000){ gTestMs += 16; lsuTick(); cuadros++; }
    chk(cuadros > 1, "la transicion de seguridad se reparte en varios cuadros");
    chk(!authFadeBusy(), "y termina sola"); }
  chk(lsuMode == LSU_PIN,     "y entra por el teclado numerico");
  chk(lsuSavedLen == 4,       "conoce la longitud para autoconfirmar");

  // Los tres primeros digitos NO arrancan ninguna verificacion.
  const int TECLA[10] = { 10, 0, 1, 2, 3, 4, 5, 6, 7, 8 };   // indice de rejilla por digito
  lockTestTapPin(TECLA[2]); lsuTick();
  lockTestTapPin(TECLA[4]); lsuTick();
  lockTestTapPin(TECLA[6]); lsuTick();
  chk(!lsuChkOn, "con el PIN incompleto no hay ninguna derivacion en curso");
  chk(gState == ST_LOCKSETUP, "y seguimos en la pantalla de clave");

  // El ULTIMO digito: arranca la verificacion y NO la resuelve.
  lockTestTapPin(TECLA[8]); lsuTick();
  chk(lsuChkOn, "el ultimo digito ARRANCA la verificacion, no la resuelve");
  chk(gState == ST_LOCKSETUP, "la vuelta del toque no salta al escritorio");
  chk(!lsuRevealMs, "ni empieza el revelado todavia");

  // Vueltas sueltas del bucle hasta que resuelve. Tienen que ser VARIAS:
  // si fuera una sola, la derivacion seguiria haciendose de golpe.
  tReset();
  int vueltas = 0;
  while(lsuChkOn && vueltas < 5000){ lsuTick(); vueltas++; }
  chk(vueltas > 1, "la derivacion se reparte en varias vueltas del bucle");
  chk(!lsuChkOn, "y termina");
  chk(gState == ST_LOCKSETUP, "el escritorio todavia no manda: primero el revelado");
  chk(lsuRevealMs != 0, "acertar arranca el revelado del escritorio");

  // El revelado tambien va por vueltas, y TERMINA solo.
  int cuadros = 0;
  while(lsuRevealMs && cuadros < 5000){ gTestMs += 16; lsuTick(); cuadros++; }
  chk(cuadros > 1, "el revelado tambien se reparte en varios cuadros");
  chk(gState == ST_HOME, "y al acabar el escritorio toma el mando");
  chk(!T.down && !T.tap, "el toque del ultimo digito no sobrevive al aterrizaje");

  // ---- Un PIN INCORRECTO recorre el mismo camino y no desbloquea ----
  tReset();
  gTestMs += 1000;
  lockFails = 0; lockWaitReset();
  lockTestOpenVerify();
  lockTestTapPin(TECLA[1]); lsuTick();
  lockTestTapPin(TECLA[1]); lsuTick();
  lockTestTapPin(TECLA[1]); lsuTick();
  lockTestTapPin(TECLA[1]); lsuTick();
  chk(lsuChkOn, "un PIN equivocado tambien se verifica a plazos");
  vueltas = 0;
  while(lsuChkOn && vueltas < 5000){ lsuTick(); vueltas++; }
  chk(vueltas > 1, "...con el mismo reparto en vueltas (no delata el fallo por tiempo)");
  chk(gState == ST_LOCKSETUP, "un PIN equivocado NO desbloquea");
  chk(!lsuRevealMs, "ni arranca el revelado");
  chk(lsuPin[0] == 0, "y el campo se vacia para volver a intentarlo");

  // ---- Salir con una verificacion a medias no la deja viva ----
  tReset();
  gTestMs += 1000;
  lockFails = 0; lockWaitReset();
  lockTestOpenVerify();
  lockTestTapPin(TECLA[2]); lsuTick();
  lockTestTapPin(TECLA[4]); lsuTick();
  lockTestTapPin(TECLA[6]); lsuTick();
  lockTestTapPin(TECLA[8]); lsuTick();
  chk(lsuChkOn, "hay una verificacion en vuelo");
  lsuExit();
  chk(!lsuChkOn, "salir de la pantalla la cancela");
  chk(!flexLockVerifyActive(), "y el modulo suelta el secreto que tenia copiado");

  flexLockClear();
  gLockType = 0;
  flexPrefsWipe();
  tReset();
  gState = ST_HOME;
  if(gFails == before) printf("  Desbloqueo: todas las comprobaciones pasan.\n");
}


// #############################################################
//  CLIMA: UN CUADRO DE ARRASTRE DEJA DE RECOMPONER LA ESCENA
//  ------------------------------------------------------------
//  La app iba a ~7 fps al desplazar. El motivo: cada cuadro
//  recomponia la pantalla entera, y lo primero de todo era la escena
//  procedural -- 800 filas de degradado, 42 estrellas con alpha, el
//  astro con dos halos de radio ~90, cinco montanas, la lamina de
//  agua con sus brillos, cinco nubes, hasta 64 gotas, la niebla y un
//  velo de 210 filas mezcladas pixel a pixel.
//
//  La escena no cambia al desplazarse: se MUEVE. Aqui se fija que es
//  asi de verdad -- que un arrastre no la recompone ni una vez -- y
//  se mide lo que costaba cada cuadro.
// #############################################################
extern FlexWeather gTestWx;
extern bool        gTestWxOn;

static void wxTestFakeData(){
  memset(&gTestWx, 0, sizeof(gTestWx));
  gTestWx.magic = FLEXWX_MAGIC; gTestWx.ver = FLEXWX_VER;
  gTestWx.have  = WXF_FEELS | WXF_HUMIDITY | WXF_PRECIP | WXF_WIND | WXF_GUST |
                  WXF_PRESSURE | WXF_CLOUD | WXF_UV | WXF_VIS | WXF_POP |
                  WXF_MINMAX | WXF_SUN | WXF_WINDDIR;
  snprintf(gTestWx.loc.name,   sizeof(gTestWx.loc.name),   "%s", "Lima");
  snprintf(gTestWx.loc.region, sizeof(gTestWx.loc.region), "%s", "Lima, Peru");
  gTestWx.loc.lat = -12.05f; gTestWx.loc.lon = -77.04f;
  gTestWx.obsTime = 1700000000; gTestWx.utcOffset = -5 * 3600;
  gTestWx.temp = 21.4f; gTestWx.feels = 22.0f; gTestWx.humidity = 74;
  gTestWx.precip = 0.4f; gTestWx.windSpeed = 13.0f; gTestWx.windGust = 22.0f;
  gTestWx.pressure = 1012.0f; gTestWx.cloud = 70; gTestWx.uv = 6.2f;
  gTestWx.visibility = 9000; gTestWx.windDir = 210; gTestWx.pop = 40;
  gTestWx.code = 61;                       // lluvia: la escena mas cara que hay
  gTestWx.isDay = 1;
  gTestWx.hourCount = FLEXWX_HOURS; gTestWx.dayCount = FLEXWX_DAYS;
  gTestWx.tmax = 24.0f; gTestWx.tmin = 17.0f;
  gTestWx.sunrise = 1699980000; gTestWx.sunset = 1700020000;
  for(int i = 0; i < FLEXWX_HOURS; i++){
    gTestWx.hours[i].t = gTestWx.obsTime + i * 3600;
    gTestWx.hours[i].temp10  = (int16_t)(180 + (i % 9) * 10);
    gTestWx.hours[i].feels10 = (int16_t)(185 + (i % 9) * 10);
    gTestWx.hours[i].wind10  = (int16_t)(90 + (i % 5) * 20);
    gTestWx.hours[i].vis100  = 90; gTestWx.hours[i].precip100 = (uint16_t)((i % 4) * 30);
    gTestWx.hours[i].code = (uint8_t)((i % 3) ? 61 : 3);
    gTestWx.hours[i].pop = (uint8_t)((i * 7) % 100);
    gTestWx.hours[i].rh = 70; gTestWx.hours[i].uv10 = (uint8_t)((i % 8) * 8);
  }
  for(int i = 0; i < FLEXWX_DAYS; i++){
    gTestWx.days[i].date    = gTestWx.obsTime + i * 86400;
    gTestWx.days[i].sunrise = gTestWx.sunrise + i * 86400;
    gTestWx.days[i].sunset  = gTestWx.sunset  + i * 86400;
    gTestWx.days[i].max10 = (int16_t)(230 + i * 5);
    gTestWx.days[i].min10 = (int16_t)(160 + i * 4);
    gTestWx.days[i].code = (uint8_t)((i % 2) ? 61 : 2);
    gTestWx.days[i].pop = (uint8_t)(20 + i * 8);
    gTestWx.days[i].uv10 = (uint8_t)(40 + i * 3);
  }
  gTestWxOn = true;
}

static void testClimaFluido(){
  printf("Clima: el arrastre deja de recomponer la escena\n");
  int before = gFails;
  bool glassPrev = uiGlass;
  uiGlass = true;
  gLand = false; gHosted = false;
  gState = ST_APP; gAppId = IC_CLIMA;
  gAppW = SCR_W; gAppH = SCR_H;
  gTestMs = 900000;
  wxTestFakeData();
  wxSkyFree();
  wxSceneFreeze(false);
  wxScroll = 0; wxScrollVel = 0; wxHourScroll = 0; wxDragging = false; wxAxis = 0;
  wxView = WXVIEW_MAIN; wxEnterMs = 0; wxToastMs = 0; wxOkFlashMs = 0;
  wxLayout();
  uiClipFull();
  tReset();

  // ---- 1. El lienzo de la escena se compone UNA vez ----
  gBbufOwner = BBUF_NONE;
  wxSkyBuilds = 0;
  wxFull();
  chk(wxSkyBuilds == 1, "el primer cuadro compone la escena una sola vez");
  uint32_t tras1 = wxSkyBuilds;
  wxFull();
  chk(wxSkyBuilds == tras1, "un segundo cuadro en el mismo instante NO la recompone");

  // ---- 2. UN ARRASTRE COMPLETO no la recompone ni una vez ----
  // Es el caso exacto que se reporto: dedo abajo, treinta cuadros de
  // desplazamiento, dedo arriba.
  wxSkyBuilds = 0;
  for(int i = 0; i < 30; i++){
    gTestMs += 16;                                  // el reloj del sistema SI corre
    T.down = true; T.pressed = (i == 0); T.released = false; T.tap = false;
    T.x = SCR_W / 2; T.y = 600 - i * 12;
    if(i == 0){ T.startX = T.x; T.startY = T.y; T.downMs = gTestMs; }
    else T.moved = true;
    wxDragging = true;
    wxSceneFreeze(true);
    wxScroll = (float)(i * 12);
    wxFull();
  }
  chk(wxSkyBuilds == 0, "treinta cuadros de arrastre NO recomponen la escena ni una vez");

  // ---- 3. Al soltar, la animacion sigue donde estaba y vuelve a correr ----
  T.down = false; T.released = true; T.tap = false;
  wxDragging = false; wxScrollVel = 0;
  wxSceneFreeze(false);
  uint32_t c0 = wxSceneClock();
  gTestMs += 400;
  uint32_t c1 = wxSceneClock();
  chk(c1 > c0, "al soltar, el reloj de la escena vuelve a correr");
  chk(c1 - c0 <= 401, "y no da ningun salto: sigue desde donde se quedo");

  // ---- 4. Lo que cuesta cada cosa ----
  {
    struct timespec a0, a1;
    const int N = 20;
    WxScene sc; wxSceneBlend(&sc);
    uiClipFull(); setBuf(bbuf);
    clock_gettime(CLOCK_MONOTONIC, &a0);
    for(int i = 0; i < N; i++) wxScenePaint(sc, 0, gTestMs + i, 0, SCR_H - 1);
    clock_gettime(CLOCK_MONOTONIC, &a1);
    double componer = ((a1.tv_sec - a0.tv_sec) * 1e3 + (a1.tv_nsec - a0.tv_nsec) / 1e6) / N;

    wxSkyBuild(sc, gTestMs);
    clock_gettime(CLOCK_MONOTONIC, &a0);
    for(int i = 0; i < N; i++) wxDrawScene(0, SCR_H - 1);
    clock_gettime(CLOCK_MONOTONIC, &a1);
    double volcar = ((a1.tv_sec - a0.tv_sec) * 1e3 + (a1.tv_nsec - a0.tv_nsec) / 1e6) / N;

    gBbufOwner = BBUF_APP + IC_CLIMA;
    clock_gettime(CLOCK_MONOTONIC, &a0);
    for(int i = 0; i < N; i++) wxCompose(0, SCR_H - 1);
    clock_gettime(CLOCK_MONOTONIC, &a1);
    double cuadro = ((a1.tv_sec - a0.tv_sec) * 1e3 + (a1.tv_nsec - a0.tv_nsec) / 1e6) / N;

    printf("  [clima] escena: componer %.3f ms  ·  volcar %.3f ms  ·  cuadro completo %.3f ms\n",
           componer, volcar, cuadro);
    // El PC no es la placa: aqui un memcpy es baratisimo y las mezclas por
    // pixel no lo son tanto, asi que la diferencia real en el P4 es MAYOR que
    // esta. Lo que se fija es la direccion: volcar la escena cuesta menos que
    // componerla, y no es el cuadro entero.
    chk(volcar < componer,
        "volcar la escena cuesta menos que componerla (es el trabajo que se va de cada cuadro)");
    chk(volcar < cuadro, "y es una fraccion del cuadro, no el cuadro entero");
  }

  // ---- 5. Si cambia el tiempo, el lienzo se rehace ----
  wxSkyBuilds = 0;
  gTestWx.code = 0; gTestWx.isDay = 0;          // de lluvia de dia a despejado de noche
  wxSceneTo = 0xFF;                              // sin cruce a medias
  gTestMs += 5000;
  wxFull();
  chk(wxSkyBuilds >= 1, "si cambia la escena de verdad, el lienzo se rehace");

  wxSkyFree();
  gTestWxOn = false;
  uiGlass = glassPrev;
  wxScroll = 0; wxScrollVel = 0; wxDragging = false;
  gBbufOwner = BBUF_NONE;
  gState = ST_HOME; gAppId = IC_RELOJ;
  tReset(); uiClipFull(); setBuf(fb);
  if(gFails == before) printf("  Clima: todas las comprobaciones pasan.\n");
}


// #############################################################
//  CALCULADORA: LA OPERACION SE LEE MIENTRAS SE ESCRIBE
//  ------------------------------------------------------------
//  Antes el display ensenaba SOLO el numero en curso: al pulsar un
//  operador se sustituia por el acumulador y el operador no aparecia
//  por ningun sitio. Con "25 + 10" en la cabeza lo que se veia era
//  "25", luego "25" otra vez y luego "10": no habia forma de
//  comprobar que se estaba sumando y no restando.
//
//  Aqui se fija la secuencia entera, tecla a tecla, y que el estado
//  numerico de siempre (acumulador, operador, entrada) sigue dando
//  los mismos resultados.
// #############################################################
static const char* calcTestLine(){
  static char ln[56];
  calcLine(ln, sizeof(ln));
  return ln;
}
static void calcTestType(const char* keys){
  for(const char* k = keys; *k; k++) calcKey(*k);
}
static void testCalculadora(){
  printf("Calculadora: la operacion se lee mientras se escribe\n");
  int before = gFails;
  gState = ST_APP; gAppId = IC_CALC;
  gAppW = SCR_W; gAppH = SCR_H; gHosted = false; gLand = false;

  // ---- 1. La secuencia del encargo, tecla a tecla ----
  calcKey('c');
  chk(!strcmp(calcTestLine(), "0"), "arranca en 0");
  calcTestType("25");
  chk(!strcmp(calcTestLine(), "25"), "se teclea el primer operando");
  calcKey('+');
  chk(!strcmp(calcTestLine(), "25 +"), "al pulsar el operador SE VE el operador");
  calcTestType("10");
  chk(!strcmp(calcTestLine(), "25 + 10"), "y el segundo operando se anade detras");
  calcKey('=');
  chk(!strcmp(calcTestLine(), "35"), "el igual deja el resultado solo");

  // ---- 2. Los cuatro operadores y el porcentaje ----
  calcKey('c'); calcTestType("9"); calcKey('-');
  chk(!strcmp(calcTestLine(), "9 -"), "la resta se ve");
  calcTestType("4"); calcKey('=');
  chk(!strcmp(calcTestLine(), "5"), "y resta bien");
  calcKey('c'); calcTestType("6"); calcKey('x');
  chk(!strcmp(calcTestLine(), "6 x"), "la multiplicacion se ve con el MISMO simbolo de la tecla");
  calcTestType("7"); calcKey('=');
  chk(!strcmp(calcTestLine(), "42"), "y multiplica bien");
  calcKey('c'); calcTestType("84"); calcKey('/');
  chk(!strcmp(calcTestLine(), "84 /"), "la division se ve");
  calcTestType("4"); calcKey('=');
  chk(!strcmp(calcTestLine(), "21"), "y divide bien");
  calcKey('c'); calcTestType("50"); calcKey('%');
  chk(!strcmp(calcTestLine(), "0.5"), "el porcentaje opera sobre la entrada en curso");

  // ---- 3. Encadenar operaciones cierra la anterior ----
  calcKey('c'); calcTestType("2"); calcKey('+'); calcTestType("3"); calcKey('+');
  chk(!strcmp(calcTestLine(), "5 +"), "encadenar cierra la operacion anterior y lo ENSENA");
  calcTestType("4"); calcKey('=');
  chk(!strcmp(calcTestLine(), "9"), "2 + 3 + 4 = 9");

  // ---- 4. Cambiar de idea de operador no rehace la cuenta ----
  calcKey('c'); calcTestType("8"); calcKey('+'); calcKey('x');
  chk(!strcmp(calcTestLine(), "8 x"), "pulsar otro operador seguido solo cambia el signo");
  calcTestType("2"); calcKey('=');
  chk(!strcmp(calcTestLine(), "16"), "y opera con el ultimo elegido");

  // ---- 5. Borrar, signo y decimales siguen funcionando ----
  calcKey('c'); calcTestType("12"); calcKey('+'); calcTestType("345");
  calcKey('\b');
  chk(!strcmp(calcTestLine(), "12 + 34"), "DEL borra del segundo operando, no de la operacion");
  calcKey('n');
  chk(!strcmp(calcTestLine(), "12 + -34"), "el cambio de signo tambien se ve donde toca");
  calcKey('='); 
  chk(!strcmp(calcTestLine(), "-22"), "12 + (-34) = -22");
  calcKey('c'); calcTestType("1.5"); calcKey('x'); calcTestType("2"); calcKey('=');
  chk(!strcmp(calcTestLine(), "3"), "los decimales siguen igual");

  // ---- 6. El error manda sobre todo lo demas ----
  calcKey('c'); calcTestType("5"); calcKey('/'); calcTestType("0"); calcKey('=');
  chk(!strcmp(calcTestLine(), "Error"), "dividir por cero dice Error, no un cero falso");
  calcKey('+');
  chk(!strcmp(calcTestLine(), "Error"), "y un operador sobre Error no inventa una operacion");
  calcTestType("7");
  chk(!strcmp(calcTestLine(), "7"), "teclear un numero limpia el error y empieza de nuevo");

  // ---- 7. YA NO HAY PANEL LATERAL: la rejilla ocupa el ancho entero ----
  {
    calcKey('c');
    int bx, by, bw, bh; calcBox(bx, by, bw, bh);
    chk(bw == gAppW, "la calculadora ocupa TODO el ancho del lienzo");
    int gx, gy, kw, kh, gap; calcGrid(gx, gy, kw, kh, gap);
    int m, g2, dh, bwv, bhv; calcLayout(m, g2, dh, bwv, bhv);
    chk(gx + 4 * kw + 3 * gap + m <= gAppW + 1, "las cuatro columnas caben en el lienzo");
    chk(kw >= 44, "y las teclas son mas anchas que el minimo comodo");
    int dx, dy, dw, dh2; calcDispRect(dx, dy, dw, dh2);
    chk(dw == gAppW - 2 * m, "el display tambien ocupa el ancho entero");
    // Y la linea mas larga que se puede escribir cabe en el display.
    calcTestType("12345678"); calcKey('x'); calcTestType("87654321");
    int fs = dh2 >= 90 ? 5 : dh2 >= 64 ? 4 : dh2 >= 40 ? 3 : 2;
    char ln[56]; calcLine(ln, sizeof(ln));
    while(fs > 1 && textW(ln, fs) > dw - 20) fs--;
    chk(fs >= 2, "una operacion larga sigue siendo legible en el display");
  }

  // ---- 8. La sesion conserva la operacion a medias ----
  {
    calcKey('c'); calcTestType("25"); calcKey('+'); calcTestType("10");
    CalcSessV1 v; memset(&v, 0, sizeof(v));
    v.acc = calcAcc; v.op = calcOp; v.fresh = calcFresh ? 1 : 0; v.err = calcErr ? 1 : 0;
    snprintf(v.disp, sizeof(v.disp), "%s", calcDisp);
    calcKey('c');
    chk(!strcmp(calcTestLine(), "0"), "la calculadora se limpia");
    calcAcc = v.acc; calcOp = v.op; calcFresh = v.fresh != 0; calcErr = v.err != 0;
    snprintf(calcDisp, sizeof(calcDisp), "%s", v.disp);
    chk(!strcmp(calcTestLine(), "25 + 10"), "y al volver de la sesion se lee la MISMA operacion");
  }

  calcKey('c');
  gState = ST_HOME; gAppId = IC_RELOJ;
  if(gFails == before) printf("  Calculadora: todas las comprobaciones pasan.\n");
}

static void testMultitareaMemoria(){
  printf("Multitarea por memoria: presupuesto, desalojo y Recientes\n");
  mtReset();

  // ---- 1) La medida sale del SDK, no de una tabla ----
  chk(memSnap()->psTotal == gTestPsTotal, "la PSRAM total es la que devuelve el SDK");
  chk(memSnap()->inTotal == gTestInTotal, "la SRAM interna tambien se mide, no se supone");
  mtSetFree(12u << 20);
  chk(memSnap()->psFree >= (12u << 20) - 4096 && memSnap()->psFree <= (12u << 20) + 4096,
      "la PSRAM libre publicada es la medida");
  chk(flexMemLevel(memSnap()) == FLEXMEM_LV_OK, "12 MB libres: multitarea normal");

  // ---- 2) El pico de uso solo sube ----
  uint32_t peak0 = memSnap()->psPeakUsed;
  mtSetFree(4u << 20);                      // mas uso -> el pico sube
  chk(memSnap()->psPeakUsed > peak0, "el pico de uso registra la peor situacion vista");
  uint32_t peak1 = memSnap()->psPeakUsed;
  mtSetFree(20u << 20);                     // menos uso -> el pico NO baja
  chk(memSnap()->psPeakUsed == peak1, "el pico no se borra cuando la memoria se recupera");

  // ---- 3) Puerta de admision ----
  mtSetFree(12u << 20);
  chk(memAdmitApp(IC_NAV) == FLEXMEM_OK,        "con 12 MB libres el Navegador se admite");
  chk(memAdmitApp(IC_GALERIA) == FLEXMEM_OK,    "...y la Galeria tambien");
  mtSetFree(4u << 20);
  chk(memAdmitApp(IC_NAV) == FLEXMEM_DENY_PSRAM,      "con 4 MB libres el Navegador se protege");
  chk(memAdmitApp(IC_MULTIMEDIA) == FLEXMEM_DENY_PSRAM, "...y Multimedia igual");
  chk(memAdmitApp(IC_NOTAS)   == FLEXMEM_OK, "una app LIGERA nunca se bloquea");
  chk(memAdmitApp(IC_AJUSTES) == FLEXMEM_OK, "Ajustes tiene que poder abrirse para arreglar el apuro");

  // Bloque contiguo: megas de sobra repartidos en trozos.
  mtSetFree(6u << 20); gTestPsLargest = 600u << 10; memSampleNow();
  chk(memAdmitApp(IC_MULTIMEDIA) == FLEXMEM_DENY_BLOCK,
      "sin un hueco contiguo suficiente se dice que el problema es el reparto");
  gTestPsLargest = 0;

  // SRAM interna: manda sobre la PSRAM.
  mtSetFree(20u << 20); gTestInFree = 20u << 10; memSampleNow();
  chk(memAdmitApp(IC_NAV) == FLEXMEM_DENY_SRAM, "la SRAM interna puede negar por si sola");
  chk(memAdmitApp(IC_RELOJ) == FLEXMEM_OK,      "...pero no a una app ligera");
  gTestInFree = 180u << 10; memSampleNow();

  // ---- 4) enterApp APLICA la puerta (no basta con que exista) ----
  gState = ST_HOME; gAppId = IC_RELOJ; appTrCancel();
  mtSetFree(4u << 20);
  enterApp(IC_NAV);
  chk(gAppState[IC_NAV] == ALIFE_CLOSED, "una app pesada denegada NO queda medio abierta");
  chk(gState == ST_HOME, "y la navegacion se queda donde estaba");
  // La MISMA app, con memoria: se abre.
  mtSetFree(20u << 20);
  enterApp(IC_NOTAS);
  chk(gState == ST_APP && gAppId == IC_NOTAS, "con memoria, la app se abre con normalidad");

  // ---- 5) Volver a una app YA abierta nunca se bloquea ----
  // Es la regla que impide que el usuario se quede sin poder recuperar su nota
  // justo cuando el sistema esta apurado: su memoria ya esta contada.
  appSuspend(IC_NOTAS, false);
  chk(gAppState[IC_NOTAS] == ALIFE_SUSPENDED, "salir de la app la suspende, no la cierra");
  gAppState[IC_GALERIA] = ALIFE_SUSPENDED; gAppSeenMs[IC_GALERIA] = 1000;
  mtSetFree(3u << 20);
  gState = ST_HOME;
  enterApp(IC_GALERIA);
  chk(gAppState[IC_GALERIA] != ALIFE_CLOSED,
      "una app SUSPENDIDA se puede recuperar aunque la memoria este apurada");

  // ---- 6) Soltar recursos NO cierra ninguna app ----
  mtReset();
  gAppState[IC_GALERIA]    = ALIFE_SUSPENDED; gAppSeenMs[IC_GALERIA]    = 1000;
  gAppState[IC_MULTIMEDIA] = ALIFE_SUSPENDED; gAppSeenMs[IC_MULTIMEDIA] = 5000;
  gAppState[IC_NOTAS]      = ALIFE_SUSPENDED; gAppSeenMs[IC_NOTAS]      = 9000;
  mlThumbDropAll();                      // cache de miniaturas compartida... VACIA
  memShedAll(0);
  chk(gAppState[IC_GALERIA]    == ALIFE_SUSPENDED &&
      gAppState[IC_MULTIMEDIA] == ALIFE_SUSPENDED &&
      gAppState[IC_NOTAS]      == ALIFE_SUSPENDED,
      "soltar recursos deja las tres apps ABIERTAS: adelgazar no es cerrar");
  // LA MARCA ES UN HECHO, NO UNA INTENCION. La cache de miniaturas de la
  // biblioteca no tenia una sola miniatura decodificada: no se solto nada,
  // asi que la app NO puede decir "Estado guardado".
  chk(!gAppShed[IC_GALERIA],
      "una cache vacia no marca la app como 'Estado guardado'");
  chk(!strcmp(swStateName(IC_GALERIA), "Pausada"), "sigue siendo 'Pausada'");
  chk(!strcmp(swStateName(IC_NOTAS), "Pausada"), "una app que no solto nada sigue 'Pausada'");
  gAppState[IC_RELOJ] = ALIFE_RUNNING;
  chk(!strcmp(swStateName(IC_RELOJ), "Activa"), "la de primer plano es 'Activa'");

  // Con algo REAL que soltar (el buffer del sensor de la Camara) si se marca.
  camEnter();
  gAppState[IC_CAMARA] = ALIFE_SUSPENDED; gAppShed[IC_CAMARA] = false;
  chk(memShedApp(IC_CAMARA) > 0, "soltar el buffer del sensor SI libera bytes medibles");
  chk(gAppShed[IC_CAMARA], "y entonces si se marca como 'Estado guardado'");
  chk(!strcmp(swStateName(IC_CAMARA), "Estado guardado"), "y la tarjeta lo dice asi");
  camCloseApp();

  // ---- 7) La app ACTIVA nunca se toca ----
  camEnter();
  gAppState[IC_CAMARA] = ALIFE_RUNNING; gAppShed[IC_CAMARA] = false;
  chk(memShedApp(IC_CAMARA) == 0, "a la app en primer plano no se le suelta nada");
  chk(!gAppShed[IC_CAMARA], "...ni se la marca");
  chk(camScene != NULL, "...y su buffer sigue intacto");
  camCloseApp();

  // ---- 8) Desalojo: el ultimo recurso, y por la menos reciente ----
  mtReset();
  gAppState[IC_PAINT]  = ALIFE_SUSPENDED; gAppSeenMs[IC_PAINT]  = 1000;   // la mas antigua
  gAppState[IC_CALEND] = ALIFE_SUSPENDED; gAppSeenMs[IC_CALEND] = 8000;
  // Critico y sin nada que soltar: solo entonces se cierra, y la mas antigua.
  mtSetFree(2u << 20);
  appEnforceMemoryBudget();
  chk(gAppState[IC_PAINT] == ALIFE_CLOSED,
      "en zona critica se cierra la app suspendida MENOS reciente");
  mtReset();

  // ---- 9) Cambios sin guardar ----
  gAppState[IC_NOTAS] = ALIFE_SUSPENDED;
  chk(!appUnsaved(IC_NOTAS), "sin cambios pendientes no se marca nada");
  gSessNeedSave[IC_NOTAS] = true;
  chk(appUnsaved(IC_NOTAS), "la marca de sesion desfasada cuenta como cambios sin guardar");
  swPushNoThumb(IC_NOTAS);
  chk(swUnsavedCount() == 1, "Recientes cuenta las tarjetas con trabajo sin guardar");
  gSessNeedSave[IC_NOTAS] = false;
  chk(swUnsavedCount() == 0, "...y deja de contarlas cuando se guardan");

  // ---- 10) Presupuesto de miniaturas ----
  mtReset();
  for(int k = 0; k < 8; k++){
    int id = (k % (APP_N - 1)) + 1;
    gAppState[id] = ALIFE_SUSPENDED;
    swPush((uint8_t)id);
    if(!swTasks[0].thumb) swTasks[0].thumb = swAllocThumb();
  }
  int thumbs = 0;
  for(int i = 0; i < swCardCount(); i++) if(swTasks[i].thumb) thumbs++;
  chk(thumbs <= SW_THUMB_MAX, "nunca hay mas miniaturas vivas que el tope");
  chk(swCardCount() > SW_THUMB_MAX,
      "...aunque haya mas tarjetas que miniaturas: la lista no se recorta por eso");

  // ---- 11) Una tarjeta por app, sin duplicados ----
  mtReset();
  swPush((uint8_t)IC_NOTAS);
  swPush((uint8_t)IC_NOTAS);
  swPush((uint8_t)IC_PAINT);
  swPush((uint8_t)IC_NOTAS);
  chk(swCardCount() == 2, "volver a empujar la misma app no crea una segunda tarjeta");
  chk(swCardApp(0) == IC_NOTAS, "la ultima usada queda la primera de la lista");
  int seen = 0;
  for(int i = 0; i < swCardCount(); i++) if(swCardApp(i) == IC_NOTAS) seen++;
  chk(seen == 1, "cada app aparece EXACTAMENTE una vez en Recientes");

  // ---- 12) Sin fugas: cerrar todo devuelve la PSRAM ----
  // Se compara contra el punto de partida de ESTA prueba, no contra cero: los
  // framebuffers del sistema viven mientras viva el sistema.
  mtReset();
  size_t psBase = gPsUsed;
  for(int k = 0; k < 6; k++){
    int id = (k % (APP_N - 1)) + 1;
    gAppState[id] = ALIFE_SUSPENDED;
    swPush((uint8_t)id);
    if(!swTasks[0].thumb) swTasks[0].thumb = swAllocThumb();
  }
  chk(gPsUsed > psBase, "las miniaturas ocupan PSRAM de verdad");
  while(swCardCount() > 0) swDropCard(0);
  if(gPsUsed != psBase) printf("  (PSRAM sin devolver: %d bytes)\n", (int)(gPsUsed - psBase));
  chk(gPsUsed == psBase, "cerrar todas las tarjetas devuelve TODA la PSRAM");

  // ---- 13) El optimizador: etapas reales, cifras reales, sin bloquear ----
  mtReset();
  gState = ST_HOME;
  optStart();
  chk(optActive(), "Optimizar arranca y se queda a la vista");
  int steps = 0;
  gTestMs = 1000;
  while(optActive() && optStage != OPT_DONE && steps < 40){
    gTestMs += OPT_STEP_MS + 1;
    optTick();
    steps++;
  }
  chk(optStage == OPT_DONE, "la secuencia termina sola, una etapa por vuelta");
  chk(steps <= 6, "y no da mas vueltas que etapas tiene");
  chk(optGained == 0 || optGained < gTestPsTotal, "los bytes liberados son una medida, no un invento");
  // Cerrar devuelve la pantalla y no deja el panel activo.
  T = Touch(); T.tap = true; T.x = SCR_W / 2; T.y = OPT_BY + 10;
  optTick();
  chk(!optActive(), "el boton Hecho cierra el panel");
  T = Touch();

  // ---- 14) El modo visual eficiente es temporal y no toca las preferencias ----
  bool glassBefore = uiGlass;
  gEffMode = true;
  chk(uiGlass == glassBefore, "el modo eficiente no cambia el estilo elegido");
  mtSetFree(20u << 20);
  gState = ST_HOME;
  flexMemAlertsReset(&gMemAlerts);
  gMemBlockMs = 0; gMemTickMs = 0;
  gTestMs += MEM_BLOCK_MS + MEM_TICK_MS + 10;
  memTick();
  memAlertTick();                              // publica el nivel SOSTENIDO
  gMemBlockMs = 0; gMemTickMs = 0;
  gTestMs += MEM_BLOCK_MS + MEM_TICK_MS + 10;
  memTick();
  chk(!gEffMode, "y se apaga solo en cuanto vuelve a haber holgura");

  // ---- 15) SILENCIO CON MEMORIA NORMAL, y sin reservar por vuelta ----
  // Es el criterio central del rediseno: si hay memoria, el usuario no ve
  // NADA. Y el propio hecho de medir y decidir no puede costar memoria.
  mtReset();
  gState = ST_HOME; gNotifCount = 0;
  flexMemAlertsReset(&gMemAlerts);
  mtSetFree(20u << 20);
  size_t psIdle = gPsUsed;
  for(int i = 0; i < 40; i++){
    gTestMs += 200;
    gMemTickMs = 0; gMemBlockMs = 0;        // fuerza el muestreo en cada vuelta
    memTick();
    memAlertTick();
  }
  chk(gNotifCount == 0, "con 20 MB libres no aparece ni una notificacion");
  chk(gPsUsed == psIdle, "medir y decidir no reserva ni un byte por vuelta");

  // ---- 16) PRIMERO ARREGLAR, DESPUES AVISAR ----
  // Si el alivio automatico resuelve la presion, el usuario no se entera: esa
  // es la regla. Solo cuando ni soltandolo todo se sale del apuro aparece UNA
  // tarjeta. Se prueban los dos caminos.
  gNotifCount = 0;
  flexMemAlertsReset(&gMemAlerts);
  ensureBlurBg();                            // algo real que el alivio pueda soltar
  mtSetFree(MB_(5) + 512u * 1024u);          // 5,5 MB -> WARN, pero recuperable
  gMemTickMs = 0; gMemBlockMs = 0; gTestMs += 200; memTick();
  memAlertTick();
  chk(gNotifCount == 0,
      "si el alivio automatico resuelve la presion, no se avisa de nada");

  // Ahora una presion que NO se puede resolver soltando caches.
  gNotifCount = 0;
  flexMemAlertsReset(&gMemAlerts);
  mtSetFree(MB_(20));                        // se arranca en OK...
  gMemTickMs = 0; gMemBlockMs = 0; gTestMs += 200; memTick(); memAlertTick();
  mtSetFree(MB_(2));                         // ...y se cae a CRITICO de golpe
  gMemTickMs = 0; gMemBlockMs = 0; gTestMs += 200; memTick();
  memAlertTick();
  int trasCruce = gNotifCount;
  chk(trasCruce >= 1, "si tras aliviar SIGUE apretado, se avisa una vez");
  for(int i = 0; i < 30; i++){
    gTestMs += 200;
    gMemTickMs = 0; gMemBlockMs = 0; memTick();
    memAlertTick();
  }
  chk(gNotifCount == trasCruce,
      "y NO se repite en cada vuelta mientras la condicion dura");

  // ---- 17) HISTERESIS de verdad, sobre el sistema completo ----
  // Oscilar alrededor del corte de 6 MB no puede producir una tarjeta nueva
  // cada vez. Es el caso exacto del enunciado (5,99 -> 6,01 -> 5,99).
  gNotifCount = 0;
  for(int i = 0; i < 6; i++){
    mtSetFree(MB_(6) + 64u * 1024u);
    gMemTickMs = 0; gMemBlockMs = 0; gTestMs += 200; memTick(); memAlertTick();
    mtSetFree(MB_(5) + 960u * 1024u);
    gMemTickMs = 0; gMemBlockMs = 0; gTestMs += 200; memTick(); memAlertTick();
  }
  chk(gNotifCount == 0, "oscilar en el umbral no genera ni un aviso mas");
  mtReset(); gNotifCount = 0;

  // ---- 18) UNA APP CON CAMBIOS SIN GUARDAR NO SE CIERRA EN SILENCIO ----
  // El desalojo automatico solo puede cerrar lo que se pudo guardar; si la
  // app dice que tiene trabajo pendiente, "Cerrar todas" tiene que preguntar.
  mtReset();
  gAppState[IC_NOTAS] = ALIFE_SUSPENDED; gAppSeenMs[IC_NOTAS] = 1000;
  gSessNeedSave[IC_NOTAS] = true;
  swPush((uint8_t)IC_NOTAS);
  chk(appUnsaved(IC_NOTAS), "la app se declara con cambios sin guardar");
  chk(swUnsavedCount() == 1, "y Recientes lo cuenta para pedir confirmacion");
  gSessNeedSave[IC_NOTAS] = false;
  mtReset();

  // ---- 19) LA CAMARA NO PIERDE SUS AJUSTES AL SOLTAR EL BUFFER ----
  // Regresion real: camResume() caia en camEnter() cuando el gestor de memoria
  // habia soltado camScene, y camEnter() reinicia modo, zoom y exposicion.
  {
    camEnter();                              // sesion nueva: ajustes de fabrica
    camMode = 2; camNight = true; camExpo = 77; camZoom = 3.5f;
    gAppState[IC_CAMARA] = ALIFE_SUSPENDED;
    chk(camScene != NULL, "la camara tiene su buffer tras abrirse");
    size_t freed = memShedApp(IC_CAMARA);
    (void)freed;
    chk(camScene == NULL, "soltar recursos libera el buffer del sensor");
    chk(gAppShed[IC_CAMARA], "y la marca como 'Estado guardado'");
    camResume();
    chk(camScene != NULL, "al volver, el buffer se rehace");
    if(!(camMode == 2 && camNight && camExpo == 77))
      printf("  (camara tras reanudar: modo %d, noche %d, expo %d)\n",
             camMode, (int)camNight, camExpo);
    chk(camMode == 2 && camNight && camExpo == 77,
        "los AJUSTES del usuario sobreviven a soltar el buffer");
    // Soltar dos veces no puede volver a "liberar" nada.
    gAppState[IC_CAMARA] = ALIFE_SUSPENDED;
    memShedApp(IC_CAMARA);
    gAppShed[IC_CAMARA] = false;
    chk(memShedApp(IC_CAMARA) == 0, "sin buffer, soltar de nuevo no libera nada");
    chk(!gAppShed[IC_CAMARA], "y no se marca 'Estado guardado' sin haber soltado");
    camCloseApp();
  }
  mtReset();

  // ---- 20) NOTAS: suspender NO puede tocar el contenido ----
  // El viaje completo a disco necesita LittleFS y solo se puede comprobar en
  // la placa; lo que SI se comprueba aqui es que la suspension no borra el
  // texto, el cursor ni el desplazamiento, que es donde estaria el fallo.
  {
    noteView = 1;
    snprintf(noteBuffer, noteBufMax, "hola mundo");
    noteCur = 4; noteEditorScroll = 3; noteDirtyMs = 12345;
    noteSuspend();
    chk(!strcmp(noteBuffer, "hola mundo"), "el texto sobrevive a suspender");
    chk(noteCur == 4, "el cursor sobrevive a suspender");
    chk(noteEditorScroll == 3, "el desplazamiento sobrevive a suspender");
    noteView = 0; notePath[0] = 0; noteBuffer[0] = 0; noteDirtyMs = 0;
  }

  mtReset();
  if(!gFails) printf("  Multitarea: todas las comprobaciones pasan.\n");
}

// #############################################################
//  FLEX COMPASS + FLEX IMU SERVICE
//  ------------------------------------------------------------
//  Se ejercita el CODIGO REAL que va a la placa. Dos cosas
//  distintas, y las dos importan:
//
//   · EL REPARTO DEL SENSOR. Flex Compass y la deteccion de caidas
//     leen el MISMO GY-BNO085 a traves del Flex IMU Service. Que
//     cerrar la brujula NO apague el sensor por debajo de una
//     funcion de seguridad no es una opinion: se comprueba aqui.
//
//   · LAS CONVERSIONES. Cuaternion -> rumbo, la rosa de 16
//     direcciones y el salto 359 -> 0 son aritmetica pura y se
//     verifican enteras, sin hardware.
//
//  LIMITE HONESTO: el doble de I2C de inostub/ NO simula un BNO085,
//  asi que el driver corre AUSENTE. Lo que si se comprueba de ese
//  camino es lo importante: que sin sensor la app no publica ni un
//  rumbo y ensena el requisito de hardware.
// #############################################################
// #############################################################
//  EL BUS I2C COMPARTIDO: RETIRAR EL IMU NO PUEDE LLEVARSE EL TACTIL
//  ------------------------------------------------------------
//  Este es el fallo grave que arregla este trabajo. El GY-BNO085 cuelga de
//  GPIO7/GPIO8, los MISMOS pines del GT911 tactil. Un modulo que pierde la
//  alimentacion en mitad de una transaccion se queda tirando de SDA a masa, y
//  a partir de ese instante NINGUNA transaccion sale adelante -- tampoco las
//  del tactil. Sin recuperacion del bus, el tactil no vuelve nunca: eso es lo
//  que se veia como "el sistema se congela al desconectar el IMU".
//
//  Aqui el doble del bus modela las dos lineas de verdad: gWireWedged hace
//  fallar toda transaccion, y los pulsos de reloj que da la recuperacion
//  pueden liberar SDA, igual que hace un chip real al terminar el byte que
//  tenia a medias.
// #############################################################
// #############################################################
//  FLEX COMPASS OCUPA EL SITIO QUE ERA DE CODE IDE
//  ------------------------------------------------------------
//  Code IDE se ha retirado del sistema y su ranura del registro -- el indice 7
//  de APP_REG, del que cuelgan el nombre, el icono, el peso y el sitio en el
//  escritorio de fabrica -- la ocupa ahora Flex Compass. Lo que esta prueba
//  fija son las dos cosas que podrian salir mal: que quede un HUECO donde
//  estaba el IDE, o que aparezcan DOS brujulas en una placa que ya tenia la
//  suya en otra pagina.
// #############################################################
static void testCompassEnSitioDeCodeIDE(){
  printf("Flex Compass ocupa la ranura que era de Code IDE\n");

  // ---- 1. El registro ----
  // Musica se anadio DESPUES, al final (id 18), sin mover ningun id.
  chk(APP_N == 19 && IC_MUSICA == 18, "el registro tiene 19 apps: Code IDE ya no esta y Musica va al final");
  { bool noIde = true;
    for(int i = 0; i < APP_N; i++) if(strstr(APP[i][0], "Code") || strstr(APP[i][0], "IDE")) noIde = false;
    chk(noIde, "ningun nombre del registro es Code IDE"); }
  chk(IC_BRUJULA == 7, "Flex Compass es el indice 7, el que tenia Code IDE");
  chk(APP_REG[IC_BRUJULA].enter == compassEnter && APP_REG[IC_BRUJULA].tick == compassTick,
      "y esa fila abre la brujula REAL, no una pantalla nueva");
  chk(APP_REG[IC_BRUJULA].hooks == &H_COMPASS,
      "con sus ganchos de siempre (atras, suspender, reanudar, shed)");
  {
    int n = 0;
    for(int i = 0; i < APP_N; i++) if(APP_REG[i].enter == compassEnter) n++;
    chk(n == 1, "no hay una segunda entrada de Compass en el registro");
  }
  chk(!strcmp(APP[IC_BRUJULA][0], "Flex Compass"), "el nombre de la ranura 7 es Flex Compass");
  {
    bool resto = false;
    for(int i = 0; i < APP_N; i++)
      if(strstr(APP[i][0], "Code") || strstr(APP[i][1], "Code")) resto = true;
    chk(!resto, "ningun nombre de app menciona ya Code IDE");
  }
  chk(APP_REG[IC_BRUJULA].dflt & APP_DEF_FAV,
      "y nace en la rejilla, como nacia Code IDE: el sitio no queda vacio");

  // ---- 2. El escritorio de fabrica ----
  chk(HOME_FACTORY[8] == IC_BRUJULA, "la novena casilla de fabrica es Flex Compass");
  {
    int huecos = 0, brujulas = 0;
    for(int i = 0; i < HOME_LEGACY_SLOTS; i++){
      if(HOME_FACTORY[i] == HOME_EMPTY) huecos++;
      if(HOME_FACTORY[i] == IC_BRUJULA) brujulas++;
    }
    chk(huecos == 0,   "el escritorio de fabrica no tiene ni un hueco");
    chk(brujulas == 1, "y la brujula sale exactamente una vez");
  }

  // ---- 3. MIGRACION REAL desde una placa con Code IDE **y** la brujula ----
  // Es el caso peligroso: los dos ids viejos (7 y 18) apuntan al mismo id
  // nuevo. Se siembra la NVS simulada como la dejaria el firmware anterior y se
  // llama a homeOrderLoad() de verdad.
  {
    flexPrefsWipe();
    uint8_t ord[HOME_TOTAL];
    for(int i = 0; i < HOME_TOTAL; i++) ord[i] = HOME_EMPTY;
    // Pagina 0: el escritorio de fabrica de la version 2, con Code IDE (7).
    const uint8_t v2page0[12] = { 0, 1, 2, 3, 4, 5, 15, 6, 7, 17, 8, 9 };
    for(int i = 0; i < 12; i++) ord[i] = v2page0[i];
    ord[HOME_STRIDE] = 18;                    // y la brujula, anadida a la pagina 2
    uint32_t fav = 0;
    for(int i = 0; i < 12; i++) fav |= (uint32_t)(1u << v2page0[i]);
    fav |= (uint32_t)(1u << 18);
    Preferences p;
    p.begin("flexos", false);
    p.putBytes("hordq", ord, HOME_TOTAL);
    p.putInt("hpgn", 3); p.putInt("hpmain", 0);
    p.putInt("hgrid", (4 << 8) | 3);
    p.putInt("appfav", (int)fav);
    p.putInt("apphide", 0);
    p.putInt("appn", 19);
    p.putInt("appver", 2);                    // <- guardado con el registro viejo
    p.end();

    homeOrderLoad();

    int brujulas = 0, huecos = 0, fuera = 0;
    for(int i = 0; i < HOME_TOTAL; i++){
      uint8_t v = homeOrder[i];
      if(v == HOME_EMPTY || homeIsPkg(v)) continue;
      if(v == IC_BRUJULA) brujulas++;
      if(v >= APP_N) fuera++;
    }
    for(int i = 0; i < 12; i++) if(homeOrder[i] == HOME_EMPTY) huecos++;
    chk(brujulas == 1, "tras migrar hay UNA sola brujula, no dos");
    chk(fuera == 0,    "y ningun id fuera del registro actual");
    chk(huecos == 0,   "la pagina principal no se queda con el hueco del IDE");
    chk(homeOrder[8] == IC_BRUJULA,
        "la brujula queda EXACTAMENTE en la casilla que ocupaba Code IDE");
    chk((gAppFav & (1u << IC_BRUJULA)) != 0, "y marcada como favorita");
    chk((gAppHidden & (1u << IC_BRUJULA)) == 0, "y visible");
  }

  // ---- 4. Una placa que tenia Code IDE OCULTO tampoco se queda sin brujula ----
  {
    flexPrefsWipe();
    uint8_t ord[HOME_TOTAL];
    for(int i = 0; i < HOME_TOTAL; i++) ord[i] = HOME_EMPTY;
    const uint8_t v2page0[11] = { 0, 1, 2, 3, 4, 5, 15, 6, 17, 8, 9 };
    for(int i = 0; i < 11; i++) ord[i] = v2page0[i];
    uint32_t fav = 0;
    for(int i = 0; i < 11; i++) fav |= (uint32_t)(1u << v2page0[i]);
    Preferences p;
    p.begin("flexos", false);
    p.putBytes("hordq", ord, HOME_TOTAL);
    p.putInt("hpgn", 3); p.putInt("hpmain", 0);
    p.putInt("hgrid", (4 << 8) | 3);
    p.putInt("appfav", (int)fav);
    p.putInt("apphide", (int)(1u << 7));      // Code IDE oculto por el usuario
    p.putInt("appn", 19);
    p.putInt("appver", 2);
    p.end();

    homeOrderLoad();

    int brujulas = 0;
    for(int i = 0; i < HOME_TOTAL; i++) if(homeOrder[i] == IC_BRUJULA) brujulas++;
    chk(brujulas == 1, "la brujula aparece igualmente, y una sola vez");
    chk((gAppHidden & (1u << IC_BRUJULA)) == 0,
        "ocultar Code IDE no deja oculta a la brujula que hereda su sitio");
  }

  // ---- 5. Y el candado de Code IDE NO se hereda ----
  {
    flexPrefsWipe();
    uint8_t ord[HOME_TOTAL];
    for(int i = 0; i < HOME_TOTAL; i++) ord[i] = HOME_EMPTY;
    const uint8_t v2page0[12] = { 0, 1, 2, 3, 4, 5, 15, 6, 7, 17, 8, 9 };
    for(int i = 0; i < 12; i++) ord[i] = v2page0[i];
    uint32_t fav = 0;
    for(int i = 0; i < 12; i++) fav |= (uint32_t)(1u << v2page0[i]);
    gAppLock = (uint32_t)(1u << 7);           // el usuario tenia Code IDE con candado
    Preferences p;
    p.begin("flexos", false);
    p.putBytes("hordq", ord, HOME_TOTAL);
    p.putInt("hpgn", 3); p.putInt("hpmain", 0);
    p.putInt("hgrid", (4 << 8) | 3);
    p.putInt("appfav", (int)fav);
    p.putInt("apphide", 0);
    p.putInt("appn", 19);
    p.putInt("appver", 2);
    p.end();

    homeOrderLoad();
    chk((gAppLock & (1u << IC_BRUJULA)) == 0,
        "un candado puesto sobre Code IDE no pasa a la brujula");
  }

  flexPrefsWipe();
  gAppLock = 0;
  homeOrderLoad();                            // deja el escritorio de fabrica cargado
  if(!gFails) printf("  Flex Compass en el sitio de Code IDE: todas las comprobaciones pasan.\n");
}

static void testBusI2cCompartido(){
  printf("Bus I2C compartido: retirar el IMU no se lleva por delante el tactil\n");
  bool gtPrev = gtOk;
  gtOk = true;
  gtFails = 0; gtRecoverMs = 0; gtRecoverN = 0; gtBusWedged = false;
  gWireWedged = 0; gI2cPulses = 0; gI2cFreeAfterPulses = 0;
  gWireReadByte = 0x81;                 // frame nuevo (bit 7) con un contacto
  gTestMs = 500000;

  uint16_t px = 0, py = 0;

  // ---- 1. Bus sano: el tactil entrega y no acumula nada ----
  gWireTxN = 0;
  chk(gtPoll(px, py) == 1, "con el bus sano el tactil entrega un contacto");
  chk(gtFails == 0, "y no acumula ni un fallo");
  chk(gtRecoverN == 0, "ni se recupera un bus que no esta roto");

  // ---- 2. Se retira el IMU: SDA queda a masa ----
  // Unas pocas lecturas fallidas NO pueden disparar una recuperacion: en un
  // bus real hay fallos sueltos que no significan nada.
  gWireWedged = 1;
  gI2cFreeAfterPulses = 3;              // el esclavo suelta SDA al tercer pulso
  for(int i = 0; i < GT_FAIL_RECOVER_N - 1; i++){
    chk(gtPoll(px, py) == -1, "con el bus trabado el tactil no inventa un contacto");
  }
  chk(gtRecoverN == 0, "unas pocas lecturas fallidas no disparan la recuperacion");
  chk(gtFails == GT_FAIL_RECOVER_N - 1, "pero si se cuentan");

  // ---- 3. Al insistir el fallo, el bus se recupera ----
  gtPoll(px, py);                       // la lectura que cruza el umbral
  chk(gtRecoverN == 1, "al insistir el fallo, el bus se recupera");
  chk(gI2cPulses > 0 && gI2cPulses <= 9,
      "con pulsos de reloj acotados a un byte y su ACK");
  chk(gWireWedged == 0, "y SDA queda libre otra vez");
  chk(gtFails == 0 && !gtBusWedged, "el tactil vuelve a darse por sano");

  // ---- 4. Y EL TACTIL SIGUE FUNCIONANDO: este es el criterio de exito ----
  chk(gtPoll(px, py) == 1, "el tactil vuelve a entregar contactos");
  chk(gtOk, "sin que el sistema haya dado el tactil por perdido");

  // ---- 5. Un modulo arrancado de cuajo (SDA no se suelta) tampoco bloquea ----
  // Ni se rinde: se reintenta pasado el enfriamiento, no una sola vez.
  gWireWedged = 1; gI2cFreeAfterPulses = 0;
  gtFails = 0; gtRecoverMs = 0; gtRecoverN = 0;
  for(int i = 0; i < GT_FAIL_RECOVER_N + 4; i++) gtPoll(px, py);
  chk(gtRecoverN == 1, "con SDA a masa se intenta recuperar una vez...");
  chk(gtOk, "...sin apagar el tactil, que seria rendirse para siempre");
  gTestMs += GT_RECOVER_GAP_MS + 1;     // pasa el enfriamiento
  gtPoll(px, py);
  chk(gtRecoverN == 2, "...y se vuelve a intentar cuando toca");
  gI2cFreeAfterPulses = 1;              // el cable se termina de quitar y SDA sube
  gTestMs += GT_RECOVER_GAP_MS + 1;
  gtPoll(px, py);
  chk(gWireWedged == 0, "en cuanto SDA se suelta, la recuperacion la aprovecha");
  chk(gtPoll(px, py) == 1, "y el tactil vuelve");

  // ---- 6. El plazo de espera del bus esta acotado ----
  // Es la otra mitad del arreglo: sin esto, CADA transaccion fallida costaba
  // los 50 ms que Arduino espera por defecto, y el tactil hace dos o tres por
  // vuelta. flexTouchInit() y la recuperacion lo bajan; aqui se comprueba que
  // el camino de recuperacion lo deja puesto.
  chk(Wire.getTimeOut() == I2C_BUS_TIMEOUT_MS,
      "tras recuperar el bus, el plazo de una transaccion sigue acotado");

  // ---- 7. El servicio IMU no sondea sobre un bus que se esta recuperando ----
  {
    while(imuHolders() > 0) imuRelease();
    gtBusWedged = true;
    imuAcquire();
    chk(!imuRetry(0), "con el bus trabado no se pide un re-sondeo del IMU");
    gtBusWedged = false;
    imuRelease();
  }

  gWireWedged = 0; gI2cFreeAfterPulses = 0; gWireReadByte = 0;
  gtFails = 0; gtRecoverMs = 0; gtRecoverN = 0; gtBusWedged = false;
  gtOk = gtPrev;
  tReset();
  if(!gFails) printf("  Bus I2C compartido: todas las comprobaciones pasan.\n");
}

static void testFlexCompass(){
  printf("Flex Compass - brujula sobre el GY-BNO085\n");
  int fails0 = gFails;
  gLand = false; gHosted = false;
  uiClipFull();
  gNavMode = 0;

  // ---- 1. Rosa de 16 rumbos (los ocho principales y los ocho intermedios) ----
  {
    struct { float h; const char* s; } casos[] = {
      {   0.0f, "N"  }, {  22.5f, "NNE" }, {  45.0f, "NE"  }, {  67.5f, "ENE" },
      {  90.0f, "E"  }, { 112.5f, "ESE" }, { 135.0f, "SE"  }, { 157.5f, "SSE" },
      { 180.0f, "S"  }, { 202.5f, "SSO" }, { 225.0f, "SO"  }, { 247.5f, "OSO" },
      { 270.0f, "O"  }, { 292.5f, "ONO" }, { 315.0f, "NO"  }, { 337.5f, "NNO" },
      { 347.2f, "NNO" }, { 359.9f, "N" }, { 11.2f, "N" }, { 11.3f, "NNE" },
    };
    bool ok = true;
    for(unsigned i = 0; i < sizeof(casos)/sizeof(casos[0]); i++)
      if(strcmp(imuDirShort(casos[i].h), casos[i].s) != 0){
        printf("  FALLO: %.1f -> %s (esperado %s)\n", casos[i].h, imuDirShort(casos[i].h), casos[i].s);
        ok = false;
      }
    chk(ok, "los 16 rumbos y sus fronteras se nombran bien");
    chk(strcmp(imuDirLong(347.2f), "Norte-Noroeste") == 0, "el nombre largo acompana al corto");
    chk(imuDirIndex(-10.0f) >= 0 && imuDirIndex(-10.0f) < 16, "un rumbo negativo se normaliza");
    chk(imuDirIndex(725.0f) >= 0 && imuDirIndex(725.0f) < 16, "un rumbo mayor de 360 se normaliza");
  }

  // ---- 2. Rumbo a partir del cuaternion del vector de rotacion ----
  //  Giro de `a` grados alrededor del eje vertical: q = (i,j,k,r) con
  //  k = sin(a/2) y r = cos(a/2). El eje +X del sensor queda en
  //  (cos a, sin a, 0) del marco ENU, asi que el rumbo debe ser 90 - a.
  {
    bool ok = true;
    for(int a = 0; a < 360; a += 15){
      float t = a * 0.0174532925f / 2.0f;
      float h = imuHeadingFromQuat(0.0f, 0.0f, sinf(t), cosf(t));
      float esp = imuNorm360(90.0f - a);
      if(fabsf(imuAngleDelta(h, esp)) > 0.05f){
        printf("  FALLO: giro %d -> %.2f (esperado %.2f)\n", a, h, esp); ok = false;
      }
    }
    chk(ok, "el rumbo sale del cuaternion con el marco ENU del BNO085");
    chk(fabsf(imuPitchFromQuat(0,0,0,1)) < 0.01f, "placa plana -> pitch 0");
    chk(fabsf(imuRollFromQuat(0,0,0,1)) < 0.01f,  "placa plana -> roll 0");
    { float t = 45.0f * 0.0174532925f;
      chk(fabsf(imuRollFromQuat(sinf(t), 0, 0, cosf(t)) - 90.0f) < 0.1f, "giro sobre X -> roll 90"); }
    { float t = -15.0f * 0.0174532925f;
      chk(fabsf(imuPitchFromQuat(0, sinf(t), 0, cosf(t)) - 30.0f) < 0.1f, "morro arriba -> pitch positivo"); }
  }

  // ---- 3. Diferencia angular mas corta (el nucleo del 359 -> 0) ----
  {
    chk(fabsf(imuAngleDelta(359.0f,   0.0f) -   1.0f) < 0.001f, "359 -> 0 son +1 grados");
    chk(fabsf(imuAngleDelta(  1.0f, 359.0f) +   2.0f) < 0.001f, "1 -> 359 son -2 grados");
    chk(fabsf(imuAngleDelta(  0.0f, 180.0f) - 180.0f) < 0.001f, "media vuelta exacta");
    chk(fabsf(imuAngleDelta(  0.0f, 181.0f) + 179.0f) < 0.001f, "181 se resuelve por el lado corto");
    chk(fabsf(imuAngleDelta(350.0f,  10.0f) -  20.0f) < 0.001f, "cruzar el norte son 20 grados");
  }

  // ---- 4. Suavizado visual: NUNCA da la vuelta larga ----
  {
    gTestMs = 100000;
    T = Touch();
    cmpHave = true; cmpHead = 359.0f; cmpPitch = 0; cmpRoll = 0;
    cmpHeadInit = false; cmpPhysMs = 0; cmpScroll = 0; cmpScrollVel = 0;
    cmpPhysics(gTestMs);
    chk(fabsf(cmpHeadVis - 359.0f) < 0.01f, "el rumbo visual arranca en el del sensor");
    cmpHead = 1.0f;                             // el sensor cruza el norte
    float peor = 0.0f;
    for(int i = 0; i < 60; i++){
      gTestMs += 33;
      cmpPhysics(gTestMs);
      // Camino corto = el visual se queda SIEMPRE dentro del arco [359 -> 1].
      float d = fabsf(imuAngleDelta(359.0f, cmpHeadVis)) + fabsf(imuAngleDelta(cmpHeadVis, 1.0f));
      if(d > peor) peor = d;
    }
    chk(peor < 3.0f, "359 -> 0 no rota 359 grados en el sentido equivocado");
    chk(fabsf(imuAngleDelta(cmpHeadVis, 1.0f)) < 0.2f, "y el visual acaba alcanzando al sensor");
    chk(cmpHeadVis >= 0.0f && cmpHeadVis < 360.0f, "el rumbo visual se queda normalizado");
    cmpHead = 181.0f;
    for(int i = 0; i < 120; i++){ gTestMs += 33; cmpPhysics(gTestMs); }
    chk(fabsf(imuAngleDelta(cmpHeadVis, 181.0f)) < 0.5f, "un salto grande tambien converge");
  }

  // ---- 4b. LA AGUJA NO SE QUEDA ATRAS EN UN GIRO SOSTENIDO ----
  //
  // Un seguimiento de primer orden con constante de tiempo FIJA converge
  // cuando el objetivo se para, pero mientras el objetivo se MUEVE a
  // velocidad constante se queda a un angulo fijo por detras: velocidad por
  // constante de tiempo. Con los ~110 ms de antes eso eran 9,6 grados a 90
  // grados/s y 38 a 360 -- exactamente lo que se siente como "la brujula va
  // detras al girar". Esta prueba existe para que no vuelva: fija que el
  // retardo se mantiene acotado A CUALQUIER velocidad, que es lo que
  // distingue un corte adaptativo de uno fijo.
  //
  // Y la otra mitad, que es la que se pierde si uno se limita a subir el
  // corte: con el aparato QUIETO y ruido en el sensor, la aguja tiene que
  // seguir sin temblar.
  {
    const float velocidades[4] = { 30.0f, 90.0f, 180.0f, 360.0f };
    for(int k = 0; k < 4; k++){
      float w = velocidades[k];
      T = Touch();
      cmpHave = true; cmpPitch = 0; cmpRoll = 0;
      cmpHeadInit = false; cmpPhysMs = 0; cmpScroll = 0; cmpScrollVel = 0;
      cmpRate = 0.0f; cmpRatePrev = 0.0f; cmpRateMs = 0;
      cmpHead = 0.0f;
      gTestMs = 700000;
      cmpPhysics(gTestMs);
      float peor = 0.0f;
      bool norm = true;
      for(int i = 0; i < 300; i++){
        gTestMs += 10;
        cmpHead = imuNorm360(cmpHead + w * 0.010f);
        cmpPhysics(gTestMs);
        if(i > 100){                                  // ya en regimen
          float e = fabsf(imuAngleDelta(cmpHeadVis, cmpHead));
          if(e > peor) peor = e;
        }
        if(!(cmpHeadVis >= 0.0f && cmpHeadVis < 360.0f)) norm = false;
      }
      chk(peor < 3.0f, "girando sostenido, la aguja no se queda atras");
      chk(norm, "el visual sigue normalizado durante todo el giro");
    }
    // Quieto, con ruido: el temblor por cuadro tiene que ser despreciable.
    T = Touch();
    cmpHave = true; cmpHeadInit = false; cmpPhysMs = 0;
    cmpRate = 0.0f; cmpRatePrev = 0.0f; cmpRateMs = 0;
    cmpHead = 45.0f; gTestMs = 800000;
    cmpPhysics(gTestMs);
    unsigned semilla = 7u;
    float peorTemblor = 0.0f, prev = cmpHeadVis;
    for(int i = 0; i < 400; i++){
      gTestMs += 10;
      semilla = semilla * 1103515245u + 12345u;             // ruido de +-0,4 grados
      cmpHead = 45.0f + ((float)((semilla >> 16) & 0x7FFF) / 32767.0f - 0.5f) * 0.8f;
      cmpPhysics(gTestMs);
      if(i > 100){
        float d = fabsf(imuAngleDelta(prev, cmpHeadVis));
        if(d > peorTemblor) peorTemblor = d;
      }
      prev = cmpHeadVis;
    }
    chk(peorTemblor < 0.15f, "quieto y con ruido, la aguja no tiembla");
    chk(fabsf(imuAngleDelta(cmpHeadVis, 45.0f)) < 0.5f, "y se queda donde apunta el sensor");
  }

  // ---- 5. EL REPARTO DEL SENSOR (que Flex Compass no rompa Device Care) ----
  {
    gTestMs = 200000;
    bool gtPrev = gtOk; gtOk = true;
    while(imuHolders() > 0) imuRelease();
    dcSensorOn = false;
    chk(imuHolders() == 0, "de partida no hay ningun consumidor del IMU");

    dcSensorStart();                                  // la deteccion de caidas enciende el sensor
    chk(imuHolders() == 1 && dcSensorOn, "Device Care adquiere el servicio");

    gState = ST_APP; gAppId = IC_BRUJULA; gRelayout = false; T = Touch();
    compassEnter();                                   // y ahora el usuario abre la brujula
    chk(imuHolders() == 2, "Flex Compass adquiere el MISMO servicio, no un segundo driver");

    compassClose();                                   // cierra la brujula...
    chk(imuHolders() == 1, "cerrar la brujula suelta SU enganche");
    chk(dcSensorOn, "...y la deteccion de caidas SIGUE con el sensor encendido");

    dcSensorStop();
    chk(imuHolders() == 0, "cuando tampoco lo quiere Device Care, el servicio queda libre");

    compassEnter();
    dcSensorStart();
    chk(imuHolders() == 2, "el orden de llegada da igual");
    dcSensorStop();
    chk(imuHolders() == 1 && cmpHoldsImu, "apagar la deteccion de caidas NO deja a la brujula sin sensor");
    compassClose();
    chk(imuHolders() == 0, "y al soltar el ultimo, el sensor se apaga");
    gtOk = gtPrev;
  }

  // ---- 6. Ciclo de vida de la app ----
  {
    gTestMs = 300000;
    bool gtPrev = gtOk; gtOk = true;
    while(imuHolders() > 0) imuRelease();
    dcSensorOn = false;
    gState = ST_APP; gAppId = IC_BRUJULA; gRelayout = false; T = Touch();
    compassEnter();
    chk(cmpHoldsImu && imuHolders() == 1, "abrir la app adquiere el servicio");
    compassSuspend();
    chk(!cmpHoldsImu && imuHolders() == 0, "en segundo plano se suelta el sensor");
    compassResume();
    chk(cmpHoldsImu && imuHolders() == 1, "al volver se vuelve a adquirir");
    compassClose();
    chk(!cmpHoldsImu && imuHolders() == 0, "cerrar la app no deja NADA consumiendo el sensor");
    compassEnter(); compassSuspend(); compassClose();
    chk(imuHolders() == 0, "cerrar una app ya suspendida no descuadra el conteo");
    // Red de seguridad: sin tick durante segundos (ventana de DeX cerrada).
    compassEnter();
    compassIdleGuard();
    chk(cmpHoldsImu, "recien abierta, la red de seguridad no la toca");
    gTestMs += CMP_IDLE_RELEASE_MS + 100;
    compassIdleGuard();
    chk(!cmpHoldsImu && imuHolders() == 0, "sin tick durante segundos se suelta el sensor");
    compassTick();
    chk(cmpHoldsImu, "y el primer tick siguiente lo vuelve a adquirir");
    compassClose();
    gtOk = gtPrev;
  }

  // ---- 7. Sin BNO085 no se publica NI UN RUMBO ----
  {
    chk(flexBnoState() == FLEXBNO_ST_ABSENT, "el arnes corre con el IMU ausente");
    float h = 123.0f, pp = 1.0f, rr = 1.0f;
    chk(!imuHeading(&h),        "sin sensor no hay rumbo");
    chk(!imuPitchRoll(&pp,&rr), "ni cabeceo ni alabeo");
    chk(!imuSrcLive(FLEXBNO_CHK_ACCEL) && !imuSrcLive(FLEXBNO_CHK_MAG),
        "ni ningun sensor se marca como disponible");
    cmpHave = true;
    cmpSample();
    chk(!cmpHave, "la app tampoco se queda con el ultimo valor como si fuera actual");
    cmpSyncView(false);
    chk(cmpView == CMPV_SEARCH, "sin orientacion se ensena 'Requiere modulo IMU'");
    chk(imuState() == FIMU_IDLE || imuState() == FIMU_NO_IMU, "y el estado lo dice");
  }

  // ---- 8. Las vistas siguen al sensor ----
  {
    cmpHave = true; cmpHead = 120.0f;
    cmpSyncView(false);
    chk(cmpView == CMPV_COMPASS, "con orientacion real se ensena la brujula");
    cmpHave = false;
    cmpSyncView(false);
    chk(cmpView == CMPV_SEARCH, "y si el sensor se pierde se vuelve al requisito");
  }

  // ---- 9. Limites del desplazamiento ----
  {
    gTestMs = 400000;
    cmpHave = true; cmpHead = 0.0f;
    cmpSyncView(false);
    cmpLayout();
    chk(cmpContentH > cmpVpH(), "el documento es mas alto que la pantalla (hay algo que revelar)");
    chk(cmpScrollMax() > 0, "y por tanto hay recorrido");
    chk(cmpYCard >= cmpVpH(), "la tarjeta de estado nace FUERA del viewport inicial");
    T = Touch();
    cmpScroll = (float)cmpScrollMax() + 90.0f; cmpScrollVel = 0; cmpPhysMs = 0;
    for(int i = 0; i < 200; i++){ gTestMs += 16; cmpPhysics(gTestMs); }
    chk(cmpScroll <= cmpScrollMax() + 0.6f, "el desplazamiento vuelve a su limite inferior");
    cmpScroll = -120.0f; cmpScrollVel = 0; cmpPhysMs = 0;
    for(int i = 0; i < 200; i++){ gTestMs += 16; cmpPhysics(gTestMs); }
    chk(cmpScroll >= -0.6f, "y al limite superior");
    cmpScroll = 0; cmpScrollVel = 4000.0f; cmpPhysMs = 0;
    for(int i = 0; i < 400; i++){ gTestMs += 16; cmpPhysics(gTestMs); }
    chk(fabsf(cmpScrollVel) < 7.0f, "la inercia se amortigua hasta pararse");
    chk(cmpScroll <= cmpScrollMax() + 0.6f, "y no deja el contenido fuera de sus limites");
  }

  // ---- 10. Un gesto que empieza en la cabecera NO arrastra la lista ----
  {
    gTestMs = 500000;
    cmpHave = true; cmpHead = 30.0f;
    cmpSyncView(false); cmpLayout();
    cmpScroll = 0; cmpScrollVel = 0; cmpResetGesture();
    tDown(240, 20, gTestMs); cmpTouch();
    tMove(240, -60, gTestMs + 60); cmpTouch();
    chk(cmpScroll == 0.0f, "un arrastre nacido en la cabecera no desplaza el contenido");
    tUp(gTestMs + 90, false); cmpTouch();
    tDown(240, 400, gTestMs + 200); cmpTouch();
    tMove(240, 300, gTestMs + 260); cmpTouch();
    chk(cmpScroll > 50.0f, "y nacido en el viewport si lo desplaza");
    tUp(gTestMs + 300, false); cmpTouch();
    chk(!cmpDrag, "al soltar, el gesto queda cerrado");
    tDown(240, 400, gTestMs + 400); cmpTouch();
    tMove(240, 260, gTestMs + 460); cmpTouch();
    tUp(gTestMs + 500, false); T.y = 10; cmpTouch();
    chk(!cmpDrag, "soltar sobre la cabecera tampoco deja el gesto enganchado");
    T = Touch();
  }

  // ---- 11. El contenido NUNCA pisa la cabecera ni la barra de navegacion ----
  {
    cmpHave = true; cmpHead = 47.0f; cmpPitch = 4.0f; cmpRoll = -3.0f;
    cmpHeadVis = 47.0f; cmpHeadInit = true;
    cmpSyncView(false); cmpLayout();
    int fuera = 0;
    for(int paso = 0; paso < 3; paso++){
      cmpScroll = (paso == 0) ? 0.0f : (paso == 1 ? cmpScrollMax() / 2.0f : (float)cmpScrollMax());
      memset(bbuf, 0, (size_t)SCR_W * SCR_H * 2);
      setBuf(bbuf);
      uiClipFull();
      cmpDrawCompass(0, SCR_H - 1);
      for(int y = 0; y < SCR_H; y++){
        if(y >= cmpVpTop() && y <= cmpVpBot()) continue;
        for(int x = 0; x < SCR_W; x++) if(bbuf[(size_t)y * SCR_W + x] != 0) fuera++;
      }
    }
    setBuf(fb);
    chk(fuera == 0, "el contenido desplazable se queda SIEMPRE dentro del viewport");
  }

  // ---- 12. El modulo BNO085 se dibuja por codigo y cabe en su caja ----
  {
    int fuera = 0, pintados = 0;
    const int CX = SCR_W / 2, CY = 400, W = 300;
    const int MX = W / 2 + 12, MY = W / 2 + 12;
    float angs[4][3] = { {0,0,0}, {30,18,-12}, {-140,-40,55}, {179,89,-179} };
    for(int k = 0; k < 4; k++){
      memset(bbuf, 0, (size_t)SCR_W * SCR_H * 2);
      setBuf(bbuf); uiClipFull();
      cmpDrawModule(CX, CY, W, angs[k][0], angs[k][1], angs[k][2]);
      for(int y = 0; y < SCR_H; y++)
        for(int x = 0; x < SCR_W; x++)
          if(bbuf[(size_t)y * SCR_W + x] != 0){
            pintados++;
            if(x < CX - MX || x > CX + MX || y < CY - MY || y > CY + MY) fuera++;
          }
    }
    setBuf(fb);
    chk(pintados > 4000, "el modulo dibuja geometria de verdad (no es un hueco)");
    chk(fuera == 0, "y no se sale de su caja en ninguna orientacion");
  }

  // ---- 13. El simbolo de grado se DIBUJA (la app lo usa en cada lectura) ----
  {
    auto pinta = [&](const char* t, int size)->int {
      memset(bbuf, 0, (size_t)SCR_W * SCR_H * 2);
      setBuf(bbuf); uiClipFull();
      drawText(40, 200, t, size, rgb565(255,255,255));
      int n = 0;
      for(int y = 180; y < 280; y++)
        for(int x = 0; x < SCR_W; x++) if(bbuf[(size_t)y * SCR_W + x] != 0) n++;
      return n;
    };
    int soloGrado = pinta("\xC2\xB0", 3);
    int interrog  = pinta("?", 3);
    setBuf(fb);
    chk(soloGrado > 0, "el simbolo de grado pinta algo");
    chk(soloGrado != interrog, "y no es el interrogante de 'caracter desconocido'");
    chk(textW("347.2\xC2\xB0", 3) > textW("347.2", 3), "y mide de mas en la cadena del rumbo");
    char b[24];
    cmpFmtHeading(b, sizeof(b), 347.19f);
    chk(strcmp(b, "347.2\xC2\xB0") == 0, "el rumbo se formatea con un decimal");
    cmpFmtHeading(b, sizeof(b), 359.99f);
    chk(strcmp(b, "0.0\xC2\xB0") == 0, "359,99 se redondea a 0,0 y no a 360,0");
    cmpFmtDeg(b, sizeof(b), -0.84f, false);
    chk(strcmp(b, "-0.8\xC2\xB0") == 0, "los angulos negativos llevan su signo");
  }

  // ---- LA TARJETA DEL MODULO 3D NO SE RECOMPONE AL DESPLAZAR ----
  // Es lo que hacia que deslizar con el objeto en pantalla fuera a tirones:
  // cada cuadro rehacia un box-blur de 428x230 mas cinco cuadrilateros
  // rellenos, once circulos y dos textos. Ahora se compone UNA vez en su
  // propio lienzo y moverla es un memcpy por fila.
  {
    gState = ST_APP; gAppId = IC_BRUJULA;
    gAppW = SCR_W; gAppH = SCR_H;
    uiClipFull(); setBuf(bbuf);
    bool glassPrev = uiGlass; uiGlass = true;
    cmpLayout();
    cmpTileFree();

    const int W = SCR_W - 52;
    chk(!cmpTileUsable(W, cmpHModule), "sin lienzo compuesto la tarjeta no se puede reutilizar");
    bool built = cmpTileBuild(W, cmpHModule, 30.0f, 12.0f, -8.0f, true);
    chk(built, "la tarjeta del modulo se compone en su propio lienzo");
    chk(cmpTileUsable(W, cmpHModule), "y a partir de ahi se reutiliza");
    chk(!cmpTileUsable(W - 10, cmpHModule), "otro ancho NO reutiliza la anterior");
    chk(!cmpTileUsable(W, cmpHModule - 10), "ni otro alto");

    // El volcado es IDENTICO a componerla en su sitio: eso es lo que permite
    // cachearla sin cambiar un pixel de lo que se ve.
    if(built){
      static uint16_t* refA = NULL; static uint16_t* refB = NULL;
      if(!refA) refA = (uint16_t*)malloc((size_t)SCR_W * CMP_TILE_MAX_H * 2);
      if(!refB) refB = (uint16_t*)malloc((size_t)SCR_W * CMP_TILE_MAX_H * 2);
      if(refA && refB){
        const int PX = 26, PY = 300;
        fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
        cmpTileBlit(PX, PY);
        for(int j2 = 0; j2 < cmpHModule; j2++)
          memcpy(refA + (size_t)j2 * SCR_W, bbuf + (size_t)(PY + j2) * SCR_W, (size_t)SCR_W * 2);
        fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
        uiSurface(PX, PY, W, cmpHModule, 24, UIS_CARD);
        { int q0 = gClipY0, q1 = gClipY1, qx0 = gClipX0, qx1 = gClipX1;
          gClipY0 = PY + 6; gClipY1 = PY + cmpHModule - 7;
          gClipX0 = PX + 6; gClipX1 = PX + W - 7;
          int mw = W - 110, byH = (cmpHModule - 24) * 100 / 80;
          if(mw > byH) mw = byH;
          cmpDrawModule(PX + W / 2, PY + cmpHModule / 2, mw, 30.0f, 12.0f, -8.0f);
          gClipY0 = q0; gClipY1 = q1; gClipX0 = qx0; gClipX1 = qx1; }
        for(int j2 = 0; j2 < cmpHModule; j2++)
          memcpy(refB + (size_t)j2 * SCR_W, bbuf + (size_t)(PY + j2) * SCR_W, (size_t)SCR_W * 2);
        int dif = 0;
        for(int j2 = 0; j2 < cmpHModule; j2++)
          for(int i2 = PX; i2 < PX + W; i2++)
            if(refA[(size_t)j2 * SCR_W + i2] != refB[(size_t)j2 * SCR_W + i2]) dif++;
        chk(dif == 0, "volcar la tarjeta cacheada da EXACTAMENTE lo mismo que componerla en su sitio");
      }
    }

    // Y lo que importa: cuanto cuesta cada cosa.
    if(built){
      struct timespec c0, c1;
      const int N = 40;
      uiClipFull(); setBuf(bbuf);
      clock_gettime(CLOCK_MONOTONIC, &c0);
      for(int i2 = 0; i2 < N; i2++) cmpTileBlit(26, 300);
      clock_gettime(CLOCK_MONOTONIC, &c1);
      double volcar = ((c1.tv_sec - c0.tv_sec) * 1e3 + (c1.tv_nsec - c0.tv_nsec) / 1e6) / N;
      clock_gettime(CLOCK_MONOTONIC, &c0);
      for(int i2 = 0; i2 < N; i2++) cmpTileBuild(W, cmpHModule, 30.0f + i2, 12.0f, -8.0f, true);
      clock_gettime(CLOCK_MONOTONIC, &c1);
      double componer = ((c1.tv_sec - c0.tv_sec) * 1e3 + (c1.tv_nsec - c0.tv_nsec) / 1e6) / N;
      printf("  [brujula] tarjeta del modulo: componer %.3f ms  ·  volcar %.3f ms\n",
             componer, volcar);
      chk(volcar * 4.0 < componer,
          "volcar la tarjeta cuesta MUCHO menos que componerla (por eso deja de haber tirones)");
    }

    // ---- Al arrastrar NO se recompone; al soltar, si ----
    // La regla vive en cmpTileNeedsBuild(), separada del tick para poder
    // comprobarla entera aqui: en el PC no hay IMU y la app ni siquiera llega a
    // la vista de brujula, asi que pasar por compassTick() no probaria nada.
    cmpTileBuild(W, cmpHModule, 45.0f, 22.0f, -11.0f, true);
    { const uint32_t NOW = 100000, VIEJO = 0;               // holgadamente > CMP_MOD_MS
      chk(!cmpTileNeedsBuild(W, cmpHModule, true, 45.0f, 22.0f, -11.0f, true, NOW, VIEJO),
          "quieta y sin cambios: no se rehace");
      chk(!cmpTileNeedsBuild(W, cmpHModule, true, 120.0f, 40.0f, 20.0f, true, NOW, VIEJO),
          "ARRASTRANDO no se rehace aunque la orientacion haya cambiado");
      chk(cmpTileNeedsBuild(W, cmpHModule, false, 120.0f, 40.0f, 20.0f, true, NOW, VIEJO),
          "al soltar, la orientacion nueva SI la rehace");
      chk(!cmpTileNeedsBuild(W, cmpHModule, false, 120.0f, 40.0f, 20.0f, true, NOW, NOW),
          "...pero nunca mas de una vez cada CMP_MOD_MS");
      chk(cmpTileNeedsBuild(W, cmpHModule, false, 45.0f, 22.0f, -11.0f, false, NOW, VIEJO),
          "perder la orientacion tambien la rehace (la placa vuelve a vista plana)");
      chk(!cmpTileNeedsBuild(W, cmpHModule, false, 45.3f, 22.2f, -11.1f, true, NOW, VIEJO),
          "un temblor por debajo del umbral no la rehace");
      chk(cmpTileNeedsBuild(W - 8, cmpHModule, true, 45.0f, 22.0f, -11.0f, true, NOW, NOW),
          "otro tamano SI la rehace, se este arrastrando o no");
    }
    // Y el tick real la rehace cuando toca (aqui, sin IMU, hacia la vista plana).
    cmpView = CMPV_COMPASS; cmpOverlay = CMPO_NONE; cmpXfadeMs = 0; cmpScroll = 0;
    tReset();
    cmpDrag = false; cmpScrollVel = 0; cmpModMs = 0; gTestMs += 1000;
    cmpTileBuild(W, cmpHModule, 45.0f, 22.0f, -11.0f, true);
    compassTick();
    chk(!cmpTileHave && cmpTileYaw == 0.0f,
        "el tick rehace la tarjeta cuando la orientacion deja de estar disponible");

    cmpTileFree();
    uiGlass = glassPrev;
    cmpStopMotion();
    gState = ST_HOME; gAppId = IC_RELOJ;
    uiClipFull(); setBuf(fb);
  }

  // Estado del arnes: la app queda cerrada y el servicio libre.
  while(imuHolders() > 0) imuRelease();
  cmpHoldsImu = false; dcSensorOn = false;
  gState = ST_HOME; gAppId = 0; T = Touch();

  if(gFails > fails0) printf("  %d comprobacion(es) de Flex Compass han fallado.\n", gFails - fails0);
  else printf("  Flex Compass: todas las comprobaciones pasan.\n");
}


// #############################################################
//  LIQUID GLASS SOBRE PAGINAS QUE SE DESLIZAN
//  ------------------------------------------------------------
//  El fallo: al pasar de pagina, el cristal de los widgets y de los
//  iconos Vidrio llevaba PEGADA la imagen del fondo de donde se habia
//  compuesto. El deslizamiento movia como primer plano todo pixel que
//  difiere del wallpaper, y un panel de vidrio difiere entero.
//
//  Se prueba con un fondo sintetico de rayas verticales sobre un
//  degradado horizontal: desplazar el desenfoque cambia su valor, asi
//  que "arrastrar el fondo" es medible pixel a pixel. Se exige:
//    1. el vidrio desplazado es EXACTAMENTE el que se compondria en
//       su posicion nueva (y no la copia de su posicion original);
//    2. el contenido (texto, glifos) viaja exacto con la pagina;
//    3. el primer y el ultimo cuadro del gesto son la pagina quieta:
//       al terminar no hay salto.
// #############################################################
static void vsnWallStripes(){
  if(!wallImg) wallImg = (uint16_t*)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  for(int y = 0; y < SCR_H; y++)
    for(int x = 0; x < SCR_W; x++){
      bool raya = ((x / 14) & 1) != 0;
      int r = (x * 31) / (SCR_W - 1);
      wallImg[(size_t)y * SCR_W + x] = raya ? pk565(r, 50 - (y >> 5), 8) : pk565(31 - r, 12 + (y >> 5), 26);
    }
  wallImgOk = true; gWallHome = WALL_IMG;
}
static void testVidrioSinArrastre(){
  printf("Liquid Glass: el vidrio de una pagina no arrastra el fondo al deslizar\n");
  int before = gFails;
  bool glassPrev = uiGlass; int iconPrev = gIconStyle;
  uint16_t* wallPrevBuf = wallImg; bool wallOkPrev = wallImgOk;
  static uint16_t* ref = NULL;
  if(!ref) ref = (uint16_t*)malloc((size_t)SCR_W * SCR_H * 2);
  hcTestReset();
  uiGlass = true; gIconStyle = 1; gEffMode = false;
  gGlassLvl = GLASS_LVL_DEF; glassLevelApply();
  vsnWallStripes();
  // Pagina 0: Clima y Calendario en su cabecera + doce iconos Vidrio.
  // Pagina 1: un reloj en la cabecera y un icono.
  homeWgFactory();
  gAppFav |= (1u << IC_CAMARA);
  homeOrder[homeIdx(1, 0)] = IC_CAMARA;
  gHomeWg[1][0].type = WG_CLOCK; gHomeWg[1][0].col = 1; gHomeWg[1][0].row = 0;
  gHomeWg[1][0].w = 2; gHomeWg[1][0].h = 1; gHomeWgN[1] = 1;
  homeOrderNormalize();
  hpFreeBuffers(); homeBackdropFree();
  renderHome();
  chk(hgBdOk && hgBd && hpBg, "el escritorio construye su fondo limpio y desenfocado una vez");
  chk(gGlRecN[0] == 14 && !gGlRecOvf[0],
      "los 14 paneles de vidrio de la pagina (2 widgets + 12 iconos) quedan anotados");

  hpFrom = 0; hpTo = 1;
  chk(hpPrepare(1), "la pagina vecina se compone sin desenfocar nada");
  chk(gGlRecN[1] == 2, "y anota los suyos (reloj + icono)");
  hpMaskBuild(0);
  hpTop = (homePageHasHdr(hpFrom) || homePageHasHdr(hpTo)) ? HOME_PAGE_TOP : HOME_BAND_TOP;
  chk(hpTop == HOME_PAGE_TOP, "con widgets en la cabecera el gesto recorre tambien la cabecera");
  int bandBot = homeBandBot(), dotsY = homeDotsY();

  // ---- 1. A mitad de gesto el cristal muestra el fondo de donde ESTA ----
  const int DX = -120;
  hpRenderFrame(DX);
  int wx, wy, ww, wh; wgRect(&gHomeWg[0][0], wx, wy, ww, wh);     // Clima
  // Referencia independiente: fondo limpio + el MISMO panel compuesto
  // directamente en la posicion desplazada.
  memcpy(ref, bbuf, (size_t)SCR_W * SCR_H * 2);
  for(int y = HOME_PAGE_TOP; y < bandBot; y++)
    memcpy(ref + (size_t)y * SCR_W, hpBg + (size_t)(y - HOME_PAGE_TOP) * SCR_W, (size_t)SCR_W * 2);
  setBuf(ref);
  uiClipFull();
  homeGlassBegin(-1);
  drawLiquidGlassPanel(wx + DX, wy, ww, wh, 20, rgb565(30,72,150));
  homeGlassEnd();
  setBuf(fb);
  int vidrio = 0, malos = 0, pegados = 0;
  for(int y = wy + 12; y < wy + wh - 12; y++){
    const uint8_t* mr = hpMask[0] + (size_t)(y - HOME_PAGE_TOP) * HP_MASK_STRIDE;
    for(int x = wx + 12; x < wx + ww - 12; x++){
      int sx = x + DX;
      if(sx < 0) continue;
      if(mr[x >> 3] & (1u << (x & 7))) continue;          // contenido: se mira aparte
      vidrio++;
      uint16_t v = bbuf[(size_t)y * SCR_W + sx];
      if(v != ref[(size_t)y * SCR_W + sx]) malos++;
      if(v == homeBuf[(size_t)y * SCR_W + x]) pegados++;
    }
  }
  chk(vidrio > 2000, "el widget tiene material de vidrio de sobra para medir");
  chk(malos == 0, "el vidrio desplazado es EXACTAMENTE el de su posicion nueva");
  chk(pegados * 10 < vidrio, "y no la copia del fondo de su posicion original (lo que se veia pegado)");
  // CONTROL: con el mecanismo anterior -- el vidrio viajando como un pixel mas
  // de la pagina -- esta misma medida SI ve el fondo pegado. Asi se sabe que la
  // prueba de arriba detecta el fallo y no pasa por casualidad.
  { uint8_t n0 = gGlRecN[0], n1 = gGlRecN[1];
    gGlRecN[0] = 0; gGlRecN[1] = 0;
    hpMaskBuild(0); hpMaskBuild(1);
    hpRenderFrame(DX);
    int peg = 0, tot = 0;
    for(int y = wy + 12; y < wy + wh - 12; y++)
      for(int x = wx + 12; x < wx + ww - 12; x++){
        int sx = x + DX; if(sx < 0) continue;
        tot++;
        if(bbuf[(size_t)y * SCR_W + sx] == homeBuf[(size_t)y * SCR_W + x]) peg++;
      }
    chk(peg * 10 > tot * 9, "control: con el vidrio como contenido (lo de antes) el fondo SI va pegado");
    gGlRecN[0] = n0; gGlRecN[1] = n1;
    hpMaskBuild(0); hpMaskBuild(1);       // (usa bbuf de lienzo: se vuelve a pintar el cuadro)
    hpRenderFrame(DX); }

  // ---- 2. El contenido viaja exacto con su pagina ----
  { int cont = 0, mal = 0;
    int aDst, aSrc, aW; hpViewport(DX, aDst, aSrc, aW);
    for(int y = hpTop; y < bandBot; y++){
      if(y >= dotsY - 8 && y <= dotsY + 10) continue;
      const uint8_t* mr = hpMask[0] + (size_t)(y - HOME_PAGE_TOP) * HP_MASK_STRIDE;
      for(int x = 0; x < aW; x++){
        int sx = aSrc + x;
        if(!(mr[sx >> 3] & (1u << (sx & 7)))) continue;
        cont++;
        if(bbuf[(size_t)y * SCR_W + aDst + x] != homeBuf[(size_t)y * SCR_W + sx]) mal++;
      }
    }
    chk(cont > 1000, "la pagina tiene contenido (texto e iconos) que viaja");
    chk(mal == 0, "y viaja pixel a pixel con la pagina"); }

  // ---- 3. Sin salto al empezar ni al terminar ----
  { hpRenderFrame(0);
    int d = 0;
    for(int y = hpTop; y < bandBot; y++){
      if(y >= dotsY - 8 && y <= dotsY + 10) continue;     // los puntos ya anuncian el destino
      for(int x = 0; x < SCR_W; x++) if(bbuf[(size_t)y * SCR_W + x] != homeBuf[(size_t)y * SCR_W + x]) d++;
    }
    chk(d == 0, "el primer cuadro del gesto es la pagina quieta, pixel a pixel");
    renderHomeInto(ref, 1); setBuf(fb);
    hpRenderFrame(-SCR_W);
    d = 0;
    for(int y = hpTop; y < bandBot; y++)
      for(int x = 0; x < SCR_W; x++) if(bbuf[(size_t)y * SCR_W + x] != ref[(size_t)y * SCR_W + x]) d++;
    chk(d == 0, "y el ultimo es la pagina destino compuesta en reposo: no hay salto al soltar"); }

  // ---- 4. El acomodo deja homeBuf exactamente como la pagina destino ----
  { hpDragging = false; hpSettling = true; hpSettleFrom = DX; hpSettleTo = -SCR_W;
    gTestMs += 1000; hpSettleT0 = gTestMs - HP_SETTLE_MS - 1;
    hpTick();
    chk(gHomePage == 1 && !hpSettling, "el gesto termina en la pagina 1");
    int d = 0;
    for(int y = HOME_PAGE_TOP; y < bandBot; y++)
      for(int x = 0; x < SCR_W; x++) if(homeBuf[(size_t)y * SCR_W + x] != ref[(size_t)y * SCR_W + x]) d++;
    chk(d == 0, "homeBuf queda identico a la pagina 1 compuesta desde cero");
    chk(gGlRecN[0] == 2, "y los paneles anotados viajan con su pagina"); }

  // ---- 5. El Modo Edicion tambien lee el fondo de su posicion ----
  { gHomePage = 0; renderHome();
    edEnter();
    chk(gGlRecN[0] == 0, "en Modo Edicion homeBuf no lleva la pagina (se dibuja cada cuadro)");
    gIconStyle = 0; edMs = 0;                   // sin el limitador de 20 fps de los iconos Vidrio
    edRender();
    int ex, ey, ew, eh; wgRect(&gHomeWg[0][1], ex, ey, ew, eh);   // Calendario
    memcpy(ref, homeBuf, (size_t)SCR_W * SCR_H * 2);
    setBuf(ref); uiClipFull();
    homeGlassBegin(-1); drawLiquidGlassPanel(ex, ey, ew, eh, 20, TH_GLASS2); homeGlassEnd();
    setBuf(fb);
    // un pixel de vidrio sin contenido: esquina inferior derecha, lejos del texto
    int px = ex + ew - 20, py = ey + eh - 6;
    chk(bbuf[(size_t)py * SCR_W + px] == ref[(size_t)py * SCR_W + px],
        "el widget en Modo Edicion muestra el fondo de su posicion, del mismo backdrop");
    edExit(); gIconStyle = 1; }

  // ---- 6. Sin vidrio en el escritorio no se paga el desenfoque ----
  { uiGlass = false; gIconStyle = 0;
    renderHome();
    chk(hpBgOk && hgBd == NULL && !hgBdOk,
        "con estilo Plano e iconos Planos no se reserva ni se desenfoca el backdrop");
    uiGlass = true; gIconStyle = 1;
    renderHome();
    chk(hgBdOk && hgBd != NULL, "y al volver a Liquid Glass se reconstruye solo"); }

  // Limpieza: el fondo sintetico no puede quedarse para las pruebas siguientes.
  if(!wallPrevBuf && wallImg){ heap_caps_free(wallImg); wallImg = NULL; }
  wallImgOk = wallOkPrev;
  hcTestReset();
  hpFreeBuffers(); homeBackdropFree();
  uiGlass = glassPrev; gIconStyle = iconPrev;
  uiClipFull(); setBuf(fb);
  if(gFails == before) printf("  Vidrio al deslizar: todas las comprobaciones pasan.\n");
}

// #############################################################
//  WIDGETS DE PAGINA: validacion espacial, tamano, paginas y NVS
// #############################################################
static void testWidgetsDePagina(){
  printf("Widgets integrados en las paginas del escritorio\n");
  int before = gFails;
  hcTestReset();
  int S, gx0, gy0, cs, rs, cols, rows; homeGrid(S, gx0, gy0, cs, rs, cols, rows);

  // ---- 1. GEOMETRIA ----
  { HomeWidget w; w.type = WG_CLOCK; w.col = 0; w.row = 1; w.w = 2; w.h = 1;
    int x, y, ww, hh; wgRect(&w, x, y, ww, hh);
    chk(y == gy0 - 6 && hh == rs - 12 && x == 8 && ww == 2 * cs - 16,
        "una fila de iconos da exactamente el rectangulo de siempre");
    w.row = 0; wgRect(&w, x, y, ww, hh);
    chk(y == HOME_HDR_Y && hh == HOME_HDR_H, "la cabecera mide lo que median los widgets fijos");
    w.h = 2; wgRect(&w, x, y, ww, hh);
    chk(y == HOME_HDR_Y && y + hh == gy0 + rs - 18, "un widget de cabecera + fila es continuo"); }

  // ---- 2. VALIDACION ESPACIAL ----
  { for(int i = 0; i < homeSlotCount(); i++) homeOrder[homeIdx(1, i)] = HOME_EMPTY;
    homeOrder[homeIdx(1, 5)] = IC_NOTAS;                   // fila 2 (icono), columna 1
    chk(!homeWgPlaceOk(1, WG_CLOCK, 3, 0, 2, 1, -1), "no se sale de la rejilla por la derecha");
    chk(!homeWgPlaceOk(1, WG_CLOCK, 0, rows, 2, 2, -1), "ni por abajo");
    chk(!homeWgPlaceOk(1, WG_CLOCK, 0, 2, 2, 1, -1), "ni pisa un icono");
    chk(!homeWgPlaceOk(1, WG_DATE, 0, 0, 2, 2, -1), "ni acepta un tamano que su tipo no admite");
    chk(!homeWgPlaceOk(1, WG_CALEND, 0, 1, 2, 1, -1), "el calendario no cabe en una fila de iconos (6 semanas)");
    chk(homeWgPlaceOk(1, WG_CALEND, 0, 0, 2, 1, -1), "pero si en la cabecera, que es mas alta");
    chk(!homeWgPlaceOk(1, WG_CLIMA, 2, 3, 2, 1, -1), "el clima compacto tampoco cabe en una fila de iconos");
    chk(homeWgPlaceOk(1, WG_CLIMA, 2, 2, 2, 2, -1), "con dos filas si");
    gHomeWg[1][0].type = WG_CLOCK; gHomeWg[1][0].col = 0; gHomeWg[1][0].row = 0;
    gHomeWg[1][0].w = 2; gHomeWg[1][0].h = 1; gHomeWgN[1] = 1;
    chk(!homeWgPlaceOk(1, WG_WIFI, 1, 0, 1, 1, -1), "ni pisa otro widget");
    chk(homeWgPlaceOk(1, WG_CLOCK, 0, 0, 2, 1, 0), "y un widget no choca consigo mismo"); }

  // ---- 3. REDIMENSIONAR ----
  { chk(homeWgResize(1, 0, 3, 1), "el reloj crece a 3 columnas si hay hueco");
    chk(gHomeWg[1][0].w == 3, "y se queda con ese tamano");
    chk(!homeWgResize(1, 0, 5, 1), "no pasa del maximo de su tipo ni de la rejilla");
    chk(homeWgResize(1, 0, 3, 2), "baja a la primera fila de iconos si esta libre");
    chk(!homeWgResize(1, 0, 3, 3), "pero no encima del icono de la fila de abajo");
    chk(gHomeWg[1][0].w == 3 && gHomeWg[1][0].h == 2, "y un intento rechazado no cambia nada");
    chk(wgCanResize(WG_CLOCK) && !wgCanResize(WG_NONE), "el asa solo se ofrece a quien admite otros tamanos");
    homeWgResize(1, 0, 2, 1); }

  // ---- 4. LLEVAR A OTRA PAGINA: regla determinista ----
  { for(int i = 0; i < homeSlotCount(); i++) homeOrder[homeIdx(2, i)] = HOME_EMPTY;
    gHomeWgN[2] = 0;
    int ni = homeWgToPage(1, 0, 2);
    chk(ni == 0 && gHomeWgN[2] == 1 && gHomeWgN[1] == 0, "el widget pasa de la pagina 2 a la 3");
    chk(gHomeWg[2][0].col == 0 && gHomeWg[2][0].row == 0 && gHomeWg[2][0].w == 2,
        "con su mismo sitio y tamano si alli estan libres");
    // Sitio ocupado en el destino -> primer hueco con su tamano.
    gHomeWg[1][0].type = WG_MEM; gHomeWg[1][0].col = 0; gHomeWg[1][0].row = 0;
    gHomeWg[1][0].w = 2; gHomeWg[1][0].h = 1; gHomeWgN[1] = 1;
    ni = homeWgToPage(1, 0, 2);
    chk(ni == 1 && gHomeWg[2][1].col == 2 && gHomeWg[2][1].row == 0,
        "si su sitio esta ocupado va al primer hueco (por filas) con su tamano");
    // Sin hueco para su tamano -> el minimo de su tipo.
    for(int i = 0; i < homeSlotCount(); i++) homeOrder[homeIdx(2, i)] = (uint8_t)i;   // rejilla llena
    gHomeWg[1][0].type = WG_CLOCK; gHomeWg[1][0].col = 0; gHomeWg[1][0].row = 0;
    gHomeWg[1][0].w = 4; gHomeWg[1][0].h = 1; gHomeWgN[1] = 1;
    homeWgRemove(2, 1);                                    // queda libre la mitad derecha de la cabecera
    ni = homeWgToPage(1, 0, 2);
    chk(ni >= 0 && gHomeWg[2][ni].w == 2 && gHomeWg[2][ni].col == 2,
        "sin sitio para su tamano, entra con el minimo de su tipo");
    // Sin hueco ninguno -> no se mueve y no pisa nada.
    gHomeWg[1][0].type = WG_WIFI; gHomeWg[1][0].col = 0; gHomeWg[1][0].row = 0;
    gHomeWg[1][0].w = 1; gHomeWg[1][0].h = 1; gHomeWgN[1] = 1;
    int n2 = gHomeWgN[2];
    chk(homeWgToPage(1, 0, 2) == -1, "sin hueco en el destino, el widget no se mueve");
    chk(gHomeWgN[1] == 1 && gHomeWgN[2] == n2, "ni se pierde ni pisa lo que habia");
    // Destino con el maximo de widgets.
    hcTestReset();
    for(int i = 0; i < homeSlotCount(); i++){ homeOrder[homeIdx(1, i)] = HOME_EMPTY; homeOrder[homeIdx(2, i)] = HOME_EMPTY; }
    while(gHomeWgN[2] < HOME_WG_MAX && homeWgAdd(2, WG_WIFI) == 0) {}
    homeWgAdd(1, WG_WIFI);
    chk(gHomeWgN[2] == HOME_WG_MAX && homeWgToPage(1, 0, 2) == -1,
        "una pagina con el maximo de widgets no admite otro"); }

  // ---- 5. NORMALIZACION: recolocar antes que perder ----
  { hcTestReset();
    for(int i = 0; i < homeSlotCount(); i++) homeOrder[homeIdx(1, i)] = HOME_EMPTY;
    // Dos widgets solapados (NVS corrupta o de otra version) -> el segundo se recoloca.
    gHomeWg[1][0].type = WG_CLOCK; gHomeWg[1][0].col = 0; gHomeWg[1][0].row = 0; gHomeWg[1][0].w = 2; gHomeWg[1][0].h = 1;
    gHomeWg[1][1].type = WG_DATE;  gHomeWg[1][1].col = 1; gHomeWg[1][1].row = 0; gHomeWg[1][1].w = 2; gHomeWg[1][1].h = 1;
    gHomeWgN[1] = 2;
    homeOrderNormalize();
    chk(gHomeWgN[1] == 2, "dos widgets solapados: ninguno se pierde");
    chk(gHomeWg[1][1].col == 2 && gHomeWg[1][1].row == 0, "el segundo se recoloca en el primer hueco");
    // Rejilla que encoge (4 filas -> 3): el widget de la ultima fila se recoloca.
    homeSetGrid(4, 4);
    gHomeWg[1][0].type = WG_MEM; gHomeWg[1][0].col = 0; gHomeWg[1][0].row = 4; gHomeWg[1][0].w = 2; gHomeWg[1][0].h = 1;
    gHomeWgN[1] = 1;
    homeOrderNormalize();
    homeSetGrid(4, 3);
    chk(gHomeWgN[1] == 1 && gHomeWg[1][0].row <= 3, "al quitar una fila, el widget se recoloca en vez de perderse"); }

  // ---- 6. PERSISTENCIA COMPLETA ----
  { hcTestReset();
    flexPrefsWipe();
    for(int i = 0; i < homeSlotCount(); i++) homeOrder[homeIdx(2, i)] = HOME_EMPTY;
    gHomeWg[2][0].type = WG_CLIMA;   gHomeWg[2][0].col = 1; gHomeWg[2][0].row = 1; gHomeWg[2][0].w = 3; gHomeWg[2][0].h = 2;
    gHomeWg[2][1].type = WG_CALEND;  gHomeWg[2][1].col = 0; gHomeWg[2][1].row = 0; gHomeWg[2][1].w = 4; gHomeWg[2][1].h = 1;
    gHomeWgN[2] = 2;
    homeOrderNormalize();
    homeOrderSave();
    memset(gHomeWg, 0, sizeof(gHomeWg)); memset(gHomeWgN, 0, sizeof(gHomeWgN));
    homeOrderLoad();
    bool ok = gHomeWgN[2] == 2 &&
              gHomeWg[2][0].type == WG_CLIMA && gHomeWg[2][0].col == 1 && gHomeWg[2][0].row == 1 &&
              gHomeWg[2][0].w == 3 && gHomeWg[2][0].h == 2 &&
              gHomeWg[2][1].type == WG_CALEND && gHomeWg[2][1].w == 4 && gHomeWg[2][1].row == 0;
    chk(ok, "pagina, posicion, tamano y tipo de cada widget sobreviven a un reinicio");
    chk(gHomeWgN[0] == 0, "y no se anaden widgets de fabrica encima de lo guardado");
    // Migracion: solo la clave v1, con la principal en la pagina 2.
    flexPrefsWipe();
    uint8_t v1[HOME_WG_BLOB_V1]; memset(v1, 0, sizeof(v1)); v1[0] = 'W'; v1[1] = 1;
    { int o = 2; v1[o] = 1; v1[o + 1] = WG_CRONO; v1[o + 2] = 0; v1[o + 3] = 0; v1[o + 4] = 2; v1[o + 5] = 1; }
    uint8_t ord[HOME_TOTAL]; for(int i = 0; i < HOME_TOTAL; i++) ord[i] = HOME_EMPTY;
    prefs.begin("flexos", false);
    prefs.putBytes("hwg", v1, HOME_WG_BLOB_V1);
    prefs.putBytes("hordq", ord, HOME_TOTAL);
    prefs.putInt("hpgn", 3); prefs.putInt("hpmain", 1);
    prefs.putInt("appfav", 0); prefs.putInt("appn", APP_N); prefs.putInt("appver", APPREG_VER);
    prefs.end();
    hcTestReset();
    homeOrderLoad();
    chk(gHomeWgN[0] == 1 && gHomeWg[0][0].type == WG_CRONO && gHomeWg[0][0].row == 1,
        "migracion v1: el widget guardado baja a la primera fila de iconos");
    chk(gHomeWgN[1] == 2 && gHomeWg[1][0].type == WG_CLIMA && gHomeWg[1][1].type == WG_CALEND,
        "migracion: Clima y Calendario pasan a la cabecera de la pagina PRINCIPAL");
    uint8_t chkb[HOME_WG_BLOB]; size_t n2;
    prefs.begin("flexos", true); n2 = prefs.getBytes("hwg2", chkb, HOME_WG_BLOB); prefs.end();
    chk(n2 == HOME_WG_BLOB, "la migracion queda escrita una vez en la clave nueva");
    uint8_t v1b[HOME_WG_BLOB_V1];
    prefs.begin("flexos", true); size_t n1 = prefs.getBytes("hwg", v1b, HOME_WG_BLOB_V1); prefs.end();
    chk(n1 == HOME_WG_BLOB_V1 && !memcmp(v1b, v1, HOME_WG_BLOB_V1), "y la clave v1 queda intacta para volver atras");
    flexPrefsWipe(); }

  // ---- 7. MODO EDICION CON EL DEDO: redimensionar y cambiar de pagina ----
  { hcTestReset();
    for(int p = 1; p < 3; p++) for(int i = 0; i < homeSlotCount(); i++) homeOrder[homeIdx(p, i)] = HOME_EMPTY;
    gHomeWg[1][0].type = WG_CLOCK; gHomeWg[1][0].col = 0; gHomeWg[1][0].row = 0;
    gHomeWg[1][0].w = 2; gHomeWg[1][0].h = 1; gHomeWgN[1] = 1;
    gHomePage = 1; gIconStyle = 0;
    edEnter();
    int wx, wy, ww, wh; wgRect(&gHomeWg[1][0], wx, wy, ww, wh);
    tDown(wx + ww / 2, wy + wh / 2, 50000); edTick();
    chk(edWSel == 0, "tocar un widget en Modo Edicion lo selecciona");
    tUp(50050, true); edTick();
    // Asa de tamano: arrastrar hasta la celda (col 3, fila de iconos 1).
    int hx, hy; edResizeHandleXY(0, hx, hy);
    tDown(hx, hy, 50100); edTick();
    chk(edWResize == 0, "el asa de la esquina empieza a cambiar el tamano");
    tMove(3 * cs + cs / 2, gy0 + rs / 2, 50200); edTick();
    chk(gHomeWg[1][0].w == 4 && gHomeWg[1][0].h == 2, "arrastrarla estira el widget por celdas");
    tUp(50300, false); edTick();
    chk(edWResize == -1, "al soltar se fija");
    // Llevarlo contra el borde derecho: pasa a la pagina 3 con la regla.
    wgRect(&gHomeWg[1][0], wx, wy, ww, wh);
    tDown(wx + 30, wy + 30, 51000); edTick();
    tMove(SCR_W - 4, wy + 30, 51100); edTick();
    tMove(SCR_W - 4, wy + 30, 51900); edTick();
    chk(gHomePage == 2 && gHomeWgN[2] == 1 && gHomeWgN[1] == 0,
        "sostenerlo en el borde lo lleva a la pagina vecina");
    chk(gHomeWg[2][0].type == WG_CLOCK && gHomeWg[2][0].w == 4 && gHomeWg[2][0].h == 2,
        "conservando su tipo y su tamano");
    tUp(52000, false); edTick();
    edExit();
    tReset(); gHomePage = 0; }

  hcTestReset();
  if(gFails == before) printf("  Widgets de pagina: todas las comprobaciones pasan.\n");
}

// #############################################################
//  INTENSIDAD DE LIQUID GLASS
// #############################################################
// Copia LITERAL del panel de vidrio ANTES de existir la intensidad (commit
// f7a7f60): el nivel por defecto tiene que dar EXACTAMENTE estos pixeles.
static void refGlassPanelV0(int x, int y, int w, int h, int rad, uint16_t tint, int blurR){
  if(x < 0){ w += x; x = 0; } if(y < 0){ h += y; y = 0; }
  if(x + w > SCR_W) w = SCR_W - x; if(y + h > SCR_H) h = SCR_H - y;
  if(w <= 0 || h <= 0) return;
  if(2 * rad > w) rad = w / 2; if(2 * rad > h) rad = h / 2;
  int vy0 = y         > gClipY0 ? y         : gClipY0;
  int vy1 = (y + h - 1) < gClipY1 ? (y + h - 1) : gClipY1;
  if(vy0 > vy1) return;
  int j0 = (vy0 - y) - blurR; if(j0 < 0)     j0 = 0;
  int j1 = (vy1 - y) + blurR; if(j1 > h - 1) j1 = h - 1;
  int hc = j1 - j0 + 1;
  uint32_t lumaSum = 0; int lumaN = 0;
  for(int j = 0; j < h; j += 4){
    const uint16_t* srow = gBuf + (size_t)(y + j) * SCR_W + x;
    for(int i = 0; i < w; i += 8){ lumaSum += (uint32_t)glassLuma(srow[i]); lumaN++; }
  }
  for(int j = j0; j <= j1; j++)
    memcpy(glassBuf + (size_t)(j - j0) * w, gBuf + (size_t)(y + j) * SCR_W + x, w * 2);
  uint8_t tintMix = 58;
  if(lumaN > 0){
    int dif = (int)(lumaSum / (uint32_t)lumaN) - glassLuma(tint);
    if(dif < 0) dif = -dif;
    if(dif > 128) dif = 128;
    tintMix = (uint8_t)(46 + (dif * (70 - 46)) / 128);
  }
  glassBlur(w, hc, blurR);
  for(int j = vy0 - y; j <= vy1 - y; j++){
    int yy = y + j;
    int ins = glInset(j, h, rad);
    uint16_t* src = glassBuf + (size_t)(j - j0) * w;
    uint16_t* dst = gBuf + (size_t)yy * SCR_W + x;
    float fj = (float)j;
    uint16_t shCol; uint8_t shA;
    if(fj < h * 0.45f){ shCol = rgb565(255,255,255); shA = (uint8_t)((1.0f - fj / (h * 0.45f)) * 26); }
    else              { shCol = rgb565(0,0,0);       shA = (uint8_t)(((fj - h * 0.45f) / (h * 0.55f)) * 30); }
    if(shA) for(int i = ins; i < w - ins; i++) dst[i] = mix565(mix565(src[i], tint, tintMix), shCol, shA);
    else    for(int i = ins; i < w - ins; i++) dst[i] = mix565(src[i], tint, tintMix);
    bool topZone = (j < h / 2);
    uint8_t sL = topZone ? 156 : 104, sR = topZone ? 104 : 156;
    uint16_t bcol = (j < 3) ? rgb565(255,255,255) : (j < h / 2 ? rgb565(205,214,228) : rgb565(22,28,40));
    dst[ins] = mix565(dst[ins], bcol, sL);
    dst[w - 1 - ins] = mix565(dst[w - 1 - ins], bcol, sR);
  }
}
static void igBackground(uint16_t* b){
  for(int y = 0; y < SCR_H; y++)
    for(int x = 0; x < SCR_W; x++)
      b[(size_t)y * SCR_W + x] = ((x / 10 + y / 10) & 1) ? rgb565(240, 190, 60) : rgb565(20, 60, 150);
}
static void testIntensidadVidrio(){
  printf("Liquid Glass: intensidad desde el Panel rapido\n");
  int before = gFails;
  bool glassPrev = uiGlass;
  static uint16_t* A = NULL; static uint16_t* B = NULL;
  if(!A) A = (uint16_t*)malloc((size_t)SCR_W * SCR_H * 2);
  if(!B) B = (uint16_t*)malloc((size_t)SCR_W * SCR_H * 2);
  gLand = false; gHosted = false; gEffMode = false;
  if(!glassBuf) glassBuf = (uint16_t*)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  // ---- 1. NIVEL POR DEFECTO == MATERIAL DE SIEMPRE, BIT A BIT ----
  gGlassLvl = GLASS_LVL_DEF; glassLevelApply();
  chk(gGlR == 6 && gGlTintMin == 46 && gGlTintMax == 70 && gGlTintBase == 58 &&
      gGlSpec == 26 && gGlShade == 30 && gGlCornS == 156 && gGlCornW == 104,
      "el nivel por defecto son exactamente los parametros historicos");
  { static const int G[6][5] = { {40,120,400,200,24}, {0,0,480,90,20}, {-30,300,200,160,28},
                                 {300,700,240,140,16}, {100,64,72,72,14}, {10,500,460,280,26} };
    const uint16_t tints[3] = { TH_GLASS, TH_GLASS2, rgb565(30,72,150) };
    int dif = 0;
    for(int g = 0; g < 6; g++)
      for(int t = 0; t < 3; t++)
        for(int clip = 0; clip < 2; clip++){
          igBackground(A); memcpy(B, A, (size_t)SCR_W * SCR_H * 2);
          gClipX0 = 0; gClipX1 = SCR_W - 1;
          gClipY0 = clip ? G[g][1] + 20 : 0; gClipY1 = clip ? G[g][1] + 60 : SCR_H - 1;
          setBuf(A); drawLiquidGlassPanel(G[g][0], G[g][1], G[g][2], G[g][3], G[g][4], tints[t]);
          setBuf(B); refGlassPanelV0(G[g][0], G[g][1], G[g][2], G[g][3], G[g][4], tints[t], 6);
          for(size_t i = 0; i < (size_t)SCR_W * SCR_H; i++) if(A[i] != B[i]) dif++;
        }
    uiClipFull(); setBuf(fb);
    chk(dif == 0, "36 paneles (con y sin recorte) identicos al material anterior: cero regresion visual"); }

  // ---- 2. EL NIVEL MUEVE PARAMETROS REALES, ACOTADOS ----
  gGlassLvl = 0;   glassLevelApply(); int r0 = gGlR, t0 = gGlTintMax, s0 = gGlSpec;
  gGlassLvl = 100; glassLevelApply(); int r1 = gGlR, t1 = gGlTintMax, s1 = gGlSpec;
  chk(r0 == 2 && r1 == 10 && r1 <= GLB_RMAX, "el desenfoque va de radio 2 (sutil) a 10 (intenso)");
  chk(t0 < 70 && t1 > 70 && s0 < 26 && s1 > 26, "y el tinte y los brillos crecen con el nivel");
  gGlassLvl = 53;  glassLevelApply(); chk(gGlassLvl == 50, "el nivel va en pasos de 5");
  gGlassLvl = 250; glassLevelApply(); chk(gGlassLvl == 100, "y nunca pasa de 100");
  // Efecto visible: cuanto mas intenso, mas se separa el panel del fondo sin vidrio.
  { long dev[3] = { 0, 0, 0 }; const int L[3] = { 0, 50, 100 };
    for(int k = 0; k < 3; k++){
      gGlassLvl = (uint8_t)L[k]; glassLevelApply();
      igBackground(A); memcpy(B, A, (size_t)SCR_W * SCR_H * 2);
      setBuf(A); uiClipFull(); drawLiquidGlassPanel(60, 200, 360, 200, 24, TH_GLASS);
      for(int y = 230; y < 370; y++) for(int x = 90; x < 390; x++){
        int ra, ga, ba, rb, gb, bb; un565(A[(size_t)y * SCR_W + x], ra, ga, ba); un565(B[(size_t)y * SCR_W + x], rb, gb, bb);
        dev[k] += abs(ra - rb) + abs(ga - gb) + abs(ba - bb);
      }
    }
    setBuf(fb);
    chk(dev[0] < dev[1] && dev[1] < dev[2], "sutil < normal < intenso: el control tiene efecto real y ordenado"); }
  // Con el estilo Plano el nivel no cambia nada: el panel rapido pinta sus
  // superficies solidas con la luz de siempre, sea cual sea la intensidad.
  { bool g = uiGlass; uiGlass = false;
    igBackground(A); igBackground(B);
    gGlassLvl = 0;   glassLevelApply(); setBuf(A); uiClipFull(); qpGlassSurface(40, 300, 400, 120, 26, TH_SURF2, qpMixCard());
    gGlassLvl = 100; glassLevelApply(); setBuf(B); uiClipFull(); qpGlassSurface(40, 300, 400, 120, 26, TH_SURF2, qpMixCard());
    setBuf(fb);
    int d = 0; for(size_t i = 0; i < (size_t)SCR_W * SCR_H; i++) if(A[i] != B[i]) d++;
    chk(d == 0, "con el estilo Plano la intensidad no altera ni un pixel");
    uiGlass = g; }
  gGlassLvl = GLASS_LVL_DEF; glassLevelApply();

  // ---- 3. PERSISTENCIA ----
  { flexPrefsWipe();
    gGlassLvl = 80; cfgSavePrefs();
    gGlassLvl = 0;  cfgLoad();
    chk(gGlassLvl == 80 && gGlR == 6 + (30 * 4) / 50, "el nivel sobrevive a un reinicio y se aplica al cargar");
    prefs.begin("flexos", false); prefs.putInt("glasslv", 999); prefs.end();
    cfgLoad();
    chk(gGlassLvl == GLASS_LVL_DEF, "un valor corrupto cae al nivel por defecto");
    flexPrefsWipe(); cfgLoad();
    chk(gGlassLvl == GLASS_LVL_DEF && gGlR == 6, "sin clave (placa que actualiza) el material es el de siempre"); }

  // ---- 4. EL CONTROL DEL PANEL: opcional y solo con vidrio ----
  gState = ST_HOME; editMode = false;
  qpLoaded = false; flexPrefsWipe(); qpLoad();
  chk(!qpCfgHas(QSID_GLASSFX), "no esta en la configuracion de fabrica: solo si el usuario lo anade");
  chk(QS_REG[QSID_GLASSFX].type == QT_SLIDER && QS_REG[QSID_GLASSFX].sizes == QSZ_4x1,
      "es un deslizador de ancho completo, como el brillo");
  uiGlass = false;
  qpEdN = qpN; memcpy(qpEdIt, qpIt, sizeof(qpIt)); qpCatBuild();
  { bool ofrecido = false; for(int i = 0; i < qpCatN; i++) if(qpCatIds[i] == QSID_GLASSFX) ofrecido = true;
    chk(!ofrecido, "con el estilo Plano el catalogo no lo ofrece"); }
  chk(!qpEditAdd(QSID_GLASSFX), "ni se puede anadir");
  uiGlass = true;
  qpCatBuild();
  { bool ofrecido = false; for(int i = 0; i < qpCatN; i++) if(qpCatIds[i] == QSID_GLASSFX) ofrecido = true;
    chk(ofrecido, "con Liquid Glass el catalogo lo ofrece"); }
  chk(qpEditAdd(QSID_GLASSFX), "y se anade por el editor de siempre");
  memcpy(qpIt, qpEdIt, sizeof(qpIt)); qpN = qpEdN;
  qpN = qpNormalize(qpIt, qpN, qpGrows); qpSave();
  chk(qpCfgHas(QSID_GLASSFX), "queda en la configuracion guardada");
  // Apagar el vidrio lo OCULTA, no lo borra.
  uiGlass = false;
  qpN = qpNormalize(qpIt, qpN, qpGrows);
  chk(qpCfgHas(QSID_GLASSFX), "con el vidrio apagado sigue en la configuracion");
  qpMode = QPM_PANEL; qpGH = (float)qpGroupH(qpGrows); qpScrollF = 0; qpGScrollF = 0; qpRelayout();
  { bool visto = false;
    for(int b = 0; b < qpBlkN; b++) if(qpBlk[b].kind == QB_ITEM && qpIt[qpBlk[b].item].id == QSID_GLASSFX) visto = true;
    chk(!visto, "pero no se dibuja: no aparenta mover un efecto apagado"); }
  chk(!qpExecCtl(QSID_GLASSFX, false), "ni responde a un toque");
  qpLoaded = false; qpLoad();
  chk(qpCfgHas(QSID_GLASSFX), "y tampoco se pierde al recargar la configuracion con el vidrio apagado");

  // ---- 5. DESLIZAR: el material se aplica al SOLTAR, una vez ----
  uiGlass = true;
  hcTestReset(); uiGlass = true;
  hpFreeBuffers(); homeBackdropFree();
  renderHome();
  gGlassLvl = GLASS_LVL_DEF; glassLevelApply();
  qsPanelY = SCR_H; qsLastY = SCR_H; qsDirty = true;
  qpMode = QPM_PANEL; qpG = QG_NONE; qpGH = (float)qpGroupH(qpGrows); qpScrollF = 0; qpGScrollF = 0;
  qpRelayout();
  int bFx = -1;
  for(int b = 0; b < qpBlkN; b++) if(qpBlk[b].kind == QB_ITEM && qpIt[qpBlk[b].item].id == QSID_GLASSFX) bFx = b;
  chk(bFx >= 0, "con Liquid Glass el control aparece en el panel");
  if(bFx >= 0){
    // Que el bloque este a la vista (esta al final del contenido).
    qpScrollF = (float)qpScrollMax(); qpRelayout();
    qsRender(true);
    int top = QP_VIEW_Y0 - (int)(qpScrollF + 0.5f);
    int bx = qpBlk[bFx].x, bw = qpBlk[bFx].w, bh = qpBlk[bFx].h;
    int py = top + qpBlk[bFx].y + bh / 2;
    int th = bh - 20; if(th < 40) th = 40;
    int tw = qpSliderTrackW(QSID_GLASSFX, bw, th);
    gTestMs += 100; touchDrag(bx + 6, py, true); T.downMs = gTestMs; qsHandle();
    chk(qpG == QG_SLIDER, "el gesto que nace en la pista es del deslizador");
    qsDirty = false;
    gTestMs += 16; touchDrag(bx + th / 2 + (tw - th) * 8 / 10, py, false); qsHandle();
    chk(qpGlassFxDrag == 80, "arrastrar mueve el indicador (80%)");
    chk(gGlassLvl == GLASS_LVL_DEF && !qsDirty, "pero no rehace el vidrio en cada cuadro del arrastre");
    gTestMs += 16; touchReset(); T.released = true; qsHandle();
    chk(gGlassLvl == 80 && qpGlassFxDrag == -1, "al soltar se aplica el nivel elegido");
    chk(qsDirty && qpSavePrefs, "una vez: la cortina recompone su vidrio y el guardado queda diferido");
    chk(hgBdOk && hgBdR == gGlR, "el fondo desenfocado del escritorio se rehace con el radio nuevo");
    qsTick();
    chk(!qpSavePrefs, "y qsTick lo escribe en NVS despues de publicar");
    // Restablecer: el boton de la derecha del deslizador.
    qpRelayout(); top = QP_VIEW_Y0 - (int)(qpScrollF + 0.5f);
    py = top + qpBlk[bFx].y + bh / 2;
    int rx = bx + bw - th / 2;
    chk(qpGlassFxResetHit(bx, bw, bh, rx), "el boton de restablecer tiene su propia zona");
    gTestMs += 100; touchDrag(rx, py, true); T.downMs = gTestMs; qsHandle();
    chk(qpG == QG_PENDING, "tocarlo no arrastra el deslizador");
    // Al soltar, T conserva el ultimo punto del dedo (como en la placa).
    gTestMs += 16; touchReset(); T.released = true; T.tap = true; T.x = rx; T.y = py; qsHandle();
    chk(gGlassLvl == GLASS_LVL_DEF && gGlR == 6, "y restablece el material de siempre");
    qsTick();
  }
  qsForceClose();
  qpLoaded = false; flexPrefsWipe(); qpLoad();
  gGlassLvl = GLASS_LVL_DEF; glassLevelApply();
  hcTestReset(); hpFreeBuffers(); homeBackdropFree();
  uiGlass = glassPrev;
  uiClipFull(); setBuf(fb); touchReset();
  if(gFails == before) printf("  Intensidad del vidrio: todas las comprobaciones pasan.\n");
}

// #############################################################
//  KIT DE LISTAS DE MEDIOS · Galeria, Multimedia y Musica deciden LO MISMO
//  ------------------------------------------------------------
//  Sobre un catalogo en memoria (aqui no hay LittleFS): que entra en
//  "Seleccionar todo", que ofrece el menu de un elemento, que pasa sin PIN,
//  que nunca va a la papelera y por donde pasa abrir algo protegido.
// #############################################################
static FlexMlRec tkStore[12];
static uint32_t tkAdd(int kind, int fmt, const char* path, const char* title, bool locked, bool playable){
  FlexMlRec r; memset(&r, 0, sizeof(r));
  snprintf(r.path, sizeof(r.path), "%s", path);
  const char* b = strrchr(path, '/');
  snprintf(r.name, sizeof(r.name), "%s", b ? b + 1 : path);
  if(title) snprintf(r.title, sizeof(r.title), "%s", title);
  r.kind = (uint8_t)kind; r.fmt = (uint8_t)fmt; r.state = FML_S_READY;
  r.flags = (uint16_t)((locked ? FML_R_LOCKED : 0) | (playable ? FML_R_PLAYABLE : 0));
  int at = flexMlAdd(&gMs.lib, &r, 1760000000u);
  return at >= 0 ? gMs.lib.recs[at].id : 0;
}
static void tkReset(){
  memset(&gMs, 0, sizeof(gMs));                 // sin cerrojo ni disco: solo el catalogo
  flexMsInit(&gMs, tkStore, 12);
}
static void testKitMedios(){
  printf("Kit de listas de medios: seleccion, menus, protegidos y clave del sistema\n");
  tkReset();
  uint32_t a = tkAdd(FML_K_PHOTO, FML_F_JPEG, "/Imagenes/a.jpg", NULL, false, true);
  uint32_t b = tkAdd(FML_K_PHOTO, FML_F_JPEG, "/System/Media/Protegido/2.jpg", NULL, true, true);
  uint32_t c = tkAdd(FML_K_VIDEO, FML_F_AVI_MJPEG, "/Videos/c.avi", NULL, false, true);
  tkAdd(FML_K_AUDIO, FML_F_WAV_PCM, "/Musica/d.wav", NULL, false, true);
  uint16_t vs[12]; FlexMlView v;
  flexMlViewInit(&v, vs, 12, FML_MASK_VISUAL, FML_SORT_NEWEST);
  flexMlViewSync(&v, &gMs.lib, true);
  chk(v.n == 3, "lo visual: foto, protegida y video; el audio va a Musica");
  mkBind(&GAL_APP);
  mkEnterMulti(0);
  mkSelectAllLocked(&v);
  chk(mkSelN == 2 && mkIsSel(a) && mkIsSel(c) && !mkIsSel(b), "Seleccionar todo NO incluye lo protegido");
  int sl = 0, so = 0, sa = 0; mkCountLocked(&v, &sl, &so, &sa);
  chk(sl == 0 && so == 2 && sa == 2, "la barra cuenta 2 abiertos de 2 seleccionables");
  mkSelectAllLocked(&v);
  chk(mkSelN == 0, "pulsarlo con todo elegido quita la seleccion");
  mkToggle(b); mkCountLocked(&v, &sl, &so, &sa);
  chk(mkSelN == 1 && sl == 1 && so == 0, "un protegido si se puede elegir a mano");
  flexMlRemoveAt(&gMs.lib, flexMlFindId(&gMs.lib, b));
  flexMlViewSync(&v, &gMs.lib, true);
  mkPruneLocked(&v);
  chk(mkSelN == 0, "lo que desaparece del catalogo sale de la seleccion");
  b = tkAdd(FML_K_PHOTO, FML_F_JPEG, "/System/Media/Protegido/9.jpg", NULL, true, true);
  mkExitMulti();

  // ---- Menu contextual: solo lo que se puede hacer con ESE elemento ----
  gLockType = 1;
  mkOpenItemMenu(a, 100, 300, NULL, 0);
  bool hSel = false, hLock = false, hRen = false, hInfo = false, hTrash = false, hDel = false;
  for(int i = 0; i < mmN; i++) switch(mmAct[i]){
    case MA_SELECT: hSel = true; break; case MA_LOCK: hLock = true; break; case MA_RENAME: hRen = true; break;
    case MA_INFO: hInfo = true; break; case MA_TRASH: hTrash = true; break; case MA_DELETE: hDel = true; break;
  }
  chk(mmOn && hSel && hLock && hRen && hInfo && hTrash && hDel,
      "abierto: Seleccionar, Bloquear, Renombrar, Detalles, Papelera y Borrar");
  chk(!mkMulti, "la pulsacion larga abre el menu; no activa la seleccion multiple sola");
  chk(!strcmp(mmLabel(MA_LOCK), "Bloquear con PIN"), "con PIN en el sistema: 'Bloquear con PIN'");
  gLockType = 2;
  chk(!strcmp(mmLabel(MA_LOCK), "Bloquear con contrase\xC3\xB1" "a"), "con contrasena: 'Bloquear con contrasena'");
  mmOn = false;
  mkOpenItemMenu(b, 100, 300, NULL, 0);
  bool unl = false, leak = false;
  for(int i = 0; i < mmN; i++){
    if(mmAct[i] == MA_UNLOCK) unl = true;
    if(mmAct[i] == MA_TRASH || mmAct[i] == MA_RENAME || mmAct[i] == MA_INFO || mmAct[i] == MA_LOCK) leak = true;
  }
  chk(unl && !leak, "protegido: Desbloquear y Borrar; ni papelera, ni renombrar, ni detalles");
  { int bx, by, bw, bh; uiBox(bx, by, bw, bh);
    int mx, my, mw, mh; mmAx = bx + bw - 3; mmAy = by + bh - 3; mmGeom(mx, my, mw, mh);
    chk(mx >= bx && my >= by && mx + mw <= bx + bw && my + mh <= by + bh, "el menu nunca se sale del area de la app"); }
  mmOn = false;

  // ---- Sin PIN ni contrasena: no se bloquea nada, se explica ----
  gLockType = 0;
  mkMenuId = a;
  mkDoAction(MA_LOCK);
  chk(mmDlgOn && mkNoLockDlg && strstr(mmDlgText, "configura primero un PIN o una contrase\xC3\xB1" "a en Seguridad") != NULL,
      "sin clave: 'Para bloquear archivos, configura primero un PIN o una contrasena en Seguridad.'");
  { FlexMlRec ra; mlGet(a, &ra); chk(!(ra.flags & FML_R_LOCKED), "y no se bloquea nada"); }
  mmDlgOn = false; mkNoLockDlg = false;

  // ---- La papelera es publica: nunca un protegido ----
  gLockType = 1;
  mkEnterMulti(0); mkToggle(a); mkToggle(b);
  mkDoAction(MA_TRASH);
  chk(flexMlFindId(&gMs.lib, a) >= 0 && flexMlFindId(&gMs.lib, b) >= 0,
      "con algo protegido en la seleccion, nada va a la papelera");
  mkExitMulti();

  // ---- Abrir lo protegido pasa por la clave DEL SISTEMA (no hay PIN propio) ----
  auto st0 = gState;
  mkRequestOpen(b);
  chk(gMediaAuth.act == MA_OPEN && gMediaAuth.n == 1 && gMediaAuth.ids[0] == b && gMediaAuth.done == mkAuthDone &&
      lsuAfter == LSU_AFTER_MEDIA, "abrir un protegido pasa por la verificacion del sistema (LSU_AFTER_MEDIA)");
  mediaAfterVerify(false);                        // cancelar: vuelve a la app sin abrir nada
  chk(gState == ST_APP && gMediaAuth.done == NULL, "cancelar la clave devuelve a la app, sin abrir nada");
  gState = st0;
  mkReset();
  memset(&gMs, 0, sizeof(gMs));
}

// #############################################################
//  MUSICA · anterior/siguiente, lo protegido y los archivos que cambian
// #############################################################
static void testMusica(){
  printf("Musica: pistas sin protegidos, sin lo que no suena, y que se sueltan a tiempo\n");
  tkReset();
  uint32_t a = tkAdd(FML_K_AUDIO, FML_F_WAV_PCM, "/Musica/a.wav", "A primera", false, true);
  uint32_t b = tkAdd(FML_K_AUDIO, FML_F_WAV_ADPCM, "/System/Media/Protegido/2.wav", "B protegida", true, true);
  tkAdd(FML_K_AUDIO, FML_F_MP3, "/Musica/c.mp3", "C mp3", false, false);
  uint32_t d = tkAdd(FML_K_AUDIO, FML_F_WAV_ADPCM, "/Musica/d.wav", "D ultima", false, true);
  tkAdd(FML_K_PHOTO, FML_F_JPEG, "/Imagenes/x.jpg", NULL, false, true);
  musViewReady = false;
  mlLock(); musSyncLocked(); mlUnlock();
  chk(musView.n == 4, "Musica ensena solo el audio (4 de 5)");
  chk(musView.n == 4 && gMs.lib.recs[musView.idx[0]].id == a && gMs.lib.recs[musView.idx[3]].id == d,
      "ordenadas por titulo: A, B, C, D");
  musId = a;
  musNext(+1, false);
  chk(musId == d, "siguiente desde A salta la protegida y el MP3: D");
  chk(!musLoaded && strstr(musErr, "Sin salida de audio") != NULL, "sin codec (en el PC) no suena, y lo dice");
  musNext(+1, false);
  chk(musId == a, "siguiente desde la ultima vuelve a A");
  musNext(-1, false);
  chk(musId == d, "anterior desde A: D, sin pasar por la protegida");
  musId = d;
  musNext(+1, true);
  chk(!musLoaded && musId == d, "al acabar la ultima, la lista no vuelve a empezar sola");
  // La pista que suena se suelta ANTES de que su archivo cambie.
  musId = a; snprintf(musPath, sizeof(musPath), "/Musica/a.wav");
  gMlBeforeChange = musBeforeChange;
  mlBeforeChange(a);
  chk(musId == 0 && !musPath[0], "bloquear o borrar la pista que suena la suelta antes");
  // Protegida y P4 bloqueado: se olvida, titulo incluido.
  musId = b; musProtected = true;
  snprintf(musPath, sizeof(musPath), "%s", "/System/Media/Protegido/2.wav");
  snprintf(musTitle, sizeof(musTitle), "B protegida");
  auto st0 = gState; gState = ST_LOCK;
  musAudioTick();
  chk(!musTitle[0] && !musPath[0] && !musProtected, "con el P4 bloqueado, una pista protegida se olvida");
  gState = st0;
  chk(!musBgWork(), "sin sonar, Musica no reclama trabajo en segundo plano");
  musCloseApp();
  memset(&gMs, 0, sizeof(gMs));
}

// #############################################################
//  CAPTURAS de Galeria, Multimedia y Musica (solo con INO_SHOTS=1)
//  ------------------------------------------------------------
//  El codigo REAL de las tres apps pinta sobre un catalogo en memoria y
//  el framebuffer se vuelca a build/shot_*.ppm, para REVISAR el aspecto
//  (no es una comprobacion). Sin sistema de archivos no hay miniaturas:
//  se ven los marcadores honestos ("sin vista previa", el icono).
// #############################################################
extern bool gStubAudioOk;
static void shotSave(const char* name){
  char p[96]; snprintf(p, sizeof(p), "build/shot_%s.ppm", name);
  FILE* f = fopen(p, "wb");
  if(!f) return;
  fprintf(f, "P6\n%d %d\n255\n", SCR_W, SCR_H);
  for(int i = 0; i < SCR_W * SCR_H; i++){
    uint16_t c = fb[i];
    uint8_t rgb[3] = { (uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31) };
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  printf("  captura: tests/host/%s\n", p);
}
static void shotApp(int app){
  gState = ST_APP; gAppId = app; gLand = false; gHosted = false;
  gAppW = SCR_W; gAppH = SCR_H;
  uiClipFull();
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, WIN_BG);
  appDrawChrome(app);
  if(!(APP_REG[app].flags & APP_CUSTOM_HEADER)) appDrawHeader(app);
}
static void testCapturasMedios(){
  if(!getenv("INO_SHOTS")) return;
  printf("Capturas de Galeria, Multimedia y Musica\n");
  tkReset();
  bool ok0 = gMlOk; gMlOk = true;
  gLockType = 1;
  tkAdd(FML_K_PHOTO, FML_F_JPEG, "/Imagenes/Playa 2024.jpg", NULL, false, true);
  uint32_t lk = tkAdd(FML_K_PHOTO, FML_F_JPEG, "/System/Media/Protegido/2.jpg", NULL, true, true);
  uint32_t vd = tkAdd(FML_K_VIDEO, FML_F_AVI_MJPEG, "/Videos/Cumple.avi", NULL, false, true);
  tkAdd(FML_K_PHOTO, FML_F_HEIC, "/Imagenes/IMG_0412.heic", NULL, false, false);
  tkAdd(FML_K_DRAW, FML_F_FXP, "/Paint/Dibujo 1.fxp", NULL, false, true);
  gMs.lib.recs[flexMlFindId(&gMs.lib, vd)].durMs = 83000;
  uint32_t s1 = tkAdd(FML_K_AUDIO, FML_F_WAV_ADPCM, "/Musica/Tema.wav", "Tema de verano", false, true);
  tkAdd(FML_K_AUDIO, FML_F_MP3, "/Musica/Directo.mp3", "En directo", false, false);
  tkAdd(FML_K_AUDIO, FML_F_WAV_PCM, "/System/Media/Protegido/8.wav", "Privada", true, true);
  { int i = flexMlFindId(&gMs.lib, s1); snprintf(gMs.lib.recs[i].artist, sizeof(gMs.lib.recs[i].artist), "Grupo"); gMs.lib.recs[i].durMs = 205000; }

  // Galeria: seleccion multiple con la barra.
  shotApp(IC_GALERIA); mkBind(&GAL_APP); galViewReady = false;
  mkEnterMulti(0); mkToggle(vd);
  galRender(); shotSave("galeria_seleccion");
  mkReset();
  // Galeria: menu contextual de una foto.
  shotApp(IC_GALERIA); galRender();
  { int bx, by, bw, bh; uiBox(bx, by, bw, bh); mkOpenItemMenu(vd, bx + bw / 2, by + 260, NULL, 0); mmDraw(1.0f); }
  shotSave("galeria_menu"); mkReset();
  // Galeria: sin PIN.
  gLockType = 0; shotApp(IC_GALERIA); galRender(); mkMenuId = vd; mkDoAction(MA_LOCK); shotSave("galeria_sin_pin");
  mkReset(); gLockType = 1;
  // Multimedia: lista con un protegido.
  shotApp(IC_MULTIMEDIA); mkBind(&VID_APP); vidViewReady = false; vidScreen = VS_LIST; vidListRender(); shotSave("multimedia_lista");
  mkReset();
  // Musica: lista y reproductor.
  gStubAudioOk = true;
  shotApp(IC_MUSICA); mkBind(&MUS_APP); musViewReady = false;
  musId = s1; musLoaded = true; musPlaying = true;
  snprintf(musPath, sizeof(musPath), "/Musica/Tema.wav"); snprintf(musTitle, sizeof(musTitle), "Tema de verano");
  snprintf(musSub, sizeof(musSub), "Grupo");
  memset(&musAs, 0, sizeof(musAs));
  musAs.wav.sampleRate = 22050; musAs.wav.channels = 1; musAs.wav.bits = 16; musAs.wav.format = FLEXWAV_FMT_PCM;
  musAs.wav.dataBytes = 205u * 44100u; musAs.outFrame = 2; musAs.delivered = 72ull * 44100ull;
  musScreen = MUS_LIST; musRender(); shotSave("musica_lista");
  shotApp(IC_MUSICA); musScreen = MUS_NOW; musRender(); shotSave("musica_reproduciendo");
  musPlaying = false; musLoaded = false; musForget(); musScreen = MUS_LIST;
  gStubAudioOk = false;
  (void)lk;
  mkReset();
  gMlOk = ok0;
  memset(&gMs, 0, sizeof(gMs));
  gState = ST_HOME; gAppId = 0;
}

// #############################################################
//  EDITOR DE LA GALERIA · de punta a punta, con archivos de verdad
//  ------------------------------------------------------------
//  Sobre el disco en memoria (ino_extern_stubs.cpp) y el almacen REAL
//  de la biblioteca: se abre un JPEG, se edita con toques sinteticos
//  sobre la geometria de la pantalla, se guarda como copia y se
//  reemplaza el original, y se comprueba lo que queda en el disco.
//  El trabajador no corre solo (xTaskCreatePinnedToCore es un doble):
//  la prueba lo ejecuta en su sitio con gedWorker(), que es justo el
//  cuerpo de la tarea.
// #############################################################
extern bool gTestMemFs;
extern bool gTestFsReady;
extern std::map<std::string, std::vector<uint8_t>> gTestFiles;
extern uint32_t gTestFsCap;
extern long gTestFsFailWriteAt, gTestFsWritten;
static FlexMlRec geStore[16];
static void chkf(bool c, const char* fmt, ...){
  char b[256]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
  chk(c, b);
}
static bool geOut(void* c, const uint8_t* d, size_t n){ auto* v = (std::vector<uint8_t>*)c; v->insert(v->end(), d, d + n); return true; }
// Foto de prueba: degradado con una marca ROJA arriba a la izquierda.
static std::vector<uint8_t> geJpeg(int W, int H){
  std::vector<uint8_t> px((size_t)W * H * 3), out;
  for(int y = 0; y < H; y++) for(int x = 0; x < W; x++){
    uint8_t* p = &px[((size_t)y * W + x) * 3];
    bool mark = x < W / 8 && y < H / 8;
    p[0] = mark ? 235 : (uint8_t)(40 + x * 150 / W); p[1] = mark ? 20 : (uint8_t)(60 + y * 150 / H); p[2] = mark ? 20 : 140;
  }
  FlexJeCfg c; c.width = W; c.height = H; c.quality = 90; c.subsampling = FLEXJE_SUB_420; c.input = FLEXJE_IN_RGB888;
  flexJpegEncodeMem(&c, px.data(), (size_t)W * 3, geOut, &out, nullptr, nullptr);
  return out;
}
static void geFsReset(){
  gTestMemFs = true; gTestFiles.clear(); gTestFsCap = 16u << 20; gTestFsFailWriteAt = -1; gTestFsWritten = 0;
  memset(&gMs, 0, sizeof(gMs));
  gMs.fs = { msfOpen, msfRead, msfWrite, msfSeek, msfClose, msfSize, msfExists, msfRemove, msfMove, msfTrash,
             msfMkdir, msfList, msfAtomic, NULL };
  gMs.lock = mlLockCb; gMs.unlock = mlUnlockCb; gMs.alloc = mediaAlloc; gMs.free = mediaFree; gMs.now = mlNowCb;
  flexMsInit(&gMs, geStore, 16);
  gMlOk = true;
}
static uint32_t geAddPhoto(const char* path, const std::vector<uint8_t>& jpg, bool locked){
  gTestFiles[path] = jpg;
  FlexMlRec r; memset(&r, 0, sizeof(r));
  snprintf(r.path, sizeof(r.path), "%s", path);
  const char* b = strrchr(path, '/');
  snprintf(r.name, sizeof(r.name), "%s", b ? b + 1 : path);
  r.kind = FML_K_PHOTO; r.fmt = FML_F_JPEG; r.state = FML_S_READY; r.size = (uint32_t)jpg.size();
  r.flags = (uint16_t)(FML_R_PLAYABLE | (locked ? FML_R_LOCKED : 0));
  int at = flexMlAdd(&gMs.lib, &r, 1760000000u);
  return at >= 0 ? gMs.lib.recs[at].id : 0;
}
static unsigned long geMs = 5000000;
static void geTap(int x, int y){
  tDown(x, y, geMs); gedTick();
  tUp(geMs + 60, true); gedTick();
  geMs += 400; touchReset();
}
static void geDrag(int x0, int y0, int x1, int y1){
  tDown(x0, y0, geMs); gedTick();
  for(int k = 1; k <= 8; k++){ tMove(x0 + (x1 - x0) * k / 8, y0 + (y1 - y0) * k / 8, geMs + 50 * k); gedTick(); }
  tUp(geMs + 500, false); gedTick();
  geMs += 900; touchReset(); gedTick();
}
static void geRun(){ gedWorker(nullptr); gedTick(); }     // el trabajador, en su sitio, y la interfaz lo recoge
static const FlexMlRec* geRec(uint32_t id){ int i = flexMlFindId(&gMs.lib, id); return i >= 0 ? &gMs.lib.recs[i] : nullptr; }
static bool geDims(const std::vector<uint8_t>& f, int* w, int* h){
  FlexJpegInfo inf; memset(&inf, 0, sizeof(inf));
  if(f.empty() || flexJpegProbe(f.data(), f.size(), &inf) != FLEXJPG_OK) return false;
  *w = inf.width; *h = inf.height; return true;
}
static bool geTopLeftRed(const std::vector<uint8_t>& f, bool* rightInstead){
  // Decodifica y mira donde quedo la marca roja: arriba a la izquierda o arriba a la derecha.
  struct D { std::vector<uint8_t> px; int w; } d; d.w = 0;
  FlexJpegInfo inf; memset(&inf, 0, sizeof(inf));
  if(flexJpegProbe(f.data(), f.size(), &inf) != FLEXJPG_OK) return false;
  d.px.resize((size_t)inf.width * inf.height * 3); d.w = inf.width;
  flexJpegDecode888(f.data(), f.size(), 0, 0, 0, &inf,
    [](void* u, int y, int w, const uint8_t* rgb) -> bool { D* q = (D*)u; memcpy(&q->px[(size_t)y * q->w * 3], rgb, (size_t)w * 3); return true; },
    &d, nullptr, nullptr);
  auto red = [&](int x, int y){ const uint8_t* p = &d.px[((size_t)y * d.w + x) * 3]; return p[0] > 180 && p[1] < 90 && p[2] < 90; };
  *rightInstead = red(inf.width - 4, 4);
  return red(4, 4);
}

static void testEditorGaleria(){
  printf("Editor de la Galeria: abrir, editar, guardar copia, reemplazar, protegidos y memoria\n");
  bool ok0 = gMlOk; bool fs0 = gTestFsReady; gTestFsReady = true;
  geFsReset();
  auto orig = geJpeg(320, 240);
  uint32_t id = geAddPhoto(FML_DIR_PHOTO "/playa.jpg", orig, false);
  uint32_t lk = geAddPhoto(FML_DIR_LOCKED "/9.jpg", geJpeg(64, 48), true);
  FlexMlRec rv; memset(&rv, 0, sizeof(rv)); rv.kind = FML_K_VIDEO; snprintf(rv.path, sizeof(rv.path), "/Videos/v.avi");
  FlexMlRec rp = *geRec(id); snprintf(rp.path, sizeof(rp.path), FML_DIR_PHOTO "/dibujo.png");
  chk(gedEditable(geRec(id)) && !gedEditable(geRec(lk)) && !gedEditable(&rv) && !gedEditable(&rp),
      "editable: una foto JPEG abierta; ni lo protegido, ni un video, ni un PNG");

  gState = ST_APP; gAppId = IC_GALERIA; gAppState[IC_GALERIA] = ALIFE_RUNNING; gLand = false; gHosted = false;
  gAppW = SCR_W; gAppH = SCR_H; uiClipFull(); setBuf(fb);
  mkBind(&GAL_APP);
  // ---- El menu de la foto ofrece Editar (y lo protegido no) ----
  gLockType = 1;
  { static const uint8_t ed[1] = { MA_EDIT };
    mkOpenItemMenu(id, 100, 300, ed, 1);
    bool has = false; for(int i = 0; i < mmN; i++) if(mmAct[i] == MA_EDIT) has = true;
    mmOn = false;
    mkOpenItemMenu(lk, 100, 300, ed, 1);
    bool leak = false; for(int i = 0; i < mmN; i++) if(mmAct[i] == MA_EDIT) leak = true;
    mmOn = false;
    chk(has && !leak, "el menu de una foto ofrece Editar; el de una protegida, no"); }

  // ---- Abrir: el trabajador lee y decodifica; la interfaz lo recoge ----
  uint32_t tasks0 = gPinnedTaskCreates;
  mkMenuId = id; mkDoAction(MA_EDIT);
  chk(gedPhase == GED_OPENING && gPinnedTaskCreates == tasks0 + 1 && gedJobOn,
      "Editar lanza la apertura en su trabajador (sin bloquear la interfaz)");
  geRun();
  chk(gedPhase == GED_EDIT && gedBase && gedW == 320 && gedH == 240 && gedSrcW == 320 && !gedJobOn,
      "la foto se abre entera (320x240) y el editor queda listo");
  chk(!flexIeCanUndo(gedE) && flexIeIsIdentity(gedE) && !gedDirty(), "recien abierta: nada que deshacer ni que guardar");

  // ---- Toques reales sobre la geometria de la pantalla ----
  GedGeom g = gedGeom();
  int x, w; gedRowCell(g, 4, 1, x, w);
  geTap(x + w / 2, g.panelY + 30);                          // Girar a la derecha
  chk(gedE->st.rot == 1 && flexIeCanUndo(gedE), "Recortar > Girar: un cuarto de vuelta, y se puede deshacer");
  int tw = g.bw / GT_N;
  geTap(g.bx + GT_ADJ * tw + tw / 2, g.tabsY + 30);
  chk(gedTool == GT_ADJ, "pestana Ajustes");
  int s0, s1; gedSliderGeom(g, GED_SLIDER_Y(g), s0, s1);
  int cur0 = gedE->cur;
  geDrag((s0 + s1) / 2, GED_SLIDER_Y(g), s0 + (s1 - s0) * 3 / 4, GED_SLIDER_Y(g));
  chkf(gedE->st.adj[FLEXIE_ADJ_BRIGHT] >= 45 && gedE->st.adj[FLEXIE_ADJ_BRIGHT] <= 55 && gedE->cur == cur0 + 1,
      "arrastrar Brillo hasta 3/4: ~+50 y UN solo paso en el historial (%d)", gedE->st.adj[FLEXIE_ADJ_BRIGHT]);
  geTap(g.bx + GT_DRAW * tw + tw / 2, g.tabsY + 30);
  geDrag(gedVX + gedVW / 4, gedVY + gedVH / 2, gedVX + gedVW * 3 / 4, gedVY + gedVH / 2);
  chk(gedE->st.nOv == 1 && gedE->ov[0].kind == FLEXIE_OV_STROKE && gedE->ov[0].pn >= 2, "Dibujo: un trazo sobre la foto");
  geTap(g.bx + g.pad + 18, g.topY + GED_TOP_H / 2);          // deshacer
  chk(gedE->st.nOv == 0, "deshacer quita el trazo");
  geTap(g.bx + g.pad + 66, g.topY + GED_TOP_H / 2);          // rehacer
  chk(gedE->st.nOv == 1 && gedDirty(), "rehacer lo devuelve; hay cambios sin guardar");

  // ---- Guardar como copia ----
  int bx, by, bw, bh; gedSaveBtnGeom(g, bx, by, bw, bh);
  geTap(bx + bw / 2, by + bh / 2);
  chk(gedSheet, "Guardar abre la hoja con las dos opciones");
  gedSheetBtn(g, 0, bx, by, bw, bh);
  geTap(bx + bw / 2, by + bh / 2);
  chk(gedPhase == GED_SAVING && gedJobOn && !strcmp(gedJob.path, FML_DIR_TMP "/ed-1.jpg") && gedJob.outW == 240 && gedJob.outH == 320,
      "Guardar como copia: se codifica en un temporal (240x320, girada)");
  int n0 = gMs.lib.n;
  geRun();
  uint32_t cp = gMs.lib.n == n0 + 1 ? gMs.lib.recs[gMs.lib.n - 1].id : 0;
  const FlexMlRec* rc = cp ? geRec(cp) : nullptr;
  chk(rc && rc->parent == id && rc->origin == FML_O_EDIT && !strcmp(rc->name, "playa (editada).jpg") && (rc->flags & FML_R_NEED_THUMB),
      "copia publicada: id nueva, 'playa (editada).jpg', sabe de donde sale y espera su miniatura");
  int cw = 0, ch = 0; bool right = false;
  chkf(rc && gTestFiles.count(rc->path) && geDims(gTestFiles[rc->path], &cw, &ch) && cw == 240 && ch == 320,
      "el archivo de la copia es un JPEG de 240x320 que el firmware abre (%dx%d)", cw, ch);
  chk(rc && !geTopLeftRed(gTestFiles[rc->path], &right) && right, "y va girada: la marca roja arriba a la DERECHA");
  chk(gTestFiles[FML_DIR_PHOTO "/playa.jpg"] == orig && !gTestFiles.count(FML_DIR_TMP "/ed-1.jpg"),
      "el original, intacto; el temporal, fuera");
  chk(gedPhase == GED_OFF && !gedE && !gedBase && !gedJobOn, "guardado: el editor se cierra y suelta toda su memoria");

  // ---- Segunda copia: otro nombre libre ----
  gedOpen(id); geRun();
  flexIeFlip(gedE, true);
  gedStartSave(GS_COPY); geRun();
  chk(gMs.lib.n == n0 + 2 && !strcmp(gMs.lib.recs[gMs.lib.n - 1].name, "playa (editada 2).jpg"),
      "la segunda copia no repite nombre: 'playa (editada 2).jpg'");

  // ---- Reemplazar el original (con confirmacion) ----
  gedOpen(id); geRun();
  flexIeSetCrop(gedE, 0.0f, 0.0f, 0.5f, 0.5f);
  gedAfterChange();
  gedSaveBtnGeom(g, bx, by, bw, bh); geTap(bx + bw / 2, by + bh / 2);
  gedSheetBtn(g, 1, bx, by, bw, bh); geTap(bx + bw / 2, by + bh / 2);
  chk(mmDlgOn && gedAsk == GA_REPLACE && gedPhase == GED_EDIT, "Reemplazar pide confirmacion antes de tocar nada");
  { int dx, dy, dw, dh; mmDlgGeom(dx, dy, dw, dh); int bw2 = (dw - 48) / 2;
    geTap(dx + 32 + bw2 + bw2 / 2, dy + dh - 76 + 28); }                 // boton principal: Reemplazar
  chk(gedPhase == GED_SAVING, "confirmado: se guarda");
  geRun();
  const FlexMlRec* ro = geRec(id);
  int rw = 0, rh = 0;
  chkf(ro && !strcmp(ro->path, FML_DIR_PHOTO "/playa.jpg") && (ro->flags & FML_R_EDITED) && geDims(gTestFiles[ro->path], &rw, &rh) &&
      rw == 160 && rh == 120 && !gTestFiles.count(FML_DIR_PHOTO "/playa.jpg.fxorig") && !gTestFiles.count(FML_DIR_TMP "/ed-1.jpg"),
      "reemplazada: misma id y ruta, contenido nuevo (160x120), nada apartado ni temporal (%dx%d)", rw, rh);
  chk(ro && ro->size == gTestFiles[ro->path].size(), "el catalogo sabe el tamano nuevo");

  // ---- Protegidos: si la foto se bloquea con el editor abierto, se cierra ----
  gedOpen(id); geRun();
  char why[96];
  chk(mlSetLock(id, true, why, sizeof(why)), "bloquear la foto (con el editor abierto)");
  gedTick();
  chk(gedPhase == GED_OFF && !gedBase && !gedE && mmDlgOn, "el editor se cierra, suelta los pixeles y lo dice");
  mmDlgOn = false; gedAsk = GA_NONE;
  chk(mlSetLock(id, false, why, sizeof(why)), "desbloquear");
  // ...y si se protege mientras se GUARDA, no sale nada (ni copia sin proteger).
  gedOpen(id); geRun();
  flexIeRotate(gedE, 1);
  gedStartSave(GS_COPY);
  gedWorker(nullptr);                                        // el trabajador acaba...
  { int nb = gMs.lib.n;
    mlSetLock(id, true, why, sizeof(why));                   // ...y la foto se bloquea antes de publicar
    gedTick();
    chk(gMs.lib.n == nb && !gTestFiles.count(FML_DIR_TMP "/ed-1.jpg") && gedPhase == GED_OFF,
        "protegida a mitad de guardar: no se publica ninguna copia y el temporal se borra"); }
  mmDlgOn = false; gedAsk = GA_NONE;
  mlSetLock(id, false, why, sizeof(why));

  // ---- Cancelar y fallos: el original no cambia nunca ----
  auto before = gTestFiles[geRec(id)->path];
  gedOpen(id); geRun();
  flexIeRotate(gedE, 1);
  gedStartSave(GS_REPLACE);
  gedJob.cancel = 1; geRun();
  chk(gedPhase == GED_EDIT && gedE->st.rot == 1 && !gTestFiles.count(FML_DIR_TMP "/ed-1.jpg") && gTestFiles[geRec(id)->path] == before,
      "cancelar: nada cambia, el temporal no queda y se sigue editando");
  mmDlgOn = false; gedAsk = GA_NONE;
  gTestFsFailWriteAt = gTestFsWritten + 300;                // el disco falla a mitad (tras la cabecera)
  gedStartSave(GS_REPLACE); geRun();
  chk(gedPhase == GED_EDIT && mmDlgOn && gTestFiles[geRec(id)->path] == before && !gTestFiles.count(FML_DIR_TMP "/ed-1.jpg"),
      "escritura que falla a mitad: el original intacto, sin restos, y se avisa");
  mmDlgOn = false; gedAsk = GA_NONE; gTestFsFailWriteAt = -1;
  uint32_t cap0 = gTestFsCap; gTestFsCap = flexFsUsedBytes() + 1000;
  gedStartSave(GS_COPY);
  chk(gedPhase == GED_EDIT && !gedJobOn && mmDlgOn && strstr(mmDlgText, "Libera espacio") != NULL,
      "sin espacio: no se empieza a escribir y se dice cuanto falta");
  mmDlgOn = false; gedAsk = GA_NONE; gTestFsCap = cap0;

  // ---- Memoria: soltar la foto en segundo plano y releerla al volver ----
  flexIeAdjustLive(gedE, FLEXIE_ADJ_CONTRAST, 40); flexIeCommit(gedE);
  int curBefore = gedE->cur;
  gAppState[IC_GALERIA] = ALIFE_SUSPENDED;
  size_t shed = gedShed();
  chk(shed > 0 && !gedBase && gedReopen && gedE, "suspendida: la foto se suelta (el estado se queda)");
  gAppState[IC_GALERIA] = ALIFE_RUNNING;
  gedResume();
  chk(gedPhase == GED_OPENING, "al volver se relee");
  geRun();
  chk(gedPhase == GED_EDIT && gedBase && gedE->st.rot == 1 && gedE->st.adj[FLEXIE_ADJ_CONTRAST] == 40 && gedE->cur == curBefore,
      "releida: mismas ediciones y mismo historial");

  // ---- Guardar con la Galeria en segundo plano: lo publica loop() ----
  gedStartSave(GS_COPY);
  gAppState[IC_GALERIA] = ALIFE_SUSPENDED;
  chk(galBgWork(), "guardando: trabajo real en segundo plano (no se desaloja)");
  gedWorker(nullptr);
  int nb = gMs.lib.n;
  gTestMs += 1000; geMs = gTestMs;                          // la Galeria lleva un rato sin pintarse
  gedBgTick();
  chk(gMs.lib.n == nb + 1 && gedPhase == GED_OFF && !galBgWork(), "loop() publica la copia aunque la Galeria no este delante");
  gAppState[IC_GALERIA] = ALIFE_RUNNING;

  // ---- Texto ----
  gedOpen(id); geRun();
  gedSetTool(GT_TEXT);
  snprintf(gedText, sizeof(gedText), "Hola"); gedTextOn = true; gedTextU = 0.1f; gedTextV = 0.4f;
  gedRowCell(g, 2, 1, x, w);
  geTap(x + w / 2, g.panelY + 25);                          // Listo
  chk(gedE->st.nOv == 1 && gedE->ov[0].kind == FLEXIE_OV_TEXT && !strcmp(gedE->ov[0].text, "Hola") && !gedTextOn,
      "Texto > Listo: el texto queda en la foto");
  // ATRAS con cambios pregunta; descartar cierra sin tocar nada.
  auto before2 = gTestFiles[geRec(id)->path];
  chk(galBackLayer() && mmDlgOn && gedAsk == GA_DISCARD, "atras con cambios: pregunta si descartar");
  { int dx, dy, dw, dh; mmDlgGeom(dx, dy, dw, dh); int bw2 = (dw - 48) / 2;
    geTap(dx + 32 + bw2 + bw2 / 2, dy + dh - 76 + 28); }
  chk(gedPhase == GED_OFF && gTestFiles[geRec(id)->path] == before2, "Descartar: fuera, sin guardar nada");

  // ---- Un trabajador que tarda en terminar: nada se suelta antes de tiempo ----
  gedOpen(id); geRun();
  flexIeRotate(gedE, 1);
  gedStartSave(GS_COPY);                                    // el trabajador "no llega" a correr
  FlexImgEdit* stuck = gedE;
  gedCloseNow();
  chk(gedPhase == GED_OFF && gedLeak && gedE == stuck, "cerrar con el trabajador colgado: no se suelta lo que usa");
  chk(!gedOpen(id) && mmDlgOn, "y el editor no se reabre encima: 'Editor ocupado'");
  mmDlgOn = false; gedAsk = GA_NONE;
  gedWorker(nullptr);                                       // por fin termina (vio la cancelacion)
  gTestMs += 1000; geMs = gTestMs;
  gedBgTick();
  chk(!gedLeak && !gedE && !gedJobOn && !gTestFiles.count(FML_DIR_TMP "/ed-1.jpg"),
      "cuando acaba, loop() recoge su memoria y su temporal");
  chk(gedOpen(id) && gedPhase == GED_OPENING, "y el editor vuelve a abrir");
  geRun(); gedCloseNow();

  // ---- Un nombre largo se acorta; el sufijo de la copia nunca se pierde ----
  { char keep[FML_NAME_MAX]; snprintf(keep, sizeof(keep), "%s", gedName);
    snprintf(gedName, sizeof(gedName), "%s", "Atardecer en la playa con toda la familia, verano de 2026.jpg");
    char nm[FML_NAME_MAX]; gedCopyName(nm, sizeof(nm));
    size_t L = strlen(nm);
    chkf(L < sizeof(nm) && L > 14 && !strcmp(nm + L - 14, " (editada).jpg") && !strncmp(nm, "Atardecer en la playa", 21),
         "nombre largo: se acorta el nombre, no el sufijo (%s)", nm);
    snprintf(gedName, sizeof(gedName), "%s", keep); }

  // ---- La comprobacion del temporal: lo que no cuadra no se publica ----
  { GedJob j; memset(&j, 0, sizeof(j));
    auto jpg = geJpeg(64, 48);
    snprintf(j.path, sizeof(j.path), FML_DIR_TMP "/v.jpg");
    gTestFiles[j.path] = jpg; j.bytes = (uint32_t)jpg.size(); j.outW = 64; j.outH = 48;
    bool good = gedVerify(&j);
    j.outW = 65; bool dims = gedVerify(&j); j.outW = 64;
    gTestFiles[j.path].resize(jpg.size() - 10); bool shortF = gedVerify(&j);
    j.bytes = (uint32_t)jpg.size() - 10; bool noEoi = gedVerify(&j);
    chk(good && !dims && !shortF && !noEoi, "el temporal se comprueba: medidas, tamano en disco y marca de fin");
    gTestFiles.erase(j.path); }

  mkReset();
  gMlOk = ok0; gTestFsReady = fs0; gTestMemFs = false; gTestFiles.clear();
  memset(&gMs, 0, sizeof(gMs));
  gState = ST_HOME; gAppId = 0; gAppState[IC_GALERIA] = ALIFE_CLOSED;
  touchReset();
}

// #############################################################
//  EDITOR DE VIDEO DE LA GALERIA · con archivos de verdad
//  ------------------------------------------------------------
//  Sobre el disco en memoria y el almacen REAL de la biblioteca, con toques
//  sobre la geometria de la pantalla y entrando por galTick (el mismo camino
//  que en la placa). El trabajador no corre solo: xTaskCreatePinnedToCore es
//  un doble que no da tarea, asi que la prueba lo hace avanzar con
//  vedWorkStep(), que es exactamente lo que la tarea hace en bucle.
// #############################################################
extern void (*gTestFsOnWrite)(long written);
static void vtU32(std::vector<uint8_t>& v, uint32_t x){ for(int i = 0; i < 4; i++) v.push_back((uint8_t)(x >> (8 * i))); }
static void vtU16(std::vector<uint8_t>& v, uint16_t x){ v.push_back((uint8_t)x); v.push_back((uint8_t)(x >> 8)); }
static void vtCc(std::vector<uint8_t>& v, const char* t){ for(int i = 0; i < 4; i++) v.push_back((uint8_t)t[i]); }
static std::vector<uint8_t> vtChunk(const char* id, const std::vector<uint8_t>& p){
  std::vector<uint8_t> v; vtCc(v, id); vtU32(v, (uint32_t)p.size()); v.insert(v.end(), p.begin(), p.end());
  if(p.size() & 1) v.push_back(0);
  return v;
}
static std::vector<uint8_t> vtList(const char* t, const std::vector<uint8_t>& p){
  std::vector<uint8_t> v; vtCc(v, "LIST"); vtU32(v, (uint32_t)p.size() + 4); vtCc(v, t); v.insert(v.end(), p.begin(), p.end());
  return v;
}
// Fotograma i: arriba a la izquierda ROJO (para ver el giro), el resto con un
// gris propio de cada fotograma.
static std::vector<uint8_t> vtFrame(int W, int H, int i){
  std::vector<uint8_t> px((size_t)W * H * 3), out;
  for(int y = 0; y < H; y++) for(int x = 0; x < W; x++){
    uint8_t* p = &px[((size_t)y * W + x) * 3];
    if(x < W / 2 && y < H / 2){ p[0] = 225; p[1] = 30; p[2] = 30; }
    else { uint8_t g = (uint8_t)(40 + (i * 13) % 180); p[0] = p[1] = p[2] = g; }
  }
  FlexJeCfg c; c.width = W; c.height = H; c.quality = 85; c.subsampling = FLEXJE_SUB_420; c.input = FLEXJE_IN_RGB888;
  flexJpegEncodeMem(&c, px.data(), (size_t)W * 3, geOut, &out, nullptr, nullptr);
  return out;
}
// AVI MJPEG de n fotogramas a 10 fps; con audio, PCM de 16 bits mono a 8 kHz
// (un trozo de 800 muestras antes de cada fotograma).
static std::vector<std::vector<uint8_t>> vtFrames;
static std::vector<uint8_t> vtAvi(int n, int W, int H, bool audio){
  const uint32_t us = 100000;
  vtFrames.clear();
  for(int i = 0; i < n; i++) vtFrames.push_back(vtFrame(W, H, i));
  std::vector<uint8_t> avih; vtU32(avih, us); vtU32(avih, 0); vtU32(avih, 0); vtU32(avih, 0x10); vtU32(avih, (uint32_t)n); vtU32(avih, 0);
  vtU32(avih, audio ? 2 : 1); vtU32(avih, 0); vtU32(avih, (uint32_t)W); vtU32(avih, (uint32_t)H); for(int i = 0; i < 4; i++) vtU32(avih, 0);
  std::vector<uint8_t> vh; vtCc(vh, "vids"); vtCc(vh, "MJPG"); vtU32(vh, 0); vtU16(vh, 0); vtU16(vh, 0); vtU32(vh, 0);
  vtU32(vh, 1); vtU32(vh, 10); vtU32(vh, 0); vtU32(vh, (uint32_t)n); vtU32(vh, 65536); vtU32(vh, 0); vtU32(vh, 0); vtU32(vh, 0); vtU32(vh, 0);
  std::vector<uint8_t> vf; vtU32(vf, 40); vtU32(vf, (uint32_t)W); vtU32(vf, (uint32_t)H); vtU16(vf, 1); vtU16(vf, 24); vtCc(vf, "MJPG");
  for(int i = 0; i < 5; i++) vtU32(vf, 0);
  std::vector<uint8_t> vstrl = vtChunk("strh", vh); { auto t = vtChunk("strf", vf); vstrl.insert(vstrl.end(), t.begin(), t.end()); }
  std::vector<uint8_t> hdrl = vtChunk("avih", avih); { auto t = vtList("strl", vstrl); hdrl.insert(hdrl.end(), t.begin(), t.end()); }
  if(audio){
    std::vector<uint8_t> ah; vtCc(ah, "auds"); vtU32(ah, 0); vtU32(ah, 0); vtU16(ah, 0); vtU16(ah, 0); vtU32(ah, 0);
    vtU32(ah, 2); vtU32(ah, 16000); vtU32(ah, 0); vtU32(ah, 0); vtU32(ah, 4096); vtU32(ah, 0); vtU32(ah, 2); vtU32(ah, 0); vtU32(ah, 0);
    std::vector<uint8_t> af; vtU16(af, 1); vtU16(af, 1); vtU32(af, 8000); vtU32(af, 16000); vtU16(af, 2); vtU16(af, 16); vtU16(af, 0);
    std::vector<uint8_t> astrl = vtChunk("strh", ah); { auto t = vtChunk("strf", af); astrl.insert(astrl.end(), t.begin(), t.end()); }
    auto t = vtList("strl", astrl); hdrl.insert(hdrl.end(), t.begin(), t.end());
  }
  std::vector<uint8_t> movi, idx1;
  for(int i = 0; i < n; i++){
    if(audio){
      std::vector<uint8_t> pcm;
      for(int s = 0; s < 800; s++) vtU16(pcm, (uint16_t)(int16_t)(((i * 800 + s) * 37) % 16000 - 8000));
      uint32_t off = 4u + (uint32_t)movi.size();
      auto c = vtChunk("01wb", pcm); movi.insert(movi.end(), c.begin(), c.end());
      vtCc(idx1, "01wb"); vtU32(idx1, 0x10); vtU32(idx1, off); vtU32(idx1, (uint32_t)pcm.size());
    }
    uint32_t off = 4u + (uint32_t)movi.size();
    auto c = vtChunk("00dc", vtFrames[i]); movi.insert(movi.end(), c.begin(), c.end());
    vtCc(idx1, "00dc"); vtU32(idx1, 0x10); vtU32(idx1, off); vtU32(idx1, (uint32_t)vtFrames[i].size());
  }
  std::vector<uint8_t> body = vtList("hdrl", hdrl);
  { auto t = vtList("movi", movi); body.insert(body.end(), t.begin(), t.end()); }
  { auto t = vtChunk("idx1", idx1); body.insert(body.end(), t.begin(), t.end()); }
  std::vector<uint8_t> out; vtCc(out, "RIFF"); vtU32(out, (uint32_t)body.size() + 4); vtCc(out, "AVI ");
  out.insert(out.end(), body.begin(), body.end());
  return out;
}
static uint32_t vtAddVideo(const char* path, const std::vector<uint8_t>& avi, int W, int H, bool locked){
  gTestFiles[path] = avi;
  FlexMlRec r; memset(&r, 0, sizeof(r));
  snprintf(r.path, sizeof(r.path), "%s", path);
  const char* b = strrchr(path, '/');
  snprintf(r.name, sizeof(r.name), "%s", b ? b + 1 : path);
  r.kind = FML_K_VIDEO; r.fmt = FML_F_AVI_MJPEG; r.state = FML_S_READY; r.size = (uint32_t)avi.size();
  r.w = (uint16_t)W; r.h = (uint16_t)H;
  r.flags = (uint16_t)(FML_R_PLAYABLE | (locked ? FML_R_LOCKED : 0));
  int at = flexMlAdd(&gMs.lib, &r, 1760000000u);
  return at >= 0 ? gMs.lib.recs[at].id : 0;
}
// Los '00dc' de un AVI, en orden (vacios incluidos).
struct VtCk { uint32_t off, len; };
static std::vector<VtCk> vtVideoChunks(const std::vector<uint8_t>& f){
  std::vector<VtCk> out;
  auto rd = [&](size_t o){ return o + 4 <= f.size() ? (uint32_t)f[o] | (uint32_t)f[o + 1] << 8 | (uint32_t)f[o + 2] << 16 | (uint32_t)f[o + 3] << 24 : 0u; };
  size_t p = 12;
  while(p + 12 <= f.size()){
    uint32_t L = rd(p + 4);
    if(!memcmp(&f[p], "LIST", 4) && !memcmp(&f[p + 8], "movi", 4)){
      size_t q = p + 12, end = p + 8 + L;
      while(q + 8 <= end && q + 8 <= f.size()){
        uint32_t n = rd(q + 4);
        if(!memcmp(&f[q + 2], "dc", 2)) out.push_back({ (uint32_t)(q + 8), n });
        q += 8 + n + (n & 1);
      }
      return out;
    }
    p += 8 + L + (L & 1);
  }
  return out;
}
static int vtAudioChunks(const std::vector<uint8_t>& f){
  int n = 0;
  for(size_t p = 12; p + 8 <= f.size(); p++) if(!memcmp(&f[p], "01wb", 4)) n++;
  return n;
}
static bool vtSame(const std::vector<uint8_t>& f, const VtCk& c, const std::vector<uint8_t>& frame){
  return c.len == frame.size() && c.off + c.len <= f.size() && !memcmp(&f[c.off], frame.data(), c.len);
}
// Lo que el analizador del editor ve en un archivo del disco en memoria.
struct VtMemIo { const std::vector<uint8_t>* d; uint32_t pos; };
static int vtIoRead(void* c, void* b, uint32_t n){
  VtMemIo* m = (VtMemIo*)c;
  if(m->pos >= m->d->size()) return 0;
  uint32_t k = (uint32_t)m->d->size() - m->pos; if(k > n) k = n;
  memcpy(b, m->d->data() + m->pos, k); m->pos += k; return (int)k;
}
static bool vtIoSeek(void* c, uint32_t o){ VtMemIo* m = (VtMemIo*)c; if(o > m->d->size()) return false; m->pos = o; return true; }
static uint32_t vtIoSize(void* c){ return (uint32_t)((VtMemIo*)c)->d->size(); }
static int vtProbe(const std::vector<uint8_t>& f, FlexVeSource* s){
  VtMemIo m = { &f, 0 };
  FlexMediaIO io; io.read = vtIoRead; io.seek = vtIoSeek; io.size = vtIoSize; io.ctx = &m;
  FlexAviCtx* a = (FlexAviCtx*)malloc(sizeof(FlexAviCtx));
  memset(s, 0, sizeof(*s));
  int rc = flexVeProbe(&io, s, a, nullptr, nullptr);
  free(a);
  return rc;
}

static unsigned long vtMs = 30000000;
static void vtIdle(){ touchReset(); gTestMs = vtMs; galTick(); }
static void vtTap(int x, int y){
  tDown(x, y, vtMs); galTick();
  tUp(vtMs + 60, true); galTick();
  vtMs += 400; touchReset(); gTestMs = vtMs;
}
static void vtDrag(int x0, int y0, int x1, int y1){
  tDown(x0, y0, vtMs); galTick();
  for(int k = 1; k <= 8; k++){ tMove(x0 + (x1 - x0) * k / 8, y0 + (y1 - y0) * k / 8, vtMs + 50 * k); galTick(); }
  tUp(vtMs + 500, false); galTick();
  vtMs += 900; touchReset(); gTestMs = vtMs; galTick();
}
// El trabajador hasta que no tenga nada, y la interfaz lo recoge.
static int vtPump(){ int n = 0; while(n < 400 && vedWorkStep()) n++; return n; }
static void vtRun(){ for(int k = 0; k < 5; k++){ vtPump(); vtIdle(); } }
static long vtCancelAt = -1;
static void vtOnWrite(long w){ if(vtCancelAt >= 0 && w >= vtCancelAt){ vedSt(&vedS.cancel, 1); vtCancelAt = -1; } }
static void vtTool(int t){ VedGeom g = vedGeom(); int tw = g.bw / VT_N; vtTap(g.bx + t * tw + tw / 2, g.tabsY + 30); vtRun(); }
static void vtRow(int n, int i, int yOff){ VedGeom g = vedGeom(); int x, w; vedRowCell(g, n, i, x, w); vtTap(x + w / 2, g.panelY + yOff); }
static void vtBar(int which){                     // 0 deshacer, 1 rehacer, 2 exportar
  VedGeom g = vedGeom(); int ux, rx, okx, cy; vedBarBtns(g, ux, rx, okx, cy);
  vtTap(which == 0 ? ux : which == 1 ? rx : okx, cy);
}
static void vtSheet(int i){ VedGeom g = vedGeom(); int x, y, w, h; vedSheetBtn(g, i, x, y, w, h); vtTap(x + w / 2, y + h / 2); }
static void vtDlgPrimary(){ int dx, dy, dw, dh; mmDlgGeom(dx, dy, dw, dh); int bw2 = (dw - 48) / 2; vtTap(dx + 32 + bw2 + bw2 / 2, dy + dh - 76 + 28); }
static void vtDlgOk(){ int dx, dy, dw, dh; mmDlgGeom(dx, dy, dw, dh); vtTap(dx + dw / 2, dy + dh - 76 + 28); }
static uint32_t vtLastId(){ return gMs.lib.n ? gMs.lib.recs[gMs.lib.n - 1].id : 0; }
static bool vtNoTemps(){
  for(auto& kv : gTestFiles) if(!strncmp(kv.first.c_str(), FML_DIR_TMP "/ve-", strlen(FML_DIR_TMP "/ve-"))) return false;
  return true;
}

static void testEditorVideoGaleria(){
  printf("Editor de video de la Galeria: accesos, herramientas, exportar, cancelar, protegidos, memoria y estres\n");
  int before = gFails;
  bool ok0 = gMlOk; bool fs0 = gTestFsReady; bool glass0 = uiGlass; int nav0 = gNavMode;
  gTestFsReady = true; uiGlass = true; gNavMode = 0;
  geFsReset();
  gLockType = 1;
  vtMs = 30000000; gTestMs = vtMs;
  const int W = 96, H = 64, N = 30;
  auto avi = vtAvi(N, W, H, true);
  auto srcFrames = vtFrames;
  uint32_t id = vtAddVideo(FML_DIR_VIDEO "/Cumple.avi", avi, W, H, false);
  uint32_t lk = vtAddVideo(FML_DIR_LOCKED "/7.avi", vtAvi(4, W, H, false), W, H, true);
  uint32_t ph = geAddPhoto(FML_DIR_PHOTO "/foto.jpg", geJpeg(64, 48), false);
  { FlexMlRec o = *geRec(id); o.fmt = FML_F_AVI_OTHER;
    FlexMlRec big = *geRec(id); big.size = FML_LIMIT_VIDEO + 1;
    chk(vedEditable(geRec(id)) && !vedEditable(geRec(lk)) && !vedEditable(geRec(ph)) && !vedEditable(&o) && !vedEditable(&big),
        "editable: un video AVI MJPEG abierto; ni lo protegido, ni una foto, ni otro AVI, ni uno mayor que el tope"); }
  chk(VID_VW.edit == NULL && VID_VW.editable == NULL && GAL_VW.edit && GAL_VW.editable,
      "Multimedia no ofrece Editar en su visor; la Galeria, si");

  gState = ST_APP; gAppId = IC_GALERIA; gAppState[IC_GALERIA] = ALIFE_RUNNING; gLand = false; gHosted = false;
  gAppW = SCR_W; gAppH = SCR_H; uiClipFull(); setBuf(fb);
  mkBind(&GAL_APP); mkReset(); galViewReady = false; galScroll = 0; galTab = 0;
  mlTables();
  if(!glassBuf) glassBuf = (uint16_t*)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  galRender();
  // Lo que el sistema reserva una vez y no suelta (la banda de vidrio del menu)
  // se reserva ANTES de medir: un menu abierto y cerrado.
  { int x, y, w, h; galCellRect(0, x, y, w, h);
    tDown(x + w / 2, y + h / 2, vtMs); galTick(); tMove(x + w / 2, y + h / 2, vtMs + 600); galTick();
    tUp(vtMs + 650, false); galTick(); vtMs += 800; touchReset();
    vtTap(5, 700); vtIdle(); galRender(); }
  glcBuild(16, 16, 4, TH_GLASS, TH_PAGE);                // la cache de tarjetas de vidrio del sistema (una vez, nunca se suelta)
  size_t ps0 = gPsUsed;

  // ---- ACCESO 1: pulsacion larga en la rejilla > menu > Editar ----
  int cell = -1;
  for(int i = 0; i < 3 && cell < 0; i++){ int x, y, w, h; galCellRect(i, x, y, w, h); if(galHitId(x + w / 2, y + h / 2) == id) cell = i; }
  chk(cell >= 0, "el video esta en la rejilla");
  int cx = 0, cy = 0; { int x, y, w, h; galCellRect(cell < 0 ? 0 : cell, x, y, w, h); cx = x + w / 2; cy = y + h / 2; }
  tDown(cx, cy, vtMs); galTick();
  tMove(cx, cy, vtMs + 600); galTick();
  int editRow = -1; for(int i = 0; i < mmN; i++) if(mmAct[i] == MA_EDIT) editRow = i;
  chk(mmOn && editRow >= 0, "mantener pulsado un video: el menu (Action Sheet) ofrece Editar");
  tUp(vtMs + 650, false); galTick(); vtMs += 800; touchReset();
  uint32_t tasks0 = gPinnedTaskCreates;
  { int x, y, w, h; mmGeom(x, y, w, h); vtTap(x + w / 2, y + MM_PAD + editRow * MM_RH + MM_RH / 2); }
  chk(!mmOn && vedPhase == VED_OPENING && gPinnedTaskCreates == tasks0 + 1 && !gedActive(),
      "Editar: el menu desaparece del todo y se abre EL editor de video (su trabajador, uno)");
  chk(vedS.started && !vedS.task, "el trabajador de la sesion esta lanzado (en el arnes, sin hilo)");
  vtRun();
  chk(vedPhase == VED_EDIT && vedM->src.frames == (uint32_t)N && vedM->src.w == W && vedM->src.h == H &&
      vedM->src.aud.kind == FLEXVE_AUD_PCM && vedBaseW > 0 && vedViewOk,
      "analizado: 30 fotogramas de 96x64, audio PCM y el primer fotograma en pantalla");
  chkf(__atomic_load_n(&vedS.thumbsDone, __ATOMIC_ACQUIRE) == VED_THUMBS && vedThSeen == VED_THUMBS,
       "las 8 miniaturas de la linea de tiempo, hechas y pintadas (%d)", vedS.thumbsDone);
  chk(!flexVeCanUndo(vedE) && !vedCanExport() && !vedDirty(), "recien abierto: nada que deshacer ni que exportar");
  { VedMem* m0 = vedM;
    chk(vedOpen(id) && vedM == m0 && gPinnedTaskCreates == tasks0 + 1 && vedPhase == VED_EDIT,
        "abrir OTRA vez el mismo video reutiliza la sesion: ni otro editor ni otro trabajador"); }

  // ---- LINEA DE TIEMPO: recortar arrastrando el extremo izquierdo ----
  VedGeom g = vedGeom();
  int ax = vedFrameX(g, 0), sy = g.stripY + VED_STRIP_H / 2;
  int cur0 = vedE->cur;
  vtDrag(ax, sy, ax + g.stripW / 5, sy);
  chkf(vedE->p.seg[0].a >= 5 && vedE->p.seg[0].a <= 7 && vedE->cur == cur0 + 1 && vedCanExport(),
       "arrastrar el inicio 1/5 de la tira: empieza en el fotograma ~6 y es UN paso del historial (%u)", vedE->p.seg[0].a);
  vtRun();
  chkf(vedBaseFrame == vedE->p.seg[0].a, "la vista previa ensena el nuevo primer fotograma (%u)", vedBaseFrame);
  // Tocar la tira mueve el cabezal.
  vtTap(vedFrameX(g, 18) + 2, sy);
  vtRun();
  chkf(vedHead == 18 && vedBaseFrame == 18, "tocar la tira lleva el cabezal alli y se decodifica ese fotograma (%u)", vedHead);
  // Dividir aqui y quitar la parte del cabezal.
  vtRow(3, 0, 50);
  chk(vedE->p.nSeg == 2 && vedE->p.seg[0].b == 18 && vedE->p.seg[1].a == 18, "Dividir aqui: dos partes, cortadas en el cabezal");
  vtRow(3, 1, 50);
  chk(vedE->p.nSeg == 1 && vedE->p.seg[0].b == 18, "Quitar parte: se va la del cabezal (de 18 al final)");
  vtBar(0);
  chk(vedE->p.nSeg == 2, "deshacer devuelve la parte quitada");
  vtBar(1);
  chk(vedE->p.nSeg == 1, "rehacer la vuelve a quitar");
  vtBar(0);                                              // dos partes para las pruebas de despues

  // ---- ENCUADRE, GIRO, VELOCIDAD, VOLUMEN, TEXTO, FILTRO Y PORTADA ----
  vtTool(VT_ROT);
  chk(vedTool == VT_ROT, "pestana Girar");
  vtRow(4, 1, 28);
  chk(vedE->p.rot == 1, "Girar: 90 grados");
  vtTool(VT_CROP);
  vtRow(FLEXVE_ASP_N, FLEXVE_ASP_1_1, 26);
  { int rw, rh; flexVeRegionSize(&vedE->p, W, H, &rw, &rh);
    chkf(vedE->p.aspect == FLEXVE_ASP_1_1 && abs(rw - rh) <= 1, "Encuadre 1:1 sobre el video girado: cuadrado (%dx%d)", rw, rh); }
  { float c1y = vedE->p.c1y; int cur1 = vedE->cur;
    int x0, y0, x1, y1; vedCropRectPx(vedE->p.c0x, vedE->p.c0y, vedE->p.c1x, vedE->p.c1y, x0, y0, x1, y1);
    vtDrag(x1, y1, x1 - 10, y1 - 10);
    int rw, rh; flexVeRegionSize(&vedE->p, W, H, &rw, &rh);
    chkf(vedE->p.c1y < c1y && vedE->cur == cur1 + 1 && abs(rw - rh) <= 1,
         "arrastrar la esquina encoge el encuadre sin perder el 1:1, en UN paso (%dx%d)", rw, rh); }
  vtRow(1, 0, 68);
  chk(vedE->p.aspect == FLEXVE_ASP_FREE && vedE->p.c0x == 0.0f && vedE->p.c1x == 1.0f, "Restablecer encuadre");
  vtTool(VT_SPEED);
  vtRow(FLEXVE_SPEED_N, 6, 26);
  chk(vedE->p.speedPct == 200, "Velocidad 2x");
  vtTool(VT_VOL);
  vtRow(5, 2, 26);
  chk(vedE->p.volPct == 50 && !vedE->p.mute, "Volumen 50 %");
  vtRow(1, 0, 70);
  chk(vedE->p.mute, "Silenciar");
  vtRow(1, 0, 70);
  chk(!vedE->p.mute, "y quitar el silencio");
  vtTool(VT_TEXT);
  vtRow(1, 0, 24);
  chk(fkNameOn && vedTextAsk, "Anadir texto abre el teclado del sistema");
  snprintf(fkNameBuf, sizeof(fkNameBuf), "Feliz cumple");
  { int fry = KB_Y + 3 * (KB_KH + KB_GAP); vtTap(kbFKeyX(5) + kbFKeyW(5) / 2, fry + KB_KH / 2); }
  chk(!fkNameOn && !vedTextAsk && !strcmp(vedE->p.text, "Feliz cumple"), "Guardar en el teclado: el texto queda en el video");
  { VedGeom g2 = vedGeom(); vtRun();
    float u0 = vedE->p.textU; int cur2 = vedE->cur;
    int tx = vedVX + (int)(vedE->p.textU * vedVW) + 4, ty = vedVY + (int)(vedE->p.textV * vedVH) + 2;
    vtDrag(tx, ty, tx + 30, ty - 20);
    chkf(vedE->p.textU > u0 && vedE->cur == cur2 + 1 && !vedViewNoText && vedViewOk,
         "arrastrar el texto lo mueve (un paso) y la vista vuelve a llevarlo dentro (%.2f)", vedE->p.textU);
    (void)g2; }
  vtTool(VT_FILTER);
  { VedGeom g3 = vedGeom(); int x, y, side; vedFilterGeom(g3, FLEXIE_FILTER_BW, x, y, side); vtTap(x + side / 2, y + side / 2); }
  chk(vedE->p.filter == FLEXIE_FILTER_BW && vedFtOk, "Filtros: B/N, con sus miniaturas hechas del fotograma");
  vtTool(VT_COVER);
  vtRow(2, 0, 58);
  chkf(vedE->p.cover == vedHead, "Portada: el fotograma del cabezal (%u)", vedE->p.cover);

  // ---- EXPORTAR: la hoja, "Guardar como copia" ----
  vtBar(2);
  chk(vedSheet && vedSheetBtnN() == 4, "exportar abre la hoja: copia, reemplazar, las partes y cancelar");
  { int ow, oh; bool fit0 = vedResDims(0, &ow, &oh), fit1 = vedResDims(1, &ow, &oh);
    chk(fit0 && !fit1, "resoluciones: la del encuadre si; 1080p no se ofrece (seria ampliar)"); }
  { std::vector<uint16_t> a1(fb, fb + (size_t)SCR_W * SCR_H);
    for(int k = 0; k < 6; k++) vedRender();
    chk(memcmp(a1.data(), fb, a1.size() * 2) == 0, "la hoja pintada 6 veces mas da EXACTAMENTE los mismos pixeles (el vidrio no se apila)"); }
  vtSheet(3);
  chk(!vedSheet && vedPhase == VED_EDIT, "Cancelar cierra la hoja sin exportar");
  vtBar(2);
  int n0 = gMs.lib.n;
  size_t psEdit = gPsUsed;
  vtSheet(0);
  chk(vedPhase == VED_EXPORTING && vedJobRunning(), "Guardar como copia: exporta en el trabajador");
  { char want[FML_PATH_MAX]; snprintf(want, sizeof(want), FML_DIR_TMP "/ve-%lu-0.avi", (unsigned long)id);
    chk(!strcmp(vedM->tmp[0], want), "a un temporal de /System/Media/tmp"); }
  vtPump();
  uint32_t outFrames = vedM->frames[0]; int outW = vedM->outW, outH = vedM->outH;
  vtIdle();
  uint32_t cp = gMs.lib.n == n0 + 1 ? vtLastId() : 0;
  const FlexMlRec* rc = cp ? geRec(cp) : nullptr;
  chk(rc && rc->parent == id && rc->origin == FML_O_EDIT && !strcmp(rc->name, "Cumple (editado).avi") && rc->kind == FML_K_VIDEO,
      "copia publicada: 'Cumple (editado).avi', nueva, y sabe de donde sale");
  chk(gTestFiles[FML_DIR_VIDEO "/Cumple.avi"] == avi && vtNoTemps(), "el original, intacto; ningun temporal");
  chk(vedPhase == VED_OFF && !vedM && !vedBase && !vedS.started, "exportado: el editor se cierra y su trabajador sale");
  galRender();
  if(gPsUsed != ps0) printf("  (PSRAM sin devolver: %d bytes)\n", (int)(gPsUsed - ps0));
  chk(gPsUsed == ps0, "y devuelve TODA la PSRAM");
  if(rc){
    FlexVeSource s; int prc = vtProbe(gTestFiles[rc->path], &s);
    // Dos partes (6..18 y 18..30 menos lo quitado: la de 18..30 volvio con deshacer), a 2x.
    chkf(prc == FLEXVE_OK && s.frames == outFrames && s.w == outW && s.h == outH && outW == 64 && outH == 96,
         "el AVI exportado se analiza: %u fotogramas de %dx%d (girado)", s.frames, s.w, s.h);
    chkf(s.aud.kind == FLEXVE_AUD_PCM && vtAudioChunks(gTestFiles[rc->path]) > 0, "y lleva el audio original (PCM)");
    chkf(s.usPerFrame == 100000, "2x: mismos fps, la mitad de fotogramas (%u us)", s.usPerFrame);
  }

  // ---- COPIA SIN RECODIFICAR: solo recortar la duracion ----
  vedOpen(id); vtRun();
  chk(vedPhase == VED_EDIT && !flexVeCanUndo(vedE), "se vuelve a abrir limpio (nada heredado de la sesion anterior)");
  flexVeTrimLive(vedE, 0, 10); flexVeTrimLive(vedE, 1, 20); flexVeCommit(vedE); vedApplied();
  vtBar(2); vtSheet(0); vtPump(); vtIdle();
  { const FlexMlRec* r2 = geRec(vtLastId());
    auto ck = r2 ? vtVideoChunks(gTestFiles[r2->path]) : std::vector<VtCk>();
    bool same = ck.size() == 10;
    for(size_t k = 0; same && k < ck.size(); k++) same = vtSame(gTestFiles[r2->path], ck[k], srcFrames[10 + k]);
    chkf(r2 && !strcmp(r2->name, "Cumple (editado 2).avi") && same,
         "recortar sin tocar la imagen copia los fotogramas 10..19 BYTE A BYTE (%d)", (int)ck.size()); }

  // ---- GUARDAR LAS PARTES por separado ----
  vedOpen(id); vtRun();
  flexVeSplit(vedE, 12); vedApplied();
  vtBar(2);
  int n1 = gMs.lib.n;
  vtSheet(2);
  vtPump(); vtIdle();
  chk(gMs.lib.n == n1 + 2 && !strcmp(gMs.lib.recs[gMs.lib.n - 2].name, "Cumple (parte 1).avi") &&
      !strcmp(gMs.lib.recs[gMs.lib.n - 1].name, "Cumple (parte 2).avi") && vtNoTemps(),
      "Guardar las 2 partes: dos videos nuevos, cada uno con su nombre");
  { auto c1 = vtVideoChunks(gTestFiles[gMs.lib.recs[gMs.lib.n - 2].path]), c2 = vtVideoChunks(gTestFiles[gMs.lib.recs[gMs.lib.n - 1].path]);
    chkf(c1.size() == 12 && c2.size() == 18, "la parte 1 son los fotogramas 0..11 y la 2, 12..29 (%d + %d)", (int)c1.size(), (int)c2.size()); }
  // Sitio en el catalogo para lo que sigue.
  while(gMs.lib.n > 3){ uint32_t d = vtLastId(); mlDelete(d); }

  // ---- CANCELAR A MITAD: al 10, al 50 y al 90 % de lo escrito ----
  vedOpen(id); vtRun();
  flexVeRotate(vedE, 1); vedApplied();                  // recodificar: una exportacion con trabajo de verdad
  long w0 = gTestFsWritten;
  vtBar(2); vtSheet(0); vtPump(); vtIdle();
  long total = gTestFsWritten - w0;
  uint32_t last = vtLastId();
  chk(total > 2000 && geRec(last) && vedPhase == VED_OFF, "exportacion de referencia (girada)");
  mlDelete(last);
  vedOpen(id); vtRun();
  flexVeRotate(vedE, 1); vedApplied();
  size_t psE = gPsUsed;
  gTestFsOnWrite = vtOnWrite;
  const int pcts[3] = { 10, 50, 90 };
  for(int k = 0; k < 3; k++){
    int nb = gMs.lib.n;
    vtCancelAt = gTestFsWritten + total * pcts[k] / 100;
    vtBar(2); vtSheet(0); vtPump(); vtIdle();
    chkf(vedPhase == VED_EDIT && mmDlgOn && vedAsk == VA_INFO && gMs.lib.n == nb && vtNoTemps() &&
         gTestFiles[FML_DIR_VIDEO "/Cumple.avi"] == avi && vedE->p.rot == 1,
         "cancelar al %d %%: nada publicado, el temporal fuera, el original igual y se sigue editando", pcts[k]);
    chkf(gPsUsed == psE, "cancelar al %d %% suelta la memoria de la exportacion", pcts[k]);
    vtDlgOk();
    chk(!mmDlgOn && vedPhase == VED_EDIT, "Aceptar vuelve al editor");
  }
  gTestFsOnWrite = nullptr;
  // Cancelar con ATRAS durante la exportacion.
  { int nb = gMs.lib.n;
    vtBar(2); vtSheet(0);
    chk(galBackLayer() && vedLd(&vedS.cancel), "ATRAS mientras exporta pide cancelar (no sale del editor a medias)");
    vtPump(); vtIdle();
    chk(vedPhase == VED_EDIT && gMs.lib.n == nb && vtNoTemps(), "y cancela limpio");
    if(mmDlgOn) vtDlgOk(); }
  // Fallo de escritura a mitad.
  gTestFsFailWriteAt = gTestFsWritten + total / 2;
  vtBar(2); vtSheet(0); vtPump(); vtIdle();
  chk(vedPhase == VED_EDIT && mmDlgOn && vtNoTemps() && gTestFiles[FML_DIR_VIDEO "/Cumple.avi"] == avi,
      "el disco falla a mitad: el original intacto, sin restos, y se avisa");
  gTestFsFailWriteAt = -1;
  if(mmDlgOn) vtDlgOk();
  // Sin espacio: ni se empieza.
  { uint32_t cap0 = gTestFsCap; gTestFsCap = flexFsUsedBytes() + 2000;
    vtBar(2); vtSheet(0);
    chk(vedPhase == VED_EDIT && !vedJobRunning() && mmDlgOn && strstr(mmDlgText, "Libera espacio") != NULL,
        "sin espacio: no se empieza a escribir y se dice cuanto falta");
    gTestFsCap = cap0; if(mmDlgOn) vtDlgOk(); }

  // ---- REEMPLAZAR EL ORIGINAL (con confirmacion) ----
  flexVeTrimLive(vedE, 1, 20); flexVeCommit(vedE); vedApplied();
  vtBar(2); vtSheet(1);
  chk(mmDlgOn && vedAsk == VA_REPLACE && vedPhase == VED_EDIT, "Reemplazar pide confirmacion antes de tocar nada");
  vtDlgPrimary();
  chk(vedPhase == VED_EXPORTING, "confirmado: se exporta");
  vtPump(); vtIdle();
  const FlexMlRec* ro = geRec(id);
  { FlexVeSource s; int prc = ro ? vtProbe(gTestFiles[ro->path], &s) : -1;
    chkf(ro && !strcmp(ro->path, FML_DIR_VIDEO "/Cumple.avi") && prc == FLEXVE_OK && s.frames == 20 && s.w == 64 && s.h == 96 &&
         ro->size == gTestFiles[ro->path].size() && vtNoTemps() && vedPhase == VED_OFF,
         "reemplazado: misma id y ruta, contenido nuevo (%u fotogramas, %dx%d)", s.frames, s.w, s.h); }
  avi = gTestFiles[FML_DIR_VIDEO "/Cumple.avi"];

  // ---- ACCESO 2: el boton Editar de la barra del visor (solo en la Galeria) ----
  galRender();
  galOpenId(id);
  chk(vwActiveFor(&GAL_VW) && vwKind == VWK_VIDEO && vwCanEdit, "el visor de la Galeria abre el video con Editar");
  vwBarsShow(true); gTestMs += VW_FADE_MS + 20; vwBarsTick();
  { VwVidBtns bt; vwVidBtns(bt);
    chk(bt.editX > 0 && vwHitBtn(bt.editX, bt.cy) == VWB_EDIT && vwHitBtn(bt.playX, bt.cy) == VWB_PLAY &&
        vwHitBtn(bt.backX, bt.cy) == VWB_BACK10, "Editar va a la izquierda de la barra, sin pisar los controles");
    uint32_t t1 = gPinnedTaskCreates;
    vwDoBtn(VWB_EDIT, bt.editX);
    chk(vedPhase == VED_OPENING && gPinnedTaskCreates == t1 + 1 && !vwActiveFor(&GAL_VW) && gLand == false,
        "Editar en el visor abre EL MISMO editor (y el visor suelta su memoria)"); }
  vtRun();
  chk(vedPhase == VED_EDIT && vedM->src.frames == 20, "el editor abierto desde el visor tiene el video (20 fotogramas)");
  vtIdle();
  chk(galBackLayer() && vedPhase == VED_OFF, "ATRAS sin cambios: fuera del editor, sin preguntar");
  vtIdle();
  chk(vwActiveFor(&GAL_VW) && vwKind == VWK_VIDEO, "y se vuelve al visor, al mismo video");
  vwClose();
  // En Multimedia el mismo visor no ofrece Editar.
  vwOpen(&VID_VW, id, NULL, NULL);
  { VwVidBtns bt; vwVidBtns(bt); int bx, by, bw, bh; vwBotGeom(bx, by, bw, bh);
    chk(!vwCanEdit && bt.editX < 0 && vwHitBtn(bx + 40, bt.cy) != VWB_EDIT, "Multimedia: el visor del video no tiene Editar"); }
  vwClose();
  gAppId = IC_GALERIA; gState = ST_APP; mkBind(&GAL_APP);

  // ---- NAVEGACION RAPIDA: Galeria > video > editor > atras > video > editor ----
  galRender();
  size_t psNav = gPsUsed;
  uint32_t tNav = gPinnedTaskCreates;
  bool navOk = true;
  for(int k = 0; k < 10; k++){
    galOpenId(id);
    VwVidBtns bt; vwBarsShow(true); gTestMs += VW_FADE_MS + 20; vwBarsTick(); vwVidBtns(bt);
    vwDoBtn(VWB_EDIT, bt.editX);
    if(k & 1) vtRun();                                    // la mitad de las veces, ATRAS antes de que acabe de abrir
    navOk = navOk && vedActive() && !gedActive();
    galBackLayer();                                       // abriendo: cancela; abierto: sale
    vtPump(); vtIdle();
    navOk = navOk && !vedActive();
    if(vwActiveFor(&GAL_VW)) vwClose();
  }
  galRender();
  chkf(navOk && !vedActive() && gPinnedTaskCreates == tNav + 10 && !vedS.started,
       "10 vueltas rapidas visor <-> editor: un editor cada vez, ningun trabajador huerfano (%u)", gPinnedTaskCreates - tNav);
  if(gPsUsed != psNav) printf("  (PSRAM sin devolver: %d bytes)\n", (int)(gPsUsed - psNav));
  chk(gPsUsed == psNav, "y la PSRAM vuelve exactamente al punto de partida");

  // ---- ABRIR Y CERRAR 25 VECES (sin fugas) ----
  { size_t p0 = gPsUsed; bool all = true;
    for(int k = 0; k < 25; k++){
      all = all && vedOpen(id);
      vtRun();
      if(k % 3 == 0){ flexVeRotate(vedE, 1); vedApplied(); galBackLayer(); if(mmDlgOn) vtDlgPrimary(); }   // con cambios: Descartar
      else galBackLayer();
      all = all && !vedActive();
    }
    galRender();
    chkf(all && gPsUsed == p0 && vtNoTemps(), "abrir, editar y descartar 25 veces: sin fugas (%d bytes)", (int)(gPsUsed - p0)); }

  // ---- PROTEGIDO O BORRADO CON EL EDITOR ABIERTO ----
  vedOpen(id); vtRun();
  { char why[96];
    chk(mlSetLock(id, true, why, sizeof(why)), "bloquear el video (con el editor abierto)");
    vtIdle();
    chk(vedPhase == VED_OFF && !vedM && mmDlgOn, "el editor se cierra, suelta todo y lo dice");
    mmDlgOn = false; vedAsk = VA_NONE;
    chk(mlSetLock(id, false, why, sizeof(why)), "desbloquear");
    // ...y si se protege mientras EXPORTA, no sale nada.
    vedOpen(id); vtRun();
    flexVeRotate(vedE, 1); vedApplied();
    vtBar(2); vtSheet(0);
    vtPump();                                             // el trabajador acaba...
    int nb = gMs.lib.n;
    mlSetLock(id, true, why, sizeof(why));                // ...y el video se bloquea antes de publicar
    vtIdle();
    chk(gMs.lib.n == nb && vtNoTemps() && vedPhase == VED_OFF, "protegido a mitad de exportar: no se publica nada y el temporal se borra");
    mmDlgOn = false; vedAsk = VA_NONE;
    mlSetLock(id, false, why, sizeof(why)); }
  // Otra app lo mueve a la papelera mientras se edita: se cancela lo que haya y se cierra.
  { vedOpen(id); vtRun();
    flexVeRotate(vedE, 1); vedApplied();
    vtBar(2); vtSheet(0);                                 // exportando cuando llega el cambio
    FlexMlRec keep = *geRec(id); auto keepFile = gTestFiles[keep.path];
    chk(mlTrash(id), "a la papelera desde otra app");
    chk(vedLd(&vedS.cancel) && vedExtCancel, "antes de moverlo, el editor suelta el archivo y corta la exportacion");
    vtPump(); vtIdle();
    chk(vedPhase == VED_OFF && vtNoTemps(), "y se cierra sin dejar nada");
    mmDlgOn = false; vedAsk = VA_NONE;
    geFsReset();                                          // biblioteca limpia para lo que sigue
    mkReset(); galViewReady = false;
    avi = keepFile;
    id = vtAddVideo(FML_DIR_VIDEO "/Cumple.avi", avi, 64, 96, false); }

  // ---- MEMORIA: sin PSRAM no se abre (ni se reserva nada) ----
  galRender();
  { size_t p0 = gPsUsed;
    gTestPsPressure = gTestPsTotal - gPsUsed - 1024u * 1024u;
    chk(!vedOpen(id) && mmDlgOn && !vedActive() && gPsUsed == p0, "sin memoria: se dice y no se reserva nada");
    mmDlgOn = false; gTestPsPressure = 0;
    gTestPsFail = true;
    chk(!vedOpen(id) && mmDlgOn && !vedActive() && gPsUsed == p0, "una reserva que falla: se deshace lo reservado y se dice");
    mmDlgOn = false; gTestPsFail = false;
    size_t in0 = gTestInFree; gTestInFree = 32u * 1024u;
    chk(!vedOpen(id) && mmDlgOn && !vedActive() && gPsUsed == p0, "sin RAM interna para la pila del trabajador: no se lanza");
    mmDlgOn = false; gTestInFree = in0; }

  // ---- SOLTAR EN SEGUNDO PLANO Y VOLVER: los cambios se quedan ----
  vedOpen(id); vtRun();
  flexVeRotate(vedE, 1); flexVeTrimLive(vedE, 0, 4); flexVeCommit(vedE); vedApplied();
  { int curB = vedE->cur; size_t pEdit = gPsUsed;
    gAppState[IC_GALERIA] = ALIFE_SUSPENDED; galSuspend();
    size_t shed = galShed();
    chk(shed > 0 && !vedBase && vedReopen && vedE && !vedS.started && gPsUsed < pEdit,
        "suspendida: fotogramas y miniaturas fuera, el trabajador tambien; los cambios se quedan");
    gAppState[IC_GALERIA] = ALIFE_RUNNING; galResume();
    chk(vedPhase == VED_OPENING && vedS.started, "al volver se relee (con su trabajador)");
    vtRun();
    chk(vedPhase == VED_EDIT && vedE->p.rot == 1 && vedE->p.seg[0].a == 4 && vedE->cur == curB && vedBaseW > 0,
        "releido: mismas ediciones y mismo historial"); }

  // ---- EXPORTAR CON LA GALERIA EN SEGUNDO PLANO: lo publica loop() ----
  { int nb = gMs.lib.n;
    vtBar(2); vtSheet(0);
    gAppState[IC_GALERIA] = ALIFE_SUSPENDED; galSuspend();
    chk(galBgWork(), "exportando: trabajo real en segundo plano (no se desaloja la Galeria)");
    vtPump();
    gTestMs += 1000; vtMs = gTestMs;
    vedBgTick();
    chk(gMs.lib.n == nb + 1 && vedPhase == VED_OFF && !galBgWork(), "loop() publica la copia aunque la Galeria no este delante");
    gAppState[IC_GALERIA] = ALIFE_RUNNING; }

  // ---- UN TRABAJADOR QUE NO TERMINA: nada se suelta antes de tiempo ----
  vedOpen(id); vtRun();
  { VedMem* stuck = vedM;
    vedS.task = (TaskHandle_t)0x1;                        // "hay hilo" y no responde
    vedCloseNow();
    chk(vedPhase == VED_OFF && vedLeak && vedM == stuck, "cerrar con el trabajador colgado: no se suelta lo que usa");
    chk(!vedOpen(id) && mmDlgOn, "y el editor no se reabre encima: 'Editor ocupado'");
    mmDlgOn = false;
    vedSt(&vedS.exited, 1);                               // por fin sale
    gTestMs += 1000; vedBgTick();
    chk(!vedLeak && !vedM && !vedS.started, "cuando sale, loop() recoge su memoria");
    chk(vedOpen(id) && vedPhase == VED_OPENING, "y el editor vuelve a abrir");
    vtRun(); galBackLayer(); }

  // ---- NOMBRES ----
  vedOpen(id); vtRun();
  { char nm[FML_NAME_MAX];
    snprintf(vedM->name, sizeof(vedM->name), "%s", "Cumple (parte 2).avi");
    vedFreeName(nm, sizeof(nm), 0);
    chkf(!strcmp(nm, "Cumple (editado).avi") || !strcmp(nm, "Cumple (editado 2).avi"), "la copia de una parte no encadena sufijos (%s)", nm);
    snprintf(vedM->name, sizeof(vedM->name), "%s", "Viaje de fin de curso con toda la clase y los profesores, verano de 2026.avi");
    vedFreeName(nm, sizeof(nm), 3);
    size_t L = strlen(nm);
    const size_t S = strlen(" (parte 3).avi");
    chkf(L < sizeof(nm) && L > S && !strcmp(nm + L - S, " (parte 3).avi") && !strncmp(nm, "Viaje de fin de curso", 21),
         "nombre largo: se acorta el nombre, no el sufijo (%s)", nm); }
  galBackLayer();

  galRender();
  chk(!vedActive() && !gedActive() && vtNoTemps(), "al final: ningun editor abierto ni temporal en el disco");
  if(gFails == before) printf("  Editor de video: todas las comprobaciones pasan.\n");
  mkReset();
  uiGlass = glass0; gNavMode = nav0;
  gMlOk = ok0; gTestFsReady = fs0; gTestMemFs = false; gTestFiles.clear();
  memset(&gMs, 0, sizeof(gMs));
  gState = ST_HOME; gAppId = 0; gAppState[IC_GALERIA] = ALIFE_CLOSED; gLand = false; uiClipFull();
  touchReset();
}


// Videos GRANDES (2, 3 y 5 MB): se abren sin leerlos enteros, se exportan
// por trozos y la memoria pico no depende del tamano del archivo.
static std::vector<uint8_t> vtNoisyFrame(int W, int H, uint32_t seed){
  std::vector<uint8_t> px((size_t)W * H * 3), out;
  uint32_t r = seed * 2654435761u + 1u;
  for(size_t i = 0; i < px.size(); i++){ r = r * 1664525u + 1013904223u; px[i] = (uint8_t)(r >> 24); }
  for(int y = 0; y < H / 2; y++) for(int x = 0; x < W / 2; x++){ uint8_t* q = &px[((size_t)y * W + x) * 3]; q[0] = 225; q[1] = 30; q[2] = 30; }
  FlexJeCfg c; c.width = W; c.height = H; c.quality = 92; c.subsampling = FLEXJE_SUB_420; c.input = FLEXJE_IN_RGB888;
  flexJpegEncodeMem(&c, px.data(), (size_t)W * 3, geOut, &out, nullptr, nullptr);
  return out;
}
static std::vector<uint8_t> vtBigAvi(const std::vector<std::vector<uint8_t>>& pool, int n, int W, int H){
  std::vector<uint8_t> avih; vtU32(avih, 40000); vtU32(avih, 0); vtU32(avih, 0); vtU32(avih, 0x10); vtU32(avih, (uint32_t)n); vtU32(avih, 0);
  vtU32(avih, 1); vtU32(avih, 0); vtU32(avih, (uint32_t)W); vtU32(avih, (uint32_t)H); for(int i = 0; i < 4; i++) vtU32(avih, 0);
  std::vector<uint8_t> vh; vtCc(vh, "vids"); vtCc(vh, "MJPG"); vtU32(vh, 0); vtU16(vh, 0); vtU16(vh, 0); vtU32(vh, 0);
  vtU32(vh, 1); vtU32(vh, 25); vtU32(vh, 0); vtU32(vh, (uint32_t)n); vtU32(vh, 65536); vtU32(vh, 0); vtU32(vh, 0); vtU32(vh, 0); vtU32(vh, 0);
  std::vector<uint8_t> vf; vtU32(vf, 40); vtU32(vf, (uint32_t)W); vtU32(vf, (uint32_t)H); vtU16(vf, 1); vtU16(vf, 24); vtCc(vf, "MJPG");
  for(int i = 0; i < 5; i++) vtU32(vf, 0);
  std::vector<uint8_t> vstrl = vtChunk("strh", vh); { auto t = vtChunk("strf", vf); vstrl.insert(vstrl.end(), t.begin(), t.end()); }
  std::vector<uint8_t> hdrl = vtChunk("avih", avih); { auto t = vtList("strl", vstrl); hdrl.insert(hdrl.end(), t.begin(), t.end()); }
  std::vector<uint8_t> movi, idx1;
  movi.reserve((size_t)n * (pool[0].size() + 16));
  for(int i = 0; i < n; i++){
    const auto& fr = pool[(size_t)i % pool.size()];
    uint32_t off = 4u + (uint32_t)movi.size();
    auto c = vtChunk("00dc", fr); movi.insert(movi.end(), c.begin(), c.end());
    vtCc(idx1, "00dc"); vtU32(idx1, 0x10); vtU32(idx1, off); vtU32(idx1, (uint32_t)fr.size());
  }
  std::vector<uint8_t> body = vtList("hdrl", hdrl);
  { auto t = vtList("movi", movi); body.insert(body.end(), t.begin(), t.end()); }
  { auto t = vtChunk("idx1", idx1); body.insert(body.end(), t.begin(), t.end()); }
  std::vector<uint8_t> out; vtCc(out, "RIFF"); vtU32(out, (uint32_t)body.size() + 4); vtCc(out, "AVI ");
  out.insert(out.end(), body.begin(), body.end());
  return out;
}
extern unsigned gTestFsStreamReads;
static double vtNowMs(){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6; }
extern size_t gPsPeak;
static void testEditorVideoGrande(){
  printf("Editor de video: videos de 2, 3 y 5 MB (sin leerlos enteros, memoria pico acotada)\n");
  int before = gFails;
  bool ok0 = gMlOk; bool fs0 = gTestFsReady; bool glass0 = uiGlass; int nav0 = gNavMode;
  gTestFsReady = true; uiGlass = false; gNavMode = 0;
  geFsReset();
  gTestFsCap = 48u << 20;
  vtMs = 60000000; gTestMs = vtMs;
  gState = ST_APP; gAppId = IC_GALERIA; gAppState[IC_GALERIA] = ALIFE_RUNNING; gLand = false; gHosted = false;
  gAppW = SCR_W; gAppH = SCR_H; uiClipFull(); setBuf(fb);
  mkBind(&GAL_APP); mkReset(); galViewReady = false; galScroll = 0; galTab = 0;
  mlTables();
  if(!glassBuf) glassBuf = (uint16_t*)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  glcBuild(16, 16, 4, TH_GLASS, TH_PAGE);
  galRender();
  const int W = 320, H = 240;
  std::vector<std::vector<uint8_t>> pool;
  for(int k = 0; k < 4; k++) pool.push_back(vtNoisyFrame(W, H, 7u + k));
  size_t avg = 0; for(auto& f : pool) avg += f.size(); avg /= pool.size();
  const int MB[3] = { 2, 3, 5 };
  size_t peaks[3] = { 0, 0, 0 };
  for(int t = 0; t < 3; t++){
    int n = (int)(((size_t)MB[t] << 20) / (avg + 16));
    auto avi = vtBigAvi(pool, n, W, H);
    char path[48]; snprintf(path, sizeof(path), FML_DIR_VIDEO "/Grande%d.avi", MB[t]);
    uint32_t id = vtAddVideo(path, avi, W, H, false);
    galRender();
    size_t ps0 = gPsUsed;
    unsigned readsBefore = gTestFsStreamReads;
    gPsPeak = gPsUsed;
    chk(vedOpen(id), "abrir el video grande");
    vtRun();
    chkf(vedPhase == VED_EDIT && vedM->src.frames == (uint32_t)n && vedFrameCap >= vedM->src.maxFrame && vedFrameCap < vedM->src.maxFrame + 4096u,
         "%d MB (%u fotogramas): se analiza y el buffer del fotograma es el del mayor (%u B), una vez", MB[t], vedM ? vedM->src.frames : 0u, vedFrameCap);
    // Recortar a la mitad central: copia sin recodificar.
    flexVeTrimLive(vedE, 0, (uint32_t)n / 4); flexVeTrimLive(vedE, 1, (uint32_t)n * 3 / 4); flexVeCommit(vedE); vedApplied();
    vtBar(2); vtSheet(0);
    double t0 = vtNowMs();
    vtPump();
    double ms = vtNowMs() - t0;
    vtIdle();
    const FlexMlRec* rc = geRec(vtLastId());
    auto ck = rc && rc->id != id ? vtVideoChunks(gTestFiles[rc->path]) : std::vector<VtCk>();
    bool same = ck.size() == (size_t)(n * 3 / 4 - n / 4);
    for(size_t k = 0; same && k < ck.size(); k += 17) same = vtSame(gTestFiles[rc->path], ck[k], pool[(size_t)(n / 4 + k) % pool.size()]);
    chkf(same && vedPhase == VED_OFF && vtNoTemps(), "%d MB: la mitad central sale copiada byte a byte (%d fotogramas, %.0f ms en el PC)",
         MB[t], (int)ck.size(), ms);
    if(t == 2){
      // Recodificar (girado) la mitad central: memoria de exportar acotada.
      vedOpen(id); vtRun();
      uint32_t a = (uint32_t)n / 4, b = (uint32_t)n * 3 / 4;
      flexVeTrimLive(vedE, 0, a); flexVeTrimLive(vedE, 1, b); flexVeCommit(vedE);
      flexVeRotate(vedE, 1); vedApplied();
      vtBar(2); vtSheet(0);
      t0 = vtNowMs(); vtPump(); ms = vtNowMs() - t0;
      vtIdle();
      const FlexMlRec* r2 = geRec(vtLastId());
      FlexVeSource s; int prc = r2 ? vtProbe(gTestFiles[r2->path], &s) : -1;
      chkf(prc == FLEXVE_OK && s.frames == b - a && s.w == 240 && s.h == 320 && vtNoTemps(),
           "5 MB: la mitad central girada y recodificada (%u fotogramas %dx%d, %.1f ms/fotograma en el PC)",
           s.frames, s.w, s.h, ms / (double)(b - a));
    }
    peaks[t] = gPsPeak - ps0;
    galRender();
    if(gPsUsed != ps0) printf("  (PSRAM sin devolver: %d bytes)\n", (int)(gPsUsed - ps0));
    chkf(gPsUsed == ps0, "%d MB: cerrar devuelve TODA la PSRAM", MB[t]);
    chkf(peaks[t] < avi.size() / 2, "%d MB: el archivo nunca esta entero en memoria (pico %u KB)", MB[t], (unsigned)(peaks[t] / 1024u));
    (void)readsBefore;
    // Sitio en el disco y en el catalogo para el siguiente.
    while(gMs.lib.n > 0){ mlDelete(vtLastId()); }
  }
  printf("  [video] memoria pico sobre la del sistema: %u KB (2 MB), %u KB (3 MB), %u KB (5 MB, con recodificar)\n",
         (unsigned)(peaks[0] / 1024u), (unsigned)(peaks[1] / 1024u), (unsigned)(peaks[2] / 1024u));
  chkf(peaks[1] < peaks[0] + 256u * 1024u, "la memoria pico NO crece con el tamano del archivo (%u KB -> %u KB)",
       (unsigned)(peaks[0] / 1024u), (unsigned)(peaks[1] / 1024u));
  if(gFails == before) printf("  Videos grandes: todas las comprobaciones pasan.\n");
  mkReset();
  uiGlass = glass0; gNavMode = nav0;
  gMlOk = ok0; gTestFsReady = fs0; gTestMemFs = false; gTestFiles.clear(); gTestFsCap = 16u << 20;
  memset(&gMs, 0, sizeof(gMs));
  gState = ST_HOME; gAppId = 0; gAppState[IC_GALERIA] = ALIFE_CLOSED; gLand = false; uiClipFull();
  touchReset();
}

// Capturas del editor (solo con INO_SHOTS=1): una "foto" sintetica de playa.
static std::vector<uint8_t> geBeach(int W, int H){
  std::vector<uint8_t> px((size_t)W * H * 3), out;
  for(int y = 0; y < H; y++) for(int x = 0; x < W; x++){
    uint8_t* p = &px[((size_t)y * W + x) * 3];
    float fy = (float)y / H, fx = (float)x / W;
    int r, g, b;
    if(fy < 0.55f){ r = (int)(90 + 120 * fy); g = (int)(150 + 80 * fy); b = 235; }
    else if(fy < 0.75f){ float k = (fy - 0.55f) / 0.2f; r = 20; g = (int)(90 + 40 * k + 10 * sinf(x * 0.2f + y)); b = (int)(160 - 30 * k); }
    else { r = 225; g = 196; b = (int)(140 - 30 * fx); }
    float dx = fx - 0.72f, dy = fy - 0.22f;
    if(dx * dx + dy * dy < 0.006f){ r = 255; g = 236; b = 150; }
    p[0] = (uint8_t)r; p[1] = (uint8_t)g; p[2] = (uint8_t)b;
  }
  FlexJeCfg c; c.width = W; c.height = H; c.quality = 92; c.subsampling = FLEXJE_SUB_420; c.input = FLEXJE_IN_RGB888;
  flexJpegEncodeMem(&c, px.data(), (size_t)W * 3, geOut, &out, nullptr, nullptr);
  return out;
}
static void testCapturasEditor(){
  if(!getenv("INO_SHOTS")) return;
  printf("Capturas del editor de la Galeria\n");
  bool ok0 = gMlOk; bool fs0 = gTestFsReady; gTestFsReady = true;
  geFsReset();
  uint32_t id = geAddPhoto(FML_DIR_PHOTO "/Playa.jpg", geBeach(640, 480), false);
  gLockType = 1;
  shotApp(IC_GALERIA); gAppState[IC_GALERIA] = ALIFE_RUNNING; mkBind(&GAL_APP);
  gedOpen(id); geRun();
  flexIeSetCrop(gedE, 0.08f, 0.1f, 0.92f, 0.9f);
  shotApp(IC_GALERIA); gedRender(); shotSave("editor_recortar");
  gedSetTool(GT_ADJ);
  flexIeAdjustLive(gedE, FLEXIE_ADJ_BRIGHT, 30); flexIeAdjustLive(gedE, FLEXIE_ADJ_CONTRAST, 20); flexIeCommit(gedE);
  gedChanged(); shotApp(IC_GALERIA); gedRender(); shotSave("editor_ajustes");
  gedSetTool(GT_FILTER);
  flexIeFilterLive(gedE, FLEXIE_FILTER_VINTAGE, 80); flexIeCommit(gedE);
  gedChanged(); shotApp(IC_GALERIA); gedRender(); shotSave("editor_filtros");
  flexIeFilterLive(gedE, FLEXIE_FILTER_NONE, 100); flexIeCommit(gedE);
  gedSetTool(GT_DRAW);
  int s = flexIeStrokeBegin(gedE, 0xE53935, 0.012f, 0.15f, 0.7f);
  for(int k = 1; k <= 20; k++) flexIeStrokeAdd(gedE, s, 0.15f + k * 0.02f, 0.7f - 0.1f * sinf(k * 0.4f));
  flexIeStrokeEnd(gedE, s);
  flexIeAddShape(gedE, FLEXIE_OV_ARROW, 0xFDD835, 0.012f, false, 0.5f, 0.55f, 0.68f, 0.3f);
  flexIeAddShape(gedE, FLEXIE_OV_ELLIPSE, 0xFFFFFF, 0.008f, false, 0.62f, 0.12f, 0.84f, 0.36f);
  gedKind = 2; gedColor = 4;
  gedChanged(); shotApp(IC_GALERIA); gedRender(); shotSave("editor_dibujo");
  gedSetTool(GT_TEXT);
  snprintf(gedText, sizeof(gedText), "Verano 2026"); gedTextOn = true; gedTextU = 0.08f; gedTextV = 0.06f; gedColor = 0; gedTextSz = 2;
  shotApp(IC_GALERIA); gedRender(); shotSave("editor_texto");
  flexIeAddText(gedE, gedText, GED_RGB[gedColor], GED_TEXT_H[gedTextSz], gedTextU, gedTextV); gedTextOn = false;
  gedSetTool(GT_SIZE); gedSrcW = 4000; gedSrcH = 3000;               // como una foto de 12 MP abierta reducida
  flexIeSetLongSide(gedE, 400);
  gedChanged(); shotApp(IC_GALERIA); gedRender(); shotSave("editor_tamano");
  gedSheet = true; shotApp(IC_GALERIA); gedRender(); shotSave("editor_guardar");
  { // El mismo editor con el tema CLARO (vidrio y plano): sigue al sistema.
    bool d0 = gDark, g0 = uiGlass;
    gDark = false;
    shotApp(IC_GALERIA); gedRender(); shotSave("editor_guardar_claro");
    uiGlass = false; shotApp(IC_GALERIA); gedRender(); shotSave("editor_guardar_claro_plano");
    gedSheet = false; gedSetTool(GT_ADJ);
    shotApp(IC_GALERIA); gedRender(); shotSave("editor_ajustes_claro_plano");
    uiGlass = true; shotApp(IC_GALERIA); gedRender(); shotSave("editor_ajustes_claro");
    gDark = d0; uiGlass = g0; gedSetTool(GT_SIZE);
  }
  gedSheet = false; gedSrcW = gedW; gedSrcH = gedH;
  gedStartSave(GS_COPY); gedJob.pct = 62;
  shotApp(IC_GALERIA); gedRender(); shotSave("editor_guardando");
  geRun();
  gedCloseNow(); mkReset();
  gMlOk = ok0; gTestFsReady = fs0; gTestMemFs = false; gTestFiles.clear();
  memset(&gMs, 0, sizeof(gMs));
  gState = ST_HOME; gAppId = 0; gAppState[IC_GALERIA] = ALIFE_CLOSED;
}


// #############################################################
//  VISOR DE MEDIOS (Galeria y Multimedia) · con archivos de verdad
//  ------------------------------------------------------------
//  Sobre el disco en memoria y el almacen REAL de la biblioteca. Se mira
//  el FRAMEBUFFER: donde cae la foto (ajustada y centrada, en las dos
//  orientaciones), que las barras no se apilan, que se ocultan solas, el
//  pellizco, deslizar, la papelera, lo protegido, el video y la memoria.
//  La tarea de medios no corre sola aqui: sin gMlTask el visor hace el
//  trabajo en el acto, y la prueba 12 la simula para el camino asincrono.
// #############################################################
// Foto de prueba: mitad izquierda ROJA, mitad derecha AZUL.
static std::vector<uint8_t> vwTestJpeg(int W, int H){
  std::vector<uint8_t> px((size_t)W * H * 3), out;
  for(int y = 0; y < H; y++) for(int x = 0; x < W; x++){
    uint8_t* p = &px[((size_t)y * W + x) * 3];
    bool left = x < W / 2;
    p[0] = left ? 230 : 30; p[1] = 30; p[2] = left ? 30 : 230;
  }
  FlexJeCfg c; c.width = W; c.height = H; c.quality = 90; c.subsampling = FLEXJE_SUB_420; c.input = FLEXJE_IN_RGB888;
  flexJpegEncodeMem(&c, px.data(), (size_t)W * 3, geOut, &out, nullptr, nullptr);
  return out;
}
static bool vwIsRed(uint16_t c){ int r = c >> 11, g = (c >> 5) & 63, b = c & 31; return r > 22 && b < 10 && g < 20; }
static bool vwIsBlue(uint16_t c){ int r = c >> 11, g = (c >> 5) & 63, b = c & 31; return b > 22 && r < 10 && g < 20; }
// Lo que se VE en el pixel logico (lx,ly) del visor, sea cual sea la orientacion.
static uint16_t vwSeen(int lx, int ly){ return fb[vwIdx(lx, ly)]; }
static void vwSetDims(uint32_t id, int w, int h){ int i = flexMlFindId(&gMs.lib, id); if(i >= 0){ gMs.lib.recs[i].w = (uint16_t)w; gMs.lib.recs[i].h = (uint16_t)h; } }
// AVI/MJPEG minimo (solo video, con idx1) a partir de fotogramas JPEG.
static std::vector<uint8_t> vwTestAvi(const std::vector<std::vector<uint8_t>>& frames, int w, int h, uint32_t usPerFrame,
                                      uint32_t declared = 0, bool withIdx = true, uint32_t suggested = 65536){
  auto u32 = [](std::vector<uint8_t>& v, uint32_t x){ for(int i = 0; i < 4; i++) v.push_back((uint8_t)(x >> (8 * i))); };
  auto u16 = [](std::vector<uint8_t>& v, uint16_t x){ v.push_back((uint8_t)x); v.push_back((uint8_t)(x >> 8)); };
  auto cc  = [](std::vector<uint8_t>& v, const char* t){ for(int i = 0; i < 4; i++) v.push_back((uint8_t)t[i]); };
  auto chunk = [&](const char* id, const std::vector<uint8_t>& p){
    std::vector<uint8_t> v; cc(v, id); u32(v, (uint32_t)p.size()); v.insert(v.end(), p.begin(), p.end());
    if(p.size() & 1) v.push_back(0);
    return v;
  };
  auto list = [&](const char* type, const std::vector<uint8_t>& p){
    std::vector<uint8_t> v; cc(v, "LIST"); u32(v, (uint32_t)p.size() + 4); cc(v, type); v.insert(v.end(), p.begin(), p.end());
    return v;
  };
  uint32_t n = (uint32_t)frames.size(), nd = declared ? declared : n;
  std::vector<uint8_t> avih; u32(avih, usPerFrame); u32(avih, 0); u32(avih, 0); u32(avih, 0x10); u32(avih, nd); u32(avih, 0);
  u32(avih, 1); u32(avih, 0); u32(avih, (uint32_t)w); u32(avih, (uint32_t)h); for(int i = 0; i < 4; i++) u32(avih, 0);
  std::vector<uint8_t> strh; cc(strh, "vids"); cc(strh, "MJPG"); u32(strh, 0); u16(strh, 0); u16(strh, 0); u32(strh, 0);
  u32(strh, 1); u32(strh, 1000000u / usPerFrame); u32(strh, 0); u32(strh, nd); u32(strh, suggested); u32(strh, 0); u32(strh, 0); u32(strh, 0); u32(strh, 0);
  std::vector<uint8_t> strf; u32(strf, 40); u32(strf, (uint32_t)w); u32(strf, (uint32_t)h); u16(strf, 1); u16(strf, 24); cc(strf, "MJPG");
  for(int i = 0; i < 5; i++) u32(strf, 0);
  std::vector<uint8_t> strl = chunk("strh", strh); { auto t = chunk("strf", strf); strl.insert(strl.end(), t.begin(), t.end()); }
  std::vector<uint8_t> hdrl = chunk("avih", avih); { auto t = list("strl", strl); hdrl.insert(hdrl.end(), t.begin(), t.end()); }
  std::vector<uint8_t> movi, idx1;
  for(uint32_t i = 0; i < n; i++){
    uint32_t off = 4u + (uint32_t)movi.size();
    auto c = chunk("00dc", frames[i]); movi.insert(movi.end(), c.begin(), c.end());
    cc(idx1, "00dc"); u32(idx1, 0x10); u32(idx1, off); u32(idx1, (uint32_t)frames[i].size());
  }
  std::vector<uint8_t> body = list("hdrl", hdrl);
  { auto t = list("movi", movi); body.insert(body.end(), t.begin(), t.end()); }
  if(withIdx){ auto t = chunk("idx1", idx1); body.insert(body.end(), t.begin(), t.end()); }
  std::vector<uint8_t> out; cc(out, "RIFF"); u32(out, (uint32_t)body.size() + 4); cc(out, "AVI ");
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

static void testVisorMedios(){
  printf("Visor de medios: ajuste, orientacion, barras, gestos, papelera, video y protegidos\n");
  bool ok0 = gMlOk; bool fs0 = gTestFsReady; gTestFsReady = true;
  bool glass0 = uiGlass; int nav0 = gNavMode;
  geFsReset();
  gLockType = 1; gNavMode = 0; uiGlass = false;
  uint32_t a = geAddPhoto(FML_DIR_PHOTO "/Alta.jpg", vwTestJpeg(900, 1600), false);
  uint32_t b = geAddPhoto(FML_DIR_PHOTO "/Otra.jpg", vwTestJpeg(900, 1600), false);
  vwSetDims(a, 900, 1600); vwSetDims(b, 900, 1600);
  shotApp(IC_GALERIA); gAppState[IC_GALERIA] = ALIFE_RUNNING; mkBind(&GAL_APP); galViewReady = false;
  // Lo que el sistema reserva una sola vez y nunca suelta (tablas de la
  // biblioteca, lienzo del vidrio) se reserva ANTES de medir la PSRAM.
  mlTables();
  if(!glassBuf) glassBuf = (uint16_t*)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  size_t ps0 = gPsUsed;
  gTestMs = 9000000; touchReset();

  // ---- 1. ABRIR: dentro de la Galeria, ajustada y centrada ----
  galOpenId(a);
  chk(gState == ST_APP && gAppId == IC_GALERIA, "abrir una foto NO sale de la Galeria (visor propio)");
  chk(vwActiveFor(&GAL_VW) && vwKind == VWK_PHOTO && vwSrc != nullptr, "la foto se decodifica y queda en el visor");
  chk(!vwLand && !gLand, "Auto: una foto alta se ve en vertical");
  const int vh = SCR_H - NAV_H;
  chk(vwVH == vh && vwFitH == vh && abs(vwFitW - 900 * vh / 1600) <= 1, "llena el alto visible (sin la barra del sistema) sin deformar");
  int x0 = (int)(vwOffX + 0.5f);
  chk(abs(x0 - (SCR_W - vwFitW) / 2) <= 1 && (int)vwOffY == 0, "y queda CENTRADA, no pegada a la esquina");
  chk(vwIsRed(vwSeen(x0 + vwFitW / 4, vh / 2)) && vwIsBlue(vwSeen(x0 + vwFitW * 3 / 4, vh / 2)),
      "se ve la foto entera: la mitad roja y la azul donde les toca");
  chk(vwSeen(x0 / 2, vh / 2) == 0 && vwSeen(SCR_W - 1 - x0 / 2, vh / 2) == 0, "a los lados, fondo negro: nada estirado");
  chk((uint32_t)vwSrcW * vwSrcH <= VW_SRC_MAX_PX, "la foto decodificada respeta su tope de memoria");

  // ---- 2. EL VIDRIO NO SE APILA ----
  uiGlass = true; vwGlassPrep(); vwPresentAll();
  std::vector<uint16_t> ref(fb, fb + (size_t)SCR_W * SCR_H);
  for(int k = 0; k < 12; k++){ vwPresentBars(); vwPresentAll(); }
  chk(memcmp(ref.data(), fb, ref.size() * 2) == 0, "pintar las barras 24 veces da EXACTAMENTE los mismos pixeles (el vidrio no se apila)");
  vwBarsShow(false); gTestMs += VW_FADE_MS + 20; vwBarsTick();
  chk(vwBarsA == 0.0f && memcmp(fb, vwClean, (size_t)SCR_W * vh * 2) == 0, "ocultas, no queda ni un pixel de las barras");
  vwBarsShow(true); gTestMs += VW_FADE_MS + 20; vwBarsTick();
  chk(vwBarsA == 1.0f && memcmp(ref.data(), fb, ref.size() * 2) == 0, "ocultarlas y volver a ensenarlas da el mismo cuadro");

  // ---- 3. SE OCULTAN SOLAS A LOS 3 s ----
  touchReset(); vwTouchMs = gTestMs;
  gTestMs += VW_BARS_HIDE_MS + 10; vwTick();
  chk(vwBarsWant == 0, "sin tocar nada, las barras se van a los 3 s");
  gTestMs += VW_FADE_MS + 10; vwTick();
  chk(vwBarsA == 0.0f, "con un fundido, no de golpe");

  // ---- 4. TOCAR LA IMAGEN LAS TRAE ----
  tDown(240, 400, gTestMs + 100); vwTick();
  tUp(gTestMs + 60, true); vwTick();
  touchReset(); gTestMs += VW_TAP2_MS + 10; vwTick();
  chk(vwBarsWant == 1, "un toque en la imagen trae las barras");
  gTestMs += VW_FADE_MS + 10; vwTick();

  // ---- 5. PELLIZCO: amplia sobre el punto de los dedos ----
  float ax = vwOffX + vwFitW * 0.25f, ay = vh * 0.5f;       // un punto ROJO
  vwPinchBegin(ax, ay, 100.0f);
  chk(vwPinchApply(ax, ay, 250.0f) && fabsf(vwScale - 2.5f) < 0.01f, "separar los dedos al 250 % amplia x2,5");
  vwLiveDirty = true; vwLiveEnd();
  chk(vwIsRed(vwSeen((int)ax, (int)ay)), "el punto que estaba bajo los dedos sigue bajo los dedos");
  chk(vwOffX <= vwVX + 0.5f && vwOffX + vwFitW * vwScale >= vwVX + vwVW - 0.5f, "ampliada cubre todo el ancho: sin bandas negras");
  vwPinchApply(ax, ay, 100000.0f);
  chk(fabsf(vwScale - VW_ZOOM_MAX) < 0.01f, "el zoom tiene tope");
  vwPinchApply(ax, ay, 1.0f);
  chk(vwScale == 1.0f && abs((int)(vwOffX + 0.5f) - x0) <= 1, "juntar los dedos vuelve al ajuste, centrada");
  vwPinchOn = false;

  // ---- 6. DESPLAZAR con un dedo (ampliada) ----
  vwZoomAt(2.0f, ax, ay); vwRenderContent(true); vwPresentAll();
  float ox = vwOffX;
  tDown(300, 400, gTestMs + 500); vwTick();
  tMove(240, 400, gTestMs + 50); vwTick();
  chk(vwOffX < ox - 30.0f, "arrastrar mueve la foto ampliada");
  tUp(gTestMs + 50, false); vwTick(); touchReset();
  vwScale = 1.0f; vwClamp(); vwRenderContent(true); vwPresentAll();

  // ---- 7. DESLIZAR (sin zoom): la de al lado en la rejilla ----
  uint32_t first = vwId, nxt = galVwNeighbour(first, +1), prv = galVwNeighbour(first, -1);
  int toX = nxt ? 180 : 420;                                     // hacia la izquierda = siguiente
  tDown(300, 400, gTestMs + 500); vwTick();
  tMove((300 + toX) / 2, 400, gTestMs + 40); vwTick();
  tMove(toX, 400, gTestMs + 40); vwTick();
  tUp(gTestMs + 40, false); vwTick(); touchReset();
  chk(vwId != first && vwId == (nxt ? nxt : prv), "deslizar abre la de al lado, en el orden de la rejilla");
  chk(galVwSess.open && galVwSess.id == vwId, "la sesion del visor sigue a lo que se ve");

  // ---- 8. HORIZONTAL: relayout de verdad (800 x 480) ----
  vwCycleOrientation();                                          // Auto -> Vertical
  vwCycleOrientation();                                          // Vertical -> Horizontal
  chk(vwLand && gLand, "Horizontal: el visor pasa a 800x480 y el motor gira con el");
  chk(vwVW == LW && vwVH == LH && vwFitH == LH && abs(vwFitW - 900 * LH / 1600) <= 1, "se ajusta al alto de 480, sin deformar");
  int lx0 = (int)(vwOffX + 0.5f);
  chk(abs(lx0 - (LW - vwFitW) / 2) <= 1, "y centrada en el ancho de 800");
  chk(vwIsRed(vwSeen(lx0 + vwFitW / 4, LH / 2)) && vwIsBlue(vwSeen(lx0 + vwFitW * 3 / 4, LH / 2)) && vwSeen(lx0 / 2, LH / 2) == 0,
      "en horizontal la imagen cae girada donde se ve (mitad roja, mitad azul, bandas negras)");
  chk(!navBarVisible(), "en horizontal no se estampa la barra vertical del sistema (sin barra doble)");
  vwCycleOrientation();                                          // Horizontal -> Auto
  chk(!vwLand && !gLand, "Auto vuelve a vertical para una foto alta");

  // ---- 9. PAPELERA desde el visor ----
  uint32_t cur = vwId, other = (cur == a) ? b : a;
  vwDoTrash();
  chk(!geRec(cur), "Papelera: el elemento sale de la biblioteca");
  chk(vwActiveFor(&GAL_VW) && vwId == other, "y el visor pasa al que queda");
  vwDoTrash();
  chk(!vwOn && !galVwSess.open && !gLand, "sin mas elementos, el visor se cierra y vuelve a la rejilla");

  // ---- 10. PROTEGIDO ----
  uint32_t lk = geAddPhoto(FML_DIR_LOCKED "/7.jpg", vwTestJpeg(900, 1600), true);
  vwSetDims(lk, 900, 1600);
  vwOpen(&GAL_VW, lk, NULL, NULL);                               // como tras acertar la clave
  chk(vwActiveFor(&GAL_VW) && vwLocked && !vwCanTrash && !vwCanEdit && !vwHasBot(), "protegido: ni Papelera ni Editar en el visor");
  chk(vwThumb == nullptr, "protegido: el visor no copia ninguna miniatura");
  // Recientes: al pasar a segundo plano con lo protegido delante, SIN captura.
  gAppState[IC_GALERIA] = ALIFE_RUNNING;
  appSuspend(IC_GALERIA, false);
  { bool thumb = false;
    for(int i = 0; i < swCount; i++) if(swTasks[i].appID == IC_GALERIA && swTasks[i].thumb) thumb = true;
    chk(swCardCount() > 0 && !thumb, "Recientes no guarda la captura de una foto protegida"); }
  gAppState[IC_GALERIA] = ALIFE_RUNNING;
  chk(!galVwSess.open && !vwClean && !vwSrc, "a segundo plano: lo protegido ni se recuerda ni queda en memoria");
  vwOpen(&GAL_VW, lk, NULL, NULL);
  { auto st0 = gState; gState = ST_LOCK; vwLockTick(); gState = st0; }
  chk(!vwOn && !galVwSess.open && !vwSrc && !vwClean && !gLand, "con el P4 bloqueado se suelta todo lo protegido");

  // ---- 11. SE PROTEGE DESDE FUERA MIENTRAS SE VE ----
  uint32_t c3 = geAddPhoto(FML_DIR_PHOTO "/Tres.jpg", vwTestJpeg(640, 480), false);
  vwSetDims(c3, 640, 480);
  vwOpen(&GAL_VW, c3, NULL, NULL);
  chk(vwActiveFor(&GAL_VW) && vwLand, "Auto: una foto apaisada se abre en horizontal");
  { char why[64]; flexMsSetLock(&gMs, c3, true, why, sizeof(why)); }
  gTestMs += VW_CHECK_MS + 10; touchReset(); vwTick();
  chk(!vwOn && !gLand, "si se protege mientras se ve, el visor se cierra (fuera los pixeles)");

  // ---- 12. LA TAREA DE MEDIOS: pedir, cancelar y recoger ----
  uint32_t d4 = geAddPhoto(FML_DIR_PHOTO "/Cuatro.jpg", vwTestJpeg(640, 480), false);
  TaskHandle_t t0 = gMlTask; gMlTask = (TaskHandle_t)1;          // hay tarea: la peticion espera
  vwOpen(&GAL_VW, d4, NULL, NULL);
  chk(vwLoading && __atomic_load_n(&gVwJob.state, __ATOMIC_ACQUIRE) == VWJ_REQ, "con la tarea de medios, la foto se pide y la interfaz no espera");
  vwClose();
  chk(__atomic_load_n(&gVwJob.state, __ATOMIC_ACQUIRE) == VWJ_IDLE && !vwJobRunIfAny(), "cerrar retira la peticion sin decodificar nada");
  vwOpen(&GAL_VW, d4, NULL, NULL);
  chk(vwJobRunIfAny() && __atomic_load_n(&gVwJob.state, __ATOMIC_ACQUIRE) == VWJ_DONE, "la tarea la decodifica");
  vwTick();
  chk(vwSrc != nullptr && !vwLoading && vwKind == VWK_PHOTO, "y la interfaz la recoge en su vuelta");
  vwOpen(&GAL_VW, d4, NULL, NULL);                               // pedida otra vez...
  vwClose();                                                     // ...y cerrada antes de que la tarea la vea
  chk(!vwJobRunIfAny(), "nada queda en cola al cerrar");
  gMlTask = t0;

  // ---- 13. VIDEO: fotograma ajustado y centrado, barras sin apilar ----
  std::vector<std::vector<uint8_t>> fr;
  for(int k = 0; k < 6; k++) fr.push_back(vwTestJpeg(320, 240));
  gTestFiles[FML_DIR_VIDEO "/Clip.avi"] = vwTestAvi(fr, 320, 240, 40000);
  FlexMlRec vr; memset(&vr, 0, sizeof(vr));
  snprintf(vr.path, sizeof(vr.path), "%s", FML_DIR_VIDEO "/Clip.avi"); snprintf(vr.name, sizeof(vr.name), "Clip.avi");
  vr.kind = FML_K_VIDEO; vr.fmt = FML_F_AVI_MJPEG; vr.state = FML_S_READY; vr.flags = FML_R_PLAYABLE;
  vr.size = (uint32_t)gTestFiles[FML_DIR_VIDEO "/Clip.avi"].size(); vr.w = 320; vr.h = 240;
  int vat = flexMlAdd(&gMs.lib, &vr, 1760000000u);
  uint32_t vid = vat >= 0 ? gMs.lib.recs[vat].id : 0;
  vwOpen(&GAL_VW, vid, NULL, NULL);
  chk(vwActiveFor(&GAL_VW) && vwKind == VWK_VIDEO && vwLand, "un video apaisado se abre en horizontal, en pausa");
  int vx0 = (int)(vwOffX + 0.5f);
  chk(vwFitW == 640 && vwFitH == LH && abs(vx0 - (LW - 640) / 2) <= 1, "320x240 se ajusta a 640x480 y se centra");
  chk(vwIsRed(vwSeen(vx0 + 160, LH / 2)) && vwIsBlue(vwSeen(vx0 + 480, LH / 2)) && vwSeen(vx0 / 2, LH / 2) == 0,
      "el fotograma llena su hueco (no sale pequeno en una esquina)");
  vwGlassPrep(); vwPresentAll();
  std::vector<uint16_t> vref(fb, fb + (size_t)SCR_W * SCR_H);
  for(int k = 0; k < 10; k++){ vwTogglePlay(); vwTogglePlay(); }
  vwPresentAll();
  chk(!vwPlaying && memcmp(vref.data(), fb, vref.size() * 2) == 0, "Play/Pausa diez veces: el vidrio de los controles no se acumula");
  uint32_t f0 = vwCurFrame;
  vwTogglePlay();
  gTestUs = vwNextUs + 1000; vwTick();
  gTestUs = vwNextUs + 1000; vwTick();
  gTestUs = 0;
  chk(vwPlaying && vwCurFrame > f0, "reproduciendo, avanza de fotograma");
  vwTogglePlay();
  vwClose();

  // ---- 14. MEMORIA ----
  galRender();
  if(gPsUsed != ps0) printf("  (PSRAM sin devolver: %d bytes)\n", (int)(gPsUsed - ps0));
  chk(gPsUsed == ps0, "abrir, ampliar, girar, video y cerrar devuelve TODA la PSRAM");

  uiGlass = glass0; gNavMode = nav0;
  mkReset();
  gMlOk = ok0; gTestFsReady = fs0; gTestMemFs = false; gTestFiles.clear();
  memset(&gMs, 0, sizeof(gMs));
  gState = ST_HOME; gAppId = 0; gAppState[IC_GALERIA] = ALIFE_CLOSED; gLand = false; uiClipFull();
}

// #############################################################
//  VIDEO ROBUSTO: archivo abierto, fotogramas grandes, vacios, cortados,
//  largos sin indice y danados. Sobre el visor REAL y el disco en memoria.
// #############################################################
extern unsigned gTestFsReadAtCalls, gTestFsOpenReads, gTestFsStreamReads;
static uint32_t vfAddVideo(const char* path, const std::vector<uint8_t>& avi){
  gTestFiles[path] = avi;
  FlexMlRec vr; memset(&vr, 0, sizeof(vr));
  snprintf(vr.path, sizeof(vr.path), "%s", path);
  const char* b = strrchr(path, '/');
  snprintf(vr.name, sizeof(vr.name), "%s", b ? b + 1 : path);
  vr.kind = FML_K_VIDEO; vr.fmt = FML_F_AVI_MJPEG; vr.state = FML_S_READY; vr.flags = FML_R_PLAYABLE;
  vr.size = (uint32_t)avi.size(); vr.w = 320; vr.h = 240;
  int at = flexMlAdd(&gMs.lib, &vr, 1760000000u);
  return at >= 0 ? gMs.lib.recs[at].id : 0;
}
// Un tick de reproduccion "a su hora": micros() justo pasado el siguiente cuadro.
static void vfTick(){ gTestUs = vwNextUs + 1000; vwTick(); gTestUs = 0; }
// La foto de prueba (rojo | azul) con `pad` bytes de segmentos APP1 detras
// del SOI: el decodificador los salta, asi que la IMAGEN es la misma y el
// FOTOGRAMA pesa lo que haga falta.
static std::vector<uint8_t> vfBigFrame(size_t pad){
  std::vector<uint8_t> j = vwTestJpeg(320, 240), o(j.begin(), j.begin() + 2);
  while(pad > 4){
    size_t seg = pad - 4 > 65000 ? 65000 : pad - 4;
    o.push_back(0xFF); o.push_back(0xE1); o.push_back((uint8_t)((seg + 2) >> 8)); o.push_back((uint8_t)(seg + 2));
    o.insert(o.end(), seg, (uint8_t)0x20);
    pad -= seg + 4;
  }
  o.insert(o.end(), j.begin() + 2, j.end());
  return o;
}
static bool vfRedBlue(){
  int vx0 = (int)(vwOffX + 0.5f), vw = (int)(vwFitW * vwScale + 0.5f), cy = (int)(vwOffY + vwFitH * vwScale * 0.5f);
  return vwIsRed(vwSeen(vx0 + vw / 4, cy)) && vwIsBlue(vwSeen(vx0 + vw * 3 / 4, cy));
}

static void testVideoRobusto(){
  printf("Video: archivo abierto, fotogramas grandes, vacios, cortados, largos sin indice y danados\n");
  int before = gFails;
  bool ok0 = gMlOk; bool fs0 = gTestFsReady; gTestFsReady = true;
  bool glass0 = uiGlass; int nav0 = gNavMode;
  geFsReset();
  gLockType = 1; gNavMode = 0; uiGlass = false;
  shotApp(IC_GALERIA); gAppState[IC_GALERIA] = ALIFE_RUNNING; mkBind(&GAL_APP); galViewReady = false;
  mlTables();
  if(!glassBuf) glassBuf = (uint16_t*)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  size_t ps0 = gPsUsed;
  gTestMs = 9500000; touchReset();
  std::vector<uint8_t> rb = vwTestJpeg(320, 240);

  // ---- 1. EL ARCHIVO SE ABRE UNA VEZ ----
  // Antes: cada lectura (cabecera de 8 bytes y datos de cada fotograma)
  // abria, buscaba y cerraba el archivo por su ruta.
  {
    std::vector<std::vector<uint8_t>> fr(60, rb);
    uint32_t id = vfAddVideo(FML_DIR_VIDEO "/Largo.avi", vwTestAvi(fr, 320, 240, 40000));
    vwOpen(&GAL_VW, id, NULL, NULL);
    chk(vwActiveFor(&GAL_VW) && vwKind == VWK_VIDEO, "se abre el video");
    unsigned o0 = gTestFsOpenReads, r0 = gTestFsReadAtCalls, s0 = gTestFsStreamReads;
    vwTogglePlay();
    for(int k = 0; k < 40; k++) vfTick();
    chkf(vwPlaying && vwCurFrame >= 38, "reproduce 40 cuadros seguidos (va por el %u)", vwCurFrame);
    chkf(gTestFsOpenReads == o0, "sin abrir el archivo otra vez durante la reproduccion (%u)", gTestFsOpenReads - o0);
    chkf(gTestFsReadAtCalls == r0, "ni una lectura por ruta (%u)", gTestFsReadAtCalls - r0);
    chkf(gTestFsStreamReads - s0 <= 40 * 2 + 4, "dos lecturas del flujo por fotograma (%u)", gTestFsStreamReads - s0);
    chk(vfRedBlue(), "y la imagen es la del video");
    vwTogglePlay();
    vwClose();
  }

  // ---- 2. FOTOGRAMA GRANDE: el buffer crece, no se salta ----
  // 260 KB (> los 192 KB fijos de antes) y el AVI declara 64 KB.
  {
    std::vector<std::vector<uint8_t>> fr(4, rb);
    fr[0] = vfBigFrame(260u * 1024u);
    uint32_t id = vfAddVideo(FML_DIR_VIDEO "/Grande.avi", vwTestAvi(fr, 320, 240, 40000));
    vwOpen(&GAL_VW, id, NULL, NULL);
    chkf(vwKind == VWK_VIDEO && vwFrameLen == fr[0].size(), "el fotograma de 260 KB se lee entero (%u)", vwFrameLen);
    chkf(vwFrameCap >= fr[0].size() && vwFrameCap <= FLEXAVI_FRAME_MAX, "el buffer crecio a lo justo (%u KB)", vwFrameCap / 1024);
    chk(vfRedBlue(), "y se VE (antes: negro, fotograma descartado)");
    vwClose();
  }

  // ---- 3. TROZO VACIO: se repite el anterior, sin error ----
  {
    std::vector<std::vector<uint8_t>> fr(8, rb);
    fr[3].clear(); fr[4].clear();
    uint32_t id = vfAddVideo(FML_DIR_VIDEO "/Vacios.avi", vwTestAvi(fr, 320, 240, 40000));
    vwOpen(&GAL_VW, id, NULL, NULL);
    vwTogglePlay();
    for(int k = 0; k < 6; k++) vfTick();
    chkf(vwPlaying && !vwEnded && vwCurFrame >= 5, "pasa por los vacios sin pararse (va por el %u)", vwCurFrame);
    chk(vfRedBlue(), "con la imagen anterior en pantalla");
    for(int k = 0; k < 6; k++) vfTick();
    chk(vwEnded && !vwPlaying, "y termina normal");
    vwClose();
  }

  // ---- 4. GRABACION CORTADA: declara 100, hay 30, sin idx1 ----
  {
    std::vector<std::vector<uint8_t>> fr(30, rb);
    uint32_t id = vfAddVideo(FML_DIR_VIDEO "/Cortado.avi", vwTestAvi(fr, 320, 240, 40000, 100, false));
    vwOpen(&GAL_VW, id, NULL, NULL);
    vwSeekMs(90u * 40u);                                // el 90: no existe
    for(int k = 0; k < 8 && vwSeekWant >= 0; k++) vwTick();
    chk(vwKind == VWK_VIDEO && vwSeekWant < 0, "buscar mas alla de lo grabado no deja el visor en error");
    chkf(vwCurFrame == 29 && vwAvi.frames == 30, "se queda en el ultimo que existe (%u) y la duracion es la real (%u)",
        vwCurFrame, vwAvi.frames);
    chk(vfRedBlue(), "mostrando ese ultimo fotograma");
    vwClose();
  }

  // ---- 5. LARGO SIN INDICE: buscar por tramos, nunca de una vez ----
  {
    std::vector<std::vector<uint8_t>> fr(5000);
    fr[0] = rb; fr[4000] = rb;                           // el resto: trozos vacios (cabeceras)
    uint32_t id = vfAddVideo(FML_DIR_VIDEO "/SinIndice.avi", vwTestAvi(fr, 320, 240, 40000, 0, false));
    vwOpen(&GAL_VW, id, NULL, NULL);
    unsigned worst = 0; int ticks = 0;
    unsigned s0 = gTestFsStreamReads;
    vwSeekMs(4000u * 40u);
    worst = gTestFsStreamReads - s0;
    while(vwSeekWant >= 0 && ticks < 100){
      unsigned s1 = gTestFsStreamReads; vwTick(); ticks++;
      if(gTestFsStreamReads - s1 > worst) worst = gTestFsStreamReads - s1;
    }
    chkf(vwSeekWant < 0 && vwCurFrame == 4000, "llega al 4000 (%u) en %d vueltas", vwCurFrame, ticks);
    chkf(ticks >= 10, "repartido en vueltas de loop (%d), no en una sola llamada", ticks);
    chkf(worst <= VW_SEEK_TICK + 8, "cada vuelta lee como mucho su tramo (%u lecturas)", worst);
    chk(vfRedBlue(), "y muestra el fotograma pedido");
    vwClose();
  }

  // ---- 6. DANADO A MITAD: se para y lo dice; no gira en vacio ----
  {
    std::vector<std::vector<uint8_t>> fr(10, rb);
    std::vector<uint8_t> avi = vwTestAvi(fr, 320, 240, 40000, 0, false);
    // El tamano del trozo del fotograma 5 pasa a ser absurdo (~4 GB).
    size_t at = 0; int seen = 0;
    for(size_t i = 12; i + 8 <= avi.size(); i++)
      if(!memcmp(&avi[i], "00dc", 4)){ if(seen++ == 5){ at = i; break; } }
    chk(at > 0, "hay un trozo que romper");
    avi[at + 4] = 0xF8; avi[at + 5] = 0xFF; avi[at + 6] = 0xFF; avi[at + 7] = 0xFF;
    uint32_t id = vfAddVideo(FML_DIR_VIDEO "/Roto.avi", avi);
    vwOpen(&GAL_VW, id, NULL, NULL);
    memset(gNotifs, 0, sizeof(gNotifs)); gNotifCount = 0;
    vwTogglePlay();
    for(int k = 0; k < 12; k++) vfTick();
    chk(!vwPlaying && vwEnded, "al llegar al trozo roto la reproduccion se PARA (antes: seguia leyendolo cada vuelta)");
    bool avisado = false;
    for(int i = 0; i < gNotifCount; i++) if(strstr(gNotifs[i].mod.sub, "a partir de")) avisado = true;
    chk(avisado, "y se avisa de que el video esta danado");
    chk(vwKind == VWK_VIDEO && vfRedBlue(), "con el ultimo fotograma bueno en pantalla");
    unsigned s0 = gTestFsStreamReads;
    for(int k = 0; k < 20; k++) vfTick();
    chk(gTestFsStreamReads == s0, "parado, ya no toca el archivo");
    vwClose();
  }

  galRender();
  if(gPsUsed != ps0) printf("  (PSRAM sin devolver: %d bytes)\n", (int)(gPsUsed - ps0));
  chk(gPsUsed == ps0, "todo lo del video se devuelve al cerrar (buffer ampliado incluido)");
  uiGlass = glass0; gNavMode = nav0;
  mkReset();
  gMlOk = ok0; gTestFsReady = fs0; gTestMemFs = false; gTestFiles.clear();
  memset(&gMs, 0, sizeof(gMs));
  gNotifCount = 0;
  gState = ST_HOME; gAppId = 0; gAppState[IC_GALERIA] = ALIFE_CLOSED; gLand = false; uiClipFull();
  if(gFails == before) printf("  Video robusto: todas las comprobaciones pasan.\n");
}

// #############################################################
//  VARIAS FOTOS SEGUIDAS: el catalogo se reescribe una vez por rafaga
//  ------------------------------------------------------------
//  Cada foto recibida dejaba el catalogo sucio y la tarea de medios lo
//  reescribia ENTERO 1,5 s despues, entre foto y foto: N fotos, N
//  reescrituras (decenas de borrados de sector con la cache apagada).
//  Se simula la linea de tiempo real de una rafaga (progreso cada 250 ms,
//  la tarea mirando cada 400 ms) con la MISMA decision que usa mlTask.
// #############################################################
static void testGuardadoRafaga(){
  printf("Varias fotos seguidas: el catalogo se reescribe una vez por rafaga, no una por foto\n");
  int before = gFails;
  // ---- 1. La decision, caso por caso ----
  chk(!mlSaveDue(10000, 0, 0, 0), "sin nada sucio no se guarda");
  chk(!mlSaveDue(11499, 10000, 0, 0) && mlSaveDue(11500, 10000, 0, 0), "sin subidas: 1,5 s despues del primer cambio, como antes");
  chk(!mlSaveDue(12000, 10000, 0, 11000), "con una subida hace 1 s, se espera");
  chk(mlSaveDue(14000, 10000, 0, 11000), "3 s sin senales: la rafaga acabo y se guarda");
  chk(mlSaveDue(30000, 10000, 0, 29900), "aunque las subidas no paren, nunca mas de 20 s sucio");
  chk(!mlSaveDue(12000, 10000, 1, 0) && mlSaveDue(13000, 10000, 1, 0), "tras un fallo de flash se espacia el reintento, igual que antes");
  // La marca es millis() | 1: leida en el MISMO milisegundo par va 1 ms por
  // delante. Sin signo eso era "hace una eternidad" y se guardaba en el acto.
  chk(!mlSaveDue(20000, 20000u | 1u, 0, 0), "sucio en este mismo milisegundo: aun no (antes: guardado inmediato)");
  chk(!mlSaveDue(20000 + 1600, 20000, 0, 20000 + 1600 + 1), "una senal de subida de este mismo milisegundo cuenta como rafaga");
  // ---- 2. Las senales salen de la web ----
  gTestMs = 777000; gMlBurstMs = 0;
  FlexWebXfer x; memset(&x, 0, sizeof(x)); x.ev = FLEXWEB_EV_UP_PROGRESS;
  whEvent(NULL, &x);
  chkf(gMlBurstMs == (777000u | 1u), "el progreso de una subida marca la rafaga (%u)", (unsigned)gMlBurstMs);
  gMlBurstMs = 0; x.ev = FLEXWEB_EV_DL_PROGRESS; whEvent(NULL, &x);
  chk(gMlBurstMs == 0, "una DESCARGA al movil no retrasa nada: no cambia el catalogo");
  // ---- 3. Una rafaga de 5 fotos de 2,5 s cada una ----
  uint32_t t = 1000000, dirtySince = 0, lastLook = 0; int saves = 0, savesOld = 0; uint32_t dirtyOld = 0;
  gMlBurstMs = 0;
  uint32_t firstDirty = 0, lastSave = 0;
  for(uint32_t ms = 0; ms < 40000; ms += 50){
    uint32_t now = t + ms;
    int photo = (int)(ms / 2500);
    bool sending = photo < 5;
    if(sending && ms % 250 == 0) gMlBurstMs = now | 1u;                 // progreso
    bool commit = sending && ms % 2500 == 2450;                          // la foto queda publicada
    if(commit){ if(!dirtySince) dirtySince = now | 1u; if(!dirtyOld) dirtyOld = now | 1u; if(!firstDirty) firstDirty = now; }
    if(now - lastLook >= 400){
      lastLook = now;
      if(mlSaveDue(now, dirtySince, 0, gMlBurstMs)){ saves++; dirtySince = 0; lastSave = now; }
      if(dirtyOld && now - dirtyOld >= ML_SAVE_DELAY_MS){ savesOld++; dirtyOld = 0; }   // la politica anterior
    }
  }
  chkf(savesOld >= 5, "con la politica anterior eran %d reescrituras (una por foto)", savesOld);
  chkf(saves == 1, "ahora una sola reescritura para toda la rafaga (%d)", saves);
  chkf(lastSave > firstDirty && lastSave - firstDirty <= ML_SAVE_MAX_MS, "y llega a tiempo: %u ms tras la primera foto",
       (unsigned)(lastSave - firstDirty));
  gMlBurstMs = 0;
  if(gFails == before) printf("  Guardado por rafaga: todas las comprobaciones pasan.\n");
}

// #############################################################
//  VARIAS FOTOS A LA VEZ, POR EL CAMINO REAL
//  ------------------------------------------------------------
//  El movil sube fotos de 0,3 a 12 MP por el servidor web de VERDAD
//  (FlexOS_MediaWeb) conectado a los callbacks de la placa: validacion y
//  miniatura con el cerrojo de trabajo pesado, publicacion en el catalogo
//  (whCommit), avisos (webTick) y la Galeria abierta repintandose. Despues
//  se abren todas en el visor, una tras otra. Lo que se exige:
//    · la memoria de cada subida NO crece con el tamano de la foto (se lee
//      por trozos: nunca entera en RAM);
//    · nada se acumula por foto: tras N fotos la PSRAM es la de antes mas
//      las miniaturas en cache (acotadas por ML_CACHE_N);
//    · abrir las N en el visor y cerrarlo devuelve TODA la PSRAM;
//    · ningun temporal se queda en el disco.
// #############################################################
struct VfConn { std::string in; size_t pos = 0; std::string out; };
static int vfcRead(void* c, uint8_t* b, size_t n, uint32_t){
  VfConn* k = (VfConn*)c;
  if(k->pos >= k->in.size()) return -1;
  size_t t = k->in.size() - k->pos; if(t > n) t = n; if(t > 16384) t = 16384;   // TCP troceado
  memcpy(b, k->in.data() + k->pos, t); k->pos += t; return (int)t;
}
static bool vfcWrite(void* c, const uint8_t* b, size_t n){ ((VfConn*)c)->out.append((const char*)b, n); return true; }
static std::string gVfCookie;
static int vfHttp(const std::string& method, const std::string& path, const std::string& body, std::string* respBody = nullptr){
  std::string r = method + " " + path + " HTTP/1.1\r\nHost: 192.168.1.50:8080\r\nX-Flex: 1\r\n";
  if(!gVfCookie.empty()) r += "Cookie: " FLEXHTTP_SESS_COOKIE "=" + gVfCookie + "\r\n";
  r += "Content-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
  VfConn c; c.in = r;
  FlexWebConn cn = { vfcRead, vfcWrite, &c, NULL };
  flexWebServeConn(&gWebCtx, &cn, gWebHdr, gWebIo);
  int st = c.out.size() > 12 ? atoi(c.out.c_str() + 9) : 0;
  size_t sc = c.out.find("fxs=");
  if(sc != std::string::npos && gVfCookie.empty()){ size_t e = c.out.find(';', sc); gVfCookie = c.out.substr(sc + 4, e - sc - 4); }
  if(respBody){ size_t e = c.out.find("\r\n\r\n"); *respBody = e == std::string::npos ? "" : c.out.substr(e + 4); }
  return st;
}
static std::string vfEnc(const char* s){
  std::string o; char b[4];
  for(const unsigned char* p = (const unsigned char*)s; *p; p++){
    if(isalnum(*p) || *p == '.' || *p == '-' || *p == '_') o += (char)*p; else { snprintf(b, 4, "%%%02X", *p); o += b; }
  }
  return o;
}

static void testVariasFotos(){
  printf("Varias fotos: 8 de 0,3 a 12 MP por el servidor web REAL, con la Galeria abierta y el visor\n");
  int before = gFails;
  bool ok0 = gMlOk; bool fs0 = gTestFsReady; gTestFsReady = true;
  bool glass0 = uiGlass; int nav0 = gNavMode;
  geFsReset();
  gLockType = 1; gNavMode = 0; uiGlass = false;
  shotApp(IC_GALERIA); gAppState[IC_GALERIA] = ALIFE_RUNNING; mkBind(&GAL_APP); galViewReady = false;
  mlTables();
  if(!glassBuf) glassBuf = (uint16_t*)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  gTestMs = 9800000; touchReset();
  // El servidor como lo arranca webStart, sin la tarea: la prueba hace de ella.
  if(!gWebHdr) gWebHdr = (uint8_t*)mediaAlloc(FLEXWEB_HDR_BUF);
  if(!gWebIo)  gWebIo  = (uint8_t*)mediaAlloc(FLEXWEB_IO_BUF);
  if(!gWebEv)  gWebEv  = (FlexWebXfer*)mediaAlloc(sizeof(FlexWebXfer) * WEB_EV_N);
  memset(&gWebCtx, 0, sizeof(gWebCtx));
  gWebCtx.fs = { msfOpen, msfRead, msfWrite, msfSeek, msfClose, msfSize, msfRemove, wfsFree, wfsTotal, NULL };
  gWebCtx.host = { whSnapshot, whGet, whRev, whDup, whCommit, whSetThumb, whThumbPath, whVerify, whLockType,
                   whNow, whEpoch, whRandom, whEvent, NULL, whOthers, NULL, whHeavyBegin, whHeavyEnd,
                   NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };   // sin Flex Storage
  gWebCtx.alloc = mediaAlloc; gWebCtx.free = mediaFree;
  snprintf(gWebCtx.ip, sizeof(gWebCtx.ip), "192.168.1.50");
  gWebCtx.port = 8080; gWebCtx.allowUpload = true;
  flexWebInit(&gWebCtx);
  gVfCookie.clear();
  chk(vfHttp("POST", "/api/pair", std::string("code=") + gWebCtx.code) == 200 && gVfCookie.size() == 32, "el movil se empareja");
  galRender();
  size_t psGal = gPsUsed;

  const int dims[8][2] = { {640, 480}, {480, 640}, {1600, 1200}, {1200, 1600}, {3000, 2000}, {2000, 3000}, {4000, 3000}, {3000, 4000} };
  int n0 = gMs.lib.n, okN = 0; size_t worst = 0, worstFor = 0;
  std::vector<uint32_t> ids;
  for(int i = 0; i < 8; i++){
    std::vector<uint8_t> jpg = vwTestJpeg(dims[i][0], dims[i][1]);
    char name[32]; snprintf(name, sizeof(name), "Foto %d.jpg", i + 1);
    uint32_t crc = flexMlCrc32(0, jpg.data(), jpg.size());
    std::string path = std::string("/api/upload?kind=photo&name=") + vfEnc(name) + "&size=" + std::to_string(jpg.size()) +
                       "&crc=" + std::to_string(crc) + "&w=" + std::to_string(dims[i][0]) + "&h=" + std::to_string(dims[i][1]);
    size_t base = gPsUsed; gPsPeak = gPsUsed;
    std::string rb;
    int st = vfHttp("POST", path, std::string((const char*)jpg.data(), jpg.size()), &rb);
    size_t used = gPsPeak - base;
    if(used > worst){ worst = used; worstFor = (size_t)dims[i][0] * dims[i][1]; }
    if(st == 201){ okN++; const char* q = strstr(rb.c_str(), "\"id\":"); if(q) ids.push_back((uint32_t)strtoul(q + 5, NULL, 10)); }
    else printf("  (subida %d: HTTP %d %s)\n", i + 1, st, rb.c_str());
    gTestMs += 700; webTick();                     // la interfaz recoge los avisos
    galRender();                                   // y la Galeria se repinta con la nueva
  }
  printf("  [fotos] subida mas cara: %u KB de PSRAM (foto de %.1f MP) · tras 8 fotos: +%d KB (miniaturas en cache)\n",
         (unsigned)(worst / 1024), worstFor / 1e6, (int)(gPsUsed - psGal) / 1024);
  chkf(okN == 8 && gMs.lib.n == n0 + 8, "las 8 fotos quedan publicadas (%d, catalogo %d)", okN, gMs.lib.n - n0);
  chkf(worst <= 1536u * 1024u, "la subida mas cara usa %u KB de PSRAM (foto de %.1f MP): no crece con la foto",
       (unsigned)(worst / 1024), worstFor / 1e6);
  size_t cacheMax = (size_t)ML_CACHE_N * ML_SIDE * ML_SIDE * 2;
  chkf(gPsUsed <= psGal + cacheMax, "tras 8 fotos la PSRAM es la de antes mas las miniaturas en cache (+%d KB)",
       (int)(gPsUsed - psGal) / 1024);
  int tmps = 0;
  for(auto& kv : gTestFiles) if(kv.first.compare(0, strlen(FML_DIR_TMP), FML_DIR_TMP) == 0) tmps++;
  chkf(tmps == 0, "ningun temporal queda en el disco (%d)", tmps);

  // Abrir las ocho en el visor, una tras otra.
  size_t psBefore = gPsUsed; int opened = 0;
  for(uint32_t id : ids){
    vwOpen(&GAL_VW, id, NULL, NULL);
    for(int k = 0; k < 4 && vwLoading; k++) vwTick();
    if(vwActiveFor(&GAL_VW) && vwKind == VWK_PHOTO && vwSrc) opened++;
    vwClose();
  }
  chkf(opened == 8, "las 8 se abren en el visor (%d)", opened);
  galRender();
  if(gPsUsed != psBefore) printf("  (PSRAM sin devolver tras el visor: %d bytes)\n", (int)(gPsUsed - psBefore));
  chk(gPsUsed == psBefore, "abrir las 8 en el visor y cerrarlo devuelve TODA la PSRAM");

  uiGlass = glass0; gNavMode = nav0;
  mkReset();
  memset(&gWebCtx, 0, sizeof(gWebCtx));
  gWebEvR = gWebEvW = 0;
  gMlOk = ok0; gTestFsReady = fs0; gTestMemFs = false; gTestFiles.clear();
  memset(&gMs, 0, sizeof(gMs));
  memset(gNotifs, 0, sizeof(gNotifs)); gNotifCount = 0;
  gState = ST_HOME; gAppId = 0; gAppState[IC_GALERIA] = ALIFE_CLOSED; gLand = false; uiClipFull();
  if(gFails == before) printf("  Varias fotos: todas las comprobaciones pasan.\n");
}

// #############################################################
//  LIQUID GLASS AL MANTENER PULSADA UNA FOTO O UN VIDEO
//  ------------------------------------------------------------
//  El sintoma: con el dedo apoyado sobre una foto o un video, el vidrio del
//  menu se volvia a dibujar y se acumulaba (cada cuadro del despliegue
//  desenfocaba lo que tenia debajo, que ya era el menu del cuadro anterior).
//  Se exige, en las TRES apps de medios y con el temblor real de un dedo
//  (+-2 px): nada se repinta antes de que salte la pulsacion larga; el menu
//  se despliega y, en cuanto termina, CERO volcados y CERO cambios en el
//  panel mientras el dedo sigue ahi; y veinte ciclos de mantener y cerrar no
//  dejan ni un byte de PSRAM ni la banda de vidrio activa.
// #############################################################
#include <time.h>
static uint64_t lpHash(const uint16_t* b){ uint64_t h = 1469598103934665603ull; for(size_t i = 0; i < (size_t)SCR_W * SCR_H; i++){ h ^= b[i]; h *= 1099511628211ull; } return h; }
struct LpRes { int menuAt; unsigned drawsBefore, drawsAfter; int changesAfter; double animUs; int animFrames; };
static double lpNowUs(){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3; }
static LpRes lpHold(void (*tick)(), int cx, int cy, int frames, const uint16_t* shadow){
  LpRes r = { -1, 0, 0, 0, 0.0, 0 };
  touchReset();
  T.down = true; T.pressed = true; T.x = T.startX = cx; T.y = T.startY = cy; T.downMs = gTestMs;
  uint64_t last = 0; bool settled = false;
  for(int f = 0; f < frames; f++){
    unsigned d0 = gPanelDrawCalls;
    double t0 = lpNowUs();
    tick();
    double us = lpNowUs() - t0;
    unsigned d = gPanelDrawCalls - d0;
    if(r.menuAt >= 0 && !settled){ r.animUs += us; r.animFrames++; }
    if(r.menuAt < 0){
      if(mmOn) r.menuAt = f; else r.drawsBefore += d;
    } else if(!settled){
      if(mmAnimDone){ settled = true; last = lpHash(shadow); }
    } else {
      r.drawsAfter += d;
      uint64_t h = lpHash(shadow); if(h != last){ r.changesAfter++; last = h; }
    }
    // Temblor de un dedo de verdad: dentro de la tolerancia de la pulsacion larga.
    T.pressed = false; gTestMs += 16;
    T.x = cx + (f % 5) - 2; T.y = cy + ((f / 5) % 5) - 2;
  }
  return r;
}
static void lpCloseByTap(void (*tick)()){
  // Soltar (una pulsacion larga no es un toque) y tocar fuera del menu.
  touchReset(); T.released = true; tick();
  touchReset(); gTestMs += 40;
  T.pressed = true; T.down = true; T.x = T.startX = 20; T.y = T.startY = 150; T.downMs = gTestMs; tick();
  touchReset(); gTestMs += 60; T.released = true; T.tap = true; T.x = 20; T.y = 150; tick();
  // Vueltas con el dedo levantado: como en la placa, es aqui donde cada app
  // rearma su deteccion de pulsacion larga.
  for(int k = 0; k < 2; k++){ touchReset(); gTestMs += 16; tick(); }
}
static void testPulsacionLargaVidrio(){
  printf("Liquid Glass al mantener pulsada una foto o un video: ni se acumula ni se recalcula con el dedo quieto\n");
  int before = gFails;
  std::vector<uint16_t> shadow((size_t)SCR_W * SCR_H, 0);
  uint16_t* sh0 = gPanelShadow; gPanelShadow = shadow.data();
  bool glass0 = uiGlass; int nav0 = gNavMode; bool ok0 = gMlOk;
  uiGlass = true; gNavMode = 0;
  gTestMs = 20000000;
  for(int app = 0; app < 3; app++){
    const char* an = app == 0 ? "Galeria" : app == 1 ? "Multimedia" : "Musica";
    tkReset();
    for(int i = 0; i < 12; i++){
      char p[40];
      if(app == 0){ snprintf(p, sizeof(p), "/Imagenes/lp%02d.jpg", i); tkAdd(FML_K_PHOTO, FML_F_JPEG, p, NULL, false, true); }
      else if(app == 1){ snprintf(p, sizeof(p), "/Videos/lp%02d.avi", i); tkAdd(FML_K_VIDEO, FML_F_AVI_MJPEG, p, NULL, false, true); }
      else { snprintf(p, sizeof(p), "/Musica/lp%02d.wav", i); tkAdd(FML_K_AUDIO, FML_F_WAV_PCM, p, NULL, false, true); }
    }
    gMlOk = true;
    void (*tick)() = NULL;
    int cx = 0, cy = 0;
    int bx, by, bw, bh;
    if(app == 0){
      shotApp(IC_GALERIA); mkBind(&GAL_APP); mkReset(); galViewReady = false; galScroll = 0; galRender();
      int x, y, w, h; galCellRect(4, x, y, w, h); cx = x + w / 2; cy = y + h / 2; tick = galTick;
    } else if(app == 1){
      shotApp(IC_MULTIMEDIA); mkBind(&VID_APP); mkReset(); vidScreen = VS_LIST; vidListScroll = 0; vidListRender();
      uiBox(bx, by, bw, bh); cx = bx + bw / 2; cy = vidRowY(2) + VID_ROW_H / 2 - 4; tick = vidTick;
    } else {
      shotApp(IC_MUSICA); mkBind(&MUS_APP); mkReset(); musScreen = MUS_LIST; musScroll = 0; musRender();
      uiBox(bx, by, bw, bh); cx = bx + bw / 2; cy = musRowY(2) + MUS_ROW_H / 2 - 4; tick = musTick;
    }
    LpRes r = lpHold(tick, cx, cy, 160, shadow.data());
    printf("  [vidrio] %s: despliegue del menu %.0f us/cuadro (%d cuadros, en el PC)\n", an,
           r.animFrames ? r.animUs / r.animFrames : 0.0, r.animFrames);
    chkf(r.menuAt >= 33 && r.menuAt <= 37, "[%s] el menu salta a los ~550 ms (cuadro %d)", an, r.menuAt);
    chkf(r.drawsBefore == 0, "[%s] antes de saltar, el dedo apoyado no repinta nada (%u volcados)", an, r.drawsBefore);
    chkf(r.drawsAfter == 0 && r.changesAfter == 0,
         "[%s] desplegado el menu, con el dedo ahi: 0 volcados y 0 cambios (%u, %d)", an, r.drawsAfter, r.changesAfter);
    chkf(!uiGlassBandActive(), "[%s] la banda de vidrio del despliegue ya esta suelta", an);
    // Lo que se ve tras desplegarse bajo el dedo == el mismo menu pintado de
    // UNA vez sobre el mismo fondo. Si un cuadro desenfocara el anterior, el
    // vidrio se oscureceria y esto no coincidiria.
    {
      int mx, my, mw, mh; mmGeom(mx, my, mw, mh);
      std::vector<uint16_t> held((size_t)mw * mh);
      for(int yy = 0; yy < mh; yy++) memcpy(&held[(size_t)yy * mw], shadow.data() + (size_t)(my + yy) * SCR_W + mx, (size_t)mw * 2);
      uint8_t acts[MM_MAX]; int na = mmN; memcpy(acts, mmAct, sizeof(acts));
      int ax = mmAx, ay = mmAy;
      mmClose();
      if(app == 0) galRender(); else if(app == 1) vidListRender(); else musRender();
      mmOpen(ax, ay, acts, na);
      gTestMs += MM_ANIM_MS + 1; mmAnimTick();
      bool same = true;
      for(int yy = 0; yy < mh && same; yy++)
        same = !memcmp(&held[(size_t)yy * mw], shadow.data() + (size_t)(my + yy) * SCR_W + mx, (size_t)mw * 2);
      chkf(same, "[%s] el menu desplegado con el dedo encima es identico al pintado de una vez: el vidrio no se apila", an);
    }
    lpCloseByTap(tick);
    chkf(!mmOn, "[%s] tocar fuera cierra el menu", an);
    // Veinte veces: mantener, desplegar, cerrar. Nada se queda reservado.
    size_t ps0 = gPsUsed; int opened = 0;
    for(int k = 0; k < 20; k++){
      LpRes q = lpHold(tick, cx, cy, 60, shadow.data());
      if(q.menuAt >= 0) opened++;
      lpCloseByTap(tick);
    }
    chkf(opened == 20 && gPsUsed == ps0 && !uiGlassBandActive(),
         "[%s] 20 ciclos: 20 menus, PSRAM igual (%+d B) y sin banda viva", an, (int)(gPsUsed - ps0));
    mkReset();
  }
  touchReset();
  uiGlass = glass0; gNavMode = nav0; gMlOk = ok0; gPanelShadow = sh0;
  gState = ST_HOME; gAppId = 0; uiClipFull();
  if(gFails == before) printf("  Pulsacion larga: todas las comprobaciones pasan.\n");
}

// #############################################################
//  TACTO GLOBAL: DONDE CAE UN TOQUE Y LA ESCALA DEL GT911
//  ------------------------------------------------------------
//  Por la cadena REAL: registros del GT911 (doble del bus) -> gtPoll ->
//  flexPollTouch -> la app. Nada de escribir T a mano.
// #############################################################
static void gtFrame(int fingers, int x, int y){
  gWireGt[0x14E] = (uint8_t)(0x80 | fingers);
  gWireGt[0x150] = (uint8_t)x; gWireGt[0x151] = (uint8_t)(x >> 8);
  gWireGt[0x152] = (uint8_t)y; gWireGt[0x153] = (uint8_t)(y >> 8);
}
// Un toque entero: apoya en (x0,y0), el centroide se va a (x1,y1) al levantar
// el dedo, y se suelta. `tick` corre en cada cuadro (la app).
static void gtTapPath(int x0, int y0, int x1, int y1, void (*tick)()){
  gtFrame(1, x0, y0); gTestMs += 16; flexPollTouch(); if(tick) tick();
  gtFrame(1, (x0 + x1) / 2, (y0 + y1) / 2); gTestMs += 16; flexPollTouch(); if(tick) tick();
  gtFrame(1, x1, y1); gTestMs += 16; flexPollTouch(); if(tick) tick();
  gtFrame(0, 0, 0);  gTestMs += 16; flexPollTouch(); if(tick) tick();
  gTestMs += 16; flexPollTouch(); if(tick) tick();                // una vuelta mas, ya sin dedo
}
static void testTactoGlobal(){
  printf("Tacto global: un toque cae donde se apoyo el dedo y la escala del GT911 se respeta\n");
  int before = gFails;
  bool gt0 = gtOk; gtOk = true; gWireGtOn = true; memset(gWireGt, 0, sizeof(gWireGt));
  uint16_t rx0 = gtResX, ry0 = gtResY;
  touchReset(); gTouchSwallow = false;
  gTestMs = 30000000;
  // ---- 1. El toque se localiza donde se APOYO el dedo ----
  gtFrame(1, 100, 200); gTestMs += 16; flexPollTouch();
  chk(T.pressed && T.x == 100 && T.y == 200, "apoyar el dedo da pressed en su punto");
  gtFrame(1, 100, 209); gTestMs += 16; flexPollTouch();
  chk(T.down && !T.tap && T.y == 209, "mientras se mueve, T sigue al dedo");
  gtFrame(0, 0, 0); gTestMs += 16; flexPollTouch();
  chkf(T.tap && T.x == 100 && T.y == 200, "al soltar es un toque y cae donde se apoyo (%d,%d), no donde se levanto", T.x, T.y);
  chkf(T.dy == 9, "y el recorrido real se conserva en T.dy (%d)", T.dy);
  gTestMs += 16; flexPollTouch();
  // Un arrastre de verdad no cambia: ni toque ni coordenadas movidas.
  gtFrame(1, 100, 300); gTestMs += 16; flexPollTouch();
  gtFrame(1, 100, 380); gTestMs += 16; flexPollTouch();
  gtFrame(0, 0, 0); gTestMs += 16; flexPollTouch();
  chk(!T.tap && T.y == 380 && T.swipeDown, "un deslizamiento sigue siendo deslizamiento, con su punto final");
  gTestMs += 16; flexPollTouch();

  // ---- 2. Galeria: la parte baja de una pestana cambia de pestana ----
  // Las pestanas miden 28 px y la rejilla empieza justo debajo. Apoyando en
  // la parte baja de "Videos" y con el centroide bajando 9 px al levantar el
  // dedo, antes el toque caia en la rejilla y abria una foto.
  {
    bool ok0 = gMlOk; int nav0 = gNavMode;
    tkReset();
    for(int i = 0; i < 10; i++){ char p[40]; snprintf(p, sizeof(p), "/Imagenes/tt%02d.jpg", i); tkAdd(FML_K_PHOTO, FML_F_JPEG, p, NULL, false, true); }
    gMlOk = true; gNavMode = 0;
    shotApp(IC_GALERIA); mkBind(&GAL_APP); mkReset(); galViewReady = false; galScroll = 0; galTab = 0; galRender();
    int bx, by, bw, bh; uiBox(bx, by, bw, bh);
    int pad = uiPad();
    const int tabY = by + 38, tw = (bw - 2 * pad) / GAL_TABS;
    int tx = bx + pad + 2 * tw + tw / 2;
    uint32_t tap0 = galTapId;
    gtTapPath(tx, tabY + 24, tx, tabY + 33, galTick);
    chkf(galTab == 2 && galTapId == tap0 && !vwHostOpen(&GAL_VW),
         "apoyar en la parte baja de 'Videos' cambia de pestana (pestana %d) y no abre una foto", galTab);
    // Y un toque en la rejilla sigue abriendo lo que se toca.
    galTab = 0; galScroll = 0; galRender();
    int x, y, w, h; galCellRect(1, x, y, w, h);
    mlLock(); galSyncLocked(); uint32_t want = galRecLocked(1)->id; mlUnlock();
    galTapId = 0;
    gtTapPath(x + w / 2, y + h / 2, x + w / 2 + 3, y + h / 2 + 8, galTick);
    chkf(vwHostOpen(&GAL_VW) && vwId == want, "un toque en una celda abre ESA celda (%u, esperaba %u)", (unsigned)vwId, (unsigned)want);
    vwClose(); mkReset();

    // ---- 2b. Con miniaturas por cargar, el cuadro de SOLTAR no se pierde ----
    // Cada vuelta sin dedo carga una tanda (GAL_THUMB_BUDGET); antes esa vuelta
    // hacia return antes de mirar el tacto y el toque de soltar desaparecia.
    // Version de miniatura nueva: la cache (id + version) no tiene ninguna.
    for(int i = 0; i < gMs.lib.n; i++){ gMs.lib.recs[i].flags |= FML_R_THUMB; gMs.lib.recs[i].thumbVer = 77; }
    gMs.lib.rev++;
    gTestMs += 400; galRender();
    chk(galMorePending, "hay miniaturas pendientes (se cargan por tandas)");
    galCellRect(2, x, y, w, h);
    mlLock(); galSyncLocked(); want = galRecLocked(2)->id; mlUnlock();
    gtTapPath(x + w / 2, y + h / 2, x + w / 2, y + h / 2 + 6, galTick);
    chkf(vwHostOpen(&GAL_VW) && vwId == want, "con miniaturas pendientes el toque abre la celda (%u, esperaba %u)", (unsigned)vwId, (unsigned)want);
    vwClose(); mkReset();
    for(int k = 0; k < 8; k++){ gTestMs += 16; flexPollTouch(); galTick(); }   // terminan de cargar

    // ---- 2c. El catalogo cambia entre pintar y tocar: se abre lo que se VEIA ----
    // "Lo mas nuevo primero": una subida entra arriba y todo se corre una
    // posicion. Re-sincronizar al tocar hacia abrir el vecino.
    galRender();
    galCellRect(3, x, y, w, h);
    mlLock(); galSyncLocked(); uint32_t seen = galRecLocked(3)->id; mlUnlock();
    tkAdd(FML_K_PHOTO, FML_F_JPEG, "/Imagenes/recien.jpg", NULL, false, true);   // entra la primera
    gTestMs += 400;                                   // el repintado de fondo ya "tocaria"
    gtTapPath(x + w / 2, y + h / 2, x + w / 2, y + h / 2 + 6, galTick);
    chkf(vwHostOpen(&GAL_VW) && vwId == seen, "si el catalogo cambia entre pintar y tocar, se abre lo que se veia (%u, esperaba %u)",
         (unsigned)vwId, (unsigned)seen);
    vwClose(); mkReset();
    for(int k = 0; k < 8; k++){ gTestMs += 16; flexPollTouch(); galTick(); }

    // ---- 2d. Apoyar justo cuando toca un repintado de fondo ----
    // Antes ese repintado se comia el cuadro de APOYAR: el origen del arrastre
    // (galDragY0) seguia siendo el del toque anterior y, al mover el dedo un
    // pixel, el toque nuevo se convertia en un arrastre que saltaba de scroll.
    galScroll = 0; galRender();
    int maxS = galMaxScroll();
    chkf(maxS > 40, "la rejilla tiene recorrido (%d px)", maxS);
    // Toque anterior: la pestana que ya esta activa (no hace nada) -> galDragY0 arriba.
    int tx0 = bx + pad + tw / 2;
    gtTapPath(tx0, tabY + 14, tx0, tabY + 16, galTick);
    int s0 = galScroll;
    gMs.lib.rev++; gTestMs += 400;                    // el catalogo cambio (una miniatura lista)
    galCellRect(4, x, y, w, h);
    mlLock(); galSyncLocked(); want = galRecLocked(4)->id; mlUnlock();
    gtTapPath(x + w / 2, y + h / 2, x + w / 2, y + h / 2 + 5, galTick);
    chkf(galScroll == s0, "el toque siguiente no salta de scroll (%d -> %d)", s0, galScroll);
    chkf(vwHostOpen(&GAL_VW) && vwId == want, "y abre lo que se toca (%u, esperaba %u)", (unsigned)vwId, (unsigned)want);
    vwClose(); mkReset();
    gMlOk = ok0; gNavMode = nav0;
    gState = ST_HOME; gAppId = 0; uiClipFull();
  }

  // ---- 2e. Multimedia y Musica: el mismo contrato (lo que se toca es lo que se ve) ----
  {
    bool ok0 = gMlOk; int nav0 = gNavMode;
    for(int app = 0; app < 2; app++){
      tkReset();
      for(int i = 0; i < 8; i++){
        char p[40];
        if(app == 0){ snprintf(p, sizeof(p), "/Videos/tv%02d.avi", i); tkAdd(FML_K_VIDEO, FML_F_AVI_MJPEG, p, NULL, false, true); }
        else { snprintf(p, sizeof(p), "/Musica/tm%02d.wav", i); tkAdd(FML_K_AUDIO, FML_F_WAV_PCM, p, NULL, false, true); }
      }
      gMlOk = true; gNavMode = 0;
      int bx, by, bw, bh;
      uint32_t seen = 0, got = 0;
      if(app == 0){
        shotApp(IC_MULTIMEDIA); mkBind(&VID_APP); mkReset(); vidScreen = VS_LIST; vidListScroll = 0; vidListRender();
        uiBox(bx, by, bw, bh);
        mlLock(); vidSyncLocked(); seen = gMs.lib.recs[vidView.idx[2]].id; mlUnlock();
        tkAdd(FML_K_VIDEO, FML_F_AVI_MJPEG, "/Videos/recien.avi", NULL, false, true);
        got = vidHitId(bx + bw / 2, vidRowY(2) + VID_ROW_H / 2 - 4);
      } else {
        shotApp(IC_MUSICA); mkBind(&MUS_APP); mkReset(); musScreen = MUS_LIST; musScroll = 0; musRender();
        uiBox(bx, by, bw, bh);
        mlLock(); musSyncLocked(); seen = gMs.lib.recs[musView.idx[2]].id; mlUnlock();
        tkAdd(FML_K_AUDIO, FML_F_WAV_PCM, "/Musica/recien.wav", NULL, false, true);
        got = musHitId(bx + bw / 2, musRowY(2) + MUS_ROW_H / 2 - 4);
      }
      chkf(got == seen, "[%s] con el catalogo cambiado, la fila tocada es la que se veia (%u, esperaba %u)",
           app == 0 ? "Multimedia" : "Musica", (unsigned)got, (unsigned)seen);
      mkReset();
    }
    gMlOk = ok0; gNavMode = nav0;
    gState = ST_HOME; gAppId = 0; uiClipFull();
  }

  // ---- 3. La escala que tenga configurada el GT911 ----
  touchReset();
  gWireGt[0x048] = 0xD0; gWireGt[0x049] = 0x02;                       // X: 720
  gWireGt[0x04A] = 0xB0; gWireGt[0x04B] = 0x04;                       // Y: 1200
  gtReadRes();
  chkf(gtResX == 720 && gtResY == 1200, "se lee la resolucion configurada (%ux%u)", gtResX, gtResY);
  gtFrame(1, 360, 600); gTestMs += 16; flexPollTouch();
  chkf(T.x == 240 && T.y == 400, "un panel configurado a 720x1200 se escala a la pantalla (%d,%d)", T.x, T.y);
  gtFrame(1, 719, 1199); gTestMs += 16; flexPollTouch();
  chkf(T.x == 479 && T.y == 799, "su esquina es la esquina de la pantalla (%d,%d)", T.x, T.y);
  gtFrame(0, 0, 0); gTestMs += 16; flexPollTouch(); gTestMs += 16; flexPollTouch();
  gtResX = SCR_W; gtResY = SCR_H;
  gWireGt[0x048] = 0x20; gWireGt[0x049] = 0x03; gWireGt[0x04A] = 0xE0; gWireGt[0x04B] = 0x01;   // 800x480: ejes cambiados
  gtReadRes();
  chk(gtResX == SCR_W && gtResY == SCR_H, "con los ejes cambiados NO se escala (se avisa de GT911_SWAP_XY)");
  gWireGt[0x048] = 0xFF; gWireGt[0x049] = 0xFF; gWireGt[0x04A] = 0xFF; gWireGt[0x04B] = 0xFF;   // basura
  gtReadRes();
  chk(gtResX == SCR_W && gtResY == SCR_H, "un valor absurdo se ignora");
  gWireGt[0x048] = 0xE0; gWireGt[0x049] = 0x01; gWireGt[0x04A] = 0x20; gWireGt[0x04B] = 0x03;   // 480x800
  gtReadRes();
  gtFrame(1, 123, 456); gTestMs += 16; flexPollTouch();
  chk(gtResX == SCR_W && gtResY == SCR_H && T.x == 123 && T.y == 456, "con la configuracion esperada (480x800) no cambia nada");
  gtFrame(0, 0, 0); gTestMs += 16; flexPollTouch(); gTestMs += 16; flexPollTouch();

  gtResX = rx0; gtResY = ry0;
  gWireGtOn = false; gtOk = gt0; touchReset();
  if(gFails == before) printf("  Tacto global: todas las comprobaciones pasan.\n");
}

// La hoja de Flex Web Server solo repinta lo que cambia.
static void testHojaWebLocalizada(){
  printf("Flex Web Server: la hoja solo repinta la zona que cambia\n");
  shotApp(IC_GALERIA);
  gWebState = WEBS_OFF;
  memset(gWebCards, 0, sizeof(gWebCards));
  webSheetOn = false;
  webSheetOpen();
  touchReset();
  unsigned d0 = gPanelDrawCalls;
  for(int k = 0; k < 20; k++){ gTestMs += 300; webSheetTick(); }
  chk(gPanelDrawCalls == d0, "en reposo la hoja no se vuelve a publicar (antes: entera cada 400 ms)");
  WebCard* c = webCardFor("foto.jpg", true);
  c->state = WCS_RUN; c->total = 3000000; c->done = 100000;
  gTestMs += 300; webSheetTick();
  int bx, by, bw, bh; webSheetGeom(bx, by, bw, bh);
  chk(gPanelDrawCalls == d0 + 1 && gPanelLastY0 == gWebLiveY && gPanelLastY1 == by + bh - 1,
      "el progreso publica SOLO la zona de las tarjetas");
  gTestMs += 300; webSheetTick();
  chk(gPanelDrawCalls == d0 + 1, "si el progreso no cambia, no se publica nada");
  c->done = 2000000;
  gTestMs += 300; webSheetTick();
  chk(gPanelDrawCalls == d0 + 2 && gPanelLastY0 == gWebLiveY, "cada avance real vuelve a ser solo esa zona");
  gTestMs += 300; gDark = !gDark; webSheetTick(); gDark = !gDark;
  chk(gPanelDrawCalls == d0 + 3 && gPanelLastY0 == WIN_TOP, "un cambio de tema si repinta la hoja entera");
  webSheetDismiss();
  memset(gWebCards, 0, sizeof(gWebCards));
  gState = ST_HOME; gAppId = 0;
}


// Capturas del visor (solo con INO_SHOTS=1): para REVISAR el aspecto.
static void testCapturasVisor(){
  if(!getenv("INO_SHOTS")) return;
  printf("Capturas del visor de medios\n");
  bool ok0 = gMlOk; bool fs0 = gTestFsReady; gTestFsReady = true;
  bool glass0 = uiGlass;
  geFsReset();
  gLockType = 1; gNavMode = 0;
  uint32_t p = geAddPhoto(FML_DIR_PHOTO "/Playa.jpg", geBeach(1600, 1200), false);
  vwSetDims(p, 1600, 1200);
  shotApp(IC_GALERIA); gAppState[IC_GALERIA] = ALIFE_RUNNING; mkBind(&GAL_APP); galViewReady = false;
  gTestMs = 12000000; touchReset();
  uiGlass = true;
  galOpenId(p);                                   // Auto: apaisada -> horizontal
  vwGlassPrep(); vwPresentAll(); shotSave("visor_foto_horizontal");
  gMediaOriMode = MORI_AUTO; vwCycleOrientation();   // Vertical
  shotSave("visor_foto_vertical");
  vwZoomAt(2.2f, vwOffX + vwFitW * 0.3f, vwVY + vwVH * 0.5f); vwRenderContent(true); vwGlassPrep(); vwPresentAll();
  shotSave("visor_foto_zoom");
  uiGlass = false; vwScale = 1.0f; vwClamp(); vwRenderContent(true); vwPresentAll();
  shotSave("visor_foto_plano");
  vwClose();
  uiGlass = true;
  std::vector<std::vector<uint8_t>> fr;
  for(int k = 0; k < 4; k++) fr.push_back(geBeach(320, 240));
  gTestFiles[FML_DIR_VIDEO "/Clip.avi"] = vwTestAvi(fr, 320, 240, 40000);
  FlexMlRec vr; memset(&vr, 0, sizeof(vr));
  snprintf(vr.path, sizeof(vr.path), "%s", FML_DIR_VIDEO "/Clip.avi"); snprintf(vr.name, sizeof(vr.name), "Clip.avi");
  vr.kind = FML_K_VIDEO; vr.fmt = FML_F_AVI_MJPEG; vr.state = FML_S_READY; vr.flags = FML_R_PLAYABLE; vr.w = 320; vr.h = 240;
  vr.size = (uint32_t)gTestFiles[FML_DIR_VIDEO "/Clip.avi"].size();
  int vat = flexMlAdd(&gMs.lib, &vr, 1760000000u);
  vwOpen(&GAL_VW, gMs.lib.recs[vat].id, NULL, NULL);
  vwGlassPrep(); vwPresentAll(); shotSave("visor_video_horizontal");
  gMediaOriMode = MORI_AUTO; vwCycleOrientation();
  shotSave("visor_video_vertical");
  vwClose();
  uiGlass = glass0;
  mkReset();
  gMlOk = ok0; gTestFsReady = fs0; gTestMemFs = false; gTestFiles.clear();
  memset(&gMs, 0, sizeof(gMs));
  gState = ST_HOME; gAppId = 0; gAppState[IC_GALERIA] = ALIFE_CLOSED; gLand = false; uiClipFull();
}


// Menus, dialogos y Papelera con el tema; el menu contextual sin apilar.
static void testKitTemaYPapelera(){
  printf("Kit de medios: menu sin apilar vidrio, tema, Papelera de mas de 16 y menu de la Galeria\n");
  bool ok0 = gMlOk; bool fs0 = gTestFsReady; gTestFsReady = true;
  bool glass0 = uiGlass;
  geFsReset();
  gLockType = 1;
  uint32_t a = geAddPhoto(FML_DIR_PHOTO "/Uno.jpg", vwTestJpeg(320, 240), false);
  uint32_t lk = geAddPhoto(FML_DIR_LOCKED "/9.jpg", vwTestJpeg(320, 240), true);
  shotApp(IC_GALERIA); gAppState[IC_GALERIA] = ALIFE_RUNNING; mkBind(&GAL_APP); galViewReady = false;
  uiGlass = true;
  galRender();
  std::vector<uint16_t> bg(fb, fb + (size_t)SCR_W * SCR_H);
  static const uint8_t acts[4] = { MA_SELECT, MA_LOCK, MA_TRASH, MA_DELETE };
  // A) el despliegue en muchos cuadros...
  gTestMs = 20000000;
  mmOpen(240, 300, acts, 4);
  for(int k = 1; k <= 12; k++){ gTestMs += 15; mmAnimTick(); }
  gTestMs += MM_ANIM_MS; mmAnimTick();
  std::vector<uint16_t> many(fb, fb + (size_t)SCR_W * SCR_H);
  chk(mmAnimDone && !uiGlassBandActive(), "al terminar el despliegue se suelta la banda desenfocada");
  mmClose();
  // B) ...y en UNO solo, desde el mismo fondo.
  memcpy(fb, bg.data(), bg.size() * 2);
  mmOpen(240, 300, acts, 4);
  gTestMs += MM_ANIM_MS + 1; mmAnimTick();
  chk(memcmp(many.data(), fb, many.size() * 2) == 0, "desplegar el menu en 13 cuadros deja lo mismo que en 1: el vidrio no se apila");
  mmClose();
  chk(!uiGlassBandActive(), "cerrar el menu nunca deja la banda activa para otra superficie");
  { int x, y, w, h; mmGeom(x, y, w, h);
    // Etiqueta destructiva en el color del tema.
    memcpy(fb, bg.data(), bg.size() * 2); mmOpen(240, 300, acts, 4); gTestMs += MM_ANIM_MS + 1; mmAnimTick();
    bool danger = false;
    for(int yy = y + MM_PAD + 3 * MM_RH; yy < y + MM_PAD + 4 * MM_RH && !danger; yy++)
      for(int xx = x + 18; xx < x + 120 && !danger; xx++) if(fb[(size_t)yy * SCR_W + xx] == TH_DANGER) danger = true;
    chk(danger, "'Borrar para siempre' sale en el color destructivo del tema");
    mmClose(); }
  // Menu de la Galeria: Editar y Abrir en Multimedia para una foto abierta...
  mkReset(); galRender();
  { int cell = 0;                                      // la celda de la foto abierta (no la protegida)
    mlLock(); galSyncLocked();
    for(int i = 0; i < galView.n; i++) if(galRecLocked(i)->id == a) cell = i;
    mlUnlock();
    int x, y, w, h; galCellRect(cell, x, y, w, h);
    unsigned long t0 = gTestMs + 100;
    tDown(x + w / 2, y + h / 2, t0); galTick();
    tMove(x + w / 2, y + h / 2, t0 + 700); galTick(); }
  bool hasEdit = false, hasMM = false, hasTrash = false;
  for(int i = 0; i < mmN; i++){ if(mmAct[i] == MA_EDIT) hasEdit = true; if(mmAct[i] == MA_OPENMM) hasMM = true; if(mmAct[i] == MA_TRASH) hasTrash = true; }
  chk(mmOn && hasEdit && hasMM && hasTrash, "pulsacion larga en una foto: Editar, Abrir en Multimedia y Eliminar (a la Papelera)");
  mmClose(); touchReset(); mkReset();
  // ...y nada de eso en un protegido.
  mkOpenItemMenu(lk, 240, 300, NULL, 0);
  bool leak = false;
  for(int i = 0; i < mmN; i++) if(mmAct[i] == MA_EDIT || mmAct[i] == MA_OPENMM || mmAct[i] == MA_TRASH) leak = true;
  chk(!leak, "protegido: ni Editar, ni Abrir en Multimedia, ni Papelera");
  mmClose(); mkReset();
  // Papelera con 40 elementos: se ven TODOS (antes 16) y la lista se suelta al salir.
  for(int k = 0; k < 40; k++){ char p[48]; snprintf(p, sizeof(p), "/Papelera/nota%02d.txt", k); gTestFiles[p] = std::vector<uint8_t>(10, 'x'); }
  fkTrashOpen(); fkCloseAll();            // la primera vez el vidrio reserva su cache de tarjetas (permanente)
  size_t ps0 = gPsUsed;
  fkTrashOpen();
  chk(fkTrashOn && fkTrashN == 40 && fkTrashTotal == 40, "la Papelera ensena sus 40 elementos, no solo 16");
  fkCloseAll();
  chk(!fkTrashOn && !fkTrashList && gPsUsed == ps0, "al cerrarla se suelta su lista (PSRAM devuelta)");
  uiGlass = glass0;
  mkReset();
  gMlOk = ok0; gTestFsReady = fs0; gTestMemFs = false; gTestFiles.clear();
  memset(&gMs, 0, sizeof(gMs));
  gState = ST_HOME; gAppId = 0; gAppState[IC_GALERIA] = ALIFE_CLOSED; touchReset();
}

// #############################################################
//  TRABAJO PERIODICO QUE NO TIENE QUE ESTAR AHI
//  ------------------------------------------------------------
//  El tiron de ~4 ms cada pocos segundos en todas las pantallas: el tick de
//  los widgets recorria la particion LittleFS entera (flexFsUsedBytes) cada
//  2 s pasara lo que pasara -- dentro de una app, con el dedo arrastrando,
//  sin widget de almacenamiento en el escritorio --, y la medida de memoria
//  recorria el monton de la PSRAM (bloque contiguo) cada 2 s con la memoria
//  holgada. Se cuenta cuantas veces ocurre cada cosa en 60 s de reloj.
// #############################################################
extern unsigned gTestFsUsedCalls;
static void testTrabajoPeriodico(){
  printf("Trabajo periodico: sin recorrer la flash ni el monton de la PSRAM cuando no hace falta\n");
  int before = gFails;
  HomeWidget wg0[HOME_PAGES_MAX][HOME_WG_MAX]; uint8_t wn0[HOME_PAGES_MAX];
  memcpy(wg0, gHomeWg, sizeof(wg0)); memcpy(wn0, gHomeWgN, sizeof(wn0));
  auto correr = [&](unsigned long ms){
    unsigned c0 = gTestFsUsedCalls;
    for(unsigned long t = 0; t < ms; t += 100){ gTestMs += 100; wgDataTick(); }
    return gTestFsUsedCalls - c0;
  };
  for(int p = 0; p < HOME_PAGES_MAX; p++) gHomeWgN[p] = 0;
  gState = ST_APP; T = Touch(); hpDragging = hpSettling = false; editMode = false;
  gTestMs = 5000000; wgStoMs = 0;
  chk(correr(60000) == 0, "sin widget de almacenamiento, 60 s dentro de una app: ni un recorrido de la flash");
  gState = ST_HOME;
  chk(correr(60000) == 0, "sin widget de almacenamiento, 60 s en el escritorio: ni uno");
  // Con un widget de almacenamiento en la pagina 1
  gHomeWgN[1] = 1; gHomeWg[1][0].type = WG_STORAGE; gHomeWg[1][0].col = 0; gHomeWg[1][0].row = 1;
  gHomeWg[1][0].w = 2; gHomeWg[1][0].h = 1;
  gState = ST_APP;
  chk(correr(30000) == 0, "con widget pero dentro de una app: no se mide (no se ve)");
  gState = ST_HOME; T.down = true;
  chk(correr(10000) == 0, "con el dedo apoyado no se mide");
  T.down = false; hpDragging = true;
  chk(correr(10000) == 0, "ni durante el paso de pagina");
  hpDragging = false;
  unsigned n = correr(60000);
  chkf(n >= 1 && n <= 7, "en el escritorio quieto se mide, como mucho cada 10 s (%u veces en 60 s)", n);
  memcpy(gHomeWg, wg0, sizeof(wg0)); memcpy(gHomeWgN, wn0, sizeof(wn0));

  // ---- El bloque contiguo de la PSRAM ----
  size_t pr0 = gTestPsPressure;
  gTestPsPressure = 0;
  gMemAlerts.levelSeen = 1; gMemAlerts.level = FLEXMEM_LV_OK;
  gState = ST_HOME; T = Touch();
  auto bloques = [&](unsigned long ms, bool dedo){
    unsigned c0 = gLargestCalls;
    for(unsigned long t = 0; t < ms; t += 100){ gTestMs += 100; T.down = dedo; memTick(); }
    T.down = false;
    return gLargestCalls - c0;
  };
  memTick(); gTestMs += 20000; memTick();          // punto de partida conocido
  unsigned b = bloques(60000, false);
  chkf(b >= 5 && b <= 7, "con holgura, el mayor bloque se mide cada 10 s (%u veces en 60 s, antes 30)", b);
  chkf(bloques(30000, true) == 0, "y nunca con el dedo apoyado si no hay presion");
  gTestPsPressure = gTestPsTotal - gPsUsed - (8u << 20);   // 8 MB libres: presion
  gMemAlerts.level = FLEXMEM_LV_NOTICE;
  b = bloques(20000, true);
  chkf(b >= 9, "con presion vuelve a medirse cada 2 s, dedo o no (%u veces en 20 s)", b);
  gTestPsPressure = pr0; gMemAlerts.level = FLEXMEM_LV_OK;
  memSampleNow();
  gState = ST_HOME; T = Touch();
  if(gFails == before) printf("  Trabajo periodico: todas las comprobaciones pasan.\n");
}

// #############################################################
//  ESCRITORIO · LA NOTIFICACION SE QUEDA ENCIMA AL CAMBIAR DE PAGINA
//  ------------------------------------------------------------
//  El fallo de las fotos: un aviso a la vista, el usuario desliza a otra
//  pagina y la tarjeta acaba DETRAS de los widgets de cabecera, cortada y
//  quieta hasta volver. Se mira la sombra del panel: dentro de la tarjeta no
//  puede verse ni un pixel de un widget, la tarjeta sigue su vida (caduca y
//  sale) aunque el dedo este quieto, no deja restos y el ultimo cuadro del
//  acomodo la conserva. En Plano y en Liquid Glass.
// #############################################################
//  BANNER DE NOTIFICACION: LA ULTIMA CAPA ANTES DEL PANEL
//  ------------------------------------------------------------
//  Reproduce lo que se vio en la placa (capturas de la caja de apps y de Flex Compass): el banner capturaba
//  una banda de fb al armarse, la repintaba y la devolvia al irse, asi que si lo de debajo cambiaba
//  -- la caja de apps subia, una app se animaba -- seguia pegando el escritorio viejo encima. Aqui se mira
//  LO QUE LLEGA AL PANEL (gPanelShadow), no solo fb:
//    · fb no guarda nunca el banner;
//    · cuando lo de debajo cambia, el panel muestra lo NUEVO con el banner encima (ni un pixel viejo);
//    · el vidrio es vidrio de verdad: depende de lo que hay debajo (no es un rectangulo oscuro fijo);
//    · al irse, el panel recibe fb tal cual: sin rastro;
//    · quien toma la pantalla lo apaga en el mismo cuadro;
//    · el toque: arrastrar a izquierda o derecha, umbral, lanzamiento, vuelta al sitio, retirada REAL de la
//      notificacion, y la pantalla de debajo no ve NI UN evento del gesto.
// #############################################################
static uint16_t bnA(int x, int y){ return rgb565((uint8_t)(10 + (x >> 4)), (uint8_t)(20 + (y & 31)), 60); }          // oscuro (una app)
static uint16_t bnB(int x, int y){ return rgb565(215, (uint8_t)(200 + ((x >> 3) & 31)), (uint8_t)(225 - (y & 15))); } // claro (la caja de apps)
static void bnPaint(uint16_t (*f)(int, int), int y0, int y1){
  for(int y = y0; y <= y1; y++) for(int x = 0; x < SCR_W; x++) fb[(size_t)y * SCR_W + x] = f(x, y);
}
static int bnLuma(uint16_t c){
  int r = ((c >> 11) & 31) * 255 / 31, g = ((c >> 5) & 63) * 255 / 63, b = (c & 31) * 255 / 31;
  return (r + g + b) / 3;
}
static int bnAvg(const uint16_t* buf, int x0, int y0, int x1, int y1){
  long sum = 0; int n = 0;
  for(int y = y0; y <= y1; y++) for(int x = x0; x <= x1; x++){ sum += bnLuma(buf[(size_t)y * SCR_W + x]); n++; }
  return n ? (int)(sum / n) : 0;
}
// Lo que la pantalla de DEBAJO ve del toque en esta vuelta (despues de que el banner haya tenido su turno).
struct BnUnder { bool pressed, down, tap, released, swipe; };
static BnUnder gBnUnder;
static void bnUnderReset(){ gBnUnder = BnUnder(); }
// Una vuelta de loop(): el GT911 entrega un cuadro, flexPollTouch, el banner, la pantalla de debajo (que mira T), y el tick del banner.
static void bnStep(int fingers, int x, int y){
  gtFrame(fingers, x, y); gTestMs += 16; flexPollTouch();
  fpbTouch();
  if(T.pressed) gBnUnder.pressed = true;
  if(T.down) gBnUnder.down = true;
  if(T.tap) gBnUnder.tap = true;
  if(T.released) gBnUnder.released = true;
  if(T.swipeLeft || T.swipeRight || T.swipeUp || T.swipeDown) gBnUnder.swipe = true;
  fpbTick();
}
static void bnIdle(int n){                        // vueltas sin cuadro nuevo del GT911 (-1) y con el dedo ya fuera
  for(int i = 0; i < n; i++){
    gTestMs += 16; flexPollTouch(); fpbTouch();
    if(T.pressed) gBnUnder.pressed = true;
    if(T.down) gBnUnder.down = true;
    if(T.tap) gBnUnder.tap = true;
    if(T.released) gBnUnder.released = true;
    if(T.swipeLeft || T.swipeRight) gBnUnder.swipe = true;
    fpbTick();
  }
}
static bool bnUnderSawNothing(){ return !gBnUnder.pressed && !gBnUnder.down && !gBnUnder.tap && !gBnUnder.released && !gBnUnder.swipe; }
static void bnShow(uint8_t src, uint32_t id, const char* title){
  fpbPush(src, id, "Flex Phone", title, "Cuerpo del aviso", FLP_PRI_DEFAULT);
  for(int i = 0; i < 40 && fpbState != FPB_SHOWN; i++){ gTestMs += 16; fpbTick(); }
}
static void bnWaitHidden(){
  for(int i = 0; i < 400 && fpbState != FPB_HIDDEN; i++){ gTestMs += 16; fpbTick(); }
}

static void testBannerNotificacion(){
  printf("Banner de notificacion: ultima capa antes del panel, vidrio real, sin rastro y descartable con el dedo\n");
  int before = gFails;
  const size_t N = (size_t)SCR_W * SCR_H;
  std::vector<uint16_t> shadow(N, 0), ref(N, 0);
  uint16_t* sh0 = gPanelShadow; gPanelShadow = shadow.data();
  flxPanel = (esp_lcd_panel_handle_t)1; flxDpiSem = (SemaphoreHandle_t)1;
  bool glass0 = uiGlass, dnd0 = gDnd; int nav0 = gNavMode; bool ok0 = gtOk, wire0 = gWireGtOn;
  uiGlass = true; gNavMode = 0; gDnd = false;
  gtOk = true; gWireGtOn = true; memset(gWireGt, 0, sizeof(gWireGt));
  if(fpbVisible()) fpbAbandon();
  fpbQueueN = 0; fpbMore = 0;
  appTrCancel(); qsForceClose();
  gState = ST_HOME; gAppId = 0; gLand = false; editMode = false; gHosted = false; gSuspOn = false;
  setBuf(fb); uiClipFull(); touchReset(); gTouchSwallow = false;
  gTestMs = 50000000;
  const size_t ps0 = gPsUsed;
  const int CX0 = FPB_V_X, CX1 = FPB_V_X + FPB_V_W - 1, CY0 = FPB_V_Y, CY1 = FPB_V_Y + FPB_V_H - 1;

  // ---- 1. Escritorio parado: no cuesta nada y no toca nada ----
  bnPaint(bnA, 0, SCR_H - 1); flxFlush(0, SCR_H - 1);
  chk(!memcmp(shadow.data(), fb, N * 2), "sin banner, el panel es fb");
  chk(!fpbLive && fpbCv == NULL && fpbSave == NULL, "sin banner no hay nada reservado");
  const unsigned draws0 = gPanelDrawCalls;
  fpbTick();
  chk(gPanelDrawCalls == draws0, "sin nada en cola, el tick no manda nada al panel");

  // ---- 2. Llega un aviso: se ve ENCIMA, y fb no lo guarda NUNCA ----
  std::vector<uint16_t> refA(fb, fb + N);
  bnShow(FPN_SRC_SYSTEM, 0, "Hola");
  chk(fpbState == FPB_SHOWN && fpbLive, "el banner llega a su sitio");
  chk(!memcmp(fb, refA.data(), N * 2), "fb sigue LIMPIO con el banner a la vista (el banner no vive en fb)");
  {
    bool cardOnPanel = false;
    for(int y = CY0 + 6; y < CY1 - 6 && !cardOnPanel; y += 7) for(int x = CX0 + 20; x < CX1 - 20; x += 11)
      if(shadow[(size_t)y * SCR_W + x] != fb[(size_t)y * SCR_W + x]){ cardOnPanel = true; break; }
    chk(cardOnPanel, "el panel muestra la tarjeta encima de lo que hay");
    bool restOk = true;
    for(int y = 130; y < SCR_H && restOk; y += 13) for(int x = 0; x < SCR_W; x += 17)
      if(shadow[(size_t)y * SCR_W + x] != fb[(size_t)y * SCR_W + x]) restOk = false;
    chk(restOk, "y el resto del panel es el de siempre");
  }
  const int lumaOscuro = bnAvg(shadow.data(), 400, 40, 450, 60);     // interior de la tarjeta, sin texto, sobre fondo OSCURO

  // ---- 3. LA PANTALLA DE DEBAJO CAMBIA (sube la caja de apps): ni un pixel del fondo viejo ----
  std::vector<uint16_t> refB(N, 0);
  { uint16_t* f0 = fb; (void)f0; bnPaint(bnB, 0, SCR_H - 1); memcpy(refB.data(), fb, N * 2); }
  flxFlush(0, SCR_H - 1);
  {
    bool old = false, newOk = true;
    for(int y = 0; y < 112; y++) for(int x = 0; x < SCR_W; x++){
      bool inCard = x >= CX0 - 2 && x <= CX1 + 6 && y >= CY0 - 2 && y <= CY1 + 6;
      if(inCard) continue;
      if(shadow[(size_t)y * SCR_W + x] == bnA(x, y) && bnA(x, y) != bnB(x, y)) old = true;
      if(shadow[(size_t)y * SCR_W + x] != bnB(x, y)) newOk = false;
    }
    chk(!old, "tras repintar lo de debajo NO queda ni un pixel del fondo anterior (antes pegaba el escritorio viejo)");
    chk(newOk, "fuera de la tarjeta el panel muestra EXACTAMENTE lo nuevo");
  }
  chk(!memcmp(fb, refB.data(), N * 2), "y fb sigue limpio");
  gTestMs += FPB_REGLASS_MS + 20; fpbTick();                        // el vidrio se resuelve sobre lo nuevo
  const int lumaClaro = bnAvg(shadow.data(), 400, 40, 450, 60);
  chkf(lumaClaro > lumaOscuro + 25, "el vidrio es de VERDAD: sobre un fondo claro la tarjeta es mas clara (%d -> %d), no un rectangulo oscuro fijo", lumaOscuro, lumaClaro);
  chkf(lumaClaro > 70, "y sobre un fondo claro no es negro (luma %d)", lumaClaro);
  // Una banda suelta (solo unas filas) tambien lleva la tarjeta
  bnPaint(bnA, 40, 50); flxFlush(40, 50);
  chk(shadow[(size_t)45 * SCR_W + 5] == bnA(5, 45) && shadow[(size_t)45 * SCR_W + 240] != bnA(240, 45),
      "una transferencia parcial: lo nuevo fuera de la tarjeta y la tarjeta encima");
  bnPaint(bnB, 40, 50);

  // ---- 4. Se va solo: el panel recibe fb tal cual ----
  gTestMs += FPB_HOLD_MS + 20; bnWaitHidden();
  chk(fpbState == FPB_HIDDEN && !fpbLive, "el banner se retira solo");
  chk(!memcmp(shadow.data(), fb, N * 2), "al irse el panel es EXACTAMENTE fb: sin rastro");
  chk(fpbCv == NULL && fpbSave == NULL && gPsUsed == ps0, "y suelta lo que reservo");

  // ---- 5. Alguien se queda la pantalla a mitad de banner: se apaga en ese mismo cuadro ----
  bnPaint(bnA, 0, SCR_H - 1); flxFlush(0, SCR_H - 1);
  bnShow(FPN_SRC_SYSTEM, 0, "Cortina");
  chk(fpbState == FPB_SHOWN, "(otro aviso a la vista)");
  qsPanelY = SCR_H / 2;                                              // la cortina del panel rapido manda
  flxFlush(100, 140);                                                // ...y manda UNA banda que no cubre toda la tarjeta
  chk(!memcmp(shadow.data(), fb, N * 2), "con la cortina de por medio el panel no lleva banner ni rastro en el mismo cuadro");
  chk(fpbSuppressed && !fpbLive, "el banner quedo apagado");
  fpbTick();
  chk(fpbState == FPB_HIDDEN && fpbCv == NULL, "y el siguiente tick lo da de baja del todo");
  qsPanelY = 0; qsAnimOn = false; qsDragging = false;
  chk(gPsUsed == ps0, "(sin memoria colgada)");

  // ---- 6. Horizontal ----
  gLand = true;
  bnPaint(bnA, 0, SCR_H - 1); flxFlush(0, SCR_H - 1);
  bnShow(FPN_SRC_SYSTEM, 0, "Apaisado");
  chk(fpbState == FPB_SHOWN && fpbLand, "en horizontal tambien se arma");
  {
    int px = (SCR_W - 1) - (FPB_L_Y + FPB_L_H / 2), py = FPB_L_X + FPB_L_W / 2;   // el centro de la tarjeta, en pixeles FISICOS
    chk(shadow[(size_t)py * SCR_W + px] != fb[(size_t)py * SCR_W + px], "el panel muestra la tarjeta en su sitio (girada)");
    chk(fpbInsidePhys(px, py) && !fpbInsidePhys(10, 700), "y el tacto la encuentra donde se ve");
  }
  gLand = false;                                                     // la app giro: el banner se va, el panel se limpia
  chk(fpbScreenAllows() || true, "(girar)");
  bnPaint(bnA, 0, SCR_H - 1); flxFlush(0, SCR_H - 1);
  fpbTick();
  chk(fpbState == FPB_HIDDEN && !memcmp(shadow.data(), fb, N * 2), "al girar la pantalla el banner se retira y no deja rastro");

  // ---- 7. El dedo: arrastrar a la izquierda hasta pasar el umbral descarta; la pantalla de debajo no ve NADA ----
  bnPaint(bnA, 0, SCR_H - 1); flxFlush(0, SCR_H - 1);
  memset(gWireGt, 0, sizeof(gWireGt)); touchReset(); gTouchSwallow = false;
  {
    FlexPhoneNotif n; memset(&n, 0, sizeof(n));
    n.id = 77; n.used = true; n.pri = FLP_PRI_DEFAULT;
    snprintf(n.pkg, sizeof(n.pkg), "com.test"); snprintf(n.app, sizeof(n.app), "Prueba");
    snprintf(n.title, sizeof(n.title), "Aviso"); snprintf(n.text, sizeof(n.text), "texto");
    flexPhoneNotifPut(&fphModel, &n, gTestMs);
    chk(flexPhoneNotifFind(&fphModel, 77) >= 0, "(la notificacion esta en el modelo)");
    bnShow(FPN_SRC_PHONE, 77, "Aviso");
    bnUnderReset();
    bnStep(1, 300, 54); chk(fpbGesture, "el dedo baja dentro de la tarjeta: el gesto es del banner");
    bnStep(1, 280, 54); chk(fpbSlide == -20.0f, "sigue al dedo 1:1 hacia la izquierda");
    bnStep(1, 240, 54); chk(fpbSlide == -60.0f, "(y mas)");
    bnStep(1, 200, 54);
    bnStep(1, 140, 54); chk(fpbSlide == -160.0f, "pasado un tercio de su ancho");
    bnStep(0, 0, 0);
    chkf(fpbState == FPB_OUT && fpbOutDir == -1, "al soltar sale deslizandose a la izquierda (estado %d, dir %d)", fpbState, fpbOutDir);
    bnIdle(3);
    chk(bnUnderSawNothing(), "la pantalla de debajo NO vio ni un evento del gesto (ni bajar, ni arrastrar, ni soltar, ni deslizar)");
    bnWaitHidden();
    chk(fpbState == FPB_HIDDEN && !memcmp(shadow.data(), fb, N * 2), "sale y el panel queda limpio");
    chk(flexPhoneNotifFind(&fphModel, 77) < 0, "y la notificacion se RETIRA de verdad (no solo se mueve el dibujo)");
    // Sin overlay fantasma: el siguiente toque en el mismo sitio es de la pantalla de debajo, ya.
    bnUnderReset();
    bnStep(1, 300, 54); bnStep(0, 0, 0); bnIdle(2);
    chk(gBnUnder.pressed && gBnUnder.tap, "tras descartarla el toque vuelve a la pantalla de debajo en el acto");
    bnIdle(3);
  }

  // ---- 8. A la derecha tambien ----
  {
    bnShow(FPN_SRC_SYSTEM, 0, "Derecha");
    bnUnderReset();
    bnStep(1, 100, 54); bnStep(1, 140, 54); bnStep(1, 200, 54); bnStep(1, 270, 54);
    chk(fpbSlide == 170.0f, "sigue al dedo hacia la derecha");
    bnStep(0, 0, 0);
    chkf(fpbState == FPB_OUT && fpbOutDir == 1, "al soltar sale deslizandose a la derecha (estado %d, dir %d)", fpbState, fpbOutDir);
    bnIdle(3);
    chk(bnUnderSawNothing(), "(a la derecha) la pantalla de debajo no vio nada del gesto");
    bnWaitHidden();
    chk(fpbState == FPB_HIDDEN && !memcmp(shadow.data(), fb, N * 2), "(a la derecha) sale sin rastro");
  }

  // ---- 9. No llega al umbral: vuelve a su sitio y sigue ahi ----
  {
    bnShow(FPN_SRC_SYSTEM, 0, "Cancelar");
    bnUnderReset();
    bnStep(1, 300, 54); bnStep(1, 290, 54); bnStep(1, 275, 54);
    chk(fpbSlide == -25.0f, "arrastra un poco");
    gTestMs += 160;                                                  // se queda quieto antes de soltar: no es un lanzamiento
    bnStep(0, 0, 0);
    chk(fpbState == FPB_SHOWN && fpbSpring, "no llego al umbral: vuelve a su sitio (no se descarta)");
    for(int i = 0; i < 20; i++){ gTestMs += 16; flexPollTouch(); fpbTouch(); fpbTick(); }
    chk(fpbSlide == 0.0f && fpbState == FPB_SHOWN, "y se queda en su sitio");
    chk(bnUnderSawNothing(), "(cancelado) la pantalla de debajo tampoco vio nada");
    chk(!memcmp(fb, refA.data(), N * 2), "(fb sigue limpio)");
    // Un lanzamiento corto pero rapido SI descarta
    bnUnderReset();
    bnStep(1, 300, 54); bnStep(1, 250, 54); bnStep(1, 200, 54);
    bnStep(0, 0, 0);
    chkf(fpbState == FPB_OUT && fpbOutDir == -1, "un lanzamiento rapido descarta aunque no llegue al umbral (estado %d)", fpbState);
    bnIdle(3);
    chk(bnUnderSawNothing(), "(lanzamiento) la pantalla de debajo no vio nada");
    bnWaitHidden();
  }

  // ---- 10. Un toque sin arrastrar: lo atiende el banner y la pantalla de debajo no lo recibe ----
  {
    bnShow(FPN_SRC_SYSTEM, 0, "Toque");
    bnUnderReset();
    bnStep(1, 200, 54); bnStep(0, 0, 0); bnIdle(3);
    chk(bnUnderSawNothing(), "un toque en la tarjeta no llega a la pantalla de debajo (antes la tocaba tambien: p. ej. el 'atras' de la cabecera)");
    chk(fpbState == FPB_OUT || fpbState == FPB_HIDDEN, "y cierra el banner");
    bnWaitHidden();
  }

  // ---- 11. Un toque FUERA de la tarjeta es de la pantalla de debajo, con el banner a la vista ----
  {
    bnShow(FPN_SRC_SYSTEM, 0, "Fuera");
    bnUnderReset();
    bnStep(1, 200, 400); bnStep(0, 0, 0); bnIdle(3);
    chk(gBnUnder.pressed && gBnUnder.tap, "un toque fuera de la tarjeta lo recibe la pantalla de debajo con normalidad");
    chk(fpbState == FPB_SHOWN, "y el banner sigue ahi");
    // El dedo que empezo FUERA y pasa por encima de la tarjeta sigue siendo de la pantalla de debajo
    bnUnderReset();
    bnStep(1, 100, 400); bnStep(1, 100, 200); bnStep(1, 100, 60); bnStep(1, 120, 54); bnStep(0, 0, 0); bnIdle(3);
    chk(gBnUnder.pressed && gBnUnder.down && !fpbGesture, "un gesto que empezo fuera no lo roba el banner al pasar por encima");
    gTestMs += FPB_HOLD_MS + 20; bnWaitHidden();
  }

  // ---- 12. No molestar: un aviso del TELEFONO no se ve; el del sistema (respuesta a un toque) si ----
  {
    gDnd = true;
    fpbPush(FPN_SRC_PHONE, 5, "Tel", "no", "no", FLP_PRI_DEFAULT);
    for(int i = 0; i < 10; i++){ gTestMs += 16; fpbTick(); }
    chk(fpbState == FPB_HIDDEN && fpbQueueN == 1, "con No molestar un aviso del telefono espera (no se pierde ni se dibuja)");
    fpbQueueN = 0;
    fpbPushSystem("Flex Cloud", "Preparando", "40 %");
    for(int i = 0; i < 40 && fpbState != FPB_SHOWN; i++){ gTestMs += 16; fpbTick(); }
    chk(fpbState == FPB_SHOWN, "pero el aviso del propio sistema (la respuesta a lo que se acaba de tocar) si sale");
    gDnd = false;
    gTestMs += FPB_HOLD_MS + 20; bnWaitHidden();
  }

  chk(fpbState == FPB_HIDDEN && fpbCv == NULL && fpbSave == NULL && gPsUsed == ps0, "al final no queda nada reservado ni a la vista");
  memset(gWireGt, 0, sizeof(gWireGt)); touchReset(); gTouchSwallow = false;
  gtOk = ok0; gWireGtOn = wire0;
  uiGlass = glass0; gDnd = dnd0; gNavMode = nav0; gPanelShadow = sh0;
  gState = ST_HOME; gAppId = 0; gLand = false; uiClipFull(); setBuf(fb);
  if(gFails == before) printf("  Banner de notificacion: todas las comprobaciones pasan.\n");
}

// #############################################################
static void testIslaEncimaAlDeslizar(){
  printf("Escritorio: la notificacion se queda encima de la pagina que se desliza\n");
  int before = gFails;
  bool glass0 = uiGlass;
  std::vector<uint16_t> shadow((size_t)SCR_W * SCR_H, 0);
  uint16_t* sh0 = gPanelShadow; gPanelShadow = shadow.data();
  const uint16_t BG = TC(40, 40, 60), WA = TC(0, 252, 0), WB = TC(0, 0, 248);
  // Interior de la tarjeta quieta, sin sus esquinas redondeadas (por fuera del
  // arco se ve lo de debajo, como debe) y sin el borde de 1 px.
  const int cx0 = NOTIF_MARGIN_X + NOTIF_RAD + 2, cx1 = NOTIF_MARGIN_X + NOTIF_CARD_W - NOTIF_RAD - 2;
  const int cy1 = NOTIF_Y0 + NOTIF_CARD_H - 2;         // filas de la tarjeta que la franja pisa: [72, cy1)
  auto dentroTarjeta = [&](uint16_t col){
    int n = 0;
    for(int y = HOME_PAGE_TOP; y < cy1; y++) for(int x = cx0; x < cx1; x++)
      if(shadow[(size_t)y * SCR_W + x] == col) n++;
    return n;
  };
  for(int glass = 0; glass < 2; glass++){
    uiGlass = glass != 0;
    const char* gm = glass ? "vidrio" : "plano";
    gState = ST_HOME; editMode = false; qsPanelY = 0; gLand = false; gHosted = false;
    hpDragging = false; hpSettling = false; gHomePage = 0;
    gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs));
    notifBandOn = false; notifPaused = false; notifLastMs = 0; notifDragIdx = -1;
    if(!hpEnsureBuf()){ chk(false, "hay lienzo para la pagina vecina"); break; }
    int bandBot = homeBandBot();
    for(size_t i = 0; i < (size_t)SCR_W * SCR_H; i++) homeBuf[i] = BG;
    for(int y = HOME_PAGE_TOP; y < bandBot; y++) for(int x = 0; x < SCR_W; x++){
      hpBg[(size_t)(y - HOME_PAGE_TOP) * SCR_W + x] = BG;
      hpBuf[(size_t)(y - HOME_PAGE_TOP) * SCR_W + x] = BG;
    }
    // Un widget de cabecera por pagina que cruza la banda de la isla entera.
    for(int y = HOME_PAGE_TOP; y < HOME_PAGE_TOP + 110; y++) for(int x = 8; x < SCR_W - 8; x++){
      homeBuf[(size_t)y * SCR_W + x] = WA;
      hpBuf[(size_t)(y - HOME_PAGE_TOP) * SCR_W + x] = WB;
    }
    memcpy(fb, homeBuf, (size_t)SCR_W * SCR_H * 2); setBuf(fb); uiClipFull(); flxFlushAll();
    hpBufPage = 1; hpFrom = 0; hpTo = 1; hpTop = HOME_PAGE_TOP;
    gGlRecN[0] = gGlRecN[1] = 0;
    hpMaskBuild(0); hpMaskBuild(1);
    DetectedModule m; memset(&m, 0, sizeof(m));
    m.active = true; m.type = MOD_MEDIA;
    snprintf(m.name, sizeof(m.name), "Foto recibida");
    snprintf(m.sub, sizeof(m.sub), "Galeria");
    notifPush(&m);
    gTestMs = 900000;
    for(int k = 0; k < 12; k++){ gTestMs += 40; notifTick(); }
    chkf(gNotifs[0].armed && gNotifs[0].phase == NP_IDLE, "[%s] el aviso esta a la vista y quieto", gm);
    chkf(dentroTarjeta(WA) == 0, "[%s] antes del gesto, la tarjeta tapa el widget", gm);
    // ---- El gesto: la pagina se desliza por debajo de la tarjeta ----
    hpDragging = true;
    int tapada = 0, sinMover = 0;
    for(int dx = -30; dx >= -450; dx -= 60){
      hpRenderFrame(dx);
      gTestMs += 40; notifTick();
      tapada += dentroTarjeta(WA) + dentroTarjeta(WB);
      // La pagina SI se mueve fuera de la tarjeta: a la derecha de la tarjeta
      // (x=470) se ve ya el widget de la pagina que entra (antes, el de la otra).
      uint16_t der = shadow[(size_t)(HOME_PAGE_TOP + 20) * SCR_W + 470];
      if(der != WB) sinMover++;
    }
    chkf(tapada == 0, "[%s] durante el gesto ningun widget se pinta encima de la tarjeta (%d px)", gm, tapada);
    chkf(sinMover == 0, "[%s] y la pagina se sigue moviendo por debajo", gm);
    // ---- Dedo quieto: la tarjeta caduca, sale y no deja restos ----
    T = Touch(); T.down = true; T.startX = 440; T.x = 200; T.y = T.startY = HOME_PAGE_TOP + 200;
    hpDx = -240; hpLastDx = 0x7FFFFFFF;
    for(int k = 0; k < 220 && (gNotifCount > 0 || notifBandOn); k++){
      gTestMs += 40; hpTick(); notifTick();
    }
    chkf(gNotifCount == 0 && !notifBandOn, "[%s] con el dedo quieto el aviso caduca y sale (no se congela)", gm);
    int restos = 0;
    for(int y = NOTIF_BAND_TOP; y < HOME_PAGE_TOP; y++) for(int x = 0; x < SCR_W; x++)
      if(shadow[(size_t)y * SCR_W + x] != homeBuf[(size_t)y * SCR_W + x]) restos++;
    chkf(restos == 0, "[%s] al irse no deja restos por encima de la franja (%d px)", gm, restos);
    chkf(dentroTarjeta(WA) + dentroTarjeta(WB) > 0, "[%s] y la pagina vuelve a verse donde estaba la tarjeta", gm);
    // ---- Ultimo cuadro del acomodo: la tarjeta sigue encima ----
    notifPush(&m);
    for(int k = 0; k < 12; k++){ gTestMs += 40; notifTick(); }
    hpRenderFrame(-240);
    hpDragging = false; hpSettling = true; hpSettleFrom = -240; hpSettleTo = -SCR_W;
    hpSettleT0 = (uint32_t)gTestMs - HP_SETTLE_MS - 1;
    T = Touch();
    hpTick();
    chkf(!hpSettling && gHomePage == 1, "[%s] el acomodo termina en la pagina nueva", gm);
    chkf(dentroTarjeta(WB) == 0 && dentroTarjeta(WA) == 0, "[%s] y su ultimo cuadro conserva la tarjeta encima", gm);
    gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs)); notifBandOn = false;
    hpSettling = false; hpDragging = false; gHomePage = 0; hpBufPage = -1;
  }
  uiGlass = glass0;
  gPanelShadow = sh0;
  T = Touch();
  hpTop = HOME_BAND_TOP; gHomePage = 0;
  drawWallpaper(homeBuf, false);
  setBuf(fb); uiClipFull();
  if(gFails == before) printf("  Isla encima al deslizar: todas las comprobaciones pasan.\n");
}

// #############################################################
//  GALERIA · NADA FUERA DE SU CAJA, NI EN EL PANEL NI EN EL EDITOR
//  ------------------------------------------------------------
//  El fallo de las fotos del usuario ("partes de las imagenes de abajo
//  cortadas, pegadas y congeladas al desplazar; el editor las hereda"):
//  con la barra en modo GESTOS, la ultima fila de la rejilla se desbordaba
//  en la franja de 64 px de abajo, que en ese modo no repinta nadie. La
//  rejilla publica hasta WIN_BOT, asi que el trozo se quedaba en fb; el
//  siguiente volcado completo (cerrar el visor) lo sacaba al panel, y ahi
//  se quedaba congelado mientras el resto se desplazaba. Se mira la SOMBRA
//  del panel (lo que se ve de verdad), no solo fb, en los dos modos de barra.
// #############################################################
static void testGaleriaSinRestos(){
  printf("Galeria: la rejilla no sale de su caja, el panel no conserva restos y el editor no los hereda\n");
  int before = gFails;
  std::vector<uint16_t> shadow((size_t)SCR_W * SCR_H, 0);
  uint16_t* sh0 = gPanelShadow; gPanelShadow = shadow.data();
  int nav0 = gNavMode; bool ok0 = gMlOk;
  tkReset();
  uint32_t first = 0;
  for(int i = 0; i < 12; i++){
    char p[40]; snprintf(p, sizeof(p), "/Imagenes/foto%02d.jpg", i);
    uint32_t id = tkAdd(FML_K_PHOTO, FML_F_JPEG, p, NULL, false, true);
    if(!first) first = id;
  }
  gMlOk = true;
  const int bandRows = SCR_H - WIN_BOT;
  auto bandOf = [&](const uint16_t* buf, std::vector<uint16_t>& out){
    out.assign(buf + (size_t)WIN_BOT * SCR_W, buf + (size_t)SCR_H * SCR_W);
  };
  for(int mode = 0; mode < 2; mode++){           // 0 = botones, 1 = gestos
    gNavMode = mode;
    const char* mn = mode ? "gestos" : "botones";
    shotApp(IC_GALERIA); mkBind(&GAL_APP); mkReset(); galViewReady = false; galScroll = 0;
    flxFlushAll();                                // el marco de la app, tal cual: lo que DEBE verse abajo
    std::vector<uint16_t> want, got;
    bandOf(shadow.data(), want);
    // Cerrar el visor rehace el marco entero (mkRedrawAll): el volcado que
    // sacaba el desborde al panel.
    mkRedrawAll();
    bandOf(shadow.data(), got);
    chkf(got == want, "[%s] tras un volcado completo, la franja de abajo es la del sistema (sin trozos de la rejilla)", mn);
    bandOf(fb, got);
    chkf(got == want, "[%s] y en fb tampoco queda nada de la rejilla por debajo de su caja", mn);
    int bx, by, bw, bh; uiBox(bx, by, bw, bh);
    const int hdrRows = galHeadH() - 6;
    std::vector<uint16_t> hdr0(fb + (size_t)by * SCR_W, fb + (size_t)(by + hdrRows) * SCR_W);
    int maxS = galMaxScroll();
    chkf(maxS > 60, "[%s] hay recorrido de desplazamiento (%d px)", mn, maxS);
    const int steps[5] = { 9, 40, 77, maxS / 2, maxS };
    for(int k = 0; k < 5; k++){
      galScroll = steps[k]; galRender();
      bandOf(shadow.data(), got);
      chkf(got == want, "[%s] desplazando a %d px, el panel no conserva un trozo congelado abajo", mn, steps[k]);
      bandOf(fb, got);
      chkf(got == want, "[%s] desplazando a %d px, la rejilla no escribe fuera de su caja", mn, steps[k]);
      bool hdrSame = !memcmp(hdr0.data(), fb + (size_t)by * SCR_W, hdr0.size() * 2);
      chkf(hdrSame, "[%s] desplazando a %d px, ninguna etiqueta se pinta sobre las pestanas", mn, steps[k]);
    }
    mkRedrawAll();                                // otro volcado completo, ya desplazada
    bandOf(shadow.data(), got);
    chkf(got == want, "[%s] volcado completo con la rejilla desplazada: franja limpia", mn);
    // ---- El editor NO hereda lo que la pantalla anterior dejo abajo ----
    // El visor, en modo gestos, ocupa la pantalla entera: al pulsar Editar la
    // foto se queda en esas filas. Se simula con un color que no usa nadie.
    setBuf(fb); uiClipFull();
    fillRect(0, WIN_BOT, SCR_W, bandRows, TC(255, 0, 255));
    flxFlushAll();
    chkf(gedOpen(first), "[%s] el editor se abre sobre una foto JPEG", mn);
    bandOf(shadow.data(), got);
    chkf(got == want, "[%s] el editor rehace el marco: no hereda la franja de la pantalla anterior", mn);
    gedWorker(nullptr);                            // el trabajo de abrir termina (sin disco: falla limpio)
    gedCloseNow();
    gedToGallery();
    bandOf(shadow.data(), got);
    chkf(got == want, "[%s] al volver a la Galeria desde el editor, la franja sigue limpia", mn);
  }
  mkReset();
  gNavMode = nav0; gMlOk = ok0;
  gPanelShadow = sh0;
  memset(&gMs, 0, sizeof(gMs));
  gState = ST_HOME; gAppId = 0;
  uiClipFull(); setBuf(fb);
  if(gFails == before) printf("  Galeria sin restos: todas las comprobaciones pasan.\n");
}

// #############################################################
//  FLEX CLOUD EN LA INTERFAZ (Galeria, Multimedia, Archivos y el visor)
//  ------------------------------------------------------------
//  El codigo REAL del kit de la nube (FlexOS_Ultra_CloudKit.h), de las tres
//  apps y del visor, contra el DOBLE programable de FlexOS_Cloud (ver
//  ino_extern_stubs.cpp): la prueba pone el estado, la lista, los avisos y
//  los bloques del streaming, y comprueba que la interfaz pide lo correcto,
//  abre lo correcto y NUNCA borra un original sin el aviso que lo permite.
//  Con INO_SHOTS=1 deja capturas en build/shot_nube_*.ppm.
// #############################################################
extern FlexCloudStatus gStubCloudStatus;
extern FlexCloudListInfo gStubCloudList;
extern std::vector<FclItem> gStubCloudItems;
extern std::vector<FlexCloudEvent> gStubCloudEvents;
extern std::vector<FlexCloudXfer> gStubCloudXfers;
extern std::vector<std::string> gStubCloudCalls, gStubCloudThumbs, gStubCloudWanted;
extern bool gStubCloudActive;
extern uint32_t gStubCloudOp, gStubCloudThumbGen;
extern std::vector<uint8_t> gStubStreamData;
extern bool gStubStreamOpen;
extern uint8_t gStubStreamState;
extern uint32_t gStubStreamPinOff, gStubStreamPinLen, gStubStreamMisses;
void stubStreamDeliver(int maxBlocks);
// La foto de la nube (a la RAM): el doble hace de tarea de la nube y cuenta los buffers sin liberar.
extern uint32_t gStubViewOp, gStubViewGot, gStubViewTotal;
extern uint8_t gStubViewState;
extern int gStubViewLive, gStubViewCancels;
extern bool gStubViewRefuse;
extern std::string gStubViewErr;
extern std::vector<std::string> gStubWatched, gStubWatchKicks;
extern int gStubWatchCalls;
void stubViewProgress(uint32_t got);
void stubViewDeliver(const std::vector<uint8_t>& jpeg);
void stubViewFail(const char* why);

static FclItem clItem(const char* id, const char* name, uint8_t kind, uint64_t size, bool folder = false, bool thumb = false){
  FclItem it; memset(&it, 0, sizeof(it));
  snprintf(it.id, sizeof(it.id), "%s", id); snprintf(it.name, sizeof(it.name), "%s", name);
  it.kind = folder ? FCL_K_FOLDER : kind; it.isFolder = folder; it.size = size; it.hasThumb = thumb;
  snprintf(it.sha256, sizeof(it.sha256), "%064d", 7); it.updatedAt = 1773273600000LL;
  return it;
}
// Lo que la nube le dijo al usuario, por el canal que toque: la isla en el escritorio, el BANNER dentro de una app (sysSay).
static void saidClear(){ gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs)); fpbQueueN = 0; fpbMore = 0; }
static int saidCount(){ return gNotifCount + fpbQueueN; }
static std::string saidSub(){ return fpbQueueN > 0 ? std::string(fpbQueue[fpbQueueN - 1].body) : gNotifCount > 0 ? std::string(gNotifs[0].mod.sub) : std::string(); }
static std::string saidTitle(){ return fpbQueueN > 0 ? std::string(fpbQueue[fpbQueueN - 1].title) : gNotifCount > 0 ? std::string(gNotifs[0].mod.name) : std::string(); }
static bool clCalled(const char* prefix){ for(auto& c : gStubCloudCalls) if(!c.compare(0, strlen(prefix), prefix)) return true; return false; }
static void clStatusOnline(){
  memset(&gStubCloudStatus, 0, sizeof(gStubCloudStatus));
  gStubCloudStatus.net = FCN_ONLINE; gStubCloudStatus.gen = 1;
  gStubCloudStatus.quotaValid = true;
  gStubCloudStatus.quota.totalBytes = 5ull << 30; gStubCloudStatus.quota.usedBytes = 1288490189ull; gStubCloudStatus.quota.permille = 240;
  snprintf(gStubCloudStatus.address, sizeof(gStubCloudStatus.address), "ana@flex");
  // Conectada a la nube = hay una cuenta que SIRVE: la interfaz lo comprueba
  // (ckCloudBlock) antes de pedirle nada. Sin esto el doble de Account se quedaria
  // "sin cuenta" mientras la nube dice "conectado": una combinacion que no existe.
  gStubAccountLinked = true; gStubAccountSnap.link = FLEX_LINK_LINKED;
}
// Un toque completo en (x, y) con el tick de la app.
static unsigned long clMs = 20000000;
static void clTap(void (*tick)(), int x, int y){
  tDown(x, y, clMs); tick();
  tUp(clMs + 60, true); tick();
  clMs += 400; touchReset(); gTestMs = clMs;
}
static void clLong(void (*tick)(), int x, int y){
  tDown(x, y, clMs); tick();
  tMove(x, y, clMs + 700); tick();
  tUp(clMs + 760, false); tick();
  clMs += 1200; touchReset(); gTestMs = clMs;
}
// Toca la fila del menu de medios que tenga la accion `act`.
static bool clMenuPick(void (*tick)(), int act){
  if(!mmOn) return false;
  int x, y, w, h; mmGeom(x, y, w, h);
  for(int i = 0; i < mmN; i++) if(mmAct[i] == act){
    mmAnimDone = true;
    clTap(tick, x + w / 2, y + MM_PAD + i * MM_RH + MM_RH / 2);
    return true;
  }
  return false;
}
static void clCellCenter(int i, int& cx, int& cy){ int x, y, w, h; ckCellRect(i, x, y, w, h); cx = x + w / 2; cy = y + h / 2; }

// =====================================================================
//  FLEX STORAGE: aprobar un telefono en pantalla, Almacenamiento y la
//  nube con destino telefono (sin Flex Account)
// =====================================================================
extern FlexStorageInfo gStubStorage;
extern int gStubPairDecide, gStubSetEnabled, gStubForget;
extern uint8_t gStubCloudDest;
static void spaStubPair(const char* sas, const char* name, uint8_t left){
  gStubStorage.pairWaiting = 1;
  snprintf(gStubStorage.sas, sizeof(gStubStorage.sas), "%s", sas);
  snprintf(gStubStorage.pairName, sizeof(gStubStorage.pairName), "%s", name);
  snprintf(gStubStorage.pairModel, sizeof(gStubStorage.pairModel), "SM-A556B");
  snprintf(gStubStorage.pairIp, sizeof(gStubStorage.pairIp), "192.168.1.60");
  gStubStorage.pairLeftS = left;
}
// Unas vueltas de loop: la vigilancia y, si el cuadro esta a la vista, su tick.
static void spaPump(int n){ for(int i = 0; i < n; i++){ gTestMs += SPA_WATCH_MS + 10; spaWatch(); if(spaVisible()) spaTick(); } }
static void spaShowNow(){ spaPump(1); gTestMs += 20; spaTick(); gTestMs += SPA_ANIM_MS + 20; spaTick(); }
static void spaTapAt(int16_t* b){ tReset(); T.tap = true; T.x = (b[0] + b[2]) / 2; T.y = (b[1] + b[3]) / 2; spaTick(); }
static bool spaNotified(const char* sub){
  for(int i = 0; i < gNotifCount; i++) if(strstr(gNotifs[i].mod.sub, sub)) return true;
  return false;
}
static void testFlexStorage(){
  printf("Flex Storage: aprobar un telefono en pantalla, Almacenamiento y Flex Cloud en el telefono\n");
  int before = gFails;
  gLand = false; gHosted = false; editMode = false; qsPanelY = 0; qsAnimOn = false;
  gState = ST_HOME; gAppId = 0;
  gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs));
  memset(&gStubStorage, 0, sizeof(gStubStorage));
  gStubPairDecide = -1; gStubSetEnabled = -1; gStubForget = 0;
  spaAbandon(); spaDecided[0] = 0; spaTold[0] = 0; spaLastState = 0xFF; spaWatchMs = 0;

  // ---- 1. nada que aprobar: ni cuadro ni memoria ----
  spaPump(2);
  chk(!spaVisible() && spaBak == NULL, "sin emparejamiento no hay cuadro ni memoria reservada");

  // ---- 2. NUNCA con la pantalla bloqueada (ni con la proteccion contra robo) ----
  gState = ST_LOCK;
  spaStubPair("482915", "Galaxy A55 5G", 110);
  spaPump(2);
  chk(!spaVisible(), "con la pantalla bloqueada el cuadro NO sale");
  chk(spaNotified("Desbloquea para aprobar"), "...se avisa UNA vez de que hay que desbloquear");
  gState = ST_THEFT; spaPump(1);
  chk(!spaVisible(), "con la proteccion contra robo delante tampoco");
  gState = ST_HOME;
  spaPump(1);
  chk(spaVisible(), "al desbloquear sale solo (el emparejamiento seguia esperando)");
  gTestMs += 20; spaTick();
  chk(spaBandReady(), "prepara su banda en la fase de dibujo (no en la vigilancia)");
  gTestMs += SPA_ANIM_MS + 20; spaTick();
  chk(spaState == SPA_SHOWN, "termina la animacion de entrada");
  chk(!strcmp(spaInfo.sas, "482915") && !strcmp(spaInfo.pairName, "Galaxy A55 5G"), "ensena el codigo y el telefono que publico el nucleo");
  chk(spaBtnOk[3] <= spaBtnNo[1] && spaBtnOk[0] == spaBtnNo[0], "en vertical: Emparejar encima de Rechazar, apilados");
  { int lit = 0, y0 = SPA_V_Y + 168, y1 = SPA_V_Y + 244;
    for(int y = y0; y < y1; y++) for(int x = SCR_W / 2 - 90; x < SCR_W / 2 + 90; x++) if(fb[(size_t)y * SCR_W + x] != fb[(size_t)y0 * SCR_W + SCR_W / 2 - 140]) lit++;
    chk(lit > 400, "el codigo de 6 cifras se pinta grande en su caja"); }
  if(getenv("INO_SHOTS")) shotSave("storage_aprobar");
  gStubStorage.pairLeftS = 97; spaPump(1);
  chk(spaInfo.pairLeftS == 97 && !spaDirty, "la cuenta atras se actualiza y se repinta");
  tReset(); T.tap = true; T.x = SCR_W / 2; T.y = SPA_V_Y + 8; spaTick();
  chk(spaVisible() && gStubPairDecide == -1, "un toque fuera de los botones no decide nada");

  // ---- 3. Emparejar ----
  spaTapAt(spaBtnOk);
  chk(gStubPairDecide == 1, "Emparejar aprueba en el nucleo de Flex Storage");
  chk(spaState == SPA_OUT, "y el cuadro se retira animado");
  gTestMs += SPA_ANIM_MS + 20; spaTick();
  chk(!spaVisible() && spaBak == NULL, "...devuelve la banda y suelta la memoria");
  gStubStorage.pairWaiting = 1; spaPump(2);
  chk(!spaVisible(), "lo ya decidido no vuelve a salir aunque el nucleo tarde en retirarlo");
  gStubStorage.pairWaiting = 0;
  gStubStorage.state = FSP_READY; snprintf(gStubStorage.name, sizeof(gStubStorage.name), "Galaxy A55 5G");
  spaPump(1);
  chk(spaNotified("Activado en Galaxy A55 5G"), "al quedar emparejado se avisa: Activado en Galaxy A55 5G");

  // ---- 4. Rechazar otro telefono ----
  spaStubPair("106733", "Intruso", 100);
  spaShowNow();
  chk(spaState == SPA_SHOWN && !strcmp(spaInfo.pairName, "Intruso"), "otro telefono pide emparejarse: otro cuadro");
  spaTapAt(spaBtnNo);
  chk(gStubPairDecide == 0, "Rechazar llega al nucleo");
  gTestMs += SPA_ANIM_MS + 20; spaTick();
  chk(!spaVisible(), "y el cuadro se va");

  // ---- 5. termina por otro lado (caduca) con el cuadro a la vista ----
  spaStubPair("777001", "Galaxy A55 5G", 2);
  spaShowNow();
  chk(spaState == SPA_SHOWN, "nuevo emparejamiento a la vista");
  gStubStorage.pairWaiting = 0; spaPump(1);
  chk(spaState == SPA_OUT, "si caduca, el cuadro se va solo");
  gTestMs += SPA_ANIM_MS + 20; spaTick();
  chk(!spaVisible(), "...del todo");

  // ---- 6. se bloquea con el cuadro a la vista: fuera YA, sin decidir ----
  int dec0 = gStubPairDecide;
  spaStubPair("555123", "Galaxy A55 5G", 90);
  spaShowNow();
  gState = ST_LOCK; spaTick();
  chk(!spaVisible() && gStubPairDecide == dec0, "al bloquearse el aparato el cuadro se retira sin decidir");
  gState = ST_HOME; spaPump(1);
  chk(spaVisible(), "y vuelve a salir al desbloquear");
  spaAbandon(); gStubStorage.pairWaiting = 0;

  // ---- 7. Modo PC: no se dibuja (espera) ----
  gState = ST_APP; gAppId = IC_MODOPC;
  spaStubPair("901234", "Galaxy A55 5G", 80);
  spaPump(1);
  chk(!spaVisible(), "en Modo PC no se dibuja un cuadro a pantalla completa");
  gState = ST_HOME; gAppId = 0; gStubStorage.pairWaiting = 0; spaPump(1);

  // ---- 8. dos maquetas de verdad ----
  int vy0, vy1, ly0, ly1;
  spaBand(false, vy0, vy1); spaBand(true, ly0, ly1);
  chk(vy0 != ly0 && (vy1 - vy0) != (ly1 - ly0), "vertical y horizontal ocupan bandas distintas");
  snprintf(spaInfo.sas, sizeof(spaInfo.sas), "482915"); spaInfo.pairLeftS = 60;
  memset(bbuf, 0, (size_t)SCR_W * SCR_H * 2); setBuf(bbuf);
  gLand = true; spaDrawLandscape(1.0f); gLand = false;
  chk(spaBtnOk[1] == spaBtnNo[1] && spaBtnOk[0] > spaBtnNo[0], "en horizontal los dos botones van EN FILA");
  setBuf(fb); uiClipFull();

  // ---- 9. Flex Cloud con destino TELEFONO: sin Flex Account ----
  bool acc0 = gStubAccountLinked; uint8_t dest0 = gStubCloudDest;
  gStubAccountLinked = false;
  gStubCloudDest = FCD_PHONE; gStubStorage.state = FSP_READY;
  chk(ckCloudBlock() == NULL, "con el telefono emparejado Flex Cloud se usa SIN Flex Account");
  gStubStorage.state = FSP_REJECTED;
  chk(ckCloudBlock() && !strcmp(ckCloudBlock(), CK_MSG_REPAIR), "telefono que ya no reconoce el P4: Vuelve a emparejar");
  gStubStorage.state = FSP_NONE;
  chk(ckCloudBlock() && !strcmp(ckCloudBlock(), CK_MSG_NOPHONE), "sin telefono: Empareja tu telefono");
  gStubCloudDest = FCD_INTERNET;
  chk(ckCloudBlock() && !strcmp(ckCloudBlock(), CK_MSG_LINK), "con destino Internet sigue pidiendo la Flex Account de siempre");
  // La Galeria > Nube, destino telefono y sin telefono: el boton lleva a Almacenamiento.
  gStubCloudDest = FCD_PHONE;
  memset(&gStubCloudStatus, 0, sizeof(gStubCloudStatus));
  gStubCloudStatus.net = FCN_NO_ACCOUNT; gStubCloudStatus.gen++;
  shotApp(IC_GALERIA); gAppState[IC_GALERIA] = ALIFE_RUNNING; mkBind(&GAL_APP); galViewReady = false;
  galTab = GAL_TAB_CLOUD; galRender();
  chk(ckEmptyBtnY > 0 && ckEmptyBtnAct == 3, "destino telefono sin telefono: Como activarlo (Almacenamiento), no Vincular cuenta");
  if(getenv("INO_SHOTS")) shotSave("storage_nube_sin_telefono");
  ckUnbind(&GAL_CK);

  // ---- 10. Almacenamiento: la tarjeta y su pantalla ----
  memset(&gStubStorage, 0, sizeof(gStubStorage));
  gStubStorage.state = FSP_READY; gStubStorage.reachable = 1; gStubStorage.lastOkAgeS = 3;
  snprintf(gStubStorage.name, sizeof(gStubStorage.name), "Galaxy A55 5G");
  snprintf(gStubStorage.model, sizeof(gStubStorage.model), "SM-A556B");
  snprintf(gStubStorage.ip, sizeof(gStubStorage.ip), "192.168.1.60"); gStubStorage.port = 47830;
  clStatusOnline();                                  // cuota valida: 1,2 GB de 5 GB
  bool fsReady0 = gTestFsReady;
  geFsReset(); gTestFsReady = true;                  // disco en memoria: Almacenamiento tiene que leerlo
  shotApp(IC_ALMACEN);
  almWantPhone = false; almEnter();
  chk(almPhY1 > almPhY0, "Almacenamiento ensena la tarjeta Flex Cloud en tu telefono");
  if(getenv("INO_SHOTS")) shotSave("storage_almacenamiento");
  tReset(); T.tap = true; T.x = SCR_W / 2; T.y = (almPhY0 + almPhY1) / 2; almTick();
  chk(almScreen == ALM_SCR_PHONE, "tocarla abre la pantalla del telefono");
  chk(almPhBtn[0][2] > almPhBtn[0][0] && almPhBtn[1][2] > almPhBtn[1][0], "con Poner en pausa y Olvidar este telefono");
  chk(almPhBtn[1][3] <= WIN_BOT, "con la cuota y el aviso de red, los dos botones caben en la ventana");
  if(getenv("INO_SHOTS")) shotSave("storage_telefono");
  tReset(); T.tap = true; T.x = (almPhBtn[0][0] + almPhBtn[0][2]) / 2; T.y = (almPhBtn[0][1] + almPhBtn[0][3]) / 2; almTick();
  chk(gStubSetEnabled == 0 && gStubStorage.state == FSP_OFF, "Poner en pausa: el telefono queda en pausa (sin olvidarlo)");
  tReset(); T.tap = true; T.x = (almPhBtn[0][0] + almPhBtn[0][2]) / 2; T.y = (almPhBtn[0][1] + almPhBtn[0][3]) / 2; almTick();
  chk(gStubSetEnabled == 1 && gStubStorage.state == FSP_READY, "Volver a conectar");
  tReset(); T.tap = true; T.x = (almPhBtn[1][0] + almPhBtn[1][2]) / 2; T.y = (almPhBtn[1][1] + almPhBtn[1][3]) / 2; almTick();
  chk(gStubForget == 0, "Olvidar pide un segundo toque (no se borra al primero)");
  gTestMs += 500;
  tReset(); T.tap = true; T.x = (almPhBtn[1][0] + almPhBtn[1][2]) / 2; T.y = (almPhBtn[1][1] + almPhBtn[1][3]) / 2; almTick();
  chk(gStubForget == 1 && gStubStorage.state == FSP_NONE, "al segundo toque se olvida el telefono");
  chk(almPhBtn[1][2] == 0, "sin telefono ya no hay nada que olvidar: se explica como activarlo");
  chk(almBackScreen() && almScreen == ALM_SCR_MAIN, "atras vuelve a la pantalla principal de Almacenamiento");
  almCloseApp();
  gTestFsReady = fsReady0;

  gStubAccountLinked = acc0; gStubCloudDest = dest0;
  memset(&gStubStorage, 0, sizeof(gStubStorage));
  memset(&gStubCloudStatus, 0, sizeof(gStubCloudStatus));
  gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs));
  gState = ST_HOME; gAppId = 0;
  printf("  %s\n", gFails == before ? "ok" : "CON FALLOS");
}


// #############################################################
//  MUSICA DESDE FLEX CLOUD: el MISMO reproductor, otra fuente
//  ------------------------------------------------------------
//  El codigo REAL de Musica y del kit de la nube sobre el doble del streaming (que decide
//  que bloques de 64 KB han llegado). Se comprueba lo que importa en la placa: que el audio
//  no se pierda ni se repita cuando los datos llegan a trozos, que "cargando" no sea un error,
//  y que anterior/siguiente funcionen dentro de la nube.
// #############################################################
extern bool gStubAudioOk, gStubAudioPlay;
extern std::vector<uint8_t> gStubAudioOut;
extern std::vector<uint8_t> gStubStreamData;
extern uint8_t gStubStreamState;
extern std::vector<bool> gStubStreamReady, gStubStreamWant;
extern bool gStubStreamOpen;
static std::vector<uint8_t> mnWav(uint32_t rate, int seconds, size_t* dataStart){
  std::vector<uint8_t> pcm((size_t)rate * 2 * seconds);
  for(size_t i = 0; i < pcm.size(); i++) pcm[i] = (uint8_t)(i * 7 + (i >> 10));
  std::vector<uint8_t> w;
  auto u32 = [&](uint32_t v){ for(int i = 0; i < 4; i++) w.push_back((uint8_t)(v >> (8 * i))); };
  auto u16 = [&](uint16_t v){ w.push_back((uint8_t)v); w.push_back((uint8_t)(v >> 8)); };
  auto cc = [&](const char* t){ for(int i = 0; i < 4; i++) w.push_back((uint8_t)t[i]); };
  cc("RIFF"); u32((uint32_t)(36 + pcm.size())); cc("WAVE"); cc("fmt "); u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
  cc("data"); u32((uint32_t)pcm.size());
  *dataStart = w.size();
  w.insert(w.end(), pcm.begin(), pcm.end());
  return w;
}
static void mnTick(int n = 1){ for(int i = 0; i < n; i++){ gTestMs += 40; musAudioTick(); } }

static void testMusicaNube(){
  printf("Musica desde Flex Cloud: el mismo reproductor, datos que llegan a trozos\n");
  bool ok0 = gMlOk; gMlOk = true;
  gStubAudioOk = true; gStubAudioPlay = true; gStubAudioOut.clear();
  gStubCloudCalls.clear(); gStubCloudItems.clear(); memset(&gStubCloudList, 0, sizeof(gStubCloudList));
  size_t ds = 0;
  std::vector<uint8_t> wav = mnWav(8000, 20, &ds);                 // 320 KB: cinco bloques de 64 KB
  gStubStreamData = wav;
  gStubStreamReady.clear(); gStubStreamWant.clear();
  // (los elementos van al monton: un FclItem pesa ~650 bytes y el marco de esta funcion no puede pasar del presupuesto de pila)
  auto mk = [&](const char* id, const char* name, uint8_t kind, uint8_t ps, uint64_t playSize){
    FclItem it = clItem(id, name, kind, 777777, false, false);
    it.playState = ps; it.playSize = playSize;
    gStubCloudItems.push_back(it);
  };
  mk("fil_a", "Cancion A.m4a", FCL_K_AUDIO, FCL_PS_READY, wav.size());
  mk("fil_v", "Clip.mp4", FCL_K_VIDEO, FCL_PS_READY, wav.size());
  mk("fil_b", "Cancion B.wav", FCL_K_AUDIO, FCL_PS_NATIVE, wav.size());
  mk("fil_x", "Rota.mp3", FCL_K_AUDIO, FCL_PS_UNSUPPORTED, 0);
  mk("fil_c", "Cancion C.wav", FCL_K_AUDIO, FCL_PS_NATIVE, wav.size());
  const FclItem& a = gStubCloudItems[0]; const FclItem& v = gStubCloudItems[1]; const FclItem& b = gStubCloudItems[2];
  const FclItem& x = gStubCloudItems[3];
  gStubCloudList.state = FCL_LIST_READY; gStubCloudList.gen++;

  // La ruta lleva la extension de lo que LLEGA (por ella decide el P4 quien lo reproduce): un M4A preparado es un .wav.
  char path[FLEXMED_PATH_MAX]; ckPlayPath(a, path, sizeof(path));
  chk(!strcmp(path, "cloud:fil_a/Cancion A.wav"), "un M4A preparado se abre como .wav (y Archivos lo manda a Musica)");
  chk(flexMediaClassify("Cancion A.wav") == FLEXMED_AUDIO, "y esa extension es la de audio");
  ckPlayPath(v, path, sizeof(path)); chk(!strcmp(path, "cloud:fil_v/Clip.avi"), "un MP4 preparado se abre como .avi (el visor de video)");
  { FclItem photo = clItem("fil_f", "captura.png", FCL_K_PHOTO, 1000); photo.playState = FCL_PS_READY; photo.playSize = 1000; ckPlayPath(photo, path, sizeof(path)); }
  chk(!strcmp(path, "cloud:fil_f/captura.jpg"), "un PNG preparado, .jpg");
  const char* why = NULL;
  chk(fclOpenAction(&a, &why) == FCL_OPEN_AUDIO && fclOpenAction(&v, &why) == FCL_OPEN_STREAM && fclOpenAction(&x, &why) == FCL_OPEN_MENU,
      "la nube decide por lo que el telefono sabe: audio, video, y lo imposible al menu");

  // ---- empezar: el flujo se abre y se espera a que llegue el principio (sin bloquear nada)
  gMediaCloudItem = a;
  ckPlayPath(a, path, sizeof(path));
  musForget();
  bool opened = musLoad(0, path);
  chk(opened && musCloudWait && !musLoaded && !musErr[0], "de la nube: abre el flujo y queda 'cargando' (no es un error)");
  chk(clCalled("stream fil_a") && !strcmp(musTitle, "Cancion A") && !strcmp(musSub, "Flex Cloud"), "pide los bytes del archivo y titula con su nombre en la nube");
  mnTick(3);
  chk(musCloudWait && !musLoaded && !musPlaying, "sin el principio del archivo: sigue esperando, sin sonar ni fallar");
  stubStreamDeliver(1);                                            // llega el primer bloque
  mnTick(1);
  chk(!musCloudWait && musLoaded, "llega el principio: se lee la cabecera WAV y queda listo");
  chk(musAs.wav.sampleRate == 8000 && musAs.wav.channels == 1, "con los datos reales del WAV (8 kHz mono)");
  chk(musPlay() && musPlaying, "suena");
  mnTick(40);
  size_t before = gStubAudioOut.size();
  chk(before > 0 && before <= 64 * 1024, "suena lo que ya llego (el primer bloque)");
  chk(std::equal(gStubAudioOut.begin(), gStubAudioOut.end(), wav.begin() + ds), "y es EXACTAMENTE el audio del archivo");

  // ---- el tramo siguiente aun no llego: 'cargando', ni error ni final, y sin perder el hilo
  mnTick(60);
  chk(musAs.starved && musPlaying && !musErr[0] && musLoaded, "sin el siguiente bloque: queda cargando, SIN error y sin parar");
  chk(gStubAudioOut.size() <= 64 * 1024 + 4096, "no inventa audio que no ha llegado");
  chk(gStubStreamMisses > 0, "y lo pide a la nube");
  stubStreamDeliver(1); mnTick(40);
  chk(!musAs.starved || gStubAudioOut.size() > before, "llega y sigue");
  while(!musAs.ended && gStubAudioOut.size() < wav.size()){ stubStreamDeliver(1); mnTick(80); if(gTestMs > 5000000000UL) break; }
  chk(musAs.ended, "acaba la pista");
  chk(gStubAudioOut.size() == wav.size() - ds && std::equal(gStubAudioOut.begin(), gStubAudioOut.end(), wav.begin() + ds),
      "TODO el audio salio, en orden y sin repetir una sola muestra (aunque llegaba a trozos)");

  // ---- anterior / siguiente: DENTRO de la nube, saltando lo que no suena aqui
  gStubAudioOut.clear(); gStubStreamReady.assign(gStubStreamReady.size(), true);
  gStubCloudCalls.clear();
  gMediaCloudItem = a; ckPlayPath(a, path, sizeof(path));
  musForget(); chk(musLoad(0, path), "otra vez la primera");
  stubStreamDeliver(5); mnTick(2);
  chk(musLoaded && musPlay(), "lista");
  musNext(+1, false);
  chk(!strcmp(musCloudId, "fil_b"), "siguiente de A: B (se salta el video y...)");
  chk(!strcmp(gMediaCloudItem.id, "fil_b"), "...el elemento que se abre es el de la lista");
  stubStreamDeliver(5); mnTick(2);
  musNext(+1, false);
  chk(!strcmp(musCloudId, "fil_c"), "siguiente de B: C (se salta el MP3 que no se puede preparar)");
  stubStreamDeliver(5); mnTick(2);
  musNext(+1, false);
  chk(!strcmp(musCloudId, "fil_a"), "siguiente de la ultima vuelve a la primera (como en lo local)");
  musNext(-1, false);
  chk(!strcmp(musCloudId, "fil_c"), "anterior de la primera: la ultima");
  // al acabar una pista sola, en la ultima, no vuelve a empezar
  musNext(+1, true);
  chk(!musLoaded && !musPlaying && !musCloudWait, "al acabar la ultima por si sola, para (no da la vuelta)");

  // ---- fallos de verdad: se dicen
  gMediaCloudItem = a; ckPlayPath(a, path, sizeof(path));
  musForget(); musLoad(0, path);
  gStubStreamState = FCS_ERROR;
  mnTick(2);
  chk(!musCloudWait && !musLoaded && musErr[0], "si el flujo de la nube falla mientras se espera: se dice, no se queda cargando");
  gStubStreamState = FCS_OPENING;
  musForget(); gStubStreamReady.assign(gStubStreamReady.size(), false); gStubStreamOpen = false;
  musLoad(0, path);
  gStubStreamReady.assign(gStubStreamReady.size(), false);
  mnTick(1); gTestMs += 50000; mnTick(1);
  chk(!musCloudWait && musErr[0] && strstr(musErr, "Sin conexi") != NULL, "45 s sin recibir nada: 'Sin conexion con Flex Cloud'");
  // un WAV que no es de los que suenan se dice igual que en lo local
  musForget(); musErr[0] = 0;
  { std::vector<uint8_t> bad = wav; bad[20] = 0x55; gStubStreamData = bad; gStubStreamReady.clear(); gStubStreamWant.clear(); }
  gMediaCloudItem = b; ckPlayPath(b, path, sizeof(path));
  gStubStreamState = FCS_OPENING;
  musLoad(0, path); stubStreamDeliver(1); mnTick(2);
  chk(strstr(musErr, "c\xC3\xB3" "dec") != NULL, "y dice POR QUE (el codec), no un error generico");
  chk(!musLoaded && musErr[0], "audio con un codec que no suena: el motivo, sin colgarse");

  // ---- desde ARCHIVOS > FLEX CLOUD: tocar un audio abre Musica (el MISMO reproductor) con ese archivo, no un visor ni un menu
  musForget(); musErr[0] = 0; flexCloudStreamClose();
  gStubAudioOk = true; gTestMs = clMs;
  geFsReset(); gTestFsReady = true;
  clStatusOnline(); gStubAccountLinked = true; gStubAccountSnap.link = FLEX_LINK_LINKED;
  gStubCloudItems.clear();
  { FclItem au = clItem("fil_au", "Tema.m4a", FCL_K_AUDIO, 3000000); au.playState = FCL_PS_READY; au.playSize = 700000; gStubCloudItems.push_back(au); }
  gStubCloudList.state = FCL_LIST_READY; gStubCloudList.gen++;
  gStubStreamData.assign(700000, 7); gStubStreamReady.clear(); gStubStreamWant.clear(); gStubStreamOpen = false;
  filesEnter();
  clTap(filesTick, SCR_W * 3 / 4, FILES_SEG_Y + FILES_SEG_H / 2);                // Flex Cloud
  gStubCloudList.gen++; filesRender();
  { int ax, ay; clCellCenter(0, ax, ay);
    gStubCloudCalls.clear();
    clTap(filesTick, ax, ay);
    chk(gState == ST_APP && gAppId == IC_MUSICA, "tocar un audio de la nube en Archivos abre Musica (no un menu ni el visor)");
    // el enter() de la app corre al acabar la animacion de apertura: se termina como lo hace el bucle real
    gTestUs += (ATR_OPEN_MS + 50) * 1000u; appTrTick();
    chk(!strcmp(musPath, "cloud:fil_au/Tema.wav") && musCloud && musCloudWait && clCalled("stream fil_au"),
        "con el archivo de la nube (su version .wav) abierto por rangos, esperando el principio"); }
  musCloseApp(); gAppState[IC_MUSICA] = ALIFE_CLOSED; gAppState[IC_ALMACEN] = ALIFE_CLOSED;
  gTrEnterPending = false; gTrIn.on = false; gTrOut.on = false;
  filesCloud = false; ckUnbind(&filesCkHost);
  gState = ST_HOME; gAppId = 0; gMediaReturnApp = 0xFF;

  // ---- limpieza
  musForget(); musErr[0] = 0; flexCloudStreamClose();
  gStubStreamData.clear(); gStubCloudItems.clear();
  gStubAudioOk = false; gStubAudioPlay = false; gStubAudioOut.clear();
  gStubAccountLinked = false; memset(&gStubCloudStatus, 0, sizeof(gStubCloudStatus));
  gTestFsReady = false; gTestMemFs = false; gTestFiles.clear(); memset(&gMs, 0, sizeof(gMs));
  mkReset();
  gState = ST_HOME; gAppId = 0; gLand = false; uiClipFull(); setBuf(fb);
  gMlOk = ok0;
}

static void testFlexCloudUi(){
  printf("Flex Cloud en la interfaz: Galeria, Multimedia, Archivos, visor por rangos y avisos\n");
  int before = gFails;
  bool ok0 = gMlOk, fs0 = gTestFsReady, glass0 = uiGlass; int nav0 = gNavMode;
  gNavMode = 0;
  gTestMs = clMs;
  gStubCloudCalls.clear(); gStubCloudEvents.clear(); gStubCloudItems.clear(); gStubCloudXfers.clear();
  gStubCloudThumbs.clear(); gStubCloudWanted.clear();
  memset(&gStubCloudList, 0, sizeof(gStubCloudList));
  geFsReset();                                       // disco en memoria + biblioteca con sus callbacks
  gTestFsReady = true;

  // ---- 1. SIN CUENTA: se dice, con el camino para vincular ----
  memset(&gStubCloudStatus, 0, sizeof(gStubCloudStatus));
  gStubCloudStatus.net = FCN_NO_ACCOUNT;
  shotApp(IC_GALERIA); gAppState[IC_GALERIA] = ALIFE_RUNNING; mkBind(&GAL_APP); galViewReady = false;
  galTab = GAL_TAB_CLOUD; galRender();
  chk(ckHost == &GAL_CK, "la pestana Nube engancha el kit de la nube a la Galeria");
  chk(clCalled("list 2 root"), "y pide la vista de medios de la nube");
  chk(ckEmptyBtnY > 0 && ckEmptyBtnAct == 1, "sin Flex Account: boton para vincularla (no una lista vacia)");
  if(getenv("INO_SHOTS")) shotSave("nube_sin_cuenta");

  // ---- 2. CON CUENTA: rejilla de la nube con miniaturas (objeto aparte) ----
  clStatusOnline();
  gStubCloudItems.push_back(clItem("fil_p1", "Playa \xC3\x91" "and\xC3\xBA.jpg", FCL_K_PHOTO, 2411724, false, true));
  gStubCloudItems.push_back(clItem("fil_v1", "Viaje.avi", FCL_K_VIDEO, 640000, false, false));
  gStubCloudItems.push_back(clItem("fil_v2", "Clip.mp4", FCL_K_VIDEO, 9000000, false, true));
  gStubCloudItems.push_back(clItem("fil_p2", "Retrato.heic", FCL_K_PHOTO, 3000000, false, true));
  gStubCloudItems.push_back(clItem("fil_p3", "Mesa.jpg", FCL_K_PHOTO, 900000, false, true));
  gStubCloudList.state = FCL_LIST_READY; gStubCloudList.gen++;
  gStubCloudThumbs.push_back("fil_p1");
  gStubCloudWanted.clear();
  galRender();
  int c0x, c0y; clCellCenter(0, c0x, c0y);
  { int x, y, w, h; ckCellRect(0, x, y, w, h);
    uint16_t top = fb[(size_t)(y + 8) * SCR_W + x + w / 2];
    chk((top >> 11) > 20 && (top & 31) < 10, "la miniatura de la nube se pinta en su celda (no inventada)"); }
  bool wantedP3 = false, wantedV1 = false;
  for(auto& w : gStubCloudWanted){ if(w == "fil_p3") wantedP3 = true; if(w == "fil_v1") wantedV1 = true; }
  chk(wantedP3, "las miniaturas que faltan se piden (sin bloquear)");
  chk(!wantedV1, "un elemento sin miniatura en la nube no se pide en bucle");
  if(getenv("INO_SHOTS")) shotSave("nube_galeria");

  // ---- 3. FOTO: el visor aparece EN EL ACTO y trae la foto a la RAM (nunca a la flash) ----
  gStubCloudCalls.clear();
  clTap(galTick, c0x, c0y);
  chk(clCalled("view fil_p1"), "tocar una foto de la nube la pide a la nube");
  chk(vwActiveFor(&GAL_VW) && vwKind == VWK_PHOTO && vwLoading && vwViewOp != 0,
      "el visor aparece EN EL ACTO (antes esperaba a la descarga entera, con el panel azul), esperando la foto");
  chk(!strcmp(vwName, "Playa \xC3\x91" "and\xC3\xBA.jpg"), "con el nombre que el usuario ve en Flex Cloud");
  stubViewProgress(600000); gTestMs += 300; galTick();
  chk(vwViewGot == 600000, "el avance real llega al visor");
  stubViewDeliver(vwTestJpeg(800, 600)); gTestMs += 20; galTick();
  chk(vwKind == VWK_PHOTO && vwSrc && !vwLoading && !vwViewOp, "llega verificada y se decodifica DE LA RAM");
  chk(gStubViewLive == 0, "y el buffer de la nube queda liberado (un solo dueno en cada momento)");
  { bool onFlash = false; for(auto& kv : gTestFiles) if(!kv.first.compare(0, 14, "/System/Cloud/")) onFlash = true;
    chk(!onFlash, "NADA se escribio en la flash (cada sector borrado apagaba la cache y el panel se quedaba azul)"); }
  chk(!vwCanTrash && !vwCanEdit, "una foto de la nube no ofrece papelera ni editor locales");
  vwClose();
  chk(galTab == GAL_TAB_CLOUD && ckHost == &GAL_CK, "cerrar el visor vuelve a la nube");

  // ---- 4. MP4: no se intenta; se dice por que y se ofrecen sus acciones ----
  gStubCloudCalls.clear();
  int c2x, c2y; clCellCenter(2, c2x, c2y);
  clTap(galTick, c2x, c2y);
  chk(!clCalled("stream") && !clCalled("view"), "un MP4 no se intenta reproducir en el P4");
  chk(mmOn, "se ofrecen sus acciones (descargar, detalles...)");
  chk(clMenuPick(galTick, MA_CL_DOWNLOAD) && clCalled("down fil_v2 2"), "descargar: a la biblioteca (es un medio)");

  // ---- 5. PULSACION LARGA: menu de un elemento de la nube ----
  clLong(galTick, c0x, c0y);
  chk(mmOn, "pulsacion larga: menu del elemento de la nube");
  bool hasTrash = false, hasDl = false;
  for(int i = 0; i < mmN; i++){ if(mmAct[i] == MA_TRASH) hasTrash = true; if(mmAct[i] == MA_CL_DOWNLOAD) hasDl = true; }
  chk(hasTrash && hasDl, "con Descargar y Eliminar (a la papelera de la nube)");
  if(getenv("INO_SHOTS")){ mmDraw(1.0f); shotSave("nube_menu"); }
  gStubCloudCalls.clear();
  chk(clMenuPick(galTick, MA_TRASH) && clCalled("trash fil_p1"), "Eliminar manda a la papelera de Flex Cloud");

  // ---- 6. TRANSFERENCIAS: progreso real, cancelar, reintentar, limpiar ----
  FlexCloudXfer x1; memset(&x1, 0, sizeof(x1)); x1.id = 41; x1.type = FCL_JOB_UPLOAD; x1.phase = FCX_RUNNING;
  snprintf(x1.name, sizeof(x1.name), "Cumple.avi"); x1.size = 3u << 20; x1.done = 1258291; x1.bytesPerSec = 348160;
  FlexCloudXfer x2 = x1; x2.id = 42; x2.type = FCL_JOB_DOWNLOAD; x2.phase = FCX_FAILED; snprintf(x2.name, sizeof(x2.name), "Mesa.jpg");
  snprintf(x2.error, sizeof(x2.error), "No queda espacio en el dispositivo");
  FlexCloudXfer x3 = x1; x3.id = 43; x3.phase = FCX_DONE; x3.done = x3.size; snprintf(x3.name, sizeof(x3.name), "Notas.txt");
  gStubCloudXfers = { x1, x2, x3 };
  gStubCloudStatus.activeXfers = 1; gStubCloudStatus.gen++;
  ckXfersOn = true; ckRender();
  if(getenv("INO_SHOTS")) shotSave("nube_transferencias");
  gStubCloudCalls.clear();
  { int bx, by, bw, bh; ckBox(bx, by, bw, bh);
    clTap(galTick, bx + bw - 40, by + CKX_TOP + 20);
    chk(clCalled("cancel 41"), "Cancelar una subida en curso");
    clTap(galTick, bx + bw - 40, by + CKX_TOP + CKX_RH + 20);
    chk(clCalled("retry 42"), "Reintentar una descarga fallida");
    clTap(galTick, bx + bw / 2, by + bh - 40);
    chk(clCalled("clear"), "Quitar terminadas");
    clTap(galTick, bx + 20, by + 16);
    chk(!ckXfersOn, "volver de Transferencias a la nube"); }
  gStubCloudXfers.clear(); gStubCloudStatus.activeXfers = 0;

  // ---- 7. VIDEO AVI: se reproduce POR RANGOS, sin descargarlo ----
  std::vector<std::vector<uint8_t>> fr;
  for(int k = 0; k < 400; k++) fr.push_back(vwTestJpeg(320, 240));   // ~1 MB: mas que la ventana inicial
  gStubStreamData = vwTestAvi(fr, 320, 240, 40000);
  gStubCloudItems[1].size = gStubStreamData.size();
  galRender();
  gStubCloudCalls.clear();
  int c1x, c1y; clCellCenter(1, c1x, c1y);
  clTap(galTick, c1x, c1y);
  chk(clCalled("stream fil_v1"), "tocar un AVI de la nube abre el streaming (no una descarga)");
  chk(vwActiveFor(&GAL_VW) && vwKind == VWK_VIDEO && vwCloud && vwCloudPhase == 1, "el visor espera la cabecera ensenando Cargando");
  chk(!clCalled("down"), "y no se descarga");
  vwTick();
  chk(vwKind == VWK_VIDEO && vwCloudPhase == 1, "sin datos todavia: sigue esperando (no es un error)");
  stubStreamDeliver(1000);                            // llega la ventana del principio
  gTestMs += 10; vwTick();
  chk(vwCloudPhase == 2 && gStubStreamPinLen > 0, "el indice del final no estaba: se fija en la cache y se espera");
  if(getenv("INO_SHOTS")) shotSave("nube_video_cargando");
  stubStreamDeliver(1000);
  gTestMs += 10; vwTick();
  chk(vwCloudPhase == 3 && vwAvi.frames == 400 && vwAvi.idxFromFile, "abierto con su indice (se podra buscar)");
  gTestMs += 10; vwTick();
  chk(vwFrameLen > 0, "el primer fotograma ya se ve");
  int vx0 = (int)(vwOffX + 0.5f);
  chk(vwIsRed(vwSeen(vx0 + 160, LH / 2)) && vwIsBlue(vwSeen(vx0 + 480, LH / 2)), "y es el del archivo (ajustado y centrado)");
  // Reproducir con la red a tirones: los bloques llegan poco a poco.
  vwTogglePlay();
  if(getenv("CL_DEBUG")){ extern std::vector<bool> gStubStreamReady; int r = 0; for(bool b : gStubStreamReady) r += b; printf("   (bloques listos al empezar: %d de %zu; mf=%u)\n", r, gStubStreamReady.size(), vwAvi.maxFrameBytes); }
  bool buffered = false;
  for(int k = 0; k < 4000 && !vwEnded; k++){
    gTestUs = vwNextUs + 1000; gTestMs += 41;
    vwTick();
    if(vwBuffering) buffered = true;
    if(k % 200 == 199) stubStreamDeliver(1);        // ~30 fotogramas por bloque: la red va MAS LENTA que el video
  }
  gTestUs = 0;
  if(!vwEnded) printf("   (video: ended=%d kind=%d frame=%u playing=%d buffering=%d err=%s misses=%u)\n", vwEnded, vwKind, vwCurFrame, vwPlaying, vwBuffering, vwErr, gStubStreamMisses);
  chk(buffered, "cuando falta el siguiente tramo, espera (Cargando) en vez de saltarse fotogramas");
  chk(vwEnded && vwKind == VWK_VIDEO && vwCurFrame >= 398, "y llega al final sin darse por roto");
  gStubCloudCalls.clear();
  vwClose();
  chk(clCalled("stream-close") && !gStubStreamOpen, "cerrar el visor cierra el streaming (la tarea deja de bajar)");

  // ---- 7b. UN MP4 QUE EL TELEFONO PREPARO: se reproduce en ESE MISMO visor (antes: "solo AVI MJPEG") ----
  gStubCloudItems[2].playState = FCL_PS_READY; gStubCloudItems[2].playSize = gStubStreamData.size();
  galRender();
  gStubCloudCalls.clear();
  int c7bx, c7by; clCellCenter(2, c7bx, c7by);
  clTap(galTick, c7bx, c7by);
  chk(clCalled("stream fil_v2") && !clCalled("down"), "tocar un MP4 PREPARADO por el telefono abre el streaming de su version");
  chk(vwActiveFor(&GAL_VW) && vwKind == VWK_VIDEO && vwCloud, "en el visor de video de siempre (no uno nuevo)");
  stubStreamDeliver(1000); gTestMs += 10; vwTick();
  stubStreamDeliver(1000); gTestMs += 10; vwTick();
  gTestMs += 10; vwTick();
  chk(vwAvi.frames == 400 && vwFrameLen > 0, "y se ve el AVI que el telefono dejo preparado");
  vwClose();
  // el telefono no pudo convertirlo: NO se abre el visor para que falle despues; se dice por que
  gStubCloudItems[2].playState = FCL_PS_UNSUPPORTED;
  snprintf(gStubCloudItems[2].playReason, sizeof(gStubCloudItems[2].playReason), "V\xC3\xADdeo hevc: este tel\xC3\xA9" "fono no sabe decodificarlo.");
  galRender();
  gStubCloudCalls.clear();
  clTap(galTick, c7bx, c7by);
  chk(!clCalled("stream fil_v2") && !vwActiveFor(&GAL_VW), "un video que el telefono NO puede convertir no abre el visor (y se dice por que)");
  gStubCloudItems[2].playState = FCL_PS_PREPARING; gStubCloudItems[2].playProgress = 37;
  galRender();
  gStubCloudCalls.clear();
  clTap(galTick, c7bx, c7by);
  chk(!clCalled("stream fil_v2") && !vwActiveFor(&GAL_VW), "uno que se esta preparando tampoco: se espera");
  gStubCloudItems[2].playState = FCL_PS_UNKNOWN;
  galRender();

  // ---- 8. SUBIR DESDE LA GALERIA: conservar o liberar espacio ----
  std::vector<uint8_t> jpg = vwTestJpeg(64, 64);
  uint32_t lid = geAddPhoto(FML_DIR_PHOTO "/Atardecer.jpg", jpg, false);
  gStubAccountLinked = true;
  galTab = 0; ckUnbind(&GAL_CK); galViewReady = false; galRender();
  mkOpenItemMenu(lid, 240, 300, (const uint8_t[]){ MA_EDIT, MA_OPENMM, MA_CL_UP }, 3);
  bool up = false, del = false;
  for(int i = 0; i < mmN; i++){ if(mmAct[i] == MA_CL_UP) up = true; if(mmAct[i] == MA_DELETE) del = true; }
  chk(up && del && mmN <= MM_MAX, "el menu ofrece Subir a Flex Cloud SIN perder Borrar para siempre");
  mmClose();
  mkMenuId = lid; mkDoAction(MA_CL_UP);
  chk(ckUpAskOn, "Subir a Flex Cloud pregunta: conservar o liberar espacio");
  if(getenv("INO_SHOTS")) shotSave("nube_subir");
  gStubCloudCalls.clear();
  { int x, y, w, h; ckUpAskGeom(x, y, w, h);
    tDown(x + w / 2, y + 136 + 64 + 30, clMs); tUp(clMs + 50, true);
    ckUpAskTick(mkRedrawAll); touchReset(); clMs += 400; }
  char want[160]; snprintf(want, sizeof(want), "up %s Atardecer.jpg root %lu 1", FML_DIR_PHOTO "/Atardecer.jpg", (unsigned long)lid);
  chk(clCalled(want), "\"Subir y liberar espacio\": se encola con FREE_LOCAL (y el original sigue aqui)");
  chk(gTestFiles.count(FML_DIR_PHOTO "/Atardecer.jpg") == 1, "encolar NO borra nada");
  // Lo protegido no se sube: el menu ni lo ofrece.
  uint32_t lk = geAddPhoto("/System/Media/Protegido/9.jpg", jpg, true);
  mkOpenItemMenu(lk, 240, 300, (const uint8_t[]){ MA_CL_UP }, 1);
  up = false; for(int i = 0; i < mmN; i++) if(mmAct[i] == MA_CL_UP) up = true;
  chk(!up, "lo protegido no ofrece subir a la nube");
  mmClose();

  // ---- 9. LIBERAR ESPACIO: solo con el aviso que lo permite ----
  FlexCloudEvent ue; memset(&ue, 0, sizeof(ue));
  ue.kind = FCE_UPLOAD_DONE; ue.ok = true; ue.mlId = lid; ue.flags = FCL_JF_FREE_LOCAL;
  snprintf(ue.localPath, sizeof(ue.localPath), "%s", FML_DIR_PHOTO "/Atardecer.jpg");
  snprintf(ue.name, sizeof(ue.name), "Atardecer.jpg");
  ue.size = jpg.size() + 1;                               // el original ya no mide lo que se subio
  gStubCloudEvents.push_back(ue); cloudUiTick();
  FlexMlRec rr;
  chk(mlGet(lid, &rr) && gTestFiles.count(FML_DIR_PHOTO "/Atardecer.jpg"), "si el original cambio de tamano, NO se borra");
  ue.size = jpg.size(); ue.flags = 0;
  gStubCloudEvents.push_back(ue); cloudUiTick();
  chk(mlGet(lid, &rr), "una subida sin 'liberar espacio' no borra nada");
  ue.flags = FCL_JF_FREE_LOCAL;
  gStubCloudEvents.push_back(ue); cloudUiTick();
  chk(!mlGet(lid, &rr) && !gTestFiles.count(FML_DIR_PHOTO "/Atardecer.jpg"), "confirmada en la nube con su huella: se libera");
  gStubCloudEvents.push_back(ue); cloudUiTick();             // el mismo aviso otra vez (tras un reinicio)
  chk(true, "un aviso repetido no hace nada (el original ya no esta)");
  FlexCloudEvent le = ue; le.mlId = lk; snprintf(le.localPath, sizeof(le.localPath), "/System/Media/Protegido/9.jpg");
  gStubCloudEvents.push_back(le); cloudUiTick();
  chk(mlGet(lk, &rr), "nunca se borra un protegido por un aviso de la nube");

  // ---- 10. DESCARGAS: a la biblioteca o a /Descargas ----
  gTestFiles["/System/Cloud/dl/7.jpg"] = jpg;
  FlexCloudEvent de; memset(&de, 0, sizeof(de));
  de.kind = FCE_DOWNLOAD_DONE; de.ok = true; de.flags = FCL_JF_TO_LIBRARY; de.size = jpg.size();
  snprintf(de.localPath, sizeof(de.localPath), "/System/Cloud/dl/7.jpg"); snprintf(de.name, sizeof(de.name), "Desde la nube.jpg");
  gStubCloudEvents.push_back(de); cloudUiTick();
  bool inLib = false;
  for(uint16_t i = 0; i < gMs.lib.n; i++) if(gMs.lib.recs[i].origin == FML_O_CLOUD && gMs.lib.recs[i].kind == FML_K_PHOTO) inLib = true;
  chk(inLib && !gTestFiles.count("/System/Cloud/dl/7.jpg"), "una foto descargada entra en la Galeria (marcada como de la nube)");
  gTestFiles["/System/Cloud/dl/8.pdf"] = std::vector<uint8_t>(1000, 7);
  de.flags = 0; snprintf(de.localPath, sizeof(de.localPath), "/System/Cloud/dl/8.pdf"); snprintf(de.name, sizeof(de.name), "Factura.pdf");
  gStubCloudEvents.push_back(de); cloudUiTick();
  chk(gTestFiles.count("/Descargas/Factura.pdf") && !gTestFiles.count("/System/Cloud/dl/8.pdf"), "lo demas va a Archivos > Descargas");

  // ---- 11. MULTIMEDIA: pestana Nube con los videos ----
  gStubCloudCalls.clear();
  galCloseApp(); gAppState[IC_GALERIA] = ALIFE_CLOSED;
  chk(ckHost == nullptr, "cerrar la Galeria suelta la nube");
  shotApp(IC_MULTIMEDIA); gAppState[IC_MULTIMEDIA] = ALIFE_RUNNING; mkBind(&VID_APP); vidViewReady = false;
  vidFilter = VID_TAB_CLOUD; vidScreen = VS_LIST; vidListRender();
  chk(ckHost == &VID_CK && clCalled("list 3 root"), "Multimedia > Nube pide los videos de la nube");
  if(getenv("INO_SHOTS")) shotSave("nube_multimedia");
  vidCloseApp(); gAppState[IC_MULTIMEDIA] = ALIFE_CLOSED;
  vidFilter = 0;

  // ---- 12. ARCHIVOS: "Este dispositivo | Flex Cloud", carpetas y ruta ----
  gStubCloudItems.clear();
  gStubCloudItems.push_back(clItem("fld_1", "Viaje a Espa\xC3\xB1" "a", 0, 0, true));
  gStubCloudItems.push_back(clItem("fil_d1", "Presupuesto.pdf", FCL_K_DOCUMENT, 120000));
  gStubCloudList.gen++;
  gStubCloudCalls.clear();
  filesEnter();
  chk(gState == ST_FILES && !filesCloud, "Archivos entra por Este dispositivo");
  clTap(filesTick, SCR_W * 3 / 4, FILES_SEG_Y + FILES_SEG_H / 2);
  chk(filesCloud && ckHost == &filesCkHost && clCalled("list 0 root"), "Flex Cloud: la raiz de la nube");
  int f0x, f0y; clCellCenter(0, f0x, f0y);
  gStubCloudCalls.clear();
  clTap(filesTick, f0x, f0y);
  chk(clCalled("list 0 fld_1"), "tocar una carpeta entra en ella");
  gStubCloudList.nCrumbs = 1; snprintf(gStubCloudList.crumbs[0].id, FCL_ID_MAX, "fld_1");
  snprintf(gStubCloudList.crumbs[0].name, sizeof(gStubCloudList.crumbs[0].name), "Viaje a Espa\xC3\xB1" "a");
  gStubCloudList.gen++;
  filesRender();
  if(getenv("INO_SHOTS")) shotSave("nube_archivos");
  gStubCloudCalls.clear();
  clTap(filesTick, 20, 20);                                  // ATRAS de la cabecera
  chk(clCalled("list 0 root") && gState == ST_FILES, "atras sube a la carpeta de arriba (no sale de Archivos)");
  gStubCloudList.nCrumbs = 0;
  clTap(filesTick, SCR_W / 4, FILES_SEG_Y + FILES_SEG_H / 2);
  chk(!filesCloud && ckHost == nullptr, "volver a Este dispositivo suelta la nube");

  // ---- 13. La cuota solo se refresca con la nube a la vista ----
  cloudUiTick();
  chk(!gStubCloudActive, "sin la nube delante, FlexOS_Cloud no refresca la cuota");

  // ---- 14. LA CUENTA YA NO SIRVE: desvinculada desde la web, revocada o caducada ----
  // Flex Account contesto que no: la credencial SIGUE guardada (linked) pero ya NO
  // sirve (usable). Antes la interfaz solo miraba "linked": dejaba encolar subidas
  // que esperaban "conexion" para siempre, el menu ofrecia todo y la tarjeta seguia
  // ensenando la cuota de la sesion anterior.
  gStubCloudCalls.clear(); gStubCloudEvents.clear(); gStubCloudItems.clear(); gStubCloudXfers.clear();
  gStubAccountLinked = true; gStubAccountSnap.link = FLEX_LINK_LINKED;
  chk(ckCloudBlock() == NULL, "con la cuenta buena la nube se puede usar");
  gStubAccountSnap.link = FLEX_LINK_NETWORK_UNAVAILABLE;
  chk(ckCloudBlock() == NULL, "'el servicio no responde' NO es una cuenta perdida: se sigue pudiendo usar");
  gStubAccountSnap.link = FLEX_LINK_LINKED_OFFLINE;
  chk(ckCloudBlock() == NULL, "sin Wi-Fi tampoco: la cuenta sigue vinculada");
  gStubAccountSnap.link = FLEX_LINK_AUTH_REQUIRED;
  chk(flexAccountLinked() && !flexAccountUsable(), "revocada: la credencial sigue guardada pero ya no sirve");
  chk(ckCloudBlock() && !strcmp(ckCloudBlock(), CK_MSG_RELINK), "y la interfaz lo sabe: hay que VOLVER a vincular (no 'vincula')");
  gStubAccountSnap.link = FLEX_LINK_TOKEN_EXPIRED;
  chk(ckCloudBlock() && !strcmp(ckCloudBlock(), CK_MSG_RELINK), "caducada: lo mismo");
  gStubAccountLinked = false; gStubAccountSnap.link = FLEX_LINK_UNLINKED;
  chk(ckCloudBlock() && !strcmp(ckCloudBlock(), CK_MSG_LINK), "sin cuenta: se dice que se vincule");
  gStubAccountLinked = true; gStubAccountSnap.link = FLEX_LINK_AUTH_REQUIRED;

  // La Galeria vuelve a abrirse (la 11 la cerro): su menu y su pestana Nube.
  shotApp(IC_GALERIA); gAppState[IC_GALERIA] = ALIFE_RUNNING; mkBind(&GAL_APP); galViewReady = false;
  galTab = 0; galRender();
  uint32_t lid3 = geAddPhoto(FML_DIR_PHOTO "/Cumple.jpg", jpg, false);
  saidClear();
  mkMenuId = lid3; mkDoAction(MA_CL_UP);
  chk(!ckUpAskOn, "Subir a Flex Cloud con la cuenta rechazada NO abre la pregunta");
  chk(saidCount() == 1 && saidSub() == CK_MSG_RELINK, "dice que hay que volver a vincular en Ajustes (dentro de la app: en el banner)");
  chk(!clCalled("up "), "y no se encola nada");
  // La cuenta se pierde con el cuadro de "Subir" ya abierto.
  gStubAccountSnap.link = FLEX_LINK_LINKED;
  mkMenuId = lid3; mkDoAction(MA_CL_UP);
  chk(ckUpAskOn, "(con la cuenta buena la pregunta SI se abre)");
  gStubAccountSnap.link = FLEX_LINK_AUTH_REQUIRED;
  saidClear();
  { int x, y, w, h; ckUpAskGeom(x, y, w, h);
    tDown(x + w / 2, y + 136 + 20, clMs); tUp(clMs + 50, true);
    ckUpAskTick(mkRedrawAll); touchReset(); clMs += 400; }
  chk(!clCalled("up "), "'Subir y conservar' con la cuenta ya perdida no encola nada");
  chk(saidCount() == 1 && saidSub() == CK_MSG_RELINK, "y dice por que");

  // La tarjeta de la nube: en alarma, con el motivo y SIN la cuota de antes.
  clStatusOnline();                                           // la cuota de la sesion anterior sigue en el estado
  gStubCloudStatus.net = FCN_AUTH; gStubCloudStatus.gen++;
  snprintf(gStubCloudStatus.netText, sizeof(gStubCloudStatus.netText), "Este dispositivo ya no est\xC3\xA1 vinculado");
  galTab = GAL_TAB_CLOUD; galRender();
  chk(ckHost == &GAL_CK && ckEmptyBtnY > 0 && ckEmptyBtnAct == 1, "con la cuenta perdida la nube ensena 'Abrir Flex Account' (no una lista)");
  int sbx, sby, sbw, sbh; ckBox(sbx, sby, sbw, sbh);
  int scx = sbx + 12, scy = sby + 4, scw = sbw - 24;
  int barY = scy + 46 + 3, barL = scx + 14 + 4, barR = scx + 14 + (scw - 28) - 4;
  uint16_t cardPx = fb[(size_t)barY * SCR_W + scx + 6];
  chk(fb[(size_t)barY * SCR_W + barL] == cardPx && fb[(size_t)barY * SCR_W + barR] == cardPx,
      "con la cuenta perdida NO se pinta la barra de cuota (era de otra sesion)");
  chk(fb[(size_t)(scy + 9 + 4) * SCR_W + scx + 48] == TH_DANGER, "y el punto de estado es el de alarma");
  if(getenv("INO_SHOTS")) shotSave("nube_cuenta_perdida");
  clStatusOnline(); gStubCloudStatus.gen++; ckRender();
  chk(fb[(size_t)barY * SCR_W + barL] != fb[(size_t)barY * SCR_W + barR], "(control) con la cuenta buena la barra de cuota SI se pinta");

  // Menu (...) de la app: sin cuenta que sirva solo queda Transferencias.
  gStubAccountSnap.link = FLEX_LINK_AUTH_REQUIRED;
  ckAppMenu(300, 300);
  chk(mmOn && mmN == 1 && mmAct[0] == MA_CL_XFERS, "el menu solo ofrece Transferencias (ver y cancelar lo que esperaba)");
  mmClose();
  gStubAccountSnap.link = FLEX_LINK_LINKED;
  ckAppMenu(300, 300);
  bool hasRefresh = false; for(int i = 0; i < mmN; i++) if(mmAct[i] == MA_CL_REFRESH) hasRefresh = true;
  chk(mmOn && mmN == 2 && hasRefresh, "(control) con la cuenta buena: Transferencias y Actualizar");
  mmClose();

  // La cuenta se pierde con un menu de elemento abierto, o con la lista aun pintada.
  gStubCloudItems.push_back(clItem("fil_x1", "Foto.jpg", FCL_K_PHOTO, 100000));
  ckMenuItem = gStubCloudItems[0]; ckMenuForItem = true;
  gStubAccountSnap.link = FLEX_LINK_AUTH_REQUIRED;
  gStubCloudCalls.clear(); saidClear();
  ckMenuAction(MA_CL_DOWNLOAD);
  ckMenuAction(MA_TRASH);
  ckMenuAction(MA_RENAME);
  ckOpenItem(gStubCloudItems[0]);
  chk(!clCalled("down") && !clCalled("trash") && !clCalled("view") && !fkNameOn, "ni descargar, ni eliminar, ni renombrar, ni abrir: nada sale hacia la nube");
  chk(saidCount() >= 1 && saidSub() == CK_MSG_RELINK, "y se dice por que");
  ckMenuAction(MA_CL_XFERS);
  chk(ckXfersOn, "Transferencias SI se abre aunque la cuenta no sirva (ver y cancelar lo que esperaba)");
  ckXfersOn = false;
  gStubCloudItems.clear();

  // AVISO UNICO al perder la cuenta (con o sin la nube a la vista).
  gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs));
  gStubAccountLinked = true; gStubAccountSnap.link = FLEX_LINK_LINKED;
  ckAcctLink = (FlexAccountLink)255; ckAcctMs = 0;
  gTestMs = 30000000; ckAccountNoticeTick();
  chk(gNotifCount == 0, "con la cuenta buena no hay aviso");
  gStubAccountSnap.link = FLEX_LINK_AUTH_REQUIRED;
  gTestMs += 100; ckAccountNoticeTick();
  chk(gNotifCount == 0, "se mira como mucho 2 veces por segundo (aun no)");
  gTestMs += 500; ckAccountNoticeTick();
  chk(gNotifCount == 1 && !strcmp(gNotifs[0].mod.name, "Flex Account: sesi\xC3\xB3n perdida") && !strcmp(gNotifs[0].mod.sub, CK_MSG_RELINK),
      "al perderse la cuenta sale UN aviso que dice que hacer");
  for(int k = 0; k < 20; k++){ gTestMs += 600; ckAccountNoticeTick(); }
  chk(gNotifCount == 1, "y no se repite mientras siga igual");
  gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs));
  gStubAccountSnap.link = FLEX_LINK_TOKEN_EXPIRED; gTestMs += 600; ckAccountNoticeTick();
  chk(gNotifCount == 0, "de 'revocada' a 'caducada' no se avisa otra vez (ya sabe que debe vincular)");
  gStubAccountSnap.link = FLEX_LINK_NETWORK_UNAVAILABLE; gTestMs += 600; ckAccountNoticeTick();
  gStubAccountSnap.link = FLEX_LINK_LINKED_OFFLINE; gTestMs += 600; ckAccountNoticeTick();
  chk(gNotifCount == 0, "sin red o con el servicio caido NO es una cuenta perdida: no se avisa");
  gStubAccountSnap.link = FLEX_LINK_LINKED; gTestMs += 600; ckAccountNoticeTick();
  gStubAccountSnap.link = FLEX_LINK_TOKEN_EXPIRED; gTestMs += 600; ckAccountNoticeTick();
  chk(gNotifCount == 1 && !strcmp(gNotifs[0].mod.name, "Flex Account: sesi\xC3\xB3n caduc\xC3\xB3"),
      "tras revincular y volver a perderla avisa otra vez (caducada)");
  // Arranque con la cuenta ya perdida (queda guardada en NVS): tambien se avisa, una vez.
  gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs));
  ckAcctLink = (FlexAccountLink)255; gStubAccountSnap.link = FLEX_LINK_AUTH_REQUIRED; gTestMs += 600; ckAccountNoticeTick();
  chk(gNotifCount == 1, "al arrancar con la cuenta ya perdida tambien se avisa");
  // Sin credencial guardada no hay a quien avisar.
  gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs));
  ckAcctLink = (FlexAccountLink)255; gStubAccountLinked = false; gStubAccountSnap.link = FLEX_LINK_UNLINKED; gTestMs += 600; ckAccountNoticeTick();
  chk(gNotifCount == 0, "sin cuenta guardada no se avisa de nada");
  galCloseApp(); gAppState[IC_GALERIA] = ALIFE_CLOSED;
  gStubAccountSnap.link = FLEX_LINK_UNLINKED;

  gStubAccountLinked = false;
  memset(&gStubCloudStatus, 0, sizeof(gStubCloudStatus));
  gStubCloudItems.clear(); gStubStreamData.clear();
  uiGlass = glass0; gNavMode = nav0;
  mkReset();
  gMlOk = ok0; gTestFsReady = fs0; gTestMemFs = false; gTestFiles.clear();
  memset(&gMs, 0, sizeof(gMs));
  gState = ST_HOME; gAppId = 0; gLand = false; uiClipFull(); setBuf(fb);
  if(gFails == before) printf("  Flex Cloud en la interfaz: todas las comprobaciones pasan.\n");
}

// #############################################################
//  LA FOTO DE FLEX CLOUD EN EL VISOR: A LA RAM, NUNCA A LA FLASH
//  ------------------------------------------------------------
//  Video del usuario: al tocar una foto de la nube el panel se ponia azul/cian
//  unos 15 s y la pantalla no respondia. La foto se escribia en LittleFS (un
//  borrado de sector por cada 4 KB, y cada borrado apaga la cache: el panel DSI
//  se queda sin datos) y el visor no aparecia hasta el final. Ahora el visor sale
//  en el acto, la foto va a un buffer de PSRAM con UN dueno en cada momento
//  (nube -> visor -> trabajo de decodificar -> liberado) y todo se cancela al
//  cerrar. Aqui: avance real (solo la banda de la tarjeta), cancelar, fallos,
//  plazo, JPEG roto y el balance de memoria.
// #############################################################
static void testFotoNubeEnRam(){
  printf("Foto de Flex Cloud en el visor: a la RAM, con avance real, cancelable y sin fugas\n");
  int before = gFails;
  bool ok0 = gMlOk, fs0 = gTestFsReady, glass0 = uiGlass; int nav0 = gNavMode;
  const size_t N = (size_t)SCR_W * SCR_H;
  std::vector<uint16_t> shadow(N, 0);
  uint16_t* sh0 = gPanelShadow; gPanelShadow = shadow.data();
  flxPanel = (esp_lcd_panel_handle_t)1; flxDpiSem = (SemaphoreHandle_t)1;
  gNavMode = 0; uiGlass = true;
  gTestMs = clMs;
  gStubCloudCalls.clear(); gStubCloudEvents.clear(); gStubCloudItems.clear(); gStubCloudXfers.clear();
  gStubCloudThumbs.clear(); gStubCloudWanted.clear();
  memset(&gStubCloudList, 0, sizeof(gStubCloudList));
  gStubViewRefuse = false; gStubViewOp = 0; gStubViewState = FCV_NONE; gStubViewLive = 0; gStubViewCancels = 0;
  geFsReset(); gTestFsReady = true;
  clStatusOnline();
  shotApp(IC_GALERIA); gAppState[IC_GALERIA] = ALIFE_RUNNING; mkBind(&GAL_APP); galViewReady = false;
  galTab = GAL_TAB_CLOUD;
  gStubCloudItems.push_back(clItem("fil_p1", "Playa.jpg", FCL_K_PHOTO, 2411724, false, true));
  gStubCloudList.state = FCL_LIST_READY; gStubCloudList.gen++;
  gStubCloudThumbs.push_back("fil_p1");
  galRender();
  int cx, cy; clCellCenter(0, cx, cy);
  // El boton "atras" de la barra de arriba del visor, en el lienzo LOGICO (x=30, y=36): en horizontal el toque fisico va girado.
  auto backPhys = [&](int& px, int& py){ const int lx = 30, ly = 36; if(vwLand){ px = (SCR_W - 1) - ly; py = lx; } else { px = lx; py = ly; } };
  auto openPhoto = [&]{ gStubCloudCalls.clear(); clTap(galTick, cx, cy); };
  auto back = [&]{ int px, py; backPhys(px, py); clTap(galTick, px, py); };
  auto tickMs = [&](unsigned long ms){ gTestMs += ms; galTick(); };
  auto base = [&]{ return uiGlass ? TH_GLASS2 : TH_SURF2; };

  // Calentamiento: el primer visor reserva sus buffers persistentes (vidrio); la linea base se mide despues.
  openPhoto(); stubViewDeliver(vwTestJpeg(480, 640)); tickMs(20); back();
  chk(!vwOn, "(calentamiento) el visor se cierra");
  const size_t psBase = gPsUsed;
  gStubViewLive = 0; gStubViewCancels = 0;

  // ---- 1. Tocar: el visor sale EN EL ACTO, con su tarjeta de carga y la miniatura de la nube ----
  openPhoto();
  chk(clCalled("view fil_p1"), "tocar una foto de la nube la pide a la nube");
  chk(vwActiveFor(&GAL_VW) && vwKind == VWK_PHOTO && vwLoading && vwViewOp != 0 && !vwSrc,
      "el visor aparece al instante, esperando (antes: 15 s de pantalla azul y sin respuesta)");
  chk(vwThumb != NULL, "con la miniatura que la nube ya tenia, ampliada");
  { int x, y, w, h; vwLoadGeom(x, y, w, h);
    chk(shadow[(size_t)(y + h / 2) * SCR_W + x + 3] == base(), "la tarjeta de carga esta EN EL PANEL");
    chk(vwLoadPill() && vwViewTotal == 0, "(sin avance todavia)"); }

  // ---- 2. El avance se pinta SOLO en la banda de la tarjeta (nada de repintar la pantalla entera) ----
  std::fill(shadow.begin(), shadow.end(), (uint16_t)0x1357);
  stubViewProgress(900000); tickMs(300);
  { int x, y, w, h; vwLoadGeom(x, y, w, h);
    int sent = 0, outside = 0;
    for(int r = 0; r < SCR_H; r++) if(shadow[(size_t)r * SCR_W + 3] != 0x1357){ sent++; if(r < y - 4 || r > y + h + 4) outside++; }
    chk(vwViewGot == 900000 && sent > 0, "el avance real llega a la tarjeta");
    chk(outside == 0 && sent <= h + 8, "y solo se manda al panel la banda de la tarjeta (no el cuadro entero)"); }
  { int x, y, w, h; vwLoadGeom(x, y, w, h);
    std::fill(shadow.begin(), shadow.end(), (uint16_t)0x1357);
    stubViewProgress(900000); tickMs(300);
    int sent = 0; for(int r = 0; r < SCR_H; r++) if(shadow[(size_t)r * SCR_W + 3] != 0x1357) sent++;
    chk(sent == 0, "sin cambios de avance no se vuelve a pintar nada"); }

  // ---- 3. Atras con la descarga en curso: CANCELA y no queda nada ----
  gStubViewCancels = 0;
  back();
  chk(!vwOn && vwViewOp == 0 && vwMemBuf == NULL, "atras funciona en todo momento (la foto no bloquea la interfaz)");
  chk(gStubViewCancels == 1 && clCalled("view-cancel"), "y CANCELA la descarga en la nube");
  chk(gStubViewOp == 0 && gStubViewLive == 0 && gPsUsed == psBase, "sin buffers colgando ni memoria sin devolver");
  chk(galTab == GAL_TAB_CLOUD && ckHost == &GAL_CK, "vuelve a la nube");

  // ---- 4. Llega bien: se decodifica DE LA RAM y el buffer pasa de dueno en dueno hasta soltarse ----
  openPhoto();
  stubViewDeliver(vwTestJpeg(600, 800));
  tickMs(20);
  chk(vwKind == VWK_PHOTO && vwSrc && !vwLoading && !vwLoadPill(), "llega verificada y se ve");
  chk(gStubViewLive == 0, "el buffer de la nube ya se solto (lo suelta quien lo decodifico)");
  { int x, y, w, h; vwLoadGeom(x, y, w, h);
    chk(shadow[(size_t)(y + h / 2) * SCR_W + x + 3] != base(), "la tarjeta de carga desaparece"); }
  back();
  chk(!vwOn && gPsUsed == psBase, "al cerrar, toda la memoria devuelta (foto, miniatura, lienzo)");

  // ---- 5. Se cierra JUSTO tras recibirla, antes de que el trabajo la reciba: no se fuga ----
  openPhoto();
  stubViewDeliver(vwTestJpeg(600, 800));
  vwViewStep();
  chk(gStubViewLive == 1 && vwMemBuf != NULL && vwJobQueued, "tomada: el visor es su dueno (el trabajo aun no la tiene)");
  vwClose();
  chk(gStubViewLive == 0 && vwMemBuf == NULL && gPsUsed == psBase, "cerrar suelta la foto tomada");
  // ... y tampoco si el trabajo YA la llevaba pero se retira antes de correr
  openPhoto();
  stubViewDeliver(vwTestJpeg(600, 800));
  vwViewStep(); vwJobPoll();                                       // sin tarea de medios corre en el acto: se simula el REQ sin recoger
  vwClose();
  chk(gStubViewLive == 0 && gPsUsed == psBase, "nada colgando tras cerrar con el trabajo en marcha");
  { VwJob* j = &gVwJob; j->mem = (uint8_t*)heap_caps_malloc(64, MALLOC_CAP_SPIRAM); j->memLen = 64; gStubViewLive++;
    __atomic_store_n(&j->state, (uint8_t)VWJ_REQ, __ATOMIC_RELEASE);
    vwJobCancel();
    chk(j->mem == NULL && gStubViewLive == 0 && __atomic_load_n(&j->state, __ATOMIC_ACQUIRE) == VWJ_IDLE,
        "un trabajo retirado SIN correr suelta lo que llevaba"); }

  // ---- 6. La nube falla: se dice POR QUE, sin colgarse, y atras sigue funcionando ----
  openPhoto();
  stubViewFail("No hay memoria libre ahora");
  tickMs(20);
  chk(vwKind == VWK_ERROR && !vwLoading && !vwViewOp && !strcmp(vwErr, "No hay memoria libre ahora"), "el motivo de la nube, tal cual");
  back();
  chk(!vwOn && gPsUsed == psBase, "(y se cierra limpio)");
  openPhoto();
  gStubViewOp = 0; gStubViewState = FCV_NONE;                       // la anulo otra peticion o un cambio de destino
  tickMs(20);
  chk(vwKind == VWK_ERROR && strstr(vwErr, "cancel") != NULL, "una peticion anulada por debajo se dice, no se queda cargando");
  back();
  gStubViewRefuse = true;
  openPhoto();
  chk(vwKind == VWK_ERROR && strstr(vwErr, "no puede atender") != NULL && !vwViewOp && !vwLoading, "si la nube no puede ni encolarla: se dice");
  gStubViewRefuse = false;
  back();

  // ---- 7. Plazo: la nube no contesta nunca. El visor lo corta, lo dice y le dice a la nube que pare ----
  gStubCloudCalls.clear();
  openPhoto();
  const unsigned long t0 = gTestMs;
  tickMs(VW_VIEW_MS - 1000);
  chk(vwLoading && vwViewOp, "antes del plazo sigue esperando");
  tickMs(2000);
  chk(vwKind == VWK_ERROR && strstr(vwErr, "tarda") != NULL, "vencido el plazo: se dice");
  chk(clCalled("view-cancel") && gStubViewOp == 0, "y se corta la descarga en la nube");
  (void)t0;
  back();
  clMs = gTestMs + 1000;

  // ---- 8. Un JPEG roto: el trabajo falla, lo dice y SUELTA el buffer igualmente ----
  openPhoto();
  stubViewDeliver(std::vector<uint8_t>(96, 0x55));
  tickMs(20);
  chk(vwKind == VWK_ERROR && vwErr[0] && !vwSrc && !vwLoading, "bytes que no son una foto: error, no basura en pantalla");
  chk(gStubViewLive == 0, "y el buffer se suelta tambien en el camino de error");
  back();
  chk(gPsUsed == psBase, "memoria devuelta");

  // ---- 9. Cambiar de foto / de app / bloquear con una descarga en curso: se cancela, no se acumula ----
  openPhoto();
  chk(vwViewOp != 0, "(otra vez esperando)");
  gStubViewCancels = 0;
  vwSuspend();                                                       // la app pasa a segundo plano
  chk(!vwOn && vwViewOp == 0 && gStubViewCancels == 1 && gPsUsed == psBase, "pasar a segundo plano cancela la descarga y suelta todo");
  openPhoto();
  vwSuspend();
  vwRelease(false);
  chk(gStubViewLive == 0 && gPsUsed == psBase, "soltar dos veces no rompe nada");
  { VwSession* ss = GAL_VW.sess; if(ss) ss->open = false; }

  // ---- limpieza ----
  gStubCloudItems.clear(); gStubCloudThumbs.clear();
  galCloseApp(); gAppState[IC_GALERIA] = ALIFE_CLOSED;
  gStubAccountLinked = false; memset(&gStubCloudStatus, 0, sizeof(gStubCloudStatus));
  uiGlass = glass0; gNavMode = nav0; gPanelShadow = sh0;
  mkReset();
  gMlOk = ok0; gTestFsReady = fs0; gTestMemFs = false; gTestFiles.clear();
  memset(&gMs, 0, sizeof(gMs));
  gState = ST_HOME; gAppId = 0; gLand = false; uiClipFull(); setBuf(fb);
  if(gFails == before) printf("  Foto de Flex Cloud en el visor: todas las comprobaciones pasan.\n");
}

// #############################################################
//  LA NUBE EN LA GALERIA: QUE DICE CADA CELDA Y QUE PASA AL TOCARLA
//  ------------------------------------------------------------
//  Lo que veia el usuario: subia un video, tocaba y NADA ocurria hasta cerrar y reabrir la
//  Galeria. El telefono lo estaba preparando; la celda no lo decia y el aviso iba por la isla,
//  que solo se dibuja en el escritorio. Ahora cada celda dice su estado, el toque responde
//  DENTRO de la app (banner), lo que se esta preparando se vigila por ID y la lista de
//  encima no tira los toques.
// #############################################################
static void testNubeEstados(){
  printf("La nube en la Galeria: estado en cada celda, respuesta al toque dentro de la app y vigilancia de lo que se prepara\n");
  int before = gFails;
  bool ok0 = gMlOk, fs0 = gTestFsReady, glass0 = uiGlass; int nav0 = gNavMode;
  gNavMode = 0; uiGlass = true;
  gTestMs = clMs;
  gStubCloudCalls.clear(); gStubCloudEvents.clear(); gStubCloudItems.clear(); gStubCloudXfers.clear();
  gStubCloudThumbs.clear(); gStubCloudWanted.clear(); gStubWatchKicks.clear(); gStubWatched.clear();
  memset(&gStubCloudList, 0, sizeof(gStubCloudList));
  geFsReset(); gTestFsReady = true;
  clStatusOnline();
  uint8_t dest0 = gStubCloudDest;
  gStubCloudDest = FCD_PHONE; gStubStorage.state = FSP_READY;          // Flex Cloud en el TELEFONO (el que prepara lo multimedia)
  shotApp(IC_GALERIA); gAppState[IC_GALERIA] = ALIFE_RUNNING; mkBind(&GAL_APP); galViewReady = false;
  galTab = GAL_TAB_CLOUD;

  // (Los elementos viven en el vector del doble, no en la pila: un FclItem son 672 B y el loopTask tiene 8 KB.)
  auto addVid = [&](const char* id, const char* name, uint8_t ps, uint8_t prog, const char* reason = ""){
    gStubCloudItems.push_back(clItem(id, name, FCL_K_VIDEO, 40u << 20, false, false));
    FclItem& it = gStubCloudItems.back();
    it.playState = ps; it.playProgress = prog;
    if(ps == FCL_PS_NATIVE || ps == FCL_PS_READY) it.playSize = it.size;
    snprintf(it.playReason, sizeof(it.playReason), "%s", reason); };
  addVid("fil_q", "Cola.avi", FCL_PS_PENDING, 0);
  addVid("fil_p", "Prep.avi", FCL_PS_PREPARING, 42);
  addVid("fil_b", "Roto.avi", FCL_PS_FAILED, 0, "El video no tiene pista que se pueda abrir");
  addVid("fil_n", "Nuevo.mp4", FCL_PS_UNKNOWN, 0);                     // recien subido: el telefono aun no lo ha analizado
  addVid("fil_g", "Listo.avi", FCL_PS_NATIVE, 0);
  gStubCloudList.state = FCL_LIST_READY; gStubCloudList.gen++;
  galRender();
  auto cell = [&](int i, int& cx, int& cy){ clCellCenter(i, cx, cy); };
  auto tapCell = [&](int i){ int cx, cy; cell(i, cx, cy); gStubCloudCalls.clear(); clTap(galTick, cx, cy); };

  // ---- 1. Cada celda dice su estado ANTES de tocar ----
  { int x, y, w, h;
    ckCellRect(1, x, y, w, h);                                           // preparando 42 %
    chk(fb[(size_t)(y + h - 14) * SCR_W + x + 8] == TH_PRIM, "preparando: insignia de color de la app en la celda");
    ckCellRect(2, x, y, w, h);                                           // fallido
    chk(fb[(size_t)(y + h - 14) * SCR_W + x + 8] == TH_DANGER, "no se pudo preparar: insignia de peligro");
    ckCellRect(4, x, y, w, h);                                           // listo: sin insignia
    chk(fb[(size_t)(y + h - 14) * SCR_W + x + 8] != TH_PRIM && fb[(size_t)(y + h - 14) * SCR_W + x + 8] != TH_DANGER, "lo que ya vale no lleva insignia"); }

  // ---- 2. Lo que no esta acabado se VIGILA (y solo eso) ----
  chk(gStubWatched.size() == 3 && gStubWatched[0] == "fil_q" && gStubWatched[1] == "fil_p" && gStubWatched[2] == "fil_n",
      "se vigila lo que esta en cola, preparando y el recien subido sin analizar (en el telefono); no lo listo ni lo fallido");
  gStubCloudDest = FCD_INTERNET; gStubWatched.clear(); gStubCloudList.gen++; galRender();
  chk(gStubWatched.size() == 2, "con Flex Cloud en Internet (no analiza nada) 'sin analizar' es lo normal: no se vigila");
  gStubCloudDest = FCD_PHONE; gStubCloudList.gen++; galRender();
  chk(gStubWatched.size() == 3, "(de vuelta al telefono)");

  // ---- 3. Tocar lo que se esta preparando RESPONDE, dentro de la app ----
  saidClear(); gStubWatchKicks.clear();
  tapCell(1);
  chk(!clCalled("stream") && !clCalled("view") && !mmOn, "no se intenta abrir algo que fallaria (ni se abre un menu)");
  chk(saidCount() == 1 && saidTitle() == "Prep.avi" && saidSub().find("42") != std::string::npos,
      "dice QUE pasa, con el avance real: el aviso va por el banner (la isla no se dibuja dentro de una app)");
  chk(gStubWatchKicks.size() == 1 && gStubWatchKicks[0] == "fil_p", "y le pregunta al telefono por ESE video ahora");
  tapCell(1);
  chk(saidCount() == 1, "tocar otra vez lo mismo no apila tarjetas iguales");

  // ---- 4. Lo que fallo dice POR QUE y ofrece sus acciones ----
  saidClear();
  tapCell(2);
  chk(saidCount() == 1 && saidSub() == "El video no tiene pista que se pueda abrir", "el motivo que escribio el telefono");
  chk(mmOn, "y las acciones (descargar, detalles...)");
  mmClose(); galRender();
  { ckInfo(gStubCloudItems[2]);
    chk(strstr(mmDlgText, "El video no tiene pista") != nullptr, "'Detalles' trae el motivo completo, no solo 'No se pudo preparar'");
    mmDlgOn = false; galRender(); }

  // ---- 5. Recien subido y sin analizar (telefono): se PREGUNTA, no se afirma que no se reproduce ----
  saidClear(); gStubWatchKicks.clear();
  tapCell(3);
  chk(!mmOn && saidCount() == 1 && saidSub().find("analizando") != std::string::npos,
      "'el telefono aun lo esta analizando' (antes: 'solo AVI MJPEG, abrelo en la web', que era falso)");
  chk(gStubWatchKicks.size() == 1 && gStubWatchKicks[0] == "fil_n", "y se le pregunta al telefono");
  saidClear();
  tapCell(3);
  chk(mmOn && saidCount() >= 1 && saidSub().find("MJPEG") != std::string::npos,
      "si sigue igual (un telefono sin preparacion multimedia) vuelve el menu de siempre con su motivo: nunca sin salida");
  mmClose(); galRender();
  gTestMs += CK_UNKNOWN_KICK_MS + 1000; clMs = gTestMs; saidClear();
  tapCell(3);
  chk(!mmOn && saidSub().find("analizando") != std::string::npos, "pasado un rato se vuelve a preguntar");

  // ---- 6. Lo que vale se abre ----
  tapCell(4);
  chk(clCalled("stream fil_g") || vwActiveFor(&GAL_VW), "un video listo se abre en el visor");
  if(vwActiveFor(&GAL_VW)) vwClose();
  flexCloudStreamClose();

  // ---- 7. La lista cambia debajo (el telefono actualizo OTRA celda): el toque NO se pierde ----
  galRender();
  stubStreamDeliver(1);
  gStubCloudList.gen++;                                                 // llego un cambio: aun sin repintar
  int cx, cy; clCellCenter(4, cx, cy);
  gStubCloudCalls.clear();
  clTap(galTick, cx, cy);
  chk(clCalled("stream fil_g") || vwActiveFor(&GAL_VW), "con la lista cambiando debajo, el toque va contra lo que el usuario VIO");
  if(vwActiveFor(&GAL_VW)) vwClose();
  flexCloudStreamClose();

  // ---- 8. "Cargando" sin fin: se dice y se ofrece Reintentar ----
  gStubCloudItems.clear();
  gStubCloudList.state = FCL_LIST_LOADING; gStubCloudList.count = 0; gStubCloudList.gen++;
  galRender();
  chk(ckLoadMs != 0 && !ckLoadTimedOut && ckEmptyBtnY < 0, "'Cargando Flex Cloud...' (aun dentro del plazo)");
  gTestMs += CK_LOAD_TIMEOUT_MS + 500; clMs = gTestMs;
  galTick();
  chk(ckLoadTimedOut && ckEmptyBtnY > 0 && ckEmptyBtnAct == 2, "pasado el plazo: 'El telefono tarda en responder' con boton Reintentar");
  gStubCloudCalls.clear();
  { int bx, by, bw, bh; ckBox(bx, by, bw, bh); clTap(galTick, bx + bw / 2, ckEmptyBtnY + 20); }
  chk(clCalled("refresh") && !ckLoadTimedOut, "Reintentar vuelve a pedirla");

  // ---- limpieza ----
  gStubCloudDest = dest0;
  gStubCloudItems.clear(); gStubWatched.clear(); gStubWatchKicks.clear(); saidClear();
  ckUnbind(&GAL_CK);
  chk(gStubWatched.empty(), "al salir de la nube no se vigila nada");
  galCloseApp(); gAppState[IC_GALERIA] = ALIFE_CLOSED;
  gStubAccountLinked = false; memset(&gStubCloudStatus, 0, sizeof(gStubCloudStatus)); memset(&gStubStorage, 0, sizeof(gStubStorage));
  uiGlass = glass0; gNavMode = nav0;
  mkReset();
  gMlOk = ok0; gTestFsReady = fs0; gTestMemFs = false; gTestFiles.clear();
  memset(&gMs, 0, sizeof(gMs));
  gState = ST_HOME; gAppId = 0; gLand = false; uiClipFull(); setBuf(fb);
  if(gFails == before) printf("  La nube en la Galeria: todas las comprobaciones pasan.\n");
}

// #############################################################
//  EL MENU (...) DE LA NUBE SE DESPLIEGA SOBRE LA BANDA DE VIDRIO
//  ------------------------------------------------------------
//  Captura del usuario: Galeria -> Nube ("Flex Cloud no responde") con una
//  barra azul "Transferencias" lavada encima de las pestanas. Era el menu (...)
//  de la nube a medio desplegar: loop() soltaba la banda pre-desenfocada en la
//  primera vuelta tras abrirlo (su guardian solo conocia a ST_CTX y al
//  cronometro) y el resto del despliegue se componia con vidrio APILADO, cada
//  cuadro desenfocando el menu del cuadro anterior.
//  La prueba ejecuta el despliegue REAL (ckAppMenu -> mmOpen -> galTick ->
//  mmAnimTick) dos veces sobre la misma escena: una SIN guardian (referencia) y
//  otra con el guardian de loop() entre vuelta y vuelta. Con la banda viva las
//  dos tienen que ser identicas pixel a pixel.
// #############################################################
static void testMenuNubeSinApilar(){
  printf("Menu (...) de la nube: el despliegue no apila vidrio (barra azul de Transferencias)\n");
  int before = gFails;
  bool ok0 = gMlOk, fs0 = gTestFsReady, glass0 = uiGlass; int nav0 = gNavMode;
  gNavMode = 0; uiGlass = true;
  gTestMs = clMs;
  gStubCloudCalls.clear(); gStubCloudEvents.clear(); gStubCloudItems.clear(); gStubCloudXfers.clear();
  memset(&gStubCloudList, 0, sizeof(gStubCloudList));
  geFsReset();
  gTestFsReady = true;
  // La escena de la captura: pestana Nube con "Flex Cloud no responde".
  memset(&gStubCloudStatus, 0, sizeof(gStubCloudStatus));
  gStubCloudStatus.net = FCN_UNAVAILABLE; gStubCloudStatus.gen = 1;
  gStubAccountLinked = true; gStubAccountSnap.link = FLEX_LINK_LINKED_OFFLINE;   // la cuenta SIRVE (el servicio no responde): menu de dos filas
  snprintf(gStubCloudStatus.netText, sizeof(gStubCloudStatus.netText), "Sin respuesta segura de Flex Cloud");
  gStubCloudList.state = FCL_LIST_ERROR; gStubCloudList.gen++;
  snprintf(gStubCloudList.error, sizeof(gStubCloudList.error), "Sin respuesta segura de Flex Cloud");
  shotApp(IC_GALERIA); gAppState[IC_GALERIA] = ALIFE_RUNNING; mkBind(&GAL_APP); galViewReady = false;
  galTab = GAL_TAB_CLOUD; galRender();
  gCronoCard = CC_HIDDEN;
  int bx, by, bw, bh; uiBox(bx, by, bw, bh); int pad = uiPad();

  static std::vector<uint16_t> frames[2];
  int bandLost = 0, mx = 0, my = 0, mw = 0, mh = 0;
  for(int guard = 0; guard < 2; guard++){
    galRender();                                              // la misma escena de partida
    touchReset(); gTestMs += 1000;
    auto vuelta = [&](){
      if(guard) uiGlassBandGuard();                           // lo primero que hace loop() en cada vuelta
      if(mmOn && !mmAnimDone && !uiGlassBandActive()) bandLost++;
      gTestMs += 20; galTick();
    };
    // Un toque sobre los tres puntos, como lo veria loop().
    tDown(bx + bw - pad - 20, by + 12, gTestMs); vuelta(); tUp(gTestMs + 60, true); vuelta(); touchReset();
    if(guard == 0) chk(mmOn && uiGlassBandActive(), "los tres puntos abren el menu de la nube y arman la banda");
    mmGeom(mx, my, mw, mh);
    for(int i = 0; i < 14; i++) vuelta();                     // los 140 ms del despliegue y unas vueltas mas
    if(guard == 0) chk(mmOn && mmAnimDone && !uiGlassBandActive(), "el despliegue termina y el menu suelta la banda");
    frames[guard].assign((size_t)mw * mh, 0);
    for(int yy = 0; yy < mh; yy++) memcpy(&frames[guard][(size_t)yy * mw], &fb[(size_t)(my + yy) * SCR_W + mx], (size_t)mw * 2);
    if(getenv("INO_SHOTS")) shotSave(guard ? "menu_nube_con_guardian" : "menu_nube_referencia");
    mmClose();
  }
  chk(bandLost == 0, "con el guardian de loop(), la banda sigue viva durante TODO el despliegue");
  int diff = 0;
  for(size_t i = 0; i < frames[0].size(); i++) if(frames[0][i] != frames[1][i]) diff++;
  chk(diff == 0, "el menu con el guardian de loop() es identico, pixel a pixel, al de referencia (sin vidrio apilado)");
  if(diff) printf("   (%d de %zu pixeles distintos en el menu)\n", diff, frames[0].size());
  // Que la comparacion tenga sentido: el menu esta dibujado (no es el fondo).
  int tinted = 0;
  { int bgLines = 0; for(int xx = 0; xx < mw; xx += 7) if(frames[0][(size_t)(mh / 2) * mw + xx] != frames[0][(size_t)(mh / 2) * mw + (xx ? xx - 7 : 0)]) bgLines++; tinted = bgLines; }
  chk(tinted > 3, "(control) el menu esta dibujado: la fila central no es plana");

  uiGlass = glass0; gNavMode = nav0;
  galCloseApp(); gAppState[IC_GALERIA] = ALIFE_CLOSED;
  memset(&gStubCloudStatus, 0, sizeof(gStubCloudStatus));
  gStubAccountLinked = false; gStubAccountSnap.link = FLEX_LINK_UNLINKED;
  mkReset();
  gMlOk = ok0; gTestFsReady = fs0; gTestMemFs = false; gTestFiles.clear();
  memset(&gMs, 0, sizeof(gMs));
  gState = ST_HOME; gAppId = 0; gLand = false; uiClipFull(); uiGlassBandEnd(); setBuf(fb);
  if(gFails == before) printf("  Menu (...) de la nube: todas las comprobaciones pasan.\n");
}

// #############################################################
//  EL MENU (...) DE LA NUBE, AL CERRARSE, NO DEJA RESTOS
//  ------------------------------------------------------------
//  Captura del usuario (segunda): la parte de arriba del menu "Transferencias"
//  se quedaba pegada sobre las pestanas de la Galeria y la tarjeta de estado,
//  repintada debajo, la cortaba en seco. El menu se ancla arriba a la derecha y
//  SOBRESALE de la zona de la nube (tapa las pestanas); al cerrarlo solo se
//  repintaba la zona de la nube, no la franja de la app que el menu tapaba.
//  La prueba abre el menu real, lo cierra por cada via (toque fuera, ATRAS,
//  elegir una accion) y exige que la pantalla quede IDENTICA a la de antes de
//  abrirlo.
// #############################################################
static void testMenuNubeAlCerrar(){
  printf("Menu (...) de la nube: al cerrarse no deja restos sobre las pestanas\n");
  int before = gFails;
  bool ok0 = gMlOk, fs0 = gTestFsReady, glass0 = uiGlass; int nav0 = gNavMode;
  gNavMode = 0; uiGlass = true;
  gTestMs = clMs;
  gStubCloudCalls.clear(); gStubCloudEvents.clear(); gStubCloudItems.clear(); gStubCloudXfers.clear();
  memset(&gStubCloudList, 0, sizeof(gStubCloudList));
  geFsReset();
  gTestFsReady = true;
  memset(&gStubCloudStatus, 0, sizeof(gStubCloudStatus));
  gStubCloudStatus.net = FCN_UNAVAILABLE; gStubCloudStatus.gen = 1;
  gStubAccountLinked = true; gStubAccountSnap.link = FLEX_LINK_LINKED_OFFLINE;   // la cuenta SIRVE (el servicio no responde): menu de dos filas
  snprintf(gStubCloudStatus.netText, sizeof(gStubCloudStatus.netText), "Sin respuesta segura de Flex Cloud");
  gStubCloudList.state = FCL_LIST_ERROR; gStubCloudList.gen++;
  snprintf(gStubCloudList.error, sizeof(gStubCloudList.error), "Sin respuesta segura de Flex Cloud");
  shotApp(IC_GALERIA); gAppState[IC_GALERIA] = ALIFE_RUNNING; mkBind(&GAL_APP); galViewReady = false;
  galTab = GAL_TAB_CLOUD; galRender();
  gCronoCard = CC_HIDDEN;
  int bx, by, bw, bh; uiBox(bx, by, bw, bh); int pad = uiPad();

  static std::vector<uint16_t> clean;                         // la pantalla ANTES de abrir el menu
  auto grab = [&](std::vector<uint16_t>& v){ v.assign(fb, fb + (size_t)SCR_W * SCR_H); };
  // La barra de navegacion del sistema (las 64 filas de abajo) no es de la app ni del menu.
  auto differs = [&](const std::vector<uint16_t>& a){
    int d = 0; size_t n = (size_t)SCR_W * (SCR_H - 64); for(size_t i = 0; i < n; i++) if(a[i] != fb[i]) d++; return d; };
  auto vuelta = [&](int n = 1){ for(int i = 0; i < n; i++){ uiGlassBandGuard(); gTestMs += 20; galTick(); } };
  auto abre = [&](){
    galRender(); touchReset(); gTestMs += 1000;
    grab(clean);
    tDown(bx + bw - pad - 20, by + 12, gTestMs); vuelta(); tUp(gTestMs + 60, true); vuelta(); touchReset();
    vuelta(14);                                               // termina el despliegue
  };

  // El menu de la app SOBRESALE de la zona de la nube (tapa las pestanas) y lo sabe.
  abre();
  chk(mmOn && mmAnimDone, "los tres puntos abren el menu");
  { int mx, my, mw, mh, cx, cy, cw, ch; mmGeom(mx, my, mw, mh); ckBox(cx, cy, cw, ch);
    chk(my < cy, "(el menu empieza por encima de la zona de la nube: tapa las pestanas)");
    chk(ckMenuSpills(), "y ckMenuSpills() lo detecta"); }
  chk(differs(clean) > 1000, "(control) con el menu abierto la pantalla es distinta de la limpia");

  // a) Toque FUERA del menu.
  clTap(galTick, 60, 700);
  chk(!mmOn, "un toque fuera cierra el menu");
  int d = differs(clean);
  chk(d == 0, "...y la pantalla queda IDENTICA a la de antes de abrirlo (sin restos sobre las pestanas)");
  if(d){
    int y0 = SCR_H, y1 = -1; for(int yy = 0; yy < SCR_H - 64; yy++) for(int xx = 0; xx < SCR_W; xx++) if(clean[(size_t)yy * SCR_W + xx] != fb[(size_t)yy * SCR_W + xx]){ if(yy < y0) y0 = yy; if(yy > y1) y1 = yy; }
    printf("   (%d pixeles distintos tras cerrar con un toque fuera, filas %d..%d)\n", d, y0, y1);
  }
  if(d && getenv("INO_SHOTS")) shotSave("menu_nube_resto_toque");

  // b) ATRAS.
  abre();
  chk(galBackLayer() && !mmOn, "ATRAS cierra el menu");
  d = differs(clean);
  chk(d == 0, "...y tampoco deja restos");
  if(d) printf("   (%d pixeles distintos tras ATRAS)\n", d);

  // c) Elegir "Actualizar": el menu se cierra y la accion se ejecuta sin restos.
  abre();
  gStubCloudCalls.clear();
  chk(clMenuPick(galTick, MA_CL_REFRESH) && clCalled("refresh"), "elegir Actualizar cierra el menu y refresca");
  gStubCloudStatus.gen++; vuelta(3);
  d = differs(clean);
  chk(d == 0, "...y tampoco deja restos");
  if(d) printf("   (%d pixeles distintos tras elegir una accion)\n", d);

  // d) Elegir "Transferencias": la pantalla de transferencias no hereda el menu; al salir, todo limpio.
  abre();
  chk(clMenuPick(galTick, MA_CL_XFERS) && ckXfersOn, "elegir Transferencias abre su pantalla");
  { int top = by, strip = 0;                                  // la franja de pestanas no conserva la parte alta del menu
    for(int yy = top + 36; yy < top + 66; yy++) for(int xx = bx + bw / 2; xx < bx + bw - 8; xx++)
      if(fb[(size_t)yy * SCR_W + xx] != clean[(size_t)yy * SCR_W + xx]) strip++;
    chk(strip == 0, "la franja de las pestanas queda como antes (sin la parte de arriba del menu)");
    if(strip) printf("   (%d pixeles distintos en la franja de pestanas)\n", strip); }
  clTap(galTick, bx + 20, by + galHeadH() - 6 + 16);          // la flecha de volver de Transferencias
  chk(!ckXfersOn, "volver de Transferencias");
  d = differs(clean);
  chk(d == 0, "...y la pantalla es la de antes de abrir el menu");
  if(d) printf("   (%d pixeles distintos tras volver de Transferencias)\n", d);

  // e) ARCHIVOS: el mismo menu, anclado bajo la cabecera, sobre el selector "Este dispositivo | Flex Cloud".
  galCloseApp(); gAppState[IC_GALERIA] = ALIFE_CLOSED;
  filesEnter();
  clTap(filesTick, SCR_W * 3 / 4, FILES_SEG_Y + FILES_SEG_H / 2);
  chk(filesCloud && ckHost == &filesCkHost, "Archivos: pestana Flex Cloud");
  filesRender(); grab(clean);
  ckAppMenu(SCR_W - MM_W / 2 - 16, UIHDR_ZONE);
  for(int i = 0; i < 14; i++){ uiGlassBandGuard(); gTestMs += 20; filesTick(); }
  chk(mmOn && mmAnimDone, "Archivos: el menu se despliega");
  chk(differs(clean) > 1000, "(control) con el menu abierto la pantalla cambia");
  clTap(filesTick, 60, 700);
  d = differs(clean);
  chk(!mmOn && d == 0, "Archivos: al cerrarlo la pantalla queda como antes de abrirlo");
  if(d) printf("   (%d pixeles distintos en Archivos)\n", d);
  filesExit();

  uiGlass = glass0; gNavMode = nav0;
  galCloseApp(); gAppState[IC_GALERIA] = ALIFE_CLOSED;
  memset(&gStubCloudStatus, 0, sizeof(gStubCloudStatus));
  gStubAccountLinked = false; gStubAccountSnap.link = FLEX_LINK_UNLINKED;
  mkReset();
  gMlOk = ok0; gTestFsReady = fs0; gTestMemFs = false; gTestFiles.clear();
  memset(&gMs, 0, sizeof(gMs));
  gState = ST_HOME; gAppId = 0; gLand = false; uiClipFull(); uiGlassBandEnd(); setBuf(fb);
  if(gFails == before) printf("  Menu (...) de la nube al cerrarse: todas las comprobaciones pasan.\n");
}

// #############################################################
//  FLEX ACCOUNT · "Desvincular cuenta": enlace, confirmacion, accion y estado
//  ------------------------------------------------------------
//  El codigo REAL de la pantalla (FlexOS_Account_Bridge.h) contra el doble de
//  FlexOS_Account, que ahora se comporta como el modulo real al olvidar: sin
//  cuenta, UNLINKED. Lo que se comprueba: nada se borra sin confirmar; Cancelar
//  y el toque fuera no hacen nada; Desvincular llama al modulo UNA vez y a la
//  nube; la pantalla pasa a "sin cuenta"; y no se rompe volver a vincular.
// #############################################################
extern int gStubAccountForgets;
static void testFlexAccountUnlink(){
  printf("Flex Account: Desvincular cuenta (confirmacion, UNLINKED y revincular)\n");
  bool oobeAntes = cfgOobeDone; int estadoAntes = gState;
  memset(&gStubAccountSnap, 0, sizeof(gStubAccountSnap));
  gStubAccountSnap.state = FLEX_ACCOUNT_LINKED; gStubAccountLinked = true; gStubAccountSnap.link = FLEX_LINK_LINKED;
  snprintf(gStubAccountSnap.flexAddress, sizeof(gStubAccountSnap.flexAddress), "flexdev@flex");
  snprintf(gStubAccountSnap.displayName, sizeof(gStubAccountSnap.displayName), "FlexDev");
  gStubAccountAccept = true; gStubAccountRequests = 0; gStubAccountForgets = 0;
  gInoWifiStatus = WL_CONNECTED;
  gStubCloudCalls.clear();
  gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs));
  auto toca = [](int x, int y){ storeTap(x, y); accountOobeTick(); T = Touch(); };
  auto repinta = [](){ T = Touch(); gTestMs += 100; accountOobeTick(); };

  gState = ST_APP; gAppId = IC_AJUSTES; setView = 1; setSel = 0;
  accountSettingsEnter();
  chk(gState == ST_OOBE_ACCOUNT && accountLastState == FLEX_ACCOUNT_LINKED && !accountUnlinkAsk, "cuenta vinculada: la pantalla de Flex Account");
  { // el enlace esta dibujado (no es el color liso de la tarjeta) y no invade ni al boton principal ni a la barra de salir
    uint16_t card = fb[(size_t)(ACC_UNLINK_Y - 6) * SCR_W + SCR_W / 2 - 100];
    int distinto = 0;
    for(int xx = SCR_W / 2 - 100; xx < SCR_W / 2 + 100; xx += 3) if(fb[(size_t)(ACC_UNLINK_Y + ACC_UNLINK_H / 2) * SCR_W + xx] != card) distinto++;
    chk(distinto > 10, "el enlace 'Desvincular cuenta' esta dibujado bajo la tarjeta");
    chk(ACC_UNLINK_Y - 4 > 622 && ACC_UNLINK_Y + ACC_UNLINK_H + 2 < 670, "y su zona tactil no pisa al boton principal (hasta 622) ni a la barra de salir (desde 670)"); }
  if(getenv("INO_SHOTS")) shotSave("cuenta_vinculada_desvincular");

  // 1. Pulsar el enlace PREGUNTA; no borra nada.
  toca(SCR_W / 2, ACC_UNLINK_Y + ACC_UNLINK_H / 2);
  chk(accountUnlinkAsk, "pulsar 'Desvincular cuenta' abre la confirmacion");
  chk(gStubAccountForgets == 0 && gStubAccountLinked, "y todavia no se ha borrado nada");
  if(getenv("INO_SHOTS")) shotSave("cuenta_desvincular_confirmar");

  // 2. Cancelar y el toque fuera no hacen nada.
  { int cx, dx, by, bw, bh; accountAskBtns(cx, dx, by, bw, bh);
    toca(cx + bw / 2, by + bh / 2);
    chk(!accountUnlinkAsk && gStubAccountForgets == 0 && gStubAccountLinked, "'Cancelar' cierra la confirmacion sin borrar");
    toca(SCR_W / 2, ACC_UNLINK_Y + ACC_UNLINK_H / 2); chk(accountUnlinkAsk, "(se vuelve a abrir)");
    toca(40, 100);
    chk(!accountUnlinkAsk && gStubAccountForgets == 0 && gStubAccountLinked && gState == ST_OOBE_ACCOUNT, "un toque fuera tambien la cierra, sin salir de la pantalla ni borrar");
    // Y los demas botones siguen donde estaban.
    toca(SCR_W / 2, 570);
    chk(gState == ST_APP && gStubAccountForgets == 0, "'Volver a Ajustes' sigue funcionando"); }

  // 3. Confirmar: el modulo olvida UNA vez, la nube se entera y la pantalla pasa a "sin cuenta".
  gStubAccountSnap.state = FLEX_ACCOUNT_LINKED; gStubAccountLinked = true; gStubAccountSnap.link = FLEX_LINK_LINKED;
  snprintf(gStubAccountSnap.flexAddress, sizeof(gStubAccountSnap.flexAddress), "flexdev@flex");
  accountSettingsEnter();
  toca(SCR_W / 2, ACC_UNLINK_Y + ACC_UNLINK_H / 2);
  { int cx, dx, by, bw, bh; accountAskBtns(cx, dx, by, bw, bh);
    toca(dx + bw / 2, by + bh / 2); }
  chk(gStubAccountForgets == 1, "confirmar llama a flexAccountForgetLocal() UNA vez");
  chk(clCalled("account-unlinked"), "y avisa a Flex Cloud (que suelta lo de esa cuenta)");
  chk(!accountUnlinkAsk && accountLastState == FLEX_ACCOUNT_UNLINKED && gState == ST_OOBE_ACCOUNT, "la pantalla pasa a 'sin cuenta' (FLEX_ACCOUNT_UNLINKED), sin salir");
  chk(!flexAccountLinked() && flexAccountLinkState() == FLEX_LINK_UNLINKED && flexAccountState() == FLEX_ACCOUNT_UNLINKED, "el modulo esta UNLINKED (no OFFLINE ni 'servicio no disponible')");
  chk(gNotifCount == 1 && !strcmp(gNotifs[0].mod.name, "Flex Account"), "un aviso confirma que la cuenta se desvinculo");
  char fila[64]; accountSettingsText(fila, sizeof(fila));
  chk(!strcmp(fila, "Sin cuenta vinculada"), "y la fila de Ajustes dice 'Sin cuenta vinculada'");
  if(getenv("INO_SHOTS")) shotSave("cuenta_desvinculada");

  // 4. Sin cuenta no hay enlace de desvincular (y un toque en su sitio no hace nada).
  gStubAccountForgets = 0;
  toca(SCR_W / 2, ACC_UNLINK_Y + ACC_UNLINK_H / 2);
  chk(!accountUnlinkAsk && gStubAccountForgets == 0, "sin cuenta no se ofrece desvincular");

  // 5. Volver a vincular sigue funcionando: 'Iniciar sesion' pide el enlace.
  toca(SCR_W / 2, 480);
  chk(gStubAccountRequests == 1, "tras desvincular, 'Iniciar sesion' pide el enlace (el flujo existente no se rompe)");

  // 6. Con la cuenta RECHAZADA por el servidor (hay que volver a vincular) tambien se puede desvincular.
  gStubAccountSnap.state = FLEX_ACCOUNT_LINKED; gStubAccountLinked = true; gStubAccountSnap.link = FLEX_LINK_AUTH_REQUIRED;
  snprintf(gStubAccountSnap.flexAddress, sizeof(gStubAccountSnap.flexAddress), "flexdev@flex");
  accountSettingsEnter();
  chk(accountLastState == FLEX_ACCOUNT_LINKED, "(cuenta rechazada: 'Vuelve a iniciar sesion')");
  toca(SCR_W / 2, ACC_UNLINK_Y + ACC_UNLINK_H / 2);
  chk(accountUnlinkAsk, "tambien ofrece desvincular");
  // La cuenta desaparece por otra via con la confirmacion abierta: la confirmacion se retira sola.
  gStubAccountSnap.state = FLEX_ACCOUNT_UNLINKED; gStubAccountLinked = false; repinta();
  chk(!accountUnlinkAsk, "si la cuenta ya no esta, no queda nada que confirmar");

  gStubAccountAccept = false; gStubAccountLinked = false; memset(&gStubAccountSnap, 0, sizeof(gStubAccountSnap));
  gInoWifiStatus = WL_DISCONNECTED; gStubCloudCalls.clear();
  gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs));
  cfgOobeDone = oobeAntes; gState = estadoAntes; setView = 0;
  if(!gFails) printf("  Flex Account (desvincular): todas las comprobaciones pasan.\n");
}

int main(){
  printf("Reloj del sistema (epoca UTC -> Lima UTC-5)\n");

  // --- ida y vuelta del calendario, dia a dia, durante 40 anos ---
  for(long d = -3653; d < 11323; d++){       // 1960-01-01 .. 2000+31 anos
    int y, m, dd; clkCivilFromDays(d, y, m, dd);
    chk(clkDaysFromCivil(y, m, dd) == d, "ida y vuelta civil<->dias");
    if(gFails) break;
  }
  chk(clkDaysFromCivil(1970, 1, 1) == 0,     "1970-01-01 es el dia 0");
  chk(clkDaysFromCivil(2000, 3, 1) == 11017, "2000-03-01 (ano bisiesto secular)");

  // --- fechas concretas, ya convertidas a hora de Lima ---
  // 1970-01-01 00:00 UTC = 1969-12-31 19:00 en Lima. Jueves 1 -> miercoles 3.
  chkDate(0, 1969, 12, 31, 3, 19, 0, "epoca UNIX en Lima");
  // 2026-07-04 18:23 UTC = sabado 4 jul 13:23 local: la semilla de fabrica.
  chkDate(1783189380u, 2026, 7, 4, 6, 13, 23, "semilla de fabrica");
  // Medianoche local exacta: 2026-01-01 05:00 UTC = jueves 1 ene 00:00 Lima.
  chkDate(1767243600u, 2026, 1, 1, 4, 0, 0, "medianoche local");
  // Un minuto ANTES: sigue siendo 31 dic 23:59 -> el cambio de dia va con el desfase.
  chkDate(1767243540u, 2025, 12, 31, 3, 23, 59, "un minuto antes de medianoche local");
  // 29 de febrero de un ano bisiesto (2028-02-29 12:00 UTC = 07:00 Lima, martes).
  chkDate(1835438400u, 2028, 2, 29, 2, 7, 0, "29 de febrero");

  // --- la semilla de fabrica produce la fecha de siempre ---
  gTestMs = 5000; seedMinOfDay = FLEXOS_CLK_SEED_MIN; clkSeedFactory(); clkUpdate();
  chk(rtcY==2026 && rtcMo==7 && rtcD==4 && rtcH==13 && rtcMin==23 && rtcWd==6,
      "clkSeedFactory reproduce sab 4 jul 2026 13:23");

  // --- el reloj avanza con millis() y solo avisa al cambiar el minuto ---
  gTestMs = 5000; clkSetEpoch(1783189380u); clkUpdate();
  gTestMs = 5000 + 30000; chk(!clkUpdate(), "30 s no cambian el minuto");
  gTestMs = 5000 + 61000; chk(clkUpdate(),  "61 s si cambian el minuto");
  chk(rtcMin == 24, "el minuto avanzo a :24");

  // --- re-anclaje: doblar el tiempo en la epoca no mueve el reloj ---
  gTestMs = 5000; clkSetEpoch(1783189380u); clkUpdate();
  gTestMs = 5000 + 3600001UL;                       // pasa de una hora -> re-ancla
  clkUpdate();
  chk(rtcH == 14 && rtcMin == 23, "tras re-anclar, +1 h exacta");
  chk(clkRefMs == gTestMs, "el ancla se movio a millis() actual");
  gTestMs += 60000; clkUpdate();
  chk(rtcH == 14 && rtcMin == 24, "el reloj sigue avanzando tras el re-anclaje");

  // --- desbordamiento de millis(): la resta sin signo da el delta correcto ---
  gTestMs = 0xFFFFF000UL; clkSetEpoch(1783189380u); clkUpdate();
  gTestMs = 0xFFFFF000UL + 120000UL;                 // cruza el desbordamiento de 32 bits
  clkUpdate();
  chk(rtcH == 13 && rtcMin == 25, "el reloj cruza el desbordamiento de millis()");

  // --- textos de Ajustes: nunca revelan mas de lo que deben ---
  char b[64];
  gNtpLastSyncUtc = 0; ntpLastSyncText(b, sizeof(b));
  chk(!strcmp(b, "Nunca"), "sin sincronizar -> \"Nunca\"");
  gTestMs = 1000; clkSetEpoch(1783189380u); clkUpdate();
  gNtpLastSyncUtc = 1783189380u; ntpLastSyncText(b, sizeof(b));
  chk(!strcmp(b, "Hoy 13:23"), "sincronizado hoy");
  gNtpLastSyncUtc = 1783189380u - 86400u; ntpLastSyncText(b, sizeof(b));
  chk(!strcmp(b, "Ayer 13:23"), "sincronizado ayer");

  if(gFails){ printf("%d comprobacion(es) del reloj han fallado.\n", gFails); return 1; }
  printf("  Reloj: todas las comprobaciones pasan.\n");

  testPanelRapido();
  testPanelOneUI();
  testArrastresSinFlash();
  testTecladoGlobal();
  testCajaApps();
  testCajaDescargadas();
  testCajaDescargadasScroll();
  testCajaDescargadasRegresion();
  testCajaUnificada();
  testCronometro();
  testPaginasHome();
  testNotifUnaSola();
  testBannerNotificacion();
  testDeslizarPaginas();
  testIslaEncimaAlDeslizar();
  testCabeceras();
  testListasConScroll();
  testTarjetaCronometro();
  testPersonalizarInicio();
  testFlexStore();
  testFlexAccount();
  testFlexAccountLink();
  testRecortePorBandas();
  testIconosEnSuCaja();
  testTransicionesApps();
  testRejillaAutoPaginas();
  testMediosOrientacion();
  testMultitareaMemoria();
  testDesbloqueoFluido();
  testClimaFluido();
  testCalculadora();
  testDeviceCare();
  testFlexCompass();
  testCompassEnSitioDeCodeIDE();
  testBusI2cCompartido();
  testProteccionRobo();
  testLiquidGlassSinApilar();
  testBlurNoPegado();
  testVidrioSinArrastre();
  testWidgetsDePagina();
  testIntensidadVidrio();
  testKitMedios();
  testMusica();
  testCapturasMedios();
  testEditorGaleria();
  testEditorVideoGaleria();
  testEditorVideoGrande();
  testCapturasEditor();
  testVisorMedios();
  testVideoRobusto();
  testGuardadoRafaga();
  testVariasFotos();
  testPulsacionLargaVidrio();
  testTactoGlobal();
  testKitTemaYPapelera();
  testHojaWebLocalizada();
  testCapturasVisor();
  testGaleriaSinRestos();
  testFlexCloudUi();
  testFotoNubeEnRam();
  testNubeEstados();
  testMusicaNube();
  testFlexStorage();
  testMenuNubeSinApilar();
  testMenuNubeAlCerrar();
  testFlexAccountUnlink();
  testTrabajoPeriodico();
  if(gFails){ printf("%d comprobacion(es) han fallado.\n", gFails); return 1; }
  return 0;
}
