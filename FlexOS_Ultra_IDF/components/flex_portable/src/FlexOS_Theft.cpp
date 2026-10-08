// #############################################################
// ##  FlexOS · PROTECCION CONTRA ROBO  ·  implementacion (logica pura)
// ##  ----------------------------------------------------------
// ##  Sin Arduino, sin hardware y sin dibujo: entra una muestra, sale
// ##  un veredicto. Se compila igual en la placa y en el PC.
// #############################################################

#include "FlexOS_Theft.h"
#include <math.h>
#include <string.h>

#define G_MS2 9.80665f

// -------------------------------------------------------------
//  Parametros de fabrica, por nivel de sensibilidad
//  ------------------------------------------------------------
//  De donde sale cada numero del nivel NORMAL, que es el recomendado:
//
//    pullG 1.45     -> aceleracion LINEAL, ya sin gravedad. Manipular el
//                      aparato con soltura se queda en 0,4-0,9 g; pasarselo
//                      a alguien ronda 1,0-1,2 g. Un tiron para arrancarlo
//                      de la mano pasa de 1,5 g con facilidad. 1,45 deja
//                      margen a los dos lados y queda MUY por debajo del
//                      fondo de escala del BNO085 (+-8 g).
//    jerkGs 65      -> el tiron no solo es fuerte: ARRANCA de golpe. 65 g/s
//                      es pasar de reposo a 1,3 g en 20 ms, una muestra a
//                      50 Hz. Un movimiento voluntario del brazo sube mucho
//                      mas despacio.
//    burst 70..400  -> duracion humana del gesto. Por debajo de 70 ms no es
//                      un tiron sino un golpe (un impacto dura 20-60 ms);
//                      por encima de 400 ms es un desplazamiento, no un
//                      arrebato.
//    dirCohMin 0.78 -> ES LA PIEZA CLAVE contra los falsos positivos.
//                      Agitar, correr o dar botes invierten la direccion
//                      de la aceleracion lineal varias veces por segundo;
//                      un tiron empuja SIEMPRE hacia el mismo lado. La
//                      coherencia es |suma de direcciones| / suma de
//                      magnitudes: 1,0 es un empujon perfectamente recto y
//                      0,0 una oscilacion simetrica.
//    twistRad 2.2   -> ~125 grados/s. Al arrancarlo de la mano el aparato
//                      gira porque los dedos lo retienen un instante.
//                      Girarlo a proposito para mirarlo se queda debajo.
//    escapeLinG 0.10-> por encima de eso el aparato se esta MOVIENDO. El
//                      temblor de una mano quieta se queda en 0,02-0,05 g,
//                      asi que sostenerlo parado NO cuenta como huida.
//    escapeNeedMs 450 de los 2,5 s posteriores. Andar o correr con el
//                      aparato lo supera de sobra; dejarlo en una mesa, no.
//    freeFallG 0.55 -> por debajo de eso no hay peso: es vuelo. Si lo hubo
//                      justo antes del pico, esto es una CAIDA y el evento
//                      se penaliza fuerte -- es lo que impide que Device
//                      Care y esta funcion se pisen.
//    threshold 68   -> exige el tiron COMPLETO (intensidad + arranque +
//                      direccion) mas una senal de contexto. Ninguna
//                      combinacion de dos rasgos llega sola.
//
//  BAJA y ALTA no mueven "un umbral": mueven el clasificador entero --
//  intensidad, duracion aceptada, coherencia exigida, cuanto movimiento
//  posterior hace falta y con cuanta confianza se cierra. Ademas BAJA
//  exige separacion posterior como CONDICION, no como puntos.
// -------------------------------------------------------------
void flexTheftDefaults(FlexTheftParams* p, uint8_t sens){
  if(!p) return;
  memset(p, 0, sizeof(*p));
  // Comunes a los tres niveles: describen el aparato y la fisica, no el gusto
  // del usuario.
  p->heldLinG      = 0.012f;
  p->restLinG      = 0.012f;
  p->freeFallG     = 0.55f;
  p->freeFallMinMs = 60;
  p->escapeWinMs   = 2500;
  p->settleTolG    = 0.12f;
  p->settleRotRad  = 0.45f;
  p->impactG       = 2.4f;
  p->refractMs     = 4000;

  switch(sens){
    case FLEXTHEFT_SENS_LOW:
      p->pullG        = 1.95f;
      p->jerkGs       = 92.0f;
      p->burstMinMs   = 90;
      p->burstMaxMs   = 320;
      p->dirCohMin    = 0.86f;
      p->twistRad     = 2.8f;
      p->escapeLinG   = 0.12f;
      p->escapeNeedMs = 600;
      p->settleMs     = 600;
      p->threshold    = 78;
      p->needEscape   = 1;      // sin separacion posterior no se declara NUNCA
      break;
    case FLEXTHEFT_SENS_HIGH:
      p->pullG        = 1.10f;
      p->jerkGs       = 48.0f;
      p->burstMinMs   = 60;
      p->burstMaxMs   = 520;
      p->dirCohMin    = 0.70f;
      p->twistRad     = 1.7f;
      p->escapeLinG   = 0.08f;
      p->escapeNeedMs = 320;
      p->settleMs     = 800;
      p->threshold    = 58;
      p->needEscape   = 0;
      break;
    default:                    // FLEXTHEFT_SENS_NORMAL
      p->pullG        = 1.45f;
      p->jerkGs       = 65.0f;
      p->burstMinMs   = 70;
      p->burstMaxMs   = 400;
      p->dirCohMin    = 0.78f;
      p->twistRad     = 2.2f;
      p->escapeLinG   = 0.10f;
      p->escapeNeedMs = 450;
      p->settleMs     = 700;
      p->threshold    = 68;
      p->needEscape   = 0;
      break;
  }
}

