// #############################################################
// ##  FlexOS · AUDIO · ES8311 + I2S  (implementacion ESP32-P4)
// #############################################################

#include "FlexOS_Audio.h"
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include "driver/i2s_std.h"

// -------------------------------------------------------------
//  Registros del ES8311 que se usan. Solo estos: no se escribe un
//  volcado completo de 60 registros copiado de ningun sitio, porque
//  la mitad no se sabria justificar.
// -------------------------------------------------------------
#define ES_RESET        0x00
#define ES_CLK_MAN1     0x01
#define ES_CLK_MAN2     0x02
#define ES_CLK_MAN3     0x03
#define ES_ADC_OSR      0x04
#define ES_CLK_MAN6     0x05
#define ES_CLK_MAN7     0x06
#define ES_CLK_MAN8     0x07
#define ES_CLK_MAN9     0x08
#define ES_SDP_IN       0x09
#define ES_SDP_OUT      0x0A
#define ES_SYSTEM_0B    0x0B
#define ES_SYSTEM_0C    0x0C
#define ES_SYSTEM_PWR   0x0D
#define ES_SYSTEM_ADC   0x0E
#define ES_SYSTEM_0F    0x0F
#define ES_SYSTEM_10    0x10
#define ES_SYSTEM_11    0x11
#define ES_SYSTEM_DACPW 0x12
#define ES_SYSTEM_OUT   0x13
#define ES_SYSTEM_ADCFM 0x14
#define ES_ADC_16       0x16
#define ES_ADC_VOL      0x17
#define ES_ADC_1C       0x1C
#define ES_DAC_VOL      0x32       // <- el volumen REAL de la salida
#define ES_DAC_RAMP     0x37
#define ES_CHIP_ID1     0xFD       // debe leer 0x83
#define ES_CHIP_ID2     0xFE       // debe leer 0x11

// 0xBF es 0 dB en el registro de volumen del DAC. Por encima el
// codec aplica ganancia digital: se deja fuera a proposito.
#define ES_VOL_0DB      0xBF

// -------------------------------------------------------------
//  Estado
// -------------------------------------------------------------
static bool          auCodecOk  = false;
static bool          auI2sOk    = false;
static bool          auPlaying  = false;
static const char*   auErr      = "Sin inicializar";
static uint8_t       auVol      = FLEXAUDIO_VOL_DEF;
static bool          auMuted    = false;
static uint32_t      auRate     = 0;
static uint16_t      auCh       = 0, auBits = 0;
static i2s_chan_handle_t auTx   = NULL;
static Preferences    auPrefs;

