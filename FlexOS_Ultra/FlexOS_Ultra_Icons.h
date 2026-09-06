// #############################################################
// ##  FLEX OS ULTRA  ·  ICONOS VECTORIALES DEL SISTEMA
// ##  ----------------------------------------------------------
// ##  Los iconos de las apps y del cromo, dibujados con las primitivas del
// ##  motor: ni un bitmap. Aqui vive tambien el enum IC_* de aplicaciones.
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
#include "FlexOS_Ultra_Font.h"   // eslabon anterior de la cadena

// #############################################################
// ##  ICONOS VECTORIALES  (originales, basados en tus imagenes)
// #############################################################

// arco con trazo grueso (para wifi y detalles)
static void arcStroke(float cx, float cy, float r, float a0, float a1, int thick, uint16_t col){
  float pr = 0.0174532925f;
  int steps = (int)(fabsf(a1 - a0) / 8.0f) + 2;
  float pxp = 0, pyp = 0; bool first = true;
  int rad = thick / 2; if(rad < 1) rad = 1;
  for(int i = 0; i <= steps; i++){
    float a = (a0 + (a1 - a0) * i / steps) * pr;
    float xx = cx + r * cosf(a), yy = cy + r * sinf(a);
    if(!first) strokeSeg(pxp, pyp, xx, yy, rad, col);
    pxp = xx; pyp = yy; first = false;
  }
}

enum { IC_RELOJ, IC_GALERIA, IC_MULTIMEDIA, IC_ALMACEN, IC_MODOPC, IC_NOTAS,
       IC_EDU, IC_NAV, IC_CODE, IC_BIEN, IC_PAINT, IC_JUEGOS,
       IC_AJUSTES, IC_CALC, IC_CALEND, IC_CAMARA,
       IC_CLIMA, IC_FLEXSTORE, IC_FLEXPHONE }; // Flex Store 17, Flex Phone 18: ningun indice anterior se mueve
#define APP_N 19

static void iconBase(int x, int y, int S, uint16_t bg, int rf100){
  int r = S * rf100 / 100;
  if(gIconStyle == 1){                     // estilo "Vidrio": fondo Liquid Glass (ver drawAppIcon)
    drawLiquidGlassPanel(x, y, S, S, r, bg);
  } else {                                 // estilo "Plano" (original)
    fillRoundRect(x, y, S, S, r, bg);
    // sutil brillo superior
    fillRoundRectA(x, y, S, S / 2, r, rgb565(255,255,255), 22);
  }
}

