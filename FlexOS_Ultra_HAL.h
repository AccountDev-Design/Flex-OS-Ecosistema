// #############################################################
// ##  FLEX OS ULTRA  ·  CAPA DE HARDWARE  (datos del fabricante)
// ##  ----------------------------------------------------------
// ##  Encender el panel (LDO canal 3 + MIPI-DSI 2 lanes + tabla DCS del
// ##  ST7701), publicar colores por DMA2D y leer el GT911 por I2C.
// ##  Es lo UNICO que se reutiliza de ArduOS: son datos del fabricante.
// ##
// ##  COMO ENCAJA ESTE ARCHIVO
// ##  ----------------------------------------------------------
// ##  Es una PARTE del sketch FlexOS_Ultra.ino, no una unidad de
// ##  traduccion independiente. FlexOS_Ultra.ino lo incluye en el
// ##  orden que fija la cadena de cabeceras (cada modulo incluye al
// ##  anterior), asi que todo el sistema sigue compilandose como UN
// ##  SOLO archivo, exactamente igual que antes de separarlo.
// ##
// ##  Consecuencias practicas, y son las que mantienen esto seguro:
// ##    · Las variables globales se DEFINEN una sola vez, aqui, en el
// ##      modulo al que pertenecen. No hace falta `extern` ni existe
// ##      el riesgo de una definicion duplicada en el enlazado.
// ##    · El ORDEN de definicion es el mismo que tenia el .ino: una
// ##      funcion `static` solo se puede llamar despues de definirse,
// ##      y esa relacion se conserva modulo a modulo.
// ##    · La cadena de includes es LINEAL (Types -> ... -> Recovery),
// ##      asi que no hay dependencias circulares posibles.
// ##    · No lo incluyas por tu cuenta desde otro sitio: el punto de
// ##      entrada del sistema es siempre FlexOS_Ultra.ino.
// #############################################################
#pragma once
#include "FlexOS_Ultra_Types.h"   // eslabon anterior de la cadena

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
// Ultimo porcentaje REALMENTE escrito al PWM. No es lo mismo que gBright: el
// fundido de la suspension baja el PWM a 0 SIN tocar gBright (para no perder el
// brillo del usuario). Los fundidos arrancan desde aqui, no desde gBright --
// si arrancaran desde gBright, el fundido de vuelta empezaria ya en el valor
// final y la pantalla daria un fogonazo en vez de aparecer poco a poco.
static int  gBlPct  = 80;
static void setBacklight(int pct){
  if(pct < 5) pct = 5; if(pct > 100) pct = 100;
  gBright = pct; gBlPct = pct;
  if(gBlPwm) ledcWrite(PIN_LCD_BL, map(pct, 0, 100, 25, 255));
}
// Escribe el PWM del backlight para los FUNDIDOS. Mismo mecanismo de siempre
// (ledcWrite sobre PIN_LCD_BL); nunca se toca el pin a pelo con digitalWrite
// salvo en el mismo fallback "sin PWM" que ya usa flexPanelInit si ledcAttach
// falla. Dos diferencias deliberadas con setBacklight():
//
//  1) NO toca gBright. El brillo elegido por el usuario sigue intacto durante
//     toda la suspension, asi que restaurarlo al despertar es exacto y gratis.
//  2) Rampa LINEAL 0..100 -> duty 0..255, en vez del mapeo 25..255 de
//     setBacklight. Ese 25 es el suelo que impide dejar la pantalla invisible
//     desde el slider del Panel Rapido, pero para un fundido es justo lo que
//     sobra: por debajo de ese suelo el backlight todavia ilumina, asi que el
//     ultimo paso hasta 0 seria un corte seco en vez de un fundido. Con la
//     rampa lineal el negro se alcanza de verdad y de forma continua.
//     En el extremo alto las dos curvas practicamente coinciden (a 80% dan 204
//     y 209 de duty), y el fundido de vuelta termina llamando a setBacklight()
//     con el valor exacto, asi que no queda ninguna diferencia visible.
static void blWritePct(int pct){
  if(pct < 0) pct = 0; if(pct > 100) pct = 100;
  gBlPct = pct;
  if(!gBlPwm){ digitalWrite(PIN_LCD_BL, pct > 0 ? HIGH : LOW); return; }
  ledcWrite(PIN_LCD_BL, (uint32_t)(pct * 255 / 100));
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

  // 7) Callback de fin de flush (libera el buffer que acaba de leer DMA2D)
  flxDpiSem = xSemaphoreCreateBinary();
  if(!flxDpiSem){
    Serial.println(F("[HW] ERROR: sin semaforo para DMA2D"));
    return false;
  }
  esp_lcd_dpi_panel_event_callbacks_t cbs = {};
  cbs.on_color_trans_done = flxDpiFlushDone;
  if(esp_lcd_dpi_panel_register_event_callbacks(flxPanel, &cbs, flxDpiSem) != ESP_OK){
    Serial.println(F("[HW] ERROR: callback DMA2D no registrado"));
    return false;
  }

  gBlPwm = ledcAttach(PIN_LCD_BL, 20000, 8);   // backlight ON con brillo PWM
  if(gBlPwm) setBacklight(gBright);
  else digitalWrite(PIN_LCD_BL, HIGH);         // fallback: encendido fijo
  Serial.println(F("[HW] Panel DSI 480x800 NATIVO OK"));
  return true;
}

