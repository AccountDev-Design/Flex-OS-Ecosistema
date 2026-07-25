// #############################################################
// ##  FlexOS Ultra  ·  ESP32-P4  ·  GUITION JC4880P443C_I_W
// ##  MIPI-DSI 480x800 (ST7701)  ·  GT911 tactil  ·  NATIVO
// #############################################################
//
//  QUE ES ESTE ARCHIVO
//  -------------------
//  Sistema operativo NUEVO, escrito DESDE CERO, a resolucion
//  NATIVA 480x800 (sin el modo puente 320x480 de ArduOS).
//
//  Lo UNICO que se reutiliza de ArduOS Z Ultra Pro v3.45-P4 es
//  la CAPA DE HARDWARE, porque son datos del fabricante que no
//  tiene sentido "reinventar" (y que ya estan probados en tu
//  placa). En concreto:
//
//    1) ENCENDER LA PANTALLA  -> flexPanelInit()
//         LDO canal 3 @2.5V (PHY MIPI) + bus DSI 2 lanes @500Mbps
//         + panel DPI 480x800 @34MHz + tabla DCS del ST7701.
//    2) MOSTRAR COLORES       -> el mecanismo de "presenter":
//         un framebuffer en PSRAM + una tarea en el core 0 que
//         sube las filas sucias al panel (esp_lcd_panel_draw_bitmap).
//         AQUI reescrito NATIVO (una sola resolucion, sin escalado).
//    3) HACER FUNCIONAR EL TACTIL -> gt* + flexTouchInit()
//         GT911 por I2C (SDA=7, SCL=8, RST=3), coords 0..479 x
//         0..799 ya calibradas de fabrica -> se usan DIRECTAS.
//
//  TODO LO DEMAS (motor grafico de alto nivel, fuentes, iconos,
//  gestos, arranque, OOBE, bloqueo, home, apps, ajustes...) es
//  original de FlexOS Ultra y NO proviene de ArduOS.
//
//  ENTORNO (identico a tu ArduOS-P4, no lo cambies):
//    Arduino IDE 2.3.10 · core arduino-esp32 v3.2.0 EXACTO
//    Board: ESP32P4 Dev Module · 360MHz · Flash 80MHz/QIO/16MB
//    PSRAM: Enabled · USB Mode: USB-OTG (TinyUSB)
//    Particion: una que incluya SPIFFS (aunque aun no se use)
//
//  DEPURACION SIN PC (trabajas solo desde el movil):
//    Si algo peta antes de dibujar, el motivo del ultimo reinicio
//    se muestra en una BANDA FORENSE en pantalla al bootear (abajo
//    del todo). Ver showBootBanner().
//
//  ESTADO: Milestone 1 (arranque + OOBE + bloqueo + escritorio).
//  Las apps y Ajustes llegan en los siguientes milestones. Ver el
//  bloque "HOJA DE RUTA" al final del archivo.
// #############################################################

#include <Wire.h>
#include <Preferences.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_heap_caps.h"
#include "esp_system.h"          // esp_reset_reason() para la banda forense
#include <WiFi.h>                // pila WiFi (transporte hosted C6 por debajo, AUTO)
#include "esp_task_wdt.h"        // TWDT: esp_task_wdt_reset() en loop()

// -------------------------------------------------------------
//  Tipos usados como PARAMETRO de alguna funcion (FGlyph en fgPix/
//  drawGlyphScaled, PWin en pcDrawWindow). Van AQUI ARRIBA DEL TODO
//  a proposito: el IDE de Arduino auto-genera prototipos de todas
//  las funciones y los inserta al inicio del archivo compilado; si
//  el tipo se define mas abajo, ese prototipo autogenerado no lo
//  conoce todavia y la compilacion falla con "does not name a type"
//  aunque en este .ino el tipo aparezca "antes" de usarse. Definir
//  estos tipos aqui arriba evita el problema pase lo que pase.
// -------------------------------------------------------------
typedef struct { uint8_t w, h; int8_t bx; int8_t topoff; uint8_t adv; uint16_t off; } FGlyph;
struct PWin { bool open; int x, y, w, h, app; };

// =============================================================
// GEOMETRIA NATIVA
// =============================================================
#define SCR_W   480
#define SCR_H   800
// Modo horizontal (PC): coords logicas landscape 800x480, rotadas 90 al panel
#define LW      SCR_H
#define LH      SCR_W

// =============================================================
// PINES (confirmados en la JC4880P443, reutilizados de ArduOS)
// =============================================================
#define PIN_LCD_RST   5     // reset del ST7701
#define PIN_LCD_BL    23    // backlight (encendido fijo)
#define PIN_TP_SDA    7     // GT911 SDA
#define PIN_TP_SCL    8     // GT911 SCL
#define PIN_TP_RST    3     // GT911 reset

// #############################################################
// ##  ISLA DINAMICA · tipos y estado  (FASE 1: solo la isla)
// ##  ------------------------------------------------------
// ##  Se definen ARRIBA (antes de cualquier funcion) a proposito:
// ##  las funciones de la isla toman punteros a estos structs, y
// ##  el auto-prototipado de Arduino (ctags) inserta prototipos al
// ##  principio del sketch. Si los tipos no existieran aun, esos
// ##  prototipos no compilarian. Definiendolos aqui, todo prototipo
// ##  generado ya conoce ModuleType/DetectedModule/Notification.
// #############################################################

// ---- Tipos de modulo (compartidos con la futura deteccion I2C, Fase 2) ----
enum ModuleType {
  MOD_UNKNOWN,
  MOD_ULTRASONIC,   // HC-SR04
  MOD_BME280,       // sensor I2C
  MOD_MPU6050,      // IMU I2C
  MOD_LED,
  MOD_BUTTON,
  MOD_SERVO,
  MOD_I2C_GENERIC
};

// Un modulo detectado (o simulado en Fase 1)
struct DetectedModule {
  ModuleType    type;
  char          name[24];       // "Sensor BME280"
  char          sub[28];        // "I2C 0x76 detectado"
  uint8_t       i2cAddr;        // 0 si no es I2C
  uint8_t       pins[4];        // reservado para Fase 2 (asignacion de pines)
  uint8_t       numPins;
  bool          active;
  unsigned long detectedAt;
};

// ---- Geometria de la isla ----
#define NOTIF_MAX         3
#define NOTIF_MARGIN_X    16
#define NOTIF_CARD_W      (SCR_W - 2 * NOTIF_MARGIN_X)                        // 448
#define NOTIF_CARD_H      64
#define NOTIF_GAP         10
#define NOTIF_RAD         28
#define NOTIF_Y0          56                                                  // borde sup. de la 1a tarjeta
#define NOTIF_ENTER_DROP  24                                                  // caida de la animacion de entrada (px)
#define NOTIF_HOLD_MS     5000                                                // ms visible antes de auto-descartarse
#define NOTIF_BAND_TOP    (NOTIF_Y0 - NOTIF_ENTER_DROP - 6)                   // 26
#define NOTIF_BAND_BOT    (NOTIF_Y0 + NOTIF_MAX * (NOTIF_CARD_H + NOTIF_GAP) + 6) // 284
#define NOTIF_BAND_H      (NOTIF_BAND_BOT - NOTIF_BAND_TOP)                   // 258

// ---- Fase de animacion de cada notificacion ----
enum NotifPhase { NP_IN, NP_IDLE, NP_DRAG, NP_OUT, NP_SPRING };

struct Notification {
  DetectedModule mod;
  bool           active;
  NotifPhase     phase;
  uint32_t       bornMs;        // inicio de la animacion de entrada (se fija al ARMARSE en Home)
  float          slideX;        // desplazamiento horizontal (descarte); 0 = en su sitio
  bool           armed;         // false = encolada pero aun no mostrada; se arma al verse en Home
};

// ---- Estado global de la cola ----
static Notification gNotifs[NOTIF_MAX];
static int          gNotifCount  = 0;
static bool         notifBandOn  = false;   // la banda tiene isla activa (o le debe un ultimo frame de limpieza)
static int          notifDragIdx = -1;      // tarjeta que el dedo esta arrastrando
static uint32_t     notifLastMs  = 0;       // throttle de animacion (~30 fps)
static bool         notifPaused    = false; // true mientras gState != ST_HOME (fases de la isla congeladas)
static uint32_t     notifPauseT0   = 0;     // millis() en que empezo la pausa (ver notifTick)

// ---- Deteccion de hardware I2C (FASE 2) ----
// Disparadores DEMO de la Fase 1 (notificacion falsa al llegar a Home + otra
// por cada tap en la esquina superior derecha).
//
// AHORA EN 0. La Fase 2 ya esta terminada y conectada: hwDetectTick() barre el
// bus I2C de verdad y i2cOnDevicePresent() empuja la notificacion real. Con los
// demos en 1 el sistema anunciaba al arrancar un "Sensor BME280 / I2C 0x76
// detectado", un "MPU6050", un "Servo"... de hardware que NO esta conectado, y
// ademas sacaba otro falso cada vez que tocabas cerca de los iconos de bateria
// y wifi (zona x>=SCR_W-52, y 36..56), sin nada que indicara que ese trozo de
// pantalla fuera pulsable. Eso es lo que hacia que la isla pareciera aleatoria
// y que lo que anunciaba no coincidiera con la placa.
//
// Ponlo en 1 solo si quieres volver a probar la isla sin sensores en el bus.
#define NOTIF_DEMO_TRIGGERS   0

#define MAX_MODULES_DETECTED  8
#define I2C_SCAN_LO           0x08     // rango 7-bit valido (evita direcciones reservadas)
#define I2C_SCAN_HI           0x77
#define I2C_SCAN_PER_TICK     8        // direcciones sondeadas por vuelta de loop (no bloquea)
static const uint32_t I2C_SWEEP_INTERVAL = 3000;   // ms entre barridos completos

static DetectedModule detectedModules[MAX_MODULES_DETECTED];
static int      detectedCount = 0;
static uint16_t modSweepId[MAX_MODULES_DETECTED];  // ultimo barrido en que se vio cada modulo
static uint8_t  i2cScanCursor = 0;                 // direccion actual dentro del barrido
static bool     i2cSweeping   = false;             // hay un barrido en curso
static uint32_t i2cLastSweep  = 0;                 // fin del ultimo barrido
static uint16_t i2cSweepId    = 0;                 // id del barrido (para reconciliar presencia)

// Radio (WiFi por el co-procesador ESP32-C6/esp-hosted). Declarado aqui
// ARRIBA -a proposito- porque Ajustes (mas abajo en el archivo, pero
// ANTES que la seccion de radio al final) necesita leerlo para mostrar
// el estado real de la conexion. La logica de arranque/escaneo/conexion
// vive toda junto a bootInitRadioSafe(), al final del archivo.
#define FLEXOS_ENABLE_WIFI 1
static volatile bool gNetOnline = false;   // true tras un WiFi.begin() exitoso; lo lee la UI (Ajustes, icono, etc.)

// #############################################################
// ##  CAPA DE HARDWARE  (reutilizada de ArduOS - datos del
// ##  fabricante de la placa)
// #############################################################

// ---- BRING-UP DEL PANEL: LDO + DSI + ST7701 + DPI -----------
static esp_lcd_panel_handle_t    flxPanel   = NULL;
static esp_lcd_panel_io_handle_t flxPanelIo = NULL;
static SemaphoreHandle_t         flxDpiSem  = NULL;

// Tabla de init del ST7701 (comandos DCS del vendor, BK0/BK1).
// Es la secuencia oficial del modelo JC4880P443 (misma que el
// demo LVGL del fabricante y la config ESPHome funcional).
typedef struct { uint8_t cmd; uint8_t n; const uint8_t* d; } FlxDcsRow;
static const uint8_t r01[] = {0x77,0x01,0x00,0x00,0x13};
static const uint8_t r02[] = {0x08};
static const uint8_t r03[] = {0x77,0x01,0x00,0x00,0x10};
static const uint8_t r04[] = {0x63,0x00};
static const uint8_t r05[] = {0x0D,0x02};
static const uint8_t r06[] = {0x10,0x08};
static const uint8_t r07[] = {0x10};
static const uint8_t r08[] = {0x80,0x09,0x53,0x0C,0xD0,0x07,0x0C,0x09,0x09,0x28,0x06,0xD4,0x13,0x69,0x2B,0x71};
static const uint8_t r09[] = {0x80,0x94,0x5A,0x10,0xD3,0x06,0x0A,0x08,0x08,0x25,0x03,0xD3,0x12,0x66,0x6A,0x0D};
static const uint8_t r10[] = {0x77,0x01,0x00,0x00,0x11};
static const uint8_t r11[] = {0x5D};
static const uint8_t r12[] = {0x58};
static const uint8_t r13[] = {0x87};
static const uint8_t r14[] = {0x80};
static const uint8_t r15[] = {0x4E};
static const uint8_t r16[] = {0x85};
static const uint8_t r17[] = {0x21};
static const uint8_t r18[] = {0x10,0x1F};
static const uint8_t r19[] = {0x03};
static const uint8_t r20[] = {0x00};
static const uint8_t r21[] = {0x78};
static const uint8_t r22[] = {0x78};
static const uint8_t r23[] = {0x88};
static const uint8_t r24[] = {0x00,0x3A,0x02};
static const uint8_t r25[] = {0x04,0xA0,0x00,0xA0,0x05,0xA0,0x00,0xA0,0x00,0x40,0x40};
static const uint8_t r26[] = {0x30,0x00,0x40,0x40,0x32,0xA0,0x00,0xA0,0x00,0xA0,0x00,0xA0,0x00};
static const uint8_t r27[] = {0x00,0x00,0x33,0x33};
static const uint8_t r28[] = {0x44,0x44};
static const uint8_t r29[] = {0x09,0x2E,0xA0,0xA0,0x0B,0x30,0xA0,0xA0,0x05,0x2A,0xA0,0xA0,0x07,0x2C,0xA0,0xA0};
static const uint8_t r30[] = {0x00,0x00,0x33,0x33};
static const uint8_t r31[] = {0x44,0x44};
static const uint8_t r32[] = {0x08,0x2D,0xA0,0xA0,0x0A,0x2F,0xA0,0xA0,0x04,0x29,0xA0,0xA0,0x06,0x2B,0xA0,0xA0};
static const uint8_t r33[] = {0x00,0x00,0x4E,0x4E,0x00,0x00,0x00};
static const uint8_t r34[] = {0x08,0x01};
static const uint8_t r35[] = {0xB0,0x2B,0x98,0xA4,0x56,0x7F,0xFF,0xFF,0xFF,0xFF,0xF7,0x65,0x4A,0x89,0xB2,0x0B};
static const uint8_t r36[] = {0x08,0x08,0x08,0x45,0x3F,0x54};
static const uint8_t r37[] = {0x77,0x01,0x00,0x00,0x00};
static const uint8_t rCM[] = {0x55};   // COLMOD: RGB565 (16 bit)
static const uint8_t rMA[] = {0x00};   // MADCTL: RGB, sin espejos

static const FlxDcsRow ST7701_INIT[] = {
  {0xFF,5,r01},{0xEF,1,r02},{0xFF,5,r03},{0xC0,2,r04},{0xC1,2,r05},
  {0xC2,2,r06},{0xCC,1,r07},{0xB0,16,r08},{0xB1,16,r09},{0xFF,5,r10},
  {0xB0,1,r11},{0xB1,1,r12},{0xB2,1,r13},{0xB3,1,r14},{0xB5,1,r15},
  {0xB7,1,r16},{0xB8,1,r17},{0xB9,2,r18},{0xBB,1,r19},{0xBC,1,r20},
  {0xC1,1,r21},{0xC2,1,r22},{0xD0,1,r23},{0xE0,3,r24},{0xE1,11,r25},
  {0xE2,13,r26},{0xE3,4,r27},{0xE4,2,r28},{0xE5,16,r29},{0xE6,4,r30},
  {0xE7,2,r31},{0xE8,16,r32},{0xEB,7,r33},{0xEC,2,r34},{0xED,16,r35},
  {0xEF,6,r36},{0xFF,5,r37},
  {0x3A,1,rCM},{0x36,1,rMA},{0x20,0,NULL},   // COLMOD + MADCTL + INVOFF
};

static bool flxDpiFlushDone(esp_lcd_panel_handle_t p,
                            esp_lcd_dpi_panel_event_data_t* e, void* ctx){
  BaseType_t hp = pdFALSE;
  xSemaphoreGiveFromISR((SemaphoreHandle_t)ctx, &hp);
  return hp == pdTRUE;
}

// ---- Brillo real por PWM del backlight (lo controla el Panel Rapido) ----
static int  gBright = 80;      // 0..100
static bool gBlPwm  = false;   // true si el PWM se pudo enganchar
static void setBacklight(int pct){
  if(pct < 5) pct = 5; if(pct > 100) pct = 100;
  gBright = pct;
  if(gBlPwm) ledcWrite(PIN_LCD_BL, map(pct, 0, 100, 25, 255));
}

static bool flexPanelInit(){
  // Backlight apagado durante el init (evita el flash blanco)
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, LOW);

  // 1) LDO interno del P4: alimenta el PHY MIPI (canal 3, 2.5V)
  esp_ldo_channel_config_t ldo = {};
  ldo.chan_id    = 3;
  ldo.voltage_mv = 2500;
  esp_ldo_channel_handle_t ldoH = NULL;
  if(esp_ldo_acquire_channel(&ldo, &ldoH) != ESP_OK){
    Serial.println(F("[HW] ERROR: LDO MIPI (canal 3) no disponible"));
    return false;
  }

  // 2) Bus DSI: 2 lanes @ 500 Mbps
  esp_lcd_dsi_bus_config_t bus = {};
  bus.bus_id             = 0;
  bus.num_data_lanes     = 2;
  bus.phy_clk_src        = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
  bus.lane_bit_rate_mbps = 500;
  esp_lcd_dsi_bus_handle_t dsiBus = NULL;
  if(esp_lcd_new_dsi_bus(&bus, &dsiBus) != ESP_OK){
    Serial.println(F("[HW] ERROR: esp_lcd_new_dsi_bus"));
    return false;
  }

  // 3) Canal de comandos DBI (para la tabla de init DCS)
  esp_lcd_dbi_io_config_t dbi = {};
  dbi.virtual_channel = 0;
  dbi.lcd_cmd_bits    = 8;
  dbi.lcd_param_bits  = 8;
  if(esp_lcd_new_panel_io_dbi(dsiBus, &dbi, &flxPanelIo) != ESP_OK){
    Serial.println(F("[HW] ERROR: esp_lcd_new_panel_io_dbi"));
    return false;
  }

  // 4) Panel DPI (el framebuffer de hardware que refresca solo)
  esp_lcd_dpi_panel_config_t dpi = {};
  dpi.virtual_channel    = 0;
  dpi.dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
  dpi.dpi_clock_freq_mhz = 34;
  dpi.pixel_format       = LCD_COLOR_PIXEL_FORMAT_RGB565;
  dpi.num_fbs            = 1;
  dpi.video_timing.h_size            = SCR_W;
  dpi.video_timing.v_size            = SCR_H;
  dpi.video_timing.hsync_pulse_width = 12;
  dpi.video_timing.hsync_back_porch  = 42;
  dpi.video_timing.hsync_front_porch = 42;
  dpi.video_timing.vsync_pulse_width = 2;
  dpi.video_timing.vsync_back_porch  = 8;
  dpi.video_timing.vsync_front_porch = 166;
  dpi.flags.use_dma2d = true;
  if(esp_lcd_new_panel_dpi(dsiBus, &dpi, &flxPanel) != ESP_OK){
    Serial.println(F("[HW] ERROR: esp_lcd_new_panel_dpi"));
    return false;
  }

  // 5) Reset fisico del ST7701 (GPIO 5) y arranque del panel
  pinMode(PIN_LCD_RST, OUTPUT);
  digitalWrite(PIN_LCD_RST, HIGH); delay(5);
  digitalWrite(PIN_LCD_RST, LOW);  delay(10);
  digitalWrite(PIN_LCD_RST, HIGH); delay(120);
  if(esp_lcd_panel_init(flxPanel) != ESP_OK){
    Serial.println(F("[HW] ERROR: esp_lcd_panel_init"));
    return false;
  }

  // 6) Tabla del vendor + COLMOD/MADCTL/INVOFF + SLPOUT + DISPON
  for(size_t i = 0; i < sizeof(ST7701_INIT)/sizeof(ST7701_INIT[0]); i++){
    esp_lcd_panel_io_tx_param(flxPanelIo, ST7701_INIT[i].cmd,
                              ST7701_INIT[i].d, ST7701_INIT[i].n);
  }
  esp_lcd_panel_io_tx_param(flxPanelIo, 0x11, NULL, 0);  // SLPOUT
  delay(120);
  esp_lcd_panel_io_tx_param(flxPanelIo, 0x29, NULL, 0);  // DISPON
  delay(20);

  // 7) Callback de fin de flush (sincroniza el presenter)
  flxDpiSem = xSemaphoreCreateBinary();
  esp_lcd_dpi_panel_event_callbacks_t cbs = {};
  cbs.on_color_trans_done = flxDpiFlushDone;
  esp_lcd_dpi_panel_register_event_callbacks(flxPanel, &cbs, flxDpiSem);

  gBlPwm = ledcAttach(PIN_LCD_BL, 20000, 8);   // backlight ON con brillo PWM
  if(gBlPwm) setBacklight(gBright);
  else digitalWrite(PIN_LCD_BL, HIGH);         // fallback: encendido fijo
  Serial.println(F("[HW] Panel DSI 480x800 NATIVO OK"));
  return true;
}

// ---- DRIVER TACTIL: GT911 capacitivo por I2C ----------------
// Coords ABSOLUTAS ya calibradas de fabrica (0..479 x 0..799,
// portrait). Sin presion Z, sin promediado, sin calibracion.
// Si tu lote sale espejado/cruzado, pon estos flags a 1:
#define GT911_SWAP_XY 0
#define GT911_FLIP_X  0
#define GT911_FLIP_Y  0

static uint8_t gtAddr = 0x5D;   // el GT911 puede ser 0x5D o 0x14
static bool    gtOk   = false;

static bool gtWr(uint16_t reg, uint8_t val){
  Wire.beginTransmission(gtAddr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  Wire.write(val);
  return Wire.endTransmission() == 0;
}
static bool gtRd(uint16_t reg, uint8_t* buf, uint8_t n){
  Wire.beginTransmission(gtAddr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  if(Wire.endTransmission(false) != 0) return false;
  if(Wire.requestFrom((int)gtAddr, (int)n) != n) return false;
  for(uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

void flexTouchInit(){
  pinMode(PIN_TP_RST, OUTPUT);
  digitalWrite(PIN_TP_RST, LOW);  delay(10);
  digitalWrite(PIN_TP_RST, HIGH); delay(100);

  Wire.begin(PIN_TP_SDA, PIN_TP_SCL, 400000);

  gtAddr = 0x5D;
  Wire.beginTransmission(gtAddr);
  if(Wire.endTransmission() != 0){
    gtAddr = 0x14;
    Wire.beginTransmission(gtAddr);
    if(Wire.endTransmission() != 0){
      Serial.println(F("[HW] GT911 NO detectado (0x5D/0x14)"));
      return;
    }
  }
  gtOk = true;
  uint8_t pid[4] = {0};
  gtRd(0x8140, pid, 4);
  Serial.printf("[HW] Touch GT911 en 0x%02X, ID: %c%c%c\n",
                gtAddr, pid[0], pid[1], pid[2]);
}

// Lee un frame del GT911. Devuelve 1=tocando (gx/gy validos),
// 0=soltado, -1=sin datos nuevos. Coords NATIVAS (0..479/0..799).
static int8_t gtPoll(uint16_t &gx, uint16_t &gy){
  if(!gtOk) return -1;
  uint8_t status = 0;
  if(!gtRd(0x814E, &status, 1)) return -1;
  if(!(status & 0x80)) return -1;
  uint8_t n = status & 0x0F;
  int8_t res = 0;
  if(n >= 1){
    uint8_t d[6];
    if(gtRd(0x8150, d, 6)){
      gx = (uint16_t)d[0] | ((uint16_t)d[1] << 8);
      gy = (uint16_t)d[2] | ((uint16_t)d[3] << 8);
#if GT911_SWAP_XY
      { uint16_t t = gx; gx = gy; gy = t; }
#endif
#if GT911_FLIP_X
      gx = (SCR_W - 1) - gx;
#endif
#if GT911_FLIP_Y
      gy = (SCR_H - 1) - gy;
#endif
      if(gx > SCR_W - 1) gx = SCR_W - 1;
      if(gy > SCR_H - 1) gy = SCR_H - 1;
      res = 1;
    }
  }
  gtWr(0x814E, 0);            // limpiar buffer status
  return res;
}
// #############################################################
// ##  FIN de la capa de hardware reutilizada
// #############################################################

// #############################################################
// ##  MOTOR GRAFICO NATIVO 480x800  (original de FlexOS)
// ##  Framebuffer en PSRAM + presenter en core 0 + primitivas
// #############################################################

// Tres capas en PSRAM (la placa tiene de sobra):
//   fb       -> lo que el presenter sube al panel
//   lockBuf  -> pantalla de bloqueo pre-renderizada (swipe fluido)
//   homeBuf  -> escritorio pre-renderizado
static uint16_t* fb      = NULL;
static uint16_t* bbuf    = NULL;   // back buffer: se compone el frame aqui y se vuelca de una vez (anti-flicker)
static uint16_t* lockBuf = NULL;
static uint16_t* homeBuf = NULL;
static uint16_t* appSnapBuf = NULL;   // instantanea de una App (ver seccion Panel Edge / Sidebar Dock)

// Destino de dibujo actual (todas las primitivas escriben aqui)
static uint16_t* gBuf = NULL;
static inline void setBuf(uint16_t* b){ gBuf = b; }

static volatile bool gReady = false;
// Banda de recorte vertical (para listas con scroll). Por defecto: toda la pantalla.
static int gClipY0 = 0, gClipY1 = SCR_H - 1;
static int gClipX0 = 0, gClipX1 = SCR_W - 1;   // recorte horizontal (viewport de ventanas)
// Desplazamiento de dibujo: se SUMA a cada coordenada antes del recorte de
// arriba. Por defecto (0,0) es un no-op total -- byte-identico al comportamiento
// previo. Solo se pone distinto de (0,0) mientras se ejecuta enter()/tick() de
// una app REAL alojada dentro de una ventana flotante (ver wmRunHostedApp() en
// la seccion Window Manager): traduce las coordenadas nativas de la app
// (pensadas para pantalla completa) al rectangulo real de su ventana. NO aplica
// en modo landscape (gLand, Modo PC), que usa su propia rotacion (putPhys).
static int gOffX = 0, gOffY = 0;
static volatile int  gDirtyY0 = 0x7FFF, gDirtyY1 = -1;
static portMUX_TYPE  gMux = portMUX_INITIALIZER_UNLOCKED;
// Handle del presenter. Sirve para DESPERTARLO en cuanto una banda queda lista,
// en vez de que descubra el trabajo en su siguiente sondeo periodico. Con esto
// baja la latencia de dibujo (antes: hasta ~11 ms de espera muerta) y se elimina
// el micro-stutter que aparecia al desfasar el ritmo de composicion de la UI
// contra la rejilla fija de 11 ms del presenter. NO cambia que pixeles se pintan
// -- solo CUANDO se suben al panel (siempre bandas ya terminadas en fb).
static TaskHandle_t  flxPresenterTask = NULL;

static void flxFlush(int y0, int y1){
  if(y0 < 0) y0 = 0; if(y1 >= SCR_H) y1 = SCR_H - 1;
  if(y0 > y1) return;
  portENTER_CRITICAL(&gMux);
  if(y0 < gDirtyY0) gDirtyY0 = y0;
  if(y1 > gDirtyY1) gDirtyY1 = y1;
  portEXIT_CRITICAL(&gMux);
  // Aviso al presenter. flxFlush SIEMPRE corre en contexto de tarea (nunca en
  // ISR: el callback de fin de DMA usa su propio semaforo, flxDpiSem), asi que
  // xTaskNotifyGive es correcto. La notificacion de FreeRTOS se "latchea": si
  // llega mientras el presenter todavia no esta bloqueado, se recuerda y el
  // frame no se pierde. Varios avisos seguidos colapsan en un solo despertar que
  // sube la UNION de las bandas sucias ya coalescida arriba -> sin volcados de mas.
  if(flxPresenterTask) xTaskNotifyGive(flxPresenterTask);
}
static inline void flxFlushAll(){ flxFlush(0, SCR_H - 1); }
// Vuelca la banda [y0,y1] del back buffer a fb de una sola pasada y la marca dirty.
// Componer en bbuf y presentar asi evita que el presenter muestre cuadros a medias.
// RECORTE DE VOLCADO PARA APPS ALOJADAS EN VENTANAS.
// Las primitivas de dibujo respetan gClip*/gOff*, pero los VOLCADOS (memcpy de
// buffer -> fb) copiaban FILAS COMPLETAS de borde a borde. Cuando una app corre
// dentro de una ventana del Panel Edge, esas filas completas arrasaban todo lo
// que hubiera a los lados: la barra de estado, el tirador y el propio Panel Edge
// desaparecian, y la pantalla quedaba inservible. Con estas variables el volcado
// se limita al rectangulo interior de la ventana activa. Fuera de una ventana
// gWinBlit es false y el comportamiento es EXACTAMENTE el de siempre (memcpy de
// filas completas, misma velocidad, sin coste extra).
static bool gWinBlit = false;
static int  gWinBX0 = 0, gWinBX1 = SCR_W - 1, gWinBY0 = 0, gWinBY1 = SCR_H - 1;

// Copia la banda [y0,y1] de src a fb. Sin ventana activa: una sola pasada por
// fila completa (ruta rapida original). Con ventana activa: solo las columnas
// del rectangulo interior, para no pisar el escritorio.
static void fbCopyBand(const uint16_t* src, int y0, int y1){
  if(!src) return;
  if(y0 < 0) y0 = 0;
  if(y1 >= SCR_H) y1 = SCR_H - 1;
  if(!gWinBlit){
    if(y0 > y1) return;
    memcpy(fb + (size_t)y0 * SCR_W, src + (size_t)y0 * SCR_W, (size_t)(y1 - y0 + 1) * SCR_W * 2);
    return;
  }
  if(y0 < gWinBY0) y0 = gWinBY0;
  if(y1 > gWinBY1) y1 = gWinBY1;
  if(y0 > y1) return;
  int x0 = gWinBX0 < 0 ? 0 : gWinBX0;
  int x1 = gWinBX1 >= SCR_W ? SCR_W - 1 : gWinBX1;
  if(x0 > x1) return;
  const size_t bytes = (size_t)(x1 - x0 + 1) * 2;
  for(int j = y0; j <= y1; j++)
    memcpy(fb + (size_t)j * SCR_W + x0, src + (size_t)j * SCR_W + x0, bytes);
}

static void present(int y0, int y1){
  if(y0 < 0) y0 = 0; if(y1 >= SCR_H) y1 = SCR_H - 1; if(y0 > y1) return;
  fbCopyBand(bbuf, y0, y1);
  flxFlush(y0, y1);
}

static void flxPresenter(void*){
  for(;;){
    // Duerme hasta que alguien ensucie una banda (flxFlush -> xTaskNotifyGive).
    // El timeout de 15 ms es una RED DE SEGURIDAD: aunque un aviso nunca deberia
    // perderse (la notificacion se latchea), si por lo que fuera se perdiera, el
    // presenter despierta igual y sube cualquier region pendiente -> jamas se
    // queda un frame "colgado". pdTRUE = limpia la cuenta al salir (se comporta
    // como un semaforo binario). Antes aqui habia un vTaskDelay(11) fijo que
    // dormia SIEMPRE, aunque hubiera un frame listo para subir de inmediato.
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(15));
    if(!gReady) continue;
    int y0, y1;
    portENTER_CRITICAL(&gMux);
    y0 = gDirtyY0; y1 = gDirtyY1;
    gDirtyY0 = 0x7FFF; gDirtyY1 = -1;
    portEXIT_CRITICAL(&gMux);
    if(y1 >= y0){
      esp_lcd_panel_draw_bitmap(flxPanel, 0, y0, SCR_W, y1 + 1,
                                fb + (size_t)y0 * SCR_W);
      // Espera a que la DMA2D termine antes del siguiente volcado (serializa los
      // draws por fin-de-DMA: nunca se solapan ni "inundan" el bus del panel).
      xSemaphoreTake(flxDpiSem, pdMS_TO_TICKS(100));
    }
  }
}

static bool flxGfxInit(){
  size_t bytes = (size_t)SCR_W * SCR_H * 2;
  // Alineados a 64 bytes = tamano de linea de cache de la PSRAM del P4.
  // El presenter vuelca BANDAS parciales (fb + y0*SCR_W) a la DMA2D, que
  // exige un write-back de cache limpio del origen antes de leer. Si fb
  // no arranca en una frontera de 64B, el offset y0*SCR_W de una banda
  // puede caer a mitad de linea de cache: el write-back deja sin
  // sincronizar el principio de esa fila y la DMA2D lee PSRAM vieja ahi
  // -> esa fila sale desplazada/con basura, y como esto se repite en
  // cada flush con distinto y0, el resultado visual es una costura
  // diagonal de pixeles de colores erraticos. Cada fila aqui son
  // 480*2=960 bytes (multiplo exacto de 64), asi que con el buffer
  // alineado TODOS los offsets de fila quedan tambien alineados.
  fb      = (uint16_t*)heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  bbuf    = (uint16_t*)heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  lockBuf = (uint16_t*)heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  homeBuf = (uint16_t*)heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  appSnapBuf = (uint16_t*)heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!fb || !bbuf || !lockBuf || !homeBuf || !appSnapBuf){
    Serial.println(F("[GFX] ERROR: sin PSRAM para framebuffers"));
    return false;
  }
  memset(fb, 0, bytes);
  setBuf(fb);
  // Primer volcado en negro
  esp_lcd_panel_draw_bitmap(flxPanel, 0, 0, SCR_W, SCR_H, fb);
  xSemaphoreTake(flxDpiSem, pdMS_TO_TICKS(200));
  gReady = true;
  xTaskCreatePinnedToCore(flxPresenter, "flxPresenter", 4096, NULL, 3, &flxPresenterTask, 0);
  return true;
}

// ---------------- Color ----------------
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b){
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
// Division exacta por 255 SIN instruccion de division, para v en [0, 65534].
// Identidad clasica: v/255 == (v + 1 + (v>>8)) >> 8 en ese rango.
// Aqui el numerador maximo es 63*255 = 16065 (canal verde, 6 bits), muy por
// debajo del limite, asi que el resultado es BIT A BIT identico al de /255.
// Motivo: la division entera en el RISC-V del P4 cuesta decenas de ciclos y
// mix565 se ejecuta por PIXEL en cada alpha, cada panel de vidrio y cada blur.
#define DIV255(v)  (uint16_t)(((v) + 1u + ((v) >> 8)) >> 8)

// mezcla a<-b con peso t (0..255). t=0 => a, t=255 => b
static inline uint16_t mix565(uint16_t a, uint16_t b, uint8_t t){
  uint32_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  uint32_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  uint32_t it = 255u - t;
  uint32_t rr = ar * it + br * t;
  uint32_t rg = ag * it + bg * t;
  uint32_t rb = ab * it + bb * t;
  return (uint16_t)((DIV255(rr) << 11) | (DIV255(rg) << 5) | DIV255(rb));
}

// ---------------- Utilidades ----------------
// Raiz entera por restas binarias: SIN division (la version anterior hacia
// una division dentro del bucle de Newton). Se llama una vez por FILA de cada
// rectangulo redondeado, circulo y panel de vidrio. Resultado identico.
static inline int isqrt32(int v){
  if(v <= 0) return 0;
  uint32_t op = (uint32_t)v, res = 0, one = 1u << 30;
  while(one > op) one >>= 2;
  while(one){
    if(op >= res + one){ op -= res + one; res += one << 1; }
    res >>= 1; one >>= 2;
  }
  return (int)res;
}

// ---------------- Primitivas (escriben en gBuf) ----------------
// Rotacion landscape (Modo PC). Cuando gLand=true, las coords logicas
// (lx 0..799, ly 0..479) se escriben rotadas 90 sobre el panel portrait.
static bool gLand = false;
static inline void putPhys(int lx, int ly, uint16_t c){
  if((unsigned)lx >= SCR_H || (unsigned)ly >= SCR_W) return;
  int x = (SCR_W - 1) - ly, y = lx;
  if(y < gClipY0 || y > gClipY1) return;
  gBuf[(size_t)y * SCR_W + x] = c;
}
static inline void putPhysA(int lx, int ly, uint16_t c, uint8_t a){
  if((unsigned)lx >= SCR_H || (unsigned)ly >= SCR_W) return;
  int x = (SCR_W - 1) - ly, y = lx;
  if(y < gClipY0 || y > gClipY1) return;
  if(a >= 255){ gBuf[(size_t)y * SCR_W + x] = c; return; }
  if(a == 0) return;
  size_t i = (size_t)y * SCR_W + x; gBuf[i] = mix565(gBuf[i], c, a);
}
static inline void px(int x, int y, uint16_t c){
  if(gLand){ putPhys(x, y, c); return; }
  x += gOffX; y += gOffY;
  if((unsigned)x >= SCR_W || (unsigned)y >= SCR_H) return;
  if(y < gClipY0 || y > gClipY1 || x < gClipX0 || x > gClipX1) return;
  gBuf[(size_t)y * SCR_W + x] = c;
}
// pixel con alpha (0..255) sobre lo que ya hay en gBuf
static inline void pxA(int x, int y, uint16_t c, uint8_t a){
  if(gLand){ putPhysA(x, y, c, a); return; }
  x += gOffX; y += gOffY;
  if((unsigned)x >= SCR_W || (unsigned)y >= SCR_H) return;
  if(y < gClipY0 || y > gClipY1 || x < gClipX0 || x > gClipX1) return;
  if(a >= 255){ gBuf[(size_t)y * SCR_W + x] = c; return; }
  if(a == 0) return;
  size_t i = (size_t)y * SCR_W + x;
  gBuf[i] = mix565(gBuf[i], c, a);
}
static void hLine(int x, int y, int w, uint16_t c){
  if(w <= 0) return;
  if(gLand){ for(int i = 0; i < w; i++) putPhys(x + i, y, c); return; }
  x += gOffX; y += gOffY;
  if((unsigned)y >= SCR_H) return;
  if(y < gClipY0 || y > gClipY1) return;
  if(x < 0){ w += x; x = 0; }
  if(x + w > SCR_W) w = SCR_W - x;
  if(x < gClipX0){ w -= (gClipX0 - x); x = gClipX0; }     // recorte horizontal
  if(x + w > gClipX1 + 1) w = gClipX1 + 1 - x;
  if(w <= 0) return;
  uint16_t* p = gBuf + (size_t)y * SCR_W + x;
  for(int i = 0; i < w; i++) p[i] = c;
}
// Igual que hLine pero mezclando. Antes llamaba a pxA por pixel, lo que repetia
// TODOS los recortes (bordes + banda vertical + viewport) en cada pixel. Ahora
// recorta UNA vez y mezcla en linea recta: mismas reglas de recorte que hLine,
// mismo resultado, sin el coste por pixel. fillRectA/fillCircleA/fillRoundRectA
// cuelgan de aqui, asi que esto abarata todo el relleno translucido del sistema.
static void hLineA(int x, int y, int w, uint16_t c, uint8_t a){
  if(a >= 255){ hLine(x, y, w, c); return; }
  if(a == 0 || w <= 0) return;
  if(gLand){ for(int i = 0; i < w; i++) pxA(x + i, y, c, a); return; }   // landscape: ruta original
  x += gOffX; y += gOffY;
  if((unsigned)y >= SCR_H) return;
  if(y < gClipY0 || y > gClipY1) return;
  if(x < 0){ w += x; x = 0; }
  if(x + w > SCR_W) w = SCR_W - x;
  if(x < gClipX0){ w -= (gClipX0 - x); x = gClipX0; }
  if(x + w > gClipX1 + 1) w = gClipX1 + 1 - x;
  if(w <= 0) return;
  uint16_t* p = gBuf + (size_t)y * SCR_W + x;
  for(int i = 0; i < w; i++) p[i] = mix565(p[i], c, a);
}
static void vLine(int x, int y, int h, uint16_t c){
  if(h <= 0) return;
  if(gLand){ for(int i = 0; i < h; i++) putPhys(x, y + i, c); return; }
  x += gOffX; y += gOffY;
  if((unsigned)x >= SCR_W || x < gClipX0 || x > gClipX1) return;   // recorte horizontal
  if(y < 0){ h += y; y = 0; }
  if(y < gClipY0){ h -= (gClipY0 - y); y = gClipY0; }
  if(y + h > SCR_H) h = SCR_H - y;
  if(y + h > gClipY1 + 1) h = gClipY1 + 1 - y;
  if(h <= 0) return;
  uint16_t* p = gBuf + (size_t)y * SCR_W + x;
  for(int i = 0; i < h; i++){ *p = c; p += SCR_W; }
}
static void fillRect(int x, int y, int w, int h, uint16_t c){
  for(int j = 0; j < h; j++) hLine(x, y + j, w, c);
}
static void fillRectA(int x, int y, int w, int h, uint16_t c, uint8_t a){
  for(int j = 0; j < h; j++) hLineA(x, y + j, w, c, a);
}
static void drawRect(int x, int y, int w, int h, uint16_t c){
  hLine(x, y, w, c); hLine(x, y + h - 1, w, c);
  vLine(x, y, h, c); vLine(x + w - 1, y, h, c);
}

// Rectangulo redondeado relleno (esquinas suaves via inset por fila)
static void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c){
  if(w <= 0 || h <= 0) return;
  if(r < 0) r = 0;
  if(2 * r > w) r = w / 2;
  if(2 * r > h) r = h / 2;
  for(int j = 0; j < h; j++){
    int inset = 0;
    if(j < r){ int dy = r - 1 - j; inset = r - isqrt32(r * r - dy * dy); }
    else if(j >= h - r){ int dy = j - (h - r); inset = r - isqrt32(r * r - dy * dy); }
    hLine(x + inset, y + j, w - 2 * inset, c);
  }
}
static void fillRoundRectA(int x, int y, int w, int h, int r, uint16_t c, uint8_t a){
  if(w <= 0 || h <= 0) return;
  if(r < 0) r = 0;
  if(2 * r > w) r = w / 2;
  if(2 * r > h) r = h / 2;
  for(int j = 0; j < h; j++){
    int inset = 0;
    if(j < r){ int dy = r - 1 - j; inset = r - isqrt32(r * r - dy * dy); }
    else if(j >= h - r){ int dy = j - (h - r); inset = r - isqrt32(r * r - dy * dy); }
    hLineA(x + inset, y + j, w - 2 * inset, c, a);
  }
}
// Borde redondeado (1 px) para tarjetas
static void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c){
  if(2 * r > w) r = w / 2;
  if(2 * r > h) r = h / 2;
  hLine(x + r, y, w - 2 * r, c);
  hLine(x + r, y + h - 1, w - 2 * r, c);
  vLine(x, y + r, h - 2 * r, c);
  vLine(x + w - 1, y + r, h - 2 * r, c);
  // esquinas
  int f = 1 - r, ddx = 1, ddy = -2 * r, xx = 0, yy = r;
  while(xx < yy){
    if(f >= 0){ yy--; ddy += 2; f += ddy; }
    xx++; ddx += 2; f += ddx;
    px(x + r - xx, y + r - yy, c); px(x + w - r - 1 + xx, y + r - yy, c);
    px(x + r - yy, y + r - xx, c); px(x + w - r - 1 + yy, y + r - xx, c);
    px(x + r - xx, y + h - r - 1 + yy, c); px(x + w - r - 1 + xx, y + h - r - 1 + yy, c);
    px(x + r - yy, y + h - r - 1 + xx, c); px(x + w - r - 1 + yy, y + h - r - 1 + xx, c);
  }
}

static void fillCircle(int cx, int cy, int r, uint16_t c){
  if(r <= 0){ px(cx, cy, c); return; }
  for(int dy = -r; dy <= r; dy++){
    int dx = isqrt32(r * r - dy * dy);
    hLine(cx - dx, cy + dy, 2 * dx + 1, c);
  }
}
static void fillCircleA(int cx, int cy, int r, uint16_t c, uint8_t a){
  if(r <= 0){ pxA(cx, cy, c, a); return; }
  for(int dy = -r; dy <= r; dy++){
    int dx = isqrt32(r * r - dy * dy);
    hLineA(cx - dx, cy + dy, 2 * dx + 1, c, a);
  }
}
static void drawCircle(int cx, int cy, int r, uint16_t c){
  int f = 1 - r, ddx = 1, ddy = -2 * r, x = 0, y = r;
  px(cx, cy + r, c); px(cx, cy - r, c); px(cx + r, cy, c); px(cx - r, cy, c);
  while(x < y){
    if(f >= 0){ y--; ddy += 2; f += ddy; }
    x++; ddx += 2; f += ddx;
    px(cx + x, cy + y, c); px(cx - x, cy + y, c);
    px(cx + x, cy - y, c); px(cx - x, cy - y, c);
    px(cx + y, cy + x, c); px(cx - y, cy + x, c);
    px(cx + y, cy - x, c); px(cx - y, cy - x, c);
  }
}
// anillo de grosor t
static void fillRing(int cx, int cy, int rOut, int t, uint16_t c){
  int rin = rOut - t; if(rin < 0) rin = 0;
  for(int dy = -rOut; dy <= rOut; dy++){
    int dxo = isqrt32(rOut * rOut - dy * dy);
    int inr2 = rin * rin - dy * dy;
    if(inr2 > 0){
      int dxi = isqrt32(inr2);
      hLine(cx - dxo, cy + dy, dxo - dxi, c);
      hLine(cx + dxi + 1, cy + dy, dxo - dxi, c);
    } else {
      hLine(cx - dxo, cy + dy, 2 * dxo + 1, c);
    }
  }
}
static void lineTo(int x0, int y0, int x1, int y1, uint16_t c){
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for(;;){
    px(x0, y0, c);
    if(x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if(e2 >= dy){ err += dy; x0 += sx; }
    if(e2 <= dx){ err += dx; y0 += sy; }
  }
}
// trazo grueso con puntas redondeadas: estampa discos por el segmento
static void strokeSeg(float x0, float y0, float x1, float y1, int rad, uint16_t c){
  float dx = x1 - x0, dy = y1 - y0;
  float len = sqrtf(dx * dx + dy * dy);
  int steps = (int)len + 1;
  for(int i = 0; i <= steps; i++){
    float t = (steps > 0) ? (float)i / steps : 0;
    fillCircle((int)(x0 + dx * t + 0.5f), (int)(y0 + dy * t + 0.5f), rad, c);
  }
}

// ---------------- Primitivas ANTI-ALIASING (bordes suaves) ----------------
// Cobertura por sub-pixel: los bordes se mezclan con alpha en vez de
// dibujarse "duros". Esto da curvas suaves al reloj y a los acentos.
static float distToSeg(float px, float py, float ax, float ay, float bx, float by){
  float dx = bx - ax, dy = by - ay;
  float l2 = dx * dx + dy * dy;
  float t = (l2 > 0.0f) ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0.0f;
  if(t < 0) t = 0; else if(t > 1) t = 1;
  float qx = ax + t * dx - px, qy = ay + t * dy - py;
  return sqrtf(qx * qx + qy * qy);
}
static void fillCircleAA(float cx, float cy, float r, uint16_t col){
  int x0 = (int)floorf(cx - r - 1), x1 = (int)ceilf(cx + r + 1);
  int y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
  for(int y = y0; y <= y1; y++) for(int x = x0; x <= x1; x++){
    float dx = x - cx, dy = y - cy;
    float cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
    if(cov <= 0) continue; if(cov > 1) cov = 1;
    pxA(x, y, col, (uint8_t)(cov * 255));
  }
}
// Segmento grueso con puntas redondeadas y bordes suaves (para el reloj)
static void strokeSegAA(float x0, float y0, float x1, float y1, float rad, uint16_t col){
  int minx = (int)floorf(fminf(x0, x1) - rad - 1), maxx = (int)ceilf(fmaxf(x0, x1) + rad + 1);
  int miny = (int)floorf(fminf(y0, y1) - rad - 1), maxy = (int)ceilf(fmaxf(y0, y1) + rad + 1);
  for(int y = miny; y <= maxy; y++) for(int x = minx; x <= maxx; x++){
    float cov = rad + 0.5f - distToSeg((float)x, (float)y, x0, y0, x1, y1);
    if(cov <= 0) continue; if(cov > 1) cov = 1;
    pxA(x, y, col, (uint8_t)(cov * 255));
  }
}

// ---------------- Fondo (wallpaper) ----------------
// Degradado diagonal 3 paradas: verde (arriba-dcha) -> azul (centro)
// -> violeta (abajo-izq), igual que tus imagenes. Opcional: blobs.
// El degradado se repinta ENTERO en cada renderHome()/renderLock(), o sea una
// vez por minuto. La version anterior hacia, por cada uno de los 384.000
// pixeles: 2 divisiones + 1 mix565 (que a su vez hacia 3 divisiones mas).
// Dos observaciones lo tiran casi todo abajo:
//   1) 'ty' NO depende de x, pero se recalculaba 480 veces por fila.
//   2) el color solo depende de t = (tx+ty)/2, que vive en [0,255]: caben
//      los 256 colores posibles en una tabla y el bucle interior pasa a ser
//      una simple consulta. Coste: 992 B de RAM estatica, una sola vez.
// El resultado en pantalla es BIT A BIT el mismo que antes.
static void drawWallpaper(uint16_t* buf, bool blobs){
  const uint16_t green  = rgb565(80, 224, 74);    // arriba-derecha
  const uint16_t blue   = rgb565(40, 150, 245);   // centro
  const uint16_t purple = rgb565(112, 46, 230);   // abajo-izquierda
  static uint16_t gradLut[256];                   // color por t          (512 B)
  static uint8_t  txLut[SCR_W];                   // rampa horizontal      (480 B)
  static bool     lutReady = false;
  if(!lutReady){
    for(int t = 0; t < 256; t++)
      gradLut[t] = (t < 128) ? mix565(purple, blue,  (uint8_t)(t * 2))
                             : mix565(blue,   green, (uint8_t)((t - 128) * 2));
    for(int x = 0; x < SCR_W; x++) txLut[x] = (uint8_t)((x * 255) / (SCR_W - 1));
    lutReady = true;
  }
  uint16_t* old = gBuf; setBuf(buf);
  for(int y = 0; y < SCR_H; y++){
    // t=1 arriba-derecha, t=0 abajo-izquierda. 'ty' es invariante en x.
    int ty = ((SCR_H - 1 - y) * 255) / (SCR_H - 1);
    uint16_t* row = buf + (size_t)y * SCR_W;
    for(int x = 0; x < SCR_W; x++) row[x] = gradLut[(txLut[x] + ty) >> 1];
  }
  if(blobs){
    // dos manchas suaves mas claras (como el escritorio de tus imagenes)
    fillCircleA(360, 150, 220, rgb565(150, 235, 180), 60);
    fillCircleA(90,  560, 260, rgb565(150, 160, 240), 55);
  }
  setBuf(old);
}

// #############################################################
// ##  LIQUID GLASS (aproximacion iOS 26 en software)
// ##  Panel reutilizable: desenfoca el fondo real (box-blur),
// ##  tinte sutil, gradiente de grosor y brillo especular animado.
// #############################################################
static bool uiGlass = false;               // estilo activo (togglea en Ajustes)
// Modo de apariencia (Ajustes -> Pantalla -> Modo de apariencia). true = Modo
// oscuro (comportamiento de siempre, por defecto). false = Modo claro.
// ALCANCE: por ahora retematiza la app Ajustes (donde vive el selector),
// que es donde vive PAGE_BG y los colores de tarjeta/texto de esa app (ver
// mas abajo, junto a PAGE_BG). Inicio, Bloqueo, notificaciones, ventanas y
// el resto de apps tienen su propia paleta oscura, muy afinada a mano, y no
// se tocan en esta pasada -- retematizarlas de verdad necesita un diseno de
// colores propio por pantalla, no un cambio mecanico.
static bool gDark = true;
static int  gIconStyle = 0;                // estilo de iconos: 0 = Plano, 1 = Vidrio (fondo Liquid Glass en drawAppIcon)
static bool glDrawSpec = true;             // false = vidrio base SIN destello (se anima aparte con glassSheen)
static uint16_t* glassBuf = NULL;          // scratch de region (PSRAM)
// NOTA: aqui vivia 'wallBuf' ("wallpaper limpio para animar el brillo"). Era
// memoria muerta: se reservaban 768 KB y se copiaban enteros en CADA
// renderHome(), pero NINGUNA funcion lo leia jamas. El brillo se anima en
// realidad desde homeBuf (animateHomeGlass) y desde lockBuf. Eliminado:
// -768 KB de PSRAM y -768 KB de memcpy por cada repintado del escritorio.
// Linea temporal para el blur. Se indexa por ANCHO (pasada horizontal) y
// por ALTO (pasada vertical) del panel de turno, asi que debe cubrir el
// mayor de los dos lados de la pantalla, no solo SCR_H, o un panel mas
// ancho que alto desbordaria este buffer y corromperia memoria vecina.
static uint16_t  glLine[(SCR_W > SCR_H ? SCR_W : SCR_H)];

static inline void un565(uint16_t c, int &r, int &g, int &b){ r = (c >> 11) & 0x1F; g = (c >> 5) & 0x3F; b = c & 0x1F; }
static inline uint16_t pk565(int r, int g, int b){ return (uint16_t)((r << 11) | (g << 5) | b); }

// box-blur (suma corrediza) sobre glassBuf de ancho w, alto h
static void glassBlur(int w, int h, int R){
  int r, g, b;
  for(int j = 0; j < h; j++){                         // horizontal
    uint16_t* row = glassBuf + (size_t)j * w;
    for(int i = 0; i < w; i++) glLine[i] = row[i];
    int sr = 0, sg = 0, sb = 0, win = 0;
    for(int i = 0; i <= R && i < w; i++){ un565(glLine[i], r, g, b); sr += r; sg += g; sb += b; win++; }
    for(int i = 0; i < w; i++){
      row[i] = pk565(sr / win, sg / win, sb / win);
      int add = i + R + 1, rem = i - R;
      if(add < w){ un565(glLine[add], r, g, b); sr += r; sg += g; sb += b; win++; }
      if(rem >= 0){ un565(glLine[rem], r, g, b); sr -= r; sg -= g; sb -= b; win--; }
    }
  }
  for(int i = 0; i < w; i++){                          // vertical
    for(int j = 0; j < h; j++) glLine[j] = glassBuf[(size_t)j * w + i];
    int sr = 0, sg = 0, sb = 0, win = 0;
    for(int j = 0; j <= R && j < h; j++){ un565(glLine[j], r, g, b); sr += r; sg += g; sb += b; win++; }
    for(int j = 0; j < h; j++){
      glassBuf[(size_t)j * w + i] = pk565(sr / win, sg / win, sb / win);
      int add = j + R + 1, rem = j - R;
      if(add < h){ un565(glLine[add], r, g, b); sr += r; sg += g; sb += b; win++; }
      if(rem >= 0){ un565(glLine[rem], r, g, b); sr -= r; sg -= g; sb -= b; win--; }
    }
  }
}
static int glInset(int j, int h, int rad){
  if(j < rad){ int dy = rad - 1 - j; return rad - isqrt32(rad * rad - dy * dy); }
  if(j >= h - rad){ int dy = j - (h - rad); return rad - isqrt32(rad * rad - dy * dy); }
  return 0;
}
// Panel Liquid Glass reutilizable. t = millis() (anima el brillo).
// "Ex" permite fijar el radio del box-blur (blurR). glassBlur() es una suma
// corrediza O(w*h) que NO depende de blurR (ver mas arriba), asi que subir
// blurR no cuesta rendimiento extra -- solo cambia cuanto se difumina el
// fondo. drawLiquidGlassPanel() de siempre (abajo) sigue llamando a esta con
// blurR=6, es decir: comportamiento IDENTICO al anterior en los ~19 sitios
// existentes que ya la usan. Se penso para el panel rapido, que quiere un
// vidrio mas "esmerilado" que el resto del sistema.
static void drawLiquidGlassPanelEx(int x, int y, int w, int h, int rad, uint16_t tint, uint32_t t, int blurR){
  // GUARDA DE LANDSCAPE (Modo PC). Esta funcion lee y escribe el buffer con
  // indexacion VERTICAL directa (gBuf + (y+j)*SCR_W + x), asi que ignora por
  // completo la rotacion de gLand. En Modo PC cada drawAppIcon() de estilo
  // "Vidrio" pintaba su panel en coordenadas rotadas: por eso aparecian paneles
  // de cristal FANTASMA flotando por el escritorio (una columna a la derecha =
  // los iconos de la barra de tareas, una fila abajo = los del escritorio).
  // fillRoundRectA() si respeta la rotacion (pasa por putPhys), asi que en
  // landscape se usa el panel plano tintado: mismo sitio, sin fantasmas. El
  // cristal propio de Modo PC lo dibuja pcGlassPanel(), que si es landscape-safe.
  if(gLand){ fillRoundRectA(x, y, w, h, rad, tint, 210); return; }
  if(!glassBuf) glassBuf = (uint16_t*)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!glassBuf){ fillRoundRectA(x, y, w, h, rad, tint, 210); return; }   // fallback sin PSRAM
  if(x < 0){ w += x; x = 0; } if(y < 0){ h += y; y = 0; }
  if(x + w > SCR_W) w = SCR_W - x; if(y + h > SCR_H) h = SCR_H - y;
  if(w <= 0 || h <= 0) return;
  if(2 * rad > w) rad = w / 2; if(2 * rad > h) rad = h / 2;
  for(int j = 0; j < h; j++) memcpy(glassBuf + (size_t)j * w, gBuf + (size_t)(y + j) * SCR_W + x, w * 2);
  glassBlur(w, h, blurR);
  int off = (int)((t / 16) % (uint32_t)(w + h + 120)) - 60;
  for(int j = 0; j < h; j++){
    int yy = y + j; if(yy < gClipY0 || yy > gClipY1) continue;   // respeta la banda de recorte
    int ins = glInset(j, h, rad);
    uint16_t* src = glassBuf + (size_t)j * w;
    uint16_t* dst = gBuf + (size_t)yy * SCR_W + x;
    float fj = (float)j;
    for(int i = ins; i < w - ins; i++){
      uint16_t out = mix565(src[i], tint, 58);
      if(fj < h * 0.45f) out = mix565(out, rgb565(255,255,255), (uint8_t)((1.0f - fj / (h * 0.45f)) * 26));
      else               out = mix565(out, rgb565(0,0,0), (uint8_t)(((fj - h * 0.45f) / (h * 0.55f)) * 30));
      int band = (i + j) - off; if(band < 0) band = -band;
      if(glDrawSpec && band < 70){ float in2 = 1.0f - band / 70.0f; out = mix565(out, rgb565(255,255,255), (uint8_t)(in2 * in2 * 72)); }
      dst[i] = out;
    }
    uint16_t bcol = (j < 3) ? rgb565(255,255,255) : (j < h / 2 ? rgb565(205,214,228) : rgb565(22,28,40));
    dst[ins] = mix565(dst[ins], bcol, 130);
    dst[w - 1 - ins] = mix565(dst[w - 1 - ins], bcol, 130);
  }
}
static void drawLiquidGlassPanel(int x, int y, int w, int h, int rad, uint16_t tint, uint32_t t){
  drawLiquidGlassPanelEx(x, y, w, h, rad, tint, t, 6);   // blur original, sin cambios, para el resto del sistema
}
// Destello diagonal MOVIL barato: se dibuja sobre un panel de vidrio ya compuesto,
// una vez por frame de animacion (no re-desenfoca). Recortado a la forma redondeada.
// Sigue el ultimo punto tocado (gTouchX/Y, actualizados en flexPollTouch() -- estas
// son variables sueltas y no parte de "struct Touch" porque esa struct se define
// mucho mas abajo en el archivo, despues de glassSheen(); usar T.x/T.y aqui
// arriba no compilaria).
static int      gTouchX = SCR_W / 2, gTouchY = SCR_H / 2;
static uint32_t gTouchMs = 0;              // millis() del ultimo toque (o arrastre) visto
#define GLASS_TOUCH_FOLLOW_MS 2500         // cuanto tiempo "sigue" el reflejo tras soltar, antes de volver al barrido ambiental
static void glassSheen(int x, int y, int w, int h, int rad, uint32_t t){
  if(2 * rad > w) rad = w / 2; if(2 * rad > h) rad = h / 2;
  int off;
  if(t - gTouchMs < GLASS_TOUCH_FOLLOW_MS)               // toque reciente: el reflejo emana de ahi
    off = (gTouchX - x) + (gTouchY - y);
  else                                                    // sin toque reciente: barrido ambiental de siempre
    off = (int)((t / 5) % (uint32_t)(w + h + 120)) - 60;
  for(int j = 2; j < h - 2; j++){
    int yy = y + j; if(yy < gClipY0 || yy > gClipY1) continue;
    int ins = glInset(j, h, rad);
    for(int i = ins + 1; i < w - ins - 1; i++){
      int band = (i + j) - off; if(band < 0) band = -band;
      if(band < 28){ int a = (28 - band) * 3; pxA(x + i, yy, rgb565(255,255,255), (uint8_t)(a > 96 ? 96 : a)); }
    }
  }
}
// Wallpaper desenfocado reutilizable (fondo del desbloqueo y de Recientes, estilo iOS)
static uint16_t* blurBg = NULL;
static void ensureBlurBg(){
  if(blurBg) return;
  blurBg = (uint16_t*)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!blurBg) return;
  uint16_t* old = gBuf;
  drawWallpaper(blurBg, true);
  setBuf(blurBg);
  glDrawSpec = false; drawLiquidGlassPanel(0, 0, SCR_W, SCR_H, 0, rgb565(18,24,42), 0); glDrawSpec = true;
  fillRectA(0, 0, SCR_W, SCR_H, rgb565(8,10,18), 70);
  setBuf(old);
}

// #############################################################
// ##  TIPOGRAFIA + RELOJ VECTORIAL + TRIANGULOS  (original)
// #############################################################

// Fuente 5x7 (column-major, bit0 = arriba). ASCII 0x20..0x7E.
static const uint8_t FONT5x7[95][5] = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
  {0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
  {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},
  {0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
  {0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
  {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},
  {0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},
  {0x00,0x56,0x36,0x00,0x00},{0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
  {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},
  {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
  {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
  {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
  {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
  {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
  {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
  {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
  {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},
  {0x40,0x40,0x40,0x40,0x40},{0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},
  {0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7F},
  {0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
  {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},
  {0x7F,0x10,0x28,0x44,0x00},{0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},
  {0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0x7C,0x14,0x14,0x14,0x08},
  {0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
  {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},
  {0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},
  {0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},{0x00,0x00,0x7F,0x00,0x00},
  {0x00,0x41,0x36,0x08,0x00},{0x08,0x04,0x08,0x10,0x08}
};
// Glifos extra: ¿ (invertido) y ¡ (invertido)
static const uint8_t GLYPH_INVQ[5]    = {0x30,0x48,0x45,0x40,0x20};
static const uint8_t GLYPH_INVEXCL[5] = {0x00,0x00,0x7D,0x00,0x00};

// Tipos de acento (se dibujan sobre/bajo el glifo base)
enum { ACC_NONE=0, ACC_ACUTE, ACC_GRAVE, ACC_TILDE, ACC_DIAER, ACC_CIRC, ACC_CED };

// Mapea un codepoint Unicode a (glifo base, acento). base 1=¿, 2=¡.
static void mapCP(uint32_t cp, uint8_t &base, uint8_t &acc){
  acc = ACC_NONE;
  if(cp < 0x80){ base = (cp >= 0x20 && cp <= 0x7E) ? (uint8_t)cp : '?'; return; }
  switch(cp){
    case 0xE1: base='a'; acc=ACC_ACUTE; return;   case 0xE9: base='e'; acc=ACC_ACUTE; return;
    case 0xED: base='i'; acc=ACC_ACUTE; return;   case 0xF3: base='o'; acc=ACC_ACUTE; return;
    case 0xFA: base='u'; acc=ACC_ACUTE; return;   case 0xFC: base='u'; acc=ACC_DIAER; return;
    case 0xF1: base='n'; acc=ACC_TILDE; return;
    case 0xC1: base='A'; acc=ACC_ACUTE; return;   case 0xC9: base='E'; acc=ACC_ACUTE; return;
    case 0xCD: base='I'; acc=ACC_ACUTE; return;   case 0xD3: base='O'; acc=ACC_ACUTE; return;
    case 0xDA: base='U'; acc=ACC_ACUTE; return;   case 0xD1: base='N'; acc=ACC_TILDE; return;
    case 0xDC: base='U'; acc=ACC_DIAER; return;
    case 0xE0: base='a'; acc=ACC_GRAVE; return;   case 0xE8: base='e'; acc=ACC_GRAVE; return;
    case 0xEC: base='i'; acc=ACC_GRAVE; return;   case 0xF2: base='o'; acc=ACC_GRAVE; return;
    case 0xF9: base='u'; acc=ACC_GRAVE; return;
    case 0xE2: base='a'; acc=ACC_CIRC; return;    case 0xEA: base='e'; acc=ACC_CIRC; return;
    case 0xEE: base='i'; acc=ACC_CIRC; return;    case 0xF4: base='o'; acc=ACC_CIRC; return;
    case 0xFB: base='u'; acc=ACC_CIRC; return;
    case 0xE3: base='a'; acc=ACC_TILDE; return;   case 0xF5: base='o'; acc=ACC_TILDE; return;
    case 0xE7: base='c'; acc=ACC_CED; return;     case 0xC7: base='C'; acc=ACC_CED; return;
    case 0xBF: base=1; return;                    case 0xA1: base=2; return;
    default:   base='?'; return;
  }
}

static void drawGlyphRaw(int x, int y, uint8_t base, int s, uint16_t col){
  const uint8_t* g;
  if(base == 1) g = GLYPH_INVQ;
  else if(base == 2) g = GLYPH_INVEXCL;
  else { int idx = (int)base - 0x20; if(idx < 0 || idx > 94) return; g = FONT5x7[idx]; }
  for(int c = 0; c < 5; c++){
    uint8_t bits = g[c];
    for(int r = 0; r < 7; r++){
      if(bits & (1 << r)){
        if(s == 1) px(x + c, y + r, col);
        else fillRect(x + c * s, y + r * s, s, s, col);
      }
    }
  }
}
static void drawAccent(int x, int y, int s, uint8_t acc, uint16_t col){
  float cx = x + 2.5f * s;
  float r = s * 0.55f; if(r < 1.0f) r = 1.0f;
  switch(acc){
    case ACC_ACUTE: strokeSegAA(cx - s, y - s, cx + s, y - 3 * s, r, col); break;
    case ACC_GRAVE: strokeSegAA(cx - s, y - 3 * s, cx + s, y - s, r, col); break;
    case ACC_CIRC:  strokeSegAA(cx - 1.6f * s, y - s, cx, y - 3 * s, r, col);
                    strokeSegAA(cx, y - 3 * s, cx + 1.6f * s, y - s, r, col); break;
    case ACC_TILDE: strokeSegAA(cx - 2 * s, y - 2 * s, cx - 0.4f * s, y - 2.9f * s, r, col);
                    strokeSegAA(cx - 0.4f * s, y - 2.9f * s, cx + 0.6f * s, y - 1.9f * s, r, col);
                    strokeSegAA(cx + 0.6f * s, y - 1.9f * s, cx + 2 * s, y - 2.7f * s, r, col); break;
    case ACC_DIAER: fillCircleAA(cx - s, y - 2 * s, r, col);
                    fillCircleAA(cx + s, y - 2 * s, r, col); break;
    case ACC_CED:   strokeSegAA(cx, y + 7 * s - s, cx - 1.2f * s, y + 7 * s + 0.6f * s, r, col); break;
  }
}

// Glifo del cuerpo con SUPERSAMPLING (3x3) -> bordes suaves.
// A tamano 1 (etiquetas diminutas) se dibuja nitido para no emborronar.
static void drawGlyphSmooth(int x, int y, uint8_t base, int s, uint16_t col, uint8_t alpha){
  const uint8_t* g;
  if(base == 1) g = GLYPH_INVQ;
  else if(base == 2) g = GLYPH_INVEXCL;
  else { int idx = (int)base - 0x20; if(idx < 0 || idx > 94) return; g = FONT5x7[idx]; }
  if(s <= 1){
    for(int c = 0; c < 5; c++){ uint8_t bits = g[c];
      for(int r = 0; r < 7; r++) if(bits & (1 << r)) pxA(x + c, y + r, col, alpha); }
    return;
  }
  const int SS = 3;
  int W = 5 * s, H = 7 * s;
  for(int ty = 0; ty < H; ty++){
    for(int tx = 0; tx < W; tx++){
      int cov = 0;
      for(int sy = 0; sy < SS; sy++) for(int sx = 0; sx < SS; sx++){
        int ix = (int)((tx + (sx + 0.5f) / SS) / s);
        int iy = (int)((ty + (sy + 0.5f) / SS) / s);
        if(ix >= 0 && ix < 5 && iy >= 0 && iy < 7 && (g[ix] & (1 << iy))) cov++;
      }
      if(cov) pxA(x + tx, y + ty, col, (uint8_t)((cov * alpha) / (SS * SS)));
    }
  }
}

// Ancho de texto en px (para centrar). Cada glifo avanza 6*s.

// #############################################################
// ##  FUENTE OUTFIT antialiased 4bpp  (reemplaza al 5x7 POR DEBAJO)
// ##  Mismas firmas publicas: drawText/drawTextC/drawTextR/textW.
// #############################################################
// Fuente Outfit-Regular antialiased 4bpp (generada). NO editar a mano.
#define FONT_LINEH 51
#define FONT_ASC 40
static const FGlyph FG[127] = {
  {0,0,0,0,8,0},{5,30,3,10,10,0},{12,10,2,11,16,90},{21,29,2,11,26,150},{19,37,2,7,24,469},{22,29,2,11,26,839},
  {24,29,2,11,26,1158},{5,10,2,11,9,1506},{10,36,2,9,12,1536},{10,36,1,9,12,1716},{16,17,2,9,20,1896},{17,19,2,17,22,2032},
  {6,11,2,35,11,2203},{13,4,3,27,18,2236},{5,5,3,35,12,2264},{16,32,0,10,16,2279},{23,29,2,11,26,2535},{10,29,1,11,14,2883},
  {19,29,1,11,22,3028},{19,29,1,11,22,3318},{22,29,1,11,24,3608},{20,29,0,11,22,3927},{19,29,2,11,23,4217},{18,29,1,11,21,4507},
  {19,29,2,11,22,4768},{20,29,2,11,23,5058},{5,18,3,22,11,5348},{6,24,2,22,11,5402},{17,19,2,17,22,5474},{17,13,2,20,22,5645},
  {17,19,2,17,22,5762},{18,30,1,10,20,5933},{26,27,2,17,30,6203},{27,29,1,11,28,6554},{20,29,3,11,25,6960},{25,29,2,11,28,7250},
  {24,29,3,11,30,7627},{19,29,3,11,24,7975},{18,29,3,11,23,8265},{27,29,2,11,31,8526},{22,29,3,11,28,8932},{4,29,3,11,10,9251},
  {16,29,1,11,20,9309},{23,29,3,11,27,9541},{18,29,3,11,22,9889},{27,29,3,11,34,10150},{22,29,3,11,28,10556},{28,29,2,11,32,10875},
  {19,29,3,11,24,11281},{30,31,2,11,33,11571},{21,29,3,11,25,12036},{19,29,1,11,22,12355},{23,29,1,11,25,12645},{22,29,3,11,27,12993},
  {26,29,1,11,28,13312},{37,29,1,11,39,13689},{26,29,1,11,28,14240},{25,29,0,11,27,14617},{20,29,2,11,23,14994},{9,33,3,11,14,15284},
  {16,32,0,10,16,15449},{9,33,1,11,14,15705},{14,11,2,10,18,15870},{19,4,1,41,20,15947},{9,9,1,9,11,15987},{19,20,1,20,23,16032},
  {19,29,3,11,23,16232},{18,20,1,20,20,16522},{19,29,1,11,23,16702},{19,20,1,20,21,16992},{18,30,0,10,16,17192},{19,28,1,20,23,17462},
  {17,29,3,11,22,17742},{5,29,2,11,9,18003},{13,37,-5,11,9,18090},{17,29,3,11,20,18349},{4,29,3,11,9,18610},{29,20,3,20,34,18668},
  {17,20,3,20,22,18968},{20,20,1,20,23,19148},{19,28,3,20,23,19348},{20,28,1,20,23,19628},{14,20,3,20,17,19908},{16,20,0,20,17,20048},
  {14,28,0,12,15,20208},{16,20,2,20,21,20404},{20,20,0,20,20,20564},{30,20,0,20,30,20764},{20,20,0,20,20,21064},{20,28,0,20,21,21264},
  {16,20,1,20,18,21544},{11,33,1,11,13,21704},{4,37,4,8,11,21902},{11,33,1,11,13,21976},{18,6,2,23,22,22174},{19,31,1,9,23,22228},
  {19,31,1,9,21,22538},{9,32,0,8,9,22848},{20,31,1,9,23,23008},{16,32,2,8,21,23318},{16,28,2,12,21,23574},{17,29,3,11,22,23798},
  {27,40,1,0,28,24059},{19,40,3,0,24,24619},{9,40,0,0,10,25019},{28,40,2,0,32,25219},{22,40,3,0,27,25779},{22,37,3,3,27,26219},
  {22,38,3,2,28,26626},{19,31,1,9,23,27044},{19,31,1,9,21,27354},{9,31,0,9,9,27664},{20,31,1,9,23,27819},{16,31,2,9,21,28129},
  {19,31,1,9,23,28377},{19,31,1,9,21,28687},{15,31,-3,9,9,28997},{20,31,1,9,23,29245},{16,31,2,9,21,29555},{19,29,1,11,23,29803},
  {20,29,1,11,23,30093},{18,29,1,20,20,30383},{25,38,2,11,28,30644},{16,28,2,20,20,31138},{5,28,3,20,11,31362},{12,13,2,11,16,31446},
  {5,5,0,23,5,31524},
};
static const uint8_t FBM[31539] = {
  15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,
  176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,
  255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,44,252,32,207,255,
  192,255,255,240,207,255,192,44,252,32,63,255,160,10,255,244,47,255,144,9,
  255,242,15,255,128,8,255,241,14,255,96,6,255,240,13,255,80,5,255,208,
  12,255,64,4,255,192,11,255,48,3,255,176,9,255,16,1,255,144,8,255,
  0,0,255,128,7,254,0,0,239,112,0,0,0,95,250,0,0,13,255,32,
  0,0,0,0,127,248,0,0,31,255,0,0,0,0,0,159,246,0,0,63,
  253,0,0,0,0,0,207,244,0,0,95,251,0,0,0,0,0,239,242,0,
  0,127,249,0,0,0,0,1,255,240,0,0,159,246,0,0,0,0,3,255,
  192,0,0,191,244,0,0,0,0,5,255,160,0,0,223,242,0,0,9,153,
  156,255,217,153,153,255,249,153,144,15,255,255,255,255,255,255,255,255,255,240,
  15,255,255,255,255,255,255,255,255,255,240,15,255,255,255,255,255,255,255,255,
  255,240,0,0,31,254,0,0,9,255,96,0,0,0,0,63,252,0,0,11,
  255,64,0,0,0,0,95,250,0,0,14,255,32,0,0,0,0,143,248,0,
  0,31,255,0,0,0,0,0,175,246,0,0,63,253,0,0,0,255,255,255,
  255,255,255,255,255,255,255,0,255,255,255,255,255,255,255,255,255,255,0,255,
  255,255,255,255,255,255,255,255,255,0,153,155,255,233,153,153,239,250,153,153,
  0,0,6,255,160,0,0,239,242,0,0,0,0,8,255,128,0,1,255,240,
  0,0,0,0,10,255,96,0,3,255,192,0,0,0,0,12,255,48,0,5,
  255,160,0,0,0,0,14,255,16,0,7,255,128,0,0,0,0,31,254,0,
  0,10,255,96,0,0,0,0,79,252,0,0,12,255,64,0,0,0,0,111,
  250,0,0,14,255,32,0,0,0,0,0,0,0,13,255,0,0,0,0,0,
  0,0,0,13,255,0,0,0,0,0,0,0,0,13,255,0,0,0,0,0,
  0,0,0,13,255,0,0,0,0,0,0,0,57,207,255,200,48,0,0,0,
  0,43,255,255,255,255,250,16,0,0,2,239,255,255,255,255,255,211,0,0,
  13,255,255,222,255,239,255,254,32,0,111,255,228,13,255,6,239,254,48,0,
  191,255,64,13,255,0,61,227,0,0,239,253,0,13,255,0,2,48,0,0,
  255,251,0,13,255,0,0,0,0,0,239,253,0,13,255,0,0,0,0,0,
  207,255,64,13,255,0,0,0,0,0,143,255,228,13,255,0,0,0,0,0,
  30,255,255,174,255,0,0,0,0,0,5,255,255,255,255,16,0,0,0,0,
  0,77,255,255,255,233,48,0,0,0,0,1,125,255,255,255,249,16,0,0,
  0,0,0,94,255,255,255,193,0,0,0,0,0,13,255,239,255,251,0,0,
  0,0,0,13,255,24,255,255,64,0,0,0,0,13,255,0,143,255,160,0,
  0,0,0,13,255,0,30,255,208,0,0,0,0,13,255,0,12,255,240,0,
  18,0,0,13,255,0,11,255,240,1,204,16,0,13,255,0,14,255,208,29,
  255,193,0,13,255,0,127,255,160,62,255,254,80,13,255,24,255,255,80,4,
  255,255,254,190,255,255,255,251,0,0,78,255,255,255,255,255,255,193,0,0,
  2,191,255,255,255,255,249,16,0,0,0,3,140,239,255,183,32,0,0,0,
  0,0,0,13,255,0,0,0,0,0,0,0,0,13,255,0,0,0,0,0,
  0,0,0,13,255,0,0,0,0,0,0,0,0,6,136,0,0,0,0,0,
  41,223,217,32,0,0,0,6,255,243,3,239,255,255,227,0,0,0,30,255,
  144,30,255,254,255,254,16,0,0,159,254,16,143,253,32,61,255,128,0,3,
  255,246,0,223,244,0,4,255,208,0,12,255,192,0,255,240,0,0,255,240,
  0,111,255,48,0,255,240,0,0,255,224,1,239,249,0,0,207,244,0,5,
  255,192,9,255,225,0,0,127,253,48,78,255,112,63,255,96,0,0,29,255,
  255,255,253,16,207,252,0,0,0,2,223,255,255,210,6,255,243,0,0,0,
  0,24,206,200,16,30,255,144,0,0,0,0,0,0,0,0,159,254,16,0,
  0,0,0,0,0,0,3,255,246,0,0,0,0,0,0,0,0,12,255,176,
  0,0,0,0,0,0,0,0,111,255,48,0,0,0,0,0,0,0,1,239,
  249,0,0,0,0,0,0,0,0,9,255,225,1,124,236,129,0,0,0,0,
  63,255,80,45,255,255,253,32,0,0,0,207,251,0,223,255,255,255,209,0,
  0,6,255,243,7,255,228,4,223,247,0,0,30,255,128,12,255,80,0,79,
  252,0,0,159,254,16,14,255,16,0,15,255,0,3,255,245,0,14,255,16,
  0,15,255,0,12,255,176,0,12,255,80,0,79,252,0,111,255,48,0,7,
  255,211,2,223,248,1,239,248,0,0,1,223,255,239,255,225,9,255,209,0,
  0,0,62,255,255,254,48,63,255,80,0,0,0,1,157,253,146,0,0,0,
  0,5,173,255,217,48,0,0,0,0,0,0,2,207,255,255,255,250,16,0,
  0,0,0,0,46,255,255,255,255,255,193,0,0,0,0,0,207,255,252,154,
  223,255,252,0,0,0,0,6,255,254,64,0,5,239,251,0,0,0,0,11,
  255,244,0,0,0,62,144,0,0,0,0,14,255,208,0,0,0,3,0,0,
  0,0,0,15,255,176,0,0,0,0,0,0,0,0,0,14,255,192,0,0,
  0,0,0,0,0,0,0,12,255,241,0,0,0,0,0,0,0,0,0,7,
  255,249,0,0,0,0,0,0,0,0,0,1,239,255,80,0,0,0,0,0,
  0,0,0,0,111,255,227,0,0,0,0,0,0,0,0,4,223,255,254,32,
  0,0,0,0,0,0,0,111,255,255,255,209,0,0,0,0,0,0,5,255,
  254,108,255,252,16,0,0,0,0,0,30,255,227,1,223,255,176,0,0,0,
  0,0,127,255,96,0,45,255,249,0,0,0,0,0,191,254,16,0,3,239,
  255,128,0,0,0,0,239,252,0,0,0,63,255,246,0,0,0,0,255,251,
  0,0,0,4,255,255,80,0,0,0,239,253,0,0,0,0,111,255,227,0,
  0,0,207,255,48,0,0,0,7,255,254,32,0,0,127,255,193,0,0,0,
  0,191,255,209,0,0,30,255,252,64,0,0,42,255,255,252,16,0,6,255,
  255,253,169,172,255,255,255,255,176,0,0,143,255,255,255,255,255,255,124,255,
  250,0,0,5,223,255,255,255,255,179,1,207,255,128,0,0,5,173,255,236,
  131,0,0,45,255,246,63,255,160,47,255,144,15,255,128,14,255,96,13,255,
  80,12,255,64,11,255,48,9,255,16,8,255,0,7,254,0,0,0,0,3,
  0,0,0,0,127,128,0,0,7,255,246,0,0,79,255,160,0,1,239,252,
  0,0,10,255,226,0,0,63,255,112,0,0,175,254,16,0,2,255,248,0,
  0,8,255,242,0,0,13,255,192,0,0,47,255,128,0,0,111,255,64,0,
  0,159,255,16,0,0,191,254,0,0,0,223,253,0,0,0,239,252,0,0,
  0,255,251,0,0,0,255,251,0,0,0,239,251,0,0,0,223,252,0,0,
  0,207,254,0,0,0,175,255,16,0,0,127,255,48,0,0,63,255,96,0,
  0,14,255,160,0,0,10,255,225,0,0,4,255,246,0,0,0,223,252,0,
  0,0,127,255,80,0,0,13,255,208,0,0,5,255,248,0,0,0,175,255,
  80,0,0,28,255,244,0,0,1,223,193,0,0,0,24,16,0,48,0,0,
  0,27,228,0,0,0,159,255,64,0,0,28,255,226,0,0,2,239,251,0,
  0,0,95,255,96,0,0,11,255,208,0,0,3,255,246,0,0,0,207,253,
  0,0,0,111,255,48,0,0,31,255,128,0,0,12,255,208,0,0,8,255,
  241,0,0,6,255,244,0,0,3,255,247,0,0,2,255,249,0,0,1,255,
  250,0,0,0,255,250,0,0,0,255,251,0,0,0,255,250,0,0,1,255,
  249,0,0,3,255,248,0,0,5,255,245,0,0,7,255,243,0,0,11,255,
  224,0,0,14,255,160,0,0,79,255,96,0,0,175,254,16,0,1,255,249,
  0,0,8,255,242,0,0,47,255,144,0,0,207,254,32,0,9,255,246,0,
  0,143,255,144,0,0,62,250,0,0,0,2,128,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,10,179,0,0,0,60,16,0,30,255,96,0,
  3,239,160,0,111,254,16,0,10,255,247,0,191,245,0,0,0,143,255,66,
  255,176,0,0,0,5,255,233,255,32,0,0,0,0,61,255,247,0,0,0,
  0,0,41,255,255,255,255,247,0,91,255,255,255,255,255,245,142,255,255,143,
  241,89,223,243,159,255,195,15,244,0,2,97,47,231,0,15,248,0,0,0,
  6,32,0,15,252,0,0,0,0,0,0,15,255,16,0,0,0,0,0,15,
  255,80,0,0,0,0,0,10,134,32,0,0,0,0,0,15,255,176,0,0,
  0,0,0,0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,
  0,0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,
  15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,153,153,153,159,255,
  217,153,153,144,255,255,255,255,255,255,255,255,240,255,255,255,255,255,255,255,
  255,240,255,255,255,255,255,255,255,255,240,0,0,0,15,255,176,0,0,0,
  0,0,0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,
  0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,15,
  255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,8,136,80,
  0,0,0,2,207,178,11,255,251,15,255,255,12,255,253,3,223,249,0,143,
  242,1,239,144,8,255,32,30,249,0,43,242,0,0,32,0,153,153,153,153,
  153,153,144,255,255,255,255,255,255,240,255,255,255,255,255,255,240,255,255,255,
  255,255,255,240,44,252,32,207,255,192,255,255,240,191,255,176,44,252,32,0,
  0,0,0,0,7,255,208,0,0,0,0,0,13,255,128,0,0,0,0,0,
  63,255,48,0,0,0,0,0,143,253,0,0,0,0,0,0,223,247,0,0,
  0,0,0,3,255,242,0,0,0,0,0,9,255,192,0,0,0,0,0,14,
  255,112,0,0,0,0,0,79,255,32,0,0,0,0,0,159,251,0,0,0,
  0,0,0,239,246,0,0,0,0,0,5,255,241,0,0,0,0,0,10,255,
  176,0,0,0,0,0,30,255,96,0,0,0,0,0,95,255,16,0,0,0,
  0,0,191,250,0,0,0,0,0,1,255,245,0,0,0,0,0,6,255,225,
  0,0,0,0,0,11,255,160,0,0,0,0,0,47,255,64,0,0,0,0,
  0,127,254,0,0,0,0,0,0,207,249,0,0,0,0,0,2,255,244,0,
  0,0,0,0,7,255,208,0,0,0,0,0,12,255,128,0,0,0,0,0,
  63,255,48,0,0,0,0,0,143,253,0,0,0,0,0,0,223,248,0,0,
  0,0,0,3,255,242,0,0,0,0,0,9,255,192,0,0,0,0,0,14,
  255,112,0,0,0,0,0,79,255,32,0,0,0,0,0,0,0,0,4,157,
  239,236,130,0,0,0,0,0,0,3,207,255,255,255,255,145,0,0,0,0,
  0,95,255,255,255,255,255,253,32,0,0,0,5,255,255,253,169,190,255,255,
  226,0,0,0,63,255,251,48,0,0,94,255,253,0,0,0,207,255,144,0,
  0,0,1,223,255,128,0,5,255,251,0,0,0,0,0,46,255,241,0,12,
  255,242,0,0,0,0,0,7,255,248,0,47,255,160,0,0,0,0,0,1,
  239,253,0,111,255,80,0,0,0,0,0,0,175,255,32,175,255,16,0,0,
  0,0,0,0,111,255,96,207,254,0,0,0,0,0,0,0,63,255,128,239,
  252,0,0,0,0,0,0,0,31,255,144,255,251,0,0,0,0,0,0,0,
  15,255,160,255,251,0,0,0,0,0,0,0,15,255,176,255,251,0,0,0,
  0,0,0,0,15,255,160,239,252,0,0,0,0,0,0,0,31,255,144,207,
  254,0,0,0,0,0,0,0,63,255,128,159,255,32,0,0,0,0,0,0,
  111,255,80,111,255,80,0,0,0,0,0,0,175,255,32,47,255,160,0,0,
  0,0,0,1,239,253,0,11,255,242,0,0,0,0,0,7,255,247,0,4,
  255,251,0,0,0,0,0,46,255,225,0,0,191,255,144,0,0,0,1,223,
  255,128,0,0,46,255,251,48,0,0,93,255,253,0,0,0,4,255,255,253,
  169,174,255,255,226,0,0,0,0,78,255,255,255,255,255,253,48,0,0,0,
  0,2,191,255,255,255,255,145,0,0,0,0,0,0,3,156,239,236,130,0,
  0,0,0,143,255,255,255,251,143,255,255,255,251,143,255,255,255,251,73,153,
  153,255,251,0,0,0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,
  0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,
  0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,
  0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,
  0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,
  0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,
  0,255,251,0,0,0,255,251,0,0,4,156,239,237,147,0,0,0,0,2,
  191,255,255,255,255,161,0,0,0,78,255,255,255,255,255,253,16,0,4,255,
  255,253,169,174,255,255,160,0,46,255,252,48,0,1,143,255,244,0,143,255,
  160,0,0,0,8,255,249,0,26,251,0,0,0,0,1,239,253,0,0,130,
  0,0,0,0,0,207,255,0,0,0,0,0,0,0,0,191,255,0,0,0,
  0,0,0,0,0,223,254,0,0,0,0,0,0,0,3,255,252,0,0,0,
  0,0,0,0,10,255,248,0,0,0,0,0,0,0,95,255,242,0,0,0,
  0,0,0,2,239,255,144,0,0,0,0,0,0,29,255,253,16,0,0,0,
  0,0,1,207,255,227,0,0,0,0,0,0,11,255,255,64,0,0,0,0,
  0,0,175,255,246,0,0,0,0,0,0,9,255,255,112,0,0,0,0,0,
  0,143,255,248,0,0,0,0,0,0,7,255,255,128,0,0,0,0,0,0,
  111,255,249,0,0,0,0,0,0,5,255,255,160,0,0,0,0,0,0,79,
  255,250,0,0,0,0,0,0,3,239,255,176,0,0,0,0,0,0,46,255,
  255,169,153,153,153,153,153,144,223,255,255,255,255,255,255,255,255,240,255,255,
  255,255,255,255,255,255,255,240,255,255,255,255,255,255,255,255,255,240,0,255,
  255,255,255,255,255,255,255,240,0,255,255,255,255,255,255,255,255,240,0,255,
  255,255,255,255,255,255,255,208,0,153,153,153,153,153,158,255,254,32,0,0,
  0,0,0,0,143,255,243,0,0,0,0,0,0,6,255,255,80,0,0,0,
  0,0,0,79,255,247,0,0,0,0,0,0,3,239,255,144,0,0,0,0,
  0,0,29,255,250,0,0,0,0,0,0,1,207,255,193,0,0,0,0,0,
  0,11,255,253,16,0,0,0,0,0,0,143,255,255,218,64,0,0,0,0,
  0,207,255,255,255,250,16,0,0,0,0,207,221,239,255,255,192,0,0,0,
  0,32,0,3,175,255,248,0,0,0,0,0,0,0,5,255,255,32,0,0,
  0,0,0,0,0,143,255,128,0,0,0,0,0,0,0,31,255,192,0,0,
  0,0,0,0,0,12,255,224,0,0,0,0,0,0,0,11,255,240,0,0,
  0,0,0,0,0,12,255,224,0,0,0,0,0,0,0,31,255,208,0,37,
  0,0,0,0,0,111,255,144,1,223,64,0,0,0,3,239,255,64,29,255,
  249,32,0,0,110,255,251,0,12,255,255,252,169,190,255,255,226,0,1,191,
  255,255,255,255,255,254,48,0,0,7,239,255,255,255,255,178,0,0,0,0,
  22,173,255,236,131,0,0,0,0,0,0,0,0,175,255,128,0,0,0,0,
  0,0,0,3,255,254,16,0,0,0,0,0,0,0,11,255,247,0,0,0,
  0,0,0,0,0,79,255,208,0,0,0,0,0,0,0,0,207,255,96,0,
  0,0,0,0,0,0,5,255,252,0,0,0,0,0,0,0,0,13,255,244,
  0,0,0,0,0,0,0,0,111,255,176,0,0,0,0,0,0,0,0,223,
  255,48,0,0,0,0,0,0,0,6,255,250,0,0,34,34,0,0,0,0,
  30,255,242,0,0,255,251,0,0,0,0,127,255,144,0,0,255,251,0,0,
  0,1,239,255,32,0,0,255,251,0,0,0,8,255,248,0,0,0,255,251,
  0,0,0,47,255,225,0,0,0,255,251,0,0,0,159,255,112,0,0,0,
  255,251,0,0,2,255,253,16,0,0,0,255,251,0,0,10,255,246,0,0,
  0,0,255,251,0,0,63,255,255,255,255,255,255,255,255,255,248,143,255,255,
  255,255,255,255,255,255,255,248,143,255,255,255,255,255,255,255,255,255,248,73,
  153,153,153,153,153,153,255,253,153,148,0,0,0,0,0,0,0,255,251,0,
  0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  0,0,0,255,251,0,0,0,5,255,255,255,255,255,255,255,80,0,7,255,
  255,255,255,255,255,255,80,0,8,255,255,255,255,255,255,255,80,0,9,255,
  233,153,153,153,153,153,48,0,11,255,160,0,0,0,0,0,0,0,12,255,
  144,0,0,0,0,0,0,0,14,255,112,0,0,0,0,0,0,0,15,255,
  96,0,0,0,0,0,0,0,31,255,64,0,0,0,0,0,0,0,63,255,
  48,0,0,0,0,0,0,0,79,255,39,206,255,218,64,0,0,0,111,255,
  239,255,255,255,252,32,0,0,127,255,255,255,255,255,255,227,0,0,79,255,
  253,185,155,255,255,253,16,0,5,251,48,0,0,24,255,255,128,0,0,48,
  0,0,0,0,111,255,225,0,0,0,0,0,0,0,10,255,245,0,0,0,
  0,0,0,0,4,255,248,0,0,0,0,0,0,0,1,255,250,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,1,255,250,0,0,0,
  0,0,0,0,5,255,248,0,22,0,0,0,0,0,12,255,244,1,191,112,
  0,0,0,0,143,255,208,11,255,250,48,0,0,42,255,255,80,10,255,255,
  253,169,172,255,255,249,0,0,175,255,255,255,255,255,255,144,0,0,5,223,
  255,255,255,255,229,0,0,0,0,5,173,239,253,165,16,0,0,0,0,0,
  0,0,207,255,96,0,0,0,0,0,0,8,255,251,0,0,0,0,0,0,
  0,63,255,226,0,0,0,0,0,0,1,223,255,96,0,0,0,0,0,0,
  9,255,250,0,0,0,0,0,0,0,79,255,225,0,0,0,0,0,0,1,
  223,255,80,0,0,0,0,0,0,9,255,249,0,0,0,0,0,0,0,95,
  255,209,0,0,0,0,0,0,1,239,255,152,152,81,0,0,0,0,10,255,
  255,255,255,254,112,0,0,0,95,255,255,255,255,255,251,16,0,1,239,255,
  255,255,255,255,255,192,0,8,255,255,213,16,38,223,255,248,0,30,255,250,
  0,0,0,10,255,255,32,111,255,192,0,0,0,0,207,255,112,175,255,64,
  0,0,0,0,79,255,176,223,254,0,0,0,0,0,14,255,224,255,251,0,
  0,0,0,0,11,255,240,255,251,0,0,0,0,0,11,255,240,239,253,0,
  0,0,0,0,13,255,208,191,255,32,0,0,0,0,47,255,160,127,255,144,
  0,0,0,0,159,255,96,47,255,245,0,0,0,5,255,254,16,8,255,255,
  112,0,1,127,255,246,0,0,207,255,254,169,174,255,255,160,0,0,28,255,
  255,255,255,255,251,16,0,0,1,159,255,255,255,255,112,0,0,0,0,2,
  140,239,236,113,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,
  255,255,255,255,255,255,255,255,255,255,255,255,255,254,153,153,153,153,153,153,
  154,255,249,0,0,0,0,0,0,6,255,243,0,0,0,0,0,0,12,255,
  208,0,0,0,0,0,0,63,255,112,0,0,0,0,0,0,143,255,32,0,
  0,0,0,0,0,239,251,0,0,0,0,0,0,5,255,245,0,0,0,0,
  0,0,11,255,225,0,0,0,0,0,0,31,255,144,0,0,0,0,0,0,
  127,255,64,0,0,0,0,0,0,223,253,0,0,0,0,0,0,3,255,248,
  0,0,0,0,0,0,9,255,242,0,0,0,0,0,0,30,255,192,0,0,
  0,0,0,0,111,255,96,0,0,0,0,0,0,191,254,16,0,0,0,0,
  0,2,255,250,0,0,0,0,0,0,8,255,244,0,0,0,0,0,0,13,
  255,208,0,0,0,0,0,0,79,255,128,0,0,0,0,0,0,175,255,32,
  0,0,0,0,0,1,255,252,0,0,0,0,0,0,6,255,246,0,0,0,
  0,0,0,12,255,241,0,0,0,0,0,0,63,255,160,0,0,0,0,0,
  0,143,255,64,0,0,0,0,0,0,3,140,239,236,131,0,0,0,0,1,
  159,255,255,255,255,145,0,0,0,28,255,255,255,255,255,252,16,0,0,175,
  255,254,169,174,255,255,160,0,3,255,255,129,0,1,143,255,243,0,8,255,
  249,0,0,0,9,255,248,0,10,255,242,0,0,0,2,255,250,0,11,255,
  240,0,0,0,0,255,251,0,9,255,242,0,0,0,2,255,249,0,5,255,
  249,0,0,0,9,255,245,0,0,223,255,129,0,1,143,255,208,0,0,63,
  255,254,169,174,255,255,48,0,0,4,223,255,255,255,255,228,0,0,0,4,
  223,255,255,255,255,195,0,0,0,127,255,255,255,255,255,255,80,0,6,255,
  255,164,16,37,207,255,243,0,30,255,245,0,0,0,9,255,252,0,127,255,
  112,0,0,0,0,191,255,48,207,254,16,0,0,0,0,79,255,112,239,252,
  0,0,0,0,0,31,255,160,255,251,0,0,0,0,0,15,255,176,239,253,
  0,0,0,0,0,63,255,160,207,255,64,0,0,0,0,143,255,112,127,255,
  209,0,0,0,3,255,255,48,30,255,253,64,0,0,110,255,251,0,6,255,
  255,253,169,174,255,255,226,0,0,143,255,255,255,255,255,254,64,0,0,5,
  223,255,255,255,255,178,0,0,0,0,5,173,255,236,148,0,0,0,0,0,
  1,107,239,253,165,0,0,0,0,0,94,255,255,255,255,212,0,0,0,9,
  255,255,255,255,255,255,112,0,0,159,255,255,185,172,255,255,247,0,5,255,
  255,145,0,0,59,255,255,48,13,255,246,0,0,0,0,175,255,176,95,255,
  160,0,0,0,0,13,255,243,175,255,32,0,0,0,0,6,255,247,223,253,
  0,0,0,0,0,2,255,250,255,251,0,0,0,0,0,0,255,251,255,252,
  0,0,0,0,0,1,255,250,239,254,0,0,0,0,0,4,255,249,191,255,
  80,0,0,0,0,9,255,246,127,255,209,0,0,0,0,63,255,241,30,255,
  252,16,0,0,3,239,255,176,7,255,255,231,48,19,159,255,255,48,0,175,
  255,255,255,255,255,255,250,0,0,10,255,255,255,255,255,255,225,0,0,0,
  93,255,255,255,255,255,64,0,0,0,0,71,153,126,255,249,0,0,0,0,
  0,0,0,143,255,193,0,0,0,0,0,0,4,255,255,48,0,0,0,0,
  0,0,30,255,247,0,0,0,0,0,0,0,191,255,176,0,0,0,0,0,
  0,6,255,254,16,0,0,0,0,0,0,63,255,245,0,0,0,0,0,0,
  1,223,255,144,0,0,0,0,0,0,9,255,253,16,0,0,0,0,0,0,
  95,255,243,0,0,0,0,0,44,252,32,207,255,192,255,255,240,207,255,192,
  44,252,32,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,44,252,32,207,255,192,255,255,240,207,255,192,44,
  252,32,2,207,194,12,255,252,15,255,255,12,255,252,2,207,194,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,2,191,178,11,255,251,15,255,255,12,255,253,3,223,249,0,111,242,0,
  223,144,7,255,32,30,250,0,26,242,0,0,32,0,0,0,0,0,0,0,
  0,5,192,0,0,0,0,0,0,6,223,240,0,0,0,0,0,23,239,255,
  240,0,0,0,0,40,239,255,255,224,0,0,0,57,255,255,255,214,16,0,
  0,75,255,255,255,180,0,0,0,92,255,255,255,147,0,0,0,109,255,255,
  253,113,0,0,0,0,255,255,252,80,0,0,0,0,0,255,255,230,16,0,
  0,0,0,0,191,255,255,232,32,0,0,0,0,3,175,255,255,250,48,0,
  0,0,0,2,159,255,255,252,80,0,0,0,0,1,142,255,255,253,113,0,
  0,0,0,1,125,255,255,254,144,0,0,0,0,0,92,255,255,240,0,0,
  0,0,0,0,75,255,240,0,0,0,0,0,0,0,58,240,0,0,0,0,
  0,0,0,0,32,255,255,255,255,255,255,255,255,240,255,255,255,255,255,255,
  255,255,240,255,255,255,255,255,255,255,255,240,153,153,153,153,153,153,153,153,
  144,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,153,153,153,153,153,153,153,153,144,255,255,255,255,255,
  255,255,255,240,255,255,255,255,255,255,255,255,240,255,255,255,255,255,255,255,
  255,240,197,0,0,0,0,0,0,0,0,255,214,0,0,0,0,0,0,0,
  255,255,231,16,0,0,0,0,0,239,255,255,232,32,0,0,0,0,22,223,
  255,255,249,48,0,0,0,0,4,191,255,255,251,64,0,0,0,0,3,159,
  255,255,252,80,0,0,0,0,1,125,255,255,253,96,0,0,0,0,0,92,
  255,255,240,0,0,0,0,0,22,239,255,240,0,0,0,0,40,239,255,255,
  176,0,0,0,58,255,255,255,163,0,0,0,92,255,255,255,146,0,0,1,
  125,255,255,254,129,0,0,0,158,255,255,253,113,0,0,0,0,255,255,252,
  80,0,0,0,0,0,255,251,64,0,0,0,0,0,0,250,48,0,0,0,
  0,0,0,0,32,0,0,0,0,0,0,0,0,0,0,4,157,239,236,130,
  0,0,0,2,191,255,255,255,255,112,0,0,78,255,255,255,255,255,250,0,
  2,239,255,253,169,191,255,255,112,11,255,252,48,0,2,191,255,225,10,255,
  193,0,0,0,12,255,246,0,142,32,0,0,0,4,255,249,0,2,0,0,
  0,0,1,255,251,0,0,0,0,0,0,0,255,250,0,0,0,0,0,0,
  3,255,249,0,0,0,0,0,0,8,255,245,0,0,0,0,0,0,79,255,
  225,0,0,0,0,0,56,255,255,112,0,0,0,0,255,255,255,250,0,0,
  0,0,0,255,255,255,144,0,0,0,0,0,255,255,180,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,2,207,194,0,0,0,0,0,0,12,255,252,0,0,0,0,0,0,15,
  255,255,0,0,0,0,0,0,12,255,252,0,0,0,0,0,0,2,207,194,
  0,0,0,0,0,0,0,22,156,222,219,132,0,0,0,0,0,0,0,41,
  255,255,255,255,255,214,0,0,0,0,0,6,239,255,255,255,255,255,255,194,
  0,0,0,0,159,255,252,115,16,18,90,255,253,32,0,0,9,255,253,64,
  0,0,0,0,44,255,209,0,0,111,255,193,0,0,0,0,0,0,175,251,
  0,2,239,253,16,0,0,0,0,0,0,12,255,80,9,255,244,0,3,156,
  203,114,153,96,4,255,192,31,255,176,0,143,255,255,254,255,160,0,207,243,
  111,255,80,7,255,255,255,255,255,160,0,127,247,175,255,16,47,255,146,2,
  159,255,160,0,79,251,223,253,0,143,249,0,0,10,255,160,0,31,253,239,
  251,0,207,242,0,0,4,255,160,0,15,254,255,251,0,207,240,0,0,2,
  255,160,0,15,254,239,251,0,191,242,0,0,4,255,160,0,31,254,223,253,
  0,127,249,0,0,10,255,160,0,47,252,191,255,0,30,255,146,2,175,255,
  160,0,95,250,127,255,64,5,255,255,255,255,255,255,255,255,245,47,255,144,
  0,78,255,255,251,255,255,255,255,224,11,255,242,0,1,88,151,48,68,68,
  68,68,32,3,255,251,0,0,0,0,0,0,0,0,0,0,0,159,255,144,
  0,0,0,0,0,1,0,0,0,0,12,255,251,32,0,0,0,0,61,144,
  0,0,0,1,207,255,233,65,0,1,90,255,248,0,0,0,0,25,255,255,
  255,237,239,255,255,211,0,0,0,0,0,76,255,255,255,255,255,231,0,0,
  0,0,0,0,0,56,189,255,236,149,0,0,0,0,0,0,0,0,0,1,
  255,242,0,0,0,0,0,0,0,0,0,0,0,7,255,248,0,0,0,0,
  0,0,0,0,0,0,0,13,255,253,0,0,0,0,0,0,0,0,0,0,
  0,79,255,255,80,0,0,0,0,0,0,0,0,0,0,175,255,255,176,0,
  0,0,0,0,0,0,0,0,1,255,255,255,242,0,0,0,0,0,0,0,
  0,0,7,255,252,255,247,0,0,0,0,0,0,0,0,0,13,255,226,255,
  253,0,0,0,0,0,0,0,0,0,79,255,160,175,255,64,0,0,0,0,
  0,0,0,0,175,255,64,79,255,160,0,0,0,0,0,0,0,1,255,253,
  0,13,255,241,0,0,0,0,0,0,0,7,255,247,0,7,255,247,0,0,
  0,0,0,0,0,13,255,241,0,2,255,253,0,0,0,0,0,0,0,79,
  255,160,0,0,191,255,64,0,0,0,0,0,0,175,255,64,0,0,95,255,
  160,0,0,0,0,0,1,255,253,0,0,0,14,255,241,0,0,0,0,0,
  7,255,247,0,0,0,8,255,247,0,0,0,0,0,13,255,242,0,0,0,
  2,255,253,0,0,0,0,0,63,255,176,0,0,0,0,191,255,64,0,0,
  0,0,159,255,255,255,255,255,255,255,255,160,0,0,0,1,239,255,255,255,
  255,255,255,255,255,241,0,0,0,6,255,255,255,255,255,255,255,255,255,247,
  0,0,0,12,255,249,153,153,153,153,153,154,255,252,0,0,0,63,255,176,
  0,0,0,0,0,0,207,255,48,0,0,159,255,80,0,0,0,0,0,0,
  111,255,144,0,1,239,254,0,0,0,0,0,0,0,31,255,225,0,6,255,
  249,0,0,0,0,0,0,0,10,255,246,0,12,255,243,0,0,0,0,0,
  0,0,4,255,252,0,63,255,192,0,0,0,0,0,0,0,0,223,255,48,
  255,255,255,255,255,254,201,48,0,0,255,255,255,255,255,255,255,250,16,0,
  255,255,255,255,255,255,255,255,193,0,255,253,153,153,153,154,239,255,249,0,
  255,251,0,0,0,0,8,255,255,32,255,251,0,0,0,0,0,159,255,112,
  255,251,0,0,0,0,0,47,255,160,255,251,0,0,0,0,0,15,255,176,
  255,251,0,0,0,0,0,31,255,160,255,251,0,0,0,0,0,79,255,112,
  255,251,0,0,0,0,1,207,255,32,255,251,0,0,0,1,92,255,248,0,
  255,255,255,255,255,255,255,255,176,0,255,255,255,255,255,255,255,252,0,0,
  255,255,255,255,255,255,255,255,177,0,255,253,153,153,153,153,207,255,252,16,
  255,251,0,0,0,0,3,207,255,144,255,251,0,0,0,0,0,29,255,242,
  255,251,0,0,0,0,0,6,255,246,255,251,0,0,0,0,0,1,255,249,
  255,251,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,2,255,250,
  255,251,0,0,0,0,0,6,255,248,255,251,0,0,0,0,0,30,255,244,
  255,251,0,0,0,0,3,223,255,208,255,253,153,153,153,154,207,255,255,64,
  255,255,255,255,255,255,255,255,246,0,255,255,255,255,255,255,255,253,64,0,
  255,255,255,255,255,255,218,96,0,0,0,0,0,0,5,156,239,254,201,80,
  0,0,0,0,0,0,7,239,255,255,255,255,253,96,0,0,0,0,3,223,
  255,255,255,255,255,255,252,32,0,0,0,95,255,255,253,169,155,239,255,255,
  228,0,0,5,255,255,232,32,0,0,3,175,255,249,0,0,46,255,251,32,
  0,0,0,0,4,223,160,0,0,207,255,160,0,0,0,0,0,0,40,0,
  0,6,255,252,16,0,0,0,0,0,0,0,0,0,12,255,243,0,0,0,
  0,0,0,0,0,0,0,63,255,176,0,0,0,0,0,0,0,0,0,0,
  143,255,80,0,0,0,0,0,0,0,0,0,0,191,255,16,0,0,0,0,
  0,0,0,0,0,0,223,253,0,0,0,0,0,0,0,0,0,0,0,255,
  252,0,0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  0,0,0,0,0,239,252,0,0,0,0,0,0,0,0,0,0,0,223,253,
  0,0,0,0,0,0,0,0,0,0,0,191,255,16,0,0,0,0,0,0,
  0,0,0,0,143,255,80,0,0,0,0,0,0,0,0,0,0,63,255,176,
  0,0,0,0,0,0,0,0,0,0,12,255,243,0,0,0,0,0,0,0,
  0,0,0,5,255,252,16,0,0,0,0,0,0,0,0,0,0,207,255,160,
  0,0,0,0,0,0,25,16,0,0,46,255,251,32,0,0,0,0,2,207,
  193,0,0,4,255,255,232,32,0,0,3,159,255,252,0,0,0,95,255,255,
  253,169,155,223,255,255,246,0,0,0,3,223,255,255,255,255,255,255,253,64,
  0,0,0,0,7,239,255,255,255,255,254,113,0,0,0,0,0,0,5,156,
  239,254,201,80,0,0,0,255,255,255,255,255,254,201,80,0,0,0,0,255,
  255,255,255,255,255,255,254,113,0,0,0,255,255,255,255,255,255,255,255,253,
  64,0,0,255,253,153,153,153,154,223,255,255,246,0,0,255,251,0,0,0,
  0,2,142,255,255,80,0,255,251,0,0,0,0,0,2,191,255,243,0,255,
  251,0,0,0,0,0,0,10,255,252,0,255,251,0,0,0,0,0,0,1,
  207,255,96,255,251,0,0,0,0,0,0,0,63,255,208,255,251,0,0,0,
  0,0,0,0,11,255,243,255,251,0,0,0,0,0,0,0,5,255,248,255,
  251,0,0,0,0,0,0,0,1,255,251,255,251,0,0,0,0,0,0,0,
  0,223,253,255,251,0,0,0,0,0,0,0,0,191,254,255,251,0,0,0,
  0,0,0,0,0,191,255,255,251,0,0,0,0,0,0,0,0,191,255,255,
  251,0,0,0,0,0,0,0,0,223,253,255,251,0,0,0,0,0,0,0,
  1,255,251,255,251,0,0,0,0,0,0,0,5,255,248,255,251,0,0,0,
  0,0,0,0,11,255,243,255,251,0,0,0,0,0,0,0,63,255,208,255,
  251,0,0,0,0,0,0,1,207,255,96,255,251,0,0,0,0,0,0,10,
  255,252,0,255,251,0,0,0,0,0,2,191,255,243,0,255,251,0,0,0,
  0,2,142,255,255,96,0,255,253,153,153,153,154,223,255,255,246,0,0,255,
  255,255,255,255,255,255,255,253,64,0,0,255,255,255,255,255,255,255,254,129,
  0,0,0,255,255,255,255,255,254,201,81,0,0,0,0,255,255,255,255,255,
  255,255,255,255,128,255,255,255,255,255,255,255,255,255,128,255,255,255,255,255,
  255,255,255,255,128,255,253,153,153,153,153,153,153,153,64,255,251,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,253,153,153,153,
  153,153,153,144,0,255,255,255,255,255,255,255,255,240,0,255,255,255,255,255,
  255,255,255,240,0,255,255,255,255,255,255,255,255,240,0,255,251,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,0,255,253,153,153,153,153,153,153,153,64,255,255,255,255,255,
  255,255,255,255,128,255,255,255,255,255,255,255,255,255,128,255,255,255,255,255,
  255,255,255,255,128,255,255,255,255,255,255,255,255,248,255,255,255,255,255,255,
  255,255,248,255,255,255,255,255,255,255,255,248,255,253,153,153,153,153,153,153,
  148,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,
  0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,
  0,0,255,255,255,255,255,255,255,255,240,255,255,255,255,255,255,255,255,240,
  255,255,255,255,255,255,255,255,240,255,253,153,153,153,153,153,153,144,255,251,
  0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,
  0,0,0,0,0,0,0,0,0,0,4,140,239,254,201,81,0,0,0,0,
  0,0,0,6,223,255,255,255,255,254,129,0,0,0,0,0,2,207,255,255,
  255,255,255,255,254,80,0,0,0,0,78,255,255,253,185,154,223,255,255,247,
  0,0,0,4,255,255,248,32,0,0,2,142,255,255,96,0,0,46,255,252,
  32,0,0,0,0,1,191,255,80,0,0,191,255,176,0,0,0,0,0,0,
  9,245,0,0,5,255,252,16,0,0,0,0,0,0,0,64,0,0,12,255,
  243,0,0,0,0,0,0,0,0,0,0,0,63,255,176,0,0,0,0,0,
  0,0,0,0,0,0,127,255,80,0,0,0,0,0,0,0,0,0,0,0,
  191,255,16,0,0,0,0,0,0,0,0,0,0,0,223,253,0,0,0,0,
  0,0,0,0,0,0,0,0,255,251,0,0,0,0,4,153,153,153,153,153,
  153,144,255,251,0,0,0,0,7,255,255,255,255,255,255,240,239,251,0,0,
  0,0,7,255,255,255,255,255,255,224,223,253,0,0,0,0,7,255,255,255,
  255,255,255,208,191,255,16,0,0,0,0,0,0,0,0,13,255,176,127,255,
  80,0,0,0,0,0,0,0,0,31,255,144,63,255,176,0,0,0,0,0,
  0,0,0,95,255,80,12,255,244,0,0,0,0,0,0,0,0,191,255,16,
  4,255,253,16,0,0,0,0,0,0,4,255,250,0,0,191,255,176,0,0,
  0,0,0,0,46,255,243,0,0,46,255,251,32,0,0,0,0,3,223,255,
  144,0,0,4,255,255,232,32,0,0,3,159,255,253,16,0,0,0,78,255,
  255,253,169,155,223,255,255,210,0,0,0,0,2,207,255,255,255,255,255,255,
  252,32,0,0,0,0,0,6,223,255,255,255,255,253,96,0,0,0,0,0,
  0,0,5,156,239,254,200,64,0,0,0,0,255,251,0,0,0,0,0,0,
  0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,
  0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,
  0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,
  0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,
  255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,
  255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,
  0,0,255,251,255,253,153,153,153,153,153,153,153,255,251,255,255,255,255,255,
  255,255,255,255,255,251,255,255,255,255,255,255,255,255,255,255,251,255,255,255,
  255,255,255,255,255,255,255,251,255,251,0,0,0,0,0,0,0,255,251,255,
  251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,
  251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,
  0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,
  0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,
  0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,
  0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,
  255,251,0,0,0,0,0,0,0,255,251,255,251,255,251,255,251,255,251,255,
  251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,
  251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,
  251,255,251,255,251,255,251,255,251,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,250,0,0,0,
  0,0,2,255,249,0,0,0,0,0,7,255,246,1,163,0,0,0,29,255,
  242,27,254,80,0,2,207,255,160,175,255,253,169,191,255,254,32,45,255,255,
  255,255,255,244,0,1,191,255,255,255,253,64,0,0,4,157,239,235,97,0,
  0,255,251,0,0,0,0,0,0,159,255,245,0,255,251,0,0,0,0,0,
  9,255,255,80,0,255,251,0,0,0,0,0,159,255,245,0,0,255,251,0,
  0,0,0,8,255,255,80,0,0,255,251,0,0,0,0,143,255,245,0,0,
  0,255,251,0,0,0,8,255,255,80,0,0,0,255,251,0,0,0,143,255,
  245,0,0,0,0,255,251,0,0,8,255,255,80,0,0,0,0,255,251,0,
  0,143,255,245,0,0,0,0,0,255,251,0,8,255,255,80,0,0,0,0,
  0,255,251,0,143,255,245,0,0,0,0,0,0,255,251,8,255,255,96,0,
  0,0,0,0,0,255,251,143,255,246,0,0,0,0,0,0,0,255,254,255,
  255,96,0,0,0,0,0,0,0,255,254,255,255,160,0,0,0,0,0,0,
  0,255,251,111,255,249,0,0,0,0,0,0,0,255,251,7,255,255,128,0,
  0,0,0,0,0,255,251,0,143,255,247,0,0,0,0,0,0,255,251,0,
  9,255,255,96,0,0,0,0,0,255,251,0,0,175,255,245,0,0,0,0,
  0,255,251,0,0,10,255,255,64,0,0,0,0,255,251,0,0,0,191,255,
  244,0,0,0,0,255,251,0,0,0,28,255,254,48,0,0,0,255,251,0,
  0,0,1,223,255,226,0,0,0,255,251,0,0,0,0,45,255,253,32,0,
  0,255,251,0,0,0,0,2,239,255,209,0,0,255,251,0,0,0,0,0,
  62,255,252,16,0,255,251,0,0,0,0,0,3,239,255,193,0,255,251,0,
  0,0,0,0,0,79,255,251,0,255,251,0,0,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,
  0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,
  0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,
  255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,253,153,153,153,153,
  153,153,148,255,255,255,255,255,255,255,255,248,255,255,255,255,255,255,255,255,
  248,255,255,255,255,255,255,255,255,248,255,209,0,0,0,0,0,0,0,0,
  0,1,223,240,255,247,0,0,0,0,0,0,0,0,0,7,255,240,255,254,
  32,0,0,0,0,0,0,0,0,46,255,240,255,255,160,0,0,0,0,0,
  0,0,0,175,255,240,255,255,244,0,0,0,0,0,0,0,4,255,255,240,
  255,255,252,0,0,0,0,0,0,0,12,255,255,240,255,255,255,96,0,0,
  0,0,0,0,111,255,255,240,255,255,255,225,0,0,0,0,0,1,239,255,
  255,240,255,254,255,249,0,0,0,0,0,9,255,255,255,240,255,251,191,255,
  48,0,0,0,0,63,255,203,255,240,255,251,63,255,176,0,0,0,0,191,
  255,59,255,240,255,251,9,255,245,0,0,0,5,255,249,11,255,240,255,251,
  1,239,253,16,0,0,29,255,225,11,255,240,255,251,0,111,255,128,0,0,
  143,255,112,11,255,240,255,251,0,12,255,242,0,2,255,253,0,11,255,240,
  255,251,0,4,255,250,0,10,255,244,0,11,255,240,255,251,0,0,175,255,
  64,79,255,160,0,11,255,240,255,251,0,0,46,255,192,207,255,32,0,11,
  255,240,255,251,0,0,7,255,252,255,248,0,0,11,255,240,255,251,0,0,
  0,223,255,255,209,0,0,11,255,240,255,251,0,0,0,95,255,255,80,0,
  0,11,255,240,255,251,0,0,0,11,255,251,0,0,0,11,255,240,255,251,
  0,0,0,2,255,243,0,0,0,11,255,240,255,251,0,0,0,0,17,16,
  0,0,0,11,255,240,255,251,0,0,0,0,0,0,0,0,0,11,255,240,
  255,251,0,0,0,0,0,0,0,0,0,11,255,240,255,251,0,0,0,0,
  0,0,0,0,0,11,255,240,255,251,0,0,0,0,0,0,0,0,0,11,
  255,240,255,251,0,0,0,0,0,0,0,0,0,11,255,240,255,225,0,0,
  0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,255,
  96,0,0,0,0,0,0,255,251,255,255,243,0,0,0,0,0,0,255,251,
  255,255,253,16,0,0,0,0,0,255,251,255,255,255,144,0,0,0,0,0,
  255,251,255,255,255,245,0,0,0,0,0,255,251,255,253,255,254,32,0,0,
  0,0,255,251,255,251,127,255,192,0,0,0,0,255,251,255,251,11,255,248,
  0,0,0,0,255,251,255,251,1,239,255,64,0,0,0,255,251,255,251,0,
  79,255,225,0,0,0,255,251,255,251,0,8,255,251,0,0,0,255,251,255,
  251,0,0,207,255,112,0,0,255,251,255,251,0,0,46,255,243,0,0,255,
  251,255,251,0,0,5,255,253,16,0,255,251,255,251,0,0,0,175,255,160,
  0,255,251,255,251,0,0,0,29,255,245,0,255,251,255,251,0,0,0,3,
  255,254,32,255,251,255,251,0,0,0,0,127,255,192,255,251,255,251,0,0,
  0,0,11,255,248,255,251,255,251,0,0,0,0,1,239,255,255,251,255,251,
  0,0,0,0,0,79,255,255,251,255,251,0,0,0,0,0,8,255,255,251,
  255,251,0,0,0,0,0,0,207,255,251,255,251,0,0,0,0,0,0,46,
  255,251,255,251,0,0,0,0,0,0,5,255,251,255,251,0,0,0,0,0,
  0,0,175,251,255,251,0,0,0,0,0,0,0,31,251,0,0,0,0,4,
  156,239,254,200,64,0,0,0,0,0,0,0,7,223,255,255,255,255,253,96,
  0,0,0,0,0,3,207,255,255,255,255,255,255,252,32,0,0,0,0,78,
  255,255,253,169,155,223,255,255,228,0,0,0,4,255,255,232,32,0,0,3,
  159,255,255,64,0,0,46,255,252,32,0,0,0,0,2,207,255,226,0,0,
  191,255,176,0,0,0,0,0,0,28,255,251,0,5,255,253,16,0,0,0,
  0,0,0,1,223,255,80,12,255,243,0,0,0,0,0,0,0,0,79,255,
  192,63,255,176,0,0,0,0,0,0,0,0,11,255,243,143,255,80,0,0,
  0,0,0,0,0,0,5,255,247,191,255,16,0,0,0,0,0,0,0,0,
  1,255,251,223,253,0,0,0,0,0,0,0,0,0,0,223,253,255,251,0,
  0,0,0,0,0,0,0,0,0,207,254,255,251,0,0,0,0,0,0,0,
  0,0,0,191,255,239,252,0,0,0,0,0,0,0,0,0,0,191,254,223,
  253,0,0,0,0,0,0,0,0,0,0,223,253,191,255,16,0,0,0,0,
  0,0,0,0,1,255,251,127,255,80,0,0,0,0,0,0,0,0,5,255,
  247,47,255,176,0,0,0,0,0,0,0,0,11,255,243,11,255,244,0,0,
  0,0,0,0,0,0,79,255,192,4,255,253,16,0,0,0,0,0,0,1,
  223,255,64,0,191,255,177,0,0,0,0,0,0,28,255,251,0,0,30,255,
  252,32,0,0,0,0,2,207,255,226,0,0,3,239,255,249,32,0,0,2,
  159,255,254,48,0,0,0,62,255,255,253,169,154,223,255,255,228,0,0,0,
  0,2,207,255,255,255,255,255,255,252,32,0,0,0,0,0,6,223,255,255,
  255,255,253,96,0,0,0,0,0,0,0,4,140,239,254,201,64,0,0,0,
  0,255,255,255,255,255,254,182,16,0,0,255,255,255,255,255,255,255,229,0,
  0,255,255,255,255,255,255,255,255,128,0,255,253,153,153,153,172,255,255,245,
  0,255,251,0,0,0,0,60,255,254,16,255,251,0,0,0,0,1,207,255,
  96,255,251,0,0,0,0,0,79,255,176,255,251,0,0,0,0,0,13,255,
  224,255,251,0,0,0,0,0,11,255,240,255,251,0,0,0,0,0,11,255,
  240,255,251,0,0,0,0,0,13,255,208,255,251,0,0,0,0,0,79,255,
  176,255,251,0,0,0,0,1,207,255,96,255,251,0,0,0,0,60,255,254,
  16,255,253,153,153,153,156,255,255,245,0,255,255,255,255,255,255,255,255,112,
  0,255,255,255,255,255,255,255,229,0,0,255,255,255,255,255,254,182,16,0,
  0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,0,0,0,0,0,4,156,239,254,201,
  64,0,0,0,0,0,0,0,0,6,223,255,255,255,255,253,96,0,0,0,
  0,0,0,2,207,255,255,255,255,255,255,252,32,0,0,0,0,0,78,255,
  255,253,169,155,223,255,255,228,0,0,0,0,4,255,255,232,32,0,0,2,
  159,255,255,64,0,0,0,46,255,252,32,0,0,0,0,2,207,255,226,0,
  0,0,191,255,176,0,0,0,0,0,0,27,255,251,0,0,5,255,253,16,
  0,0,0,0,0,0,1,223,255,80,0,12,255,243,0,0,0,0,0,0,
  0,0,79,255,192,0,63,255,176,0,0,0,0,0,0,0,0,11,255,243,
  0,127,255,80,0,0,0,0,0,0,0,0,5,255,247,0,191,255,16,0,
  0,0,0,0,0,0,0,1,255,251,0,223,253,0,0,0,0,0,0,0,
  0,0,0,223,253,0,255,251,0,0,0,0,0,0,0,0,0,0,207,254,
  0,255,251,0,0,0,0,0,0,0,0,0,0,191,255,0,239,252,0,0,
  0,0,0,0,182,0,0,0,191,254,0,223,253,0,0,0,0,0,11,255,
  96,0,0,223,253,0,191,255,16,0,0,0,0,111,255,246,0,1,255,251,
  0,127,255,96,0,0,0,0,9,255,255,80,5,255,247,0,47,255,192,0,
  0,0,0,0,159,255,245,11,255,243,0,11,255,244,0,0,0,0,0,9,
  255,255,143,255,192,0,4,255,253,16,0,0,0,0,0,159,255,255,255,80,
  0,0,175,255,177,0,0,0,0,0,9,255,255,251,0,0,0,29,255,252,
  48,0,0,0,0,2,239,255,246,0,0,0,3,239,255,249,48,0,0,2,
  143,255,255,255,64,0,0,0,62,255,255,253,185,154,223,255,255,255,255,228,
  0,0,0,2,191,255,255,255,255,255,255,252,58,255,254,48,0,0,0,5,
  223,255,255,255,255,253,96,0,191,255,227,0,0,0,0,4,140,239,254,201,
  64,0,0,11,255,246,0,0,0,0,0,0,0,0,0,0,0,0,0,191,
  112,0,0,0,0,0,0,0,0,0,0,0,0,0,21,0,255,255,255,255,
  255,254,200,32,0,0,0,255,255,255,255,255,255,255,248,0,0,0,255,255,
  255,255,255,255,255,255,176,0,0,255,253,153,153,153,155,239,255,249,0,0,
  255,251,0,0,0,0,24,255,255,48,0,255,251,0,0,0,0,0,159,255,
  144,0,255,251,0,0,0,0,0,31,255,208,0,255,251,0,0,0,0,0,
  12,255,224,0,255,251,0,0,0,0,0,11,255,240,0,255,251,0,0,0,
  0,0,13,255,224,0,255,251,0,0,0,0,0,63,255,176,0,255,251,0,
  0,0,0,1,207,255,112,0,255,251,0,0,0,2,109,255,254,16,0,255,
  255,255,255,255,255,255,255,245,0,0,255,255,255,255,255,255,255,255,96,0,
  0,255,255,255,255,255,255,255,179,0,0,0,255,253,153,223,255,249,98,0,
  0,0,0,255,251,0,46,255,251,0,0,0,0,0,255,251,0,4,255,255,
  128,0,0,0,0,255,251,0,0,127,255,245,0,0,0,0,255,251,0,0,
  10,255,254,32,0,0,0,255,251,0,0,1,207,255,209,0,0,0,255,251,
  0,0,0,46,255,251,0,0,0,255,251,0,0,0,4,255,255,128,0,0,
  255,251,0,0,0,0,127,255,245,0,0,255,251,0,0,0,0,10,255,254,
  32,0,255,251,0,0,0,0,1,207,255,209,0,255,251,0,0,0,0,0,
  46,255,250,0,255,251,0,0,0,0,0,4,255,255,112,0,0,0,57,206,
  254,217,64,0,0,0,0,43,255,255,255,255,252,64,0,0,2,239,255,255,
  255,255,255,247,0,0,13,255,255,218,154,223,255,255,96,0,111,255,228,0,
  0,4,207,255,64,0,191,255,48,0,0,0,10,244,0,0,239,252,0,0,
  0,0,0,48,0,0,255,251,0,0,0,0,0,0,0,0,239,253,0,0,
  0,0,0,0,0,0,207,255,80,0,0,0,0,0,0,0,143,255,246,0,
  0,0,0,0,0,0,46,255,255,197,0,0,0,0,0,0,6,255,255,255,
  232,32,0,0,0,0,0,94,255,255,255,251,64,0,0,0,0,1,158,255,
  255,255,250,16,0,0,0,0,1,108,255,255,255,210,0,0,0,0,0,0,
  57,255,255,252,0,0,0,0,0,0,0,42,255,255,80,0,0,0,0,0,
  0,0,175,255,160,0,0,0,0,0,0,0,31,255,208,0,0,0,0,0,
  0,0,12,255,240,0,32,0,0,0,0,0,11,255,240,3,231,0,0,0,
  0,0,14,255,208,62,255,96,0,0,0,0,111,255,160,143,255,251,48,0,
  0,24,255,255,80,10,255,255,253,169,155,239,255,251,0,0,159,255,255,255,
  255,255,255,193,0,0,5,223,255,255,255,255,249,16,0,0,0,5,156,239,
  253,183,32,0,0,143,255,255,255,255,255,255,255,255,255,255,240,143,255,255,
  255,255,255,255,255,255,255,255,240,143,255,255,255,255,255,255,255,255,255,255,
  240,73,153,153,153,153,255,253,153,153,153,153,144,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,
  0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,
  0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,
  0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,
  0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,
  0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,0,0,0,255,251,0,0,0,0,0,255,251,0,0,0,0,0,
  0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,
  0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,
  0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,
  251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,
  251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,
  0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,
  0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,
  0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,
  0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,
  255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,
  255,251,239,253,0,0,0,0,0,0,2,255,250,191,255,16,0,0,0,0,
  0,5,255,247,127,255,112,0,0,0,0,0,11,255,243,47,255,226,0,0,
  0,0,0,79,255,208,10,255,252,16,0,0,0,3,239,255,96,2,239,255,
  230,0,0,1,127,255,251,0,0,79,255,255,235,154,191,255,255,210,0,0,
  4,239,255,255,255,255,255,252,32,0,0,0,42,255,255,255,255,255,129,0,
  0,0,0,0,56,206,255,219,113,0,0,0,159,255,128,0,0,0,0,0,
  0,0,14,255,242,63,255,208,0,0,0,0,0,0,0,95,255,176,12,255,
  244,0,0,0,0,0,0,0,191,255,80,6,255,249,0,0,0,0,0,0,
  1,255,254,0,1,255,254,16,0,0,0,0,0,7,255,248,0,0,175,255,
  96,0,0,0,0,0,13,255,242,0,0,79,255,176,0,0,0,0,0,63,
  255,192,0,0,13,255,242,0,0,0,0,0,159,255,96,0,0,8,255,247,
  0,0,0,0,0,239,254,16,0,0,2,255,253,0,0,0,0,5,255,249,
  0,0,0,0,191,255,64,0,0,0,11,255,243,0,0,0,0,95,255,144,
  0,0,0,47,255,208,0,0,0,0,14,255,225,0,0,0,127,255,112,0,
  0,0,0,9,255,245,0,0,0,223,255,16,0,0,0,0,3,255,251,0,
  0,3,255,250,0,0,0,0,0,0,207,255,32,0,9,255,244,0,0,0,
  0,0,0,127,255,112,0,30,255,208,0,0,0,0,0,0,31,255,208,0,
  95,255,128,0,0,0,0,0,0,10,255,244,0,191,255,32,0,0,0,0,
  0,0,4,255,249,2,255,251,0,0,0,0,0,0,0,0,223,254,23,255,
  245,0,0,0,0,0,0,0,0,143,255,93,255,225,0,0,0,0,0,0,
  0,0,47,255,223,255,144,0,0,0,0,0,0,0,0,11,255,255,255,48,
  0,0,0,0,0,0,0,0,6,255,255,252,0,0,0,0,0,0,0,0,
  0,1,239,255,246,0,0,0,0,0,0,0,0,0,0,159,255,241,0,0,
  0,0,0,0,0,0,0,0,63,255,160,0,0,0,0,0,0,0,0,0,
  0,12,255,64,0,0,0,0,0,159,255,48,0,0,0,0,0,14,255,32,
  0,0,0,0,0,14,255,192,79,255,128,0,0,0,0,0,63,255,96,0,
  0,0,0,0,79,255,112,14,255,192,0,0,0,0,0,143,255,176,0,0,
  0,0,0,143,255,32,9,255,242,0,0,0,0,0,207,255,241,0,0,0,
  0,0,223,253,0,5,255,246,0,0,0,0,2,255,255,245,0,0,0,0,
  3,255,248,0,1,239,251,0,0,0,0,6,255,255,249,0,0,0,0,7,
  255,243,0,0,175,255,16,0,0,0,11,255,255,254,0,0,0,0,12,255,
  208,0,0,111,255,80,0,0,0,31,255,207,255,48,0,0,0,31,255,144,
  0,0,31,255,144,0,0,0,95,255,62,255,128,0,0,0,111,255,64,0,
  0,11,255,224,0,0,0,159,253,10,255,192,0,0,0,175,254,0,0,0,
  6,255,244,0,0,0,239,249,6,255,242,0,0,1,239,250,0,0,0,2,
  255,248,0,0,3,255,244,1,255,246,0,0,5,255,245,0,0,0,0,207,
  253,0,0,8,255,225,0,207,251,0,0,9,255,241,0,0,0,0,127,255,
  32,0,12,255,160,0,127,255,16,0,14,255,176,0,0,0,0,63,255,112,
  0,47,255,96,0,63,255,80,0,63,255,96,0,0,0,0,13,255,176,0,
  111,255,16,0,13,255,144,0,143,255,16,0,0,0,0,8,255,241,0,191,
  252,0,0,9,255,224,0,207,252,0,0,0,0,0,4,255,246,1,255,247,
  0,0,4,255,243,2,255,247,0,0,0,0,0,0,239,250,5,255,243,0,
  0,0,239,248,7,255,242,0,0,0,0,0,0,159,254,9,255,208,0,0,
  0,175,252,11,255,208,0,0,0,0,0,0,95,255,78,255,144,0,0,0,
  95,255,63,255,128,0,0,0,0,0,0,30,255,207,255,64,0,0,0,31,
  255,191,255,48,0,0,0,0,0,0,10,255,255,254,16,0,0,0,11,255,
  255,253,0,0,0,0,0,0,0,5,255,255,250,0,0,0,0,7,255,255,
  249,0,0,0,0,0,0,0,1,255,255,246,0,0,0,0,2,255,255,244,
  0,0,0,0,0,0,0,0,191,255,242,0,0,0,0,0,223,255,224,0,
  0,0,0,0,0,0,0,111,255,192,0,0,0,0,0,143,255,160,0,0,
  0,0,0,0,0,0,47,255,112,0,0,0,0,0,79,255,80,0,0,0,
  0,0,0,0,0,12,255,48,0,0,0,0,0,14,255,16,0,0,0,0,
  29,255,248,0,0,0,0,0,0,0,159,255,160,4,255,255,48,0,0,0,
  0,0,4,255,254,16,0,159,255,209,0,0,0,0,0,29,255,244,0,0,
  29,255,249,0,0,0,0,0,159,255,144,0,0,3,255,255,64,0,0,0,
  4,255,253,16,0,0,0,127,255,209,0,0,0,29,255,244,0,0,0,0,
  12,255,250,0,0,0,159,255,128,0,0,0,0,2,239,255,80,0,5,255,
  252,0,0,0,0,0,0,111,255,226,0,30,255,243,0,0,0,0,0,0,
  10,255,251,0,175,255,112,0,0,0,0,0,0,1,239,255,101,255,252,0,
  0,0,0,0,0,0,0,95,255,238,255,226,0,0,0,0,0,0,0,0,
  9,255,255,255,96,0,0,0,0,0,0,0,0,1,223,255,251,0,0,0,
  0,0,0,0,0,0,0,191,255,252,0,0,0,0,0,0,0,0,0,6,
  255,255,255,112,0,0,0,0,0,0,0,0,46,255,255,255,243,0,0,0,
  0,0,0,0,0,191,255,104,255,252,0,0,0,0,0,0,0,7,255,251,
  0,207,255,112,0,0,0,0,0,0,63,255,226,0,63,255,242,0,0,0,
  0,0,0,223,255,80,0,8,255,252,0,0,0,0,0,9,255,250,0,0,
  1,223,255,112,0,0,0,0,79,255,209,0,0,0,79,255,242,0,0,0,
  1,223,255,64,0,0,0,9,255,252,0,0,0,10,255,248,0,0,0,0,
  1,223,255,112,0,0,95,255,208,0,0,0,0,0,79,255,226,0,2,239,
  255,48,0,0,0,0,0,9,255,251,0,11,255,247,0,0,0,0,0,0,
  1,223,255,112,127,255,192,0,0,0,0,0,0,0,79,255,226,30,255,246,
  0,0,0,0,0,0,0,159,255,160,6,255,254,16,0,0,0,0,0,3,
  255,254,32,0,191,255,144,0,0,0,0,0,12,255,247,0,0,47,255,243,
  0,0,0,0,0,111,255,192,0,0,8,255,252,0,0,0,0,1,239,255,
  64,0,0,1,223,255,96,0,0,0,10,255,249,0,0,0,0,95,255,225,
  0,0,0,79,255,225,0,0,0,0,10,255,249,0,0,0,223,255,96,0,
  0,0,0,2,239,255,64,0,7,255,251,0,0,0,0,0,0,127,255,192,
  0,46,255,242,0,0,0,0,0,0,13,255,247,0,175,255,128,0,0,0,
  0,0,0,4,255,254,36,255,253,16,0,0,0,0,0,0,0,175,255,173,
  255,244,0,0,0,0,0,0,0,0,30,255,255,255,160,0,0,0,0,0,
  0,0,0,6,255,255,254,32,0,0,0,0,0,0,0,0,0,207,255,247,
  0,0,0,0,0,0,0,0,0,0,63,255,208,0,0,0,0,0,0,0,
  0,0,0,15,255,176,0,0,0,0,0,0,0,0,0,0,15,255,176,0,
  0,0,0,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,0,0,
  0,0,15,255,176,0,0,0,0,0,0,0,0,0,0,15,255,176,0,0,
  0,0,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,0,0,0,
  0,15,255,176,0,0,0,0,0,0,0,0,0,0,15,255,176,0,0,0,
  0,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,0,0,0,0,
  15,255,176,0,0,0,0,0,0,0,0,0,0,15,255,176,0,0,0,0,
  0,0,0,0,0,0,15,255,176,0,0,0,0,0,15,255,255,255,255,255,
  255,255,255,255,15,255,255,255,255,255,255,255,255,255,15,255,255,255,255,255,
  255,255,255,255,9,153,153,153,153,153,153,158,255,247,0,0,0,0,0,0,
  0,95,255,192,0,0,0,0,0,0,1,239,255,48,0,0,0,0,0,0,
  10,255,247,0,0,0,0,0,0,0,111,255,192,0,0,0,0,0,0,2,
  239,255,48,0,0,0,0,0,0,11,255,248,0,0,0,0,0,0,0,111,
  255,208,0,0,0,0,0,0,2,239,255,64,0,0,0,0,0,0,11,255,
  249,0,0,0,0,0,0,0,111,255,209,0,0,0,0,0,0,2,239,255,
  64,0,0,0,0,0,0,11,255,249,0,0,0,0,0,0,0,111,255,225,
  0,0,0,0,0,0,2,239,255,80,0,0,0,0,0,0,11,255,250,0,
  0,0,0,0,0,0,127,255,225,0,0,0,0,0,0,2,239,255,80,0,
  0,0,0,0,0,11,255,250,0,0,0,0,0,0,0,127,255,226,0,0,
  0,0,0,0,2,255,255,96,0,0,0,0,0,0,12,255,251,0,0,0,
  0,0,0,0,127,255,251,153,153,153,153,153,153,153,255,255,255,255,255,255,
  255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
  255,255,255,255,255,255,255,255,240,255,255,255,255,240,255,254,238,238,224,255,
  240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,
  240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,
  240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,
  240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,
  240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,
  240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,
  240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,255,255,255,240,255,
  255,255,255,240,255,255,255,255,240,79,255,32,0,0,0,0,0,14,255,112,
  0,0,0,0,0,9,255,192,0,0,0,0,0,3,255,242,0,0,0,0,
  0,0,223,248,0,0,0,0,0,0,143,253,0,0,0,0,0,0,63,255,
  48,0,0,0,0,0,12,255,128,0,0,0,0,0,7,255,208,0,0,0,
  0,0,2,255,244,0,0,0,0,0,0,207,249,0,0,0,0,0,0,127,
  254,0,0,0,0,0,0,47,255,64,0,0,0,0,0,11,255,160,0,0,
  0,0,0,6,255,225,0,0,0,0,0,1,255,245,0,0,0,0,0,0,
  191,250,0,0,0,0,0,0,95,255,16,0,0,0,0,0,30,255,96,0,
  0,0,0,0,10,255,176,0,0,0,0,0,5,255,241,0,0,0,0,0,
  0,239,246,0,0,0,0,0,0,159,251,0,0,0,0,0,0,79,255,32,
  0,0,0,0,0,14,255,112,0,0,0,0,0,9,255,192,0,0,0,0,
  0,3,255,242,0,0,0,0,0,0,223,247,0,0,0,0,0,0,143,253,
  0,0,0,0,0,0,63,255,48,0,0,0,0,0,13,255,128,0,0,0,
  0,0,7,255,208,255,255,255,255,240,255,255,255,255,240,238,238,238,255,240,
  0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,
  0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,
  0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,
  0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,
  0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,
  0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,
  0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,255,255,255,255,240,
  255,255,255,255,240,255,255,255,255,240,0,0,2,136,48,0,0,0,0,11,
  255,192,0,0,0,0,79,255,245,0,0,0,0,207,255,253,0,0,0,5,
  255,184,255,96,0,0,13,255,49,239,225,0,0,111,249,0,127,248,0,1,
  239,226,0,30,255,32,8,255,128,0,6,255,144,46,254,16,0,0,223,243,
  159,246,0,0,0,95,251,153,153,153,153,153,153,153,153,153,144,255,255,255,
  255,255,255,255,255,255,240,255,255,255,255,255,255,255,255,255,240,255,255,255,
  255,255,255,255,255,255,240,0,3,0,0,0,0,143,64,0,0,8,255,226,
  0,0,46,255,252,0,0,3,223,255,144,0,0,44,255,246,0,0,1,175,
  255,48,0,0,8,255,64,0,0,0,85,0,0,0,3,140,239,235,96,11,
  255,240,0,1,159,255,255,255,253,59,255,240,0,28,255,255,255,255,255,253,
  255,240,0,207,255,255,185,172,255,255,255,240,8,255,255,129,0,0,42,255,
  255,240,30,255,246,0,0,0,0,143,255,240,127,255,160,0,0,0,0,13,
  255,240,191,255,32,0,0,0,0,7,255,240,239,253,0,0,0,0,0,4,
  255,240,255,251,0,0,0,0,0,2,255,240,255,251,0,0,0,0,0,3,
  255,240,239,253,0,0,0,0,0,5,255,240,191,255,32,0,0,0,0,8,
  255,240,127,255,160,0,0,0,0,13,255,240,31,255,246,0,0,0,0,159,
  255,240,8,255,255,145,0,0,42,255,255,240,1,207,255,255,185,156,255,255,
  255,240,0,44,255,255,255,255,255,253,255,240,0,1,159,255,255,255,253,75,
  255,240,0,0,3,156,239,235,97,11,255,240,255,240,0,0,0,0,0,0,
  0,0,255,240,0,0,0,0,0,0,0,0,255,240,0,0,0,0,0,0,
  0,0,255,240,0,0,0,0,0,0,0,0,255,240,0,0,0,0,0,0,
  0,0,255,240,0,0,0,0,0,0,0,0,255,240,0,0,0,0,0,0,
  0,0,255,240,0,0,0,0,0,0,0,0,255,240,0,0,0,0,0,0,
  0,0,255,240,1,107,239,236,130,0,0,0,255,240,77,255,255,255,255,145,
  0,0,255,245,255,255,255,255,255,252,16,0,255,255,255,252,169,191,255,255,
  192,0,255,255,250,32,0,1,143,255,248,0,255,255,144,0,0,0,6,255,
  255,16,255,253,0,0,0,0,0,175,255,112,255,246,0,0,0,0,0,47,
  255,176,255,242,0,0,0,0,0,13,255,224,255,240,0,0,0,0,0,11,
  255,240,255,240,0,0,0,0,0,11,255,240,255,242,0,0,0,0,0,13,
  255,224,255,246,0,0,0,0,0,63,255,176,255,253,0,0,0,0,0,175,
  255,112,255,255,144,0,0,0,6,255,255,32,255,255,250,32,0,1,159,255,
  248,0,255,255,255,252,169,191,255,255,193,0,255,246,255,255,255,255,255,253,
  32,0,255,240,94,255,255,255,255,145,0,0,255,240,1,107,239,236,147,0,
  0,0,0,0,1,123,239,253,165,0,0,0,0,126,255,255,255,255,212,0,
  0,27,255,255,255,255,255,255,112,0,191,255,255,185,156,255,255,209,7,255,
  255,145,0,0,25,253,32,30,255,246,0,0,0,0,82,0,111,255,160,0,
  0,0,0,0,0,191,255,32,0,0,0,0,0,0,239,253,0,0,0,0,
  0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  0,223,253,0,0,0,0,0,0,0,191,255,32,0,0,0,0,0,0,111,
  255,160,0,0,0,0,0,0,30,255,246,0,0,0,0,83,0,6,255,255,
  145,0,0,24,254,32,0,175,255,255,185,155,255,255,209,0,10,255,255,255,
  255,255,255,112,0,0,110,255,255,255,255,212,0,0,0,1,107,239,253,165,
  0,0,0,0,0,0,0,0,0,11,255,240,0,0,0,0,0,0,0,11,
  255,240,0,0,0,0,0,0,0,11,255,240,0,0,0,0,0,0,0,11,
  255,240,0,0,0,0,0,0,0,11,255,240,0,0,0,0,0,0,0,11,
  255,240,0,0,0,0,0,0,0,11,255,240,0,0,0,0,0,0,0,11,
  255,240,0,0,0,0,0,0,0,11,255,240,0,0,3,156,239,235,96,11,
  255,240,0,1,159,255,255,255,253,59,255,240,0,28,255,255,255,255,255,237,
  255,240,0,207,255,255,185,172,255,255,255,240,8,255,255,129,0,0,43,255,
  255,240,30,255,246,0,0,0,0,175,255,240,127,255,160,0,0,0,0,13,
  255,240,191,255,32,0,0,0,0,7,255,240,239,253,0,0,0,0,0,4,
  255,240,255,251,0,0,0,0,0,2,255,240,255,251,0,0,0,0,0,3,
  255,240,239,253,0,0,0,0,0,5,255,240,191,255,32,0,0,0,0,8,
  255,240,127,255,160,0,0,0,0,13,255,240,31,255,246,0,0,0,0,159,
  255,240,8,255,255,145,0,0,43,255,255,240,1,207,255,255,185,172,255,255,
  255,240,0,45,255,255,255,255,255,253,255,240,0,1,175,255,255,255,253,59,
  255,240,0,0,3,157,255,235,96,11,255,240,0,0,1,107,239,253,165,0,
  0,0,0,0,110,255,255,255,255,195,0,0,0,10,255,255,255,255,255,254,
  64,0,0,175,255,254,185,172,255,255,226,0,6,255,255,113,0,0,60,255,
  251,0,30,255,244,0,0,0,1,223,255,48,111,255,128,0,0,0,0,95,
  255,144,191,255,16,0,0,0,0,14,255,192,239,255,255,255,255,255,255,255,
  255,224,255,255,255,255,255,255,255,255,255,240,255,255,255,255,255,255,255,255,
  255,224,223,255,153,153,153,153,153,153,153,128,191,255,32,0,0,0,0,0,
  0,0,111,255,144,0,0,0,0,0,0,0,30,255,246,0,0,0,0,38,
  0,0,6,255,255,145,0,0,6,239,144,0,0,175,255,255,201,155,239,255,
  247,0,0,10,255,255,255,255,255,255,177,0,0,0,110,255,255,255,255,232,
  0,0,0,0,1,106,223,254,183,16,0,0,0,0,0,0,40,206,253,146,
  0,0,0,0,6,239,255,255,254,80,0,0,0,95,255,255,255,255,208,0,
  0,1,239,255,234,156,253,32,0,0,8,255,251,16,0,98,0,0,0,12,
  255,242,0,0,0,0,0,0,14,255,192,0,0,0,0,0,0,15,255,176,
  0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,
  0,0,143,255,255,255,255,255,255,128,0,143,255,255,255,255,255,255,128,0,
  143,255,255,255,255,255,255,128,0,73,153,159,255,217,153,153,64,0,0,0,
  15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,15,255,
  176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,15,255,176,0,
  0,0,0,0,0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,
  0,0,0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,
  0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,15,
  255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,15,255,176,
  0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,
  0,0,0,0,4,173,255,218,64,11,255,240,0,1,191,255,255,255,251,27,
  255,240,0,45,255,255,255,255,255,204,255,240,1,223,255,254,185,174,255,255,
  255,240,9,255,255,112,0,0,110,255,255,240,47,255,244,0,0,0,4,255,
  255,240,127,255,144,0,0,0,0,143,255,240,191,255,32,0,0,0,0,47,
  255,240,239,253,0,0,0,0,0,13,255,240,255,251,0,0,0,0,0,11,
  255,240,255,251,0,0,0,0,0,11,255,240,239,253,0,0,0,0,0,13,
  255,240,191,255,32,0,0,0,0,31,255,240,127,255,144,0,0,0,0,143,
  255,240,47,255,244,0,0,0,3,255,255,240,9,255,255,112,0,0,94,255,
  255,240,1,223,255,254,185,173,255,255,255,240,0,45,255,255,255,255,255,204,
  255,240,0,2,191,255,255,255,251,27,255,240,0,0,4,173,255,218,64,11,
  255,240,0,0,0,0,0,0,0,14,255,208,0,0,0,0,0,0,0,79,
  255,144,3,214,0,0,0,0,1,223,255,64,95,255,163,0,0,0,93,255,
  251,0,62,255,255,219,153,190,255,255,226,0,4,239,255,255,255,255,255,254,
  48,0,0,42,255,255,255,255,255,161,0,0,0,0,40,190,255,236,130,0,
  0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,
  255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,255,251,0,90,239,236,113,0,0,255,251,27,255,255,255,253,64,
  0,255,252,223,255,255,255,255,243,0,255,255,255,235,154,239,255,253,0,255,
  255,250,16,0,25,255,255,96,255,255,176,0,0,0,175,255,176,255,255,32,
  0,0,0,47,255,224,255,253,0,0,0,0,13,255,240,255,251,0,0,0,
  0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,
  255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,
  255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,
  0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,
  0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,
  11,255,240,44,252,32,207,255,192,255,255,240,207,255,192,44,252,32,0,0,
  0,0,0,0,0,0,0,0,0,0,191,255,0,191,255,0,191,255,0,191,
  255,0,191,255,0,191,255,0,191,255,0,191,255,0,191,255,0,191,255,0,
  191,255,0,191,255,0,191,255,0,191,255,0,191,255,0,191,255,0,191,255,
  0,191,255,0,191,255,0,191,255,0,0,0,0,0,44,252,32,0,0,0,
  0,207,255,192,0,0,0,0,255,255,240,0,0,0,0,207,255,192,0,0,
  0,0,44,252,32,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,191,255,0,
  0,0,0,0,191,255,0,0,0,0,0,191,255,0,0,0,0,0,191,255,
  0,0,0,0,0,191,255,0,0,0,0,0,191,255,0,0,0,0,0,191,
  255,0,0,0,0,0,191,255,0,0,0,0,0,191,255,0,0,0,0,0,
  191,255,0,0,0,0,0,191,255,0,0,0,0,0,191,255,0,0,0,0,
  0,191,255,0,0,0,0,0,191,255,0,0,0,0,0,191,255,0,0,0,
  0,0,191,255,0,0,0,0,0,191,255,0,0,0,0,0,191,255,0,0,
  0,0,0,191,255,0,0,0,0,0,191,255,0,0,0,0,0,191,255,0,
  0,0,0,0,191,255,0,0,0,0,0,223,254,0,0,163,0,7,255,251,
  0,11,255,185,207,255,246,0,127,255,255,255,255,192,0,10,255,255,255,252,
  16,0,0,91,239,235,96,0,0,255,251,0,0,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,4,255,254,48,255,
  251,0,0,0,62,255,244,0,255,251,0,0,2,239,255,80,0,255,251,0,
  0,29,255,246,0,0,255,251,0,1,207,255,112,0,0,255,251,0,11,255,
  248,0,0,0,255,251,0,175,255,160,0,0,0,255,251,8,255,251,0,0,
  0,0,255,251,127,255,193,0,0,0,0,255,254,255,255,16,0,0,0,0,
  255,252,223,255,144,0,0,0,0,255,251,62,255,247,0,0,0,0,255,251,
  4,255,255,80,0,0,0,255,251,0,95,255,244,0,0,0,255,251,0,8,
  255,254,32,0,0,255,251,0,0,159,255,210,0,0,255,251,0,0,11,255,
  252,16,0,255,251,0,0,1,207,255,176,0,255,251,0,0,0,46,255,249,
  0,255,251,0,0,0,3,239,255,112,255,251,255,251,255,251,255,251,255,251,
  255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,
  255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,
  255,251,255,251,255,251,255,251,255,251,0,107,239,235,96,0,0,41,223,236,
  130,0,0,255,251,61,255,255,255,253,48,7,255,255,255,254,96,0,255,253,
  239,255,255,255,255,226,143,255,255,255,255,245,0,255,255,255,234,155,239,255,
  253,255,253,169,223,255,254,16,255,255,248,16,0,26,255,255,255,112,0,6,
  255,255,128,255,255,144,0,0,0,207,255,248,0,0,0,111,255,192,255,255,
  16,0,0,0,79,255,225,0,0,0,14,255,224,255,252,0,0,0,0,31,
  255,192,0,0,0,12,255,240,255,251,0,0,0,0,15,255,176,0,0,0,
  11,255,240,255,251,0,0,0,0,15,255,176,0,0,0,11,255,240,255,251,
  0,0,0,0,15,255,176,0,0,0,11,255,240,255,251,0,0,0,0,15,
  255,176,0,0,0,11,255,240,255,251,0,0,0,0,15,255,176,0,0,0,
  11,255,240,255,251,0,0,0,0,15,255,176,0,0,0,11,255,240,255,251,
  0,0,0,0,15,255,176,0,0,0,11,255,240,255,251,0,0,0,0,15,
  255,176,0,0,0,11,255,240,255,251,0,0,0,0,15,255,176,0,0,0,
  11,255,240,255,251,0,0,0,0,15,255,176,0,0,0,11,255,240,255,251,
  0,0,0,0,15,255,176,0,0,0,11,255,240,255,251,0,0,0,0,15,
  255,176,0,0,0,11,255,240,255,251,0,90,239,235,96,0,0,255,251,27,
  255,255,255,252,32,0,255,252,223,255,255,255,255,209,0,255,255,255,235,154,
  239,255,251,0,255,255,250,16,0,25,255,255,64,255,255,176,0,0,0,175,
  255,160,255,255,32,0,0,0,47,255,208,255,253,0,0,0,0,13,255,240,
  255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,
  0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,
  0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,
  11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,
  240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,
  251,0,0,0,0,11,255,240,0,0,1,123,239,253,165,0,0,0,0,0,
  110,255,255,255,255,212,0,0,0,10,255,255,255,255,255,255,112,0,0,175,
  255,255,185,172,255,255,246,0,6,255,255,129,0,0,59,255,255,48,30,255,
  246,0,0,0,0,175,255,176,111,255,144,0,0,0,0,29,255,242,191,255,
  32,0,0,0,0,7,255,247,239,253,0,0,0,0,0,2,255,249,255,251,
  0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,1,255,251,223,253,
  0,0,0,0,0,3,255,249,175,255,48,0,0,0,0,7,255,246,111,255,
  160,0,0,0,0,29,255,242,30,255,246,0,0,0,0,175,255,160,6,255,
  255,145,0,0,59,255,254,32,0,175,255,255,185,172,255,255,246,0,0,10,
  255,255,255,255,255,255,96,0,0,0,110,255,255,255,255,212,0,0,0,0,
  1,123,239,253,165,0,0,0,255,240,1,107,239,236,130,0,0,0,255,240,
  77,255,255,255,255,145,0,0,255,245,255,255,255,255,255,252,16,0,255,255,
  255,252,169,191,255,255,192,0,255,255,250,32,0,1,143,255,248,0,255,255,
  144,0,0,0,6,255,255,16,255,253,0,0,0,0,0,175,255,112,255,246,
  0,0,0,0,0,47,255,176,255,242,0,0,0,0,0,13,255,224,255,240,
  0,0,0,0,0,11,255,240,255,240,0,0,0,0,0,11,255,240,255,242,
  0,0,0,0,0,13,255,224,255,246,0,0,0,0,0,63,255,176,255,253,
  0,0,0,0,0,175,255,112,255,255,144,0,0,0,6,255,255,32,255,255,
  250,32,0,1,159,255,248,0,255,255,255,252,169,191,255,255,193,0,255,246,
  255,255,255,255,255,253,32,0,255,240,94,255,255,255,255,145,0,0,255,240,
  1,107,239,236,147,0,0,0,255,240,0,0,0,0,0,0,0,0,255,240,
  0,0,0,0,0,0,0,0,255,240,0,0,0,0,0,0,0,0,255,240,
  0,0,0,0,0,0,0,0,255,240,0,0,0,0,0,0,0,0,255,240,
  0,0,0,0,0,0,0,0,255,240,0,0,0,0,0,0,0,0,255,240,
  0,0,0,0,0,0,0,0,0,0,2,140,239,235,97,2,255,248,0,1,
  159,255,255,255,253,66,255,248,0,28,255,255,255,255,255,246,255,248,0,207,
  255,255,185,172,255,255,255,248,8,255,255,129,0,0,43,255,255,248,31,255,
  246,0,0,0,0,175,255,248,127,255,160,0,0,0,0,30,255,248,191,255,
  32,0,0,0,0,8,255,248,239,253,0,0,0,0,0,4,255,248,255,251,
  0,0,0,0,0,3,255,248,255,251,0,0,0,0,0,3,255,248,239,253,
  0,0,0,0,0,4,255,248,191,255,32,0,0,0,0,8,255,248,127,255,
  160,0,0,0,0,30,255,248,47,255,246,0,0,0,0,191,255,248,8,255,
  255,145,0,0,43,255,255,248,1,207,255,255,185,172,255,255,255,248,0,45,
  255,255,255,255,255,246,255,248,0,1,159,255,255,255,253,66,255,248,0,0,
  3,156,239,235,97,2,255,248,0,0,0,0,0,0,0,2,255,248,0,0,
  0,0,0,0,0,2,255,248,0,0,0,0,0,0,0,2,255,248,0,0,
  0,0,0,0,0,2,255,248,0,0,0,0,0,0,0,2,255,248,0,0,
  0,0,0,0,0,2,255,248,0,0,0,0,0,0,0,2,255,248,0,0,
  0,0,0,0,0,2,255,248,255,251,2,157,255,199,16,255,251,95,255,255,
  255,210,255,253,255,255,255,255,244,255,255,255,234,155,255,112,255,255,247,0,
  0,55,0,255,255,128,0,0,0,0,255,255,16,0,0,0,0,255,252,0,
  0,0,0,0,255,251,0,0,0,0,0,255,251,0,0,0,0,0,255,251,
  0,0,0,0,0,255,251,0,0,0,0,0,255,251,0,0,0,0,0,255,
  251,0,0,0,0,0,255,251,0,0,0,0,0,255,251,0,0,0,0,0,
  255,251,0,0,0,0,0,255,251,0,0,0,0,0,255,251,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,23,190,255,218,64,0,0,3,223,255,
  255,255,251,32,0,46,255,255,255,255,255,210,0,159,255,251,154,223,255,247,
  0,223,255,48,0,4,223,128,0,255,251,0,0,0,22,0,0,239,254,16,
  0,0,0,0,0,175,255,213,0,0,0,0,0,46,255,255,234,81,0,0,
  0,3,223,255,255,254,146,0,0,0,5,191,255,255,254,64,0,0,0,1,
  107,255,255,225,0,0,0,0,0,44,255,247,0,0,0,0,0,2,255,250,
  0,75,16,0,0,1,255,251,5,255,212,0,0,8,255,249,12,255,255,218,
  154,223,255,244,1,207,255,255,255,255,255,176,0,24,255,255,255,255,250,16,
  0,0,39,206,255,217,64,0,0,0,15,255,176,0,0,0,0,15,255,176,
  0,0,0,0,15,255,176,0,0,0,0,15,255,176,0,0,0,0,15,255,
  176,0,0,0,0,15,255,176,0,0,0,0,15,255,176,0,0,0,0,15,
  255,176,0,0,143,255,255,255,255,255,248,143,255,255,255,255,255,248,143,255,
  255,255,255,255,248,73,153,159,255,217,153,148,0,0,15,255,176,0,0,0,
  0,15,255,176,0,0,0,0,15,255,176,0,0,0,0,15,255,176,0,0,
  0,0,15,255,176,0,0,0,0,15,255,176,0,0,0,0,15,255,176,0,
  0,0,0,15,255,176,0,0,0,0,15,255,176,0,0,0,0,15,255,176,
  0,0,0,0,15,255,176,0,0,0,0,15,255,176,0,0,0,0,15,255,
  176,0,0,0,0,15,255,176,0,0,0,0,15,255,176,0,0,0,0,15,
  255,176,0,0,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,
  255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,
  0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,
  255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,
  0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,
  239,252,0,0,0,0,207,254,207,255,16,0,0,1,255,252,143,255,128,0,
  0,8,255,248,63,255,246,0,0,111,255,243,9,255,255,218,157,255,255,144,
  1,207,255,255,255,255,252,16,0,26,255,255,255,255,161,0,0,0,73,223,
  253,164,0,0,95,255,160,0,0,0,0,1,255,252,13,255,241,0,0,0,
  0,7,255,245,7,255,247,0,0,0,0,13,255,208,1,239,253,0,0,0,
  0,95,255,112,0,159,255,80,0,0,0,191,255,16,0,63,255,176,0,0,
  2,255,249,0,0,11,255,242,0,0,8,255,243,0,0,4,255,248,0,0,
  30,255,176,0,0,0,223,254,0,0,111,255,64,0,0,0,111,255,96,0,
  207,253,0,0,0,0,30,255,192,3,255,246,0,0,0,0,8,255,243,9,
  255,225,0,0,0,0,2,255,249,31,255,128,0,0,0,0,0,175,254,143,
  255,32,0,0,0,0,0,79,255,255,250,0,0,0,0,0,0,12,255,255,
  244,0,0,0,0,0,0,6,255,255,192,0,0,0,0,0,0,0,239,255,
  96,0,0,0,0,0,0,0,127,254,0,0,0,0,0,0,0,0,31,247,
  0,0,0,0,79,255,112,0,0,0,3,255,48,0,0,0,7,255,244,13,
  255,208,0,0,0,9,255,144,0,0,0,13,255,208,8,255,243,0,0,0,
  14,255,224,0,0,0,63,255,128,2,255,248,0,0,0,79,255,244,0,0,
  0,143,255,32,0,191,253,0,0,0,159,255,249,0,0,0,223,251,0,0,
  111,255,64,0,0,239,255,254,0,0,4,255,246,0,0,30,255,144,0,4,
  255,255,255,64,0,9,255,225,0,0,9,255,224,0,10,255,186,255,160,0,
  14,255,144,0,0,4,255,244,0,30,255,85,255,225,0,79,255,64,0,0,
  0,223,250,0,95,254,16,239,245,0,175,253,0,0,0,0,143,254,16,175,
  250,0,159,250,1,239,248,0,0,0,0,47,255,81,255,244,0,79,255,21,
  255,242,0,0,0,0,11,255,166,255,208,0,13,255,106,255,192,0,0,0,
  0,5,255,252,255,128,0,8,255,207,255,96,0,0,0,0,1,239,255,255,
  48,0,2,255,255,254,16,0,0,0,0,0,159,255,252,0,0,0,207,255,
  250,0,0,0,0,0,0,79,255,247,0,0,0,111,255,244,0,0,0,0,
  0,0,13,255,241,0,0,0,31,255,208,0,0,0,0,0,0,7,255,176,
  0,0,0,11,255,128,0,0,0,0,0,0,2,255,80,0,0,0,5,255,
  32,0,0,0,9,255,251,0,0,0,0,111,255,192,1,223,255,112,0,0,
  2,239,254,32,0,63,255,243,0,0,11,255,245,0,0,7,255,252,0,0,
  111,255,160,0,0,0,207,255,128,2,239,253,16,0,0,0,46,255,243,11,
  255,244,0,0,0,0,5,255,253,127,255,128,0,0,0,0,0,175,255,255,
  252,0,0,0,0,0,0,29,255,255,243,0,0,0,0,0,0,4,255,255,
  128,0,0,0,0,0,0,9,255,255,209,0,0,0,0,0,0,95,255,255,
  250,0,0,0,0,0,2,239,254,223,255,80,0,0,0,0,11,255,245,79,
  255,225,0,0,0,0,127,255,160,9,255,251,0,0,0,3,255,253,16,1,
  223,255,96,0,0,12,255,244,0,0,79,255,226,0,0,143,255,144,0,0,
  9,255,251,0,4,255,253,16,0,0,1,223,255,112,29,255,244,0,0,0,
  0,63,255,243,47,255,208,0,0,0,0,4,255,250,11,255,243,0,0,0,
  0,10,255,244,5,255,249,0,0,0,0,47,255,192,0,239,254,16,0,0,
  0,143,255,96,0,143,255,96,0,0,0,239,254,0,0,47,255,192,0,0,
  5,255,247,0,0,10,255,243,0,0,11,255,225,0,0,4,255,249,0,0,
  47,255,144,0,0,0,223,254,0,0,159,255,32,0,0,0,127,255,80,1,
  239,251,0,0,0,0,31,255,176,6,255,244,0,0,0,0,9,255,242,12,
  255,192,0,0,0,0,3,255,248,63,255,96,0,0,0,0,0,207,254,175,
  254,0,0,0,0,0,0,111,255,255,247,0,0,0,0,0,0,30,255,255,
  241,0,0,0,0,0,0,9,255,255,144,0,0,0,0,0,0,2,255,255,
  32,0,0,0,0,0,0,1,255,251,0,0,0,0,0,0,0,8,255,244,
  0,0,0,0,0,0,0,30,255,192,0,0,0,0,0,0,0,127,255,96,
  0,0,0,0,0,0,1,239,253,0,0,0,0,0,0,0,7,255,247,0,
  0,0,0,0,0,0,13,255,225,0,0,0,0,0,0,0,111,255,128,0,
  0,0,0,0,0,0,223,255,32,0,0,0,0,0,0,5,255,250,0,0,
  0,0,0,0,15,255,255,255,255,255,255,255,15,255,255,255,255,255,255,255,
  15,255,255,255,255,255,255,251,9,153,153,153,153,255,255,225,0,0,0,0,
  8,255,255,64,0,0,0,0,63,255,248,0,0,0,0,1,223,255,192,0,
  0,0,0,10,255,254,32,0,0,0,0,95,255,245,0,0,0,0,2,239,
  255,144,0,0,0,0,11,255,252,0,0,0,0,0,127,255,226,0,0,0,
  0,3,255,255,80,0,0,0,0,29,255,249,0,0,0,0,0,159,255,209,
  0,0,0,0,5,255,255,48,0,0,0,0,46,255,253,153,153,153,153,144,
  191,255,255,255,255,255,255,240,255,255,255,255,255,255,255,240,255,255,255,255,
  255,255,255,240,0,0,23,206,255,128,0,2,223,255,255,128,0,11,255,255,
  238,112,0,63,255,113,0,0,0,111,250,0,0,0,0,143,247,0,0,0,
  0,143,247,0,0,0,0,111,248,0,0,0,0,95,249,0,0,0,0,63,
  251,0,0,0,0,47,252,0,0,0,0,31,253,0,0,0,0,15,254,0,
  0,0,0,31,253,0,0,0,1,143,249,0,0,0,255,255,210,0,0,0,
  255,255,64,0,0,0,221,255,227,0,0,0,0,111,250,0,0,0,0,15,
  253,0,0,0,0,15,254,0,0,0,0,31,253,0,0,0,0,47,252,0,
  0,0,0,79,251,0,0,0,0,95,249,0,0,0,0,111,248,0,0,0,
  0,143,247,0,0,0,0,143,247,0,0,0,0,111,251,0,0,0,0,63,
  255,129,0,0,0,11,255,255,255,112,0,1,223,255,255,128,0,0,23,206,
  255,128,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,
  255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,
  255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,
  255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,253,163,0,
  0,0,255,255,255,112,0,0,238,255,255,244,0,0,0,5,239,250,0,0,
  0,0,127,254,0,0,0,0,79,255,0,0,0,0,79,255,0,0,0,0,
  95,254,0,0,0,0,111,252,0,0,0,0,143,251,0,0,0,0,159,250,
  0,0,0,0,175,248,0,0,0,0,191,247,0,0,0,0,159,248,0,0,
  0,0,111,254,48,0,0,0,12,255,255,128,0,0,2,239,255,128,0,0,
  29,255,237,96,0,0,127,252,16,0,0,0,175,247,0,0,0,0,191,247,
  0,0,0,0,175,248,0,0,0,0,159,250,0,0,0,0,127,251,0,0,
  0,0,111,253,0,0,0,0,95,254,0,0,0,0,79,255,0,0,0,0,
  79,255,0,0,0,0,143,254,0,0,0,21,239,250,0,0,255,255,255,244,
  0,0,255,255,255,112,0,0,255,253,163,0,0,0,0,4,137,133,0,0,
  0,1,0,2,207,255,255,214,0,0,110,80,46,255,255,255,255,234,156,255,
  244,175,255,255,255,255,255,255,255,226,29,249,32,22,223,255,255,254,48,2,
  96,0,0,5,190,253,129,0,0,0,0,0,0,23,0,0,0,0,0,0,
  0,0,0,191,128,0,0,0,0,0,0,0,8,255,247,0,0,0,0,0,
  0,0,79,255,248,0,0,0,0,0,0,2,239,255,96,0,0,0,0,0,
  0,12,255,228,0,0,0,0,0,0,0,143,254,48,0,0,0,0,0,0,
  0,62,194,0,0,0,0,0,0,0,0,3,16,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  3,140,239,235,96,11,255,240,0,1,159,255,255,255,253,59,255,240,0,28,
  255,255,255,255,255,253,255,240,0,207,255,255,185,172,255,255,255,240,8,255,
  255,129,0,0,42,255,255,240,30,255,246,0,0,0,0,143,255,240,127,255,
  160,0,0,0,0,13,255,240,191,255,32,0,0,0,0,7,255,240,239,253,
  0,0,0,0,0,4,255,240,255,251,0,0,0,0,0,2,255,240,255,251,
  0,0,0,0,0,3,255,240,239,253,0,0,0,0,0,5,255,240,191,255,
  32,0,0,0,0,8,255,240,127,255,160,0,0,0,0,13,255,240,31,255,
  246,0,0,0,0,159,255,240,8,255,255,145,0,0,42,255,255,240,1,207,
  255,255,185,156,255,255,255,240,0,44,255,255,255,255,255,253,255,240,0,1,
  159,255,255,255,253,75,255,240,0,0,3,156,239,235,97,11,255,240,0,0,
  0,0,0,84,0,0,0,0,0,0,0,0,2,239,64,0,0,0,0,0,
  0,0,12,255,227,0,0,0,0,0,0,0,159,255,229,0,0,0,0,0,
  0,6,255,254,48,0,0,0,0,0,0,63,255,194,0,0,0,0,0,0,
  0,223,251,16,0,0,0,0,0,0,0,127,144,0,0,0,0,0,0,0,
  0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,1,107,239,253,165,0,0,0,0,0,
  110,255,255,255,255,195,0,0,0,10,255,255,255,255,255,254,64,0,0,175,
  255,254,185,172,255,255,226,0,6,255,255,113,0,0,60,255,251,0,30,255,
  244,0,0,0,1,223,255,48,111,255,128,0,0,0,0,95,255,144,191,255,
  16,0,0,0,0,14,255,192,239,255,255,255,255,255,255,255,255,224,255,255,
  255,255,255,255,255,255,255,240,255,255,255,255,255,255,255,255,255,224,223,255,
  153,153,153,153,153,153,153,128,191,255,32,0,0,0,0,0,0,0,111,255,
  144,0,0,0,0,0,0,0,30,255,246,0,0,0,0,38,0,0,6,255,
  255,145,0,0,6,239,144,0,0,175,255,255,201,155,239,255,247,0,0,10,
  255,255,255,255,255,255,177,0,0,0,110,255,255,255,255,232,0,0,0,0,
  1,106,223,254,183,16,0,0,0,0,0,16,0,0,0,8,177,0,0,0,
  95,251,16,0,3,239,255,160,0,29,255,252,32,0,191,255,161,0,8,255,
  248,0,0,30,255,80,0,0,3,195,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,15,255,176,0,0,15,255,176,0,0,15,
  255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,
  255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,
  255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,
  255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,
  255,176,0,0,15,255,176,0,0,0,0,0,0,53,0,0,0,0,0,0,
  0,0,1,223,80,0,0,0,0,0,0,0,11,255,245,0,0,0,0,0,
  0,0,143,255,246,0,0,0,0,0,0,5,255,254,64,0,0,0,0,0,
  0,46,255,210,0,0,0,0,0,0,0,223,252,16,0,0,0,0,0,0,
  0,127,160,0,0,0,0,0,0,0,0,4,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  1,123,239,253,165,0,0,0,0,0,110,255,255,255,255,212,0,0,0,10,
  255,255,255,255,255,255,112,0,0,175,255,255,185,172,255,255,246,0,6,255,
  255,129,0,0,59,255,255,48,30,255,246,0,0,0,0,175,255,176,111,255,
  144,0,0,0,0,29,255,242,191,255,32,0,0,0,0,7,255,247,239,253,
  0,0,0,0,0,2,255,249,255,251,0,0,0,0,0,0,255,251,255,251,
  0,0,0,0,0,1,255,251,223,253,0,0,0,0,0,3,255,249,175,255,
  48,0,0,0,0,7,255,246,111,255,160,0,0,0,0,29,255,242,30,255,
  246,0,0,0,0,175,255,160,6,255,255,145,0,0,59,255,254,32,0,175,
  255,255,185,172,255,255,246,0,0,10,255,255,255,255,255,255,96,0,0,0,
  110,255,255,255,255,212,0,0,0,0,1,123,239,253,165,0,0,0,0,0,
  0,0,1,0,0,0,0,0,0,0,108,16,0,0,0,0,0,3,255,176,
  0,0,0,0,0,29,255,249,0,0,0,0,0,159,255,193,0,0,0,0,
  5,255,251,16,0,0,0,0,46,255,160,0,0,0,0,0,143,248,0,0,
  0,0,0,0,11,112,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,
  191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,
  0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,
  191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,
  0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,
  191,255,255,251,0,0,0,0,191,255,239,252,0,0,0,0,207,254,207,255,
  16,0,0,1,255,252,143,255,128,0,0,8,255,248,63,255,246,0,0,111,
  255,243,9,255,255,218,157,255,255,144,1,207,255,255,255,255,252,16,0,26,
  255,255,255,255,161,0,0,0,73,223,253,164,0,0,0,44,252,32,0,44,
  252,32,0,207,255,192,0,207,255,192,0,255,255,240,0,255,255,240,0,207,
  255,192,0,207,255,192,0,44,252,32,0,44,252,32,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,251,
  0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,
  191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,
  0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,
  191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,
  0,0,0,0,191,255,255,251,0,0,0,0,191,255,239,252,0,0,0,0,
  207,254,207,255,16,0,0,1,255,252,143,255,128,0,0,8,255,248,63,255,
  246,0,0,111,255,243,9,255,255,218,157,255,255,144,1,207,255,255,255,255,
  252,16,0,26,255,255,255,255,161,0,0,0,73,223,253,164,0,0,0,0,
  1,16,0,0,0,0,0,0,42,255,251,48,0,5,32,0,3,239,255,255,
  250,68,143,210,0,13,255,255,255,255,255,255,248,0,4,237,66,57,255,255,
  255,144,0,0,49,0,0,58,239,198,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,
  251,0,90,239,235,96,0,0,255,251,27,255,255,255,252,32,0,255,252,223,
  255,255,255,255,209,0,255,255,255,235,154,239,255,251,0,255,255,250,16,0,
  25,255,255,64,255,255,176,0,0,0,175,255,160,255,255,32,0,0,0,47,
  255,208,255,253,0,0,0,0,13,255,240,255,251,0,0,0,0,11,255,240,
  255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,
  0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,
  0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,
  11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,
  240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,0,
  0,0,0,0,0,1,214,0,0,0,0,0,0,0,0,0,0,0,0,11,
  255,96,0,0,0,0,0,0,0,0,0,0,0,143,255,245,0,0,0,0,
  0,0,0,0,0,0,5,255,255,144,0,0,0,0,0,0,0,0,0,0,
  62,255,246,0,0,0,0,0,0,0,0,0,0,1,223,254,64,0,0,0,
  0,0,0,0,0,0,0,7,255,194,0,0,0,0,0,0,0,0,0,0,
  0,0,154,16,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,255,
  242,0,0,0,0,0,0,0,0,0,0,0,7,255,248,0,0,0,0,0,
  0,0,0,0,0,0,13,255,253,0,0,0,0,0,0,0,0,0,0,0,
  79,255,255,80,0,0,0,0,0,0,0,0,0,0,175,255,255,176,0,0,
  0,0,0,0,0,0,0,1,255,255,255,242,0,0,0,0,0,0,0,0,
  0,7,255,252,255,247,0,0,0,0,0,0,0,0,0,13,255,226,255,253,
  0,0,0,0,0,0,0,0,0,79,255,160,175,255,64,0,0,0,0,0,
  0,0,0,175,255,64,79,255,160,0,0,0,0,0,0,0,1,255,253,0,
  13,255,241,0,0,0,0,0,0,0,7,255,247,0,7,255,247,0,0,0,
  0,0,0,0,13,255,241,0,2,255,253,0,0,0,0,0,0,0,79,255,
  160,0,0,191,255,64,0,0,0,0,0,0,175,255,64,0,0,95,255,160,
  0,0,0,0,0,1,255,253,0,0,0,14,255,241,0,0,0,0,0,7,
  255,247,0,0,0,8,255,247,0,0,0,0,0,13,255,242,0,0,0,2,
  255,253,0,0,0,0,0,63,255,176,0,0,0,0,191,255,64,0,0,0,
  0,159,255,255,255,255,255,255,255,255,160,0,0,0,1,239,255,255,255,255,
  255,255,255,255,241,0,0,0,6,255,255,255,255,255,255,255,255,255,247,0,
  0,0,12,255,249,153,153,153,153,153,154,255,252,0,0,0,63,255,176,0,
  0,0,0,0,0,207,255,48,0,0,159,255,80,0,0,0,0,0,0,111,
  255,144,0,1,239,254,0,0,0,0,0,0,0,31,255,225,0,6,255,249,
  0,0,0,0,0,0,0,10,255,246,0,12,255,243,0,0,0,0,0,0,
  0,4,255,252,0,63,255,192,0,0,0,0,0,0,0,0,223,255,48,0,
  0,0,0,3,227,0,0,0,0,0,0,0,0,29,254,48,0,0,0,0,
  0,0,0,191,255,225,0,0,0,0,0,0,7,255,254,80,0,0,0,0,
  0,0,79,255,211,0,0,0,0,0,0,2,239,252,32,0,0,0,0,0,
  0,8,255,161,0,0,0,0,0,0,0,0,168,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,255,255,255,255,255,255,255,255,255,128,255,
  255,255,255,255,255,255,255,255,128,255,255,255,255,255,255,255,255,255,128,255,
  253,153,153,153,153,153,153,153,64,255,251,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,255,253,153,153,153,153,153,153,144,0,255,
  255,255,255,255,255,255,255,240,0,255,255,255,255,255,255,255,255,240,0,255,
  255,255,255,255,255,255,255,240,0,255,251,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,
  253,153,153,153,153,153,153,153,64,255,255,255,255,255,255,255,255,255,128,255,
  255,255,255,255,255,255,255,255,128,255,255,255,255,255,255,255,255,255,128,0,
  0,8,177,0,0,0,95,251,0,0,2,239,255,144,0,28,255,252,32,0,
  175,255,161,0,6,255,248,0,0,13,255,96,0,0,2,212,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  0,0,0,0,0,0,39,0,0,0,0,0,0,0,0,0,0,0,0,1,
  223,128,0,0,0,0,0,0,0,0,0,0,0,10,255,247,0,0,0,0,
  0,0,0,0,0,0,0,127,255,248,0,0,0,0,0,0,0,0,0,0,
  4,255,255,96,0,0,0,0,0,0,0,0,0,0,46,255,228,0,0,0,
  0,0,0,0,0,0,0,0,207,252,32,0,0,0,0,0,0,0,0,0,
  0,0,111,161,0,0,0,0,0,0,0,0,0,0,0,0,4,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,156,239,
  254,200,64,0,0,0,0,0,0,0,7,223,255,255,255,255,253,96,0,0,
  0,0,0,3,207,255,255,255,255,255,255,252,32,0,0,0,0,78,255,255,
  253,169,155,223,255,255,228,0,0,0,4,255,255,232,32,0,0,3,159,255,
  255,64,0,0,46,255,252,32,0,0,0,0,2,207,255,226,0,0,191,255,
  176,0,0,0,0,0,0,28,255,251,0,5,255,253,16,0,0,0,0,0,
  0,1,223,255,80,12,255,243,0,0,0,0,0,0,0,0,79,255,192,63,
  255,176,0,0,0,0,0,0,0,0,11,255,243,143,255,80,0,0,0,0,
  0,0,0,0,5,255,247,191,255,16,0,0,0,0,0,0,0,0,1,255,
  251,223,253,0,0,0,0,0,0,0,0,0,0,223,253,255,251,0,0,0,
  0,0,0,0,0,0,0,207,254,255,251,0,0,0,0,0,0,0,0,0,
  0,191,255,239,252,0,0,0,0,0,0,0,0,0,0,191,254,223,253,0,
  0,0,0,0,0,0,0,0,0,223,253,191,255,16,0,0,0,0,0,0,
  0,0,1,255,251,127,255,80,0,0,0,0,0,0,0,0,5,255,247,47,
  255,176,0,0,0,0,0,0,0,0,11,255,243,11,255,244,0,0,0,0,
  0,0,0,0,79,255,192,4,255,253,16,0,0,0,0,0,0,1,223,255,
  64,0,191,255,177,0,0,0,0,0,0,28,255,251,0,0,30,255,252,32,
  0,0,0,0,2,207,255,226,0,0,3,239,255,249,32,0,0,2,159,255,
  254,48,0,0,0,62,255,255,253,169,154,223,255,255,228,0,0,0,0,2,
  207,255,255,255,255,255,255,252,32,0,0,0,0,0,6,223,255,255,255,255,
  253,96,0,0,0,0,0,0,0,4,140,239,254,201,64,0,0,0,0,0,
  0,0,0,0,8,193,0,0,0,0,0,0,0,0,0,95,252,16,0,0,
  0,0,0,0,0,3,239,255,176,0,0,0,0,0,0,0,29,255,253,32,
  0,0,0,0,0,0,0,191,255,177,0,0,0,0,0,0,0,8,255,248,
  0,0,0,0,0,0,0,0,30,255,96,0,0,0,0,0,0,0,0,3,
  211,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,
  255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,
  0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,
  0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,
  0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,
  251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,
  251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,
  0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,
  0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,
  0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,
  0,0,0,0,0,0,0,255,251,239,253,0,0,0,0,0,0,2,255,250,
  191,255,16,0,0,0,0,0,5,255,247,127,255,112,0,0,0,0,0,11,
  255,243,47,255,226,0,0,0,0,0,79,255,208,10,255,252,16,0,0,0,
  3,239,255,96,2,239,255,230,0,0,1,127,255,251,0,0,79,255,255,235,
  154,191,255,255,210,0,0,4,239,255,255,255,255,255,252,32,0,0,0,42,
  255,255,255,255,255,129,0,0,0,0,0,56,206,255,219,113,0,0,0,0,
  0,44,252,32,0,44,252,32,0,0,0,0,207,255,192,0,207,255,192,0,
  0,0,0,255,255,240,0,255,255,240,0,0,0,0,207,255,192,0,207,255,
  192,0,0,0,0,44,252,32,0,44,252,32,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,255,251,
  0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,
  255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,
  255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,
  0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,
  0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,
  0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,
  251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,
  251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,
  0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,
  0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,239,253,0,0,
  0,0,0,0,2,255,250,191,255,16,0,0,0,0,0,5,255,247,127,255,
  112,0,0,0,0,0,11,255,243,47,255,226,0,0,0,0,0,79,255,208,
  10,255,252,16,0,0,0,3,239,255,96,2,239,255,230,0,0,1,127,255,
  251,0,0,79,255,255,235,154,191,255,255,210,0,0,4,239,255,255,255,255,
  255,252,32,0,0,0,42,255,255,255,255,255,129,0,0,0,0,0,56,206,
  255,219,113,0,0,0,0,0,0,1,16,0,0,0,0,0,0,0,0,5,
  223,254,129,0,0,112,0,0,0,0,143,255,255,254,115,75,250,0,0,0,
  5,255,255,255,255,255,255,254,32,0,0,0,175,131,38,223,255,255,228,0,
  0,0,0,4,0,0,7,223,234,32,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,255,225,0,0,0,0,0,0,0,255,251,255,251,0,0,
  0,0,0,0,0,255,251,255,255,96,0,0,0,0,0,0,255,251,255,255,
  243,0,0,0,0,0,0,255,251,255,255,253,16,0,0,0,0,0,255,251,
  255,255,255,144,0,0,0,0,0,255,251,255,255,255,245,0,0,0,0,0,
  255,251,255,253,255,254,32,0,0,0,0,255,251,255,251,127,255,192,0,0,
  0,0,255,251,255,251,11,255,248,0,0,0,0,255,251,255,251,1,239,255,
  64,0,0,0,255,251,255,251,0,79,255,225,0,0,0,255,251,255,251,0,
  8,255,251,0,0,0,255,251,255,251,0,0,207,255,112,0,0,255,251,255,
  251,0,0,46,255,243,0,0,255,251,255,251,0,0,5,255,253,16,0,255,
  251,255,251,0,0,0,175,255,160,0,255,251,255,251,0,0,0,29,255,245,
  0,255,251,255,251,0,0,0,3,255,254,32,255,251,255,251,0,0,0,0,
  127,255,192,255,251,255,251,0,0,0,0,11,255,248,255,251,255,251,0,0,
  0,0,1,239,255,255,251,255,251,0,0,0,0,0,79,255,255,251,255,251,
  0,0,0,0,0,8,255,255,251,255,251,0,0,0,0,0,0,207,255,251,
  255,251,0,0,0,0,0,0,46,255,251,255,251,0,0,0,0,0,0,5,
  255,251,255,251,0,0,0,0,0,0,0,175,251,255,251,0,0,0,0,0,
  0,0,31,251,0,0,0,1,48,0,0,0,0,0,0,0,0,11,209,0,
  0,0,0,0,0,0,0,191,251,0,0,0,0,0,0,0,4,255,255,112,
  0,0,0,0,0,0,0,94,255,244,0,0,0,0,0,0,0,3,239,254,
  32,0,0,0,0,0,0,0,45,255,176,0,0,0,0,0,0,0,1,191,
  193,0,0,0,0,0,0,0,0,8,16,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,140,239,235,
  96,11,255,240,0,1,159,255,255,255,253,59,255,240,0,28,255,255,255,255,
  255,253,255,240,0,207,255,255,185,172,255,255,255,240,8,255,255,129,0,0,
  42,255,255,240,30,255,246,0,0,0,0,143,255,240,127,255,160,0,0,0,
  0,13,255,240,191,255,32,0,0,0,0,7,255,240,239,253,0,0,0,0,
  0,4,255,240,255,251,0,0,0,0,0,2,255,240,255,251,0,0,0,0,
  0,3,255,240,239,253,0,0,0,0,0,5,255,240,191,255,32,0,0,0,
  0,8,255,240,127,255,160,0,0,0,0,13,255,240,31,255,246,0,0,0,
  0,159,255,240,8,255,255,145,0,0,42,255,255,240,1,207,255,255,185,156,
  255,255,255,240,0,44,255,255,255,255,255,253,255,240,0,1,159,255,255,255,
  253,75,255,240,0,0,3,156,239,235,97,11,255,240,0,0,0,2,16,0,
  0,0,0,0,0,0,0,62,160,0,0,0,0,0,0,0,2,239,247,0,
  0,0,0,0,0,0,9,255,255,48,0,0,0,0,0,0,0,159,255,209,
  0,0,0,0,0,0,0,7,255,251,0,0,0,0,0,0,0,0,94,255,
  128,0,0,0,0,0,0,0,3,239,144,0,0,0,0,0,0,0,0,40,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,1,107,239,253,165,0,0,0,0,0,110,255,255,255,
  255,195,0,0,0,10,255,255,255,255,255,254,64,0,0,175,255,254,185,172,
  255,255,226,0,6,255,255,113,0,0,60,255,251,0,30,255,244,0,0,0,
  1,223,255,48,111,255,128,0,0,0,0,95,255,144,191,255,16,0,0,0,
  0,14,255,192,239,255,255,255,255,255,255,255,255,224,255,255,255,255,255,255,
  255,255,255,240,255,255,255,255,255,255,255,255,255,224,223,255,153,153,153,153,
  153,153,153,128,191,255,32,0,0,0,0,0,0,0,111,255,144,0,0,0,
  0,0,0,0,30,255,246,0,0,0,0,38,0,0,6,255,255,145,0,0,
  6,239,144,0,0,175,255,255,201,155,239,255,247,0,0,10,255,255,255,255,
  255,255,177,0,0,0,110,255,255,255,255,232,0,0,0,0,1,106,223,254,
  183,16,0,0,0,131,0,0,0,8,253,16,0,0,143,255,192,0,0,111,
  255,249,0,0,3,239,255,96,0,0,44,255,244,0,0,1,175,254,0,0,
  0,8,246,0,0,0,0,32,0,0,0,0,0,0,0,0,0,0,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  0,0,2,16,0,0,0,0,0,0,0,0,46,160,0,0,0,0,0,0,
  0,2,223,247,0,0,0,0,0,0,0,9,255,255,64,0,0,0,0,0,
  0,0,159,255,226,0,0,0,0,0,0,0,6,255,252,0,0,0,0,0,
  0,0,0,78,255,144,0,0,0,0,0,0,0,3,223,160,0,0,0,0,
  0,0,0,0,40,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,1,123,239,253,165,0,0,0,0,
  0,110,255,255,255,255,212,0,0,0,10,255,255,255,255,255,255,112,0,0,
  175,255,255,185,172,255,255,246,0,6,255,255,129,0,0,59,255,255,48,30,
  255,246,0,0,0,0,175,255,176,111,255,144,0,0,0,0,29,255,242,191,
  255,32,0,0,0,0,7,255,247,239,253,0,0,0,0,0,2,255,249,255,
  251,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,1,255,251,223,
  253,0,0,0,0,0,3,255,249,175,255,48,0,0,0,0,7,255,246,111,
  255,160,0,0,0,0,29,255,242,30,255,246,0,0,0,0,175,255,160,6,
  255,255,145,0,0,59,255,254,32,0,175,255,255,185,172,255,255,246,0,0,
  10,255,255,255,255,255,255,96,0,0,0,110,255,255,255,255,212,0,0,0,
  0,1,123,239,253,165,0,0,0,0,0,4,112,0,0,0,0,0,0,62,
  244,0,0,0,0,0,3,239,253,16,0,0,0,0,2,207,255,176,0,0,
  0,0,0,27,255,247,0,0,0,0,0,0,175,255,48,0,0,0,0,0,
  8,255,208,0,0,0,0,0,0,127,80,0,0,0,0,0,0,2,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,251,0,
  0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,
  255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,
  0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,
  255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,
  0,0,0,191,255,255,251,0,0,0,0,191,255,239,252,0,0,0,0,207,
  254,207,255,16,0,0,1,255,252,143,255,128,0,0,8,255,248,63,255,246,
  0,0,111,255,243,9,255,255,218,157,255,255,144,1,207,255,255,255,255,252,
  16,0,26,255,255,255,255,161,0,0,0,73,223,253,164,0,0,0,0,0,
  0,175,253,16,0,0,0,0,0,0,7,255,255,176,0,0,0,0,0,0,
  79,255,255,249,0,0,0,0,0,2,239,254,223,255,112,0,0,0,0,29,
  255,227,28,255,245,0,0,0,0,175,253,32,1,207,255,64,0,0,5,255,
  194,0,0,27,255,208,0,0,0,172,16,0,0,0,174,48,0,0,0,1,
  0,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,3,140,239,235,96,11,255,240,0,1,159,
  255,255,255,253,59,255,240,0,28,255,255,255,255,255,253,255,240,0,207,255,
  255,185,172,255,255,255,240,8,255,255,129,0,0,42,255,255,240,30,255,246,
  0,0,0,0,143,255,240,127,255,160,0,0,0,0,13,255,240,191,255,32,
  0,0,0,0,7,255,240,239,253,0,0,0,0,0,4,255,240,255,251,0,
  0,0,0,0,2,255,240,255,251,0,0,0,0,0,3,255,240,239,253,0,
  0,0,0,0,5,255,240,191,255,32,0,0,0,0,8,255,240,127,255,160,
  0,0,0,0,13,255,240,31,255,246,0,0,0,0,159,255,240,8,255,255,
  145,0,0,42,255,255,240,1,207,255,255,185,156,255,255,255,240,0,44,255,
  255,255,255,255,253,255,240,0,1,159,255,255,255,253,75,255,240,0,0,3,
  156,239,235,97,11,255,240,0,0,0,1,223,249,0,0,0,0,0,0,0,
  12,255,255,112,0,0,0,0,0,0,159,255,255,244,0,0,0,0,0,6,
  255,253,255,254,32,0,0,0,0,79,255,177,78,255,209,0,0,0,2,239,
  250,0,3,239,251,0,0,0,11,255,144,0,0,45,255,112,0,0,2,215,
  0,0,0,2,219,16,0,0,0,16,0,0,0,0,17,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  107,239,253,165,0,0,0,0,0,110,255,255,255,255,195,0,0,0,10,255,
  255,255,255,255,254,64,0,0,175,255,254,185,172,255,255,226,0,6,255,255,
  113,0,0,60,255,251,0,30,255,244,0,0,0,1,223,255,48,111,255,128,
  0,0,0,0,95,255,144,191,255,16,0,0,0,0,14,255,192,239,255,255,
  255,255,255,255,255,255,224,255,255,255,255,255,255,255,255,255,240,255,255,255,
  255,255,255,255,255,255,224,223,255,153,153,153,153,153,153,153,128,191,255,32,
  0,0,0,0,0,0,0,111,255,144,0,0,0,0,0,0,0,30,255,246,
  0,0,0,0,38,0,0,6,255,255,145,0,0,6,239,144,0,0,175,255,
  255,201,155,239,255,247,0,0,10,255,255,255,255,255,255,177,0,0,0,110,
  255,255,255,255,232,0,0,0,0,1,106,223,254,183,16,0,0,0,0,0,
  104,132,0,0,0,0,0,7,255,255,64,0,0,0,0,95,255,255,226,0,
  0,0,4,255,255,255,253,16,0,0,62,255,229,143,255,193,0,2,223,253,
  48,5,239,251,0,12,255,177,0,0,45,255,128,4,233,0,0,0,1,172,
  16,0,32,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  255,251,0,0,0,0,0,0,1,223,250,0,0,0,0,0,0,0,11,255,
  255,112,0,0,0,0,0,0,159,255,255,244,0,0,0,0,0,6,255,253,
  239,254,48,0,0,0,0,79,255,193,62,255,209,0,0,0,2,239,251,16,
  3,223,251,0,0,0,10,255,144,0,0,45,255,96,0,0,2,216,0,0,
  0,1,204,16,0,0,0,16,0,0,0,0,17,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,123,239,
  253,165,0,0,0,0,0,110,255,255,255,255,212,0,0,0,10,255,255,255,
  255,255,255,112,0,0,175,255,255,185,172,255,255,246,0,6,255,255,129,0,
  0,59,255,255,48,30,255,246,0,0,0,0,175,255,176,111,255,144,0,0,
  0,0,29,255,242,191,255,32,0,0,0,0,7,255,247,239,253,0,0,0,
  0,0,2,255,249,255,251,0,0,0,0,0,0,255,251,255,251,0,0,0,
  0,0,1,255,251,223,253,0,0,0,0,0,3,255,249,175,255,48,0,0,
  0,0,7,255,246,111,255,160,0,0,0,0,29,255,242,30,255,246,0,0,
  0,0,175,255,160,6,255,255,145,0,0,59,255,254,32,0,175,255,255,185,
  172,255,255,246,0,0,10,255,255,255,255,255,255,96,0,0,0,110,255,255,
  255,255,212,0,0,0,0,1,123,239,253,165,0,0,0,0,0,0,191,251,
  0,0,0,0,0,8,255,255,144,0,0,0,0,111,255,255,246,0,0,0,
  4,255,253,223,255,64,0,0,46,255,194,28,255,226,0,1,223,251,16,1,
  191,253,16,6,255,160,0,0,10,255,96,0,136,0,0,0,0,153,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,
  251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,
  0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,
  251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,
  0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,239,
  252,0,0,0,0,207,254,207,255,16,0,0,1,255,252,143,255,128,0,0,
  8,255,248,63,255,246,0,0,111,255,243,9,255,255,218,157,255,255,144,1,
  207,255,255,255,255,252,16,0,26,255,255,255,255,161,0,0,0,73,223,253,
  164,0,0,0,0,0,1,16,0,0,0,0,0,0,0,42,255,252,64,0,
  3,64,0,0,3,239,255,255,251,84,126,227,0,0,30,255,255,255,255,255,
  255,247,0,0,5,251,66,74,255,255,255,128,0,0,0,64,0,0,75,239,
  197,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,140,239,235,96,
  11,255,240,0,1,159,255,255,255,253,59,255,240,0,28,255,255,255,255,255,
  253,255,240,0,207,255,255,185,172,255,255,255,240,8,255,255,129,0,0,42,
  255,255,240,30,255,246,0,0,0,0,143,255,240,127,255,160,0,0,0,0,
  13,255,240,191,255,32,0,0,0,0,7,255,240,239,253,0,0,0,0,0,
  4,255,240,255,251,0,0,0,0,0,2,255,240,255,251,0,0,0,0,0,
  3,255,240,239,253,0,0,0,0,0,5,255,240,191,255,32,0,0,0,0,
  8,255,240,127,255,160,0,0,0,0,13,255,240,31,255,246,0,0,0,0,
  159,255,240,8,255,255,145,0,0,42,255,255,240,1,207,255,255,185,156,255,
  255,255,240,0,44,255,255,255,255,255,253,255,240,0,1,159,255,255,255,253,
  75,255,240,0,0,3,156,239,235,97,11,255,240,0,0,0,17,0,0,0,
  0,0,0,0,0,93,255,232,16,0,7,0,0,0,8,255,255,255,231,69,
  191,144,0,0,95,255,255,255,255,255,255,226,0,0,10,248,50,109,255,255,
  254,64,0,0,0,80,0,0,125,254,162,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,1,123,239,253,165,0,0,0,0,0,110,255,255,255,255,
  212,0,0,0,10,255,255,255,255,255,255,112,0,0,175,255,255,185,172,255,
  255,246,0,6,255,255,129,0,0,59,255,255,48,30,255,246,0,0,0,0,
  175,255,176,111,255,144,0,0,0,0,29,255,242,191,255,32,0,0,0,0,
  7,255,247,239,253,0,0,0,0,0,2,255,249,255,251,0,0,0,0,0,
  0,255,251,255,251,0,0,0,0,0,1,255,251,223,253,0,0,0,0,0,
  3,255,249,175,255,48,0,0,0,0,7,255,246,111,255,160,0,0,0,0,
  29,255,242,30,255,246,0,0,0,0,175,255,160,6,255,255,145,0,0,59,
  255,254,32,0,175,255,255,185,172,255,255,246,0,0,10,255,255,255,255,255,
  255,96,0,0,0,110,255,255,255,255,212,0,0,0,0,1,123,239,253,165,
  0,0,0,0,0,1,107,223,253,182,16,0,0,0,110,255,255,255,255,230,
  0,0,10,255,255,255,255,255,255,144,0,175,255,255,201,155,239,255,243,6,
  255,255,145,0,0,23,255,64,30,255,246,0,0,0,0,52,0,111,255,160,
  0,0,0,0,0,0,191,255,32,0,0,0,0,0,0,239,253,0,0,0,
  0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,
  0,0,223,253,0,0,0,0,0,0,0,191,255,48,0,0,0,0,0,0,
  111,255,160,0,0,0,0,0,0,30,255,247,0,0,0,0,52,0,6,255,
  255,146,0,0,23,239,64,0,175,255,255,201,155,239,255,243,0,10,255,255,
  255,255,255,255,160,0,0,110,255,255,255,255,230,0,0,0,1,107,255,253,
  182,16,0,0,0,0,5,255,128,0,0,0,0,0,0,10,255,233,32,0,
  0,0,0,0,14,255,255,227,0,0,0,0,0,3,52,175,252,0,0,0,
  0,0,0,0,31,255,0,0,0,0,28,130,1,143,253,0,0,0,0,191,
  255,255,255,244,0,0,0,0,41,223,255,251,48,0,0,0,0,0,1,34,
  0,0,0,0,0,0,0,0,4,140,239,254,218,97,0,0,0,0,0,0,
  6,223,255,255,255,255,255,146,0,0,0,0,2,207,255,255,255,255,255,255,
  254,96,0,0,0,78,255,255,253,185,154,223,255,255,250,0,0,4,255,255,
  249,48,0,0,1,126,255,254,32,0,46,255,252,32,0,0,0,0,1,175,
  227,0,0,191,255,177,0,0,0,0,0,0,7,48,0,5,255,253,16,0,
  0,0,0,0,0,0,0,0,12,255,244,0,0,0,0,0,0,0,0,0,
  0,63,255,176,0,0,0,0,0,0,0,0,0,0,127,255,80,0,0,0,
  0,0,0,0,0,0,0,191,255,16,0,0,0,0,0,0,0,0,0,0,
  223,253,0,0,0,0,0,0,0,0,0,0,0,255,252,0,0,0,0,0,
  0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,0,0,0,239,
  252,0,0,0,0,0,0,0,0,0,0,0,223,253,0,0,0,0,0,0,
  0,0,0,0,0,191,255,16,0,0,0,0,0,0,0,0,0,0,127,255,
  80,0,0,0,0,0,0,0,0,0,0,63,255,176,0,0,0,0,0,0,
  0,0,0,0,12,255,244,0,0,0,0,0,0,0,0,0,0,5,255,253,
  16,0,0,0,0,0,0,0,0,0,0,191,255,177,0,0,0,0,0,0,
  6,96,0,0,46,255,252,32,0,0,0,0,0,143,246,0,0,4,255,255,
  249,48,0,0,1,109,255,255,80,0,0,78,255,255,253,185,154,207,255,255,
  251,16,0,0,2,207,255,255,255,255,255,255,255,128,0,0,0,0,6,223,
  255,255,255,255,255,163,0,0,0,0,0,0,4,140,255,254,218,98,0,0,
  0,0,0,0,0,0,5,255,128,0,0,0,0,0,0,0,0,0,0,10,
  255,233,32,0,0,0,0,0,0,0,0,0,14,255,255,227,0,0,0,0,
  0,0,0,0,0,3,52,175,252,0,0,0,0,0,0,0,0,0,0,0,
  31,255,0,0,0,0,0,0,0,0,28,114,1,143,253,0,0,0,0,0,
  0,0,0,191,255,255,255,244,0,0,0,0,0,0,0,0,41,239,255,250,
  48,0,0,0,0,0,0,0,0,0,1,34,0,0,0,0,0,0,0,0,
  2,207,194,0,0,0,0,0,12,255,252,0,0,0,0,0,15,255,255,0,
  0,0,0,0,12,255,252,0,0,0,0,0,2,207,194,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,1,255,240,0,0,0,0,0,
  2,255,240,0,0,0,0,0,3,255,240,0,0,0,0,0,92,255,240,0,
  0,0,0,44,255,255,240,0,0,0,2,223,255,255,240,0,0,0,11,255,
  252,65,0,0,0,0,79,255,176,0,0,0,0,0,175,255,32,0,0,0,
  0,0,223,253,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,252,
  0,0,0,0,2,0,223,255,16,0,0,0,63,96,175,255,112,0,0,1,
  223,248,95,255,246,0,0,60,255,249,11,255,255,218,156,255,255,209,2,223,
  255,255,255,255,254,48,0,43,255,255,255,255,178,0,0,0,90,239,253,164,
  0,0,44,252,32,207,255,192,255,255,240,207,255,192,44,252,32,0,0,0,
  0,0,0,0,0,0,0,0,0,15,255,176,15,255,176,15,255,176,15,255,
  176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,
  255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,
  15,255,176,15,255,176,0,6,206,236,96,0,1,207,255,255,252,16,12,255,
  255,255,255,176,95,255,113,23,255,245,191,248,0,0,143,251,239,242,0,0,
  47,254,255,240,0,0,15,255,239,242,0,0,47,254,191,248,0,0,143,251,
  111,255,96,6,255,246,12,255,255,255,255,192,2,207,255,255,252,32,0,23,
  207,252,113,0,44,252,32,207,255,192,255,255,240,207,255,192,44,252,32,
};
static int fontIdx(uint32_t cp){
  if(cp>=0x20 && cp<=0x7E) return cp-0x20;
  switch(cp){
    case 0xE1: return 95;
    case 0xE9: return 96;
    case 0xED: return 97;
    case 0xF3: return 98;
    case 0xFA: return 99;
    case 0xFC: return 100;
    case 0xF1: return 101;
    case 0xC1: return 102;
    case 0xC9: return 103;
    case 0xCD: return 104;
    case 0xD3: return 105;
    case 0xDA: return 106;
    case 0xDC: return 107;
    case 0xD1: return 108;
    case 0xE0: return 109;
    case 0xE8: return 110;
    case 0xEC: return 111;
    case 0xF2: return 112;
    case 0xF9: return 113;
    case 0xE2: return 114;
    case 0xEA: return 115;
    case 0xEE: return 116;
    case 0xF4: return 117;
    case 0xFB: return 118;
    case 0xE3: return 119;
    case 0xF5: return 120;
    case 0xE7: return 121;
    case 0xC7: return 122;
    case 0xBF: return 123;
    case 0xA1: return 124;
    case 0xB0: return 125;
    case 0xB7: return 126;
    default: return 0x3F-0x20;
  }
}
#define FONT_HPS 8    // px de alto por unidad de 'size' logico
#define FONT_CAPOFF 11 // alinea el tope de mayusculas/digitos con y (como el 5x7)
static inline uint8_t fgPix(const FGlyph* g, int x, int y){
  if(x < 0 || y < 0 || x >= g->w || y >= g->h) return 0;
  int bpr = (g->w + 1) >> 1;
  uint8_t b = FBM[g->off + (uint32_t)y * bpr + (x >> 1)];
  return (x & 1) ? (b & 0x0F) : (b >> 4);
}
static inline float fontSc(int size){ return (float)(size * FONT_HPS) / (float)FONT_LINEH; }
static uint32_t nextCP(const char** ps){
  const char* s = *ps; uint8_t b = (uint8_t)*s++; uint32_t cp;
  if(b < 0x80) cp = b;
  else if((b & 0xE0) == 0xC0){ uint8_t b1 = *s ? (uint8_t)*s++ : 0; cp = ((b & 0x1F) << 6) | (b1 & 0x3F); }
  else if((b & 0xF0) == 0xE0){ if(*s) s++; if(*s) s++; cp = 0x3F; }
  else cp = 0x3F;
  *ps = s; return cp;
}
// Dibuja un glifo escalado (bilineal desde el master 4bpp)
static void drawGlyphScaled(int px0, int py0, const FGlyph* g, float sc, uint16_t col, uint8_t alpha){
  if(g->w == 0) return;
  int tw = (int)(g->w * sc + 0.999f), th = (int)(g->h * sc + 0.999f);
  for(int ty = 0; ty < th; ty++){
    float fy = ty / sc; int y0 = (int)fy; float dyf = fy - y0;
    for(int tx = 0; tx < tw; tx++){
      float fx = tx / sc; int x0 = (int)fx; float dxf = fx - x0;
      float a00 = fgPix(g, x0, y0),     a10 = fgPix(g, x0 + 1, y0);
      float a01 = fgPix(g, x0, y0 + 1), a11 = fgPix(g, x0 + 1, y0 + 1);
      float top = a00 * (1 - dxf) + a10 * dxf;
      float bot = a01 * (1 - dxf) + a11 * dxf;
      float cov = (top * (1 - dyf) + bot * dyf) * 17.0f;   // 0..255
      if(cov > 4.0f){
        int a = (int)(cov * alpha / 255.0f); if(a > 255) a = 255;
        pxA(px0 + tx, py0 + ty, col, (uint8_t)a);
      }
    }
  }
}

static int textW(const char* s, int size){
  if(size <= 1){                 // texto minusculo: bitmap 5x7 nitido (6px monoespaciado)
    int n = 0;
    while(*s){ uint8_t b = (uint8_t)*s++;
      if(b >= 0x80){ if((b & 0xE0) == 0xC0){ if(*s) s++; } else if((b & 0xF0) == 0xE0){ if(*s) s++; if(*s) s++; } }
      n++;
    }
    return n > 0 ? n * 6 - 1 : 0;
  }
  float sc = fontSc(size), w = 0;
  while(*s){ uint32_t cp = nextCP(&s); w += FG[fontIdx(cp)].adv * sc; }
  return (int)(w + 0.5f);
}
// Texto con alpha (workhorse). REGLA: vectorial Outfit para tamano >=2
// (curvas suaves), bitmap 5x7 NITIDO para size 1 (evita el emborronado
// de encoger demasiado una vectorial fina).
static int drawTextA(int x, int y, const char* s, int size, uint16_t col, uint8_t alpha){
  if(size <= 1){
    while(*s){
      uint8_t b = (uint8_t)*s++; uint32_t cp;
      if(b < 0x80) cp = b;
      else if((b & 0xE0) == 0xC0){ uint8_t b1 = *s ? (uint8_t)*s++ : 0; cp = ((b & 0x1F) << 6) | (b1 & 0x3F); }
      else if((b & 0xF0) == 0xE0){ if(*s) s++; if(*s) s++; cp = 0x3F; }
      else cp = 0x3F;
      uint8_t base, acc; mapCP(cp, base, acc);
      drawGlyphSmooth(x, y, base, 1, col, alpha);     // 1:1 = nitido
      if(acc) drawAccent(x, y, 1, acc, col);
      x += 6;
    }
    return x;
  }
  float sc = fontSc(size), penx = x;
  while(*s){
    uint32_t cp = nextCP(&s);
    const FGlyph* g = &FG[fontIdx(cp)];
    drawGlyphScaled((int)(penx + g->bx * sc + 0.5f), y + (int)((g->topoff - FONT_CAPOFF) * sc + 0.5f), g, sc, col, alpha);
    penx += g->adv * sc;
  }
  return (int)(penx + 0.5f);
}
static int  drawText(int x, int y, const char* s, int size, uint16_t col){ return drawTextA(x, y, s, size, col, 255); }
static void drawTextC(int cx, int y, const char* s, int size, uint16_t col){ drawTextA(cx - textW(s, size) / 2, y, s, size, col, 255); }
static void drawTextCA(int cx, int y, const char* s, int size, uint16_t col, uint8_t a){ drawTextA(cx - textW(s, size) / 2, y, s, size, col, a); }
static void drawTextR(int rx, int y, const char* s, int size, uint16_t col){ drawTextA(rx - textW(s, size), y, s, size, col, 255); }

// ---------------- Triangulo relleno (baricentrico) ----------------
static void fillTriangle(int x0,int y0,int x1,int y1,int x2,int y2,uint16_t c){
  int minx = min(x0, min(x1, x2)), maxx = max(x0, max(x1, x2));
  int miny = min(y0, min(y1, y2)), maxy = max(y0, max(y1, y2));
  // En modo landscape (gLand) el lienzo LOGICO es 800x480, no 480x800: recortar
  // contra los limites correctos para no perder los triangulos con x logica >=480
  // (picos/naves/wave del juego Geo Dash). En portrait no cambia nada.
  int bcw = gLand ? SCR_H : SCR_W, bch = gLand ? SCR_W : SCR_H;
  if(minx < 0) minx = 0; if(miny < 0) miny = 0;
  if(maxx >= bcw) maxx = bcw - 1; if(maxy >= bch) maxy = bch - 1;
  for(int y = miny; y <= maxy; y++){
    for(int x = minx; x <= maxx; x++){
      long e0 = (long)(x - x0) * (y1 - y0) - (long)(y - y0) * (x1 - x0);
      long e1 = (long)(x - x1) * (y2 - y1) - (long)(y - y1) * (x2 - x1);
      long e2 = (long)(x - x2) * (y0 - y2) - (long)(y - y2) * (x0 - x2);
      bool neg = (e0 < 0) || (e1 < 0) || (e2 < 0);
      bool pos = (e0 > 0) || (e1 > 0) || (e2 > 0);
      if(!(neg && pos)) px(x, y, c);
    }
  }
}
static void fillQuad(int x0,int y0,int x1,int y1,int x2,int y2,int x3,int y3,uint16_t c){
  fillTriangle(x0,y0,x1,y1,x2,y2,c);
  fillTriangle(x0,y0,x2,y2,x3,y3,c);
}

// ---------------- Reloj vectorial (trazo grueso redondeado) -------
static float bigCharAdvance(char ch, int capH){
  if(ch == ':') return capH * 0.34f;
  return capH * 0.60f + capH * 0.12f;   // ancho + hueco
}
static void drawBigChar(char ch, int ox, int oy, int capH, int thick, uint16_t col){
  float DW = capH * 0.60f, DH = capH;
  float m = thick * 0.5f;
  float L = ox + m, R = ox + DW - m, T = oy + m, B = oy + DH - m;
  float MX = ox + DW * 0.5f, MY = oy + DH * 0.5f;
  int rad = thick / 2; if(rad < 1) rad = 1;
  float pr = 0.0174532925f;
  auto arc = [&](float cxx, float cyy, float rx, float ry, float a0, float a1){
    int steps = (int)(fabsf(a1 - a0) / 7.0f) + 2;
    float pxp = 0, pyp = 0; bool first = true;
    for(int i = 0; i <= steps; i++){
      float a = (a0 + (a1 - a0) * i / steps) * pr;
      float xx = cxx + rx * cosf(a), yy = cyy + ry * sinf(a);
      if(!first) strokeSegAA(pxp, pyp, xx, yy, (float)rad, col);
      pxp = xx; pyp = yy; first = false;
    }
  };
  auto seg = [&](float x0, float y0, float x1, float y1){ strokeSegAA(x0, y0, x1, y1, (float)rad, col); };
  float sx;
  switch(ch){
    case '0': arc(MX, MY, DW * 0.5f - m, DH * 0.5f - m, 0, 360); break;
    case '1':
      sx = MX + DW * 0.06f;
      seg(sx, T, sx, B);
      seg(MX - DW * 0.26f, T + DH * 0.16f, sx, T);
      seg(MX - DW * 0.30f, B, MX + DW * 0.34f, B);
      break;
    case '2': {
      arc(MX, T + DH * 0.24f, DW * 0.5f - m, DH * 0.24f, 180, 380);
      float ex = MX + (DW * 0.5f - m) * cosf(20 * pr);
      float ey = (T + DH * 0.24f) + (DH * 0.24f) * sinf(20 * pr);
      seg(ex, ey, L, B);
      seg(L, B, R, B);
    } break;
    case '3':
      // FIX: los angulos originales (200-430 / 290-520) hacian que ambos arcos
      // se pasaran ~50-70 grados mas alla del centro vertical del glifo, asi
      // que se superponian en una franja ancha del lado derecho -- eso era la
      // "raya horizontal que sobresale". Los angulos de abajo se resolvieron
      // para que el arco superior TERMINE exactamente donde el arco inferior
      // EMPIEZA (mismo punto, sin solape), verificado por simulacion en Python
      // (distancia entre los dos extremos = 0px). El resto del barrido (el
      // rizo de la izquierda en cada extremo) se dejo igual que el original.
      arc(MX - DW * 0.05f, T + DH * 0.27f, DW * 0.40f, DH * 0.27f, 200, 398);
      arc(MX - DW * 0.05f, B - DH * 0.27f, DW * 0.40f, DH * 0.27f, 322, 520);
      break;
    case '4':
      sx = MX + DW * 0.16f;
      seg(sx, T, L, T + DH * 0.64f);
      seg(L, T + DH * 0.64f, R, T + DH * 0.64f);
      seg(sx, T, sx, B);
      break;
    case '5':
      seg(R, T, L + DW * 0.04f, T);
      seg(L + DW * 0.04f, T, L + DW * 0.04f, T + DH * 0.40f);
      arc(MX - DW * 0.02f, B - DH * 0.28f, DW * 0.44f, DH * 0.28f, 190, 470);
      break;
    case '6':
      arc(MX, MY + DH * 0.02f, DW * 0.42f, DH * 0.44f, 300, 120);
      arc(MX, B - DH * 0.26f, DW * 0.42f, DH * 0.26f, 0, 360);
      break;
    case '7':
      seg(L, T, R, T);
      seg(R, T, MX - DW * 0.06f, B);
      break;
    case '8':
      arc(MX, T + DH * 0.25f, DW * 0.42f, DH * 0.25f, 0, 360);
      arc(MX, B - DH * 0.27f, DW * 0.46f, DH * 0.27f, 0, 360);
      break;
    case '9':
      arc(MX, T + DH * 0.26f, DW * 0.42f, DH * 0.26f, 0, 360);
      arc(MX, MY, DW * 0.42f, DH * 0.44f, 40, 200);
      break;
    case ':': {
      int dr = thick; if(dr < 2) dr = 2;
      fillCircleAA(ox + DW * 0.16f, T + DH * 0.32f, (float)dr, col);
      fillCircleAA(ox + DW * 0.16f, B - DH * 0.28f, (float)dr, col);
    } break;
  }
}
// Dibuja "H:MM" centrado horizontalmente en cx
static void drawBigClock(const char* s, int cx, int y, int capH, int thick, uint16_t col){
  float total = 0;
  for(const char* p = s; *p; p++) total += bigCharAdvance(*p, capH);
  float x = cx - total / 2;
  for(const char* p = s; *p; p++){
    drawBigChar(*p, (int)x, y, capH, thick, col);
    x += bigCharAdvance(*p, capH);
  }
}

// #############################################################
// ##  ICONOS VECTORIALES  (originales, basados en tus imagenes)
// #############################################################

// arco con trazo grueso (para wifi y detalles)
static void arcStroke(float cx, float cy, float r, float a0, float a1, int thick, uint16_t col){
  float pr = 0.0174532925f;
  int steps = (int)(fabsf(a1 - a0) / 8.0f) + 2;
  float pxp = 0, pyp = 0; bool first = true;
  int rad = thick / 2; if(rad < 1) rad = 1;
  for(int i = 0; i <= steps; i++){
    float a = (a0 + (a1 - a0) * i / steps) * pr;
    float xx = cx + r * cosf(a), yy = cy + r * sinf(a);
    if(!first) strokeSeg(pxp, pyp, xx, yy, rad, col);
    pxp = xx; pyp = yy; first = false;
  }
}

enum { IC_RELOJ, IC_GALERIA, IC_MULTIMEDIA, IC_ALMACEN, IC_MODOPC, IC_NOTAS,
       IC_EDU, IC_NAV, IC_CODE, IC_BIEN, IC_PAINT, IC_JUEGOS,
       IC_AJUSTES, IC_CALC, IC_CALEND, IC_CAMARA };

static void iconBase(int x, int y, int S, uint16_t bg, int rf100){
  int r = S * rf100 / 100;
  if(gIconStyle == 1){                     // estilo "Vidrio": fondo Liquid Glass (sin destello barrido; ver drawAppIcon)
    glDrawSpec = false;
    drawLiquidGlassPanel(x, y, S, S, r, bg, 0);
    glDrawSpec = true;
  } else {                                 // estilo "Plano" (original)
    fillRoundRect(x, y, S, S, r, bg);
    // sutil brillo superior
    fillRoundRectA(x, y, S, S / 2, r, rgb565(255,255,255), 22);
  }
}

static void drawAppIcon(int id, int x, int y, int S){
  int cx = x + S / 2, cy = y + S / 2;
  int tk = S / 12; if(tk < 2) tk = 2;               // grosor de trazo generico
  uint16_t WHITE = rgb565(255,255,255);
  switch(id){
    case IC_RELOJ: {
      iconBase(x, y, S, rgb565(245,245,247), 22);
      fillRing(cx, cy, (int)(S * 0.36f), 2, rgb565(70,70,74));
      // ticks 12/3/6/9
      fillRect(cx - 1, y + (int)(S * 0.16f), 2, S / 12, rgb565(70,70,74));
      fillRect(cx - 1, y + S - (int)(S * 0.16f) - S / 12, 2, S / 12, rgb565(70,70,74));
      fillRect(x + (int)(S * 0.16f), cy - 1, S / 12, 2, rgb565(70,70,74));
      fillRect(x + S - (int)(S * 0.16f) - S / 12, cy - 1, S / 12, 2, rgb565(70,70,74));
      strokeSeg(cx, cy, cx - S * 0.14f, cy - S * 0.10f, tk / 2 + 1, rgb565(30,30,30)); // hora
      strokeSeg(cx, cy, cx + S * 0.12f, cy - S * 0.20f, tk / 2, rgb565(245,140,30));   // min
      fillCircle(cx, cy, tk / 2 + 1, rgb565(30,30,30));
    } break;
    case IC_GALERIA: {
      iconBase(x, y, S, WHITE, 22);
      uint16_t cols[8] = { rgb565(233,64,64), rgb565(240,150,40), rgb565(240,210,50),
                           rgb565(90,200,90), rgb565(50,190,190), rgb565(60,120,235),
                           rgb565(120,80,220), rgb565(220,80,200) };
      float d = S * 0.17f, pr = S * 0.135f;
      for(int k = 0; k < 8; k++){
        float a = k * 45 * 0.0174532925f;
        fillCircle((int)(cx + d * cosf(a)), (int)(cy + d * sinf(a)), (int)pr, cols[k]);
      }
      fillCircle(cx, cy, (int)(S * 0.11f), WHITE);
    } break;
    case IC_MULTIMEDIA: {
      iconBase(x, y, S, rgb565(27,95,217), 22);
      fillTriangle(cx - (int)(S * 0.12f), cy - (int)(S * 0.18f),
                   cx - (int)(S * 0.12f), cy + (int)(S * 0.18f),
                   cx + (int)(S * 0.22f), cy, WHITE);
    } break;
    case IC_ALMACEN: {
      iconBase(x, y, S, rgb565(59,123,217), 22);
      uint16_t fol = rgb565(225,236,250);
      fillRoundRect(x + (int)(S * 0.20f), y + (int)(S * 0.28f), (int)(S * 0.30f), (int)(S * 0.12f), 3, fol);
      fillRoundRect(x + (int)(S * 0.18f), y + (int)(S * 0.36f), (int)(S * 0.64f), (int)(S * 0.34f), 5, fol);
      // nubecita
      uint16_t cl = rgb565(205,222,245);
      fillCircle(x + (int)(S * 0.60f), y + (int)(S * 0.34f), (int)(S * 0.10f), cl);
      fillCircle(x + (int)(S * 0.72f), y + (int)(S * 0.36f), (int)(S * 0.08f), cl);
      fillRect(x + (int)(S * 0.58f), y + (int)(S * 0.36f), (int)(S * 0.18f), (int)(S * 0.07f), cl);
    } break;
    case IC_MODOPC: {
      iconBase(x, y, S, rgb565(30,58,110), 22);
      fillRoundRect(x + (int)(S * 0.16f), y + (int)(S * 0.24f), (int)(S * 0.68f), (int)(S * 0.40f), 4, rgb565(235,240,250));
      fillRect(x + (int)(S * 0.22f), y + (int)(S * 0.30f), (int)(S * 0.56f), (int)(S * 0.28f), rgb565(45,95,205));
      fillRect(cx - (int)(S * 0.05f), y + (int)(S * 0.64f), (int)(S * 0.10f), (int)(S * 0.08f), rgb565(200,210,225));
      fillRoundRect(cx - (int)(S * 0.16f), y + (int)(S * 0.72f), (int)(S * 0.32f), (int)(S * 0.06f), 2, rgb565(200,210,225));
    } break;
    case IC_NOTAS: {
      iconBase(x, y, S, rgb565(232,167,90), 22);
      fillRoundRect(x + (int)(S * 0.22f), y + (int)(S * 0.18f), (int)(S * 0.48f), (int)(S * 0.62f), 5, WHITE);
      for(int i = 0; i < 3; i++)
        fillRect(x + (int)(S * 0.30f), y + (int)(S * 0.30f) + i * (int)(S * 0.12f), (int)(S * 0.32f), 2, rgb565(180,180,185));
      strokeSeg(x + S * 0.56f, y + S * 0.66f, x + S * 0.78f, y + S * 0.40f, tk / 2 + 1, rgb565(120,90,40)); // lapiz
      fillCircle((int)(x + S * 0.78f), (int)(y + S * 0.40f), tk / 2 + 1, rgb565(245,210,90));
    } break;
    case IC_EDU: {
      iconBase(x, y, S, rgb565(79,179,196), 22);
      uint16_t cap = rgb565(28,52,96);
      fillQuad(cx, cy - (int)(S * 0.24f), cx + (int)(S * 0.28f), cy - (int)(S * 0.06f),
               cx, cy + (int)(S * 0.12f), cx - (int)(S * 0.28f), cy - (int)(S * 0.06f), cap);
      fillRoundRect(cx - (int)(S * 0.18f), cy + (int)(S * 0.08f), (int)(S * 0.36f), (int)(S * 0.16f), 3, WHITE);
      strokeSeg(cx + S * 0.26f, cy - S * 0.06f, cx + S * 0.26f, cy + S * 0.14f, 1, cap);
    } break;
    case IC_NAV: {
      iconBase(x, y, S, rgb565(46,155,230), 22);
      fillRing(cx, cy, (int)(S * 0.30f), 2, WHITE);
      vLine(cx, cy - (int)(S * 0.30f), (int)(S * 0.60f), WHITE);
      hLine(cx - (int)(S * 0.30f), cy, (int)(S * 0.60f), WHITE);
      arcStroke(cx, cy, S * 0.18f, 90, 270, 2, WHITE);   // meridiano
      arcStroke(cx, cy, S * 0.18f, -90, 90, 2, WHITE);
    } break;
    case IC_CODE: {
      iconBase(x, y, S, rgb565(154,160,166), 22);
      uint16_t dk = rgb565(55,58,66);
      strokeSeg(cx - S * 0.10f, cy - S * 0.15f, cx - S * 0.26f, cy, tk / 2 + 1, dk);
      strokeSeg(cx - S * 0.26f, cy, cx - S * 0.10f, cy + S * 0.15f, tk / 2 + 1, dk);
      strokeSeg(cx + S * 0.10f, cy - S * 0.15f, cx + S * 0.26f, cy, tk / 2 + 1, dk);
      strokeSeg(cx + S * 0.26f, cy, cx + S * 0.10f, cy + S * 0.15f, tk / 2 + 1, dk);
      strokeSeg(cx + S * 0.04f, cy - S * 0.17f, cx - S * 0.04f, cy + S * 0.17f, tk / 2, dk);
    } break;
    case IC_BIEN: {
      iconBase(x, y, S, rgb565(92,193,90), 30);
      strokeSeg(cx - S * 0.16f, cy + S * 0.02f, cx - S * 0.02f, cy + S * 0.16f, tk / 2 + 1, WHITE);
      strokeSeg(cx - S * 0.02f, cy + S * 0.16f, cx + S * 0.20f, cy - S * 0.14f, tk / 2 + 1, WHITE);
    } break;
    case IC_PAINT: {
      iconBase(x, y, S, rgb565(241,231,210), 22);
      fillCircle(cx - (int)(S * 0.04f), cy + (int)(S * 0.02f), (int)(S * 0.27f), rgb565(236,226,205));
      fillCircle(cx + (int)(S * 0.11f), cy + (int)(S * 0.11f), (int)(S * 0.06f), rgb565(241,231,210)); // hueco
      fillCircle(cx - (int)(S * 0.14f), cy - (int)(S * 0.06f), (int)(S * 0.045f), rgb565(230,70,70));
      fillCircle(cx - (int)(S * 0.02f), cy - (int)(S * 0.13f), (int)(S * 0.045f), rgb565(240,200,60));
      fillCircle(cx + (int)(S * 0.10f), cy - (int)(S * 0.07f), (int)(S * 0.045f), rgb565(70,130,235));
      fillCircle(cx - (int)(S * 0.16f), cy + (int)(S * 0.09f), (int)(S * 0.045f), rgb565(80,190,90));
      strokeSeg(cx + S * 0.02f, cy - S * 0.16f, cx + S * 0.26f, cy - S * 0.30f, tk / 2 + 1, rgb565(140,100,60));
    } break;
    case IC_JUEGOS: {
      iconBase(x, y, S, rgb565(142,30,30), 22);
      drawRoundRect(x + (int)(S * 0.14f), y + (int)(S * 0.34f), (int)(S * 0.72f), (int)(S * 0.30f), (int)(S * 0.13f), WHITE);
      drawRoundRect(x + (int)(S * 0.14f) + 1, y + (int)(S * 0.34f) + 1, (int)(S * 0.72f) - 2, (int)(S * 0.30f) - 2, (int)(S * 0.12f), WHITE);
      // dpad
      fillRect(cx - (int)(S * 0.22f) - 1, cy + (int)(S * 0.02f), (int)(S * 0.12f), 3, WHITE);
      fillRect(cx - (int)(S * 0.16f) - 1, cy - (int)(S * 0.04f), 3, (int)(S * 0.12f), WHITE);
      // botones
      fillCircle(cx + (int)(S * 0.14f), cy - (int)(S * 0.01f), 3, WHITE);
      fillCircle(cx + (int)(S * 0.22f), cy + (int)(S * 0.05f), 3, WHITE);
    } break;
    case IC_AJUSTES: {
      iconBase(x, y, S, rgb565(138,143,152), 22);
      uint16_t g = rgb565(70,74,84);
      fillCircle(cx, cy, (int)(S * 0.26f), g);
      for(int k = 0; k < 8; k++){
        float a = k * 45 * 0.0174532925f;
        fillCircle((int)(cx + S * 0.30f * cosf(a)), (int)(cy + S * 0.30f * sinf(a)), (int)(S * 0.075f), g);
      }
      fillCircle(cx, cy, (int)(S * 0.10f), rgb565(138,143,152));
    } break;
    case IC_CALC: {
      iconBase(x, y, S, rgb565(58,58,60), 22);
      fillRoundRect(x + (int)(S * 0.18f), y + (int)(S * 0.16f), (int)(S * 0.64f), (int)(S * 0.16f), 3, rgb565(210,210,215));
      for(int rr = 0; rr < 3; rr++) for(int cc = 0; cc < 4; cc++){
        uint16_t bc = (cc == 3) ? rgb565(245,150,30) : rgb565(150,150,155);
        fillRoundRect(x + (int)(S * 0.18f) + cc * (int)(S * 0.17f), y + (int)(S * 0.40f) + rr * (int)(S * 0.15f),
                      (int)(S * 0.12f), (int)(S * 0.10f), 2, bc);
      }
    } break;
    case IC_CALEND: {
      iconBase(x, y, S, WHITE, 22);
      fillRect(x + (int)(S * 0.16f), y + (int)(S * 0.18f), (int)(S * 0.68f), (int)(S * 0.16f), rgb565(232,70,70));
      drawBigChar('1', cx - (int)(S * 0.60f * 0.30f), y + (int)(S * 0.36f), (int)(S * 0.44f), 3, rgb565(60,60,64));
    } break;
    case IC_CAMARA: {
      iconBase(x, y, S, rgb565(74,74,78), 22);
      fillCircle(cx, cy, (int)(S * 0.27f), rgb565(30,30,32));
      fillRing(cx, cy, (int)(S * 0.27f), 3, rgb565(120,120,128));
      fillCircle(cx, cy, (int)(S * 0.16f), rgb565(60,72,95));
      fillCircle(cx - (int)(S * 0.06f), cy - (int)(S * 0.06f), (int)(S * 0.05f), rgb565(150,175,205));
      fillRoundRect(x + (int)(S * 0.66f), y + (int)(S * 0.18f), (int)(S * 0.10f), (int)(S * 0.06f), 2, rgb565(190,190,195));
    } break;
  }
}

// ---------------- Iconos de barra de estado ----------------
static void drawWifi(int cx, int by, int R, uint16_t col){
  arcStroke(cx, by, R,        225, 315, 2, col);
  arcStroke(cx, by, R * 0.66f, 225, 315, 2, col);
  arcStroke(cx, by, R * 0.33f, 225, 315, 2, col);
  fillCircle(cx, by, 2, col);
}
static void drawBattery(int x, int y, int w, int h, int level, uint16_t col){
  drawRoundRect(x, y, w, h, 2, col);
  drawRoundRect(x + 1, y + 1, w - 2, h - 2, 2, col);
  fillRect(x + w, y + h / 3, 2, h / 3, col);       // pin +
  int fw = (w - 6) * level / 100;
  if(fw > 0) fillRect(x + 3, y + 3, fw, h - 6, col);
}

// #############################################################
// ##  TACTIL DE ALTO NIVEL (gestos)  ·  original
// #############################################################
struct Touch {
  bool down=false, pressed=false, released=false, tap=false, moved=false;
  bool swipeUp=false, swipeDown=false, swipeLeft=false, swipeRight=false;
  int  x=0, y=0, startX=0, startY=0, dx=0, dy=0;
  unsigned long downMs=0, lastMs=0;
};
static Touch T;

static void tDoRelease(unsigned long now){
  T.down = false; T.released = true;
  T.dx = T.x - T.startX; T.dy = T.y - T.startY;
  unsigned long dur = now - T.downMs;
  int adx = abs(T.dx), ady = abs(T.dy);
  if(adx < 16 && ady < 16 && dur < 550) T.tap = true;
  else if(ady > 55 && ady >= adx){ if(T.dy < 0) T.swipeUp = true; else T.swipeDown = true; }
  else if(adx > 55 && adx > ady){ if(T.dx < 0) T.swipeLeft = true; else T.swipeRight = true; }
}
static void flexPollTouch(){
  T.pressed = T.released = T.tap = false;
  T.swipeUp = T.swipeDown = T.swipeLeft = T.swipeRight = false;
  uint16_t gx = 0, gy = 0;
  int8_t ev = gtPoll(gx, gy);
  unsigned long now = millis();
  bool wasDown = T.down;
  if(ev == 1){
    T.x = gx; T.y = gy; T.lastMs = now;
    gTouchX = gx; gTouchY = gy; gTouchMs = now;   // para glassSheen(): reflejo que sigue el dedo
    if(!wasDown){ T.down = true; T.pressed = true; T.startX = gx; T.startY = gy; T.downMs = now; T.moved = false; }
    else if(abs((int)gx - T.startX) > 12 || abs((int)gy - T.startY) > 12) T.moved = true;
  } else if(ev == 0){
    if(wasDown) tDoRelease(now);
  } else {
    if(wasDown && now - T.lastMs > 90) tDoRelease(now);
  }
}

// #############################################################
// ##  ALMACENAMIENTO (NVS)  ·  reloj interno  ·  idiomas
// #############################################################
static Preferences prefs;
static bool  cfgOobeDone = false;
static int   cfgLang     = 0;                 // 0=ES 1=EN 2=FR 3=PT 4=IT 5=ZH
static bool  g24h        = false;             // formato de hora 24h
static int   gLockType   = 0;                 // 0 ninguno, 1 PIN, 2 contraseña
#define LW_CLOCK   0x01   // reloj grande + fecha
#define LW_WEATHER 0x02   // clima (mock, sin datos reales aun)
#define LW_CAL     0x04   // calendario (mock, sin eventos reales aun)
#define LW_NOTIF   0x08   // notificaciones (datos reales: gNotifs[])
static uint8_t gLockWidgets = LW_CLOCK;       // widgets activos en Bloqueo (por defecto: solo el reloj, igual que hoy)
static int   gNavMode    = 0;                 // 0 = botones clasicos, 1 = gestos iOS
// Widgets redimensionables del Home (Fase 1): solo 2 tamanos, no continuo.
// bit0 = clima ancho (ocupa toda la fila), bit1 = noticias ancho. Mutuamente
// excluyentes -- si uno se ensancha, el otro se oculta para no tener que
// mover la rejilla de apps (que empieza justo debajo, a altura fija).
#define WW_CLIMA  0x01
#define WW_NOTICIAS 0x02
static uint8_t gWidgetWide = 0;               // por defecto: los dos normales, lado a lado (igual que hoy)
static int   gAnimStyle  = 0;                 // transicion al abrir/cerrar apps: 0=zoom, 1=fundido, 2=deslizar
static char  cfgName[24]  = "FlexOS Ultra";

static void cfgLoad(){
  prefs.begin("flexos", true);
  cfgOobeDone = prefs.getBool("oobe", false);
  cfgLang     = prefs.getInt("lang", 0);
  g24h        = prefs.getBool("h24", false);
  uiGlass     = prefs.getBool("glass", false);
  gDark       = prefs.getBool("dark", true);
  gIconStyle  = prefs.getInt("iconstyle", 0);
  gBright     = prefs.getInt("bright", 80);
  gLockType   = prefs.getInt("locktype", 0);
  gLockWidgets = (uint8_t)prefs.getInt("lockwidgets", LW_CLOCK);
  gNavMode    = prefs.getInt("navmode", 0);
  gWidgetWide = (uint8_t)prefs.getInt("widgetwide", 0);
  if((gWidgetWide & WW_CLIMA) && (gWidgetWide & WW_NOTICIAS)) gWidgetWide = 0;  // combinacion invalida (prefs corruptas): normaliza
  gAnimStyle  = prefs.getInt("animstyle", 0);
  String n = prefs.getString("name", "FlexOS Ultra");
  n.toCharArray(cfgName, sizeof(cfgName));
  prefs.end();
}
// Guarda las preferencias personalizables (idioma, formato, estilo, brillo)
static void cfgSavePrefs(){
  prefs.begin("flexos", false);
  prefs.putInt("lang", cfgLang);
  prefs.putBool("h24", g24h);
  prefs.putBool("glass", uiGlass);
  prefs.putBool("dark", gDark);
  prefs.putInt("iconstyle", gIconStyle);
  prefs.putInt("bright", gBright);
  prefs.putInt("lockwidgets", gLockWidgets);
  prefs.putInt("navmode", gNavMode);
  prefs.putInt("widgetwide", gWidgetWide);
  prefs.putInt("animstyle", gAnimStyle);
  prefs.end();
}
static void cfgSaveOobe(){
  prefs.begin("flexos", false);
  prefs.putBool("oobe", true);
  prefs.putInt("lang", cfgLang);
  prefs.putString("name", cfgName);
  prefs.end();
  cfgOobeDone = true;
}

// -------- Idiomas --------
#define NLANG 6
static const char* LANG_ENDONYM[NLANG] = {
  "Espa\xC3\xB1ol", "English", "Fran\xC3\xA7" "ais", "Portugu\xC3\xAas", "Italiano", "\xE4\xB8\xAD\xE6\x96\x87" };
// idx de arrays de fecha (ZH no tiene glifos -> usa EN)
static inline int LI(){ return (cfgLang == 5) ? 1 : cfgLang; }

// Cadenas de interfaz. Columnas: ES,EN,FR,PT,IT. ZH usa EN.
enum { S_SELLANG, S_CONTINUE, S_YOURNAME, S_NAMEHINT, S_START, S_SWIPE,
       S_WEATHER, S_NEWS, S_NONEWS, S_NOEVENTS, S_NOTIFS, S_NONOTIFS, S_SOON, S_M2, S_BACK, S_WELCOME, S_NSTR };
static const char* CH[S_NSTR][5] = {
  {"Selecciona tu idioma","Select your language","Choisis ta langue","Selecione o idioma","Seleziona la lingua"},
  {"Continuar","Continue","Continuer","Continuar","Continua"},
  {"\xC2\xBF" "C\xC3\xB3mo se llama el equipo?","Name your device","Nomme ton appareil","Nomeie o dispositivo","Nomina il dispositivo"},
  {"Toca para escribir","Tap to type","Touche pour \xC3\xA9" "crire","Toque para escrever","Tocca per scrivere"},
  {"Comenzar","Get started","Commencer","Come\xC3\xA7" "ar","Inizia"},
  {"Desliza arriba para desbloquear","Swipe up to unlock","Glisse vers le haut","Deslize para desbloquear","Scorri per sbloccare"},
  {"Clima","Weather","M\xC3\xA9t\xC3\xA9o","Clima","Meteo"},
  {"Noticias","News","Actualit\xC3\xA9s","Not\xC3\xAD" "cias","Notizie"},
  {"(No hay noticias que mostrar)","(No news to show)","(Aucune actualit\xC3\xA9)","(Sem not\xC3\xAD" "cias)","(Nessuna notizia)"},
  {"(Sin eventos)","(No events)","(Aucun \xC3\xA9v\xC3\xA9nement)","(Sem eventos)","(Nessun evento)"},
  {"Notificaciones","Notifications","Notifications","Notifica\xC3\xA7\xC3\xB5" "es","Notifiche"},
  {"(Sin notificaciones)","(No notifications)","(Aucune notification)","(Sem notifica\xC3\xA7\xC3\xB5" "es)","(Nessuna notifica)"},
  {"En construcci\xC3\xB3n","Coming soon","Bient\xC3\xB4t disponible","Em breve","Prossimamente"},
  {"Llega en el Milestone 2","Arrives in Milestone 2","Arrive au Milestone 2","Chega no Milestone 2","Arriva nel Milestone 2"},
  {"Volver","Back","Retour","Voltar","Indietro"},
  {"Bienvenido a","Welcome to","Bienvenue sur","Bem-vindo ao","Benvenuto in"},
};
static const char* t(int id){ return CH[id][LI()]; }

// Etiquetas de apps. Mismo orden que el enum IC_*.
static const char* APP[16][5] = {
  {"Reloj","Clock","Horloge","Rel\xC3\xB3gio","Orologio"},
  {"Galer\xC3\xAD" "a","Gallery","Galerie","Galeria","Galleria"},
  {"Multimedia","Media","Multim\xC3\xA9" "dia","Multim\xC3\xAD" "dia","Multimedia"},
  {"Almacenamiento","Storage","Stockage","Armazenamento","Archivi"},
  {"Modo PC","PC Mode","Mode PC","Modo PC","Modo PC"},
  {"Notas","Notes","Notes","Notas","Note"},
  {"Educaci\xC3\xB3n","Education","\xC3\x89" "ducation","Educa\xC3\xA7\xC3\xA3o","Istruzione"},
  {"Navegador","Browser","Navigateur","Navegador","Browser"},
  {"Code IDE","Code IDE","Code IDE","Code IDE","Code IDE"},
  {"Bienestar","Wellbeing","Bien-\xC3\xAatre","Bem-estar","Benessere"},
  {"Paint","Paint","Dessin","Paint","Disegno"},
  {"Juegos","Games","Jeux","Jogos","Giochi"},
  {"Ajustes","Settings","R\xC3\xA9glages","Ajustes","Impostazioni"},
  {"Calculadora","Calculator","Calculatrice","Calculadora","Calcolatrice"},
  {"Calendario","Calendar","Calendrier","Calend\xC3\xA1rio","Calendario"},
  {"C\xC3\xA1mara","Camera","Appareil","C\xC3\xA2mera","Fotocamera"},
};
static const char* appName(int id){ return APP[id][LI()]; }

// -------- Nombres de dias/meses --------
static const char* WD_FULL[5][7] = {
  {"Domingo","Lunes","Martes","Mi\xC3\xA9rcoles","Jueves","Viernes","S\xC3\xA1" "bado"},
  {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"},
  {"Dimanche","Lundi","Mardi","Mercredi","Jeudi","Vendredi","Samedi"},
  {"Domingo","Segunda","Ter\xC3\xA7" "a","Quarta","Quinta","Sexta","S\xC3\xA1" "bado"},
  {"Domenica","Luned\xC3\xAC","Marted\xC3\xAC","Mercoled\xC3\xAC","Gioved\xC3\xAC","Venerd\xC3\xAC","Sabato"},
};
static const char* WD_SHORT[5][7] = {
  {"dom","lun","mar","mi\xC3\xA9","jue","vie","s\xC3\xA1" "b"},
  {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"},
  {"dim","lun","mar","mer","jeu","ven","sam"},
  {"dom","seg","ter","qua","qui","sex","s\xC3\xA1" "b"},
  {"dom","lun","mar","mer","gio","ven","sab"},
};
static const char* MO_FULL[5][12] = {
  {"enero","febrero","marzo","abril","mayo","junio","julio","agosto","septiembre","octubre","noviembre","diciembre"},
  {"January","February","March","April","May","June","July","August","September","October","November","December"},
  {"janvier","f\xC3\xA9vrier","mars","avril","mai","juin","juillet","ao\xC3\xBBt","septembre","octobre","novembre","d\xC3\xA9" "cembre"},
  {"janeiro","fevereiro","mar\xC3\xA7" "o","abril","maio","junho","julho","agosto","setembro","outubro","novembro","dezembro"},
  {"gennaio","febbraio","marzo","aprile","maggio","giugno","luglio","agosto","settembre","ottobre","novembre","dicembre"},
};
static const char* MO_SHORT[5][12] = {
  {"ene","feb","mar","abr","may","jun","jul","ago","sep","oct","nov","dic"},
  {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"},
  {"jan","f\xC3\xA9v","mar","avr","mai","jui","jul","ao\xC3\xBB","sep","oct","nov","d\xC3\xA9" "c"},
  {"jan","fev","mar","abr","mai","jun","jul","ago","set","out","nov","dez"},
  {"gen","feb","mar","apr","mag","giu","lug","ago","set","ott","nov","dic"},
};

// -------- Reloj interno (sin NTP; se siembra a sab, 4 jul 13:23) --------
static int rtcY = 2026, rtcMo = 7, rtcD = 4, rtcWd = 6, rtcH = 13, rtcMin = 23;
static long          seedMinOfDay = 13 * 60 + 23;
static unsigned long clkBootMs = 0;
static long          clkLastMin = -1;

static int daysInMonth(int y, int m){
  static const int dm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if(m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
  return dm[m - 1];
}
static void clkSetDate(long addDays){
  int Y = 2026, Mo = 7, D = 4, Wd = 6;
  for(long i = 0; i < addDays; i++){
    D++; Wd = (Wd + 1) % 7;
    if(D > daysInMonth(Y, Mo)){ D = 1; Mo++; if(Mo > 12){ Mo = 1; Y++; } }
  }
  rtcY = Y; rtcMo = Mo; rtcD = D; rtcWd = Wd;
}
// devuelve true si cambio el minuto (para repintar el reloj)
static bool clkUpdate(){
  long mins = seedMinOfDay + (long)((millis() - clkBootMs) / 60000UL);
  long mod = mins % 1440; if(mod < 0) mod += 1440;
  int h = (int)(mod / 60), mi = (int)(mod % 60);
  if(mins == clkLastMin) return false;
  clkLastMin = mins;
  rtcH = h; rtcMin = mi;
  clkSetDate(mins / 1440);
  return true;
}
// Reloj "H:MM" pelado. Lo usa el RELOJ GIGANTE vectorial (bloqueo y app Reloj),
// que solo sabe dibujar digitos y ':' -> aqui NO puede entrar el AM/PM.
static void clkStr12(char* out, size_t n){
  if(g24h){ snprintf(out, n, "%d:%02d", rtcH, rtcMin); return; }
  int h12 = rtcH % 12; if(h12 == 0) h12 = 12;
  snprintf(out, n, "%d:%02d", h12, rtcMin);
}
// Reloj para las BARRAS DE ESTADO (fuente normal, sí admite letras).
// Antes todas las barras llamaban a clkStr12, y como en 12 h no se anadia
// AM/PM, el ajuste "Formato de hora" no cambiaba NADA visible entre las 00:00
// y las 12:59, y a partir de esa hora "1:23" podia ser la una de la tarde o de
// la madrugada. Ahora 24 h -> "13:23" y 12 h -> "1:23 PM".
// Reserva al menos 12 bytes: "12:34 PM" son 9 con el terminador.
static void clkStrBar(char* out, size_t n){
  if(g24h){ snprintf(out, n, "%d:%02d", rtcH, rtcMin); return; }
  int h12 = rtcH % 12; if(h12 == 0) h12 = 12;
  snprintf(out, n, "%d:%02d %s", h12, rtcMin, (rtcH < 12) ? "AM" : "PM");
}
// #############################################################
// ##  PANTALLAS + MAQUINA DE ESTADOS + ARRANQUE  (original)
// #############################################################

// ---- Declaraciones adelantadas ----
static void blitToFb(uint16_t* src);
static const char* resetReasonStr();
static void showBootBanner();
static void splashFrame(uint8_t a);
static void splashTick();
static void enterOobeLang();  static void renderOobeLang();  static void oobeLangTick();
static void enterOobeName();  static void drawKeyboard();    static void drawNameField();
static bool hitKey(int px, int py, int &code); static void oobeNameTick();
static void buildLongDate(char* out, size_t n); static void buildShortDate(char* out, size_t n);
static void renderLock(); static void showLock();
static void renderHome(); static void showHome(); static bool hitHomeIcon(int px, int py, int &id);
static void enterHome();  static void enterApp(int id); static void appTick();
static void swPushAndCapture(uint8_t id); static void activarMultitarea(); static void swTick();  // App Switcher
static void wmEnter(); static void wmTick(); static bool wmTouchWindows();  // WindowManager
static void wmTickHostedApps(); static void wmRunHostedApp(int idx, bool isEnter); static bool wmCloseIfHosted();  // Apps reales alojadas en ventanas
static bool sbTick(); static void sbDrawTabHandle(); static bool sbOwnsScreen(); static void sbDrawTabOnApp();  // Panel Edge (Sidebar Dock)
static void sbRenderOverlay(); static void sbPinnedLoad();                                                 // Panel Edge: composicion + carga de la lista anclada
static void sbDrawMinBubbles(); static bool sbHitMinBubble(int px, int py, int &idx);                       // burbujas de ventana minimizada
static void wmClampWin(int idx); static int wmCtrlCX(int wx, int ww, int i); static void wmCtrlAction(int idx, int b);  // barra de control de ventana (Imagen A)
static void lsuEnter(); static void lsuTick();             // Seguridad -> Bloqueo (PIN/Contraseña)
static void lsuStartVerify();                              // pedir PIN/contraseña al desbloquear
static void composeUnlock(int off); static void animateTo(int from, int to);
static void lockTick(); static void homeTick();
static bool handleiOSGestures();                          // gestos de la barra inferior (modo iOS)

// ---- Estado global ----
enum { ST_SPLASH = 0, ST_OOBE_LANG, ST_OOBE_NAME, ST_LOCK, ST_HOME, ST_APP, ST_SWITCHER, ST_WINMGR, ST_LOCKSETUP, ST_WIFI };
static int  gState = ST_SPLASH;
static unsigned long splashStart = 0;
static int  lockOff = 0, lastLockOff = -1;
static int  oobeSel = 0;
static int  gAppId  = 0;
static bool editMode = false;                                   // Modo Edicion del Home
static uint8_t homeOrder[12] = { 0,1,2,3,4,5,6,7,8,9,10,11 };   // app id por slot (reordenable)
static bool gMinChanged = false;   // lo pone loop(): true cuando cambia el minuto

// ---- Invalidacion de caches de pantalla ----
// homeBuf (y la cortina qsBuf, que se compone de el) son escritorios YA
// pintados. Si cambia un ajuste que altera su aspecto -idioma, formato de
// hora, Liquid Glass, estilo de iconos- hay que volver a componerlos, o al
// salir de Ajustes se ve el escritorio VIEJO hasta que cambie el minuto.
// Ese era el motivo de que "algo no coincidiera" tras tocar un ajuste.
static bool gHomeDirty = false;    // homeBuf no refleja los ajustes actuales
static bool qsDirty    = true;     // qsBuf debe recomponerse
// teclado
static int  keyN = 0;
static int  kX[48], kY[48], kW[48], kH[48], kCode[48];

static void blitToFb(uint16_t* src){ fbCopyBand(src, 0, SCR_H - 1); }  // respeta el recorte de ventana (ver fbCopyBand)

// ---------------- Banda forense de arranque ----------------
static const char* resetReasonStr(){
  switch(esp_reset_reason()){
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_SW:        return "SW";
    case ESP_RST_PANIC:     return "PANIC (crash)";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_BROWNOUT:  return "BROWNOUT (voltaje)";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    default:                return "OTRO";
  }
}
// Solo se muestra tras un reinicio ANORMAL (crash/watchdog/brownout),
// para que puedas leer el motivo sin monitor serie. En un encendido
// normal NO aparece: el arranque va directo al splash limpio.
static void showBootBanner(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, rgb565(0,0,0));
  drawTextC(SCR_W / 2, SCR_H / 2 - 24, "FlexOS Ultra", 3, rgb565(235,238,245));
  char b[72];
  snprintf(b, sizeof(b), "ultimo reinicio: %s", resetReasonStr());
  drawTextC(SCR_W / 2, SCR_H / 2 + 22, b, 1, rgb565(240,185,90));
  drawTextC(SCR_W / 2, SCR_H / 2 + 42, "P4 480x800 - modo offline", 1, rgb565(140,150,170));
  flxFlushAll();
  delay(2200);
}

// ---------------- SPLASH (fundido sobre NEGRO ABSOLUTO) ----------------
static void splashFrame(uint8_t a){
  int size = 6;
  int ty = SCR_H / 2 - 40;
  int ss = 2, sty = ty + size * 7 + 20;
  int y0 = ty - 10, y1 = sty + ss * 7 + 8;
  setBuf(fb);
  fillRect(0, y0, SCR_W, y1 - y0 + 1, rgb565(0,0,0));                  // banda negra
  drawTextCA(SCR_W / 2, ty, "FlexOS Ultra", size, rgb565(255,255,255), a);
  drawTextCA(SCR_W / 2, sty, "ESP32-P4", ss, rgb565(170,182,200), (uint8_t)(a * 7 / 10));
  flxFlush(y0, y1);
  // puntos de carga (estilo movil): uno se ilumina en secuencia
  int dy = SCR_H - 128, phase = (int)((millis() / 320) % 3);
  fillRect(0, dy - 7, SCR_W, 15, rgb565(0,0,0));
  for(int i = 0; i < 3; i++)
    fillCircleAA(SCR_W / 2 - 16 + i * 16, dy, 4.0f,
                 (i == phase) ? rgb565(255,255,255) : rgb565(70,74,82));
  flxFlush(dy - 8, dy + 8);
}
static void splashTick(){
  unsigned long e = millis() - splashStart;
  uint8_t a;
  if(e < 600)       a = (uint8_t)(e * 255 / 600);
  else if(e < 2000) a = 255;
  else if(e < 2600) a = (uint8_t)(255 - (e - 2000) * 255 / 600);
  else {
    if(!cfgOobeDone) enterOobeLang();
    else { renderHome(); renderLock(); showLock(); gState = ST_LOCK; lockOff = 0; lastLockOff = -1; }
    return;
  }
  splashFrame(a);
  delay(16);
}

// ---------------- OOBE: idioma ----------------
static void renderOobeLang(){
  drawWallpaper(fb, false); setBuf(fb);
  drawTextC(SCR_W / 2, 78, t(S_SELLANG), 3, rgb565(255,255,255));
  int rowH = 74, gap = 14, x = 44, w = SCR_W - 88, y0 = 158;
  for(int i = 0; i < NLANG; i++){
    int y = y0 + i * (rowH + gap);
    bool sel = (i == oobeSel);
    fillRoundRectA(x, y, w, rowH, 18, rgb565(255,255,255), sel ? 235 : 55);
    uint16_t tc = sel ? rgb565(28,28,38) : rgb565(255,255,255);
    const char* lbl = (i == 5) ? "Chinese" : LANG_ENDONYM[i];
    drawText(x + 28, y + rowH / 2 - 10, lbl, 3, tc);
    if(sel){
      int chx = x + w - 48, chy = y + rowH / 2;
      strokeSeg(chx - 8, chy, chx - 2, chy + 8, 2, rgb565(40,160,90));
      strokeSeg(chx - 2, chy + 8, chx + 12, chy - 10, 2, rgb565(40,160,90));
    }
  }
  int by = SCR_H - 96, bw = SCR_W - 88, bx = 44;
  fillRoundRect(bx, by, bw, 60, 30, rgb565(255,255,255));
  drawTextC(SCR_W / 2, by + 21, t(S_CONTINUE), 3, rgb565(40,80,200));
  flxFlushAll();
}
static void enterOobeLang(){ cfgLang = 0; oobeSel = 0; gState = ST_OOBE_LANG; renderOobeLang(); }
static void oobeLangTick(){
  if(!T.tap) return;
  int rowH = 74, gap = 14, x = 44, w = SCR_W - 88, y0 = 158;
  for(int i = 0; i < NLANG; i++){
    int y = y0 + i * (rowH + gap);
    if(T.x >= x && T.x <= x + w && T.y >= y && T.y <= y + rowH){
      oobeSel = i; cfgLang = i; renderOobeLang(); return;
    }
  }
  int by = SCR_H - 96, bw = SCR_W - 88, bx = 44;
  if(T.x >= bx && T.x <= bx + bw && T.y >= by && T.y <= by + 60){ enterOobeName(); return; }
}

// ---------------- OOBE: nombre (teclado QWERTY) ----------------
static void kbAdd(int x, int y, int w, int h, int code, const char* cap){
  kX[keyN] = x; kY[keyN] = y; kW[keyN] = w; kH[keyN] = h; kCode[keyN] = code; keyN++;
  fillRoundRect(x, y, w, h, 8, rgb565(250,250,252));
  if(cap) drawTextC(x + w / 2, y + h / 2 - 7, cap, 2, rgb565(28,28,38));
  else if(code == -2) fillRoundRect(x + w / 2 - 30, y + h / 2 - 2, 60, 4, 2, rgb565(90,90,100)); // barra espacio
}
static void drawKeyboard(){
  keyN = 0;
  const char* r1 = "QWERTYUIOP";
  const char* r2 = "ASDFGHJKL";
  const char* r3 = "ZXCVBNM";
  int kw = 40, kh = 52, g = 6, step = 60, kbTop = 544;
  { int n = 10; int sx = (SCR_W - (n * kw + (n - 1) * g)) / 2;
    for(int i = 0; i < n; i++){ char c[2] = { r1[i], 0 }; kbAdd(sx + i * (kw + g), kbTop, kw, kh, r1[i], c); } }
  { int n = 9; int sx = (SCR_W - (n * kw + (n - 1) * g)) / 2;
    for(int i = 0; i < n; i++){ char c[2] = { r2[i], 0 }; kbAdd(sx + i * (kw + g), kbTop + step, kw, kh, r2[i], c); } }
  { int n = 7; int backW = 2 * kw + g;
    int totalW = n * kw + (n - 1) * g + g + backW;
    int sx = (SCR_W - totalW) / 2;
    for(int i = 0; i < n; i++){ char c[2] = { r3[i], 0 }; kbAdd(sx + i * (kw + g), kbTop + 2 * step, kw, kh, r3[i], c); }
    kbAdd(sx + n * (kw + g), kbTop + 2 * step, backW, kh, -1, "<-"); }
  { int okW = 120, y = kbTop + 3 * step;
    int spX = 8 + kw, spW = SCR_W - spX - (okW + g) - 8;
    kbAdd(spX, y, spW, kh, -2, NULL);
    kbAdd(spX + spW + g, y, okW, kh, -3, "OK"); }
}
static void drawNameField(){
  int fx = 44, fy = 150, fw = SCR_W - 88, fh = 64;
  fillRoundRect(fx, fy, fw, fh, 16, rgb565(255,255,255));
  if(strlen(cfgName) == 0)
    drawText(fx + 20, fy + fh / 2 - 8, t(S_NAMEHINT), 2, rgb565(150,150,158));
  else {
    int ex = drawText(fx + 20, fy + fh / 2 - 10, cfgName, 3, rgb565(24,24,30));
    fillRect(ex + 2, fy + 16, 3, fh - 32, rgb565(55,120,240));
  }
  flxFlush(fy - 2, fy + fh + 2);
}
static void enterOobeName(){
  gState = ST_OOBE_NAME;
  cfgName[0] = 0;
  drawWallpaper(fb, false); setBuf(fb);
  drawTextC(SCR_W / 2, 70, t(S_YOURNAME), 3, rgb565(255,255,255));
  drawKeyboard();
  drawNameField();
  flxFlushAll();
}
static bool hitKey(int px, int py, int &code){
  for(int i = 0; i < keyN; i++)
    if(px >= kX[i] && px <= kX[i] + kW[i] && py >= kY[i] && py <= kY[i] + kH[i]){ code = kCode[i]; return true; }
  return false;
}
static void oobeNameTick(){
  if(!T.tap) return;
  int code;
  if(!hitKey(T.x, T.y, code)) return;
  int L = strlen(cfgName);
  if(code >= 32){ if(L < 20){ cfgName[L] = (char)code; cfgName[L + 1] = 0; drawNameField(); } }
  else if(code == -1){ if(L > 0){ cfgName[L - 1] = 0; drawNameField(); } }
  else if(code == -2){ if(L > 0 && L < 20){ cfgName[L] = ' '; cfgName[L + 1] = 0; drawNameField(); } }
  else if(code == -3){
    if(strlen(cfgName) == 0) strcpy(cfgName, "FlexOS Ultra");
    cfgSaveOobe();
    renderHome(); renderLock(); showLock();
    gState = ST_LOCK; lockOff = 0; lastLockOff = -1;
  }
}

// ---------------- Fechas localizadas ----------------
static void buildLongDate(char* out, size_t n){
  int li = LI();
  const char* wd = WD_FULL[li][rtcWd];
  const char* mo = MO_FULL[li][rtcMo - 1];
  switch(cfgLang){
    case 0: case 3: snprintf(out, n, "%s, %d de %s", wd, rtcD, mo); break; // ES, PT
    case 2: case 4: snprintf(out, n, "%s %d %s", wd, rtcD, mo); break;     // FR, IT
    default:        snprintf(out, n, "%s, %s %d", wd, mo, rtcD); break;    // EN / ZH
  }
}
static void buildShortDate(char* out, size_t n){
  int li = LI();
  snprintf(out, n, "%s, %d %s", WD_SHORT[li][rtcWd], rtcD, MO_SHORT[li][rtcMo - 1]);
}

// ---------------- LOCK ----------------
// Barra de gestos (estilo iOS): pildora fina centrada cerca del borde inferior.
// yBottom = borde inferior de referencia (normalmente SCR_H). col por defecto blanca.
static void drawHomeIndicator(int yBottom, uint8_t alpha, uint16_t col = rgb565(255,255,255)){
  int barW = 130, barH = 5, radius = 2;
  int x = (SCR_W - barW) / 2;
  int y = yBottom - 20 - barH;          // 20 px de margen desde el borde
  fillRoundRectA(x, y, barW, barH, radius, col, alpha);
}


// Glifos pequeños para las tarjetas de widgets del bloqueo. Copia minima e
// independiente de los de RI_* (Ajustes -> drawRowGlyph), que se definen
// mas abajo en el archivo; asi esta funcion no depende de nada definido
// despues de ella (evita sorpresas con el auto-prototipado de Arduino).
// kind: 0 = nube (clima), 1 = calendario, cualquier otro = campana (notif).
static void lockGlyph(int kind, int cx, int cy, uint16_t col){
  switch(kind){
    case 0:
      fillCircle(cx - 4, cy + 2, 5, col); fillCircle(cx + 4, cy + 2, 6, col);
      fillCircle(cx, cy - 2, 6, col); fillRect(cx - 8, cy + 2, 16, 5, col); break;
    case 1:
      drawRoundRect(cx - 9, cy - 8, 18, 17, 3, col); fillRect(cx - 9, cy - 8, 18, 5, col);
      fillCircle(cx - 4, cy + 2, 1, col); fillCircle(cx + 3, cy + 2, 1, col); break;
    default:   // campana (notificaciones) -- domo + cuerpo conico + reborde + badajo
      fillCircle(cx, cy - 6, 5, col);
      fillTriangle(cx - 8, cy + 4, cx + 8, cy + 4, cx, cy - 6, col);
      fillRect(cx - 9, cy + 3, 18, 3, col);
      fillCircle(cx, cy + 9, 2, col); break;
  }
}
// Tarjeta compacta para un widget opcional del bloqueo (clima/calendario/
// notificaciones). Estilo Vidrio u overlay plano segun uiGlass, igual que
// el resto de superficies de la app.
static void lockWidgetCard(int y, int kind, const char* title, const char* val, uint16_t accent){
  int x = 28, w = SCR_W - 56, h = 50;
  if(uiGlass){ glDrawSpec = false; drawLiquidGlassPanel(x, y, w, h, 16, rgb565(40,50,90), 0); glDrawSpec = true; }
  else fillRoundRectA(x, y, w, h, 16, rgb565(255,255,255), 45);
  lockGlyph(kind, x + 30, y + h / 2, accent);
  drawText(x + 54, y + 9, title, 2, rgb565(255,255,255));
  drawText(x + 54, y + 30, val, 1, rgb565(205,214,232));
}

static void renderLock(){
  drawWallpaper(lockBuf, false); setBuf(lockBuf);
  drawWifi(SCR_W - 66, 40, 12, rgb565(255,255,255));
  drawBattery(SCR_W - 46, 31, 30, 15, 82, rgb565(255,255,255));
  if(gLockWidgets & LW_CLOCK){
    if(uiGlass){ glDrawSpec = false; drawLiquidGlassPanel(28, 198, SCR_W - 56, 252, 28, rgb565(40,62,128), 0); glDrawSpec = true; }  // vidrio tras el reloj (destello aparte)
    char cs[8]; clkStr12(cs, sizeof(cs));
    drawBigClock(cs, SCR_W / 2, 242, 140, 18, rgb565(255,255,255));
    char ds[64]; buildLongDate(ds, sizeof(ds));
    drawTextC(SCR_W / 2, 242 + 140 + 36, ds, 3, rgb565(255,255,255));
  }
  // widgets opcionales (clima/calendario/notificaciones), apilados debajo
  // del reloj -- o mas arriba si el reloj esta desactivado, para no dejar
  // media pantalla vacia. El panel del reloj (y=198,h=252) termina en
  // y=450 -- el primer widget tiene que empezar DESPUES de eso, con margen
  // (antes empezaba en 448 y se solapaba 2px con el panel: el corte raro
  // que se ve en la foto justo debajo de la fecha).
  int wy = (gLockWidgets & LW_CLOCK) ? 462 : 200;
  if(gLockWidgets & LW_WEATHER){
    lockWidgetCard(wy, 0, t(S_WEATHER), "21C, Lima, Peru", rgb565(90,170,235));  // mock: sin fuente de datos real aun
    wy += 60;
  }
  if(gLockWidgets & LW_CAL){
    lockWidgetCard(wy, 1, appName(IC_CALEND), t(S_NOEVENTS), rgb565(235,110,90));  // mock: sin eventos reales aun
    wy += 60;
  }
  if(gLockWidgets & LW_NOTIF){
    const char* val = gNotifCount > 0 ? gNotifs[gNotifCount - 1].mod.name : t(S_NONOTIFS);  // dato real
    lockWidgetCard(wy, 2, t(S_NOTIFS), val, rgb565(230,180,90));
    wy += 60;
  }
  fillRoundRect(SCR_W / 2 - 70, SCR_H - 150, 140, 10, 5, rgb565(255,255,255));
  drawTextC(SCR_W / 2, SCR_H - 118, t(S_SWIPE), 2, rgb565(255,255,255));
  setBuf(fb);
}
static void showLock(){ blitToFb(lockBuf); flxFlushAll(); }

// ---------------- HOME ----------------
// Widgets del escritorio (clima, noticias, dock) en estilo Liquid Glass
// o plano segun uiGlass. tm = millis() para animar el brillo especular.
// Calcula el rectangulo de cada widget del Home segun gWidgetWide. Mutuamente
// excluyente: si uno esta "ancho" ocupa toda la fila (mismo alto de siempre,
// 120px) y el otro no se dibuja ese frame -- asi la rejilla de apps de abajo
// (a altura fija, gy0=212) nunca se tiene que mover. Compartida entre
// drawHomeWidgets() (para pintar) y edTick()/edRender() (para el asa de
// resize en Modo Edicion), asi no hay dos copias de esta geometria.
static void widgetLayout(int &cx, int &cy, int &cw, int &ch, bool &climaVis,
                          int &nx, int &ny, int &nw, int &nh, bool &newsVis){
  cy = ny = 72; ch = nh = 120;
  if(gWidgetWide & WW_CLIMA){        cx = 24; cw = 432; climaVis = true;  newsVis = false; nx = ny = nw = nh = 0; }
  else if(gWidgetWide & WW_NOTICIAS){ nx = 24; nw = 432; newsVis = true;  climaVis = false; cx = cy = cw = ch = 0; }
  else { cx = 24; cw = 208; nx = 24 + 208 + 16; nw = 208; climaVis = newsVis = true; }
}
static void drawHomeWidgets(uint32_t tm){
  int wx, wy, cw, ch, nx, ny, nw, nh; bool climaVis, newsVis;
  widgetLayout(wx, wy, cw, ch, climaVis, nx, ny, nw, nh, newsVis);
  uint16_t W = rgb565(255,255,255);
  glDrawSpec = false;                        // vidrio base (el destello se anima con glassSheen)
  if(climaVis){
    if(uiGlass) drawLiquidGlassPanel(wx, wy, cw, ch, 20, rgb565(30,72,150), tm);
    else fillRoundRect(wx, wy, cw, ch, 20, rgb565(28,58,120));
    drawText(wx + 16, wy + 16, t(S_WEATHER), 2, W);
    fillCircle(wx + cw - 42, wy + 34, 13, rgb565(250,205,60));
    fillCircle(wx + cw - 58, wy + 52, 11, rgb565(236,240,248));
    fillCircle(wx + cw - 42, wy + 55, 13, rgb565(236,240,248));
    fillRect(wx + cw - 58, wy + 53, 28, 10, rgb565(236,240,248));
    { int ex = drawText(wx + 16, wy + 48, "21", 4, W);
      drawCircle(ex + 7, wy + 52, 4, W); drawCircle(ex + 7, wy + 52, 3, W); }
    drawText(wx + 16, wy + ch - 22, "Lima, Peru", 1, rgb565(215,224,240));
  }
  if(newsVis){
    if(uiGlass) drawLiquidGlassPanel(nx, ny, nw, nh, 20, rgb565(205,212,226), tm);
    else fillRoundRect(nx, ny, nw, nh, 20, rgb565(240,242,246));
    uint16_t ntxt = uiGlass ? W : rgb565(40,40,50), nsub = uiGlass ? rgb565(238,240,248) : rgb565(120,120,132);
    drawText(nx + 16, ny + 16, t(S_NEWS), 2, ntxt);
    drawTextC(nx + nw / 2, ny + nh / 2, t(S_NONEWS), 1, nsub);
  }
  int dkx = 24, dky = SCR_H - 176, dkw = SCR_W - 48, dkh = 96;
  if(uiGlass) drawLiquidGlassPanel(dkx, dky, dkw, dkh, 28, rgb565(180,186,206), tm);
  else fillRoundRectA(dkx, dky, dkw, dkh, 28, W, 45);
  int dS = 64, inner = dkw - 32, dgap = (inner - 4 * dS) / 3;
  for(int i = 0; i < 4; i++){ int ix = dkx + 16 + i * (dS + dgap), iy = dky + (dkh - dS) / 2; drawAppIcon(12 + i, ix, iy, dS); }
  glDrawSpec = true;
}

static void renderHome(){
  drawWallpaper(homeBuf, true); setBuf(homeBuf);
  gHomeDirty = false;                      // homeBuf ya refleja los ajustes actuales
  qsDirty    = true;                       // la cortina se compone de homeBuf: invalidar su cache
  // barra de estado
  char cs[12]; clkStrBar(cs, sizeof(cs));
  drawText(20, 16, cs, 2, rgb565(255,255,255));
  char sd[48]; buildShortDate(sd, sizeof(sd));
  drawText(20, 40, sd, 1, rgb565(238,240,246));
  drawWifi(SCR_W - 66, 28, 11, rgb565(255,255,255));
  drawBattery(SCR_W - 46, 20, 30, 15, 82, rgb565(255,255,255));
  // widgets (clima, noticias) + dock: estilo Liquid Glass o plano
  drawHomeWidgets(millis());
  // rejilla de apps 4x3
  int S = 72, gx0 = 24, gy0 = 212, rowStep = 112;
  if(!editMode) for(int i = 0; i < 12; i++){                    // en Modo Edicion los pinta edRender()
    int c = i % 4, r = i / 4;
    int ix = gx0 + c * 120, iy = gy0 + r * rowStep;
    drawAppIcon(homeOrder[i], ix, iy, S);
    drawTextC(ix + S / 2, iy + S + 6, appName(homeOrder[i]), 2, rgb565(255,255,255));
  }
  // puntos de pagina
  int dotsY = gy0 + 2 * rowStep + S + 34;
  for(int i = 0; i < 3; i++)
    fillCircleA(SCR_W / 2 - 18 + i * 18, dotsY, 4, rgb565(255,255,255), i == 0 ? 255 : 110);
  // barra de navegacion: botones clasicos o barra de gestos (modo iOS)
  if(gNavMode == 0){
    int ny = SCR_H - 52; uint16_t nv = rgb565(255,255,255);
    int bx = SCR_W / 6;
    fillTriangle(bx - 10, ny + 8, bx + 8, ny - 2, bx + 8, ny + 18, nv);   // atras
    drawCircle(SCR_W / 2, ny + 8, 12, nv); drawCircle(SCR_W / 2, ny + 8, 11, nv); // inicio
    int rx = SCR_W * 5 / 6;
    drawRoundRect(rx - 11, ny - 3, 22, 22, 4, nv);                        // recientes
  } else {
    drawHomeIndicator(SCR_H, 220);                                        // barra de gestos
  }
  sbDrawTabHandle();                    // tirador del Panel Edge (horneado en homeBuf, ver seccion Sidebar Dock)
  setBuf(fb);
}
static void showHome(){ blitToFb(homeBuf); flxFlushAll(); }
static bool hitHomeIcon(int px, int py, int &id){
  int S = 72, gx0 = 24, gy0 = 212, rowStep = 112;
  for(int i = 0; i < 12; i++){
    int c = i % 4, r = i / 4;
    int ix = gx0 + c * 120, iy = gy0 + r * rowStep;
    if(px >= ix - 6 && px <= ix + S + 6 && py >= iy && py <= iy + S + 16){ id = homeOrder[i]; return true; }
  }
  int dkx = 24, dky = SCR_H - 176, dkw = SCR_W - 48, dkh = 96, dS = 64, inner = dkw - 32, dgap = (inner - 4 * dS) / 3;
  for(int i = 0; i < 4; i++){
    int ix = dkx + 16 + i * (dS + dgap), iy = dky + (dkh - dS) / 2;
    if(px >= ix && px <= ix + dS && py >= iy && py <= iy + dS){ id = 12 + i; return true; }
  }
  return false;
}

// ---------------- Desbloqueo con fisica (composicion) ----------------
static void composeUnlock(int off){
  if(off < 0) off = 0; if(off > SCR_H) off = SCR_H;
  for(int y = 0; y < SCR_H; y++){
    if(y < SCR_H - off)
      memcpy(fb + (size_t)y * SCR_W, lockBuf + (size_t)(y + off) * SCR_W, SCR_W * 2);
    else
      memcpy(fb + (size_t)y * SCR_W, homeBuf + (size_t)y * SCR_W, SCR_W * 2);
  }
  flxFlushAll();
}
static void animateTo(int from, int to){
  int steps = 14;
  for(int i = 1; i <= steps; i++){
    float p = (float)i / steps; p = 1 - (1 - p) * (1 - p);   // ease-out
    composeUnlock(from + (int)((to - from) * p));
    delay(14);
  }
  composeUnlock(to);
}
static void lockTick(){
  if(gLockType > 0){
    // Con bloqueo: deslizar arriba lleva DIRECTO a verificar (nunca se revela el escritorio)
    if(T.down && (T.startY - T.y) > 60){ lsuStartVerify(); return; }
    if(T.released && T.swipeUp){ lsuStartVerify(); return; }
    return;
  }
  if(T.down){
    int off = T.startY - T.y; if(off < 0) off = 0; if(off > SCR_H) off = SCR_H;
    if(off != lastLockOff){ composeUnlock(off); lastLockOff = off; }
    lockOff = off;
  } else if(T.released){
    if(lockOff > SCR_H / 3 || T.swipeUp){ animateTo(lockOff, SCR_H); enterHome(); }
    else { animateTo(lockOff, 0); lockOff = 0; lastLockOff = -1; showLock(); }
  }
}
// Anima el brillo especular de los paneles glass del escritorio:
// restaura las regiones desde el wallpaper limpio y repinta con millis().
// #############################################################
// ##  MODO EDICION del Home (long-press, jiggle, drag & drop)
// #############################################################
static float edCurX[12], edCurY[12];        // posiciones animadas (resorte)
static int   edDrag = -1, edHoverSlot = -1; // icono arrastrado / slot bajo el dedo
static float edDragX = 0, edDragY = 0;
static unsigned long edHoverMs = 0, edMs = 0;

static void homeOrderSave(){ prefs.begin("flexos", false); prefs.putBytes("hord", homeOrder, 12); prefs.end(); }
static void homeOrderLoad(){
  prefs.begin("flexos", true); size_t n = prefs.getBytes("hord", homeOrder, 12); prefs.end();
  if(n != 12){ for(int i = 0; i < 12; i++) homeOrder[i] = i; return; }
  bool seen[12] = { false };                 // valida: ids unicos 0..11 (por si prefs corruptas)
  for(int i = 0; i < 12; i++){ if(homeOrder[i] >= 12 || seen[homeOrder[i]]){ for(int j = 0; j < 12; j++) homeOrder[j] = j; return; } seen[homeOrder[i]] = true; }
}
static void edSlotXY(int slot, int &x, int &y){ int c = slot % 4, r = slot / 4; x = 24 + c * 120; y = 212 + r * 112; }
static int  edSlotAt(int px, int py){
  if(px < 24 || py < 212) return -1;
  int c = (px - 24) / 120, r = (py - 212) / 112;
  if(c < 0 || c > 3 || r < 0 || r > 2) return -1;
  int slot = r * 4 + c; return slot < 12 ? slot : -1;
}
static void edMove(int from, int to){        // reinserta el icono (desplaza los demas)
  if(from == to || from < 0 || to < 0 || from >= 12 || to >= 12) return;
  uint8_t v = homeOrder[from];
  if(from < to) for(int i = from; i < to; i++) homeOrder[i] = homeOrder[i + 1];
  else          for(int i = from; i > to; i--) homeOrder[i] = homeOrder[i - 1];
  homeOrder[to] = v;
}
// Asa de resize de los widgets del Home (Fase 1: alterna 2 tamanos --
// normal/ancho -- no arrastre continuo. Con solo 2 estados posibles, un
// toque en el asa es mas confiable que afinar un umbral de distancia de
// arrastre, y evita tocar la maquina de estados de edDrag/dwell de abajo).
static void drawWidgetHandle(int x, int y){
  fillRoundRect(x, y, 24, 24, 8, rgb565(255,255,255));
  strokeSegAA(x + 6, y + 16, x + 16, y + 6, 2.0f, rgb565(60,80,140));
  strokeSegAA(x + 6, y + 10, x + 10, y + 6, 2.0f, rgb565(60,80,140));
  strokeSegAA(x + 14, y + 18, x + 18, y + 14, 2.0f, rgb565(60,80,140));
}
static bool widgetHandleAt(int px, int py, int &which){
  int cx, cy, cw, ch, nx, ny, nw, nh; bool cv, nv;
  widgetLayout(cx, cy, cw, ch, cv, nx, ny, nw, nh, nv);
  if(cv && px >= cx + cw - 40 && px <= cx + cw - 4 && py >= cy + ch - 40 && py <= cy + ch - 4){ which = 0; return true; }
  if(nv && px >= nx + nw - 40 && px <= nx + nw - 4 && py >= ny + nh - 40 && py <= ny + nh - 4){ which = 1; return true; }
  return false;
}
static void widgetToggleSize(int which){
  if(which == 0) gWidgetWide = (gWidgetWide & WW_CLIMA)    ? 0 : WW_CLIMA;
  else           gWidgetWide = (gWidgetWide & WW_NOTICIAS) ? 0 : WW_NOTICIAS;
  cfgSavePrefs();
  renderHome();          // homeBuf tiene que reflejar el nuevo layout antes de que edRender() lo recomponga
}
static void edRender(){
  // Los iconos en Modo Edicion ahora usan el gIconStyle REAL (Vidrio si esta
  // activo en Ajustes) en vez de forzarse a Plano. Cada icono Vidrio pasa por
  // drawLiquidGlassPanel() -- un blur real, no gratis -- y aqui se dibujan
  // hasta 12 por frame. Para no trompicar el jiggle/arrastre en la P4, si el
  // estilo es Vidrio se limita el refresco de ESTA funcion a ~20 fps (50 ms).
  // Ojo: esto es un throttle LOCAL (reutiliza edMs, declarada mas arriba y
  // hasta ahora sin usar) -- a proposito NO se toca uiAnimMs, que es el
  // throttle compartido del sheen/qsPanel/ripple y no debe frenarse por esto.
  // En estilo Plano no hay throttle: se conserva el mismo refresco fluido de
  // siempre.
  if(gIconStyle == 1){
    unsigned long now = millis();
    if(now - edMs < 50) return;
    edMs = now;
  }
  setBuf(bbuf);
  for(int j = 120; j < 580; j++) memcpy(bbuf + (size_t)j * SCR_W, homeBuf + (size_t)j * SCR_W, SCR_W * 2);  // fondo (sin rejilla)
  uint32_t t = millis();
  { int cx, cy, cw, ch, nx, ny, nw, nh; bool cv, nv;                          // asas de resize de los widgets
    widgetLayout(cx, cy, cw, ch, cv, nx, ny, nw, nh, nv);
    if(cv) drawWidgetHandle(cx + cw - 30, cy + ch - 30);
    if(nv) drawWidgetHandle(nx + nw - 30, ny + nh - 30);
  }
  for(int i = 0; i < 12; i++){
    if(i == edDrag) continue;
    int tx, ty; edSlotXY(i, tx, ty);
    edCurX[i] += (tx - edCurX[i]) * 0.2f; edCurY[i] += (ty - edCurY[i]) * 0.2f;   // resorte
    float ph = i * 0.6f;
    int ox = (int)(2 * sinf(t * 0.02f + ph)), oy = (int)(2 * cosf(t * 0.017f + ph));  // temblor +-2px
    int s = 64, off = (72 - s) / 2;                                              // escala ~90%
    drawAppIcon(homeOrder[i], (int)edCurX[i] + off + ox, (int)edCurY[i] + off + oy, s);
  }
  if(edDrag >= 0){                                                              // icono arrastrado (translucido)
    int dx = (int)edDragX, dy = (int)edDragY, s = 72;
    if(uiGlass) drawLiquidGlassPanel(dx - 6, dy - 6, s + 12, s + 12, 16, rgb565(120,140,205), t);
    else fillRoundRectA(dx - 6, dy - 6, s + 12, s + 12, 16, rgb565(60,80,140), 150);
    drawAppIcon(homeOrder[edDrag], dx, dy, s);
  }
  drawTextC(SCR_W / 2, 176, "Arrastra los iconos - Inicio para salir", 1, rgb565(230,234,244));
  present(120, 580);
}
static void edEnter(){
  editMode = true;
  renderHome();                              // homeBuf sin rejilla (editMode salta el grid)
  for(int i = 0; i < 12; i++){ int x, y; edSlotXY(i, x, y); edCurX[i] = x; edCurY[i] = y; }
  edDrag = -1; edHoverSlot = -1;
}
static void edExit(){
  editMode = false; edDrag = -1;
  homeOrderSave();
  renderHome(); showHome();
}
static void edTick(){
  if(T.pressed){
    int which;
    if(widgetHandleAt(T.x, T.y, which)){ widgetToggleSize(which); edRender(); return; }
    edDrag = edSlotAt(T.x, T.y); edDragX = T.x - 36; edDragY = T.y - 36; edHoverSlot = -1; edRender(); return;
  }
  if(T.down && edDrag >= 0){
    edDragX = T.x - 36; if(edDragX < 8) edDragX = 8; if(edDragX > SCR_W - 80) edDragX = SCR_W - 80;
    edDragY = T.y - 36; if(edDragY < 140) edDragY = 140; if(edDragY > 500) edDragY = 500;
    int over = edSlotAt((int)edDragX + 36, (int)edDragY + 36);                  // slot bajo el centro
    if(over >= 0 && over != edDrag){
      if(over != edHoverSlot){ edHoverSlot = over; edHoverMs = millis(); }
      else if(millis() - edHoverMs > 400){ edMove(edDrag, over); edDrag = over; edHoverSlot = -1; }  // dwell 400ms
    } else edHoverSlot = -1;
    edRender(); return;
  }
  if(T.released){
    if(edDrag >= 0){ edDrag = -1; homeOrderSave(); edRender(); }                // soltar -> fija
    else if(T.tap) edExit();                                                    // toque en vacio/Inicio -> salir
    return;
  }
  // reposo: el jiggle continuo lo mueve uiTick()
}

static unsigned long homeGlassMs = 0;
static void animateHomeGlass(){
  if(!uiGlass || !bbuf) return;
  setBuf(bbuf);
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;   // recorte completo (evita lineas de borde)
  uint32_t t = millis();
  // BANDA COMPLETA A PROPOSITO (auto-reparacion). Restaurar solo las dos franjas
  // del sheen es mas barato, pero deja de repintar la zona central (filas
  // ~192..623): cualquier resto que otro render deje ahi se queda PEGADO para
  // siempre. Repintar la banda entera desde homeBuf en cada frame garantiza que
  // el escritorio se auto-repara en <40 ms pase lo que pase. La correccion vale
  // mas que el ahorro de ancho de banda.
  const int y0 = 64, y1 = 726;                           // banda continua (sin costuras internas)
  for(int j = y0; j <= y1; j++) memcpy(bbuf + (size_t)j * SCR_W, homeBuf + (size_t)j * SCR_W, SCR_W * 2);
  glassSheen(24, 72, 208, 120, 20, t);
  glassSheen(248, 72, 208, 120, 20, t);
  glassSheen(24, SCR_H - 176, SCR_W - 48, 96, 28, t);
  // El tirador (x[0..12], y[340..460]) queda a la IZQUIERDA y ENTRE las franjas
  // de sheen (widgets hasta y=191, dock desde y=624), asi que jamas se solapa
  // con ellas: permanece intacto en fb y no hace falta restaurarlo ni
  // re-estamparlo. (Antes se re-estampaba cada frame por precaucion; era un
  // dibujo redundante que ahora se elimina.)
  sbDrawTabHandle();          // el tirador vuelve a estamparse encima del vidrio animado
  present(y0, y1);
}

// ---- Destello de reflejo al tocar un icono (estilo "Vidrio") ----
// Circulo blanco que crece y se desvanece (~0.5 s) desde el punto exacto
// donde se toco. Se activa en homeTick() (T.pressed sobre un icono) y se
// anima aqui; se llama desde uiTick() solo mientras gState==ST_HOME, asi
// que si se abre otra pantalla (enterApp) el destello deja de dibujarse
// de inmediato aunque el temporizador no haya terminado.
static bool     gRippleActive = false;
static int      gRippleX = 0, gRippleY = 0;
static uint32_t gRippleStart = 0;
static const uint32_t RIPPLE_DUR_MS = 500;
static const int      RIPPLE_MAX_R  = 70;
static void animateIconRipple(){
  if(gIconStyle != 1 || !bbuf || !homeBuf){ gRippleActive = false; return; }
  setBuf(bbuf);
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;   // recorte completo
  // REPINTADO PARCIAL. El destello es un circulo de radio <= RIPPLE_MAX_R
  // centrado en el punto tocado, que NO se mueve durante la animacion. Antes se
  // recopiaba/volcaba la banda entera 64..726; ahora solo la franja vertical que
  // el circulo puede alcanzar (centro +- radio maximo, acotada a la pantalla).
  // El resto de fb ya es correcto. Salida byte-identica, mucho menos memcpy por
  // frame durante el ~medio segundo del efecto.
  int y0 = gRippleY - RIPPLE_MAX_R; if(y0 < 64)  y0 = 64;
  int y1 = gRippleY + RIPPLE_MAX_R; if(y1 > 726) y1 = 726;
  for(int j = y0; j <= y1; j++) memcpy(bbuf + (size_t)j * SCR_W, homeBuf + (size_t)j * SCR_W, SCR_W * 2);
  uint32_t e = millis() - gRippleStart;
  if(e < RIPPLE_DUR_MS){
    float   p = (float)e / RIPPLE_DUR_MS;                // 0..1
    int     r = (int)(RIPPLE_MAX_R * p);                  // crece
    uint8_t a = (uint8_t)(160 * (1.0f - p));              // se desvanece
    if(r > 0 && a > 0) fillCircleA(gRippleX, gRippleY, r, rgb565(255,255,255), a);
  } else {
    gRippleActive = false;                                // termino: este frame sale limpio (sin circulo)
  }
  sbDrawTabHandle();          // el ripple (circulo desde un icono de la col. izq.)
                              // puede alcanzar la franja del tirador: re-estamparlo
                              // lo mantiene por encima. Si su franja no cae dentro
                              // del volcado, no se presenta -> el tirador queda intacto.
  present(y0, y1);
}
static void homeTick(){
  if(sbTick()) return;                 // Panel Edge (arrastre activo / panel abierto / ventanas flotantes)
  if(editMode){ edTick(); return; }
  if(gNavMode == 1 && handleiOSGestures()) return;   // gestos iOS antes que los toques normales
  // destello Liquid Glass al posar el dedo sobre un icono (solo estilo "Vidrio")
  if(T.pressed && gIconStyle == 1){
    int rid;
    if(hitHomeIcon(T.x, T.y, rid)){ gRippleActive = true; gRippleX = T.x; gRippleY = T.y; gRippleStart = millis(); }
  }
  // pulsacion larga (>1000 ms sin mover) sobre un icono -> Modo Edicion
  if(T.down && edSlotAt(T.startX, T.startY) >= 0 && (millis() - T.downMs) > 1000
     && abs(T.x - T.startX) < 12 && abs(T.y - T.startY) < 12){
    int slot = edSlotAt(T.startX, T.startY);
    edEnter(); edDrag = slot; edDragX = T.x - 36; edDragY = T.y - 36; return;
  }
  if(T.tap){
    if(T.x > SCR_W * 2 / 3 && T.y > SCR_H - 72){ activarMultitarea(); return; }   // boton Recientes
    int id;
    if(hitHomeIcon(T.x, T.y, id)) enterApp(id);
  }
}

// #############################################################
// ##  FRAMEWORK DE VENTANAS / APPS  (Milestone 2 - base)
// #############################################################
// Cada app expone dos callbacks: enter() dibuja su contenido inicial en
// el AREA DE VENTANA, y tick() (opcional) actualiza por frame. El marco
// (barra de estado + cabecera con "atras" + barra de navegacion) y los
// gestos de cierre los gestiona el framework: las apps solo pintan su
// contenido. Para rellenar una app, se reemplaza su entrada en APP_REG.

#define WIN_TOP 96                  // borde superior del area de contenido
#define WIN_BOT (SCR_H - 64)        // borde inferior (encima de la nav bar)
#define WIN_BG  rgb565(18, 20, 28)  // fondo de ventana (oscuro, profesional)

typedef struct { void (*enter)(); void (*tick)(); uint8_t flags; } FlexApp;
#define APP_CUSTOM_HEADER 1   // la app pinta su propia cabecera (no la centrada)
#define APP_OWN_TOUCH     2   // la app gestiona TODOS sus toques (solo swipe-derecha cierra)
#define APP_NO_WINDOW     4   // NO se puede alojar en una ventana flotante/Split Screen (ver sbOpenFloating/sbTick):
                               // exige pantalla completa (cambia gLand o gState internamente -- Modo PC, Ajustes/Wi-Fi)
static void settingsEnter(); static void settingsTick();   // Ajustes (M3), abajo
static void wifiSettingsEnter(); static void wifiTick();    // Ajustes -> Red e Internet -> Wi-Fi, abajo
static void calcEnter(); static void calcTick();           // Calculadora (M2), abajo
static void pcEnter(); static void pcTick();               // Modo PC (M4), abajo
static void galEnter();                                    // Galeria (M2)
static void bienEnter(); static void bienTick();           // Bienestar (M2)
static void calEnter(); static void calTick();             // Calendario (M2)
static void vidEnter(); static void vidTick();             // Multimedia (esqueleto)
static void camEnter(); static void camTick();             // Camara (esqueleto)
static void noteEnter(); static void noteTick();           // Notas + teclado 4 capas
static void almEnter(); static void eduEnter(); static void navEnter();  // apps simples
static void ideEnter(); static void ideTick(); static void paintEnter(); static void paintTick();
static void geoEnter(); static void geoTick();   // Juegos -> Geo Dash (clon de Geometry Dash)

// Rect del icono en el escritorio (para animar la apertura desde el)
static void getIconRect(int id, int &rx, int &ry, int &rs){
  if(id < 12){
    int S = 72, gx0 = 24, gy0 = 212, rowStep = 112;
    rx = gx0 + (id % 4) * 120; ry = gy0 + (id / 4) * rowStep; rs = S;
  } else {
    int dkx = 24, dky = SCR_H - 176, dkw = SCR_W - 48, dkh = 96, dS = 64;
    int inner = dkw - 32, dgap = (inner - 4 * dS) / 3, i = id - 12;
    rx = dkx + 16 + i * (dS + dgap); ry = dky + (dkh - dS) / 2; rs = dS;
  }
}

// Marco estandar de ventana (barra de estado + cabecera + nav bar)
static void appDrawChrome(int id){
  setBuf(fb);
  uint16_t W = rgb565(255,255,255);
  char cs[12]; clkStrBar(cs, sizeof(cs));
  drawText(20, 16, cs, 2, W);
  drawWifi(SCR_W - 66, 28, 11, W);
  drawBattery(SCR_W - 46, 20, 30, 15, 82, W);
  if(gNavMode == 0){
    int ny = SCR_H - 52;
    fillTriangle(SCR_W / 6 - 10, ny + 8, SCR_W / 6 + 8, ny - 2, SCR_W / 6 + 8, ny + 18, W);
    drawCircle(SCR_W / 2, ny + 8, 12, W); drawCircle(SCR_W / 2, ny + 8, 11, W);
    drawRoundRect(SCR_W * 5 / 6 - 11, ny - 3, 22, 22, 4, W);
  } else {
    drawHomeIndicator(SCR_H, 180);
  }
  (void)id;
}
// Cabecera estandar (chevron "atras" + titulo centrado). Las apps con
// APP_CUSTOM_HEADER se saltan esto y pintan su propia cabecera.
static void appDrawHeader(int id){
  uint16_t W = rgb565(255,255,255);
  int hy = 50;
  strokeSegAA(30, hy + 16, 18, hy + 8, 2.4f, W);
  strokeSegAA(18, hy + 8, 30, hy, 2.4f, W);
  drawTextC(SCR_W / 2, hy + 3, appName(id), 3, W);
}

// ---- Contenido de apps ----
// (1) Placeholder para apps aun no implementadas (dentro de la ventana)
static void appPlaceholderEnter(){
  setBuf(fb);
  int cy = (WIN_TOP + WIN_BOT) / 2;
  if(uiGlass) drawLiquidGlassPanel(36, cy - 168, SCR_W - 72, 268, 26, rgb565(50,72,146), millis());  // modal glass
  drawAppIcon(gAppId, SCR_W / 2 - 44, cy - 130, 88);
  drawTextC(SCR_W / 2, cy + 6, t(S_SOON), 3, rgb565(232,234,240));
  drawTextC(SCR_W / 2, cy + 48, t(S_M2), 2, rgb565(140,150,166));
}
// (2) App REAL de referencia: Reloj (prueba el patron completo)
static void appRelojRender(){
  setBuf(fb);
  fillRect(0, WIN_TOP, SCR_W, WIN_BOT - WIN_TOP, WIN_BG);
  char cs[8]; clkStr12(cs, sizeof(cs));
  drawBigClock(cs, SCR_W / 2, WIN_TOP + 70, 140, 18, rgb565(255,255,255));
  char ds[64]; buildLongDate(ds, sizeof(ds));
  drawTextC(SCR_W / 2, WIN_TOP + 70 + 140 + 40, ds, 3, rgb565(200,210,230));
  drawTextC(SCR_W / 2, WIN_BOT - 54, "Reloj de FlexOS", 2, rgb565(120,132,152));
  flxFlush(WIN_TOP, WIN_BOT);
}
static void appRelojEnter(){ appRelojRender(); }
static void appRelojTick(){ if(gMinChanged) appRelojRender(); }

// ---- Registro de apps (indices = enum IC_*) ----
static FlexApp APP_REG[16] = {
  { appRelojEnter, appRelojTick, 0 },              // 0  Reloj  (REAL)
  { galEnter, NULL, 0 },                           // 1  Galeria (REAL, M2)
  { vidEnter, vidTick, APP_CUSTOM_HEADER | APP_OWN_TOUCH },  // 2  Multimedia (esqueleto)
  { almEnter, NULL, 0 },                           // 3  Almacenamiento (REAL)
  { pcEnter, pcTick, APP_CUSTOM_HEADER | APP_NO_WINDOW },  // 4  Modo PC (REAL, M4) -- usa render landscape (gLand): incompatible con ventana
  { noteEnter, noteTick, APP_CUSTOM_HEADER | APP_OWN_TOUCH },// 5  Notas + teclado (REAL)
  { eduEnter, NULL, 0 },                           // 6  Educacion (REAL)
  { navEnter, NULL, 0 },                           // 7  Navegador (REAL)
  { ideEnter, ideTick, 0 },                        // 8  Code IDE (REAL + Asistente de Hardware)
  { bienEnter, bienTick, 0 },                      // 9  Bienestar (REAL, M2)
  { paintEnter, paintTick, APP_CUSTOM_HEADER | APP_OWN_TOUCH },// 10 Paint (REAL)
  { geoEnter, geoTick, APP_OWN_TOUCH | APP_CUSTOM_HEADER }, // 11 Juegos (Geo Dash, REAL)
  { settingsEnter, settingsTick, APP_CUSTOM_HEADER | APP_NO_WINDOW },// 12 Ajustes (REAL, M3) -- Wi-Fi/PIN cambian gState a pantalla completa: incompatible con ventana
  { calcEnter, calcTick, 0 },                      // 13 Calculadora (REAL, M2)
  { calEnter, calTick, 0 },                        // 14 Calendario (REAL, M2)
  { camEnter, camTick, APP_CUSTOM_HEADER | APP_OWN_TOUCH },  // 15 Camara (esqueleto)
};

// Animacion de apertura/cierre: la ventana crece/encoge desde el icono
static void winRevealAnim(int id, bool opening){
  int ix, iy, is; getIconRect(id, ix, iy, is);
  uint16_t bg = (APP_REG[id].flags & APP_CUSTOM_HEADER) ? rgb565(244,247,251) : WIN_BG;
  uint32_t t0 = millis(), dur = 200;                 // 0.2 s exactos (basado en tiempo)
  for(;;){
    uint32_t e = millis() - t0; if(e > dur) e = dur;
    float tt = (float)e / dur;
    float p = opening ? tt : (1.0f - tt);
    p = 1 - (1 - p) * (1 - p) * (1 - p);              // ease-out cubico (mas suave)
    memcpy(bbuf, homeBuf, (size_t)SCR_W * SCR_H * 2); setBuf(bbuf);   // compone off-screen
    if(gAnimStyle == 1){                                // fundido: rect de pantalla completa, alpha creciente
      fillRectA(0, 0, SCR_W, SCR_H, bg, (uint8_t)(255 * p));
    } else if(gAnimStyle == 2){                          // deslizar: sube desde el borde inferior
      int y0 = (int)(SCR_H * (1 - p));
      fillRect(0, y0, SCR_W, SCR_H - y0, bg);
    } else {                                             // zoom (el de siempre): crece desde el icono
      int x0 = (int)(ix * (1 - p));
      int y0 = (int)(iy * (1 - p));
      int x1 = (int)((ix + is) * (1 - p) + SCR_W * p);
      int y1 = (int)((iy + is) * (1 - p) + SCR_H * p);
      int r  = (int)(18 * (1 - p));
      fillRoundRect(x0, y0, x1 - x0, y1 - y0, r, bg);
    }
    present(0, SCR_H - 1);                             // vuelca de una vez (sin parpadeo)
    if(e >= dur) break;
  }
}

static void appClose(){
  // Si esto se disparo desde el boton "atras" propio de una app alojada DENTRO
  // de una ventana flotante/Split Screen (ver wmRunHostedApp), NO se debe cerrar
  // la app a pantalla completa (que ni siquiera es la que esta en la ventana) --
  // se cierra unicamente esa ventana, y ya. wmCloseIfHosted() vive en la seccion
  // Window Manager (usa wmHostedWin, wmRemove()); un solo punto de verdad.
  if(wmCloseIfHosted()) return;
  swPushAndCapture(gAppId);        // guarda miniatura para el App Switcher
  // winRevealAnim compone la animacion SOBRE homeBuf: si Ajustes lo dejo sucio,
  // hay que recomponerlo ANTES, o la animacion de cierre encoge hacia el
  // escritorio viejo y este cambia de golpe al terminar.
  if(gHomeDirty) renderHome();
  winRevealAnim(gAppId, false);
  enterHome();
}
static void enterApp(int id){
  gAppId = id; gState = ST_APP;
  winRevealAnim(id, true);                        // crece desde el icono
  if(!(APP_REG[id].flags & APP_CUSTOM_HEADER)){   // apps normales: marco blanco
    appDrawChrome(id);
    appDrawHeader(id);
  }
  if(APP_REG[id].enter) APP_REG[id].enter();      // la app pinta su contenido (y su marco si es custom)
  flxFlushAll();
}
static void appTick(){
  if(gLand){ if(APP_REG[gAppId].tick) APP_REG[gAppId].tick(); return; }  // Modo PC: gestiona todo, el Panel Edge no aplica aqui
  if(sbTick()) return;                               // Panel Edge: misma prioridad que en homeTick()
  if(gNavMode == 1 && handleiOSGestures()) return;   // gestos iOS: swipe-arriba -> Home/multitarea
  // Cierre universal: tocar "atras" (nav; y cabecera en apps normales). Gesto swipe-to-close eliminado.
  bool back = false;  // antes: T.swipeRight -> deshabilitado a peticion, ya no cierra la app
  if(T.tap){
    int ny = SCR_H - 52;
    if(!(APP_REG[gAppId].flags & APP_OWN_TOUCH) && T.y >= ny - 10 && T.y <= ny + 22 && T.x < SCR_W / 3) back = true;               // nav atras
    if(!(APP_REG[gAppId].flags & APP_CUSTOM_HEADER) && T.y <= WIN_TOP && T.x < 72) back = true; // chevron
  }
  if(back){ appClose(); return; }
  if(APP_REG[gAppId].tick) APP_REG[gAppId].tick();
  sbDrawTabOnApp();   // reafirma el tirador sobre fb (las Apps no tienen un homeBuf donde hornearlo una vez)
}

static void enterHome(){
  gState = ST_HOME; lockOff = 0; lastLockOff = -1;
  // Antes se volcaba homeBuf tal cual. Si venias de Ajustes de cambiar idioma,
  // formato de hora, Liquid Glass o estilo de iconos, homeBuf seguia siendo el
  // ANTERIOR y el escritorio contradecia al ajuste que acababas de tocar,
  // hasta que el reloj cambiaba de minuto y lo repintaba por su cuenta.
  if(gHomeDirty) renderHome();
  blitToFb(homeBuf); flxFlushAll();
}

// Gestos de la barra inferior (modo iOS). Solo actua si el toque EMPEZO en los
// ultimos ~44 px de la pantalla (la zona de la barra). Al soltar:
//   · deslizamiento hacia arriba rapido (<300 ms) -> Home
//   · deslizamiento hacia arriba mantenido (>=300 ms) -> App Switcher
// Devuelve true si consumio el gesto (para que el tick no siga procesando).
static bool handleiOSGestures(){
  if(gNavMode != 1) return false;
  if(T.released && T.startY > SCR_H - 44){
    int dy = T.startY - T.y;                    // positivo si el dedo subio
    unsigned long dur = millis() - T.downMs;
    if(dy > 30){
      if(dur >= 300)            activarMultitarea();  // mantener -> multitarea
      else if(gState == ST_APP) appClose();           // rapido en app -> Home (guarda miniatura)
      else                      enterHome();          // rapido en Home -> refresca
      return true;
    }
  }
  return false;
}

// #############################################################
// ##  APP AJUSTES  (Milestone 3)  ·  dos paneles: categorias + detalle
// ##  (segun tu diseno: barra lateral izquierda + panel de detalle)
// #############################################################
#define SB_X       8
#define SB_W       222
#define DP_X       238
#define DP_W       (SCR_W - DP_X - 10)      // panel de detalle ~232
#define DLIST_TOP  112
#define DLIST_BOT  (SCR_H - 70)             // 730
// PAGE_BG y la paleta de abajo dependen de gDark (Ajustes -> Pantalla ->
// Modo de apariencia). Colores de MARCA/acento (los puntitos de colores de
// cada fila, iconos como Wi-Fi o el reloj) se dejan igual en ambos modos a
// proposito -- es el fondo y el texto lo que define si algo "se ve" claro
// u oscuro, y es lo unico que cambia aqui.
#define PAGE_BG        (gDark ? rgb565(18,20,28)     : rgb565(244,247,251))   // fondo de pagina
#define SET_CARD_BG    (gDark ? rgb565(34,38,50)     : rgb565(255,255,255))   // fondo de tarjeta (fila/sidebar)
#define SET_CARD_GLASS (gDark ? rgb565(48,54,72)     : rgb565(246,248,252))   // tinte de vidrio de la tarjeta
#define SET_TXT_HI     (gDark ? rgb565(240,242,248)  : rgb565(20,22,30))      // texto principal
#define SET_TXT_LO     (gDark ? rgb565(160,166,182)  : rgb565(120,126,140))   // texto secundario / valor
#define SET_TXT_MUTE   (gDark ? rgb565(120,126,142)  : rgb565(140,146,160))   // texto de ayuda / pie
#define SET_CHEV       (gDark ? rgb565(110,116,132)  : rgb565(160,165,178))   // chevron
#define SET_SIDE_SUB   (gDark ? rgb565(150,156,172)  : rgb565(140,145,158))   // subtitulo de la barra lateral
#define SET_NAVPILL    (gDark ? rgb565(200,204,214)  : rgb565(60,64,74))      // pildora de gestos (modo iOS)

static const char* SET_CAT[12] = {
  "General","Pantalla","Sonido","Red e Internet","Dispositivos",
  "Personalizaci\xC3\xB3n","Seguridad","Bater\xC3\xAD" "a","Almacenamiento",
  "Desarrollador","Sistema","Acerca de" };
static const char* SET_SUB[12] = {
  "Idioma, fecha, hora","Brillo, fondo, tema","Volumen, tonos","WiFi, Bluetooth",
  "GPIO, perifericos","Temas, iconos","Bloqueo, permisos","Ahorro de energia",
  "Interna, SD","Opciones dev","Sistema, logs","Version, creditos" };
static const char* SET_DESC[12] = {
  "Configura las opciones basicas del sistema.","Brillo, fondo de pantalla y modo oscuro.",
  "Volumen, tonos y notificaciones.","Conexiones de red (offline por ahora).",
  "GPIO, modulos y perifericos.","Temas, iconos y estilo del sistema.",
  "Bloqueo, permisos y privacidad.","Estado de la bateria y ahorro de energia.",
  "Memoria interna y tarjeta SD.","Herramientas y diagnostico de desarrollo.",
  "Informacion del sistema y registros.","Version, hardware y creditos de FlexOS." };

static int setSel = 0, setScroll = 0, setContentH = 0;

// Texto recortado por la derecha (evita que se salga del panel)
static int drawTextClip(int x, int y, const char* s, int size, uint16_t col, int maxRight){
  if(size <= 1){
    while(*s){
      if(x + 6 > maxRight) break;
      uint8_t b = (uint8_t)*s++; uint32_t cp;
      if(b < 0x80) cp = b;
      else if((b & 0xE0) == 0xC0){ uint8_t b1 = *s ? (uint8_t)*s++ : 0; cp = ((b & 0x1F) << 6) | (b1 & 0x3F); }
      else if((b & 0xF0) == 0xE0){ if(*s) s++; if(*s) s++; cp = 0x3F; }
      else cp = 0x3F;
      uint8_t base, acc; mapCP(cp, base, acc);
      drawGlyphSmooth(x, y, base, 1, col, 255);
      if(acc) drawAccent(x, y, 1, acc, col);
      x += 6;
    }
    return x;
  }
  float sc = fontSc(size), penx = x;
  while(*s){
    uint32_t cp = nextCP(&s);
    const FGlyph* g = &FG[fontIdx(cp)];
    if(penx + g->adv * sc > maxRight) break;
    drawGlyphScaled((int)(penx + g->bx * sc + 0.5f), y + (int)((g->topoff - FONT_CAPOFF) * sc + 0.5f), g, sc, col, 255);
    penx += g->adv * sc;
  }
  return (int)(penx + 0.5f);
}

static const char* langNameCur(){ return (cfgLang == 5) ? "Chinese" : LANG_ENDONYM[cfgLang]; }
static void settingsDateTimeStr(char* out, size_t n){
  int h12 = rtcH % 12; if(h12 == 0) h12 = 12;
  snprintf(out, n, "%02d/%02d/%04d  %d:%02d %s", rtcD, rtcMo, rtcY, h12, rtcMin, rtcH < 12 ? "AM" : "PM");
}
static void buildUptime(char* out, size_t n){
  unsigned long s = millis() / 1000UL;
  unsigned long d = s / 86400; s %= 86400;
  unsigned long h = s / 3600;  s %= 3600;
  unsigned long m = s / 60;
  if(d > 0)      snprintf(out, n, "%lud %luh %lum", d, h, m);
  else if(h > 0) snprintf(out, n, "%luh %lum", h, m);
  else           snprintf(out, n, "%lum", m);
}

// ---- iconos de fila (panel General) ----
enum { RI_GLOBE, RI_CAL, RI_CLOCK, RI_PIN, RI_REFRESH, RI_CLOUD, RI_RESET, RI_DOT };
static void drawRowGlyph(int k, int cx, int cy, uint16_t col){
  switch(k){
    case RI_GLOBE:
      drawCircle(cx, cy, 10, col); vLine(cx, cy - 10, 21, col); hLine(cx - 10, cy, 21, col);
      arcStroke(cx, cy, 5, 90, 270, 1, col); arcStroke(cx, cy, 5, -90, 90, 1, col); break;
    case RI_CAL:
      drawRoundRect(cx - 9, cy - 8, 18, 17, 3, col); fillRect(cx - 9, cy - 8, 18, 5, col);
      fillCircle(cx - 4, cy + 2, 1, col); fillCircle(cx + 3, cy + 2, 1, col); break;
    case RI_CLOCK:
      drawCircle(cx, cy, 10, col); strokeSegAA(cx, cy, cx, cy - 6, 1.4f, col);
      strokeSegAA(cx, cy, cx + 4, cy, 1.4f, col); break;
    case RI_PIN:
      fillCircle(cx, cy - 3, 7, col); fillTriangle(cx - 6, cy, cx + 6, cy, cx, cy + 9, col);
      fillCircle(cx, cy - 3, 3, rgb565(255,255,255)); break;
    case RI_REFRESH:
      arcStroke(cx, cy, 9, 30, 300, 2, col);
      fillTriangle(cx + 8, cy - 7, cx + 14, cy - 4, cx + 7, cy - 1, col); break;
    case RI_CLOUD:
      fillCircle(cx - 4, cy + 2, 5, col); fillCircle(cx + 4, cy + 2, 6, col);
      fillCircle(cx, cy - 2, 6, col); fillRect(cx - 8, cy + 2, 16, 5, col); break;
    case RI_RESET:
      arcStroke(cx, cy, 9, 40, 320, 2, col);
      fillTriangle(cx + 6, cy - 8, cx + 12, cy - 9, cx + 8, cy - 2, col); break;
    default: fillCircle(cx, cy, 4, col); break;
  }
}

// ---- iconos de categoria (barra lateral) ----
static void drawSetCatIcon(int cat, int x, int y, int S, uint16_t col){
  int cx = x + S / 2, cy = y + S / 2;
  switch(cat){
    case 0: // engranaje
      fillCircleAA(cx, cy, S * 0.18f, col);
      for(int k = 0; k < 8; k++){ float a = k * 0.7853982f;
        fillCircleAA(cx + cosf(a) * S * 0.30f, cy + sinf(a) * S * 0.30f, S * 0.065f, col); }
      fillCircleAA(cx, cy, S * 0.08f, rgb565(255,255,255)); break;
    case 1: // sol
      fillCircleAA(cx, cy, S * 0.15f, col);
      for(int k = 0; k < 8; k++){ float a = k * 0.7853982f;
        strokeSegAA(cx + cosf(a) * S * 0.24f, cy + sinf(a) * S * 0.24f,
                    cx + cosf(a) * S * 0.34f, cy + sinf(a) * S * 0.34f, 1.4f, col); } break;
    case 2: // altavoz
      fillRect((int)(cx - S * 0.22f), (int)(cy - S * 0.06f), (int)(S * 0.10f), (int)(S * 0.12f), col);
      fillTriangle((int)(cx - S * 0.12f), (int)(cy - S * 0.14f), (int)(cx - S * 0.12f), (int)(cy + S * 0.14f), (int)(cx + S * 0.02f), cy, col);
      arcStroke(cx - S * 0.02f, cy, S * 0.14f, -55, 55, 2, col); break;
    case 3: drawWifi(cx, (int)(cy + S * 0.14f), (int)(S * 0.28f), col); break;
    case 4: // cubo
      fillQuad(cx, (int)(cy - S * 0.22f), (int)(cx + S * 0.20f), (int)(cy - S * 0.10f), cx, (int)(cy + S * 0.02f), (int)(cx - S * 0.20f), (int)(cy - S * 0.10f), col);
      fillQuad(cx, (int)(cy + S * 0.02f), (int)(cx + S * 0.20f), (int)(cy - S * 0.10f), (int)(cx + S * 0.20f), (int)(cy + S * 0.14f), cx, (int)(cy + S * 0.26f), mix565(col, rgb565(0,0,0), 70));
      fillQuad(cx, (int)(cy + S * 0.02f), (int)(cx - S * 0.20f), (int)(cy - S * 0.10f), (int)(cx - S * 0.20f), (int)(cy + S * 0.14f), cx, (int)(cy + S * 0.26f), mix565(col, rgb565(0,0,0), 120)); break;
    case 5: // pincel
      strokeSegAA(cx - S * 0.16f, cy + S * 0.18f, cx + S * 0.10f, cy - S * 0.16f, 2.4f, col);
      fillCircleAA(cx - S * 0.18f, cy + S * 0.20f, S * 0.08f, col); break;
    case 6: // candado
      fillRoundRect((int)(cx - S * 0.16f), (int)(cy - S * 0.02f), (int)(S * 0.32f), (int)(S * 0.24f), 3, col);
      arcStroke(cx, cy - S * 0.02f, S * 0.12f, 180, 360, 2, col); break;
    case 7: drawBattery((int)(x + S * 0.24f), (int)(y + S * 0.34f), (int)(S * 0.5f), (int)(S * 0.3f), 80, col); break;
    case 8: // discos apilados
      for(int i = 0; i < 3; i++)
        fillRoundRect((int)(cx - S * 0.22f), (int)(cy - S * 0.16f + i * S * 0.14f), (int)(S * 0.44f), (int)(S * 0.09f), 2, col); break;
    case 9: // </>
      strokeSegAA(cx - S * 0.05f, cy - S * 0.14f, cx - S * 0.20f, cy, 2.0f, col);
      strokeSegAA(cx - S * 0.20f, cy, cx - S * 0.05f, cy + S * 0.14f, 2.0f, col);
      strokeSegAA(cx + S * 0.05f, cy - S * 0.14f, cx + S * 0.20f, cy, 2.0f, col);
      strokeSegAA(cx + S * 0.20f, cy, cx + S * 0.05f, cy + S * 0.14f, 2.0f, col); break;
    default: // info (i)
      drawCircle(cx, cy, (int)(S * 0.30f), col); drawCircle(cx, cy, (int)(S * 0.30f) - 1, col);
      fillCircle(cx, (int)(cy - S * 0.13f), 2, col);
      fillRect(cx - 1, (int)(cy - S * 0.03f), 3, (int)(S * 0.18f), col); break;
  }
}

// Registro de las filas realmente dibujadas en el panel de detalle de
// Ajustes (se resetea en settingsDetailContent() y lo llena cada
// setRowCard()). El tap-handler de settingsTick() lo consulta en vez de
// asumir que todas las filas miden 60px exactos y estan pegadas -- ese
// supuesto se rompia en cuanto habia un titulo de seccion o un texto de
// ayuda entre filas (bug: Pantalla->Bloqueo detectaba la fila equivocada).
#define SET_ROW_MAX 16
static int setRowY0[SET_ROW_MAX], setRowY1[SET_ROW_MAX], setRowN = 0;
// Tarjeta de fila (icono + titulo + valor + chevron). Devuelve la y siguiente.
static int setRowCard(int y, int rIcon, uint16_t iCol, const char* title, const char* val, bool chevron){
  int rh = 60, mr = DP_X + DP_W - 26;
  if(setRowN < SET_ROW_MAX){ setRowY0[setRowN] = y; setRowY1[setRowN] = y + rh; setRowN++; }  // registra el rango real de esta fila
  if(uiGlass) drawLiquidGlassPanel(DP_X, y, DP_W, rh - 8, 12, SET_CARD_GLASS, millis());  // tarjeta vidrio
  else fillRoundRect(DP_X, y, DP_W, rh - 8, 12, SET_CARD_BG);
  drawRowGlyph(rIcon, DP_X + 22, y + (rh - 8) / 2, iCol);
  drawTextClip(DP_X + 44, y + 10, title, 2, SET_TXT_HI, mr);
  if(val) drawTextClip(DP_X + 44, y + 32, val, 1, SET_TXT_LO, mr);
  if(chevron){ int chx = DP_X + DP_W - 18, chy = y + (rh - 8) / 2;
    strokeSegAA(chx - 3, chy - 6, chx + 3, chy, 2.0f, SET_CHEV);
    strokeSegAA(chx + 3, chy, chx - 3, chy + 6, 2.0f, SET_CHEV); }
  return y + rh;
}
static int drawInfoLine(int y, const char* label, const char* val){
  drawText(DP_X, y, label, 1, SET_TXT_LO);
  drawTextR(SCR_W - 12, y, val, 1, SET_TXT_HI);
  return y + 24;
}
static int drawDeviceInfo(int y){
  drawText(DP_X, y, "Dispositivo", 2, SET_TXT_HI); y += 30;
  char v[48];
  y = drawInfoLine(y, "Nombre", cfgName);
  y = drawInfoLine(y, "Modelo", "ESP32-P4 DevKit");
  y = drawInfoLine(y, "Version", "FlexOS Ultra 1.0");
  buildUptime(v, sizeof(v)); y = drawInfoLine(y, "Actividad", v);
  snprintf(v, sizeof(v), "%u KB libre", (unsigned)(esp_get_free_heap_size() / 1024)); y = drawInfoLine(y, "RAM", v);
  snprintf(v, sizeof(v), "%u / %u MB", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1048576),
           (unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1048576)); y = drawInfoLine(y, "PSRAM", v);
  y = drawInfoLine(y, "Flash", "16 MB");
  y = drawInfoLine(y, "CPU", "RISC-V dual 360 MHz");
  return y;
}

// Contenido del panel de detalle (con clip vertical activo)
static void settingsDetailContent(int cat){
  setRowN = 0;                       // reinicia el registro de filas (ver setRowCard)
  int base = DLIST_TOP - setScroll;
  int y = base + 6;
  char v[48];
  uint16_t ic = rgb565(70,120,225);
  if(cat == 0){
    y = setRowCard(y, RI_GLOBE,   rgb565(60,140,235), "Idioma", langNameCur(), true);
    settingsDateTimeStr(v, sizeof(v));
    y = setRowCard(y, RI_CAL,     rgb565(235,90,90),  "Fecha y hora", v, true);
    y = setRowCard(y, RI_CLOCK,   rgb565(90,120,230), "Formato de hora", g24h ? "24 horas" : "12 horas", true);
    y = setRowCard(y, RI_PIN,     rgb565(230,80,80),  "Zona horaria", "GMT-05:00 Lima", true);
    y = setRowCard(y, RI_REFRESH, rgb565(60,160,230), "Actualizaciones", "Proximamente", true);
    y = setRowCard(y, RI_CLOUD,   rgb565(120,160,230),"Copias de seguridad", "Proximamente", true);
    y = setRowCard(y, RI_RESET,   rgb565(220,80,80),  "Restablecer", "Opciones de fabrica", true);
    y += 12; y = drawDeviceInfo(y);
  } else if(cat == 11){
    y = drawDeviceInfo(y);
    y += 8; drawText(DP_X, y, "FlexOS Ultra - desde cero", 1, SET_TXT_MUTE); y += 22;
    drawText(DP_X, y, "para ESP32-P4 - 2026", 1, SET_TXT_MUTE); y += 22;
  } else if(cat == 1){                     // Pantalla (funcional)
    char bv[16]; snprintf(bv, sizeof(bv), "%d%%", gBright);
    y = setRowCard(y, RI_DOT, rgb565(240,170,50), "Brillo", bv, true);
    y = setRowCard(y, RI_DOT, rgb565(90,110,235), "Estilo", uiGlass ? "Liquid Glass" : "Plano", true);
    y = setRowCard(y, RI_DOT, gDark ? rgb565(120,130,220) : rgb565(240,170,50), "Modo de apariencia", gDark ? "Oscuro" : "Claro", true);
    y = setRowCard(y, RI_DOT, rgb565(100,180,240), "Barra de navegacion", gNavMode == 0 ? "Botones" : "Gestos iOS", true);
    y += 8; drawText(DP_X, y, "Toca una fila para cambiarla", 1, SET_TXT_MUTE); y += 24;
    y += 8; drawText(DP_X, y, "Bloqueo", 2, SET_TXT_HI); y += 30;
    y = setRowCard(y, RI_CLOCK, rgb565(90,120,230), "Reloj grande", (gLockWidgets & LW_CLOCK) ? "Activado" : "Desactivado", true);
    y = setRowCard(y, RI_CLOUD, rgb565(90,170,235), "Clima", (gLockWidgets & LW_WEATHER) ? "Activado" : "Desactivado", true);
    y = setRowCard(y, RI_CAL, rgb565(235,110,90), "Calendario", (gLockWidgets & LW_CAL) ? "Activado" : "Desactivado", true);
    y = setRowCard(y, RI_DOT, rgb565(230,180,90), "Notificaciones", (gLockWidgets & LW_NOTIF) ? "Activado" : "Desactivado", true);
    y += 8; drawText(DP_X, y, "Elige los widgets de la pantalla de bloqueo", 1, SET_TXT_MUTE); y += 24;
  } else if(cat == 5){                      // Personalizacion (funcional)
    y = setRowCard(y, RI_DOT, rgb565(90,110,235), "Personalizar UI", uiGlass ? "Liquid Glass" : "Plano", true);
    y = setRowCard(y, RI_DOT, rgb565(150,90,210), "Iconos", gIconStyle == 1 ? "Vidrio" : "Plano", true);
    const char* av = gAnimStyle == 1 ? "Fundido" : gAnimStyle == 2 ? "Deslizar" : "Zoom";
    y = setRowCard(y, RI_DOT, rgb565(90,200,160), "Transiciones", av, true);
    y += 8; drawText(DP_X, y, "Toca una fila para cambiar su estilo", 1, SET_TXT_MUTE); y += 24;
  } else if(cat == 6){                      // Seguridad (funcional)
    const char* lt = gLockType == 1 ? "PIN configurado" : gLockType == 2 ? "Contrase\xC3\xB1" "a configurada" : "Deslizar";
    y = setRowCard(y, RI_DOT, rgb565(220,120,120), "Bloqueo", lt, true);
    y += 8; drawText(DP_X, y, "Toca para configurar PIN o contrase\xC3\xB1" "a", 1, SET_TXT_MUTE); y += 24;
  } else {
    const char* rt[3] = {0,0,0}; const char* rv[3] = {0,0,0}; int rn = 0;
    switch(cat){
      case 2: rt[0]="Volumen";rv[0]="70%"; rt[1]="Tono";rv[1]="Predeterminado"; rn=2; break;
      case 3:
        rt[0]="Wi-Fi";
#if FLEXOS_ENABLE_WIFI
        rv[0] = gNetOnline ? "Conectado" : "Desactivado";
#else
        rv[0] = "No disponible";
#endif
        rt[1]="Bluetooth";rv[1]="No disponible"; rn=2; break;
      case 4: rt[0]="GPIO";rv[0]="Configurable"; rt[1]="Perifericos";rv[1]="Ninguno"; rn=2; break;
      case 6: rt[0]="Bloqueo";rv[0]="Deslizar"; rt[1]="PIN";rv[1]="No configurado"; rn=2; break;
      case 7: rt[0]="Nivel";rv[0]="82%"; rt[1]="Ahorro";rv[1]="Desactivado"; rn=2; break;
      case 8: rt[0]="Interna";rv[0]="6.2 / 16 GB"; rt[1]="Tarjeta SD";rv[1]="No insertada"; rn=2; break;
      case 9: rt[0]="Depuracion";rv[0]="En pantalla"; rt[1]="Banda reinicio";rv[1]="Solo crash"; rn=2; break;
      case 10: rt[0]="Version";rv[0]="FlexOS 1.0"; rt[1]="Logs";rv[1]="Puerto serie"; rn=2; break;
      default: rn=0; break;
    }
    for(int i = 0; i < rn; i++) y = setRowCard(y, RI_DOT, ic, rt[i], rv[i], true);
    y += 8; drawText(DP_X, y, "Mas opciones proximamente", 1, SET_TXT_MUTE); y += 24;
  }
  setContentH = (y - base) + 10;
}

static void settingsDrawSidebar(){
  drawText(16, 54, "Ajustes", 4, SET_TXT_HI);
  const uint16_t accent[12] = {
    rgb565(70,120,235), rgb565(240,170,50), rgb565(70,120,235), rgb565(60,150,235),
    rgb565(80,180,120), rgb565(90,110,235), rgb565(90,95,110), rgb565(80,190,110),
    rgb565(150,90,210), rgb565(70,75,90), rgb565(70,120,235), rgb565(70,120,235) };
  int cardH = 48, gap = 4, sy = 100;
  for(int i = 0; i < 12; i++){
    int y = sy + i * (cardH + gap);
    bool sel = (i == setSel);
    fillRoundRect(SB_X, y, SB_W, cardH, 12, sel ? rgb565(58,120,235) : SET_CARD_BG);
    uint16_t tcol = sel ? rgb565(255,255,255) : SET_TXT_HI;
    uint16_t scol = sel ? rgb565(222,234,255) : SET_SIDE_SUB;
    uint16_t icol = sel ? rgb565(255,255,255) : accent[i];
    drawSetCatIcon(i, SB_X + 10, y + (cardH - 28) / 2, 28, icol);
    drawTextClip(SB_X + 48, y + 9,  SET_CAT[i], 2, tcol, SB_X + SB_W - 6);
    drawTextClip(SB_X + 48, y + 30, SET_SUB[i], 1, scol, SB_X + SB_W - 6);
  }
}
static void settingsDrawDetailHead(){
  drawText(DP_X, 54, SET_CAT[setSel], 4, SET_TXT_HI);
  drawTextClip(DP_X, 96, SET_DESC[setSel], 1, SET_TXT_LO, SCR_W - 8);
}
static void settingsDrawChromeDark(){
  uint16_t D = SET_TXT_HI;
  char cs[12]; clkStrBar(cs, sizeof(cs));
  drawText(16, 16, cs, 2, D);
  char sd[40]; buildShortDate(sd, sizeof(sd));
  drawText(16 + textW(cs, 2) + 14, 20, sd, 1, D);
  drawWifi(SCR_W - 66, 28, 11, D);
  drawBattery(SCR_W - 46, 20, 30, 15, 82, D);
  if(gNavMode == 0){
    int ny = SCR_H - 52;
    fillTriangle(SCR_W / 6 - 10, ny + 8, SCR_W / 6 + 8, ny - 2, SCR_W / 6 + 8, ny + 18, D);
    drawCircle(SCR_W / 2, ny + 8, 12, D); drawCircle(SCR_W / 2, ny + 8, 11, D);
    drawRoundRect(SCR_W * 5 / 6 - 11, ny - 3, 22, 22, 4, D);
  } else {
    drawHomeIndicator(SCR_H, 210, SET_NAVPILL);   // pildora de gestos, a juego con el fondo de la pagina
  }
}
static void settingsRenderDetailOnly(){
  setBuf(fb);
  fillRect(DP_X - 4, DLIST_TOP, SCR_W - (DP_X - 4), DLIST_BOT - DLIST_TOP, PAGE_BG);
  int c0 = gClipY0, c1 = gClipY1;
  gClipY0 = DLIST_TOP; gClipY1 = DLIST_BOT - 1;
  settingsDetailContent(setSel);
  gClipY0 = c0; gClipY1 = c1;
  flxFlush(DLIST_TOP, DLIST_BOT);
}
static void settingsRender(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, PAGE_BG);
  settingsDrawChromeDark();
  settingsDrawSidebar();
  settingsDrawDetailHead();
  int c0 = gClipY0, c1 = gClipY1;
  gClipY0 = DLIST_TOP; gClipY1 = DLIST_BOT - 1;
  settingsDetailContent(setSel);
  gClipY0 = c0; gClipY1 = c1;
  flxFlushAll();
}
static void settingsEnter(){ setSel = 0; setScroll = 0; settingsRender(); }
// Accion al tocar una fila del panel de detalle (ajustes funcionales)
// Todo ajuste que cambie el ASPECTO del escritorio marca gHomeDirty: homeBuf es
// una cache ya pintada y hay que recomponerla antes de volver a mostrarla.
// (gBright no lo hace: es PWM del backlight, no repinta nada.)
static void settingsRowAction(int cat, int idx){
  if(cat == 0){
    if(idx == 0){ cfgLang = (cfgLang + 1) % 6; cfgSavePrefs(); gHomeDirty = true; settingsRender(); }         // idioma (cicla)
    else if(idx == 2){ g24h = !g24h; cfgSavePrefs(); gHomeDirty = true; settingsRenderDetailOnly(); }         // formato 12/24
  } else if(cat == 1){
    if(idx == 0){ gBright += 25; if(gBright > 100) gBright = 25; setBacklight(gBright); cfgSavePrefs(); settingsRenderDetailOnly(); }  // brillo real
    else if(idx == 1){ uiGlass = !uiGlass; cfgSavePrefs(); gHomeDirty = true; settingsRenderDetailOnly(); }    // estilo Liquid Glass
    else if(idx == 2){ gDark = !gDark; cfgSavePrefs(); settingsRender(); }  // Modo de apariencia: oscuro <-> claro (aplica ya, sin reiniciar)
    else if(idx == 3){ gNavMode = (gNavMode == 0) ? 1 : 0; cfgSavePrefs(); gHomeDirty = true; settingsRender(); } // barra: botones <-> gestos (redibuja tambien la barra inferior)
    else if(idx == 4){ gLockWidgets ^= LW_CLOCK;   cfgSavePrefs(); settingsRenderDetailOnly(); }  // Bloqueo: reloj grande
    else if(idx == 5){ gLockWidgets ^= LW_WEATHER; cfgSavePrefs(); settingsRenderDetailOnly(); }  // Bloqueo: clima
    else if(idx == 6){ gLockWidgets ^= LW_CAL;     cfgSavePrefs(); settingsRenderDetailOnly(); }  // Bloqueo: calendario
    else if(idx == 7){ gLockWidgets ^= LW_NOTIF;   cfgSavePrefs(); settingsRenderDetailOnly(); }  // Bloqueo: notificaciones
  } else if(cat == 5){
    if(idx == 0){ uiGlass = !uiGlass; cfgSavePrefs(); gHomeDirty = true; settingsRenderDetailOnly(); }         // Personalizar UI
    else if(idx == 1){ gIconStyle = (gIconStyle == 0) ? 1 : 0; cfgSavePrefs(); gHomeDirty = true; settingsRenderDetailOnly(); }  // estilo de iconos: Plano <-> Vidrio
    else if(idx == 2){ gAnimStyle = (gAnimStyle + 1) % 3; cfgSavePrefs(); settingsRenderDetailOnly(); }  // transiciones: zoom -> fundido -> deslizar -> zoom
  } else if(cat == 3){
    if(idx == 0) wifiSettingsEnter();                                                       // Red e Internet -> Wi-Fi
  } else if(cat == 6){
    if(idx == 0) lsuEnter();                                                                 // Seguridad -> Bloqueo (PIN/Contraseña)
  }
}
static void settingsTick(){
  // Seleccion de categoria (tap en la barra lateral)
  if(T.tap && T.x < SB_X + SB_W && T.y >= 100 && T.y < 100 + 12 * 52){
    int idx = (T.y - 100) / 52;
    if(idx >= 0 && idx < 12){
      if(idx != setSel){ setSel = idx; setScroll = 0; settingsRender(); }
      return;
    }
  }
  // Tap en una fila del panel de detalle -> accion funcional
  if(T.tap && T.x > DP_X && T.y >= DLIST_TOP && T.y <= DLIST_BOT){
    for(int i = 0; i < setRowN; i++) if(T.y >= setRowY0[i] && T.y < setRowY1[i]){ settingsRowAction(setSel, i); return; }
  }
  // Scroll del panel de detalle (deslizar arriba/abajo sobre el)
  int vp = DLIST_BOT - DLIST_TOP, maxS = setContentH - vp; if(maxS < 0) maxS = 0;
  if(T.swipeUp && T.x > DP_X - 24){ setScroll += 130; if(setScroll > maxS) setScroll = maxS; settingsRenderDetailOnly(); }
  else if(T.swipeDown && T.x > DP_X - 24){ setScroll -= 130; if(setScroll < 0) setScroll = 0; settingsRenderDetailOnly(); }
}

// #############################################################
// ##  APP CALCULADORA  (Milestone 2)  ·  app normal (marco estandar)
// #############################################################
static char   calcDisp[24] = "0";
static double calcAcc = 0;
static char   calcOp = 0;          // 0,'+','-','x','/'
static bool   calcFresh = true;    // el proximo digito empieza entrada nueva

static const char* CALC_LBL[5][4] = {
  {"C","+/-","%","/"}, {"7","8","9","x"}, {"4","5","6","-"},
  {"1","2","3","+"},   {"0",".","=","DEL"} };

static bool calcErr = false;       // el display muestra "Error" (division por cero / desbordamiento)

// Finito sin depender de macros de math.h (a prueba de .ino):
// NaN falla (v == v); los infinitos fallan el rango.
static inline bool calcFinite(double v){ return (v == v) && (v > -1.0e308) && (v < 1.0e308); }

static void calcFmt(double v){
  if(!calcFinite(v)){ snprintf(calcDisp, sizeof(calcDisp), "Error"); calcErr = true; return; }
  calcErr = false;
  if(v == 0) v = 0;                 // evita "-0"
  snprintf(calcDisp, sizeof(calcDisp), "%g", v);
}
static double calcCompute(double a, double b, char op){
  // Antes 5/0 devolvia 0 en silencio: una respuesta falsa presentada como buena.
  // Ahora se propaga un NaN y calcFmt lo convierte en "Error".
  switch(op){ case '+': return a + b; case '-': return a - b;
              case 'x': return a * b; case '/': return (b != 0) ? (a / b) : (double)NAN; }
  return b;
}
static void calcKey(char k){
  // Con "Error" en pantalla, el estado numerico no vale: cualquier entrada de
  // numero limpia primero. Los operadores se ignoran (no hay operando valido).
  if(calcErr){
    if(k == 'c' || (k >= '0' && k <= '9') || k == '.'){
      strcpy(calcDisp, "0"); calcAcc = 0; calcOp = 0; calcFresh = true; calcErr = false;
      if(k == 'c') return;
    } else return;
  }
  int L = strlen(calcDisp);
  if(k >= '0' && k <= '9'){
    if(calcFresh || (L == 1 && calcDisp[0] == '0')){ calcDisp[0] = k; calcDisp[1] = 0; }
    else if(L < 16){ calcDisp[L] = k; calcDisp[L + 1] = 0; }
    calcFresh = false;
  } else if(k == '.'){
    if(calcFresh){ strcpy(calcDisp, "0."); calcFresh = false; }
    else if(!strchr(calcDisp, '.') && L < 15){ calcDisp[L] = '.'; calcDisp[L + 1] = 0; }
  } else if(k == 'c'){ strcpy(calcDisp, "0"); calcAcc = 0; calcOp = 0; calcFresh = true; }
  else if(k == '\b'){ if(!calcFresh && L > 0){ calcDisp[L - 1] = 0; if(calcDisp[0] == 0) strcpy(calcDisp, "0"); } }
  else if(k == 'n'){
    if(strcmp(calcDisp, "0") != 0){
      if(calcDisp[0] == '-') memmove(calcDisp, calcDisp + 1, strlen(calcDisp));
      else { memmove(calcDisp + 1, calcDisp, strlen(calcDisp) + 1); calcDisp[0] = '-'; }
    }
  } else if(k == '%'){ calcFmt(atof(calcDisp) / 100.0); calcFresh = true; }
  else if(k == '+' || k == '-' || k == 'x' || k == '/'){
    double cur = atof(calcDisp);
    calcAcc = (calcOp && !calcFresh) ? calcCompute(calcAcc, cur, calcOp) : cur;
    calcOp = k; calcFresh = true; calcFmt(calcAcc);
  } else if(k == '='){
    if(calcOp){ calcAcc = calcCompute(calcAcc, atof(calcDisp), calcOp); calcFmt(calcAcc); calcOp = 0; }
    calcFresh = true;
  }
}
static void calcKeyFromLabel(const char* t){
  char k;
  if(!strcmp(t, "C")) k = 'c';
  else if(!strcmp(t, "+/-")) k = 'n';
  else if(!strcmp(t, "DEL")) k = '\b';
  else k = t[0];   // digitos, '.', '%', '/', 'x', '-', '+', '='
  calcKey(k);
}
static void calcGrid(int &gx, int &gy, int &bw, int &bh, int &gap){
  gap = 12; gx = 16; gy = WIN_TOP + 120; bw = (SCR_W - 32 - 3 * gap) / 4; bh = 86;
}
static int calcKeyY0 = 0, calcKeyY1 = 0;
static void calcRender(){                              // compone base (teclas glass SIN sheen) en lockBuf, luego vuelca
  setBuf(lockBuf);
  fillRect(0, WIN_TOP, SCR_W, WIN_BOT - WIN_TOP, WIN_BG);
  fillRoundRect(16, WIN_TOP + 8, SCR_W - 32, 92, 14, rgb565(28,31,40));
  drawTextR(SCR_W - 34, WIN_TOP + 44, calcDisp, 5, rgb565(255,255,255));
  int gx, gy, bw, bh, gap; calcGrid(gx, gy, bw, bh, gap);
  calcKeyY0 = gy - 4; calcKeyY1 = gy + 5 * (bh + gap) + 4; if(calcKeyY1 > SCR_H) calcKeyY1 = SCR_H;
  for(int r = 0; r < 5; r++) for(int c = 0; c < 4; c++){
    int x = gx + c * (bw + gap), y = gy + r * (bh + gap);
    const char* tl = CALC_LBL[r][c];
    uint16_t bg;
    if(c == 3 || (r == 4 && c == 2)) bg = rgb565(245,150,40);
    else if(r == 0)                  bg = rgb565(70,74,86);
    else                             bg = rgb565(92,96,110);
    if(uiGlass){ glDrawSpec = false; drawLiquidGlassPanel(x, y, bw, bh, 14, bg, 0); glDrawSpec = true; }
    else fillRoundRect(x, y, bw, bh, 14, bg);
    int fs = (strlen(tl) > 1) ? 3 : 4;
    drawTextC(x + bw / 2, y + bh / 2 - (fs == 4 ? 15 : 11), tl, fs, rgb565(248,248,252));
  }
  setBuf(fb);
  fbCopyBand(lockBuf, WIN_TOP, WIN_BOT - 1);   // antes: memcpy de filas COMPLETAS -> dentro de una ventana borraba el escritorio
  flxFlush(WIN_TOP, WIN_BOT);
}
static void calcAnimSheen(){                           // reflejo movil sobre las teclas (uiTick)
  if(!uiGlass || calcKeyY1 <= calcKeyY0) return;
  setBuf(bbuf);
  for(int j = calcKeyY0; j < calcKeyY1; j++) memcpy(bbuf + (size_t)j * SCR_W, lockBuf + (size_t)j * SCR_W, SCR_W * 2);
  int gx, gy, bw, bh, gap; calcGrid(gx, gy, bw, bh, gap);
  uint32_t t = millis();
  for(int r = 0; r < 5; r++) for(int c = 0; c < 4; c++){ int x = gx + c * (bw + gap), y = gy + r * (bh + gap); glassSheen(x, y, bw, bh, 14, t); }
  present(calcKeyY0, calcKeyY1);
}
static void calcRenderDisplay(){                       // solo el display (al teclear) -> responsivo
  // Mismo bug que tenia sbDrawTabOnApp(): dos dibujos SEPARADOS directo en
  // fb (fillRoundRect + drawTextR) antes de un solo flxFlush. Como esta
  // funcion se llama en CADA tecla tocada, era el candidato mas probable
  // para el "parpadeo al hacer algo en una app" -- se nota mucho mas que
  // el tirador porque pasa constantemente, no cada 120ms. Se compone en
  // lockBuf (igual que calcRender() ya hace) y se vuelca de una pasada.
  int y0 = WIN_TOP + 8, y1 = WIN_TOP + 102;
  setBuf(lockBuf);
  fillRoundRect(16, WIN_TOP + 8, SCR_W - 32, 92, 14, rgb565(28,31,40));
  drawTextR(SCR_W - 34, WIN_TOP + 44, calcDisp, 5, rgb565(255,255,255));
  fbCopyBand(lockBuf, y0, y1);                 // idem: recortado a la ventana si la app esta alojada
  setBuf(fb);
  flxFlush(y0, y1);
}
static void calcEnter(){ strcpy(calcDisp, "0"); calcAcc = 0; calcOp = 0; calcFresh = true; calcRender(); }
static void calcTick(){
  if(!T.tap) return;
  int gx, gy, bw, bh, gap; calcGrid(gx, gy, bw, bh, gap);
  for(int r = 0; r < 5; r++) for(int c = 0; c < 4; c++){
    int x = gx + c * (bw + gap), y = gy + r * (bh + gap);
    if(T.x >= x && T.x <= x + bw && T.y >= y && T.y <= y + bh){
      calcKeyFromLabel(CALC_LBL[r][c]); calcRenderDisplay(); return;
    }
  }
}

// #############################################################
// ##  APPS M2: Calendario, Bienestar, Galeria (marco estandar)
// #############################################################

// ---- Calendario: vista de mes con el dia de hoy resaltado ----
static void calRender(){
  setBuf(fb);
  fillRect(0, WIN_TOP, SCR_W, WIN_BOT - WIN_TOP, WIN_BG);
  char hdr[40]; snprintf(hdr, sizeof(hdr), "%s %d", MO_FULL[LI()][rtcMo - 1], rtcY);
  drawTextC(SCR_W / 2, WIN_TOP + 14, hdr, 3, rgb565(255,255,255));
  const char* wd[7] = { "D", "L", "M", "M", "J", "V", "S" };
  int gx = 16, gy = WIN_TOP + 74, cw = (SCR_W - 32) / 7, ch = 66;
  for(int i = 0; i < 7; i++) drawTextC(gx + i * cw + cw / 2, gy, wd[i], 2, rgb565(150,160,190));
  int fw = ((rtcWd - (rtcD - 1)) % 7 + 7) % 7;      // dia de la semana del dia 1
  int dim = daysInMonth(rtcY, rtcMo);
  for(int d = 1; d <= dim; d++){
    int cell = fw + d - 1, r = cell / 7, c = cell % 7;
    int cx = gx + c * cw + cw / 2, cy = gy + 40 + r * ch;
    if(d == rtcD) fillCircle(cx, cy + 10, 20, rgb565(58,120,235));
    char ds[4]; snprintf(ds, sizeof(ds), "%d", d);
    drawTextC(cx, cy, ds, 3, d == rtcD ? rgb565(255,255,255) : rgb565(220,224,235));
  }
  flxFlush(WIN_TOP, WIN_BOT);
}
static void calEnter(){ calRender(); }
static void calTick(){ if(gMinChanged) calRender(); }

// ---- Bienestar: tiempo encendido + uso de memoria ----
static void bienRender(){
  setBuf(fb);
  fillRect(0, WIN_TOP, SCR_W, WIN_BOT - WIN_TOP, WIN_BG);
  drawTextC(SCR_W / 2, WIN_TOP + 14, "Bienestar del equipo", 3, rgb565(255,255,255));
  char up[40]; buildUptime(up, sizeof(up));
  drawTextC(SCR_W / 2, WIN_TOP + 74, up, 5, rgb565(120,200,255));
  drawTextC(SCR_W / 2, WIN_TOP + 138, "tiempo encendido", 2, rgb565(150,160,185));
  int bx = 40, bw = SCR_W - 80;
  size_t pf = heap_caps_get_free_size(MALLOC_CAP_SPIRAM), pt = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  int usedp = pt > 0 ? (int)(100 - (uint64_t)pf * 100 / pt) : 0;
  int y1 = WIN_TOP + 200; char v[40];
  drawText(bx, y1, "PSRAM", 2, rgb565(220,224,235));
  snprintf(v, sizeof(v), "%d%% en uso", usedp); drawTextR(bx + bw, y1, v, 2, rgb565(180,188,205));
  fillRoundRect(bx, y1 + 30, bw, 18, 9, rgb565(48,52,66));
  fillRoundRect(bx, y1 + 30, bw * usedp / 100, 18, 9, rgb565(90,180,120));
  int y2 = y1 + 90;
  drawText(bx, y2, "RAM interna libre", 2, rgb565(220,224,235));
  snprintf(v, sizeof(v), "%u KB", (unsigned)(esp_get_free_heap_size() / 1024));
  drawTextR(bx + bw, y2, v, 2, rgb565(180,188,205));
  drawTextC(SCR_W / 2, WIN_BOT - 64, "Recuerda descansar la vista", 2, rgb565(150,160,185));
  flxFlush(WIN_TOP, WIN_BOT);
}
static void bienEnter(){ bienRender(); }
static void bienTick(){ if(gMinChanged) bienRender(); }

// ---- Galeria: cuadricula de miniaturas (mini-paisajes generados) ----
static void galRender(){
  setBuf(fb);
  fillRect(0, WIN_TOP, SCR_W, WIN_BOT - WIN_TOP, WIN_BG);
  drawTextC(SCR_W / 2, WIN_TOP + 14, "Galer\xC3\xAD" "a", 3, rgb565(255,255,255));
  int gx = 16, gap = 12, tw = (SCR_W - 32 - 2 * gap) / 3, th = 120, gy = WIN_TOP + 56;
  uint16_t sky[6] = { rgb565(120,180,235), rgb565(250,200,120), rgb565(180,150,220),
                      rgb565(120,210,190), rgb565(240,160,170), rgb565(150,170,235) };
  uint16_t sun[6] = { rgb565(255,240,150), rgb565(255,120,80), rgb565(255,230,180),
                      rgb565(255,255,210), rgb565(255,210,120), rgb565(255,245,190) };
  uint16_t mtn[6] = { rgb565(60,110,90), rgb565(120,80,60), rgb565(80,70,110),
                      rgb565(50,110,110), rgb565(120,70,90), rgb565(70,90,130) };
  for(int i = 0; i < 12; i++){
    int c = i % 3, r = i / 3, x = gx + c * (tw + gap), y = gy + r * (th + gap), k = i % 6;
    fillRoundRect(x, y, tw, th, 10, sky[k]);
    fillCircle(x + tw - 26, y + 24, 12, sun[k]);
    fillTriangle(x + 6, y + th - 6, x + tw / 2 - 8, y + th - 48, x + tw - 30, y + th - 6, mtn[k]);
    fillTriangle(x + tw / 2 - 4, y + th - 6, x + tw - 26, y + th - 42, x + tw - 6, y + th - 6, mix565(mtn[k], rgb565(0,0,0), 60));
  }
  flxFlush(WIN_TOP, WIN_BOT);
}
static void galEnter(){ galRender(); }

// #############################################################
// ##  MODO PC  (Milestone 4)  ·  escritorio LANDSCAPE 800x480
// ##  Se dibuja rotado 90 sobre el panel (gLand). Barra de tareas,
// ##  menu Inicio y ventanas flotantes. Segun tu imagen 4.
// #############################################################
static PWin pwins[4];
static bool pcStartOpen = false;

static void pcExit(){ gLand = false; pcStartOpen = false; appClose(); }
static void pcOpen(int app){
  for(int i = 0; i < 4; i++) if(pwins[i].open && pwins[i].app == app) return;
  int n = 0; for(int j = 0; j < 4; j++) if(pwins[j].open) n++;
  for(int i = 0; i < 4; i++) if(!pwins[i].open){
    pwins[i].open = true; pwins[i].app = app;
    pwins[i].x = 90 + n * 40; pwins[i].y = 46 + n * 30; pwins[i].w = 380; pwins[i].h = 250;
    return;
  }
}
// Panel glass para LANDSCAPE (Modo PC): drawLiquidGlassPanel escribe en coords
// portrait y no rota, asi que aqui uso primitivas rotacion-aware (fillRoundRectA
// + reflejo con pxA). Translucido + sheen + borde, sin blur.
static void pcGlassPanel(int x, int y, int w, int h, int rad, uint16_t tint, uint32_t t){
  // Le faltaba el mismo resguardo que ya tienen fillRoundRect/fillRoundRectA/
  // drawLiquidGlassPanelEx: sin esto, rad llegaba SIN recortar hasta glInset()
  // y el bucle de reflejo de abajo -- con una ventana lo bastante chica (o un
  // rad grande a proposito) el inset calculado podia superar la mitad de w/h,
  // dejando manchas en las esquinas en vez de la curva limpia.
  if(rad < 0) rad = 0;
  if(2 * rad > w) rad = w / 2;
  if(2 * rad > h) rad = h / 2;
  fillRoundRectA(x, y, w, h, rad, tint, 205);
  int off = (int)((t / 22) % (uint32_t)(w + h)) - h / 2;
  for(int j = 3; j < h - 3; j += 2){ int ins = glInset(j, h, rad), sxp = off - j + w / 2;
    for(int i = -16; i <= 16; i++){ int xi = sxp + i; if(xi > ins + 1 && xi < w - ins - 1){ int a = 16 - (i < 0 ? -i : i); if(a > 0) pxA(x + xi, y + j, rgb565(255,255,255), (uint8_t)a); } }
  }
  drawRoundRect(x, y, w, h, rad, rgb565(210,220,240));
}
static void pcDrawWindow(PWin* wn){
  int x = wn->x, y = wn->y, w = wn->w, h = wn->h;
  fillRoundRect(x + 4, y + 6, w, h, 12, rgb565(8,10,18));       // sombra
  if(uiGlass) pcGlassPanel(x, y, w, h, 12, rgb565(232,238,250), millis());   // cuerpo glass
  else fillRoundRect(x, y, w, h, 12, rgb565(246,247,251));                    // cuerpo plano
  fillRoundRect(x, y, w, 32, 12, rgb565(45,90,200));            // barra de titulo
  fillRect(x, y + 18, w, 14, rgb565(45,90,200));
  drawText(x + 14, y + 8, appName(wn->app), 2, rgb565(255,255,255));
  int cxb = x + w - 22, cyb = y + 16;                           // boton cerrar (X)
  strokeSegAA(cxb - 6, cyb - 6, cxb + 6, cyb + 6, 1.8f, rgb565(255,255,255));
  strokeSegAA(cxb - 6, cyb + 6, cxb + 6, cyb - 6, 1.8f, rgb565(255,255,255));
  drawText(x + 18, y + 52, appName(wn->app), 4, rgb565(28,32,44));
  drawText(x + 18, y + 96, "Ventana de Modo PC", 2, rgb565(110,116,132));
  drawAppIcon(wn->app, x + w - 92, y + h - 92, 72);
}
static void pcRender(){
  gLand = true; setBuf(fb);
  gClipY0 = 0; gClipY1 = SCR_H - 1;
  for(int ly = 0; ly < LH; ly++)                               // fondo (degradado azul)
    hLine(0, ly, LW, mix565(rgb565(16,34,80), rgb565(44,96,168), (uint8_t)(ly * 255 / (LH - 1))));
  const int dico[3] = { IC_ALMACEN, IC_NAV, IC_CAMARA };       // iconos de escritorio
  const char* dicn[3] = { "Equipo", "Navegador", "Camara" };
  for(int i = 0; i < 3; i++){
    int ix = 30, iy = 26 + i * 104;
    drawAppIcon(dico[i], ix, iy, 64);
    drawTextC(ix + 32, iy + 70, dicn[i], 2, rgb565(238,242,255));
  }
  for(int i = 0; i < 4; i++) if(pwins[i].open) pcDrawWindow(&pwins[i]);
  int tb = LH - 46;                                            // barra de tareas
  if(uiGlass) pcGlassPanel(0, tb, LW, 46, 0, rgb565(30,36,54), millis());
  else fillRect(0, tb, LW, 46, rgb565(24,28,42));
  int sx = LW / 2 - 140;
  fillRoundRect(sx, tb + 9, 28, 28, 6, rgb565(45,90,200));     // boton Inicio (logo 2x2)
  fillRect(sx + 8, tb + 15, 6, 6, rgb565(255,255,255)); fillRect(sx + 16, tb + 15, 6, 6, rgb565(255,255,255));
  fillRect(sx + 8, tb + 23, 6, 6, rgb565(255,255,255)); fillRect(sx + 16, tb + 23, 6, 6, rgb565(255,255,255));
  const int pin[4] = { IC_NAV, IC_NOTAS, IC_CALC, IC_AJUSTES };
  for(int i = 0; i < 4; i++) drawAppIcon(pin[i], sx + 44 + i * 44, tb + 7, 32);
  fillRoundRect(LW - 240, tb + 9, 84, 28, 8, rgb565(180,60,60));  // boton Salir
  drawTextC(LW - 198, tb + 15, "Salir", 2, rgb565(255,255,255));
  char cs[12]; clkStrBar(cs, sizeof(cs)); drawTextR(LW - 16, tb + 7, cs, 2, rgb565(240,244,255));
  char sd[40]; buildShortDate(sd, sizeof(sd)); drawTextR(LW - 16, tb + 28, sd, 1, rgb565(200,206,224));
  if(pcStartOpen){                                             // menu Inicio
    int mx = sx - 6, my = tb - 208, mw = 250, mh = 198;
    if(uiGlass) pcGlassPanel(mx, my, mw, mh, 12, rgb565(40,46,66), millis());
    else fillRoundRect(mx, my, mw, mh, 12, rgb565(34,38,54));
    drawText(mx + 16, my + 12, "Aplicaciones", 2, rgb565(220,224,238));
    const int mi[5] = { IC_NAV, IC_NOTAS, IC_CALC, IC_AJUSTES, IC_ALMACEN };
    for(int i = 0; i < 5; i++){
      int iy = my + 42 + i * 28;
      drawAppIcon(mi[i], mx + 14, iy - 4, 22);
      drawText(mx + 44, iy, appName(mi[i]), 2, rgb565(234,238,248));
    }
    drawText(mx + 16, my + mh - 24, "Salir de Modo PC", 2, rgb565(255,170,170));
  }
  flxFlushAll();
}
static void pcEnter(){
  for(int i = 0; i < 4; i++) pwins[i].open = false;
  pcStartOpen = false;
  pcOpen(IC_MODOPC);           // ventana de bienvenida
  pcRender();
}
static void pcTick(){
  if(gMinChanged) pcRender();
  if(!T.tap) return;
  int lx = T.y, ly = (SCR_W - 1) - T.x;      // fisico -> landscape
  int tb = LH - 46, sx = LW / 2 - 140;
  if(pcStartOpen){
    int mx = sx - 6, my = tb - 208, mw = 250, mh = 198;
    if(lx >= mx && lx <= mx + mw && ly >= my && ly <= my + mh){
      if(ly >= my + mh - 30){ pcExit(); return; }
      const int mi[5] = { IC_NAV, IC_NOTAS, IC_CALC, IC_AJUSTES, IC_ALMACEN };
      for(int i = 0; i < 5; i++){ int iy = my + 42 + i * 28; if(ly >= iy - 6 && ly < iy + 22){ pcOpen(mi[i]); pcStartOpen = false; pcRender(); return; } }
      return;
    }
    pcStartOpen = false; pcRender(); return;
  }
  if(lx >= LW - 240 && lx <= LW - 156 && ly >= tb + 9 && ly <= tb + 37){ pcExit(); return; }   // Salir
  if(lx >= sx && lx <= sx + 28 && ly >= tb + 9 && ly <= tb + 37){ pcStartOpen = true; pcRender(); return; }
  const int pin[4] = { IC_NAV, IC_NOTAS, IC_CALC, IC_AJUSTES };
  for(int i = 0; i < 4; i++){ int ix = sx + 44 + i * 44; if(lx >= ix && lx <= ix + 32 && ly >= tb + 7 && ly <= tb + 39){ pcOpen(pin[i]); pcRender(); return; } }
  const int dico[3] = { IC_ALMACEN, IC_NAV, IC_CAMARA };
  for(int i = 0; i < 3; i++){ int ix = 30, iy = 26 + i * 104; if(lx >= ix && lx <= ix + 64 && ly >= iy && ly <= iy + 64){ pcOpen(dico[i]); pcRender(); return; } }
  for(int i = 3; i >= 0; i--) if(pwins[i].open){
    int cxb = pwins[i].x + pwins[i].w - 22, cyb = pwins[i].y + 16;
    if(lx >= cxb - 11 && lx <= cxb + 11 && ly >= cyb - 11 && ly <= cyb + 11){ pwins[i].open = false; pcRender(); return; }
  }
}

// #############################################################
// ##  PANEL RAPIDO (cortina deslizable estilo Android/iOS)
// ##  Arrastre desde el borde superior + glassmorphism + 7 controles.
// #############################################################
static int  qsPanelY = 0;        // 0 = oculto, SCR_H = abierto del todo
static bool qsDragging = false;
static bool qsPower = false;             // Ahorro Ultra -- el UNICO bool de estado que hacia falta (Wi-Fi/BT se quitaron: no tenian radio real detras, ver resumen de cambios)
static int  qsFlashIdx = -1;             // control con destello de toque activo (-1 = ninguno). Usa el mismo indice que qsTileIcon: 2=Ventanas, 3=Modo PC, 4=Ahorro Ultra, 5=Ajustes
static unsigned long qsFlashMs = 0;      // millis() del toque que disparo el destello
#define QS_FLASH_DUR_MS 220              // duracion del destello de feedback al tocar

// Geometria centralizada del panel rapido, estilo Control Center de iOS.
// A PROPOSITO solo hay 5 controles -- son los UNICOS respaldados por una
// funcion real en este archivo (ver qsApplyPower/wmEnter/enterApp/setBacklight
// mas abajo). No se incluyen Wi-Fi, Bluetooth ni bateria (%) porque ese
// codigo no controla ningun radio real ni lee ningun sensor real todavia.
#define QS_CAP_X 24                      // capsula vertical de Brillo (PWM real)
#define QS_CAP_Y 92
#define QS_CAP_W 140
#define QS_CAP_H 330
#define QS_CAP_R (QS_CAP_W / 2)
#define QS_CIRC_D 90                     // columna de circulos: Ventanas / Modo PC / Ajustes
#define QS_CIRC_R (QS_CIRC_D / 2)
#define QS_CIRC_CX (QS_CAP_X + QS_CAP_W + 18 + (SCR_W - 24 - (QS_CAP_X + QS_CAP_W + 18)) / 2)
#define QS_CIRC_CY(i) (QS_CAP_Y + QS_CIRC_R + (i) * 120)
#define QS_PILL_X 24                     // pastilla Ahorro Ultra (cambia la frecuencia real de la CPU)
#define QS_PILL_Y (QS_CAP_Y + QS_CAP_H + 18)
#define QS_PILL_W (SCR_W - 48)
#define QS_PILL_H 72
#define QS_PILL_R (QS_PILL_H / 2)

static void qsTileIcon(int idx, int cx, int cy, uint16_t col){
  switch(idx){
    case 2:                                                      // Ventanas (dos ventanas)
      drawRoundRect(cx - 12, cy - 10, 16, 13, 2, col);
      fillRoundRect(cx - 2, cy - 2, 15, 13, 2, col); break;
    case 3:                                                      // Modo PC (monitor)
      drawRoundRect(cx - 14, cy - 11, 28, 20, 3, col);
      fillRect(cx - 4, cy + 9, 8, 4, col); fillRect(cx - 10, cy + 13, 20, 3, col); break;
    case 4:                                                      // Ahorro (bateria+rayo)
      drawRoundRect(cx - 12, cy - 8, 22, 16, 3, col); fillRect(cx + 10, cy - 3, 3, 6, col);
      fillTriangle(cx - 2, cy - 6, cx + 3, cy - 6, cx - 1, cy, col);
      fillTriangle(cx - 1, cy, cx + 4, cy, cx - 2, cy + 6, col); break;
    case 5: drawSetCatIcon(0, cx - 14, cy - 14, 28, col); break; // Ajustes (engranaje)
  }
}
// Boton circular de accion (Ventanas / Modo PC / Ajustes). idx usa la misma
// numeracion que qsTileIcon. rad = d/2 en un cuadro d x d = circulo perfecto
// (mismo truco que usan los iconos redondos en el resto del sistema).
static void qsCircleBtn(int idx, int cx, int cy){
  int d = QS_CIRC_D, x = cx - d / 2, y = cy - d / 2;
  fillRoundRectA(x + 2, y + 3, d, d, d / 2, rgb565(0,0,0), 55);            // sombra sutil
  if(uiGlass) drawLiquidGlassPanel(x, y, d, d, d / 2, rgb565(56,62,86), millis());
  else fillRoundRect(x, y, d, d, d / 2, rgb565(38,42,56));
  drawRoundRect(x, y, d, d, d / 2, rgb565(90,98,120));                     // borde sutil
  qsTileIcon(idx, cx, cy - 10, rgb565(220,224,236));
  const char* lab = idx == 2 ? "Ventanas" : idx == 3 ? "Modo PC" : "Ajustes";
  drawTextC(cx, cy + 20, lab, 1, rgb565(190,196,212));
}
// Pastilla ancha de toggle (Ahorro Ultra -- el unico toggle real que queda).
static void qsTogglePill(int x, int y, int w, int h, bool on){
  fillRoundRectA(x + 2, y + 4, w, h, h / 2, rgb565(0,0,0), 55);
  if(uiGlass) drawLiquidGlassPanel(x, y, w, h, h / 2, on ? rgb565(255,150,40) : rgb565(56,62,86), millis());
  else fillRoundRect(x, y, w, h, h / 2, on ? rgb565(210,110,20) : rgb565(38,42,56));
  drawRoundRect(x, y, w, h, h / 2, on ? rgb565(255,190,110) : rgb565(90,98,120));
  qsTileIcon(4, x + h / 2, y + h / 2, rgb565(255,255,255));
  drawText(x + h + 12, y + 14, "Ahorro Ultra", 2, rgb565(255,255,255));
  drawText(x + h + 12, y + 40, on ? "Activado - 160 MHz" : "Desactivado - 360 MHz", 1, rgb565(220,224,236));
}
static uint16_t* qsBuf = NULL;      // cortina precompuesta (para arrastre fluido)
static int  qsLastY = 0;
// qsDirty se declara arriba, junto a gHomeDirty (invalidacion de caches).

// dibuja titulo + tiles + etiqueta y pista del slider en el gBuf actual (sin relleno/perilla)
static void qsDrawContent(){
  drawText(24, 40, "Ajustes r\xC3\xA1pidos", 3, rgb565(240,244,252));
  glDrawSpec = false;                                            // elementos SIN reflejo horneado (se anima con sheen en qsRender)
  // Brillo: pista de la capsula vertical (el relleno ambar es dinamico, se
  // pinta cada frame en qsRender porque cambia con el arrastre / PWM real)
  fillRoundRectA(QS_CAP_X + 3, QS_CAP_Y + 5, QS_CAP_W, QS_CAP_H, QS_CAP_R, rgb565(0,0,0), 55);
  if(uiGlass) drawLiquidGlassPanel(QS_CAP_X, QS_CAP_Y, QS_CAP_W, QS_CAP_H, QS_CAP_R, rgb565(40,46,64), millis());
  else fillRoundRect(QS_CAP_X, QS_CAP_Y, QS_CAP_W, QS_CAP_H, QS_CAP_R, rgb565(32,36,50));
  drawRoundRect(QS_CAP_X, QS_CAP_Y, QS_CAP_W, QS_CAP_H, QS_CAP_R, rgb565(80,86,106));
  // Ventanas / Modo PC / Ajustes: columna de 3 circulos (acciones reales)
  qsCircleBtn(2, QS_CIRC_CX, QS_CIRC_CY(0));
  qsCircleBtn(3, QS_CIRC_CX, QS_CIRC_CY(1));
  qsCircleBtn(5, QS_CIRC_CX, QS_CIRC_CY(2));
  // Ahorro Ultra: pastilla ancha (toggle real, cambia la frecuencia de la CPU)
  qsTogglePill(QS_PILL_X, QS_PILL_Y, QS_PILL_W, QS_PILL_H, qsPower);
  glDrawSpec = true;
}
// compone la cortina COMPLETA una sola vez en qsBuf (parte cara)
static void qsCompose(){
  if(!qsBuf) qsBuf = (uint16_t*)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!qsBuf) return;
  memcpy(qsBuf, homeBuf, (size_t)SCR_W * SCR_H * 2);
  setBuf(qsBuf);
  int c0 = gClipY0, c1 = gClipY1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  if(uiGlass){
    glDrawSpec = false;
    drawLiquidGlassPanelEx(0, 0, SCR_W, SCR_H, 0, rgb565(26,34,60), millis(), 11);   // cortina Liquid Glass (blur mas fuerte que el resto del sistema; ver drawLiquidGlassPanelEx)
    glDrawSpec = true;
    fillRectA(0, 0, SCR_W, SCR_H, rgb565(12,14,24), 140);                       // oscurecer para legibilidad (sin tocar: preserva el contraste ya afinado)
  } else {
    fillRectA(0, 0, SCR_W, SCR_H, rgb565(14,16,26), 234);                       // glassmorphism plano
    fillRectA(0, 0, SCR_W, 130, rgb565(30,42,74), 46);                          // tinte superior
  }
  qsDrawContent();
  gClipY0 = c0; gClipY1 = c1;
  setBuf(fb);
  qsDirty = false;
}
// por-frame: SOLO copia la banda revelada (memcpy) + relleno del slider + flush de banda
static void qsSpecular(int py, uint32_t t){        // destello diagonal que se desliza sobre la cortina
  int off = (int)((t / 14) % (uint32_t)(SCR_W + py + 80)) - 40;
  for(int y = 0; y < py; y += 2){
    int cx = off - y + SCR_W / 2;
    for(int i = -44; i <= 44; i++){ int x = cx + i; if((unsigned)x < SCR_W){ int a = 16 - (i < 0 ? -i : i) / 3; if(a > 0) pxA(x, y, rgb565(255,255,255), (uint8_t)a); } }
  }
}
static void qsRender(){
  if(qsPanelY <= 0){ blitToFb(homeBuf); flxFlushAll(); qsLastY = 0; return; }
  if(qsDirty || !qsBuf) qsCompose();
  if(!qsBuf){ blitToFb(homeBuf); flxFlushAll(); return; }
  setBuf(bbuf);
  int py = qsPanelY < SCR_H ? qsPanelY : SCR_H;
  for(int j = 0; j < py; j++) memcpy(bbuf + (size_t)j * SCR_W, qsBuf + (size_t)j * SCR_W, SCR_W * 2);
  int maxY = py > qsLastY ? py : qsLastY; if(maxY > SCR_H) maxY = SCR_H;
  for(int j = py; j < maxY; j++) memcpy(bbuf + (size_t)j * SCR_W, homeBuf + (size_t)j * SCR_W, SCR_W * 2);
  uint32_t t = millis();                        // fuera del if: la usan tambien el destello de toque y la sombra de abajo
  // RECORTE AL BORDE DE LA CORTINA. La capsula de Brillo ocupa y=92..422 y solo
  // se comprobaba "if(QS_CAP_Y < py)": con la cortina a medio abrir (py~100) se
  // pintaban >300 filas POR DEBAJO del borde, encima del escritorio. Eso producia
  // (a) la barra ambar saliendose de la tarjeta, (b) la pastilla amarilla flotando
  // sobre los iconos y (c) restos que se quedaban PEGADOS: qsRender solo restaura
  // hasta max(py, qsLastY), asi que lo pintado mas abajo nunca se limpiaba.
  // Recortando a py, ningun elemento de la cortina puede salirse de ella.
  const int oCY1 = gClipY1, oCY0 = gClipY0, oCX0 = gClipX0, oCX1 = gClipX1;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = py - 1;
  if(uiGlass){
    if(QS_CAP_Y < py) glassSheen(QS_CAP_X, QS_CAP_Y, QS_CAP_W, QS_CAP_H, QS_CAP_R, t);
    for(int i = 0; i < 3; i++){ int cy = QS_CIRC_CY(i); if(cy - QS_CIRC_R < py) glassSheen(QS_CIRC_CX - QS_CIRC_R, cy - QS_CIRC_R, QS_CIRC_D, QS_CIRC_D, QS_CIRC_R, t); }
    if(QS_PILL_Y < py) glassSheen(QS_PILL_X, QS_PILL_Y, QS_PILL_W, QS_PILL_H, QS_PILL_R, t);
    qsSpecular(py, t);                                                 // destello diagonal ambiental sobre toda la cortina
  }
  if(qsFlashIdx >= 0){                                                // destello de feedback al tocar (2=Ventanas,3=Modo PC,4=Ahorro Ultra,5=Ajustes)
    uint32_t e = t - qsFlashMs;
    if(e < QS_FLASH_DUR_MS){
      float p = 1.0f - (float)e / QS_FLASH_DUR_MS;                    // se desvanece
      uint8_t a = (uint8_t)(120 * p);
      if(qsFlashIdx == 4){
        if(QS_PILL_Y < py) fillRoundRectA(QS_PILL_X, QS_PILL_Y, QS_PILL_W, QS_PILL_H, QS_PILL_R, rgb565(255,255,255), a);
      } else {
        int ci = qsFlashIdx == 5 ? 2 : qsFlashIdx - 2;                 // 2->0, 3->1, 5->2
        int cy = QS_CIRC_CY(ci);
        if(cy - QS_CIRC_R < py) fillRoundRectA(QS_CIRC_CX - QS_CIRC_R, cy - QS_CIRC_R, QS_CIRC_D, QS_CIRC_D, QS_CIRC_R, rgb565(255,255,255), a);
      }
    } else qsFlashIdx = -1;
  }
  // Brillo: relleno ambar dinamico (cambia con el arrastre / PWM real) + icono + %
  if(QS_CAP_Y < py){
    int fillH = QS_CAP_H * gBright / 100;
    if(fillH > 0) fillRoundRect(QS_CAP_X, QS_CAP_Y + QS_CAP_H - fillH, QS_CAP_W, fillH, QS_CAP_R, rgb565(255,190,40));
    drawSetCatIcon(1, QS_CAP_X + QS_CAP_W / 2 - 14, QS_CAP_Y + QS_CAP_H - 44, 28, rgb565(70,50,10));   // sol: casi siempre cae sobre el relleno
    char pb[8]; snprintf(pb, sizeof(pb), "%d%%", gBright);
    drawTextC(QS_CAP_X + QS_CAP_W / 2, QS_CAP_Y + QS_CAP_H - 74, pb, 2, gBright >= 25 ? rgb565(70,50,10) : rgb565(255,255,255));
  }
  gClipY0 = oCY0; gClipY1 = oCY1; gClipX0 = oCX0; gClipX1 = oCX1;      // fin del recorte a la cortina
  int shY = py, shEnd = shY + 18; if(shEnd > maxY) shEnd = maxY;       // sombra suave bajo el borde movil de la cortina
  for(int yy = shY; yy < shEnd; yy++){
    uint8_t a = (uint8_t)(70 * (1.0f - (float)(yy - shY) / 18.0f));
    if(a > 0) hLineA(0, yy, SCR_W, rgb565(0,0,0), a);
  }
  fillRoundRect(SCR_W / 2 - 28, py - 14, 56, 5, 2, rgb565(180,185,200));
  present(0, maxY);
  qsLastY = py;
}
static void qsAnimTo(int target){
  int cur = qsPanelY, steps = 16;
  const float c1 = 1.70158f, c3 = c1 + 1.0f;                     // easeOutBack estandar
  for(int i = 1; i <= steps; i++){
    float p = (float)i / steps, pm1 = p - 1.0f;
    float e = 1.0f + c3 * pm1 * pm1 * pm1 + c1 * pm1 * pm1;      // overshoot leve y asienta en 1.0
    int ny = cur + (int)((target - cur) * e);
    if(ny < 0) ny = 0; if(ny > SCR_H) ny = SCR_H;                // el overshoot no debe salir del rango valido
    qsPanelY = ny;
    qsRender(); delay(12);
  }
  qsPanelY = target; qsRender();
  if(target <= 0){ qsPanelY = 0; blitToFb(homeBuf); flxFlushAll(); }
}
static void qsApplyPower(){ setCpuFrequencyMhz(qsPower ? 160 : 360); }
static bool qsTapTile(int px, int py){
  const int idxOf[3] = { 2, 3, 5 };                     // Ventanas, Modo PC, Ajustes
  for(int i = 0; i < 3; i++){
    int cy = QS_CIRC_CY(i), dx = px - QS_CIRC_CX, dy = py - cy;
    if(dx * dx + dy * dy <= QS_CIRC_R * QS_CIRC_R){     // hit-test circular real (no la caja cuadrada)
      qsFlashIdx = idxOf[i]; qsFlashMs = millis();       // destello de feedback al tocar (ver overlay en qsRender)
      switch(idxOf[i]){
        case 2: qsPanelY = 0; wmEnter(); return true;                // abrir WindowManager
        case 3: qsPanelY = 0; enterApp(IC_MODOPC); return true;      // -> framework de ventanas
        case 5: qsPanelY = 0; enterApp(IC_AJUSTES); return true;     // -> Ajustes
      }
    }
  }
  if(px >= QS_PILL_X && px <= QS_PILL_X + QS_PILL_W && py >= QS_PILL_Y && py <= QS_PILL_Y + QS_PILL_H){
    qsFlashIdx = 4; qsFlashMs = millis();
    qsPower = !qsPower; qsApplyPower();
    qsDirty = true; qsRender(); return true;
  }
  return false;
}
// Devuelve true si la cortina consumio el toque (esta activa)
static float qsVel = 0; static int qsPrevY = 0;
static bool qsHandle(){
  if(qsDragging){
    if(T.down){
      int y = T.y; if(y < 0) y = 0; if(y > SCR_H) y = SCR_H;
      qsVel = qsVel * 0.5f + (y - qsPrevY) * 0.5f;      // velocidad suavizada del gesto
      qsPrevY = y; qsPanelY = y; qsRender();
    } else {
      qsDragging = false;
      if(qsVel > 6.5f) qsAnimTo(SCR_H);                 // flick rapido abajo -> abrir de inmediato
      else if(qsVel < -6.5f) qsAnimTo(0);               // flick rapido arriba -> cerrar de inmediato
      else if(qsPanelY < SCR_H / 2) qsAnimTo(0); else qsAnimTo(SCR_H);   // si no, por posicion
    }
    return true;
  }
  if(qsPanelY >= SCR_H){
    if(T.down && T.x >= QS_CAP_X - 14 && T.x <= QS_CAP_X + QS_CAP_W + 14 && T.y >= QS_CAP_Y - 14 && T.y <= QS_CAP_Y + QS_CAP_H + 14){
      int v = (QS_CAP_Y + QS_CAP_H - T.y) * 100 / QS_CAP_H; if(v < 0) v = 0; if(v > 100) v = 100;   // arriba = 100%, abajo = 0%
      setBacklight(v); qsRender(); return true;                 // brillo real (PWM)
    }
    if(T.pressed && T.startY < 30){ qsDragging = true; qsPrevY = T.y; qsVel = 0; return true; }
    if(T.swipeUp){ qsAnimTo(0); return true; }
    if(T.tap){ if(!qsTapTile(T.x, T.y) && T.y > QS_PILL_Y + QS_PILL_H) qsAnimTo(0); return true; }
    return true;                                                // consume todo mientras abierto
  }
  // cerrado: capturar arrastre desde la zona caliente (borde superior 0..30 px)
  if(T.pressed && T.startY < 30){ qsDragging = true; qsDirty = true; qsPrevY = T.y; qsVel = 0; qsPanelY = T.y; qsRender(); return true; }
  return false;
}

// #############################################################
// ##  APP MULTIMEDIA (ESQUELETO reproductor de video)
// ##  Doble buffer ping-pong en PSRAM + control de FPS por millis().
// ##  FUENTE = patron sintetico. Para video real, sustituir
// ##  vidDecodeFrame() por: leer MJPEG/.bin de la SD + decodificar.
// #############################################################
#define VID_W     448
#define VID_H     252
#define VID_RX    16
#define VID_RY    64
#define VID_TOTAL 300           // frames simulados (10 s @ 30 fps)
#define VID_FPS_MS 33           // 33 ms ~ 30 fps

static uint16_t *vidBufA = NULL, *vidBufB = NULL, *vidFront = NULL, *vidBack = NULL;
static bool vidPlaying = false;
static int  vidFrame = 0;
static unsigned long vidLastMs = 0;

// <<< PUNTO DE PORTABILIDAD >>> aqui iria: SD.open(archivo) + JPEGDEC/esp_jpeg
// para decodificar el frame f dentro de 'buf'. Ahora: patron animado.
static void vidDecodeFrame(uint16_t* buf, int f){
  for(int y = 0; y < VID_H; y++){
    uint16_t* row = buf + (size_t)y * VID_W;
    for(int x = 0; x < VID_W; x++)
      row[x] = rgb565((uint8_t)(x + f * 3), (uint8_t)(y * 2 + f * 2), (uint8_t)((x + y) / 2 + f * 4));
  }
}
static void vidBlit(){                 // vuelca el buffer FRONT al area de render
  // Antes escribia en una posicion FIJA de pantalla (VID_RX/VID_RY) saltandose
  // gOff*/gClip*: alojado en una ventana del Panel Edge, el video se pintaba
  // fuera de su ventana y arrasaba el escritorio. Ahora aplica el desplazamiento
  // de la ventana y recorta a su rectangulo interior. Sin ventana, gOff* son 0 y
  // el recorte es la pantalla entera -> comportamiento identico al original.
  const int cx0 = gWinBlit ? (gWinBX0 < 0 ? 0 : gWinBX0) : 0;
  const int cx1 = gWinBlit ? (gWinBX1 >= SCR_W ? SCR_W - 1 : gWinBX1) : SCR_W - 1;
  const int cy0 = gWinBlit ? (gWinBY0 < 0 ? 0 : gWinBY0) : 0;
  const int cy1 = gWinBlit ? (gWinBY1 >= SCR_H ? SCR_H - 1 : gWinBY1) : SCR_H - 1;
  int drawn0 = SCR_H, drawn1 = -1;
  for(int y = 0; y < VID_H; y++){
    const int dy = VID_RY + y + gOffY;
    if(dy < cy0 || dy > cy1) continue;
    const uint16_t* srcRow = vidFront + (size_t)y * VID_W;
    int x0 = VID_RX + gOffX, x1 = x0 + VID_W - 1;
    if(x0 < cx0){ srcRow += (size_t)(cx0 - x0); x0 = cx0; }   // recorta por la izquierda (avanza el origen)
    if(x1 > cx1) x1 = cx1;                                     // recorta por la derecha
    if(x0 > x1) continue;
    memcpy(fb + (size_t)dy * SCR_W + x0, srcRow, (size_t)(x1 - x0 + 1) * 2);
    if(dy < drawn0) drawn0 = dy;
    if(dy > drawn1) drawn1 = dy;
  }
  if(drawn1 >= drawn0) flxFlush(drawn0, drawn1);              // solo lo realmente escrito
}
static void vidDrawSeek(){
  int sbx = 24, sby = 434, sbw = SCR_W - 48;
  setBuf(fb);
  fillRoundRect(sbx, sby, sbw, 8, 4, rgb565(50,54,68));
  fillRoundRect(sbx, sby, sbw * vidFrame / VID_TOTAL, 8, 4, rgb565(80,160,240));
  fillCircle(sbx + sbw * vidFrame / VID_TOTAL, sby + 4, 10, rgb565(255,255,255));
  char tc[24]; snprintf(tc, sizeof(tc), "%d / %d", vidFrame, VID_TOTAL);
  fillRect(sbx, sby + 18, 160, 20, rgb565(10,12,18));
  drawText(sbx, sby + 18, tc, 2, rgb565(150,158,180));
  flxFlush(sby - 12, sby + 40);
}
static void vidDrawControls(){
  setBuf(fb);
  int pcx = SCR_W / 2, pcy = 366;
  fillCircle(pcx, pcy, 30, rgb565(50,110,235));           // play/pausa
  if(vidPlaying){ fillRect(pcx - 9, pcy - 12, 6, 24, rgb565(255,255,255)); fillRect(pcx + 3, pcy - 12, 6, 24, rgb565(255,255,255)); }
  else fillTriangle(pcx - 8, pcy - 12, pcx - 8, pcy + 12, pcx + 12, pcy, rgb565(255,255,255));
  int scx = pcx + 92;
  fillCircle(scx, pcy, 22, rgb565(60,64,78));             // stop
  fillRect(scx - 8, pcy - 8, 16, 16, rgb565(230,90,90));
  flxFlush(pcy - 34, pcy + 34);
}
static void vidRenderAll(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, rgb565(10,12,18));
  strokeSegAA(30, 26, 18, 18, 2.4f, rgb565(255,255,255));  // back (esq. sup-izq)
  strokeSegAA(18, 18, 30, 10, 2.4f, rgb565(255,255,255));
  drawTextC(SCR_W / 2, 14, "Multimedia", 3, rgb565(255,255,255));
  drawRoundRect(VID_RX - 2, VID_RY - 2, VID_W + 4, VID_H + 4, 6, rgb565(40,44,58));
  vidBlit();
  vidDrawControls();
  vidDrawSeek();
  drawTextC(SCR_W / 2, 476, "Fuente: patron de prueba (enchufa SD + MJPEG)", 1, rgb565(120,128,150));
  flxFlushAll();
}
static void vidEnter(){
  if(!vidBufA){                       // doble buffer en PSRAM (una sola vez)
    size_t bytes = (size_t)VID_W * VID_H * 2;
    vidBufA = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    vidBufB = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  vidFront = vidBufA; vidBack = vidBufB;
  vidPlaying = false; vidFrame = 0; vidLastMs = millis();
  if(vidFront) vidDecodeFrame(vidFront, 0);
  vidRenderAll();
}
static void vidTick(){
  // Reproduccion no-bloqueante: un frame exacto cada VID_FPS_MS
  if(vidPlaying && vidFront && (millis() - vidLastMs) >= VID_FPS_MS){
    vidLastMs = millis();
    vidFrame++; if(vidFrame >= VID_TOTAL) vidFrame = 0;
    vidDecodeFrame(vidBack, vidFrame);                    // decodifica en BACK...
    uint16_t* t = vidFront; vidFront = vidBack; vidBack = t;  // ...intercambia (ping-pong)...
    vidBlit();                                            // ...y dibuja FRONT
    vidDrawSeek();
  }
  if(!T.tap) return;
  if(T.x < 48 && T.y < 48){ appClose(); return; }         // back
  int pcx = SCR_W / 2, pcy = 366, scx = pcx + 92;
  int sbx = 24, sby = 434, sbw = SCR_W - 48;
  if(T.x >= pcx - 34 && T.x <= pcx + 34 && T.y >= pcy - 34 && T.y <= pcy + 34){
    vidPlaying = !vidPlaying; vidLastMs = millis(); vidDrawControls();
  } else if(T.x >= scx - 26 && T.x <= scx + 26 && T.y >= pcy - 26 && T.y <= pcy + 26){
    vidPlaying = false; vidFrame = 0;
    if(vidFront) vidDecodeFrame(vidFront, 0);
    vidBlit(); vidDrawControls(); vidDrawSeek();           // stop: libera/rebobina
  } else if(T.x >= sbx - 12 && T.x <= sbx + sbw + 12 && T.y >= sby - 14 && T.y <= sby + 16){
    int fr = (T.x - sbx) * VID_TOTAL / sbw; if(fr < 0) fr = 0; if(fr >= VID_TOTAL) fr = VID_TOTAL - 1;
    vidFrame = fr;                                         // seek: mueve el puntero
    if(vidFront) vidDecodeFrame(vidFront, vidFrame);
    vidBlit(); vidDrawSeek();
  }
}

// #############################################################
// ##  APP CAMARA (ESQUELETO estilo iPhone)
// ##  Zoom digital x1..x50 por recorte-central + reescalado (funciona
// ##  sobre una ESCENA SINTETICA en PSRAM). EIS = scaffold (offset).
// ##  FUENTE = escena generada. Para camara real, sustituir camGenScene()
// ##  por la captura del sensor (esp_cam / DVP) hacia camScene.
// #############################################################
#define CAM_SW SCR_W
#define CAM_SH SCR_H
static uint16_t* camScene = NULL;   // "sensor" simulado en PSRAM
static float camZoom = 1.0f;        // x1..x50
static bool  camRec = false, camNight = false, camRaw = false;
static int   camMode = 0;           // 0 FOTO,1 VIDEO,2 CINE,3 ACCION,4 PRORES
static int   camExpo = 50;          // 0..100
static int   camEisX = 0, camEisY = 0;   // <<< EIS: offset suavizado (real: de vectores de movimiento)

// <<< HOOK DE CAMARA REAL >>>
// Esta placa (JC4880P443C) SI tiene camara (MIPI-CSI en el ESP32-P4). Para
// capturar de verdad hace falta el driver del sensor (esp_video / esp_cam_sensor
// de ESP-IDF) y el MODELO exacto del sensor + su pinout, que no puedo adivinar.
// Cuando lo tengas: pon CAM_HAS_SENSOR en 1 e implementa camCapture() para volcar
// un frame del sensor frontal en 'dst' (CAM_SW x CAM_SH, RGB565). El resto del
// pipeline (zoom, EIS, UI, grabacion) ya esta listo y usara esos frames reales.
#define CAM_HAS_SENSOR 0
static bool camCapture(uint16_t* dst){
#if CAM_HAS_SENSOR
  // TODO: capturar frame del sensor aqui -> dst ; return true si es valido
  (void)dst; return false;
#else
  (void)dst; return false;   // sin sensor configurado -> patron de prueba
#endif
}
// Patron de prueba honesto (NO es la camara): barras de color + anillos para
// el zoom + aviso "SIN SENAL". Se muestra hasta cablear camCapture().
static void camGenScene(){
  gClipY0 = 0; gClipY1 = SCR_H - 1;
  setBuf(camScene);
  uint16_t bars[7] = { rgb565(200,200,200), rgb565(210,210,0), rgb565(0,200,210),
                       rgb565(0,200,0), rgb565(210,0,210), rgb565(210,0,0), rgb565(0,0,210) };
  int bw = CAM_SW / 7;
  for(int i = 0; i < 7; i++) fillRect(i * bw, 0, (i == 6 ? CAM_SW - 6 * bw : bw), CAM_SH, bars[i]);
  for(int r = 20; r < 300; r += 20) drawCircle(CAM_SW / 2, CAM_SH / 2, r, rgb565(255,255,255));  // detalle para zoom
  strokeSeg(CAM_SW / 2 - 30, CAM_SH / 2, CAM_SW / 2 + 30, CAM_SH / 2, 2, rgb565(255,255,255));
  strokeSeg(CAM_SW / 2, CAM_SH / 2 - 30, CAM_SW / 2, CAM_SH / 2 + 30, 2, rgb565(255,255,255));
  fillRoundRect(CAM_SW / 2 - 150, CAM_SH / 2 - 58, 300, 116, 16, rgb565(14,14,20));
  drawTextC(CAM_SW / 2, CAM_SH / 2 - 40, "SIN SE\xC3\x91" "AL", 4, rgb565(255,255,255));
  drawTextC(CAM_SW / 2, CAM_SH / 2 + 8, "esperando sensor de camara", 2, rgb565(200,205,215));
  setBuf(fb);
}
// Bucle de procesamiento de pixeles: recorta la region central segun el
// zoom y la reescala a pantalla completa (zoom digital real).
static void camRenderPreview(){
  int cw = (int)(CAM_SW / camZoom); if(cw < 4) cw = 4;
  int ch = (int)(CAM_SH / camZoom); if(ch < 4) ch = 4;
  int cx0 = (CAM_SW - cw) / 2 + camEisX, cy0 = (CAM_SH - ch) / 2 + camEisY;
  if(cx0 < 0) cx0 = 0; if(cx0 + cw > CAM_SW) cx0 = CAM_SW - cw;
  if(cy0 < 0) cy0 = 0; if(cy0 + ch > CAM_SH) cy0 = CAM_SH - ch;
  for(int py = 0; py < SCR_H; py++){
    int sy = cy0 + py * ch / SCR_H;
    uint16_t* srow = camScene + (size_t)sy * CAM_SW;
    uint16_t* drow = fb + (size_t)py * SCR_W;
    for(int px = 0; px < SCR_W; px++){ int sx = cx0 + px * cw / SCR_W; drow[px] = srow[sx]; }
  }
  setBuf(fb);
  if(camNight) fillRectA(0, 0, SCR_W, SCR_H, rgb565(10,20,45), 120);          // modo noche
  if(camExpo > 55) fillRectA(0, 0, SCR_W, SCR_H, rgb565(255,255,255), (camExpo - 55) * 3);
  else if(camExpo < 45) fillRectA(0, 0, SCR_W, SCR_H, rgb565(0,0,0), (45 - camExpo) * 3);
}
static void camDrawUI(){
  setBuf(fb);
  uint16_t W = rgb565(255,255,255);
  strokeSegAA(30, 26, 18, 18, 2.4f, W); strokeSegAA(18, 18, 30, 10, 2.4f, W);   // back
  fillRoundRectA(60, 8, 40, 36, 10, camNight ? rgb565(60,110,235) : rgb565(0,0,0), camNight ? 255 : 90);  // noche
  fillCircle(80, 26, 9, camNight ? rgb565(255,255,255) : rgb565(205,210,220));
  fillCircle(84, 22, 3, camNight ? rgb565(60,110,235) : rgb565(0,0,0));
  fillRoundRectA(108, 8, 50, 36, 10, camRaw ? rgb565(240,160,40) : rgb565(0,0,0), camRaw ? 255 : 90);     // RAW
  drawTextC(133, 16, "RAW", 2, camRaw ? rgb565(30,30,30) : rgb565(220,220,225));
  { char z[12]; snprintf(z, sizeof(z), "%.1fx", (double)camZoom);
    fillRoundRectA(SCR_W / 2 - 34, 8, 68, 32, 16, rgb565(0,0,0), 110); drawTextC(SCR_W / 2, 14, z, 2, W); }
  { int ex = SCR_W - 26, ey = 130, eh = 280;                                    // exposicion (vertical)
    fillRoundRectA(ex - 8, ey - 22, 32, eh + 44, 14, rgb565(0,0,0), 80);
    fillCircle(ex + 8, ey - 10, 4, rgb565(255,230,120));
    vLine(ex + 8, ey, eh, rgb565(120,124,140));
    int ky = ey + eh - (camExpo * eh / 100); fillCircle(ex + 8, ky, 9, W); }
  { const char* lb[5] = { "0.5x", "1x", "2x", "5x", "50x" }; float lv[5] = { 0.5f, 1, 2, 5, 50 };
    int bw = 56, g = 8, tot = 5 * bw + 4 * g, sx = (SCR_W - tot) / 2, y = SCR_H - 192;
    for(int i = 0; i < 5; i++){ int x = sx + i * (bw + g); bool on = fabsf(camZoom - lv[i]) < 0.05f;
      fillRoundRectA(x, y, bw, 34, 16, on ? rgb565(240,200,60) : rgb565(0,0,0), on ? 255 : 100);
      drawTextC(x + bw / 2, y + 9, lb[i], 2, on ? rgb565(30,30,30) : W); } }
  { int zx = 40, zy = SCR_H - 150, zw = SCR_W - 80;                             // zoom manual (horizontal)
    fillRoundRect(zx, zy, zw, 6, 3, rgb565(70,74,88));
    fillCircle(zx + (int)((camZoom - 1) / 49.0f * zw), zy + 3, 9, W); }
  { const char* md[5] = { "FOTO", "VIDEO", "CINE", "ACCION", "PRORES" }; int y = SCR_H - 30, cw2 = SCR_W / 5;
    for(int i = 0; i < 5; i++) drawTextC(cw2 * i + cw2 / 2, y, md[i], 2, i == camMode ? rgb565(255,220,60) : rgb565(170,176,190)); }
  { int cbx = SCR_W / 2, cby = SCR_H - 84; bool vidmode = (camMode >= 1);        // boton captura
    drawCircle(cbx, cby, 34, W); drawCircle(cbx, cby, 33, W);
    if(camRec && vidmode) fillRoundRect(cbx - 12, cby - 12, 24, 24, 5, rgb565(230,60,60));
    else fillCircle(cbx, cby, 27, vidmode ? rgb565(230,60,60) : W); }
  if(camRec){ fillCircle(SCR_W / 2 - 42, 60, 6, rgb565(230,60,60)); drawText(SCR_W / 2 - 30, 54, "REC", 2, rgb565(230,60,60)); }
}
static void camRenderAll(){ gClipY0 = 0; gClipY1 = SCR_H - 1; camRenderPreview(); camDrawUI(); flxFlushAll(); }
static void camEnter(){
  if(!camScene){
    camScene = (uint16_t*)heap_caps_malloc((size_t)CAM_SW * CAM_SH * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(camScene && !camCapture(camScene)) camGenScene();   // camara real si hay sensor; si no, patron
  }
  camZoom = 1.0f; camRec = false; camMode = 0; camNight = false; camRaw = false; camExpo = 50; camEisX = 0; camEisY = 0;
  if(camScene) camRenderAll();
}
static void camTick(){
  if(!camScene) return;
#if CAM_HAS_SENSOR
  static unsigned long camMs = 0;                       // streaming en vivo (cuando haya sensor)
  if(millis() - camMs > 33){ camMs = millis(); if(camCapture(camScene)) camRenderAll(); }
#endif
  int ex = SCR_W - 26, ey = 130, eh = 280;
  if(T.down && T.x >= ex - 18 && T.x <= ex + 26 && T.y >= ey - 12 && T.y <= ey + eh + 12){
    int v = (ey + eh - T.y) * 100 / eh; if(v < 0) v = 0; if(v > 100) v = 100; camExpo = v; camRenderAll(); return;
  }
  int zx = 40, zy = SCR_H - 150, zw = SCR_W - 80;
  if(T.down && T.y >= zy - 14 && T.y <= zy + 18 && T.x >= zx - 14 && T.x <= zx + zw + 14){
    float z = 1 + (float)(T.x - zx) / zw * 49.0f; if(z < 1) z = 1; if(z > 50) z = 50; camZoom = z; camRenderAll(); return;
  }
  if(!T.tap) return;
  if(T.x < 48 && T.y < 48){ appClose(); return; }
  if(T.x >= 60 && T.x <= 100 && T.y >= 8 && T.y <= 44){ camNight = !camNight; camRenderAll(); return; }
  if(T.x >= 108 && T.x <= 158 && T.y >= 8 && T.y <= 44){ camRaw = !camRaw; camRenderAll(); return; }
  { float lv[5] = { 0.5f, 1, 2, 5, 50 }; int bw = 56, g = 8, tot = 5 * bw + 4 * g, sx = (SCR_W - tot) / 2, y = SCR_H - 192;
    for(int i = 0; i < 5; i++){ int x = sx + i * (bw + g); if(T.x >= x && T.x <= x + bw && T.y >= y && T.y <= y + 34){ camZoom = lv[i]; camRenderAll(); return; } } }
  { int y = SCR_H - 30, cw2 = SCR_W / 5; if(T.y >= y - 8 && T.y <= y + 22){ int m = T.x / cw2; if(m >= 0 && m < 5){ camMode = m; if(camMode == 0) camRec = false; camRenderAll(); return; } } }
  { int cbx = SCR_W / 2, cby = SCR_H - 84; if(T.x >= cbx - 34 && T.x <= cbx + 34 && T.y >= cby - 34 && T.y <= cby + 34){
      if(camMode >= 1) camRec = !camRec; camRenderAll(); return; } }
}

// #############################################################
// ##  APP NOTAS + TECLADO 4 CAPAS (ES/EN/NUM/EMOJI)
// ##  Punteros dinamicos (mapaActivo), buffer UTF-8 seguro,
// ##  long-press para acentos y mapeo por cuadricula tactil.
// #############################################################
#define KB_COLS 10
#define KB_ROWS 3
#define KB_X    6
#define KB_KW   43
#define KB_KH   48
#define KB_GAP  4
#define KB_Y    (SCR_H - 4 * (KB_KH + KB_GAP) - 6)

// 4 matrices independientes de cadenas (const char*). La N con "\xC3\xB1".
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
static const char* LAYOUT_EMOJI[KB_ROWS][KB_COLS] = {   // emoticones de texto (la fuente los dibuja)
  {":)",":D",":(",";)",":P","xD",":o",":|","<3",":3"},
  {"^^","o_o",">:(",":'(","B)","-_-","=)","D:",":v",":c"},
  {"uwu",":*","<_<",">_>","(y)","!!",":]","[:","T_T","o/"} };

static const char* (*mapaActivo)[KB_COLS] = LAYOUT_ES;   // <<< puntero maestro
static bool kbShift = false, kbLangEs = true;
static char noteBuffer[512] = "";
// estado del pop-up de acentos
static int kbLpKey = -1, kbPopX = 0, kbPopY = 0, kbPopN = 0, kbPopW = 40, kbPopG = 4;
static bool kbPopup = false;

// ---- Insercion/borrado UTF-8 seguros ----
// ---- Modelo de texto editable (cursor + seleccion) + PORTAPAPELES GLOBAL ----
static int  noteCur = 0;                          // cursor (indice en bytes)
static int  noteSelA = -1, noteSelB = -1;         // seleccion A..B en bytes (-1 = ninguna)
static bool noteMenu = false;                     // menu contextual visible
static int  noteHandleDrag = 0;                   // 0 no, 1 manija izq, 2 der
static char clipboard[512] = "";                  // <<< portapapeles UNICO de todo FlexOS >>>

static int  utf8Prev(const char* s, int i){ if(i <= 0) return 0; i--; while(i > 0 && (s[i] & 0xC0) == 0x80) i--; return i; }
static int  utf8Next(const char* s, int i){ int L = strlen(s); if(i >= L) return L; i++; while(i < L && (s[i] & 0xC0) == 0x80) i++; return i; }
static bool isWordByte(unsigned char c){ return c >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }
static bool noteHasSel(){ return noteSelA >= 0 && noteSelB > noteSelA; }
static void noteClearSel(){ noteSelA = noteSelB = -1; noteMenu = false; }
static void noteDeleteSel(){
  if(!noteHasSel()) return;
  int a = noteSelA, b = noteSelB, L = strlen(noteBuffer);
  memmove(noteBuffer + a, noteBuffer + b, L - b + 1);
  noteCur = a; noteClearSel();
}
static void noteInsert(const char* s){            // inserta en el cursor (reemplaza seleccion si hay)
  if(noteHasSel()) noteDeleteSel();
  int L = strlen(noteBuffer), sl = strlen(s);
  if(L + sl >= (int)sizeof(noteBuffer) - 1) return;
  if(noteCur < 0) noteCur = 0; if(noteCur > L) noteCur = L;
  memmove(noteBuffer + noteCur + sl, noteBuffer + noteCur, L - noteCur + 1);
  memcpy(noteBuffer + noteCur, s, sl);
  noteCur += sl; noteMenu = false;
}
static void noteBackspace(){                       // borra antes del cursor (multibyte) o la seleccion
  if(noteHasSel()){ noteDeleteSel(); return; }
  if(noteCur <= 0) return;
  int p = utf8Prev(noteBuffer, noteCur), L = strlen(noteBuffer);
  memmove(noteBuffer + p, noteBuffer + noteCur, L - noteCur + 1);
  noteCur = p; noteMenu = false;
}
static void clipCopy(){ if(!noteHasSel()) return; int a = noteSelA, b = noteSelB, n = b - a; if(n >= (int)sizeof(clipboard)) n = sizeof(clipboard) - 1; memcpy(clipboard, noteBuffer + a, n); clipboard[n] = 0; }
static void clipCut(){ clipCopy(); noteDeleteSel(); }
static void clipPaste(){ if(clipboard[0]) noteInsert(clipboard); }
static void selectAllTxt(){ noteSelA = 0; noteSelB = strlen(noteBuffer); noteCur = noteSelB; noteMenu = noteHasSel(); }
static void selectWordAt(int bi){
  int L = strlen(noteBuffer); if(L == 0) return;
  if(bi >= L) bi = utf8Prev(noteBuffer, L);
  int a = bi; while(a > 0){ int p = utf8Prev(noteBuffer, a); if(!isWordByte((unsigned char)noteBuffer[p])) break; a = p; }
  int b = bi; while(b < L){ if(!isWordByte((unsigned char)noteBuffer[b])) break; b = utf8Next(noteBuffer, b); }
  if(b > a){ noteSelA = a; noteSelB = b; noteCur = b; noteMenu = true; }
}
static void kbPressChar(const char* s){
  if(kbShift && s[1] == 0 && s[0] >= 'a' && s[0] <= 'z'){ char u[2] = { (char)(s[0] - 32), 0 }; noteInsert(u); kbShift = false; }
  else if(kbShift && !strcmp(s, "\xC3\xB1")){ noteInsert("\xC3\x91"); kbShift = false; }
  else noteInsert(s);
}
// variantes acentuadas (solo las que tiene la fuente). Devuelve el numero.
static int kbGetVariants(char b, const char* var[4]){
  switch(b){
    case 'a': var[0]="\xC3\xA1"; var[1]="\xC3\xA0"; var[2]="\xC3\xA2"; var[3]="\xC3\xA3"; return 4;
    case 'e': var[0]="\xC3\xA9"; var[1]="\xC3\xA8"; var[2]="\xC3\xAA"; return 3;
    case 'i': var[0]="\xC3\xAD"; var[1]="\xC3\xAC"; var[2]="\xC3\xAE"; return 3;
    case 'o': var[0]="\xC3\xB3"; var[1]="\xC3\xB2"; var[2]="\xC3\xB4"; var[3]="\xC3\xB5"; return 4;
    case 'u': var[0]="\xC3\xBA"; var[1]="\xC3\xB9"; var[2]="\xC3\xBB"; var[3]="\xC3\xBC"; return 4;
  }
  return 0;
}
// ---- Mapeo por cuadricula: (x,y) -> celda (fila*COLS+col) o -1 ----
static int kbCellAt(int px, int py){
  for(int r = 0; r < KB_ROWS; r++){
    int ry = KB_Y + r * (KB_KH + KB_GAP);
    for(int c = 0; c < KB_COLS; c++){
      int cx = KB_X + c * (KB_KW + KB_GAP);
      if(px >= cx && px <= cx + KB_KW && py >= ry && py <= ry + KB_KH) return r * KB_COLS + c;
    }
  }
  return -1;
}
static bool kbIsVowelCell(int cell){
  if(cell < 0 || !(mapaActivo == LAYOUT_ES || mapaActivo == LAYOUT_EN)) return false;
  const char* k = mapaActivo[cell / KB_COLS][cell % KB_COLS];
  return k[1] == 0 && (k[0]=='a'||k[0]=='e'||k[0]=='i'||k[0]=='o'||k[0]=='u');
}

static int curSX, curSY, hASX, hASY, hBSX, hBSY, noteMenuX, noteMenuY;
static void noteDrawText(){
  setBuf(fb);
  fillRect(8, 48, SCR_W - 16, KB_Y - 56, rgb565(24,26,34));
  int x = 18, y = 60, maxX = SCR_W - 18, lh = 26, size = 2;
  float sc = fontSc(size);
  const char* s = noteBuffer; int bi = 0;
  bool hasSel = noteHasSel();
  curSX = 18; curSY = 60; hASX = hBSX = 18; hASY = hBSY = 60;
  while(*s){
    if(*s == '\n'){
      if(bi == noteCur){ curSX = x; curSY = y; } if(bi == noteSelA){ hASX = x; hASY = y; } if(bi == noteSelB){ hBSX = x; hBSY = y; }
      s++; bi++; x = 18; y += lh; if(y > KB_Y - 30) break; continue;
    }
    const char* save = s; uint32_t cp = nextCP(&s); int nb = s - save;
    int w = (int)(FG[fontIdx(cp)].adv * sc + 0.5f);
    if(x + w > maxX){ x = 18; y += lh; if(y > KB_Y - 30) break; }
    if(bi == noteCur){ curSX = x; curSY = y; } if(bi == noteSelA){ hASX = x; hASY = y; } if(bi == noteSelB){ hBSX = x; hBSY = y; }
    if(hasSel && bi >= noteSelA && bi < noteSelB) fillRect(x - 1, y - 2, w + 1, lh - 4, rgb565(48,92,168));  // resaltado
    char one[6]; int n = nb; if(n > 5) n = 5; for(int i = 0; i < n; i++) one[i] = save[i]; one[n] = 0;
    drawText(x, y, one, size, rgb565(235,238,246));
    x += w; bi += nb;
  }
  if(bi == noteCur){ curSX = x; curSY = y; } if(bi == noteSelA){ hASX = x; hASY = y; } if(bi == noteSelB){ hBSX = x; hBSY = y; }
  if(hasSel){                                       // manijas (gotas arrastrables)
    vLine(hASX, hASY - 2, 24, rgb565(90,150,240)); fillCircle(hASX, hASY + 24, 7, rgb565(90,150,240));
    vLine(hBSX, hBSY - 2, 24, rgb565(90,150,240)); fillCircle(hBSX, hBSY + 24, 7, rgb565(90,150,240));
  } else {
    fillRect(curSX + 1, curSY - 2, 2, 22, rgb565(90,150,240));   // cursor
  }
  if(noteMenu){                                     // menu contextual flotante
    const char* it[4] = { "Cortar", "Copiar", "Pegar", "Todo" };
    int bw = 92, gap = 4, tot = 4 * bw + 3 * gap, mx = (SCR_W - tot) / 2, my = hASY - 44; if(my < 50) my = 50;
    noteMenuX = mx; noteMenuY = my;
    fillRoundRect(mx - 6, my - 6, tot + 12, 40, 8, rgb565(38,42,56));
    for(int i = 0; i < 4; i++){ int bx = mx + i * (bw + gap); fillRoundRect(bx, my, bw, 28, 6, rgb565(58,64,84)); drawTextC(bx + bw / 2, my + 7, it[i], 2, rgb565(240,244,252)); }
  }
  flxFlush(44, KB_Y - 8);
}
// mapea un toque (px,py) al indice de byte mas cercano en el texto
static int noteLayoutHit(int px, int py){
  int x = 18, y = 60, maxX = SCR_W - 18, lh = 26, size = 2; float sc = fontSc(size);
  const char* s = noteBuffer; int bi = 0, best = 0; long bestd = 1L << 30;
  while(*s){
    if(*s == '\n'){ long d = (long)abs(px - x) + (long)abs(py - (y + 8)) * 2; if(d < bestd){ bestd = d; best = bi; } s++; bi++; x = 18; y += lh; continue; }
    const char* save = s; uint32_t cp = nextCP(&s); int nb = s - save;
    int w = (int)(FG[fontIdx(cp)].adv * sc + 0.5f);
    if(x + w > maxX){ x = 18; y += lh; }
    long d0 = (long)abs(px - x) + (long)abs(py - (y + 8)) * 2; if(d0 < bestd){ bestd = d0; best = bi; }
    long d1 = (long)abs(px - (x + w)) + (long)abs(py - (y + 8)) * 2; if(d1 < bestd){ bestd = d1; best = bi + nb; }
    x += w; bi += nb;
  }
  long d = (long)abs(px - x) + (long)abs(py - (y + 8)) * 2; if(d < bestd){ bestd = d; best = bi; }
  return best;
}
static int noteMenuHit(int px, int py){
  if(!noteMenu || py < noteMenuY || py > noteMenuY + 28) return -1;
  int bw = 92, gap = 4;
  for(int i = 0; i < 4; i++){ int bx = noteMenuX + i * (bw + gap); if(px >= bx && px <= bx + bw) return i; }
  return -1;
}
static void kbFKey(int x, int fy, int w, const char* label, bool on){
  fillRoundRect(x, fy, w, KB_KH, 6, on ? rgb565(60,110,235) : rgb565(66,70,86));
  drawTextC(x + w / 2, fy + KB_KH / 2 - 8, label, 2, rgb565(240,242,248));
}
static void noteDrawFuncRow(){
  int fy = KB_Y + 3 * (KB_KH + KB_GAP);
  kbFKey(6, fy, 58, "shift", kbShift);
  const char* nl = (mapaActivo == LAYOUT_NUM) ? "emoji" : (mapaActivo == LAYOUT_EMOJI) ? "ABC" : "?123";
  kbFKey(68, fy, 54, nl, false);
  kbFKey(126, fy, 48, kbLangEs ? "ES" : "EN", false);
  kbFKey(178, fy, 190, "espacio", false);
  kbFKey(372, fy, 44, "<-", false);
  kbFKey(420, fy, 52, "ent", false);
}
static void noteRenderKeyboard(){
  setBuf(fb);
  if(uiGlass) drawLiquidGlassPanel(0, KB_Y - 4, SCR_W, SCR_H - (KB_Y - 4), 0, rgb565(36,40,58), millis());
  else fillRect(0, KB_Y - 4, SCR_W, SCR_H - (KB_Y - 4), rgb565(18,20,28));
  for(int r = 0; r < KB_ROWS; r++) for(int c = 0; c < KB_COLS; c++){
    int x = KB_X + c * (KB_KW + KB_GAP), y = KB_Y + r * (KB_KH + KB_GAP);
    fillRoundRect(x, y, KB_KW, KB_KH, 6, rgb565(52,56,70));
    const char* k = mapaActivo[r][c];
    if(kbShift && k[1] == 0 && k[0] >= 'a' && k[0] <= 'z'){ char u[2] = { (char)(k[0] - 32), 0 }; drawTextC(x + KB_KW / 2, y + KB_KH / 2 - 8, u, 2, rgb565(240,242,248)); }
    else drawTextC(x + KB_KW / 2, y + KB_KH / 2 - 8, k, 2, rgb565(240,242,248));
  }
  noteDrawFuncRow();
  flxFlush(KB_Y - 6, SCR_H - 1);
}
static void noteRenderAll(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, rgb565(12,14,20));
  strokeSegAA(30, 26, 18, 18, 2.4f, rgb565(255,255,255));
  strokeSegAA(18, 18, 30, 10, 2.4f, rgb565(255,255,255));
  drawTextC(SCR_W / 2, 14, "Notas", 3, rgb565(255,255,255));
  noteDrawText();
  noteRenderKeyboard();
  flxFlushAll();
}
static void kbRenderPopup(int cell){
  int r = cell / KB_COLS, c = cell % KB_COLS;
  int kx = KB_X + c * (KB_KW + KB_GAP), ky = KB_Y + r * (KB_KH + KB_GAP);
  const char* var[4]; int n = kbGetVariants(mapaActivo[r][c][0], var);
  if(n == 0) return;
  int pw = 40, ph = 46, gap = 4, totw = n * pw + (n - 1) * gap;
  int px0 = kx + KB_KW / 2 - totw / 2; if(px0 < 4) px0 = 4; if(px0 + totw > SCR_W - 4) px0 = SCR_W - 4 - totw;
  int py0 = ky - ph - 10;
  kbPopX = px0; kbPopY = py0; kbPopN = n; kbPopW = pw; kbPopG = gap;
  setBuf(fb);
  fillRoundRect(px0 - 6, py0 - 6, totw + 12, ph + 12, 10, rgb565(40,44,58));
  for(int i = 0; i < n; i++){
    int x = px0 + i * (pw + gap);
    fillRoundRect(x, py0, pw, ph, 8, rgb565(64,68,86));
    drawTextC(x + pw / 2, py0 + ph / 2 - 12, var[i], 3, rgb565(255,255,255));
  }
  flxFlush(py0 - 8, ky + KB_KH);
}
static int kbPopupHit(int px, int py){
  for(int i = 0; i < kbPopN; i++){ int x = kbPopX + i * (kbPopW + kbPopG); if(px >= x && px <= x + kbPopW && py >= kbPopY && py <= kbPopY + 46) return i; }
  return -1;
}
static void handleKeyRelease(int px, int py){
  if(px < 48 && py < 48){ appClose(); return; }
  int fy = KB_Y + 3 * (KB_KH + KB_GAP);
  if(py >= fy && py <= fy + KB_KH){
    if(px < 64) kbShift = !kbShift;
    else if(px < 122){                                   // cicla ABC -> NUM -> EMOJI
      if(mapaActivo == LAYOUT_NUM) mapaActivo = LAYOUT_EMOJI;
      else if(mapaActivo == LAYOUT_EMOJI) mapaActivo = kbLangEs ? LAYOUT_ES : LAYOUT_EN;
      else mapaActivo = LAYOUT_NUM;
    }
    else if(px < 174){ kbLangEs = !kbLangEs; if(mapaActivo == LAYOUT_ES || mapaActivo == LAYOUT_EN) mapaActivo = kbLangEs ? LAYOUT_ES : LAYOUT_EN; }
    else if(px < 368) noteInsert(" ");
    else if(px < 416) noteBackspace();
    else noteInsert("\n");
    noteRenderAll(); return;
  }
  int cell = kbCellAt(px, py);
  if(cell >= 0){ kbPressChar(mapaActivo[cell / KB_COLS][cell % KB_COLS]); noteRenderAll(); return; }
}
static void noteEnter(){
  mapaActivo = LAYOUT_ES; kbLangEs = true; kbShift = false; kbLpKey = -1; kbPopup = false;
  noteCur = strlen(noteBuffer); noteClearSel(); noteHandleDrag = 0;
  noteRenderAll();
}
static void noteTick(){
  int txTop = 48, txBot = KB_Y - 8;

  // 1) Menu contextual: intercepta toques en el area de texto
  if(noteMenu && T.released && T.tap && T.y < KB_Y - 4){
    int mi = noteMenuHit(T.x, T.y);
    if(mi >= 0){
      if(mi == 0) clipCut(); else if(mi == 1) clipCopy(); else if(mi == 2) clipPaste(); else selectAllTxt();
      if(mi != 3) noteMenu = false;
      noteRenderAll(); return;
    }
    noteMenu = false; noteClearSel(); noteCur = noteLayoutHit(T.x, T.y); noteRenderAll(); return;
  }

  // 2) Inicio de gesto: manija de seleccion o long-press de tecla
  if(T.pressed){
    noteHandleDrag = 0; kbLpKey = -1; kbPopup = false;
    if(noteHasSel() && T.y >= txTop && T.y < txBot + 30){
      if(abs(T.x - hASX) < 24 && abs(T.y - (hASY + 20)) < 28) noteHandleDrag = 1;
      else if(abs(T.x - hBSX) < 24 && abs(T.y - (hBSY + 20)) < 28) noteHandleDrag = 2;
    }
    if(!noteHandleDrag && T.y >= KB_Y - 4){ int cell = kbCellAt(T.x, T.y); kbLpKey = kbIsVowelCell(cell) ? cell : -1; }
    return;
  }

  // 3) Arrastre de manija -> extender seleccion
  if(noteHandleDrag && T.down){
    int bi = noteLayoutHit(T.x, T.y);
    if(noteHandleDrag == 1){ if(bi >= 0 && bi < noteSelB) noteSelA = bi; }
    else { if(bi > noteSelA) noteSelB = bi; }
    noteCur = (noteHandleDrag == 1) ? noteSelA : noteSelB;
    noteRenderAll(); return;
  }

  // 4) Long-press en texto -> seleccionar palabra
  if(!noteHandleDrag && kbLpKey < 0 && T.down && !noteHasSel()
     && T.startY >= txTop && T.startY < txBot && (millis() - T.downMs) > 500
     && abs(T.x - T.startX) < 12 && abs(T.y - T.startY) < 12){
    selectWordAt(noteLayoutHit(T.startX, T.startY)); noteRenderAll(); return;
  }

  // 5) Long-press en tecla -> popup de acentos
  if(kbLpKey >= 0 && T.down && !kbPopup && (millis() - T.downMs) > 500){ kbPopup = true; kbRenderPopup(kbLpKey); }

  // 6) Soltar
  if(T.released){
    if(noteHandleDrag){ noteHandleDrag = 0; noteMenu = true; noteRenderAll(); return; }
    if(kbPopup){
      int v = kbPopupHit(T.x, T.y); const char* var[4];
      if(v >= 0){ kbGetVariants(mapaActivo[kbLpKey / KB_COLS][kbLpKey % KB_COLS][0], var); noteInsert(var[v]); }
      else kbPressChar(mapaActivo[kbLpKey / KB_COLS][kbLpKey % KB_COLS]);
      kbPopup = false; kbLpKey = -1; noteRenderAll(); return;
    }
    if(T.tap && T.y >= txTop && T.y < txBot){        // tap en texto -> posicionar cursor
      noteClearSel(); noteCur = noteLayoutHit(T.x, T.y); noteRenderAll(); return;
    }
    handleKeyRelease(T.x, T.y); kbLpKey = -1;         // teclado
  }
}

// #############################################################
// ##  APPS SIMPLES: Almacenamiento, Educacion, Navegador,
// ##  Code IDE, Paint (funcional), Juegos
// #############################################################
static int simpBar(int y, const char* label, const char* val, int pct, uint16_t col){
  int bx = 40, bw = SCR_W - 80;
  drawText(bx, y, label, 2, rgb565(225,229,240));
  drawTextR(bx + bw, y, val, 2, rgb565(180,188,205));
  fillRoundRect(bx, y + 30, bw, 18, 9, rgb565(48,52,66));
  fillRoundRect(bx, y + 30, bw * pct / 100, 18, 9, col);
  return y + 74;
}
static void almEnter(){
  setBuf(fb); fillRect(0, WIN_TOP, SCR_W, WIN_BOT - WIN_TOP, WIN_BG);
  drawTextC(SCR_W / 2, WIN_TOP + 14, "Almacenamiento", 3, rgb565(255,255,255));
  int y = WIN_TOP + 80;
  y = simpBar(y, "Flash (sistema)", "~2 / 16 MB", 13, rgb565(90,160,240));
  size_t pf = heap_caps_get_free_size(MALLOC_CAP_SPIRAM), pt = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  int up = pt > 0 ? (int)(100 - (uint64_t)pf * 100 / pt) : 0;
  char v[40]; snprintf(v, sizeof(v), "%d%% en uso", up);
  y = simpBar(y, "PSRAM", v, up, rgb565(90,180,120));
  y = simpBar(y, "Tarjeta SD", "No insertada", 0, rgb565(200,120,80));
  drawTextC(SCR_W / 2, WIN_BOT - 60, "Inserta una microSD para mas espacio", 1, rgb565(140,148,168));
  flxFlush(WIN_TOP, WIN_BOT);
}
static void simpCards(const char* title, const char* items[], int n){
  setBuf(fb); fillRect(0, WIN_TOP, SCR_W, WIN_BOT - WIN_TOP, WIN_BG);
  drawTextC(SCR_W / 2, WIN_TOP + 14, title, 3, rgb565(255,255,255));
  for(int i = 0; i < n; i++){
    int y = WIN_TOP + 70 + i * 92;
    if(uiGlass) drawLiquidGlassPanel(20, y, SCR_W - 40, 76, 14, rgb565(60,80,150), millis());
    else fillRoundRect(20, y, SCR_W - 40, 76, 14, rgb565(40,44,58));
    drawText(40, y + 16, items[i], 3, rgb565(255,255,255));
    drawText(40, y + 48, "Proximamente", 1, rgb565(150,158,180));
  }
  flxFlush(WIN_TOP, WIN_BOT);
}
static void eduEnter(){ const char* it[4] = { "Electr\xC3\xB3nica b\xC3\xA1sica", "Programaci\xC3\xB3n C++", "Redes y WiFi", "Sensores I2C" }; simpCards("Educaci\xC3\xB3n", it, 4); }
// #############################################################
// ##  JUEGO: "Geo Dash" -- selector de niveles + 4 niveles REALES
// ##  (Stereo Madness, Clutterfunk, Can't Let Go, Blast Processing)
// ##  con 5 formas (Cubo/Ship/Ball/Wave/UFO), portales de forma /
// ##  tamano / gravedad / velocidad, Modo Practica con checkpoints
// ##  y progreso persistente por nivel en Preferences ("flexos").
// ##
// ##  ORIENTACION HORIZONTAL: se dibuja rotado 90 sobre el panel
// ##  portrait usando el modo landscape ya existente (gLand). El
// ##  lienzo LOGICO es 800x480 (LW x LH). Igual que el Modo PC, la
// ##  app pone gLand=true y es la unica que gestiona la pantalla
// ##  (el Panel Edge no aplica); al salir se restaura gLand=false.
// ##
// ##  Patron: APP_OWN_TOUCH | APP_CUSTOM_HEADER -- posee TODA la
// ##  pantalla y TODOS los toques, compone en bbuf y vuelca con
// ##  present() (anti-flicker), y trae su propio boton de salir.
// ##
// ##  Todo el estado es static a nivel de archivo (sin heap).
// ##  NINGUNA firma de funcion usa los tipos GeoObstacle/GeoTrail/
// ##  GeoPart/GeoLevel, asi los prototipos que auto-genera el IDE
// ##  (insertados ARRIBA del archivo, antes de estas definiciones)
// ##  nunca referencian un tipo desconocido -> compila limpio.
// #############################################################

// ---- Geometria del escenario (coords LOGICAS landscape: ancho LW=800, alto LH=480) ----
#define GEO_TS       34          // tamano de baldosa (pincho/bloque)
#define GEO_PL       34          // lado del jugador a tamano NORMAL (mini = la mitad)
#define GEO_PLX      140         // X fija del jugador
#define GEO_HUD_H    40          // banda superior (salir + progreso)
#define GEO_FLOOR_Y  400         // Y del suelo (superficie superior de la franja de suelo)
#define GEO_CEIL_Y   84          // Y del techo (superficie en gravedad invertida / corredores)

// ---- Fisica (px/seg; dt real via millis()) ----
#define GEO_GRAV     2600.0f     // gravedad (cubo / ball)
#define GEO_JUMP     720.0f      // impulso de salto del cubo (apice ~100 px)
#define GEO_FALLCAP  1300.0f     // tope de velocidad de caida (cubo / ball)
#define GEO_SPEED    230.0f      // velocidad base de scroll del mundo
#define GEO_ROT      3.6f        // vel. de giro cosmetico (rad/s)
#define GEO_SHIP_GRAV 1500.0f    // gravedad reducida del ship
#define GEO_SHIP_ACC  3200.0f    // empuje del ship al mantener tocado
#define GEO_SHIP_VMAX 520.0f     // tope de velocidad vertical del ship
#define GEO_UFO_GRAV  1500.0f    // gravedad del ufo (flappy)
#define GEO_UFO_IMP   470.0f     // impulso corto del ufo por cada tap
#define GEO_UFO_VMAX  660.0f     // tope de velocidad vertical del ufo
#define GEO_LANDTOL  30          // holgura de "aterrizar desde arriba" sobre un bloque
#define GEO_FRAME_MS 33          // throttle de render (~30 FPS estables)
#define GEO_DTMAX    0.05f       // dt maximo por frame (tope si el loop se traba)
#define GEO_SUBSTEP  0.02f       // paso fijo de la fisica (evita tuneles a cualquier FPS)
#define GEO_RESPAWN_MS 520       // espera tras morir antes de reaparecer

// ---- Estela y particulas ----
#define GEO_TRAIL_N   22
#define GEO_TRAIL_FADE 120.0f    // distancia (px de scroll) tras la que la estela se apaga
#define GEO_PART_N    18

// ---- Paleta base (los colores de identidad de cada nivel se aplican aparte) ----
#define GEO_INK      rgb565(10,12,26)      // relleno oscuro de obstaculos
#define GEO_TXT      rgb565(232,236,248)
#define GEO_AMBER    rgb565(250,200,70)    // portal de gravedad
#define GEO_CYAN     rgb565(90,220,240)    // portal de velocidad
#define GEO_PURP     rgb565(200,120,255)   // portal de forma
#define GEO_LIME     rgb565(150,240,120)   // portal de tamano

// ---- Tipos de obstaculo (los primeros 6 conservan su valor original) ----
enum { OBS_PICO = 0, OBS_BLOQUE, OBS_HUECO, OBS_PICO_T,
       OBS_PORTAL_GRAV, OBS_PORTAL_VEL,
       OBS_P_CUBO, OBS_P_SHIP, OBS_P_BALL, OBS_P_WAVE, OBS_P_UFO,
       OBS_P_MINI, OBS_P_NORMAL, OBS_BLOQUE_F };

// x = posicion en el MUNDO (px); tipo; param = altura en baldosas (bloque),
// ancho en baldosas (hueco) o subtipo (portal de velocidad).
struct GeoObstacle { int16_t x; uint8_t tipo; uint8_t param; };

// ---- Formas del jugador ----
enum { FRM_CUBO = 0, FRM_SHIP, FRM_BALL, FRM_WAVE, FRM_UFO };

// #############################################################
// ##  DATOS DE LOS 4 NIVELES (nivel = DATA, no dibujo a mano)
// #############################################################

// NIVEL 1 -- STEREO MADNESS (1*): cubo tutorial + un tramo de ship a la mitad
// y otro corto cerca del final. Progresion MUY gradual, gaps generosos.
static const GeoObstacle LVL0[] = {
  {  560, OBS_PICO,     0 },                 // pista de arranque larga
  {  780, OBS_PICO,     0 },
  { 1000, OBS_BLOQUE,   1 },                 // bloque simple (saltar por encima/encima)
  { 1240, OBS_PICO,     0 },
  { 1480, OBS_BLOQUE,   2 },                 // plataforma alta
  { 1740, OBS_HUECO,    2 },                 // hueco (saltar por encima)
  { 2000, OBS_PICO,     0 },
  { 2220, OBS_P_SHIP,   0 },                 // --> SHIP (zona espaciosa)
  { 2480, OBS_PICO_T,   0 },
  { 2600, OBS_PICO,     0 },
  { 2820, OBS_PICO_T,   0 },
  { 3020, OBS_P_CUBO,   0 },                 // --> volver a CUBO
  { 3220, OBS_PICO,     0 },
  { 3420, OBS_BLOQUE,   1 },
  { 3620, OBS_PICO,     0 },
  { 3800, OBS_P_SHIP,   0 },                 // --> SHIP corto final
  { 4020, OBS_PICO_T,   0 },
  { 4140, OBS_PICO,     0 },
  { 4340, OBS_P_CUBO,   0 },
  { 4520, OBS_PICO,     0 },
};

// NIVEL 2 -- CLUTTERFUNK (11*, el mas dificil): portal de TAMANO (mini/normal),
// inversion de gravedad frecuente, ship angosto y un tramo de ball. Denso.
static const GeoObstacle LVL1[] = {
  {  480, OBS_PICO,        0 },
  {  640, OBS_PICO,        0 },
  {  780, OBS_BLOQUE,      1 },
  {  920, OBS_PICO,        0 },
  { 1060, OBS_PORTAL_GRAV, 0 },              // invertir -> al techo
  { 1240, OBS_PICO_T,      0 },
  { 1380, OBS_PICO_T,      0 },
  { 1520, OBS_PORTAL_GRAV, 0 },              // volver a normal
  { 1660, OBS_PICO,        0 },
  { 1800, OBS_P_MINI,      0 },              // --> MINI (hitbox a la mitad)
  { 1920, OBS_PICO,        0 },              // obstaculos mas juntos en mini
  { 2020, OBS_PICO,        0 },
  { 2130, OBS_BLOQUE,      1 },
  { 2250, OBS_PICO,        0 },
  { 2380, OBS_P_NORMAL,    0 },              // --> tamano NORMAL
  { 2540, OBS_P_SHIP,      0 },              // --> SHIP angosto
  { 2740, OBS_PICO_T,      0 },
  { 2840, OBS_PICO,        0 },
  { 3000, OBS_PICO_T,      0 },
  { 3100, OBS_PICO,        0 },
  { 3260, OBS_P_BALL,      0 },              // --> BALL (tap invierte gravedad)
  { 3440, OBS_PICO,        0 },
  { 3580, OBS_PICO_T,      0 },
  { 3740, OBS_PICO,        0 },
  { 3900, OBS_PORTAL_VEL,  2 },              // acelerar (tramo final)
  { 4060, OBS_P_CUBO,      0 },
  { 4200, OBS_PICO,        0 },
  { 4340, OBS_BLOQUE,      2 },
  { 4500, OBS_PICO,        0 },
};

// NIVEL 3 -- CAN'T LET GO (6*): introduce el modo WAVE (zigzag diagonal en
// corredores con picos arriba Y abajo). Cubo y ship mas ajustados que Stereo.
static const GeoObstacle LVL2[] = {
  {  500, OBS_PICO,     0 },
  {  700, OBS_PICO,     0 },
  {  860, OBS_BLOQUE,   1 },
  { 1040, OBS_PICO,     0 },
  { 1200, OBS_PICO,     0 },                 // picos mas seguidos
  { 1380, OBS_P_WAVE,   0 },                 // --> WAVE (corredor 1)
  { 1540, OBS_PICO_T,   0 },
  { 1640, OBS_PICO,     0 },
  { 1760, OBS_PICO_T,   0 },
  { 1860, OBS_PICO,     0 },
  { 2000, OBS_P_CUBO,   0 },                 // --> CUBO
  { 2180, OBS_PICO,     0 },
  { 2340, OBS_P_SHIP,   0 },                 // --> SHIP ajustado
  { 2540, OBS_PICO_T,   0 },
  { 2640, OBS_PICO,     0 },
  { 2820, OBS_P_CUBO,   0 },                 // --> CUBO
  { 2980, OBS_PICO,     0 },
  { 3120, OBS_BLOQUE,   2 },
  { 3280, OBS_P_WAVE,   0 },                 // --> WAVE (corredor 2)
  { 3440, OBS_PICO_T,   0 },
  { 3540, OBS_PICO,     0 },
  { 3660, OBS_PICO_T,   0 },
  { 3780, OBS_P_CUBO,   0 },                 // --> CUBO
  { 3960, OBS_PICO,     0 },
  { 4140, OBS_PICO,     0 },
};

// NIVEL 4 -- BLAST PROCESSING (10*): usa las 5 formas (Cubo->Wave->Ship->Ball->
// UFO), un tramo de wave largo y fino (lo mas dificil) y un final traicionero
// con bloques reales/falsos (visualmente iguales, solo algunos solidos).
static const GeoObstacle LVL3[] = {
  {  460, OBS_PICO,     0 },
  {  660, OBS_BLOQUE,   1 },
  {  840, OBS_PICO,     0 },
  { 1000, OBS_P_WAVE,   0 },                 // --> WAVE largo (seccion mas dificil)
  { 1140, OBS_PICO_T,   0 },
  { 1240, OBS_PICO,     0 },
  { 1340, OBS_PICO_T,   0 },
  { 1440, OBS_PICO,     0 },
  { 1540, OBS_PICO_T,   0 },
  { 1640, OBS_PICO,     0 },
  { 1780, OBS_P_SHIP,   0 },                 // --> SHIP
  { 1980, OBS_PICO_T,   0 },
  { 2080, OBS_PICO,     0 },
  { 2260, OBS_P_BALL,   0 },                 // --> BALL
  { 2440, OBS_PICO,     0 },
  { 2580, OBS_PICO_T,   0 },
  { 2760, OBS_P_UFO,    0 },                 // --> UFO (tap = salto corto)
  { 2940, OBS_PICO_T,   0 },
  { 3040, OBS_PICO,     0 },
  { 3220, OBS_PICO_T,   0 },
  { 3400, OBS_P_CUBO,   0 },                 // --> CUBO (final)
  { 3560, OBS_BLOQUE,   1 },                 // bloque real
  { 3700, OBS_BLOQUE_F, 1 },                 // bloque FALSO (no solido)
  { 3820, OBS_PICO,     0 },
  { 3960, OBS_BLOQUE_F, 1 },                 // falso
  { 4080, OBS_BLOQUE,   1 },                 // real
  { 4220, OBS_PICO,     0 },
  { 4360, OBS_BLOQUE,   1 },
};

#define LVLN(a) ((uint8_t)(sizeof(a) / sizeof(a[0])))
#define GEO_TRIG_MAX 48          // >= max de obstaculos de cualquier nivel

// Metadatos + paleta de identidad de cada nivel (name/stars/skin/sky/floor/neon).
struct GeoLevel {
  const char* name; uint8_t stars;
  uint16_t skin, sky, floorc, neon;
  const GeoObstacle* obs; uint8_t obsN; uint16_t len;
};
static const GeoLevel GEO_LEVELS[4] = {
  { "Stereo Madness", 1,  rgb565(70,150,240),  rgb565(26,54,168),  rgb565(10,16,64),  rgb565(120,200,255), LVL0, LVLN(LVL0), 4600 },
  { "Clutterfunk",   11,  rgb565(232,66,204),  rgb565(176,40,168), rgb565(70,10,72),  rgb565(255,130,240), LVL1, LVLN(LVL1), 4600 },
  { "Can't Let Go",   6,  rgb565(214,204,86),  rgb565(150,158,32), rgb565(58,58,12),  rgb565(232,240,120), LVL2, LVLN(LVL2), 4240 },
  { "Blast Processing",10, rgb565(244,112,58), rgb565(24,150,150), rgb565(8,58,60),   rgb565(120,240,232), LVL3, LVLN(LVL3), 4500 },
};

// Multiplicadores de los portales de velocidad (indexados por param)
static const float GEO_VELMUL[4] = { 0.75f, 1.0f, 1.35f, 1.7f };

// #############################################################
// ##  ESTADO DE LA PARTIDA (todo static, sin heap)
// #############################################################
enum { GEO_PLAY = 0, GEO_DEAD, GEO_WIN };     // estado dentro de una partida
enum { GS_SELECT = 0, GS_GAME };              // pantalla activa de la app
static int    gScreen    = GS_SELECT;         // selector <-> juego
static int    gCurLevel  = 0;                 // nivel elegido en el carrusel

static int    geoState   = GEO_PLAY;
static float  geoScroll  = 0;                 // desplazamiento del mundo hacia la izquierda
static float  geoPlayerY = 0;                 // Y del borde superior del jugador
static float  geoPrevBot = 0;                 // borde inferior del frame anterior (aterrizaje)
static float  geoVelY    = 0;
static float  geoAngle   = 0;                 // giro cosmetico (cubo/ball)
static int    geoGravDir = 1;                 // +1 normal, -1 invertida
static float  geoSpeedMul = 1.0f;
static bool   geoGrounded = true;
static int    geoAttempts = 1;
static int    gForma     = FRM_CUBO;          // forma actual del jugador
static bool   gMini      = false;             // tamano mini activo
static bool   gPractice  = false;             // Modo Practica activo
static bool   geoDownPrev = false;            // estado de T.down del tick anterior (flanco)
static bool   geoHeldLatch = false;           // hubo dedo apoyado desde el ultimo update fisico
static bool   geoTapLatch  = false;           // hubo un flanco de nuevo toque (ball/ufo)
static uint32_t geoFrameMs = 0;               // millis() del frame anterior (para dt)
static uint32_t geoDeadMs  = 0;
static bool   geoTrig[GEO_TRIG_MAX];          // portales ya disparados (evita re-disparo)

// Punteros/estado del nivel activo (se fijan al empezar; asi ninguna firma de
// funcion necesita el tipo GeoObstacle/GeoLevel -> compila limpio).
static const GeoObstacle* gObs = 0;
static int      gObsN = 0;
static int      gLen  = 4500;
static uint16_t gSky, gFloorC, gNeon, gSkin;

// Progreso persistente (cache en RAM; se lee de Preferences al entrar).
static int  geoBest[4] = { 0, 0, 0, 0 };
static bool geoDone[4] = { false, false, false, false };

// Checkpoint de Modo Practica (solo el ultimo alcanzado; NO se guarda en NVS).
static bool  geoCPset = false;
static float geoCPscroll = 0, geoCPy = 0;
static uint8_t geoCPform = FRM_CUBO, geoCPmini = 0;
static int8_t  geoCPgrav = 1;
static float   geoCPspeed = 1.0f;
static float   geoNextCP = 0;                 // umbral de scroll para el proximo checkpoint

// Estela: buffer circular de posiciones (se apagan al alejarse por el scroll)
struct GeoTrail { float scrollAt; int16_t y; bool used; };
static GeoTrail geoTrail[GEO_TRAIL_N];
static int      geoTrailHead = 0;

// Particulas de la explosion de muerte
struct GeoPart { float x, y, vx, vy, life; bool used; };
static GeoPart  geoPart[GEO_PART_N];

// PRNG propio (xorshift32) -- sin dependencias, semilla desde millis()
static uint32_t geoRngState = 0x9e3779b9u;
static inline uint32_t geoRand(){
  uint32_t x = geoRngState; x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  return (geoRngState = x);
}
static inline float geoRandf(){ return (geoRand() & 0xFFFF) / 65535.0f; }

// AABB (rectangulo contra rectangulo)
static inline bool geoAABB(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh){
  return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

// Touch fisico (portrait) -> coords LOGICAS landscape (igual que hace el Modo PC).
static inline int geoLX(){ return T.y; }                  // logica X 0..799
static inline int geoLY(){ return (SCR_W - 1) - T.x; }    // logica Y 0..479

// #############################################################
// ##  FAST-FILL landscape: rellena un rect LOGICO escribiendo runs
// ##  CONTIGUOS en memoria fisica (una hLine logica seria dispersa,
// ##  pero una columna logica es una fila fisica contigua). Esto es
// ##  lo que mantiene el FPS estable pese a la rotacion: el fondo,
// ##  suelo y HUD son el grueso del pintado y salen casi a costo
// ##  portrait. Escribe DIRECTO al buffer destino (sin gClip: el
// ##  juego ocupa toda la pantalla), respetando el mismo mapeo que
// ##  putPhys -> encaja pixel a pixel con el resto de primitivas gLand.
// #############################################################
static void geoFillL(uint16_t* buf, int lx, int ly, int w, int h, uint16_t col){
  if(lx < 0){ w += lx; lx = 0; }
  if(ly < 0){ h += ly; ly = 0; }
  if(lx + w > LW) w = LW - lx;
  if(ly + h > LH) h = LH - ly;
  if(w <= 0 || h <= 0) return;
  int px0 = (SCR_W - 1) - (ly + h - 1);          // columna fisica minima del run
  for(int i = 0; i < w; i++){
    uint16_t* p = buf + (size_t)(lx + i) * SCR_W + px0;
    for(int k = 0; k < h; k++) p[k] = col;       // run horizontal fisico = contiguo
  }
}

// #############################################################
// ##  PROGRESO PERSISTENTE (Preferences, namespace "flexos")
// ##  Mismo mecanismo que el resto del sistema. Claves por nivel:
// ##  "g0_best".. "g3_best" (int %), "g0_done".."g3_done" (bool).
// #############################################################
static void geoLoadProgress(){
  prefs.begin("flexos", true);
  char k[12];
  for(int i = 0; i < 4; i++){
    snprintf(k, sizeof(k), "g%d_best", i); geoBest[i] = prefs.getInt(k, 0);
    snprintf(k, sizeof(k), "g%d_done", i); geoDone[i] = prefs.getBool(k, false);
  }
  prefs.end();
}
static void geoSaveProgress(int lvl, int pct, bool done){
  if(pct < 0) pct = 0; if(pct > 100) pct = 100;
  bool changed = false;
  if(pct > geoBest[lvl]){ geoBest[lvl] = pct; changed = true; }
  if(done && !geoDone[lvl]){ geoDone[lvl] = true; changed = true; }
  if(!changed) return;                            // no escribir NVS si nada mejoro
  prefs.begin("flexos", false);
  char k[12];
  snprintf(k, sizeof(k), "g%d_best", lvl); prefs.putInt(k, geoBest[lvl]);
  if(geoDone[lvl]){ snprintf(k, sizeof(k), "g%d_done", lvl); prefs.putBool(k, true); }
  prefs.end();
}

// Fija los punteros/paleta del nivel activo desde la tabla (por indice).
static void geoBindLevel(int idx){
  gCurLevel = idx;
  gObs   = GEO_LEVELS[idx].obs;
  gObsN  = GEO_LEVELS[idx].obsN;
  gLen   = GEO_LEVELS[idx].len;
  gSkin  = GEO_LEVELS[idx].skin;
  gSky   = GEO_LEVELS[idx].sky;
  gFloorC= GEO_LEVELS[idx].floorc;
  gNeon  = GEO_LEVELS[idx].neon;
}

// ---- Reinicio del nivel al estado inicial (no toca intentos) ----
static void geoResetLevel(){
  geoScroll = 0; geoVelY = 0; geoAngle = 0;
  geoGravDir = 1; geoSpeedMul = 1.0f;
  gForma = FRM_CUBO; gMini = false;
  geoPlayerY = GEO_FLOOR_Y - GEO_PL; geoPrevBot = geoPlayerY + GEO_PL;
  geoGrounded = true;
  for(int i = 0; i < GEO_TRIG_MAX; i++) geoTrig[i] = false;
  for(int i = 0; i < GEO_TRAIL_N; i++) geoTrail[i].used = false;
  for(int i = 0; i < GEO_PART_N; i++)  geoPart[i].used = false;
  geoTrailHead = 0;
  geoFrameMs = millis();
}

// ---- Cambio de forma (portal): conserva Y; las formas de vuelo arrancan en el aire ----
static void geoSetForma(int f){
  gForma = f;
  geoAngle = 0;
  if(f == FRM_SHIP || f == FRM_WAVE || f == FRM_UFO) geoGrounded = false;
}

// ---- Cambio de tamano (portal mini/normal): mantiene el borde inferior ----
static void geoSetMini(bool m){
  if(gMini == m) return;
  int oldPL = gMini ? (GEO_PL / 2) : GEO_PL;
  gMini = m;
  int newPL = gMini ? (GEO_PL / 2) : GEO_PL;
  geoPlayerY += (oldPL - newPL);                 // el borde inferior no se mueve
  if(geoPlayerY < GEO_CEIL_Y) geoPlayerY = GEO_CEIL_Y;
}

// ---- Checkpoints de Practica: guardar / restaurar / re-armar portales ----
static void geoSaveCP(){
  geoCPset = true;
  geoCPscroll = geoScroll; geoCPy = geoPlayerY;
  geoCPform = (uint8_t)gForma; geoCPmini = gMini ? 1 : 0;
  geoCPgrav = (int8_t)geoGravDir; geoCPspeed = geoSpeedMul;
}
static void geoRestoreCP(){
  geoScroll = geoCPscroll; geoPlayerY = geoCPy; geoVelY = 0;
  gForma = geoCPform; gMini = (geoCPmini != 0);
  geoGravDir = geoCPgrav; geoSpeedMul = geoCPspeed; geoGrounded = false;
  geoAngle = 0;
  // Re-armar portales: los que quedaron ANTES del checkpoint no deben re-disparar
  // (su efecto ya viene reflejado en el estado guardado); los de despues, si.
  int cp = (int)geoCPscroll;
  for(int i = 0; i < gObsN; i++){
    int sx = gObs[i].x - cp;                      // X en pantalla al momento del checkpoint
    geoTrig[i] = (sx < GEO_PLX + 16);             // ya atravesado -> marcado como disparado
  }
  for(int i = 0; i < GEO_TRAIL_N; i++) geoTrail[i].used = false;
  geoTrailHead = 0;
}

// ---- Explosion de particulas en la posicion del jugador ----
static void geoSpawnParticles(){
  int gPL = gMini ? (GEO_PL / 2) : GEO_PL;
  float cx = GEO_PLX + gPL / 2.0f, cy = geoPlayerY + gPL / 2.0f;
  for(int i = 0; i < GEO_PART_N; i++){
    float ang = geoRandf() * 6.2831853f;
    float sp  = 90.0f + geoRandf() * 260.0f;
    geoPart[i].x = cx; geoPart[i].y = cy;
    geoPart[i].vx = cosf(ang) * sp;
    geoPart[i].vy = sinf(ang) * sp - 70.0f;
    geoPart[i].life = 0.45f + geoRandf() * 0.35f;
    geoPart[i].used = true;
  }
}
static void geoDie(){
  if(geoState != GEO_PLAY) return;
  geoState = GEO_DEAD; geoDeadMs = millis();
  geoVelY = 0;
  geoSpawnParticles();
  // El % guardado del Normal Mode se actualiza al morir (nunca baja).
  if(!gPractice){
    int pct = (int)(geoScroll / (float)gLen * 100.0f);
    geoSaveProgress(gCurLevel, pct, false);
  }
}
static void geoUpdateParticles(float dt){
  for(int i = 0; i < GEO_PART_N; i++){
    if(!geoPart[i].used) continue;
    geoPart[i].x += geoPart[i].vx * dt;
    geoPart[i].y += geoPart[i].vy * dt;
    geoPart[i].vy += 900.0f * dt;
    geoPart[i].life -= dt;
    if(geoPart[i].life <= 0) geoPart[i].used = false;
  }
}

// #############################################################
// ##  FISICA + COLISION + PORTALES + MUERTE (un substep)
// ##  held: dedo apoyado en la zona de juego (latcheado en geoTick).
// ##  Los efectos de "un solo tap" (ball/ufo) se aplican FUERA, en
// ##  geoTick, para no repetirse en cada substep.
// #############################################################
static void geoUpdate(float dt, bool held){
  int gPL = gMini ? (GEO_PL / 2) : GEO_PL;

  // --- Impulso continuo del cubo (salta al mantener si esta en el piso) ---
  if(gForma == FRM_CUBO && geoGrounded && held){
    geoVelY = -geoGravDir * GEO_JUMP;
    geoGrounded = false;
  }
  bool prevGrounded = geoGrounded;

  // --- Scroll del mundo + estela ---
  geoScroll += GEO_SPEED * geoSpeedMul * dt;
  geoTrail[geoTrailHead].scrollAt = geoScroll;
  geoTrail[geoTrailHead].y = (int16_t)(geoPlayerY + gPL / 2);
  geoTrail[geoTrailHead].used = true;
  geoTrailHead = (geoTrailHead + 1) % GEO_TRAIL_N;

  // --- Integracion vertical (depende de la forma) ---
  geoPrevBot = geoPlayerY + gPL;
  if(gForma == FRM_CUBO || gForma == FRM_BALL){
    geoVelY += geoGravDir * GEO_GRAV * dt;
    if(geoVelY >  GEO_FALLCAP) geoVelY =  GEO_FALLCAP;
    if(geoVelY < -GEO_FALLCAP) geoVelY = -GEO_FALLCAP;
  } else if(gForma == FRM_SHIP){
    geoVelY += geoGravDir * GEO_SHIP_GRAV * dt;
    if(held) geoVelY -= geoGravDir * GEO_SHIP_ACC * dt;   // empuje sostenido
    if(geoVelY >  GEO_SHIP_VMAX) geoVelY =  GEO_SHIP_VMAX;
    if(geoVelY < -GEO_SHIP_VMAX) geoVelY = -GEO_SHIP_VMAX;
  } else if(gForma == FRM_UFO){
    geoVelY += geoGravDir * GEO_UFO_GRAV * dt;
    if(geoVelY >  GEO_UFO_VMAX) geoVelY =  GEO_UFO_VMAX;
    if(geoVelY < -GEO_UFO_VMAX) geoVelY = -GEO_UFO_VMAX;
  } else { // FRM_WAVE: diagonal instantanea, sin inercia (45 grados con el scroll)
    float wv = GEO_SPEED * geoSpeedMul;
    geoVelY = held ? -wv : wv;
  }
  geoPlayerY += geoVelY * dt;

  int scroll = (int)geoScroll;
  int pl = GEO_PLX, pr = GEO_PLX + gPL;

  // --- Disparadores de portal (colision de "atravesar", una sola vez) ---
  for(int i = 0; i < gObsN; i++){
    uint8_t tp = gObs[i].tipo;
    if(tp < OBS_PORTAL_GRAV || tp > OBS_P_NORMAL) continue;   // no es portal
    if(geoTrig[i]) continue;
    int sx = gObs[i].x - scroll;
    int bx = sx + GEO_TS / 2 - 15;
    if(geoAABB(pl, (int)geoPlayerY, gPL, gPL, bx, GEO_CEIL_Y, 30, GEO_FLOOR_Y - GEO_CEIL_Y)){
      geoTrig[i] = true;
      switch(tp){
        case OBS_PORTAL_GRAV: geoGravDir = -geoGravDir; break;
        case OBS_PORTAL_VEL:  geoSpeedMul = GEO_VELMUL[gObs[i].param & 3]; break;
        case OBS_P_CUBO:   geoSetForma(FRM_CUBO); break;
        case OBS_P_SHIP:   geoSetForma(FRM_SHIP); break;
        case OBS_P_BALL:   geoSetForma(FRM_BALL); break;
        case OBS_P_WAVE:   geoSetForma(FRM_WAVE); break;
        case OBS_P_UFO:    geoSetForma(FRM_UFO);  break;
        case OBS_P_MINI:   geoSetMini(true);  break;
        case OBS_P_NORMAL: geoSetMini(false); break;
      }
    }
  }

  // --- Resolucion vertical segun forma ---
  geoGrounded = false;
  if(gForma == FRM_CUBO){
    if(geoVelY >= 0){                                        // cayendo: suelo o techo de bloque
      bool overGap = false;
      for(int i = 0; i < gObsN; i++){
        if(gObs[i].tipo != OBS_HUECO) continue;
        int gx0 = gObs[i].x - scroll, gx1 = gx0 + gObs[i].param * GEO_TS;
        if(pr > gx0 && pl < gx1){ overGap = true; break; }
      }
      float top = overGap ? 100000.0f : (float)GEO_FLOOR_Y;
      for(int i = 0; i < gObsN; i++){
        if(gObs[i].tipo != OBS_BLOQUE) continue;
        int bx = gObs[i].x - scroll, by = GEO_FLOOR_Y - gObs[i].param * GEO_TS;
        if(pr > bx && pl < bx + GEO_TS && geoPrevBot <= by + GEO_LANDTOL && by < top) top = by;
      }
      if(geoPlayerY + gPL >= top){ geoPlayerY = top - gPL; geoVelY = 0; geoGrounded = true; }
    }
    if(geoVelY <= 0){                                        // subiendo: techo (grav invertida)
      if(geoPlayerY <= GEO_CEIL_Y){ geoPlayerY = GEO_CEIL_Y; geoVelY = 0; geoGrounded = true; }
    }
  } else if(gForma == FRM_BALL){                             // se pega a piso o techo
    if(geoPlayerY + gPL >= GEO_FLOOR_Y){ geoPlayerY = GEO_FLOOR_Y - gPL; if(geoVelY > 0) geoVelY = 0; geoGrounded = true; }
    if(geoPlayerY <= GEO_CEIL_Y){        geoPlayerY = GEO_CEIL_Y;         if(geoVelY < 0) geoVelY = 0; geoGrounded = true; }
  } else {                                                   // ship/wave/ufo: deslizan en los bordes
    if(geoPlayerY < GEO_CEIL_Y){         geoPlayerY = GEO_CEIL_Y;         if(geoVelY < 0) geoVelY = 0; }
    if(geoPlayerY > GEO_FLOOR_Y - gPL){  geoPlayerY = GEO_FLOOR_Y - gPL;  if(geoVelY > 0) geoVelY = 0; }
  }

  // --- Giro cosmetico ---
  if(gForma == FRM_CUBO){
    if(geoGrounded && !prevGrounded){ float q = 1.5707963f; geoAngle = q * roundf(geoAngle / q); }
    if(!geoGrounded) geoAngle += GEO_ROT * dt;
  } else if(gForma == FRM_BALL){
    geoAngle += (geoGravDir > 0 ? GEO_ROT : -GEO_ROT) * dt * 1.5f;   // rueda
  } else {
    geoAngle = 0;
  }
  while(geoAngle >= 6.2831853f) geoAngle -= 6.2831853f;
  while(geoAngle < 0)           geoAngle += 6.2831853f;

  // --- Muerte: picos (piso/techo), interior de bloque SOLIDO, caer en hueco ---
  int hx = gMini ? 5 : 8, hy = gMini ? 12 : 22, hw = gMini ? 12 : 18;  // hitbox de pico segun tamano
  for(int i = 0; i < gObsN; i++){
    int sx = gObs[i].x - scroll;
    uint8_t tp = gObs[i].tipo;
    if(tp == OBS_PICO){
      if(geoAABB(pl, (int)geoPlayerY, gPL, gPL, sx + hx, GEO_FLOOR_Y - hy - 2, hw, hy)){ geoDie(); return; }
    } else if(tp == OBS_PICO_T){
      if(geoAABB(pl, (int)geoPlayerY, gPL, gPL, sx + hx, GEO_CEIL_Y + 2, hw, hy)){ geoDie(); return; }
    } else if(tp == OBS_BLOQUE){
      int by = GEO_FLOOR_Y - gObs[i].param * GEO_TS;
      if(pr > sx + 2 && pl < sx + GEO_TS - 2 && geoPlayerY + gPL > by + 6 && geoPlayerY < GEO_FLOOR_Y){ geoDie(); return; }
    }
    // OBS_BLOQUE_F: bloque FALSO -> se dibuja igual pero NO colisiona ni sostiene.
  }
  if((gForma == FRM_CUBO || gForma == FRM_BALL) && geoPlayerY > GEO_FLOOR_Y + 30){ geoDie(); return; }

  // --- Fin del nivel ---
  if(geoScroll >= gLen){
    geoState = GEO_WIN;
    if(!gPractice) geoSaveProgress(gCurLevel, 100, true);
  }
}

// #############################################################
// ##  DIBUJO DEL JUEGO (coords LOGICAS 800x480; escribe en bbuf)
// ##  Los grandes rellenos usan geoFillL (runs contiguos) para no
// ##  perder FPS por la rotacion. Obstaculos y jugador usan las
// ##  primitivas gLand normales (triangulos/quads/circulos/texto).
// #############################################################

// ---- Fondo: cielo + parallax + techo solido si la gravedad esta invertida ----
static void geoDrawBackground(){
  geoFillL(bbuf, 0, GEO_HUD_H, LW, GEO_FLOOR_Y - GEO_HUD_H, gSky);
  // Parallax: paneles verticales que derivan a 0.30x del scroll (profundidad).
  uint16_t pane = mix565(gSky, GEO_INK, 60);
  int off = (int)(geoScroll * 0.30f) % 130; if(off < 0) off += 130;
  for(int x = -off; x < LW; x += 130){
    geoFillL(bbuf, x + 14, GEO_HUD_H + 24, 84, GEO_FLOOR_Y - GEO_HUD_H - 60, pane);
  }
  if(geoGravDir < 0){                                        // superficie de techo
    geoFillL(bbuf, 0, GEO_HUD_H, LW, GEO_CEIL_Y - GEO_HUD_H, gFloorC);
    hLine(0, GEO_CEIL_Y,     LW, gNeon);
    hLine(0, GEO_CEIL_Y + 1, LW, mix565(gNeon, GEO_INK, 90));
  }
}

// ---- Suelo con textura de velocidad + huecos ----
static void geoDrawFloor(){
  geoFillL(bbuf, 0, GEO_FLOOR_Y, LW, LH - GEO_FLOOR_Y, gFloorC);
  uint16_t voidc = mix565(gFloorC, rgb565(0,0,0), 120);
  int off = (int)geoScroll % GEO_TS; if(off < 0) off += GEO_TS;
  for(int x = -off; x < LW; x += GEO_TS) vLine(x, GEO_FLOOR_Y, LH - GEO_FLOOR_Y, voidc);
  hLine(0, GEO_FLOOR_Y,     LW, gNeon);
  hLine(0, GEO_FLOOR_Y - 1, LW, mix565(gNeon, GEO_INK, 90));
  // Huecos: recortar el suelo (pozo) y remarcar bordes.
  int scroll = (int)geoScroll;
  for(int i = 0; i < gObsN; i++){
    if(gObs[i].tipo != OBS_HUECO) continue;
    int gx0 = gObs[i].x - scroll, w = gObs[i].param * GEO_TS;
    if(gx0 > LW || gx0 + w < 0) continue;
    geoFillL(bbuf, gx0, GEO_FLOOR_Y - 1, w, LH - GEO_FLOOR_Y + 1, voidc);
    vLine(gx0,     GEO_FLOOR_Y, 36, gNeon);
    vLine(gx0 + w, GEO_FLOOR_Y, 36, gNeon);
  }
}

// ---- Un bloque (real o falso: se dibujan IGUAL) ----
static void geoDrawBlock(int sx, int ntiles){
  int bh = ntiles * GEO_TS, by = GEO_FLOOR_Y - bh;
  fillRect(sx, by, GEO_TS, bh, GEO_INK);
  drawRect(sx, by, GEO_TS, bh, mix565(gNeon, GEO_INK, 90));
  hLine(sx, by, GEO_TS, gNeon);
  for(int k = 1; k < ntiles; k++) hLine(sx, by + k * GEO_TS, GEO_TS, mix565(gNeon, GEO_INK, 90));
  vLine(sx + GEO_TS / 2, by, bh, mix565(gNeon, GEO_INK, 90));
}

// ---- Obstaculos visibles (picos, bloques, portales) ----
static void geoDrawObstacles(){
  int scroll = (int)geoScroll;
  for(int i = 0; i < gObsN; i++){
    int sx = gObs[i].x - scroll;
    if(sx < -GEO_TS * 3 || sx > LW + GEO_TS) continue;       // recorte
    uint8_t tp = gObs[i].tipo;
    if(tp == OBS_PICO){
      fillTriangle(sx, GEO_FLOOR_Y, sx + GEO_TS, GEO_FLOOR_Y, sx + GEO_TS / 2, GEO_FLOOR_Y - GEO_TS, gNeon);
      fillTriangle(sx + 3, GEO_FLOOR_Y - 1, sx + GEO_TS - 3, GEO_FLOOR_Y - 1, sx + GEO_TS / 2, GEO_FLOOR_Y - GEO_TS + 6, GEO_INK);
    } else if(tp == OBS_PICO_T){
      fillTriangle(sx, GEO_CEIL_Y, sx + GEO_TS, GEO_CEIL_Y, sx + GEO_TS / 2, GEO_CEIL_Y + GEO_TS, gNeon);
      fillTriangle(sx + 3, GEO_CEIL_Y + 1, sx + GEO_TS - 3, GEO_CEIL_Y + 1, sx + GEO_TS / 2, GEO_CEIL_Y + GEO_TS - 6, GEO_INK);
    } else if(tp == OBS_BLOQUE || tp == OBS_BLOQUE_F){
      geoDrawBlock(sx, gObs[i].param);                       // real y falso: identicos
    } else if(tp == OBS_PORTAL_GRAV || tp == OBS_PORTAL_VEL ||
              (tp >= OBS_P_CUBO && tp <= OBS_P_NORMAL)){
      uint16_t col;
      if(tp == OBS_PORTAL_GRAV)      col = GEO_AMBER;
      else if(tp == OBS_PORTAL_VEL)  col = GEO_CYAN;
      else if(tp == OBS_P_MINI || tp == OBS_P_NORMAL) col = GEO_LIME;
      else                           col = GEO_PURP;         // portales de forma
      int px_ = sx + GEO_TS / 2;
      fillRectA(px_ - 15, GEO_CEIL_Y, 30, GEO_FLOOR_Y - GEO_CEIL_Y, col, 70);
      drawRect(px_ - 15, GEO_CEIL_Y, 30, GEO_FLOOR_Y - GEO_CEIL_Y, col);
      drawRect(px_ - 14, GEO_CEIL_Y + 1, 28, GEO_FLOOR_Y - GEO_CEIL_Y - 2, col);
      int cy = (GEO_CEIL_Y + GEO_FLOOR_Y) / 2;
      if(tp == OBS_PORTAL_GRAV){                              // flechas arriba/abajo
        fillTriangle(px_, cy - 26, px_ - 9, cy - 12, px_ + 9, cy - 12, col);
        fillTriangle(px_, cy + 26, px_ - 9, cy + 12, px_ + 9, cy + 12, col);
      } else if(tp == OBS_PORTAL_VEL){                        // chevrones (velocidad)
        fillTriangle(px_ - 10, cy - 14, px_ - 10, cy + 14, px_ + 4, cy, col);
        fillTriangle(px_ + 2,  cy - 14, px_ + 2,  cy + 14, px_ + 16, cy, col);
      } else if(tp == OBS_P_MINI || tp == OBS_P_NORMAL){      // simbolo de tamano
        int s = (tp == OBS_P_MINI) ? 6 : 12;
        drawRect(px_ - s, cy - s, 2 * s, 2 * s, col);
        fillRect(px_ - s + 2, cy - s + 2, 2 * s - 4, 2 * s - 4, mix565(col, GEO_INK, 120));
      } else {                                                // letra de la forma
        const char* g = "C";
        if(tp == OBS_P_SHIP) g = "S"; else if(tp == OBS_P_BALL) g = "B";
        else if(tp == OBS_P_WAVE) g = "W"; else if(tp == OBS_P_UFO) g = "U";
        drawTextC(px_, cy - 8, g, 2, col);
      }
    }
  }
}

// ---- Estela (cuadraditos que se desvanecen hacia atras) ----
static void geoDrawTrail(){
  int gPL = gMini ? (GEO_PL / 2) : GEO_PL;
  for(int i = 0; i < GEO_TRAIL_N; i++){
    if(!geoTrail[i].used) continue;
    float dist = geoScroll - geoTrail[i].scrollAt;
    if(dist >= GEO_TRAIL_FADE){ geoTrail[i].used = false; continue; }
    int sx = GEO_PLX + gPL / 2 - (int)dist;
    if(sx < -10){ geoTrail[i].used = false; continue; }
    float f = 1.0f - dist / GEO_TRAIL_FADE;
    int sz = (int)(3 + f * 11);
    uint8_t a = (uint8_t)(f * 150);
    fillRectA(sx - sz / 2, geoTrail[i].y - sz / 2, sz, sz, gSkin, a);
  }
}

// ---- Cuadrado rotado por sus 4 esquinas (cubo) ----
static void geoQuadRot(int cx, int cy, float h, float s, float c, uint16_t col){
  int x0 = cx + (int)(-h * c + h * s), y0 = cy + (int)(-h * s - h * c);
  int x1 = cx + (int)( h * c + h * s), y1 = cy + (int)( h * s - h * c);
  int x2 = cx + (int)( h * c - h * s), y2 = cy + (int)( h * s + h * c);
  int x3 = cx + (int)(-h * c - h * s), y3 = cy + (int)(-h * s + h * c);
  fillQuad(x0, y0, x1, y1, x2, y2, x3, y3, col);
}

// ---- Jugador: dibujo por forma (cx,cy = centro; r = radio segun tamano) ----
static void geoDrawPlayer(int cx, int cy, int r){
  uint16_t hi = gSkin, dk = mix565(gSkin, GEO_INK, 150), eye = rgb565(240,246,255);
  if(gForma == FRM_CUBO){
    float s = sinf(geoAngle), c = cosf(geoAngle);
    geoQuadRot(cx, cy, r,          s, c, hi);
    geoQuadRot(cx, cy, r * 0.62f,  s, c, dk);
    geoQuadRot(cx, cy, r * 0.28f,  s, c, eye);
  } else if(gForma == FRM_BALL){
    fillCircle(cx, cy, r, hi);
    fillCircle(cx, cy, (int)(r * 0.60f), dk);
    // marca que gira (sensacion de rodar)
    int mx = cx + (int)(cosf(geoAngle) * r * 0.55f);
    int my = cy + (int)(sinf(geoAngle) * r * 0.55f);
    fillCircle(mx, my, r > 12 ? 4 : 2, eye);
    drawCircle(cx, cy, r, mix565(hi, rgb565(255,255,255), 90));
  } else if(gForma == FRM_SHIP){
    // inclinacion segun velocidad vertical
    float tilt = geoVelY / GEO_SHIP_VMAX; if(tilt > 1) tilt = 1; if(tilt < -1) tilt = -1;
    int ty = (int)(tilt * r * 0.5f);
    fillTriangle(cx - r, cy - r + ty, cx - r, cy + r - ty, cx + r + 4, cy, hi);   // casco
    fillTriangle(cx - r + 3, cy - r + 3 + ty, cx - r + 3, cy + r - 3 - ty, cx + r, cy, dk);
    fillCircle(cx - 2, cy - r + 6, r > 12 ? 6 : 3, eye);                          // cabina
    fillCircle(cx - 2, cy - r + 6, r > 12 ? 3 : 2, gSkin);
  } else if(gForma == FRM_WAVE){
    // rombo que apunta en la direccion del movimiento
    int dy = (geoVelY < 0) ? -r : r;
    fillTriangle(cx - r, cy, cx + r, cy, cx, cy + dy, hi);
    fillTriangle(cx - r, cy, cx + r, cy, cx, cy - dy / 2, dk);
  } else { // FRM_UFO
    fillCircle(cx, cy - 1, r, hi);                                                // cupula
    geoFillL(bbuf, cx - r, cy, 2 * r, r / 2 + 2, dk);                             // base
    fillCircle(cx, cy - r / 2, r > 12 ? 4 : 2, eye);
    drawCircle(cx, cy - 1, r, mix565(hi, rgb565(255,255,255), 90));
  }
}

static void geoDrawParticles(){
  for(int i = 0; i < GEO_PART_N; i++){
    if(!geoPart[i].used) continue;
    float f = geoPart[i].life / 0.8f; if(f > 1) f = 1; if(f < 0) f = 0;
    int sz = 2 + (int)(f * 6);
    uint8_t a = (uint8_t)(f * 235);
    uint16_t col = (i & 1) ? gSkin : rgb565(240,246,255);
    fillRectA((int)geoPart[i].x - sz / 2, (int)geoPart[i].y - sz / 2, sz, sz, col, a);
  }
}

// ---- Banderita del ultimo checkpoint (solo Practica, si esta en pantalla) ----
static void geoDrawCheckpoint(){
  if(!gPractice || !geoCPset) return;
  int sx = GEO_PLX + (int)(geoCPscroll - geoScroll);         // deriva hacia la izquierda
  if(sx < -6 || sx > LW) return;
  vLine(sx, GEO_FLOOR_Y - 34, 34, rgb565(255,255,255));
  fillTriangle(sx, GEO_FLOOR_Y - 34, sx + 16, GEO_FLOOR_Y - 28, sx, GEO_FLOOR_Y - 22, rgb565(120,255,140));
}

// ---- HUD (salir + progreso + intentos + boton reiniciar CP en Practica) ----
static void geoDrawHUD(){
  geoFillL(bbuf, 0, 0, LW, GEO_HUD_H, rgb565(18,18,34));
  // Boton salir (vuelve al selector) -- esquina superior izquierda.
  fillRoundRect(10, 6, 46, 28, 8, rgb565(58,58,96));
  strokeSeg(36, 12, 26, 20, 2, GEO_TXT);
  strokeSeg(26, 20, 36, 28, 2, GEO_TXT);
  // Barra de progreso.
  int bx = 66, bxr = gPractice ? (LW - 180) : (LW - 90);
  int bw = bxr - bx, by = 13, bh = 12;
  fillRoundRect(bx, by, bw, bh, 6, rgb565(38,38,64));
  float pr = geoScroll / (float)gLen; if(pr < 0) pr = 0; if(pr > 1) pr = 1;
  if(pr > 0.01f) fillRoundRect(bx, by, (int)(bw * pr), bh, 6, gNeon);
  char p[10]; snprintf(p, sizeof(p), "%d%%", (int)(pr * 100));
  drawTextR(bxr + (gPractice ? 40 : 78), by + 1, p, 1, GEO_TXT);
  // Modo + intentos.
  char b[26]; snprintf(b, sizeof(b), "%s  #%d", gPractice ? "Practica" : "Normal", geoAttempts);
  drawTextR(LW - 8, 28, b, 1, mix565(GEO_TXT, gSky, 60));
  // Boton "Reiniciar CP" (solo Practica).
  if(gPractice){
    fillRoundRect(LW - 132, 6, 122, 28, 8, rgb565(70,54,110));
    drawTextC(LW - 71, 13, "Reiniciar CP", 1, GEO_TXT);
  }
}

// ---- Composicion de un frame de JUEGO (en bbuf) y volcado atomico ----
static void geoRenderGame(){
  gLand = true; setBuf(bbuf);
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  int gPL = gMini ? (GEO_PL / 2) : GEO_PL;
  geoDrawBackground();
  geoDrawFloor();
  geoDrawObstacles();
  geoDrawCheckpoint();
  if(geoState != GEO_DEAD){
    geoDrawTrail();
    geoDrawPlayer(GEO_PLX + gPL / 2, (int)geoPlayerY + gPL / 2, gPL / 2);
  } else {
    geoDrawParticles();
  }
  geoDrawHUD();
  if(geoState == GEO_DEAD){
    drawTextC(LW / 2, GEO_FLOOR_Y - 96, "Fallaste", 4, GEO_TXT);
  } else if(geoState == GEO_WIN){
    drawTextC(LW / 2, GEO_FLOOR_Y - 118, "\xC2\xA1Nivel completado!", 4, gNeon);
    drawTextC(LW / 2, GEO_FLOOR_Y - 78, "Toca para volver al selector", 2, GEO_TXT);
  }
  present(0, SCR_H - 1);
}

// #############################################################
// ##  SELECTOR DE NIVEL (carrusel, como el selector real de GD)
// #############################################################

// Estrellita de 4 puntas (dificultad) -- barata, sin depender de la fuente.
static void geoStar(int cx, int cy, int r, uint16_t col){
  int t = r / 3;
  fillTriangle(cx, cy - r, cx - t, cy, cx + t, cy, col);
  fillTriangle(cx, cy + r, cx - t, cy, cx + t, cy, col);
  fillTriangle(cx - r, cy, cx, cy - t, cx, cy + t, col);
  fillTriangle(cx + r, cy, cx, cy - t, cx, cy + t, col);
}

// Barra de progreso del selector (verde Normal / celeste Practica).
static void geoSelBar(int x, int y, int w, int h, uint16_t fillc, int pct, bool done, const char* fixed){
  fillRoundRect(x, y, w, h, h / 2, rgb565(28,30,44));
  drawRoundRect(x, y, w, h, h / 2, mix565(fillc, rgb565(255,255,255), 60));
  if(pct < 0) pct = 0; if(pct > 100) pct = 100;
  int fw = (done ? w : (w * pct / 100));
  if(fw > h) fillRoundRect(x, y, fw, h, h / 2, fillc);
  else if(fw > 0) fillRect(x + h / 2, y, fw, h, fillc);
  char t[16];
  if(fixed) snprintf(t, sizeof(t), "%s", fixed);
  else if(done) snprintf(t, sizeof(t), "COMPLETADO");
  else snprintf(t, sizeof(t), "%d%%", pct);
  drawTextC(x + w / 2, y + h / 2 - 7, t, 2, rgb565(255,255,255));
}

static void geoRenderSelect(){
  gLand = true; setBuf(bbuf);
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  uint16_t sky = GEO_LEVELS[gCurLevel].sky;
  uint16_t neon = GEO_LEVELS[gCurLevel].neon;
  uint16_t skin = GEO_LEVELS[gCurLevel].skin;
  geoFillL(bbuf, 0, 0, LW, LH, sky);
  // Franja decorativa superior (bloques estilo menu GD).
  geoFillL(bbuf, 0, 0, LW, 12, mix565(sky, rgb565(255,255,255), 40));
  for(int x = 0; x < LW; x += 60) geoFillL(bbuf, x + 6, 2, 42, 20, mix565(neon, sky, 120));

  // Boton salir (cierra la app) -- esquina superior izquierda.
  fillRoundRect(10, 8, 46, 28, 8, mix565(sky, GEO_INK, 110));
  strokeSeg(36, 14, 26, 22, 2, GEO_TXT);
  strokeSeg(26, 22, 36, 30, 2, GEO_TXT);

  // Flechas del carrusel.
  fillTriangle(44, 240, 84, 210, 84, 270, rgb565(255,255,255));
  fillTriangle(52, 240, 82, 216, 82, 264, mix565(sky, GEO_INK, 90));
  fillTriangle(756, 240, 716, 210, 716, 270, rgb565(255,255,255));
  fillTriangle(748, 240, 718, 216, 718, 264, mix565(sky, GEO_INK, 90));

  // Tarjeta central.
  int x0 = 168, y0 = 60, w = 464, h = 316;
  fillRoundRect(x0, y0, w, h, 18, mix565(sky, GEO_INK, 120));
  drawRoundRect(x0, y0, w, h, 18, mix565(neon, sky, 110));

  // "Cara del cubo" (cuadrado de color) + ojos, como el icono del nivel.
  int fx = x0 + 26, fy = y0 + 24, fs = 64;
  fillRoundRect(fx, fy, fs, fs, 12, skin);
  drawRoundRect(fx, fy, fs, fs, 12, mix565(skin, rgb565(255,255,255), 80));
  fillRect(fx + 16, fy + 24, 10, 12, rgb565(20,22,34));
  fillRect(fx + fs - 26, fy + 24, 10, 12, rgb565(20,22,34));

  // Nombre + dificultad.
  drawText(x0 + 104, y0 + 34, GEO_LEVELS[gCurLevel].name, 3, rgb565(255,255,255));
  char st[6]; snprintf(st, sizeof(st), "%d", GEO_LEVELS[gCurLevel].stars);
  int sw = textW(st, 2);
  drawText(x0 + w - 24 - sw - 18, y0 + 20, st, 2, rgb565(250,210,80));
  geoStar(x0 + w - 22, y0 + 26, 9, rgb565(250,210,80));

  // Barras Normal / Practica.
  drawTextC(x0 + w / 2, y0 + 118, "Normal Mode", 2, rgb565(230,236,250));
  geoSelBar(x0 + 44, y0 + 142, w - 88, 34, rgb565(70,220,90),
            geoBest[gCurLevel], geoDone[gCurLevel], NULL);
  drawTextC(x0 + w / 2, y0 + 202, "Practice Mode", 2, rgb565(230,236,250));
  geoSelBar(x0 + 44, y0 + 226, w - 88, 34, rgb565(90,205,240), 0, false, "Practicar");

  // Puntos indicadores del carrusel.
  for(int i = 0; i < 4; i++){
    int dx = LW / 2 - 3 * 22 / 2 + i * 22;
    if(i == gCurLevel) fillCircle(dx, 432, 6, rgb565(255,255,255));
    else               fillCircle(dx, 432, 4, mix565(sky, rgb565(255,255,255), 120));
  }
  drawTextC(LW / 2, 448, "Toca una barra para jugar", 1, mix565(rgb565(255,255,255), sky, 90));

  present(0, SCR_H - 1);
}

// #############################################################
// ##  MENU DE JUEGOS  (capa por encima de Geo Dash / Futbol)
// ##  ------------------------------------------------------
// ##  La app "Juegos" (slot 11 de APP_REG) ya NO entra directa a
// ##  Geo Dash: geoEnter() abre PRIMERO este menu, que deja elegir
// ##  "Geometry Dash" (motor original, intacto) o "Futbol". El
// ##  routing esta en geoTick() (mas abajo). El motor de Geo Dash
// ##  no se toca -> CERO regresiones; solo cambia el DESTINO del
// ##  boton salir de su selector (antes cerraba la app; ahora
// ##  vuelve a este menu). Todo lo nuevo lleva prefijo games*/fut*.
// #############################################################
enum { GM_MENU = 0, GM_GEO, GM_FUT };
static int gGameMode = GM_MENU;

// #############################################################
// ##  FUTBOL ARCADE  (modo landscape, camara alta con scroll)
// ##  ------------------------------------------------------
// ##  Mismo patron que Geo Dash: lienzo LOGICO 800x480 (LW x LH),
// ##  compone en bbuf y vuelca con present() (anti-flicker), ~30
// ##  fps con throttle por millis() (GEO_FRAME_MS) y fisica con
// ##  substep fijo (GEO_SUBSTEP), watchdog seguro (sin delays).
// ##  TODO el estado es static (sin heap): no se aloca ningun
// ##  framebuffer nuevo -- se dibuja directo sobre bbuf, igual que
// ##  geoRenderGame(). NINGUN struct nuevo (FutP) se usa como
// ##  PARAMETRO de funcion: se indexa por int, asi los prototipos
// ##  que auto-genera el IDE nunca referencian un tipo desconocido.
// ##
// ##  HARDWARE: input tactil de UN SOLO punto (struct Touch T). No
// ##  hay joystick ni botones fisicos, asi que los controles son
// ##  ZONAS tactiles dibujadas: un stick analogico virtual (mueve
// ##  al jugador con balon / mas cercano) y botones Sprint/Pase/
// ##  Tiro/Cambio. Al ser un solo dedo, mover y pulsar boton son
// ##  excluyentes por diseno: el stick vive en la mitad izquierda
// ##  y los botones en la derecha; Sprint es un TOGGLE y Pase/Tiro/
// ##  Cambio usan la ULTIMA direccion del stick (futFace) como
// ##  orientacion. Pseudo-3D barato: escalado por profundidad (Y
// ##  en cancha) con tablas Q8 precalculadas, sin trig por sprite.
// #############################################################

// ---- Geometria del campo (coords de MUNDO) ----
#define FUT_LEN     1400    // largo del campo (eje X mundo; porteria en 0 y en FUT_LEN)
#define FUT_WID     300     // ancho del campo (eje Y mundo; 0=fondo lejano, FUT_WID=cerca)
#define FUT_MIDY    150     // centro del ancho (FUT_WID/2)
#define FUT_GHALF   46      // medio-ancho de la porteria (en Y de mundo, centrada en FUT_MIDY)
#define FUT_CX      400     // X de pantalla que sigue la camara (LW/2)
#define FUT_HORIZON 116     // Y de pantalla de la touchline LEJANA (fondo del campo)
#define FUT_BOTTOM  452     // Y de pantalla de la touchline CERCANA

// ---- Fisica (unidades de mundo/seg; dt real via millis()) ----
#define FUT_MATCH_SEC   150.0f   // segundos reales de un partido completo (-> 90' de juego)
#define FUT_BASE_SPD    118.0f   // velocidad base de carrera de un jugador
#define FUT_SPRINT_MUL  1.62f    // multiplicador de Sprint
#define FUT_CARRY_SPD   108.0f   // velocidad del portador (algo mas lento que corriendo)
#define FUT_GK_SPD      96.0f    // velocidad del portero
#define FUT_PASS_SPD    380.0f   // velocidad del balon en un pase
#define FUT_SHOOT_SPD   540.0f   // velocidad del balon en un tiro
#define FUT_DRIBBLE     15.0f    // distancia del balon por delante del portador
#define FUT_CTRL_R      15.0f    // radio de control del balon suelto (jugador de campo)
#define FUT_GK_CTRL_R   26.0f    // radio de control/atajada del portero (mas amplio)
#define FUT_TACKLE_R    15.0f    // radio para intentar quitar el balon
#define FUT_SHOOT_RANGE 380.0f   // distancia a porteria a la que la IA se plantea tirar
#define FUT_BALL_G      460.0f   // gravedad del balon en el aire (altura Z)
#define FUT_BALL_FRIC   1.7f     // rozamiento del balon suelto en el suelo (por seg)

// ---- Controles virtuales (coords LOGICAS landscape) ----
#define FUT_STK_CX   118    // stick analogico: centro X
#define FUT_STK_CY   372    // stick analogico: centro Y
#define FUT_STK_R    66     // radio del stick
#define FUT_STK_DEAD 12     // zona muerta
#define FUT_BTN_R    30     // radio de los botones
enum { FBT_SPR = 0, FBT_PAS, FBT_TIR, FBT_CAM };
static const int16_t FUT_BTNX[4] = { 696, 696, 764, 628 };   // SPR arriba, PAS abajo, TIR der, CAM izq
static const int16_t FUT_BTNY[4] = { 306, 438, 372, 372 };

// ---- Roles y estados de IA (maquina de estados barata) ----
enum { FR_GK = 0, FR_DEF, FR_MID, FR_FWD };
enum { FA_HOLD = 0, FA_CHASE, FA_SUPPORT, FA_MARK, FA_GK, FA_CARRY };

// ---- Formacion base 4-3-3 del equipo 0 (ataca +X). El equipo 1 es
//      el espejo (x' = FUT_LEN - x). 11 jugadores por equipo. ----
static const int16_t FUT_FORM_X[11] = {  60, 300,300,300,300, 560,600,560, 860,940,860 };
static const int16_t FUT_FORM_Y[11] = { 150,  60,110,190,240,  90,150,210,  80,150,220 };
static const uint8_t FUT_FORM_R[11] = { FR_GK, FR_DEF,FR_DEF,FR_DEF,FR_DEF, FR_MID,FR_MID,FR_MID, FR_FWD,FR_FWD,FR_FWD };

// #############################################################
// ##  EQUIPOS (24-32) EN FLASH (.rodata) -- NO en RAM/PSRAM.
// ##  Struct compacto pedido: { char nombre[16]; uint8_t rating;
// ##  uint16_t colorA; uint16_t colorB; }. Sin escudos/logos con
// ##  derechos: el escudo se genera en runtime (forma + colores).
// ##  Incluye selecciones, clubes top y clubes peruanos.
// #############################################################
struct FutTeam { char nombre[16]; uint8_t rating; uint16_t colorA; uint16_t colorB; };
static const FutTeam FUT_TEAMS[] = {
  { "Peru",         78, rgb565(220,40,60),   rgb565(245,245,245) },  // 0  (por defecto)
  { "Argentina",    90, rgb565(110,190,235), rgb565(245,245,245) },
  { "Brasil",       91, rgb565(245,215,40),  rgb565(20,110,60)   },
  { "Uruguay",      84, rgb565(90,170,225),  rgb565(20,24,40)    },
  { "Chile",        79, rgb565(210,40,50),   rgb565(30,60,140)   },
  { "Colombia",     83, rgb565(245,210,40),  rgb565(30,70,160)   },
  { "Mexico",       81, rgb565(20,130,70),   rgb565(245,245,245) },
  { "Espana",       90, rgb565(200,30,40),   rgb565(30,40,120)   },
  { "Francia",      92, rgb565(40,70,170),   rgb565(245,245,245) },
  { "Alemania",     90, rgb565(240,240,245), rgb565(20,24,34)    },
  { "Inglaterra",   88, rgb565(245,245,245), rgb565(30,40,120)   },
  { "Italia",       88, rgb565(40,90,190),   rgb565(245,245,245) },
  { "Portugal",     89, rgb565(190,30,50),   rgb565(20,110,70)   },
  { "Paises Bajos", 86, rgb565(240,130,30),  rgb565(245,245,245) },
  { "Alianza Lima", 76, rgb565(30,60,150),   rgb565(245,245,245) },  // 14  peruano
  { "Universitario",76, rgb565(235,230,215), rgb565(140,30,50)   },  // 15  peruano (crema)
  { "S. Cristal",   75, rgb565(90,180,225),  rgb565(245,245,245) },  // 16  peruano (celeste)
  { "Barcelona",    90, rgb565(140,30,60),   rgb565(30,50,120)   },  // 17  (grana + azul)
  { "Real Madrid",  91, rgb565(245,245,245), rgb565(210,180,60)  },
  { "Man City",     90, rgb565(120,195,225), rgb565(245,245,245) },
  { "Liverpool",    88, rgb565(210,40,50),   rgb565(245,245,245) },
  { "Bayern",       90, rgb565(210,30,50),   rgb565(245,245,245) },
  { "PSG",          88, rgb565(30,40,90),    rgb565(210,40,60)   },
  { "Juventus",     86, rgb565(240,240,245), rgb565(20,22,30)    },
  { "Milan",        85, rgb565(200,30,45),   rgb565(20,22,30)    },
  { "Boca Juniors", 80, rgb565(30,50,130),   rgb565(230,190,60)  },
  { "River Plate",  80, rgb565(240,240,245), rgb565(210,40,60)   },
  { "Flamengo",     82, rgb565(210,40,50),   rgb565(20,22,30)    },
};
#define FUT_NTEAMS ((int)(sizeof(FUT_TEAMS) / sizeof(FUT_TEAMS[0])))

// #############################################################
// ##  ESTADO DEL PARTIDO (todo static, sin heap)
// #############################################################
#define FUT_NP 22
struct FutP { float x, y, vx, vy; uint8_t team; uint8_t role; uint8_t st; float dcd; };
static FutP futP[FUT_NP];

enum { FS_TEAM = 0, FS_OPP, FS_PLAY, FS_END };   // pantalla activa del modo Futbol
static int   futScreen  = FS_TEAM;
static int   futTeamSel[2] = { 0, 1 };           // [0]=tu equipo, [1]=rival (indices en FUT_TEAMS)
static int   futScoreA = 0, futScoreB = 0;
static float futPlaySec = 0;                     // tiempo de JUEGO acumulado (0..5400 = 0'..90')

static float futBx, futBy, futBz;                // balon: posicion mundo + altura Z
static float futBvx, futBvy, futBvz;             // balon: velocidad
static int   futOwner   = -1;                    // -1 = suelto, else indice del portador
static int   futCtrl    = 0;                     // jugador controlado (siempre del equipo 0)
static int   futLastTouch = -1;                  // ultimo que toco el balon (para cooldown de pase)
static float futTouchCd = 0;                     // cooldown para que el pasador no reciba su propio pase
static float futCam     = FUT_CX;                // X de mundo bajo la camara (sigue al balon)

static float   futMoveX = 0, futMoveY = 0;       // vector del stick (-1..1)
static float   futFaceX = 1, futFaceY = 0;       // ultima direccion (orientacion para pase/tiro)
static int     futStkKnobX = 0, futStkKnobY = 0; // desplazamiento visual de la perilla
static bool    futSprint = false;                // toggle de Sprint
static bool    futReqPass = false, futReqShoot = false, futReqSwitch = false, futReqSprint = false;

static uint32_t futFrameMs = 0;                  // millis() del frame anterior (dt)
static float    futDt = 0;                       // dt del frame actual (para la IA)
static uint32_t futFreezeUntil = 0;              // congela juego (saque / celebracion de gol)
static bool     futPending = false;              // hay un saque pendiente tras un gol
static int      futKickNext = 0;                 // equipo que saca tras el gol
static char     futFlash[16] = "";               // rotulo grande temporal ("GOL")

// ---- Tablas Q8 de perspectiva (pseudo-3D): profundidad d=wy/FUT_WID.
//      Se calculan UNA vez (powf/interp) -> por sprite solo hay lookups. ----
#define FUT_DSTEPS 64
static uint16_t futSyLUT[FUT_DSTEPS];   // Y de pantalla de los PIES a cada profundidad
static uint8_t  futPHt [FUT_DSTEPS];    // alto del sprite (px) a cada profundidad
static uint8_t  futXsLUT[FUT_DSTEPS];   // Q8: compresion horizontal (trapecio) * 256
static bool     futLUTready = false;

static void futBuildLUT(){
  for(int i = 0; i < FUT_DSTEPS; i++){
    float t = (float)i / (FUT_DSTEPS - 1);          // 0=lejos(arriba) .. 1=cerca(abajo)
    float ty = powf(t, 1.22f);                       // leve curva de perspectiva en Y
    futSyLUT[i] = (uint16_t)(FUT_HORIZON + ty * (FUT_BOTTOM - FUT_HORIZON));
    futPHt[i]   = (uint8_t)(12 + t * 34);            // 12 px (lejos) .. 46 px (cerca)
    float xs    = 0.60f + t * 0.40f;                 // 0.60 (lejos) .. 1.00 (cerca) -> trapecio
    futXsLUT[i] = (uint8_t)(xs * 256.0f + 0.5f);
  }
  futLUTready = true;
}

// ---- PRNG: reutiliza el xorshift de Geo Dash (geoRand/geoRandf) ----
static inline float futErrf(){ return geoRandf() * 2.0f - 1.0f; }   // [-1..1]

// #############################################################
// ##  TRANSFORMACIONES MUNDO -> PANTALLA (afines + LUT, sin trig)
// #############################################################
static inline int futDIdx(float wy){
  int d = (int)(wy * (FUT_DSTEPS - 1) / FUT_WID);
  if(d < 0) d = 0; if(d > FUT_DSTEPS - 1) d = FUT_DSTEPS - 1;
  return d;
}
static inline int futGX(float wx, float wy){          // X de pantalla en el suelo
  int d = futDIdx(wy);
  return FUT_CX + (int)(((long)((int)(wx - futCam)) * futXsLUT[d]) >> 8);
}
static inline int futGY(float wy){ return futSyLUT[futDIdx(wy)]; }   // Y de pantalla (pies)

static inline float futDist2(int i, int j){
  float dx = futP[i].x - futP[j].x, dy = futP[i].y - futP[j].y;
  return dx * dx + dy * dy;
}
static inline float futHomeX(int i){
  int k = i % 11; float x = FUT_FORM_X[k];
  return (i >= 11) ? (FUT_LEN - x) : x;
}
static inline float futHomeY(int i){ return FUT_FORM_Y[i % 11]; }
static inline float futGoalX(int team){ return (team == 0) ? (float)FUT_LEN : 0.0f; }   // porteria que ATACA
static inline float futOwnX (int team){ return (team == 0) ? 0.0f : (float)FUT_LEN; }   // porteria que DEFIENDE
static inline float futRating(int team){ return FUT_TEAMS[futTeamSel[team]].rating / 99.0f; }  // 0..1

// #############################################################
// ##  PRIMITIVAS DE DIBUJO PROPIAS DEL FUTBOL
// #############################################################
// Elipse rellena y aplastada (sombra bajo pies / balon)
static void futShadow(int cx, int cy, int rx, int ry, uint16_t col, uint8_t a){
  if(rx < 1) rx = 1; if(ry < 1) ry = 1;
  for(int dy = -ry; dy <= ry; dy++){
    int dx = (int)(rx * sqrtf(1.0f - (float)dy * dy / ((float)ry * ry)));
    hLineA(cx - dx, cy + dy, 2 * dx + 1, col, a);
  }
}
// Elipse (contorno) para el circulo central
static void futEllipse(int cx, int cy, int rx, int ry, uint16_t col){
  if(rx < 2) rx = 2; if(ry < 1) ry = 1;
  int pxp = cx + rx, pyp = cy;
  for(int a = 1; a <= 40; a++){
    float th = a * (6.2831853f / 40);
    int x = cx + (int)(rx * cosf(th)), y = cy + (int)(ry * sinf(th));
    strokeSeg(pxp, pyp, x, y, 1, col); pxp = x; pyp = y;
  }
}
// Escudo generado en runtime (forma pentagonal + franja) con los colores del equipo
static void futCrest(int cx, int cy, int w, int h, uint16_t colA, uint16_t colB){
  int x0 = cx - w / 2, y0 = cy - h / 2, hr = h * 3 / 5;
  fillRect(x0, y0, w, hr, colA);
  fillTriangle(x0, y0 + hr, x0 + w, y0 + hr, cx, y0 + h, colA);
  int sw = w / 3; if(sw < 3) sw = 3;
  fillRect(cx - sw / 2, y0, sw, hr, colB);
  fillTriangle(cx - sw / 2, y0 + hr, cx + sw / 2, y0 + hr, cx, y0 + h - 3, colB);
  uint16_t edge = rgb565(238,240,246);
  drawRect(x0, y0, w, hr, edge);
  strokeSeg(x0, y0 + hr, cx, y0 + h, 1, edge);
  strokeSeg(x0 + w, y0 + hr, cx, y0 + h, 1, edge);
}

// #############################################################
// ##  BUSQUEDAS AUXILIARES (nearest, etc.)
// #############################################################
static int futNearestOutfield(int team, float x, float y){   // el mas cercano (sin portero)
  int best = -1; float bd = 1e18f;
  for(int k = 0; k < 11; k++){
    int i = team * 11 + k;
    if(futP[i].role == FR_GK) continue;
    float dx = futP[i].x - x, dy = futP[i].y - y, d = dx * dx + dy * dy;
    if(d < bd){ bd = d; best = i; }
  }
  return best;
}
static int futNearestToBall(int team){ return futNearestOutfield(team, futBx, futBy); }
static int futNearestOpp(int i){
  int opp = (futP[i].team == 0) ? 1 : 0, best = -1; float bd = 1e18f;
  for(int k = 0; k < 11; k++){
    int j = opp * 11 + k; float d = futDist2(i, j);
    if(d < bd){ bd = d; best = j; }
  }
  return best;
}

// #############################################################
// ##  PASE / TIRO / CAMBIO / QUITE  (acciones)
// #############################################################
static void futSetLoose(int from, float dirx, float diry, float speed, float upz){
  float d = sqrtf(dirx * dirx + diry * diry); if(d < 0.0001f){ dirx = futFaceX; diry = futFaceY; d = 1; }
  futBvx = dirx / d * speed; futBvy = diry / d * speed; futBvz = upz;
  futOwner = -1; futLastTouch = from; futTouchCd = 0.22f; futBz = 0;
}
// Pase del jugador humano (solo si su equipo tiene el balon)
static void futDoPass(){
  if(futOwner < 0 || futP[futOwner].team != 0) return;
  int o = futOwner, best = -1; float bestSc = -1e18f;
  for(int k = 0; k < 11; k++){
    int r = k; if(r == o || futP[r].role == FR_GK) continue;
    float dx = futP[r].x - futP[o].x, dy = futP[r].y - futP[o].y;
    float dist = sqrtf(dx * dx + dy * dy);
    if(dist < 28 || dist > 380) continue;
    float dot = (dx * futFaceX + dy * futFaceY) / dist;         // alineado con la direccion apuntada
    float fwd = (futGoalX(0) > futP[o].x) ? (dx > 0 ? 0.4f : -0.3f) : 0;
    float sc = dot + fwd - dist * 0.0012f;
    if(sc > bestSc){ bestSc = sc; best = r; }
  }
  if(best < 0) return;
  // Precision del pase: escala con el rating propio, con margen de error MINIMO (>=10%).
  float err = 0.10f + (1.0f - futRating(0)) * 0.16f;
  float lead = 0.12f;
  float tx = futP[best].x + futP[best].vx * lead, ty = futP[best].y + futP[best].vy * lead;
  float dx = tx - futBx + futErrf() * err * 90, dy = ty - futBy + futErrf() * err * 90;
  futSetLoose(o, dx, dy, FUT_PASS_SPD, 0);
}
// Tiro del jugador humano
static void futDoShoot(){
  if(futOwner < 0 || futP[futOwner].team != 0) return;
  int o = futOwner;
  float gx = futGoalX(0), gy = FUT_MIDY;
  float dx = gx - futBx, dy = gy - futBy;
  dx += futFaceX * 60; dy += futFaceY * 40;                     // sesga con lo apuntado
  float err = 0.10f + (1.0f - futRating(0)) * 0.14f;
  dy += futErrf() * err * 120;
  futSetLoose(o, dx, dy, FUT_SHOOT_SPD, 70 + geoRandf() * 40);  // con algo de altura
  futFlash[0] = 0;
}
// Cambiar de jugador controlado (util cuando NO tienes el balon)
static void futDoSwitch(){
  int start = futCtrl, best = -1; float bd = 1e18f;
  for(int k = 0; k < 11; k++){
    int i = k; if(i == start || futP[i].role == FR_GK) continue;
    float dx = futP[i].x - futBx, dy = futP[i].y - futBy, d = dx * dx + dy * dy;
    if(d < bd){ bd = d; best = i; }
  }
  if(best >= 0) futCtrl = best;
}

// #############################################################
// ##  IA (maquina de estados barata; sin pathfinding)
// #############################################################
static void futSteer(int i, float tx, float ty, float maxspd){
  float dx = tx - futP[i].x, dy = ty - futP[i].y, d = sqrtf(dx * dx + dy * dy);
  if(d > 1.5f){ futP[i].vx = dx / d * maxspd; futP[i].vy = dy / d * maxspd; }
  else { futP[i].vx = 0; futP[i].vy = 0; }
}
// Tiro/pase de un portador controlado por la IA (equipo del portador)
static void futAIShoot(int i){
  int t = futP[i].team; float gx = futGoalX(t);
  float dx = gx - futBx, dy = FUT_MIDY - futBy;
  float err = 0.10f + (1.0f - futRating(t)) * 0.16f;            // margen MINIMO 10% aun en top
  dy += futErrf() * err * 130;
  futSetLoose(i, dx, dy, FUT_SHOOT_SPD, 60 + geoRandf() * 40);
}
static void futAIPass(int i){
  int t = futP[i].team, best = -1; float bestSc = -1e18f;
  for(int k = 0; k < 11; k++){
    int r = t * 11 + k; if(r == i || futP[r].role == FR_GK) continue;
    float dx = futP[r].x - futP[i].x, dy = futP[r].y - futP[i].y, dist = sqrtf(dx * dx + dy * dy);
    if(dist < 30 || dist > 340) continue;
    float toward = (futGoalX(t) - futP[i].x);                   // preferir hacia el ataque
    float fwd = ((dx > 0) == (toward > 0)) ? 0.5f : -0.2f;
    // penaliza si hay un rival cerca del receptor
    int op = futNearestOpp(r); float opd = op >= 0 ? sqrtf(futDist2(r, op)) : 999;
    float sc = fwd + opd * 0.004f - dist * 0.0011f;
    if(sc > bestSc){ bestSc = sc; best = r; }
  }
  if(best < 0){ futAIShoot(i); return; }
  float err = 0.10f + (1.0f - futRating(t)) * 0.15f;
  float dx = futP[best].x - futBx + futErrf() * err * 90;
  float dy = futP[best].y - futBy + futErrf() * err * 90;
  futSetLoose(i, dx, dy, FUT_PASS_SPD, 0);
}
// Decision del portador IA: regatear / tirar / pasar
static void futAICarry(int i){
  int t = futP[i].team; float skill = futRating(t);
  float gx = futGoalX(t), gy = FUT_MIDY;
  float dgx = gx - futP[i].x, dgy = gy - futP[i].y, dg = sqrtf(dgx * dgx + dgy * dgy);
  int op = futNearestOpp(i); float pd = op >= 0 ? sqrtf(futDist2(i, op)) : 999;
  futP[i].dcd -= futDt;
  if(futP[i].dcd <= 0){
    bool inRange = (dg < FUT_SHOOT_RANGE);
    if(inRange && geoRandf() < (0.28f + 0.55f * skill)){ futAIShoot(i); futP[i].dcd = 0.7f; return; }
    if(pd < 28 && geoRandf() < (0.35f + 0.45f * skill)){ futAIPass(i); futP[i].dcd = 0.6f; return; }
    futP[i].dcd = 0.14f + (1.0f - skill) * 0.34f;               // reaccion: menos rating -> repiensa mas lento
  }
  // Regate hacia porteria, esquivando levemente la presion
  float tx = gx, ty = gy;
  if(pd < 40 && op >= 0){ ty += (futP[i].y < futP[op].y) ? -50 : 50; }
  futSteer(i, tx, ty, FUT_CARRY_SPD * (0.9f + 0.2f * skill));
  futP[i].st = FA_CARRY;
}
static void futGKTarget(int i, float* tx, float* ty){
  int t = futP[i].team; float ogx = futOwnX(t);
  *tx = ogx + (t == 0 ? 42 : -42);
  float y = futBy; if(y < FUT_MIDY - 72) y = FUT_MIDY - 72; if(y > FUT_MIDY + 72) y = FUT_MIDY + 72;
  *ty = y;
}
static void futAI(){
  int poss = (futOwner >= 0) ? futP[futOwner].team : -1;
  if(futOwner >= 0 && futP[futOwner].team == 0) futCtrl = futOwner;   // tu equipo con balon -> controlas al portador
  int chaser0 = futNearestToBall(0), chaser1 = futNearestToBall(1);
  for(int i = 0; i < FUT_NP; i++){
    if(i == futCtrl && futP[i].team == 0) continue;                  // al controlado lo mueve el humano
    int t = futP[i].team, role = futP[i].role;
    if(role == FR_GK){ float tx, ty; futGKTarget(i, &tx, &ty); futSteer(i, tx, ty, FUT_GK_SPD); futP[i].st = FA_GK; continue; }
    float spd = (role == FR_FWD) ? 126 : (role == FR_MID ? 118 : 112);
    if(poss == t){
      if(futOwner == i){ futAICarry(i); continue; }                  // portador IA
      // apoyo: empuja hacia el ataque y abre espacios
      float tx = futHomeX(i) * 0.40f + futBx * 0.28f + futGoalX(t) * 0.32f;
      float ty = futHomeY(i) * 0.45f + futBy * 0.55f;
      futSteer(i, tx, ty, spd); futP[i].st = FA_SUPPORT;
    } else if(poss == (1 - t)){
      int chaser = (t == 0) ? chaser0 : chaser1;
      if(i == chaser){ futSteer(i, futBx, futBy, spd * 1.14f); futP[i].st = FA_CHASE; }
      else {           // marca zonal: posicion base desplazada hacia el balon
        float tx = futHomeX(i) * 0.62f + futBx * 0.24f + futOwnX(t) * 0.14f;
        float ty = futHomeY(i) * 0.50f + futBy * 0.50f;
        futSteer(i, tx, ty, spd * 0.96f); futP[i].st = FA_MARK;
      }
    } else {           // balon suelto: el mas cercano de cada equipo va a por el
      int chaser = (t == 0) ? chaser0 : chaser1;
      if(i == chaser){ futSteer(i, futBx, futBy, spd * 1.08f); futP[i].st = FA_CHASE; }
      else { float tx = futHomeX(i) * 0.66f + futBx * 0.20f; float ty = futHomeY(i) * 0.55f + futBy * 0.45f;
             futSteer(i, tx, ty, spd * 0.9f); futP[i].st = FA_HOLD; }
    }
  }
}

// #############################################################
// ##  FISICA + COLISION (un substep)  ·  watchdog-safe (sin delay)
// #############################################################
static int futBallController(){
  int best = -1; float bd = 1e18f;
  for(int i = 0; i < FUT_NP; i++){
    if(i == futLastTouch && futTouchCd > 0) continue;               // el pasador no recibe su propio pase
    float r = (futP[i].role == FR_GK) ? FUT_GK_CTRL_R : FUT_CTRL_R;
    float dx = futP[i].x - futBx, dy = futP[i].y - futBy, d2 = dx * dx + dy * dy;
    if(d2 <= r * r && d2 < bd){ bd = d2; best = i; }
  }
  return best;
}
static void futGoalKick(int defTeam){                                // saque de porteria
  int gk = defTeam * 11 + 0;
  futBx = futP[gk].x; futBy = futP[gk].y; futBz = 0;
  futBvx = futBvy = futBvz = 0; futOwner = gk; futLastTouch = gk; futTouchCd = 0;
  if(defTeam == 0) futCtrl = gk;
}
static void futGoal(int team){
  if(team == 0) futScoreA++; else futScoreB++;
  snprintf(futFlash, sizeof(futFlash), "GOL");
  futFreezeUntil = millis() + 1500;
  futPending = true; futKickNext = 1 - team;                        // saca el que encajo
  futOwner = -1;
}
static void futStep(float dt){
  // --- Quite: rivales cerca del portador intentan robar ---
  if(futOwner >= 0){
    int o = futOwner, ot = futP[o].team;
    for(int k = 0; k < 11; k++){
      int j = (1 - ot) * 11 + k;
      if(futP[j].role == FR_GK) continue;
      if(futDist2(o, j) <= FUT_TACKLE_R * FUT_TACKLE_R){
        float sk = futRating(futP[j].team);
        float prob = (1.4f + 2.2f * sk) * dt;                       // por segundo, escalado por rating
        if(geoRandf() < prob){
          futOwner = j; futLastTouch = j; futTouchCd = 0.14f;
          if(futP[j].team == 0) futCtrl = j;
          break;
        }
      }
    }
  }
  // --- Integracion de jugadores ---
  for(int i = 0; i < FUT_NP; i++){
    futP[i].x += futP[i].vx * dt; futP[i].y += futP[i].vy * dt;
    if(futP[i].x < 4) futP[i].x = 4; if(futP[i].x > FUT_LEN - 4) futP[i].x = FUT_LEN - 4;
    if(futP[i].y < 6) futP[i].y = 6; if(futP[i].y > FUT_WID - 6) futP[i].y = FUT_WID - 6;
  }
  if(futTouchCd > 0) futTouchCd -= dt;
  // --- Balon ---
  if(futOwner >= 0){
    int o = futOwner; float fdx, fdy;
    if(o == futCtrl && futP[o].team == 0){ fdx = futFaceX; fdy = futFaceY; }
    else { float d = sqrtf(futP[o].vx * futP[o].vx + futP[o].vy * futP[o].vy);
           if(d > 4){ fdx = futP[o].vx / d; fdy = futP[o].vy / d; }
           else { fdx = (futGoalX(futP[o].team) > futP[o].x) ? 1 : -1; fdy = 0; } }
    float fn = sqrtf(fdx * fdx + fdy * fdy); if(fn < 0.001f){ fdx = 1; fdy = 0; fn = 1; }
    futBx = futP[o].x + fdx / fn * FUT_DRIBBLE;
    futBy = futP[o].y + fdy / fn * FUT_DRIBBLE;
    // Mantener el balon dentro del campo aunque el portador este pegado a una
    // linea: si no, un tiro calculado desde una X fuera de rango saldria invertido.
    if(futBx < 2) futBx = 2; if(futBx > FUT_LEN - 2) futBx = FUT_LEN - 2;
    if(futBy < 2) futBy = 2; if(futBy > FUT_WID - 2) futBy = FUT_WID - 2;
    futBz = 0; futBvx = futBvy = futBvz = 0;
  } else {
    futBx += futBvx * dt; futBy += futBvy * dt;
    float fr = 1.0f - FUT_BALL_FRIC * dt; if(fr < 0) fr = 0;
    futBvx *= fr; futBvy *= fr;
    if(futBz > 0 || futBvz != 0){
      futBz += futBvz * dt; futBvz -= FUT_BALL_G * dt;
      if(futBz <= 0){ futBz = 0; if(futBvz < 0){ futBvz = -futBvz * 0.42f; if(futBvz < 24) futBvz = 0; } }
    }
    if(futBy < 4){ futBy = 4; futBvy = -futBvy * 0.5f; }            // rebote en las bandas
    if(futBy > FUT_WID - 4){ futBy = FUT_WID - 4; futBvy = -futBvy * 0.5f; }
    // --- Linea de gol / porteria ---
    bool inMouth = (futBy > FUT_MIDY - FUT_GHALF && futBy < FUT_MIDY + FUT_GHALF && futBz < 46);
    if(futBx >= FUT_LEN - 2){
      if(inMouth){ futGoal(0); return; } else { futGoalKick(1); return; }
    } else if(futBx <= 2){
      if(inMouth){ futGoal(1); return; } else { futGoalKick(0); return; }
    }
    int g = futBallController();
    if(g >= 0){ futOwner = g; futBvx = futBvy = futBvz = 0; futBz = 0; if(futP[g].team == 0) futCtrl = g; }
  }
}

// ---- Colocar la formacion (saque). kickTeam pone un jugador al centro. ----
static void futResetFormation(int kickTeam){
  for(int t = 0; t < 2; t++){
    for(int k = 0; k < 11; k++){
      int i = t * 11 + k;
      float x = FUT_FORM_X[k], y = FUT_FORM_Y[k];
      if(t == 1) x = FUT_LEN - x;
      if(t == 0 && x > 688) x = 688;                               // en el saque, cada equipo en su campo
      if(t == 1 && x < 712) x = 712;
      futP[i].x = x; futP[i].y = y; futP[i].vx = 0; futP[i].vy = 0;
      futP[i].team = (uint8_t)t; futP[i].role = FUT_FORM_R[k]; futP[i].st = FA_HOLD; futP[i].dcd = 0;
    }
  }
  int kick = kickTeam * 11 + 6;                                    // el mediocentro saca
  futP[kick].x = (kickTeam == 0) ? 686 : 714; futP[kick].y = FUT_MIDY;
  futBx = 700; futBy = FUT_MIDY; futBz = 0; futBvx = futBvy = futBvz = 0;
  futOwner = -1; futLastTouch = -1; futTouchCd = 0; futCam = 700;
  futFaceX = (kickTeam == 0) ? 1 : -1; futFaceY = 0;
  futCtrl = futNearestToBall(0);
}

// #############################################################
// ##  DIBUJO DEL CAMPO (coords LOGICAS 800x480; escribe en bbuf)
// #############################################################
static void futDrawMarkings(){
  uint16_t ln = rgb565(232,240,236);
  int c00x = futGX(0,0),        c00y = futGY(0);
  int c10x = futGX(FUT_LEN,0),  c10y = futGY(0);
  int c01x = futGX(0,FUT_WID),  c01y = futGY(FUT_WID);
  int c11x = futGX(FUT_LEN,FUT_WID), c11y = futGY(FUT_WID);
  strokeSeg(c00x, c00y, c10x, c10y, 1, ln);                        // touchline lejana
  strokeSeg(c01x, c01y, c11x, c11y, 1, ln);                        // touchline cercana
  strokeSeg(c00x, c00y, c01x, c01y, 1, ln);                        // fondo izq (linea de gol)
  strokeSeg(c10x, c10y, c11x, c11y, 1, ln);                        // fondo der
  // Linea de medio campo + circulo central
  int mid = FUT_LEN / 2;
  strokeSeg(futGX(mid,0), futGY(0), futGX(mid,FUT_WID), futGY(FUT_WID), 1, ln);
  int ccx = futGX(mid, FUT_MIDY), ccy = futGY(FUT_MIDY);
  int crx = (futGX(mid + 70, FUT_MIDY) - futGX(mid - 70, FUT_MIDY)) / 2;
  int cry = (futGY(FUT_MIDY + 42) - futGY(FUT_MIDY - 42)) / 2;
  futEllipse(ccx, ccy, crx, cry, ln);
  fillCircle(ccx, ccy, 2, ln);
  // Areas (una en cada porteria): tres lineas del rectangulo
  for(int s = 0; s < 2; s++){
    float bx = (s == 0) ? 0 : (FUT_LEN - 160);
    float bx2 = (s == 0) ? 160 : FUT_LEN;
    float edge = (s == 0) ? bx2 : bx;                              // linea vertical interior del area
    strokeSeg(futGX(edge, FUT_MIDY - 100), futGY(FUT_MIDY - 100),
              futGX(edge, FUT_MIDY + 100), futGY(FUT_MIDY + 100), 1, ln);
    strokeSeg(futGX(bx, FUT_MIDY - 100), futGY(FUT_MIDY - 100),
              futGX(bx2, FUT_MIDY - 100), futGY(FUT_MIDY - 100), 1, ln);
    strokeSeg(futGX(bx, FUT_MIDY + 100), futGY(FUT_MIDY + 100),
              futGX(bx2, FUT_MIDY + 100), futGY(FUT_MIDY + 100), 1, ln);
  }
  // Porterias (postes + larguero + red simple)
  for(int s = 0; s < 2; s++){
    float gx = (s == 0) ? 0 : FUT_LEN;
    int d = futDIdx(FUT_MIDY); int gh = futPHt[d] + 8;             // alto en pantalla
    int p0x = futGX(gx, FUT_MIDY - FUT_GHALF), p0y = futGY(FUT_MIDY - FUT_GHALF);
    int p1x = futGX(gx, FUT_MIDY + FUT_GHALF), p1y = futGY(FUT_MIDY + FUT_GHALF);
    uint16_t net = rgb565(220,226,230);
    // red
    for(int n = 1; n < 5; n++){
      int yy0 = p0y - gh * n / 5, yy1 = p1y - gh * n / 5;
      strokeSeg(p0x, yy0, p1x, yy1, 1, mix565(net, rgb565(60,110,70), 150));
    }
    strokeSeg(p0x, p0y, p0x, p0y - gh, 1, rgb565(245,245,245));    // poste 1
    strokeSeg(p1x, p1y, p1x, p1y - gh, 1, rgb565(245,245,245));    // poste 2
    strokeSeg(p0x, p0y - gh, p1x, p1y - gh, 1, rgb565(245,245,245)); // larguero
  }
}
static void futDrawField(){
  // Cielo + tribuna sobre el horizonte
  geoFillL(bbuf, 0, 0, LW, FUT_HORIZON, rgb565(58,68,104));
  geoFillL(bbuf, 0, FUT_HORIZON - 30, LW, 30, rgb565(38,42,58));
  int soff = ((int)futCam) % 16; if(soff < 0) soff += 16;
  for(int x = -soff; x < LW; x += 16)                              // publico (motas)
    geoFillL(bbuf, x + 3, FUT_HORIZON - 26, 6, 20, rgb565(70,78,104));
  // Cesped base
  geoFillL(bbuf, 0, FUT_HORIZON, LW, LH - FUT_HORIZON, rgb565(38,150,54));
  // Franjas de cortado (ancladas al mundo -> hacen scroll con la camara)
  int sw = 48;
  int n0 = (((int)futCam - LW / 2) / sw) - 2;
  for(int n = n0; ; n++){
    int sx0 = FUT_CX + (n * sw - (int)futCam);
    if(sx0 > LW) break;
    int a = sx0 < 0 ? 0 : sx0, b = sx0 + sw; if(b > LW) b = LW;
    if(b > a){ uint16_t g = (n & 1) ? rgb565(44,164,60) : rgb565(33,138,48);
               geoFillL(bbuf, a, FUT_HORIZON, b - a, LH - FUT_HORIZON, g); }
  }
  futDrawMarkings();
}

// ---- Un jugador (por indice; sin pasar el struct como parametro) ----
static void futDrawPlayer(int i){
  int d = futDIdx(futP[i].y);
  int sx = futGX(futP[i].x, futP[i].y);
  int fy = futSyLUT[d], ph = futPHt[d];
  if(sx < -30 || sx > LW + 30) return;
  int tid = futTeamSel[futP[i].team];
  uint16_t colA = FUT_TEAMS[tid].colorA, colB = FUT_TEAMS[tid].colorB;
  if(futP[i].role == FR_GK){ colA = rgb565(40,220,130); colB = rgb565(18,40,30); }   // portero distinto
  futShadow(sx, fy + 1, ph * 42 / 100 + 3, ph * 15 / 100 + 2, rgb565(12,32,14), 95);
  int legH = ph * 30 / 100, bodyH = ph * 30 / 100, shortH = ph * 22 / 100;
  int bw = ph * 46 / 100; if(bw < 4) bw = 4;
  int legW = ph * 12 / 100; if(legW < 1) legW = 1;
  int headR = ph * 16 / 100; if(headR < 2) headR = 2;
  int topBody = fy - legH - shortH - bodyH;
  fillRect(sx - legW - 1, fy - legH, legW, legH, rgb565(28,30,40));           // piernas
  fillRect(sx + 1, fy - legH, legW, legH, rgb565(28,30,40));
  fillRect(sx - bw / 2, fy - legH - shortH, bw, shortH, colB);                // short
  fillRect(sx - bw / 2, topBody, bw, bodyH, colA);                            // camiseta
  drawRect(sx - bw / 2, topBody, bw, bodyH, mix565(colA, rgb565(0,0,0), 90));
  fillCircle(sx, topBody - headR + 1, headR, rgb565(226,182,142));            // cabeza
  if(i == futCtrl && futP[i].team == 0){                                      // indicador "1UP" (flecha)
    int ay = topBody - headR * 2 - 6 - ((millis() / 300) % 2) * 3;
    fillTriangle(sx - 7, ay - 8, sx + 7, ay - 8, sx, ay, rgb565(245,60,60));
    drawTextC(sx, ay - 20, "1UP", 1, rgb565(245,240,120));
  }
}
static void futDrawBall(){
  int d = futDIdx(futBy);
  int sx = futGX(futBx, futBy), fy = futSyLUT[d];
  int r = futPHt[d] * 13 / 100; if(r < 2) r = 2;
  int zoff = (int)(futBz * (futPHt[d] / 42.0f));
  futShadow(sx, fy + 1, r + 1, r / 2 + 1, rgb565(12,32,14), 80);
  fillCircle(sx, fy - r - zoff, r, rgb565(245,245,248));
  drawCircle(sx, fy - r - zoff, r, rgb565(60,64,70));
}

// ---- HUD arcade: marcador arriba, tiempo al centro, nombres ----
static void futFmtClock(char* out, int n){
  int gs = (int)futPlaySec; if(gs > 5400) gs = 5400;
  snprintf(out, n, "%02d:%02d", gs / 60, gs % 60);
}
static void futDrawHUD(){
  fillRectA(0, 0, LW, 46, rgb565(12,14,26), 214);
  hLineA(0, 46, LW, rgb565(0,0,0), 120);
  // Boton salir / pausa (esquina superior izquierda)
  fillRoundRect(6, 6, 34, 22, 6, rgb565(78,86,126));
  fillRect(15, 10, 4, 14, rgb565(240,242,248)); fillRect(23, 10, 4, 14, rgb565(240,242,248));
  int idA = futTeamSel[0], idB = futTeamSel[1];
  uint16_t gold = rgb565(250,214,90), txt = rgb565(238,242,250);
  // Izquierda: escudo + nombre + marcador A
  futCrest(62, 24, 24, 28, FUT_TEAMS[idA].colorA, FUT_TEAMS[idA].colorB);
  drawText(80, 6, FUT_TEAMS[idA].nombre, 1, txt);
  char s[8]; snprintf(s, sizeof(s), "%d", futScoreA); drawText(80, 18, s, 3, gold);
  // Derecha: escudo + nombre + marcador B
  futCrest(LW - 62, 24, 24, 28, FUT_TEAMS[idB].colorA, FUT_TEAMS[idB].colorB);
  drawTextR(LW - 80, 6, FUT_TEAMS[idB].nombre, 1, txt);
  snprintf(s, sizeof(s), "%d", futScoreB); drawTextR(LW - 80, 18, s, 3, gold);
  // Centro: cronometro (tiempo de juego 0'..90')
  char tm[8]; futFmtClock(tm, sizeof(tm));
  drawTextC(LW / 2, 6, tm, 3, gold);
}

// ---- Controles virtuales (stick + botones), estilo del sistema ----
static void futDrawBtn(int id, const char* label, bool active, uint16_t col){
  int cx = FUT_BTNX[id], cy = FUT_BTNY[id];
  fillCircleA(cx, cy, FUT_BTN_R, col, active ? 236 : 150);
  if(uiGlass) fillCircleA(cx, cy - FUT_BTN_R / 3, FUT_BTN_R / 2, rgb565(255,255,255), 46);  // brillo Liquid Glass
  drawCircle(cx, cy, FUT_BTN_R, mix565(col, rgb565(255,255,255), 120));
  drawTextC(cx, cy - 6, label, 1, rgb565(255,255,255));
}
static void futDrawControls(){
  // Stick
  fillCircleA(FUT_STK_CX, FUT_STK_CY, FUT_STK_R, rgb565(20,24,42), 120);
  drawCircle(FUT_STK_CX, FUT_STK_CY, FUT_STK_R, rgb565(150,168,206));
  if(uiGlass) fillCircleA(FUT_STK_CX, FUT_STK_CY - FUT_STK_R / 3, FUT_STK_R / 2, rgb565(255,255,255), 30);
  int kx = FUT_STK_CX + futStkKnobX, ky = FUT_STK_CY + futStkKnobY;
  fillCircleA(kx, ky, 26, rgb565(184,204,244), 210);
  drawCircle(kx, ky, 26, rgb565(232,240,255));
  // Botones
  futDrawBtn(FBT_SPR, "SPR", futSprint, rgb565(90,200,120));
  futDrawBtn(FBT_PAS, "PAS", false,     rgb565(90,150,240));
  futDrawBtn(FBT_TIR, "TIR", false,     rgb565(240,92,92));
  futDrawBtn(FBT_CAM, "CAM", false,     rgb565(240,200,84));
}

// ---- Composicion de un frame de PARTIDO (en bbuf) + volcado ----
static void futRenderPlay(){
  gLand = true; setBuf(bbuf);
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  futDrawField();
  // Orden por profundidad (wy ascendente = lejos primero) para que los
  // cercanos tapen a los lejanos. Insercion sobre 22 indices: barato.
  int order[FUT_NP];
  for(int i = 0; i < FUT_NP; i++) order[i] = i;
  for(int a = 1; a < FUT_NP; a++){
    int v = order[a]; float vy = futP[v].y; int b = a - 1;
    while(b >= 0 && futP[order[b]].y > vy){ order[b + 1] = order[b]; b--; }
    order[b + 1] = v;
  }
  bool ballDrawn = false;
  for(int a = 0; a < FUT_NP; a++){
    if(!ballDrawn && futBy <= futP[order[a]].y){ futDrawBall(); ballDrawn = true; }
    futDrawPlayer(order[a]);
  }
  if(!ballDrawn) futDrawBall();
  futDrawHUD();
  futDrawControls();
  if(millis() < futFreezeUntil && futFlash[0]){
    drawTextC(LW / 2, FUT_HORIZON + 30, futFlash, 6, rgb565(250,230,90));
    int sc = (futKickNext == 1) ? 0 : 1;                            // marco quien acaba de marcar
    drawTextC(LW / 2, FUT_HORIZON + 92, FUT_TEAMS[futTeamSel[sc]].nombre, 3, rgb565(240,244,252));
  }
  present(0, SCR_H - 1);
}

// ---- Lectura del stick + acciones (un solo dedo) ----
static void futReadStick(int lx, int ly){
  bool stick = T.down && lx < 380 && ly > 190;
  if(stick){
    float ox = lx - FUT_STK_CX, oy = ly - FUT_STK_CY, d = sqrtf(ox * ox + oy * oy);
    if(d > FUT_STK_DEAD){
      float dd = (d > FUT_STK_R) ? FUT_STK_R : d;
      float mx = ox / d, my = oy / d;
      float mag = (dd - FUT_STK_DEAD) / (FUT_STK_R - FUT_STK_DEAD);
      futMoveX = mx * mag; futMoveY = my * mag;
      futFaceX = mx; futFaceY = my;
      futStkKnobX = (int)(mx * dd); futStkKnobY = (int)(my * dd);
      return;
    }
  }
  futMoveX = futMoveY = 0; futStkKnobX = 0; futStkKnobY = 0;
}

// ---- Tick del partido: throttle + fisica con substep + render ----
static void futPlayTick(){
  uint32_t now = millis();
  int lx = geoLX(), ly = geoLY();
  // Salir a la seleccion de equipo -- en TODOS los ticks (antes del throttle).
  if(T.tap && lx < 46 && ly < 30){ futScreen = FS_TEAM; futRenderTeamSel(false); return; }
  bool frozen = (now < futFreezeUntil);
  // Latcheo de taps de boton (transitorios) antes del throttle, para no perderlos.
  if(T.tap && !frozen){
    for(int b = 0; b < 4; b++){
      int dx = lx - FUT_BTNX[b], dy = ly - FUT_BTNY[b];
      if(dx * dx + dy * dy <= (FUT_BTN_R + 7) * (FUT_BTN_R + 7)){
        if(b == FBT_SPR) futReqSprint = true; else if(b == FBT_PAS) futReqPass = true;
        else if(b == FBT_TIR) futReqShoot = true; else if(b == FBT_CAM) futReqSwitch = true;
        break;
      }
    }
  }
  if(now - futFrameMs < GEO_FRAME_MS) return;                       // ~30 fps
  float dt = (now - futFrameMs) / 1000.0f; futFrameMs = now;
  if(dt > GEO_DTMAX) dt = GEO_DTMAX;
  futDt = dt;
  if(!frozen && futPending && now >= futFreezeUntil){ futPending = false; futResetFormation(futKickNext); futFlash[0] = 0; }
  if(!frozen){
    futReadStick(lx, ly);
    if(futReqSprint){ futSprint = !futSprint; futReqSprint = false; }
    if(futReqPass){ futDoPass(); futReqPass = false; }
    if(futReqShoot){ futDoShoot(); futReqShoot = false; }
    if(futReqSwitch){ futDoSwitch(); futReqSwitch = false; }
    // Velocidad del jugador controlado desde el stick
    float spd = FUT_BASE_SPD * (futSprint ? FUT_SPRINT_MUL : 1.0f);
    futP[futCtrl].vx = futMoveX * spd; futP[futCtrl].vy = futMoveY * spd;
    futAI();
    float remain = dt;
    while(remain > 0.0001f){ float s = (remain > GEO_SUBSTEP) ? GEO_SUBSTEP : remain; futStep(s); remain -= s; }
    futCam += (futBx - futCam) * (dt * 3.0f > 1 ? 1 : dt * 3.0f);
    if(futCam < FUT_CX) futCam = FUT_CX; if(futCam > FUT_LEN - FUT_CX) futCam = FUT_LEN - FUT_CX;
    futPlaySec += dt * (5400.0f / FUT_MATCH_SEC);
    if(futPlaySec >= 5400){ futPlaySec = 5400; futScreen = FS_END; futRenderEnd(); return; }
  } else {
    futMoveX = futMoveY = 0; futStkKnobX = futStkKnobY = 0;
  }
  futRenderPlay();
}

// #############################################################
// ##  SELECTOR DE EQUIPO  (tu equipo -> rival -> saque)
// #############################################################
static void futDrawTeamCell(int x, int y, int w, int h, int idx, bool hl){
  uint16_t a = FUT_TEAMS[idx].colorA, b = FUT_TEAMS[idx].colorB;
  fillRoundRect(x, y, w, h, 8, mix565(rgb565(20,24,42), a, 55));
  fillRect(x + 8, y + 8, 16, 18, a); fillRect(x + 24, y + 8, 9, 18, b);
  drawRect(x + 8, y + 8, 25, 18, rgb565(10,12,20));
  futCrest(x + w - 20, y + 20, 20, 24, a, b);
  drawTextC(x + w / 2, y + h - 30, FUT_TEAMS[idx].nombre, 1, rgb565(236,240,250));
  char rt[6]; snprintf(rt, sizeof(rt), "%d", FUT_TEAMS[idx].rating);
  drawText(x + 8, y + h - 16, rt, 1, rgb565(250,214,92));
  drawRoundRect(x, y, w, h, 8, hl ? rgb565(250,230,90) : mix565(a, rgb565(255,255,255), 46));
}
static void futRenderTeamSel(bool opp){
  gLand = true; setBuf(bbuf);
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  geoFillL(bbuf, 0, 0, LW, LH, rgb565(16,20,36));
  geoFillL(bbuf, 0, 0, LW, 54, rgb565(24,30,54));
  drawTextC(LW / 2, 14, opp ? "ELIGE RIVAL" : "ELIGE TU EQUIPO", 3, rgb565(240,244,252));
  fillRoundRect(10, 12, 44, 28, 8, rgb565(60,66,100));
  if(opp){ strokeSeg(38, 18, 26, 26, 2, rgb565(240,240,240)); strokeSeg(26, 26, 38, 34, 2, rgb565(240,240,240)); }  // <- volver
  else   { strokeSeg(24, 18, 40, 34, 2, rgb565(240,240,240)); strokeSeg(40, 18, 24, 34, 2, rgb565(240,240,240)); }  // X salir
  int cols = 7, cw = 106, ch = 76, x0 = 18, y0 = 70, gx = 2, gy = 6;
  for(int i = 0; i < FUT_NTEAMS; i++){
    int c = i % cols, r = i / cols;
    int x = x0 + c * (cw + gx), y = y0 + r * (ch + gy);
    bool hl = opp ? (i == futTeamSel[1]) : (i == futTeamSel[0]);
    futDrawTeamCell(x, y, cw - 4, ch - 4, i, hl);
  }
  drawTextC(LW / 2, LH - 16, opp ? "Toca el equipo rival" : "Toca tu equipo (se guarda)", 1, rgb565(160,170,190));
  present(0, SCR_H - 1);
}
static void futStartMatch(){
  futScoreA = 0; futScoreB = 0; futPlaySec = 0; futSprint = false;
  futReqPass = futReqShoot = futReqSwitch = futReqSprint = false;
  futPending = false; futFlash[0] = 0;
  futResetFormation(0);                                            // saca el jugador (equipo 0)
  futFreezeUntil = millis() + 800;                                 // breve pausa de saque
  futFrameMs = millis();
  futScreen = FS_PLAY;
  futRenderPlay();
}
static void futTeamSelTick(bool opp){
  if(!T.tap) return;
  int lx = geoLX(), ly = geoLY();
  if(lx >= 10 && lx <= 54 && ly >= 12 && ly <= 40){
    if(opp){ futScreen = FS_TEAM; futRenderTeamSel(false); }
    else   { gGameMode = GM_MENU; gamesRenderMenu(); }             // salir del futbol al menu de juegos
    return;
  }
  int cols = 7, cw = 106, ch = 76, x0 = 18, y0 = 70, gx = 2, gy = 6;
  if(ly < y0 || lx < x0) return;
  int c = (lx - x0) / (cw + gx), r = (ly - y0) / (ch + gy);
  if(c < 0 || c >= cols || r < 0) return;
  int idx = r * cols + c; if(idx >= FUT_NTEAMS) return;
  int cx = x0 + c * (cw + gx), cy = y0 + r * (ch + gy);
  if(lx > cx + cw - 4 || ly > cy + ch - 4) return;                 // en la separacion
  if(!opp){ futTeamSel[0] = idx; futSaveTeam(idx); futScreen = FS_OPP; futRenderTeamSel(true); }
  else    { futTeamSel[1] = idx; futStartMatch(); }
}

// ---- Pantalla final del partido ----
static void futRenderEnd(){
  gLand = true; setBuf(bbuf);
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  geoFillL(bbuf, 0, 0, LW, LH, rgb565(10,14,28));
  drawTextC(LW / 2, 78, "FINAL", 5, rgb565(250,230,90));
  char sc[48];
  snprintf(sc, sizeof(sc), "%s  %d - %d  %s",
           FUT_TEAMS[futTeamSel[0]].nombre, futScoreA, futScoreB, FUT_TEAMS[futTeamSel[1]].nombre);
  drawTextC(LW / 2, 190, sc, 3, rgb565(240,244,252));
  const char* res = (futScoreA > futScoreB) ? "\xC2\xA1Ganaste!" : (futScoreA < futScoreB ? "Perdiste" : "Empate");
  drawTextC(LW / 2, 256, res, 3, (futScoreA > futScoreB) ? rgb565(120,240,140) : rgb565(240,180,180));
  drawTextC(LW / 2, 366, "Toca para continuar", 2, rgb565(170,180,200));
  present(0, SCR_H - 1);
}
static void futEndTick(){
  if(T.tap){ futScreen = FS_TEAM; futRenderTeamSel(false); }
}

// ---- NVS: ultimo equipo elegido (namespace "flexos", clave "fut_team") ----
static void futLoadTeam(){
  prefs.begin("flexos", true);
  int v = prefs.getInt("fut_team", 0);
  prefs.end();
  if(v < 0 || v >= FUT_NTEAMS) v = 0;
  futTeamSel[0] = v;
}
static void futSaveTeam(int idx){
  if(idx < 0 || idx >= FUT_NTEAMS) return;
  prefs.begin("flexos", false);
  prefs.putInt("fut_team", idx);
  prefs.end();
}

// ---- Entrada al modo Futbol (desde el menu de juegos) ----
static void futEnter(){
  gGameMode = GM_FUT;
  if(!futLUTready) futBuildLUT();
  geoRngState = 0x9e3779b9u ^ millis();
  futLoadTeam();
  if(futTeamSel[1] == futTeamSel[0]) futTeamSel[1] = (futTeamSel[0] + 1) % FUT_NTEAMS;
  futScreen = FS_TEAM;
  futRenderTeamSel(false);
}
// ---- Router del modo Futbol ----
static void futTick(){
  if(futScreen == FS_TEAM){ futTeamSelTick(false); return; }
  if(futScreen == FS_OPP){  futTeamSelTick(true);  return; }
  if(futScreen == FS_END){  futEndTick();          return; }
  futPlayTick();
}

// #############################################################
// ##  MENU DE JUEGOS  (Geometry Dash / Futbol)
// #############################################################
static void gamesMenuCard(int x, int y, int w, int h, uint16_t accent, const char* label, int kind){
  fillRoundRectA(x, y, w, h, 20, mix565(rgb565(18,22,40), accent, 70), 235);
  if(uiGlass) fillRoundRectA(x + 6, y + 6, w - 12, h / 3, 16, rgb565(255,255,255), 26);
  drawRoundRect(x, y, w, h, 20, mix565(accent, rgb565(255,255,255), 90));
  int icx = x + w / 2, icy = y + h / 2 - 24;
  if(kind == 0){                                                   // icono Geo Dash: cubo con "ojos"
    int s = 46;
    fillRoundRect(icx - s, icy - s, 2 * s, 2 * s, 10, accent);
    fillRoundRect(icx - s + 6, icy - s + 6, 2 * s - 12, 2 * s - 12, 8, mix565(accent, rgb565(0,0,0), 90));
    fillRect(icx - 20, icy - 8, 12, 16, rgb565(240,244,252));
    fillRect(icx + 8, icy - 8, 12, 16, rgb565(240,244,252));
  } else {                                                         // icono Futbol: balon + cesped
    fillRoundRect(icx - 52, icy + 30, 104, 16, 6, rgb565(40,160,60));
    fillCircle(icx, icy, 40, rgb565(245,245,248));
    drawCircle(icx, icy, 40, rgb565(50,54,60));
    fillTriangle(icx, icy - 16, icx - 15, icy + 6, icx + 15, icy + 6, rgb565(30,32,40));  // pentagono
    fillCircle(icx, icy, 4, rgb565(30,32,40));
  }
  drawTextC(icx, y + h - 52, label, 3, rgb565(244,247,252));
}
static void gamesRenderMenu(){
  gLand = true; setBuf(bbuf);
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  geoFillL(bbuf, 0, 0, LW, LH, rgb565(18,22,40));
  geoFillL(bbuf, 0, 0, LW, 58, rgb565(26,32,58));
  drawTextC(LW / 2, 16, "JUEGOS", 4, rgb565(240,244,252));
  fillRoundRect(12, 14, 46, 30, 8, rgb565(58,64,98));              // X salir de la app
  strokeSeg(24, 20, 44, 38, 2, rgb565(240,240,240)); strokeSeg(44, 20, 24, 38, 2, rgb565(240,240,240));
  int cw = 320, ch = 300, gap = 40, y0 = 112;
  int x1 = (LW - (2 * cw + gap)) / 2, x2 = x1 + cw + gap;
  gamesMenuCard(x1, y0, cw, ch, rgb565(70,150,240), "Geometry Dash", 0);
  gamesMenuCard(x2, y0, cw, ch, rgb565(40,170,80),  "Futbol", 1);
  drawTextC(LW / 2, LH - 22, "Toca un juego para empezar", 2, rgb565(160,170,190));
  present(0, SCR_H - 1);
}
static void gamesExitApp(){ gLand = false; appClose(); }           // cierra la app "Juegos" -> Home
static void gamesEnterGeo(){                                       // abre Geo Dash (motor original, intacto)
  gGameMode = GM_GEO;
  geoLoadProgress();
  gCurLevel = 0;
  geoEnterSelect();
}
static void gamesMenuTick(){
  if(!T.tap) return;
  int lx = geoLX(), ly = geoLY();
  if(lx >= 12 && lx <= 58 && ly >= 14 && ly <= 44){ gamesExitApp(); return; }
  int cw = 320, ch = 300, gap = 40, y0 = 112;
  int x1 = (LW - (2 * cw + gap)) / 2, x2 = x1 + cw + gap;
  if(ly >= y0 && ly <= y0 + ch){
    if(lx >= x1 && lx <= x1 + cw){ gamesEnterGeo(); return; }
    if(lx >= x2 && lx <= x2 + cw){ futEnter(); return; }
  }
}

// ---- Salir del selector de Geo Dash: vuelve al menu de juegos ----
static void geoExit(){ gGameMode = GM_MENU; gamesRenderMenu(); }

// ---- Empezar un nivel (Normal o Practica) ----
static void geoStartLevel(int idx, bool practice){
  geoBindLevel(idx);
  gPractice = practice;
  geoRngState = 0x9e3779b9u ^ millis();
  geoAttempts = 1;
  geoState = GEO_PLAY;
  geoResetLevel();
  geoCPset = false;
  geoNextCP = gLen * 0.22f;                     // primer checkpoint ~22%
  geoDownPrev = false; geoHeldLatch = false; geoTapLatch = false;
  gScreen = GS_GAME;
  geoRenderGame();
}

// ---- Volver al selector (recarga el progreso mostrado) ----
static void geoEnterSelect(){
  gScreen = GS_SELECT;
  geoBindLevel(gCurLevel);
  geoRenderSelect();
}

// ---- Toques del selector ----
static void geoSelectTick(){
  if(!T.tap) return;
  int lx = geoLX(), ly = geoLY();
  int x0 = 168, y0 = 60, w = 464;
  if(lx >= 10 && lx <= 56 && ly >= 8 && ly <= 40){ geoExit(); return; }         // salir de la app
  if(lx >= 28 && lx <= 96  && ly >= 196 && ly <= 288){ gCurLevel = (gCurLevel + 3) % 4; geoRenderSelect(); return; }  // <
  if(lx >= 704 && lx <= 772 && ly >= 196 && ly <= 288){ gCurLevel = (gCurLevel + 1) % 4; geoRenderSelect(); return; } // >
  // Barra Normal.
  if(lx >= x0 + 44 && lx <= x0 + w - 44 && ly >= y0 + 142 && ly <= y0 + 176){ geoStartLevel(gCurLevel, false); return; }
  // Barra Practica.
  if(lx >= x0 + 44 && lx <= x0 + w - 44 && ly >= y0 + 226 && ly <= y0 + 260){ geoStartLevel(gCurLevel, true); return; }
}

// ---- Tick de JUEGO (throttle + fisica con substep + render) ----
static void geoGameTick(){
  uint32_t now = millis();
  int lx = geoLX(), ly = geoLY();
  // Salir al selector -- en TODOS los ticks (antes del throttle) para no perderlo.
  if(T.tap && lx >= 10 && lx <= 56 && ly >= 6 && ly <= 36){ geoEnterSelect(); return; }
  // Reiniciar checkpoints (solo Practica): reinicia el nivel desde 0%.
  if(gPractice && T.tap && lx >= LW - 132 && lx <= LW - 10 && ly >= 6 && ly <= 36){
    geoCPset = false; geoNextCP = gLen * 0.22f;
    geoAttempts = 1; geoResetLevel(); geoState = GEO_PLAY;
    geoRenderGame(); return;
  }
  // Latcheo de input de juego (zona bajo el HUD): held sostenido + flanco de tap.
  bool inPlay = T.down && ly > GEO_HUD_H;
  if(inPlay) geoHeldLatch = true;
  if(inPlay && !geoDownPrev) geoTapLatch = true;
  geoDownPrev = T.down;

  // Throttle: no avanzar fisica/render mas rapido que ~GEO_FRAME_MS.
  if(now - geoFrameMs < GEO_FRAME_MS) return;
  float dt = (now - geoFrameMs) / 1000.0f;
  geoFrameMs = now;
  if(dt > GEO_DTMAX) dt = GEO_DTMAX;

  bool held = geoHeldLatch, tapEdge = geoTapLatch;
  geoHeldLatch = false; geoTapLatch = false;

  if(geoState == GEO_PLAY){
    // Efectos de "un solo tap" (una vez por frame, fuera del substep).
    if(tapEdge){
      if(gForma == FRM_BALL) geoGravDir = -geoGravDir;                       // invertir gravedad
      else if(gForma == FRM_UFO){ geoVelY = -geoGravDir * GEO_UFO_IMP; geoGrounded = false; }  // salto corto
    }
    // Substepping: fisica en pasos fijos <= GEO_SUBSTEP (sin tuneles a cualquier FPS).
    float remain = dt;
    while(remain > 0.0001f && geoState == GEO_PLAY){
      float step = remain > GEO_SUBSTEP ? GEO_SUBSTEP : remain;
      geoUpdate(step, held);
      remain -= step;
    }
    // Checkpoint automatico en Practica (~cada 22% de avance).
    if(gPractice && geoState == GEO_PLAY && geoScroll >= geoNextCP){
      geoSaveCP();
      geoNextCP += gLen * 0.22f;
    }
  } else if(geoState == GEO_DEAD){
    geoUpdateParticles(dt);
    if(now - geoDeadMs > GEO_RESPAWN_MS){
      geoAttempts++;
      if(gPractice && geoCPset){ geoRestoreCP(); geoState = GEO_PLAY; }        // reaparece en el checkpoint
      else { geoResetLevel(); geoState = GEO_PLAY; }                           // Normal: desde 0%
    }
  } else { // GEO_WIN
    if(T.tap && ly > GEO_HUD_H){ geoEnterSelect(); return; }
  }
  geoRenderGame();
}

// ---- Entrada de la app "Juegos": abre el menu de seleccion de juego ----
static void geoEnter(){
  gLand = true;
  gGameMode = GM_MENU;
  gamesRenderMenu();
}

// ---- Tick de la app: enruta menu / Geo Dash / Futbol ----
static void geoTick(){
  if(gGameMode == GM_MENU){ gamesMenuTick(); return; }
  if(gGameMode == GM_FUT){ futTick(); return; }
  // GM_GEO -> motor original de Geometry Dash (intacto)
  if(gScreen == GS_SELECT){ geoSelectTick(); return; }
  geoGameTick();
}
static void navEnter(){
  setBuf(fb); fillRect(0, WIN_TOP, SCR_W, WIN_BOT - WIN_TOP, WIN_BG);
  drawTextC(SCR_W / 2, WIN_TOP + 14, "Navegador", 3, rgb565(255,255,255));
  fillRoundRect(24, WIN_TOP + 58, SCR_W - 48, 44, 12, rgb565(240,242,248));
  drawText(40, WIN_TOP + 72, "https://", 2, rgb565(120,126,140));
  int cy = (WIN_TOP + WIN_BOT) / 2 + 20;
  drawCircle(SCR_W / 2, cy, 40, rgb565(80,120,200)); drawCircle(SCR_W / 2, cy, 39, rgb565(80,120,200));
  vLine(SCR_W / 2, cy - 40, 80, rgb565(80,120,200)); hLine(SCR_W / 2 - 40, cy, 80, rgb565(80,120,200));
  drawTextC(SCR_W / 2, cy + 70, "Sin conexi\xC3\xB3n - modo offline", 2, rgb565(160,168,188));
  flxFlush(WIN_TOP, WIN_BOT);
}
// #############################################################
// ##  ASISTENTE DE HARDWARE  (FASE 3, dentro de Code IDE)
// ##  ------------------------------------------------------
// ##  Panel de SOLO LECTURA (no se refactoriza el editor, que
// ##  hoy es una demo). Lista los modulos I2C detectados en la
// ##  Fase 2 (detectedModules[]) y, al elegir uno, genera el
// ##  codigo de inicializacion y lo muestra como texto para
// ##  copiar al portapapeles global (clipboard[]). Se dibuja
// ##  a fb con flxFlush una sola vez por cambio de estado
// ##  (nada lo redibuja por frame durante ST_APP con el IDE),
// ##  asi que no hay parpadeo ni conflicto con el presenter.
// #############################################################

// Estado del asistente
static bool  hwWizardActive = false;   // panel abierto
static int   hwSelModule    = -1;      // -1 = lista; >=0 = indice en detectedModules (vista codigo)
static bool  hwCopied       = false;   // feedback del boton "Copiar"
static char  hwCode[512]    = "";      // codigo generado (tambien va al portapapeles)

// Geometria: boton del editor
#define HW_BTN_X   (SCR_W / 2 - 140)
#define HW_BTN_Y   (WIN_BOT - 104)
#define HW_BTN_W   280
#define HW_BTN_H   46
// Geometria: lista de modulos
#define HW_LIST_Y0 (WIN_TOP + 56)
#define HW_ROW_H   56
#define HW_CARD_X  24
#define HW_CARD_W  (SCR_W - 48)
#define HW_CARD_H  48
// Geometria: boton "Cerrar" (vista lista)
#define HW_CLOSE_X (SCR_W / 2 - 80)
#define HW_CLOSE_Y (WIN_BOT - 60)
#define HW_CLOSE_W 160
#define HW_CLOSE_H 44
// Geometria: botones "Volver"/"Copiar" (vista codigo)
#define HW_BACK_X  24
#define HW_COPY_X  (SCR_W - 24 - 150)
#define HW_ACT_Y   (WIN_BOT - 60)
#define HW_ACT_W   150
#define HW_ACT_H   44

// drawModuleIcon() se define mas abajo (bloque de la isla); forward-decl para
// poder reutilizar el mismo mapeo tipo->icono aqui.
static void drawModuleIcon(ModuleType type, int x, int y, int S);

static void ideEnter(){
  setBuf(fb); fillRect(0, WIN_TOP, SCR_W, WIN_BOT - WIN_TOP, rgb565(20,22,30));
  drawTextC(SCR_W / 2, WIN_TOP + 14, "Code IDE", 3, rgb565(255,255,255));
  const char* code[8] = {
    "#include <FlexOS.h>", "", "void setup() {", "  screen.begin();",
    "  ui.drawHome();", "}", "", "void loop() { ui.tick(); }" };
  int y = WIN_TOP + 60;
  for(int i = 0; i < 8; i++){
    char ln[8]; snprintf(ln, sizeof(ln), "%2d", i + 1);
    drawText(16, y, ln, 1, rgb565(90,96,112));
    drawText(44, y, code[i], 1, rgb565(150,220,180));   // 5x7 nitido (aspecto de codigo)
    y += 22;
  }
  // Asistente de Hardware (Fase 3): resetear estado y dibujar su boton
  hwWizardActive = false; hwSelModule = -1; hwCopied = false;
  fillRoundRect(HW_BTN_X, HW_BTN_Y, HW_BTN_W, HW_BTN_H, 12, rgb565(60, 110, 235));
  drawTextC(SCR_W / 2, HW_BTN_Y + 15, "Asistente de Hardware", 2, rgb565(255, 255, 255));
  drawTextC(SCR_W / 2, WIN_BOT - 50, "Editor de codigo (demostracion)", 1, rgb565(130,138,158));
  flxFlush(WIN_TOP, WIN_BOT);
}

// Indices de los modulos activos (para mapear filas de la lista <-> detectedModules)
static int hwActiveList(int* out, int maxn){
  int c = 0;
  for(int i = 0; i < detectedCount && c < maxn; i++)
    if(detectedModules[i].active) out[c++] = i;
  return c;
}

// Genera el codigo de inicializacion del modulo en hwCode[] (char[] + snprintf)
static void hwGenCode(const DetectedModule* m){
  size_t n = 0;
  hwCode[0] = 0;
  #define HWCAT(...) do{ int _w = snprintf(hwCode + n, sizeof(hwCode) - n, __VA_ARGS__); \
                         if(_w > 0){ n += (size_t)_w; if(n >= sizeof(hwCode)) n = sizeof(hwCode) - 1; } }while(0)
  HWCAT("// Inicializacion automatica\n");
  HWCAT("// Modulo: %s", m->name);
  if(m->i2cAddr) HWCAT(" (0x%02X)", m->i2cAddr);
  HWCAT("\n#include <Wire.h>\n\n");
  HWCAT("void setup() {\n");
  HWCAT("  Wire.begin(7, 8);   // SDA=7 SCL=8\n");
  if(m->i2cAddr){
    HWCAT("  Wire.beginTransmission(0x%02X);\n", m->i2cAddr);
    HWCAT("  bool ok = (Wire.endTransmission() == 0);\n");
  }
  switch(m->type){
    case MOD_BME280:  HWCAT("  // Lib sugerida: Adafruit_BME280\n"); break;
    case MOD_MPU6050: HWCAT("  // Lib sugerida: MPU6050 (I2Cdev)\n"); break;
    default: break;
  }
  HWCAT("}\n");
  #undef HWCAT
}

// Dibuja el panel del asistente (una sola vez por cambio de estado)
static void hwDrawWizard(){
  setBuf(fb);
  fillRect(0, WIN_TOP, SCR_W, WIN_BOT - WIN_TOP, WIN_BG);

  if(hwSelModule < 0){
    // ---- Vista LISTA ----
    drawTextC(SCR_W / 2, WIN_TOP + 16, "Asistente de Hardware", 3, rgb565(255, 255, 255));
    int idxs[MAX_MODULES_DETECTED];
    int nc = hwActiveList(idxs, MAX_MODULES_DETECTED);
    if(nc == 0){
      drawTextC(SCR_W / 2, WIN_TOP + 120, "No hay modulos I2C detectados", 2, rgb565(170, 178, 196));
      drawTextC(SCR_W / 2, WIN_TOP + 150, "Conecta un sensor al bus (SDA=7, SCL=8)", 1, rgb565(130, 138, 158));
    } else {
      for(int r = 0; r < nc; r++){
        DetectedModule* m = &detectedModules[idxs[r]];
        int cy = HW_LIST_Y0 + r * HW_ROW_H;
        glDrawSpec = false;
        drawLiquidGlassPanel(HW_CARD_X, cy, HW_CARD_W, HW_CARD_H, 12, rgb565(40, 60, 130), millis());
        glDrawSpec = true;
        drawModuleIcon(m->type, HW_CARD_X + 8, cy + 8, 32);
        char label[48];
        if(m->i2cAddr) snprintf(label, sizeof(label), "%s (0x%02X)", m->name, m->i2cAddr);
        else           snprintf(label, sizeof(label), "%s", m->name);
        drawText(HW_CARD_X + 50, cy + 16, label, 2, rgb565(240, 242, 248));
        drawTextC(HW_CARD_X + HW_CARD_W - 46, cy + 18, "Config", 1, rgb565(180, 200, 255));
      }
    }
    fillRoundRect(HW_CLOSE_X, HW_CLOSE_Y, HW_CLOSE_W, HW_CLOSE_H, 14, rgb565(70, 74, 90));
    drawTextC(SCR_W / 2, HW_CLOSE_Y + 14, "Cerrar", 2, rgb565(255, 255, 255));
  } else {
    // ---- Vista CODIGO (solo lectura) ----
    DetectedModule* m = &detectedModules[hwSelModule];
    char t[40]; snprintf(t, sizeof(t), "Codigo: %s", m->name);
    drawTextC(SCR_W / 2, WIN_TOP + 16, t, 2, rgb565(255, 255, 255));

    int px = 16, py = WIN_TOP + 52, pw = SCR_W - 32, ph = (HW_ACT_Y - 12) - (WIN_TOP + 52);
    glDrawSpec = false;
    drawLiquidGlassPanel(px, py, pw, ph, 12, rgb565(24, 40, 80), millis());
    glDrawSpec = true;

    // Volcar hwCode linea a linea (split por '\n')
    int ly = py + 12, lineNo = 1;
    const char* p = hwCode;
    char line[80];
    while(*p){
      int li = 0;
      while(*p && *p != '\n' && li < 78){ line[li++] = *p++; }
      line[li] = 0;
      if(*p == '\n') p++;
      char num[6]; snprintf(num, sizeof(num), "%2d", lineNo++);
      drawText(px + 10, ly, num,  1, rgb565(90, 96, 112));
      drawText(px + 38, ly, line, 1, rgb565(150, 220, 180));
      ly += 18;
      if(ly > py + ph - 16) break;
    }

    fillRoundRect(HW_BACK_X, HW_ACT_Y, HW_ACT_W, HW_ACT_H, 12, rgb565(70, 74, 90));
    drawTextC(HW_BACK_X + HW_ACT_W / 2, HW_ACT_Y + 14, "Volver", 2, rgb565(255, 255, 255));
    fillRoundRect(HW_COPY_X, HW_ACT_Y, HW_ACT_W, HW_ACT_H, 12, hwCopied ? rgb565(46, 160, 90) : rgb565(60, 110, 235));
    drawTextC(HW_COPY_X + HW_ACT_W / 2, HW_ACT_Y + 14, hwCopied ? "Copiado" : "Copiar", 2, rgb565(255, 255, 255));
  }
  flxFlush(WIN_TOP, WIN_BOT);
}

// tick del Code IDE: gestiona el boton del editor y los toques del asistente.
// El marco (chevron/atras/nav) lo sigue cerrando el framework -> cierra la app.
static void ideTick(){
  if(hwWizardActive){
    if(!T.tap) return;
    if(hwSelModule < 0){
      // Vista lista: tap en una tarjeta -> generar codigo y pasar a vista codigo
      int idxs[MAX_MODULES_DETECTED];
      int nc = hwActiveList(idxs, MAX_MODULES_DETECTED);
      for(int r = 0; r < nc; r++){
        int cy = HW_LIST_Y0 + r * HW_ROW_H;
        if(T.x >= HW_CARD_X && T.x <= HW_CARD_X + HW_CARD_W && T.y >= cy && T.y <= cy + HW_CARD_H){
          hwSelModule = idxs[r]; hwCopied = false;
          hwGenCode(&detectedModules[hwSelModule]);
          hwDrawWizard();
          return;
        }
      }
      // Cerrar -> volver al editor
      if(T.x >= HW_CLOSE_X && T.x <= HW_CLOSE_X + HW_CLOSE_W && T.y >= HW_CLOSE_Y && T.y <= HW_CLOSE_Y + HW_CLOSE_H){
        hwWizardActive = false;
        ideEnter();
        return;
      }
    } else {
      // Vista codigo: Volver
      if(T.x >= HW_BACK_X && T.x <= HW_BACK_X + HW_ACT_W && T.y >= HW_ACT_Y && T.y <= HW_ACT_Y + HW_ACT_H){
        hwSelModule = -1; hwCopied = false; hwDrawWizard();
        return;
      }
      // Copiar al portapapeles global
      if(T.x >= HW_COPY_X && T.x <= HW_COPY_X + HW_ACT_W && T.y >= HW_ACT_Y && T.y <= HW_ACT_Y + HW_ACT_H){
        strncpy(clipboard, hwCode, sizeof(clipboard) - 1);
        clipboard[sizeof(clipboard) - 1] = 0;
        hwCopied = true; hwDrawWizard();
        return;
      }
    }
    return;
  }
  // Editor: abrir el asistente
  if(T.tap && T.x >= HW_BTN_X && T.x <= HW_BTN_X + HW_BTN_W && T.y >= HW_BTN_Y && T.y <= HW_BTN_Y + HW_BTN_H){
    hwWizardActive = true; hwSelModule = -1; hwCopied = false;
    hwDrawWizard();
  }
}


// ---- Paint: lienzo con dibujo tactil real ----
#define P_TOP 96
#define P_BOT (SCR_H - 66)
static uint16_t pColor = 0;
static int pPrevX = -1, pPrevY = -1, pSize = 4;
static const uint16_t P_PAL[6] = { rgb565(30,30,40), rgb565(230,60,60), rgb565(240,150,40),
                                   rgb565(240,210,50), rgb565(80,180,120), rgb565(60,120,235) };
static void paintTools(){
  setBuf(fb);
  int y = SCR_H - 56, sw = 42, gap = 8, x = 16;
  fillRect(0, P_BOT, SCR_W, SCR_H - P_BOT, rgb565(18,20,28));
  for(int i = 0; i < 6; i++){
    int cx = x + i * (sw + gap) + sw / 2;
    fillCircle(cx, y + sw / 2, sw / 2 - 2, P_PAL[i]);
    if(P_PAL[i] == pColor){ drawCircle(cx, y + sw / 2, sw / 2, rgb565(255,255,255)); drawCircle(cx, y + sw / 2, sw / 2 - 1, rgb565(255,255,255)); }
  }
  fillRoundRect(SCR_W - 92, y + 2, 78, 40, 10, rgb565(60,64,78));
  drawTextC(SCR_W - 53, y + 12, "Limpiar", 2, rgb565(240,242,248));
  flxFlush(P_BOT, SCR_H - 1);
}
static void paintEnter(){
  setBuf(fb); fillRect(0, 0, SCR_W, SCR_H, rgb565(16,18,26));
  strokeSegAA(30, 26, 18, 18, 2.4f, rgb565(255,255,255));
  strokeSegAA(18, 18, 30, 10, 2.4f, rgb565(255,255,255));
  drawTextC(SCR_W / 2, 14, "Paint", 3, rgb565(255,255,255));
  fillRect(8, P_TOP, SCR_W - 16, P_BOT - P_TOP, rgb565(250,250,252));   // lienzo
  pColor = P_PAL[0]; pPrevX = pPrevY = -1;
  paintTools();
  flxFlushAll();
}
static void paintTick(){
  if(T.down && T.y >= P_TOP && T.y <= P_BOT && T.x >= 8 && T.x <= SCR_W - 8){
    setBuf(fb);
    int y0, y1;
    if(pPrevX >= 0){ strokeSeg(pPrevX, pPrevY, T.x, T.y, pSize, pColor); y0 = min(pPrevY, T.y); y1 = max(pPrevY, T.y); }
    else { fillCircle(T.x, T.y, pSize, pColor); y0 = y1 = T.y; }
    pPrevX = T.x; pPrevY = T.y;
    flxFlush(y0 - pSize - 1, y1 + pSize + 1);
    return;
  }
  if(!T.down){ pPrevX = pPrevY = -1; }
  if(T.tap){
    if(T.x < 48 && T.y < 48){ appClose(); return; }
    int y = SCR_H - 56, sw = 42, gap = 8, x = 16;
    for(int i = 0; i < 6; i++){ int cx = x + i * (sw + gap) + sw / 2; if(T.x >= cx - sw / 2 && T.x <= cx + sw / 2 && T.y >= y && T.y <= y + sw){ pColor = P_PAL[i]; paintTools(); return; } }
    if(T.x >= SCR_W - 92 && T.y >= y){ setBuf(fb); fillRect(8, P_TOP, SCR_W - 16, P_BOT - P_TOP, rgb565(250,250,252)); flxFlush(P_TOP, P_BOT); return; }
  }
}

// #############################################################
// ##  APP SWITCHER (Multitarea) · carrusel horizontal estilo iOS
// ##  Tarjetas con mini-captura en PSRAM. Arrastre + inercia,
// ##  swipe-arriba para cerrar (free), toque para maximizar.
// #############################################################
#define SW_MAX  6
#define TH_W    150
#define TH_H    250
#define SW_CW   260
#define SW_CH   430
#define SW_STEP 288
#define SW_TOP  150

struct AppTask { uint8_t appID; bool used; uint16_t* thumb; };   // estado suspendido + miniatura
static AppTask swTasks[SW_MAX];
static int   swCount = 0;
static float swScrollPx = 0, swVel = 0, swLiftY = 0;
static int   swLiftCard = -1, swGesture = 0;                     // 0 nada, 1 horizontal, 2 vertical
static float swStartX, swStartY, swLastX2, swLastY2;

static uint16_t* swAllocThumb(){ return (uint16_t*)heap_caps_malloc((size_t)TH_W * TH_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
static void captureThumb(uint16_t* dst){                          // reduce fb 480x800 -> 150x250
  for(int j = 0; j < TH_H; j++){
    int sy = j * SCR_H / TH_H; uint16_t* d = dst + (size_t)j * TH_W;
    for(int i = 0; i < TH_W; i++) d[i] = fb[(size_t)sy * SCR_W + i * SCR_W / TH_W];
  }
}
static void swPush(uint8_t id){                                   // mueve al frente (o inserta)
  int at = -1; for(int i = 0; i < swCount; i++) if(swTasks[i].appID == id){ at = i; break; }
  if(at >= 0){ AppTask tmp = swTasks[at]; for(int i = at; i > 0; i--) swTasks[i] = swTasks[i - 1]; swTasks[0] = tmp; return; }
  if(swCount < SW_MAX){ for(int i = swCount; i > 0; i--) swTasks[i] = swTasks[i - 1]; swCount++; }
  else { if(swTasks[SW_MAX - 1].thumb) free(swTasks[SW_MAX - 1].thumb); for(int i = SW_MAX - 1; i > 0; i--) swTasks[i] = swTasks[i - 1]; }
  swTasks[0].appID = id; swTasks[0].used = true; swTasks[0].thumb = NULL;
}
static void swPushAndCapture(uint8_t id){
  swPush(id);
  if(!swTasks[0].thumb) swTasks[0].thumb = swAllocThumb();
  if(swTasks[0].thumb) captureThumb(swTasks[0].thumb);
}
static void swCloseCard(int idx){                                 // libera PSRAM y reordena
  if(idx < 0 || idx >= swCount) return;
  if(swTasks[idx].thumb){ free(swTasks[idx].thumb); swTasks[idx].thumb = NULL; }
  for(int i = idx; i < swCount - 1; i++) swTasks[i] = swTasks[i + 1];
  swCount--;
}
static uint16_t swSxLUT[SW_CW], swSyLUT[SW_CH]; static bool swLUTdone = false;
static void swBuildLUT(){
  int dw = SW_CW - 16, dh = SW_CH - 52;
  for(int i = 0; i < dw; i++) swSxLUT[i] = (uint16_t)(i * TH_W / dw);
  for(int j = 0; j < dh; j++) swSyLUT[j] = (uint16_t)(j * TH_H / dh);
  swLUTdone = true;
}
static void blitThumbScaled(uint16_t* th, int dx, int dy, int dw, int dh){
  bool lut = (dw == SW_CW - 16 && dh == SW_CH - 52);        // ruta rapida (tamano fijo)
  if(lut && !swLUTdone) swBuildLUT();
  for(int j = 0; j < dh; j++){ int yy = dy + j; if((unsigned)yy >= SCR_H) continue;
    int sy = lut ? swSyLUT[j] : j * TH_H / dh;
    uint16_t* s = th + (size_t)sy * TH_W; uint16_t* d = gBuf + (size_t)yy * SCR_W;
    int x0 = dx < 0 ? 0 : dx, x1 = dx + dw > SCR_W ? SCR_W : dx + dw;
    for(int xx = x0; xx < x1; xx++) d[xx] = s[lut ? swSxLUT[xx - dx] : (xx - dx) * TH_W / dw];
  }
}
// Marco tipo vidrio (barato: sobre el fondo oscuro uniforme el blur no aporta,
// asi el carrusel corre fluido). drawLiquidGlassPanel se reserva para superficies con contenido detras.
static void swCardFrame(int x, int y, int w, int h, int rad){
  fillRoundRect(x, y, w, h, rad, rgb565(26,30,44));
  drawRoundRect(x, y, w, h, rad, rgb565(95,105,138));
}
static void swSheen(int x, int y, int w, int h, int rad, uint32_t t){   // reflejo diagonal en movimiento
  int off = (int)((t / 20) % (uint32_t)(w + h)) - h / 2;
  for(int j = 3; j < h - 3; j += 2){
    int ins = glInset(j, h, rad), sxp = off - j + w / 2;
    for(int i = -18; i <= 18; i++){ int xi = sxp + i; if(xi > ins + 1 && xi < w - ins - 1){ int a = 22 - (i < 0 ? -i : i); if(a > 0) pxA(x + xi, y + j, rgb565(255,255,255), (uint8_t)a); } }
  }
}
static void swRender(float scale){                        // completo (solo animacion de entrada)
  setBuf(bbuf);
  if(blurBg) memcpy(bbuf, blurBg, (size_t)SCR_W * SCR_H * 2); else fillRect(0, 0, SCR_W, SCR_H, rgb565(8,10,16));
  drawTextC(SCR_W / 2, 6, "Recientes", 3, rgb565(240,244,252));
  if(swCount == 0) drawTextC(SCR_W / 2, SCR_H / 2, "Sin apps recientes", 2, rgb565(150,158,180));
  int cw = (int)(SW_CW * scale), ch = (int)(SW_CH * scale);
  for(int i = 0; i < swCount; i++){
    int cx = SCR_W / 2 + i * SW_STEP - (int)swScrollPx;
    if(cx < -SW_CW || cx > SCR_W + SW_CW) continue;
    int x = cx - cw / 2, y = SW_TOP + (SW_CH - ch) / 2;
    if(i == swLiftCard) y -= (int)swLiftY;
    swCardFrame(x, y, cw, ch, 22);
    if(swTasks[i].thumb) blitThumbScaled(swTasks[i].thumb, x + 8, y + 8, cw - 16, ch - 52);
    else { fillRoundRect(x + 8, y + 8, cw - 16, ch - 52, 14, rgb565(28,32,44)); drawAppIcon(swTasks[i].appID, x + cw / 2 - 30, y + ch / 2 - 70, 60); }
    swSheen(x, y, cw, ch, 22, millis());
    drawTextC(x + cw / 2, y + ch - 32, appName(swTasks[i].appID), 2, rgb565(255,255,255));
  }
  drawTextC(SCR_W / 2, SCR_H - 28, "Desliza una tarjeta arriba para cerrar", 1, rgb565(130,138,158));
  present(0, SCR_H - 1);
}
// por-frame: SOLO repinta y vuelca la banda de las tarjetas (mucho mas ligero)
static void swRenderCards(){
  setBuf(bbuf);
  if(blurBg){ for(int j = 32; j < 604; j++) memcpy(bbuf + (size_t)j * SCR_W, blurBg + (size_t)j * SCR_W, SCR_W * 2); }
  else fillRect(0, 32, SCR_W, 572, rgb565(8,10,16));
  if(swCount == 0) drawTextC(SCR_W / 2, SCR_H / 2, "Sin apps recientes", 2, rgb565(150,158,180));
  for(int i = 0; i < swCount; i++){
    int cx = SCR_W / 2 + i * SW_STEP - (int)swScrollPx;
    if(cx < -SW_CW || cx > SCR_W + SW_CW) continue;
    int x = cx - SW_CW / 2, y = SW_TOP;
    if(i == swLiftCard) y -= (int)swLiftY;
    swCardFrame(x, y, SW_CW, SW_CH, 22);
    if(swTasks[i].thumb) blitThumbScaled(swTasks[i].thumb, x + 8, y + 8, SW_CW - 16, SW_CH - 52);
    else { fillRoundRect(x + 8, y + 8, SW_CW - 16, SW_CH - 52, 14, rgb565(28,32,44)); drawAppIcon(swTasks[i].appID, x + SW_CW / 2 - 30, y + SW_CH / 2 - 70, 60); }
    swSheen(x, y, SW_CW, SW_CH, 22, millis());
    drawTextC(x + SW_CW / 2, y + SW_CH - 32, appName(swTasks[i].appID), 2, rgb565(255,255,255));
  }
  present(32, 604);
}
static int swCardIndexAt(int px){
  for(int i = 0; i < swCount; i++){ int cx = SCR_W / 2 + i * SW_STEP - (int)swScrollPx; if(px >= cx - SW_CW / 2 && px <= cx + SW_CW / 2) return i; }
  return -1;
}
static int swCenterIndex(){ int i = (int)roundf(swScrollPx / SW_STEP); if(i < 0) i = 0; if(i >= swCount) i = swCount - 1; return i; }
static void swExitToHome(){ gState = ST_HOME; renderHome(); showHome(); }
static void swMaximize(int idx){ if(idx >= 0 && idx < swCount) enterApp(swTasks[idx].appID); }  // restaura a pantalla completa

// Congela la app activa, cambia a MODO_MULTITAREA y hace la animacion elastica de entrada.
static void activarMultitarea(){
  ensureBlurBg();
  gState = ST_SWITCHER;
  swScrollPx = 0; swVel = 0; swLiftCard = -1; swLiftY = 0; swGesture = 0;
  for(int s = 1; s <= 10; s++){                         // spring scale-in (ease-out-back)
    float p = s / 10.0f, pm = p - 1.0f, eob = 1.0f + 2.6f * pm * pm * pm + 1.6f * pm * pm;
    swRender(0.6f + 0.4f * eob); delay(14);
  }
  swRender(1.0f);
}
static void swTick(){
  if(T.pressed){
    swStartX = T.x; swStartY = T.y; swLastX2 = T.x; swLastY2 = T.y; swVel = 0; swGesture = 0;
    swLiftCard = swCardIndexAt(T.x); swLiftY = 0;
    return;
  }
  if(T.down){
    float dx = T.x - swLastX2, dy = T.y - swLastY2; (void)dy;
    if(swGesture == 0){
      if(fabsf(T.x - swStartX) > 12) swGesture = 1;
      else if(fabsf(T.y - swStartY) > 14) swGesture = 2;
    }
    if(swGesture == 1){                                  // scroll horizontal + velocidad
      swScrollPx -= dx; swVel = -dx;
      float mn = -90, mx = (swCount - 1) * SW_STEP + 90; if(swScrollPx < mn) swScrollPx = mn; if(swScrollPx > mx) swScrollPx = mx;
      swLiftY = 0; swRenderCards();
    } else if(swGesture == 2 && swLiftCard >= 0){        // levantar tarjeta (cerrar)
      swLiftY = swStartY - T.y; if(swLiftY < 0) swLiftY = 0; if(swLiftY > 116) swLiftY = 116; swRenderCards();
    }
    swLastX2 = T.x; swLastY2 = T.y;
    return;
  }
  if(T.released){
    if(swGesture == 2 && swLiftCard >= 0 && swLiftY > 110){   // swipe-arriba -> cerrar (free)
      swCloseCard(swLiftCard); swLiftCard = -1; swLiftY = 0;
      float mx = (swCount > 0 ? (swCount - 1) * SW_STEP : 0); if(swScrollPx > mx) swScrollPx = mx; if(swScrollPx < 0) swScrollPx = 0;
      swRenderCards(); return;
    }
    if(swGesture == 0){                                  // toque
      if(T.y > SCR_H - 60){ swExitToHome(); return; }
      int idx = swCardIndexAt(T.x);
      if(idx >= 0){ if(idx == swCenterIndex()) swMaximize(idx); else { swScrollPx = idx * SW_STEP; swRenderCards(); } return; }
      swExitToHome(); return;
    }
    swLiftCard = -1; swLiftY = 0;
    return;
  }
  // reposo: inercia + enganche elastico a la tarjeta mas cercana
  if(fabsf(swVel) > 0.4f){
    swScrollPx += swVel; swVel *= 0.90f;
    float mx = (swCount > 0 ? (swCount - 1) * SW_STEP : 0);
    if(swScrollPx < 0){ swScrollPx = 0; swVel = 0; } if(swScrollPx > mx){ swScrollPx = mx; swVel = 0; }
    swRenderCards();
  } else {
    int tgt = swCenterIndex() * SW_STEP;
    if((int)swScrollPx != tgt){ swScrollPx += (tgt - swScrollPx) * 0.25f; if(fabsf(tgt - swScrollPx) < 1) swScrollPx = tgt; swRenderCards(); }
  }
}

// #############################################################
// ##  WINDOW MANAGER (portrait 480x800): ventanas flotantes
// ##  redimensionables + split-screen 2/4 + z-order + recorte
// ##  de viewport. Respeta la barra de navegacion (Y >= 750).
// #############################################################
struct WindowInstance {
  uint8_t appID; int x, y, w, h; bool isFloating; uint8_t zIndex; bool isFocused; bool used;
  bool minimized;   // Panel Edge / barra de control: ventana colapsada a una burbuja
                    // flotante en el borde (ver sbDrawMinBubbles/sbHitMinBubble). No se
                    // dibuja ni ejecuta su tick mientras esta minimizada; tap en la burbuja restaura.
};
#define WM_MAX   4
#define WM_TOOLB 46          // barra de herramientas superior
#define WM_NAV   750         // area util termina aqui (Y >= 750 reservado)
static WindowInstance wmWins[WM_MAX];
static int wmCount = 0, wmMode = 0, wmDrag = -1, wmAction = 0, wmDX = 0, wmDY = 0;
static uint8_t wmZTop = 0;
static int8_t wmNeedContent = -1;  // ventana cuyo contenido hay que repintar tras mover/redimensionar (ver wmTouchWindows)
static int8_t wmHostedWin = -1;   // indice en wmWins[] cuya app real se esta ejecutando AHORA MISMO (ver wmRunHostedApp); -1 = ninguna
static const uint8_t WM_APPS[4] = { 5, 13, 1, 14 };   // Notas, Calculadora, Galeria, Calendario

static void wmFocus(int idx){
  if(idx < 0 || idx >= wmCount) return;
  wmWins[idx].zIndex = ++wmZTop;
  for(int i = 0; i < wmCount; i++) wmWins[i].isFocused = (i == idx);
}
static int wmAdd(uint8_t appID){
  if(wmCount >= WM_MAX) return -1;
  int n = wmCount;
  wmWins[n].appID = appID; wmWins[n].isFloating = true; wmWins[n].used = true; wmWins[n].minimized = false;
  wmWins[n].x = 22 + n * 26; wmWins[n].y = 66 + n * 26; wmWins[n].w = 300; wmWins[n].h = 360;
  wmWins[n].zIndex = ++wmZTop;
  wmCount++; wmMode = 0;
  wmFocus(n);
  return n;
}
static void wmRemove(int idx){
  if(idx < 0 || idx >= wmCount) return;
  for(int i = idx; i < wmCount - 1; i++) wmWins[i] = wmWins[i + 1];
  wmCount--;
}
static int wmTopAt(int px, int py){                   // ventana superior (mayor z) bajo el punto
  int best = -1, bz = -1;
  for(int i = 0; i < wmCount; i++){ WindowInstance& w = wmWins[i];
    if(w.minimized) continue;                        // minimizada: no captura toques (pasan a la burbuja)
    if(px >= w.x && px < w.x + w.w && py >= w.y && py < w.y + w.h && w.zIndex > bz){ bz = w.zIndex; best = i; }
  }
  return best;
}
// Divide el area util (480 x 704) en 2 (lado a lado) o 4 (cuadrantes)
static void setWindowLayout(int mode){
  wmMode = mode;
  int uh = WM_NAV - WM_TOOLB;
  if(mode == 2){
    if(wmCount >= 1){ wmWins[0].x = 0;   wmWins[0].y = WM_TOOLB; wmWins[0].w = 240; wmWins[0].h = uh; wmWins[0].isFloating = false; }
    if(wmCount >= 2){ wmWins[1].x = 240; wmWins[1].y = WM_TOOLB; wmWins[1].w = 240; wmWins[1].h = uh; wmWins[1].isFloating = false; }
  } else if(mode == 4){
    int hh = uh / 2, px[4] = { 0, 240, 0, 240 }, py[4] = { WM_TOOLB, WM_TOOLB, WM_TOOLB + hh, WM_TOOLB + hh };
    for(int i = 0; i < wmCount && i < 4; i++){ wmWins[i].x = px[i]; wmWins[i].y = py[i]; wmWins[i].w = 240; wmWins[i].h = hh; wmWins[i].isFloating = false; }
  }
}
// Centro X del boton `i` (0..4, de izquierda a derecha) de la barra de control de
// una ventana de ancho `ww` cuyo borde izquierdo esta en `wx`. Fuente unica: la
// usan TANTO wmDrawWindow() (dibujar) como wmTouchWindows() (hit-test), para que
// el boton que se ve y el que responde al toque nunca se desincronicen.
static int wmCtrlCX(int wx, int ww, int i){
  const int n = 5, bw = 24, gap = 4;
  int totalW = n * bw + (n - 1) * gap;
  int x0 = wx + ww - 8 - totalW;                  // barra pegada al borde derecho de la ventana
  return x0 + i * (bw + gap) + bw / 2;
}
static void wmDrawWindow(int idx){
  WindowInstance& w = wmWins[idx];
  if(w.minimized) return;                                                         // minimizada: se dibuja como burbuja (sbDrawMinBubbles)
  fillRoundRect(w.x + 3, w.y + 4, w.w, w.h, 12, rgb565(6,8,14));                 // sombra
  if(uiGlass) pcGlassPanel(w.x, w.y, w.w, w.h, 12, rgb565(232,238,250), millis());  // cuerpo (glass ligero, fluido)
  else fillRoundRect(w.x, w.y, w.w, w.h, 12, rgb565(244,246,250));
  uint16_t tb = w.isFocused ? rgb565(45,90,200) : rgb565(92,100,120);            // barra de titulo
  fillRoundRect(w.x, w.y, w.w, 30, 12, tb); fillRect(w.x, w.y + 16, w.w, 14, tb);
  drawText(w.x + 12, w.y + 7, appName(w.appID), 2, rgb565(255,255,255));
  // Barra de control estilo Samsung (Imagen A), de izquierda a derecha:
  // 0 restaurar (tamano por defecto) · 1 minimizar (burbuja) · 2 split ·
  // 3 expandir (maximizar) · 4 cerrar. Glifos vectoriales sobre la barra de titulo.
  uint16_t gcol = rgb565(255,255,255);
  int cyb = w.y + 15;
  int c0 = wmCtrlCX(w.x, w.w, 0);                                                 // restaurar: cuadro pequeno
  drawRoundRect(c0 - 6, cyb - 6, 12, 12, 2, gcol);
  int c1 = wmCtrlCX(w.x, w.w, 1);                                                 // minimizar: guion
  fillRect(c1 - 7, cyb - 1, 14, 2, gcol);
  int c2 = wmCtrlCX(w.x, w.w, 2);                                                 // split: dos rectangulos
  drawRoundRect(c2 - 8, cyb - 6, 16, 12, 2, gcol);
  strokeSegAA(c2, cyb - 6, c2, cyb + 6, 1.2f, gcol);
  int c3 = wmCtrlCX(w.x, w.w, 3);                                                 // expandir: flechas diagonales
  strokeSegAA(c3 - 6, cyb - 6, c3 + 6, cyb + 6, 1.6f, gcol);
  strokeSegAA(c3 - 6, cyb - 6, c3 - 1, cyb - 6, 1.6f, gcol);
  strokeSegAA(c3 - 6, cyb - 6, c3 - 6, cyb - 1, 1.6f, gcol);
  strokeSegAA(c3 + 6, cyb + 6, c3 + 1, cyb + 6, 1.6f, gcol);
  strokeSegAA(c3 + 6, cyb + 6, c3 + 6, cyb + 1, 1.6f, gcol);
  int c4 = wmCtrlCX(w.x, w.w, 4);                                                 // cerrar: X
  strokeSegAA(c4 - 6, cyb - 6, c4 + 6, cyb + 6, 1.8f, gcol);
  strokeSegAA(c4 - 6, cyb + 6, c4 + 6, cyb - 6, 1.8f, gcol);
  // El cuerpo (fillRoundRect/pcGlassPanel de arriba) YA cubre el area de contenido
  // con un fondo neutro -- de eso vive hasta que la app real dibuje encima (ver
  // wmRunHostedApp(), que corre en un paso APARTE, fuera de bbuf, justo despues de
  // que esta funcion se compone y presenta via wmRender()/sbRenderOverlay()).
  strokeSegAA(w.x + w.w - 16, w.y + w.h - 4, w.x + w.w - 4, w.y + w.h - 16, 1.6f, rgb565(120,126,140));  // asa de redimension
  strokeSegAA(w.x + w.w - 10, w.y + w.h - 4, w.x + w.w - 4, w.y + w.h - 10, 1.6f, rgb565(120,126,140));
}
// Rectangulo INTERIOR de una ventana (fuente unica: antes vivia inline en
// wmDrawWindow(), ahora tambien lo necesita wmRunHostedApp() para saber donde
// recortar/desplazar el dibujo de la app alojada).
static void wmContentRect(int idx, int &ix, int &iy, int &iw, int &ih){
  ix = wmWins[idx].x + 6; iy = wmWins[idx].y + 34; iw = wmWins[idx].w - 12; ih = wmWins[idx].h - 40;
}
// Y "nativa" donde una app empieza a pintar su contenido si viviera a pantalla
// completa: 0 para las de cabecera propia (APP_CUSTOM_HEADER, dueñas de toda la
// pantalla), o WIN_TOP para las normales (bajo el marco/cabecera generica que
// wmDrawWindow() ya reemplaza por la barra de titulo de la ventana).
static int wmAppOriginY(uint8_t appID){
  return (APP_REG[appID].flags & APP_CUSTOM_HEADER) ? 0 : WIN_TOP;
}
// Ejecuta enter() (isEnter=true, una vez al crear la ventana) o tick() (cada
// vuelta de loop() mientras este abierta) de la app REAL alojada en wmWins[idx],
// con gOffX/gOffY + gClip* limitados a su rectangulo interior -- la app dibuja
// con sus coordenadas nativas de siempre (piensa que tiene toda la pantalla) y
// aqui se traducen+recortan a su ventana. La app dibuja/parpadea-nunca via su
// propio setBuf(fb)+flxFlush(), exactamente igual que a pantalla completa: no
// pasa por bbuf, asi que NO hace falta sbRenderOverlay() para verla, solo para
// mover/redimensionar/enfocar/cerrar (eso lo sigue haciendo wmTouchWindows()).
// El toque global T se remapea a las coordenadas nativas de la app SOLO si esta
// ventana es la dueña del gesto actual (wmDrag==idx && wmAction==0, ver
// wmTouchWindows() mas abajo); en cualquier otro caso la app recibe un toque
// neutro, para poder seguir animando (ej. reloj) sin interpretar toques ajenos
// (otra ventana, el propio Panel Edge, etc.). Todo se restaura al salir, incluso
// si la app cierra su propia ventana desde dentro (ver appClose()/wmCloseIfHosted).
static void wmRunHostedApp(int idx, bool isEnter){
  if(idx < 0 || idx >= wmCount) return;
  uint8_t appID = wmWins[idx].appID;
  if(APP_REG[appID].flags & APP_NO_WINDOW) return;              // salvaguarda (nunca deberia crearse una ventana con esta app)
  void (*fn)() = isEnter ? APP_REG[appID].enter : APP_REG[appID].tick;
  if(!fn) return;
  int ix, iy, iw, ih; wmContentRect(idx, ix, iy, iw, ih);
  if(iw <= 0 || ih <= 0) return;
  int oX0 = gClipX0, oX1 = gClipX1, oY0 = gClipY0, oY1 = gClipY1, oOffX = gOffX, oOffY = gOffY;
  gClipX0 = ix; gClipX1 = ix + iw - 1; gClipY0 = iy; gClipY1 = iy + ih - 1;
  gOffX = ix; gOffY = iy - wmAppOriginY(appID);
  // Ademas del recorte de DIBUJO (gClip*), activa el recorte de VOLCADO: las
  // apps que se componen en un buffer y hacen memcpy a fb (Calculadora, Video,
  // cualquier blitToFb/present) quedan confinadas a esta ventana y ya no borran
  // la barra de estado ni el Panel Edge. Ver fbCopyBand().
  bool oWB = gWinBlit; int oBX0 = gWinBX0, oBX1 = gWinBX1, oBY0 = gWinBY0, oBY1 = gWinBY1;
  gWinBlit = true; gWinBX0 = ix; gWinBX1 = ix + iw - 1; gWinBY0 = iy; gWinBY1 = iy + ih - 1;
  Touch realT = T;
  bool ownsGesture = !isEnter && wmDrag == idx && wmAction == 0;
  if(ownsGesture){ T.x -= gOffX; T.y -= gOffY; T.startX -= gOffX; T.startY -= gOffY; }  // dx/dy son deltas: no cambian
  else {
    T.down = false; T.pressed = false; T.released = false; T.tap = false; T.moved = false;
    T.swipeUp = false; T.swipeDown = false; T.swipeLeft = false; T.swipeRight = false;
  }
  int8_t prevHost = wmHostedWin; wmHostedWin = (int8_t)idx;
  int prevAppId = gAppId; gAppId = appID;              // por si algo interno de la app lo consulta (defensivo)
  fn();
  gAppId = prevAppId;
  wmHostedWin = prevHost;
  T = realT;
  gClipX0 = oX0; gClipX1 = oX1; gClipY0 = oY0; gClipY1 = oY1; gOffX = oOffX; gOffY = oOffY;
  gWinBlit = oWB; gWinBX0 = oBX0; gWinBX1 = oBX1; gWinBY0 = oBY0; gWinBY1 = oBY1;   // restaura SIEMPRE (aunque la app se cierre sola)
}
// Tick de TODAS las apps alojadas en ventanas abiertas -- se llama en cada
// vuelta mientras wmCount>0 (igual que appTick() para una app a pantalla
// completa); cada app decide sola cuando redibujar (su propio patron ya
// probado, ej. "if(gMinChanged)"). Iteracion defensiva: si una app se cierra a
// si misma (appClose() -> wmCloseIfHosted() -> wmRemove()) el arreglo se
// compacta, asi que NO se avanza el indice ese ciclo (ver notifTick(), mismo
// criterio ya usado en este archivo para arreglos que pueden mutar mientras se
// recorren).
// Repinta el contenido de la ventana marcada por wmTouchWindows() tras un
// mover/redimensionar. Se llama SIEMPRE despues de recomponer el marco.
static void wmFlushPendingContent(){
  if(wmNeedContent < 0) return;
  int idx = wmNeedContent; wmNeedContent = -1;
  if(idx < wmCount) wmRunHostedApp(idx, true);   // enter() = repintado completo a la nueva geometria
}
static void wmTickHostedApps(){
  for(int i = 0; i < wmCount; ){
    if(wmWins[i].minimized){ i++; continue; }        // minimizada: su app no dibuja (viviria en coords viejas)
    int before = wmCount;
    wmRunHostedApp(i, false);
    if(wmCount < before) continue;
    i++;
  }
}
// Si hay una app alojada ejecutandose ahora mismo (ver wmRunHostedApp), cierra
// SOLO su ventana y devuelve true -- para que appClose() (pensado para apps a
// pantalla completa) no actue quando lo dispara el boton "atras" interno de una
// app que en realidad vive dentro de una ventana (Notas/Paint/Video/Camara/
// GeoDash tienen su propio boton de "atras" que llama a appClose()).
static bool wmCloseIfHosted(){
  if(wmHostedWin < 0) return false;
  int idx = wmHostedWin; wmHostedWin = -1;
  wmRemove(idx);
  return true;
}
static void wmRender(){
  setBuf(bbuf);
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  for(int y = 0; y < SCR_H; y++) hLine(0, y, SCR_W, mix565(rgb565(18,26,48), rgb565(30,44,80), (uint8_t)(y * 255 / (SCR_H - 1))));
  fillRect(0, 0, SCR_W, WM_TOOLB, rgb565(24,28,44));                             // barra de herramientas
  drawText(12, 12, "Ventanas", 3, rgb565(240,244,252));
  fillRoundRect(SCR_W - 212, 8, 44, 30, 8, rgb565(50,110,235)); drawTextC(SCR_W - 190, 12, "+", 3, rgb565(255,255,255));
  fillRoundRect(SCR_W - 162, 8, 44, 30, 8, rgb565(56,62,86));   drawTextC(SCR_W - 140, 15, "2", 2, rgb565(240,244,252));
  fillRoundRect(SCR_W - 112, 8, 44, 30, 8, rgb565(56,62,86));   drawTextC(SCR_W - 90, 15, "4", 2, rgb565(240,244,252));
  fillRoundRect(SCR_W - 62, 8, 54, 30, 8, rgb565(180,60,60));   drawTextC(SCR_W - 35, 16, "Salir", 1, rgb565(255,255,255));
  if(wmCount == 0) drawTextC(SCR_W / 2, SCR_H / 2, "Sin ventanas - pulsa +", 2, rgb565(180,188,205));
  int order[WM_MAX]; for(int i = 0; i < wmCount; i++) order[i] = i;               // dibujar por z ascendente
  for(int a = 0; a < wmCount; a++) for(int b = a + 1; b < wmCount; b++)
    if(wmWins[order[b]].zIndex < wmWins[order[a]].zIndex){ int t = order[a]; order[a] = order[b]; order[b] = t; }
  for(int i = 0; i < wmCount; i++) wmDrawWindow(order[i]);
  sbDrawMinBubbles();                                                             // burbujas de ventanas minimizadas (fuente unica con el overlay)
  present(0, SCR_H - 1);
}
static void wmEnter(){
  gState = ST_WINMGR; wmCount = 0; wmZTop = 0; wmMode = 0; wmDrag = -1; wmAction = 0;
  int i0 = wmAdd(WM_APPS[0]), i1 = wmAdd(WM_APPS[1]);
  wmRender();                              // marco de ambas primero (bbuf/present)
  if(i0 >= 0) wmRunHostedApp(i0, true);    // contenido real despues (fb/flxFlush propio de cada app)
  if(i1 >= 0) wmRunHostedApp(i1, true);
}
static void wmExit(){ gState = ST_HOME; renderHome(); showHome(); }
// Logica de toques SOBRE una ventana ya existente (enfocar, mover,
// redimensionar, cerrar). Extraida de wmTick() para reutilizarla tambien
// desde el overlay del Panel Edge sobre el Home (ver sbTick(), seccion
// Sidebar Dock mas abajo) -- una sola fuente de verdad para el arrastre
// de ventanas, en vez de duplicar esta logica en dos sitios. Devuelve
// true si el toque fue consumido por una ventana (hace falta repintar).
static bool wmTouchWindows(){
  if(T.pressed){
    // Burbuja de una ventana minimizada: restaurar. Se comprueba ANTES que wmTopAt
    // (la ventana minimizada no captura toques) y en ambos contextos (Modo PC y
    // overlay del Panel Edge). El repintado lo hace el llamante; aqui solo se marca
    // la ventana para repintar su contenido tras recomponer el marco (wmNeedContent).
    int bi;
    if(sbHitMinBubble(T.x, T.y, bi)){ wmWins[bi].minimized = false; wmFocus(bi); wmNeedContent = (int8_t)bi; wmDrag = -1; return true; }
    int idx = wmTopAt(T.x, T.y);
    if(idx < 0){ wmDrag = -1; return false; }
    bool wasFocused = wmWins[idx].isFocused;
    wmFocus(idx);
    WindowInstance& w = wmWins[idx];
    if(T.y >= w.y && T.y < w.y + 30){                      // barra de titulo: primero los 5 botones de control
      for(int b = 0; b < 5; b++){
        if(abs(T.x - wmCtrlCX(w.x, w.w, b)) <= 13 && abs(T.y - (w.y + 15)) <= 15){ wmCtrlAction(idx, b); wmDrag = -1; return true; }
      }
    }
    if(T.x >= w.x + w.w - 22 && T.y >= w.y + w.h - 22){ wmDrag = idx; wmAction = 2; return true; }                  // redimensionar
    if(T.y < w.y + 30){ wmDrag = idx; wmAction = 1; wmDX = T.x - w.x; wmDY = T.y - w.y; return true; }              // mover (zona libre de la barra)
    wmDrag = idx; wmAction = 0;                            // contenido: lo atiende wmRunHostedApp() (ver wmTickHostedApps)
    return !wasFocused;                                    // solo recomponer marco si el foco realmente cambio
  }
  if(T.down && wmDrag >= 0){
    WindowInstance& w = wmWins[wmDrag];
    if(wmAction == 1){                                           // MOVER (barra de titulo)
      w.x = T.x - wmDX; w.y = T.y - wmDY; w.isFloating = true;
      if(w.x < 0) w.x = 0; if(w.x + w.w > SCR_W) w.x = SCR_W - w.w;
      if(w.y < WM_TOOLB) w.y = WM_TOOLB; if(w.y + w.h > WM_NAV) w.y = WM_NAV - w.h;
      return true;
    } else if(wmAction == 2){                                    // REDIMENSIONAR (esquina, min 150x150)
      w.w = T.x - w.x; w.h = T.y - w.y; w.isFloating = true;
      if(w.w < 150) w.w = 150; if(w.h < 150) w.h = 150;
      if(w.x + w.w > SCR_W) w.w = SCR_W - w.x; if(w.y + w.h > WM_NAV) w.h = WM_NAV - w.y;
      return true;
    }
    return false;
  }
  if(T.released){
    // Al soltar tras MOVER o REDIMENSIONAR, el marco se recompone pero el area
    // de contenido se queda con el fondo neutro que pinta wmDrawWindow(): la
    // ventana quedaba GRIS y muerta para siempre, porque el tick() de casi
    // todas las apps solo repinta cuando algo cambia. Se marca la ventana para
    // que el llamante repinte su contenido JUSTO DESPUES de recomponer el marco
    // (el orden importa: primero marco, luego contenido encima).
    if(wmDrag >= 0 && (wmAction == 1 || wmAction == 2)){
      wmNeedContent = (int8_t)wmDrag; wmDrag = -1; wmAction = 0; return true;
    }
    wmDrag = -1; wmAction = 0;
  }
  return false;
}
static void wmTick(){
  if(T.tap && T.y < WM_TOOLB){                                  // barra de herramientas
    if(T.x >= SCR_W - 212 && T.x < SCR_W - 168){
      int ni = wmAdd(WM_APPS[wmCount % 4]); wmRender();
      if(ni >= 0) wmRunHostedApp(ni, true);
    }
    else if(T.x >= SCR_W - 162 && T.x < SCR_W - 118){ setWindowLayout(2); wmRender(); }
    else if(T.x >= SCR_W - 112 && T.x < SCR_W - 68){ setWindowLayout(4); wmRender(); }
    else if(T.x >= SCR_W - 62){ wmExit(); }
    return;
  }
  if(wmTouchWindows()){ wmRender(); wmFlushPendingContent(); }   // marco y, si toca, contenido de la ventana movida/redimensionada
  int beforeCount = wmCount;
  wmTickHostedApps();                        // contenido real de cada ventana, cada vuelta (self-throttle propio de cada app)
  if(wmCount != beforeCount) wmRender();      // una app se cerro sola (boton "atras" propio) -> recomponer el marco
}

// #############################################################
// ##  PANEL EDGE (SIDEBAR DOCK) -- Fase 3 (reescrito para paridad Samsung)
// ##  Tirador en el borde izquierdo. Se ABRE de dos formas (como el Edge
// ##  Panel real): un TOQUE simple, o ARRASTRANDO el tirador hacia el
// ##  centro (el panel se revela siguiendo al dedo; al soltar decide por
// ##  posicion/velocidad si termina de abrir o se retrae).
// ##
// ##  Con el panel abierto:
// ##    · TOQUE sobre un icono         -> abre la app en VENTANA FLOTANTE
// ##                                      (crece desde el icono). Imagen A:
// ##                                      barra de control (restaurar /
// ##                                      minimizar / split / expandir /
// ##                                      cerrar) en el borde superior.
// ##    · LONG-PRESS + arrastrar icono -> lo despega y sigue al dedo:
// ##        Y < SB_DROP_TOP            -> Split Screen (mitad superior = la
// ##                                      app soltada; abajo, selector).
// ##        SB_DROP_TOP..SB_DROP_BOT   -> Ventana Flotante en ese punto.
// ##        Y > SB_DROP_BOT            -> Cancelar (resorte de vuelta).
// ##    · SCROLL vertical de la lista con REBOTE (rubber-band) en los
// ##      extremos y recorte estricto a la mascara del panel.
// ##    · Boton de CAPTURA (arriba) y, fijos abajo, REJILLA (añadir apps)
// ##      y LAPIZ (modo edicion: jiggle + quitar con X + reordenar).
// ##
// ##  DECISION DE DISENO (documentada; cambia el comportamiento previo):
// ##  el Panel Edge sigue ACCESIBLE con ventanas abiertas (wmCount>0), como
// ##  en Samsung real -- el tirador se dibuja SOBRE las ventanas y el panel
// ##  se puede abrir para lanzar mas apps o rellenar el Split. Antes se
// ##  ocultaba hasta cerrar todo; ahora no. sbOwnsScreen() sigue siendo la
// ##  unica fuente de verdad de "quien manda en la pantalla".
// ##
// ##  Composicion SIEMPRE offscreen (bbuf) + present() de una pasada, para
// ##  no mostrar nunca un frame a medio dibujar (mismo patron del resto del
// ##  sistema). Fondo: homeBuf en Home, appSnapBuf (instantanea de fb) en
// ##  una App -- se congela al abrir el panel y se reanuda al cerrar.
// #############################################################
enum GestureState { GEST_IDLE, GEST_PRESSED, GEST_DRAGGING, GEST_SCROLLING, GEST_HANDLE, GEST_REORDER };

#define SB_TAB_X        12          // ancho del tirador (franja pegada al borde)
#define SB_TAB_Y        340         // Y del tirador (ligeramente sobre el centro)
#define SB_TAB_H        120
#define SB_PANEL_W      92          // ancho del panel abierto
#define SB_PANEL_TOP    64          // borde superior del panel (bajo la barra de estado)
#define SB_PANEL_BOT    WM_NAV      // borde inferior del panel (=750, respeta la nav)
#define SB_ICON_S       56
#define SB_ICON_X       ((SB_PANEL_W - SB_ICON_S) / 2)   // iconos centrados en el panel (=18)
#define SB_ICON_GAP     22          // separacion vertical entre iconos
#define SB_MAX_PINNED   16          // como mucho, TODAS las apps (0..15) ancladas al panel
#define SB_SHOT_Y       (SB_PANEL_TOP + 12)               // boton de captura (fijo, arriba) =76
#define SB_LIST_TOP     (SB_SHOT_Y + SB_ICON_S + 16)      // primer icono con sbScrollY==0  =148
#define SB_BTN_S        52          // lado de los botones fijos inferiores
#define SB_BTN_X        ((SB_PANEL_W - SB_BTN_S) / 2)     // centrados (=20)
#define SB_PENCIL_Y     (SB_PANEL_BOT - SB_BTN_S - 14)    // lapiz (modo edicion) =684
#define SB_GRID_Y       (SB_PENCIL_Y - SB_BTN_S - 10)     // rejilla (añadir apps) =622
#define SB_LIST_BOT     (SB_GRID_Y - 14)                  // fondo de la mascara de la lista =608
#define SB_MASK_TOP     SB_LIST_TOP   // mascara visible/tactil de la lista: NADA fuera de aqui
#define SB_MASK_BOT     SB_LIST_BOT
#define SB_LONGPRESS    350UL       // ms sin moverse para pasar a arrastre/reordenar (mas agil que 500, ver nota)
#define SB_DROP_TOP     150         // Y < 150            -> Split Screen
#define SB_DROP_BOT     650         // Y > 650            -> Cancelar (resorte). 150..650 -> Flotante
#define SB_OPEN_ANIM_MS 200UL       // duracion de TODAS las animaciones del panel (ease-out cubico)
#define SB_SPLIT_COLS   4           // selector del split: grid 4x4 -- 16 apps exactas, sin scroll
#define SB_SPLIT_ICON_S 64
#define SB_FRAME_MS     30UL        // throttle del redibujado continuo durante arrastre/scroll (~33/seg)
#define SB_OVERSCROLL   64          // margen elastico de rebote en los extremos del scroll
#define SB_MIN_BUB_S    54          // diametro de la burbuja de una ventana minimizada

// Nota SB_LONGPRESS: 350 ms se siente mas cercano al long-press real de un
// Samsung (500 ms arrastraba tarde). El umbral de movimiento (12 px) decide
// antes que el tiempo si el dedo se desplaza: mover = scroll, quieto = arrastre.

static bool          sbPanelOpen        = false;
static bool          sbEditMode         = false;   // lapiz: jiggle + quitar + reordenar
static bool          sbAddPickerOpen     = false;   // rejilla: selector de apps para añadir/quitar del panel
static GestureState  sbGesture          = GEST_IDLE;
static int8_t        sbDragIcon         = -1;       // slot 0..sbPinnedCount-1 en arrastre/reorden (o -1)
static float         sbDragX = 0, sbDragY = 0;      // posicion del icono arrastrado (sigue al dedo)
static unsigned long sbPressMs = 0;
static int           sbPressX = 0, sbPressY = 0;
static int           sbScrollY = 0;                 // desplazamiento de la lista (puede salirse en el rebote)
static int           sbScrollLastY = 0;             // Y del dedo el frame anterior (delta del scroll)
static int           sbHandleReveal = 0;            // ancho revelado del panel durante el arrastre del tirador
static bool          sbSplitBottomPicker = false;   // wmWins[] ya tiene la mitad superior; falta elegir la inferior
static unsigned long sbFrameMs = 0;                 // throttle del redibujado continuo
static unsigned long sbStuckMs = 0;                 // watchdog de estados transitorios (ultimo recurso)

// ---- Lista de apps ancladas al panel (fuente propia, editable) --------------
// Antes el panel reutilizaba homeOrder[]+dock; para poder AÑADIR/QUITAR/REORDENAR
// desde el modo edicion sin tocar la rejilla del Home, el panel tiene su propia
// lista persistida. Por defecto: las 16 apps. Persistencia con el mismo patron
// que homeOrder (prefs "flexos").
static uint8_t sbPinned[SB_MAX_PINNED];
static int     sbPinnedCount = 0;
static void sbPinnedDefault(){ sbPinnedCount = 16; for(int i = 0; i < 16; i++) sbPinned[i] = (uint8_t)i; }
static void sbPinnedSave(){
  prefs.begin("flexos", false);
  prefs.putBytes("sbpin", sbPinned, SB_MAX_PINNED);
  prefs.putInt("sbpinc", sbPinnedCount);
  prefs.end();
}
static void sbPinnedLoad(){
  prefs.begin("flexos", true);
  size_t n = prefs.getBytes("sbpin", sbPinned, SB_MAX_PINNED);
  int c = prefs.getInt("sbpinc", -1);
  prefs.end();
  if(n != SB_MAX_PINNED || c < 1 || c > SB_MAX_PINNED){ sbPinnedDefault(); return; }
  bool seen[16] = { false };                         // validacion: appID<16 y sin duplicados en [0,c)
  for(int i = 0; i < c; i++){ if(sbPinned[i] >= 16 || seen[sbPinned[i]]){ sbPinnedDefault(); return; } seen[sbPinned[i]] = true; }
  sbPinnedCount = c;
}
static bool sbIsPinned(uint8_t appID){ for(int i = 0; i < sbPinnedCount; i++) if(sbPinned[i] == appID) return true; return false; }
static void sbPinAdd(uint8_t appID){ if(sbPinnedCount >= SB_MAX_PINNED || sbIsPinned(appID)) return; sbPinned[sbPinnedCount++] = appID; sbPinnedSave(); }
static void sbPinRemove(int slot){
  if(slot < 0 || slot >= sbPinnedCount) return;
  for(int i = slot; i < sbPinnedCount - 1; i++) sbPinned[i] = sbPinned[i + 1];
  sbPinnedCount--; sbPinnedSave();
}
// OJO: NO persiste. El reorden se llama en cada cruce de slot durante el
// arrastre; guardar en NVS aqui desgastaria la flash y daria tirones. Se
// persiste UNA vez al soltar (ver rama GEST_REORDER de T.released en sbTick).
static void sbPinMove(int from, int to){
  if(from < 0 || to < 0 || from >= sbPinnedCount || to >= sbPinnedCount || from == to) return;
  uint8_t v = sbPinned[from];
  if(from < to) for(int i = from; i < to; i++) sbPinned[i] = sbPinned[i + 1];
  else          for(int i = from; i > to; i--) sbPinned[i] = sbPinned[i - 1];
  sbPinned[to] = v;
}
// Slot -> appID real. Fuente unica para dibujar la lista Y para el hit-test.
static uint8_t sbAppId(int slot){ return (slot >= 0 && slot < sbPinnedCount) ? sbPinned[slot] : 0; }

// ---- Geometria de la lista --------------------------------------------------
// Y del icono `i` con el scroll actual. Usada al dibujar (sbDrawPanel) Y al
// tocar (sbIconAt/sbSlotAtY): una sola funcion, imposible desincronizar.
static int sbIconY(int i){ return SB_LIST_TOP + i * (SB_ICON_S + SB_ICON_GAP) - sbScrollY; }
static int sbScrollMax(){
  if(sbPinnedCount <= 0) return 0;
  int contentH = (sbPinnedCount - 1) * (SB_ICON_S + SB_ICON_GAP) + SB_ICON_S;   // hasta el FONDO del ultimo icono
  int visH = SB_LIST_BOT - SB_LIST_TOP;
  int mx = contentH - visH;
  return (mx > 0) ? mx : 0;                          // asi el ultimo icono se ve COMPLETO al final del scroll
}
// Fondo sobre el que compone el overlay: homeBuf en Home, appSnapBuf en una App.
static uint16_t* sbBgBuf(){ return (gState == ST_APP) ? appSnapBuf : homeBuf; }

// ---- Tirador ----------------------------------------------------------------
// Pastilla semi-transparente con marca central (grip). Se hornea en homeBuf
// desde renderHome() (Home) y se recompone en el overlay sobre las ventanas.
static void sbDrawTabHandle(){
  fillRoundRect(0, SB_TAB_Y, SB_TAB_X, SB_TAB_H, 6, rgb565(90,110,190));
  fillRoundRect(3, SB_TAB_Y + SB_TAB_H / 2 - 14, 4, 28, 2, rgb565(230,234,250));  // grip
}
// Igual, pero reafirmado sobre fb dentro de una App (no hay homeBuf donde
// hornearlo). Throttle ~8/seg. Compone ENTERO en bbuf (copiando la banda real
// de fb primero) y vuelca de una pasada -> nunca se ve a medio pintar aunque el
// presentador (Core 0) lea fb en mitad del dibujo.
static unsigned long sbTabAppMs = 0;
static void sbDrawTabOnApp(){
  if(!bbuf || !fb) return;               // guarda nula coherente con el resto de la seccion
  unsigned long now = millis();
  if(now - sbTabAppMs < 120) return;
  sbTabAppMs = now;
  for(int j = SB_TAB_Y; j <= SB_TAB_Y + SB_TAB_H; j++)
    memcpy(bbuf + (size_t)j * SCR_W, fb + (size_t)j * SCR_W, SCR_W * 2);
  setBuf(bbuf);
  sbDrawTabHandle();
  present(SB_TAB_Y, SB_TAB_Y + SB_TAB_H);
}
static bool sbHitTab(int px, int py){
  return px >= 0 && px < SB_TAB_X && py >= SB_TAB_Y && py <= SB_TAB_Y + SB_TAB_H;
}

// ---- Hit-tests del panel ----------------------------------------------------
// Icono `which` bajo (px,py), con el scroll aplicado (via sbIconY) y recortado a
// la mascara: no se puede tocar lo que no se ve.
static bool sbIconAt(int px, int py, int &which){
  if(px < SB_ICON_X - 4 || px > SB_ICON_X + SB_ICON_S + 4) return false;
  for(int i = 0; i < sbPinnedCount; i++){
    int iy = sbIconY(i);
    if(iy + SB_ICON_S < SB_MASK_TOP || iy > SB_MASK_BOT) continue;
    if(py >= iy && py <= iy + SB_ICON_S){ which = i; return true; }
  }
  return false;
}
// Slot cuyo CENTRO vertical esta mas cerca de py (para el reorden en edicion).
static bool sbSlotAtY(int py, int &slot){
  for(int i = 0; i < sbPinnedCount; i++){
    int iy = sbIconY(i);
    if(iy + SB_ICON_S < SB_MASK_TOP || iy > SB_MASK_BOT) continue;
    if(py >= iy && py <= iy + SB_ICON_S){ slot = i; return true; }
  }
  return false;
}
static bool sbHitScreenshot(int px, int py){ return px >= SB_ICON_X && px <= SB_ICON_X + SB_ICON_S && py >= SB_SHOT_Y && py <= SB_SHOT_Y + SB_ICON_S; }
static bool sbHitGridBtn(int px, int py){ return px >= SB_BTN_X && px <= SB_BTN_X + SB_BTN_S && py >= SB_GRID_Y && py <= SB_GRID_Y + SB_BTN_S; }
static bool sbHitPencilBtn(int px, int py){ return px >= SB_BTN_X && px <= SB_BTN_X + SB_BTN_S && py >= SB_PENCIL_Y && py <= SB_PENCIL_Y + SB_BTN_S; }
// Badge "X" de quitar (modo edicion) sobre el icono `slot`.
static bool sbHitRemoveBadge(int px, int py, int &slot){
  for(int i = 0; i < sbPinnedCount; i++){
    int iy = sbIconY(i);
    if(iy + SB_ICON_S < SB_MASK_TOP || iy > SB_MASK_BOT) continue;
    int bx = SB_ICON_X + 8, by = iy + 8, dx = px - bx, dy = py - by;
    if(dx * dx + dy * dy <= 13 * 13){ slot = i; return true; }
  }
  return false;
}

// ---- Burbujas de ventanas minimizadas --------------------------------------
// Una ventana minimizada colapsa a un circulo con su icono, apilado en el borde
// derecho. Tap en la burbuja -> restaurar. Posicion determinista (no se guarda
// estado extra): el orden es el de aparicion en wmWins[]. Dibujo y hit-test
// comparten sbMinBubblePos() -> imposible desincronizar lo que se ve y se toca.
static void sbMinBubblePos(int order, int &cx, int &cy){
  cx = SCR_W - SB_MIN_BUB_S / 2 - 6;
  cy = 120 + order * (SB_MIN_BUB_S + 12) + SB_MIN_BUB_S / 2;
}
static void sbDrawMinBubbles(){
  int order = 0;
  for(int i = 0; i < wmCount; i++){
    if(!wmWins[i].minimized) continue;
    int cx, cy; sbMinBubblePos(order, cx, cy); order++;
    fillCircleA(cx + 2, cy + 3, SB_MIN_BUB_S / 2, rgb565(6,8,14), 120);   // sombra
    fillCircle(cx, cy, SB_MIN_BUB_S / 2, rgb565(40,54,110));
    drawAppIcon(wmWins[i].appID, cx - 18, cy - 18, 36);                    // icono centrado (mas pequeno que la burbuja)
  }
}
static bool sbHitMinBubble(int px, int py, int &idx){
  int order = 0;
  for(int i = 0; i < wmCount; i++){
    if(!wmWins[i].minimized) continue;
    int cx, cy; sbMinBubblePos(order, cx, cy); order++;
    int dx = px - cx, dy = py - cy;
    if(dx * dx + dy * dy <= (SB_MIN_BUB_S / 2) * (SB_MIN_BUB_S / 2)){ idx = i; return true; }
  }
  return false;
}

static void sbOpenPanel(){  sbPanelOpen = true;  sbGesture = GEST_IDLE; sbDragIcon = -1; sbEditMode = false; sbAddPickerOpen = false;
                            if(sbScrollY < 0) sbScrollY = 0; if(sbScrollY > sbScrollMax()) sbScrollY = sbScrollMax(); }
static void sbClosePanel(){ sbPanelOpen = false; sbGesture = GEST_IDLE; sbDragIcon = -1; sbEditMode = false; sbAddPickerOpen = false; }
// true mientras el Panel Edge (panel/arrastre/ventanas/selectores) tiene el
// control visual. uiTick() la consulta para NO lanzar animaciones que pelean
// por bbuf. Fuente unica de verdad -- ninguna bandera aparte que desincronizar.
static bool sbOwnsScreen(){ return sbPanelOpen || sbAddPickerOpen || sbSplitBottomPicker || sbGesture != GEST_IDLE || wmCount > 0; }

// ---- Acciones de la barra de control de ventana (Imagen A) -------------------
// Definidas aqui (necesitan sbSplitBottomPicker); declaradas arriba para que
// wmDrawWindow()/wmTouchWindows() (seccion Window Manager, mas arriba) las usen.
// NO recomponen: solo mutan estado y marcan wmNeedContent -- el llamante
// (wmTick()->wmRender() o sbTick()->sbRenderOverlay()) recompone con el
// renderer correcto de su contexto.
static void wmClampWin(int idx){
  WindowInstance& w = wmWins[idx];
  if(w.w < 150) w.w = 150; if(w.h < 150) w.h = 150;
  if(w.w > SCR_W) w.w = SCR_W; if(w.h > WM_NAV - SB_PANEL_TOP) w.h = WM_NAV - SB_PANEL_TOP;
  if(w.x < 0) w.x = 0; if(w.x + w.w > SCR_W) w.x = SCR_W - w.w;
  if(w.y < SB_PANEL_TOP) w.y = SB_PANEL_TOP; if(w.y + w.h > WM_NAV) w.y = WM_NAV - w.h;
}
static void wmCtrlAction(int idx, int b){
  if(idx < 0 || idx >= wmCount) return;
  WindowInstance& w = wmWins[idx];
  switch(b){
    case 0:  // RESTAURAR: tamano/posicion flotante por defecto (420x560, centrada)
      w.isFloating = true; w.w = 420; w.h = 560; w.x = (SCR_W - w.w) / 2; w.y = SB_PANEL_TOP;
      wmClampWin(idx); wmNeedContent = (int8_t)idx; break;
    case 1:  // MINIMIZAR: a burbuja (no se dibuja ni ejecuta su tick)
      w.minimized = true; break;
    case 2:  // SPLIT: esta ventana pasa a mitad superior + selector de la inferior
      w.isFloating = false; w.x = 0; w.y = SB_PANEL_TOP; w.w = SCR_W; w.h = (WM_NAV - SB_PANEL_TOP) / 2;
      sbSplitBottomPicker = true; wmNeedContent = (int8_t)idx; break;
    case 3:  // EXPANDIR: maximizar al area util (sigue siendo ventana)
      w.isFloating = true; w.x = 0; w.y = SB_PANEL_TOP; w.w = SCR_W; w.h = WM_NAV - SB_PANEL_TOP;
      wmNeedContent = (int8_t)idx; break;
    case 4:  // CERRAR
      wmRemove(idx); if(wmCount == 0) sbSplitBottomPicker = false; break;
  }
}

// Abre appID como ventana flotante centrada en (cx,cy) -- reutiliza wmWins[]
// (mismo almacen que el Window Manager de pantalla completa) SIN cambiar gState.
static void sbOpenFloating(uint8_t appID, int cx, int cy){
  if(wmCount >= WM_MAX) return;
  if(APP_REG[appID].flags & APP_NO_WINDOW) return;
  int n = wmCount;
  wmWins[n].appID = appID; wmWins[n].isFloating = true; wmWins[n].used = true; wmWins[n].minimized = false;
  wmWins[n].w = 420; wmWins[n].h = 560;
  wmWins[n].x = cx - wmWins[n].w / 2; wmWins[n].y = cy - wmWins[n].h / 2;
  if(wmWins[n].x < 0) wmWins[n].x = 0;
  if(wmWins[n].x + wmWins[n].w > SCR_W) wmWins[n].x = SCR_W - wmWins[n].w;
  if(wmWins[n].y < SB_PANEL_TOP) wmWins[n].y = SB_PANEL_TOP;
  if(wmWins[n].y + wmWins[n].h > WM_NAV) wmWins[n].y = WM_NAV - wmWins[n].h;
  wmWins[n].zIndex = ++wmZTop;
  wmCount++;
  wmFocus(n);
}
// Cierra TODO (ventanas/split/panel) y abre appID a pantalla completa -- para
// apps que exigen pantalla completa (APP_NO_WINDOW: Modo PC, Ajustes) al
// tocarlas en el panel: no se pueden alojar en una ventana.
static void sbLaunchFull(uint8_t appID){
  wmCount = 0; wmHostedWin = -1; sbSplitBottomPicker = false;
  sbClosePanel();
  enterApp((int)appID);
}

// ---- Selector de la mitad inferior del Split (grid 4x4, TODAS las apps) ------
static bool sbSplitPickerAt(int px, int py, int &appID){
  int y0 = SB_PANEL_TOP + (WM_NAV - SB_PANEL_TOP) / 2;
  if(px < 0 || px >= SCR_W || py < y0 || py >= WM_NAV) return false;
  int cellW = SCR_W / SB_SPLIT_COLS, cellH = (WM_NAV - SB_PANEL_TOP) / 2 / SB_SPLIT_COLS;
  int col = px / cellW; if(col >= SB_SPLIT_COLS) col = SB_SPLIT_COLS - 1;
  int row = (py - y0) / cellH; if(row >= SB_SPLIT_COLS) row = SB_SPLIT_COLS - 1;
  appID = row * SB_SPLIT_COLS + col;
  return appID >= 0 && appID < 16;
}
static void sbDrawSplitPicker(){
  int y0 = SB_PANEL_TOP + (WM_NAV - SB_PANEL_TOP) / 2;
  int cellW = SCR_W / SB_SPLIT_COLS, cellH = (WM_NAV - SB_PANEL_TOP) / 2 / SB_SPLIT_COLS;
  if(uiGlass) drawLiquidGlassPanel(0, y0, SCR_W, WM_NAV - y0, 0, rgb565(40,54,110), millis());
  else fillRect(0, y0, SCR_W, WM_NAV - y0, rgb565(28,32,52));
  drawTextC(SCR_W / 2, y0 + 6, "Elige la otra app", 2, rgb565(210,216,235));
  for(int a = 0; a < 16; a++){
    int col = a % SB_SPLIT_COLS, row = a / SB_SPLIT_COLS;
    int ix = col * cellW + (cellW - SB_SPLIT_ICON_S) / 2;
    int iy = y0 + 24 + row * cellH + (cellH - SB_SPLIT_ICON_S) / 2;
    drawAppIcon(a, ix, iy, SB_SPLIT_ICON_S);
  }
}

// ---- Selector "añadir apps" (rejilla del panel, modo edicion) ---------------
static void sbDrawAddPicker(){
  const int cols = 4, s = 64, rows = 4;
  int gw = cols * s + (cols - 1) * 24, gx0 = (SCR_W - gw) / 2;
  int gh = rows * s + (rows - 1) * 24 + 80, gy0 = (SCR_H - gh) / 2;
  fillRoundRectA(gx0 - 24, gy0 - 24, gw + 48, gh + 16, 24, rgb565(18,22,40), 235);
  drawTextC(SCR_W / 2, gy0 - 16, "A\xC3\xB1" "adir al panel", 2, rgb565(230,234,250));
  for(int a = 0; a < 16; a++){
    int c = a % cols, r = a / cols, ix = gx0 + c * (s + 24), iy = gy0 + 30 + r * (s + 24);
    drawAppIcon(a, ix, iy, s);
    if(sbIsPinned((uint8_t)a)){                       // ya anclada: tic verde
      fillCircle(ix + s - 8, iy + 8, 9, rgb565(70,200,120));
      strokeSegAA(ix + s - 12, iy + 8, ix + s - 9, iy + 12, 1.6f, rgb565(255,255,255));
      strokeSegAA(ix + s - 9, iy + 12, ix + s - 4, iy + 4, 1.6f, rgb565(255,255,255));
    }
  }
  fillRoundRect(SCR_W / 2 - 50, gy0 + gh - 40, 100, 34, 12, rgb565(50,110,235));
  drawTextC(SCR_W / 2, gy0 + gh - 32, "Listo", 2, rgb565(255,255,255));
}
static void sbAddPickerTick(){
  if(!T.tap) return;
  const int cols = 4, s = 64, rows = 4;
  int gw = cols * s + (cols - 1) * 24, gx0 = (SCR_W - gw) / 2;
  int gh = rows * s + (rows - 1) * 24 + 80, gy0 = (SCR_H - gh) / 2;
  if(T.y >= gy0 + gh - 40 && T.y <= gy0 + gh - 6 && T.x >= SCR_W / 2 - 50 && T.x <= SCR_W / 2 + 50){ sbAddPickerOpen = false; sbRenderOverlay(); return; }
  for(int a = 0; a < 16; a++){
    int c = a % cols, r = a / cols, ix = gx0 + c * (s + 24), iy = gy0 + 30 + r * (s + 24);
    if(T.x >= ix && T.x <= ix + s && T.y >= iy && T.y <= iy + s){
      if(sbIsPinned((uint8_t)a)){ for(int i = 0; i < sbPinnedCount; i++) if(sbPinned[i] == a){ sbPinRemove(i); break; } }
      else sbPinAdd((uint8_t)a);
      if(sbScrollY > sbScrollMax()) sbScrollY = sbScrollMax();
      sbRenderOverlay(); return;
    }
  }
  if(T.x < gx0 - 24 || T.x > gx0 + gw + 24 || T.y < gy0 - 24 || T.y > gy0 + gh){ sbAddPickerOpen = false; sbRenderOverlay(); }
}

// ---- Botones fijos del panel ------------------------------------------------
static void sbDrawScreenshotBtn(){
  int x = SB_ICON_X, y = SB_SHOT_Y, s = SB_ICON_S, m = 12;
  fillRoundRect(x, y, s, s, 14, rgb565(52,66,120));
  uint16_t w = rgb565(230,234,250);                   // glifo: 4 esquinas (captura con seleccion)
  strokeSegAA(x + m, y + m, x + m + 10, y + m, 1.6f, w);         strokeSegAA(x + m, y + m, x + m, y + m + 10, 1.6f, w);
  strokeSegAA(x + s - m, y + m, x + s - m - 10, y + m, 1.6f, w); strokeSegAA(x + s - m, y + m, x + s - m, y + m + 10, 1.6f, w);
  strokeSegAA(x + m, y + s - m, x + m + 10, y + s - m, 1.6f, w); strokeSegAA(x + m, y + s - m, x + m, y + s - m - 10, 1.6f, w);
  strokeSegAA(x + s - m, y + s - m, x + s - m - 10, y + s - m, 1.6f, w); strokeSegAA(x + s - m, y + s - m, x + s - m, y + s - m - 10, 1.6f, w);
}
static void sbDrawBottomBtns(){
  uint16_t w = rgb565(230,234,250);
  int gy = SB_GRID_Y, s = SB_BTN_S, x = SB_BTN_X;      // rejilla (añadir apps)
  fillRoundRect(x, gy, s, s, 14, sbAddPickerOpen ? rgb565(70,110,220) : rgb565(46,56,96));
  for(int r = 0; r < 3; r++) for(int c = 0; c < 3; c++) fillCircle(x + 16 + c * 10, gy + 16 + r * 10, 2, w);
  int py = SB_PENCIL_Y;                                 // lapiz (modo edicion)
  fillRoundRect(x, py, s, s, 14, sbEditMode ? rgb565(70,110,220) : rgb565(46,56,96));
  strokeSegAA(x + 16, py + s - 16, x + s - 16, py + 16, 2.2f, w);   // cuerpo
  strokeSegAA(x + 14, py + s - 14, x + 20, py + s - 20, 2.2f, w);   // punta
}
// Pinta el panel completo: fondo glass + captura + lista (recortada a la
// mascara) + botones fijos + (en arrastre) silueta bajo el dedo y zonas de drop.
static void sbDrawPanel(){
  int ph = SB_PANEL_BOT - SB_PANEL_TOP;
  if(uiGlass) drawLiquidGlassPanel(0, SB_PANEL_TOP, SB_PANEL_W, ph, 18, rgb565(40,54,110), millis());
  else fillRoundRect(0, SB_PANEL_TOP, SB_PANEL_W, ph, 18, rgb565(28,32,52));
  sbDrawScreenshotBtn();
  int c0 = gClipY0, c1 = gClipY1;                       // recorte estricto de la lista (mismo patron que Ajustes)
  gClipY0 = SB_MASK_TOP; gClipY1 = SB_MASK_BOT;
  unsigned long now = millis();
  for(int i = 0; i < sbPinnedCount; i++){
    if(i == sbDragIcon && (sbGesture == GEST_DRAGGING || sbGesture == GEST_REORDER)) continue;   // se dibuja siguiendo al dedo
    int iy = sbIconY(i);
    if(iy + SB_ICON_S < SB_MASK_TOP || iy > SB_MASK_BOT) continue;
    int jx = 0, jy = 0;
    if(sbEditMode){ jx = (int)(2.0f * sinf(now * 0.012f + i * 1.3f)); jy = (int)(2.0f * cosf(now * 0.011f + i * 1.7f)); }
    drawAppIcon(sbAppId(i), SB_ICON_X + jx, iy + jy, SB_ICON_S);
    if(sbEditMode){                                      // badge "X" para quitar
      int bx = SB_ICON_X + jx + 8, by = iy + jy + 8;
      fillCircle(bx, by, 8, rgb565(230,70,70));
      strokeSegAA(bx - 3, by - 3, bx + 3, by + 3, 1.6f, rgb565(255,255,255));
      strokeSegAA(bx - 3, by + 3, bx + 3, by - 3, 1.6f, rgb565(255,255,255));
    }
  }
  gClipY0 = c0; gClipY1 = c1;
  sbDrawBottomBtns();
  if(sbGesture == GEST_DRAGGING && sbDragIcon >= 0){     // arrastre fuera del panel (crear ventana/split)
    int s = (int)(SB_ICON_S * 0.9f), dx = (int)sbDragX - s / 2, dy = (int)sbDragY - s / 2;
    if(sbDragY < SB_DROP_TOP)        fillRectA(0, 0, SCR_W, SB_DROP_TOP, rgb565(60,140,255), 60);
    else if(sbDragY <= SB_DROP_BOT)  fillRectA(0, SB_DROP_TOP, SCR_W, SB_DROP_BOT - SB_DROP_TOP, rgb565(70,200,120), 40);
    fillRoundRectA(dx - 6, dy - 6, s + 12, s + 12, 14, rgb565(60,90,180), 150);
    drawAppIcon(sbAppId(sbDragIcon), dx, dy, s);
  }
  if(sbGesture == GEST_REORDER && sbDragIcon >= 0){       // reorden dentro del panel (modo edicion)
    int dx = SB_ICON_X, dy = (int)sbDragY - SB_ICON_S / 2;
    if(dy < SB_MASK_TOP) dy = SB_MASK_TOP; if(dy > SB_MASK_BOT - SB_ICON_S) dy = SB_MASK_BOT - SB_ICON_S;
    fillRoundRectA(dx - 4, dy - 4, SB_ICON_S + 8, SB_ICON_S + 8, 12, rgb565(60,90,180), 120);
    drawAppIcon(sbAppId(sbDragIcon), dx, dy, SB_ICON_S);
  }
}

// ---- Composicion del overlay (fuente unica) ---------------------------------
// Dibuja las ventanas no minimizadas por z ascendente. Extraida para no
// duplicar el bucle en cada animacion (reveal/split/floating/screenshot).
static void sbDrawWindowsSorted(){
  int order[WM_MAX], m = 0;
  for(int i = 0; i < wmCount; i++) if(!wmWins[i].minimized) order[m++] = i;
  for(int a = 0; a < m; a++) for(int b = a + 1; b < m; b++)
    if(wmWins[order[b]].zIndex < wmWins[order[a]].zIndex){ int t = order[a]; order[a] = order[b]; order[b] = t; }
  for(int i = 0; i < m; i++) wmDrawWindow(order[i]);
}
// Compone TODA la pantalla: fondo + ventanas + burbujas + selectores + panel o
// tirador. Full-frame en bbuf y present() de una pasada (anti-parpadeo).
static void sbRenderOverlay(){
  uint16_t* bg = sbBgBuf();
  if(!bbuf || !bg) return;
  setBuf(bbuf);
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  memcpy(bbuf, bg, (size_t)SCR_W * SCR_H * 2);
  sbDrawWindowsSorted();
  sbDrawMinBubbles();
  if(sbSplitBottomPicker) sbDrawSplitPicker();
  if(sbPanelOpen)         sbDrawPanel();
  else                    sbDrawTabHandle();            // tirador SOBRE las ventanas (Samsung real)
  if(sbAddPickerOpen)     sbDrawAddPicker();            // encima de todo
  present(0, SCR_H - 1);
}
// Un frame del panel a medio revelar (ancho `w`) durante el arrastre del
// tirador: fondo + ventanas + burbujas + rectangulo glass de ancho w.
static void sbRevealFrame(int w){
  uint16_t* bg = sbBgBuf();
  if(!bbuf || !bg) return;
  setBuf(bbuf);
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  memcpy(bbuf, bg, (size_t)SCR_W * SCR_H * 2);
  sbDrawWindowsSorted();
  sbDrawMinBubbles();
  if(w > 2){
    if(uiGlass) drawLiquidGlassPanel(0, SB_PANEL_TOP, w, SB_PANEL_BOT - SB_PANEL_TOP, 18, rgb565(40,54,110), millis());
    else fillRoundRect(0, SB_PANEL_TOP, w, SB_PANEL_BOT - SB_PANEL_TOP, 18, rgb565(28,32,52));
  } else sbDrawTabHandle();
  present(0, SCR_H - 1);
}
// Termina de abrir (ancho actual -> completo, ease-out) y compone el panel con
// los iconos. Bucle acotado a 200 ms, alimentando el watchdog.
static void sbFinishOpenAnim(int startW){
  uint32_t t0 = millis();
  for(;;){
    uint32_t e = millis() - t0; if(e > SB_OPEN_ANIM_MS) e = SB_OPEN_ANIM_MS;
    float p = (float)e / SB_OPEN_ANIM_MS; p = 1 - (1 - p) * (1 - p) * (1 - p);
    int w = startW + (int)((SB_PANEL_W - startW) * p);
    sbRevealFrame(w); esp_task_wdt_reset();
    if(e >= SB_OPEN_ANIM_MS) break;
  }
  sbRenderOverlay();
}
// Retrae (ancho actual -> 0) y restaura el fondo (panel cancelado).
static void sbRestoreBackground();
static void sbRevealCancel(int startW){
  uint32_t t0 = millis();
  for(;;){
    uint32_t e = millis() - t0; if(e > SB_OPEN_ANIM_MS) e = SB_OPEN_ANIM_MS;
    float p = (float)e / SB_OPEN_ANIM_MS; p = 1 - (1 - p) * (1 - p) * (1 - p);
    int w = startW - (int)(startW * p);
    sbRevealFrame(w); esp_task_wdt_reset();
    if(e >= SB_OPEN_ANIM_MS) break;
  }
  sbRestoreBackground();
}

// Split Screen: la app soltada arriba "crece" desde el punto de soltado hasta
// la mitad superior. Se AÑADE como ventana no flotante (no clobbera otras
// ventanas ya abiertas -- el panel es accesible con ventanas) y se abre el
// selector de la mitad inferior.
static void sbSplitOpenAnim(uint8_t appID, int originX, int originY){
  uint16_t* bg = sbBgBuf();
  if(bbuf && bg){
    uint32_t t0 = millis();
    int fx = originX - 28, fy = originY - 28, fw = 56, fh = 56;
    int tx = 0, ty = SB_PANEL_TOP, tw = SCR_W, th = (WM_NAV - SB_PANEL_TOP) / 2;
    for(;;){
      uint32_t e = millis() - t0; if(e > SB_OPEN_ANIM_MS) e = SB_OPEN_ANIM_MS;
      float p = (float)e / SB_OPEN_ANIM_MS; p = 1 - (1 - p) * (1 - p) * (1 - p);
      setBuf(bbuf);
      gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
      memcpy(bbuf, bg, (size_t)SCR_W * SCR_H * 2);
      sbDrawWindowsSorted(); sbDrawMinBubbles();
      int x = fx + (int)((tx - fx) * p), y = fy + (int)((ty - fy) * p);
      int w = fw + (int)((tw - fw) * p), h = fh + (int)((th - fh) * p);
      fillRoundRect(x, y, w, h, (int)(16 * (1 - p)), rgb565(30,34,48));
      present(0, SCR_H - 1); esp_task_wdt_reset();
      if(e >= SB_OPEN_ANIM_MS) break;
    }
  }
  int n = wmCount;
  wmWins[n].appID = appID; wmWins[n].isFloating = false; wmWins[n].used = true; wmWins[n].minimized = false;
  wmWins[n].x = 0; wmWins[n].y = SB_PANEL_TOP; wmWins[n].w = SCR_W; wmWins[n].h = (WM_NAV - SB_PANEL_TOP) / 2;
  wmWins[n].zIndex = ++wmZTop; wmCount++;
  sbSplitBottomPicker = true; wmFocus(n);
  sbRenderOverlay();
  wmRunHostedApp(n, true);
}
static void sbOpenSplitBottom(uint8_t appID){
  if(wmCount >= WM_MAX){ sbSplitBottomPicker = false; sbRenderOverlay(); return; }
  if(APP_REG[appID].flags & APP_NO_WINDOW) return;      // exige pantalla completa: el selector sigue esperando otra
  int y0 = SB_PANEL_TOP + (WM_NAV - SB_PANEL_TOP) / 2, n = wmCount;
  wmWins[n].appID = appID; wmWins[n].isFloating = false; wmWins[n].used = true; wmWins[n].minimized = false;
  wmWins[n].x = 0; wmWins[n].y = y0; wmWins[n].w = SCR_W; wmWins[n].h = WM_NAV - y0;
  wmWins[n].zIndex = ++wmZTop; wmCount++;
  sbSplitBottomPicker = false; wmFocus(n);
  sbRenderOverlay();
  wmRunHostedApp(n, true);
}
// Cancelar el arrastre (zona inferior / app sin ventana): el icono vuelve a su
// slot con resorte (0.2s ease-out). El panel se queda abierto.
static void sbCancelDragAnim(){
  if(sbDragIcon < 0){ sbGesture = GEST_IDLE; return; }
  uint32_t t0 = millis();
  float fx = sbDragX, fy = sbDragY;
  float tx = SB_ICON_X + SB_ICON_S / 2.0f, ty = (float)sbIconY(sbDragIcon) + SB_ICON_S / 2.0f;
  for(;;){
    uint32_t e = millis() - t0; if(e > SB_OPEN_ANIM_MS) e = SB_OPEN_ANIM_MS;
    float p = (float)e / SB_OPEN_ANIM_MS; p = 1 - (1 - p) * (1 - p) * (1 - p);
    sbDragX = fx + (tx - fx) * p; sbDragY = fy + (ty - fy) * p;
    sbRenderOverlay(); esp_task_wdt_reset();
    if(e >= SB_OPEN_ANIM_MS) break;
  }
  sbGesture = GEST_IDLE; sbDragIcon = -1;
  sbRenderOverlay();
}
// Abrir una app como ventana flotante con crecimiento desde el icono (toque
// simple en el panel). Reutiliza sbOpenFloating para la creacion real.
static void sbOpenFloatingAnim(uint8_t appID, int cx, int cy){
  if(wmCount >= WM_MAX) return;
  if(APP_REG[appID].flags & APP_NO_WINDOW) return;
  int tw = 420, th = 560, tx = cx - tw / 2, ty = cy - th / 2;
  if(tx < 0) tx = 0; if(tx + tw > SCR_W) tx = SCR_W - tw;
  if(ty < SB_PANEL_TOP) ty = SB_PANEL_TOP; if(ty + th > WM_NAV) ty = WM_NAV - th;
  uint16_t* bg = sbBgBuf();
  if(bbuf && bg){
    uint32_t t0 = millis();
    for(;;){
      uint32_t e = millis() - t0; if(e > SB_OPEN_ANIM_MS) e = SB_OPEN_ANIM_MS;
      float p = (float)e / SB_OPEN_ANIM_MS; p = 1 - (1 - p) * (1 - p) * (1 - p);
      setBuf(bbuf);
      gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
      memcpy(bbuf, bg, (size_t)SCR_W * SCR_H * 2);
      sbDrawWindowsSorted(); sbDrawMinBubbles();
      int x = cx + (int)((tx - cx) * p), y = cy + (int)((ty - cy) * p);
      int w = 56 + (int)((tw - 56) * p), h = 56 + (int)((th - 56) * p);
      fillRoundRect(x, y, w, h, 12, rgb565(244,246,250));
      present(0, SCR_H - 1); esp_task_wdt_reset();
      if(e >= SB_OPEN_ANIM_MS) break;
    }
  }
  int n = wmCount;
  sbOpenFloating(appID, cx, cy);
  sbRenderOverlay();
  if(wmCount > n) wmRunHostedApp(n, true);
}
// Captura de pantalla: destello blanco de confirmacion (feedback identico al de
// un movil). Sin capa de almacenamiento (SD/SPIFFS) todavia no se guarda a
// disco -- ver hoja de ruta; queda pendiente cuando exista almacenamiento.
static void sbDoScreenshot(){
  uint16_t* bg = sbBgBuf();
  if(!bbuf || !bg) return;
  uint32_t t0 = millis(), dur = 180;
  for(;;){
    uint32_t e = millis() - t0; if(e > dur) e = dur;
    float p = (float)e / dur; uint8_t a = (uint8_t)((1 - p) * 220);
    setBuf(bbuf);
    gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
    memcpy(bbuf, bg, (size_t)SCR_W * SCR_H * 2);
    sbDrawWindowsSorted(); sbDrawMinBubbles();
    if(sbPanelOpen) sbDrawPanel(); else sbDrawTabHandle();
    fillRectA(0, 0, SCR_W, SCR_H, rgb565(255,255,255), a);
    present(0, SCR_H - 1); esp_task_wdt_reset();
    if(e >= dur) break;
  }
  sbRenderOverlay();
}
// Rebote del scroll: si sbScrollY se salio de [0,max], vuelve con resorte.
static void sbScrollSettle(){
  int mx = sbScrollMax();
  int target = (sbScrollY < 0) ? 0 : (sbScrollY > mx ? mx : sbScrollY);
  if(target == sbScrollY){ sbRenderOverlay(); return; }
  int from = sbScrollY; uint32_t t0 = millis(), dur = 160;
  for(;;){
    uint32_t e = millis() - t0; if(e > dur) e = dur;
    float p = (float)e / dur; p = 1 - (1 - p) * (1 - p);
    sbScrollY = from + (int)((target - from) * p);
    sbRenderOverlay(); esp_task_wdt_reset();
    if(e >= dur) break;
  }
  sbScrollY = target; sbRenderOverlay();
}
// Restaura el fondo tras cerrar el panel/cancelar sin dejar residuos. Si aun
// hay overlay activo (ventanas/panel/selectores) recompone; si no, vuelve al
// camino normal del Home o deja la App (su contenido sigue intacto en fb).
static void sbRestoreBackground(){
  if(wmCount > 0 || sbPanelOpen || sbAddPickerOpen || sbSplitBottomPicker){ sbRenderOverlay(); return; }
  if(gState == ST_HOME){ if(gHomeDirty) renderHome(); showHome(); }
  else {
    // ST_APP: fb pudo quedar "sucio" si hubo ventanas flotantes alojadas (su
    // contenido se pinto dentro de fb en el rectangulo de cada ventana). El unico
    // fb LIMPIO del fondo congelado es appSnapBuf (capturado al abrir el panel);
    // se restaura antes de volcar para no arrastrar restos de las ventanas. La
    // App reanuda su tick() a partir del siguiente frame (sbTick devuelve false).
    if(appSnapBuf && fb) memcpy(fb, appSnapBuf, (size_t)SCR_W * SCR_H * 2);
    flxFlushAll();
  }
}

// #############################################################
// ##  Maquina de estados de gestos del Panel Edge. Se llama desde
// ##  homeTick()/appTick() ANTES que el resto, para tener prioridad
// ##  total mientras esta activo. Devuelve true si consumio el toque.
// #############################################################
static bool sbTick(){
  // 0) WATCHDOG (ultimo recurso). Los estados GEST_* son transitorios: solo
  //    valen mientras un dedo toca. Si el controlador no reporto un release
  //    limpio y el gesto se quedo a medias sin T.down, tras 800 ms se fuerza
  //    IDLE. NO toca sbPanelOpen/wmCount (mirar el panel/ventanas sin tocar es
  //    normal). Las vias de salida NORMALES estan en cada rama de release; esto
  //    es solo la red de seguridad, no el mecanismo principal.
  if(sbGesture != GEST_IDLE && !T.down){
    if(sbStuckMs == 0) sbStuckMs = millis();
    else if(millis() - sbStuckMs > 800){ sbGesture = GEST_IDLE; sbDragIcon = -1; sbStuckMs = 0; sbRenderOverlay(); }
  } else sbStuckMs = 0;

  // 1) Selector "añadir apps" abierto: absorbe todo.
  if(sbAddPickerOpen){ sbAddPickerTick(); return true; }

  // 2) Selector de la mitad inferior del Split: un tap elige la app.
  if(sbSplitBottomPicker && T.tap){
    int appID;
    if(sbSplitPickerAt(T.x, T.y, appID)){ sbOpenSplitBottom((uint8_t)appID); return true; }
  }

  // 3) Arrastrando un icono FUERA del panel (crear ventana / split / cancelar).
  if(sbGesture == GEST_DRAGGING){
    if(T.down){
      sbDragX = T.x; sbDragY = T.y;
      unsigned long now = millis(); if(now - sbFrameMs >= SB_FRAME_MS){ sbFrameMs = now; sbRenderOverlay(); }
      return true;
    }
    if(T.released){
      uint8_t appID = sbAppId(sbDragIcon);
      if(APP_REG[appID].flags & APP_NO_WINDOW){ sbCancelDragAnim(); return true; }   // exige pantalla completa: rebota
      if(T.y < SB_DROP_TOP){                                                          // Split Screen
        if(wmCount >= WM_MAX){ sbCancelDragAnim(); return true; }                     // sin hueco: rebota
        sbSplitOpenAnim(appID, (int)sbDragX, (int)sbDragY);
        sbDragIcon = -1; sbGesture = GEST_IDLE; sbPanelOpen = false; return true;
      }
      if(T.y > SB_DROP_BOT){ sbCancelDragAnim(); return true; }                       // zona inferior: cancelar
      if(wmCount >= WM_MAX){ sbCancelDragAnim(); return true; }                       // Flotante sin hueco: rebota
      int n = wmCount;                                                                // Ventana Flotante en el punto
      sbGesture = GEST_IDLE; sbDragIcon = -1; sbPanelOpen = false;
      sbOpenFloating(appID, T.x, T.y); sbRenderOverlay();
      if(wmCount > n) wmRunHostedApp(n, true);
      return true;
    }
    return true;
  }

  // 4) Arrastrando el TIRADOR para abrir (reveal progresivo siguiendo al dedo).
  if(sbGesture == GEST_HANDLE){
    if(T.down){
      int rv = T.x; if(rv < 0) rv = 0; if(rv > SB_PANEL_W) rv = SB_PANEL_W;
      sbHandleReveal = rv;
      unsigned long now = millis(); if(now - sbFrameMs >= SB_FRAME_MS){ sbFrameMs = now; sbRevealFrame(rv); }
      return true;
    }
    if(T.released){
      int dur = (int)(millis() - sbPressMs), adx = abs(T.x - sbPressX), ady = abs(T.y - sbPressY);
      bool tap = (dur < 300 && adx < 16 && ady < 16);                                 // toque simple = abrir
      sbGesture = GEST_IDLE;
      if(sbHandleReveal >= (SB_PANEL_W * 45 / 100) || tap){ sbOpenPanel(); sbFinishOpenAnim(sbHandleReveal); }
      else sbRevealCancel(sbHandleReveal);
      return true;
    }
    return true;
  }

  // 5) PANEL ABIERTO: botones, iconos (tap=abrir / long-press=arrastrar),
  //    scroll con rebote, reorden en edicion, o toque fuera = cerrar.
  if(sbPanelOpen){
    if(T.tap){   // los botones fijos resetean el gesto al consumir el tap (sin GEST_PRESSED colgado)
      if(sbHitScreenshot(T.x, T.y)){ sbGesture = GEST_IDLE; sbDragIcon = -1; sbDoScreenshot(); return true; }
      if(sbHitGridBtn(T.x, T.y)){ sbGesture = GEST_IDLE; sbDragIcon = -1; sbAddPickerOpen = true; sbRenderOverlay(); return true; }
      if(sbHitPencilBtn(T.x, T.y)){ sbGesture = GEST_IDLE; sbDragIcon = -1; sbEditMode = !sbEditMode; sbRenderOverlay(); return true; }
      if(sbEditMode){ int slot; if(sbHitRemoveBadge(T.x, T.y, slot)){ sbGesture = GEST_IDLE; sbDragIcon = -1; sbPinRemove(slot); if(sbScrollY > sbScrollMax()) sbScrollY = sbScrollMax(); sbRenderOverlay(); return true; } }
    }
    if(T.pressed){
      int which;
      if(sbIconAt(T.x, T.y, which)){                    // sobre un icono: espera tap o long-press
        sbGesture = GEST_PRESSED; sbDragIcon = which;
        sbPressMs = millis(); sbPressX = T.x; sbPressY = T.y; sbScrollLastY = T.y;
        return true;
      }
      if(T.x <= SB_PANEL_W){                            // dentro del panel, fuera de iconos: scroll o long-press->edicion
        sbGesture = GEST_PRESSED; sbDragIcon = -1;
        sbPressMs = millis(); sbPressX = T.x; sbPressY = T.y; sbScrollLastY = T.y;
        return true;
      }
      sbClosePanel(); sbRestoreBackground(); return true;   // fuera del panel: cerrar
    }
    if(T.down && sbGesture == GEST_PRESSED){
      int adx = abs(T.x - sbPressX), ady = abs(T.y - sbPressY);
      if(sbDragIcon >= 0){
        if(adx < 12 && ady < 12 && (millis() - sbPressMs) > SB_LONGPRESS){
          if(sbEditMode){ sbGesture = GEST_REORDER; sbDragX = T.x; sbDragY = T.y; }
          else { sbGesture = GEST_DRAGGING; sbDragX = T.x; sbDragY = T.y; }
          sbRenderOverlay(); return true;
        }
        if(ady >= 12 && ady >= adx){ sbGesture = GEST_SCROLLING; sbScrollLastY = T.y; return true; }
        return true;
      } else {
        if(adx < 12 && ady < 12 && (millis() - sbPressMs) > SB_LONGPRESS){ sbEditMode = !sbEditMode; sbGesture = GEST_IDLE; sbRenderOverlay(); return true; }
        if(ady >= 12 && ady >= adx){ sbGesture = GEST_SCROLLING; sbScrollLastY = T.y; return true; }
        return true;
      }
    }
    if(T.down && sbGesture == GEST_SCROLLING){
      int dy = T.y - sbScrollLastY; sbScrollLastY = T.y;
      int mx = sbScrollMax();
      if(sbScrollY < 0 || sbScrollY > mx) sbScrollY -= dy / 2;   // zona de rebote: resistencia (mitad)
      else sbScrollY -= dy;
      if(sbScrollY < -SB_OVERSCROLL) sbScrollY = -SB_OVERSCROLL;
      if(sbScrollY > mx + SB_OVERSCROLL) sbScrollY = mx + SB_OVERSCROLL;
      unsigned long now = millis(); if(now - sbFrameMs >= SB_FRAME_MS){ sbFrameMs = now; sbRenderOverlay(); }
      return true;
    }
    if(T.down && sbGesture == GEST_REORDER){
      sbDragX = T.x; sbDragY = T.y;
      int over; if(sbSlotAtY((int)sbDragY, over) && over != sbDragIcon){ sbPinMove(sbDragIcon, over); sbDragIcon = over; }
      unsigned long now = millis(); if(now - sbFrameMs >= SB_FRAME_MS){ sbFrameMs = now; sbRenderOverlay(); }
      return true;
    }
    if(T.released){
      if(sbGesture == GEST_PRESSED && sbDragIcon >= 0){          // TAP sobre icono -> abrir ventana flotante
        int adx = abs(T.x - sbPressX), ady = abs(T.y - sbPressY);
        if(adx < 16 && ady < 16 && (millis() - sbPressMs) < 550){
          uint8_t appID = sbAppId(sbDragIcon);
          int iconCx = SB_ICON_X + SB_ICON_S / 2, iconCy = sbIconY(sbDragIcon) + SB_ICON_S / 2;
          sbGesture = GEST_IDLE; sbDragIcon = -1;
          if(APP_REG[appID].flags & APP_NO_WINDOW){ sbLaunchFull(appID); return true; }   // pantalla completa
          if(wmCount >= WM_MAX){ return true; }                                            // sin hueco: ignora, panel abierto
          sbClosePanel();
          sbOpenFloatingAnim(appID, iconCx, iconCy);
          return true;
        }
      }
      if(sbGesture == GEST_REORDER) sbPinnedSave();      // persiste el nuevo orden UNA vez, al soltar
      sbGesture = GEST_IDLE; sbDragIcon = -1;
      if(sbScrollY < 0 || sbScrollY > sbScrollMax()) sbScrollSettle();   // rebote de vuelta
      else sbRenderOverlay();
      return true;
    }
    return true;                                        // panel abierto: absorbe cualquier otro toque
  }

  // 6) PANEL CERRADO: el tirador abre (tap o arrastre). Dentro de una App se
  //    captura fb ANTES (unica copia del contenido; a partir de aqui su tick
  //    queda en pausa mientras sbTick consuma el toque).
  if(T.pressed && sbHitTab(T.startX, T.startY)){
    if(gState == ST_APP && appSnapBuf && fb) memcpy(appSnapBuf, fb, (size_t)SCR_W * SCR_H * 2);
    sbGesture = GEST_HANDLE; sbHandleReveal = 0;
    sbPressMs = millis(); sbPressX = T.x; sbPressY = T.y;
    return true;
  }

  // 7) Con ventanas abiertas (y sin tocar el tirador/panel): sus toques
  //    (burbujas/controles/mover/redimensionar/cerrar) + tick de las alojadas.
  if(wmCount > 0){
    if(wmTouchWindows()){ sbRenderOverlay(); wmFlushPendingContent(); }
    int before = wmCount;
    wmTickHostedApps();
    if(wmCount != before) sbRenderOverlay();
    if(wmCount == 0){ sbSplitBottomPicker = false; sbRestoreBackground(); }
    return true;
  }

  return false;
}

// #############################################################
// ##  SEGURIDAD -> BLOQUEO (PIN / Contraseña)
// ##  Todo compone en bbuf y presenta de una vez (anti-flicker).
// #############################################################
enum { LSU_SEL = 0, LSU_PIN, LSU_PASS };
static int lsuMode = LSU_SEL;
static char lsuPin[12] = "", lsuPass[64] = "";
static int lsuPress = -1; static uint32_t lsuPressMs = 0;
static uint32_t lsuKbAnim = 0;                       // millis al abrir el teclado (para el slide de 0.3s)
static uint32_t lsuAnimMs = 0;
static const char* PIN_KEYS[12] = { "1","2","3","4","5","6","7","8","9","<","0","OK" };
static bool lsuVerify = false;                       // true = desbloquear (verificar), false = crear
static char lsuSaved[64] = "";                       // clave guardada a comparar
static uint32_t lsuWrong = 0;                        // millis del ultimo error (para el flash rojo)
static void lsuBg(){
  if(lsuVerify && blurBg) memcpy(gBuf, blurBg, (size_t)SCR_W * SCR_H * 2);   // wallpaper borroso
  else fillRect(0, 0, SCR_W, SCR_H, rgb565(12,14,22));
}

static int  utf8Count(const char* s){ int n = 0; while(*s){ if((*s & 0xC0) != 0x80) n++; s++; } return n; }
static void lsuPassAppend(const char* s){ int L = strlen(lsuPass), sl = strlen(s); if(L + sl < (int)sizeof(lsuPass) - 1){ memcpy(lsuPass + L, s, sl); lsuPass[L + sl] = 0; } }
static void lsuExit(){
  if(lsuVerify){ lsuVerify = false; gState = ST_LOCK; lockOff = 0; lastLockOff = -1; renderLock(); showLock(); }
  else { gState = ST_APP; settingsRender(); }
}
static void lsuUnlock(){
  lsuVerify = false; lsuWrong = 0; lockOff = 0; lastLockOff = -1;
  gState = ST_HOME;
  renderHome();                          // compone el home en homeBuf
  uint32_t t0 = millis(), dur = 400;     // 0.4s: aparecer desvanecido + leve temblor
  for(;;){
    uint32_t e = millis() - t0; if(e > dur) e = dur;
    float p = (float)e / dur;
    uint8_t a = (uint8_t)(p * 255);
    int sh = (int)((1.0f - p) * 6.0f * sinf(e * 0.05f));   // temblor que decae
    for(int j = 0; j < SCR_H; j++){
      uint16_t* d  = bbuf + (size_t)j * SCR_W;
      uint16_t* bg = (blurBg ? blurBg : homeBuf) + (size_t)j * SCR_W;
      uint16_t* hm = homeBuf + (size_t)j * SCR_W;
      for(int i = 0; i < SCR_W; i++){
        int si = i - sh; if(si < 0) si = 0; if(si >= SCR_W) si = SCR_W - 1;
        d[i] = mix565(bg[i], hm[si], a);
      }
    }
    present(0, SCR_H - 1);
    if(e >= dur) break;
  }
  showHome();
}
static void lsuSavePin(){ prefs.begin("flexos", false); prefs.putString("lockpin", lsuPin); prefs.putInt("locktype", 1); prefs.end(); gLockType = 1; lsuExit(); }
static void lsuSavePass(){ prefs.begin("flexos", false); prefs.putString("lockpass", lsuPass); prefs.putInt("locktype", 2); prefs.end(); gLockType = 2; lsuExit(); }
static void lsuBack(){ strokeSegAA(30, 26, 18, 18, 2.4f, rgb565(255,255,255)); strokeSegAA(18, 18, 30, 10, 2.4f, rgb565(255,255,255)); }

// ---- Selector PIN / Contraseña ----
static void lsuRenderSel(){
  setBuf(bbuf);
  fillRect(0, 0, SCR_W, SCR_H, rgb565(12,14,22));
  lsuBack();
  drawTextC(SCR_W / 2, 74, "Bloqueo de pantalla", 3, rgb565(255,255,255));
  drawTextC(SCR_W / 2, 118, "Elige un metodo", 2, rgb565(150,158,180));
  int bw = SCR_W - 80, bh = 120, y1 = 220, y2 = y1 + bh + 30; uint32_t t = millis();
  const char* lbl[2] = { "PIN", "Contrase\xC3\xB1" "a" }; int ys[2] = { y1, y2 };
  for(int k = 0; k < 2; k++){
    if(uiGlass){ glDrawSpec = false; drawLiquidGlassPanel(40, ys[k], bw, bh, 22, rgb565(50,90,200), t); glDrawSpec = true; glassSheen(40, ys[k], bw, bh, 22, t); }
    else fillRoundRect(40, ys[k], bw, bh, 22, rgb565(46,82,182));
    drawTextC(SCR_W / 2, ys[k] + bh / 2 - 18, lbl[k], 4, rgb565(255,255,255));
  }
  present(0, SCR_H - 1);
}

// ---- Pantalla PIN (teclado numerico Liquid Glass + feedback de tecleo) ----
static void lsuPinRect(int i, int &x, int &y, int &w, int &h){
  int c = i % 3, r = i / 3, bw = 132, bh = 82, gap = 12;
  int tot = 3 * bw + 2 * gap, x0 = (SCR_W - tot) / 2, y0 = 300;
  x = x0 + c * (bw + gap); y = y0 + r * (bh + gap); w = bw; h = bh;
}
static void lsuComposePin(){                          // base de vidrio en lockBuf (SOLO una vez)
  setBuf(lockBuf);
  lsuBg();
  lsuBack();
  drawTextC(SCR_W / 2, 60, lsuVerify ? "Introduce el PIN" : "Crear PIN", lsuVerify ? 3 : 4, rgb565(255,255,255));
  for(int i = 0; i < 12; i++){
    int x, y, w, h; lsuPinRect(i, x, y, w, h);
    if(uiGlass){ glDrawSpec = false; drawLiquidGlassPanel(x, y, w, h, 16, rgb565(48,60,110), 0); glDrawSpec = true; }
    else fillRoundRect(x, y, w, h, 16, rgb565(44,54,92));
    uint16_t col = (i == 9) ? rgb565(230,180,90) : (i == 11) ? rgb565(120,220,150) : rgb565(255,255,255);
    drawTextC(x + w / 2, y + h / 2 - 12, PIN_KEYS[i], 3, col);
  }
  setBuf(bbuf);
}
static void lsuShowPin(){ lsuComposePin(); memcpy(bbuf, lockBuf, (size_t)SCR_W * SCR_H * 2); present(0, SCR_H - 1); }
static void lsuAnimPin(){                              // puntos dinamicos + destello + flash (NO re-desenfoca)
  setBuf(bbuf);
  for(int j = 120; j < 700; j++) memcpy(bbuf + (size_t)j * SCR_W, lockBuf + (size_t)j * SCR_W, SCR_W * 2);
  int n = strlen(lsuPin);
  uint16_t dc = (lsuWrong && millis() - lsuWrong < 500) ? rgb565(235,70,70) : rgb565(90,150,240);
  for(int i = 0; i < 8; i++){ int cx = SCR_W / 2 - 4 * 28 + 14 + i * 28; if(i < n) fillCircle(cx, 150, 8, dc); else drawCircle(cx, 150, 8, rgb565(90,100,130)); }
  uint32_t t = millis();
  for(int i = 0; i < 12; i++){
    int x, y, w, h; lsuPinRect(i, x, y, w, h);
    if(uiGlass) glassSheen(x, y, w, h, 16, t);
    if(i == lsuPress){ float p = (millis() - lsuPressMs) / 200.0f; if(p < 1) fillRoundRectA(x, y, w, h, 16, rgb565(255,255,255), (uint8_t)((1 - p) * 90)); else lsuPress = -1; }
  }
  present(120, 700);
}

// ---- Teclado alfanumerico para contraseña (con offset para el slide) ----
static void lsuDrawKb(int yoff){
  int ky = KB_Y + yoff;
  if(uiGlass){ glDrawSpec = false; drawLiquidGlassPanel(0, ky - 4, SCR_W, SCR_H - (ky - 4), 0, rgb565(36,40,58), millis()); glDrawSpec = true; }
  else fillRect(0, ky - 4, SCR_W, SCR_H - (ky - 4), rgb565(18,20,28));
  for(int r = 0; r < KB_ROWS; r++) for(int c = 0; c < KB_COLS; c++){
    int x = KB_X + c * (KB_KW + KB_GAP), y = ky + r * (KB_KH + KB_GAP);
    fillRoundRect(x, y, KB_KW, KB_KH, 6, rgb565(52,56,70));
    const char* k = mapaActivo[r][c];
    if(kbShift && k[1] == 0 && k[0] >= 'a' && k[0] <= 'z'){ char u[2] = { (char)(k[0] - 32), 0 }; drawTextC(x + KB_KW / 2, y + KB_KH / 2 - 8, u, 2, rgb565(240,242,248)); }
    else drawTextC(x + KB_KW / 2, y + KB_KH / 2 - 8, k, 2, rgb565(240,242,248));
  }
  int fy = ky + 3 * (KB_KH + KB_GAP);
  kbFKey(6, fy, 58, "shift", kbShift);
  kbFKey(68, fy, 54, (mapaActivo == LAYOUT_NUM) ? "emoji" : (mapaActivo == LAYOUT_EMOJI) ? "ABC" : "?123", false);
  kbFKey(126, fy, 48, kbLangEs ? "ES" : "EN", false);
  kbFKey(178, fy, 190, "espacio", false);
  kbFKey(372, fy, 44, "<-", false);
  kbFKey(420, fy, 52, "OK", false);
}
static void lsuRenderPass(int yoff){
  setBuf(bbuf);
  lsuBg();
  lsuBack();
  drawTextC(SCR_W / 2, 50, lsuVerify ? "Introduce contrase\xC3\xB1" "a" : "Crear contrase\xC3\xB1" "a", 3, rgb565(255,255,255));
  int cnt = utf8Count(lsuPass);
  for(int i = 0; i < cnt && i < 18; i++) fillCircle(30 + i * 24, 120, 7, rgb565(90,150,240));
  lsuDrawKb(yoff);
  present(0, SCR_H - 1);
}

static void lsuEnter(){
  gState = ST_LOCKSETUP; lsuMode = LSU_SEL; lsuPin[0] = 0; lsuPass[0] = 0; lsuPress = -1; lsuKbAnim = 0;
  mapaActivo = LAYOUT_ES; kbLangEs = true; kbShift = false;
  lsuRenderSel();
}
static void lsuTick(){
  if(lsuMode == LSU_SEL){
    lsuRenderSel();                                    // destello continuo
    if(T.tap){
      if(T.x < 48 && T.y < 48){ lsuExit(); return; }
      int bw = SCR_W - 80, y1 = 220, bh = 120, y2 = y1 + bh + 30;
      if(T.x >= 40 && T.x <= 40 + bw && T.y >= y1 && T.y <= y1 + bh){ lsuMode = LSU_PIN; lsuShowPin(); return; }
      if(T.x >= 40 && T.x <= 40 + bw && T.y >= y2 && T.y <= y2 + bh){ lsuMode = LSU_PASS; lsuKbAnim = millis(); return; }
    }
    return;
  }
  if(lsuMode == LSU_PIN){
    if(T.tap){
      if(T.x < 48 && T.y < 48){ lsuExit(); return; }
      for(int i = 0; i < 12; i++){ int x, y, w, h; lsuPinRect(i, x, y, w, h);
        if(T.x >= x && T.x <= x + w && T.y >= y && T.y <= y + h){
          lsuPress = i; lsuPressMs = millis();
          if(i == 9){ int L = strlen(lsuPin); if(L > 0) lsuPin[L - 1] = 0; }                         // borrar
          else if(i == 11){                                                                          // OK
            if(lsuVerify){ if(!strcmp(lsuPin, lsuSaved)) lsuUnlock(); else { lsuWrong = millis(); lsuPin[0] = 0; } return; }
            else if(strlen(lsuPin) >= 4){ lsuSavePin(); return; }
          }
          else if(strlen(lsuPin) < 8){                                                               // digito
            int L = strlen(lsuPin); lsuPin[L] = PIN_KEYS[i][0]; lsuPin[L + 1] = 0;
            if(lsuVerify && (int)strlen(lsuPin) == (int)strlen(lsuSaved) && strlen(lsuSaved) > 0){
              if(!strcmp(lsuPin, lsuSaved)) lsuUnlock(); else { lsuWrong = millis(); lsuPin[0] = 0; }
              return;
            }
          }
          break;
        }
      }
    }
    if(millis() - lsuAnimMs > 30){ lsuAnimMs = millis(); lsuAnimPin(); }        // anim con throttle (responsivo)
    return;
  }
  // LSU_PASS
  if(lsuKbAnim){                                        // animacion de apertura del teclado = 0.3s EXACTOS
    float p = (millis() - lsuKbAnim) / 300.0f; if(p >= 1){ p = 1; lsuKbAnim = 0; }
    int kbh = SCR_H - KB_Y;
    lsuRenderPass((int)((1.0f - p) * kbh));
    return;
  }
  if(T.tap){
    if(T.x < 48 && T.y < 48){ lsuExit(); return; }
    int fy = KB_Y + 3 * (KB_KH + KB_GAP);
    if(T.y >= fy && T.y <= fy + KB_KH){
      if(T.x < 64) kbShift = !kbShift;
      else if(T.x < 122) mapaActivo = (mapaActivo == LAYOUT_NUM) ? LAYOUT_EMOJI : (mapaActivo == LAYOUT_EMOJI) ? (kbLangEs ? LAYOUT_ES : LAYOUT_EN) : LAYOUT_NUM;
      else if(T.x < 174){ kbLangEs = !kbLangEs; if(mapaActivo == LAYOUT_ES || mapaActivo == LAYOUT_EN) mapaActivo = kbLangEs ? LAYOUT_ES : LAYOUT_EN; }
      else if(T.x < 368) lsuPassAppend(" ");
      else if(T.x < 416){ int L = strlen(lsuPass); if(L > 0){ int q = L - 1; while(q > 0 && (lsuPass[q] & 0xC0) == 0x80) q--; lsuPass[q] = 0; } }
      else { if(lsuVerify){ if(!strcmp(lsuPass, lsuSaved)) lsuUnlock(); else { lsuWrong = millis(); lsuPass[0] = 0; lsuRenderPass(0); } return; } else if(strlen(lsuPass) >= 4){ lsuSavePass(); return; } }
      lsuRenderPass(0); return;
    }
    int cell = kbCellAt(T.x, T.y);
    if(cell >= 0){
      const char* k = mapaActivo[cell / KB_COLS][cell % KB_COLS];
      if(kbShift && k[1] == 0 && k[0] >= 'a' && k[0] <= 'z'){ char u[2] = { (char)(k[0] - 32), 0 }; lsuPassAppend(u); kbShift = false; }
      else lsuPassAppend(k);
      lsuRenderPass(0);
    }
  }
}

static void lsuStartVerify(){
  prefs.begin("flexos", true);
  String s = (gLockType == 1) ? prefs.getString("lockpin", "") : prefs.getString("lockpass", "");
  prefs.end();
  s.toCharArray(lsuSaved, sizeof(lsuSaved));
  lsuVerify = true; lsuWrong = 0; lsuPin[0] = 0; lsuPass[0] = 0; lsuPress = -1;
  mapaActivo = LAYOUT_ES; kbLangEs = true; kbShift = false;
  ensureBlurBg();
  gState = ST_LOCKSETUP;
  if(gLockType == 1){ lsuMode = LSU_PIN; lsuShowPin(); }
  else { lsuMode = LSU_PASS; lsuKbAnim = millis(); }
}

// #############################################################
// ##  setup / loop
// #############################################################
// -------------------------------------------------------------
//  Radio (WiFi via co-procesador ESP32-C6 / esp-hosted por SDIO)
//  EL ARRANQUE YA NUNCA TOCA LA RADIO. Antes, bootInitRadioSafe()
//  lanzaba un intento de conexion automatico en cada boot (con SSID/
//  PASS fijos en el codigo) en cuanto FLEXOS_ENABLE_WIFI valia 1. Eso
//  es lo que producia el bucle de "PANIC (crash)": si el enlace SDIO
//  con el C6 fallaba (placa sin C6 operativo en esos pines, firmware
//  slave desactualizado, etc.), fallaba EN CADA arranque, de forma
//  determinista, antes de que pudieras hacer nada.
//
//  Ahora la radio es 100% bajo demanda: no se toca ni una sola vez
//  durante setup()/loop() salvo que el usuario entre a Ajustes -> Red
//  e Internet -> Wi-Fi y pulse "Buscar redes". Si el C6 falla ahi, el
//  fallo se ve como un error EN PANTALLA dentro de esa app (o, en el
//  peor caso, como un reinicio aislado y reproducible en ese momento
//  exacto), nunca como un cuelgue silencioso del arranque completo.
//
//  1) El enlace P4<->C6 de referencia es SDIO (CLK/CMD/D0-D3 + reset
//     del esclavo), NO SPI, y en arduino-esp32 3.2.0 esos pines los
//     fija el "variant" de la placa en tiempo de COMPILACION.
//  2) WiFi.begin()/WiFi.scanNetworks() disparan por debajo el
//     transporte hosted (esp_wifi_remote); nunca se llama a
//     esp_hosted_init() a mano.
//  3) El C6 necesita firmware "slave" de esp-hosted flasheado aparte;
//     si por Serial ves version v0.0.0, no hay enlace real posible.
//
//  FLEXOS_ENABLE_WIFI y gNetOnline estan declarados arriba del todo
//  del archivo (junto a los PINES) porque Ajustes los necesita antes
//  de llegar aqui. Si tu placa NO tiene el C6 operativo, pon
//  FLEXOS_ENABLE_WIFI a 0: la pantalla de Wi-Fi lo respeta y deja de
//  ofrecer "Buscar redes" sin que toques nada mas.
// -------------------------------------------------------------
#define FLEXOS_WIFI_TIMEOUT_MS 15000

static void bootInitRadioSafe(){
  Serial.println(F("[C6] radio en modo bajo demanda -> se activa solo desde Ajustes > Red e Internet > Wi-Fi"));
}

// ---- BLE opcional (tambien via C6, requiere firmware slave con BT) ----
// NOTA: el P4 NO tiene controlador Bluetooth propio, asi que aqui NO se
// llama a esp_bt_controller_mem_release(): esa API es para liberar RAM
// de Bluetooth Clasico en un chip con radio LOCAL (S3, C3...), y el P4
// no tiene radio local ni Bluetooth Clasico que liberar. El C6 ademas
// solo ofrece BLE (no Classic). Descomenta esto SOLO despues de
// confirmar que WiFi ya enlaza por el mismo C6.
#if 0
#include <NimBLEDevice.h>
static void bootInitBleSafe(){
  NimBLEDevice::init("FlexOS");
  // ... tu logica de advertising / GATT aqui
}
#endif

// #############################################################
// ##  AJUSTES -> RED E INTERNET -> WI-FI
// ##  Escaneo y conexion corren en su propia tarea del Core 1
// ##  (igual patron que arriba): loop() nunca se bloquea, y un
// ##  fallo del C6 se queda contenido en esta pantalla.
// #############################################################
#define WIFI_MAX_NETS 16
struct WifiNet { char ssid[33]; int8_t rssi; bool secure; };
static WifiNet      wifiNets[WIFI_MAX_NETS];
static volatile int wifiNetCount = 0;
static portMUX_TYPE wifiMux = portMUX_INITIALIZER_UNLOCKED;

enum { WUI_LIST = 0, WUI_SCANNING, WUI_PASS, WUI_CONNECTING, WUI_OK, WUI_FAIL };
static volatile int wifiUIState = WUI_LIST;
static int      wifiSel = -1;
static char     wifiPass[64] = "";
static uint32_t wifiKbAnim = 0;
static char     wifiConnSSID[33] = "";
static char     wifiConnPass[64] = "";
static char     wifiConnIP[24]   = "";

#if FLEXOS_ENABLE_WIFI
static void wifiScanTask(void*){
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();                // bloqueante, pero en su PROPIA tarea: loop() sigue vivo
  // ANTI-CRASH: construir la lista FUERA de toda seccion critica. La version
  // anterior copiaba dentro de portENTER_CRITICAL(&wifiMux), pero WiFi.SSID()
  // devuelve un String (malloc) y ademas toca el driver WiFi. Hacer malloc con
  // las interrupciones deshabilitadas y un spinlock tomado puede (a) bloquear
  // el lock interno del heap -> deadlock, o (b) mantener las IRQ apagadas
  // demasiado tiempo -> disparar el watchdog de interrupciones (INT_WDT) y
  // reiniciar el ESP32. wifiNets[] SOLO lo escribe esta tarea; la UI lee unica-
  // mente indices [0, wifiNetCount). Por eso basta con publicar wifiNetCount AL
  // FINAL, bajo una seccion critica minima (barrera de memoria): la UI nunca ve
  // una entrada a medio escribir y no hay ninguna asignacion bajo el spinlock.
  int cnt = 0;
  if(n > 0){
    for(int i = 0; i < n && cnt < WIFI_MAX_NETS; i++){
      String ss = WiFi.SSID(i);
      if(ss.length() == 0) continue;                                // oculta redes sin nombre
      bool dup = false;
      for(int k = 0; k < cnt; k++) if(!strcmp(wifiNets[k].ssid, ss.c_str())){ dup = true; break; }
      if(dup) continue;                                             // mismo SSID visto en varios canales
      ss.toCharArray(wifiNets[cnt].ssid, sizeof(wifiNets[cnt].ssid));
      wifiNets[cnt].rssi   = (int8_t)WiFi.RSSI(i);
      wifiNets[cnt].secure = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
      cnt++;
    }
  }
  portENTER_CRITICAL(&wifiMux); wifiNetCount = cnt; portEXIT_CRITICAL(&wifiMux);  // publicacion atomica del contador
  WiFi.scanDelete();
  wifiUIState = WUI_LIST;                     // n<=0 -> lista vacia (mensaje "sin redes"), no es un error fatal
  vTaskDelete(NULL);
}
static void wifiConnTask(void*){
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiConnSSID, wifiConnPass);
  uint32_t t0 = millis();
  while(WiFi.status() != WL_CONNECTED && millis() - t0 < FLEXOS_WIFI_TIMEOUT_MS){
    vTaskDelay(pdMS_TO_TICKS(200));           // cede CPU -> no molesta a nadie, no dispara TWDT
  }
  if(WiFi.status() == WL_CONNECTED){
    gNetOnline = true;
    IPAddress ip = WiFi.localIP();
    String ips = ip.toString();
    ips.toCharArray(wifiConnIP, sizeof(wifiConnIP));
    wifiUIState = WUI_OK;
  } else {
    gNetOnline = false;
    WiFi.disconnect(true, true);              // libera el intento fallido, no deja el C6 a medias
    wifiUIState = WUI_FAIL;
  }
  vTaskDelete(NULL);
}
#endif

static void wifiStartScan(){
#if FLEXOS_ENABLE_WIFI
  wifiUIState = WUI_SCANNING;
  portENTER_CRITICAL(&wifiMux); wifiNetCount = 0; portEXIT_CRITICAL(&wifiMux);
  xTaskCreatePinnedToCore(wifiScanTask, "wifiScan", 8192, NULL, 1, NULL, 1);   // 8KB: scanNetworks() + String necesitan mas que 6KB (evita stack overflow)
#endif
}
static void wifiStartConnect(){
#if FLEXOS_ENABLE_WIFI
  if(wifiSel < 0 || wifiSel >= WIFI_MAX_NETS){ wifiUIState = WUI_LIST; return; }  // indice invalido -> nunca leer wifiNets[] fuera de rango
  strncpy(wifiConnSSID, wifiNets[wifiSel].ssid, sizeof(wifiConnSSID) - 1); wifiConnSSID[sizeof(wifiConnSSID) - 1] = 0;
  strncpy(wifiConnPass, wifiPass, sizeof(wifiConnPass) - 1); wifiConnPass[sizeof(wifiConnPass) - 1] = 0;
  wifiUIState = WUI_CONNECTING;
  xTaskCreatePinnedToCore(wifiConnTask, "wifiConn", 8192, NULL, 1, NULL, 1);   // 8KB: WiFi.begin() + pila del driver necesitan mas que 6KB
#endif
}
static void wifiPassAppend(const char* s){ int L = strlen(wifiPass), sl = strlen(s); if(L + sl < (int)sizeof(wifiPass) - 1){ memcpy(wifiPass + L, s, sl); wifiPass[L + sl] = 0; } }
static void wifiPassBackspace(){ int L = strlen(wifiPass); if(L > 0){ int q = L - 1; while(q > 0 && (wifiPass[q] & 0xC0) == 0x80) q--; wifiPass[q] = 0; } }
static void wifiBack(){ strokeSegAA(30, 26, 18, 18, 2.4f, rgb565(255,255,255)); strokeSegAA(18, 18, 30, 10, 2.4f, rgb565(255,255,255)); }

static int wifiRowY(int i){ return 150 + i * 66; }
static void wifiRenderList(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, rgb565(14,16,24));
  wifiBack();
  drawTextC(SCR_W / 2, 60, "Wi-Fi", 4, rgb565(255,255,255));
  int cnt; portENTER_CRITICAL(&wifiMux); cnt = wifiNetCount; portEXIT_CRITICAL(&wifiMux);
  if(wifiUIState == WUI_SCANNING){
    drawTextC(SCR_W / 2, 300, "Buscando redes...", 2, rgb565(160,170,196));
  } else if(cnt == 0){
    drawTextC(SCR_W / 2, 300, "No se encontraron redes", 2, rgb565(160,170,196));
  } else {
    for(int i = 0; i < cnt; i++){
      int y = wifiRowY(i); if(y > SCR_H - 60) break;
      fillRoundRect(24, y, SCR_W - 48, 56, 14, rgb565(30,34,48));
      drawWifi(56, y + 28, 13, rgb565(255,255,255));
      drawTextClip(88, y + 12, wifiNets[i].ssid, 2, rgb565(240,242,248), SCR_W - 100);
      if(wifiNets[i].secure){
        fillRoundRect(SCR_W - 80, y + 20, 16, 14, 3, rgb565(180,186,204));
        arcStroke(SCR_W - 72, y + 20, 6, 180, 360, 2, rgb565(180,186,204));
      }
    }
  }
  int by = SCR_H - 90;                        // boton "Buscar redes" (reescanear)
  fillRoundRect(SCR_W / 2 - 110, by, 220, 56, 16, rgb565(60,110,235));
  drawTextC(SCR_W / 2, by + 18, wifiUIState == WUI_SCANNING ? "Buscando..." : "Buscar redes", 2, rgb565(255,255,255));
  flxFlushAll();
}
static void wifiRenderPass(int yoff){
  setBuf(bbuf);
  fillRect(0, 0, SCR_W, SCR_H, rgb565(14,16,24));
  wifiBack();
  char title[48]; snprintf(title, sizeof(title), "Contrase\xC3\xB1" "a de %s", wifiNets[wifiSel].ssid);
  drawTextC(SCR_W / 2, 50, title, 2, rgb565(255,255,255));
  int cnt = utf8Count(wifiPass);
  for(int i = 0; i < cnt && i < 18; i++) fillCircle(30 + i * 24, 120, 7, rgb565(90,150,240));
  int ky = KB_Y + yoff;
  if(uiGlass){ glDrawSpec = false; drawLiquidGlassPanel(0, ky - 4, SCR_W, SCR_H - (ky - 4), 0, rgb565(36,40,58), millis()); glDrawSpec = true; }
  else fillRect(0, ky - 4, SCR_W, SCR_H - (ky - 4), rgb565(18,20,28));
  for(int r = 0; r < KB_ROWS; r++) for(int c = 0; c < KB_COLS; c++){
    int x = KB_X + c * (KB_KW + KB_GAP), y = ky + r * (KB_KH + KB_GAP);
    fillRoundRect(x, y, KB_KW, KB_KH, 6, rgb565(52,56,70));
    const char* k = mapaActivo[r][c];
    if(kbShift && k[1] == 0 && k[0] >= 'a' && k[0] <= 'z'){ char u[2] = { (char)(k[0] - 32), 0 }; drawTextC(x + KB_KW / 2, y + KB_KH / 2 - 8, u, 2, rgb565(240,242,248)); }
    else drawTextC(x + KB_KW / 2, y + KB_KH / 2 - 8, k, 2, rgb565(240,242,248));
  }
  int fy = ky + 3 * (KB_KH + KB_GAP);
  kbFKey(6, fy, 58, "shift", kbShift);
  kbFKey(68, fy, 54, (mapaActivo == LAYOUT_NUM) ? "emoji" : (mapaActivo == LAYOUT_EMOJI) ? "ABC" : "?123", false);
  kbFKey(126, fy, 48, kbLangEs ? "ES" : "EN", false);
  kbFKey(178, fy, 190, "espacio", false);
  kbFKey(372, fy, 44, "<-", false);
  kbFKey(420, fy, 52, "Conectar", false);
  present(0, SCR_H - 1);
}
static void wifiRenderStatus(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, rgb565(14,16,24));
  wifiBack();
  if(wifiUIState == WUI_OK){
    drawCircle(SCR_W / 2, 280, 46, rgb565(90,220,140)); drawCircle(SCR_W / 2, 280, 45, rgb565(90,220,140));
    strokeSegAA(SCR_W / 2 - 20, 280, SCR_W / 2 - 4, 300, 4.0f, rgb565(90,220,140));
    strokeSegAA(SCR_W / 2 - 4, 300, SCR_W / 2 + 26, 260, 4.0f, rgb565(90,220,140));
    drawTextC(SCR_W / 2, 350, "Conectado", 3, rgb565(255,255,255));
    char ipl[40]; snprintf(ipl, sizeof(ipl), "IP: %s", wifiConnIP);
    drawTextC(SCR_W / 2, 390, ipl, 2, rgb565(160,170,196));
    fillRoundRect(SCR_W / 2 - 100, SCR_H - 120, 200, 56, 16, rgb565(60,110,235));
    drawTextC(SCR_W / 2, SCR_H - 102, "Listo", 2, rgb565(255,255,255));
  } else if(wifiUIState == WUI_CONNECTING){
    drawTextC(SCR_W / 2, 280, "Conectando...", 3, rgb565(255,255,255));
    char sub[48]; snprintf(sub, sizeof(sub), "a %s", wifiConnSSID);
    drawTextC(SCR_W / 2, 320, sub, 2, rgb565(160,170,196));
  } else {                                     // WUI_FAIL
    drawCircle(SCR_W / 2, 280, 46, rgb565(230,90,90)); drawCircle(SCR_W / 2, 280, 45, rgb565(230,90,90));
    strokeSegAA(SCR_W / 2 - 14, 264, SCR_W / 2 + 14, 296, 4.0f, rgb565(230,90,90));
    strokeSegAA(SCR_W / 2 + 14, 264, SCR_W / 2 - 14, 296, 4.0f, rgb565(230,90,90));
    drawTextC(SCR_W / 2, 350, "No se pudo conectar", 3, rgb565(255,255,255));
    drawTextC(SCR_W / 2, 388, "Contrase\xC3\xB1" "a incorrecta o red fuera de rango", 1, rgb565(160,170,196));
    fillRoundRect(SCR_W / 2 - 210, SCR_H - 120, 200, 56, 16, rgb565(70,74,90));
    drawTextC(SCR_W / 2 - 110, SCR_H - 102, "Cancelar", 2, rgb565(255,255,255));
    fillRoundRect(SCR_W / 2 + 10, SCR_H - 120, 200, 56, 16, rgb565(60,110,235));
    drawTextC(SCR_W / 2 + 110, SCR_H - 102, "Reintentar", 2, rgb565(255,255,255));
  }
  flxFlushAll();
}
static void wifiRenderUnavail(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, rgb565(14,16,24));
  wifiBack();
  drawTextC(SCR_W / 2, 60, "Wi-Fi", 4, rgb565(255,255,255));
  drawTextC(SCR_W / 2, 300, "Wi-Fi desactivado en este build", 2, rgb565(160,170,196));
  drawTextC(SCR_W / 2, 334, "(FLEXOS_ENABLE_WIFI = 0 en el .ino)", 1, rgb565(120,128,150));
  flxFlushAll();
}
static void wifiExit(){ gState = ST_APP; settingsRender(); }

static void wifiSettingsEnter(){
  gState = ST_WIFI;
#if FLEXOS_ENABLE_WIFI
  wifiSel = -1; wifiPass[0] = 0;
  mapaActivo = LAYOUT_ES; kbLangEs = true; kbShift = false;
  wifiStartScan();
  wifiRenderList();
#else
  wifiRenderUnavail();
#endif
}
static void wifiTick(){
#if !FLEXOS_ENABLE_WIFI
  if(T.tap && T.x < 48 && T.y < 48) wifiExit();
  return;
#else
  // Repinta UNA vez cuando el estado cambia por causas externas (la
  // tarea de escaneo/conexion en Core 1 termino). Las transiciones que
  // dispara el propio tap ya pintan de inmediato mas abajo.
  static int wifiUIStateShown = -1;
  if(wifiUIState != wifiUIStateShown){
    wifiUIStateShown = wifiUIState;
    if(wifiUIState == WUI_LIST || wifiUIState == WUI_SCANNING) wifiRenderList();
    else if(wifiUIState == WUI_CONNECTING || wifiUIState == WUI_OK || wifiUIState == WUI_FAIL) wifiRenderStatus();
  }
  switch(wifiUIState){
    case WUI_LIST: {
      if(T.tap){
        if(T.x < 48 && T.y < 48){ wifiExit(); return; }
        int by = SCR_H - 90;
        if(T.y >= by && T.y <= by + 56 && T.x >= SCR_W/2 - 110 && T.x <= SCR_W/2 + 110){ wifiStartScan(); return; }
        int cnt; portENTER_CRITICAL(&wifiMux); cnt = wifiNetCount; portEXIT_CRITICAL(&wifiMux);
        for(int i = 0; i < cnt; i++){
          int y = wifiRowY(i); if(y > SCR_H - 60) break;
          if(T.y >= y && T.y <= y + 56 && T.x >= 24 && T.x <= SCR_W - 24){
            wifiSel = i;
            if(!wifiNets[i].secure) wifiStartConnect();                          // red abierta: conecta directo
            else { wifiPass[0] = 0; wifiUIState = WUI_PASS; wifiKbAnim = millis(); }
            return;
          }
        }
      }
      break;
    }
    case WUI_SCANNING: break;    // pantalla estatica "Buscando..."; el repintado de arriba cambia a LIST solo
    case WUI_PASS: {
      if(wifiKbAnim){
        float p = (millis() - wifiKbAnim) / 300.0f; if(p >= 1){ p = 1; wifiKbAnim = 0; }
        wifiRenderPass((int)((1.0f - p) * (SCR_H - KB_Y)));
        return;
      }
      if(T.tap){
        if(T.x < 48 && T.y < 48){ wifiUIState = WUI_LIST; wifiRenderList(); return; }
        int fy = KB_Y + 3 * (KB_KH + KB_GAP);
        if(T.y >= fy && T.y <= fy + KB_KH){
          if(T.x < 64) kbShift = !kbShift;
          else if(T.x < 122) mapaActivo = (mapaActivo == LAYOUT_NUM) ? LAYOUT_EMOJI : (mapaActivo == LAYOUT_EMOJI) ? (kbLangEs ? LAYOUT_ES : LAYOUT_EN) : LAYOUT_NUM;
          else if(T.x < 174){ kbLangEs = !kbLangEs; if(mapaActivo == LAYOUT_ES || mapaActivo == LAYOUT_EN) mapaActivo = kbLangEs ? LAYOUT_ES : LAYOUT_EN; }
          else if(T.x < 368) wifiPassAppend(" ");
          else if(T.x < 416) wifiPassBackspace();
          else if(strlen(wifiPass) > 0){ wifiStartConnect(); return; }
          wifiRenderPass(0); return;
        }
        int cell = kbCellAt(T.x, T.y);
        if(cell >= 0){
          const char* k = mapaActivo[cell / KB_COLS][cell % KB_COLS];
          if(kbShift && k[1] == 0 && k[0] >= 'a' && k[0] <= 'z'){ char u[2] = { (char)(k[0] - 32), 0 }; wifiPassAppend(u); kbShift = false; }
          else wifiPassAppend(k);
          wifiRenderPass(0);
        }
      }
      break;
    }
    case WUI_CONNECTING: break;  // pantalla estatica "Conectando..."; el repintado de arriba cambia a OK/FAIL solo
    case WUI_OK: {
      if(T.tap){
        if(T.x < 48 && T.y < 48){ wifiExit(); return; }
        if(T.y >= SCR_H - 120 && T.y <= SCR_H - 64 && T.x >= SCR_W/2 - 100 && T.x <= SCR_W/2 + 100){ wifiExit(); return; }
      }
      break;
    }
    case WUI_FAIL: {
      if(T.tap){
        if(T.x < 48 && T.y < 48){ wifiUIState = WUI_LIST; wifiRenderList(); return; }
        if(T.y >= SCR_H - 120 && T.y <= SCR_H - 64){
          if(T.x >= SCR_W/2 - 210 && T.x <= SCR_W/2 - 10){ wifiUIState = WUI_LIST; wifiRenderList(); return; }             // cancelar
          if(T.x >= SCR_W/2 + 10 && T.x <= SCR_W/2 + 210){ wifiPass[0] = 0; wifiUIState = WUI_PASS; wifiKbAnim = millis(); return; }  // reintentar
        }
      }
      break;
    }
  }
#endif
}

// #############################################################
// ##  ISLA DINAMICA · logica y render  (FASE 1, parche anti-flicker)
// ##  ------------------------------------------------------
// ##  FIX aplicado tras el bug de parpadeo + tarjetas pegadas:
// ##  la isla ya NO compone sobre fb (el buffer que flxPresenter
// ##  lee por DMA en otro core). Ahora sigue el mismo patron que
// ##  animateHomeGlass(): restaura la banda limpia copiando desde
// ##  homeBuf hacia bbuf, dibuja las tarjetas sobre bbuf, y cruza
// ##  a fb con un unico present() atomico. bbuf es de un solo
// ##  escritor (el loop task); nadie mas lo lee, asi que el
// ##  presenter nunca puede capturar un frame a medio pintar.
// ##  Por eso el compose (restore+dibujar+present) solo corre con
// ##  gState==ST_HOME: homeBuf solo es un fondo valido ahi. El
// ##  avance de fases sigue sin condicion (es aritmetica pura).
// ##
// ##  En Fase 1 NO hay deteccion I2C real: las notificaciones se
// ##  disparan con un trigger de prueba (demo al primer Home + tap
// ##  arriba-derecha) para validar render/animacion/descarte de
// ##  forma AISLADA.
// ##
// ##  DESVIACION DELIBERADA respecto al plan original: el vidrio
// ##  se RE-HORNEA cada frame (drawLiquidGlassPanel) en vez de
// ##  "hornear una vez". Se hace asi solo porque son <=3 tarjetas
// ##  pequenas (448x64) y el coste es bajo; permite que el destello
// ##  (glassSheen) y el deslizamiento reutilicen el mismo camino
// ##  sin cachear un buffer por tarjeta. El blur costoso (pantalla
// ##  completa) se sigue evitando.
// #############################################################

// Icono del modulo: reutiliza los iconos de app existentes (mapeo simple)
static void drawModuleIcon(ModuleType type, int x, int y, int S){
  int id = IC_AJUSTES;
  switch(type){
    case MOD_ULTRASONIC:  id = IC_NAV;     break;
    case MOD_BME280:      id = IC_BIEN;    break;
    case MOD_MPU6050:     id = IC_JUEGOS;  break;
    case MOD_LED:         id = IC_CALC;    break;
    case MOD_BUTTON:      id = IC_NOTAS;   break;
    case MOD_SERVO:       id = IC_MODOPC;  break;
    case MOD_I2C_GENERIC: id = IC_ALMACEN; break;
    default:              id = IC_AJUSTES; break;
  }
  drawAppIcon(id, x, y, S);
}

// Restaura la banda limpia EN bbuf, copiando desde homeBuf (siempre al dia:
// se recompone solo en cada cambio de minuto, al salir de edicion, etc.).
// Mismo patron que animateHomeGlass(): homeBuf es la fuente, bbuf el lienzo
// de trabajo. Ya no hace falta snapshot manual (notifSnapshotBg desaparece).
static void notifRestoreBg(){
  if(!homeBuf || !bbuf) return;
  memcpy(bbuf + (size_t)NOTIF_BAND_TOP * SCR_W, homeBuf + (size_t)NOTIF_BAND_TOP * SCR_W,
         (size_t)SCR_W * NOTIF_BAND_H * 2);
}

// Encola una notificacion a partir de un modulo
static void notifPush(const DetectedModule* m){
  if(gNotifCount >= NOTIF_MAX) return;           // cola llena: se descarta (Fase 1)
  Notification* n = &gNotifs[gNotifCount++];
  n->mod    = *m;
  n->active = true;
  n->phase  = NP_IN;
  n->bornMs = millis();
  n->slideX = 0.0f;
  n->armed  = false;            // se arma (entrada + 5 s) al hacerse visible en Home
}

// Elimina la ranura idx y compacta la cola
static void notifRemove(int idx){
  if(idx < 0 || idx >= gNotifCount) return;
  for(int j = idx; j < gNotifCount - 1; j++) gNotifs[j] = gNotifs[j + 1];
  gNotifCount--;
  gNotifs[gNotifCount].active = false;
  if(notifDragIdx == idx)      notifDragIdx = -1;
  else if(notifDragIdx > idx)  notifDragIdx--;
}

// Ease-out cubica (0..1)
static inline float notifEaseOut(float p){ float q = 1.0f - p; return 1.0f - q * q * q; }

// Cola de burbuja de chat: un triangulo apuntando hacia ARRIBA, porque las
// tarjetas de notificacion caen desde el borde superior de la pantalla (no
// hay un icono de app en el Home al que apuntar -- estas son detecciones de
// hardware I2C via hwDetectTick(), no notificaciones que vengan de una app
// abierta). Solido, no vidrio: es demasiado pequeña para que el blur se
// note, y agrandar el panel solo para la cola no vale la pena.
static void notifDrawTail(int cx, int topY, uint16_t col){
  fillTriangle(cx - 8, topY, cx + 8, topY, cx, topY - 9, col);
}
// Dibuja una tarjeta en la coordenada Y dada (aplica su slideX horizontal)
static void notifDrawCard(Notification* n, int cardY){
  int x = NOTIF_MARGIN_X + (int)n->slideX;       // al deslizar a la izq, x se vuelve negativo
  int y = cardY, w = NOTIF_CARD_W, h = NOTIF_CARD_H;
  uint32_t t = millis();
  notifDrawTail(x + w / 2, y, rgb565(90, 120, 200));   // primero: la tarjeta se dibuja justo debajo, sin solaparla
  // Vidrio base (blur) + destello animado. drawLiquidGlassPanel recorta x<0
  // conservando el borde derecho -> el deslizamiento a la izquierda sale natural.
  glDrawSpec = false;
  drawLiquidGlassPanel(x, y, w, h, NOTIF_RAD, rgb565(40, 60, 130), t);
  glDrawSpec = true;
  glassSheen(x, y, w, h, NOTIF_RAD, t);
  // Degradado extra estilo burbuja (mas claro arriba, mas oscuro abajo).
  // Fila a fila con glInset() -- igual que drawLiquidGlassPanel -- para no
  // salirse de las esquinas redondeadas (una fillRectA plana sí se saldría).
  for(int j = 0; j < h; j++){
    int ins = glInset(j, h, NOTIF_RAD);
    uint8_t a = (uint8_t)(42 - 42 * j / h);
    if(a > 0) hLineA(x + ins, y + j, w - 2 * ins, rgb565(255,255,255), a);
  }
  drawRoundRect(x, y, w, h, NOTIF_RAD, rgb565(200, 210, 230));
  // Icono 40x40 (las primitivas acotan coords negativas: seguro fuera de pantalla)
  drawModuleIcon(n->mod.type, x + 12, y + (h - 40) / 2, 40);
  // Textos
  drawText(x + 62, y + 14, n->mod.name, 2, rgb565(255, 255, 255));
  drawText(x + 62, y + 38, n->mod.sub,  1, rgb565(205, 214, 232));
  // Boton cerrar (X)
  int cx = x + w - 22, cy = y + 20;
  strokeSegAA(cx - 5, cy - 5, cx + 5, cy + 5, 1.8f, rgb565(255, 255, 255));
  strokeSegAA(cx - 5, cy + 5, cx + 5, cy - 5, 1.8f, rgb565(255, 255, 255));
}

// ---- Trigger de PRUEBA (solo Fase 1; se retira/reemplaza en Fase 2) ----
// Compilado SOLO si NOTIF_DEMO_TRIGGERS = 1. Sus dos unicos llamadores estan
// bajo el mismo #if, asi que dejarlo siempre presente daba un aviso de funcion
// estatica sin usar en cada compilacion.
#if NOTIF_DEMO_TRIGGERS
// Empuja una notificacion demo, rotando por tipos para ver todos los iconos.
static void notifPushDemo(){
  static uint8_t k = 0;
  DetectedModule m;
  memset(&m, 0, sizeof(m));
  m.active = true; m.detectedAt = millis();
  switch(k % 5){
    case 0: m.type = MOD_BME280;      m.i2cAddr = 0x76;
            snprintf(m.name, sizeof(m.name), "Sensor BME280");
            snprintf(m.sub,  sizeof(m.sub),  "I2C 0x76 detectado"); break;
    case 1: m.type = MOD_MPU6050;     m.i2cAddr = 0x68;
            snprintf(m.name, sizeof(m.name), "MPU6050");
            snprintf(m.sub,  sizeof(m.sub),  "IMU - I2C 0x68"); break;
    case 2: m.type = MOD_ULTRASONIC;
            snprintf(m.name, sizeof(m.name), "Ultrasonido");
            snprintf(m.sub,  sizeof(m.sub),  "HC-SR04 detectado"); break;
    case 3: m.type = MOD_SERVO;
            snprintf(m.name, sizeof(m.name), "Servo");
            snprintf(m.sub,  sizeof(m.sub),  "Actuador PWM"); break;
    default:m.type = MOD_I2C_GENERIC; m.i2cAddr = 0x3C;
            snprintf(m.name, sizeof(m.name), "Dispositivo I2C");
            snprintf(m.sub,  sizeof(m.sub),  "0x3C detectado"); break;
  }
  k++;
  notifPush(&m);
}
#endif   // NOTIF_DEMO_TRIGGERS

// ---- Toque de la isla: intercepta SOLO dentro de las tarjetas ----
// Se llama en loop() justo despues de flexPollTouch() y antes del switch de
// estado. Consume unicamente los flags de evento que usa (tap/pressed/released/
// swipeLeft); NUNCA toca T.down (lo gestiona flexPollTouch) para no corromper la
// maquina de estados del tactil.
static void notifHandleTouch(){
  // La isla solo recibe toques cuando es visible (Home principal desbloqueado).
  if(gState != ST_HOME || qsPanelY != 0 || editMode || sbOwnsScreen()){ notifDragIdx = -1; return; }
  // Toques en tarjetas (cerrar, flick, iniciar arrastre)
  for(int i = 0; i < gNotifCount; i++){
    if(!gNotifs[i].active || gNotifs[i].phase == NP_OUT) continue;
    int cardY = NOTIF_Y0 + i * (NOTIF_CARD_H + NOTIF_GAP);
    int x0 = NOTIF_MARGIN_X, x1 = NOTIF_MARGIN_X + NOTIF_CARD_W;
    int y0 = cardY, y1 = cardY + NOTIF_CARD_H;
    // Boton cerrar (X) arriba-derecha
    int cx = NOTIF_MARGIN_X + NOTIF_CARD_W - 22, cy = cardY + 20;
    if(T.tap && abs(T.x - cx) < 16 && abs(T.y - cy) < 16){
      gNotifs[i].phase = NP_OUT; T.tap = false; T.pressed = false; return;
    }
    // Flick rapido a la izquierda sobre la tarjeta
    if(T.swipeLeft && T.startY >= y0 && T.startY <= y1){
      gNotifs[i].phase = NP_OUT; T.swipeLeft = false; T.tap = false; return;
    }
    // Iniciar arrastre (dedo dentro de la tarjeta)
    if(T.pressed && T.x >= x0 && T.x <= x1 && T.y >= y0 && T.y <= y1){
      notifDragIdx = i; T.pressed = false;
    }
  }
  // Arrastre en curso
  if(notifDragIdx >= 0 && notifDragIdx < gNotifCount){
    Notification* n = &gNotifs[notifDragIdx];
    if(T.down){
      float dx = (float)(T.x - T.startX);
      if(dx > 0) dx = 0;                                    // solo hacia la izquierda
      if(dx < -(NOTIF_CARD_W + 40)) dx = -(NOTIF_CARD_W + 40);
      n->slideX = dx; n->phase = NP_DRAG;
      T.tap = false; T.swipeLeft = false;                  // no propagar a la pantalla
    } else {
      // Soltar: descartar si paso el umbral, si no volver a su sitio
      if(n->slideX < -NOTIF_CARD_W / 4) n->phase = NP_OUT;
      else                              n->phase = NP_SPRING;
      notifDragIdx = -1;
      T.tap = false; T.released = false;
    }
  }
#if NOTIF_DEMO_TRIGGERS
  // Re-trigger de PRUEBA: tap arriba-derecha, fuera de la zona caliente de la
  // cortina (que captura startY<30) y solo en Home. Genera la siguiente demo.
  if(T.tap && gState == ST_HOME && qsPanelY == 0 && !editMode &&
     T.x >= SCR_W - 52 && T.y >= 36 && T.y <= 56){
    notifPushDemo();
    T.tap = false; T.pressed = false;
  }
#endif
}

// ---- Tick de la isla: anima y compone (se llama al final de loop) ----
static void notifTick(){
  // Trigger de prueba: primera demo al llegar a Home
#if NOTIF_DEMO_TRIGGERS
  static bool bootDemo = false;
  if(!bootDemo && gState == ST_HOME && millis() > 1200){ bootDemo = true; notifPushDemo(); }
#endif

  // Throttle ~30 fps
  if(millis() - notifLastMs < 33) return;
  notifLastMs = millis();

  // Nada que mostrar y banda ya limpia -> salida barata
  if(gNotifCount == 0 && !notifBandOn) return;

  // La isla SOLO vive en el Home principal desbloqueado (sin cortina ni edicion).
  // Fuera de ahi no avanzamos fases ni dibujamos: las notificaciones detectadas
  // durante el bloqueo esperan congeladas y su animacion de entrada + los 5 s
  // arrancan al llegar aqui. Asi tambien evitamos el conflicto de dibujo con
  // otras pantallas (que son quienes deben poseer el fb en ese momento).
  if(gState != ST_HOME || qsPanelY != 0 || editMode || sbOwnsScreen()){
    if(!notifPaused){ notifPaused = true; notifPauseT0 = millis(); }   // marca el inicio de la pausa (p.ej. se abrio una app)
    return;
  }
  if(notifPaused){
    // Reanudando tras una pausa (p.ej. se cerro la app que se abrio encima):
    // sumar el tiempo pausado a bornMs de cada tarjeta activa para que
    // conserven el tiempo que les quedaba, en vez de que millis()-bornMs se
    // dispare de golpe y todas pasen de fase (y se reindexen) en el mismo
    // frame -- eso era el parpadeo/"se queda bugeado" al volver de una app.
    uint32_t paused = millis() - notifPauseT0;
    for(int i = 0; i < gNotifCount; i++) gNotifs[i].bornMs += paused;
    notifPaused = false;
  }
  if(gNotifCount > 0) notifBandOn = true;

  // Armar la entrada de las notificaciones aun no mostradas
  for(int i = 0; i < gNotifCount; i++){
    if(!gNotifs[i].armed){
      gNotifs[i].armed  = true;
      gNotifs[i].phase  = NP_IN;
      gNotifs[i].bornMs = millis();
      gNotifs[i].slideX = 0.0f;
    }
  }

  // Avanzar fases de animacion
  for(int i = 0; i < gNotifCount; i++){
    Notification* n = &gNotifs[i];
    switch(n->phase){
      case NP_IN:
        if(millis() - n->bornMs >= 280) n->phase = NP_IDLE;
        break;
      case NP_IDLE:
        if(millis() - n->bornMs >= NOTIF_HOLD_MS) n->phase = NP_OUT;   // auto-descarte a los 5 s
        break;
      case NP_SPRING:
        n->slideX += (0.0f - n->slideX) * 0.35f;           // muelle de vuelta
        if(n->slideX > -0.5f){ n->slideX = 0.0f; n->phase = NP_IDLE; }
        break;
      case NP_OUT:
        n->slideX -= (NOTIF_CARD_W + NOTIF_MARGIN_X) * 0.18f + 6.0f;  // sale por la izquierda
        if(n->slideX < -(NOTIF_CARD_W + NOTIF_MARGIN_X + 4)){ notifRemove(i); i--; continue; }
        break;
      default: break;
    }
  }

  // Recorte completo (por si una app lo dejo estrecho) antes de componer
  gClipY0 = 0; gClipY1 = SCR_H - 1; gClipX0 = 0; gClipX1 = SCR_W - 1;

  // Componer en bbuf (nadie mas lo lee): restaurar fondo limpio y dibujar las
  // tarjetas encima. Mismo patron que animateHomeGlass(), con el que ademas
  // hay exclusion mutua (uiTick no anima el vidrio del Home mientras la isla
  // este activa) para que nadie mas presente esta banda -> sin parpadeo.
  setBuf(bbuf);
  notifRestoreBg();
  for(int i = 0; i < gNotifCount; i++){
    if(!gNotifs[i].active) continue;
    int cardY = NOTIF_Y0 + i * (NOTIF_CARD_H + NOTIF_GAP);
    if(gNotifs[i].phase == NP_IN){
      float p = (millis() - gNotifs[i].bornMs) / 280.0f; if(p > 1.0f) p = 1.0f;
      cardY -= (int)((1.0f - notifEaseOut(p)) * NOTIF_ENTER_DROP);   // entrada: cae desde arriba
    }
    notifDrawCard(&gNotifs[i], cardY);
  }
  // Volcado atomico bbuf->fb de una banda ya terminada. El presenter nunca ve
  // un fb a medio pintar.
  present(NOTIF_BAND_TOP, NOTIF_BAND_BOT - 1);

  // Banda vaciada: el frame de limpieza ya se compuso y volco arriba.
  if(gNotifCount == 0) notifBandOn = false;
}


// #############################################################
// ##  DETECCION DE HARDWARE I2C  (FASE 2)
// ##  ------------------------------------------------------
// ##  CLAVE DE SEGURIDAD: el escaneo corre en el MISMO contexto
// ##  que flexPollTouch() -el loop task, Core 1- llamando a
// ##  hwDetectTick() en cada vuelta. El GT911 tactil vive en el
// ##  mismo bus Wire; al no haber una segunda tarea tocando Wire,
// ##  las transacciones NUNCA se solapan y no hace falta mutex.
// ##  (Esto es a proposito lo contrario del plan original, que
// ##  ponia una tarea de escaneo en Core 1: eso compartia Wire
// ##  con el tactil sin proteccion -> corrupcion del bus/crash.)
// ##
// ##  Ademas el barrido es INCREMENTAL: sondea I2C_SCAN_PER_TICK
// ##  direcciones por vuelta, para no anadir latencia perceptible
// ##  al tactil ni forzar el watchdog. Los dispositivos nuevos
// ##  avisan por la isla dinamica de la Fase 1 (notifPush).
// ##
// ##  ALCANCE HONESTO: solo I2C, que es fiable. La deteccion de
// ##  modulos por GPIO (pulsadores, HC-SR04, servos) NO se hace
// ##  aqui porque no es distinguible sin falsos positivos; esos
// ##  llegaran por asignacion manual de pines en el asistente
// ##  (Fase 3), no por auto-deteccion.
// #############################################################

// Mapea una direccion I2C a un tipo de modulo conocido
static ModuleType identifyI2CDevice(uint8_t addr){
  switch(addr){
    case 0x76: case 0x77: return MOD_BME280;    // BME280 / BMP280
    case 0x68: case 0x69: return MOD_MPU6050;   // MPU6050 / MPU9250
    default:              return MOD_I2C_GENERIC;
  }
}

// Rellena name/sub descriptivos de un modulo I2C
static void i2cDescribe(DetectedModule* m){
  switch(m->type){
    case MOD_BME280:
      snprintf(m->name, sizeof(m->name), "Sensor BME280");
      snprintf(m->sub,  sizeof(m->sub),  "I2C 0x%02X detectado", m->i2cAddr);
      break;
    case MOD_MPU6050:
      snprintf(m->name, sizeof(m->name), "MPU6050");
      snprintf(m->sub,  sizeof(m->sub),  "IMU - I2C 0x%02X", m->i2cAddr);
      break;
    default:
      snprintf(m->name, sizeof(m->name), "Dispositivo I2C");
      snprintf(m->sub,  sizeof(m->sub),  "0x%02X detectado", m->i2cAddr);
      break;
  }
}

// ¿La direccion es la del GT911 tactil? (nunca notificar el propio panel)
static inline bool i2cIsTouch(uint8_t addr){ return addr == gtAddr || addr == 0x5D || addr == 0x14; }

// Indice de un modulo por direccion (o -1)
static int i2cFindByAddr(uint8_t addr){
  for(int i = 0; i < detectedCount; i++)
    if(detectedModules[i].i2cAddr == addr) return i;
  return -1;
}

// Marca presencia de una direccion; si es NUEVA la registra y avisa por la isla
static void i2cOnDevicePresent(uint8_t addr){
  if(i2cIsTouch(addr)) return;
  int idx = i2cFindByAddr(addr);
  if(idx >= 0){
    modSweepId[idx] = i2cSweepId;                 // sigue presente en este barrido
    if(!detectedModules[idx].active){             // reaparecio tras haberse desconectado
      detectedModules[idx].active = true;
      detectedModules[idx].detectedAt = millis();
      notifPush(&detectedModules[idx]);
    }
    return;
  }
  if(detectedCount >= MAX_MODULES_DETECTED) return;
  DetectedModule m;
  memset(&m, 0, sizeof(m));
  m.i2cAddr = addr;
  m.type    = identifyI2CDevice(addr);
  m.active  = true;
  m.numPins = 0;
  m.detectedAt = millis();
  i2cDescribe(&m);
  int slot = detectedCount++;
  detectedModules[slot] = m;
  modSweepId[slot] = i2cSweepId;
  notifPush(&detectedModules[slot]);
}

// Cierra un barrido completo: lo no visto -> inactivo (permite re-aviso al reconectar)
static void i2cEndSweep(){
  for(int i = 0; i < detectedCount; i++)
    if(detectedModules[i].active && modSweepId[i] != i2cSweepId)
      detectedModules[i].active = false;
  i2cSweepId++;
  i2cLastSweep = millis();
}

// Tick de deteccion I2C. Llamar en loop() en el mismo contexto que flexPollTouch.
static void hwDetectTick(){
  if(!gtOk) return;                                          // sin I2C inicializado, nada
  if(!i2cSweeping){
    if(millis() - i2cLastSweep < I2C_SWEEP_INTERVAL) return; // espera entre barridos
    i2cSweeping   = true;
    i2cScanCursor = I2C_SCAN_LO;
  }
  int probes = 0;
  while(i2cSweeping && probes < I2C_SCAN_PER_TICK){
    uint8_t addr = i2cScanCursor;
    if(!i2cIsTouch(addr)){
      Wire.beginTransmission(addr);
      if(Wire.endTransmission() == 0) i2cOnDevicePresent(addr);   // ACK -> hay dispositivo
    }
    probes++;
    if(i2cScanCursor >= I2C_SCAN_HI){ i2cSweeping = false; i2cEndSweep(); }
    else i2cScanCursor++;
  }
}


void setup(){
  Serial.begin(115200);
  delay(60);
  Serial.println(F("\n=== FlexOS Ultra (ESP32-P4) arrancando ==="));

  // (Ya NO se toca el TWDT aqui.) La version anterior llamaba a
  // esp_task_wdt_reconfigure() en cada arranque para dar margen al
  // bring-up del panel. Era innecesario -nada en setup() bloquea mas
  // de una fraccion de segundo, y la radio ya no corre aqui- y es
  // codigo nuevo no verificado contra el estado real del TWDT en esta
  // placa, asi que se retira: menos superficie para un crash en cada
  // boot. Sigue en pie esp_task_wdt_reset() en loop() (ver mas abajo).

  // Panel: reintento acotado. Si de verdad no enciende (cableado DSI),
  // parpadeo del backlight como SOS -> la placa sigue viva, no muerta.
  bool panelOk = flexPanelInit();
  if(!panelOk){ delay(150); panelOk = flexPanelInit(); }
  if(!panelOk){
    Serial.println(F("[FATAL] el panel DSI no responde (revisa cableado)"));
    pinMode(PIN_LCD_BL, OUTPUT);
    for(;;){ digitalWrite(PIN_LCD_BL, HIGH); delay(150); digitalWrite(PIN_LCD_BL, LOW); delay(150); }
  }
  if(!flxGfxInit()){
    Serial.println(F("[FATAL] sin PSRAM (activa 'PSRAM: Enabled' en el IDE)"));
    for(;;) delay(1000);
  }

  flexTouchInit();       // GT911: fallo suave (si no aparece, se sigue sin tactil)
  bootInitRadioSafe();   // WiFi/C6: BYPASS -> nunca bloquea el arranque
  cfgLoad();
  setBacklight(gBright);          // aplica el brillo guardado
  homeOrderLoad();                // orden de iconos del Home
  sbPinnedLoad();                 // lista de apps ancladas al Panel Edge (persistida)

  clkBootMs = millis();
  seedMinOfDay = 13 * 60 + 23;      // siembra: sab 4 jul, 13:23 (como tus imagenes)
  clkLastMin = -1;
  clkUpdate();

  // Pantalla de diagnostico SOLO si el reinicio fue ANORMAL
  // (crash / watchdog / brownout). En encendido normal, arranque limpio.
  esp_reset_reason_t rr = esp_reset_reason();
  bool abnormal = !(rr == ESP_RST_POWERON || rr == ESP_RST_SW);
  if(abnormal) showBootBanner();

  // Fondo NEGRO ABSOLUTO para el splash (como un movil comercial)
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, rgb565(0,0,0));
  flxFlushAll();

  splashStart = millis();
  gState = ST_SPLASH;
}

// Bucle de animacion continuo del vidrio (destello) — corre pase lo que pase,
// no solo cuando hay un tap. Compone off-screen (anti-flicker) via cada render.
static unsigned long uiAnimMs = 0;
static void uiTick(){
  // El sheen del Home y el ripple del icono son animaciones puramente TEMPORALES
  // (su posicion es funcion de millis(), no un paso fijo por frame) y, tras el
  // repintado parcial de animateHomeGlass/animateIconRipple, cuestan muy poco por
  // frame. Por eso ESAS DOS rutas se refrescan a ~60 fps: el reflejo se ve
  // claramente mas fluido con trayectoria IDENTICA. El resto (edicion, cortina,
  // calculadora, ventanas) conserva su cadencia de ~26 fps -- algunas llevan
  // pasos por-frame (p.ej. el resorte de iconos en Modo Edicion) y acelerarlas
  // cambiaria su VELOCIDAD, no solo su suavidad; por eso no se tocan.
  // El ripple SI puede ir a 60 fps: restaura exactamente la franja que dibuja, asi
  // que es correcto por construccion y muy barato. El sheen del Home vuelve a su
  // cadencia original porque repinta la banda completa (ver animateHomeGlass).
  bool fastPath = (gState == ST_HOME && gRippleActive && !sbOwnsScreen()
                   && qsPanelY <= 0 && !editMode);
  unsigned long interval = fastPath ? 16 : 38;
  if(millis() - uiAnimMs < interval) return;
  uiAnimMs = millis();
  if(gState == ST_HOME){
    if(sbOwnsScreen()){}                         // Panel Edge activo: cede el control por completo (ver sbTick/sbRenderOverlay)
    else if(qsPanelY > 0) qsRender();            // cortina visible: destello continuo (incluso durante el drag)
    else if(editMode) edRender();               // jiggle continuo
    else if(gRippleActive) animateIconRipple(); // destello del icono tocado (Vidrio), ~0.5s, tiene prioridad
    else if(uiGlass && gNotifCount == 0 && !notifBandOn) animateHomeGlass();  // widgets + dock (cede la banda a la isla)
  } else if(gState == ST_WINMGR){
    if(uiGlass) wmRender();                      // sheen de las ventanas
  } else if(gState == ST_APP && gAppId == IC_CALC && !sbOwnsScreen()){
    calcAnimSheen();                             // reflejo de las teclas de la calculadora
  }
}

void loop(){
  esp_task_wdt_reset();   // alimenta el TWDT del loopTask en cada vuelta (API 3.x/IDF5)
  flexPollTouch();
  notifHandleTouch();     // la isla intercepta toques dentro de sus tarjetas (Fase 1)
  hwDetectTick();         // deteccion I2C incremental, mismo contexto que el tactil (Fase 2)
  bool minChanged = clkUpdate();
  gMinChanged = minChanged;

  switch(gState){
    case ST_SPLASH:    splashTick(); break;
    case ST_OOBE_LANG: oobeLangTick(); break;
    case ST_OOBE_NAME: oobeNameTick(); break;
    case ST_LOCK:
      if(minChanged){ renderLock(); if(lockOff == 0) showLock(); }
      lockTick();
      break;
    case ST_HOME:
      if(minChanged && qsPanelY == 0 && !editMode){
        renderHome();                    // refresca el cache homeBuf (offscreen: setBuf(homeBuf)...setBuf(fb), sin tocar pantalla)
        if(sbOwnsScreen()) sbRenderOverlay();  // Panel Edge/ventanas visibles: recompone CON ellas encima (nunca las tapa)
        else                showHome();        // camino normal, sin cambios
      }
      if(editMode || !qsHandle()) homeTick();     // en edicion, saltar la cortina
      break;
    case ST_APP:       appTick(); break;
    case ST_SWITCHER:  swTick(); break;
    case ST_WINMGR:    wmTick(); break;
    case ST_LOCKSETUP: lsuTick(); break;
    case ST_WIFI:      wifiTick(); break;
  }
  uiTick();               // animacion continua del vidrio
  notifTick();            // isla dinamica: anima y compone sobre la pantalla activa (Fase 1)
  delay(5);
}

// #############################################################
// ##  HOJA DE RUTA  (lo que llega despues del Milestone 1)
// #############################################################
//
//  Milestone 1 (ESTE archivo) — COMPLETO:
//    · Capa HW nativa 480x800 (panel ST7701 + GT911) reusada de ArduOS
//    · Motor grafico propio (framebuffers PSRAM + presenter core 0)
//    · Fuente 5x7 con acentos UTF-8 (es/fr/pt/it) + reloj vectorial
//    · Splash con fundido · OOBE (6 idiomas + teclado QWERTY)
//    · Bloqueo con reloj gigante y desbloqueo con fisica (swipe-up)
//    · Escritorio: barra de estado, 2 widgets, rejilla 4x3, dock, nav
//    · Banda forense de reinicio (depuracion sin PC)
//
//  Milestone 2 — Framework de apps + apps reales:
//    [HECHO] Sistema de ventanas: apertura/cierre animado desde el icono,
//            marco estandar (estado + cabecera "atras" + nav), registro
//            APP_REG enchufable, gestos de cierre. App de referencia: Reloj.
//    [PENDIENTE] Rellenar el resto (reemplazar entradas de APP_REG):
//      Galeria, Multimedia, Almacenamiento, Modo PC, Notas, Educacion,
//      Navegador, Code IDE, Bienestar, Paint, Juegos, Calculadora,
//      Calendario, Camara.
//
//  Milestone 3 — Ajustes (imagen 3): [HECHO] dos paneles (barra lateral
//    de 12 categorias + panel de detalle con scroll). General y Acerca de
//    con datos reales del dispositivo; resto con filas representativas.
//    Motor: se anadio recorte vertical (clip) para listas con scroll.
//
//  Milestone 4 — Modo PC estilo Windows 11 (imagen 4): barra de
//    tareas, ventanas flotantes, escritorio horizontal.
//
//  Pendientes de plataforma (cuando toque):
//    · Fuente CJK (archivo de fuente en SPIFFS) para chino real.
//    · WiFi/NTP: hoy OFF por la inestabilidad del co-procesador C6
//      (esp-hosted). Reactivar tras actualizar su firmware/core.
//    · Bateria real por ADC y brillo por PWM del backlight.
//    · Almacenamiento en SD / SPIFFS para fondos y ajustes.
// #############################################################
