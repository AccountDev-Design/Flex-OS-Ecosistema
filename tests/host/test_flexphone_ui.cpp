// #############################################################
//  test_flexphone_ui.cpp  ·  desplazamiento de las listas de Flex Phone
// #############################################################
//
//  POR QUE EXISTE ESTE FICHERO
//  ---------------------------
//  Las listas de Flex Phone se pintaban CON barra de desplazamiento y
//  no se movian ni un pixel al arrastrar. El motivo era una confusion
//  entre dos campos del tactil:
//
//      T.pressed  -> FLANCO: true SOLO en el cuadro en que el dedo
//                    toca el cristal.
//      T.down     -> NIVEL:  true mientras el dedo sigue apoyado.
//
//  El arrastre usaba el flanco para su fase continua, asi que
//  empezaba y terminaba en el cuadro siguiente con cero pixeles
//  recorridos. Y no saltaba ninguna alarma: el alto del contenido se
//  calculaba bien, la barra se dibujaba, y la lista simplemente no
//  respondia.
//
//  Un fallo asi no se ve leyendo el codigo -- se ve pasandole una
//  SECUENCIA DE CUADROS como la que produce el tactil de verdad. Eso
//  es lo que hace esta bateria.

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

// -------------------------------------------------------------
//  Lo minimo del entorno del sketch que necesita la pieza probada
// -------------------------------------------------------------
// FlexOS_FlexPhone_UI.h es un modulo del .ino: usa las primitivas
// graficas y el tema. Aqui solo se extrae la parte PURA -- el
// desplazamiento y el arrastre --, que es la que tiene la logica y la
// que se rompio. El resto (vidrio, texto, iconos) es pintura y se
// comprueba compilando el sketch entero en test_ino.
#include "../../FlexOS_Ultra/FlexOS_FlexPhone_UI_Scroll.h"

// -------------------------------------------------------------
//  Un tactil simulado que se comporta como el de verdad
// -------------------------------------------------------------
// Reproduce la regla de FlexOS_Ultra_Touch.h: `pressed` se pone a
// false al principio de cada lectura y solo vuelve a true cuando el
// dedo ACABA de posarse.
struct FakeTouch {
  bool down = false, pressed = false;
  int  y = 0;

  void press(int yy){ down = true; pressed = true; y = yy; }   // el dedo toca
  void move(int yy){ pressed = false; y = yy; }                // sigue apoyado
  void release(){ down = false; pressed = false; }             // se levanta
};

// Una pasada del arrastre con el tactil simulado.
static int step(FgDrag* d, FgScroll* s, const FakeTouch& t){
  return fgDragStep(d, s, t.down, t.pressed, t.y);
}

