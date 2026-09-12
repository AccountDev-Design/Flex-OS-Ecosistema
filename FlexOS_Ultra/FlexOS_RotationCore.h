// #############################################################
// ##  FlexOS · FLEX ROTATION  ·  NUCLEO DE DECISION (logica pura)
// ##  ----------------------------------------------------------
// ##  Mismo criterio que FlexOS_FallDetect y FlexOS_Mem: aqui NO se
// ##  lee hardware, NO se dibuja y NO se toca ningun estado del
// ##  sistema. Este modulo recibe muestras ya medidas (inclinacion,
// ##  aceleracion y giro) y decide UNA cosa: cual es la postura
// ##  ESTABLE del aparato. Medir es del driver, girar la interfaz es
// ##  del motor. Por eso se compila y se ejercita entero en el PC
// ##  (tests/host/test_rotation).
// ##
// ##  POR QUE NO BASTA "si el angulo pasa de 45 grados, gira"
// ##  ----------------------------------------------------------
// ##  Con un umbral unico, un aparato sujeto a mano cerca del limite
// ##  entra y sale de la postura decenas de veces por minuto, y cada
// ##  entrada seria una reconstruccion completa de la interfaz. Eso es
// ##  exactamente el parpadeo que este nucleo existe para evitar.
// ##
// ##  Tres filtros, en este orden:
// ##
// ##    1) PLANITUD. Boca arriba o boca abajo sobre una mesa, la
// ##       gravedad casi no se proyecta sobre el plano de la pantalla:
// ##       el angulo que sale de ahi es ruido amplificado. No se
// ##       clasifica nada -- se conserva la postura que ya habia.
// ##
// ##    2) MOVIMIENTO. Si la aceleracion se aleja de 1 g o el
// ##       giroscopio se dispara, hay un EVENTO DE MOVIMIENTO en
// ##       curso (una sacudida, una caida, un golpe). Durante el
// ##       evento no se persigue la orientacion: la interfaz se queda
// ##       quieta. Al terminar, hace falta un tiempo de calma antes de
// ##       volver a admitir un cambio.
// ##
// ##    3) PERMANENCIA CON HISTERESIS. Una postura candidata tiene que
// ##       mantenerse sin interrupcion durante dwellMs para
// ##       confirmarse, y el umbral para SALIR de la postura actual es
// ##       mayor que el de ENTRAR en otra. Entre los dos hay una zona
// ##       muerta en la que no pasa nada.
// ##
// ##  LO QUE ESTE ARCHIVO NO HACE
// ##  ----------------------------------------------------------
// ##  No sabe que es una pantalla, ni un framebuffer, ni el BNO085.
// ##  No decide si la rotacion esta permitida (eso es politica del
// ##  sistema) y no detecta caidas: la deteccion de caidas es un motor
// ##  INDEPENDIENTE con su propia logica temporal (FlexOS_FallDetect).
// ##  Que aqui haya una senal de "movimiento" no la sustituye ni la
// ##  alimenta: son dos consumidores distintos del mismo sensor.
// #############################################################
#pragma once
#include <stdint.h>
#include <stdbool.h>

// -------------------------------------------------------------
//  1) LAS POSTURAS
//  ------------------------------------------------------------
//  Son posturas FISICAS del aparato, no orientaciones de la
//  interfaz. Quien traduce una postura en una orientacion pintable
//  es el motor, y puede perfectamente no soportarlas todas.
// -------------------------------------------------------------
enum {
  FROT_POS_UNKNOWN = 0,   // indeterminada (plano, o aun sin dato)
  FROT_POS_PORTRAIT,      // vertical, borde superior arriba
  FROT_POS_LAND_A,        // horizontal: el borde DERECHO del panel queda arriba
  FROT_POS_LAND_B,        // horizontal: el borde IZQUIERDO del panel queda arriba
  FROT_POS_INVERTED       // vertical invertida (borde superior abajo)
};

// -------------------------------------------------------------
//  2) LOS PARAMETROS, EN UN SOLO SITIO
//  ------------------------------------------------------------
//  Centralizados a proposito: son los numeros que hay que calibrar
//  contra el modulo real y no pueden estar repartidos por el codigo.
//  Los valores de fabrica salen de la fisica del gesto: girar un
//  aparato a mano lleva entre 200 y 500 ms, y quien lo gira lo deja
//  quieto despues; un umbral de entrada de 32 grados con salida a 58
//  deja 26 grados de zona muerta, mas que el temblor de una mano.
// -------------------------------------------------------------
typedef struct {
  float    enterDeg;     // desviacion maxima para ENTRAR en una postura
  float    exitDeg;      // desviacion minima para SALIR de la actual
  float    flatCos;      // |up_z| por encima de esto = plano: postura indeterminada
  float    motionG;      // |,|a|-1g| por encima de esto = evento de movimiento
  float    motionRad;    // ...o el giroscopio por encima de esto
  uint16_t dwellMs;      // permanencia continua que confirma una postura
  uint16_t settleMs;     // calma exigida despues de un evento de movimiento
  uint16_t staleMs;      // sin muestras mas de esto: el estado se da por caducado
} FrotParams;