// ---- Comandos DCS de bajo consumo del ST7701 -----------------
// La capa de driver de este proyecto SI expone DCS de bajo nivel: flxPanelIo es
// el canal DBI y flexPanelInit() ya manda 0x11 (SLPOUT) y 0x29 (DISPON) a pelo
// con esp_lcd_panel_io_tx_param. Se reutiliza ese mismo camino, sin librerias
// nuevas ni APIs sin confirmar.
//
//   0x28 DISPOFF -> el panel deja de driver la matriz. Reversible al instante
//                   con 0x29 y SIN reinicializar la tabla del vendor. Es lo que
//                   usa la SUSPENSION.
//   0x10 SLPIN   -> ademas apaga el generador interno. Mas ahorro, pero salir
//                   pide 0x11 + 120 ms. Solo se usa en el APAGADO COMPLETO,
//                   donde el chip se va a deep sleep y al volver flexPanelInit()
//                   rehace el panel entero de todas formas -> riesgo cero.
//
// SIEMPRE se llaman DESPUES de que el backlight haya llegado a 0, nunca antes:
// asi el usuario no llega a ver el efecto del panel entrando en el modo.
static void panelDisplayOff(){
#if PANEL_DCS_SLEEP_ON
  if(flxPanelIo) esp_lcd_panel_io_tx_param(flxPanelIo, 0x28, NULL, 0);   // DISPOFF
#endif
}
static void panelDisplayOn(){
#if PANEL_DCS_SLEEP_ON
  if(flxPanelIo) esp_lcd_panel_io_tx_param(flxPanelIo, 0x29, NULL, 0);   // DISPON
#endif
}
static void panelSleepIn(){
#if PANEL_DCS_SLEEP_ON
  if(!flxPanelIo) return;
  esp_lcd_panel_io_tx_param(flxPanelIo, 0x28, NULL, 0);                  // DISPOFF
  esp_lcd_panel_io_tx_param(flxPanelIo, 0x10, NULL, 0);                  // SLPIN
#endif
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
// NUMERO DE DEDOS del ultimo frame valido del GT911. El byte de estado 0x814E
// ya trae la cuenta en (status & 0x0F); antes se leia y se tiraba. Lo unico que
// necesita el gesto de suspension es ESA cuenta, asi que NO se leen los bloques
// de los puntos extra (0x8158, 0x8160...): seguiria costando I2C en cada poll
// para unas coordenadas que nadie usa. gtPoll sigue devolviendo el primer punto
// exactamente igual que siempre -> el contrato de struct Touch no cambia.
static uint8_t  gtFingers   = 0;
static uint32_t gtFingersMs = 0;   // millis() del ultimo frame valido (para caducar la cuenta)

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
  gtFingers = n; gtFingersMs = millis();   // cuenta de contactos para el gesto de suspension
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

// ---- FASE B: lectura MULTIPUNTO del GT911 (solo para el teclado) ----
// gtPoll() de arriba NO se toca: sigue siendo la unica fuente de struct Touch
// y todo el sistema (ventanas, gestos, Kiosk, notificaciones, juegos) sigue
// exactamente igual de single-touch que siempre. Esto es una ruta APARTE que
// solo usan las superficies de teclado.
//
// MAPA DE REGISTROS (hoja de datos del GT911, confirmado):
//   0x814E  estado: bit7 = hay frame nuevo, bits 3..0 = numero de dedos
//   0x8150  punto 1 (bloque de 8 bytes)   0x8158 punto 2   0x8160 punto 3
//   0x8168  punto 4                       0x8170 punto 5
//   dentro del bloque: [0..1] X (LSB primero), [2..3] Y (LSB primero),
//                      [4..5] tamano del contacto (no se usa), [6] reservado,
//                      [7] TRACK ID  <- lo que identifica al dedo entre frames
//
// POR QUE NO SE PELEA CON gtPoll(): esta funcion se llama desde el tick de la
// superficie, que corre en la MISMA vuelta del loop unos microsegundos despues
// de flexPollTouch(). A esas alturas gtPoll ya limpio el flag de "frame nuevo",
// asi que aqui casi siempre se entra por la rama de abajo: se reutiliza la
// cuenta de dedos de ESE mismo frame (gtFingers) y se leen los bloques de
// puntos, que conservan las coordenadas del ultimo frame reportado. Cero
// frames robados a gtPoll. En la rara vuelta en que el chip publica un frame
// justo en medio, se consume y se limpia el estado como manda la hoja de datos
// (gtPoll devolvera -1 esa vuelta, que es un caso que flexPollTouch ya
// contempla desde siempre).
//
// Devuelve el numero de puntos activos, o -1 si no habia dato utilizable.
static int gtPollMulti(){
  for(int i = 0; i < KB_MAXPOINTS; i++) gKbPoints[i].active = false;
  if(!gtOk) return -1;
  uint8_t status = 0;
  bool fresh = false;
  int n = -1;
  if(gtRd(0x814E, &status, 1) && (status & 0x80)){
    fresh = true;
    n = status & 0x0F;
    gtFingers = (uint8_t)n; gtFingersMs = millis();
  }
  if(n < 0){
    if(millis() - gtFingersMs > 120) return -1;   // la cuenta ya caduco: sin dato fiable
    n = gtFingers;
  }
  if(n > KB_MAXPOINTS) n = KB_MAXPOINTS;
  for(int i = 0; i < n; i++){
    uint8_t d[8];
    if(!gtRd((uint16_t)(0x8150 + i * 8), d, 8)) break;
    int px = (int)((uint16_t)d[0] | ((uint16_t)d[1] << 8));
    int py = (int)((uint16_t)d[2] | ((uint16_t)d[3] << 8));
#if GT911_SWAP_XY
    { int t = px; px = py; py = t; }
#endif
#if GT911_FLIP_X
    px = (SCR_W - 1) - px;
#endif
#if GT911_FLIP_Y
    py = (SCR_H - 1) - py;
#endif
    if(px < 0) px = 0; if(px > SCR_W - 1) px = SCR_W - 1;
    if(py < 0) py = 0; if(py > SCR_H - 1) py = SCR_H - 1;
    gKbPoints[i].id     = (int)d[7];
    gKbPoints[i].x      = px;
    gKbPoints[i].y      = py;
    gKbPoints[i].active = true;
  }
  if(fresh) gtWr(0x814E, 0);     // igual que gtPoll: se limpia el estado tras leer
  return n;
}
// #############################################################
// ##  FIN de la capa de hardware reutilizada
// #############################################################
