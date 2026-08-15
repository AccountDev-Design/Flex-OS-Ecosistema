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
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char*, uint32_t, void*, UBaseType_t, TaskHandle_t*, BaseType_t){ return pdPASS; }
void vTaskDelete(TaskHandle_t){}
void vTaskDelay(TickType_t){}
TaskHandle_t xTaskGetCurrentTaskHandle(){ return nullptr; }
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t){ return 0; }
BaseType_t xTaskNotifyGive(TaskHandle_t){ return pdTRUE; }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t){ return 1024; }
SemaphoreHandle_t xSemaphoreCreateMutex(){ return (SemaphoreHandle_t)1; }
// Mutex recursivo: lo usa el candado del motor de trabajos. Aqui no
// simula exclusion ninguna -- las pruebas corren en un hilo -- pero
// tiene que existir para enlazar, y devolver pdTRUE es exactamente lo
// que hace un mutex libre.
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(){ return (SemaphoreHandle_t)1; }
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t, TickType_t){ return pdTRUE; }
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t){ return pdTRUE; }
SemaphoreHandle_t xSemaphoreCreateBinary(){ return (SemaphoreHandle_t)1; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t){ return pdTRUE; }
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
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t, int, int, int, int, const void*){ return ESP_OK; }
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
static void testLongPress();
static void testPaginasHome();
static void testNotifCowork();
static void testCorrectorTeclado();
static void testPrivacidadVault();
static void testAppId16();
static void testAcciones();
static void testConfirmacion();
static void testChatFinal();
static void testBusqueda();
static void testBateria();
static void testNoHorizontal();
static void testNotifUnaSola();
static void testDeslizarPaginas();
static void testCabeceras();
static void testListasConScroll();
static void testTarjetaCronometro();
static void testTecladoIA();
// Almacen de trabajos para las pruebas: la placa lo pone en PSRAM, aqui
// basta con un array estatico.
static CoworkJob gTestJobs[CW_MAX_JOBS];
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