static inline void frotDefaults(FrotParams* p){
  if(!p) return;
  p->enterDeg  = 32.0f;
  p->exitDeg   = 58.0f;
  p->flatCos   = 0.84f;    // ~33 grados de inclinacion minima sobre la horizontal
  p->motionG   = 0.35f;
  p->motionRad = 2.20f;
  p->dwellMs   = 650;
  p->settleMs  = 400;
  p->staleMs   = 1500;
}

// -------------------------------------------------------------
//  3) EL ESTADO
//  ------------------------------------------------------------
//  Tamano fijo, sin reservas dinamicas. Vive donde lo ponga quien lo
//  use (en el motor, un static del modulo).
// -------------------------------------------------------------
typedef struct {
  FrotParams p;
  uint8_t  pos;          // postura ESTABLE confirmada (FROT_POS_*)
  uint8_t  cand;         // postura candidata en curso
  uint8_t  motion;       // 1 = evento de movimiento ACTIVO ahora mismo
  uint8_t  flat;         // 1 = plano: la inclinacion no informa de nada
  uint8_t  started;      // ya hubo al menos una muestra
  uint32_t candMs;       // millis en que la candidata empezo a mantenerse
  uint32_t calmMs;       // millis de la ultima muestra SIN movimiento brusco
  uint32_t lastMs;       // ultima muestra
  uint32_t nChanges;     // posturas confirmadas desde el arranque (diagnostico)
} FrotStab;

// -------------------------------------------------------------
//  4) CONVERSIONES PURAS
// -------------------------------------------------------------

// Normaliza a (-180,180]. Un NaN no se propaga: devuelve 0.
static inline float frotWrap180(float a){
  if(!(a == a)) return 0.0f;
  while(a >  180.0f) a -= 360.0f;
  while(a <= -180.0f) a += 360.0f;
  return a;
}
// Distancia angular ABSOLUTA a una referencia, en [0,180].
static inline float frotDistDeg(float a, float ref){
  float d = frotWrap180(a - ref);
  return d < 0.0f ? -d : d;
}

// Angulo de referencia de cada postura sobre el eje de inclinacion.
// 0 = la gravedad tira hacia el borde INFERIOR del panel (vertical normal).
static inline float frotPosRefDeg(int pos){
  switch(pos){
    case FROT_POS_PORTRAIT: return 0.0f;
    case FROT_POS_LAND_A:   return 90.0f;
    case FROT_POS_LAND_B:   return -90.0f;
    case FROT_POS_INVERTED: return 180.0f;
    default:                return 0.0f;
  }
}

// CLASIFICACION CON HISTERESIS.
//   · Si el angulo sigue dentro de exitDeg de la postura ACTUAL, se
//     conserva la actual aunque otra este mas cerca: eso es la zona
//     muerta, y es lo que impide el vaiven en el limite.
//   · Si ya salio de la actual, solo se propone otra cuando esta
//     DENTRO de enterDeg de su referencia. Entre medias no hay
//     candidata: se devuelve UNKNOWN y nadie cambia nada.
// Funcion pura: mismo resultado siempre, sin estado ni tiempo.
static inline int frotClassify(float angDeg, int cur, const FrotParams* p){
  FrotParams d; if(!p){ frotDefaults(&d); p = &d; }
  // Un angulo que no es un numero NO describe ninguna postura. Se dice eso y
  // no se propone ninguna: frotWrap180 normaliza un NaN a 0 para que no se
  // propague, y sin esta guarda ese 0 se leeria como "vertical perfecta" --
  // o sea, el sensor roto girando la interfaz.
  if(!(angDeg == angDeg)) return FROT_POS_UNKNOWN;
  if(cur != FROT_POS_UNKNOWN && frotDistDeg(angDeg, frotPosRefDeg(cur)) <= p->exitDeg)
    return cur;
  static const int POS[4] = { FROT_POS_PORTRAIT, FROT_POS_LAND_A, FROT_POS_LAND_B, FROT_POS_INVERTED };
  for(int i = 0; i < 4; i++)
    if(frotDistDeg(angDeg, frotPosRefDeg(POS[i])) <= p->enterDeg) return POS[i];
  return FROT_POS_UNKNOWN;
}

// EVENTO DE MOVIMIENTO. accMag en g (modulo de la aceleracion CON
// gravedad, o sea 1.0 en reposo) y gyrMag en rad/s.
static inline bool frotIsViolent(float accMag, float gyrMag, const FrotParams* p){
  FrotParams d; if(!p){ frotDefaults(&d); p = &d; }
  if(!(accMag == accMag) || !(gyrMag == gyrMag)) return true;   // NaN: se trata como inestable
  float dev = accMag - 1.0f; if(dev < 0.0f) dev = -dev;
  return (dev > p->motionG) || (gyrMag > p->motionRad);
}

