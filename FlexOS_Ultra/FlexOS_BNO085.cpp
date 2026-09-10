// #############################################################
// ##  FlexOS · IMU · BNO085 sobre SHTP/SH-2  (implementacion ESP32-P4)
// ##  ----------------------------------------------------------
// ##  SHTP es el transporte: una cabecera de 4 bytes (longitud de 15
// ##  bits -- el bit 15 marca continuacion --, canal y numero de
// ##  secuencia) delante de cada paquete. SH-2 es lo que viaja por el
// ##  canal 2 (control) y por el canal 3 (informes de entrada).
// ##
// ##  De todo el protocolo aqui solo se implementa lo que hace falta:
// ##    · 0xF9 Product ID request  -> 0xF1 Product ID response
// ##    · 0xFD Set Feature Command (activar un informe con su periodo)
// ##    · informes 0x01 acelerometro, 0x02 giroscopio calibrado,
// ##      0x03 campo magnetico calibrado y 0x05 rotation vector
// ##    · reset por el canal 1 (ejecutable), payload 0x01
// ##  Nada mas. Ni calibracion dinamica, ni tara, ni FRS: no se usan,
// ##  asi que no se escriben.
// #############################################################

#include "FlexOS_BNO085.h"
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <string.h>

// -------------------------------------------------------------
//  SHTP / SH-2
// -------------------------------------------------------------
#define SHTP_CH_COMMAND     0
#define SHTP_CH_EXECUTABLE  1
#define SHTP_CH_CONTROL     2
#define SHTP_CH_REPORTS     3
#define SHTP_CH_WAKE        4
#define SHTP_CH_GYRO_RV     5
#define SHTP_CH_N           6

#define SH2_CMD_PRODUCT_ID_REQ   0xF9
#define SH2_RSP_PRODUCT_ID       0xF1
#define SH2_CMD_SET_FEATURE      0xFD
#define SH2_BASE_TIMESTAMP       0xFB
#define SH2_TIMESTAMP_REBASE     0xFA

#define SH2_RPT_ACCEL            0x01
#define SH2_RPT_GYRO             0x02
#define SH2_RPT_MAG              0x03
#define SH2_RPT_ROTVEC           0x05

// El paquete mas grande que interesa leer entero es el de informes de
// entrada: cabecera de tiempo (5 B) + cuatro informes de 10..14 B.
// El anuncio inicial del sensor es mucho mayor (~270 B) y se descarta
// leyendolo por trozos sin guardarlo, que es justo para lo que sirve
// que bnoGetData() acote lo que copia.
#define BNO_BUF        128
// Trozo de lectura I2C. El driver de Arduino-ESP32 admite mas, pero 32
// es el tamano seguro en cualquier core y no cambia el resultado: solo
// el numero de transacciones de un paquete grande.
#define BNO_I2C_CHUNK  32

// Cuantos paquetes se leen como mucho por vuelta de loop(). El bus es
// el del tactil: aqui no se hace un bucle "hasta que no quede nada".
#define BNO_PKT_PER_TICK 3

// Plazos del dialogo de arranque, en ms. Si uno vence, el modulo lo
// dice con un error real en vez de quedarse esperando para siempre.
#define BNO_RESET_MS   120
#define BNO_IDENT_MS   700
#define BNO_CONFIG_MS  700
// Sin un solo informe en este tiempo, un sensor que estaba READY pasa a
// LOST: la deteccion de caidas se apaga y la interfaz lo dice.
#define BNO_STALE_MS   1500

// -------------------------------------------------------------
//  Estado del modulo
// -------------------------------------------------------------
static uint8_t     bnoAddr    = 0;
static int         bnoSt      = FLEXBNO_ST_ABSENT;
static const char* bnoErr     = "Sin inicializar";
static uint8_t     bnoChk     = 0;
static uint32_t    bnoStepMs  = 0;      // entrada en el estado actual
static uint32_t    bnoLastMs  = 0;      // ultimo informe recibido
static uint32_t    bnoNRep    = 0;
static uint8_t     bnoSeq[SHTP_CH_N];
static uint8_t     bnoBuf[BNO_BUF];
static uint8_t     bnoFeatIdx = 0;      // cual de los cuatro informes toca activar

static uint8_t     bnoSwMaj = 0, bnoSwMin = 0;
static uint32_t    bnoSwPart = 0;

static float   bnoA[3] = {0,0,0}, bnoG[3] = {0,0,0}, bnoM[3] = {0,0,0};
static float   bnoQ[4] = {0,0,0,1};
static uint8_t bnoAAcc = 0xFF, bnoGAcc = 0xFF, bnoMAcc = 0xFF, bnoFAcc = 0xFF;

