// #############################################################
//  DETECCION DE CAIDAS  ·  prueba de host (sin placa)
//  ------------------------------------------------------------
//  FlexOS_FallDetect.cpp es logica pura: entra una muestra medida y
//  sale un veredicto. Eso se puede ejercitar ENTERO en el PC, que es
//  justo lo que hace falta -- comprobar en la mano que caminar no
//  dispara una caida obliga a caminar con el aparato en la mano.
//
//  Cada escenario sintetiza una senal con la forma del gesto real y
//  comprueba el VEREDICTO, no los numeros internos:
//    · caida completa (vuelo + golpe + vuelco + reposo)  -> CAIDA
//    · golpe en la mesa sin vuelo                        -> normal
//    · caminar                                           -> normal
//    · dejarlo en la mesa                                -> normal
//    · agitarlo                                          -> normal
//    · vuelo largo sin impacto                           -> sin evento
// #############################################################
#include "FlexOS_FallDetect.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int fallos = 0;
static void ok(const char* que, bool cond){
  printf("  %s %s\n", cond ? "OK  " : "FALLA", que);
  if(!cond) fallos++;
}

#define G 9.80665f
#define DT 20            // 50 Hz, la cadencia real del driver

// Un pequeno generador de escenarios. `feed` devuelve el ultimo evento
// evaluado durante el tramo, o confidence = 255 si no hubo ninguno.
struct Sim {
  FlexFallDet d;
  uint32_t t;
  FlexFallEvent last;
  bool got;
  float qi, qj, qk, qr;
};

static void simInit(Sim* s){
  flexFallInit(&s->d);
  s->t = 1000;
  s->got = false;
  memset(&s->last, 0, sizeof(s->last));
  s->qi = 0; s->qj = 0; s->qk = 0; s->qr = 1;    // en reposo, boca arriba
}

// Empuja `ms` milisegundos con una aceleracion vertical de `gz` g y un
// giro de `rot` rad/s.
static void simRun(Sim* s, uint32_t ms, float gz, float rot){
  for(uint32_t e = 0; e < ms; e += DT){
    FlexFallSample smp;
    memset(&smp, 0, sizeof(smp));
    smp.tMs = s->t;
    smp.ax = 0; smp.ay = 0; smp.az = gz * G;
    smp.gx = rot; smp.gy = 0; smp.gz = 0;
    smp.qi = s->qi; smp.qj = s->qj; smp.qk = s->qk; smp.qr = s->qr;
    smp.haveQuat = 1;
    FlexFallEvent ev;
    if(flexFallFeed(&s->d, &smp, &ev)){ s->last = ev; s->got = true; }
    s->t += DT;
  }
}

// Cambia la postura a `deg` grados de giro alrededor de X.
static void simTilt(Sim* s, float deg){
  float h = deg * 0.5f * 3.14159265f / 180.0f;
  s->qi = sinf(h); s->qj = 0; s->qk = 0; s->qr = cosf(h);
}

// -------------------------------------------------------------
static void testCaidaCompleta(){
  printf("Caida completa (vuelo + impacto + vuelco + reposo)\n");
  Sim s; simInit(&s);
  simRun(&s, 1200, 1.0f, 0.02f);      // quieto: fija la postura de referencia
  simRun(&s, 260,  0.10f, 1.0f);      // vuelo
  simTilt(&s, 90.0f);                 // el aparato acaba de canto
  simRun(&s, 60,   5.2f, 6.0f);       // golpe con vuelco
  simRun(&s, 1400, 1.0f, 0.01f);      // se queda quieto en el suelo
  ok("se evaluo un evento", s.got);
  ok("veredicto = caida", s.got && s.last.fall == 1);
  ok("confianza alta", s.got && s.last.confidence >= 70);
  ok("motivo: caida libre", s.got && (s.last.reasons & FLEXFALL_R_FREEFALL));
  ok("motivo: impacto", s.got && (s.last.reasons & FLEXFALL_R_IMPACT));
  ok("motivo: cambio de orientacion", s.got && (s.last.reasons & FLEXFALL_R_ORIENT));
  ok("motivo: estabilizacion", s.got && (s.last.reasons & FLEXFALL_R_SETTLED));
  ok("contador de caidas = 1", flexFallFallCount(&s.d) == 1);
}

static void testGolpeMesa(){
  printf("Golpe seco en la mesa (sin vuelo, sin vuelco)\n");
  Sim s; simInit(&s);
  simRun(&s, 1200, 1.0f, 0.02f);
  simRun(&s, 40,   3.2f, 0.4f);       // el golpe llega sin caida libre
  simRun(&s, 1400, 1.0f, 0.01f);
  ok("se evaluo un evento", s.got);
  ok("veredicto = normal", s.got && s.last.fall == 0);
  ok("no se conto como caida", flexFallFallCount(&s.d) == 0);
}

static void testCaminar(){
  printf("Caminar con el aparato encima\n");
  Sim s; simInit(&s);
  simRun(&s, 1000, 1.0f, 0.02f);
  // Zancadas regulares: pico moderado cada 500 ms.
  for(int paso = 0; paso < 6; paso++){
    simRun(&s, 60,  1.9f, 0.8f);
    simRun(&s, 440, 1.05f, 0.3f);
  }
  // Un paso mas fuerte al final: es el caso que un umbral simple
  // confundiria con una caida.
  simRun(&s, 40,   2.9f, 1.2f);
  simRun(&s, 1400, 1.0f, 0.02f);
  ok("no se declaro ninguna caida", flexFallFallCount(&s.d) == 0);
}

