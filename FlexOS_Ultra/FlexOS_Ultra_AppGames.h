// #############################################################
// ##  FLEX OS ULTRA  ·  APP JUEGOS  ·  contenedor de Jumper
// ##  ----------------------------------------------------------
// ##  El registro de la app, su orientacion y su ciclo de vida. El juego en
// ##  si vive en FlexOS_Jumper.h, que se incluye desde aqui.
// ##
// ##  COMO ENCAJA ESTE ARCHIVO
// ##  ----------------------------------------------------------
// ##  Es una PARTE del sketch FlexOS_Ultra.ino, no una unidad de
// ##  traduccion independiente. FlexOS_Ultra.ino lo incluye en el
// ##  orden que fija la cadena de cabeceras (cada modulo incluye al
// ##  anterior), asi que todo el sistema sigue compilandose como UN
// ##  SOLO archivo, exactamente igual que antes de separarlo.
// ##
// ##  Consecuencias practicas, y son las que mantienen esto seguro:
// ##    · Las variables globales se DEFINEN una sola vez, aqui, en el
// ##      modulo al que pertenecen. No hace falta `extern` ni existe
// ##      el riesgo de una definicion duplicada en el enlazado.
// ##    · El ORDEN de definicion es el mismo que tenia el .ino: una
// ##      funcion `static` solo se puede llamar despues de definirse,
// ##      y esa relacion se conserva modulo a modulo.
// ##    · La cadena de includes es LINEAL (Types -> ... -> Recovery),
// ##      asi que no hay dependencias circulares posibles.
// ##    · No lo incluyas por tu cuenta desde otro sitio: el punto de
// ##      entrada del sistema es siempre FlexOS_Ultra.ino.
// #############################################################
#pragma once
#include "FlexOS_Ultra_AppFiles.h"   // eslabon anterior de la cadena

// #############################################################
// ##  APP: JUEGOS -- JUMPER
// ##  ----------------------------------------------------------
// ##  El contenedor de la app se queda AQUI (registro, orientacion
// ##  horizontal y salida segura); el juego entero vive en
// ##  FlexOS_Jumper.h, que se incluye en este punto porque es
// ##  donde ya estan disponibles las primitivas graficas, el
// ##  tactil (T), bbuf/fb, present()/flxFlush(), appClose() y las
// ##  variables de orientacion.
// ##
// ##  Solo existe el nivel Jumper: no hay editor, ni tienda de
// ##  niveles, ni multijugador, ni audio.
// #############################################################
#include "FlexOS_Jumper.h"

static void gamesEnter(){ jmpEnter(); }
static void gamesTick(){  jmpTick();  }
static void gamesSuspend(){
  jmpFlushSave();
  if(JG.screen == JS_PLAY) JG.screen = JS_PAUSE;
  jmpClearInput();
}
static void gamesResume(){
  gLand = true; JG.lastUs = micros(); JG.acc = 0; jNextFrameUs = JG.lastUs;
  if(JG.screen == JS_PAUSE) jmpPauseFrame(); else jmpTick();
}
static void gamesCloseApp(){ jmpFlushSave(); jmpClearInput(); }

// NAVEGADOR
//   La implementacion ANTIGUA de navEnter() vivia aqui: pintaba una
//   barra de direcciones falsa con el texto "https://", tres pestanas
//   decorativas ("Inicio", "Marcadores", "Historial") que no hacian
//   nada, un globo dibujado a mano y el pie "Sin conexion - modo
//   offline". Era una MAQUETA: no habia red, ni entrada de texto, ni
//   navegacion.
//
//   Se ha sustituido entera, no ampliada, porque no habia nada
//   reutilizable: ni un estado, ni un parser, ni un modelo de pestanas.
//   La app real es ahora navEnter()/navTick() y vive en
//   FlexOS_Browser_Bridge.h (incluido antes de setup()), que a su vez
//   se apoya en FlexOS_BrowserApp.cpp -- comun a las tres placas, con
//   la misma logica de siempre de este proyecto: los .ino dibujan, los
//   modulos comunes hacen el trabajo.
//
//   Lo que SI se conserva del diseno anterior: la app sigue siendo
//   APP_FLEX (maqueta contra el lienzo real, asi que funciona igual a
//   pantalla completa y dentro de una ventana de Modo PC/DeX) y sigue
//   respetando el tema semantico global.
