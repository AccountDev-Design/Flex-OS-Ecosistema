// #############################################################
// ##  FLEX OS -- APP JUEGOS: JUMPER
// ##  ----------------------------------------------------------
// ##  Recreacion del nivel "Jumper" para el ESP32-P4 Ultra
// ##  (GUITION JC4880P443C_I_W, MIPI-DSI 480x800, GT911).
// ##  Se dibuja sobre el lienzo LOGICO HORIZONTAL de 800x480 que
// ##  el framework ya ofrece con gLand=true; ni el pipeline
// ##  DMA2D ni el controlador del panel se tocan.
// ##
// ##  CONTENIDO
// ##    1. Constantes y estado
// ##    2. Guardado (Preferences, claves jmp_*)
// ##    3. Utilidades (color HSV, ruido, suavizados, texto grueso)
// ##    4. Consulta del mundo (suelo, techo, ventana de objetos)
// ##    5. Fisica de paso fijo (cubo y nave)
// ##    6. Particulas (pool estatico, sin heap)
// ##    7. Dibujo (fondo de una pasada, objetos, jugador, HUD)
// ##    8. Introduccion animada
// ##    9. Selector de nivel
// ##   10. Final y resultados
// ##   11. Bucle publico: jmpEnter() / jmpTick()
// ##
// ##  REGLAS QUE SE CUMPLEN AQUI
// ##    · Cero framebuffers nuevos: se compone en bbuf y se
// ##      publica UNA sola vez por cuadro.
// ##    · Cero memoria dinamica durante el juego: todo el estado
// ##      es estatico y acotado.
// ##    · Cero delay(): el tick vuelve siempre en el mismo cuadro.
// ##    · La fisica va a paso FIJO de 120 Hz con acumulador de
// ##      tiempo real, asi que la velocidad del nivel no depende
// ##      de la frecuencia del loop().
// ##    · Sin audio, sin red.
// #############################################################
#pragma once
#include "FlexOS_Jumper_Level.h"

// -------- Instrumentacion opcional (APAGADA por defecto) -----------------
// #define JMP_PROFILE 1

// #############################################################
// ##  1. CONSTANTES Y ESTADO
// #############################################################

// Lienzo logico horizontal.
#define JW        LW          // 800
#define JH        LH          // 480

#define JB        45.0f       // pixeles por bloque (480/45 = 10.67 bloques de alto,
                              // la misma altura visible que el video de referencia)
#define JGY       410         // y logica de la superficie del suelo base
#define JPX       230.0f      // x logica fija del jugador (28% del ancho, como el video)

#define JSPD      (JL_SPEED_BLK * JB)   // 468 px/s -> 900 bloques en 86.54 s
#define JLEN      (JL_LEN_BLK * JB)     // longitud del nivel en pixeles

// Cubo: salto de 2.38 bloques de alto y 0.425 s de vuelo (el de Geometry Dash
// a velocidad 1x). De ahi salen la gravedad y el impulso.
#define JJUMP_V   1007.0f
#define JGRAV_A   4738.0f
#define JVMAX     1600.0f
// Nave.
#define JSHIP_A   2500.0f
#define JSHIP_V    640.0f

#define JSTEP     (1.0f / 120.0f)   // paso fijo de fisica
#define JMAXSTEP  10                // tope del acumulador (anti "spiral of death")

// Tamanos de colision (algo menores que el dibujo, como pide el juego original).
#define JCUBE_DRAW  40.0f
#define JCUBE_HIT   34.0f
#define JSHIP_DW    42.0f
#define JSHIP_DH    24.0f
#define JSHIP_HW    32.0f
#define JSHIP_HH    17.0f

enum : uint8_t { JM_CUBE = 0, JM_SHIP = 1 };
enum : uint8_t {
  JS_INTRO = 0, JS_SELECT, JS_PLAY, JS_DEAD, JS_DONE, JS_PAUSE, JS_INFO
};

struct JmpPart {                 // pool unico para TODOS los efectos
  float x, y, vx, vy;
  uint16_t col;
  uint8_t life, life0, kind, size;
};
#define JPART_N 64

struct JmpState {
  uint8_t  screen;
  bool     practice;
  bool     introDone;            // solo se ve al abrir la app

  // --- reloj ---
  uint32_t lastUs;
  float    acc;                  // acumulador de fisica
  float    tScreen;              // segundos dentro de la pantalla actual

  // --- jugador ---
  float    px, py, vy;
  float    camX, camY;
  int8_t   gdir;                 // +1 normal, -1 invertida
  uint8_t  mode;                 // JM_CUBE / JM_SHIP
  bool     onGround;
  float    rot;                  // grados
  float    tilt;                 // nave
  bool     held, tapEdge;

  // --- partida ---
  uint16_t attempt;
  uint32_t jumps;
  float    runTime;
  uint8_t  coinGot;              // bits de las monedas cogidas EN ESTE intento
  int      bgIdx;                // disparador de color activo
  float    hue; uint8_t sat, val, pulse;
  float    shake;
  float    deadT;
  float    doneT;
  float    flash;                // destello de portal
  int      chk;                  // punto de control (practica)
  float    chkPy, chkCamY; int8_t chkG; uint8_t chkMode;

  // --- progreso guardado ---
  uint8_t  bestNormal, bestPractice, coinSave;
  bool     completed;
  uint32_t attemptsTotal;

  // --- animaciones de interfaz ---
  float    selT, barN, barP, arrowL, arrowR;
  int8_t   pendSave;

  JmpPart  part[JPART_N];
  uint8_t  partHead;
};
static JmpState JG;

// #############################################################
// ##  2. GUARDADO
// ##  Namespace "flexos" (el del resto del sistema) con claves
// ##  NUEVAS con prefijo jmp_: no chocan con nada anterior y no
// ##  reutilizan ni un byte del motor retirado. Solo se escribe
// ##  cuando cambia un record o un estado, jamas por cuadro.
// #############################################################
static void jmpLoad(){
  prefs.begin("flexos", true);
  JG.bestNormal    = (uint8_t)prefs.getUInt("jmp_bn", 0);
  JG.bestPractice  = (uint8_t)prefs.getUInt("jmp_bp", 0);
  JG.coinSave      = (uint8_t)prefs.getUInt("jmp_coin", 0);
  JG.completed     = prefs.getBool("jmp_cmp", false);
  JG.attemptsTotal = (uint32_t)prefs.getUInt("jmp_att", 0);
  prefs.end();
  if(JG.bestNormal   > 100) JG.bestNormal   = 100;
  if(JG.bestPractice > 100) JG.bestPractice = 100;
  JG.coinSave &= 0x07;
}
static void jmpSave(){
  prefs.begin("flexos", false);
  prefs.putUInt("jmp_bn",   JG.bestNormal);
  prefs.putUInt("jmp_bp",   JG.bestPractice);
  prefs.putUInt("jmp_coin", JG.coinSave);
  prefs.putBool("jmp_cmp",  JG.completed);
  prefs.putUInt("jmp_att",  (unsigned int)JG.attemptsTotal);
  prefs.end();
}

// #############################################################
// ##  3. UTILIDADES
// #############################################################
static inline float jclampf(float v, float a, float b){ return v < a ? a : (v > b ? b : v); }
static inline float jlerp(float a, float b, float t){ return a + (b - a) * t; }
static inline float jeaseOut(float t){ t = jclampf(t,0,1); float u = 1 - t; return 1 - u*u*u; }
static inline float jeaseIn (float t){ t = jclampf(t,0,1); return t*t*t; }
// Rebote elastico de entrada (el titulo y la tarjeta del selector).
static float jeaseBack(float t){
  t = jclampf(t, 0, 1);
  float u = t - 1.0f;
  return 1.0f + u * u * (2.70158f * u + 1.70158f);
}
// Ruido determinista: mismo x -> mismo valor. Las decoraciones del fondo
// salen de aqui, asi que no hace falta guardarlas en ninguna tabla.
static inline uint32_t jhash(uint32_t v){
  v ^= v >> 16; v *= 0x7feb352dU; v ^= v >> 15; v *= 0x846ca68bU; v ^= v >> 16;
  return v;
}
static inline float jrnd01(uint32_t v){ return (jhash(v) & 0xFFFF) / 65535.0f; }