// -------------------------------------------------------------
//  Utilidades
// -------------------------------------------------------------
float flexTheftMagG(float x, float y, float z){
  return sqrtf(x * x + y * y + z * z) / G_MS2;
}

// El vector de rotacion del BNO085 lleva el cuaternion mundo<-sensor con
// ejes Este-Norte-Arriba. El eje "Arriba" del mundo expresado en el sistema
// del sensor es la TERCERA FILA de esa matriz; multiplicado por g da lo que
// el acelerometro mide en reposo. Misma convencion (i,j,k,real) que usa
// FlexOS_Ultra_IMU.h para el rumbo, asi que las dos conversiones describen
// el mismo montaje.
void flexTheftUpFromQuat(const float* q, float* up3){
  if(!up3) return;
  up3[0] = 0.0f; up3[1] = 0.0f; up3[2] = 1.0f;
  if(!q) return;
  float x = q[0], y = q[1], z = q[2], w = q[3];
  float n = sqrtf(x*x + y*y + z*z + w*w);
  // Un cuaternion que no es unitario no describe una rotacion: se deja el
  // eje por defecto en vez de propagar basura a la aceleracion lineal.
  if(!(n > 0.5f) || !(n < 2.0f)) return;
  x /= n; y /= n; z /= n; w /= n;
  up3[0] = 2.0f * (x * z - w * y);
  up3[1] = 2.0f * (y * z + w * x);
  up3[2] = 1.0f - 2.0f * (x * x + y * y);
}

static float theftNorm3(const float* v){
  return sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
}
static bool theftFinite(float v){ return (v == v) && (v > -1e9f) && (v < 1e9f); }

static int16_t theftClamp16(float v){
  if(!(v == v)) return 0;
  if(v >  32767.0f) return  32767;
  if(v < -32768.0f) return -32768;
  return (int16_t)v;
}

// -------------------------------------------------------------
//  Ciclo de vida
// -------------------------------------------------------------
static void theftToNormal(FlexTheftDet* d, uint32_t t){
  d->state       = (d->linEma <= d->p.restLinG && d->rotEma <= 0.05f)
                   ? FLEXTHEFT_REST : FLEXTHEFT_CARRY;
  d->stateMs     = t;
  d->burstT0     = 0;
  d->burstPeakMs = 0;
  d->burstQuietMs = 0;
  d->peakLinG    = 0.0f;
  d->peakJerk    = 0.0f;
  d->peakRot     = 0.0f;
  d->dirSum[0] = d->dirSum[1] = d->dirSum[2] = 0.0f;
  d->dirMag      = 0.0f;
  d->revInBurst  = 0;
  d->ctxHeld = d->ctxRest = d->ctxFreeFall = d->ctxWalk = d->ctxRev = 0;
  d->confirmT0   = 0;
  d->escapeMs    = 0;
  d->quietMs     = 0;
  d->postPeakG   = 0.0f;
}

void flexTheftInit(FlexTheftDet* d, uint8_t sens){
  if(!d) return;
  memset(d, 0, sizeof(*d));
  if(sens > FLEXTHEFT_SENS_HIGH) sens = FLEXTHEFT_SENS_NORMAL;
  d->sens = sens;
  flexTheftDefaults(&d->p, sens);
  d->gEma[2] = 1.0f;
  theftToNormal(d, 0);
  d->state = FLEXTHEFT_REST;
}

void flexTheftSetParams(FlexTheftDet* d, const FlexTheftParams* p){
  if(!d || !p) return;
  d->p = *p;
  theftToNormal(d, d->lastMs);
}