static void testPanelRapido(){
  printf("Panel rapido global\n");
  if(!flxGfxInit()){ printf("  FALLO: no se pudieron reservar los framebuffers\n"); gFails++; return; }
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
  // Escritorio de fabrica: las doce de siempre en la pagina 0 y las
  // otras dos paginas vacias. Es el estado del que parte una placa
  // recien instalada, y desde el que homeOrderNormalize() decide donde
  // cae cada app nueva.
  for(int i = 0; i < HOME_TOTAL; i++) homeOrder[i] = HOME_EMPTY;
  for(int i = 0; i < HOME_SLOTS; i++) homeOrder[i] = (uint8_t)i;
  gHomePage = 0;
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
  chk(appIsFav(IC_FLEXAI), "Flex Intelligence nace en el escritorio");
  chk(drwN == APP_N, "la caja muestra todas las apps del registro");
  // Y CAE EN LA PAGINA 2, sin mover ni un icono de la primera: la
  // pagina 0 esta llena con las doce de siempre.
  { int slot = -1;
    for(int i = 0; i < HOME_TOTAL; i++) if(homeOrder[i] == IC_FLEXAI){ slot = i; break; }
    chk(slot == HOME_SLOTS, "y ocupa la PRIMERA ranura de la segunda pagina");
    for(int i = 0; i < HOME_SLOTS; i++) chk(homeOrder[i] == (uint8_t)i,
        "las doce de la primera pagina se quedan donde estaban"); }

  // --- normalizacion: una ranura con una app no favorita se vacia ---
  gAppFav &= (uint32_t)~(1u << 5);
  homeOrderNormalize();
  chk(drwSlotOf(5) < 0, "quitar la marca de favorita libera su ranura");
  // --- y una favorita sin ranura recupera hueco ---
  gAppFav |= (uint32_t)(1u << 5);
  homeOrderNormalize();
  chk(drwSlotOf(5) >= 0, "una favorita sin ranura ocupa el primer hueco");

  // --- escritorio lleno: la marca se retira, no se inventa una ranura 37 ---
  drwTestReset();
  for(int id = 0; id < APP_N; id++) gAppFav |= (uint32_t)(1u << id);   // todas favoritas
  homeOrderNormalize();
  int nfav = 0;
  for(int id = 0; id < APP_N; id++) if(appIsFav(id)) nfav++;
  chk(nfav <= HOME_TOTAL, "nunca hay mas favoritas que ranuras en el escritorio");
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
  drwHideToggle(7);
  chk(appIsHidden(7),  "\"Ocultar\" marca la app");
  chk(!appIsFav(7),    "una app oculta no puede quedarse en Inicio");
  chk(drwSlotOf(7) < 0,"y su ranura queda libre");
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


// #############################################################
//  FLEX INTELLIGENCE  ·  interaccion (dentro del sketch)
//  ------------------------------------------------------------
//  Aqui se prueban las maquinas de estado que viven EN EL .ino y
//  que no se pueden ver desde fuera: la pulsacion larga, el
//  deslizamiento entre paginas del escritorio y el descarte de
//  una tarjeta de notificacion.
//
//  Se hace moviendo la estructura T (el estado tactil de alto
//  nivel) y el reloj virtual a mano, que es exactamente lo que
//  hace el driver del GT911 en la placa. Nada de esto necesita
//  pantalla: se comprueba el ESTADO al que se llega, no los
//  pixeles.
// #############################################################

// Deja el tactil en reposo.
static void tReset(){
  memset(&T, 0, sizeof(T));
}
// Simula que el dedo BAJA en (x,y) en el instante `ms`.
static void tDown(int x, int y, unsigned long ms){
  gTestMs = ms;
  tReset();
  T.down = true; T.pressed = true;
  T.x = T.startX = x;
  T.y = T.startY = y;
  T.downMs = ms;
}
// El dedo SIGUE abajo, ahora en (x,y).
static void tMove(int x, int y, unsigned long ms){
  gTestMs = ms;
  T.pressed = false;
  T.down = true;
  T.x = x; T.y = y;
  T.moved = true;
}
// El dedo SE LEVANTA.
static void tUp(unsigned long ms, bool tap){
  gTestMs = ms;
  T.down = false; T.pressed = false;
  T.released = true;
  T.tap = tap;
}

static void testLongPress(){
  printf("Activacion de Flex Intelligence (pulsacion larga)\n");
  aiConfigDefaults();
  aiConfig()->enabled = true;
  gState = ST_HOME; qsPanelY = 0; editMode = false; kioskOn = false; gLand = false;
  gAiLp = AIL_IDLE; gAiLpConsumed = false; aiOvOpen = false;

  const int hx = SCR_W / 2, hy = SCR_H - 52 + 4;   // centro del boton de Inicio

  // ---- NAVEGACION POR BOTONES ----
  gNavMode = 0;

  // (1) TOQUE CORTO: se suelta antes de los 700 ms. No debe abrir nada,
  //     y sobre todo NO debe consumir el toque: la accion normal de
  //     Inicio tiene que seguir su camino.
  tDown(hx, hy, 1000);
  chk(!flexAiLongPressTick(), "toque corto: el panel no se queda el toque");
  chk(gAiLp == AIL_CAND,      "pero queda como candidato a pulsacion larga");
  tMove(hx, hy, 1400);                            // 400 ms: aun no
  chk(!flexAiLongPressTick(), "a los 400 ms sigue sin ser larga");
  chk(!aiOvOpen,              "y no se ha abierto nada");
  tUp(1450, true);
  chk(!flexAiLongPressTick(), "al soltar antes de tiempo, el toque sigue siendo de Inicio");
  chk(T.tap,                  "y el tap NO se consumio");
  chk(!aiOvOpen,              "el panel sigue cerrado");

  // (2) PULSACION LARGA CONFIRMADA a los 700 ms.
  gAiLp = AIL_IDLE; gAiLpConsumed = false;
  tDown(hx, hy, 2000);
  flexAiLongPressTick();
  tMove(hx, hy, 2400);
  chk(!flexAiLongPressTick(), "a 400 ms todavia no");
  tMove(hx, hy, 2705);                            // 705 ms
  chk(flexAiLongPressTick(),  "a los 700 ms se confirma y el toque es suyo");
  chk(gAiLp == AIL_FIRED,     "la maquina llega a CONFIRMADA");
  chk(gAiLpConsumed,          "la pulsacion queda marcada como consumida");

  // (3) LA REGLA QUE EVITA LA DOBLE ACCION. Al soltar el dedo despues de
  //     abrir el panel, el toque NO puede llegar tambien a Inicio: si
  //     llegara, el usuario acabaria en el escritorio con el panel
  //     encima -- que es exactamente lo que no puede pasar.
  tUp(2800, true);
  chk(flexAiLongPressTick(), "al soltar, la pulsacion consumida se queda el evento");
  chk(!T.tap,                "y el tap de Inicio queda ANULADO");
  chk(!T.released,           "igual que el released");
  chk(!gAiLpConsumed,        "levantar el dedo limpia la marca para la proxima");

  // (4) CANCELACION POR MOVIMIENTO: si el dedo se va, esto vuelve a ser
  //     un gesto de navegacion y se CEDE, no se compite por el.
  gAiLp = AIL_IDLE; gAiLpConsumed = false; aiOvOpen = false;
  tDown(hx, hy, 4000);
  flexAiLongPressTick();
  tMove(hx, hy - (AI_LP_TOL + 6), 4100);          // se paso de la tolerancia
  chk(!flexAiLongPressTick(), "movido: el toque deja de ser nuestro");
  chk(gAiLp == AIL_NAV,       "la maquina pasa a GESTO DE NAVEGACION");
  tMove(hx, hy - 60, 4900);                       // y aunque aguante 900 ms...
  chk(!flexAiLongPressTick(), "...ya no se confirma nunca");
  chk(!aiOvOpen,              "el panel no se abre");

  // (5) FUERA DE ZONA: mantener el dedo en mitad de la pantalla no abre
  //     nada. La zona caliente es el boton, no la pantalla entera.
  gAiLp = AIL_IDLE;
  tDown(SCR_W / 2, SCR_H / 2, 6000);
  flexAiLongPressTick();
  tMove(SCR_W / 2, SCR_H / 2, 6800);
  chk(!flexAiLongPressTick(), "fuera de la zona no se activa");
  chk(gAiLp == AIL_IDLE,      "ni siquiera llega a candidato");

  // ---- NAVEGACION POR GESTOS ----
  gNavMode = 1;
  gAiLp = AIL_IDLE; gAiLpConsumed = false; aiOvOpen = false;
  const int gx = SCR_W / 2, gy = SCR_H - 20;      // centro de la barra de gestos

  // (6) Dedo quieto en el centro de la barra: se activa.
  tDown(gx, gy, 8000);
  flexAiLongPressTick();
  tMove(gx, gy, 8750);
  chk(flexAiLongPressTick(), "gestos: mantener quieto en el centro activa");
  chk(gAiLp == AIL_FIRED,    "y confirma");
  tUp(8800, true);
  flexAiLongPressTick();

  // (7) DESLIZAMIENTO VERTICAL: es el gesto de Inicio de siempre y se
  //     conserva intacto.
  gAiLp = AIL_IDLE; gAiLpConsumed = false;
  tDown(gx, gy, 9000);
  flexAiLongPressTick();
  tMove(gx, gy - 50, 9100);
  chk(!flexAiLongPressTick(), "un deslizamiento vertical NO activa");
  chk(gAiLp == AIL_NAV,       "sigue siendo navegacion");
  tMove(gx, gy - 90, 9900);
  chk(!flexAiLongPressTick(), "aunque se mantenga despues de deslizar");

  // (8) DESLIZAMIENTO HORIZONTAL sobre la barra: tampoco.
  gAiLp = AIL_IDLE;
  tDown(gx, gy, 10000);
  flexAiLongPressTick();
  tMove(gx + 40, gy, 10100);
  chk(!flexAiLongPressTick(), "un deslizamiento horizontal tampoco activa");
  chk(gAiLp == AIL_NAV,       "es navegacion");

  // (9) Los LATERALES de la barra se dejan libres: ahi viven los gestos
  //     de borde.
  gAiLp = AIL_IDLE;
  tDown(10, gy, 11000);
  flexAiLongPressTick();
  tMove(10, gy, 11800);
  chk(!flexAiLongPressTick(), "el borde izquierdo de la barra no activa");

  // (10) DONDE NO SE PUEDE ABRIR. Con la pantalla bloqueada, en kiosco o
  //      con Flex Intelligence desactivada, ni se intenta.
  gNavMode = 0;
  gAiLp = AIL_IDLE;
  gState = ST_LOCK;
  chk(!flexAiCanOpen(), "bloqueado: no se puede abrir");
  gState = ST_HOME;
  kioskOn = true;
  chk(!flexAiCanOpen(), "en kiosco: no se puede abrir");
  kioskOn = false;
  aiConfig()->enabled = false;
  chk(!flexAiCanOpen(), "desactivada en Ajustes: no se puede abrir");
  aiConfig()->enabled = true;
  qsPanelY = 100;
  chk(!flexAiCanOpen(), "con la cortina desplegada: no se puede abrir");
  qsPanelY = 0;
  chk(flexAiCanOpen(),  "y en el escritorio normal, si");

  tReset();
  gAiLp = AIL_IDLE; gAiLpConsumed = false;
  if(!gFails) printf("  Pulsacion larga: todas las comprobaciones pasan.\n");
}

static void testPaginasHome(){
  printf("Paginas del escritorio\n");
  drwTestReset();
  gState = ST_HOME; editMode = false; gLand = false;
  gHomePage = 0; hpDragging = false; hpSettling = false; hpBuf = NULL; hpBufPage = -1;

  // La app 17 esta en la pagina 1 y NO se ve desde la 0.
  { int id;
    chk(!hitHomeIcon(HOME_GX0 + 10, HOME_GY0 + 10, id) || id != IC_FLEXAI,
        "en la pagina 0 no se toca Flex Intelligence");
    gHomePage = 1;
    chk(hitHomeIcon(HOME_GX0 + 10, HOME_GY0 + 10, id) && id == IC_FLEXAI,
        "en la pagina 1 su icono responde en la primera casilla");
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
  gHomePage = HOME_PAGES - 1;
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
  editMode = false;

  tReset();
  hpDragging = false; hpSettling = false; gHomePage = 0;
  if(!gFails) printf("  Paginas del escritorio: todas las comprobaciones pasan.\n");
}

static void testNotifCowork(){
  printf("Notificaciones de Cowork\n");
  // La cola de la isla se comparte con las notificaciones de hardware:
  // se vacia antes de empezar para no arrastrar nada de otra bateria.
  gNotifCount = 0;
  notifDragIdx = -1;
  memset(gNotifs, 0, sizeof(gNotifs));

  notifPushJob(42, "Resumen listo", "Se creo una nota", NACT_VIEW, false);
  chk(gNotifCount == 1,                    "encola una tarjeta de Cowork");
  chk(gNotifs[0].src == NSRC_COWORK,       "marcada como de Cowork");
  chk(gNotifs[0].jobId == 42,              "apuntando a su trabajo");
  chk(gNotifs[0].act == NACT_VIEW,         "con su boton");
  chk(!gNotifs[0].sticky,                  "y caducando sola");

  // Una tarjeta que PIDE UNA DECISION no caduca a los 5 s: un aviso que
  // exige respuesta y desaparece solo es un aviso que no sirve.
  notifPushJob(43, "Necesito tu permiso", "Para continuar", NACT_OPEN, true);
  chk(gNotifs[1].sticky, "la que pide intervencion es persistente");

  // Cola llena: se hace sitio sacando la mas vieja que NO pida
  // intervencion, no la primera que haya.
  notifPushJob(44, "Traduccion terminada", "", NACT_VIEW, false);
  chk(gNotifCount == NOTIF_MAX, "la cola llega a su tope");
  notifPushJob(45, "Cuarta", "", NACT_VIEW, false);
  chk(gNotifCount == NOTIF_MAX, "y no lo pasa");
  { bool has43 = false, has45 = false;
    for(int i = 0; i < gNotifCount; i++){
      if(gNotifs[i].jobId == 43) has43 = true;
      if(gNotifs[i].jobId == 45) has45 = true;
    }
    chk(has43, "la que pedia intervencion NO se descarto");
    chk(has45, "y la nueva si entro"); }

  // --- DESLIZAR A LA IZQUIERDA SOLO DESCARTA LA TARJETA ---
  // Es la distincion que pide el enunciado: descartar la tarjeta,
  // cancelar la tarea y borrar el resultado son tres cosas distintas.
  // Esta -- la del gesto -- es la que NO toca el trabajo.
  gNotifCount = 0;
  memset(gNotifs, 0, sizeof(gNotifs));
  notifPushJob(50, "Resumen listo", "", NACT_VIEW, false);
  gNotifs[0].armed = true;
  gNotifs[0].phase = NP_IDLE;
  gState = ST_HOME; qsPanelY = 0; editMode = false;

  // Arrastre a la izquierda por debajo del umbral: vuelve a su sitio.
  int cardY = NOTIF_Y0 + 10;
  tDown(NOTIF_MARGIN_X + 100, cardY, 1000);
  notifHandleTouch();
  chk(notifDragIdx == 0, "el dedo agarra la tarjeta");
  tMove(NOTIF_MARGIN_X + 100 - 30, cardY, 1050);
  notifHandleTouch();
  chk(gNotifs[0].phase == NP_DRAG, "y la arrastra");
  chk(gNotifs[0].slideX < 0,       "hacia la izquierda");
  tUp(1100, false);
  notifHandleTouch();
  chk(gNotifs[0].phase == NP_SPRING, "poco recorrido: vuelve a su sitio");
  chk(gNotifCount == 1,              "y la tarjeta sigue ahi");

  // Arrastre largo: se descarta la TARJETA.
  gNotifs[0].phase = NP_IDLE; gNotifs[0].slideX = 0; notifDragIdx = -1;
  tDown(NOTIF_MARGIN_X + 100, cardY, 2000);
  notifHandleTouch();
  tMove(NOTIF_MARGIN_X + 100 - (NOTIF_CARD_W / 2), cardY, 2100);
  notifHandleTouch();
  tUp(2200, false);
  notifHandleTouch();
  chk(gNotifs[0].phase == NP_OUT, "pasado el umbral, la tarjeta se va");
  // Y lo que importa: el TRABAJO no se ha tocado. La tarjeta es un
  // aviso; el resultado vive en Cowork hasta que el usuario lo borre.
  chk(gNotifs[0].jobId == 50, "la tarjeta sigue apuntando a su trabajo");

  // Un arrastre HACIA LA DERECHA no descarta nada.
  gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs)); notifDragIdx = -1;
  notifPushJob(51, "Otra", "", NACT_VIEW, false);
  gNotifs[0].armed = true; gNotifs[0].phase = NP_IDLE;
  tDown(NOTIF_MARGIN_X + 100, cardY, 3000);
  notifHandleTouch();
  tMove(NOTIF_MARGIN_X + 100 + 120, cardY, 3100);
  notifHandleTouch();
  chk(gNotifs[0].slideX == 0.0f, "hacia la derecha la tarjeta no se mueve");
  tUp(3200, false);
  notifHandleTouch();
  chk(gNotifs[0].phase != NP_OUT, "y no se descarta");

  // --- geometria de la tarjeta flotante en las dos orientaciones ---
  // En horizontal NO es la vertical girada: es una barra ancha y baja,
  // y deja libre el lado por donde los juegos ponen sus controles.
  { int x, y, w, h;
    gLand = false; nflBox(x, y, w, h);
    chk(w > h,            "vertical: la tarjeta es mas ancha que alta");
    chk(x >= 0 && x + w <= SCR_W, "y cabe a lo ancho de la pantalla");
    chk(y >= 0 && y + h <= SCR_H, "y a lo alto");
    int pw = w;
    gLand = true; nflBox(x, y, w, h);
    chk(w > h,            "horizontal: sigue siendo ancha y baja, no girada");
    chk(w != pw,          "con un ancho PROPIO, no el de vertical estirado");
    chk(x + w <= LW,      "cabe en el lienzo horizontal 800x480");
    chk(y + h <= LH,      "sin salirse por abajo");
    chk(x > LW / 4,       "y deja libre el lado donde los juegos ponen los controles");
    gLand = false; }

  // El rectangulo que se guarda para poder devolver el fondo tiene que
  // caber en el buffer reservado, en las DOS orientaciones. Si no
  // cupiera, nflSaveBg se niega a dibujar -- pero es mejor saberlo aqui.
  { int x, y, w, h;
    gLand = false; nflPhysRect(x, y, w, h);
    chk((long)w * h <= NFL_SAVE_PX, "vertical: el fondo guardado cabe en su buffer");
    chk(x >= 0 && y >= 0 && x + w <= SCR_W && y + h <= SCR_H, "y esta dentro de la pantalla");
    gLand = true; nflPhysRect(x, y, w, h);
    chk((long)w * h <= NFL_SAVE_PX, "horizontal: tambien cabe");
    chk(x >= 0 && y >= 0 && x + w <= SCR_W && y + h <= SCR_H, "y tambien esta dentro");
    gLand = false; }

  // --- cuando NO debe salir la tarjeta flotante ---
  gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs));
  notifPushJob(60, "Algo", "", NACT_VIEW, false);
  gState = ST_HOME; qsPanelY = 0; editMode = false;
  chk(!nflWanted(), "en el escritorio manda la isla de siempre, no la flotante");
  gState = ST_APP;
  chk(nflWanted(),  "encima de una app SI sale");
  gState = ST_LOCK;
  chk(!nflWanted(), "en la pantalla de bloqueo NO se filtra nada");
  gState = ST_APP; qsPanelY = 200;
  chk(!nflWanted(), "con la cortina desplegada, manda la cortina");
  qsPanelY = 0;
  gState = ST_SPLASH;
  chk(!nflWanted(), "durante el arranque tampoco");
  gState = ST_APP;
  gNotifCount = 0;
  chk(!nflWanted(), "sin tarjetas no hay nada que enseñar");

  tReset();
  gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs));
  notifDragIdx = -1; gState = ST_HOME;
  if(!gFails) printf("  Notificaciones de Cowork: todas las comprobaciones pasan.\n");
}

