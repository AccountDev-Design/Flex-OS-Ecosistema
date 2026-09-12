// #############################################################
//  PROTECCION CONTRA ROBO  ·  prueba de host (sin placa)
//  ------------------------------------------------------------
//  FlexOS_Theft.cpp es logica pura: entran muestras medidas y sale un
//  veredicto. Eso se puede ejercitar ENTERO en el PC, que es justo lo
//  que hace falta: comprobar en la mano que correr no dispara un
//  arrebato obliga a correr con el aparato en la mano, y comprobar que
//  un arrebato SI lo dispara obliga a que alguien te lo arranque.
//
//  Cada escenario sintetiza una senal con la forma del gesto real y
//  comprueba el VEREDICTO, no los numeros internos:
//    · arrebato tipico                      -> POSIBLE ARREBATO
//    · arrebato + caida posterior           -> POSIBLE ARREBATO con impacto
//    · arrebato y se lo llevan andando      -> POSIBLE ARREBATO
//    · caida sola                           -> normal  (es de Device Care)
//    · caminar / correr                     -> normal
//    · dejarlo en la mesa / recogerlo       -> normal
//    · agitarlo                             -> normal
//    · golpecito                            -> normal (ni evento)
//    · ensenarselo a alguien (giro puro)    -> normal
//    · sensibilidad Baja/Normal/Alta        -> cambian el veredicto
//    · datos invalidos / perdida del sensor -> ni cuelga ni inventa
// #############################################################
#include "FlexOS_Theft.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int fallos = 0;
static void ok(const char* que, bool cond){
  printf("  %s %s\n", cond ? "OK  " : "FALLA", que);
  if(!cond) fallos++;
}

#define G   9.80665f
#define DT  20              // 50 Hz, la cadencia real del driver

// -------------------------------------------------------------
//  Simulador
//  ------------------------------------------------------------
//  Genera lo que el BNO085 entrega de verdad: aceleracion CON
//  gravedad y el cuaternion del rotation vector. La aceleracion lineal
//  la pone cada escenario; la gravedad la anade el simulador segun la
//  postura, igual que hace el sensor.
// -------------------------------------------------------------
struct Sim {
  FlexTheftDet d;
  uint32_t t;
  float q[4];                 // i, j, k, real
  FlexTheftEvent last;
  bool got;
  int  nEvents;
};

static void simInit(Sim* s, uint8_t sens){
  flexTheftInit(&s->d, sens);
  s->t = 5000;
  s->q[0] = 0; s->q[1] = 0; s->q[2] = 0; s->q[3] = 1;   // plano, boca arriba
  memset(&s->last, 0, sizeof(s->last));
  s->got = false;
  s->nEvents = 0;
}

// Gira `deg` grados alrededor del eje X (cambia la postura de verdad).
static void simTilt(Sim* s, float deg){
  float h = deg * 0.5f * 3.14159265f / 180.0f;
  s->q[0] = sinf(h); s->q[1] = 0; s->q[2] = 0; s->q[3] = cosf(h);
}

// UNA muestra: `lin` en g (aceleracion lineal, sin gravedad) y `rot` en rad/s.
static void simStep(Sim* s, float lx, float ly, float lz, float rot){
  float up[3];
  flexTheftUpFromQuat(s->q, up);
  FlexTheftSample smp;
  memset(&smp, 0, sizeof(smp));
  smp.tMs = s->t;
  smp.ax = lx * G + G * up[0];
  smp.ay = ly * G + G * up[1];
  smp.az = lz * G + G * up[2];
  smp.gx = rot; smp.gy = 0; smp.gz = 0;
  smp.qi = s->q[0]; smp.qj = s->q[1]; smp.qk = s->q[2]; smp.qr = s->q[3];
  smp.haveGyro = 1;
  smp.haveQuat = 1;
  FlexTheftEvent ev;
  if(flexTheftFeed(&s->d, &smp, &ev)){ s->last = ev; s->got = true; s->nEvents++; }
  s->t += DT;
}