// -------------------------------------------------------------
//  I2C. Se usa el Wire que ya inicializo el sketch para el tactil:
//  es el MISMO bus fisico (GPIO7/8). Aqui no se llama a
//  Wire.begin() ni se cambia la velocidad, porque eso reconfiguraria
//  el bus por debajo del GT911.
// -------------------------------------------------------------
static bool esWrite(uint8_t reg, uint8_t val){
  Wire.beginTransmission(FLEXAUDIO_I2C_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}
static bool esRead(uint8_t reg, uint8_t* val){
  Wire.beginTransmission(FLEXAUDIO_I2C_ADDR);
  Wire.write(reg);
  if(Wire.endTransmission(false) != 0) return false;
  if(Wire.requestFrom((int)FLEXAUDIO_I2C_ADDR, 1) != 1) return false;
  *val = (uint8_t)Wire.read();
  return true;
}

// -------------------------------------------------------------
//  VOLUMEN REAL
//  ------------------------------------------------------------
//  0 -> registro 0x00 (mudo de verdad, -95,5 dB).
//  1..100 -> 0x01..0xBF, o sea hasta 0 dB y ni un paso mas.
// -------------------------------------------------------------
static uint8_t auVolReg(uint8_t v){
  if(v == 0) return 0x00;
  if(v > FLEXAUDIO_VOL_MAX) v = FLEXAUDIO_VOL_MAX;
  uint32_t r = ((uint32_t)v * ES_VOL_0DB) / FLEXAUDIO_VOL_MAX;
  if(r == 0) r = 1;
  return (uint8_t)r;
}
static void auApplyVolume(){
  if(!auCodecOk) return;
  esWrite(ES_DAC_VOL, auVolReg(auMuted ? 0 : auVol));
}

// El amplificador se enciende SOLO mientras hay algo sonando: un
// NS4150 alimentado sin senal es ruido de fondo y consumo.
static void auAmp(bool on){
  pinMode(FLEXAUDIO_PIN_PA, OUTPUT);
  digitalWrite(FLEXAUDIO_PIN_PA, on ? HIGH : LOW);
}

// -------------------------------------------------------------
//  Secuencia de arranque del codec (modo esclavo, MCLK = 256 x Fs)
//  ------------------------------------------------------------
//  El P4 es el maestro del bus I2S y entrega MCLK, BCLK y LRCK; el
//  ES8311 solo recibe. Es la configuracion mas simple y la que menos
//  depende de divisores internos del codec.
// -------------------------------------------------------------
static bool esInit(){
  if(!esWrite(ES_RESET, 0x1F)) return false;   // reset general
  delay(20);
  esWrite(ES_RESET, 0x00);
  esWrite(ES_RESET, 0x80);                     // fuera de reset, modo ESCLAVO

  esWrite(ES_CLK_MAN1, 0x30);                  // MCLK activo, desde el pin
  esWrite(ES_CLK_MAN2, 0x00);                  // sin division (MCLK = 256 Fs)
  esWrite(ES_CLK_MAN3, 0x10);
  esWrite(ES_ADC_OSR,  0x10);
  esWrite(ES_CLK_MAN6, 0x00);
  esWrite(ES_CLK_MAN7, 0x03);
  esWrite(ES_CLK_MAN8, 0x00);
  esWrite(ES_CLK_MAN9, 0xFF);

  esWrite(ES_SYSTEM_0B, 0x00);
  esWrite(ES_SYSTEM_0C, 0x00);
  esWrite(ES_SYSTEM_10, 0x1F);                 // referencias analogicas
  esWrite(ES_SYSTEM_11, 0x7C);
  esWrite(ES_ADC_16,    0x24);
  esWrite(ES_ADC_1C,    0x6A);
  esWrite(ES_DAC_RAMP,  0x08);                 // rampa de volumen: sin chasquido

  esWrite(ES_SYSTEM_PWR,   0x01);              // enciende la parte analogica
  esWrite(ES_SYSTEM_ADC,   0x02);
  esWrite(ES_SYSTEM_DACPW, 0x00);              // DAC encendido
  esWrite(ES_SYSTEM_OUT,   0x10);              // salida a la etapa de linea
  esWrite(ES_SYSTEM_ADCFM, 0x1A);
  esWrite(ES_ADC_VOL,      0xBF);
  return true;
}

// Formato de la interfaz serie segun los bits por muestra.
static bool esSetBits(uint16_t bits){
  uint8_t v;
  if(bits == 16)     v = 0x00;                 // I2S, 16 bits
  else if(bits == 8) v = 0x0C;                 // I2S, 8 bits
  else return false;
  esWrite(ES_SDP_IN,  v);
  esWrite(ES_SDP_OUT, v);
  return true;
}

// -------------------------------------------------------------
//  Arranque del modulo
// -------------------------------------------------------------
bool flexAudioBegin(){
  auCodecOk = auI2sOk = false;

  // Volumen guardado. Se lee siempre, aunque no haya codec: asi el
  // valor sobrevive a un arranque en el que el bus falle.
  if(auPrefs.begin("flexaudio", true)){
    auVol   = (uint8_t)auPrefs.getUChar("vol", FLEXAUDIO_VOL_DEF);
    auMuted = auPrefs.getBool("mute", false);
    auPrefs.end();
  }
  if(auVol > FLEXAUDIO_VOL_MAX) auVol = FLEXAUDIO_VOL_DEF;

  // 1) ¿HAY codec de verdad? Se pregunta su identificacion. Un ACK
  //    suelto no basta: cualquier cosa podria estar en 0x18. Estos
  //    dos registros solo leen 0x83/0x11 si es un ES8311 y si el bus
  //    funciona en LOS DOS sentidos.
  uint8_t id1 = 0, id2 = 0;
  if(!esRead(ES_CHIP_ID1, &id1) || !esRead(ES_CHIP_ID2, &id2)){
    auErr = "El codec ES8311 no responde en el bus I2C";
    Serial.println(F("[AUDIO] ES8311 no responde (0x18)"));
    return false;
  }
  if(id1 != 0x83 || id2 != 0x11){
    auErr = "En 0x18 hay otro chip, no un ES8311";
    Serial.printf("[AUDIO] identificacion inesperada: %02X %02X\n", id1, id2);
    return false;
  }
  if(!esInit()){
    auErr = "El codec no acepto su configuracion";
    return false;
  }
  auCodecOk = true;
  auAmp(false);
  auApplyVolume();
  auErr = "Listo";
  Serial.println(F("[AUDIO] ES8311 detectado y configurado"));
  return true;
}

bool        flexAudioAvailable(){ return auCodecOk; }
const char* flexAudioError(){ return auErr ? auErr : ""; }
bool        flexAudioPlaying(){ return auPlaying; }
uint8_t     flexAudioVolume(){ return auVol; }
bool        flexAudioMuted(){ return auMuted; }

static void auSavePrefs(){
  if(!auPrefs.begin("flexaudio", false)) return;
  auPrefs.putUChar("vol", auVol);
  auPrefs.putBool("mute", auMuted);
  auPrefs.end();
}

void flexAudioSetVolume(uint8_t v){
  if(v > FLEXAUDIO_VOL_MAX) v = FLEXAUDIO_VOL_MAX;
  if(v == auVol) return;
  auVol = v;
  if(v > 0) auMuted = false;      // subir el volumen quita el silencio
  auApplyVolume();
  auSavePrefs();
}
void flexAudioSetMuted(bool m){
  if(m == auMuted) return;
  auMuted = m;
  auApplyVolume();
  auSavePrefs();
}

// -------------------------------------------------------------
//  Canal I2S
// -------------------------------------------------------------
static void auI2sRelease(){
  if(auTx){
    i2s_channel_disable(auTx);
    i2s_del_channel(auTx);
    auTx = NULL;
  }
  auI2sOk = false;
}

bool flexAudioStartPcm(uint32_t rate, uint16_t ch, uint16_t bits){
  if(!auCodecOk){ auErr = "Sin codec de audio"; return false; }
  if(ch < 1 || ch > 2) return false;
  if(bits != 8 && bits != 16) return false;
  if(rate < 8000 || rate > 96000) return false;

  flexAudioStop();

  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.dma_desc_num  = 6;
  cc.dma_frame_num = 240;          // ~5 ms por descriptor a 48 kHz
  cc.auto_clear    = true;         // al vaciarse, silencio y no la ultima muestra repetida
  if(i2s_new_channel(&cc, &auTx, NULL) != ESP_OK){
    auErr = "No se pudo crear el canal I2S";
    auTx = NULL;
    return false;
  }

  i2s_std_config_t sc = {};
  sc.clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(rate);
  sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                  bits == 8 ? I2S_DATA_BIT_WIDTH_8BIT : I2S_DATA_BIT_WIDTH_16BIT,
                  ch == 1 ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO);
  sc.gpio_cfg.mclk = (gpio_num_t)FLEXAUDIO_PIN_MCLK;
  sc.gpio_cfg.bclk = (gpio_num_t)FLEXAUDIO_PIN_BCLK;
  sc.gpio_cfg.ws   = (gpio_num_t)FLEXAUDIO_PIN_WS;
  sc.gpio_cfg.dout = (gpio_num_t)FLEXAUDIO_PIN_DOUT;
  sc.gpio_cfg.din  = (gpio_num_t)FLEXAUDIO_PIN_DIN;

  if(i2s_channel_init_std_mode(auTx, &sc) != ESP_OK){
    auErr = "No se pudo configurar la salida I2S";
    auI2sRelease();
    return false;
  }
  if(!esSetBits(bits)){ auI2sRelease(); auErr = "Formato no admitido"; return false; }
  if(i2s_channel_enable(auTx) != ESP_OK){
    auErr = "No se pudo arrancar la salida I2S";
    auI2sRelease();
    return false;
  }
  auI2sOk   = true;
  auRate    = rate; auCh = ch; auBits = bits;
  auPlaying = true;
  auApplyVolume();
  auAmp(true);
  auErr = "Listo";
  return true;
}

int flexAudioWrite(const void* data, size_t n){
  if(!auPlaying || !auTx || !data) return -1;
  size_t wrote = 0;
  // Espera CERO: si el DMA esta lleno se devuelve 0 y el llamante lo
  // reintenta en la siguiente vuelta. Bloquear aqui congelaria la
  // interfaz al ritmo de la tarjeta de sonido.
  esp_err_t r = i2s_channel_write(auTx, data, n, &wrote, 0);
  if(r != ESP_OK && r != ESP_ERR_TIMEOUT) return -1;
  return (int)wrote;
}

void flexAudioStop(){
  if(auTx) auAmp(false);           // primero calla el altavoz, luego suelta
  auPlaying = false;
  auI2sRelease();
  auRate = 0; auCh = auBits = 0;
}