static void testCorrectorTeclado(){
  printf("Corrector en el teclado\n");
  flexSpellBegin();
  flexSpellSetLang(FLEX_SPELL_ES);
  aiConfigDefaults();
  gKbSpell = true;                       // corrector activado en Ajustes
  aiConfig()->autoCorrect = false;       // autocorregir apagado (el de fabrica)

  // El teclado escribe sobre el buffer de Notas; aqui se usa el
  // estatico, que es el mismo camino que en una placa sin PSRAM.
  static char buf[512];
  char*  saveBuf = noteBuffer;
  size_t saveMax = noteBufMax;
  noteBuffer = buf; noteBufMax = sizeof(buf);

  // --- sugerencias tras terminar la palabra ---
  snprintf(buf, sizeof(buf), "njota ");
  noteCur = (int)strlen(buf);
  kbSpellClear();
  kbSpellOnWordEnd();
  chk(kbSpStart == 0,           "encuentra la palabra que acaba de terminar");
  chk(kbSpLen == 5,             "con su longitud exacta");
  chk(!strcmp(kbSpWord, "njota"), "y su texto");
  chk(kbSpSugN > 0,             "propone correcciones");
  chk(!strcmp(kbSpSug[0], "nota"), "la primera es \"nota\"");

  // --- aceptar una sugerencia SUSTITUYE solo esa palabra ---
  snprintf(buf, sizeof(buf), "njota mas texto");
  noteCur = 6;                            // justo detras del espacio
  kbSpellClear();
  kbSpellOnWordEnd();
  chk(kbSpSugN > 0, "vuelve a proponer");
  kbSpellAccept(0);
  chk(!strcmp(buf, "nota mas texto"), "aceptar sustituye SOLO la palabra dudosa");
  chk(noteCur == 5,                   "y el cursor se ajusta a lo que crecio/encogio");
  chk(kbSpSugN == 0,                  "y se limpia el estado");
  chk(flexSpellKnown("nota"),         "la palabra aceptada queda aprendida");

  // --- autocorreccion + DESHACER ---
  aiConfig()->autoCorrect = true;
  flexSpellBegin();                       // olvidar lo aprendido arriba
  flexSpellSetLang(FLEX_SPELL_ES);
  snprintf(buf, sizeof(buf), "njota ");
  noteCur = (int)strlen(buf);
  kbSpellClear();
  kbSpellOnWordEnd();
  chk(!strcmp(buf, "nota "), "con autocorregir activado, se corrige sola");
  chk(kbSpUndo,              "y queda marcada como deshacible");
  chk(!strcmp(kbSpUndoTxt, "njota"), "guardando lo que el usuario escribio");
  kbSpellUndoFix();
  chk(!strcmp(buf, "njota "), "deshacer devuelve exactamente lo escrito");
  chk(!kbSpUndo,              "y se consume");
  // Y APRENDE la palabra: si el usuario la escribio a proposito y ademas
  // rechazo la correccion, no hay que volver a corregirsela.
  chk(flexSpellKnown("njota"), "deshacer aprende la palabra del usuario");

  // --- una palabra CORRECTA no se toca ---
  flexSpellBegin(); flexSpellSetLang(FLEX_SPELL_ES);
  snprintf(buf, sizeof(buf), "nota ");
  noteCur = (int)strlen(buf);
  kbSpellClear();
  kbSpellOnWordEnd();
  chk(!strcmp(buf, "nota "), "una palabra correcta se queda igual");
  chk(kbSpSugN == 0,         "y no se proponen correcciones");
  chk(!kbSpUndo,             "ni hay nada que deshacer");

  // --- con el corrector APAGADO no pasa nada ---
  gKbSpell = false;
  snprintf(buf, sizeof(buf), "njota ");
  noteCur = (int)strlen(buf);
  kbSpellClear();
  kbSpellOnWordEnd();
  chk(!strcmp(buf, "njota "), "con el corrector apagado, el texto no se toca");
  chk(kbSpSugN == 0,          "y no hay sugerencias");
  gKbSpell = true;

  // --- SEGURIDAD DE LA SUSTITUCION ---
  // Si el buffer cambio por debajo desde que se analizo, sustituir a
  // ciegas destrozaria el texto. Se comprueba y se abandona.
  aiConfig()->autoCorrect = false;
  flexSpellBegin(); flexSpellSetLang(FLEX_SPELL_ES);
  snprintf(buf, sizeof(buf), "njota ");
  noteCur = (int)strlen(buf);
  kbSpellClear();
  kbSpellOnWordEnd();
  chk(kbSpStart >= 0, "hay una palabra marcada");
  snprintf(buf, sizeof(buf), "otra cosa distinta");   // el texto cambio por debajo
  noteCur = (int)strlen(buf);
  chk(!kbSpellReplace("nota"), "no se sustituye sobre un texto que ya no coincide");
  chk(!strcmp(buf, "otra cosa distinta"), "y el texto queda intacto");

  // Buffer vacio y offsets imposibles: nada revienta.
  buf[0] = 0; noteCur = 0;
  kbSpellClear();
  kbSpellOnWordEnd();
  chk(kbSpStart < 0, "con el buffer vacio no hay nada que analizar");
  chk(!kbSpellReplace("x"), "y sustituir sin palabra marcada no hace nada");

  // El disparo por PAUSA analiza la palabra a medias (sin terminador).
  flexSpellBegin(); flexSpellSetLang(FLEX_SPELL_ES);
  snprintf(buf, sizeof(buf), "guardsr");
  noteCur = (int)strlen(buf);
  kbSpellClear();
  kbSpPending = true; kbSpLastKeyMs = 1000;
  gTestMs = 1000 + KB_SPELL_PAUSE_MS - 10;
  kbSpellPauseTick();
  chk(kbSpSugN == 0, "antes de la pausa no se analiza");
  gTestMs = 1000 + KB_SPELL_PAUSE_MS + 10;
  kbSpPending = true;
  kbSpellPauseTick();
  chk(kbSpSugN > 0, "cumplida la pausa, si");
  chk(!strcmp(kbSpSug[0], "guardar"), "y propone la correccion buena");

  noteBuffer = saveBuf; noteBufMax = saveMax; noteCur = 0;
  kbSpellClear();
  if(!gFails) printf("  Corrector en el teclado: todas las comprobaciones pasan.\n");
}

static void testPrivacidadVault(){
  printf("Privacidad de Flex Vault");
  printf("\n");
  aiConfigDefaults();
  aiConfig()->perms = AI_PERM_TEXT;

  // Sin consentimiento NO se lee nada, ni con la boveda abierta.
  chk(!aiVaultConsentHas(9), "de entrada no hay consentimiento");
  chk(!vwSendToAi(9),        "sin consentimiento no se envia nada");

  // El consentimiento es de UN item y de UN solo uso.
  aiVaultConsentGrant(9);
  chk(aiVaultConsentHas(9),  "concedido para el item 9");
  chk(!aiVaultConsentHas(10),"no vale para el item 10");
  // El doble de la boveda de este arnes dice que esta CERRADA, asi que
  // vwSendToAi se para en la primera puerta -- que es justo la primera
  // regla: sin boveda abierta no se lee, tenga el permiso que tenga.
  chk(!flexVaultUnlocked(),  "la boveda esta cerrada en el arnes");
  chk(!vwSendToAi(9),        "con la boveda cerrada no se envia, aunque haya permiso");

  // Cerrar la boveda CADUCA el consentimiento, venga de donde venga el
  // cierre (pantalla, inactividad, salida manual).
  aiVaultConsentGrant(9);
  vaultLockNow(FXV_LOCK_SCREEN);
  chk(!aiVaultConsentHas(9), "al cerrar la boveda, el permiso caduca");

  aiVaultConsentClear();
  if(!gFails) printf("  Privacidad de Flex Vault: todas las comprobaciones pasan.\n");
}


// #############################################################
//  FLEX INTELLIGENCE  ·  segunda tanda
//  ------------------------------------------------------------
//  Lo que se anadio despues de la primera version: la app con id
//  16 suelta por todo el sistema, la ejecucion de acciones
//  estructuradas, el chat que se completa solo, la busqueda local
//  de archivos, la pausa por bateria y -- lo que se dejo FUERA a
//  proposito -- que nada de esto asome en horizontal.
// #############################################################

// El sistema de archivos de mentira vive en ino_extern_stubs.cpp.
extern void fakeFsReset();
extern void fakeFsAdd(const char* path, const char* body, unsigned size);

static void testAppId16(){
  printf("Flex Intelligence con id 16\n");
  drwTestReset();

  // --- FAVORITA: quitar y volver a poner ---
  chk(appIsFav(IC_FLEXAI), "nace en el escritorio");
  int slot0 = drwSlotOf(IC_FLEXAI);
  chk(slot0 >= 0, "y tiene ranura");
  drwFavToggle(IC_FLEXAI);
  chk(!appIsFav(IC_FLEXAI),        "\"Quitar de Inicio\" SI funciona con la app 16");
  chk(drwSlotOf(IC_FLEXAI) < 0,    "y libera su ranura");
  drwFavToggle(IC_FLEXAI);
  chk(appIsFav(IC_FLEXAI),         "\"Anadir a Inicio\" tambien");
  chk(drwSlotOf(IC_FLEXAI) >= 0,   "y le devuelve una ranura");

  // EL FALLO CONCRETO QUE SE ARREGLO: con la pagina 1 llena -- que es
  // como esta un escritorio de fabrica -- el hueco tiene que buscarse
  // en las tres paginas. Antes se miraban solo las 12 primeras y
  // "Anadir a Inicio" no hacia nada.
  drwTestReset();
  drwFavToggle(IC_FLEXAI);                       // fuera
  for(int i = 0; i < HOME_SLOTS; i++) chk(homeOrder[i] != HOME_EMPTY,
      "la primera pagina esta llena");
  drwFavToggle(IC_FLEXAI);                       // y dentro otra vez
  chk(appIsFav(IC_FLEXAI), "con la pagina 1 llena, sigue pudiendo entrar");
  chk(drwSlotOf(IC_FLEXAI) >= HOME_SLOTS, "y cae en la pagina 2 o 3");

  // --- OCULTAR: y que no quede un icono fantasma ---
  drwTestReset();
  int sl = drwSlotOf(IC_FLEXAI);
  chk(sl >= HOME_SLOTS, "de fabrica esta en la pagina 2");
  drwHideToggle(IC_FLEXAI);
  chk(appIsHidden(IC_FLEXAI),   "se puede ocultar");
  chk(!appIsFav(IC_FLEXAI),     "ocultarla la saca de Inicio");
  chk(!drwInList(IC_FLEXAI),    "y de la caja");
  // Este es el fallo: la limpieza del escritorio miraba solo las 12
  // primeras ranuras, asi que una app oculta que viviera en la pagina 2
  // seguia dibujada ahi.
  chk(drwSlotOf(IC_FLEXAI) < 0, "NO queda un icono fantasma en la pagina 2");
  drwHideToggle(IC_FLEXAI);
  chk(!appIsHidden(IC_FLEXAI),  "se puede volver a mostrar");
  chk(drwInList(IC_FLEXAI),     "y vuelve a la caja");

  // --- CANDADO Y KIOSCO: las guardas tambien llegaban solo a 15 ---
  drwTestReset();
  gLockType = 1;                                  // hace falta clave para el candado
  appLockSet(IC_FLEXAI, true);
  chk(appLockGet(IC_FLEXAI),  "se le puede poner candado");
  appLockSet(IC_FLEXAI, false);
  chk(!appLockGet(IC_FLEXAI), "y quitarselo");
  gLockType = 0;

  // --- LAS 36 RANURAS: colocable en cualquiera de las tres paginas ---
  drwTestReset();
  for(int p = 0; p < HOME_PAGES; p++){
    // Se vacia todo y se pone SOLO en la primera ranura de esa pagina.
    for(int i = 0; i < HOME_TOTAL; i++) homeOrder[i] = HOME_EMPTY;
    gAppFav = (uint32_t)(1u << IC_FLEXAI);
    gAppHidden = 0;
    homeOrder[p * HOME_SLOTS] = IC_FLEXAI;
    homeOrderNormalize();
    chk(homeOrder[p * HOME_SLOTS] == IC_FLEXAI, "se queda en la ranura de su pagina");
    gHomePage = p;
    int id = -1;
    chk(hitHomeIcon(HOME_GX0 + 10, HOME_GY0 + 10, id) && id == IC_FLEXAI,
        "y su icono responde ahi");
  }
  gHomePage = 0;
  drwTestReset();
  if(!gFails) printf("  App id 16: todas las comprobaciones pasan.\n");
}