// Caida libre de verdad: el acelerometro deja de medir peso. No se puede
// expresar como "aceleracion lineal": hay que anular la gravedad.
static void simFreeFall(Sim* s, uint32_t ms, float rot){
  float up[3];
  for(uint32_t e = 0; e < ms; e += DT){
    flexTheftUpFromQuat(s->q, up);
    FlexTheftSample smp;
    memset(&smp, 0, sizeof(smp));
    smp.tMs = s->t;
    smp.ax = 0.04f * G * up[0];        // no baja a cero exacto: roza y rota
    smp.ay = 0.04f * G * up[1];
    smp.az = 0.04f * G * up[2];
    smp.gx = rot;
    smp.qi = s->q[0]; smp.qj = s->q[1]; smp.qk = s->q[2]; smp.qr = s->q[3];
    smp.haveGyro = 1; smp.haveQuat = 1;
    FlexTheftEvent ev;
    if(flexTheftFeed(&s->d, &smp, &ev)){ s->last = ev; s->got = true; s->nEvents++; }
    s->t += DT;
  }
}

// -------------------------------------------------------------
//  Tramos reutilizables
// -------------------------------------------------------------

// EN LA MANO, quieto: micromovimiento real (0,02-0,04 g) y un giro minimo.
// No es ruido decorativo: es lo que distingue una mano de una mesa.
static void simHeld(Sim* s, uint32_t ms){
  int k = 0;
  for(uint32_t e = 0; e < ms; e += DT, k++){
    float ph = (float)k * 0.31f;
    simStep(s, 0.020f * sinf(ph), 0.016f * cosf(ph * 1.7f), 0.024f * sinf(ph * 0.6f),
            0.05f + 0.03f * sinf(ph * 0.9f));
  }
}

// APOYADO EN UNA MESA: practicamente cero en todo.
static void simTable(Sim* s, uint32_t ms){
  for(uint32_t e = 0; e < ms; e += DT) simStep(s, 0.0015f, -0.001f, 0.002f, 0.004f);
}

// EL TIRON. Empuje DIRECCIONAL (siempre hacia +X) con arranque de una sola
// muestra: es lo que separa un arrebato de un movimiento voluntario.
//   peak    intensidad, en g de aceleracion lineal
//   holdMs  cuanto se mantiene arriba
//   rot     giro acoplado (los dedos retienen el aparato un instante)
static void simYank(Sim* s, float peak, uint32_t holdMs, float rot){
  simStep(s, peak * 0.72f, peak * 0.10f, 0.0f, rot * 0.6f);    // arranque: 1 muestra
  simStep(s, peak,         peak * 0.14f, 0.0f, rot);
  for(uint32_t e = 0; e < holdMs; e += DT)
    simStep(s, peak * 0.90f, peak * 0.12f, 0.0f, rot * 0.8f);
  simStep(s, peak * 0.55f, peak * 0.08f, 0.0f, rot * 0.5f);    // decaida
  simStep(s, peak * 0.26f, peak * 0.04f, 0.0f, rot * 0.3f);
  simStep(s, peak * 0.10f, 0.0f,         0.0f, rot * 0.1f);
}

// SE LO LLEVAN: movimiento sostenido, con la irregularidad de una carrera.
static void simCarry(Sim* s, uint32_t ms){
  int k = 0;
  for(uint32_t e = 0; e < ms; e += DT, k++){
    float ph = (float)k * 0.44f;
    simStep(s, 0.30f * sinf(ph), 0.22f * cosf(ph * 1.3f), 0.34f * sinf(ph * 0.8f),
            0.9f * sinf(ph * 1.1f));
  }
}

// CAMINAR: picos verticales moderados y MUY regulares (~1,8 pasos/s).
static void simWalk(Sim* s, int pasos){
  for(int i = 0; i < pasos; i++){
    for(uint32_t e = 0; e < 420; e += DT) simStep(s, 0.05f, 0.04f, 0.10f, 0.15f);
    simStep(s, 0.10f, 0.08f,  0.55f, 0.7f);     // apoyo
    simStep(s, 0.08f, 0.05f, -0.45f, 0.6f);     // rebote (direccion INVERTIDA)
    simStep(s, 0.04f, 0.02f,  0.18f, 0.3f);
  }
}

