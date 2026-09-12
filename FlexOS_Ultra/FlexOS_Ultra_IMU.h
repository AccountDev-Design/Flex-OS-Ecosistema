// #############################################################
// ##  FLEX OS ULTRA  ·  FLEX IMU SERVICE
// ##  ----------------------------------------------------------
// ##  Capa FINA sobre el driver que ya existe (FlexOS_BNO085.cpp).
// ##  Aqui NO hay un segundo driver, ni un segundo bus, ni una
// ##  segunda forma de hablar con el sensor: solo dos cosas que el
// ##  driver no puede resolver por si solo porque no son suyas.
// ##
// ##      FlexOS_BNO085.cpp   (SHTP / SH-2 sobre el Wire del tactil)
// ##               |
// ##      Flex IMU Service    <-- este archivo
// ##               |
// ##      +--------+-----------------+------------------+
// ##      |                          |                  |
// ##  Flex Motion Engine       Flex Compass       (futuros)
// ##  (una muestra por          (brujula)
// ##   informe, repartida)
// ##      |
// ##      +--------+------------------------+
// ##      |                                 |
// ##  Flex Device Care              Proteccion contra robo
// ##  (deteccion de caidas)         (patron de posible arrebato)
// ##
// ##  El Flex Motion Engine esta al FINAL de este mismo archivo: es la
// ##  tercera cosa que el driver no puede resolver solo -- repartir la
// ##  MISMA muestra entre varios clasificadores.
// ##
// ##  1) QUIEN MANDA SOBRE EL SENSOR
// ##  ----------------------------------------------------------
// ##  El driver tiene flexBnoBegin() / flexBnoStop(), que son
// ##  absolutos: el ultimo que llama gana. Con UN solo consumidor eso
// ##  bastaba. Con dos, salir de Flex Compass apagaria el sensor por
// ##  debajo de la deteccion de caidas -- que es justo lo que no puede
// ##  pasar en una funcion de seguridad.
// ##
// ##  El servicio lleva la cuenta:
// ##    · imuAcquire()  0 -> 1  : flexBnoBegin()  (arranca de verdad)
// ##    · imuAcquire()  n -> n+1: nada, ya esta encendido
// ##    · imuRelease()  n -> n-1: nada mientras quede alguien
// ##    · imuRelease()  1 -> 0  : flexBnoStop()   (apaga los informes)
// ##  Y imuServiceTick() llama a flexBnoTick() exactamente cuando hay
// ##  alguien escuchando, desde loop(), en el MISMO hilo que el tactil
// ##  -- que es la regla de oro del bus compartido con el GT911.
// ##
// ##  2) ORIENTACION, NO SOLO CUATERNION
// ##  ----------------------------------------------------------
// ##  El driver entrega el cuaternion crudo. El rumbo de brujula, el
// ##  cabeceo, el alabeo y la rosa de 16 direcciones son CONVERSIONES
// ##  -- no medidas nuevas -- y viven aqui para que cualquier
// ##  consumidor futuro obtenga exactamente el mismo numero. Son
// ##  funciones puras: se prueban enteras en el PC.
// ##
// ##  LO QUE ESTE ARCHIVO NO HACE
// ##  ----------------------------------------------------------
// ##  No inventa datos. Cada lectura devuelve false si el sensor no ha
// ##  entregado ese informe, y quien dibuja tiene que decir "no
// ##  disponible" en vez de ensenar un cero que parece una medida.
// ##
// ##  COMO ENCAJA ESTE ARCHIVO
// ##  ----------------------------------------------------------
// ##  Es una PARTE del sketch FlexOS_Ultra.ino, no una unidad de
// ##  traduccion independiente. Se incluye en la cadena de modulos, en
// ##  su sitio; no lo incluyas por tu cuenta desde otro lado.
// #############################################################
#pragma once
#include "FlexOS_Ultra_Vault.h"     // eslabon anterior de la cadena
#include "FlexOS_BNO085.h"          // EL driver del IMU (unidad de traduccion aparte)

