// #############################################################
// ##  FlexOS · DETECCION DE CAIDAS  ·  implementacion (logica pura)
// ##  ----------------------------------------------------------
// ##  Sin Arduino, sin hardware y sin dibujo: entra una muestra, sale
// ##  un veredicto. Se compila igual en la placa y en el PC.
// #############################################################

#include "FlexOS_FallDetect.h"
#include <math.h>
#include <string.h>

#define G_MS2 9.80665f

// -------------------------------------------------------------
//  Parametros de fabrica
//  ------------------------------------------------------------
//  De donde sale cada numero:
//    freeFallG 0.42  -> en caida libre real el acelerometro no baja a
//                       0 exacto: el aparato rota y roza. 0,4-0,5 g es
//                       el corte que usa la literatura de detectores
//                       basados en acelerometro de tres ejes.
//    freeFallMinMs 70-> por debajo de eso no ha caido: es un tiron. 70 ms
//                       de caida libre son unos 2,5 cm, el minimo que se
//                       puede distinguir de un movimiento de muneca.
//    impactG 2.6     -> un golpe seco de sobremesa ronda 1,8-2,2 g; una
//                       caida al suelo pasa de 3 g con facilidad. 2,6
//                       deja margen a los dos lados.
//    impactBigG 4.5  -> impacto claramente fuerte; suma mas confianza.
//                       Sigue muy por debajo del fondo de escala del
//                       BNO085 (+-8 g), asi que no se satura la medida.
//    rotRad 3.5      -> ~200 grados/s. Girar el telefono para mirarlo se
//                       queda muy por debajo; una caida con vuelco, no.
//    orientDeg 35    -> menos que eso lo produce dejarlo en la mesa
//                       ligeramente inclinado.
//    threshold 62    -> exige DOS pruebas fuertes (p.ej. caida libre +
//                       impacto) o una fuerte y dos debiles. Un impacto
//                       solo, por grande que sea, no llega.
//  NO son definitivos: son el punto de partida, y estan todos aqui
//  para poder ajustarlos contra el modulo real sin tocar la logica.
// -------------------------------------------------------------
void flexFallDefaults(FlexFallParams* p){
  if(!p) return;
  p->freeFallG     = 0.42f;
  p->freeFallMinMs = 70;
  p->freeFallMaxMs = 900;
  p->impactG       = 2.6f;
  p->impactBigG    = 4.5f;
  p->rotRad        = 3.5f;
  p->motionG       = 0.18f;
  p->impactWinMs   = 500;
  p->settleMs      = 900;
  p->settleTolG    = 0.12f;
  p->settleRotRad  = 0.45f;
  p->evalMs        = 2200;
  p->orientDeg     = 35.0f;
  p->threshold     = 62;
}

// -------------------------------------------------------------
//  Utilidades
// -------------------------------------------------------------
float flexFallMagG(float x, float y, float z){
  return sqrtf(x * x + y * y + z * z) / G_MS2;
}

static float fallRotMag(const FlexFallSample* s){
  return sqrtf(s->gx * s->gx + s->gy * s->gy + s->gz * s->gz);
}

float flexFallQuatAngleDeg(const float* qa, const float* qb){
  if(!qa || !qb) return 0.0f;
  float na = sqrtf(qa[0]*qa[0] + qa[1]*qa[1] + qa[2]*qa[2] + qa[3]*qa[3]);
  float nb = sqrtf(qb[0]*qb[0] + qb[1]*qb[1] + qb[2]*qb[2] + qb[3]*qb[3]);
  // Un cuaternion que no es unitario no describe una rotacion: se
  // devuelve 0 en vez de un angulo inventado.
  if(na < 0.5f || nb < 0.5f) return 0.0f;
  float d = (qa[0]*qb[0] + qa[1]*qb[1] + qa[2]*qb[2] + qa[3]*qb[3]) / (na * nb);
  if(d < 0.0f) d = -d;          // q y -q son la misma orientacion
  if(d > 1.0f) d = 1.0f;
  return 2.0f * acosf(d) * 57.29577951f;
}