// CORRER: lo mismo pero mas fuerte y mas rapido (~3 pasos/s).
static void simRun(Sim* s, int pasos){
  for(int i = 0; i < pasos; i++){
    for(uint32_t e = 0; e < 200; e += DT) simStep(s, 0.12f, 0.10f, 0.25f, 0.5f);
    simStep(s, 0.30f, 0.20f,  1.70f, 1.6f);
    simStep(s, 0.25f, 0.15f,  1.30f, 1.4f);
    simStep(s, 0.20f, 0.10f, -1.40f, 1.2f);     // la direccion se INVIERTE
    simStep(s, 0.10f, 0.05f, -0.60f, 0.7f);
  }
}

// AGITARLO: oscilacion de 3 Hz. Fuerte, pero cambia de sentido cada 160 ms.
static void simShake(Sim* s, uint32_t ms, float amp){
  int k = 0;
  for(uint32_t e = 0; e < ms; e += DT, k++){
    float ph = (float)k * (2.0f * 3.14159265f * 3.0f * DT / 1000.0f);
    simStep(s, amp * sinf(ph), amp * 0.3f * sinf(ph), 0.0f, 1.4f * cosf(ph));
  }
}

// -------------------------------------------------------------
static void testArrebato(){
  printf("Arrebato tipico (en la mano -> tiron direccional -> se lo llevan)\n");
  Sim s; simInit(&s, FLEXTHEFT_SENS_NORMAL);
  simHeld(&s, 2000);
  simYank(&s, 2.6f, 80, 4.0f);
  simCarry(&s, 2600);
  ok("se evaluo un evento", s.got);
  ok("veredicto = posible arrebato", s.got && s.last.snatch == 1);
  ok("confianza alta", s.got && s.last.confidence >= 80);
  ok("motivo: estaba en la mano", s.got && (s.last.reasons & FLEXTHEFT_R_HELD));
  ok("motivo: intensidad del tiron", s.got && (s.last.reasons & FLEXTHEFT_R_PULL));
  ok("motivo: arranque brusco", s.got && (s.last.reasons & FLEXTHEFT_R_JERK));
  ok("motivo: direccion coherente", s.got && (s.last.reasons & FLEXTHEFT_R_DIR));
  ok("motivo: giro acoplado", s.got && (s.last.reasons & FLEXTHEFT_R_TWIST));
  ok("motivo: separacion posterior", s.got && (s.last.reasons & FLEXTHEFT_R_ESCAPE));
  ok("NO se marco como caida libre", s.got && !(s.last.reasons & FLEXTHEFT_R_FREEFALL));
  ok("sin impacto posterior", s.got && s.last.hadImpact == 0);
  ok("contador de arrebatos = 1", flexTheftSnatchCount(&s.d) == 1);
  ok("duracion del tiron dentro del rango humano",
     s.got && s.last.burstMs >= 70 && s.last.burstMs <= 400);
}

static void testArrebatoMasCaida(){
  printf("Arrebato + caida (se lo arrancan, huyen y se les cae)\n");
  Sim s; simInit(&s, FLEXTHEFT_SENS_NORMAL);
  simHeld(&s, 2000);
  simYank(&s, 2.8f, 80, 4.5f);
  simCarry(&s, 900);                 // huida
  simFreeFall(&s, 260, 2.0f);        // se les escapa de la mano
  simTilt(&s, 80.0f);
  simStep(&s, 4.6f, 1.2f, 2.0f, 7.0f);   // golpe contra el suelo
  simStep(&s, 1.4f, 0.4f, 0.6f, 2.0f);
  simTable(&s, 1600);                // se queda tirado, quieto
  ok("se evaluo un evento", s.got);
  ok("veredicto = posible arrebato (la caida NO lo cancela)", s.got && s.last.snatch == 1);
  ok("se anoto el impacto posterior", s.got && s.last.hadImpact == 1);
  ok("motivo: impacto", s.got && (s.last.reasons & FLEXTHEFT_R_IMPACT));
  ok("hubo separacion antes del impacto", s.got && (s.last.reasons & FLEXTHEFT_R_ESCAPE));
  ok("quedarse quieto DESPUES de la huida no penaliza",
     s.got && !(s.last.reasons & FLEXTHEFT_R_SETTLED));
  ok("pico del impacto por encima de 2 g", s.got && s.last.impactG > 2.0f);
}