static void testDejarEnLaMesa(){
  printf("Dejarlo en la mesa (descenso corto + toque)\n");
  Sim s; simInit(&s);
  simRun(&s, 1200, 1.0f, 0.02f);
  simRun(&s, 40,   0.55f, 0.6f);      // se afloja la mano: no llega a vuelo
  simRun(&s, 40,   1.9f,  0.5f);      // apoyo
  simRun(&s, 1400, 1.0f,  0.01f);
  ok("no se declaro ninguna caida", flexFallFallCount(&s.d) == 0);
}

static void testAgitar(){
  printf("Agitarlo en la mano\n");
  Sim s; simInit(&s);
  simRun(&s, 1000, 1.0f, 0.02f);
  for(int i = 0; i < 10; i++){
    simRun(&s, 80, 2.8f, 4.0f);
    simRun(&s, 80, 0.6f, 4.0f);       // el zarandeo pasa cerca del vuelo
  }
  simRun(&s, 600, 1.4f, 2.0f);        // sigue en la mano: NO se estabiliza
  ok("no se declaro ninguna caida", flexFallFallCount(&s.d) == 0);
}

static void testVueloSinImpacto(){
  printf("Vuelo largo sin impacto (lectura rota o sensor en el aire)\n");
  Sim s; simInit(&s);
  simRun(&s, 1200, 1.0f, 0.02f);
  simRun(&s, 1800, 0.05f, 0.1f);      // por encima de freeFallMaxMs
  simRun(&s, 600,  1.0f, 0.02f);
  ok("no se evaluo ningun evento", flexFallEventCount(&s.d) == 0);
  ok("el detector volvio al reposo", flexFallState(&s.d) == FLEXFALL_IDLE);
}

static void testSinFusion(){
  printf("Caida SIN sensor fusion: no se puntua la orientacion\n");
  FlexFallDet d; flexFallInit(&d);
  uint32_t t = 1000;
  FlexFallEvent ev; bool got = false;
  auto push = [&](uint32_t ms, float gz, float rot){
    for(uint32_t e = 0; e < ms; e += DT){
      FlexFallSample s; memset(&s, 0, sizeof(s));
      s.tMs = t; s.az = gz * G; s.gx = rot; s.haveQuat = 0;
      FlexFallEvent o;
      if(flexFallFeed(&d, &s, &o)){ ev = o; got = true; }
      t += DT;
    }
  };
  push(1200, 1.0f, 0.02f);
  push(260,  0.10f, 1.0f);
  push(60,   5.2f, 6.0f);
  push(1400, 1.0f, 0.01f);
  ok("se evaluo un evento", got);
  ok("orientacion = 0 (no habia cuaternion)", got && ev.orientDeg == 0.0f);
  ok("no se marco el motivo de orientacion", got && !(ev.reasons & FLEXFALL_R_ORIENT));
  // Sigue pudiendo ser caida: vuelo + impacto + estabilizacion bastan.
  ok("veredicto = caida", got && ev.fall == 1);
}

static void testParametrosYReset(){
  printf("Parametros centralizados y reinicio\n");
  FlexFallParams p; flexFallDefaults(&p);
  ok("umbral de fabrica en rango", p.threshold > 0 && p.threshold <= 100);
  ok("caida libre por debajo de 1 g", p.freeFallG > 0.0f && p.freeFallG < 1.0f);
  ok("impacto por encima de 1 g", p.impactG > 1.0f);
  ok("impacto fuerte dentro del fondo de escala del BNO085", p.impactBigG <= 8.0f);

  // Umbral imposible: la misma caida deja de declararse.
  Sim s; simInit(&s);
  p.threshold = 100;
  flexFallSetParams(&s.d, &p);
  simRun(&s, 1200, 1.0f, 0.02f);
  simRun(&s, 260,  0.10f, 1.0f);
  simTilt(&s, 90.0f);
  simRun(&s, 60,   5.2f, 6.0f);
  simRun(&s, 1400, 1.0f, 0.01f);
  ok("con umbral 100 no se declara caida", flexFallFallCount(&s.d) == 0);
  ok("pero el evento SI se evaluo", flexFallEventCount(&s.d) == 1);

  flexFallReset(&s.d);
  ok("reset deja el detector en reposo", flexFallState(&s.d) == FLEXFALL_IDLE);
  FlexFallEvent e;
  ok("el ultimo evento sobrevive al reset", flexFallLast(&s.d, &e));
}

static void testUtilidades(){
  printf("Utilidades geometricas\n");
  ok("|(0,0,9.80665)| = 1 g", fabsf(flexFallMagG(0, 0, G) - 1.0f) < 0.001f);
  float a[4] = {0, 0, 0, 1};
  float b[4] = {0, 0, 0, 1};
  ok("misma postura -> 0 grados", flexFallQuatAngleDeg(a, b) < 0.01f);
  float h = 45.0f * 0.5f * 3.14159265f / 180.0f;
  float c[4] = {sinf(h), 0, 0, cosf(h)};
  ok("45 grados medidos", fabsf(flexFallQuatAngleDeg(a, c) - 45.0f) < 0.5f);
  float z[4] = {0, 0, 0, 0};
  ok("cuaternion no unitario -> 0 (no se inventa angulo)", flexFallQuatAngleDeg(a, z) == 0.0f);
  ok("nombre de estado localizado", strcmp(flexFallStateName(FLEXFALL_IDLE, 1), "Idle") == 0);
}

int main(){
  printf("== Deteccion de caidas (logica pura) ==\n");
  testCaidaCompleta();
  testGolpeMesa();
  testCaminar();
  testDejarEnLaMesa();
  testAgitar();
  testVueloSinImpacto();
  testSinFusion();
  testParametrosYReset();
  testUtilidades();
  if(fallos){ printf("\n%d comprobacion(es) fallaron\n", fallos); return 1; }
  printf("\nTodo correcto.\n");
  return 0;
}