// -------------------------------------------------------------
//  Ciclo de vida
// -------------------------------------------------------------
void flexFallInit(FlexFallDet* d){
  if(!d) return;
  memset(d, 0, sizeof(*d));
  flexFallDefaults(&d->p);
  d->state  = FLEXFALL_IDLE;
  d->minG   = 99.0f;
  d->qRest[3] = 1.0f;
}

void flexFallSetParams(FlexFallDet* d, const FlexFallParams* p){
  if(!d || !p) return;
  d->p = *p;
}

// Vuelve al reposo descartando lo acumulado. No toca los contadores
// historicos ni el ultimo evento: eso es historia, no estado.
static void fallToIdle(FlexFallDet* d){
  d->state    = FLEXFALL_IDLE;
  d->stateMs  = d->lastMs;
  d->peakG    = 0.0f;
  d->minG     = 99.0f;
  d->rotPeak  = 0.0f;
  d->ffMs     = 0;
  d->ffStartMs = 0;
  d->ffEndMs  = 0;
  d->impactMs = 0;
  d->haveQ0   = 0;
  d->settleAccum = 0.0f;
  d->lastSettleMs = 0;
}

void flexFallReset(FlexFallDet* d){
  if(!d) return;
  fallToIdle(d);
  d->started  = 0;
  d->stepN    = 0;
  d->stepHead = 0;
  d->haveRest = 0;
}

// -------------------------------------------------------------
//  Cadencia de pasos
//  ------------------------------------------------------------
//  Caminar produce picos moderados MUY regulares (entre 1,5 y 2,5
//  pasos por segundo). Si en los ultimos segundos hubo tres o mas
//  picos con separaciones parecidas, lo que viene despues es mucho mas
//  probable que sea un paso fuerte que una caida: eso RESTA confianza.
// -------------------------------------------------------------
static void fallPushStep(FlexFallDet* d, uint32_t t){
  d->stepMs[d->stepHead] = t;
  d->stepHead = (uint8_t)((d->stepHead + 1) % FLEXFALL_HIST);
  if(d->stepN < FLEXFALL_HIST) d->stepN++;
}

static bool fallWalkingPattern(const FlexFallDet* d, uint32_t now){
  if(d->stepN < 3) return false;
  // Intervalos entre picos dentro de los ultimos 3 s.
  uint32_t ts[FLEXFALL_HIST];
  int n = 0;
  for(int i = 0; i < d->stepN; i++){
    uint32_t t = d->stepMs[i];
    if(t && now - t <= 3000u) ts[n++] = t;
  }
  if(n < 3) return false;
  // Ordenacion por insercion: n <= 16 y esto corre una vez por evento.
  for(int i = 1; i < n; i++){
    uint32_t v = ts[i]; int j = i - 1;
    while(j >= 0 && ts[j] > v){ ts[j + 1] = ts[j]; j--; }
    ts[j + 1] = v;
  }
  int regular = 0;
  for(int i = 2; i < n; i++){
    long d1 = (long)ts[i - 1] - (long)ts[i - 2];
    long d2 = (long)ts[i]     - (long)ts[i - 1];
    if(d1 < 250 || d1 > 900 || d2 < 250 || d2 > 900) continue;   // fuera de cadencia humana
    long diff = d1 > d2 ? d1 - d2 : d2 - d1;
    if(diff <= 160) regular++;                                    // dos zancadas parecidas
  }
  return regular >= 1;
}