static void testArrebatoYSeLoLlevanAndando(){
  printf("Arrebato y se lo llevan andando\n");
  Sim s; simInit(&s, FLEXTHEFT_SENS_NORMAL);
  simHeld(&s, 2000);
  simYank(&s, 2.4f, 70, 3.6f);
  simWalk(&s, 8);                    // 3,8 s: la ventana posterior (2,5 s) se cierra dentro
  ok("se evaluo un evento", s.got);
  ok("veredicto = posible arrebato", s.got && s.last.snatch == 1);
  ok("motivo: separacion posterior", s.got && (s.last.reasons & FLEXTHEFT_R_ESCAPE));
}

static void testCaidaSola(){
  printf("Caida sola (vuelo + impacto + reposo)  ->  NO es un arrebato\n");
  Sim s; simInit(&s, FLEXTHEFT_SENS_NORMAL);
  simHeld(&s, 2000);
  simFreeFall(&s, 280, 1.5f);
  simTilt(&s, 90.0f);
  simStep(&s, 5.0f, 1.0f, 1.5f, 7.5f);
  simStep(&s, 1.6f, 0.3f, 0.5f, 2.2f);
  simTable(&s, 2600);
  ok("no se declara arrebato", !(s.got && s.last.snatch));
  if(s.got){
    ok("si hubo evaluacion, la confianza es baja", s.last.confidence < 68);
  } else {
    ok("si hubo evaluacion, la confianza es baja", true);
  }
  ok("contador de arrebatos = 0", flexTheftSnatchCount(&s.d) == 0);
}

// EL CASO DIFICIL. Una caida cuyo golpe dura lo suficiente como para pasar por
// un tiron: intensidad alta, arranque brusco y direccion perfectamente
// coherente (hacia abajo). Sin la penalizacion de caida libre, esto sumaria
// intensidad + arranque + direccion y se declararia arrebato. Es exactamente
// la confusion que el enunciado prohibe, y la que separa esta funcion de Flex
// Device Care.
static void testCaidaConGolpeLargo(){
  printf("Caida con golpe LARGO y direccional  ->  sigue sin ser un arrebato\n");
  Sim s; simInit(&s, FLEXTHEFT_SENS_NORMAL);
  simHeld(&s, 2000);
  simFreeFall(&s, 240, 1.2f);          // el vuelo termina EN el golpe, no antes
  simTilt(&s, 70.0f);
  simStep(&s, 0.0f, 0.0f, 2.6f, 6.0f); // golpe que ademas se prolonga: rebota y
  simStep(&s, 0.0f, 0.0f, 2.9f, 5.0f); // arrastra, siempre hacia el mismo lado
  for(uint32_t e = 0; e < 120; e += DT) simStep(&s, 0.0f, 0.0f, 2.2f, 3.0f);
  simStep(&s, 0.0f, 0.0f, 0.8f, 1.0f);
  simTable(&s, 2800);
  ok("no se declara arrebato", !(s.got && s.last.snatch));
  if(s.got){
    ok("se reconocio la caida libre previa", (s.last.reasons & FLEXTHEFT_R_FREEFALL) != 0);
    ok("y la confianza queda por debajo del umbral", s.last.confidence < 68);
  } else {
    ok("se reconocio la caida libre previa", true);
    ok("y la confianza queda por debajo del umbral", true);
  }
  ok("contador de arrebatos = 0", flexTheftSnatchCount(&s.d) == 0);
}

