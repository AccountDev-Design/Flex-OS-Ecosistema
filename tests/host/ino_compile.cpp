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

// Reloj virtual: las pruebas de abajo lo mueven a voluntad.
unsigned long gTestMs = 0;
static unsigned gDelayCalls = 0;
static unsigned gPinnedTaskCreates = 0;
static unsigned gSemTakeCalls = 0;
static unsigned gPanelDrawCalls = 0;
unsigned long millis(){ return gTestMs; }
// micros() SI avanza de verdad (reloj monotonico del PC). Lo usa la
// instrumentacion del Panel Rapido para medir el coste de un cuadro; con el
// doble de antes, que devolvia 0, esa medida no valia nada. El resto del
// sketch no usa micros(), asi que esto no altera ninguna otra prueba.
unsigned long micros(){
  struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
  return (unsigned long)(t.tv_sec * 1000000UL + t.tv_nsec / 1000UL);
}
void delay(unsigned long){}
void delayMicroseconds(unsigned long){}
void pinMode(int, int){}
void digitalWrite(int, int){}
int  digitalRead(int){ return 0; }
void yield(){}
long random(long m){ return m ? 0 : 0; }
long random(long a, long){ return a; }
uint32_t esp_random(){ return 0; }
bool setCpuFrequencyMhz(uint32_t){ return true; }
uint32_t getCpuFrequencyMhz(){ return 360; }

void*  heap_caps_malloc(size_t n, uint32_t){ return malloc(n); }
void*  heap_caps_calloc(size_t n, size_t s, uint32_t){ return calloc(n, s); }
void*  heap_caps_realloc(void* p, size_t n, uint32_t){ return realloc(p, n); }
void*  heap_caps_aligned_alloc(size_t a, size_t n, uint32_t){ return aligned_alloc(a, n); }
void   heap_caps_free(void* p){ free(p); }
size_t heap_caps_get_free_size(uint32_t){ return 8u << 20; }
size_t heap_caps_get_total_size(uint32_t){ return 32u << 20; }
size_t heap_caps_get_largest_free_block(uint32_t){ return 4u << 20; }
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
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t, int, int, int, int, const void*){ gPanelDrawCalls++; return ESP_OK; }
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

// El sketch entero, tal cual va a la placa.
#include "../../FlexOS_Ultra.ino"

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
extern bool gFlexOtaOwns;
static void testTecladoGlobal();
static void testCajaApps();
static void testCronometro();
static void testPaginasHome();
static void testNotifUnaSola();
static void testDeslizarPaginas();
static void testCabeceras();
static void testListasConScroll();
static void testTarjetaCronometro();
static void testPersonalizarInicio();
static void testFlexStore();
static void testFlexAccount();
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

