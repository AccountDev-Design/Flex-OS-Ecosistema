// #############################################################
// ##  FlexOS · DETECCION DE CAIDAS  ·  logica pura
// ##  ----------------------------------------------------------
// ##  Mismo criterio que FlexOS_Mem: aqui NO se lee hardware y NO se
// ##  dibuja. Este modulo recibe muestras ya medidas del BNO085 y
// ##  decide; medir es del driver y ensenar es de la interfaz. Por eso
// ##  se compila y se ejercita entero en el PC (tests/host/test_fall).
// ##
// ##  QUE ES Y QUE NO ES
// ##  ----------------------------------------------------------
// ##  Es un detector EXPERIMENTAL de un evento fisico: caida libre,
// ##  impacto y cambio de postura. NO es un dispositivo medico, no
// ##  avisa a nadie y no sustituye a nada. Su salida es un nivel de
// ##  CONFIANZA, no un diagnostico.
// ##
// ##  POR QUE NO BASTA "aceleracion > X"
// ##  ----------------------------------------------------------
// ##  Un golpe en la mesa, dejar el aparato de canto o agitarlo pasan
// ##  ese umbral todos los dias. Lo que distingue una caida es la
// ##  SECUENCIA en el tiempo:
// ##
// ##     movimiento normal
// ##            v
// ##     caida libre        (|a| cae muy por debajo de 1 g)
// ##            v
// ##     impacto            (pico de |a| muy por encima de 1 g)
// ##            v
// ##     giro brusco        (el giroscopio se dispara)
// ##            v
// ##     cambio de postura  (la orientacion final no es la de antes)
// ##            v
// ##     estabilizacion     (se queda quieto)
// ##            v
// ##     evaluacion -> Confidence Score -> ¿supera el umbral?
// ##
// ##  Cada tramo suma o resta puntos. Ninguno decide por si solo.
// #############################################################
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// -------------------------------------------------------------
//  1) LA MUESTRA
//  ------------------------------------------------------------
//  Lo que el driver entrega, en unidades del SI. `haveQuat` a 0
//  significa que no hay sensor fusion en esa muestra: entonces el
//  detector NO puntua el cambio de orientacion, en vez de inventarselo.
// -------------------------------------------------------------
typedef struct {
  uint32_t tMs;
  float    ax, ay, az;        // m/s^2, CON gravedad
  float    gx, gy, gz;        // rad/s
  float    qi, qj, qk, qr;    // cuaternion unitario del rotation vector
  uint8_t  haveQuat;
} FlexFallSample;

// -------------------------------------------------------------
//  2) LOS PARAMETROS, EN UN SOLO SITIO
//  ------------------------------------------------------------
//  Estan centralizados a proposito: son los numeros que habra que
//  calibrar contra el modulo real, y no pueden estar repartidos por el
//  codigo. Los valores de fabrica (flexFallDefaults) salen de lo que
//  el BNO085 puede medir de verdad -- su acelerometro llega a +-8 g en
//  el rango por defecto, asi que ningun umbral se pone por encima de
//  eso -- y de la fisica del evento: una caida desde el bolsillo o la
//  mano da entre 80 y 400 ms de caida libre.
//
//  NO son valores definitivos: son el punto de partida documentado.
// -------------------------------------------------------------
typedef struct {
  float    freeFallG;      // |a| por debajo de esto = candidato a caida libre
  uint16_t freeFallMinMs;  // ...y tiene que durar al menos esto
  uint16_t freeFallMaxMs;  // por encima ya no es una caida, es descenso libre
  float    impactG;        // pico de |a| que cuenta como impacto
  float    impactBigG;     // impacto fuerte (puntua mas)
  float    rotRad;         // rad/s que cuentan como giro brusco
  float    motionG;        // |,|a|-1g| por encima de esto = hay movimiento
  uint16_t impactWinMs;    // plazo para ver el impacto tras la caida libre
  uint16_t settleMs;       // tiempo quieto que se exige despues del impacto
  float    settleTolG;     // cuanto puede alejarse de 1 g estando "quieto"
  float    settleRotRad;   // ...y cuanto puede girar
  uint16_t evalMs;         // tope de la ventana de evaluacion completa
  float    orientDeg;      // cambio de postura minimo para puntuar
  uint8_t  threshold;      // confianza minima para declarar una caida
} FlexFallParams;

void flexFallDefaults(FlexFallParams* p);

// -------------------------------------------------------------
//  3) LOS ESTADOS
// -------------------------------------------------------------
enum {
  FLEXFALL_IDLE = 0,       // quieto
  FLEXFALL_MOTION,         // se mueve, nada raro
  FLEXFALL_FREEFALL,       // |a| por los suelos
  FLEXFALL_IMPACT,         // pico detectado
  FLEXFALL_POST,           // despues del impacto, midiendo si se estabiliza
  FLEXFALL_EVAL            // ventana cerrada: se emite el veredicto
};