static void testCaminar(){
  printf("Caminar\n");
  Sim s; simInit(&s, FLEXTHEFT_SENS_NORMAL);
  simHeld(&s, 1200);
  simWalk(&s, 14);
  ok("no se declara arrebato", !(s.got && s.last.snatch));
  ok("contador de arrebatos = 0", flexTheftSnatchCount(&s.d) == 0);
}

static void testCorrer(){
  printf("Correr\n");
  Sim s; simInit(&s, FLEXTHEFT_SENS_NORMAL);
  simHeld(&s, 1200);
  simRun(&s, 16);
  ok("no se declara arrebato", !(s.got && s.last.snatch));
  ok("contador de arrebatos = 0", flexTheftSnatchCount(&s.d) == 0);
}

static void testMesa(){
  printf("Dejarlo sobre la mesa y volver a cogerlo\n");
  Sim s; simInit(&s, FLEXTHEFT_SENS_NORMAL);
  simHeld(&s, 1800);
  // bajarlo despacio
  for(uint32_t e = 0; e < 500; e += DT) simStep(&s, 0.02f, 0.01f, -0.28f, 0.25f);
  simStep(&s, 0.10f, 0.05f, 1.35f, 1.1f);       // apoyo seco
  simStep(&s, 0.05f, 0.02f, 0.40f, 0.4f);
  simTable(&s, 2800);
  ok("dejarlo en la mesa no declara arrebato", !(s.got && s.last.snatch));
  // recogerlo
  simStep(&s, 0.20f, 0.10f, 0.75f, 1.0f);
  for(uint32_t e = 0; e < 420; e += DT) simStep(&s, 0.10f, 0.06f, 0.35f, 0.6f);
  simHeld(&s, 2800);
  ok("recogerlo no declara arrebato", !(s.got && s.last.snatch));
  ok("contador de arrebatos = 0", flexTheftSnatchCount(&s.d) == 0);
}

static void testAgitar(){
  printf("Agitarlo (oscilacion de 3 Hz, hasta 2,2 g)\n");
  Sim s; simInit(&s, FLEXTHEFT_SENS_NORMAL);
  simHeld(&s, 1600);
  simShake(&s, 2200, 2.2f);
  simHeld(&s, 2800);
  ok("no se declara arrebato", !(s.got && s.last.snatch));
  if(s.got) ok("si hubo evaluacion, se marco como oscilacion",
               (s.last.reasons & FLEXTHEFT_R_SHAKE) || s.last.confidence < 68);
  else      ok("si hubo evaluacion, se marco como oscilacion", true);
}

static void testGolpecito(){
  printf("Golpecito seco (30 ms)\n");
  Sim s; simInit(&s, FLEXTHEFT_SENS_NORMAL);
  simHeld(&s, 1600);
  simStep(&s, 3.2f, 0.4f, 0.6f, 1.0f);
  simStep(&s, 0.8f, 0.1f, 0.1f, 0.3f);
  simHeld(&s, 3000);
  ok("no se declara arrebato", !(s.got && s.last.snatch));
  ok("un golpe tan corto ni llega a evaluarse", flexTheftEventCount(&s.d) == 0);
}

static void testGiroPuro(){
  printf("Ensenarselo a alguien (giro grande, casi sin traslacion)\n");
  Sim s; simInit(&s, FLEXTHEFT_SENS_NORMAL);
  simHeld(&s, 1600);
  for(uint32_t e = 0; e < 600; e += DT) simStep(&s, 0.22f, 0.15f, 0.10f, 4.5f);
  simTilt(&s, 160.0f);
  simHeld(&s, 2800);
  ok("no se declara arrebato", !(s.got && s.last.snatch));
  ok("contador de arrebatos = 0", flexTheftSnatchCount(&s.d) == 0);
}