void flexTheftSetSensitivity(FlexTheftDet* d, uint8_t sens){
  if(!d) return;
  if(sens > FLEXTHEFT_SENS_HIGH) sens = FLEXTHEFT_SENS_NORMAL;
  d->sens = sens;
  flexTheftDefaults(&d->p, sens);
  theftToNormal(d, d->lastMs);
}

void flexTheftReset(FlexTheftDet* d){
  if(!d) return;
  theftToNormal(d, d->lastMs);
  d->state    = FLEXTHEFT_REST;
  d->started  = 0;
  d->rN = d->rHead = 0;          // la ventana temporal tambien se va: un
  d->havePrev = 0;               // agujero en medio no describe una secuencia
  d->haveEma  = 0;
  d->linEma   = 0.0f;
  d->rotEma   = 0.0f;
  d->stepN = d->stepHead = 0;
  d->blockUntilMs = 0;
}

// -------------------------------------------------------------
//  La ventana temporal
// -------------------------------------------------------------
static void theftRingPush(FlexTheftDet* d, uint32_t t, float g,
                          const float* lin, float rot){
  FlexTheftSlot* s = &d->ring[d->rHead];
  s->tMs  = t;
  s->mg   = theftClamp16(g * 1000.0f);
  s->lx   = theftClamp16(lin[0] * 100.0f);
  s->ly   = theftClamp16(lin[1] * 100.0f);
  s->lz   = theftClamp16(lin[2] * 100.0f);
  s->mrot = theftClamp16(rot * 1000.0f);
  d->rHead = (uint16_t)((d->rHead + 1) % FLEXTHEFT_RING);
  if(d->rN < FLEXTHEFT_RING) d->rN++;
}

// Recorre la ventana de la mas ANTIGUA a la mas reciente.
static inline const FlexTheftSlot* theftRingAt(const FlexTheftDet* d, uint16_t i){
  uint16_t start = (uint16_t)((d->rHead + FLEXTHEFT_RING - d->rN) % FLEXTHEFT_RING);
  return &d->ring[(start + i) % FLEXTHEFT_RING];
}

// -------------------------------------------------------------
//  Cadencia de pasos
//  ------------------------------------------------------------
//  Caminar y correr producen picos MUY regulares. Si en los ultimos
//  segundos hubo tres o mas con separaciones parecidas, lo que venga
//  despues es mucho mas probable que sea otro paso que un arrebato.
//  Es el mismo razonamiento que en el detector de caidas, con su
//  propia implementacion: dos clasificadores independientes.
// -------------------------------------------------------------
static void theftPushStep(FlexTheftDet* d, uint32_t t){
  d->stepMs[d->stepHead] = t;
  d->stepHead = (uint8_t)((d->stepHead + 1) % FLEXTHEFT_STEPS);
  if(d->stepN < FLEXTHEFT_STEPS) d->stepN++;
}

static bool theftWalkPattern(const FlexTheftDet* d, uint32_t now){
  if(d->stepN < 3) return false;
  uint32_t ts[FLEXTHEFT_STEPS];
  int n = 0;
  for(int i = 0; i < d->stepN; i++){
    uint32_t t = d->stepMs[i];
    if(t && now >= t && now - t <= 3500u) ts[n++] = t;
  }
  if(n < 3) return false;
  for(int i = 1; i < n; i++){                    // insercion: n <= 16
    uint32_t v = ts[i]; int j = i - 1;
    while(j >= 0 && ts[j] > v){ ts[j + 1] = ts[j]; j--; }
    ts[j + 1] = v;
  }
  int regular = 0;
  for(int i = 2; i < n; i++){
    long d1 = (long)ts[i - 1] - (long)ts[i - 2];
    long d2 = (long)ts[i]     - (long)ts[i - 1];
    // 200..900 ms cubre de correr (3-5 pasos/s) a caminar despacio.
    if(d1 < 200 || d1 > 900 || d2 < 200 || d2 > 900) continue;
    long diff = d1 > d2 ? d1 - d2 : d2 - d1;
    if(diff <= 170) regular++;
    }
  return regular >= 1;
}