static void testAcciones(){
  printf("Acciones estructuradas\n");
  fakeFsReset();
  fakeFsAdd("/Notas/a.txt", "texto original", 0);
  coworkAttachStorage(gTestJobs, CW_MAX_JOBS);
  aiConfigDefaults();

  // --- RUTAS PROHIBIDAS: lo que la IA no puede tocar, pase lo que pase ---
  chk(aiPathForbidden("/System/config"),     "/System esta prohibido");
  chk(aiPathForbidden("/System/spell.txt"),  "...incluido el diccionario");
  chk(aiPathForbidden("/.fxvault/d/1"),      "Flex Vault esta prohibido");
  chk(aiPathForbidden("/Notas/../System/x"), "subir de directorio esta prohibido");
  chk(aiPathForbidden("/.oculto"),           "lo oculto esta prohibido");
  chk(aiPathForbidden("Notas/a.txt"),        "una ruta relativa se rechaza");
  chk(aiPathForbidden(""),                   "vacio se rechaza");
  chk(aiPathForbidden(NULL),                 "NULL se rechaza");
  chk(!aiPathForbidden("/Notas/a.txt"),      "una nota normal si se puede");

  // --- QUE SE PUEDE SOBRESCRIBIR ---
  chk(aiPathWritableDoc("/Notas/a.txt"),       "una nota .txt");
  chk(aiPathWritableDoc("/Documentos/b.note"), "un .note en Documentos");
  chk(!aiPathWritableDoc("/Paint/c.fpn"),      "un dibujo NO: un resultado de texto no lo sustituye");
  chk(!aiPathWritableDoc("/Notas/d.jpg"),      "una imagen tampoco");
  chk(!aiPathWritableDoc("/System/e.txt"),     "ni nada en /System");

  // --- EL ARGUMENTO DEL SERVIDOR SE REDUCE A UN NOMBRE ---
  { char st[32];
    chk(aiSafeStem("Resumen", st, sizeof(st)) && !strcmp(st, "Resumen"), "un nombre normal pasa");
    chk(aiSafeStem("../../System/config", st, sizeof(st)), "una ruta deja algo...");
    chk(!strchr(st, '/') && !strstr(st, ".."), "...pero sin barras ni puntos");
    chk(aiSafeStem("a/b/c", st, sizeof(st)) && !strcmp(st, "abc"), "las barras se caen");
    chk(!aiSafeStem("///", st, sizeof(st)), "si no queda nada, se rechaza");
    chk(!aiSafeStem(NULL, st, sizeof(st)),  "NULL se rechaza"); }

  // --- open_app: solo apps que existen y que el usuario no cerro ---
  chk(aiResolveAppId("0") == 0,            "por numero");
  chk(aiResolveAppId("16") == IC_FLEXAI,   "el 16 tambien");
  chk(aiResolveAppId("99") < 0,            "un id inexistente NO");
  chk(aiResolveAppId("-1") < 0,            "ni uno negativo");
  chk(aiResolveAppId("") < 0,              "ni vacio");
  chk(aiResolveAppId(NULL) < 0,            "ni NULL");
  chk(aiResolveAppId("Calculadora") >= 0,  "por nombre");
  chk(aiResolveAppId("NoExiste") < 0,      "un nombre inventado NO");
  gAppHidden |= (uint32_t)(1u << IC_CALC);
  chk(aiResolveAppId("13") < 0,            "una app OCULTA no se abre por la espalda");
  gAppHidden = 0;
  gLockType = 1; appLockSet(IC_CALC, true);
  chk(aiResolveAppId("13") < 0,            "una app con CANDADO tampoco");
  appLockSet(IC_CALC, false); gLockType = 0;
  kioskOn = true; kioskApp = IC_RELOJ;
  chk(aiResolveAppId("13") < 0,            "en kiosco, solo la app clavada");
  chk(aiResolveAppId("0") == IC_RELOJ,     "y esa si");
  kioskOn = false;

  // --- EJECUCION: crear nota ---
  { uint32_t id = coworkSubmit(CW_KIND_SUMMARY, "S", "x", 1, NULL, 0, 0, 1000);
    coworkBeginWork(id, 1000);
    coworkSetAction(id, AI_ACT_CREATE_NOTE, "Resumen");
    coworkFinish(id, "contenido del resumen", 21, 0, true, 1100);
    char msg[64];
    chk(aiExecAction(id, msg, sizeof(msg)), "crear nota funciona");
    chk(coworkFind(id)->applied, "queda marcada como aplicada");
    chk(!aiExecAction(id, msg, sizeof(msg)), "y NO se aplica dos veces");
    // El texto acabo donde debia y con el nombre reducido.
    char body[64];
    chk(flexFsReadText("/Notas/Resumen.txt", body, sizeof(body)) > 0, "la nota existe");
    chk(!strcmp(body, "contenido del resumen"), "con el contenido correcto");
    coworkDelete(id); }

  // Un argumento con ruta NO saca la nota de /Notas.
  { uint32_t id = coworkSubmit(CW_KIND_SUMMARY, "S", "x", 1, NULL, 0, 0, 2000);
    coworkBeginWork(id, 2000);
    coworkSetAction(id, AI_ACT_CREATE_NOTE, "../../System/config");
    coworkFinish(id, "malo", 4, 0, true, 2100);
    char msg[64];
    aiExecAction(id, msg, sizeof(msg));
    char body[64];
    chk(flexFsReadText("/System/config", body, sizeof(body)) < 0,
        "un argumento con ruta NO escribe en /System");
    coworkDelete(id); }

  // --- EJECUCION: reemplazar texto, sobre el src del FIRMWARE ---
  { uint32_t h = coworkHash("texto original", 14);
    uint32_t id = coworkSubmit(CW_KIND_CORRECT, "C", "texto original", 14, "/Notas/a.txt", h, 0, 3000);
    coworkBeginWork(id, 3000);
    coworkSetAction(id, AI_ACT_REPLACE_TEXT, "/System/config");   // el arg se IGNORA
    coworkFinish(id, "texto corregido", 15, h, false, 3100);
    char msg[64];
    chk(aiExecAction(id, msg, sizeof(msg)), "reemplazar funciona");
    char body[64]; flexFsReadText("/Notas/a.txt", body, sizeof(body));
    chk(!strcmp(body, "texto corregido"), "escribio en el documento de ORIGEN");
    chk(flexFsReadText("/System/config", body, sizeof(body)) < 0,
        "y NO donde decia el argumento del servidor");
    coworkDelete(id); }

  // Sin documento de origen no se reemplaza nada.
  { uint32_t id = coworkSubmit(CW_KIND_CORRECT, "C", "x", 1, NULL, 0, 0, 4000);
    coworkBeginWork(id, 4000);
    coworkSetAction(id, AI_ACT_REPLACE_TEXT, NULL);
    coworkFinish(id, "y", 1, 0, false, 4100);
    char msg[64];
    chk(!aiExecAction(id, msg, sizeof(msg)), "sin origen no se reemplaza");
    coworkDelete(id); }

  // --- EJECUCION: anadir a la nota ---
  { fakeFsReset(); fakeFsAdd("/Notas/b.txt", "primera linea", 0);
    uint32_t h = coworkHash("primera linea", 13);
    uint32_t id = coworkSubmit(CW_KIND_SUMMARY, "S", "primera linea", 13, "/Notas/b.txt", h, 0, 5000);
    coworkBeginWork(id, 5000);
    coworkSetAction(id, AI_ACT_APPEND_NOTE, NULL);
    coworkFinish(id, "segunda linea", 13, h, false, 5100);
    char msg[64];
    chk(aiExecAction(id, msg, sizeof(msg)), "anadir funciona");
    char body[64]; flexFsReadText("/Notas/b.txt", body, sizeof(body));
    chk(strstr(body, "primera linea") && strstr(body, "segunda linea"),
        "conserva lo que habia y anade lo nuevo");
    coworkDelete(id); }

  // --- una accion FUERA de la lista no llega a ejecutarse ---
  // (FlexOS_AI ya la degrada a show_text; aqui se comprueba que
  //  show_text no escribe nada.)
  { fakeFsReset(); fakeFsAdd("/Notas/c.txt", "intacto", 0);
    uint32_t h = coworkHash("intacto", 7);
    uint32_t id = coworkSubmit(CW_KIND_ANALYZE, "A", "intacto", 7, "/Notas/c.txt", h, 0, 6000);
    coworkBeginWork(id, 6000);
    coworkSetAction(id, AI_ACT_SHOW_TEXT, NULL);
    coworkFinish(id, "solo mirar", 10, h, false, 6100);
    char msg[64];
    aiExecAction(id, msg, sizeof(msg));
    char body[64]; flexFsReadText("/Notas/c.txt", body, sizeof(body));
    chk(!strcmp(body, "intacto"), "show_text NO escribe en ningun fichero");
    coworkDelete(id); }

  fakeFsReset();
  coworkAttachStorage(gTestJobs, CW_MAX_JOBS);
  if(!gFails) printf("  Acciones estructuradas: todas las comprobaciones pasan.\n");
}