static void testSensibilidad(){
  printf("Sensibilidad: el mismo gesto flojo con los tres niveles\n");
  // Un tiron en el limite: 1,25 g. Por debajo de NORMAL (1,45) y de BAJA (1,95),
  // por encima de ALTA (1,10).
  uint8_t niveles[3] = { FLEXTHEFT_SENS_LOW, FLEXTHEFT_SENS_NORMAL, FLEXTHEFT_SENS_HIGH };
  bool det[3];
  for(int i = 0; i < 3; i++){
    Sim s; simInit(&s, niveles[i]);
    simHeld(&s, 2000);
    simYank(&s, 1.25f, 70, 2.4f);
    simCarry(&s, 2600);
    det[i] = s.got && s.last.snatch == 1;
  }
  ok("Baja NO lo declara",    det[0] == false);
  ok("Normal NO lo declara",  det[1] == false);
  ok("Alta SI lo declara",    det[2] == true);

  // Y un arrebato claro tiene que salir en los tres.
  bool todos = true;
  for(int i = 0; i < 3; i++){
    Sim s; simInit(&s, niveles[i]);
    simHeld(&s, 2000);
    simYank(&s, 3.0f, 90, 4.5f);
    simCarry(&s, 2800);
    if(!(s.got && s.last.snatch == 1)) todos = false;
  }
  ok("un arrebato claro se detecta en los tres niveles", todos);

  // BAJA exige separacion posterior como CONDICION, no como puntos.
  Sim s; simInit(&s, FLEXTHEFT_SENS_LOW);
  simHeld(&s, 2000);
  simYank(&s, 3.2f, 100, 5.0f);
  simHeld(&s, 2800);                 // sin huida: se queda en la mano
  ok("Baja sin separacion posterior no declara nada", !(s.got && s.last.snatch));
}

static void testNombres(){
  printf("Nombres de estado y de sensibilidad\n");
  ok("estado 0 en espanol", strcmp(flexTheftStateName(FLEXTHEFT_REST, 0), "En reposo") == 0);
  ok("estado fuera de rango no revienta", flexTheftStateName(99, 0) != NULL);
  ok("idioma fuera de rango no revienta", flexTheftStateName(0, 42) != NULL);
  ok("sensibilidad Normal", strcmp(flexTheftSensName(FLEXTHEFT_SENS_NORMAL, 0), "Normal") == 0);
  ok("sensibilidad fuera de rango cae en Normal",
     strcmp(flexTheftSensName(200, 0), "Normal") == 0);
}