// -------------------------------------------------------------
//  EL CONTEXTO ANTERIOR  ·  aqui es donde sirve la ventana temporal
//  ------------------------------------------------------------
//  Responde, mirando hacia atras desde el instante del tiron:
//    · ¿estaba en la mano, o apoyado en una superficie?
//    · ¿venia de una caida libre? (entonces esto es una caida)
//    · ¿la direccion ya se estaba invirtiendo? (agitar / correr)
// -------------------------------------------------------------
static void theftPreContext(FlexTheftDet* d, uint32_t t0){
  d->ctxHeld = d->ctxRest = d->ctxFreeFall = d->ctxRev = 0;
  if(d->rN == 0) return;

  int    n = 0;
  float  sumLin = 0.0f, sumRot = 0.0f;
  int    rev = 0;
  float  prevDir[3] = {0,0,0};
  bool   havePrevDir = false;
  uint32_t ffRun0 = 0, ffBest = 0;
  bool   inFf = false;

  for(uint16_t i = 0; i < d->rN; i++){
    const FlexTheftSlot* s = theftRingAt(d, i);
    if(s->tMs >= t0) break;                 // solo lo ANTERIOR al tiron
    uint32_t age = t0 - s->tMs;
    float lin[3] = { s->lx / 100.0f, s->ly / 100.0f, s->lz / 100.0f };
    float linG   = theftNorm3(lin) / G_MS2;

    // --- caida libre: una racha de |a| por los suelos en el ultimo medio
    // --- segundo. Es lo que separa "se lo arrancaron" de "se cayo".
    if(age <= 700u){
      bool low = (s->mg <= (int16_t)(d->p.freeFallG * 1000.0f));
      if(low && !inFf){ inFf = true; ffRun0 = s->tMs; }
      else if(!low && inFf){
        inFf = false;
        uint32_t run = s->tMs - ffRun0;
        if(run > ffBest) ffBest = run;
      }
    }
    // --- estado del aparato en el segundo anterior ---
    if(age <= 1200u){
      n++;
      sumLin += linG;
      sumRot += s->mrot / 1000.0f;
      if(linG > 0.30f){
        float inv = 1.0f / (linG * G_MS2);
        float dir[3] = { lin[0]*inv, lin[1]*inv, lin[2]*inv };
        if(havePrevDir){
          float dot = dir[0]*prevDir[0] + dir[1]*prevDir[1] + dir[2]*prevDir[2];
          if(dot < -0.25f) rev++;
        }
        prevDir[0] = dir[0]; prevDir[1] = dir[1]; prevDir[2] = dir[2];
        havePrevDir = true;
      }
    }
  }
  // RACHA ABIERTA CONTRA EL TIRON. Es el caso que mas importa y el mas facil
  // de dejarse: en una caida real el vuelo NO termina antes del pico, termina
  // EN el pico -- la ultima muestra ligera es la de justo antes del golpe. Si
  // la racha solo se cerrase al ver una muestra pesada, esa caida entraria con
  // ffBest = 0, sin la penalizacion de caida libre, y seria justo la secuencia
  // que hay que distinguir de un arrebato. Se cierra contra t0.
  if(inFf){
    uint32_t run = (t0 > ffRun0) ? (t0 - ffRun0) : 0;
    if(run > ffBest) ffBest = run;
  }

  d->ctxFreeFall = (ffBest >= d->p.freeFallMinMs) ? 1 : 0;
  d->ctxRev      = (uint8_t)(rev > 255 ? 255 : rev);
  if(n < 12) return;                          // sin contexto suficiente no se afirma nada
  float meanLin = sumLin / (float)n;
  float meanRot = sumRot / (float)n;
  // EN LA MANO: hay vida, pero no violencia. Una mesa da casi exactamente
  // cero en los dos; un bolsillo mientras se corre se sale por arriba.
  d->ctxHeld = (meanLin > d->p.heldLinG && meanLin < 0.55f && meanRot < 2.5f) ? 1 : 0;
  d->ctxRest = (meanLin <= d->p.restLinG && meanRot <= 0.05f) ? 1 : 0;
}