// -------------------------------------------------------------
//  5) LA MAQUINA
// -------------------------------------------------------------
static inline void frotStabInit(FrotStab* s){
  if(!s) return;
  frotDefaults(&s->p);
  s->pos = FROT_POS_UNKNOWN; s->cand = FROT_POS_UNKNOWN;
  s->motion = 0; s->flat = 0; s->started = 0;
  s->candMs = 0; s->calmMs = 0; s->lastMs = 0; s->nChanges = 0;
}
static inline void frotStabSetParams(FrotStab* s, const FrotParams* p){
  if(!s || !p) return;
  s->p = *p;
  s->cand = FROT_POS_UNKNOWN; s->candMs = 0;   // la candidata a medias no sobrevive a un cambio de reglas
}
// Vuelve a "sin dato" SIN inventar nada. Lo llama el motor cuando el
// sensor se pierde: una candidata a medias no puede sobrevivir a eso.
static inline void frotStabReset(FrotStab* s){
  if(!s) return;
  s->pos = FROT_POS_UNKNOWN; s->cand = FROT_POS_UNKNOWN;
  s->motion = 0; s->flat = 0; s->started = 0;
  s->candMs = 0; s->calmMs = 0; s->lastMs = 0;
}

// ALIMENTA UNA MUESTRA.
//   tMs     millis de la muestra
//   angDeg  inclinacion en el PLANO de la pantalla, en (-180,180];
//           0 = vertical normal, +90 = FROT_POS_LAND_A
//   upDotN  componente de la vertical del mundo sobre la NORMAL de la
//           pantalla, en [-1,1]. Cerca de +-1 el aparato esta plano.
//   accMag  modulo de la aceleracion en g (1.0 en reposo)
//   gyrMag  modulo de la velocidad angular en rad/s
//
// Devuelve true SOLO en la muestra que CONFIRMA una postura nueva.
static inline bool frotStabFeed(FrotStab* s, uint32_t tMs, float angDeg, float upDotN,
                                float accMag, float gyrMag){
  if(!s) return false;

  // Hueco largo sin muestras (sensor dormido, app en segundo plano): lo
  // acumulado ya no describe el presente. Se parte de cero en vez de
  // confirmar una postura con permanencia falsa.
  if(s->started && s->p.staleMs && (uint32_t)(tMs - s->lastMs) > s->p.staleMs){
    s->cand = FROT_POS_UNKNOWN; s->candMs = 0; s->calmMs = 0;
  }
  s->lastMs = tMs;
  if(!s->started){ s->started = 1; s->calmMs = tMs; }

  // --- filtro 2: evento de movimiento (se evalua ANTES que nada mas, y
  //     se publica aunque el aparato este plano: es informacion del
  //     estado fisico, no de la postura).
  bool violent = frotIsViolent(accMag, gyrMag, &s->p);
  s->motion = violent ? 1 : 0;
  if(violent){
    s->cand = FROT_POS_UNKNOWN; s->candMs = 0;
    return false;                         // durante el evento NO se cambia nada
  }
  if(s->calmMs == 0) s->calmMs = tMs;
  // Al salir del evento hace falta calma sostenida. Sin esto, el primer
  // instante tranquilo de un rebote ya valdria como "se estabilizo".
  bool settled = (uint32_t)(tMs - s->calmMs) >= s->p.settleMs;

  // --- filtro 1: planitud
  float n = upDotN; if(n < 0.0f) n = -n;
  if(!(n == n)) n = 1.0f;                 // NaN -> se trata como plano: no se clasifica
  s->flat = (n >= s->p.flatCos) ? 1 : 0;
  if(s->flat){
    s->cand = FROT_POS_UNKNOWN; s->candMs = 0;
    return false;                         // plano: se CONSERVA la postura que hubiera
  }

  // --- filtro 3: permanencia con histeresis
  int c = frotClassify(angDeg, s->pos, &s->p);
  if(c == FROT_POS_UNKNOWN || c == s->pos){
    s->cand = FROT_POS_UNKNOWN; s->candMs = 0;
    return false;
  }
  if(c != s->cand){ s->cand = (uint8_t)c; s->candMs = tMs; return false; }
  if(!settled) return false;              // aun no hay calma suficiente tras el movimiento
  if((uint32_t)(tMs - s->candMs) < s->p.dwellMs) return false;

  s->pos  = (uint8_t)c;
  s->cand = FROT_POS_UNKNOWN; s->candMs = 0;
  s->nChanges++;
  return true;
}

// Consulta
static inline int  frotStabPos(const FrotStab* s){ return s ? (int)s->pos : (int)FROT_POS_UNKNOWN; }
static inline bool frotStabMotion(const FrotStab* s){ return s && s->motion; }
static inline bool frotStabFlat(const FrotStab* s){ return s && s->flat; }
