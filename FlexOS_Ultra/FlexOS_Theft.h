// #############################################################
// ##  FlexOS · PROTECCION CONTRA ROBO  ·  logica pura
// ##  ----------------------------------------------------------
// ##  Mismo criterio que FlexOS_FallDetect y FlexOS_Mem: aqui NO se
// ##  lee hardware y NO se dibuja. Entran muestras ya medidas del
// ##  BNO085 (por el Flex Motion Engine) y sale un veredicto. Por eso
// ##  se compila y se ejercita entero en el PC
// ##  (tests/host/test_theft).
// ##
// ##  QUE ES Y QUE NO ES
// ##  ----------------------------------------------------------
// ##  Es un clasificador TEMPORAL de un patron de movimiento. Un IMU
// ##  mide aceleracion y giro: no sabe quien sujeta el aparato ni con
// ##  que intencion. Por eso su salida NUNCA se llama "robo", se llama
// ##  POSIBLE ARREBATO, y es lo unico que el sensor puede sostener.
// ##  La cifra que acompana al evento es una CONFIANZA, no una prueba.
// ##
// ##  POR QUE NO ES EL DETECTOR DE CAIDAS
// ##  ----------------------------------------------------------
// ##  Son dos preguntas distintas sobre la misma senal:
// ##
// ##     Flex Device Care          Proteccion contra robo
// ##     "¿hubo una caida?"        "¿hubo un patron compatible
// ##                                con un posible arrebato?"
// ##
// ##  Y las respuestas se apoyan en rasgos OPUESTOS:
// ##
// ##                      caida                 arrebato
// ##     antes            da igual              en la mano (micromovimiento)
// ##     caida libre      SI, la firma          NO (penaliza: es una caida)
// ##     tiron            no hace falta         SI, direccional y sostenido
// ##     direccion        irrelevante           COHERENTE (no oscila)
// ##     despues          se QUEDA QUIETO       SIGUE MOVIENDOSE (huida)
// ##
// ##  De ahi que una caida sola no pueda producir un arrebato ni al
// ##  reves: lo que suma en uno resta en el otro. Este modulo NO toca
// ##  ni una linea del detector de caidas; comparte con el la fuente
// ##  de datos (el Flex Motion Engine) y nada mas.
// ##
// ##  POR QUE NO BASTA "aceleracion > X"
// ##  ----------------------------------------------------------
// ##  Correr, agitar el aparato, dejarlo en la mesa o un golpe seco
// ##  pasan ese umbral a diario. Lo que distingue un arrebato es la
// ##  SECUENCIA en el tiempo y la COMBINACION de senales:
// ##
// ##     movimiento normal / en la mano
// ##            v
// ##     tiron direccional     (aceleracion LINEAL alta, derivada alta,
// ##            v               direccion que no se invierte)
// ##     giro acoplado         (se escapa de la mano, no es un giro solo)
// ##            v
// ##     separacion            (sigue moviendose: se lo llevan)
// ##            v
// ##     evaluacion -> confianza -> ¿supera el umbral?
// ##
// ##  Cada tramo suma o resta. Ninguno decide por si solo.
// #############################################################
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// -------------------------------------------------------------
//  1) LA MUESTRA
//  ------------------------------------------------------------
//  Lo que el Flex Motion Engine entrega, en unidades del SI. Las
//  banderas `have*` NO son decorativas: sin giroscopio no se puntua el
//  giro acoplado y sin cuaternion la gravedad se estima con un filtro
//  -- en vez de inventarse un dato que no se ha medido.
// -------------------------------------------------------------
typedef struct {
  uint32_t tMs;
  float    ax, ay, az;        // m/s^2, CON gravedad
  float    gx, gy, gz;        // rad/s
  float    qi, qj, qk, qr;    // cuaternion unitario del rotation vector
  uint8_t  haveGyro;
  uint8_t  haveQuat;
} FlexTheftSample;