static void testRobustez(){
  printf("Robustez: datos invalidos, punteros nulos y perdida del sensor\n");
  ok("feed con detector nulo devuelve false", flexTheftFeed(NULL, NULL, NULL) == false);
  ok("state con detector nulo no revienta", flexTheftState(NULL) == FLEXTHEFT_REST);
  ok("activity con detector nulo = 0", flexTheftActivity(NULL) == 0);
  ok("last con detector nulo = false", flexTheftLast(NULL, NULL) == false);
  flexTheftInit(NULL, 0);        // no debe reventar
  flexTheftReset(NULL);
  flexTheftSetSensitivity(NULL, 0);

  Sim s; simInit(&s, FLEXTHEFT_SENS_NORMAL);
  simHeld(&s, 1200);
  // NaN e infinito: la muestra se descarta entera.
  FlexTheftSample bad;
  memset(&bad, 0, sizeof(bad));
  bad.tMs = s.t; bad.ax = NAN; bad.ay = 0; bad.az = G;
  ok("una muestra con NaN no produce evento", flexTheftFeed(&s.d, &bad, NULL) == false);
  bad.ax = 1e30f;
  ok("una muestra fuera de rango no produce evento", flexTheftFeed(&s.d, &bad, NULL) == false);
  // Valores extremos pero finitos (saturacion del sensor).
  bad.ax = 300.0f; bad.ay = 300.0f; bad.az = 300.0f;
  ok("saturacion extrema se descarta", flexTheftFeed(&s.d, &bad, NULL) == false);

  // Sin cuaternion: el detector sigue funcionando con la gravedad estimada.
  Sim s2; simInit(&s2, FLEXTHEFT_SENS_NORMAL);
  for(int i = 0; i < 140; i++){
    FlexTheftSample m;
    memset(&m, 0, sizeof(m));
    m.tMs = s2.t; s2.t += DT;
    m.ax = 0.02f * G * sinf(i * 0.3f); m.ay = 0; m.az = G;
    m.haveGyro = 1;
    flexTheftFeed(&s2.d, &m, NULL);
  }
  ok("sin sensor fusion no cuelga ni inventa un arrebato",
     flexTheftSnatchCount(&s2.d) == 0);

  // Hueco largo (el sensor se perdio y volvio): la secuencia se descarta.
  Sim s3; simInit(&s3, FLEXTHEFT_SENS_NORMAL);
  simHeld(&s3, 2000);
  simStep(&s3, 2.0f, 0.2f, 0.0f, 3.0f);     // empieza un tiron
  s3.t += 3000;                              // ...y el sensor desaparece 3 s
  simHeld(&s3, 2000);
  ok("un hueco de 3 s no deja un caso a medias",
     flexTheftState(&s3.d) == FLEXTHEFT_REST || flexTheftState(&s3.d) == FLEXTHEFT_CARRY);
  ok("y no produce un arrebato", flexTheftSnatchCount(&s3.d) == 0);

  // flexTheftReset tras un arrebato: el contador historico se conserva.
  Sim s4; simInit(&s4, FLEXTHEFT_SENS_NORMAL);
  simHeld(&s4, 2000);
  simYank(&s4, 2.8f, 90, 4.2f);
  simCarry(&s4, 2600);
  uint32_t n = flexTheftSnatchCount(&s4.d);
  flexTheftReset(&s4.d);
  ok("reset no borra el contador historico", flexTheftSnatchCount(&s4.d) == n && n == 1);
  ok("reset deja el detector en reposo", flexTheftState(&s4.d) == FLEXTHEFT_REST);
  FlexTheftEvent le;
  ok("reset conserva el ultimo evento", flexTheftLast(&s4.d, &le) && le.snatch == 1);
}

static void testSinDuplicados(){
  printf("Periodo refractario: un solo incidente, un solo evento\n");
  Sim s; simInit(&s, FLEXTHEFT_SENS_NORMAL);
  simHeld(&s, 2000);
  simYank(&s, 2.8f, 90, 4.2f);
  simCarry(&s, 600);
  simYank(&s, 2.6f, 80, 4.0f);      // el forcejeo sigue
  simCarry(&s, 600);
  simYank(&s, 2.7f, 80, 4.0f);
  simCarry(&s, 2600);
  ok("los tirones encadenados no generan tres eventos", s.nEvents <= 2);
  ok("y al menos uno es un posible arrebato", flexTheftSnatchCount(&s.d) >= 1);
}

static void testUtilidades(){
  printf("Utilidades puras\n");
  float up[3];
  float qi[4] = {0, 0, 0, 1};
  flexTheftUpFromQuat(qi, up);
  ok("cuaternion identidad -> arriba = +Z",
     fabsf(up[0]) < 1e-5f && fabsf(up[1]) < 1e-5f && fabsf(up[2] - 1.0f) < 1e-5f);
  float qbad[4] = {0, 0, 0, 0};
  up[0] = up[1] = up[2] = 9.0f;
  flexTheftUpFromQuat(qbad, up);
  ok("cuaternion no unitario -> eje por defecto, no basura",
     fabsf(up[2] - 1.0f) < 1e-5f);
  flexTheftUpFromQuat(NULL, up);
  ok("cuaternion nulo -> eje por defecto", fabsf(up[2] - 1.0f) < 1e-5f);
  // 90 grados alrededor de X: el "arriba" del mundo pasa a -Y del sensor.
  float h = 45.0f * 3.14159265f / 180.0f;
  float q90[4] = { sinf(h), 0, 0, cosf(h) };
  flexTheftUpFromQuat(q90, up);
  ok("giro de 90 grados en X mueve el eje de gravedad",
     fabsf(up[2]) < 1e-4f && fabsf(fabsf(up[1]) - 1.0f) < 1e-4f);
  ok("magnitud en g", fabsf(flexTheftMagG(0, 0, G) - 1.0f) < 1e-5f);

  // La actividad es una MEDIDA de la ventana, no un adorno.
  Sim s; simInit(&s, FLEXTHEFT_SENS_NORMAL);
  ok("sin muestras la actividad es 0", flexTheftActivity(&s.d) == 0);
  simTable(&s, 1200);
  uint8_t quieto = flexTheftActivity(&s.d);
  simCarry(&s, 1200);
  uint8_t movido = flexTheftActivity(&s.d);
  ok("quieto da casi cero", quieto < 12);
  ok("moviendose da bastante mas", movido > quieto + 20);
}