static void testConfirmacion(){
  printf("Confirmacion antes de tocar un fichero\n");
  fakeFsReset();
  fakeFsAdd("/Notas/a.txt", "original", 0);
  coworkAttachStorage(gTestJobs, CW_MAX_JOBS);
  gState = ST_APP; gAppId = IC_FLEXAI; gLand = false;

  uint32_t h = coworkHash("original", 8);
  uint32_t id = coworkSubmit(CW_KIND_CORRECT, "C", "original", 8, "/Notas/a.txt", h, 0, 1000);
  coworkBeginWork(id, 1000);
  coworkSetAction(id, AI_ACT_REPLACE_TEXT, NULL);
  coworkFinish(id, "corregido", 9, h, false, 1100);

  int bx, by, bw, bh; uiBox(bx, by, bw, bh);

  // --- CANCELAR no toca el fichero ---
  aiConfirmOpen(id);
  chk(aiConfirmOpenNow(), "la confirmacion se abre");
  { int x, y, w, hh; aiConfirmBtn(1, bx, by, bw, bh, x, y, w, hh);
    tReset(); T.tap = true; T.x = x + w / 2; T.y = y + hh / 2;
    chk(aiConfirmTick(bx, by, bw, bh), "el modal se queda el toque");
    chk(!aiConfirmOpenNow(), "y se cierra"); }
  { char body[32]; flexFsReadText("/Notas/a.txt", body, sizeof(body));
    chk(!strcmp(body, "original"), "cancelar NO escribio nada"); }

  // --- Un toque FUERA de los botones no cierra ni aplica ---
  aiConfirmOpen(id);
  tReset(); T.tap = true; T.x = bx + 2; T.y = by + 2;
  aiConfirmTick(bx, by, bw, bh);
  chk(aiConfirmOpenNow(), "tocar fuera no cierra: hay que decidir");
  { char body[32]; flexFsReadText("/Notas/a.txt", body, sizeof(body));
    chk(!strcmp(body, "original"), "y sigue sin escribir"); }

  // --- APLICAR si escribe ---
  { int x, y, w, hh; aiConfirmBtn(0, bx, by, bw, bh, x, y, w, hh);
    tReset(); T.tap = true; T.x = x + w / 2; T.y = y + hh / 2;
    aiConfirmTick(bx, by, bw, bh);
    chk(!aiConfirmOpenNow(), "aplicar cierra el modal"); }
  { char body[32]; flexFsReadText("/Notas/a.txt", body, sizeof(body));
    chk(!strcmp(body, "corregido"), "y AHORA si escribio"); }
  coworkDelete(id);

  // --- DOCUMENTO CAMBIADO: confirmacion DOBLE ---
  fakeFsReset(); fakeFsAdd("/Notas/d.txt", "lo que el usuario escribio despues", 0);
  uint32_t h2 = coworkHash("version vieja", 13);
  uint32_t id2 = coworkSubmit(CW_KIND_CORRECT, "C", "version vieja", 13, "/Notas/d.txt", h2, 0, 2000);
  coworkBeginWork(id2, 2000);
  coworkSetAction(id2, AI_ACT_REPLACE_TEXT, NULL);
  // huella distinta = el usuario escribio encima mientras se trabajaba
  coworkFinish(id2, "resultado del trabajo", 21, coworkHash("otra cosa", 9), false, 2100);
  chk(coworkFind(id2)->srcChanged, "se detecto que el documento cambio");

  aiConfirmOpen(id2);
  chk(gAiConfirmDouble, "por eso la confirmacion es DOBLE");
  chk(!gAiConfirmArmed, "y el primer boton todavia no aplica");
  { int x, y, w, hh; aiConfirmBtn(0, bx, by, bw, bh, x, y, w, hh);
    tReset(); T.tap = true; T.x = x + w / 2; T.y = y + hh / 2;
    aiConfirmTick(bx, by, bw, bh);
    chk(aiConfirmOpenNow(), "el primer toque NO cierra");
    chk(gAiConfirmArmed,    "solo acepta el aviso"); }
  { char body[64]; flexFsReadText("/Notas/d.txt", body, sizeof(body));
    chk(!strcmp(body, "lo que el usuario escribio despues"),
        "y NO ha escrito nada todavia"); }
  { int x, y, w, hh; aiConfirmBtn(0, bx, by, bw, bh, x, y, w, hh);
    tReset(); T.tap = true; T.x = x + w / 2; T.y = y + hh / 2;
    aiConfirmTick(bx, by, bw, bh);
    chk(!aiConfirmOpenNow(), "el segundo toque si aplica"); }
  { char body[64]; flexFsReadText("/Notas/d.txt", body, sizeof(body));
    chk(!strcmp(body, "resultado del trabajo"), "y ahora si escribio"); }

  tReset(); fakeFsReset();
  coworkAttachStorage(gTestJobs, CW_MAX_JOBS);
  gState = ST_HOME;
  if(!gFails) printf("  Confirmacion: todas las comprobaciones pasan.\n");
}

static void testChatFinal(){
  printf("El chat se completa solo\n");
  coworkAttachStorage(gTestJobs, CW_MAX_JOBS);
  aiChatEnsure();
  gChatN = 0;

  // --- RESPUESTA ---
  uint32_t id = coworkSubmit(CW_KIND_ANALYZE, "Consulta", "hola", 4, NULL, 0, 0, 1000);
  aiChatPush(true, "hola");
  aiChatPushPending("Trabajando en ello...", id);
  chk(gChatN == 2, "dos turnos");
  chk(gChat[1].jobId == id, "el segundo espera a su trabajo");
  chk(!aiChatResolve(), "mientras no termina, no cambia nada");
  coworkBeginWork(id, 1000);
  coworkSetAction(id, AI_ACT_SHOW_TEXT, NULL);
  coworkFinish(id, "esta es la respuesta", 20, 0, false, 1100);
  chk(aiChatResolve(), "al terminar, se resuelve");
  chk(!strcmp(gChat[1].text, "esta es la respuesta"), "y el turno lleva la RESPUESTA");
  chk(gChat[1].jobId == 0, "y deja de esperar");
  chk(coworkFind(id)->seen, "el resultado queda marcado como visto");
  coworkDelete(id);

  // --- ERROR ENTENDIBLE ---
  gChatN = 0;
  uint32_t id2 = coworkSubmit(CW_KIND_ANALYZE, "Consulta", "x", 1, NULL, 0, 0, 2000);
  aiChatPushPending("Trabajando en ello...", id2);
  coworkBeginWork(id2, 2000);
  coworkFail(id2, CW_ERR_NOCFG, "Sin configurar", 2000);
  chk(coworkFind(id2)->state == CW_ERROR, "el trabajo falla");
  chk(aiChatResolve(), "el chat se entera");
  chk(strstr(gChat[0].text, "No pude responder") != NULL, "y lo dice");
  chk(strstr(gChat[0].text, "Sin configurar") != NULL,   "con el motivo REAL, no un codigo");
  coworkDelete(id2);

  // --- RESULTADO QUE HAY QUE CONFIRMAR ---
  gChatN = 0;
  uint32_t id3 = coworkSubmit(CW_KIND_SUMMARY, "S", "x", 1, NULL, 0, 0, 3000);
  aiChatPushPending("Trabajando en ello...", id3);
  coworkBeginWork(id3, 3000);
  coworkSetAction(id3, AI_ACT_CREATE_NOTE, "Resumen");
  coworkFinish(id3, "el resumen", 10, 0, true, 3100);
  aiChatResolve();
  chk(strstr(gChat[0].text, "Listo") != NULL,   "dice que esta listo");
  chk(strstr(gChat[0].text, "confirma") != NULL, "y que hay que confirmarlo");
  coworkDelete(id3);

  // --- CANCELADO ---
  gChatN = 0;
  uint32_t id4 = coworkSubmit(CW_KIND_ANALYZE, "A", "x", 1, NULL, 0, 0, 4000);
  aiChatPushPending("Trabajando en ello...", id4);
  coworkCancel(id4);
  aiChatResolve();
  chk(!strcmp(gChat[0].text, "Cancelado."), "un trabajo cancelado se dice");
  coworkDelete(id4);

  // --- EL TRABAJO DESAPARECIO: no se deja el "estoy en ello" colgado ---
  gChatN = 0;
  uint32_t id5 = coworkSubmit(CW_KIND_ANALYZE, "A", "x", 1, NULL, 0, 0, 5000);
  aiChatPushPending("Trabajando en ello...", id5);
  coworkDelete(id5);
  chk(aiChatResolve(), "se resuelve igual");
  chk(strstr(gChat[0].text, "ya no est") != NULL, "diciendo que la tarea ya no esta");
  chk(gChat[0].jobId == 0, "y sin quedarse esperando para siempre");

  // --- el historial sigue acotado ---
  gChatN = 0;
  for(int i = 0; i < AI_CHAT_MAX + 5; i++) aiChatPush(i & 1, "linea");
  chk(gChatN == AI_CHAT_MAX, "el historial no pasa de su tope");

  gChatN = 0;
  if(!gFails) printf("  Chat: todas las comprobaciones pasan.\n");
}

