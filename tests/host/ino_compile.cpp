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
#include "Arduino.h"
#include "Wire.h"
#include "Preferences.h"
#include "WiFi.h"
#include "WiFiClientSecure.h"
#include "WiFiUdp.h"
#include "HTTPClient.h"
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
unsigned long micros(){ return 0; }
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
static void testNoticias();
static void testCajaApps();
static void testCronometro();
static void testPaginasHome();
static void testNotifUnaSola();
static void testDeslizarPaginas();
static void testCabeceras();
static void testListasConScroll();
static void testTarjetaCronometro();
static void testPersonalizarInicio();
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
//  PRUEBAS DEL SERVICIO DE NOTICIAS
//  ------------------------------------------------------------
//  El lector de JSON, la plantilla de URL, el historial de IDs, el
//  Centro persistente y el horario silencioso son logica pura: se
//  comprueban aqui de verdad, con respuestas realistas y con
//  respuestas rotas. Lo que NO se prueba aqui es el socket -- eso
//  necesita placa.
// #############################################################
static void testNoticias(){
  printf("Servicio de noticias\n");
  newsCfgDefaults();

  // --- respuesta tipica de una API publica ---
  static const char* J1 =
    "{\"status\":\"ok\",\"totalResults\":42,\"articles\":["
    "{\"id\":\"n1\",\"title\":\"Primera noticia\",\"source\":{\"name\":\"El Comercio\"},"
     "\"category\":\"peru\",\"publishedAt\":\"2026-08-10T14:05:00Z\",\"url\":\"https://x/1\"},"
    "{\"guid\":\"n2\",\"headline\":\"Segunda noticia\",\"fuente\":\"BBC\",\"section\":\"world\","
     "\"date\":\"2026-08-10T13:00:00Z\",\"breaking\":true},"
    "{\"title\":\"Tercera sin id ni fuente\"}"
    "]}";
  int n = newsParseBody(J1, strlen(J1));
  chk(n == 3, "se leen los tres articulos");
  chk(!strcmp(gNewsStage[0].title, "Primera noticia"), "titulo por \"title\"");
  chk(!strcmp(gNewsStage[0].source, "El Comercio"), "fuente por objeto {\"name\":...}");
  chk(gNewsStage[0].cat == 0, "categoria \"peru\"");
  chk(gNewsStage[0].when == 1786370700u, "fecha ISO-8601 -> epoca UTC");
  chk(!gNewsStage[0].important, "sin marca de urgente");
  chk(!strcmp(gNewsStage[1].title, "Segunda noticia"), "titulo por sinonimo \"headline\"");
  chk(!strcmp(gNewsStage[1].source, "BBC"), "fuente por sinonimo \"fuente\"");
  chk(gNewsStage[1].cat == 1, "categoria \"world\" -> Mundo");
  chk(gNewsStage[1].important, "urgente por sinonimo \"breaking\"");
  chk(!strcmp(gNewsStage[2].source, "Noticias"), "sin fuente -> valor por defecto");
  chk(gNewsStage[2].cat == 0xFF, "sin categoria -> sin clasificar");
  chk(gNewsStage[2].idHash != 0 && gNewsStage[2].idHash != gNewsStage[0].idHash,
      "sin id se deriva uno del titulo");
  // El mismo cuerpo dos veces da los MISMOS ids: es lo que sostiene el historial.
  uint32_t h0 = gNewsStage[0].idHash, h2 = gNewsStage[2].idHash;
  newsParseBody(J1, strlen(J1));
  chk(gNewsStage[0].idHash == h0 && gNewsStage[2].idHash == h2, "los ids son estables entre consultas");

  // --- fechas: formatos aceptados y basura rechazada ---
  chk(newsParseWhen("2026-08-10T14:05:00Z") == 1786370700u, "ISO-8601 completo con Z");
  chk(newsParseWhen("2026-08-10T14:05:00+00:00") == 1786370700u, "ISO-8601 con desfase (se lee como UTC)");
  chk(newsParseWhen("2026-08-10T14:05") == 1786370700u, "ISO-8601 sin segundos");
  chk(newsParseWhen("2026-08-10 14:05:00") == 1786370700u, "separador de espacio en vez de T");
  chk(newsParseWhen("2026-08-10") == 1786320000u, "solo fecha -> medianoche UTC");
  static const char* FECHAS_MALAS[] = {
    "", "hoy", "2026", "2026-08", "26-08-10", "2026-13-10T00:00:00Z",
    "2026-08-32T00:00:00Z", "1999-01-01T00:00:00Z", "2026-08-10T99:00:00Z",
    "2026-08-10TXX:05:00Z", "----------", "2026/08/10",
  };
  bool fechasOk = true;
  for(size_t i = 0; i < sizeof(FECHAS_MALAS) / sizeof(FECHAS_MALAS[0]); i++)
    if(newsParseWhen(FECHAS_MALAS[i]) != 0) fechasOk = false;
  chk(fechasOk, "una fecha ilegible da 0, no una fecha inventada");

  // --- etiquetas de categoria, sin distinguir mayusculas ---
  chk(newsParseCat("technology") == 2, "slug \"technology\"");
  chk(newsParseCat("TECHNOLOGY") == 2, "el slug no distingue mayusculas");
  chk(newsParseCat("Tech") == 2, "sinonimo \"tech\"");
  chk(newsParseCat("SPORTS") == 4, "slug \"sports\"");
  chk(newsParseCat("deporte") == 4, "sinonimo en castellano");
  chk(newsParseCat("cocina") == 0xFF, "una etiqueta desconocida no se fuerza a ninguna");
  chk(newsParseCat("") == 0xFF, "etiqueta vacia -> sin clasificar");

  // --- claves alternativas del array ---
  static const char* J2 = "{\"items\":[{\"title\":\"Con items\"}]}";
  chk(newsParseBody(J2, strlen(J2)) == 1, "array bajo \"items\"");
  static const char* J3 = "{\"results\":[{\"title\":\"Con results\"}]}";
  chk(newsParseBody(J3, strlen(J3)) == 1, "array bajo \"results\"");

  // --- el tope de 5 se respeta aunque el servicio mande mas ---
  {
    char big[2048]; int o = 0;
    o += snprintf(big + o, sizeof(big) - o, "{\"articles\":[");
    for(int i = 0; i < 12; i++)
      o += snprintf(big + o, sizeof(big) - o, "%s{\"id\":\"x%d\",\"title\":\"T%d\"}", i ? "," : "", i, i);
    snprintf(big + o, sizeof(big) - o, "]}");
    chk(newsParseBody(big, strlen(big)) == NEWS_MAX_RESULTS, "nunca se leen mas de 5 articulos");
  }

  // --- entradas hostiles: ni desbordan ni cuelgan ---
  {
    char largo[1024]; int o = 0;
    o += snprintf(largo + o, sizeof(largo) - o, "{\"articles\":[{\"title\":\"");
    for(int i = 0; i < 500; i++) largo[o++] = 'A';
    snprintf(largo + o, sizeof(largo) - o, "\"}]}");
    chk(newsParseBody(largo, strlen(largo)) == 1, "un titular larguisimo se acepta");
    chk(strlen(gNewsStage[0].title) == NEWS_TITLE_MAX - 1, "y se recorta al buffer");
    chk(gNewsStage[0].title[NEWS_TITLE_MAX - 1] == 0, "quedando terminado en cero");
  }
  static const char* ROTOS[] = {
    "", "{", "[]", "{\"articles\":", "{\"articles\":[", "{\"articles\":[{",
    "{\"articles\":[{\"title\":", "{\"articles\":[{\"title\":\"sin cerrar}]}",
    "{\"articles\":{\"title\":\"no es un array\"}}", "no es json en absoluto",
    "{\"articles\":[{\"source\":{\"name\":\"solo fuente\"}}]}",
  };
  bool rotosOk = true;
  for(size_t i = 0; i < sizeof(ROTOS) / sizeof(ROTOS[0]); i++)
    if(newsParseBody(ROTOS[i], strlen(ROTOS[i])) != 0) rotosOk = false;
  chk(rotosOk, "ningun JSON roto produce articulos");

  // --- plantilla de la URL ---
  snprintf(gNews.url, sizeof(gNews.url),
           "https://api.ej.com/v1?country={country}&category={category}&n={max}&apiKey={key}");
  snprintf(gNews.key, sizeof(gNews.key), "SECRETO1234");
  snprintf(gNews.country, sizeof(gNews.country), "pe");
  {
    char url[NEWS_URL_MAX + NEWS_KEY_MAX + 64];
    chk(newsBuildUrl(url, sizeof(url), 2), "la plantilla se construye");
    chk(!strcmp(url, "https://api.ej.com/v1?country=pe&category=technology&n=5&apiKey=SECRETO1234"),
        "los cuatro marcadores se sustituyen");
    char chico[16];
    chk(!newsBuildUrl(chico, sizeof(chico), 0), "una URL que no cabe se rechaza en vez de truncarse");
  }

  // --- historial de IDs: anillo de 64, sin falsos negativos recientes ---
  gNewsSeenN = 0; gNewsSeenHead = 0; memset(gNewsSeen, 0, sizeof(gNewsSeen));
  chk(!newsSeenHas(newsHash("a")), "un id nuevo no esta visto");
  newsSeenAdd(newsHash("a"));
  chk(newsSeenHas(newsHash("a")), "tras anadirlo, si esta visto");
  newsSeenAdd(newsHash("a"));
  chk(gNewsSeenN == 1, "anadir dos veces el mismo id no ocupa dos ranuras");
  for(int i = 0; i < NEWS_SEEN_MAX + 20; i++){ char k[8]; snprintf(k, sizeof(k), "id%d", i); newsSeenAdd(newsHash(k)); }
  chk(gNewsSeenN == NEWS_SEEN_MAX, "el historial se queda en 64 entradas");
  {
    bool recientes = true;
    for(int i = NEWS_SEEN_MAX + 20 - 60; i < NEWS_SEEN_MAX + 20; i++){
      char k[8]; snprintf(k, sizeof(k), "id%d", i);
      if(!newsSeenHas(newsHash(k))) recientes = false;
    }
    chk(recientes, "los 60 ids mas recientes siguen en el historial");
  }

  // --- Centro de noticias: 20, la mas nueva primero ---
  gNewsCount = 0; gNewsUnread = 0;
  for(int i = 0; i < NEWS_CENTER_MAX + 7; i++){
    NewsItem it; memset(&it, 0, sizeof(it));
    snprintf(it.title, sizeof(it.title), "N%d", i);
    it.idHash = newsHash(it.title);
    newsCenterPush(&it);
  }
  chk(gNewsCount == NEWS_CENTER_MAX, "el Centro se queda en 20 noticias");
  chk(!strcmp(gNewsCenter[0].title, "N26"), "la mas reciente queda la primera");
  chk(!strcmp(gNewsCenter[NEWS_CENTER_MAX - 1].title, "N7"), "y la mas vieja se cae del final");
  chk(gNewsUnread == NEWS_CENTER_MAX, "todas entran como no leidas");
  chk(!strcmp(newsTopTitle(), "N26"), "el widget lee el titular mas reciente");
  newsMarkAllRead();
  chk(gNewsUnread == 0 && newsUnreadCount() == 0, "marcar leidas pone el contador a cero");

  // --- horario silencioso, incluido el tramo que cruza medianoche ---
  gNews.quietOn = true;
  gNews.quietFromIdx = 1;   // 22:00
  gNews.quietToIdx   = 1;   // 07:00
  rtcH = 23; rtcMin = 30; chk(newsInQuiet(),  "23:30 cae dentro de 22:00-07:00");
  rtcH =  2; rtcMin =  0;  chk(newsInQuiet(),  "02:00 tambien (cruza medianoche)");
  rtcH =  6; rtcMin = 59;  chk(newsInQuiet(),  "06:59 aun es silencio");
  rtcH =  7; rtcMin =  0;  chk(!newsInQuiet(), "07:00 ya no");
  rtcH = 15; rtcMin =  0;  chk(!newsInQuiet(), "las tres de la tarde no");
  gNews.quietOn = false;
  rtcH = 23; rtcMin = 30;  chk(!newsInQuiet(), "con el horario apagado nunca hay silencio");

  // --- relevancia: importante > categoria activa > orden de llegada ---
  {
    NewsItem a, b, c;
    memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b)); memset(&c, 0, sizeof(c));
    gNews.cats = 0x1F;
    a.important = false; a.cat = 0xFF;      // ninguna ventaja
    b.important = false; b.cat = 2;         // categoria activa
    c.important = true;  c.cat = 0xFF;      // urgente
    chk(newsScore(&c, 4) > newsScore(&b, 0), "una urgente gana a una de categoria activa");
    chk(newsScore(&b, 4) > newsScore(&a, 0), "una de categoria activa gana a una sin clasificar");
    chk(newsScore(&a, 0) > newsScore(&a, 3), "a igualdad, gana la que llego antes");
  }

  // --- credenciales: enmascaradas, borrables y fuera de todo texto ---
  snprintf(gNews.url, sizeof(gNews.url), "https://api.ej.com/v1?apiKey={key}");
  snprintf(gNews.key, sizeof(gNews.key), "SECRETO1234");
  gNews.enabled = true;
  {
    char st[96];
    gNewsErr = NERR_HTTP; gNewsHttp = 401; gNewsBusy = false;
    newsStateText(st, sizeof(st));
    chk(!strstr(st, "SECRETO"), "el estado no filtra la clave");
    chk(!strstr(st, "api.ej.com"), "el estado no filtra la URL");
    chk(strstr(st, "401") != NULL, "pero si dice el codigo HTTP, que es diagnostico util");
  }
  newsWipeCreds();
  chk(gNews.url[0] == 0, "borrar credenciales vacia la URL");
  chk(gNews.key[0] == 0, "borrar credenciales vacia la clave");
  chk(!gNews.enabled, "y deja el servicio apagado");
  chk(!newsConfigured(), "sin URL, el servicio consta como no configurado");
  {
    char st[96]; newsStateText(st, sizeof(st));
    chk(strstr(st, "Falta configurar") != NULL, "y lo dice claramente en pantalla");
  }
  // El firmware sale de fabrica SIN servicio: es el requisito de fondo.
  newsCfgDefaults();
  chk(gNews.url[0] == 0 && gNews.key[0] == 0 && !gNews.enabled,
      "de fabrica no hay ni URL ni clave ni servicio activo");
  chk(gNews.maxNotif == 1, "de fabrica se avisa de UNA noticia por consulta");

  if(!gFails) printf("  Noticias: todas las comprobaciones pasan.\n");
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
  for(int id = 12; id < 16; id++) chk(!appIsFav(id), "las cuatro del dock no ocupan rejilla");
  chk(drwN == 16, "la caja muestra las 16 apps del registro");

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
    chk(homeWgAdd(1, WG_NEWS) == 0, "y un tercero");
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
    chk(!homeEmptySpaceAt(240, 100), "la banda de los widgets clima/noticias no");
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
  testNoticias();
  testCajaApps();
  testCronometro();
  testPaginasHome();
  testNotifUnaSola();
  testDeslizarPaginas();
  testCabeceras();
  testListasConScroll();
  testTarjetaCronometro();
  testPersonalizarInicio();
  if(gFails){ printf("%d comprobacion(es) han fallado.\n", gFails); return 1; }
  return 0;
}
