// #############################################################
//  FLEX ROTATION  ·  prueba de host (sin placa)
//  ------------------------------------------------------------
//  FlexOS_RotationCore.h es logica pura: entran inclinacion,
//  planitud, aceleracion y giro, y sale UNA cosa -- la postura
//  estable del aparato. Eso se ejercita ENTERO en el PC, que es
//  justo donde hace falta: comprobar en la mano que un giro pequeno
//  no provoca un vaiven de orientaciones obliga a repetir el mismo
//  gesto decenas de veces sin poder medir nada.
//
//  Los escenarios son los del encargo, uno por uno:
//    · giro lento y franco              -> cambia de postura
//    · giro pequeno (borde del umbral)  -> NO cambia (histeresis)
//    · vaiven en el limite              -> NO cambia ni una vez
//    · movimiento brusco continuado     -> NO cambia (MOTION ACTIVE)
//    · caida con giro de 180 grados     -> NO cambia durante el evento
//    · sobre la mesa boca arriba        -> postura indeterminada: se conserva
//    · postura horizontal OPUESTA       -> se reconoce y NO se aplica
// #############################################################
#include "FlexOS_RotationCore.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int fallos = 0;
static void ok(const char* que, bool cond){
  printf("  %s %s\n", cond ? "OK  " : "FALLA", que);
  if(!cond) fallos++;
}

#define DT 20            // 50 Hz, la cadencia real del driver

// Pequeno simulador: mantiene el reloj y cuenta cuantas veces cambio la
// postura, que es la cifra que de verdad importa (una rotacion = una
// reconstruccion completa de la interfaz).
struct Sim {
  FrotStab s;
  uint32_t t;
  int      changes;
};
static void simInit(Sim* m){
  frotStabInit(&m->s);
  m->t = 1000;
  m->changes = 0;
}
// Empuja `ms` milisegundos con el aparato inclinado `ang` grados sobre el
// plano de la pantalla, con `dotN` de componente sobre la normal (0 = de
// canto), `accG` de modulo de aceleracion y `gyr` rad/s de giro.
static void hold(Sim* m, int ms, float ang, float dotN, float accG, float gyr){
  for(int i = 0; i < ms; i += DT){
    if(frotStabFeed(&m->s, m->t, ang, dotN, accG, gyr)) m->changes++;
    m->t += DT;
  }
}
// Reposo perfecto: de canto, 1 g, sin giro.
static void rest(Sim* m, int ms, float ang){ hold(m, ms, ang, 0.0f, 1.0f, 0.0f); }