// -------------------------------------------------------------
//  2) SENSIBILIDAD
//  ------------------------------------------------------------
//  NO es un umbral de aceleracion con tres valores. Cada nivel mueve
//  el clasificador ENTERO: cuanto tiron hace falta, cuanto tiene que
//  durar, cuanta coherencia de direccion se exige, cuanto movimiento
//  posterior, y con cuanta confianza se cierra el caso. "Alta" no es
//  "el mismo detector mas nervioso": es un detector que acepta
//  patrones mas cortos y menos limpios, y por eso pide menos confianza.
// -------------------------------------------------------------
enum {
  FLEXTHEFT_SENS_LOW = 0,     // exigente: casi nada lo dispara
  FLEXTHEFT_SENS_NORMAL,      // recomendado
  FLEXTHEFT_SENS_HIGH         // permisivo: patrones mas cortos cuentan
};

// -------------------------------------------------------------
//  3) LOS PARAMETROS, EN UN SOLO SITIO
//  ------------------------------------------------------------
//  Centralizados a proposito, igual que en el detector de caidas: son
//  los numeros que hay que calibrar contra el modulo real y no pueden
//  acabar repartidos por el codigo. Los de fabrica salen de lo que el
//  BNO085 puede medir de verdad (su acelerometro llega a +-8 g en el
//  rango por defecto, asi que ningun umbral se acerca a ese techo) y
//  de la fisica del gesto: un tiron humano dura entre 70 y 400 ms.
//
//  NO son definitivos: son el punto de partida documentado.
// -------------------------------------------------------------
typedef struct {
  // --- el tiron ---
  float    pullG;          // aceleracion LINEAL (sin gravedad) que define un tiron, en g
  float    jerkGs;         // derivada de esa aceleracion, en g/s
  uint16_t burstMinMs;     // menos que esto es un golpe, no un tiron
  uint16_t burstMaxMs;     // mas que esto es un desplazamiento, no un tiron
  float    dirCohMin;      // coherencia de direccion minima (0..1); agitar da poca
  float    twistRad;       // giro que cuenta como acoplado al tiron, rad/s
  // --- el contexto anterior ---
  float    heldLinG;       // micromovimiento tipico de la mano (media de |lineal|, g)
  float    restLinG;       // por debajo de esto estaba apoyado, no en la mano
  float    freeFallG;      // |a| por debajo de esto = caida libre (PENALIZA)
  uint16_t freeFallMinMs;  // ...y tiene que durar al menos esto
  // --- el contexto posterior ---
  uint16_t escapeWinMs;    // ventana que se observa despues del tiron
  float    escapeLinG;     // por encima de esto el aparato SIGUE moviendose
  uint16_t escapeNeedMs;   // cuanto de esa ventana tiene que estar en movimiento
  float    settleTolG;     // cuanto puede alejarse de 1 g estando "quieto"
  float    settleRotRad;   // ...y cuanto puede girar
  uint16_t settleMs;       // quietud acumulada que cuenta como "se quedo parado"
  float    impactG;        // pico de |a| que se anota como impacto posterior
  // --- decision ---
  uint8_t  threshold;      // confianza minima para declarar POSIBLE ARREBATO
  uint8_t  needEscape;     // 1 = sin movimiento posterior no se declara nunca
  uint16_t refractMs;      // tras un veredicto, no se abre otro caso en este plazo
} FlexTheftParams;

// Rellena `p` con los valores de fabrica del nivel de sensibilidad.
void flexTheftDefaults(FlexTheftParams* p, uint8_t sens);

// -------------------------------------------------------------
//  4) LOS ESTADOS
//  ------------------------------------------------------------
//  Son los del enunciado, sin estados ambiguos de por medio:
//     REST / CARRY  = normal      SUSPECT = movimiento sospechoso
//     CONFIRM       = ventana de separacion
//     EVAL          = veredicto (dura una muestra)
// -------------------------------------------------------------
enum {
  FLEXTHEFT_REST = 0,      // quieto: apoyado en una superficie
  FLEXTHEFT_CARRY,         // movimiento normal / en la mano
  FLEXTHEFT_SUSPECT,       // tiron direccional en curso
  FLEXTHEFT_CONFIRM,       // tiron cerrado: mirando que pasa despues
  FLEXTHEFT_EVAL           // ventana cerrada: se emite el veredicto
};