static void testBusqueda(){
  printf("Busqueda local de archivos\n");
  coworkAttachStorage(gTestJobs, CW_MAX_JOBS);
  fakeFsReset();
  fakeFsAdd("/Notas/lista de la compra.txt", "leche pan huevos", 0);
  fakeFsAdd("/Notas/receta.txt",             "pan con tomate", 0);
  fakeFsAdd("/Documentos/informe.txt",       "resultados del trimestre", 0);
  fakeFsAdd("/Paint/dibujo.fpn",             "", 4096);
  // Lo que NO debe salir NUNCA:
  fakeFsAdd("/System/spell.txt",             "pan", 0);
  fakeFsAdd("/System/cowork.dat",            "pan", 0);
  fakeFsAdd("/.fxvault/d/1",                 "pan", 0);
  fakeFsAdd("/Papelera/viejo.txt",           "pan", 0);

  static char out[CW_OUT_MAX];
  uint32_t id = coworkSubmit(CW_KIND_FIND, "Buscar", "pan", 3, NULL, 0, 0, 1000);
  coworkBeginWork(id, 1000);

  int n = aiLocalFind(id, "pan", out, sizeof(out));
  chk(n > 0, "encuentra algo");
  // Por NOMBRE no coincide ninguno con "pan"; los dos que salen es por
  // CONTENIDO, que es justo lo que hay que probar.
  chk(strstr(out, "/Notas/lista de la compra.txt") != NULL, "encuentra por contenido (1)");
  chk(strstr(out, "/Notas/receta.txt") != NULL,             "encuentra por contenido (2)");
  chk(strstr(out, "informe") == NULL,                       "y no trae lo que no coincide");

  // LO QUE NO PUEDE SALIR, y es el punto de toda la funcion.
  chk(strstr(out, "/System") == NULL,   "NUNCA sale nada de /System");
  chk(strstr(out, "spell")   == NULL,   "ni el diccionario");
  chk(strstr(out, "cowork")  == NULL,   "ni el historial");
  chk(strstr(out, "fxvault") == NULL,   "NUNCA sale nada de Flex Vault");
  chk(strstr(out, "Papelera")== NULL,   "ni de la papelera");

  // --- por NOMBRE, y sin distinguir tildes ni mayusculas ---
  n = aiLocalFind(id, "RECETA", out, sizeof(out));
  chk(n == 1 && strstr(out, "receta.txt"), "busca por nombre sin distinguir mayusculas");

  // --- se dice el tamano, y si se puede abrir ---
  n = aiLocalFind(id, "dibujo", out, sizeof(out));
  chk(n == 1, "encuentra el dibujo");
  chk(strstr(out, "/Paint/dibujo.fpn") != NULL, "con su ruta");
  chk(strstr(out, "KB") != NULL || strstr(out, "B)") != NULL, "y su tamano");

  // Un fichero que ninguna app puede abrir se marca.
  fakeFsAdd("/Documentos/raro.bin", "", 100);
  n = aiLocalFind(id, "raro", out, sizeof(out));
  chk(n == 1 && strstr(out, "no se puede abrir") != NULL,
      "lo que ninguna app abre se lista marcado");

  // --- SIN RESULTADOS: se dice, y se dice DONDE se busco ---
  n = aiLocalFind(id, "zzzzznoexiste", out, sizeof(out));
  chk(n == 0, "sin resultados");
  chk(strstr(out, "Sin resultados") != NULL, "y lo dice");
  chk(strstr(out, "/Notas") != NULL,         "diciendo tambien donde busco");

  // --- CANCELACION ---
  coworkCancel(id);
  n = aiLocalFind(id, "pan", out, sizeof(out));
  chk(n == -1, "cancelar la busqueda la para de verdad");

  // --- LIMITE DE RESULTADOS ---
  { coworkAttachStorage(gTestJobs, CW_MAX_JOBS);
    fakeFsReset();
    char p[64];
    for(int i = 0; i < 30; i++){                   // mas que AIF_MAX_RESULTS
      snprintf(p, sizeof(p), "/Notas/comun%02d.txt", i);
      fakeFsAdd(p, "x", 0);
    }
    uint32_t id2 = coworkSubmit(CW_KIND_FIND, "B", "comun", 5, NULL, 0, 0, 2000);
    coworkBeginWork(id2, 2000);
    int k = aiLocalFind(id2, "comun", out, sizeof(out));
    chk(k <= AIF_MAX_RESULTS, "no devuelve mas resultados que su tope");
    chk(strstr(out, "acotada") != NULL, "y AVISA de que la lista esta cortada");
    coworkDelete(id2); }

  // --- ficheros grandes: no se abren para mirar dentro ---
  { fakeFsReset();
    fakeFsAdd("/Notas/enorme.txt", "pan", AIF_MAX_CONTENT + 1000);
    uint32_t id3 = coworkSubmit(CW_KIND_FIND, "B", "pan", 3, NULL, 0, 0, 3000);
    coworkBeginWork(id3, 3000);
    int k = aiLocalFind(id3, "pan", out, sizeof(out));
    chk(k == 0, "un fichero demasiado grande no se abre para buscar dentro");
    coworkDelete(id3); }

  // --- sin almacenamiento se dice, no se inventa ---
  { fakeFsReset();
    coworkAttachStorage(gTestJobs, CW_MAX_JOBS);
    uint32_t id4 = coworkSubmit(CW_KIND_FIND, "B", "x", 1, NULL, 0, 0, 4000);
    int k = aiLocalFind(id4, "x", out, sizeof(out));
    chk(k == 0 && strstr(out, "Sin almacenamiento") != NULL,
        "sin almacenamiento montado se dice"); }

  fakeFsReset();
  coworkAttachStorage(gTestJobs, CW_MAX_JOBS);
  if(!gFails) printf("  Busqueda de archivos: todas las comprobaciones pasan.\n");
}

static void testBateria(){
  printf("Pausa por bateria\n");
  coworkAttachStorage(gTestJobs, CW_MAX_JOBS);

  // Esta placa NO tiene telemetria: es el estado real y hay que
  // comprobar que en ese caso no pasa NADA.
  gBatt.valid = false; gBattPaused = false;
  chk(!flexBattAvailable(), "de fabrica no hay telemetria");
  chk(flexBattPercent() == -1, "y el porcentaje es 'no se sabe', no un numero");
  uint32_t id = coworkSubmit(CW_KIND_SUMMARY, "S", "x", 1, NULL, 0, 0, 1000);
  flexBattCoworkTick();
  chk(coworkFind(id)->state == CW_QUEUED, "sin telemetria no se pausa nada");

  // --- CON telemetria: se pausa por debajo del umbral ---
  gBatt.valid = true; gBatt.percent = 50; gBatt.charging = false;
  flexBattCoworkTick();
  chk(coworkFind(id)->state == CW_QUEUED, "al 50% no se pausa");
  gBatt.percent = AI_BATT_PAUSE_PCT - 1;
  flexBattCoworkTick();
  chk(coworkFind(id)->state == CW_PAUSED, "por debajo del umbral SI se pausa");
  chk(coworkFind(id)->pauseWhy == CW_PAUSE_BATTERY, "y con el motivo correcto");

  // --- HISTERESIS: no se reanuda en el mismo umbral ---
  gBatt.percent = AI_BATT_PAUSE_PCT + 1;
  flexBattCoworkTick();
  chk(coworkFind(id)->state == CW_PAUSED, "justo por encima del umbral NO se reanuda");
  gBatt.percent = AI_BATT_RESUME_PCT;
  flexBattCoworkTick();
  chk(coworkFind(id)->state == CW_QUEUED, "se reanuda al recuperar de verdad");

  // --- CARGANDO no se pausa, por bajo que este ---
  gBatt.percent = 5; gBatt.charging = true;
  flexBattCoworkTick();
  chk(coworkFind(id)->state == CW_QUEUED, "cargando no se pausa aunque este al 5%");
  gBatt.charging = false;
  flexBattCoworkTick();
  chk(coworkFind(id)->state == CW_PAUSED, "al desconectar, si");

  // --- LA REGLA QUE MAS IMPORTA ---
  // Lo que paro el USUARIO no lo reanuda la bateria. Ni la carga.
  coworkPause(id, CW_PAUSE_USER);
  gBatt.percent = 90;
  flexBattCoworkTick();
  chk(coworkFind(id)->state == CW_PAUSED,
      "recuperar bateria NO reanuda lo que paro el usuario");
  chk(coworkFind(id)->pauseWhy == CW_PAUSE_USER, "que sigue pausado por SU motivo");

  // Ni la pausa por juego/camara.
  coworkResume(id);
  coworkPauseAll(CW_PAUSE_LOAD);
  gBatt.percent = 10;
  flexBattCoworkTick();
  gBatt.percent = 90;
  flexBattCoworkTick();
  chk(coworkFind(id)->state == CW_PAUSED, "ni lo que pauso una app pesada");

  // --- si la fuente se cae, no se dejan trabajos colgados ---
  coworkResumeAll(CW_PAUSE_LOAD);
  gBatt.percent = 5; gBatt.valid = true; gBatt.charging = false;
  flexBattCoworkTick();
  chk(coworkFind(id)->state == CW_PAUSED, "pausado por bateria");
  gBatt.valid = false;                       // la fuente desaparece
  flexBattCoworkTick();
  chk(coworkFind(id)->state == CW_QUEUED,
      "si la telemetria se cae, no se queda pausado para siempre");

  coworkDelete(id);
  gBatt.valid = false; gBattPaused = false;
  coworkAttachStorage(gTestJobs, CW_MAX_JOBS);
  if(!gFails) printf("  Bateria: todas las comprobaciones pasan.\n");
}

static void testNoHorizontal(){
  printf("Flex Intelligence NO asoma en horizontal\n");
  coworkAttachStorage(gTestJobs, CW_MAX_JOBS);
  aiConfigDefaults();
  gState = ST_HOME; qsPanelY = 0; editMode = false; kioskOn = false;
  gHosted = false; aiOvOpen = false;
  // flexAiPump se rinde si no hay almacen conectado (en la placa lo pone
  // flexAiBegin); aqui se apunta al array de la prueba.
  gCwSlots = gTestJobs;

  // --- vertical: todo funciona ---
  gLand = false;
  chk(flexAiUiAllowed(), "en vertical la interfaz esta permitida");
  chk(flexAiCanOpen(),   "y se puede abrir");

  // --- horizontal: nada ---
  gLand = true;
  chk(!flexAiUiAllowed(), "en horizontal NO esta permitida");
  chk(!flexAiCanOpen(),   "y NO se puede abrir");

  // La pulsacion larga tampoco activa, en ninguno de los dos modos.
  for(int mode = 0; mode <= 1; mode++){
    gNavMode = mode;
    gAiLp = AIL_IDLE; gAiLpConsumed = false;
    tDown(SCR_W / 2, SCR_H - 20, 1000);
    flexAiLongPressTick();
    tMove(SCR_W / 2, SCR_H - 20, 1800);
    chk(!flexAiLongPressTick(), mode == 0 ? "horizontal + botones: no activa"
                                          : "horizontal + gestos: no activa");
    chk(!aiOvOpen, "y el panel sigue cerrado");
  }
  gNavMode = 0;

  // Ni la tarjeta flotante.
  gState = ST_APP;
  gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs));
  notifPushJob(77, "Resumen listo", "", NACT_VIEW, false);
  chk(!nflWanted(), "una tarjeta de Cowork NO sale encima de una app horizontal");
  gLand = false;
  chk(nflWanted(),  "y en vertical si");

  // Embebida en una ventana de Modo PC, tampoco.
  gHosted = true;
  chk(!flexAiUiAllowed(), "embebida en Modo PC tampoco");
  chk(!nflWanted(),       "ni su tarjeta");
  gHosted = false;

  // --- EL RESULTADO NO SE PIERDE: se guarda y se anuncia al volver ---
  gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs));
  gAnnN = 0;
  uint32_t id = coworkSubmit(CW_KIND_SUMMARY, "S", "x", 1, NULL, 0, 0, 1000);
  coworkBeginWork(id, 1000);
  coworkFinish(id, "resultado", 9, 0, false, 1100);
  gLand = true;
  gTestMs = 100000;
  flexAiPump();
  chk(gNotifCount == 0, "en horizontal no se anuncia nada");
  chk(coworkFind(id)->state == CW_DONE, "pero el resultado sigue guardado");
  gLand = false;
  gTestMs = 200000;
  flexAiPump();
  chk(gNotifCount == 1, "al volver a vertical, SE ANUNCIA");
  chk(gNotifs[0].jobId == id, "y es el trabajo que habia terminado");

  // Limpieza.
  gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs)); gAnnN = 0;
  coworkDelete(id);
  gState = ST_HOME; gLand = false; tReset();
  coworkAttachStorage(gTestJobs, CW_MAX_JOBS);
  if(!gFails) printf("  Sin interferencia en horizontal: todas las comprobaciones pasan.\n");
}