// #############################################################
// ##  ESTADO PUBLICO
// ##  ----------------------------------------------------------
// ##  Traduce los estados del driver a lo unico que una interfaz
// ##  necesita para decidir que pantalla ensena. Son EXCLUYENTES: no
// ##  se mezclan "detectando" con "sin IMU", ni "desconectada" con
// ##  "nunca hubo".
// #############################################################
enum FlexImuState : uint8_t {
  FIMU_IDLE = 0,      // nadie ha adquirido el servicio: el sensor esta apagado
  FIMU_DETECTING,     // sondeando / identificando / activando informes
  FIMU_NO_IMU,        // el sondeo termino: no hay ninguna IMU compatible en el bus
  FIMU_CONNECTED,     // BNO085 identificado, pero el sensor fusion aun no publica
  FIMU_AHRS_ACTIVE,   // llegan vectores de rotacion: hay orientacion real
  FIMU_ERROR,         // sin bus I2C: no hay ni donde sondear
  FIMU_DISCONNECTED   // estuvo listo y dejo de responder
};

static int      imuRefs      = 0;    // consumidores vivos
static uint32_t imuRetryMs   = 0;    // ultimo re-sondeo pedido por un consumidor

// Flex Motion Engine: se define al final del archivo (necesita
// FLEXBNO_REPORT_HZ y las lecturas del driver ya declaradas) y lo llama
// imuServiceTick, que esta antes. Mismo patron de prototipo previo que usa
// el resto del sketch.
static void motionTick(uint32_t now);

// ---- Conteo de consumidores -----------------------------------------------
static void imuAcquire(){
  if(imuRefs < 0) imuRefs = 0;
  imuRefs++;
  if(imuRefs == 1){
    if(!gtOk) return;                // sin bus inicializado no hay nada que sondear
    flexBnoBegin();
    imuRetryMs = millis();
  }
}
static void imuRelease(){
  if(imuRefs > 0) imuRefs--;
  if(imuRefs == 0) flexBnoStop();    // apaga los informes: nada queda emitiendo para nadie
}
static inline int imuHolders(){ return imuRefs; }

// ---- Tick del servicio ----------------------------------------------------
// Se llama desde loop(), en el MISMO contexto que flexPollTouch() y que la
// deteccion I2C incremental. Sin consumidores sale en la primera linea: una
// placa con las dos apps cerradas no paga ni una transaccion.
static void imuServiceTick(){
  if(imuRefs <= 0) return;
  uint32_t now = millis();
  flexBnoTick(now);
  // Y justo despues, en la MISMA vuelta y el MISMO hilo, el Flex Motion
  // Engine publica la muestra (ver el bloque del final del archivo). Asi
  // Device Care y Proteccion contra robo consumen la misma lectura del
  // mismo instante en vez de sondear el driver cada uno por su cuenta.
  motionTick(now);
}

// RE-SONDEO A PETICION. El driver NO reintenta solo desde ABSENT/LOST, y es a
// proposito: un cable suelto no puede convertir el bucle del sistema en un
// sondeo I2C permanente. Quien tenga una pantalla delante del usuario SI puede
// pedirlo, y solo se hace efectivo cuando no hay nada que romper -- con el
// sensor listo o configurandose, esta llamada no toca nada.
static bool imuRetry(uint32_t minGapMs){
  if(imuRefs <= 0 || !gtOk) return false;
  int st = flexBnoState();
  if(st != FLEXBNO_ST_ABSENT && st != FLEXBNO_ST_LOST) return false;
  uint32_t now = millis();
  if(imuRetryMs && (now - imuRetryMs) < minGapMs) return false;
  imuRetryMs = now;
  flexBnoRescan();
  return true;
}