// -------------------------------------------------------------
//  Confidence Score
//  ------------------------------------------------------------
//  Cada prueba aporta lo suyo y ninguna decide sola. Los pesos estan
//  aqui, juntos y en claro, por el mismo motivo que los umbrales.
// -------------------------------------------------------------
static uint8_t fallScore(const FlexFallDet* d, FlexFallEvent* e, bool walking){
  const FlexFallParams* p = &d->p;
  int score = 0;
  e->reasons = walking ? FLEXFALL_R_WALKING : 0;

  // Caida libre: hasta 28 puntos, proporcional a lo que duro.
  if(e->freeFallMs >= p->freeFallMinMs && e->freeFallMs <= p->freeFallMaxMs){
    int extra = (int)e->freeFallMs - (int)p->freeFallMinMs;
    int cap   = (int)p->freeFallMaxMs - (int)p->freeFallMinMs;
    int add   = 16 + (cap > 0 ? (12 * extra) / cap : 0);
    if(add > 28) add = 28;
    score += add;
    e->reasons |= FLEXFALL_R_FREEFALL;
  }

  // Impacto: 24 puntos, y 14 mas si fue claramente fuerte.
  if(e->peakG >= p->impactG){
    score += 24;
    e->reasons |= FLEXFALL_R_IMPACT;
    if(e->peakG >= p->impactBigG){
      score += 14;
      e->reasons |= FLEXFALL_R_BIGIMPACT;
    }
  }

  // Giro brusco: 14 puntos.
  if(e->rotPeak >= p->rotRad){
    score += 14;
    e->reasons |= FLEXFALL_R_ROTATION;
  }

  // Cambio de postura: hasta 16 puntos. Solo si hubo sensor fusion:
  // sin cuaternion, orientDeg vale 0 y esto no puntua.
  if(e->orientDeg >= p->orientDeg){
    int add = 10 + (int)((e->orientDeg - p->orientDeg) / 6.0f);
    if(add > 16) add = 16;
    score += add;
    e->reasons |= FLEXFALL_R_ORIENT;
  }

  // Estabilizacion posterior: 12 puntos. Un aparato que sigue
  // moviendose despues no se ha caido: lo llevan en la mano.
  if(e->settled){
    score += 12;
    e->reasons |= FLEXFALL_R_SETTLED;
  } else {
    score -= 18;
  }

  // Cadencia de pasos justo antes: penalizacion fuerte.
  if(e->reasons & FLEXFALL_R_WALKING) score -= 22;

  // Un impacto SIN caida libre y SIN cambio de postura es un golpe,
  // no una caida: se le quita el peso que el pico le habia dado.
  if(!(e->reasons & FLEXFALL_R_FREEFALL) && !(e->reasons & FLEXFALL_R_ORIENT))
    score -= 20;

  if(score < 0)   score = 0;
  if(score > 100) score = 100;
  return (uint8_t)score;
}

// -------------------------------------------------------------
//  Cierre de la ventana: emite el veredicto
// -------------------------------------------------------------
static void fallEmit(FlexFallDet* d, FlexFallEvent* out, bool walking){
  FlexFallEvent e;
  memset(&e, 0, sizeof(e));
  e.tMs        = d->impactMs;
  e.peakG      = d->peakG;
  e.minG       = (d->minG < 90.0f) ? d->minG : 0.0f;
  e.freeFallMs = d->ffMs;
  e.rotPeak    = d->rotPeak;
  e.settled    = (d->settleAccum >= (float)d->p.settleMs) ? 1 : 0;
  e.orientDeg  = 0.0f;
  if(d->haveQ0 && d->haveRest)
    e.orientDeg = flexFallQuatAngleDeg(d->q0, d->qRest);
  e.confidence = fallScore(d, &e, walking);
  e.fall = (e.confidence >= d->p.threshold) ? 1 : 0;

  d->nEvents++;
  if(e.fall) d->nFalls++;
  d->last      = e;
  d->lastValid = 1;
  if(out) *out = e;
}