// ---------------------------------------------------------------
int main(){
  printf("Flex Rotation - nucleo de decision\n");

  // --- 0) Funcion pura de clasificacion, sin tiempo de por medio ---------
  {
    FrotParams p; frotDefaults(&p);
    ok("0.1 vertical franca se clasifica vertical",
       frotClassify(3.0f, FROT_POS_UNKNOWN, &p) == FROT_POS_PORTRAIT);
    ok("0.2 +90 se clasifica horizontal A",
       frotClassify(90.0f, FROT_POS_UNKNOWN, &p) == FROT_POS_LAND_A);
    ok("0.3 -90 se clasifica horizontal B",
       frotClassify(-90.0f, FROT_POS_UNKNOWN, &p) == FROT_POS_LAND_B);
    ok("0.4 180 se clasifica vertical invertida",
       frotClassify(180.0f, FROT_POS_UNKNOWN, &p) == FROT_POS_INVERTED);
    ok("0.5 el salto 179->-179 no da una vuelta entera",
       frotClassify(-179.0f, FROT_POS_INVERTED, &p) == FROT_POS_INVERTED);
    // ZONA MUERTA: 45 grados esta fuera de enterDeg de las dos posturas y
    // dentro de exitDeg de la actual -> se conserva la actual, sea cual sea.
    ok("0.6 a 45 grados se conserva la postura actual (zona muerta)",
       frotClassify(45.0f, FROT_POS_PORTRAIT, &p) == FROT_POS_PORTRAIT &&
       frotClassify(45.0f, FROT_POS_LAND_A,   &p) == FROT_POS_LAND_A);
    // Sin postura actual, esa misma inclinacion no propone ninguna.
    ok("0.7 a 45 grados y sin postura previa no hay candidata",
       frotClassify(45.0f, FROT_POS_UNKNOWN, &p) == FROT_POS_UNKNOWN);
    ok("0.8 un NaN no propone ninguna postura",
       frotClassify(NAN, FROT_POS_UNKNOWN, &p) == FROT_POS_UNKNOWN);
  }

  // --- 1) Giro lento y franco: la orientacion cambia ---------------------
  {
    Sim m; simInit(&m);
    rest(&m, 1200, 0.0f);                       // vertical, quieto
    ok("1.1 arranca en vertical", frotStabPos(&m.s) == FROT_POS_PORTRAIT);
    // el giro en si: 900 ms moviendo la mano, con giro moderado
    for(int i = 1; i <= 45; i++) hold(&m, DT, (float)i * 2.0f, 0.0f, 1.05f, 0.8f);
    rest(&m, 1200, 90.0f);                      // ...y se deja quieto en horizontal
    ok("1.2 termina en horizontal A", frotStabPos(&m.s) == FROT_POS_LAND_A);
    ok("1.3 y lo hizo en UNA sola transicion", m.changes == 2);   // vertical + horizontal
  }

  // --- 2) Giro pequeno: NO debe cambiar nada ----------------------------
  {
    Sim m; simInit(&m);
    rest(&m, 1200, 0.0f);
    int base = m.changes;
    rest(&m, 3000, 25.0f);                      // 25 grados: por debajo del umbral de salida
    ok("2.1 un giro de 25 grados no cambia la orientacion",
       m.changes == base && frotStabPos(&m.s) == FROT_POS_PORTRAIT);
  }

  // --- 3) Vaiven en el limite: ni una sola rotacion ---------------------
  {
    Sim m; simInit(&m);
    rest(&m, 1200, 0.0f);
    int base = m.changes;
    for(int i = 0; i < 30; i++){                // 30 idas y venidas alrededor de 50 grados
      rest(&m, 200, 44.0f);
      rest(&m, 200, 56.0f);
    }
    ok("3.1 el vaiven en la zona muerta no produce NI UNA rotacion",
       m.changes == base);
  }

  // --- 4) Movimiento brusco continuado: la UI se queda quieta ------------
  {
    Sim m; simInit(&m);
    rest(&m, 1200, 0.0f);
    int base = m.changes;
    // el aparato pasa por todas las inclinaciones, pero agitado
    for(int i = 0; i < 60; i++)
      hold(&m, DT, (float)((i * 37) % 360) - 180.0f, 0.0f, 2.4f, 6.0f);
    ok("4.1 sacudirlo no produce una cascada de rotaciones", m.changes == base);
    ok("4.2 y el evento de movimiento se publica", frotStabMotion(&m.s));
    // al pararse en una postura franca SI se acepta
    rest(&m, 1600, 90.0f);
    ok("4.3 al estabilizarse en horizontal, se acepta",
       frotStabPos(&m.s) == FROT_POS_LAND_A && m.changes == base + 1);
  }

  // --- 5) Caida con giro de 180 grados ----------------------------------
  //  El caso que el encargo protege de forma explicita: el aparato cae,
  //  gira, golpea y queda boca abajo. Durante TODO el evento la orientacion
  //  de la interfaz no se mueve -- y la deteccion de caidas, que es otro
  //  motor, sigue recibiendo sus muestras por su cuenta.
  {
    Sim m; simInit(&m);
    rest(&m, 1200, 0.0f);
    int base = m.changes;
    hold(&m, 260, 10.0f, 0.1f, 0.04f, 1.0f);              // caida libre
    for(int i = 0; i < 9; i++)                            // giro de 180 durante el vuelo
      hold(&m, DT, 10.0f + (float)i * 20.0f, 0.2f, 0.05f, 9.0f);
    hold(&m,  60, 180.0f, 0.9f, 6.5f, 12.0f);             // impacto
    hold(&m, 200, 180.0f, 0.95f, 1.6f, 3.0f);             // rebote
    ok("5.1 durante la caida la orientacion NO cambia", m.changes == base);
    rest(&m, 2000, 180.0f);                               // queda boca abajo, quieto
    // Vertical invertida: postura reconocida... pero el motor no la aplica
    // (solo hay dos rasterizaciones). Aqui se comprueba que el NUCLEO la
    // distingue, que es lo que permite al motor decidir con criterio.
    ok("5.2 despues del impacto reconoce la vertical invertida",
       frotStabPos(&m.s) == FROT_POS_INVERTED);
    ok("5.3 y el evento de movimiento ya termino", !frotStabMotion(&m.s));
  }

  // --- 6) Giro normal, sin caida: no hay evento de movimiento sostenido --
  {
    Sim m; simInit(&m);
    rest(&m, 1200, 0.0f);
    for(int i = 1; i <= 45; i++) hold(&m, DT, (float)i * 2.0f, 0.0f, 1.05f, 0.8f);
    ok("6.1 un giro a mano no se marca como evento de movimiento",
       !frotStabMotion(&m.s));
  }

  // --- 7) Sobre la mesa: postura indeterminada, se conserva la anterior --
  {
    Sim m; simInit(&m);
    rest(&m, 1600, 90.0f);
    ok("7.1 de canto en horizontal", frotStabPos(&m.s) == FROT_POS_LAND_A);
    int base = m.changes;
    hold(&m, 4000, 0.0f, 1.0f, 1.0f, 0.0f);     // tumbado boca arriba: dotN = 1
    ok("7.2 boca arriba se marca plano", frotStabFlat(&m.s));
    ok("7.3 y NO cambia la orientacion que habia",
       m.changes == base && frotStabPos(&m.s) == FROT_POS_LAND_A);
  }

  // --- 8) Horizontal OPUESTA: se reconoce (el motor decide no aplicarla) -
  {
    Sim m; simInit(&m);
    rest(&m, 1200, 0.0f);
    rest(&m, 2000, -90.0f);
    ok("8.1 la horizontal opuesta se distingue de la soportada",
       frotStabPos(&m.s) == FROT_POS_LAND_B);
  }

  // --- 9) Hueco largo sin muestras: no se confirma con permanencia falsa -
  {
    Sim m; simInit(&m);
    rest(&m, 1200, 0.0f);
    int base = m.changes;
    // una sola muestra en horizontal, y el reloj salta cuatro segundos
    frotStabFeed(&m.s, m.t, 90.0f, 0.0f, 1.0f, 0.0f);
    m.t += 4000;
    if(frotStabFeed(&m.s, m.t, 90.0f, 0.0f, 1.0f, 0.0f)) m.changes++;
    ok("9.1 un hueco largo no confirma una postura por permanencia falsa",
       m.changes == base);
  }

  // --- 10) Reinicio: no queda nada del estado anterior -------------------
  {
    Sim m; simInit(&m);
    rest(&m, 2000, 90.0f);
    frotStabReset(&m.s);
    ok("10.1 tras el reinicio no hay postura",
       frotStabPos(&m.s) == FROT_POS_UNKNOWN && !frotStabMotion(&m.s));
  }

  printf(fallos ? "\n%d FALLOS\n" : "\nTodo correcto\n", fallos);
  return fallos ? 1 : 0;
}
