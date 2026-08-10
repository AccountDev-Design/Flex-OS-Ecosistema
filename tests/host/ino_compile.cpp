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
  if(gFails){ printf("%d comprobacion(es) han fallado.\n", gFails); return 1; }
  return 0;
}