// -------------------------------------------------------------
//  CONFIANZA
//  ------------------------------------------------------------
//  Cada prueba aporta lo suyo y ninguna decide sola. Los pesos estan
//  aqui, juntos y en claro, por el mismo motivo que los umbrales.
// -------------------------------------------------------------
static uint8_t theftScore(const FlexTheftDet* d, FlexTheftEvent* e){
  const FlexTheftParams* p = &d->p;
  int score = 0;
  e->reasons = 0;

  // --- Contexto: venia de la mano ---
  if(d->ctxHeld){ score += 10; e->reasons |= FLEXTHEFT_R_HELD; }

  // --- Intensidad del tiron: 22, y 10 mas si fue claramente violento ---
  if(e->pullG >= p->pullG){
    score += 22;
    e->reasons |= FLEXTHEFT_R_PULL;
    if(e->pullG >= p->pullG * 1.6f) score += 10;
  }

  // --- Arranque brusco (derivada): 16, y 8 mas si fue instantaneo ---
  if(e->jerkGs >= p->jerkGs){
    score += 16;
    e->reasons |= FLEXTHEFT_R_JERK;
    if(e->jerkGs >= p->jerkGs * 1.8f) score += 8;
  }

  // --- Direccion coherente: hasta 24. LA pieza contra agitar y correr ---
  if(e->dirCoh >= p->dirCohMin){
    float span = 1.0f - p->dirCohMin;
    int add = 14;
    if(span > 0.001f) add += (int)(10.0f * (e->dirCoh - p->dirCohMin) / span);
    if(add > 24) add = 24;
    score += add;
    e->reasons |= FLEXTHEFT_R_DIR;
  }

  // --- Giro acoplado al tiron: 12. Solo cuenta si HUBO tiron: un giro
  // --- suelto es ensenar el aparato, no perderlo.
  if(e->rotPeak >= p->twistRad && (e->reasons & FLEXTHEFT_R_PULL)){
    score += 12;
    e->reasons |= FLEXTHEFT_R_TWIST;
  }

  // --- Separacion: siguio moviendose. Hasta 22 ---
  if(e->escapeMs >= p->escapeNeedMs){
    int extra = (int)e->escapeMs - (int)p->escapeNeedMs;
    int cap   = (int)p->escapeWinMs - (int)p->escapeNeedMs;
    int add   = 14 + (cap > 0 ? (8 * extra) / cap : 0);
    if(add > 22) add = 22;
    score += add;
    e->reasons |= FLEXTHEFT_R_ESCAPE;
  }

  // --- Impacto posterior: CONTEXTO, no puntua ---
  // Un golpe despues ni confirma ni desmiente un arrebato. Se anota para
  // que el Event Manager pueda correlacionar "arrebato + caida", y punto.
  if(e->hadImpact) e->reasons |= FLEXTHEFT_R_IMPACT;

  // --- PENALIZACIONES ---

  // Oscilacion: la direccion se invirtio. Agitar, correr, dar botes.
  if(d->revInBurst + d->ctxRev >= 2){
    score -= 30;
    e->reasons |= FLEXTHEFT_R_SHAKE;
  }
  // Cadencia de pasos justo antes.
  if(d->ctxWalk){
    score -= 16;
    e->reasons |= FLEXTHEFT_R_WALK;
  }
  // Caida libre antes del pico: esto es una CAIDA. La respuesta a
  // "¿se cayo?" es de Flex Device Care, no de aqui.
  if(d->ctxFreeFall){
    score -= 28;
    e->reasons |= FLEXTHEFT_R_FREEFALL;
  }
  // Se quedo quieto SIN haberse movido antes: cayo, o lo dejaron en una
  // mesa. Si hubo separacion y DESPUES se quedo quieto, eso no penaliza:
  // es exactamente lo que pasa cuando se lo llevan y se les cae.
  if(e->settled && !(e->reasons & FLEXTHEFT_R_ESCAPE)){
    score -= 30;
    e->reasons |= FLEXTHEFT_R_SETTLED;
  }
  // Giro grande sin traslacion: ensenarselo a alguien, girarlo para mirarlo.
  if(e->rotPeak >= p->twistRad * 1.4f && e->pullG < p->pullG * 0.9f){
    score -= 18;
    e->reasons |= FLEXTHEFT_R_TURNONLY;
  }
  // Estaba apoyado en una superficie: cogerlo de una mesa se parece a un
  // tiron. No se descarta -- tambien se puede arrebatar de una mesa --
  // pero tiene que traer el resto de pruebas.
  if(d->ctxRest && !d->ctxHeld){
    score -= 12;
    e->reasons |= FLEXTHEFT_R_TABLE;
  }

  if(score < 0)   score = 0;
  if(score > 100) score = 100;
  return (uint8_t)score;
}