static void drawAppIcon(int id, int x, int y, int S){
  // RECORTE POR CAJA, ANTES DE DIBUJAR NADA.
  //
  // Un icono son decenas de primitivas (fondo, anillos, trazos, texto) y en la
  // Caja de aplicaciones, en la rejilla del escritorio y en Recientes se dibujan
  // rejillas enteras dentro de un viewport recortado o de una banda sucia. Los
  // iconos que caen fuera ejecutaban igual todas sus primitivas para que px()
  // fuera descartando pixel a pixel. Ningun icono se sale de [x,x+S)x[y,y+S) --
  // lo comprueba testIconosEnSuCaja() en tests/host midiendo los 18 iconos en
  // los dos estilos --, asi que descartar por la caja no puede recortar dibujo.
  // El margen es cortesia por si un icono futuro pinta un borde justo encima.
  {
    const int M = 2;
    if(gLand){                                   // en landscape la fila FISICA es la x logica
      if(x + S + M <= gClipY0 || x - M > gClipY1) return;
    } else {
      if(y + S + M <= gClipY0 || y - M > gClipY1) return;
      if(x + S + M <= gClipX0 || x - M > gClipX1) return;
    }
  }
  int cx = x + S / 2, cy = y + S / 2;
  int tk = S / 12; if(tk < 2) tk = 2;               // grosor de trazo generico
  // ICONOS DE APP: son MARCA. Cada uno tiene su color y su glifo, igual que en
  // iOS o Android, y no cambian con la apariencia del sistema (lo que cambia es
  // el fondo sobre el que se dibujan). gIconStyle si los afecta: es la
  // preferencia de ESTILO de icono, ortogonal a gDark y a uiGlass.
  uint16_t WHITE = rgb565(255,255,255);
  switch(id){
    case IC_RELOJ: {
      iconBase(x, y, S, rgb565(245,245,247), 22);
      fillRing(cx, cy, (int)(S * 0.36f), 2, rgb565(70,70,74));
      // ticks 12/3/6/9
      fillRect(cx - 1, y + (int)(S * 0.16f), 2, S / 12, rgb565(70,70,74));
      fillRect(cx - 1, y + S - (int)(S * 0.16f) - S / 12, 2, S / 12, rgb565(70,70,74));
      fillRect(x + (int)(S * 0.16f), cy - 1, S / 12, 2, rgb565(70,70,74));
      fillRect(x + S - (int)(S * 0.16f) - S / 12, cy - 1, S / 12, 2, rgb565(70,70,74));
      strokeSeg(cx, cy, cx - S * 0.14f, cy - S * 0.10f, tk / 2 + 1, rgb565(30,30,30)); // hora
      strokeSeg(cx, cy, cx + S * 0.12f, cy - S * 0.20f, tk / 2, rgb565(245,140,30));   // min
      fillCircle(cx, cy, tk / 2 + 1, rgb565(30,30,30));
    } break;
    case IC_GALERIA: {
      iconBase(x, y, S, WHITE, 22);
      uint16_t cols[8] = { rgb565(233,64,64), rgb565(240,150,40), rgb565(240,210,50),
                           rgb565(90,200,90), rgb565(50,190,190), rgb565(60,120,235),
                           rgb565(120,80,220), rgb565(220,80,200) };
      float d = S * 0.17f, pr = S * 0.135f;
      for(int k = 0; k < 8; k++){
        float a = k * 45 * 0.0174532925f;
        fillCircle((int)(cx + d * cosf(a)), (int)(cy + d * sinf(a)), (int)pr, cols[k]);
      }
      fillCircle(cx, cy, (int)(S * 0.11f), WHITE);
    } break;
    case IC_MULTIMEDIA: {
      iconBase(x, y, S, rgb565(27,95,217), 22);
      fillTriangle(cx - (int)(S * 0.12f), cy - (int)(S * 0.18f),
                   cx - (int)(S * 0.12f), cy + (int)(S * 0.18f),
                   cx + (int)(S * 0.22f), cy, WHITE);
    } break;
    case IC_ALMACEN: {
      iconBase(x, y, S, rgb565(59,123,217), 22);
      uint16_t fol = rgb565(225,236,250);
      fillRoundRect(x + (int)(S * 0.20f), y + (int)(S * 0.28f), (int)(S * 0.30f), (int)(S * 0.12f), 3, fol);
      fillRoundRect(x + (int)(S * 0.18f), y + (int)(S * 0.36f), (int)(S * 0.64f), (int)(S * 0.34f), 5, fol);
      // nubecita
      uint16_t cl = rgb565(205,222,245);
      fillCircle(x + (int)(S * 0.60f), y + (int)(S * 0.34f), (int)(S * 0.10f), cl);
      fillCircle(x + (int)(S * 0.72f), y + (int)(S * 0.36f), (int)(S * 0.08f), cl);
      fillRect(x + (int)(S * 0.58f), y + (int)(S * 0.36f), (int)(S * 0.18f), (int)(S * 0.07f), cl);
    } break;
    case IC_MODOPC: {
      iconBase(x, y, S, rgb565(30,58,110), 22);
      fillRoundRect(x + (int)(S * 0.16f), y + (int)(S * 0.24f), (int)(S * 0.68f), (int)(S * 0.40f), 4, rgb565(235,240,250));
      fillRect(x + (int)(S * 0.22f), y + (int)(S * 0.30f), (int)(S * 0.56f), (int)(S * 0.28f), rgb565(45,95,205));
      fillRect(cx - (int)(S * 0.05f), y + (int)(S * 0.64f), (int)(S * 0.10f), (int)(S * 0.08f), rgb565(200,210,225));
      fillRoundRect(cx - (int)(S * 0.16f), y + (int)(S * 0.72f), (int)(S * 0.32f), (int)(S * 0.06f), 2, rgb565(200,210,225));
    } break;
    case IC_NOTAS: {
      iconBase(x, y, S, rgb565(232,167,90), 22);
      fillRoundRect(x + (int)(S * 0.22f), y + (int)(S * 0.18f), (int)(S * 0.48f), (int)(S * 0.62f), 5, WHITE);
      for(int i = 0; i < 3; i++)
        fillRect(x + (int)(S * 0.30f), y + (int)(S * 0.30f) + i * (int)(S * 0.12f), (int)(S * 0.32f), 2, rgb565(180,180,185));
      strokeSeg(x + S * 0.56f, y + S * 0.66f, x + S * 0.78f, y + S * 0.40f, tk / 2 + 1, rgb565(120,90,40)); // lapiz
      fillCircle((int)(x + S * 0.78f), (int)(y + S * 0.40f), tk / 2 + 1, rgb565(245,210,90));
    } break;
    case IC_EDU: {
      iconBase(x, y, S, rgb565(79,179,196), 22);
      uint16_t cap = rgb565(28,52,96);
      fillQuad(cx, cy - (int)(S * 0.24f), cx + (int)(S * 0.28f), cy - (int)(S * 0.06f),
               cx, cy + (int)(S * 0.12f), cx - (int)(S * 0.28f), cy - (int)(S * 0.06f), cap);
      fillRoundRect(cx - (int)(S * 0.18f), cy + (int)(S * 0.08f), (int)(S * 0.36f), (int)(S * 0.16f), 3, WHITE);
      strokeSeg(cx + S * 0.26f, cy - S * 0.06f, cx + S * 0.26f, cy + S * 0.14f, 1, cap);
    } break;
    case IC_NAV: {
      iconBase(x, y, S, rgb565(46,155,230), 22);
      fillRing(cx, cy, (int)(S * 0.30f), 2, WHITE);
      vLine(cx, cy - (int)(S * 0.30f), (int)(S * 0.60f), WHITE);
      hLine(cx - (int)(S * 0.30f), cy, (int)(S * 0.60f), WHITE);
      arcStroke(cx, cy, S * 0.18f, 90, 270, 2, WHITE);   // meridiano
      arcStroke(cx, cy, S * 0.18f, -90, 90, 2, WHITE);
    } break;
    case IC_CODE: {
      iconBase(x, y, S, rgb565(154,160,166), 22);
      uint16_t dk = rgb565(55,58,66);
      strokeSeg(cx - S * 0.10f, cy - S * 0.15f, cx - S * 0.26f, cy, tk / 2 + 1, dk);
      strokeSeg(cx - S * 0.26f, cy, cx - S * 0.10f, cy + S * 0.15f, tk / 2 + 1, dk);
      strokeSeg(cx + S * 0.10f, cy - S * 0.15f, cx + S * 0.26f, cy, tk / 2 + 1, dk);
      strokeSeg(cx + S * 0.26f, cy, cx + S * 0.10f, cy + S * 0.15f, tk / 2 + 1, dk);
      strokeSeg(cx + S * 0.04f, cy - S * 0.17f, cx - S * 0.04f, cy + S * 0.17f, tk / 2, dk);
    } break;
    case IC_BIEN: {
      iconBase(x, y, S, rgb565(92,193,90), 30);
      strokeSeg(cx - S * 0.16f, cy + S * 0.02f, cx - S * 0.02f, cy + S * 0.16f, tk / 2 + 1, WHITE);
      strokeSeg(cx - S * 0.02f, cy + S * 0.16f, cx + S * 0.20f, cy - S * 0.14f, tk / 2 + 1, WHITE);
    } break;
    case IC_PAINT: {
      iconBase(x, y, S, rgb565(241,231,210), 22);
      fillCircle(cx - (int)(S * 0.04f), cy + (int)(S * 0.02f), (int)(S * 0.27f), rgb565(236,226,205));
      fillCircle(cx + (int)(S * 0.11f), cy + (int)(S * 0.11f), (int)(S * 0.06f), rgb565(241,231,210)); // hueco
      fillCircle(cx - (int)(S * 0.14f), cy - (int)(S * 0.06f), (int)(S * 0.045f), rgb565(230,70,70));
      fillCircle(cx - (int)(S * 0.02f), cy - (int)(S * 0.13f), (int)(S * 0.045f), rgb565(240,200,60));
      fillCircle(cx + (int)(S * 0.10f), cy - (int)(S * 0.07f), (int)(S * 0.045f), rgb565(70,130,235));
      fillCircle(cx - (int)(S * 0.16f), cy + (int)(S * 0.09f), (int)(S * 0.045f), rgb565(80,190,90));
      strokeSeg(cx + S * 0.02f, cy - S * 0.16f, cx + S * 0.26f, cy - S * 0.30f, tk / 2 + 1, rgb565(140,100,60));
    } break;
    case IC_JUEGOS: {
      iconBase(x, y, S, rgb565(142,30,30), 22);
      drawRoundRect(x + (int)(S * 0.14f), y + (int)(S * 0.34f), (int)(S * 0.72f), (int)(S * 0.30f), (int)(S * 0.13f), WHITE);
      drawRoundRect(x + (int)(S * 0.14f) + 1, y + (int)(S * 0.34f) + 1, (int)(S * 0.72f) - 2, (int)(S * 0.30f) - 2, (int)(S * 0.12f), WHITE);
      // dpad
      fillRect(cx - (int)(S * 0.22f) - 1, cy + (int)(S * 0.02f), (int)(S * 0.12f), 3, WHITE);
      fillRect(cx - (int)(S * 0.16f) - 1, cy - (int)(S * 0.04f), 3, (int)(S * 0.12f), WHITE);
      // botones
      fillCircle(cx + (int)(S * 0.14f), cy - (int)(S * 0.01f), 3, WHITE);
      fillCircle(cx + (int)(S * 0.22f), cy + (int)(S * 0.05f), 3, WHITE);
    } break;
    case IC_AJUSTES: {
      iconBase(x, y, S, rgb565(138,143,152), 22);
      uint16_t g = rgb565(70,74,84);
      fillCircle(cx, cy, (int)(S * 0.26f), g);
      for(int k = 0; k < 8; k++){
        float a = k * 45 * 0.0174532925f;
        fillCircle((int)(cx + S * 0.30f * cosf(a)), (int)(cy + S * 0.30f * sinf(a)), (int)(S * 0.075f), g);
      }
      fillCircle(cx, cy, (int)(S * 0.10f), rgb565(138,143,152));
    } break;
    case IC_CALC: {
      iconBase(x, y, S, rgb565(58,58,60), 22);
      fillRoundRect(x + (int)(S * 0.18f), y + (int)(S * 0.16f), (int)(S * 0.64f), (int)(S * 0.16f), 3, rgb565(210,210,215));
      for(int rr = 0; rr < 3; rr++) for(int cc = 0; cc < 4; cc++){
        uint16_t bc = (cc == 3) ? rgb565(245,150,30) : rgb565(150,150,155);
        fillRoundRect(x + (int)(S * 0.18f) + cc * (int)(S * 0.17f), y + (int)(S * 0.40f) + rr * (int)(S * 0.15f),
                      (int)(S * 0.12f), (int)(S * 0.10f), 2, bc);
      }
    } break;
    case IC_CALEND: {
      iconBase(x, y, S, WHITE, 22);
      fillRect(x + (int)(S * 0.16f), y + (int)(S * 0.18f), (int)(S * 0.68f), (int)(S * 0.16f), rgb565(232,70,70));
      drawBigChar('1', cx - (int)(S * 0.60f * 0.30f), y + (int)(S * 0.36f), (int)(S * 0.44f), 3, rgb565(60,60,64));
    } break;
    case IC_CLIMA: {
      // Azulejo azul con sol tras nube. Es MARCA (como el resto de iconos):
      // no se retine con la apariencia del sistema, solo respeta gIconStyle.
      iconBase(x, y, S, rgb565(48,124,226), 22);
      fillCircleA(cx + (int)(S * 0.10f), cy - (int)(S * 0.13f), (int)(S * 0.26f), rgb565(255,214,96), 70);
      fillCircle(cx + (int)(S * 0.10f), cy - (int)(S * 0.13f), (int)(S * 0.18f), rgb565(255,206,72));
      {
        int r = (int)(S * 0.13f); if(r < 2) r = 2;
        int ccx = cx - (int)(S * 0.05f), ccy = cy + (int)(S * 0.12f);
        fillCircle(ccx - r, ccy, r, WHITE);
        fillCircle(ccx + r, ccy, (r * 4) / 5, WHITE);
        fillCircle(ccx, ccy - (r * 3) / 4, (r * 6) / 5, WHITE);
        fillRect(ccx - r - r / 2, ccy, (int)(S * 0.40f), r + 1, WHITE);
        fillCircle(ccx - r - r / 2, ccy, r / 2 + 1, WHITE);
      }
    } break;
    case IC_FLEXSTORE: {
      // Bolsa firmada de Flex Store: marca propia, vectorial y sin recursos externos.
      iconBase(x, y, S, rgb565(105,91,230), 22);
      int bx = x + (int)(S * 0.22f), by = y + (int)(S * 0.34f);
      int bw = (int)(S * 0.56f), bh = (int)(S * 0.46f);
      fillRoundRect(bx, by, bw, bh, (int)(S * 0.09f), WHITE);
      strokeSeg(cx - (int)(S * 0.16f), by + 2, cx - (int)(S * 0.10f), y + (int)(S * 0.22f), 2, WHITE);
      strokeSeg(cx - (int)(S * 0.10f), y + (int)(S * 0.22f), cx + (int)(S * 0.10f), y + (int)(S * 0.22f), 2, WHITE);
      strokeSeg(cx + (int)(S * 0.10f), y + (int)(S * 0.22f), cx + (int)(S * 0.16f), by + 2, 2, WHITE);
      fillRoundRect(cx - (int)(S * 0.16f), by + (int)(S * 0.13f), (int)(S * 0.32f), (int)(S * 0.09f),
                    (int)(S * 0.04f), rgb565(105,91,230));
    } break;
    case IC_FLEXPHONE: {
      // Telefono con onda de enlace: dice de un vistazo que esta app
      // es "tu movil aqui", y se dibuja entera dentro de su caja.
      iconBase(x, y, S, rgb565(38,132,255), 22);
      int pw = (int)(S * 0.34f), ph = (int)(S * 0.54f);
      int px0 = cx - pw / 2, py0 = y + (int)(S * 0.23f);
      fillRoundRect(px0, py0, pw, ph, (int)(S * 0.07f), WHITE);
      fillRoundRect(px0 + (int)(S * 0.04f), py0 + (int)(S * 0.07f),
                    pw - (int)(S * 0.08f), ph - (int)(S * 0.16f),
                    (int)(S * 0.03f), rgb565(38,132,255));
      fillRoundRect(cx - (int)(S * 0.05f), py0 + ph - (int)(S * 0.07f),
                    (int)(S * 0.10f), (int)(S * 0.03f), 1, WHITE);
      // Dos arcos de enlace a la derecha del aparato.
      arcStroke(px0 + pw, cy, (int)(S * 0.16f), 300, 60, 2, WHITE);
      arcStroke(px0 + pw, cy, (int)(S * 0.26f), 310, 50, 2, WHITE);
    } break;
    case IC_CAMARA: {
      iconBase(x, y, S, rgb565(74,74,78), 22);
      fillCircle(cx, cy, (int)(S * 0.27f), rgb565(30,30,32));
      fillRing(cx, cy, (int)(S * 0.27f), 3, rgb565(120,120,128));
      fillCircle(cx, cy, (int)(S * 0.16f), rgb565(60,72,95));
      fillCircle(cx - (int)(S * 0.06f), cy - (int)(S * 0.06f), (int)(S * 0.05f), rgb565(150,175,205));
      fillRoundRect(x + (int)(S * 0.66f), y + (int)(S * 0.18f), (int)(S * 0.10f), (int)(S * 0.06f), 2, rgb565(190,190,195));
    } break;
  }
}

// ---------------- Iconos de barra de estado ----------------
static void drawWifi(int cx, int by, int R, uint16_t col){
  arcStroke(cx, by, R,        225, 315, 2, col);
  arcStroke(cx, by, R * 0.66f, 225, 315, 2, col);
  arcStroke(cx, by, R * 0.33f, 225, 315, 2, col);
  fillCircle(cx, by, 2, col);
}
static void drawBattery(int x, int y, int w, int h, int level, uint16_t col){
  drawRoundRect(x, y, w, h, 2, col);
  drawRoundRect(x + 1, y + 1, w - 2, h - 2, 2, col);
  fillRect(x + w, y + h / 3, 2, h / 3, col);       // pin +
  int fw = (w - 6) * level / 100;
  if(fw > 0) fillRect(x + 3, y + 3, fw, h - 6, col);
}