// HSV -> RGB565. h en grados (se envuelve), s/v en 0..255.
static uint16_t jhsv(float h, int s, int v){
  h = fmodf(h, 360.0f); if(h < 0) h += 360.0f;
  float c = (v / 255.0f) * (s / 255.0f);
  float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
  float m = (v / 255.0f) - c;
  float r, g, b;
  int k = (int)(h / 60.0f);
  switch(k){
    case 0:  r = c; g = x; b = 0; break;
    case 1:  r = x; g = c; b = 0; break;
    case 2:  r = 0; g = c; b = x; break;
    case 3:  r = 0; g = x; b = c; break;
    case 4:  r = x; g = 0; b = c; break;
    default: r = c; g = 0; b = x; break;
  }
  int R = (int)((r + m) * 255.0f + 0.5f), G = (int)((g + m) * 255.0f + 0.5f), B = (int)((b + m) * 255.0f + 0.5f);
  if(R > 255) R = 255; if(G > 255) G = 255; if(B > 255) B = 255;
  if(R < 0) R = 0; if(G < 0) G = 0; if(B < 0) B = 0;
  return rgb565((uint8_t)R, (uint8_t)G, (uint8_t)B);
}
// Escala un RGB565 ya mezclado (para variantes de un mismo color sin repetir
// la conversion HSV: se usa por PANEL de fondo, no por pixel).
static inline uint16_t jshade(uint16_t c, int num, int den){
  int r = ((c >> 11) & 0x1F) * num / den; if(r > 31) r = 31;
  int g = ((c >>  5) & 0x3F) * num / den; if(g > 63) g = 63;
  int b = ( c        & 0x1F) * num / den; if(b > 31) b = 31;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

// Texto grueso con contorno y sombra: la fuente vectorial del sistema
// dibujada varias veces desplazada. Es la opcion de MENOS memoria (no anade
// ni un glifo nuevo) y da el aspecto de los rotulos del juego.
static const int8_t JFAT[8][2] = { {-1,0}, {1,0}, {0,-1}, {0,1}, {-1,-1}, {1,-1}, {-1,1}, {1,1} };
// Rotulo con contorno: ocho pasadas de la fuente vectorial. Se sigue usando
// para textos CORTOS y esporadicos (botones, panel de resultados). Para los
// rotulos grandes que se repintan en cada cuadro esta el atlas de abajo, que
// cuesta una decima parte.
static void jmpTextFat(int cx, int y, const char* s, int size,
                       uint16_t fill, uint16_t edge, uint16_t shadow, int th){
  int w = textW(s, size), x = cx - w / 2;
  if(th < 1) th = 1;
  if(shadow != fill) drawTextA(x + th + 2, y + th + 3, s, size, shadow, 190);
  for(int i = 0; i < 8; i++)
    drawTextA(x + JFAT[i][0] * th, y + JFAT[i][1] * th, s, size, edge, 255);
  drawTextA(x, y, s, size, fill, 255);
}

// Variante barata: sombra + relleno (dos pasadas). Para etiquetas pequenas
// que no necesitan un contorno completo pero si despegarse del fondo.
static void jmpTextShadow(int cx, int y, const char* s, int size,
                          uint16_t fill, uint16_t shadow){
  int x = cx - textW(s, size) / 2;
  drawTextA(x + 2, y + 2, s, size, shadow, 255);
  drawTextA(x, y, s, size, fill, 255);
}
// Contorno de cuatro direcciones (cinco pasadas en vez de diez): la mitad de
// coste y, a tamanos medianos, practicamente indistinguible.
static void jmpTextEdge4(int cx, int y, const char* s, int size,
                         uint16_t fill, uint16_t edge, int th){
  int x = cx - textW(s, size) / 2;
  if(th < 1) th = 1;
  drawTextA(x - th, y, s, size, edge, 255); drawTextA(x + th, y, s, size, edge, 255);
  drawTextA(x, y - th, s, size, edge, 255); drawTextA(x, y + th, s, size, edge, 255);
  drawTextA(x, y, s, size, fill, 255);
}

// #############################################################
// ##  ATLAS DE ROTULO (1 bit por pixel)
// ##  ----------------------------------------------------------
// ##  Medido en el banco de host: el rotulo "GEOMETRY DASH" de la
// ##  intro costaba 5.49 ms de los 7.24 ms del cuadro -- el 76% --
// ##  porque son doce letras por diez pasadas de una fuente
// ##  vectorial con bilineal y mezcla alfa POR PIXEL.
// ##
// ##  Aqui cada letra se rasteriza UNA sola vez a dos mascaras de
// ##  1 bit (relleno y borde) y despues cada cuadro son dos
// ##  volcados planos por letra: sin coma flotante, sin alfa y sin
// ##  volver a tocar la fuente. Se conserva la animacion letra a
// ##  letra porque el atlas guarda cada glifo en su propia celda.
// ##
// ##  El rasterizado usa bbuf como borrador: es el back buffer que
// ##  el cuadro siguiente repinta entero, asi que no hace falta ni
// ##  un byte de memoria extra para construirlo.
// #############################################################
#define JCAP_W       736
#define JCAP_H        76
#define JCAP_STRIDE  (JCAP_W / 8)
#define JCAP_MAX      16
static uint8_t  jCapFill[JCAP_STRIDE * JCAP_H];
static uint8_t  jCapEdge[JCAP_STRIDE * JCAP_H];
static uint16_t jCapX[JCAP_MAX + 1];      // inicio de cada celda
static uint8_t  jCapN = 0, jCapH = 0;
static char     jCapKey[JCAP_MAX + 1] = { 0 };
static uint8_t  jCapSize = 0;

static inline bool jCapBit(const uint8_t* m, int x, int y){
  return (m[y * JCAP_STRIDE + (x >> 3)] >> (7 - (x & 7))) & 1;
}
// Lee la region [0,w)x[0,h) del borrador y marca los bits con tinta.
static void jCapGrab(uint8_t* dst, int w, int h){
  memset(dst, 0, JCAP_STRIDE * JCAP_H);
  for(int y = 0; y < h && y < JCAP_H; y++)
    for(int x = 0; x < w && x < JCAP_W; x++)
      if(gBuf[(size_t)x * SCR_W + (SCR_W - 1 - y)])
        dst[y * JCAP_STRIDE + (x >> 3)] |= (uint8_t)(0x80 >> (x & 7));
}
// Construye el atlas de `s` al tamano `size` con contorno `th`.
// Devuelve false si no cabe (entonces se sigue usando jmpTextFat).
static bool jmpCapBuild(const char* s, int size, int th){
  int n = (int)strlen(s);
  if(n > JCAP_MAX) return false;
  if(jCapSize == size && !strcmp(jCapKey, s)) return true;      // ya esta
  int hh = size * 5 + 2 * th + 4;
  if(hh > JCAP_H) return false;
  char c[2] = { 0, 0 };
  int x = 0;
  for(int i = 0; i < n; i++){
    c[0] = s[i];
    jCapX[i] = (uint16_t)x;
    x += textW(c, size) + 2 * th + 1;
  }
  if(x > JCAP_W) return false;
  jCapX[n] = (uint16_t)x;
  // relleno
  fillRect(0, 0, x, hh, 0);
  for(int i = 0; i < n; i++){ c[0] = s[i]; drawTextA(jCapX[i] + th, th, c, size, 0xFFFF, 255); }
  jCapGrab(jCapFill, x, hh);
  // borde: las ocho pasadas, pero UNA sola vez en toda la vida del rotulo
  fillRect(0, 0, x, hh, 0);
  for(int i = 0; i < n; i++){
    c[0] = s[i];
    for(int k = 0; k < 8; k++)
      drawTextA(jCapX[i] + th + JFAT[k][0] * th, th + JFAT[k][1] * th, c, size, 0xFFFF, 255);
  }
  jCapGrab(jCapEdge, x, hh);
  fillRect(0, 0, x, hh, 0);
  jCapN = (uint8_t)n; jCapH = (uint8_t)hh; jCapSize = (uint8_t)size;
  memcpy(jCapKey, s, n); jCapKey[n] = 0;
  return true;
}
// Vuelca la celda `idx` con su esquina superior izquierda en (px,py).
static void jmpCapCell(int idx, int px, int py, uint16_t fill, uint16_t edge){
  if(idx < 0 || idx >= jCapN) return;
  int x0 = jCapX[idx], x1 = jCapX[idx + 1];
  for(int mx = x0; mx < x1; mx++){
    int lx = px + (mx - x0);
    if((unsigned)lx >= (unsigned)JW) continue;
    int y0 = py, y1 = py + jCapH;
    int my = 0;
    if(y0 < 0){ my = -y0; y0 = 0; }
    if(y1 > JH) y1 = JH;
    if(y0 >= y1) continue;
    uint16_t* p = gBuf + (size_t)lx * SCR_W + (SCR_W - 1 - y0);
    for(int ly = y0; ly < y1; ly++, my++, p--){
      if(jCapBit(jCapFill, mx, my)) *p = fill;
      else if(jCapBit(jCapEdge, mx, my)) *p = edge;
    }
  }
}
static inline int jmpCapCellW(int idx){ return jCapX[idx + 1] - jCapX[idx]; }
static int jmpCapTotal(){ return jCapN ? jCapX[jCapN] : 0; }

// #############################################################
// ##  4. CONSULTA DEL MUNDO
// ##  ----------------------------------------------------------
// ##  Todo en PIXELES de mundo: wx desde el inicio del nivel, wy
// ##  sobre la superficie del suelo base (positivo hacia arriba).
// ##  Las tablas estan ordenadas por x, asi que la ventana visible
// ##  se localiza con una busqueda binaria y a partir de ahi se
// ##  recorre en linea recta: NUNCA se recorre el nivel entero.
// #############################################################
static inline float jQX2px(uint16_t q){ return q * (JB * 0.25f); }
static inline float jHY2px(int8_t  h){ return h * (JB * 0.5f);  }

// Primer objeto con x >= qx (busqueda binaria sobre la tabla en flash).
static int jObjLower(uint16_t qx){
  int lo = 0, hi = JL_OBJ_N;
  while(lo < hi){ int m = (lo + hi) >> 1; if(JL_OBJ[m].x < qx) lo = m + 1; else hi = m; }
  return lo;
}
// Altura del suelo (px) en wx.
static float jFloorAt(float wx){
  uint16_t q = (uint16_t)(wx / (JB * 0.25f));
  for(int i = JL_FLOOR_N - 1; i >= 0; i--)
    if(q >= JL_FLOOR[i].x0 && q < JL_FLOOR[i].x1) return jHY2px(JL_FLOOR[i].y);
  return 0.0f;
}
// Techo (px) en wx, o 1e9 si en ese tramo no hay.
static float jCeilAt(float wx){
  uint16_t q = (uint16_t)(wx / (JB * 0.25f));
  for(int i = 0; i < JL_CEIL_N; i++)
    if(q >= JL_CEIL[i].x0 && q < JL_CEIL[i].x1) return jHY2px(JL_CEIL[i].y);
  return 1e9f;
}
static bool jEdgeSpikes(float wx, int side){
  uint16_t q = (uint16_t)(wx / (JB * 0.25f));
  for(int i = 0; i < JL_EDGE_N; i++)
    if(JL_EDGE[i].y == side && q >= JL_EDGE[i].x0 && q < JL_EDGE[i].x1) return true;
  return false;
}

// Caja de un objeto solido. Devuelve false si el tipo no es solido.
static bool jSolidBox(const JmpObj& o, float& x0, float& y0, float& x1, float& y1){
  float ox = jQX2px(o.x), oy = jHY2px(o.y);
  switch(o.t){
    case JO_BLOCK: x0 = ox; y0 = oy; x1 = ox + o.a * (JB * 0.5f); y1 = oy + o.b * (JB * 0.5f); return true;
    case JO_BULLET:x0 = ox; y0 = oy; x1 = ox + JB;               y1 = oy + JB;                return true;
    case JO_PLATE: x0 = ox; y0 = oy; x1 = ox + o.a * (JB * 0.5f); y1 = oy + JB * 0.5f;         return true;
    default: return false;
  }
}
// #############################################################
// ##  PELIGROS: UN TRIANGULO, UNA COLISION
// ##  ----------------------------------------------------------
// ##  Un grupo de 2 o 3 pinchos NO es una caja unica. Con una caja
// ##  continua, el hueco en forma de V que queda entre dos
// ##  triangulos vecinos -- que se ve vacio -- mataba igual: esa
// ##  era la causa de las "muertes invisibles".
// ##
// ##  Cada pincho se prueba por separado y su forma se aproxima
// ##  con tres bandas INSCRITAS en el triangulo dibujado: el ancho
// ##  de cada banda es el que tiene el triangulo en su borde MAS
// ##  ESTRECHO, asi que la colision cabe entera dentro del dibujo y
// ##  nunca sobresale. Es la regla del juego original: la punta
// ##  perdona, el centro mata.
// #############################################################
// Cada familia de pincho lleva SU medida util, siempre por debajo del dibujo:
//   ..._H  = largo desde la base hasta la punta
//   ..._HW = media anchura de la base
#define JSPK_H    (JB * 0.85f)      // pinchos de suelo/techo (dibujo 0.92)
#define JSPK_HW   (JB * 0.46f)      // (dibujo 0.50)
#define JSPKS_H   (JB * 0.78f)      // pinchos laterales (dibujo 0.85)
#define JSPKS_HW  (JB * 0.23f)      // (dibujo 0.25)
#define JSPKE_H   (JB * 0.50f)      // pinchos continuos del pasillo de nave (dibujo 0.55)
#define JSPKE_HW  (JB * 0.46f)      // (dibujo 0.50)

static inline bool jOv(float a0, float a1, float b0, float b1){ return a0 < b1 && b0 < a1; }

// Triangulo isosceles con la base en `base` y el eje de simetria en `cen`,
// aproximado por tres bandas INSCRITAS: cada banda usa el ancho del borde mas
// estrecho, asi que la colision entera cabe dentro del dibujo.
//   axis 0 -> el triangulo crece en Y (dir +1 arriba, -1 abajo); `cen` es X
//   axis 1 -> crece en X            (dir +1 derecha, -1 izquierda); `cen` es Y
static bool jSpikeHit(float cen, float base, int axis, int dir, float len, float hw0,
                      float px0, float py0, float px1, float py1){
  for(int k = 0; k < 3; k++){
    float f0 = k * (1.0f / 3.0f), f1 = (k + 1) * (1.0f / 3.0f);
    float hw = hw0 * (1.0f - f1);
    if(hw <= 0.5f) break;
    float d0 = len * f0, d1 = len * f1;
    float a0 = (dir > 0) ? base + d0 : base - d1;
    float a1 = (dir > 0) ? base + d1 : base - d0;
    if(axis == 0){ if(jOv(px0, px1, cen - hw, cen + hw) && jOv(py0, py1, a0, a1)) return true; }
    else         { if(jOv(px0, px1, a0, a1) && jOv(py0, py1, cen - hw, cen + hw)) return true; }
  }
  return false;
}

// Prueba de peligro de un objeto contra la caja del jugador.
static bool jHazHit(const JmpObj& o, float px0, float py0, float px1, float py1){
  float ox = jQX2px(o.x), oy = jHY2px(o.y);
  switch(o.t){
    case JO_SPIKE:
      for(int k = 0; k < o.a; k++)
        if(jSpikeHit(ox + k * JB + JB * 0.5f, oy, 0, +1, JSPK_H, JSPK_HW, px0, py0, px1, py1)) return true;
      return false;
    case JO_SPIKED:
      for(int k = 0; k < o.a; k++)
        if(jSpikeHit(ox + k * JB + JB * 0.5f, oy, 0, -1, JSPK_H, JSPK_HW, px0, py0, px1, py1)) return true;
      return false;
    case JO_SPIKEL: {
      int n = o.b ? o.b : 1;
      for(int k = 0; k < n; k++)
        if(jSpikeHit(oy + k * JB * 0.5f + JB * 0.25f, ox, 1, -1, JSPKS_H, JSPKS_HW, px0, py0, px1, py1)) return true;
      return false;
    }
    case JO_SPIKER: {
      int n = o.b ? o.b : 1;
      for(int k = 0; k < n; k++)
        if(jSpikeHit(oy + k * JB * 0.5f + JB * 0.25f, ox, 1, +1, JSPKS_H, JSPKS_HW, px0, py0, px1, py1)) return true;
      return false;
    }
    default: return false;
  }
}

// Envolvente GRUESA del grupo. Solo para descarte rapido, el modo de
// depuracion y el auditor: NUNCA decide una muerte por si sola.
static bool jHazBox(const JmpObj& o, float& x0, float& y0, float& x1, float& y1){
  float ox = jQX2px(o.x), oy = jHY2px(o.y);
  switch(o.t){
    case JO_SPIKE:  x0 = ox; y0 = oy;            x1 = ox + o.a*JB; y1 = oy + JSPK_H; return true;
    case JO_SPIKED: x0 = ox; y0 = oy - JSPK_H;   x1 = ox + o.a*JB; y1 = oy;          return true;
    case JO_SPIKEL: x0 = ox - JSPKS_H; y0 = oy;  x1 = ox;           y1 = oy + (o.b?o.b:1)*JB*0.5f; return true;
    case JO_SPIKER: x0 = ox;           y0 = oy;  x1 = ox + JSPKS_H; y1 = oy + (o.b?o.b:1)*JB*0.5f; return true;
    default: return false;
  }
}

// #############################################################
// ##  5. FISICA
// #############################################################
static void jmpSpawnParts(float x, float y, int n, uint16_t col, uint8_t kind, float spd);

static inline void jmpPlayerBox(float& x0, float& y0, float& x1, float& y1){
  if(JG.mode == JM_CUBE){
    x0 = JG.px - JCUBE_HIT * 0.5f; x1 = JG.px + JCUBE_HIT * 0.5f;
    y0 = JG.py + (JCUBE_DRAW - JCUBE_HIT) * 0.5f;
    y1 = y0 + JCUBE_HIT;
  } else {
    x0 = JG.px - JSHIP_HW * 0.5f; x1 = JG.px + JSHIP_HW * 0.5f;
    y0 = JG.py + (JSHIP_DH - JSHIP_HH) * 0.5f;
    y1 = y0 + JSHIP_HH;
  }
}
static inline float jmpBodyH(){ return JG.mode == JM_CUBE ? JCUBE_DRAW : JSHIP_DH; }

static void jmpDie();
static void jmpFinish();
static void jmpApplyBg(float wx);

// #############################################################
// ##  LATCH DE ENTRADA
// ##  ----------------------------------------------------------
// ##  flexPollTouch() corre UNA vez por vuelta del loop y la fisica
// ##  va a pasos fijos de 1/120 s. Si un cuadro tarda menos que un
// ##  paso -- o si el acumulador se queda corto por reparto de
// ##  tiempo -- el bucle `while` no ejecuta ningun paso y el flanco
// ##  se perdia: era la causa de los toques ignorados.
// ##
// ##  Ahora el flanco se GUARDA aqui hasta que un paso de fisica lo
// ##  consuma, y se consume UNA sola vez. Un toque corto no puede
// ##  caer entre dos pasos y desaparecer.
// #############################################################
static bool  jJumpQueued = false;   // flanco de toque pendiente de consumir
static float jTapWin     = 0.0f;    // ventana de salto/orbe abierta por ese flanco
#define JTAPWIN 0.12f               // 120 ms: un toque humano cabe de sobra

static inline void jmpClearInput(){ jJumpQueued = false; jTapWin = 0.0f; JG.held = false; }

// Un paso fijo de simulacion.
static void jmpStep(float dt){
  if(JG.screen != JS_PLAY) return;
  const float prevX = JG.px, prevY = JG.py;

  // ---- avance horizontal (constante: el nivel no cambia de velocidad) ----
  JG.px += JSPD * dt;
  JG.runTime += dt;

  // ---- portales: se disparan al CRUZAR su x ----
  {
    uint16_t q0 = (uint16_t)(prevX / (JB * 0.25f)), q1 = (uint16_t)(JG.px / (JB * 0.25f));
    if(q1 > q0){
      int i = jObjLower(q0 + 1);
      for(; i < JL_OBJ_N && JL_OBJ[i].x <= q1; i++){
        const JmpObj& o = JL_OBJ[i];
        if(o.t == JO_PGRAV){
          int8_t ng = o.a ? +1 : -1;
          if(ng != JG.gdir){ JG.gdir = ng; JG.flash = 1.0f;
            jmpSpawnParts(JG.px, jHY2px(o.y), 10, rgb565(255,235,120), 2, 200.0f); }
        } else if(o.t == JO_PSHIP && JG.mode != JM_SHIP){
          JG.mode = JM_SHIP; JG.vy *= 0.4f; JG.flash = 1.0f; JG.rot = 0;
          jmpSpawnParts(JG.px, jHY2px(o.y), 12, rgb565(255,130,225), 2, 220.0f);
        } else if(o.t == JO_PCUBE && JG.mode != JM_CUBE){
          JG.mode = JM_CUBE; JG.flash = 1.0f; JG.rot = 0;
          jmpSpawnParts(JG.px, jHY2px(o.y), 12, rgb565(130,255,150), 2, 220.0f);
        }
      }
    }
  }

  // ---- control ----
  // El flanco guardado se consume AQUI, en el primer paso que corra.
  if(jJumpQueued){ jJumpQueued = false; jTapWin = JTAPWIN; }
  else if(jTapWin > 0) jTapWin -= dt;
  if(JG.mode == JM_CUBE){
    // Mantener pulsado encadena saltos, pero SOLO al volver a tocar suelo.
    if(JG.onGround && (JG.held || jTapWin > 0)){
      JG.vy = JJUMP_V * JG.gdir; JG.onGround = false; JG.jumps++; jTapWin = 0;
    }
    JG.vy -= JGRAV_A * JG.gdir * dt;
    if(JG.vy >  JVMAX) JG.vy =  JVMAX;
    if(JG.vy < -JVMAX) JG.vy = -JVMAX;
  } else {
    float a = JG.held ? (JSHIP_A * JG.gdir) : (-JSHIP_A * JG.gdir);
    JG.vy += a * dt;
    if(JG.vy >  JSHIP_V) JG.vy =  JSHIP_V;
    if(JG.vy < -JSHIP_V) JG.vy = -JSHIP_V;
  }
  JG.py += JG.vy * dt;

  // ---- limites del tramo: suelo y techo ----
  const float bh = jmpBodyH();
  float fl = jFloorAt(JG.px), ce = jCeilAt(JG.px);
  bool land = false;
  if(JG.gdir > 0){
    if(JG.py <= fl){ JG.py = fl; JG.vy = 0; land = true; }
    if(ce < 1e8f && JG.py + bh >= ce){ JG.py = ce - bh; if(JG.vy > 0) JG.vy = 0; if(JG.mode == JM_SHIP) land = true; }
  } else {
    if(ce < 1e8f && JG.py + bh >= ce){ JG.py = ce - bh; JG.vy = 0; land = true; }
    if(JG.py <= fl){ JG.py = fl; if(JG.vy < 0) JG.vy = 0; if(JG.mode == JM_SHIP) land = true; }
  }
  // Pinchos continuos del borde en los pasillos de nave: tambien uno a uno.
  {
    float px0, py0, px1, py1; jmpPlayerBox(px0, py0, px1, py1);
    if(py0 < fl + JSPKE_H || (ce < 1e8f && py1 > ce - JSPKE_H)){
      int k0 = (int)floorf((px0 - JB) / JB), k1 = (int)ceilf((px1 + JB) / JB);
      for(int k = k0; k <= k1; k++){
        float cx = k * JB + JB * 0.5f;
        if(jEdgeSpikes(cx, 0) && jSpikeHit(cx, fl, 0, +1, JSPKE_H, JSPKE_HW, px0, py0, px1, py1)){ jmpDie(); return; }
        if(ce < 1e8f && jEdgeSpikes(cx, 1) && jSpikeHit(cx, ce, 0, -1, JSPKE_H, JSPKE_HW, px0, py0, px1, py1)){ jmpDie(); return; }
      }
    }
  }

  // ---- objetos de la ventana cercana ----
  float px0, py0, px1, py1; jmpPlayerBox(px0, py0, px1, py1);
  uint16_t qa = (uint16_t)(jclampf((JG.px - 4 * JB) / (JB * 0.25f), 0, 65000));
  uint16_t qb = (uint16_t)(jclampf((JG.px + 4 * JB) / (JB * 0.25f), 0, 65000));
  int i0 = jObjLower(qa);
  for(int i = i0; i < JL_OBJ_N && JL_OBJ[i].x <= qb; i++){
    const JmpObj& o = JL_OBJ[i];
    float x0, y0, x1, y1;

    if(jSolidBox(o, x0, y0, x1, y1)){
      if(px1 <= x0 || px0 >= x1 || py1 <= y0 || py0 >= y1) continue;
      // El criterio de "aterrizar" mira donde estaban los pies ANTES del paso:
      // si venian por encima de la cara superior, se apoya; si no, es un muro.
      float prevBot = prevY + (py0 - JG.py);
      float prevTop = prevBot + (py1 - py0);
      if(JG.gdir > 0){
        if(JG.vy <= 0 && prevBot >= y1 - 2.0f){
          JG.py += (y1 - py0); JG.vy = 0; land = true;
        } else if(JG.vy > 0 && prevTop <= y0 + 2.0f){
          JG.py -= (py1 - y0); JG.vy = 0;
          if(JG.mode == JM_SHIP) land = true;
        } else { jmpDie(); return; }
      } else {
        if(JG.vy >= 0 && prevTop <= y0 + 2.0f){
          JG.py -= (py1 - y0); JG.vy = 0; land = true;
        } else if(JG.vy < 0 && prevBot >= y1 - 2.0f){
          JG.py += (y1 - py0); JG.vy = 0;
          if(JG.mode == JM_SHIP) land = true;
        } else { jmpDie(); return; }
      }
      jmpPlayerBox(px0, py0, px1, py1);
      continue;
    }

    if(jHazBox(o, x0, y0, x1, y1)){
      // Descarte por envolvente y, solo si roza, prueba triangulo a triangulo.
      if(px1 > x0 && px0 < x1 && py1 > y0 && py0 < y1 && jHazHit(o, px0, py0, px1, py1)){ jmpDie(); return; }
      continue;
    }

    if(o.t == JO_COIN){
      float cx = jQX2px(o.x), cy = jHY2px(o.y);
      float dx = (px0 + px1) * 0.5f - cx, dy = (py0 + py1) * 0.5f - cy;
      if(dx * dx + dy * dy < (JB * 0.75f) * (JB * 0.75f) && !(JG.coinGot & (1 << o.a))){
        JG.coinGot |= (1 << o.a);
        jmpSpawnParts(cx, cy, 12, rgb565(255,205,60), 3, 190.0f);
      }
      continue;
    }

    if(o.t == JO_ORB && JG.mode == JM_CUBE){
      float cx = jQX2px(o.x), cy = jHY2px(o.y);
      float dx = (px0 + px1) * 0.5f - cx, dy = (py0 + py1) * 0.5f - cy;
      // Ventana de activacion: hay que estar TOCANDO el orbe y pulsar.
      if(jTapWin > 0 && dx * dx + dy * dy < (JB * 0.95f) * (JB * 0.95f)){
        jTapWin = 0;
        if(o.a == 1) JG.gdir = (int8_t)-JG.gdir;      // orbe azul: invierte
        JG.vy = JJUMP_V * JG.gdir * (o.a == 1 ? 0.82f : 1.0f);
        JG.onGround = false; JG.jumps++;
        jmpSpawnParts(cx, cy, 8, o.a ? rgb565(120,215,255) : rgb565(255,225,90), 4, 170.0f);
      }
      continue;
    }
  }

  // ---- estado de apoyo y giro ----
  JG.onGround = land;
  if(JG.mode == JM_CUBE){
    if(land){
      float t = JG.rot / 90.0f;
      JG.rot = floorf(t + 0.5f) * 90.0f;             // encaje exacto al aterrizar
      while(JG.rot >= 360.0f) JG.rot -= 360.0f;
      while(JG.rot <    0.0f) JG.rot += 360.0f;
    } else {
      JG.rot += 424.0f * JG.gdir * dt;               // ~90 por cada cuarto de salto
    }
    JG.tilt = 0;
  } else {
    float want = jclampf(JG.vy / JSHIP_V, -1.0f, 1.0f) * 26.0f * JG.gdir;
    JG.tilt += (want - JG.tilt) * jclampf(dt * 12.0f, 0, 1);
  }

  // ---- camara ----
  JG.camX = JG.px - JPX;
  {
    float sy = JGY - (JG.py - JG.camY);
    float want = JG.camY;
    if(sy < 160.0f) want += (160.0f - sy);
    else if(sy > 320.0f) want -= (sy - 320.0f);
    if(want < 0) want = 0;
    JG.camY += (want - JG.camY) * jclampf(dt * 9.0f, 0, 1);
  }

  // ---- color del tramo ----
  jmpApplyBg(JG.px);

  // ---- meta ----
  if(JG.px >= JLEN) jmpFinish();
}

// #############################################################
// ##  6. PARTICULAS
// ##  Un unico pool estatico para estela, propulsor, portales,
// ##  orbes, muerte y final. No hay new/delete/malloc en ninguna
// ##  ruta: cuando el pool esta lleno se recicla la mas vieja.
// ##  kind: 0 estela · 1 propulsor · 2 portal · 3 moneda ·
// ##        4 orbe · 5 muerte · 6 final
// #############################################################
static void jmpSpawnParts(float x, float y, int n, uint16_t col, uint8_t kind, float spd){
  for(int k = 0; k < n; k++){
    JmpPart& p = JG.part[JG.partHead];
    JG.partHead = (uint8_t)((JG.partHead + 1) % JPART_N);
    uint32_t h = jhash((uint32_t)(x * 7.0f) + k * 2654435761u + JG.attempt * 97u + (uint32_t)(JG.runTime * 1000.0f));
    float a = (h & 1023) / 1023.0f * 6.28318f;
    float s = spd * (0.35f + ((h >> 10) & 255) / 255.0f * 0.65f);
    p.x = x; p.y = y;
    p.vx = cosf(a) * s; p.vy = sinf(a) * s;
    p.col = col; p.kind = kind;
    p.life0 = p.life = (uint8_t)(kind == 0 ? 16 : (kind == 6 ? 60 : 34));
    p.size = (uint8_t)(2 + ((h >> 18) & 3));
  }
}
static void jmpPartTick(float dt){
  for(int i = 0; i < JPART_N; i++){
    JmpPart& p = JG.part[i];
    if(!p.life) continue;
    p.x += p.vx * dt; p.y += p.vy * dt;
    if(p.kind >= 2) p.vy -= 420.0f * dt;
    p.vx *= (1.0f - jclampf(dt * 2.2f, 0, 1));
    int l = p.life - (int)(dt * 60.0f + 0.5f);
    p.life = (uint8_t)(l > 0 ? l : 0);
  }
}
static void jmpPartClear(){ for(int i = 0; i < JPART_N; i++) JG.part[i].life = 0; JG.partHead = 0; }

// #############################################################
// ##  MUERTE, REINICIO Y META
// #############################################################
static void jmpResetRun(bool keepCheckpoint);

static void jmpDie(){
  if(JG.screen != JS_PLAY) return;
  JG.screen = JS_DEAD; JG.deadT = 0; JG.shake = 1.0f;
  jmpClearInput();                       // el toque que mato no reaparece como salto
  jmpSpawnParts(JG.px, JG.py + jmpBodyH() * 0.5f, 22, rgb565(255,255,255), 5, 340.0f);
  int pct = (int)(JG.px / JLEN * 100.0f); if(pct > 100) pct = 100; if(pct < 0) pct = 0;
  uint8_t& best = JG.practice ? JG.bestPractice : JG.bestNormal;
  if(pct > best){ best = (uint8_t)pct; JG.pendSave = 1; }   // el progreso NUNCA baja
}
static void jmpFinish(){
  if(JG.screen != JS_PLAY) return;
  JG.px = JLEN;
  JG.screen = JS_DONE; JG.doneT = 0; JG.shake = 0.9f;
  jmpClearInput();
  jmpSpawnParts(JG.px, JG.py + jmpBodyH() * 0.5f, 44, rgb565(150,255,190), 6, 420.0f);
  if(JG.practice){
    if(JG.bestPractice < 100){ JG.bestPractice = 100; JG.pendSave = 1; }
  } else {
    if(JG.bestNormal < 100){ JG.bestNormal = 100; JG.pendSave = 1; }
    if(!JG.completed){ JG.completed = true; JG.pendSave = 1; }
    // Las monedas SOLO se consolidan al terminar el intento en modo normal.
    if((JG.coinSave | JG.coinGot) != JG.coinSave){ JG.coinSave |= JG.coinGot; JG.pendSave = 1; }
  }
}

// #############################################################
// ##  COLOR DEL TRAMO
// #############################################################
// La paleta es funcion PURA de x: no hay estado que arrastrar, asi que
// reaparecer en un punto de control o reiniciar da exactamente el mismo color
// que si se hubiera llegado jugando.
static void jmpApplyBg(float wx){
  uint16_t q = (uint16_t)(wx < 0 ? 0 : wx / (JB * 0.25f));
  int idx = 0;
  for(int i = 0; i < JL_BG_N; i++){ if(JL_BG[i].x <= q) idx = i; else break; }
  const JmpBg& c = JL_BG[idx];
  float h = c.hue; int s = c.sat, v = c.val;
  if(idx > 0 && c.blend > 0 && q < (uint32_t)c.x + c.blend){
    const JmpBg& b = JL_BG[idx - 1];
    float t = (float)(q - c.x) / (float)c.blend;
    t = jclampf(t, 0, 1);
    h = jlerp((float)b.hue, (float)c.hue, t);
    s = (int)jlerp((float)b.sat, (float)c.sat, t);
    v = (int)jlerp((float)b.val, (float)c.val, t);
  }
  JG.bgIdx = idx; JG.hue = h; JG.sat = (uint8_t)s; JG.val = (uint8_t)v; JG.pulse = c.pulse;
}

// #############################################################
// ##  7. DIBUJO
// ##  ----------------------------------------------------------
// ##  TODO se rasteriza por COLUMNA LOGICA. En landscape la
// ##  rotacion mapea (lx,ly) -> lx*480 + (479-ly): para un lx fijo
// ##  los pixeles son CONSECUTIVOS en memoria. Una linea logica
// ##  horizontal, en cambio, salta 960 bytes por pixel (una linea
// ##  de cache de PSRAM por pixel). Por eso aqui no hay ni una
// ##  primitiva basada en hLine.
// #############################################################
static uint16_t jSky[JH];      // color de fondo por scanline (degradado)
static uint16_t jBrk[JH];      // el mismo degradado, variante de "ladrillo"
static uint16_t jPan[2][JH];   // paneles de la intro, YA premezclados por scanline

// Copia [y0,y1) de la columna lx tomando el color de una tabla por scanline.
static inline void jmpColLut(int lx, int y0, int y1, const uint16_t* lut){
  if((unsigned)lx >= (unsigned)JW) return;
  if(y0 < 0) y0 = 0; if(y1 > JH) y1 = JH; if(y0 >= y1) return;
  uint16_t* p = gBuf + (size_t)lx * SCR_W + (SCR_W - 1 - y0);
  for(int y = y0; y < y1; y++) *p-- = lut[y];
}
// Rectangulo relleno tomando el color de una tabla por scanline: sustituye a
// un fillRectA sin pagar una mezcla alfa por pixel.
static void jmpBoxLut(int x, int y, int w, int h, const uint16_t* lut){
  if(w <= 0 || h <= 0) return;
  int x0 = x < 0 ? 0 : x, x1 = x + w; if(x1 > JW) x1 = JW;
  int y0 = y < 0 ? 0 : y, y1 = y + h; if(y1 > JH) y1 = JH;
  if(y0 >= y1) return;
  for(int lx = x0; lx < x1; lx++) jmpColLut(lx, y0, y1, lut);
}

// --- primitivas por columna ---------------------------------------------
static void jmpBox(int x, int y, int w, int h, uint16_t c){
  if(w <= 0 || h <= 0) return;
  int x0 = x < 0 ? 0 : x, x1 = x + w; if(x1 > JW) x1 = JW;
  for(int i = x0; i < x1; i++) fillSpanLand(i, y, h, c);
}
static void jmpBoxEdge(int x, int y, int w, int h, uint16_t fill, uint16_t edge, int t){
  jmpBox(x, y, w, h, fill);
  if(t <= 0) return;
  jmpBox(x, y, t, h, edge); jmpBox(x + w - t, y, t, h, edge);
  int x0 = x < 0 ? 0 : x, x1 = x + w; if(x1 > JW) x1 = JW;
  for(int i = x0; i < x1; i++){ fillSpanLand(i, y, t, edge); fillSpanLand(i, y + h - t, t, edge); }
}
static void jmpDisc(int cx, int cy, int r, uint16_t c){
  if(r <= 0) return;
  for(int dx = -r; dx <= r; dx++){
    int lx = cx + dx; if((unsigned)lx >= (unsigned)JW) continue;
    int dy = isqrt32(r * r - dx * dx);
    fillSpanLand(lx, cy - dy, 2 * dy + 1, c);
  }
}
static void jmpOval(int cx, int cy, int rx, int ry, uint16_t c){
  if(rx <= 0 || ry <= 0) return;
  for(int dx = -rx; dx <= rx; dx++){
    int lx = cx + dx; if((unsigned)lx >= (unsigned)JW) continue;
    float k = 1.0f - (float)(dx * dx) / (float)(rx * rx);
    if(k < 0) k = 0;
    int dy = (int)(ry * sqrtf(k));
    fillSpanLand(lx, cy - dy, 2 * dy + 1, c);
  }
}
static void jmpOvalRing(int cx, int cy, int rx, int ry, int th, uint16_t c){
  if(rx <= th || ry <= th) return;
  for(int dx = -rx; dx <= rx; dx++){
    int lx = cx + dx; if((unsigned)lx >= (unsigned)JW) continue;
    float k = 1.0f - (float)(dx * dx) / (float)(rx * rx); if(k < 0) k = 0;
    int dy = (int)(ry * sqrtf(k));
    int irx = rx - th, iry = ry - th;
    int idy = 0;
    if(abs(dx) < irx){ float k2 = 1.0f - (float)(dx * dx) / (float)(irx * irx); if(k2 < 0) k2 = 0; idy = (int)(iry * sqrtf(k2)); }
    if(idy > 0){
      fillSpanLand(lx, cy - dy, dy - idy, c);
      fillSpanLand(lx, cy + idy, dy - idy, c);
    } else fillSpanLand(lx, cy - dy, 2 * dy + 1, c);
  }
}
// Triangulos isosceles rasterizados por columna (los pinchos del nivel).
static void jmpTriUp(int x, int ybase, int w, int h, uint16_t c){
  if(w <= 0 || h <= 0) return;
  float half = w * 0.5f;
  for(int i = 0; i < w; i++){
    int lx = x + i; if((unsigned)lx >= (unsigned)JW) continue;
    float t = fabsf((i + 0.5f) - half) / half; if(t > 1) t = 1;
    int hh = (int)(h * (1.0f - t) + 0.5f);
    if(hh > 0) fillSpanLand(lx, ybase - hh, hh, c);
  }
}
static void jmpTriDown(int x, int ytop, int w, int h, uint16_t c){
  if(w <= 0 || h <= 0) return;
  float half = w * 0.5f;
  for(int i = 0; i < w; i++){
    int lx = x + i; if((unsigned)lx >= (unsigned)JW) continue;
    float t = fabsf((i + 0.5f) - half) / half; if(t > 1) t = 1;
    int hh = (int)(h * (1.0f - t) + 0.5f);
    if(hh > 0) fillSpanLand(lx, ytop, hh, c);
  }
}
static void jmpTriSide(int xtip, int ycen, int w, int h, int dir, uint16_t c){
  if(w <= 0 || h <= 0) return;
  for(int i = 0; i < w; i++){
    int lx = (dir > 0) ? (xtip - w + 1 + i) : (xtip + i);
    if((unsigned)lx >= (unsigned)JW) continue;
    float t = (dir > 0) ? (float)i / w : 1.0f - (float)i / w;   // 0 en la punta
    int hh = (int)(h * t + 0.5f);
    if(hh > 0) fillSpanLand(lx, ycen - hh / 2, hh, c);
  }
}
// Poligono convexo (<=8 vertices) por columna: lo usa el cubo girado y la nave.
static void jmpPoly(const float* xs, const float* ys, int n, uint16_t c){
  float mnx = xs[0], mxx = xs[0];
  for(int i = 1; i < n; i++){ if(xs[i] < mnx) mnx = xs[i]; if(xs[i] > mxx) mxx = xs[i]; }
  int x0 = (int)floorf(mnx), x1 = (int)ceilf(mxx);
  if(x0 < 0) x0 = 0; if(x1 > JW) x1 = JW;
  for(int lx = x0; lx < x1; lx++){
    float cxp = lx + 0.5f, lo = 1e9f, hi = -1e9f;
    for(int i = 0; i < n; i++){
      int j = (i + 1) % n;
      float ax = xs[i], ay = ys[i], bx = xs[j], by = ys[j];
      if((ax <= cxp && bx > cxp) || (bx <= cxp && ax > cxp)){
        float t = (cxp - ax) / (bx - ax);
        float y = ay + (by - ay) * t;
        if(y < lo) lo = y; if(y > hi) hi = y;
      }
    }
    if(hi > lo){
      int y0 = (int)(lo + 0.5f), y1 = (int)(hi + 0.5f);
      if(y1 > y0) fillSpanLand(lx, y0, y1 - y0, c);
    }
  }
}

// --- colores del cuadro (se calculan UNA vez por cuadro, nunca por pixel) --
static uint16_t jcObj, jcEdge, jcGnd, jcGnd2, jcGndLine, jcGrass;

static void jmpBuildPalette(float t){
  float pulse = 1.0f + (JG.pulse / 255.0f) * 0.10f * sinf(t * 3.4f);
  int v = (int)(JG.val * pulse); if(v > 255) v = 255;
  uint16_t cMid = jhsv(JG.hue, JG.sat, v);
  uint16_t cTop = jhsv(JG.hue, JG.sat, (int)(v * 0.70f));
  uint16_t cBot = jhsv(JG.hue, JG.sat, (int)(v * 0.52f));
  // Degradado por scanline: oscuro arriba, banda clara a media altura,
  // oscuro abajo -- la misma lectura que el fondo del video.
  for(int y = 0; y < JH; y++){
    uint16_t c;
    if(y < 210)      c = mix565(cTop, cMid, (uint8_t)(y * 255 / 210));
    else if(y < 300) c = cMid;
    else             c = mix565(cMid, cBot, (uint8_t)((y - 300) * 255 / (JH - 300)));
    jSky[y] = c;
    jBrk[y] = jshade(c, 118, 100);
  }
  jcObj     = rgb565(8, 8, 14);
  jcEdge    = rgb565(255, 255, 255);
  jcGnd     = jhsv(JG.hue, (int)(JG.sat * 0.92f), (int)(v * 0.38f));
  jcGnd2    = jhsv(JG.hue, (int)(JG.sat * 0.92f), (int)(v * 0.31f));
  jcGndLine = jhsv(JG.hue, (int)(JG.sat * 0.35f), 255);
  jcGrass   = rgb565(10, 10, 16);
}

// Atenua la paleta ENTERA (fondo, suelo y objetos) en vez de tapar el cuadro
// con un relleno alfa a pantalla completa. Un relleno asi son 384.000 mezclas
// por pixel -- 0.9 ms medidos en el banco --; esto son 960 entradas de tabla y
// ocho colores. Lo usan la pausa y el oscurecido de la muerte.
static void jmpDimPalette(int num, int den){
  if(num == den) return;
  for(int y = 0; y < JH; y++){ jSky[y] = jshade(jSky[y], num, den); jBrk[y] = jshade(jBrk[y], num, den); }
  jcObj     = jshade(jcObj, num, den);
  jcEdge    = jshade(jcEdge, num, den);
  jcGnd     = jshade(jcGnd, num, den);
  jcGnd2    = jshade(jcGnd2, num, den);
  jcGndLine = jshade(jcGndLine, num, den);
  jcGrass   = jshade(jcGrass, num, den);
}

// #############################################################
// ##  FONDO POR PLANTILLAS DE COLUMNA
// ##  ----------------------------------------------------------
// ##  El contenido de una columna de fondo solo depende de:
// ##    · si cae dentro de un panel de ladrillo  -> 2 casos,
// ##    · la paridad de la franja del suelo/techo -> 2 casos,
// ##  y de la altura de suelo y techo, que son constantes dentro
// ##  de cada tramo. O sea: en todo el cuadro hay como mucho
// ##  CUATRO columnas distintas.
// ##
// ##  Se componen esas cuatro (1920 escrituras) y cada una de las
// ##  800 columnas de pantalla se vuelca con UN memcpy contiguo de
// ##  960 bytes, en vez de 480 escrituras sueltas por columna. La
// ##  plantilla se guarda ya INVERTIDA porque la rotacion landscape
// ##  mapea ly ascendente a direcciones descendentes: asi el volcado
// ##  es una copia recta, que es lo que la PSRAM hace rapido.
// ##
// ##  Coste: 4 x 480 x 2 = 3840 bytes estaticos. Ni un framebuffer.
// #############################################################
static uint16_t jTpl[4][JH];        // [ladrillo*2 + paridad][479-ly]

static void jmpBuildTemplates(int gsY, int ceY, int offY){
  uint16_t gnd[2] = { jcGnd, jcGnd2 };
  for(int k = 0; k < 4; k++){
    const bool brick = (k >> 1) != 0;
    uint16_t* t = jTpl[k];
    uint16_t g = gnd[k & 1];
    for(int ly = 0; ly < JH; ly++){
      uint16_t c;
      if(ceY > 0 && ly < ceY){                       // techo solido
        c = (ly >= ceY - 3) ? jcGndLine : g;
      } else if(ly >= gsY){                          // suelo
        c = (ly < gsY + 3) ? jcGndLine : g;
      } else if(brick){                              // cielo con panel
        int m = (ly + offY) % 72;
        c = (m < 62) ? jBrk[ly] : jSky[ly];
      } else {
        c = jSky[ly];
      }
      t[JH - 1 - ly] = c;                            // ya invertida
    }
  }
}

static void jmpPaintWorld(float shx, float shy){
  const float camX = JG.camX + shx, camY = JG.camY;
  int offX = (int)(camX * 0.32f) % 96; if(offX < 0) offX += 96;
  int offY = (int)(-camY * 0.32f) % 72; if(offY < 0) offY += 72;

  int lastG = -32000, lastC = -32000;
  for(int lx = 0; lx < JW; lx++){
    float wx = camX + lx;
    int gsY = (int)(JGY - (jFloorAt(wx) - camY) + shy);
    float ce = jCeilAt(wx);
    int ceY = (ce < 1e8f) ? (int)(JGY - (ce - camY) + shy) : -32000;
    if(gsY > JH) gsY = JH;
    if(ceY > JH) ceY = JH;
    if(gsY != lastG || ceY != lastC){                // solo en los bordes de tramo
      jmpBuildTemplates(gsY < 0 ? 0 : gsY, ceY, offY);
      lastG = gsY; lastC = ceY;
    }
    int brick = (((lx + offX) % 96) < 86) ? 1 : 0;
    int stripe = ((int)(wx * 0.5f) / 45) & 1;
    memcpy(gBuf + (size_t)lx * SCR_W, jTpl[brick * 2 + stripe], SCR_W * 2);
  }

  // Hierba: siluetas cortas sobre la linea del suelo (decoracion, sin colision).
  for(int lx = 0; lx < JW; lx += 7){
    float wx = camX + lx;
    int gsY = (int)(JGY - (jFloorAt(wx) - camY) + shy);
    if(gsY < -8 || gsY > JH) continue;
    int h = 4 + (int)(jrnd01((uint32_t)(wx * 0.14f)) * 8.0f);
    jmpTriUp(lx, gsY, 7, h, jcGrass);
  }
  for(int lx = 0; lx < JW; lx += 7){
    float wx = camX + lx;
    float ce = jCeilAt(wx); if(ce > 1e8f) continue;
    int ceY = (int)(JGY - (ce - camY) + shy);
    if(ceY < 0 || ceY > JH) continue;
    int h = 4 + (int)(jrnd01((uint32_t)(wx * 0.19f) + 7777u) * 8.0f);
    jmpTriDown(lx, ceY, 7, h, jcGrass);
  }
}

static void jmpDrawBlock(int x, int y, int w, int h){
  jmpBoxEdge(x, y, w, h, jcObj, jcEdge, 3);
  // Rejilla interior de las columnas altas, como en el video.
  if(h > 70){
    uint16_t g = rgb565(48, 48, 60);
    for(int yy = y + 3 + 22; yy < y + h - 3; yy += 22) jmpBox(x + 4, yy, w - 8, 1, g);
    for(int xx = x + 3 + 22; xx < x + w - 3; xx += 22) jmpBox(xx, y + 4, 1, h - 8, g);
  }
}
static void jmpDrawSpike(int x, int ybase, int n, int dir){
  int sw = (int)JB, sh = (int)(JB * 0.92f);
  for(int k = 0; k < n; k++){
    int sx = x + k * sw;
    if(sx > JW || sx + sw < 0) continue;
    if(dir == 0){ jmpTriUp(sx, ybase, sw, sh, jcEdge);   jmpTriUp(sx + 4, ybase - 3, sw - 8, sh - 7, jcObj); }
    else        { jmpTriDown(sx, ybase, sw, sh, jcEdge); jmpTriDown(sx + 4, ybase + 3, sw - 8, sh - 7, jcObj); }
  }
}
// #############################################################
// ##  OBJETOS DEL NIVEL EN CAPAS
// ##  ----------------------------------------------------------
// ##  La tabla esta ordenada por X, no por profundidad. Dibujarla
// ##  de una sola pasada hacia que una columna DECORATIVA posterior
// ##  en X tapara un bloque solido anterior: el jugador veia una
// ##  plataforma "incrustada" en una torre y no sabia donde estaba
// ##  el borde real. Por eso hay cuatro barridos sobre la MISMA
// ##  ventana visible (una treintena de objetos, coste irrelevante):
// ##    1 decoracion -> 2 solidos -> 3 peligros -> 4 interactivos.
// ##  La decoracion queda SIEMPRE detras de todo lo que se juega.
// #############################################################
static void jmpDecorPass(int sx, int sy, const JmpObj& o){
  if(o.t == JO_TOWER){
    int w = (int)(o.a * JB * 0.5f), h = (int)(o.b * JB * 0.5f);
    // Tono derivado del CIELO: se lee como silueta de fondo y nunca se
    // confunde con la geometria solida, que es negra con borde blanco.
    int mid = (int)jclampf((float)(sy - h / 2), 0, JH - 1);
    uint16_t body = jshade(jSky[mid], 66, 100);
    uint16_t cap  = jshade(jSky[mid], 86, 100);
    jmpBox(sx, sy - h, w, h, body);
    jmpBox(sx, sy - h, w, 5, cap);
    for(int yy = sy - h + 18; yy < sy; yy += 18) jmpBox(sx + 3, yy, w - 6, 1, cap);
  } else {                                        // JO_CHAIN
    int len = (int)(o.b * JB * 0.5f);
    uint16_t cc = jshade(jcGndLine, 78, 100);
    for(int k = 0; k < len; k += 11) jmpBox(sx + (int)(JB * 0.44f), sy - k - 9, 5, 8, cc);
  }
}
static void jmpSolidPass(int sx, int sy, const JmpObj& o){
  switch(o.t){
    case JO_BLOCK: {
      int w = (int)(o.a * JB * 0.5f), h = (int)(o.b * JB * 0.5f);
      jmpDrawBlock(sx, sy - h, w, h);
    } break;
    case JO_PLATE: {
      int w = (int)(o.a * JB * 0.5f), h = (int)(JB * 0.5f);
      jmpBoxEdge(sx, sy - h, w, h, jcObj, jcEdge, 3);
    } break;
    case JO_BULLET: {
      int w = (int)JB, h = (int)JB;
      jmpBoxEdge(sx, sy - h, w, h, jcObj, jcEdge, 3);
      jmpTriSide(sx - 1, sy - h / 2, (int)(JB * 0.45f), h, +1, jcEdge);
      jmpTriSide(sx - 4, sy - h / 2, (int)(JB * 0.45f) - 4, h - 8, +1, jcObj);
    } break;
    default: break;
  }
}
static void jmpHazardPass(int sx, int sy, const JmpObj& o){
  switch(o.t){
    case JO_SPIKE:  jmpDrawSpike(sx, sy, o.a, 0); break;
    case JO_SPIKED: jmpDrawSpike(sx, sy, o.a, 1); break;
    case JO_SPIKEL: case JO_SPIKER: {
      int n = o.b ? o.b : 1, d = (o.t == JO_SPIKEL) ? +1 : -1;
      for(int k = 0; k < n; k++){
        int cy = sy - (int)(k * JB * 0.5f) - (int)(JB * 0.25f);
        jmpTriSide(sx, cy, (int)(JB * 0.85f), (int)(JB * 0.5f), d, jcEdge);
        jmpTriSide(sx - 3 * d, cy, (int)(JB * 0.85f) - 5, (int)(JB * 0.5f) - 6, d, jcObj);
      }
    } break;
    default: break;
  }
}
static void jmpInteractPass(int sx, int sy, const JmpObj& o, float t){
  switch(o.t){
    case JO_ORB: {
      int cx = sx + (int)(JB * 0.5f), cy = sy;
      float pl = 0.82f + 0.18f * sinf(t * 5.0f + o.x * 0.05f);
      uint16_t core = o.a ? rgb565(120, 210, 255) : rgb565(255, 224, 88);
      uint16_t ring = o.a ? rgb565(60, 130, 220)  : rgb565(220, 150, 20);
      jmpDisc(cx, cy, (int)(15 * pl), ring);
      jmpDisc(cx, cy, (int)(9 * pl), core);
      jmpDisc(cx, cy, (int)(4 * pl), rgb565(255, 255, 255));
    } break;
    case JO_COIN: {
      if((JG.coinGot >> o.a) & 1) break;
      int cx = sx + (int)(JB * 0.5f), cy = sy;
      float ph = t * 3.0f + o.a * 2.1f;
      int rx = (int)(15 * fabsf(cosf(ph))) + 3;          // giro simulado por ancho
      jmpOval(cx, cy, rx + 3, 18, rgb565(150, 96, 8));
      jmpOval(cx, cy, rx, 15, rgb565(255, 196, 48));
      if(rx > 7) jmpOval(cx, cy, rx - 5, 10, rgb565(255, 232, 130));
    } break;
    case JO_PGRAV: case JO_PSHIP: case JO_PCUBE: {
      int cx = sx + (int)(JB * 0.5f), cy = sy;
      uint16_t c1, c2;
      if(o.t == JO_PGRAV){ c1 = o.a ? rgb565(90,200,255) : rgb565(255,214,40);
                           c2 = o.a ? rgb565(30,110,190) : rgb565(190,130,10); }
      else if(o.t == JO_PSHIP){ c1 = rgb565(255,120,225); c2 = rgb565(150,30,140); }
      else                    { c1 = rgb565(120,255,150); c2 = rgb565(20,150,60); }
      float sp = t * 2.6f + o.x * 0.01f;
      int rx1 = 12 + (int)(6 * fabsf(cosf(sp)));
      int rx2 = 12 + (int)(6 * fabsf(cosf(-sp * 1.35f)));
      jmpOval(cx, cy, 20, 40, jcObj);
      jmpOvalRing(cx, cy, rx1 + 8, 40, 4, c2);
      jmpOvalRing(cx, cy, rx2 + 3, 34, 4, c1);
      for(int k = 0; k < 4; k++){
        float f = t * 1.6f + k * 0.25f; f -= (int)f;
        int px_ = cx + (int)((1.0f - f) * 46.0f) + 8;
        int py_ = cy + (int)(sinf(k * 2.1f + t * 3.0f) * 26.0f * f);
        jmpBox(px_, py_, 4, 4, c1);
      }
    } break;
    default: break;
  }
}

#ifdef JMP_DEBUG_HITBOX
// Contorno hueco de 1 px (solo depuracion).
static void jmpOutline(int x, int y, int w, int h, uint16_t c){
  if(w <= 0 || h <= 0) return;
  jmpBox(x, y, w, 1, c); jmpBox(x, y + h - 1, w, 1, c);
  jmpBox(x, y, 1, h, c); jmpBox(x + w - 1, y, 1, h, c);
}
static void jmpDebugBoxes(float shx, float shy){
  const float camX = JG.camX + shx, camY = JG.camY;
  const uint16_t GRN = rgb565(0,255,0), RED = rgb565(255,0,0),
                 BLU = rgb565(60,120,255), YEL = rgb565(255,255,0);
  uint16_t qa = (uint16_t)(jclampf((camX - 2*JB) / (JB*0.25f), 0, 65000));
  uint16_t qb = (uint16_t)(jclampf((camX + JW + 2*JB) / (JB*0.25f), 0, 65000));
  for(int i = jObjLower(qa); i < JL_OBJ_N && JL_OBJ[i].x <= qb; i++){
    const JmpObj& o = JL_OBJ[i]; float x0,y0,x1,y1;
    if(o.t == JO_TOWER || o.t == JO_CHAIN){
      float ox = jQX2px(o.x), oy = jHY2px(o.y);
      float w = (o.t == JO_TOWER) ? o.a*(JB*0.5f) : 6.0f, h = o.b*(JB*0.5f);
      jmpOutline((int)(ox-camX), (int)(JGY-(oy+h-camY)+shy), (int)w, (int)h, BLU);
      continue;
    }
    if(jSolidBox(o, x0,y0,x1,y1))
      jmpOutline((int)(x0-camX), (int)(JGY-(y1-camY)+shy), (int)(x1-x0), (int)(y1-y0), GRN);
    if(jHazBox(o, x0,y0,x1,y1)){
      // Se dibujan las TRES bandas reales de cada triangulo, no la envolvente.
      float ox = jQX2px(o.x), oy = jHY2px(o.y);
      int n = (o.t==JO_SPIKE||o.t==JO_SPIKED) ? o.a : (o.b?o.b:1);
      for(int k = 0; k < n; k++) for(int q = 0; q < 3; q++){
        float f0=q/3.0f, f1=(q+1)/3.0f;
        bool vert = (o.t==JO_SPIKE||o.t==JO_SPIKED);
        float len = vert?JSPK_H:JSPKS_H, hw0 = vert?JSPK_HW:JSPKS_HW;
        float hw = hw0*(1.0f-f1); if(hw <= 0.5f) break;
        int dir = (o.t==JO_SPIKE||o.t==JO_SPIKER) ? +1 : -1;
        float cen = vert ? (ox + k*JB + JB*0.5f) : (oy + k*JB*0.5f + JB*0.25f);
        float base = vert ? oy : ox;
        float a0 = (dir>0)? base+len*f0 : base-len*f1;
        float a1 = (dir>0)? base+len*f1 : base-len*f0;
        if(vert) jmpOutline((int)(cen-hw-camX), (int)(JGY-(a1-camY)+shy), (int)(2*hw), (int)(a1-a0), RED);
        else     jmpOutline((int)(a0-camX), (int)(JGY-(cen+hw-camY)+shy), (int)(a1-a0), (int)(2*hw), RED);
      }
    }
  }
  // El jugador se dibuja en la X fija de pantalla, asi que su caja tambien.
  float bx0,by0,bx1,by1; jmpPlayerBox(bx0,by0,bx1,by1);
  jmpOutline((int)(JPX - (bx1-bx0)*0.5f), (int)(JGY-(by1-camY)+shy),
             (int)(bx1-bx0), (int)(by1-by0), YEL);
}
#endif

static void jmpDrawObjects(float shx, float shy, float t){
  const float camX = JG.camX + shx, camY = JG.camY;
  uint16_t qa = (uint16_t)(jclampf((camX - 2 * JB) / (JB * 0.25f), 0, 65000));
  uint16_t qb = (uint16_t)(jclampf((camX + JW + 2 * JB) / (JB * 0.25f), 0, 65000));
  const int i0 = jObjLower(qa);

  for(int pass = 0; pass < 4; pass++){
    for(int i = i0; i < JL_OBJ_N && JL_OBJ[i].x <= qb; i++){
      const JmpObj& o = JL_OBJ[i];
      uint8_t k = o.t;
      int want = (k == JO_TOWER || k == JO_CHAIN) ? 0
               : (k == JO_BLOCK || k == JO_PLATE || k == JO_BULLET) ? 1
               : (k == JO_SPIKE || k == JO_SPIKED || k == JO_SPIKEL || k == JO_SPIKER) ? 2 : 3;
      if(want != pass) continue;
      int sx = (int)(jQX2px(o.x) - camX);
      if(sx > JW + 2 * (int)JB || sx < -6 * (int)JB) continue;      // recorte inmediato
      int sy = (int)(JGY - (jHY2px(o.y) - camY) + shy);
      switch(pass){
        case 0: jmpDecorPass(sx, sy, o); break;
        case 1: jmpSolidPass(sx, sy, o); break;
        case 2: jmpHazardPass(sx, sy, o); break;
        default: jmpInteractPass(sx, sy, o, t); break;
      }
    }
    // Los pinchos continuos del pasillo de nave van con el resto de peligros.
    if(pass != 2) continue;
    for(int e = 0; e < JL_EDGE_N; e++){
      float ex0 = jQX2px(JL_EDGE[e].x0), ex1 = jQX2px(JL_EDGE[e].x1);
      if(ex1 < camX - JB || ex0 > camX + JW + JB) continue;
      int side = JL_EDGE[e].y;
      float from = ex0 > camX - JB ? ex0 : camX - JB;
      from = floorf(from / JB) * JB;
      for(float wx = from; wx < ex1 && wx < camX + JW + JB; wx += JB){
        int sx = (int)(wx - camX);
        if(side == 0){
          int sy = (int)(JGY - (jFloorAt(wx) - camY) + shy);
          jmpTriUp(sx, sy, (int)JB, (int)(JB * 0.55f), jcEdge);
          jmpTriUp(sx + 4, sy - 3, (int)JB - 8, (int)(JB * 0.55f) - 7, jcObj);
        } else {
          float ce = jCeilAt(wx); if(ce > 1e8f) continue;
          int sy = (int)(JGY - (ce - camY) + shy);
          jmpTriDown(sx, sy, (int)JB, (int)(JB * 0.55f), jcEdge);
          jmpTriDown(sx + 4, sy + 3, (int)JB - 8, (int)(JB * 0.55f) - 7, jcObj);
        }
      }
    }
  }
}

// #############################################################
// ##  JUGADOR Y PARTICULAS
// #############################################################
#define JCOL_P1  rgb565(88, 232, 64)     // verde del icono (el del video)
#define JCOL_P2  rgb565(26, 120, 24)
#define JCOL_PD  rgb565(10, 12, 16)

static void jmpDrawPlayer(float shx, float shy){
  if(JG.screen == JS_DEAD) return;                    // al morir el jugador se oculta
  float cx = JPX, cy = JGY - (JG.py - JG.camY) + shy - jmpBodyH() * 0.5f;
  if(JG.mode == JM_CUBE){
    float a = JG.rot * 0.017453f, ca = cosf(a), sa = sinf(a);
    float xs[4], ys[4];
    const float k[4][2] = { {-1,-1}, {1,-1}, {1,1}, {-1,1} };
    for(int s = 0; s < 3; s++){
      float r = (s == 0) ? JCUBE_DRAW * 0.5f : (s == 1 ? JCUBE_DRAW * 0.5f - 4.0f : JCUBE_DRAW * 0.185f);
      uint16_t c = (s == 0) ? JCOL_PD : (s == 1 ? JCOL_P1 : JCOL_PD);
      for(int i = 0; i < 4; i++){
        xs[i] = cx + (k[i][0] * r) * ca - (k[i][1] * r) * sa;
        ys[i] = cy + (k[i][0] * r) * sa + (k[i][1] * r) * ca;
      }
      jmpPoly(xs, ys, 4, c);
    }
    // pequeno realce interior
    for(int i = 0; i < 4; i++){
      float r = JCUBE_DRAW * 0.105f;
      xs[i] = cx + (k[i][0] * r) * ca - (k[i][1] * r) * sa;
      ys[i] = cy + (k[i][0] * r) * sa + (k[i][1] * r) * ca;
    }
    jmpPoly(xs, ys, 4, JCOL_P2);
  } else {
    float a = -JG.tilt * 0.017453f, ca = cosf(a), sa = sinf(a);
    const float body[6][2] = { {-21,-9}, {6,-12}, {21,-2}, {21,4}, {4,12}, {-21,10} };
    float xs[6], ys[6];
    for(int i = 0; i < 6; i++){
      xs[i] = cx + body[i][0] * ca - body[i][1] * JG.gdir * sa;
      ys[i] = cy + body[i][0] * sa + body[i][1] * JG.gdir * ca;
    }
    jmpPoly(xs, ys, 6, JCOL_PD);
    const float in[6][2] = { {-17,-6}, {5,-8}, {17,-1}, {17,2}, {3,8}, {-17,7} };
    for(int i = 0; i < 6; i++){
      xs[i] = cx + in[i][0] * ca - in[i][1] * JG.gdir * sa;
      ys[i] = cy + in[i][0] * sa + in[i][1] * JG.gdir * ca;
    }
    jmpPoly(xs, ys, 6, JCOL_P1);
    // cabina: el cubo dentro de la nave
    jmpBox((int)(cx - 8), (int)(cy - 8 * JG.gdir - (JG.gdir > 0 ? 9 : 0)), 16, 16, JCOL_PD);
    jmpBox((int)(cx - 5), (int)(cy - 8 * JG.gdir - (JG.gdir > 0 ? 6 : -3)), 10, 10, JCOL_P1);
  }
}
static void jmpDrawParts(float shx, float shy){
  // Todas las particulas viven en coordenadas de MUNDO: se quedan donde se
  // generaron y la camara las deja atras, como el resto del escenario.
  for(int i = 0; i < JPART_N; i++){
    const JmpPart& p = JG.part[i];
    if(!p.life) continue;
    int sx = (int)(p.x - JG.camX + shx);
    int sy = (int)(JGY - (p.y - JG.camY) + shy);
    if((unsigned)sx >= (unsigned)JW || (unsigned)sy >= (unsigned)JH) continue;
    int l0 = p.life0 ? p.life0 : 1;
    int s = p.size * p.life / l0; if(s < 1) s = 1;
    uint16_t c = mix565(jSky[sy], p.col, (uint8_t)(255 * p.life / l0));
    jmpBox(sx - s, sy - s, s * 2, s * 2, c);
  }
}

// #############################################################
// ##  HUD
// #############################################################
static void jmpDrawHud(){
  int pct = (int)(JG.px / JLEN * 100.0f);
  if(pct < 0) pct = 0; if(pct > 100) pct = 100;
  const int bx = 190, by = 16, bw = 400, bh = 20;
  jmpBoxEdge(bx, by, bw, bh, rgb565(20, 20, 28), rgb565(240, 240, 250), 2);
  int fw = (bw - 6) * pct / 100;
  if(fw > 0) jmpBox(bx + 3, by + 3, fw, bh - 6, rgb565(56, 236, 40));
  char t[8]; snprintf(t, sizeof(t), "%d%%", pct);
  drawText(bx + bw + 12, by + 2, t, 4, rgb565(255, 255, 255));
  // Boton de pausa/salida propio de la app (la app posee TODO el tactil).
  jmpBox(14, 12, 48, 30, rgb565(24, 26, 38));
  jmpBox(28, 18, 6, 18, rgb565(235, 238, 248));
  jmpBox(41, 18, 6, 18, rgb565(235, 238, 248));
  if(JG.practice){
    drawText(78, 16, "PRACTICA", 3, rgb565(120, 226, 255));
  }
}

// #############################################################
// ##  PUBLICACION DEL CUADRO
// ##  ----------------------------------------------------------
// ##  Un solo volcado por cuadro. En vez de copiar 768 KB de bbuf
// ##  a fb (lo que hace present() y lo que por si solo se comia el
// ##  presupuesto de 16.67 ms), se INTERCAMBIAN los dos lienzos
// ##  que el motor grafico ya tiene reservados y se vuelca el que
// ##  acaba de componerse. No se reserva ni un byte nuevo, no hay
// ##  cuadro publicado a medias y no hay tearing: la DMA2D lee un
// ##  buffer que ya nadie escribe.
// #############################################################
// #############################################################
// ##  PERFILADO OPCIONAL  (-DJMP_PROFILE)
// ##  ----------------------------------------------------------
// ##  Apagado por defecto: sin la macro no queda ni una linea de
// ##  codigo ni un byte de estado. Encendido, mide por separado
// ##  fisica, composicion del cuadro y volcado DMA2D, y una vez
// ##  por segundo imprime medias, maximo, FPS efectivo y cuantos
// ##  cuadros pasaron de los 16.67 ms de presupuesto.
// #############################################################
#ifdef JMP_PROFILE
static struct {
  uint32_t phys, draw, flush, worst, n, late, lastMs;
} jProf = { 0, 0, 0, 0, 0, 0, 0 };
#define JP_T0(v)      uint32_t v = micros()
#define JP_ADD(f, v)  jProf.f += (uint32_t)(micros() - (v))
static void jmpProfReport(uint32_t frameUs){
  jProf.n++;
  if(frameUs > jProf.worst) jProf.worst = frameUs;
  if(frameUs > 16667u) jProf.late++;
  uint32_t now = millis();
  if(!jProf.lastMs){ jProf.lastMs = now; return; }
  if(now - jProf.lastMs < 1000u || !jProf.n) return;
  uint32_t tot = jProf.phys + jProf.draw + jProf.flush;
  Serial.printf("[JMP] %s  fisica %lu us | cuadro %lu us | volcado %lu us | total %lu us"
                " | maximo %lu us | %lu fps | fuera de plazo %lu/%lu\n",
                JG.screen == JS_PLAY ? "juego " :
                JG.screen == JS_INTRO ? "intro " :
                JG.screen == JS_SELECT ? "menu  " :
                JG.screen == JS_DONE ? "final " : "otro  ",
                (unsigned long)(jProf.phys  / jProf.n),
                (unsigned long)(jProf.draw  / jProf.n),
                (unsigned long)(jProf.flush / jProf.n),
                (unsigned long)(tot / jProf.n),
                (unsigned long)jProf.worst,
                (unsigned long)(tot ? (1000000ULL * jProf.n / tot) : 0),
                (unsigned long)jProf.late, (unsigned long)jProf.n);
  jProf.phys = jProf.draw = jProf.flush = jProf.worst = jProf.n = jProf.late = 0;
  jProf.lastMs = now;
}
#else
#define JP_T0(v)      ((void)0)
#define JP_ADD(f, v)  ((void)0)
#endif

static uint32_t jNextFrameUs = 0;                     // cadencia de presentacion
// El panel refresca a 60 Hz: presentar mas a menudo no se ve y si cuesta.
// La fisica NO pasa por aqui -- avanza con el reloj en cada vuelta del loop.
static inline bool jmpFrameDue(uint32_t now){
  if((int32_t)(now - jNextFrameUs) < 0) return false;
  jNextFrameUs += 16667u;                             // 60 Hz exactos
  // Tras un paron (cortina, suspension, bloqueo) no se acumula deuda de
  // cuadros: se resincroniza al reloj en vez de disparar una rafaga.
  if((int32_t)(now - jNextFrameUs) > 0) jNextFrameUs = now + 16667u;
  return true;
}
static void jmpPresent(){
  if(gRtTarget){ present(0, SCR_H - 1); return; }     // hospedada en una ventana: ruta normal
  JP_T0(t0);
  uint16_t* tmp = fb; fb = bbuf; bbuf = tmp;
  setBuf(bbuf);
  flxFlush(0, SCR_H - 1);
  JP_ADD(flush, t0);
}
static void jmpBeginFrame(){
  gLand = true;
  setBuf(bbuf);
  gClipX0 = 0; gClipX1 = SCR_W - 1;
  gClipY0 = 0; gClipY1 = SCR_H - 1;
}

// #############################################################
// ##  ARRANQUE DE UN INTENTO
// #############################################################
// #############################################################
// ##  REAPARICION
// ##  ----------------------------------------------------------
// ##  El estado de un punto de control se RECONSTRUYE, no se copia
// ##  a medias: la forma y la gravedad son las guardadas, pero la
// ##  posicion se encaja en la superficie que le toca y la
// ##  velocidad vertical se pone a cero. Asi reaparecer es siempre
// ##  la misma situacion, se haya guardado el punto en el aire o
// ##  en el suelo, y el auditor puede comprobarla una a una.
// #############################################################
static void jmpPlaceAt(float wx, uint8_t mode, int8_t gdir){
  float fl = jFloorAt(wx), ce = jCeilAt(wx);
  JG.mode = mode; JG.gdir = gdir; JG.vy = 0; JG.rot = 0; JG.tilt = 0;
  float bh = jmpBodyH();
  if(mode == JM_SHIP){
    JG.py = (ce < 1e8f) ? (fl + (ce - fl) * 0.5f - bh * 0.5f) : (fl + 4.0f * JB);
  } else if(gdir > 0){
    JG.py = fl;
  } else {
    JG.py = (ce < 1e8f) ? (ce - bh) : fl;
  }
}
static bool jmpSpawnGrounded(){
  if(JG.mode == JM_SHIP) return false;
  float fl = jFloorAt(JG.px), ce = jCeilAt(JG.px);
  if(JG.gdir > 0) return JG.py <= fl + 1.0f;
  return (ce < 1e8f) && (JG.py + jmpBodyH() >= ce - 1.0f);
}

static void jmpResetRun(bool keepCheckpoint){
  JG.mode = JM_CUBE; JG.gdir = +1; JG.vy = 0; JG.rot = 0; JG.tilt = 0;
  JG.onGround = true; JG.coinGot = 0; JG.flash = 0; JG.shake = 0;
  // Estado tactil a cero: sin esto, un flanco o una ventana de salto que
  // quedara viva del intento anterior producia un SALTO FANTASMA en el
  // primer paso del intento nuevo.
  jmpClearInput();
  JG.acc = 0;
  jmpPartClear();
  if(keepCheckpoint && JG.practice && JG.chk >= 0){
    JG.px = jQX2px(JL_CHK[JG.chk]);
    jmpPlaceAt(JG.px, JG.chkMode, JG.chkG);
    JG.camY = JG.chkCamY;
  } else {
    JG.px = 0; JG.py = 0; JG.camY = 0; JG.chk = -1;
    JG.runTime = 0;
  }
  JG.camX = JG.px - JPX;
  jmpApplyBg(JG.px);
  JG.onGround = jmpSpawnGrounded();       // ver mas abajo: reaparicion segura
  JG.attempt++; JG.attemptsTotal++;   // el contador se persiste al salir, no por intento
}

// #############################################################
// ##  BUCLE DE JUEGO
// #############################################################
static void jmpPlayFrame(float dt, bool draw){
  const bool alive = (JG.screen == JS_PLAY);

  JP_T0(tFrame);
  // --- fisica de paso fijo (el reloj manda, no el numero de cuadros) ---
  JP_T0(tPhys);
  if(alive){
    JG.acc += dt;
    int steps = 0;
    while(JG.acc >= JSTEP && steps < JMAXSTEP){
      jmpStep(JSTEP);
      JG.acc -= JSTEP; steps++;
      if(JG.screen != JS_PLAY) break;
    }
    if(JG.acc > JSTEP * JMAXSTEP) JG.acc = 0;          // tope: nada de espiral de la muerte
    // Punto de control del modo practica.
    if(JG.practice && JG.screen == JS_PLAY){
      uint16_t q = (uint16_t)(JG.px / (JB * 0.25f));
      while(JG.chk + 1 < JL_CHK_N && JL_CHK[JG.chk + 1] <= q){
        JG.chk++;
        JG.chkPy = JG.py; JG.chkCamY = JG.camY; JG.chkG = JG.gdir; JG.chkMode = JG.mode;
      }
    }
    // Estela / propulsor.
    static float trail = 0;
    trail += dt;
    if(trail > 0.045f){
      trail = 0;
      float wy = JG.py + jmpBodyH() * 0.5f;
      if(JG.mode == JM_SHIP)
        jmpSpawnParts(JG.px - 22.0f, wy, 1, rgb565(255, 210, 120), 1, 40.0f);
      else if(!JG.onGround)
        jmpSpawnParts(JG.px - 16.0f, wy - jmpBodyH() * 0.45f * JG.gdir, 1, rgb565(230, 240, 255), 0, 30.0f);
    }
  }
  jmpPartTick(dt);
  if(JG.flash > 0) JG.flash -= dt * 3.6f;
  if(JG.shake > 0) JG.shake -= dt * 2.6f;
  JP_ADD(phys, tPhys);

  // --- muerte: espera corta SIN bloquear el loop y reinicio ---
  if(JG.screen == JS_DEAD){
    JG.deadT += dt;
    if(JG.deadT > 0.62f){ JG.screen = JS_PLAY; JG.acc = 0; jmpResetRun(true); }
  }

  // --- dibujo (se omite si el cuadro no toca todavia; la fisica ya avanzo) ---
  if(!draw) return;
  JP_T0(tDraw);
  float shx = 0, shy = 0;
  if(JG.shake > 0){
    float s = JG.shake * 14.0f;
    shx = (jrnd01((uint32_t)(JG.runTime * 733.0f)) - 0.5f) * s;
    shy = (jrnd01((uint32_t)(JG.runTime * 977.0f) + 31u) - 0.5f) * s;
  }
  jmpBeginFrame();
  jmpBuildPalette(JG.runTime);
  if(JG.screen == JS_DEAD)                // oscurecido de la muerte, en la paleta
    jmpDimPalette(100 - (int)(jclampf(JG.deadT * 1.6f, 0, 1) * 32.0f), 100);
  else if(JG.flash > 0)                   // destello de portal: se aclara la paleta
    jmpDimPalette(100 + (int)(jclampf(JG.flash, 0, 1) * 55.0f), 100);
  jmpPaintWorld(shx, shy);
  jmpDrawObjects(shx, shy, JG.runTime);
  jmpDrawPlayer(shx, shy);
  jmpDrawParts(shx, shy);
#ifdef JMP_DEBUG_HITBOX
  jmpDebugBoxes(shx, shy);      // verde solido · rojo peligro · azul decoracion · amarillo jugador
#endif

  // Rotulo "ATTEMPT n" anclado al mundo, como en el juego original.
  if(JG.px < 9.0f * JB + 380.0f){
    char t[20]; snprintf(t, sizeof(t), "ATTEMPT %u", (unsigned)JG.attempt);
    int ax = (int)(6.0f * JB - JG.camX);
    if(ax > -260 && ax < JW) jmpTextEdge4(ax, 140, t, 6, rgb565(255,255,255), rgb565(18,18,28), 3);
  }
  if(JG.screen == JS_DEAD){
    // onda expansiva + oscurecido corto
    int r = (int)(JG.deadT * 900.0f);
    uint16_t c = rgb565(255, 255, 255);
    float sy = JGY - (JG.py - JG.camY) - jmpBodyH() * 0.5f;
    for(int k = 0; k < 3; k++){
      int rr = r - k * 16; if(rr <= 0) continue;
      uint8_t a = (uint8_t)(jclampf(1.0f - JG.deadT * 1.7f, 0, 1) * (200 - k * 60));
      if(a) drawCircle((int)JPX, (int)sy, rr, mix565(jSky[(int)jclampf(sy,0,JH-1)], c, a));
    }
  }
  jmpDrawHud();
  JP_ADD(draw, tDraw);
  jmpPresent();
#ifdef JMP_PROFILE
  jmpProfReport((uint32_t)(micros() - tFrame));
#endif
}

// #############################################################
// ##  8. INTRODUCCION ANIMADA
// ##  ----------------------------------------------------------
// ##  Reconstruye la lamina de portada: fondo azul profundo con
// ##  paneles geometricos en parallax, haces de luz, rotulo
// ##  GEOMETRY DASH, cubo rosa que salta y activa un orbe, pincho
// ##  colgante con cadena, cubo verde, nave verde y portal ovalado.
// ##  TODO procedural: ni un byte de imagen en el firmware.
// ##  Cada animacion depende del TIEMPO transcurrido, nunca de
// ##  "sumar una cantidad por cuadro".
// #############################################################
#define JINTRO_T   2.65f

static float jIZ = 1.0f, jIDX = 0, jIDY = 0;
static uint8_t jIA = 255;                     // atenuacion global del cuadro de intro
static inline uint16_t IC(uint16_t c){ return jIA >= 255 ? c : mix565(0, c, jIA); }
static bool jIntroCap = false;                // el atlas del rotulo esta listo
#define JINTRO_GAP 30
static inline int IX(float v){ return (int)(400.0f + (v - 400.0f) * jIZ + jIDX); }
static inline int IY(float v){ return (int)(240.0f + (v - 240.0f) * jIZ + jIDY); }
static inline int IS(float v){ return (int)(v * jIZ); }

static void jmpIntroFrame(float t){
  jmpBeginFrame();
  const float fade = jclampf(t / 0.42f, 0, 1);
  const float out  = jclampf((t - 2.28f) / 0.37f, 0, 1);
  // Atenuacion GLOBAL del cuadro. Antes la entrada y la salida se hacian con
  // dos rellenos alfa a pantalla completa (384.000 mezclas por pixel, 0.9 ms
  // medidos por fundido). Ahora se aplica a los ~30 colores del cuadro, una
  // vez cada uno, y el fondo ya sale premezclado de su tabla.
  jIA = (uint8_t)(fade * (1.0f - out) * 255.0f);
  jIZ  = 1.0f + jeaseIn(jclampf((t - 2.20f) / 0.45f, 0, 1)) * 0.30f;
  jIDX = 0; jIDY = 0;

  const int gy = 384;
  const int gyS = IY(gy);
  // --- tablas por scanline: degradado + suelo + las dos capas de panel ---
  {
    uint16_t bTop = rgb565(6, 16, 74), bMid = rgb565(14, 44, 140), bBot = rgb565(5, 12, 58);
    for(int y = 0; y < JH; y++){
      uint16_t c;
      if(y >= gyS)            c = (y < gyS + 3) ? rgb565(120, 200, 255) : rgb565(6, 18, 72);
      else if(y < 250)        c = mix565(bTop, bMid, (uint8_t)(y * 255 / 250));
      else                    c = mix565(bMid, bBot, (uint8_t)((y - 250) * 255 / (JH - 250)));
      c = IC(c);
      jSky[y]    = c;
      jPan[0][y] = mix565(c, IC(rgb565(12, 36, 118)), 150);
      jPan[1][y] = mix565(c, IC(rgb565(22, 62, 172)), 190);
      jTpl[0][JH - 1 - y] = c;                       // ya invertida para el memcpy
    }
  }
  for(int lx = 0; lx < JW; lx++) memcpy(gBuf + (size_t)lx * SCR_W, jTpl[0], SCR_W * 2);

  // --- paneles geometricos con parallax (dos ritmos), ya premezclados ---
  for(int lay = 0; lay < 2; lay++){
    float sp = 16.0f + lay * 30.0f;
    int   pw = lay ? 132 : 92, ph = lay ? 104 : 74;
    int   gx = 30, gyy = 26;
    int per = pw + gx, off = (int)(t * sp) % per;
    for(int i = -1; i < JW / per + 2; i++){
      for(int j = 0; j < JH / (ph + gyy) + 2; j++){
        uint32_t h = jhash((uint32_t)(i * 131 + j * 977 + lay * 7717));
        if((h & 7) < 3) continue;                    // huecos: no es un enrejado macizo
        int x = i * per - off + lay * 46;
        int y = -30 + j * (ph + gyy) + (lay ? 18 : 0);
        jmpBoxLut(IX(x), IY(y), IS(pw), IS(ph), jPan[lay]);
      }
    }
  }
  // --- haces de luz azules transparentes (unica capa que sigue con alfa) ---
  for(int k = 0; k < 5; k++){
    float ph = t * 0.7f + k * 1.25f;
    int x = 50 + k * 160 + (int)(sinf(ph) * 26.0f);
    int w = 30 + k * 9;
    fillRectA(IX(x), IY(0), IS(w), IS(JH), IC(rgb565(110, 190, 255)), (uint8_t)(30 + 18 * sinf(ph * 1.7f)));
    fillRectA(IX(x + w / 3), IY(0), IS(w / 3), IS(JH), IC(rgb565(170, 220, 255)), (uint8_t)(20 + 10 * cosf(ph)));
  }

  // --- particulas del suelo (el suelo en si ya viene en la tabla) ---
  for(int k = 0; k < 16; k++){
    float f = fmodf(t * 0.55f + k * 0.0625f, 1.0f);
    int x = (int)((1.0f - f) * (JW + 60)) - 30;
    int y = gy - 6 - (int)(sinf(k * 2.3f + t * 2.0f) * 10.0f);
    jmpBox(IX(x), IY(y), IS(4), IS(4), IC(rgb565(150, 220, 255)));
  }

  // --- rotulo GEOMETRY DASH: entra desde arriba con rebote elastico ---
  // Sale del atlas de 1 bit: dos volcados planos por letra en vez de diez
  // pasadas de la fuente vectorial. La animacion letra a letra se conserva
  // porque cada glifo vive en su propia celda.
  if(jIntroCap){
    int total = jmpCapTotal() + JINTRO_GAP;
    int px_ = 400 - total / 2;
    for(int i = 0; i < 12; i++){
      if(i == 8) px_ += JINTRO_GAP;                 // separacion entre las dos palabras
      float la = jclampf((t - 0.16f - i * 0.030f) / 0.66f, 0, 1);
      float ly = -110.0f + jeaseBack(la) * 156.0f;
      jmpCapCell(i, IX(px_), IY(ly), IC(rgb565(178, 246, 96)), IC(rgb565(14, 46, 14)));
      px_ += jmpCapCellW(i);
    }
  }

  // --- pinchos blancos sobre el suelo ---
  for(int k = 0; k < 3; k++){
    int x = 258 + k * 50;
    jmpTriUp(IX(x), IY(gy), IS(44), IS(50), IC(rgb565(255, 255, 255)));
    jmpTriUp(IX(x + 5), IY(gy - 4), IS(34), IS(40), IC(rgb565(8, 10, 20)));
  }
  // --- bloque negro con borde blanco ---
  jmpBoxEdge(IX(400), IY(gy - 60), IS(60), IS(60), IC(rgb565(8, 10, 20)), IC(rgb565(255, 255, 255)), IS(4));

  // --- obstaculo colgante con cadena (baja un poco y la cadena oscila) ---
  {
    float drop = jeaseOut(jclampf((t - 0.55f) / 0.9f, 0, 1)) * 16.0f;
    float sw = sinf(t * 3.1f) * 5.0f * jclampf((t - 0.6f) / 0.5f, 0, 1);
    int cx = 512;
    for(int k = 0; k < 7; k++){
      int yy = 40 + k * 13 + (int)drop;
      fillRect(IX(cx + (int)(sw * k / 7.0f) - 3), IY(yy), IS(7), IS(9), IC(rgb565(120, 190, 255)));
    }
    int bx = cx + (int)sw - 30, by = 118 + (int)drop;
    jmpBoxEdge(IX(bx), IY(by), IS(44), IS(34), IC(rgb565(8, 10, 20)), IC(rgb565(255, 255, 255)), IS(3));
    jmpTriDown(IX(bx + 4), IY(by + 34), IS(36), IS(26), IC(rgb565(255, 255, 255)));
    jmpTriDown(IX(bx + 8), IY(by + 34), IS(28), IS(19), IC(rgb565(8, 10, 20)));
  }

  // --- orbe amarillo pulsante (lo activa el cubo rosa) ---
  float orbT = 0.92f;
  {
    int ox = 196, oy = gy - 120;
    float pl = 1.0f + 0.16f * sinf(t * 7.0f);
    jmpDisc(IX(ox), IY(oy), IS(17 * pl), IC(rgb565(210, 150, 20)));
    jmpDisc(IX(ox), IY(oy), IS(11 * pl), IC(rgb565(255, 226, 90)));
    jmpDisc(IX(ox), IY(oy), IS(5 * pl),  IC(rgb565(255, 255, 255)));
    // dos ondas circulares + destello al activarse
    for(int k = 0; k < 2; k++){
      float a = (t - orbT - k * 0.16f) / 0.55f;
      if(a > 0 && a < 1){
        int r = (int)(24 + a * 104);
        uint8_t al = (uint8_t)((1 - a) * 200);
        drawCircle(IX(ox), IY(oy), IS(r), mix565(IC(rgb565(14,44,140)), IC(rgb565(255,240,150)), al));
        drawCircle(IX(ox), IY(oy), IS(r) - 1, mix565(IC(rgb565(14,44,140)), IC(rgb565(255,240,150)), (uint8_t)(al * 3 / 4)));
      }
    }
    // El destello se aplica a la tabla del fondo (gratis), no con un relleno
    // alfa a pantalla completa.
  }

  // --- cubo rosa: entra por la izquierda, salta y gira en multiplos de 90 ---
  {
    float a = jclampf((t - 0.30f) / 1.35f, 0, 1);
    float x = -80.0f + a * 300.0f;
    float jt = jclampf((t - 0.52f) / 0.86f, 0, 1);
    float y = gy - 30.0f - sinf(jt * 3.14159f) * 150.0f;
    float rot = jt * 180.0f;
    float ca = cosf(rot * 0.017453f), sa = sinf(rot * 0.017453f);
    float xs[4], ys[4]; const float k[4][2] = { {-1,-1}, {1,-1}, {1,1}, {-1,1} };
    for(int s = 0; s < 2; s++){
      float r = s ? 25.0f : 31.0f;
      uint16_t c = s ? IC(rgb565(255, 96, 190)) : IC(rgb565(20, 8, 26));
      for(int i = 0; i < 4; i++){
        xs[i] = IX(x + (k[i][0] * r) * ca - (k[i][1] * r) * sa);
        ys[i] = IY(y + (k[i][0] * r) * sa + (k[i][1] * r) * ca);
      }
      jmpPoly(xs, ys, 4, c);
    }
  }

  // --- cubo verde central: aterriza con una compresion minima ---
  {
    float a = jclampf((t - 0.62f) / 0.55f, 0, 1);
    float y = -60.0f + jeaseOut(a) * (gy - 34.0f + 60.0f);
    float sq = 1.0f;
    if(a >= 1.0f){ float b = jclampf((t - 1.17f) / 0.20f, 0, 1); sq = 1.0f - sinf(b * 3.14159f) * 0.16f; }
    int w = IS(62 * (2.0f - sq)), h = IS(62 * sq);
    jmpBoxEdge(IX(478) - w / 2, IY(y) - h / 2, w, h, IC(rgb565(20, 40, 20)), IC(rgb565(120, 240, 70)), IS(5));
    jmpBox(IX(478) - w / 6, IY(y) - h / 6, w / 3, h / 3, IC(rgb565(20, 40, 20)));
  }

  // --- nave verde: entra por la derecha, se inclina segun su trayectoria ---
  {
    float a = jclampf((t - 1.05f) / 1.10f, 0, 1);
    float x = 920.0f - a * 300.0f;
    float y = 214.0f + sinf(a * 3.6f) * 54.0f;
    float dy = cosf(a * 3.6f) * 46.0f * 3.6f;
    float tl = jclampf(-dy / 240.0f, -1, 1) * 22.0f;
    float ca = cosf(tl * 0.017453f), sa = sinf(tl * 0.017453f);
    const float body[6][2] = { {-24,-10}, {7,-14}, {24,-2}, {24,5}, {5,14}, {-24,12} };
    float xs[6], ys[6];
    for(int s = 0; s < 2; s++){
      float k = s ? 1.20f : 1.45f;
      uint16_t c = s ? IC(rgb565(120, 240, 70)) : IC(rgb565(16, 34, 18));
      for(int i = 0; i < 6; i++){
        xs[i] = IX(x + body[i][0] * k * ca - body[i][1] * k * sa);
        ys[i] = IY(y + body[i][0] * k * sa + body[i][1] * k * ca);
      }
      jmpPoly(xs, ys, 6, c);
    }
    // propulsor con particulas acotadas
    if(a > 0.02f) for(int k = 0; k < 6; k++){
      float f = fmodf(t * 2.2f + k * 0.166f, 1.0f);
      int px_ = (int)(x + 26 + f * 46), py_ = (int)(y + sinf(k * 1.9f) * 5.0f);
      fillRectA(IX(px_), IY(py_), IS(5), IS(5), IC(rgb565(255, 200, 90)), (uint8_t)((1 - f) * 220));
    }
  }

  // --- portal ovalado rosa/azul con dos aros en sentidos opuestos ---
  {
    int px_ = 674, py_ = gy - 104;
    float s1 = t * 2.4f, s2 = -t * 3.1f;
    int r1 = 18 + (int)(12 * fabsf(cosf(s1))), r2 = 15 + (int)(11 * fabsf(cosf(s2)));
    jmpOval(IX(px_), IY(py_), IS(34), IS(76), IC(rgb565(10, 12, 30)));
    jmpOvalRing(IX(px_), IY(py_), IS(r1 + 14), IS(76), IS(6), IC(rgb565(255, 90, 190)));
    jmpOvalRing(IX(px_), IY(py_), IS(r2 + 6),  IS(63), IS(6), IC(rgb565(90, 180, 255)));
    for(int k = 0; k < 6; k++){
      float f = fmodf(t * 1.5f + k * 0.166f, 1.0f);
      int qx = (int)(px_ + (1.0f - f) * 90.0f + 14.0f);
      int qy = (int)(py_ + sinf(k * 2.0f + t * 2.6f) * 40.0f * f);
      fillRectA(IX(qx), IY(qy), IS(5), IS(5), IC(rgb565(255, 150, 220)), (uint8_t)(f * 220));
    }
    // pinchos junto al portal
    for(int k = 0; k < 3; k++){
      jmpTriUp(IX(px_ - 52 + k * 44), IY(gy), IS(40), IS(46), IC(rgb565(255, 255, 255)));
      jmpTriUp(IX(px_ - 47 + k * 44), IY(gy - 4), IS(30), IS(36), IC(rgb565(8, 10, 20)));
    }
  }

  // La transicion al selector ya va dentro de jIA: el cuadro entero se
  // apaga sin una sola mezcla alfa de pantalla completa.
  jmpPresent();
}

// #############################################################
// ##  9. SELECTOR DE NIVEL
// ##  Reproduce la pantalla de seleccion de Jumper adaptada a
// ##  800x480 (sin deformarla). Solo existe Jumper: las flechas
// ##  laterales rebotan y se quedan, no llevan a niveles falsos.
// #############################################################
static void jmpStar(int cx, int cy, int r, uint16_t c){
  // Estrella de 5 puntas rasterizada por columna (poligono convexo por partes:
  // se dibuja como dos triangulos y un pentagono aproximado -- barato y nitido).
  float xs[10], ys[10];
  for(int i = 0; i < 10; i++){
    float a = -1.5708f + i * 0.62832f;
    float rr = (i & 1) ? r * 0.44f : r;
    xs[i] = cx + cosf(a) * rr; ys[i] = cy + sinf(a) * rr;
  }
  // Rasterizado por columna con paridad (la estrella no es convexa).
  int x0 = cx - r, x1 = cx + r;
  for(int lx = x0; lx <= x1; lx++){
    if((unsigned)lx >= (unsigned)JW) continue;
    float cxp = lx + 0.5f; float ys2[10]; int n = 0;
    for(int i = 0; i < 10; i++){
      int j = (i + 1) % 10;
      float ax = xs[i], ay = ys[i], bx = xs[j], by = ys[j];
      if((ax <= cxp && bx > cxp) || (bx <= cxp && ax > cxp)){
        float t = (cxp - ax) / (bx - ax);
        if(n < 10) ys2[n++] = ay + (by - ay) * t;
      }
    }
    for(int a = 0; a < n - 1; a++) for(int b = a + 1; b < n; b++)
      if(ys2[b] < ys2[a]){ float tmp = ys2[a]; ys2[a] = ys2[b]; ys2[b] = tmp; }
    for(int a = 0; a + 1 < n; a += 2){
      int ya = (int)(ys2[a] + 0.5f), yb = (int)(ys2[a + 1] + 0.5f);
      if(yb > ya) fillSpanLand(lx, ya, yb - ya, c);
    }
  }
}
static void jmpCoinIcon(int cx, int cy, int r, bool got, float t){
  if(got){
    jmpDisc(cx, cy, r, rgb565(150, 96, 8));
    jmpDisc(cx, cy, r - 3, rgb565(255, 196, 48));
    jmpStar(cx, cy, r - 7, rgb565(190, 120, 10));
    float g = 0.5f + 0.5f * sinf(t * 3.2f + cx * 0.05f);
    jmpDisc(cx - r / 3, cy - r / 3, 2 + (int)(2 * g), rgb565(255, 250, 210));
  } else {
    jmpDisc(cx, cy, r, rgb565(28, 62, 30));
    jmpDisc(cx, cy, r - 3, rgb565(44, 86, 46));
    jmpStar(cx, cy, r - 7, rgb565(28, 62, 30));
  }
}
static void jmpFace(int cx, int cy, int r, float t){
  float pl = 1.0f + 0.045f * sinf(t * 2.6f);
  jmpDisc(cx, cy, (int)(r * pl) + 3, rgb565(120, 20, 10));
  jmpDisc(cx, cy, (int)(r * pl),     rgb565(238, 62, 34));
  jmpDisc(cx, cy - r / 3, (int)(r * pl * 0.62f), rgb565(250, 96, 60));
  // Cejas inclinadas hacia dentro, ojos y boca: la cara de dificultad "Harder".
  uint16_t ink = rgb565(52, 6, 4);
  for(int i = 0; i < 14; i++){
    jmpBox(cx - r / 2 - 3 + i, cy - r / 3 - 5 + i / 2, 2, 5, ink);
    jmpBox(cx + r / 2 - 9 + i, cy - r / 3 + 1 - i / 2, 2, 5, ink);
  }
  jmpBox(cx - r / 2 + 1, cy - r / 8, 8, 10, ink);
  jmpBox(cx + r / 2 - 8, cy - r / 8, 8, 10, ink);
  jmpBox(cx - r / 3, cy + r / 2 - 4, (2 * r) / 3, 5, ink);
}
static void jmpBar(int x, int y, int w, int h, float frac, uint16_t fill, uint16_t dark, const char* label){
  fillRoundRect(x, y, w, h, h / 2, rgb565(10, 62, 12));
  int fw = (int)((w - 8) * jclampf(frac, 0, 1));
  if(fw > h) fillRoundRect(x + 4, y + 4, fw, h - 8, (h - 8) / 2, fill);
  else if(fw > 0) fillRoundRect(x + 4, y + 4, fw, h - 8, fw / 2, fill);
  drawRoundRect(x, y, w, h, h / 2, dark);
  drawTextC(x + w / 2, y + h / 2 - 12, label, 5, rgb565(255, 255, 255));
}
static void jmpSelectFrame(float t){
  jmpBeginFrame();
  bool cap = jmpCapBuild("JUMPER", 9, 4);   // no-op salvo la primera entrada
  float in = jeaseOut(jclampf(t / 0.55f, 0, 1));
  float bounce = jeaseBack(jclampf((t - 0.10f) / 0.60f, 0, 1));

  // --- fondo verde brillante: una plantilla de columna y 800 memcpy ---
  {
    uint8_t a = (uint8_t)(in * 255.0f);
    for(int y = 0; y < JH; y++){
      uint16_t c = mix565(rgb565(46, 214, 46), rgb565(10, 140, 18), (uint8_t)(y * 255 / JH));
      jTpl[0][JH - 1 - y] = (a >= 255) ? c : mix565(0, c, a);
    }
    for(int lx = 0; lx < JW; lx++) memcpy(gBuf + (size_t)lx * SCR_W, jTpl[0], SCR_W * 2);
  }
  // --- decoraciones geometricas de los bordes ---
  const int dec[10][5] = {
    { 150,   0, 190,  38, 0 }, { 350,   0, 120,  56, 1 }, { 470,   0, 210,  30, 0 },
    {   0, 430, 110,  50, 1 }, { 120, 452, 150,  28, 0 }, { 640, 440, 160,  40, 1 },
    { 700, 405,  60,  32, 0 }, {  40,   0,  80,  26, 1 }, { 690,   0, 110,  44, 0 },
    { 300, 452,  90,  28, 1 },
  };
  for(int i = 0; i < 10; i++){
    uint16_t c = dec[i][4] ? rgb565(120, 236, 120) : rgb565(96, 216, 240);
    fillRect(dec[i][0], dec[i][1], dec[i][2], dec[i][3], c);
    fillRect(dec[i][0], dec[i][1] + dec[i][3] - 4, dec[i][2], 4, rgb565(24, 130, 30));
  }

  // --- boton volver (arriba izquierda) ---
  {
    float xs[3] = { 22, 74, 74 }, ys[3] = { 40, 12, 68 };
    jmpPoly(xs, ys, 3, rgb565(16, 110, 22));
    float xs2[3] = { 28, 70, 70 }, ys2[3] = { 40, 18, 62 };
    jmpPoly(xs2, ys2, 3, rgb565(150, 240, 120));
  }
  // --- boton de informacion (arriba derecha) ---
  jmpDisc(760, 38, 24, rgb565(12, 76, 150));
  jmpDisc(760, 38, 20, rgb565(80, 190, 250));
  jmpBox(757, 26, 6, 6, rgb565(255, 255, 255));
  jmpBox(757, 36, 6, 16, rgb565(255, 255, 255));

  // --- flechas laterales (rebotan y se quedan: solo existe Jumper) ---
  {
    int lo = (int)(JG.arrowL * 14.0f), ro = (int)(JG.arrowR * 14.0f);
    float xs[3] = { (float)(26 + lo), (float)(74 + lo), (float)(74 + lo) }, ys[3] = { 240, 198, 282 };
    jmpPoly(xs, ys, 3, rgb565(255, 255, 255));
    float xs2[3] = { (float)(774 - ro), (float)(726 - ro), (float)(726 - ro) }, ys2[3] = { 240, 198, 282 };
    jmpPoly(xs2, ys2, 3, rgb565(255, 255, 255));
  }

  // --- tarjeta central ---
  int cw = (int)(486 * (0.90f + 0.10f * bounce)), ch = (int)(154 * (0.90f + 0.10f * bounce));
  int cx = 400 - cw / 2, cy = 146 - ch / 2 + (int)((1.0f - bounce) * -26.0f);
  fillRoundRect(cx, cy, cw, ch, 16, rgb565(12, 92, 16));
  drawRoundRect(cx, cy, cw, ch, 16, rgb565(8, 60, 12));
  jmpFace(cx + 82, cy + ch / 2 - 6, 34, t);
  if(cap){
    int px_ = cx + 258 - jmpCapTotal() / 2;
    for(int i = 0; i < 6; i++){
      jmpCapCell(i, px_, cy + ch / 2 - 42, rgb565(255, 255, 255), rgb565(18, 52, 20));
      px_ += jmpCapCellW(i);
    }
  } else jmpTextFat(cx + 258, cy + ch / 2 - 38, "JUMPER", 9,
                    rgb565(255, 255, 255), rgb565(18, 52, 20), rgb565(0, 40, 0), 4);
  // dificultad: 7 estrellas
  {
    char n[6]; snprintf(n, sizeof(n), "%d", 7);
    drawTextR(cx + cw - 44, cy + 12, n, 4, rgb565(255, 224, 70));
    float pl = 1.0f + 0.07f * sinf(t * 3.0f);
    jmpStar(cx + cw - 26, cy + 26, (int)(13 * pl), rgb565(255, 208, 40));
  }
  // tres monedas, apareciendo escalonadas
  for(int i = 0; i < 3; i++){
    float a = jclampf((t - 0.45f - i * 0.13f) / 0.30f, 0, 1);
    if(a <= 0) continue;
    int r = (int)(18 * jeaseBack(a));
    jmpCoinIcon(cx + cw - 122 + i * 44, cy + ch - 26, r, (JG.coinSave >> i) & 1, t);
  }

  // --- barras de progreso REALES ---
  {
    char p1[8], p2[8];
    snprintf(p1, sizeof(p1), "%d%%", (int)(JG.barN + 0.5f));
    snprintf(p2, sizeof(p2), "%d%%", (int)(JG.barP + 0.5f));
    jmpTextShadow(400, 236, "NORMAL MODE", 4, rgb565(255,255,255), rgb565(8,58,12));
    jmpBar(163, 268, 474, 38, JG.barN / 100.0f, rgb565(64, 236, 44), rgb565(8, 70, 10), p1);
    jmpTextShadow(400, 316, "PRACTICE MODE", 4, rgb565(255,255,255), rgb565(8,58,12));
    jmpBar(163, 348, 474, 38, JG.barP / 100.0f, rgb565(88, 226, 250), rgb565(8, 70, 10), p2);
  }
  // --- indicador de pagina (un solo nivel) ---
  for(int i = 0; i < 7; i++){
    int x = 400 - 3 * 22 + i * 22;
    if(i == 3) jmpDisc(x, 462, 7, rgb565(255, 255, 255));
    else       jmpDisc(x, 462, 5, rgb565(120, 200, 130));
  }

  jmpPresent();
}

// #############################################################
// ##  PANTALLA DE INFORMACION (boton "i" del selector)
// #############################################################
static void jmpInfoFrame(float t){
  jmpBeginFrame();
  for(int y = 0; y < JH; y += 6)
    fillRect(0, y, JW, 6, mix565(rgb565(30, 170, 40), rgb565(8, 96, 14), (uint8_t)(y * 255 / JH)));
  int w = 560, h = 300, x = 400 - w / 2, y = 240 - h / 2;
  float a = jeaseBack(jclampf(t / 0.35f, 0, 1));
  int ww = (int)(w * (0.9f + 0.1f * a)), hh = (int)(h * (0.9f + 0.1f * a));
  x = 400 - ww / 2; y = 240 - hh / 2;
  fillRoundRect(x, y, ww, hh, 18, rgb565(12, 84, 16));
  drawRoundRect(x, y, ww, hh, 18, rgb565(6, 52, 10));
  jmpTextEdge4(400, y + 20, "JUMPER", 8, rgb565(255,255,255), rgb565(16,50,18), 3);
  drawTextC(400, y + 80,  "Nivel 6 de Geometry Dash", 3, rgb565(210, 250, 210));
  drawTextC(400, y + 112, "Dificultad: 7 estrellas (Harder)", 3, rgb565(210, 250, 210));
  drawTextC(400, y + 144, "Longitud: 900 bloques - 1:26", 3, rgb565(210, 250, 210));
  drawTextC(400, y + 176, "Formas: cubo y nave", 3, rgb565(210, 250, 210));
  drawTextC(400, y + 208, "3 monedas secretas: 36%, 43% y 68%", 3, rgb565(255, 214, 90));
  drawTextC(400, y + hh - 46, "Toca para volver", 3, rgb565(150, 230, 160));
  jmpPresent();
}

// #############################################################
// ## 10. FINAL Y RESULTADOS
// #############################################################
static void jmpDoneFrame(float dt, bool draw){
  JG.doneT += dt;
  float t = JG.doneT;
  jmpPartTick(dt);
  if(JG.shake > 0) JG.shake -= dt * 2.0f;

  float shx = 0, shy = 0;
  if(JG.shake > 0){
    float s = JG.shake * 12.0f;
    shx = (jrnd01((uint32_t)(t * 733.0f)) - 0.5f) * s;
    shy = (jrnd01((uint32_t)(t * 977.0f) + 31u) - 0.5f) * s;
  }
  if(!draw) return;
  jmpBeginFrame();
  // El atlas se rasteriza en el PRIMER cuadro del final, justo cuando la
  // pantalla esta cubierta por el destello blanco: el unico cuadro que lo
  // paga es el que no se ve.
  bool capDone = jmpCapBuild("LEVELCOMPLETE!", 11, 4);
  jmpBuildPalette(JG.runTime + t);
  jmpPaintWorld(shx, shy);
  jmpDrawObjects(shx, shy, JG.runTime + t);
  jmpDrawParts(shx, shy);

  // Explosion verde/celeste: rayos radiales + ondas expansivas.
  {
    int cx = (int)JPX, cy = (int)(JGY - (JG.py - JG.camY));
    for(int k = 0; k < 12; k++){
      float a = k * 0.5236f + t * 0.6f;
      float len = jclampf(t * 1400.0f, 0, 900.0f);
      float wdt = 10.0f * jclampf(1.4f - t, 0, 1);
      if(wdt < 1) continue;
      float xs[3] = { (float)cx, cx + cosf(a) * len - sinf(a) * wdt, cx + cosf(a) * len + sinf(a) * wdt };
      float ys[3] = { (float)cy, cy + sinf(a) * len + cosf(a) * wdt, cy + sinf(a) * len - cosf(a) * wdt };
      jmpPoly(xs, ys, 3, rgb565(150, 255, 190));
    }
    for(int k = 0; k < 3; k++){
      float a = (t - k * 0.13f) / 0.8f;
      if(a > 0 && a < 1){
        int r = (int)(20 + a * 460);
        uint8_t al = (uint8_t)((1 - a) * 220);
        int syc = (int)jclampf((float)cy, 0, JH - 1);
        drawCircle(cx, cy, r, mix565(jSky[syc], rgb565(190, 255, 230), al));
        drawCircle(cx, cy, r - 2, mix565(jSky[syc], rgb565(120, 240, 255), al));
      }
    }
  }
  if(t < 0.30f) fillRectA(0, 0, JW, JH, rgb565(230, 255, 255), (uint8_t)((1.0f - t / 0.30f) * 180));

  // Rotulo grande: cae letra a letra con rebote elastico, desde el atlas de
  // 1 bit. Antes eran diez pasadas de fuente vectorial en CADA cuadro de la
  // celebracion; ahora son dos volcados planos por letra.
  if(t > 0.30f && capDone){
    int px_ = 400 - (jmpCapTotal() + 12) / 2;
    for(int i = 0; i < 14; i++){
      if(i == 5) px_ += 12;                        // hueco entre las dos palabras
      float a = jeaseBack(jclampf((t - 0.30f - i * 0.028f) / 0.50f, 0, 1));
      int ly = 78 - (int)((1.0f - a) * 90.0f);
      jmpCapCell(i, px_, ly, rgb565(160, 245, 70), rgb565(14, 44, 16));
      px_ += jmpCapCellW(i);
    }
  }
  // Panel de resultados con conteo animado.
  if(t > 1.05f){
    float a = jeaseOut(jclampf((t - 1.05f) / 0.40f, 0, 1));
    int w = 540, h = 264, x = 400 - w / 2, y = 158 + (int)((1 - a) * 40);
    fillRectA(x, y, w, h, rgb565(6, 24, 10), (uint8_t)(a * 232));
    drawRoundRect(x, y, w, h, 10, rgb565(120, 240, 120));
    float cnt = jclampf((t - 1.35f) / 0.75f, 0, 1);
    char l[40];
    snprintf(l, sizeof(l), "ATTEMPTS: %u", (unsigned)(JG.attempt * cnt + 0.5f));
    drawText(x + 40, y + 26, l, 4, rgb565(255, 224, 90));
    snprintf(l, sizeof(l), "JUMPS: %u", (unsigned)(JG.jumps * cnt + 0.5f));
    drawText(x + 40, y + 62, l, 4, rgb565(255, 224, 90));
    int sec = (int)(JG.runTime * cnt);
    snprintf(l, sizeof(l), "TIME: %02d:%02d", sec / 60, sec % 60);
    drawText(x + 40, y + 98, l, 4, rgb565(255, 224, 90));
    for(int i = 0; i < 3; i++){
      float ca = jclampf((t - 1.6f - i * 0.18f) / 0.3f, 0, 1);
      if(ca <= 0) continue;
      jmpCoinIcon(x + 62 + i * 64, y + 168, (int)(23 * jeaseBack(ca)),
                  ((JG.practice ? JG.coinSave : (JG.coinSave | JG.coinGot)) >> i) & 1, t);
    }
    for(int i = 0; i < 7; i++){
      float sa = jclampf((t - 1.8f - i * 0.07f) / 0.25f, 0, 1);
      if(sa <= 0) continue;
      jmpStar(x + w - 200 + i * 27, y + 168, (int)(13 * jeaseBack(sa)), rgb565(255, 208, 40));
    }
    // Botones REALES.
    if(t > 2.3f){
      fillRoundRect(x + 40, y + h - 46, 190, 38, 10, rgb565(30, 150, 40));
      drawRoundRect(x + 40, y + h - 46, 190, 38, 10, rgb565(140, 250, 140));
      drawTextC(x + 135, y + h - 36, "REINTENTAR", 3, rgb565(255, 255, 255));
      fillRoundRect(x + w - 230, y + h - 46, 190, 38, 10, rgb565(20, 90, 130));
      drawRoundRect(x + w - 230, y + h - 46, 190, 38, 10, rgb565(120, 220, 250));
      drawTextC(x + w - 135, y + h - 36, "NIVELES", 3, rgb565(255, 255, 255));
    }
  }
  jmpPresent();
}

// #############################################################
// ##  PAUSA
// #############################################################
static void jmpPauseFrame(){
  jmpBeginFrame();
  jmpBuildPalette(JG.runTime);
  jmpDimPalette(42, 100);                 // el escenario se apaga sin una sola mezcla alfa
  jmpPaintWorld(0, 0);
  jmpDrawObjects(0, 0, JG.runTime);
  jmpDrawPlayer(0, 0);
  jmpDrawHud();
  int w = 420, h = 250, x = 400 - w / 2, y = 240 - h / 2;
  fillRoundRect(x, y, w, h, 16, rgb565(10, 40, 16));
  drawRoundRect(x, y, w, h, 16, rgb565(120, 240, 120));
  jmpTextEdge4(400, y + 18, "PAUSA", 8, rgb565(255,255,255), rgb565(16,50,18), 3);
  const char* lb[3] = { "CONTINUAR", "REINTENTAR", "NIVELES" };
  for(int i = 0; i < 3; i++){
    int by = y + 76 + i * 52;
    fillRoundRect(x + 40, by, w - 80, 42, 10, i == 0 ? rgb565(30, 150, 40) : rgb565(18, 80, 110));
    drawRoundRect(x + 40, by, w - 80, 42, 10, rgb565(150, 250, 150));
    drawTextC(400, by + 8, lb[i], 4, rgb565(255, 255, 255));
  }
  jmpPresent();
}

// #############################################################
// ## 11. TACTIL Y BUCLE PUBLICO
// ##  ----------------------------------------------------------
// ##  La app posee TODO el tactil (APP_OWN_TOUCH). El GT911
// ##  entrega coordenadas del panel en portrait; aqui se giran al
// ##  lienzo logico horizontal con la MISMA transformacion que ya
// ##  usaba el contenedor vacio de Juegos.
// #############################################################
static inline int jTX(){ return T.y; }
static inline int jTY(){ return (SCR_W - 1) - T.x; }
static inline bool jHit(int x, int y, int w, int h){
  int lx = jTX(), ly = jTY();
  return lx >= x && lx < x + w && ly >= y && ly < y + h;
}
static void jmpFlushSave(){ if(JG.pendSave){ jmpSave(); JG.pendSave = 0; } }

static void jmpGoSelect(){
  JG.screen = JS_SELECT; JG.tScreen = 0;
  jmpClearInput(); JG.acc = 0;
  JG.barN = 0; JG.barP = 0; JG.arrowL = JG.arrowR = 0;
  jmpFlushSave();
}
static void jmpStartLevel(bool practice){
  jmpClearInput();
  JG.practice = practice;
  JG.attempt = 0; JG.jumps = 0; JG.runTime = 0; JG.chk = -1;
  JG.acc = 0; JG.deadT = 0; JG.doneT = 0;
  JG.screen = JS_PLAY; JG.tScreen = 0;
  jmpResetRun(false);
}

static void jmpEnter(){
  gLand = true;
  jmpLoad();
  JG.screen = JS_INTRO; JG.tScreen = 0; JG.introDone = false;
  JG.lastUs = micros(); JG.acc = 0; jNextFrameUs = JG.lastUs;
  JG.practice = false; JG.pendSave = 0;
  JG.barN = 0; JG.barP = 0; JG.arrowL = JG.arrowR = 0;
  JG.attempt = 0; JG.jumps = 0; JG.runTime = 0;
  JG.hue = JL_BG[0].hue; JG.sat = JL_BG[0].sat; JG.val = JL_BG[0].val; JG.pulse = JL_BG[0].pulse;
  jmpClearInput();
  jmpPartClear();
  // El atlas del rotulo se rasteriza AQUI, una sola vez al abrir la app y
  // antes del primer cuadro: el coste queda tapado por la animacion de
  // apertura de ventana del sistema y ningun cuadro de la intro lo paga.
  jmpBeginFrame();
  jIntroCap = jmpCapBuild("GEOMETRYDASH", 12, 4);
  jmpIntroFrame(0.0f);
}

static void jmpTick(){
  // --- reloj monotonico: la animacion depende del TIEMPO, no del cuadro ---
  uint32_t now = micros();
  float dt = (uint32_t)(now - JG.lastUs) * 1e-6f;
  JG.lastUs = now;
  if(dt < 0) dt = 0;
  if(dt > 0.25f) dt = 0.25f;      // tras una pausa larga no se teletransporta
  JG.tScreen += dt;

  const bool press = T.pressed;
  JG.held = T.down;
  const bool draw = jmpFrameDue(now);

  switch(JG.screen){
    // -------------------------------------------------- INTRO
    case JS_INTRO: {
      // Se puede omitir con un toque, pero solo pasados 300 ms: asi el mismo
      // toque que abrio la app no se la salta.
      if(press && JG.tScreen > 0.30f){ JG.introDone = true; jmpGoSelect(); break; }
      if(JG.tScreen >= JINTRO_T){ JG.introDone = true; jmpGoSelect(); break; }
      if(draw) jmpIntroFrame(JG.tScreen);
    } break;

    // -------------------------------------------------- SELECTOR
    case JS_SELECT: {
      // Llenado animado de las barras desde cero hasta el valor guardado.
      float kN = JG.bestNormal, kP = JG.bestPractice;
      float sp = jclampf(dt * 3.2f, 0, 1);
      JG.barN += (kN - JG.barN) * sp; if(fabsf(kN - JG.barN) < 0.25f) JG.barN = kN;
      JG.barP += (kP - JG.barP) * sp; if(fabsf(kP - JG.barP) < 0.25f) JG.barP = kP;
      if(JG.arrowL > 0) JG.arrowL -= dt * 3.2f; else JG.arrowL = 0;
      if(JG.arrowR > 0) JG.arrowR -= dt * 3.2f; else JG.arrowR = 0;
      if(press){
        if(jHit(10, 4, 78, 76)){ jmpFlushSave(); appClose(); return; }
        if(jHit(730, 8, 62, 62)){ JG.screen = JS_INFO; JG.tScreen = 0; break; }
        if(jHit(14, 190, 74, 100)){ JG.arrowL = 1.0f; break; }
        if(jHit(712, 190, 74, 100)){ JG.arrowR = 1.0f; break; }
        if(jHit(163, 262, 474, 50)){ jmpStartLevel(false); break; }
        if(jHit(163, 342, 474, 50)){ jmpStartLevel(true);  break; }
      }
      if(draw) jmpSelectFrame(JG.tScreen);
    } break;

    // -------------------------------------------------- INFO
    case JS_INFO: {
      if(press && JG.tScreen > 0.20f){ JG.screen = JS_SELECT; JG.tScreen = 0.60f; break; }
      if(draw) jmpInfoFrame(JG.tScreen);
    } break;

    // -------------------------------------------------- JUEGO
    case JS_PLAY: {
      // El HUD nunca salta: ni el flanco ni el mantenido cuentan ahi.
      const bool onHud = (jTX() < 84 && jTY() < 62);
      if(press && onHud){ JG.screen = JS_PAUSE; jmpClearInput(); break; }
      if(onHud) JG.held = false;
      else if(press) jJumpQueued = true;        // flanco al latch, lo consume la fisica
      jmpPlayFrame(dt, draw);
    } break;

    // -------------------------------------------------- MUERTE
    case JS_DEAD: {
      jmpPlayFrame(dt, draw);
      if(JG.screen == JS_PLAY) jmpFlushSave();     // el record se guarda una sola vez
    } break;

    // -------------------------------------------------- PAUSA
    case JS_PAUSE: {
      if(press){
        int w = 420, x = 400 - w / 2, y = 240 - 250 / 2;
        for(int i = 0; i < 3; i++){
          if(jHit(x + 40, y + 76 + i * 52, w - 80, 42)){
            jmpClearInput();
            if(i == 0){ JG.screen = JS_PLAY; JG.lastUs = micros(); JG.acc = 0; }
            else if(i == 1){ JG.screen = JS_PLAY; JG.lastUs = micros(); jmpResetRun(JG.practice); }
            else { jmpGoSelect(); }
            return;
          }
        }
      }
      if(draw) jmpPauseFrame();
    } break;

    // -------------------------------------------------- FINAL
    case JS_DONE: {
      if(press && JG.doneT > 2.3f){
        int w = 540, x = 400 - w / 2, y = 158, h = 264;
        if(jHit(x + 40, y + h - 46, 190, 38)){ jmpStartLevel(JG.practice); return; }
        if(jHit(x + w - 230, y + h - 46, 190, 38)){ jmpGoSelect(); return; }
      }
      jmpDoneFrame(dt, draw);
      if(JG.doneT > 0.05f && JG.pendSave) jmpFlushSave();
    } break;
  }
}