static void testParametros(){
  printf("Los tres niveles cambian el clasificador entero, no un umbral\n");
  FlexTheftParams lo, no_, hi;
  flexTheftDefaults(&lo, FLEXTHEFT_SENS_LOW);
  flexTheftDefaults(&no_, FLEXTHEFT_SENS_NORMAL);
  flexTheftDefaults(&hi, FLEXTHEFT_SENS_HIGH);
  ok("intensidad: Baja > Normal > Alta",   lo.pullG > no_.pullG && no_.pullG > hi.pullG);
  ok("arranque: Baja > Normal > Alta",     lo.jerkGs > no_.jerkGs && no_.jerkGs > hi.jerkGs);
  ok("duracion aceptada: Alta es la mas amplia",
     (hi.burstMaxMs - hi.burstMinMs) > (no_.burstMaxMs - no_.burstMinMs) &&
     (no_.burstMaxMs - no_.burstMinMs) > (lo.burstMaxMs - lo.burstMinMs));
  ok("coherencia exigida: Baja > Normal > Alta",
     lo.dirCohMin > no_.dirCohMin && no_.dirCohMin > hi.dirCohMin);
  ok("separacion exigida: Baja > Normal > Alta",
     lo.escapeNeedMs > no_.escapeNeedMs && no_.escapeNeedMs > hi.escapeNeedMs);
  ok("confianza exigida: Baja > Normal > Alta",
     lo.threshold > no_.threshold && no_.threshold > hi.threshold);
  ok("solo Baja exige separacion como condicion",
     lo.needEscape == 1 && no_.needEscape == 0 && hi.needEscape == 0);
  ok("ningun umbral se acerca al fondo de escala del BNO085 (8 g)",
     lo.pullG < 6.0f && lo.impactG < 6.0f);
  FlexTheftParams x;
  flexTheftDefaults(&x, 200);
  ok("un nivel fuera de rango cae en Normal", x.threshold == no_.threshold);
  flexTheftDefaults(NULL, 0);    // no debe reventar
}

int main(){
  printf("\n=== FlexOS \xC2\xB7 Proteccion contra robo (clasificador temporal) ===\n");
  printf("[buffer] %d ranuras de %d bytes = %d bytes de ventana temporal\n",
         FLEXTHEFT_RING, (int)sizeof(FlexTheftSlot),
         (int)(FLEXTHEFT_RING * sizeof(FlexTheftSlot)));
  printf("[detector] %d bytes en total, sin una sola reserva dinamica\n",
         (int)sizeof(FlexTheftDet));
  testParametros();
  testUtilidades();
  testNombres();
  testArrebato();
  testArrebatoMasCaida();
  testArrebatoYSeLoLlevanAndando();
  testCaidaSola();
  testCaidaConGolpeLargo();
  testCaminar();
  testCorrer();
  testMesa();
  testAgitar();
  testGolpecito();
  testGiroPuro();
  testSensibilidad();
  testSinDuplicados();
  testRobustez();
  printf("=== %s ===\n\n", fallos ? "HAY FALLOS" : "todas las comprobaciones pasan");
  return fallos ? 1 : 0;
}
