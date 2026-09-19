// #############################################################
// ##  FLEX PHONE · DESPLAZAMIENTO Y ARRASTRE  (nucleo puro)
// ##  ---------------------------------------------------------
// ##  Solo aritmetica: ni una primitiva grafica, ni el tactil, ni
// ##  Arduino. Por eso se compila y se ejecuta EN EL PC
// ##  (tests/host/test_flexphone_ui.cpp), que es justo lo que le
// ##  faltaba cuando las listas se dibujaban con barra y no se
// ##  movian.
// ##
// ##  Lo que pinta -- tarjetas, vidrio, texto, la propia barra de
// ##  desplazamiento -- se queda en FlexOS_FlexPhone_UI.h, que si
// ##  es un modulo del sketch.
// #############################################################
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>   // abs

// =============================================================
//  3) DESPLAZAMIENTO
// =============================================================
// Una sola pieza de scroll para todas las pantallas. Guarda el alto
// del contenido MIENTRAS se pinta, asi que el tope siempre
// corresponde a lo que hay de verdad en la pantalla actual.
typedef struct {
  int  off;          // desplazamiento actual (px, >= 0)
  int  content;      // alto del contenido de la ultima pasada
  int  viewTop;      // primera fila visible del area desplazable
  int  viewBot;      // ultima + 1
} FgScroll;

static void fgScrollReset(FgScroll* s, int top, int bot){
  if(!s) return;
  s->off = 0; s->content = 0; s->viewTop = top; s->viewBot = bot;
}
static void fgScrollSetView(FgScroll* s, int top, int bot){
  if(!s) return;
  s->viewTop = top; s->viewBot = bot;
}
// Limita el desplazamiento a lo que hay. Devuelve true si cambio:
// asi el llamador solo repinta cuando el scroll se movio de verdad.
static bool fgScrollClamp(FgScroll* s){
  if(!s) return false;
  const int view = s->viewBot - s->viewTop;
  int max = s->content - view;
  if(max < 0) max = 0;
  const int was = s->off;
  if(s->off > max) s->off = max;
  if(s->off < 0)   s->off = 0;
  return s->off != was;
}
static bool fgScrollBy(FgScroll* s, int dy){
  if(!s) return false;
  const int was = s->off;
  s->off += dy;
  fgScrollClamp(s);
  return s->off != was;
}
static bool fgScrollNeeded(const FgScroll* s){
  return s && s->content > (s->viewBot - s->viewTop);
}
// #############################################################
// ##  ARRASTRE PARA DESPLAZAR
// ##  ------------------------------------------------------
// ##  UNA sola copia de esta maquina de estados, y esta probada.
// ##  Habia dos escritas a mano -- la de las pantallas de Flex
// ##  Phone y la del Centro de notificaciones -- y las dos tenian
// ##  el MISMO fallo: usaban `T.pressed` para la fase continua del
// ##  arrastre.
// ##
// ##  Y `T.pressed` es un FLANCO: vale true solo en el cuadro en
// ##  que el dedo toca el cristal (ver FlexOS_Ultra_Touch.h, donde
// ##  se pone a false al principio de cada lectura). El nivel -- el
// ##  dedo SIGUE apoyado -- es `T.down`.
// ##
// ##  Consecuencia: el arrastre empezaba y terminaba en el cuadro
// ##  siguiente, con cero pixeles recorridos. La barra de
// ##  desplazamiento se pintaba igual, porque el alto del contenido
// ##  si se calculaba bien, asi que la lista PARECIA desplazable y
// ##  no respondia nunca. El panel rapido no lo sufria porque el si
// ##  usa `T.down`.
// #############################################################
typedef struct {
  bool active;   // hay un arrastre en curso
  bool moved;    // supero el umbral: es desplazamiento, no toque
  int  y0;       // y del dedo al empezar
  int  off0;     // desplazamiento al empezar
} FgDrag;

enum {
  FG_DRAG_IDLE = 0,    // el gesto no es de desplazamiento: sigue su camino
  FG_DRAG_SCROLLING,   // se esta desplazando AHORA: quien llama repinta y sale
  FG_DRAG_CONSUMED,    // acaba de soltar tras desplazar: el toque NO es un toque
};

// Umbral para distinguir un toque de un arrastre. Por debajo de esto,
// el dedo no se ha movido: es una pulsacion con pulso.
#define FG_DRAG_SLOP 8

static void fgDragReset(FgDrag* d){
  if(!d) return;
  d->active = false; d->moved = false; d->y0 = 0; d->off0 = 0;
}

// Un paso del arrastre. `down` es el NIVEL (T.down) y `justPressed` el
// FLANCO (T.pressed). Devuelve FG_DRAG_*.
static int fgDragStep(FgDrag* d, FgScroll* s, bool down, bool justPressed, int y){
  if(!d || !s) return FG_DRAG_IDLE;

  if(justPressed && !d->active){
    d->active = true;
    d->moved  = false;
    d->y0     = y;
    d->off0   = s->off;
  }

  if(d->active && down){
    const int dy = d->y0 - y;
    if(!d->moved && abs(dy) > FG_DRAG_SLOP) d->moved = true;
    if(d->moved){
      s->off = d->off0 + dy;
      fgScrollClamp(s);
      return FG_DRAG_SCROLLING;
    }
    // Dedo apoyado pero quieto: todavia puede acabar siendo un toque,
    // asi que no se consume nada.
    return FG_DRAG_IDLE;
  }

  if(d->active && !down){
    const bool wasScroll = d->moved;
    fgDragReset(d);
    // Si hubo desplazamiento, el `tap` que llega en este mismo cuadro
    // NO es una pulsacion: es el dedo levantandose al final del
    // arrastre. Sin esto, desplazar una lista abriria la tarjeta que
    // quedara debajo del dedo.
    if(wasScroll) return FG_DRAG_CONSUMED;
  }
  return FG_DRAG_IDLE;
}