// -------------------------------------------------------------
//  Cierre de la ventana: emite el veredicto
// -------------------------------------------------------------
static void theftEmit(FlexTheftDet* d, FlexTheftEvent* out){
  FlexTheftEvent e;
  memset(&e, 0, sizeof(e));
  e.tMs      = d->burstPeakMs;
  e.pullG    = d->peakLinG;
  e.jerkGs   = d->peakJerk;
  e.rotPeak  = d->peakRot;
  e.dirCoh   = (d->dirMag > 0.0001f) ? (theftNorm3(d->dirSum) / d->dirMag) : 0.0f;
  if(e.dirCoh > 1.0f) e.dirCoh = 1.0f;
  e.burstMs  = (uint16_t)(d->burstT0 && d->confirmT0 > d->burstT0
                          ? (d->confirmT0 - d->burstT0) : 0);
  e.escapeMs = (uint16_t)(d->escapeMs > 65535u ? 65535u : d->escapeMs);
  e.quietMs  = (uint16_t)(d->quietMs  > 65535u ? 65535u : d->quietMs);
  e.impactG  = d->postPeakG;
  e.hadImpact = (d->postPeakG >= d->p.impactG) ? 1 : 0;
  e.settled   = (d->quietMs >= d->p.settleMs) ? 1 : 0;

  e.confidence = theftScore(d, &e);
  // CONDICIONES, no puntos. La confianza dice CUANTO se parece; estas dos
  // dicen si se parece SIQUIERA.
  //
  //  · Direccion coherente. Un arrebato ES un tiron en una direccion. Sin
  //    eso no hay arrebato que valer: hay una oscilacion. Sin esta
  //    condicion, la primera zancada fuerte de una carrera sumaba
  //    intensidad + arranque + "estaba en la mano" + "sigue moviendose" y
  //    llegaba al umbral con una coherencia de 0,27 -- o sea, empujando a
  //    un lado y al otro. Esto es lo que separa correr de que te lo
  //    arranquen, y por eso es una puerta y no unos puntos mas.
  //  · Separacion posterior, SOLO en sensibilidad Baja (ver needEscape).
  bool dirOk    = (e.reasons & FLEXTHEFT_R_DIR) != 0;
  bool escapeOk = (!d->p.needEscape) || (e.reasons & FLEXTHEFT_R_ESCAPE);
  e.snatch = (e.confidence >= d->p.threshold && dirOk && escapeOk) ? 1 : 0;

  d->nEvents++;
  if(e.snatch) d->nSnatch++;
  d->last      = e;
  d->lastValid = 1;
  if(out) *out = e;
}