// -------------------------------------------------------------
//  El tick
// -------------------------------------------------------------
bool flexFallFeed(FlexFallDet* d, const FlexFallSample* s, FlexFallEvent* out){
  if(!d || !s) return false;

  const FlexFallParams* p = &d->p;
  float g   = flexFallMagG(s->ax, s->ay, s->az);
  float rot = fallRotMag(s);
  float dev = fabsf(g - 1.0f);
  uint32_t t = s->tMs;

  if(!d->started){
    d->started = 1;
    d->stateMs = t;
    d->lastMs  = t;
    if(s->haveQuat){
      d->qRest[0] = s->qi; d->qRest[1] = s->qj; d->qRest[2] = s->qk; d->qRest[3] = s->qr;
      d->haveRest = 1;
    }
    return false;
  }
  // El reloj retrocedio (muestras desordenadas): se descarta la muestra
  // en vez de calcular una duracion negativa.
  if(t < d->lastMs){ d->lastMs = t; return false; }
  uint32_t dt = t - d->lastMs;
  d->lastMs = t;

  // Picos moderados: alimentan el detector de cadencia de pasos.
  if(g >= 1.6f && g < p->impactG) fallPushStep(d, t);

  bool quiet = (dev <= p->settleTolG) && (rot <= p->settleRotRad);
  bool emitted = false;

  switch(d->state){
    case FLEXFALL_IDLE:
      // En reposo se refresca la postura de referencia: es contra ella
      // contra la que luego se mide el cambio de orientacion.
      if(quiet && s->haveQuat){
        d->qRest[0] = s->qi; d->qRest[1] = s->qj; d->qRest[2] = s->qk; d->qRest[3] = s->qr;
        d->haveRest = 1;
      }
      if(g <= p->freeFallG){
        d->state = FLEXFALL_FREEFALL; d->stateMs = t;
        d->ffStartMs = t; d->ffEndMs = t;
        d->minG = g; d->peakG = g; d->rotPeak = rot;
        if(s->haveQuat){
          d->q0[0] = d->qRest[0]; d->q0[1] = d->qRest[1];
          d->q0[2] = d->qRest[2]; d->q0[3] = d->qRest[3];
          d->haveQ0 = d->haveRest;
        }
      } else if(g >= p->impactG){
        // Impacto sin caida libre previa. Se evalua igual -- puede ser
        // un tropiezo sin vuelo -- pero entra sin los puntos de caida
        // libre, asi que necesitara postura y estabilizacion.
        d->state = FLEXFALL_IMPACT; d->stateMs = t;
        d->impactMs = t; d->peakG = g; d->rotPeak = rot; d->ffMs = 0;
        if(s->haveQuat){
          d->q0[0] = d->qRest[0]; d->q0[1] = d->qRest[1];
          d->q0[2] = d->qRest[2]; d->q0[3] = d->qRest[3];
          d->haveQ0 = d->haveRest;
        }
      } else if(dev > p->motionG){
        d->state = FLEXFALL_MOTION; d->stateMs = t;
      }
      break;

    case FLEXFALL_MOTION:
      // La caida libre RECIEN terminada sigue contando durante la
      // ventana de impacto: entre el final del vuelo y el golpe pasan
      // unos milisegundos en los que |a| ya no esta por los suelos.
      // Pasada esa ventana, el credito se pierde y lo que venga sera
      // un impacto suelto.
      if(d->ffMs && t - d->ffEndMs > (uint32_t)p->impactWinMs){
        d->ffMs = 0; d->minG = 99.0f; d->haveQ0 = 0;
      }
      if(g <= p->freeFallG){
        d->state = FLEXFALL_FREEFALL; d->stateMs = t;
        d->ffStartMs = t; d->ffEndMs = t;
        d->minG = g; d->peakG = g; d->rotPeak = rot;
        if(s->haveQuat && d->haveRest){
          d->q0[0] = d->qRest[0]; d->q0[1] = d->qRest[1];
          d->q0[2] = d->qRest[2]; d->q0[3] = d->qRest[3];
          d->haveQ0 = 1;
        }
      } else if(g >= p->impactG){
        d->state = FLEXFALL_IMPACT; d->stateMs = t;
        d->impactMs = t; d->peakG = g;
        if(rot > d->rotPeak) d->rotPeak = rot;
        if(!d->haveQ0 && s->haveQuat && d->haveRest){
          d->q0[0] = d->qRest[0]; d->q0[1] = d->qRest[1];
          d->q0[2] = d->qRest[2]; d->q0[3] = d->qRest[3];
          d->haveQ0 = 1;
        }
      } else if(quiet && t - d->stateMs > 300){
        fallToIdle(d);
      }
      break;

    case FLEXFALL_FREEFALL:
      if(g < d->minG) d->minG = g;
      if(rot > d->rotPeak) d->rotPeak = rot;
      if(g <= p->freeFallG){
        // Sigue cayendo. La duracion se mide mientras dura, no despues.
        uint32_t dur = t - d->ffStartMs;
        d->ffMs   = (uint16_t)(dur > 65535u ? 65535u : dur);
        d->ffEndMs = t;
        // Un "vuelo" interminable no es una caida: es el sensor en
        // orbita o una lectura rota. Se descarta.
        if(dur > (uint32_t)p->freeFallMaxMs) fallToIdle(d);
      } else if(g >= p->impactG){
        d->ffEndMs = t;
        d->state = FLEXFALL_IMPACT; d->stateMs = t;
        d->impactMs = t; d->peakG = g;
      } else {
        // Salio del vuelo sin golpe todavia: se espera el impacto
        // durante impactWinMs (lo controla la rama de MOTION).
        d->ffEndMs = t;
        d->state = FLEXFALL_MOTION; d->stateMs = t;
      }
      break;

    case FLEXFALL_IMPACT:
      if(g > d->peakG) d->peakG = g;
      if(rot > d->rotPeak) d->rotPeak = rot;
      // El pico dura unos pocos ms; en cuanto baja se pasa a medir la
      // estabilizacion.
      if(t - d->impactMs > 120){
        d->state = FLEXFALL_POST; d->stateMs = t;
        d->settleAccum  = 0.0f;
        d->lastSettleMs = t;
      }
      break;

    case FLEXFALL_POST:
      if(g > d->peakG) d->peakG = g;
      if(rot > d->rotPeak) d->rotPeak = rot;
      if(quiet) d->settleAccum += (float)dt;
      else      d->settleAccum  = 0.0f;
      if(s->haveQuat){
        // La postura FINAL es la de ahora: se guarda en qRest para que
        // el veredicto la compare con la de antes del evento.
        d->qRest[0] = s->qi; d->qRest[1] = s->qj; d->qRest[2] = s->qk; d->qRest[3] = s->qr;
        d->haveRest = 1;
      }
      if(d->settleAccum >= (float)p->settleMs || t - d->impactMs >= (uint32_t)p->evalMs){
        d->state = FLEXFALL_EVAL; d->stateMs = t;
      }
      break;

    case FLEXFALL_EVAL:
      break;

    default:
      fallToIdle(d);
      break;
  }

  if(d->state == FLEXFALL_EVAL){
    bool walking = fallWalkingPattern(d, d->impactMs);
    fallEmit(d, out, walking);
    emitted = true;
    fallToIdle(d);
    d->stepN = 0; d->stepHead = 0;      // la cadencia anterior ya no describe nada
  }
  return emitted;
}