// Motivos que sumaron o restaron. Salen en el detalle del evento y en
// el historial: sin esto, un "82 de confianza" no se puede revisar.
#define FLEXTHEFT_R_HELD      0x0001   // estaba en la mano justo antes
#define FLEXTHEFT_R_PULL      0x0002   // intensidad del tiron
#define FLEXTHEFT_R_JERK      0x0004   // arranque brusco (derivada)
#define FLEXTHEFT_R_DIR       0x0008   // direccion coherente: tiron, no agitacion (CONDICION)
#define FLEXTHEFT_R_TWIST     0x0010   // giro acoplado al tiron
#define FLEXTHEFT_R_ESCAPE    0x0020   // siguio moviendose: separacion
#define FLEXTHEFT_R_IMPACT    0x0040   // hubo impacto despues (CONTEXTO: no puntua)
#define FLEXTHEFT_R_SHAKE     0x0080   // PENALIZA: la direccion se invirtio (agitar/correr)
#define FLEXTHEFT_R_WALK      0x0100   // PENALIZA: cadencia de pasos antes del pico
#define FLEXTHEFT_R_FREEFALL  0x0200   // PENALIZA: hubo caida libre antes -> es una caida
#define FLEXTHEFT_R_SETTLED   0x0400   // PENALIZA: se quedo quieto sin haberse movido
#define FLEXTHEFT_R_TURNONLY  0x0800   // PENALIZA: giro sin traslacion (ensenarselo a alguien)
#define FLEXTHEFT_R_TABLE     0x1000   // PENALIZA: estaba apoyado, no en la mano

// -------------------------------------------------------------
//  5) EL EVENTO
//  ------------------------------------------------------------
//  Lo que sale de una evaluacion, supere el umbral o no. Guardar
//  tambien los descartados es lo que permite calibrar sin adivinar.
//  `impactG`/`hadImpact` son CONTEXTO -- no puntuan -- y existen para
//  que el Event Manager pueda correlacionar "arrebato + caida" sin
//  volver a mirar el sensor.
// -------------------------------------------------------------
typedef struct {
  uint32_t tMs;            // instante del pico del tiron
  uint8_t  confidence;     // 0..100
  uint8_t  snatch;         // 1 = supero el umbral
  uint16_t reasons;        // FLEXTHEFT_R_*
  float    pullG;          // pico de aceleracion lineal, en g
  float    jerkGs;         // pico de la derivada, en g/s
  float    rotPeak;        // pico de giro durante el tiron, rad/s
  float    dirCoh;         // coherencia de direccion del tiron, 0..1
  uint16_t burstMs;        // cuanto duro el tiron
  uint16_t escapeMs;       // ms en movimiento dentro de la ventana posterior
  uint16_t quietMs;        // ms quieto dentro de la ventana posterior
  float    impactG;        // pico de |a| despues del tiron, en g (0 si no hubo)
  uint8_t  hadImpact;
  uint8_t  settled;
} FlexTheftEvent;

// -------------------------------------------------------------
//  6) EL BUFFER TEMPORAL
//  ------------------------------------------------------------
//  Una ventana circular en RAM con los ultimos segundos de
//  movimiento. Es lo que permite mirar ATRAS cuando algo pasa: que
//  estaba haciendo el aparato antes del tiron, si venia de una caida
//  libre, si la direccion ya venia invirtiendose.
//
//  NO hay reservas dinamicas: vive dentro del propio detector, que es
//  un static del modulo de interfaz. Y NO se guarda en almacenamiento
//  permanente: lo que se conserva es el EVENTO, no el flujo del IMU.
//
//  Coste: 128 ranuras de 16 bytes = 2 KB, o sea 2,56 s a los 50 Hz
//  reales del driver. Cada ranura va en enteros de 16 bits (milesimas
//  de g, centesimas de m/s^2, milesimas de rad/s) en vez de en coma
//  flotante: mismo alcance util, la mitad de memoria.
// -------------------------------------------------------------
#define FLEXTHEFT_RING  128
#define FLEXTHEFT_STEPS 16     // picos recientes para la cadencia de pasos

typedef struct {
  uint32_t tMs;
  int16_t  mg;             // |a| en milesimas de g
  int16_t  lx, ly, lz;     // aceleracion lineal en centesimas de m/s^2
  int16_t  mrot;           // |giro| en milesimas de rad/s
} FlexTheftSlot;