// ---- Estado traducido -----------------------------------------------------
static uint8_t imuState(){
  if(imuRefs <= 0) return FIMU_IDLE;
  if(!gtOk)        return FIMU_ERROR;
  switch(flexBnoState()){
    case FLEXBNO_ST_PROBING:
    case FLEXBNO_ST_IDENT:
    case FLEXBNO_ST_CONFIG: return FIMU_DETECTING;
    case FLEXBNO_ST_READY:
      return (flexBnoChecks() & FLEXBNO_CHK_FUSION) ? FIMU_AHRS_ACTIVE : FIMU_CONNECTED;
    case FLEXBNO_ST_LOST:   return FIMU_DISCONNECTED;
    default:                return FIMU_NO_IMU;
  }
}
static inline bool imuConnected(){  uint8_t s = imuState(); return s == FIMU_CONNECTED || s == FIMU_AHRS_ACTIVE; }
static inline bool imuAhrsActive(){ return imuState() == FIMU_AHRS_ACTIVE; }
// Un sensor cuenta como DISPONIBLE solo si el enlace esta listo Y ese informe
// concreto ha llegado. Ni un tick se dibuja sin eso detras.
static bool imuSrcLive(uint8_t chk){
  return flexBnoAvailable() && (flexBnoChecks() & chk) != 0;
}

// #############################################################
// ##  CONVERSIONES PURAS  (sin hardware: se prueban en el PC)
// #############################################################

// RUMBO A PARTIR DEL CUATERNION.
//
// El vector de rotacion del BNO085 lleva el cuaternion que pasa del sistema
// del sensor al de la Tierra, con los ejes Este-Norte-Arriba (ENU). Con
// q = (i, j, k, real) = (x, y, z, w), la primera columna de la matriz de
// rotacion mundo<-sensor es el eje +X del sensor expresado en el mundo:
//
//     Este  = 1 - 2*(y*y + z*z)
//     Norte = 2*(x*y + w*z)
//
// El rumbo de brujula es el angulo de ese eje medido EN SENTIDO HORARIO desde
// el Norte, o sea atan2(Este, Norte). Comprobacion: con el cuaternion
// identidad el eje +X apunta al Este y la formula da 90 grados, que es
// exactamente lo que tiene que dar.
//
// DE QUE DEPENDE. El "delante" es el eje +X del modulo, que es el que llevan
// serigrafiado los GY-BNO085. Si tu montaje apunta con otro eje, lo unico que
// hay que cambiar esta AQUI: es una sola expresion.
static float imuHeadingFromQuat(float x, float y, float z, float w){
  float east  = 1.0f - 2.0f * (y * y + z * z);
  float north = 2.0f * (x * y + w * z);
  float h = atan2f(east, north) * 57.2957795f;
  if(h < 0.0f)    h += 360.0f;
  if(h >= 360.0f) h -= 360.0f;
  return h;
}
// ELEVACION del eje +X del sensor sobre la horizontal: la componente "Arriba"
// de ese mismo eje (tercera fila, primera columna de la matriz).
static float imuPitchFromQuat(float x, float y, float z, float w){
  float up = 2.0f * (x * z - w * y);
  if(up >  1.0f) up =  1.0f;
  if(up < -1.0f) up = -1.0f;
  return asinf(up) * 57.2957795f;
}
// GIRO sobre el propio eje +X, con las componentes "Arriba" de los ejes +Y
// y +Z del sensor. Con el modulo plano y boca arriba da 0.
static float imuRollFromQuat(float x, float y, float z, float w){
  float upY = 2.0f * (y * z + w * x);
  float upZ = 1.0f - 2.0f * (x * x + y * y);
  return atan2f(upY, upZ) * 57.2957795f;
}