// Motivos que sumaron. Salen en el detalle del evento y en el
// historial: sin esto, un "78 de confianza" no se puede revisar.
#define FLEXFALL_R_FREEFALL  0x01
#define FLEXFALL_R_IMPACT    0x02
#define FLEXFALL_R_BIGIMPACT 0x04
#define FLEXFALL_R_ROTATION  0x08
#define FLEXFALL_R_ORIENT    0x10
#define FLEXFALL_R_SETTLED   0x20
#define FLEXFALL_R_WALKING   0x40   // penalizacion: cadencia de pasos antes del pico

// -------------------------------------------------------------
//  4) EL EVENTO
//  ------------------------------------------------------------
//  Lo que sale de una evaluacion, tanto si supera el umbral como si
//  no. Guardar tambien los descartados es lo que permite calibrar sin
//  adivinar.
// -------------------------------------------------------------
typedef struct {
  uint32_t tMs;            // instante del impacto
  uint8_t  confidence;     // 0..100
  uint8_t  reasons;        // FLEXFALL_R_*
  uint8_t  fall;           // 1 = supero el umbral
  float    peakG;          // pico de |a| en g
  float    minG;           // minimo de |a| en g (caida libre)
  uint16_t freeFallMs;
  float    rotPeak;        // rad/s
  float    orientDeg;      // cambio de postura, grados
  uint8_t  settled;        // se quedo quieto despues
} FlexFallEvent;

// -------------------------------------------------------------
//  5) EL DETECTOR
//  ------------------------------------------------------------
//  Estado opaco de tamano fijo: nada de reservas dinamicas. Vive
//  donde lo ponga quien lo use (aqui, en un static del modulo de
//  Device Care).
// -------------------------------------------------------------
#define FLEXFALL_HIST 16     // picos recientes para la cadencia de pasos

typedef struct {
  FlexFallParams p;
  uint8_t  state;
  uint32_t stateMs;         // entrada en el estado actual
  uint32_t lastMs;          // ultima muestra
  uint8_t  started;         // ya hay al menos una muestra

  // Acumuladores del evento en curso
  float    peakG, minG;
  float    rotPeak;
  uint32_t ffStartMs;
  uint32_t ffEndMs;         // final del vuelo: abre la ventana de impacto
  uint16_t ffMs;
  uint32_t impactMs;
  float    q0[4];           // postura ANTES del evento
  uint8_t  haveQ0;
  float    settleAccum;     // ms acumulados de quietud
  uint32_t lastSettleMs;

  // Ventana corta de picos, para descartar la cadencia de caminar
  uint32_t stepMs[FLEXFALL_HIST];
  uint8_t  stepN, stepHead;

  // Postura de referencia mientras esta en reposo
  float    qRest[4];
  uint8_t  haveRest;

  uint32_t nEvents, nFalls;
  FlexFallEvent last;       // ultimo evento evaluado (caida o no)
  uint8_t  lastValid;
} FlexFallDet;

// Deja el detector en IDLE con los parametros de fabrica.
void flexFallInit(FlexFallDet* d);
// Cambia los parametros sin perder el estado (calibracion en caliente).
void flexFallSetParams(FlexFallDet* d, const FlexFallParams* p);
// Vuelve a IDLE descartando el evento en curso. Se llama cuando el
// sensor se pierde: un evento a medias no puede sobrevivir a eso.
void flexFallReset(FlexFallDet* d);

// Alimenta UNA muestra. Devuelve true SOLO en la muestra en la que se
// cierra una evaluacion; entonces `out` (si no es NULL) trae el evento
// completo, tanto si supero el umbral (out->fall = 1) como si no.
bool flexFallFeed(FlexFallDet* d, const FlexFallSample* s, FlexFallEvent* out);

// Consulta
int         flexFallState(const FlexFallDet* d);
const char* flexFallStateName(int state, int lang);   // lang: 0 ES, 1 EN, 2 FR, 3 PT, 4 IT
uint32_t    flexFallEventCount(const FlexFallDet* d);
uint32_t    flexFallFallCount(const FlexFallDet* d);
// Ultimo evento evaluado. false si todavia no ha habido ninguno.
bool        flexFallLast(const FlexFallDet* d, FlexFallEvent* out);

// Utilidad publica porque la usan tanto el detector como la interfaz
// (la pantalla del IMU ensena |a| en g mientras monitoriza).
float       flexFallMagG(float x, float y, float z);
// Angulo entre dos cuaterniones unitarios, en grados. 0 si alguno no
// es unitario: se prefiere no puntuar a puntuar con basura.
float       flexFallQuatAngleDeg(const float* qa, const float* qb);