// -------------------------------------------------------------
//  Consulta
// -------------------------------------------------------------
int      flexFallState(const FlexFallDet* d){      return d ? (int)d->state : (int)FLEXFALL_IDLE; }
uint32_t flexFallEventCount(const FlexFallDet* d){ return d ? d->nEvents : 0; }
uint32_t flexFallFallCount(const FlexFallDet* d){  return d ? d->nFalls : 0; }

bool flexFallLast(const FlexFallDet* d, FlexFallEvent* out){
  if(!d || !d->lastValid) return false;
  if(out) *out = d->last;
  return true;
}

// Nombres de estado, localizados con el mismo indice de idioma que el
// resto del sistema (0 ES, 1 EN, 2 FR, 3 PT, 4 IT).
const char* flexFallStateName(int state, int lang){
  static const char* N[6][5] = {
    {"En reposo","Idle","Au repos","Em repouso","A riposo"},
    {"Movimiento","Motion","Mouvement","Movimento","Movimento"},
    {"Posible ca\xC3\xAD" "da libre","Possible free fall","Chute libre possible","Poss\xC3\xADvel queda livre","Possibile caduta libera"},
    {"Impacto","Impact","Impact","Impacto","Impatto"},
    {"Tras el impacto","After impact","Apr\xC3\xA8s l'impact","Ap\xC3\xB3s o impacto","Dopo l'impatto"},
    {"Evaluando","Evaluating","\xC3\x89valuation","Avaliando","Valutazione"},
  };
  if(state < 0 || state > 5) state = 0;
  if(lang  < 0 || lang  > 4) lang  = 0;
  return N[state][lang];
}