// -------------------------------------------------------------
//  El tick
// -------------------------------------------------------------
bool flexTheftFeed(FlexTheftDet* d, const FlexTheftSample* s, FlexTheftEvent* out){
  if(!d || !s) return false;
  // VALIDACION ANTES DE USAR NADA. Un informe corrupto del bus no puede
  // convertirse en un arrebato: la muestra se descarta entera.
  if(!theftFinite(s->ax) || !theftFinite(s->ay) || !theftFinite(s->az)) return false;

  const FlexTheftParams* p = &d->p;
  uint32_t t = s->tMs;

  float a[3] = { s->ax, s->ay, s->az };
  float g    = flexTheftMagG(a[0], a[1], a[2]);
  if(g > 24.0f) return false;                 // fuera del alcance fisico del modulo

  float rot = 0.0f;
  if(s->haveGyro && theftFinite(s->gx) && theftFinite(s->gy) && theftFinite(s->gz))
    rot = sqrtf(s->gx*s->gx + s->gy*s->gy + s->gz*s->gz);

  // ---- GRAVEDAD Y ACELERACION LINEAL ----
  // Con cuaternion es una conversion exacta. Sin el, un filtro de primer
  // orden sobre la propia aceleracion: no es tan bueno, pero es una
  // estimacion honesta y documentada, no un dato inventado.
  float up[3];
  if(s->haveQuat){
    float q[4] = { s->qi, s->qj, s->qk, s->qr };
    flexTheftUpFromQuat(q, up);
    d->gEma[0] = up[0]; d->gEma[1] = up[1]; d->gEma[2] = up[2];
    d->haveEma = 1;
  } else {
    float n = theftNorm3(a);
    if(n > 0.5f){
      float k = d->haveEma ? 0.03f : 1.0f;
      d->gEma[0] += ((a[0] / n) - d->gEma[0]) * k;
      d->gEma[1] += ((a[1] / n) - d->gEma[1]) * k;
      d->gEma[2] += ((a[2] / n) - d->gEma[2]) * k;
      d->haveEma = 1;
    }
    float en = theftNorm3(d->gEma);
    if(en > 0.1f){ up[0] = d->gEma[0]/en; up[1] = d->gEma[1]/en; up[2] = d->gEma[2]/en; }
    else         { up[0] = 0; up[1] = 0; up[2] = 1; }
  }
  float lin[3] = { a[0] - G_MS2 * up[0], a[1] - G_MS2 * up[1], a[2] - G_MS2 * up[2] };
  float linG   = theftNorm3(lin) / G_MS2;

  // ---- PRIMERA MUESTRA ----
  if(!d->started){
    d->started  = 1;
    d->lastMs   = t;
    d->stateMs  = t;
    d->prevLinG = linG;
    d->prevLin[0] = lin[0]; d->prevLin[1] = lin[1]; d->prevLin[2] = lin[2];
    d->havePrev = 1;
    d->linEma   = linG;
    d->rotEma   = rot;
    theftRingPush(d, t, g, lin, rot);
    return false;
  }
  // El reloj retrocedio (muestras desordenadas): se descarta en vez de
  // calcular una duracion negativa.
  if(t < d->lastMs){ d->lastMs = t; return false; }
  uint32_t dt = t - d->lastMs;
  d->lastMs = t;
  // Un hueco largo (el sensor se perdio y volvio) invalida la secuencia:
  // no se puede juzgar un tiron con la mitad de las muestras ausentes.
  if(dt > 400u){
    theftToNormal(d, t);
    d->rN = d->rHead = 0;
    d->havePrev = 0;
    d->stepN = d->stepHead = 0;
  }

  float dtS  = (dt > 0) ? (dt / 1000.0f) : 0.02f;
  float jerk = d->havePrev ? (fabsf(linG - d->prevLinG) / dtS) : 0.0f;

  theftRingPush(d, t, g, lin, rot);

  // Medias moviles del fondo (REST / CARRY). Se congelan durante un caso
  // abierto: el propio tiron no puede redefinir lo que es "normal".
  if(d->state == FLEXTHEFT_REST || d->state == FLEXTHEFT_CARRY){
    d->linEma += (linG - d->linEma) * 0.06f;
    d->rotEma += (rot  - d->rotEma) * 0.06f;
  }
  // Picos moderados: alimentan la cadencia de pasos.
  if(linG >= 0.35f && linG < p->pullG) theftPushStep(d, t);

  bool quiet = (fabsf(g - 1.0f) <= p->settleTolG) && (rot <= p->settleRotRad);
  bool emitted = false;

  switch(d->state){
    case FLEXTHEFT_REST:
    case FLEXTHEFT_CARRY: {
      d->state = (d->linEma <= p->restLinG && d->rotEma <= 0.05f)
                 ? FLEXTHEFT_REST : FLEXTHEFT_CARRY;
      if(d->blockUntilMs && t < d->blockUntilMs) break;   // periodo refractario
      // PUERTA DE ENTRADA. Deliberadamente floja: abrir un caso no cuesta
      // nada y no genera evento por si sola. Lo que decide es el cierre
      // del tiron, mas abajo.
      if(linG >= p->pullG * 0.55f && jerk >= p->jerkGs * 0.6f){
        theftPreContext(d, t);
        d->ctxWalk    = theftWalkPattern(d, t) ? 1 : 0;
        d->state      = FLEXTHEFT_SUSPECT;
        d->stateMs    = t;
        d->burstT0    = t;
        d->burstPeakMs = t;
        d->burstQuietMs = 0;
        d->peakLinG   = linG;
        d->peakJerk   = jerk;
        d->peakRot    = rot;
        d->revInBurst = 0;
        d->dirSum[0] = d->dirSum[1] = d->dirSum[2] = 0.0f;
        d->dirMag     = 0.0f;
      }
      break;
    }

    case FLEXTHEFT_SUSPECT: {
      if(linG > d->peakLinG){ d->peakLinG = linG; d->burstPeakMs = t; }
      if(jerk > d->peakJerk) d->peakJerk = jerk;
      if(rot  > d->peakRot)  d->peakRot  = rot;
      // COHERENCIA DE DIRECCION. Se suma la direccion UNITARIA ponderada
      // por la magnitud: el modulo de la suma dividido por la suma de
      // magnitudes vale 1 si todo empuja al mismo sitio y baja hacia 0
      // segun se invierte. Un tiron da 0,85-0,98; agitar, 0,2-0,5.
      if(linG > 0.30f){
        float inv = 1.0f / (linG * G_MS2);
        float dir[3] = { lin[0]*inv, lin[1]*inv, lin[2]*inv };
        d->dirSum[0] += dir[0] * linG;
        d->dirSum[1] += dir[1] * linG;
        d->dirSum[2] += dir[2] * linG;
        d->dirMag    += linG;
        if(d->havePrev){
          float pn = theftNorm3(d->prevLin);
          if(pn > 0.30f * G_MS2){
            float pin = 1.0f / pn;
            float dot = dir[0]*d->prevLin[0]*pin + dir[1]*d->prevLin[1]*pin
                      + dir[2]*d->prevLin[2]*pin;
            if(dot < -0.25f && d->revInBurst < 255) d->revInBurst++;
          }
        }
      }
      // Fin del tiron: la aceleracion lineal cae y se queda abajo.
      if(linG < p->pullG * 0.30f) d->burstQuietMs += dt;
      else                        d->burstQuietMs = 0;

      uint32_t dur = t - d->burstT0;
      bool over = (d->burstQuietMs >= 40u) || (dur >= (uint32_t)p->burstMaxMs + 120u);
      if(!over) break;

      // GATE. No todo empujon produce un evento: sin intensidad o con una
      // duracion que no es la de un tiron, el caso se cierra en silencio.
      // Asi el historial no se llena de descartes triviales.
      if(d->peakLinG < p->pullG || dur < (uint32_t)p->burstMinMs
                                || dur > (uint32_t)p->burstMaxMs){
        theftToNormal(d, t);
        break;
      }
      d->state     = FLEXTHEFT_CONFIRM;
      d->stateMs   = t;
      d->confirmT0 = t;
      d->escapeMs  = 0;
      d->quietMs   = 0;
      d->postPeakG = 0.0f;
      break;
    }

    case FLEXTHEFT_CONFIRM: {
      if(g > d->postPeakG) d->postPeakG = g;
      if(linG > p->escapeLinG) d->escapeMs += dt;
      if(quiet)                d->quietMs  += dt;
      if(t - d->confirmT0 < (uint32_t)p->escapeWinMs) break;
      d->state = FLEXTHEFT_EVAL;
      // cae al EVAL de abajo en esta misma muestra
      theftEmit(d, out);
      emitted = true;
      d->blockUntilMs = t + p->refractMs;
      theftToNormal(d, t);
      break;
    }

    default:
      theftToNormal(d, t);
      break;
  }

  d->prevLinG = linG;
  d->prevLin[0] = lin[0]; d->prevLin[1] = lin[1]; d->prevLin[2] = lin[2];
  d->havePrev = 1;
  return emitted;
}