// #############################################################
//  UNA SOLA NOTIFICACION FLOTANTE, Y NUNCA SOBRE EL PIN
//  ------------------------------------------------------------
//  Regresion de los fallos que se ven en el video de la placa:
//    · tres tarjetas apiladas encima de los widgets del escritorio,
//      con dos botones de cerrar a la vez;
//    · una tarjeta de "Dispositivo I2C" dibujada ENCIMA del teclado
//      del PIN (nflWanted comprobaba ST_LOCK pero no ST_LOCKSETUP);
//    · el mismo dispositivo encolado tres veces por el rescaneo I2C;
//    · la banda de la isla llegando hasta y=284, solapada con la
//      banda de la rejilla (HOME_BAND_TOP = 206), que es lo que
//      mezclaba pagina vieja y pagina nueva al deslizar.
//
//  Las comprobaciones de pixeles son de verdad: se compone en los
//  framebuffers reales y se compara fb contra homeBuf.
// #############################################################
static void testNotifUnaSola(){
  printf("Notificaciones: una a la vez, y nunca sobre el PIN\n");
  gNotifCount = 0; notifDragIdx = -1; notifBandOn = false; notifPaused = false;
  memset(gNotifs, 0, sizeof(gNotifs));
  gState = ST_HOME; qsPanelY = 0; editMode = false; gLand = false; gHosted = false;
  hpDragging = false; hpSettling = false;

  // --- 1. LAS DOS BANDAS NO PUEDEN PISARSE ---
  // Es la comprobacion estructural: mientras la isla acabe por encima de
  // donde empieza la rejilla, ningun ajuste de coordenadas arregla el
  // deslizamiento entre paginas, porque son dos compositores escribiendo
  // en bbuf sobre las mismas filas.
  chk(NOTIF_BAND_BOT <= HOME_BAND_TOP,
      "la banda de la isla acaba antes de que empiece la de la rejilla");
  chk(NOTIF_VISIBLE == 1, "solo se dibuja UNA tarjeta a la vez");
  chk(NOTIF_MAX >= NOTIF_VISIBLE, "y la cola puede guardar mas de las que se ven");
  chk(NOTIF_Y0 + NOTIF_CARD_H <= NOTIF_BAND_BOT,
      "la tarjeta visible cabe entera dentro de su banda");

  // --- 2. PANTALLAS SEGURAS ---
  // La del video: el teclado del PIN. Y las demas de la misma familia.
  { const struct { int st; const char* q; } seg[] = {
      { ST_SPLASH,           "durante el arranque" },
      { ST_OOBE_LANG,        "en la primera configuracion (idioma)" },
      { ST_OOBE_NAME,        "en la primera configuracion (nombre)" },
      { ST_LOCK,             "en la pantalla de bloqueo" },
      { ST_LOCKSETUP,        "en el TECLADO DEL PIN" },
      { ST_VAULT,            "en Flex Vault" },
      { ST_POWEROFF_CONFIRM, "apagando (confirmacion)" },
      { ST_POWEROFF_ANIM,    "apagando (animacion)" } };
    gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs));
    notifPushJob(70, "Dispositivo I2C", "0x18 detectado", NACT_VIEW, false);
    for(unsigned k = 0; k < sizeof(seg) / sizeof(seg[0]); k++){
      gState = seg[k].st;
      char q[96];
      snprintf(q, sizeof(q), "no se notifica %s", seg[k].q);
      chk(notifSecureScreen(), q);
      snprintf(q, sizeof(q), "y la tarjeta flotante tampoco sale %s", seg[k].q);
      chk(!nflWanted(), q);
    }
    gState = ST_APP;
    chk(!notifSecureScreen(), "encima de una app normal si se notifica");
    chk(nflWanted(),          "y la tarjeta flotante sale"); }

  // --- 3. LA COLA NO SE PIERDE MIENTRAS SE DESBLOQUEA ---
  // Un aviso que caduca mientras tecleas el PIN es un aviso que nunca
  // llegaste a ver. Se queda sin armar y arranca al volver al escritorio.
  { gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs));
    notifBandOn = false; notifPaused = false; notifLastMs = 0;
    gTestMs = 100000;
    gState = ST_LOCKSETUP;
    notifPushJob(71, "Resumen listo", "Se creo una nota", NACT_VIEW, false);
    for(int f = 0; f < 30; f++){ gTestMs += 40; notifTick(); }
    chk(gNotifCount == 1,       "el aviso espera en la cola mientras se teclea el PIN");
    chk(!gNotifs[0].armed,      "sin armar: su cuenta atras no ha empezado");
    chk(gNotifs[0].phase != NP_OUT, "y no ha caducado");
    gState = ST_HOME;
    gTestMs += 40; notifTick();
    chk(gNotifs[0].armed,       "al volver al escritorio SI se arma");
    chk(gNotifs[0].bornMs == (uint32_t)gTestMs, "y su cuenta atras arranca ahora"); }

  // --- 4. UNA SOLA TARJETA DIBUJADA, AUNQUE HAYA TRES EN LA COLA ---
  { gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs));
    notifBandOn = false; notifPaused = false; notifLastMs = 0;
    gState = ST_HOME; gTestMs = 200000;
    notifPushJob(80, "Primera",  "", NACT_VIEW, false);
    notifPushJob(81, "Segunda",  "", NACT_VIEW, false);
    notifPushJob(82, "Tercera",  "", NACT_VIEW, false);
    chk(gNotifCount == 3, "las tres entran en la cola");

    // Fondo conocido y fb igual a el: cualquier pixel distinto despues
    // del tick es algo que ha dibujado la isla.
    drawWallpaper(homeBuf, false);
    memcpy(fb, homeBuf, (size_t)SCR_W * SCR_H * 2);
    setBuf(fb);
    gTestMs += 40; notifTick();
    gTestMs += 300; notifTick();          // ya pasada la animacion de entrada

    chk(gNotifs[0].armed,  "la primera de la cola se arma");
    chk(!gNotifs[1].armed, "la segunda espera su turno");
    chk(!gNotifs[2].armed, "y la tercera tambien");

    // Pixeles: la banda de la isla ha cambiado; TODO lo de debajo, no.
    int cambiadosDentro = 0, cambiadosFuera = 0;
    for(int y = 0; y < SCR_H; y++)
      for(int x = 0; x < SCR_W; x++)
        if(fb[(size_t)y * SCR_W + x] != homeBuf[(size_t)y * SCR_W + x]){
          if(y >= NOTIF_BAND_TOP && y < NOTIF_BAND_BOT) cambiadosDentro++;
          else                                          cambiadosFuera++;
        }
    chk(cambiadosDentro > 2000, "la tarjeta visible se ha dibujado de verdad");
    chk(cambiadosFuera == 0,
        "y NI UN PIXEL fuera de su banda: ni segunda tarjeta, ni widgets tocados");

    // Y la banda no llega a los widgets del escritorio (que empiezan a
    // media pantalla) ni a la rejilla.
    chk(NOTIF_BAND_BOT < HOME_GY0, "la isla no alcanza la rejilla de apps"); }

  // --- 5. DEDUPLICACION POR ORIGEN E IDENTIDAD ---
  { gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs));
    gTestMs = 300000;
    DetectedModule m; memset(&m, 0, sizeof(m));
    m.active = true; m.type = MOD_I2C_GENERIC; m.i2cAddr = 0x18;
    snprintf(m.name, sizeof(m.name), "Dispositivo I2C");
    snprintf(m.sub,  sizeof(m.sub),  "0x18 detectado");
    notifPush(&m);
    chk(gNotifCount == 1, "el dispositivo se anuncia una vez");
    notifPush(&m);
    notifPush(&m);
    chk(gNotifCount == 1, "y el rescaneo I2C NO lo encola tres veces");
    // El texto secundario cambia pero es el MISMO dispositivo: refresca.
    snprintf(m.sub, sizeof(m.sub), "0x18 listo");
    notifPush(&m);
    chk(gNotifCount == 1, "un cambio de subtitulo refresca, no duplica");
    chk(!strcmp(gNotifs[0].mod.sub, "0x18 listo"), "y el texto es el nuevo");
    // Otra direccion I2C SI es otro dispositivo.
    m.i2cAddr = 0x76; snprintf(m.name, sizeof(m.name), "Sensor BME280");
    notifPush(&m);
    chk(gNotifCount == 2, "otro dispositivo si es otro aviso");

    // Cowork: el mismo trabajo avisa varias veces y es la MISMA tarjeta.
    gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs));
    notifPushJob(90, "Trabajando", "Resumiendo", NACT_NONE, false);
    notifPushJob(90, "Resumen listo", "Se creo una nota", NACT_VIEW, false);
    chk(gNotifCount == 1,              "el mismo trabajo no encola dos tarjetas");
    chk(gNotifs[0].act == NACT_VIEW,   "la tarjeta se actualiza con el boton nuevo");
    notifPushJob(91, "Otro trabajo", "", NACT_VIEW, false);
    chk(gNotifCount == 2,              "otro trabajo si es otra tarjeta"); }

  // --- 6. DESCARTAR LA TARJETA NO CANCELA NI BORRA EL TRABAJO ---
  { coworkAttachStorage(gTestJobs, CW_MAX_JOBS);
    gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs)); notifDragIdx = -1;
    uint32_t id = coworkSubmit(CW_KIND_SUMMARY, "S", "x", 1, NULL, 0, 0, 1000);
    coworkBeginWork(id, 1000);
    coworkFinish(id, "resultado", 9, 0, false, 1100);
    notifPushJob(id, "Resumen listo", "", NACT_VIEW, false);
    gNotifs[0].armed = true; gNotifs[0].phase = NP_IDLE;
    gState = ST_HOME; qsPanelY = 0; editMode = false;
    int cardY = NOTIF_Y0 + 10;
    tDown(NOTIF_MARGIN_X + 100, cardY, 4000);
    notifHandleTouch();
    tMove(NOTIF_MARGIN_X + 100 - (NOTIF_CARD_W / 2), cardY, 4100);
    notifHandleTouch();
    tUp(4200, false);
    notifHandleTouch();
    chk(gNotifs[0].phase == NP_OUT,             "el deslizamiento descarta la tarjeta");
    CoworkJob* j = coworkFind(id);
    chk(j != NULL && j->state == CW_DONE,       "pero el trabajo sigue terminado");
    chk(j != NULL && j->outLen > 0,             "y su resultado sigue guardado");
    coworkDelete(id); }

  // --- 7. UNA CAPTURA DE FONDO MUERTA NO SE DEVUELVE ---
  // Es la otra cara del mismo fallo: si se bloquea con una tarjeta
  // puesta, devolver el rectangulo guardado estamparia un recorte de la
  // app ENCIMA del teclado del PIN.
  { gState = ST_APP; gLand = false;
    nflHasBg = true; nflLandSaved = false; nflSavedState = ST_APP;
    chk(nflBgStillValid(), "en la misma pantalla la captura sigue valiendo");
    gState = ST_LOCKSETUP;
    chk(!nflBgStillValid(), "al pasar al teclado del PIN, ya no");
    gState = ST_APP; gLand = true;
    chk(!nflBgStillValid(), "y al girar a horizontal, tampoco");
    gLand = false; nflForgetBg();
    chk(!nflHasBg && nflSavedState < 0, "olvidarla la deja del todo limpia"); }

  // Limpieza.
  gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs));
  notifDragIdx = -1; notifBandOn = false; notifPaused = false;
  gState = ST_HOME; gLand = false; tReset();
  if(!gFails) printf("  Una notificacion a la vez: todas las comprobaciones pasan.\n");
}

