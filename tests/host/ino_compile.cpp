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
  if(gFails){ printf("%d comprobacion(es) han fallado.\n", gFails); return 1; }
  return 0;
}