// Normaliza a [0,360). Acepta cualquier entrada finita; un NaN no se propaga
// a la interfaz.
static float imuNorm360(float a){
  if(!(a == a)) return 0.0f;
  a = fmodf(a, 360.0f);
  if(a < 0.0f) a += 360.0f;
  return a;
}
// Diferencia angular MAS CORTA entre dos rumbos, en (-180,180]. Es la pieza
// que resuelve el 359 -> 0 sin dar una vuelta entera al reves.
static float imuAngleDelta(float from, float to){
  float d = to - from;
  while(d >   180.0f) d -= 360.0f;
  while(d <= -180.0f) d += 360.0f;
  return d;
}

// ---- Lecturas de alto nivel (false = ese dato NO existe ahora mismo) ----
static bool imuHeading(float* outDeg){
  float q[4];
  if(!flexBnoQuat(q)) return false;
  if(outDeg) *outDeg = imuHeadingFromQuat(q[0], q[1], q[2], q[3]);
  return true;
}
static bool imuPitchRoll(float* pitchDeg, float* rollDeg){
  float q[4];
  if(!flexBnoQuat(q)) return false;
  if(pitchDeg) *pitchDeg = imuPitchFromQuat(q[0], q[1], q[2], q[3]);
  if(rollDeg)  *rollDeg  = imuRollFromQuat(q[0], q[1], q[2], q[3]);
  return true;
}

// ---- Rosa de 16 rumbos ----
static const char* IMU_DIR_SHORT[16] = {
  "N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSO","SO","OSO","O","ONO","NO","NNO"
};
static const char* IMU_DIR_LONG[16] = {
  "Norte","Norte-Noreste","Noreste","Este-Noreste",
  "Este","Este-Sureste","Sureste","Sur-Sureste",
  "Sur","Sur-Suroeste","Suroeste","Oeste-Suroeste",
  "Oeste","Oeste-Noroeste","Noroeste","Norte-Noroeste"
};
// Sector de 22,5 grados CENTRADO en cada rumbo: por eso el +11,25 antes de
// dividir. 0 cae en N, 45 en NE, 347,2 en NNO.
static int imuDirIndex(float heading){
  float h = imuNorm360(heading);
  int i = (int)((h + 11.25f) / 22.5f);
  return i & 15;
}
static inline const char* imuDirShort(float heading){ return IMU_DIR_SHORT[imuDirIndex(heading)]; }
static inline const char* imuDirLong(float heading){  return IMU_DIR_LONG[imuDirIndex(heading)];  }

// #############################################################
// ##  FLEX MOTION ENGINE
// ##  ----------------------------------------------------------
// ##  UNA sola lectura del sensor por periodo de informe, repartida a
// ##  todos los que la necesiten.
// ##
// ##      FlexOS_BNO085.cpp      (SHTP / SH-2 sobre el Wire del tactil)
// ##               |
// ##      Flex IMU Service       (quien manda sobre el sensor: conteo
// ##               |              de consumidores + orientacion)
// ##      Flex Motion Engine     <-- este bloque
// ##               |
// ##      +--------+--------------------+
// ##      |                             |
// ##  Flex Device Care          Proteccion contra robo
// ##  (¿hubo una caida?)        (¿patron de posible arrebato?)
// ##
// ##  POR QUE HACE FALTA
// ##  ----------------------------------------------------------
// ##  Con UN consumidor bastaba con que Device Care llamase a
// ##  flexBnoAccel/Gyro/Quat y se marcase su propio ritmo. Con DOS,
// ##  cada uno tendria su propia cadencia, su propio "¿ha llegado ya
// ##  un informe nuevo?" y su propia idea de que muestra acaba de
// ##  medirse. Dos verdades sobre el mismo instante es exactamente lo
// ##  que no puede haber cuando uno de los dos decide si bloquear el
// ##  aparato.
// ##
// ##  El motor publica UNA muestra por periodo del sensor, con un
// ##  numero de secuencia. Quien consume pregunta "¿hay una nueva
// ##  desde la que yo vi?" y recibe exactamente la misma que el otro.
// ##
// ##  LO QUE NO HACE
// ##  ----------------------------------------------------------
// ##  No habla con el bus (eso es del driver, y solo desde el hilo del
// ##  tactil), no clasifica nada y no inventa datos: cada campo trae su
// ##  bandera `have*` y vale false mientras ese informe no haya
// ##  llegado. Un consumidor que necesite giroscopio y no lo tenga
// ##  tiene que decirlo, no rellenar con ceros.
// ##
// ##  COSTE: una struct de 60 bytes y una comparacion por vuelta. Sin
// ##  consumidores del servicio, motionTick() sale en su primera linea.
// #############################################################
typedef struct {
  uint32_t tMs;
  float    a[3];        // m/s^2, CON gravedad
  float    g[3];        // rad/s
  float    q[4];        // cuaternion (i, j, k, real)
  uint8_t  haveA, haveG, haveQ;
} FlexMotionSample;