// #############################################################
//  DESLIZAMIENTO ENTRE PAGINAS: CADA PAGINA EN SU VIEWPORT
//  ------------------------------------------------------------
//  En el video se ve, a mitad del gesto, la columna de la pagina
//  vieja mezclada con el icono de la nueva y las etiquetas
//  superpuestas. La causa no era la geometria del gesto sino que
//  MAS DE UN COMPOSITOR escribia en las mismas filas de bbuf en el
//  mismo frame, cada uno con su propio desplazamiento.
//
//  Aqui se comprueba lo unico que garantiza que eso no pase:
//    · los dos viewports son COMPLEMENTARIOS -- juntos cubren el
//      ancho exactamente una vez, sin solape y sin hueco;
//    · un frame compuesto de verdad no conserva NI UN PIXEL de lo
//      que hubiera antes en el buffer de composicion;
//    · y los demas dibujantes del escritorio callan mientras dura
//      el gesto.
// #############################################################
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

  // --- 2. UN FRAME REAL NO DEJA RASTRO DEL ANTERIOR ---
  // homeBuf y hpBuf se rellenan con dos colores planos distintos, y
  // bbuf con un tercero que NO debe sobrevivir en ninguna fila de la
  // banda. Es la comprobacion de pixeles del "resto del frame anterior".
  { const uint16_t COL_A = 0x1234, COL_B = 0x4321, VENENO = 0x7BEF;
    if(!hpEnsureBuf()){ chk(false, "hay lienzo para la pagina vecina"); }
    else {
      for(size_t i = 0; i < (size_t)SCR_W * SCR_H; i++){
        homeBuf[i] = COL_A;
        hpBuf[i]   = COL_B;
      }
      hpBufPage = 1; hpFrom = 0; hpTo = 1;    // hacia la izquierda: la 1 entra por la derecha
      int malos = 0, cortes = 0;
      for(int dx = -SCR_W + 1; dx <= -1; dx += 37){
        for(size_t i = 0; i < (size_t)SCR_W * SCR_H; i++) bbuf[i] = VENENO;
        hpRenderFrame(dx);
        for(int y = HOME_BAND_TOP; y < HOME_BAND_BOT; y++){
          // Los puntos de pagina se dibujan encima de su propia franja:
          // ahi hay pixeles legitimos que no son ni A ni B.
          if(y >= HOME_DOTS_Y - 8 && y <= HOME_DOTS_Y + 10) continue;
          const uint16_t* row = bbuf + (size_t)y * SCR_W;
          for(int x = 0; x < SCR_W; x++) if(row[x] == VENENO) malos++;
          // La frontera entre las dos paginas cae donde toca, y hay
          // exactamente UNA: ni dos trozos de la pagina vieja sueltos.
          int c = 0;
          for(int x = 1; x < SCR_W; x++) if(row[x] != row[x - 1]) c++;
          if(c != 1) cortes++;
        }
      }
      chk(malos == 0,  "no queda ni un pixel del frame anterior en la banda");
      chk(cortes == 0, "y hay UNA sola frontera por fila: no hay trozos sueltos");

      // El corte esta exactamente en SCR_W+dx: la pagina vieja a la
      // izquierda, la nueva a la derecha.
      for(size_t i = 0; i < (size_t)SCR_W * SCR_H; i++) bbuf[i] = VENENO;
      hpRenderFrame(-200);
      const uint16_t* row = bbuf + (size_t)(HOME_GY0 + 10) * SCR_W;
      chk(row[SCR_W - 200 - 1] == COL_A, "justo antes del corte, la pagina que sale");
      chk(row[SCR_W - 200]     == COL_B, "y justo despues, la que entra");

      // Sin lienzo vecino compuesto no se deja ni una columna sin
      // escribir: el hueco se rellena con fondo, no con lo que hubiera.
      hpBufPage = -1;
      for(size_t i = 0; i < (size_t)SCR_W * SCR_H; i++) bbuf[i] = VENENO;
      hpRenderFrame(-200);
      int huecos = 0;
      for(int y = HOME_BAND_TOP; y < HOME_BAND_BOT; y++)
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
      if(y >= HOME_BAND_TOP && y < HOME_BAND_BOT) continue;
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
    notifPushJob(95, "Resumen listo", "", NACT_VIEW, false);
    gState = ST_HOME; qsPanelY = 0; editMode = false;
    hpDragging = true;
    gTestMs += 40; notifTick();
    chk(!gNotifs[0].armed, "con un gesto de pagina en curso, la isla no dibuja");
    chk(notifPaused,       "y contabiliza la pausa para no comerse los 5 s");
    hpDragging = false;
    gTestMs += 40; notifTick();
    chk(gNotifs[0].armed,  "al acabar el gesto, la isla vuelve");
    gNotifCount = 0; memset(gNotifs, 0, sizeof(gNotifs)); }

  // --- 5. FLEX INTELLIGENCE EN UNA CASILLA NORMAL ---
  // Nada de centrarla porque sea la unica de su pagina: mismos margenes
  // y misma casilla que cualquier otra app.
  { int x0, y0, x1, y1;
    homeSlotXY(0, x0, y0);
    homeSlotXY(1, x1, y1);
    chk(x0 == HOME_GX0,               "la primera casilla usa el margen normal");
    chk(y0 == HOME_GY0,               "y la fila normal");
    chk(x1 - x0 == HOME_COLSTEP,      "y el paso entre columnas es el de siempre");
    int id;
    gHomePage = 1;
    chk(hitHomeIcon(HOME_GX0 + 10, HOME_GY0 + 10, id) && id == IC_FLEXAI,
        "Flex Intelligence responde en la primera casilla de su pagina");
    chk(!hitHomeIcon(SCR_W / 2, HOME_GY0 + HOME_ROWSTEP, id) || id != IC_FLEXAI,
        "y NO esta centrada artificialmente en su pagina");
    gHomePage = 0; }

  chk(HOME_PAGES == 3, "siguen siendo tres paginas");

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
//  EL TECLADO DE FLEX INTELLIGENCE ES DEL PANEL, NO DEL FONDO
//  ------------------------------------------------------------
//  En el video se ve el teclado dibujado directamente sobre el
//  escritorio: encima de el se leen los iconos del dock, y no hay
//  ni panel, ni campo de texto, ni fila de sugerencias.
//
//  Tres causas, las tres comprobables aqui:
//    · flexAiRender() publicaba solo la ventana de la app
//      (WIN_TOP..WIN_BOT = 96..735), pero el panel de teclas llega
//      hasta la ultima fila de la PANTALLA (799). Las ultimas ~60
//      filas -- la fila de funciones -- no se subian nunca, y ahi
//      seguia lo que hubiera antes;
//    · kbExtrasOn no se encendia nunca en esta app, asi que el
//      panel salia sin barra ni fila de sugerencias (Notas si la
//      encendia). El corrector local trabajaba sin sitio donde
//      enseñar el resultado;
//    · la barra de entrada se ocultaba justo al abrir el teclado
//      (`gAiTab == AIT_CHAT && !gAiKbOn`), asi que se escribia a
//      ciegas.
// #############################################################
static void testTecladoIA(){
  printf("Teclado de Flex Intelligence\n");
  aiConfigDefaults();
  gLand = false; gHosted = false; gState = ST_APP; gAppId = IC_FLEXAI;
  gAiTab = AIT_CHAT;
  aiKbShow(false);

  int bx, by, bw, bh; uiBox(bx, by, bw, bh);

  // --- 1. LO QUE SE COMPONE ES LO QUE SE PUBLICA ---
  chk(aiRenderBottom(by, bh) == by + bh - 1,
      "sin teclado, se publica exactamente la ventana de la app");
  aiKbShow(true);
  chk(gAiKbOn, "el teclado se abre");
  chk(kbRowsTop() + 4 * (KB_KH + KB_GAP) > by + bh,
      "el panel de teclas SI baja por debajo de la ventana de la app");
  chk(aiRenderBottom(by, bh) == SCR_H - 1,
      "con teclado, se publica hasta la ultima fila de la pantalla");
  chk(aiRenderBottom(by, bh) >= kbRowsTop() + 4 * (KB_KH + KB_GAP) - 1 ||
      aiRenderBottom(by, bh) == SCR_H - 1,
      "es decir: la fila de funciones entra en el volcado");

  // --- 2. BARRA Y FILA DE SUGERENCIAS ENCENDIDAS ---
  chk(kbExtrasOn, "con el teclado abierto, el panel lleva sus extras");
  chk(kbTopH() > 0, "y por tanto tiene una franja propia por encima de las teclas");
  chk(kbPanelTop() < kbRowsTop(), "la franja de extras queda ARRIBA, sin comerse las teclas");
  chk(kbChipsY() >= kbToolbarY(), "los chips van debajo de la barra, no encima de ella");
  chk(kbChipsY() + kbChipsH() <= kbRowsTop(),
      "y la fila de sugerencias acaba antes de la primera fila de teclas");

  // --- 3. EL CAMPO DE TEXTO SE VE MIENTRAS SE ESCRIBE ---
  { int ey = aiInputTop(by, bh);
    chk(ey + AI_INPUT_H <= kbPanelTop(),
        "con teclado, la barra de entrada cabe justo encima del panel");
    chk(ey > by + 84, "y por debajo de las pestanas: no las tapa");
    aiKbShow(false);
    int ey2 = aiInputTop(by, bh);
    chk(ey2 + AI_INPUT_H <= by + bh, "sin teclado, la barra sigue dentro de la ventana");
    chk(ey2 != ey, "y las dos posiciones son distintas: la barra SUBE con el teclado"); }

  // --- 4. DIBUJO Y TACTO USAN LA MISMA GEOMETRIA ---
  // Es lo que evita el clasico "se ve aqui y responde alla".
  { aiKbShow(true);
    int ey = aiInputTop(by, bh);
    int antes = ey;
    aiKbShow(false);
    aiKbShow(true);
    chk(aiInputTop(by, bh) == antes, "la posicion de la barra es estable, no depende del orden"); }

  // --- 5. AL CERRAR, EL TECLADO NO DEJA NADA ENCENDIDO ---
  aiKbShow(false);
  chk(!gAiKbOn,      "el teclado se cierra");
  chk(!kbExtrasOn,   "y apaga sus extras: la siguiente pantalla no los hereda");
  chk(kbTopH() == 0, "la franja de extras desaparece con el");

  // --- 6. LAS TECLAS SIGUEN SIENDO TOCABLES ---
  // El area sensible de una tecla es su paso completo, no el rectangulo
  // pintado: no puede haber huecos muertos entre teclas.
  { aiKbShow(true);
    chk(KB_KH >= 44, "cada tecla mide al menos 44 px de alto");
    int c0 = kbCellAt(KB_X + 2, KB_Y + 2);
    int c1 = kbCellAt(KB_X + KB_KW + 1, KB_Y + 2);      // justo en la separacion
    chk(c0 == 0,  "la primera tecla responde");
    chk(c1 >= 0,  "y la separacion entre teclas tambien pertenece a alguna");
    aiKbShow(false); }

  // Limpieza.
  aiKbShow(false);
  gState = ST_HOME; gAppId = 0; gAiTab = AIT_CHAT;
  kbExtrasOn = false; kbApplySize();
  tReset();
  if(!gFails) printf("  Teclado de Flex Intelligence: todas las comprobaciones pasan.\n");
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
  testLongPress();
  testPaginasHome();
  testNotifCowork();
  testCorrectorTeclado();
  testPrivacidadVault();
  testAppId16();
  testAcciones();
  testConfirmacion();
  testChatFinal();
  testBusqueda();
  testBateria();
  testNoHorizontal();
  testNotifUnaSola();
  testDeslizarPaginas();
  testCabeceras();
  testListasConScroll();
  testTarjetaCronometro();
  testTecladoIA();
  if(gFails){ printf("%d comprobacion(es) han fallado.\n", gFails); return 1; }
  return 0;
}
