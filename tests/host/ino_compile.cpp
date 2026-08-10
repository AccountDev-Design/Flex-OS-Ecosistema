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
  return 0;
}