int main(){
  std::printf("\n=== FlexOS · desplazamiento de las listas de Flex Phone ===\n");

  // ---------------------------------------------------------
  //  1) Una lista que no llena la pantalla no se desplaza
  // ---------------------------------------------------------
  {
    std::printf("[scroll] una lista corta no se mueve, y no finge que si\n");
    FgScroll s; fgScrollReset(&s, 96, 800);
    s.content = 200;                       // menos que el area visible
    CHECK(!fgScrollNeeded(&s), "declaro que hacia falta desplazar una lista corta");
    CHECK(!fgScrollBy(&s, 300), "se desplazo una lista que cabe entera");
    CHECK(s.off == 0, "quedo desplazada en %d", s.off);
  }

  // ---------------------------------------------------------
  //  2) EL FALLO: arrastrar con una secuencia de cuadros REAL
  // ---------------------------------------------------------
  {
    std::printf("[scroll] arrastrar mueve la lista (el fallo que existia)\n");
    FgScroll s; fgScrollReset(&s, 96, 800);
    s.content = 2000;                      // mucho mas que el area visible
    FgDrag d;  fgDragReset(&d);
    FakeTouch t;

    // Cuadro 1: el dedo se posa. Todavia no se ha movido nada.
    t.press(700);
    CHECK(step(&d, &s, t) == FG_DRAG_IDLE, "el primer contacto ya contaba como arrastre");
    CHECK(s.off == 0, "se movio sin que el dedo se moviera");

    // Cuadros 2..N: el dedo sube. AQUI es donde se rompia: `pressed`
    // ya es false y el arrastre se daba por terminado.
    t.move(690);   // 10 px: pasa el umbral
    CHECK(step(&d, &s, t) == FG_DRAG_SCROLLING, "EL ARRASTRE NO SE RECONOCIO");
    CHECK(s.off == 10, "desplazamiento %d, esperaba 10", s.off);

    t.move(600);
    CHECK(step(&d, &s, t) == FG_DRAG_SCROLLING, "el arrastre se corto a mitad");
    CHECK(s.off == 100, "desplazamiento %d, esperaba 100", s.off);

    // Al soltar, el toque de ese mismo cuadro NO es una pulsacion.
    t.release();
    CHECK(step(&d, &s, t) == FG_DRAG_CONSUMED,
          "el final del arrastre se habria tomado como un toque");
    CHECK(s.off == 100, "perdio el desplazamiento al soltar (%d)", s.off);
  }

  // ---------------------------------------------------------
  //  3) Un toque sigue siendo un toque
  // ---------------------------------------------------------
  {
    std::printf("[scroll] un toque limpio no se confunde con un arrastre\n");
    FgScroll s; fgScrollReset(&s, 96, 800);
    s.content = 2000;
    FgDrag d;  fgDragReset(&d);
    FakeTouch t;

    t.press(400);
    CHECK(step(&d, &s, t) == FG_DRAG_IDLE, "el contacto se tomo por arrastre");
    // Un dedo humano tiembla unos pocos pixeles: eso NO es arrastrar.
    t.move(403);
    CHECK(step(&d, &s, t) == FG_DRAG_IDLE, "3 px de pulso contaron como arrastre");
    CHECK(s.off == 0, "un pulso movio la lista (%d)", s.off);
    t.release();
    CHECK(step(&d, &s, t) == FG_DRAG_IDLE, "SE COMIO UN TOQUE que no era arrastre");
  }

  // ---------------------------------------------------------
  //  4) No se sale por ningun extremo
  // ---------------------------------------------------------
  {
    std::printf("[scroll] no se pasa ni por arriba ni por abajo\n");
    FgScroll s; fgScrollReset(&s, 100, 800);   // area visible: 700
    s.content = 1000;                          // tope: 300
    FgDrag d;  fgDragReset(&d);
    FakeTouch t;

    t.press(700);
    step(&d, &s, t);
    t.move(0);                                 // arrastre enorme hacia arriba
    step(&d, &s, t);
    CHECK(s.off == 300, "se paso del final: %d (tope 300)", s.off);
    t.release(); step(&d, &s, t);

    t.press(0);
    step(&d, &s, t);
    t.move(700);                               // y de vuelta hacia abajo
    step(&d, &s, t);
    CHECK(s.off == 0, "se paso del principio: %d", s.off);
    t.release(); step(&d, &s, t);
  }

  // ---------------------------------------------------------
  //  5) El tope se recalcula cuando cambia el contenido
  // ---------------------------------------------------------
  {
    std::printf("[scroll] al encoger la lista, el desplazamiento se ajusta\n");
    FgScroll s; fgScrollReset(&s, 96, 800);
    s.content = 2000;
    fgScrollBy(&s, 1000);
    CHECK(s.off == 1000, "desplazamiento %d", s.off);
    // Se borran notificaciones y la lista se queda corta: no puede
    // quedarse mirando un hueco vacio.
    s.content = 300;
    CHECK(fgScrollClamp(&s), "no ajusto el desplazamiento al encoger");
    CHECK(s.off == 0, "quedo desplazada sobre una lista corta (%d)", s.off);
  }

  // ---------------------------------------------------------
  //  6) Argumentos imposibles
  // ---------------------------------------------------------
  {
    std::printf("[scroll] punteros nulos no revientan\n");
    FgDrag d; fgDragReset(&d);
    FgScroll s; fgScrollReset(&s, 0, 100);
    CHECK(fgDragStep(nullptr, &s, true, true, 0) == FG_DRAG_IDLE, "acepto un arrastre nulo");
    CHECK(fgDragStep(&d, nullptr, true, true, 0) == FG_DRAG_IDLE, "acepto un scroll nulo");
    CHECK(!fgScrollNeeded(nullptr), "declaro desplazamiento sobre un nulo");
    CHECK(!fgScrollClamp(nullptr), "ajusto un nulo");
    fgScrollReset(nullptr, 0, 0);
    fgDragReset(nullptr);
  }

  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