static void testPanelOneUI(){
  printf("Panel rapido - rediseno One UI\n");
  gState = ST_HOME; gLand = false; gHosted = false; editMode = false;
  gTestMs = 200000;

  // ---- 1. REGISTRO: nada falso ----
  bool idsOk = true, ptrsOk = true, accionSinEstado = true;
  for(int i = 0; i < QSID_COUNT; i++){
    if(QS_REG[i].id != i) idsOk = false;
    if(i == QSID_RETIRED_13) continue;  // hueco NVS reservado; nunca se ofrece ni ejecuta
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
  for(int i = 0; i < drwN; i++) if(drwList[i] == id) return true;
  return false;
}
static void testCajaApps(){
  printf("Caja de aplicaciones\n");
  drwTestReset();

  // --- reparto de fabrica: exactamente el escritorio de siempre ---
  chk(gAppHidden == 0, "de fabrica no hay ninguna app oculta");
  for(int id = 0; id < 12; id++) chk(appIsFav(id),  "las doce de la rejilla nacen en Inicio");
  for(int id = 12; id < APP_N; id++) chk(!appIsFav(id), "las del dock y las nuevas no ocupan rejilla");
  // La caja ensena TODAS las del registro: se compara contra APP_N y no contra
  // un numero escrito a mano, para que anadir una app no obligue a tocar esto
  // (pero SI siga fallando si alguna se queda fuera de la caja).
  chk(drwN == APP_N, "la caja muestra todas las apps del registro");

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
  hpBuf = NULL; hpBg = NULL; hpBufPage = -1;

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

  // --- fuera de la banda de la rejilla el gesto es de otro ---
  gHomePage = 0;
  tDown(300, 100, 8000);                           // sobre los widgets
  tMove(260, 100, 8050);
  chk(!hpTryStart(), "sobre los widgets no se arrastran paginas");
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
                        ST_LOCKSETUP, ST_VAULT, ST_POWEROFF_CONFIRM, ST_POWEROFF_ANIM };
    for(unsigned k = 0; k < sizeof(seg) / sizeof(seg[0]); k++){
      gState = seg[k];
      chk(notifSecureScreen(), "ningun aviso se pinta sobre una pantalla sensible");
    }
    gState = ST_HOME; }

  DetectedModule a; memset(&a, 0, sizeof(a));
  a.active = true; a.type = MOD_I2C_GENERIC; a.i2cAddr = 0x18;
  snprintf(a.name, sizeof(a.name), "Dispositivo I2C");
  snprintf(a.sub, sizeof(a.sub), "0x18 detectado");

  // La cola queda congelada durante el alta del PIN y se arma al volver.
  gTestMs = 100000; gState = ST_LOCKSETUP;
  notifPush(&a);
  for(int f = 0; f < 30; f++){ gTestMs += 40; notifTick(); }
  chk(gNotifCount == 1 && !gNotifs[0].armed,
      "el aviso real espera sin caducar mientras se teclea el PIN");
  gState = ST_HOME; gTestMs += 40; notifTick();
  chk(gNotifs[0].armed && gNotifs[0].bornMs == (uint32_t)gTestMs,
      "su cuenta atras empieza al regresar al escritorio");

  // Repetir la misma deteccion refresca la tarjeta; no la apila.
  notifPush(&a); notifPush(&a);
  chk(gNotifCount == 1, "el rescaneo I2C no duplica el mismo dispositivo");
  snprintf(a.sub, sizeof(a.sub), "0x18 listo");
  notifPush(&a);
  chk(gNotifCount == 1 && !strcmp(gNotifs[0].mod.sub, "0x18 listo"),
      "un subtitulo nuevo refresca la tarjeta existente");
  a.i2cAddr = 0x76; a.type = MOD_BME280;
  snprintf(a.name, sizeof(a.name), "Sensor BME280");
  notifPush(&a);
  chk(gNotifCount == 2, "otro dispositivo real queda esperando en la cola");

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
  a.i2cAddr = 0x18; a.type = MOD_I2C_GENERIC;
  snprintf(a.name, sizeof(a.name), "Dispositivo I2C A"); notifPush(&a);
  a.i2cAddr = 0x76; a.type = MOD_BME280;
  snprintf(a.name, sizeof(a.name), "Sensor BME280 B"); notifPush(&a);
  bool segundaVisible = false;
  for(int f = 0; f < 360 && gNotifCount > 0; f++){
    gTestMs += 40; notifTick();
    if(gNotifCount == 1 && gNotifs[0].mod.i2cAddr == 0x76 && gNotifs[0].armed)
      segundaVisible = true;
  }
  chk(segundaVisible, "la segunda notificacion entra despues de la primera");
  chk(gNotifCount == 0 && !notifBandOn,
      "la ultima sale y limpia la banda sin congelar ni reiniciar el OS");

  // Reponer dos entradas para comprobar tambien la pausa de la Caja.
  a.i2cAddr = 0x18; a.type = MOD_I2C_GENERIC;
  snprintf(a.name, sizeof(a.name), "Dispositivo I2C A"); notifPush(&a);
  a.i2cAddr = 0x76; a.type = MOD_BME280;
  snprintf(a.name, sizeof(a.name), "Sensor BME280 B"); notifPush(&a);
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
  { const uint16_t COL_A = 0x1234, COL_B = 0x4321, VENENO = 0x7BEF;
    if(!hpEnsureBuf()){ chk(false, "hay lienzo para la pagina vecina"); }
    else {
      memset(homeBuf, 0, (size_t)SCR_W * SCR_H * 2);
      for(int y = HOME_BAND_TOP; y < homeBandBot(); y++) for(int x = 0; x < SCR_W; x++){
        uint16_t bg = (uint16_t)(0x0800u + (unsigned)x);
        homeBuf[(size_t)y * SCR_W + x] = bg;
        hpBg[(size_t)(y - HOME_BAND_TOP) * SCR_W + x] = bg;
        hpBuf[(size_t)(y - HOME_BAND_TOP) * SCR_W + x] = bg;
      }
      const int fy = HOME_GY0 + 12;
      for(int y = fy; y < fy + 18; y++){
        for(int x = 60; x < 92; x++) homeBuf[(size_t)y * SCR_W + x] = COL_A;
        for(int x = 100; x < 132; x++) hpBuf[(size_t)(y - HOME_BAND_TOP) * SCR_W + x] = COL_B;
      }
      hpBufPage = 1; hpFrom = 0; hpTo = 1;    // hacia la izquierda: la 1 entra por la derecha
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

  // --- 3. FUERA DE LA BANDA NO SE TOCA NADA ---
  // Barra de estado, widgets, dock y barra de navegacion son identicos
  // en las tres paginas: el gesto no puede escribir ahi.
  { const uint16_t MARCA = 0x0A0A;
    for(size_t i = 0; i < (size_t)SCR_W * SCR_H; i++) bbuf[i] = MARCA;
    hpBufPage = 1; hpFrom = 0; hpTo = 1;
    hpRenderFrame(-240);
    int tocados = 0;
    for(int y = 0; y < SCR_H; y++){
      if(y >= HOME_BAND_TOP && y < homeBandBot()) continue;
      for(int x = 0; x < SCR_W; x++)
        if(bbuf[(size_t)y * SCR_W + x] != MARCA) tocados++;
    }
    chk(tocados == 0, "widgets, barra superior, dock y navegacion no se tocan"); }

  // --- 4. NADIE MAS DIBUJA MIENTRAS DURA EL GESTO ---
  // La isla de notificaciones componia en bbuf las MISMAS filas y las
  // publicaba sin desplazar: media pantalla quedaba en la pagina vieja.
  { gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs));
    notifBandOn = false; notifPaused = false; notifLastMs = 0;
    gTestMs = 400000;
    DetectedModule m; memset(&m, 0, sizeof(m));
    m.active = true; m.type = MOD_I2C_GENERIC; m.i2cAddr = 0x22;
    snprintf(m.name, sizeof(m.name), "Dispositivo I2C");
    notifPush(&m);
    gState = ST_HOME; qsPanelY = 0; editMode = false;
    hpDragging = true;
    gTestMs += 40; notifTick();
    chk(!gNotifs[0].armed, "con un gesto de pagina en curso, la isla no dibuja");
    chk(notifPaused,       "y contabiliza la pausa para no comerse los 5 s");
    hpDragging = false;
    gTestMs += 40; notifTick();
    chk(gNotifs[0].armed,  "al acabar el gesto, la isla vuelve");
    gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs)); }

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
  chk(HP_BUF_PIXELS == (size_t)SCR_W * HOME_BAND_H,
      "la pagina vecina reserva exactamente la banda movil");
  chk(HP_BUF_PIXELS * 2 < (size_t)SCR_W * SCR_H * 2,
      "y no otro framebuffer completo de 768 KB");
  chk(HP_BUF_PIXELS * 4 < (size_t)SCR_W * SCR_H * 2,
      "pagina vecina mas fondo fijo siguen ocupando menos que una pantalla completa");
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
//  En el video de Flex Vault, al arrastrar la lista hacia arriba,
//  "Ultimo acceso", "Bloqueo automatico" e "Intentos fallidos" se
//  apilan unos sobre otros en la banda de la cabecera. La causa:
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

  // --- El recorte funciona en las dos direcciones ---
  { memset(bbuf, 0, (size_t)SCR_W * SCR_H * 2);
    setBuf(bbuf);
    uiClipViewport(VW_VP_TOP, SCR_H - 1);
    // Una tarjeta que empieza MUY por encima del viewport, como una fila
    // arrastrada hacia arriba, con sus textos en la banda de la cabecera.
    fillRoundRect(12, -90, SCR_W - 24, 176, 16, TINTA);
    drawText(26, -30, "Ultimo acceso", 1, TINTA);
    drawText(26, 10,  "Bloqueo automatico", 1, TINTA);
    int arriba = 0;
    for(int y = 0; y < VW_VP_TOP; y++)
      for(int x = 0; x < SCR_W; x++)
        if(bbuf[(size_t)y * SCR_W + x] != FONDO) arriba++;
    chk(arriba == 0, "una fila subida no escribe NI UN PIXEL en la cabecera");
    // Y lo que si cae dentro del viewport se dibuja: el recorte no
    // apaga la lista, solo la contiene.
    int dentro = 0;
    for(int y = VW_VP_TOP; y < SCR_H; y++)
      for(int x = 0; x < SCR_W; x++)
        if(bbuf[(size_t)y * SCR_W + x] != FONDO) dentro++;
    chk(dentro > 500, "y la parte que si entra en el viewport se ve"); }

  // --- Por abajo igual ---
  { memset(bbuf, 0, (size_t)SCR_W * SCR_H * 2);
    setBuf(bbuf);
    uiClipViewport(VW_VP_TOP, SCR_H - 120);
    fillRoundRect(12, SCR_H - 200, SCR_W - 24, 176, 16, TINTA);
    int abajo = 0;
    for(int y = SCR_H - 119; y < SCR_H; y++)
      for(int x = 0; x < SCR_W; x++)
        if(bbuf[(size_t)y * SCR_W + x] != FONDO) abajo++;
    chk(abajo == 0, "y tampoco por debajo del viewport"); }

  // --- El viewport deja libre la cabecera de Flex Vault ---
  chk(VW_VP_TOP >= 56,  "el viewport de Flex Vault empieza bajo su cabecera");
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
  { for(int i = 0; i < homeSlotCount(); i++) homeOrder[homeIdx(1, i)] = HOME_EMPTY;
    chk(homeWgAdd(1, WG_CLOCK) == 0, "un reloj 2x1 entra en una pagina vacia");
    chk(gHomeWgN[1] == 1, "y queda registrado");
    uint32_t m = homeCellMask(1, -1);
    chk((m & 1u) && (m & 2u), "ocupa sus dos celdas");
    chk(!(m & 4u), "y solo esas");
    chk(homeWgAdd(1, WG_CLOCK_A) == 0, "un reloj analogico 2x2 tambien cabe");
    chk(homeWgAdd(1, WG_CLIMA) == 0, "y un tercero");
    chk(homeWgAdd(1, WG_WIFI) == 1, "el cuarto no: tres por pagina");
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
  { hcTestReset();
    chk(homeWgAdd(0, WG_CLOCK) == 2, "en una pagina llena de iconos no hay hueco");
    chk(homeWgAdd(0, WG_WIFI)  == 2, "ni siquiera para uno de 1x1"); }

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
    chk(!homeWgDeserialize(bad), "un blob corrupto se rechaza entero"); }

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
    chk(!homeEmptySpaceAt(240, 100), "la banda de clima/calendario no");
    chk(homeFixedWidgetAppAt(HOME_FW_X + 20, HOME_FW_Y + 20) == IC_CLIMA,
        "el widget fijo izquierdo abre Clima");
    chk(homeFixedWidgetAppAt(HOME_CAL_X + 20, HOME_FW_Y + 20) == IC_CALEND,
        "el widget fijo derecho abre Calendario");
    chk(homeFixedWidgetAppAt(HOME_FW_X + HOME_FW_W + 4, HOME_FW_Y + 20) == -1,
        "el espacio entre tarjetas no abre una app por error");
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
  accountSettingsText(fila, sizeof(fila));
  chk(!strcmp(fila, "usuario@flex"), "con cuenta, muestra la direccion que trajo el modulo");

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
  testTecladoGlobal();
  testCajaApps();
  testCronometro();
  testPaginasHome();
  testNotifUnaSola();
  testDeslizarPaginas();
  testCabeceras();
  testListasConScroll();
  testTarjetaCronometro();
  testPersonalizarInicio();
  testFlexStore();
  testFlexAccount();
  if(gFails){ printf("%d comprobacion(es) han fallado.\n", gFails); return 1; }
  return 0;
}