// -------------------------------------------------------------
//  7) EL DETECTOR
//  ------------------------------------------------------------
//  Estado opaco de tamano fijo: nada de reservas dinamicas, igual que
//  el detector de caidas. Vive donde lo ponga quien lo use.
// -------------------------------------------------------------
typedef struct {
  FlexTheftParams p;
  uint8_t  sens;
  uint8_t  state;
  uint8_t  started;
  uint32_t stateMs;
  uint32_t lastMs;
  uint32_t blockUntilMs;    // periodo refractario tras un veredicto

  // Ventana temporal
  FlexTheftSlot ring[FLEXTHEFT_RING];
  uint16_t rN, rHead;

  // Gravedad estimada cuando NO hay cuaternion (filtro de primer orden
  // sobre la aceleracion medida). Con cuaternion no se usa.
  float    gEma[3];
  uint8_t  haveEma;

  // Medias moviles del reposo (contexto REST / CARRY)
  float    linEma, rotEma;

  // Muestra anterior (derivada y cambio de direccion)
  float    prevLinG;
  float    prevLin[3];
  uint8_t  havePrev;

  // Acumuladores del tiron en curso
  uint32_t burstT0, burstPeakMs, burstQuietMs;
  float    peakLinG, peakJerk, peakRot;
  float    dirSum[3], dirMag;
  uint8_t  revInBurst;

  // Contexto capturado al abrir el caso
  uint8_t  ctxHeld, ctxRest, ctxFreeFall, ctxWalk;
  uint8_t  ctxRev;

  // Ventana posterior
  uint32_t confirmT0;
  uint32_t escapeMs, quietMs;
  float    postPeakG;

  // Cadencia de pasos
  uint32_t stepMs[FLEXTHEFT_STEPS];
  uint8_t  stepN, stepHead;

  uint32_t nEvents, nSnatch;
  FlexTheftEvent last;
  uint8_t  lastValid;
} FlexTheftDet;

// Deja el detector en reposo con los parametros del nivel pedido.
void flexTheftInit(FlexTheftDet* d, uint8_t sens);
// Cambia el nivel de sensibilidad SIN perder el estado ni el historial
// de contadores. Descarta el caso en curso: los parametros con los que
// se abrio ya no son los que lo cerrarian.
void flexTheftSetSensitivity(FlexTheftDet* d, uint8_t sens);
// Sustituye los parametros a mano (calibracion). Misma regla.
void flexTheftSetParams(FlexTheftDet* d, const FlexTheftParams* p);
// Vuelve al reposo descartando el caso en curso Y la ventana temporal.
// Se llama cuando el sensor se pierde: una ventana con un agujero de
// varios segundos en medio no describe ninguna secuencia.
void flexTheftReset(FlexTheftDet* d);

// Alimenta UNA muestra. Devuelve true SOLO en la muestra en la que se
// cierra una evaluacion; entonces `out` (si no es NULL) trae el evento
// completo, supere el umbral (out->snatch = 1) o no.
bool flexTheftFeed(FlexTheftDet* d, const FlexTheftSample* s, FlexTheftEvent* out);

// ---- Consulta ----
int         flexTheftState(const FlexTheftDet* d);
const char* flexTheftStateName(int state, int lang);   // lang: 0 ES, 1 EN, 2 FR, 3 PT, 4 IT
const char* flexTheftSensName(uint8_t sens, int lang);
uint32_t    flexTheftEventCount(const FlexTheftDet* d);
uint32_t    flexTheftSnatchCount(const FlexTheftDet* d);
bool        flexTheftLast(const FlexTheftDet* d, FlexTheftEvent* out);
// Nivel de actividad 0..255 de la ventana reciente. Lo usa la interfaz
// para el grafico de movimiento: es una medida, no un adorno animado.
uint8_t     flexTheftActivity(const FlexTheftDet* d);

// ---- Utilidades puras (publicas porque tambien se prueban solas) ----
float flexTheftMagG(float x, float y, float z);
// Eje "arriba" del mundo expresado en el sistema del sensor, a partir
// del cuaternion del rotation vector. Multiplicado por 9,80665 da el
// vector de gravedad que mide el acelerometro en reposo.
void  flexTheftUpFromQuat(const float* q, float* up3);