static FlexMotionSample gMotion;
static uint32_t         gMotionSeq = 0;    // 0 = todavia no hay ninguna muestra
static uint32_t         gMotionMs  = 0;    // millis de la ultima publicada

// Publica como mucho una muestra por periodo del sensor. La llama
// imuServiceTick(), justo despues del tick del driver y en el MISMO
// hilo: aqui no hay concurrencia que proteger porque no hay dos hilos.
static void motionTick(uint32_t now){
  if(imuRefs <= 0) return;
  if(!flexBnoAvailable()){
    // Sensor perdido: se deja de publicar. NO se repite la ultima
    // muestra -- alimentar a un clasificador con la misma lectura una y
    // otra vez le inventaria una quietud que nadie ha medido.
    return;
  }
  if(gMotionSeq && (now - gMotionMs) < (1000u / FLEXBNO_REPORT_HZ)) return;
  float a[3], g[3], q[4];
  bool ha = flexBnoAccel(a);
  if(!ha) return;                    // sin acelerometro no hay muestra que publicar
  bool hg = flexBnoGyro(g);
  bool hq = flexBnoQuat(q);
  gMotion.tMs   = now;
  gMotion.a[0] = a[0]; gMotion.a[1] = a[1]; gMotion.a[2] = a[2];
  gMotion.haveA = 1;
  gMotion.haveG = hg ? 1 : 0;
  gMotion.haveQ = hq ? 1 : 0;
  if(hg){ gMotion.g[0] = g[0]; gMotion.g[1] = g[1]; gMotion.g[2] = g[2]; }
  else  { gMotion.g[0] = gMotion.g[1] = gMotion.g[2] = 0.0f; }
  if(hq){ gMotion.q[0] = q[0]; gMotion.q[1] = q[1]; gMotion.q[2] = q[2]; gMotion.q[3] = q[3]; }
  else  { gMotion.q[0] = gMotion.q[1] = gMotion.q[2] = 0.0f; gMotion.q[3] = 1.0f; }
  gMotionMs = now;
  gMotionSeq++;
  if(!gMotionSeq) gMotionSeq = 1;    // el desbordamiento no puede parecer "sin muestras"
}

// Toma la muestra siguiente a la que ya vio el consumidor. Devuelve NULL
// cuando no hay ninguna nueva: quien llama no repite trabajo ni vuelve a
// alimentar su clasificador con lo mismo.
static const FlexMotionSample* motionTake(uint32_t* seen){
  if(!gMotionSeq) return NULL;
  if(seen && *seen == gMotionSeq) return NULL;
  if(seen) *seen = gMotionSeq;
  return &gMotion;
}
static inline uint32_t motionSeqNo(){ return gMotionSeq; }
// Edad de la ultima muestra publicada. 0xFFFFFFFF si no hay ninguna: se
// usa para decidir "el sensor dejo de entregar", nunca para dibujar.
static inline uint32_t motionAge(uint32_t now){
  return gMotionSeq ? (now - gMotionMs) : 0xFFFFFFFFu;
}