// -------------------------------------------------------------
//  Consulta
// -------------------------------------------------------------
int flexTheftState(const FlexTheftDet* d){ return d ? (int)d->state : FLEXTHEFT_REST; }

static const char* THEFT_ST[5][5] = {
  {"En reposo","At rest","Au repos","Em repouso","A riposo"},
  {"Movimiento normal","Normal motion","Mouvement normal","Movimento normal","Movimento normale"},
  {"Movimiento sospechoso","Suspicious motion","Mouvement suspect","Movimento suspeito","Movimento sospetto"},
  {"Comprobando separaci\xC3\xB3n","Checking separation","V\xC3\xA9rification","Verificando separa\xC3\xA7\xC3\xA3o","Verifica separazione"},
  {"Evaluando","Evaluating","\xC3\x89valuation","Avaliando","Valutazione"}
};
const char* flexTheftStateName(int state, int lang){
  if(state < 0 || state > FLEXTHEFT_EVAL) state = FLEXTHEFT_REST;
  if(lang < 0 || lang > 4) lang = 0;
  return THEFT_ST[state][lang];
}

static const char* THEFT_SENS[3][5] = {
  {"Baja","Low","Faible","Baixa","Bassa"},
  {"Normal","Normal","Normale","Normal","Normale"},
  {"Alta","High","\xC3\x89lev\xC3\xA9""e","Alta","Alta"}
};
const char* flexTheftSensName(uint8_t sens, int lang){
  if(sens > FLEXTHEFT_SENS_HIGH) sens = FLEXTHEFT_SENS_NORMAL;
  if(lang < 0 || lang > 4) lang = 0;
  return THEFT_SENS[sens][lang];
}

uint32_t flexTheftEventCount(const FlexTheftDet* d){  return d ? d->nEvents : 0; }
uint32_t flexTheftSnatchCount(const FlexTheftDet* d){ return d ? d->nSnatch : 0; }

bool flexTheftLast(const FlexTheftDet* d, FlexTheftEvent* out){
  if(!d || !d->lastValid) return false;
  if(out) *out = d->last;
  return true;
}

// Actividad reciente 0..255: media de |aceleracion lineal| del ultimo
// medio segundo, escalada de forma que 1 g llene la barra. Es una MEDIDA
// de la ventana, no una animacion decorativa: sin muestras devuelve 0 y
// quien dibuja ensena la linea plana que corresponde.
uint8_t flexTheftActivity(const FlexTheftDet* d){
  if(!d || d->rN == 0) return 0;
  uint32_t now = d->lastMs;
  float sum = 0.0f; int n = 0;
  for(uint16_t i = 0; i < d->rN; i++){
    const FlexTheftSlot* s = theftRingAt(d, i);
    if(now < s->tMs || now - s->tMs > 500u) continue;
    float lin[3] = { s->lx / 100.0f, s->ly / 100.0f, s->lz / 100.0f };
    sum += theftNorm3(lin) / G_MS2;
    n++;
  }
  if(n == 0) return 0;
  float v = (sum / (float)n) * 255.0f;
  if(v < 0.0f) v = 0.0f;
  if(v > 255.0f) v = 255.0f;
  return (uint8_t)v;
}