// Los cuatro informes que se activan, en orden. Uno por vuelta de
// CONFIG: escribir cuatro comandos de 17 bytes seguidos monopolizaria
// el bus del tactil durante toda la secuencia.
static const uint8_t BNO_FEATURES[4] = { SH2_RPT_ACCEL, SH2_RPT_GYRO, SH2_RPT_MAG, SH2_RPT_ROTVEC };

// -------------------------------------------------------------
//  Transporte
// -------------------------------------------------------------
static bool bnoPing(uint8_t addr){
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// Envia un paquete SHTP por `ch`. `len` es la carga util (sin cabecera).
static bool bnoSend(uint8_t ch, const uint8_t* payload, uint8_t len){
  if(!bnoAddr) return false;
  uint16_t total = (uint16_t)len + 4;
  Wire.beginTransmission(bnoAddr);
  Wire.write((uint8_t)(total & 0xFF));
  Wire.write((uint8_t)((total >> 8) & 0xFF));   // bit 15 a 0: no hay continuacion
  Wire.write(ch);
  Wire.write(bnoSeq[ch]++);
  for(uint8_t i = 0; i < len; i++) Wire.write(payload[i]);
  return Wire.endTransmission() == 0;
}

// Lee el cuerpo de un paquete cuya cabecera ya se consumio. Cada trozo
// I2C repite la cabecera de 4 bytes (continuacion), asi que se salta.
// Lo que no cabe en bnoBuf se LEE igual y se tira: hay que vaciar el
// paquete del sensor o el siguiente llegaria desalineado.
static bool bnoGetData(uint16_t remaining, uint16_t* outLen){
  uint16_t spot = 0;
  while(remaining > 0){
    uint16_t want = remaining + 4;
    if(want > BNO_I2C_CHUNK) want = BNO_I2C_CHUNK;
    if((uint16_t)Wire.requestFrom((int)bnoAddr, (int)want) != want) return false;
    for(int i = 0; i < 4 && Wire.available(); i++) (void)Wire.read();
    uint16_t body = want - 4;
    for(uint16_t i = 0; i < body; i++){
      if(!Wire.available()) return false;
      uint8_t b = (uint8_t)Wire.read();
      if(spot < BNO_BUF) bnoBuf[spot] = b;
      spot++;
    }
    remaining -= body;
  }
  if(outLen) *outLen = (spot > BNO_BUF) ? BNO_BUF : spot;
  return true;
}

// Lee UN paquete. Devuelve el canal (0..5) y la longitud util en
// *len, o -1 si no habia nada / hubo un fallo de bus.
static int bnoRecv(uint16_t* len){
  if(!bnoAddr) return -1;
  if(Wire.requestFrom((int)bnoAddr, 4) != 4) return -1;
  uint8_t h0 = (uint8_t)Wire.read();
  uint8_t h1 = (uint8_t)Wire.read();
  uint8_t ch = (uint8_t)Wire.read();
  (void)Wire.read();                                  // numero de secuencia
  uint16_t total = ((uint16_t)(h1 & 0x7F) << 8) | h0; // bit 15 = continuacion
  if(total <= 4) return -1;                           // 0 = sin datos
  if(ch >= SHTP_CH_N) return -1;                      // canal imposible: bus sucio
  uint16_t body = total - 4;
  uint16_t got = 0;
  if(!bnoGetData(body, &got)) return -1;
  if(len) *len = got;
  return (int)ch;
}

// -------------------------------------------------------------
//  Conversion de punto fijo. El BNO085 entrega enteros de 16 bits con
//  un Q-point fijo por informe: el valor real es v * 2^-Q.
// -------------------------------------------------------------
static inline float bnoQFix(int16_t v, int q){ return (float)v * (1.0f / (float)(1u << q)); }
static inline int16_t bnoI16(const uint8_t* p){ return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }

// -------------------------------------------------------------
//  Comandos
// -------------------------------------------------------------
static bool bnoSoftReset(){
  uint8_t p = 0x01;                       // canal ejecutable: 1 = reset
  for(int i = 0; i < SHTP_CH_N; i++) bnoSeq[i] = 0;
  return bnoSend(SHTP_CH_EXECUTABLE, &p, 1);
}
static bool bnoAskProductId(){
  uint8_t p[2] = { SH2_CMD_PRODUCT_ID_REQ, 0x00 };
  return bnoSend(SHTP_CH_CONTROL, p, 2);
}
// Set Feature Command: 17 bytes. El unico campo que se toca es el
// periodo, en microsegundos; sensibilidad, lote y configuracion
// especifica van a cero porque no se usan.
static bool bnoSetFeature(uint8_t report, uint32_t usInterval){
  uint8_t p[17];
  memset(p, 0, sizeof(p));
  p[0] = SH2_CMD_SET_FEATURE;
  p[1] = report;
  p[5] = (uint8_t)( usInterval        & 0xFF);
  p[6] = (uint8_t)((usInterval >>  8) & 0xFF);
  p[7] = (uint8_t)((usInterval >> 16) & 0xFF);
  p[8] = (uint8_t)((usInterval >> 24) & 0xFF);
  return bnoSend(SHTP_CH_CONTROL, p, sizeof(p));
}

// -------------------------------------------------------------
//  Parseo de informes de entrada (canal 3)
//  ------------------------------------------------------------
//  La carga util empieza con una cabecera de tiempo (0xFB + delta de
//  4 bytes) y luego encadena informes. Cada uno: id, secuencia,
//  estado, retardo y sus datos. Se avanza informe a informe; un id
//  desconocido corta el recorrido -- sin su longitud no se puede
//  saltar, y seguir adivinando produciria medidas inventadas.
// -------------------------------------------------------------
static void bnoParseReports(uint16_t len, uint32_t nowMs){
  uint16_t i = 0;
  if(len >= 5 && (bnoBuf[0] == SH2_BASE_TIMESTAMP || bnoBuf[0] == SH2_TIMESTAMP_REBASE)) i = 5;
  while(i + 4 <= len){
    uint8_t id  = bnoBuf[i];
    uint8_t sta = bnoBuf[i + 2] & 0x03;      // precision declarada por el sensor
    const uint8_t* d = &bnoBuf[i + 4];
    uint16_t need = 0;
    switch(id){
      case SH2_RPT_ACCEL:  need = 10; break;   // 4 + 3 x int16
      case SH2_RPT_GYRO:   need = 10; break;
      case SH2_RPT_MAG:    need = 10; break;
      case SH2_RPT_ROTVEC: need = 14; break;   // 4 + 4 x int16 + precision
      default:             return;             // longitud desconocida: no se adivina
    }
    if(i + need > len) return;
    switch(id){
      case SH2_RPT_ACCEL:
        bnoA[0] = bnoQFix(bnoI16(d + 0), 8);   // Q8 -> m/s^2
        bnoA[1] = bnoQFix(bnoI16(d + 2), 8);
        bnoA[2] = bnoQFix(bnoI16(d + 4), 8);
        bnoAAcc = sta; bnoChk |= FLEXBNO_CHK_ACCEL;
        break;
      case SH2_RPT_GYRO:
        bnoG[0] = bnoQFix(bnoI16(d + 0), 9);   // Q9 -> rad/s
        bnoG[1] = bnoQFix(bnoI16(d + 2), 9);
        bnoG[2] = bnoQFix(bnoI16(d + 4), 9);
        bnoGAcc = sta; bnoChk |= FLEXBNO_CHK_GYRO;
        break;
      case SH2_RPT_MAG:
        bnoM[0] = bnoQFix(bnoI16(d + 0), 4);   // Q4 -> uT
        bnoM[1] = bnoQFix(bnoI16(d + 2), 4);
        bnoM[2] = bnoQFix(bnoI16(d + 4), 4);
        bnoMAcc = sta; bnoChk |= FLEXBNO_CHK_MAG;
        break;
      case SH2_RPT_ROTVEC:
        bnoQ[0] = bnoQFix(bnoI16(d + 0), 14);  // Q14, cuaternion unitario
        bnoQ[1] = bnoQFix(bnoI16(d + 2), 14);
        bnoQ[2] = bnoQFix(bnoI16(d + 4), 14);
        bnoQ[3] = bnoQFix(bnoI16(d + 6), 14);
        bnoFAcc = sta; bnoChk |= FLEXBNO_CHK_FUSION;
        break;
      default: break;
    }
    bnoNRep++;
    bnoLastMs = nowMs;
    i += need;
  }
}

// Reporte 0xF1: es la prueba de que el bus funciona en los DOS
// sentidos y de que al otro lado hay un BNO08x.
static void bnoParseControl(uint16_t len){
  if(len < 1) return;
  if(bnoBuf[0] != SH2_RSP_PRODUCT_ID) return;
  if(len >= 12){
    bnoSwMaj  = bnoBuf[2];
    bnoSwMin  = bnoBuf[3];
    bnoSwPart = (uint32_t)bnoBuf[4] | ((uint32_t)bnoBuf[5] << 8) |
                ((uint32_t)bnoBuf[6] << 16) | ((uint32_t)bnoBuf[7] << 24);
  }
  bnoChk |= FLEXBNO_CHK_I2C;
}

// -------------------------------------------------------------
//  Ciclo de vida
// -------------------------------------------------------------
static void bnoResetState(){
  bnoSt   = FLEXBNO_ST_ABSENT;
  bnoChk  = 0;
  bnoNRep = 0;
  bnoLastMs = 0;
  bnoFeatIdx = 0;
  bnoSwMaj = bnoSwMin = 0; bnoSwPart = 0;
  bnoAAcc = bnoGAcc = bnoMAcc = bnoFAcc = 0xFF;
  for(int i = 0; i < 3; i++){ bnoA[i] = 0; bnoG[i] = 0; bnoM[i] = 0; }
  bnoQ[0] = bnoQ[1] = bnoQ[2] = 0; bnoQ[3] = 1;
  for(int i = 0; i < SHTP_CH_N; i++) bnoSeq[i] = 0;
}

bool flexBnoBegin(){
  bnoResetState();
  bnoAddr = 0;
  if(bnoPing(FLEXBNO_ADDR_LOW))       bnoAddr = FLEXBNO_ADDR_LOW;
  else if(bnoPing(FLEXBNO_ADDR_HIGH)) bnoAddr = FLEXBNO_ADDR_HIGH;
  if(!bnoAddr){
    bnoErr = "No hay ningun modulo IMU en el bus I2C";
    return false;
  }
  bnoSoftReset();                       // no se comprueba: el reset corta el ACK
  bnoSt     = FLEXBNO_ST_PROBING;
  bnoStepMs = millis();
  bnoErr    = "Iniciando el modulo";
  return true;
}

void flexBnoRescan(){ flexBnoBegin(); }

void flexBnoStop(){
  if(bnoAddr && bnoSt >= FLEXBNO_ST_CONFIG){
    // Periodo 0 = informe apagado. Si el bus falla no pasa nada: el
    // sensor deja de leerse igualmente y su trafico no molesta a nadie
    // mas que a si mismo.
    for(uint8_t i = 0; i < 4; i++) bnoSetFeature(BNO_FEATURES[i], 0);
  }
  bnoResetState();
  bnoAddr = 0;
  bnoErr  = "Sensor detenido";
}

void flexBnoTick(uint32_t nowMs){
  if(bnoSt == FLEXBNO_ST_ABSENT) return;

  // Lectura acotada: como mucho BNO_PKT_PER_TICK paquetes por vuelta.
  for(int n = 0; n < BNO_PKT_PER_TICK; n++){
    uint16_t len = 0;
    int ch = bnoRecv(&len);
    if(ch < 0) break;
    if(ch == SHTP_CH_CONTROL)      bnoParseControl(len);
    else if(ch == SHTP_CH_REPORTS) bnoParseReports(len, nowMs);
    else if(ch == SHTP_CH_GYRO_RV) bnoParseReports(len, nowMs);
    // Canales 0/1/4: anuncio inicial y avisos del ejecutable. Se leen
    // (hay que vaciarlos) y se descartan: no se usan.
  }

  switch(bnoSt){
    case FLEXBNO_ST_PROBING:
      if(nowMs - bnoStepMs < BNO_RESET_MS) break;
      bnoAskProductId();
      bnoSt = FLEXBNO_ST_IDENT;
      bnoStepMs = nowMs;
      break;

    case FLEXBNO_ST_IDENT:
      if(bnoChk & FLEXBNO_CHK_I2C){
        bnoSt = FLEXBNO_ST_CONFIG;
        bnoStepMs = nowMs;
        bnoFeatIdx = 0;
        bnoErr = "Activando los sensores";
        break;
      }
      if(nowMs - bnoStepMs > BNO_IDENT_MS){
        // Contesta en el bus pero no habla SH-2: no es un BNO08x, o el
        // cableado deja pasar el ACK y no los datos. No se finge nada.
        bnoSt  = FLEXBNO_ST_ABSENT;
        bnoErr = "El modulo del bus no responde como un BNO085";
      }
      break;

    case FLEXBNO_ST_CONFIG:
      if(bnoFeatIdx < 4){
        // Un Set Feature por vuelta: el bus lo comparte el tactil.
        bnoSetFeature(BNO_FEATURES[bnoFeatIdx], 1000000UL / FLEXBNO_REPORT_HZ);
        bnoFeatIdx++;
        break;
      }
      if(bnoChk & (FLEXBNO_CHK_ACCEL | FLEXBNO_CHK_GYRO | FLEXBNO_CHK_MAG | FLEXBNO_CHK_FUSION)){
        bnoSt = FLEXBNO_ST_READY;
        bnoErr = "Listo";
        break;
      }
      if(nowMs - bnoStepMs > BNO_CONFIG_MS){
        bnoSt  = FLEXBNO_ST_LOST;
        bnoErr = "El sensor no entrega datos";
      }
      break;

    case FLEXBNO_ST_READY:
      if(bnoLastMs && nowMs - bnoLastMs > BNO_STALE_MS){
        bnoSt  = FLEXBNO_ST_LOST;
        bnoErr = "El sensor dejo de responder";
      }
      break;

    case FLEXBNO_ST_LOST:
      // No se reintenta solo en bucle: quien decide reintentar es la
      // interfaz (o el usuario). Asi un cable suelto no convierte el
      // bucle del sistema en un sondeo I2C permanente.
      break;

    default: break;
  }
}

// -------------------------------------------------------------
//  Consulta
// -------------------------------------------------------------
int         flexBnoState(){     return bnoSt; }
bool        flexBnoPresent(){   return bnoAddr != 0 && bnoSt != FLEXBNO_ST_ABSENT; }
bool        flexBnoAvailable(){ return bnoSt == FLEXBNO_ST_READY; }
uint8_t     flexBnoChecks(){    return bnoChk; }
uint8_t     flexBnoAddr(){      return bnoAddr; }
const char* flexBnoError(){     return bnoErr ? bnoErr : ""; }
uint8_t     flexBnoSwMajor(){   return bnoSwMaj; }
uint8_t     flexBnoSwMinor(){   return bnoSwMin; }
uint32_t    flexBnoSwPart(){    return bnoSwPart; }
uint32_t    flexBnoReportCount(){ return bnoNRep; }

bool flexBnoAccel(float* xyz){
  if(!(bnoChk & FLEXBNO_CHK_ACCEL) || !xyz) return false;
  xyz[0] = bnoA[0]; xyz[1] = bnoA[1]; xyz[2] = bnoA[2];
  return true;
}
bool flexBnoGyro(float* xyz){
  if(!(bnoChk & FLEXBNO_CHK_GYRO) || !xyz) return false;
  xyz[0] = bnoG[0]; xyz[1] = bnoG[1]; xyz[2] = bnoG[2];
  return true;
}
bool flexBnoMag(float* xyz){
  if(!(bnoChk & FLEXBNO_CHK_MAG) || !xyz) return false;
  xyz[0] = bnoM[0]; xyz[1] = bnoM[1]; xyz[2] = bnoM[2];
  return true;
}
bool flexBnoQuat(float* ijkr){
  if(!(bnoChk & FLEXBNO_CHK_FUSION) || !ijkr) return false;
  for(int i = 0; i < 4; i++) ijkr[i] = bnoQ[i];
  return true;
}
uint8_t flexBnoAccelAcc(){  return bnoAAcc; }
uint8_t flexBnoGyroAcc(){   return bnoGAcc; }
uint8_t flexBnoMagAcc(){    return bnoMAcc; }
uint8_t flexBnoFusionAcc(){ return bnoFAcc; }

uint32_t flexBnoLastReportAge(uint32_t nowMs){
  if(!bnoLastMs) return 0xFFFFFFFFu;
  return nowMs - bnoLastMs;
}

bool flexBnoEuler(float* rpy){
  if(!(bnoChk & FLEXBNO_CHK_FUSION) || !rpy) return false;
  float i = bnoQ[0], j = bnoQ[1], k = bnoQ[2], r = bnoQ[3];
  const float RAD = 57.29577951f;
  float sinr = 2.0f * (r * i + j * k);
  float cosr = 1.0f - 2.0f * (i * i + j * j);
  rpy[0] = atan2f(sinr, cosr) * RAD;
  float sinp = 2.0f * (r * j - k * i);
  if(sinp >  1.0f) sinp =  1.0f;
  if(sinp < -1.0f) sinp = -1.0f;
  rpy[1] = asinf(sinp) * RAD;
  float siny = 2.0f * (r * k + i * j);
  float cosy = 1.0f - 2.0f * (j * j + k * k);
  rpy[2] = atan2f(siny, cosy) * RAD;
  return true;
}
