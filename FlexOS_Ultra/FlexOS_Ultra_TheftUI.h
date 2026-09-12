// #############################################################
// ##  FLEX OS ULTRA  ·  PROTECCION CONTRA ROBO  ·  interfaz
// ##  ----------------------------------------------------------
// ##  Las tres pantallas de la funcion y su animacion:
// ##
// ##    1. PRINCIPAL        con el BNO085 conectado
// ##    2. REQUIERE IMU     con el modulo ausente o desconectado
// ##    3. HISTORIAL        lista de incidentes + detalle
// ##
// ##  mas el aviso que se dibuja sobre la pantalla de bloqueo del
// ##  sistema cuando la proteccion se ha disparado.
// ##
// ##  DISENADAS PARA 480x800. No hay una sola coordenada "a ojo": cada
// ##  bloque sale de una constante de maquetacion, los textos largos
// ##  pasan por drawTextClip o por uiFontFit, y la lista con scroll
// ##  tiene su recorte propio -- que es lo que impide que una tarjeta a
// ##  medio salir se dibuje encima de la cabecera.
// ##
// ##  LA ANIMACION
// ##  ----------------------------------------------------------
// ##  Es un BUCLE PERFECTO de 7,2 s dibujado por CODIGO: ni un byte de
// ##  imagen, ni un GIF, ni un video. Todo son rectangulos redondeados,
// ##  circulos y segmentos suavizados -- las mismas primitivas del
// ##  resto del sistema -- compuestos por interpolacion sobre un unico
// ##  parametro `u` en [0,1). Que sea funcion de `u` y no de "un paso
// ##  por cuadro" es lo que hace que la velocidad no dependa de los fps
// ##  y que el ultimo cuadro empalme EXACTAMENTE con el primero: cada
// ##  magnitud animada vale en u=1 lo mismo que en u=0.
// ##
// ##  Y se repinta SOLO el rectangulo interior de su tarjeta (420x190),
// ##  no la pantalla: el resto de la banda se siembra una vez en bbuf
// ##  desde fb y se publica con UN present() por cuadro. Sin reservas
// ##  por cuadro, sin delay bloqueante y sin repintar nada mas.
// ##
// ##  COMO ENCAJA ESTE ARCHIVO
// ##  ----------------------------------------------------------
// ##  Es una PARTE del sketch FlexOS_Ultra.ino, no una unidad de
// ##  traduccion independiente. Se incluye en la cadena de modulos, en
// ##  su sitio; no lo incluyas por tu cuenta desde otro lado.
// #############################################################
#pragma once
#include "FlexOS_Ultra_Theft.h"     // eslabon anterior de la cadena (y el nucleo)

// #############################################################
// ##  MAQUETACION 480x800
// ##  ------------------------------------------------------
// ##  Un solo sitio con las coordenadas. El dibujo y el tactil leen de
// ##  aqui: dos copias de estos numeros habrian acabado separandose, y
// ##  entonces un boton respondria donde no se ve.
// #############################################################
#define TP_HDR_H      62
#define TP_X          14
#define TP_W          (SCR_W - 28)          // 452
#define TP_PAD        20                     // margen interior de tarjeta

#define TP_TOGGLE_Y   72
#define TP_TOGGLE_H   76
#define TP_SENSOR_Y   158
#define TP_SENSOR_H   88
#define TP_ANIM_Y     256
#define TP_ANIM_H     230
#define TP_STATE_Y    496
#define TP_STATE_H    94
#define TP_HISTROW_Y  600
#define TP_ROW_H      58
#define TP_SENSROW_Y  666
#define TP_FOOT_Y     736

// Rectangulo INTERIOR de la animacion: lo unico que se repinta por cuadro.
#define TP_ST_X       (TP_X + 16)
#define TP_ST_Y       (TP_ANIM_Y + 20)
#define TP_ST_W       (TP_W - 32)            // 420
#define TP_ST_H       (TP_ANIM_H - 40)       // 190

#define TP_ANIM_LOOP_MS 7200u                // duracion del bucle completo
#define TP_ANIM_FRAME_MS 34u                 // ~29 fps: fluido y sin castigar el bus

// Zonas pulsables. Igual que en Device Care: se registran al dibujar, asi el
// sitio donde se ve y el sitio donde responde no pueden separarse.
enum { TPH_NONE = 0, TPH_BACK, TPH_TOGGLE, TPH_HIST, TPH_SENS, TPH_RETRY, TPH_ROW0 };
#define TP_HIT_MAX 24
struct TpHit { int16_t x0, y0, x1, y1; uint8_t id; };
static TpHit tpHits[TP_HIT_MAX];
static int   tpHitN = 0;
static void  tpHitClear(){ tpHitN = 0; }
static void  tpHitAdd(int x, int y, int w, int h, uint8_t id){
  if(tpHitN >= TP_HIT_MAX) return;
  tpHits[tpHitN].x0 = (int16_t)x;      tpHits[tpHitN].y0 = (int16_t)y;
  tpHits[tpHitN].x1 = (int16_t)(x + w); tpHits[tpHitN].y1 = (int16_t)(y + h);
  tpHits[tpHitN].id = id;
  tpHitN++;
}
static int tpHitTest(int px, int py){
  for(int i = 0; i < tpHitN; i++)
    if(px >= tpHits[i].x0 && px <= tpHits[i].x1 && py >= tpHits[i].y0 && py <= tpHits[i].y1)
      return tpHits[i].id;
  return TPH_NONE;
}

// Estado de la interfaz.
// Principal, historial y detalle son TRES vistas de UN SOLO estado global
// (ST_THEFT), igual que Ajustes hace con setView: la navegacion interna no
// tiene por que multiplicar los estados de la maquina del sistema.
enum { TPV_MAIN = 0, TPV_HIST, TPV_DETAIL };
static int      tpView     = TPV_MAIN;
static int      tpHistSel  = -1;         // registro abierto en el detalle
static int      tpScroll   = 0;
static bool     tpDrag = false;
static int      tpDragY0 = 0, tpDragS0 = 0;
static uint32_t tpAnimT0   = 0;          // origen del bucle de la animacion
static uint32_t tpAnimMs   = 0;          // ultimo cuadro publicado
static int      tpShownSt  = -1;         // estado del driver que hay dibujado
static uint32_t tpStatusMs = 0;          // refresco de la linea de estado

// #############################################################
// ##  INTERPOLACION
// ##  ------------------------------------------------------
// ##  Todo lo que se mueve sale de estas cuatro funciones sobre el
// ##  parametro del bucle. Ninguna guarda estado: por eso el cuadro
// ##  que toca dibujar depende SOLO del tiempo, y perder cuadros no
// ##  desincroniza nada.
// #############################################################
static inline float tpClamp01(float v){ return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static float tpRamp(float u, float a, float b){
  if(b <= a) return u >= b ? 1.0f : 0.0f;
  return tpClamp01((u - a) / (b - a));
}
static inline float tpEaseOut(float p){ return 1.0f - (1.0f - p) * (1.0f - p); }
static inline float tpEaseIn(float p){ return p * p; }
static inline uint8_t tpA(float v){
  float a = v * 255.0f;
  return (uint8_t)(a < 0.0f ? 0.0f : (a > 255.0f ? 255.0f : a));
}

// #############################################################
// ##  PIEZAS DE LA ILUSTRACION
// ##  ------------------------------------------------------
// ##  Geometricas y planas, en el lenguaje del sistema: ni dibujo
// ##  infantil ni intento de realismo. Todas aceptan alfa para poder
// ##  entrar y salir con un fundido en vez de aparecer de golpe.
// #############################################################

// EL APARATO. Un telefono visto de frente: cuerpo, pantalla y auricular.
static void tpDrawDevice(int cx, int cy, int w, int h, uint8_t a, uint16_t body, uint16_t scr){
  if(!a) return;
  int r = w / 4; if(r < 3) r = 3;
  fillRoundRectA(cx - w / 2, cy - h / 2, w, h, r, body, a);
  int iw = w - 6, ih = h - 12;
  if(iw > 2 && ih > 2)
    fillRoundRectA(cx - iw / 2, cy - h / 2 + 7, iw, ih, r > 2 ? r - 2 : 1, scr, a);
  fillRoundRectA(cx - w / 6, cy - h / 2 + 3, w / 3, 2, 1, scr, (uint8_t)(a * 3 / 4));
}

// UNA MANO. Palma, muneca y cuatro dedos que se abren con `open` (0 = agarre
// cerrado, 1 = mano abierta). `dir` es el lado HACIA EL QUE AGARRA: +1 agarra
// hacia la derecha (la mano del dueno, que esta a la izquierda del aparato) y
// -1 hacia la izquierda (la que se lo lleva, que llega por la derecha). La
// muneca sale siempre por el lado contrario, que es de donde viene el brazo.
static void tpDrawHand(int cx, int cy, int s, int dir, float open, uint8_t a, uint16_t col){
  if(!a || s < 8) return;
  int palmW = s, palmH = (s * 62) / 100;
  int px = cx - palmW / 2, py = cy - palmH / 2;
  fillRoundRectA(px, py, palmW, palmH, s / 4, col, a);
  // Muneca: sale por el lado contrario al aparato.
  int ww = (s * 46) / 100, wh = (s * 42) / 100;
  fillRoundRectA(cx - ww / 2 - dir * (palmW / 2), cy - wh / 2 + palmH / 5, ww, wh,
                 s / 6, col, (uint8_t)(a * 9 / 10));
  // Dedos: cuatro barras sobre el borde que mira al aparato. Al abrirse se
  // separan y se acortan, que es lo que lee el ojo como "soltar".
  int fw = (s * 17) / 100; if(fw < 3) fw = 3;
  int fhBase = (s * 40) / 100;
  int fh = (int)(fhBase * (1.0f - 0.34f * open));
  if(fh < 3) fh = 3;
  int baseX = cx + dir * (palmW / 2 - fw);
  for(int i = 0; i < 4; i++){
    int spread = (int)(open * (s * 0.16f) * (float)i);
    int fy = py - fh + (s * 8) / 100 + (i == 0 || i == 3 ? fh / 5 : 0);
    fillRoundRectA(baseX + dir * (int)(open * s * 0.10f), fy - spread, fw, fh,
                   fw / 2, col, a);
    baseX -= dir * (fw + 2);
  }
}

// EL CANDADO. `close` 0 = arco levantado y abierto, 1 = cerrado.
static void tpDrawPadlock(int cx, int cy, int s, float close, uint8_t a,
                          uint16_t body, uint16_t on){
  if(!a || s < 10) return;
  int bw = s, bh = (s * 74) / 100;
  int by = cy - bh / 3;
  // Arco: se dibuja con segmentos suavizados sobre media circunferencia. Al
  // abrirse sube y se recorta por un lado, como un candado real.
  int ar = (s * 32) / 100;
  int lift = (int)((1.0f - close) * s * 0.30f);
  int acy = by - lift;
  float sweep = 3.14159265f * (0.62f + 0.38f * close);
  float mid   = -3.14159265f / 2.0f;
  int   segs  = 12;
  float th = (float)s / 14.0f; if(th < 1.6f) th = 1.6f;
  // strokeSegAA no acepta alfa, asi que el color del arco se premezcla con el
  // fondo sobre el que se dibuja (`on`, que en los dos llamantes ES el fondo de
  // su tarjeta). Sin esto el arco aparecia opaco de golpe mientras el cuerpo
  // del candado todavia se estaba fundiendo.
  uint16_t arcCol = (a >= 255) ? body : mix565(on, body, a);
  for(int i = 0; i < segs; i++){
    float t0 = mid - sweep / 2.0f + sweep * (float)i / (float)segs;
    float t1 = mid - sweep / 2.0f + sweep * (float)(i + 1) / (float)segs;
    strokeSegAA(cx + ar * cosf(t0), acy + ar * sinf(t0),
                cx + ar * cosf(t1), acy + ar * sinf(t1), th, arcCol);
  }
  fillRoundRectA(cx - bw / 2, by, bw, bh, s / 6, body, a);
  // Ojo de la cerradura
  fillCircleA(cx, by + bh / 2 - bh / 10, s / 10 > 1 ? s / 10 : 2, on, a);
  fillRectA(cx - 1, by + bh / 2 - bh / 10, 3, bh / 4, on, a);
}

// LINEAS DE VELOCIDAD detras de lo que se va. Tres trazos de distinto largo
// que se desvanecen: sugieren el tiron sin dibujar una estela literal.
static void tpDrawSpeedLines(int x, int cy, int len, uint8_t a, uint16_t col){
  if(!a || len < 8) return;
  const int off[3] = { -14, 0, 13 };
  const int mul[3] = { 70, 100, 55 };
  for(int i = 0; i < 3; i++){
    int L = len * mul[i] / 100;
    if(L < 4) continue;
    strokeSegAA((float)(x - L), (float)(cy + off[i]), (float)x, (float)(cy + off[i]),
                2.0f, mix565(TH_SURF, col, a));
  }
}

// #############################################################
// ##  EL ESCENARIO DE LA ANIMACION
// ##  ------------------------------------------------------
// ##  SECUENCIA, en fracciones del bucle:
// ##
// ##    0,00  una mano sostiene el aparato con calma
// ##    0,16  otra mano se acerca por la derecha
// ##    0,34  ARREBATO: el aparato sale disparado, la mano se abre
// ##    0,46  huida: el aparato y la mano salen de escena
// ##    0,60  el aparato ha quedado separado; aviso
// ##    0,70  aparece el estado de seguridad
// ##    0,76  el candado se cierra
// ##    0,84  pulso de confirmacion
// ##    0,90  transicion suave al primer estado
// ##    1,00  = 0,00   (bucle exacto)
// ##
// ##  CADA magnitud vale en u=1 lo mismo que en u=0. Esa es toda la
// ##  receta del bucle perfecto: no hay corte, hay coincidencia.
// #############################################################
static void tpStageBg(int x, int y, int w, int h){
  // Fondo del escenario: degradado vertical corto, en el lenguaje de las
  // superficies del sistema. Se calcula por filas -- ni una reserva, ni una
  // lectura del framebuffer -- y por eso cuesta lo mismo en cada cuadro.
  uint16_t top = uiGlass ? TH_GLASS2 : TH_SURF;
  uint16_t bot = TH_SURF2;
  for(int j = 0; j < h; j++){
    uint8_t t = (uint8_t)(j * 255 / (h > 1 ? h - 1 : 1));
    hLine(x, y + j, w, mix565(top, bot, t));
  }
  // Suelo: la linea de referencia sobre la que ocurre todo. Es fija, asi que
  // el bucle no puede "saltar" por su culpa.
  fillRectA(x + 18, y + h - 30, w - 36, 2, TH_DIV, 190);
}

static void tpDrawStage(float u){
  int sx = TP_ST_X, sy = TP_ST_Y, sw = TP_ST_W, sh = TP_ST_H;
  tpStageBg(sx, sy, sw, sh);

  int ground = sy + sh - 30;
  int cx0    = sx + sw / 2 - 26;        // el aparato, en reposo, algo a la izquierda
  int cy0    = ground - 46;
  int devW   = 30, devH = 52;
  int handS  = 40;

  // ---- Pesos de cada fase (ver la tabla de arriba) ----
  float pAway   = tpEaseIn(tpRamp(u, 0.34f, 0.64f));
  float recoil  = tpRamp(u, 0.34f, 0.47f) * (1.0f - tpRamp(u, 0.90f, 1.00f));
  float openHand= tpRamp(u, 0.34f, 0.45f) * (1.0f - tpRamp(u, 0.90f, 1.00f));
  float aDevGo  = 1.0f - tpRamp(u, 0.54f, 0.64f);
  float aDevBack= tpRamp(u, 0.90f, 1.00f);
  float aDev    = aDevGo > aDevBack ? aDevGo : aDevBack;
  float aThief  = tpRamp(u, 0.16f, 0.26f) * (1.0f - tpRamp(u, 0.56f, 0.66f));
  float aAlert  = tpRamp(u, 0.58f, 0.66f) * (1.0f - tpRamp(u, 0.70f, 0.78f));
  float aLock   = tpRamp(u, 0.70f, 0.79f) * (1.0f - tpRamp(u, 0.92f, 1.00f));
  float close   = tpEaseOut(tpRamp(u, 0.76f, 0.87f));
  float pulse   = tpRamp(u, 0.84f, 0.97f);
  float aSpeed  = tpRamp(u, 0.36f, 0.42f) * (1.0f - tpRamp(u, 0.58f, 0.66f));

  int travel = sw / 2 + 70;
  // Despues de 0,70 el aparato ya no esta a la vista (aDevGo = 0), asi que se
  // devuelve a su sitio: el fundido de vuelta ocurre EN REPOSO y el bucle
  // empalma sin un salto de posicion.
  int devX = (u < 0.70f) ? (cx0 + (int)(pAway * travel)) : cx0;
  int devY = (u < 0.70f) ? (cy0 - (int)(tpRamp(u, 0.34f, 0.52f) * 16.0f)) : cy0;

  // ---- 1. LA MANO DEL DUENO. Siempre en escena: es su mano. ----
  int ownX = cx0 - 30 - (int)(recoil * 26.0f);
  tpDrawHand(ownX, cy0 + 8, handS, +1, openHand, 255, TH_TXT2);

  // ---- 2. LINEAS DE VELOCIDAD ----
  if(aSpeed > 0.01f)
    tpDrawSpeedLines(devX - devW / 2 - 6, devY, 46, tpA(aSpeed * 0.85f), wallAccent());

  // ---- 3. EL APARATO ----
  tpDrawDevice(devX, devY, devW, devH, tpA(aDev), TH_TXT, wallAccent());

  // ---- 4. LA MANO QUE SE LO LLEVA ----
  if(aThief > 0.01f){
    int approach = sx + sw + 40;
    int grabX    = cx0 + 34;
    float pIn    = tpEaseOut(tpRamp(u, 0.16f, 0.34f));
    int tx = (u < 0.34f) ? (approach + (int)((grabX - approach) * pIn))
                         : (devX + 34 + (int)(pAway * 8.0f));
    tpDrawHand(tx, devY + 6, handS, -1, 0.0f, tpA(aThief), TH_MUTE);
  }

  // ---- 5. AVISO: el aparato ha quedado separado ----
  if(aAlert > 0.01f){
    int ax = sx + sw / 2, ay = sy + sh / 2 - 6;
    uint8_t a = tpA(aAlert);
    fillCircleA(ax, ay, 26, TH_WARN, (uint8_t)(a / 5));
    fillRectA(ax - 3, ay - 16, 6, 20, TH_WARN, a);
    fillCircleA(ax, ay + 12, 3, TH_WARN, a);
  }

  // ---- 6. ESTADO DE SEGURIDAD: la tarjeta con el candado ----
  if(aLock > 0.01f){
    uint8_t a = tpA(aLock);
    int cw = 150, ch = 104;
    int gx = sx + (sw - cw) / 2, gy = sy + (sh - ch) / 2 - 6;
    // Pulso de confirmacion: un anillo que se expande y se apaga. Va DEBAJO
    // de la tarjeta para no tapar el candado.
    if(pulse > 0.01f && pulse < 1.0f){
      int pr = 34 + (int)(pulse * 74.0f);
      uint8_t pa = tpA((1.0f - pulse) * aLock * 0.55f);
      if(pa) drawCircle(gx + cw / 2, gy + ch / 2, pr, mix565(TH_SURF2, TH_OK, pa));
    }
    fillRoundRectA(gx, gy, cw, ch, 22, TH_SURF2, a);
    fillRoundRectA(gx, gy, cw, ch / 2, 22, rgb565(255, 255, 255), (uint8_t)(a / 14));
    tpDrawPadlock(gx + cw / 2, gy + 40, 40, close, a, TH_OK, TH_SURF2);
    int fs = uiFontFit(tpt(TPS_LOCKON), cw - 16, 1);
    drawTextCA(gx + cw / 2, gy + ch - 26, tpt(TPS_LOCKON), fs, TH_TXT, a);
  }
}

// Siembra la banda de la animacion en bbuf desde fb. Se llama UNA vez tras
// cada dibujo completo de la pantalla: asi los cuadros siguientes solo tienen
// que repintar el rectangulo interior y el marco de la tarjeta que ya esta en
// bbuf sigue siendo el bueno.
static void tpSeedBand(){
  if(!bbuf || !fb || gRtTarget) return;
  memcpy(bbuf + (size_t)TP_ANIM_Y * SCR_W, fb + (size_t)TP_ANIM_Y * SCR_W,
         (size_t)TP_ANIM_H * SCR_W * 2);
}

// UN cuadro de la animacion. Compone en bbuf y publica con un solo present():
// el mismo camino anti-parpadeo que usan la tarjeta del cronometro y el aviso
// de caida. Nunca repinta la pantalla entera y nunca bloquea el bucle.
static void tpAnimFrame(){
  if(!bbuf) return;
  uint32_t now = millis();
  float u = (float)((now - tpAnimT0) % TP_ANIM_LOOP_MS) / (float)TP_ANIM_LOOP_MS;
  uint16_t* ob = gBuf;
  int cx0 = gClipX0, cx1 = gClipX1, cy0 = gClipY0, cy1 = gClipY1;
  setBuf(bbuf);
  gClipX0 = TP_ST_X; gClipX1 = TP_ST_X + TP_ST_W - 1;
  gClipY0 = TP_ST_Y; gClipY1 = TP_ST_Y + TP_ST_H - 1;
  tpDrawStage(u);
  gClipX0 = cx0; gClipX1 = cx1; gClipY0 = cy0; gClipY1 = cy1;
  setBuf(ob);
  present(TP_ANIM_Y, TP_ANIM_Y + TP_ANIM_H - 1);
}

// #############################################################
// ##  PIEZAS DE INTERFAZ
// #############################################################

// Cabecera comun: chevron de volver + titulo. La zona pulsable se registra
// aqui mismo.
static void tpHeader(const char* title){
  fillRect(0, 0, SCR_W, TP_HDR_H, TH_PAGE);
  strokeSegAA(30, 26, 18, 18, 2.4f, TH_NAV);
  strokeSegAA(18, 18, 30, 10, 2.4f, TH_NAV);
  int fs = uiFontFit(title, SCR_W - 130, 3);
  drawTextC(SCR_W / 2, (TP_HDR_H - uiLineH(fs)) / 2 - 2, title, fs, TH_TXT);
  tpHitAdd(0, 0, 64, TP_HDR_H, TPH_BACK);
}

// Interruptor de pildora, con el mismo material que el resto del sistema.
static void tpPill(int x, int y, int w, int h, bool on, bool enabled){
  uint16_t track = !enabled ? TH_DIS : (on ? wallAccent() : TH_TRACK);
  fillRoundRect(x, y, w, h, h / 2, track);
  int kr = h / 2 - 4;
  int kx = on ? (x + w - h / 2) : (x + h / 2);
  fillCircle(kx, y + h / 2, kr, enabled ? TH_WIN : TH_SURF2);
}

// Punto de estado con halo, el mismo que usa el resto del sistema para decir
// "esto esta vivo".
static void tpDot(int cx, int cy, uint16_t col){
  fillCircleA(cx, cy, 9, col, 60);
  fillCircle(cx, cy, 5, col);
}

// Escudo: el glifo de la funcion. Dibujado, no un bitmap.
static void tpShield(int cx, int cy, int s, uint16_t col, uint8_t a){
  int w = s, h = (s * 118) / 100;
  int x = cx - w / 2, y = cy - h / 2;
  fillRoundRectA(x, y, w, (h * 62) / 100, w / 6, col, a);
  fillTriangle(x, y + (h * 52) / 100, x + w, y + (h * 52) / 100, cx, y + h, col);
}

// ---- Textos de estado: TODOS salen del servicio, ninguno es decorativo ----
static const char* tpSensorStateText(){
  switch(imuState()){
    case FIMU_DETECTING:    return tpt(TPS_DETECTING);
    case FIMU_CONNECTED:
    case FIMU_AHRS_ACTIVE:  return tpt(TPS_CONNECTED);
    case FIMU_DISCONNECTED: return tpt(TPS_LOST);
    case FIMU_ERROR:        return tpt(TPS_BUSERR);
    default:                return tpt(TPS_NOTFOUND);
  }
}
static uint16_t tpSensorStateColor(){
  switch(imuState()){
    case FIMU_CONNECTED:
    case FIMU_AHRS_ACTIVE:  return TH_OK;
    case FIMU_DETECTING:    return TH_WARN;
    case FIMU_ERROR:        return TH_ERR;
    default:                return TH_DANGER;
  }
}
static const char* tpStatusTitle(){
  switch(tpStatus()){
    case TP_ST_ACTIVE:   return tpt(TPS_ON);
    case TP_ST_PAUSED:   return tpt(TPS_PAUSED);
    case TP_ST_DETECTED:
    case TP_ST_LOCKED:   return tpt(TPS_LOCKEDST);
    default:             return tpt(TPS_OFF);
  }
}
static const char* tpStatusSub(){
  switch(tpStatus()){
    case TP_ST_ACTIVE:   return tpt(TPS_MONITORING);
    case TP_ST_PAUSED:   return tpt(TPS_PAUSEDWHY);
    case TP_ST_DETECTED:
    case TP_ST_LOCKED:   return tpt(TPS_LOCKBODY);
    default:             return tpt(TPS_FOOT);
  }
}
static uint16_t tpStatusColor(){
  switch(tpStatus()){
    case TP_ST_ACTIVE:   return TH_OK;
    case TP_ST_PAUSED:   return TH_WARN;
    case TP_ST_DETECTED:
    case TP_ST_LOCKED:   return TH_DANGER;
    default:             return TH_MUTE;
  }
}

// ---- Fecha y hora EXACTAS de un registro ----
// La hora del evento se guarda en epoca UTC; aqui se pasa a la hora local con
// el mismo desplazamiento que usa todo el sistema. Segundos incluidos: un
// incidente de seguridad sin la hora exacta no sirve para reconstruir nada.
static void tpFmtTime(uint32_t utc, char* out, size_t n){
  long local = (long)utc + FLEXOS_TZ_OFFSET_SEC;
  long rem = local % 86400L; if(rem < 0) rem += 86400L;
  snprintf(out, n, "%02d:%02d:%02d", (int)(rem / 3600L),
           (int)((rem % 3600L) / 60L), (int)(rem % 60L));
}
static void tpFmtDate(uint32_t utc, char* out, size_t n){
  long local = (long)utc + FLEXOS_TZ_OFFSET_SEC;
  long days  = local / 86400L; if(local % 86400L < 0) days--;
  int yy, mo, dd; clkCivilFromDays(days, yy, mo, dd);
  snprintf(out, n, "%02d/%02d/%04d", dd, mo, yy);
}
static long tpLocalDay(uint32_t utc){
  long local = (long)utc + FLEXOS_TZ_OFFSET_SEC;
  long days  = local / 86400L; if(local % 86400L < 0) days--;
  return days;
}
static const char* tpKindName(uint8_t kind){
  return (kind == TP_EV_SNATCH_FALL) ? tpt(TPS_SNATCHFALL) : tpt(TPS_SNATCH);
}

// #############################################################
// ##  PANTALLA 1  ·  PRINCIPAL (con el modulo conectado)
// #############################################################
static void tpDrawToggleCard(){
  int y = TP_TOGGLE_Y;
  uiSurface(TP_X, y, TP_W, TP_TOGGLE_H, 22, UIS_CARD);
  int tx = TP_X + TP_PAD;
  int pillW = 76, pillH = 38;
  int pillX = TP_X + TP_W - TP_PAD - pillW;
  drawTextClip(tx, y + 14, tpt(TPS_PROTECTION), 3, TH_TXT, pillX - 14);
  const char* sub = tpOn ? tpStatusTitle() : tpt(TPS_OFF);
  drawTextClip(tx, y + 46, sub, 1, tpOn ? tpStatusColor() : TH_MUTE, pillX - 14);
  tpPill(pillX, y + (TP_TOGGLE_H - pillH) / 2, pillW, pillH, tpOn, true);
  tpHitAdd(TP_X, y, TP_W, TP_TOGGLE_H, TPH_TOGGLE);
}

static void tpDrawSensorCard(){
  int y = TP_SENSOR_Y;
  uiSurface(TP_X, y, TP_W, TP_SENSOR_H, 22, UIS_CARD);
  int tx = TP_X + TP_PAD;
  tpDot(tx + 6, y + 24, tpSensorStateColor());
  const char* st = tpSensorStateText();
  int stW = textW(st, 2);
  int right = TP_X + TP_W - TP_PAD;
  drawTextClip(tx + 24, y + 13, "BNO085", 3, TH_TXT, right - stW - 16);
  drawTextR(right, y + 16, st, 2, tpSensorStateColor());
  drawTextClip(tx + 24, y + 46, tpt(TPS_MODULESUB), 1, TH_TXT2, right);
  drawTextClip(tx + 24, y + 62, "AHRS \xC2\xB7 9DOF \xC2\xB7 I\xC2\xB2""C", 1, TH_MUTE, right);
}

static void tpDrawAnimCard(){
  uiSurface(TP_X, TP_ANIM_Y, TP_W, TP_ANIM_H, 24, UIS_CARD);
  // El marco se dibuja aqui UNA vez; el interior lo repinta tpAnimFrame().
  int cx0 = gClipX0, cx1 = gClipX1, cy0 = gClipY0, cy1 = gClipY1;
  gClipX0 = TP_ST_X; gClipX1 = TP_ST_X + TP_ST_W - 1;
  gClipY0 = TP_ST_Y; gClipY1 = TP_ST_Y + TP_ST_H - 1;
  float u = (float)((millis() - tpAnimT0) % TP_ANIM_LOOP_MS) / (float)TP_ANIM_LOOP_MS;
  tpDrawStage(u);
  gClipX0 = cx0; gClipX1 = cx1; gClipY0 = cy0; gClipY1 = cy1;
}

static void tpDrawStateCard(){
  int y = TP_STATE_Y;
  uiSurface(TP_X, y, TP_W, TP_STATE_H, 22, UIS_CARD);
  int tx = TP_X + TP_PAD, right = TP_X + TP_W - TP_PAD;
  drawTextClip(tx, y + 10, tpStatusTitle(), 2, tpStatusColor(), right);
  drawTextClip(tx, y + 32, tpStatusSub(), 1, TH_TXT2, right);
  fillRect(tx, y + 50, TP_W - 2 * TP_PAD, 1, TH_DIV);

  tpHistLoad();
  if(tpHistN <= 0){
    drawTextClip(tx, y + 60, tpt(TPS_LASTEVENT), 1, TH_MUTE, right);
    drawTextClip(tx, y + 72, tpt(TPS_NOEVENTS), 2, TH_TXT2, right);
    return;
  }
  const TpRec* r = &tpHist[0];
  faWarnIcon(tx + 9, y + 70, 9, TH_WARN, TH_SURF);
  drawTextClip(tx + 26, y + 58, tpKindName(r->kind), 2, TH_TXT, right);
  char d[16], h[16], line[48];
  tpFmtDate(r->utc, d, sizeof(d));
  tpFmtTime(r->utc, h, sizeof(h));
  snprintf(line, sizeof(line), "%s \xC2\xB7 %s", d, h);
  drawTextClip(tx + 26, y + 78, line, 1, TH_TXT2, right - 90);
  if(r->flags & TP_F_LOCKED) drawTextR(right, y + 78, tpt(TPS_LOCKON), 1, TH_DANGER);
}

static void tpDrawRow(int y, const char* title, const char* value, uint8_t id, bool chevron){
  uiSurface(TP_X, y, TP_W, TP_ROW_H, 18, UIS_CARD);
  int tx = TP_X + TP_PAD, right = TP_X + TP_W - TP_PAD;
  int vw = value ? textW(value, 2) : 0;
  int cw = chevron ? 18 : 0;
  drawTextClip(tx, y + (TP_ROW_H - uiLineH(2)) / 2, title, 2, TH_TXT, right - vw - cw - 14);
  if(value) drawTextR(right - cw, y + (TP_ROW_H - uiLineH(2)) / 2, value, 2, TH_TXT2);
  if(chevron){
    int cx = right - 5, cy = y + TP_ROW_H / 2;
    strokeSegAA(cx - 6, cy - 6, cx, cy, 2.0f, TH_MUTE);
    strokeSegAA(cx, cy, cx - 6, cy + 6, 2.0f, TH_MUTE);
  }
  tpHitAdd(TP_X, y, TP_W, TP_ROW_H, id);
}

static void tpRenderMain(){
  setBuf(fb);
  tpHitClear();
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  tpHeader(tpt(TPS_TITLE));
  tpDrawToggleCard();
  tpDrawSensorCard();
  tpDrawAnimCard();
  tpDrawStateCard();
  tpDrawRow(TP_HISTROW_Y, tpt(TPS_HISTORY), NULL, TPH_HIST, true);
  tpDrawRow(TP_SENSROW_Y, tpt(TPS_SENS), flexTheftSensName(tpSens, LI()), TPH_SENS, false);
  int fs = uiFontFit(tpt(TPS_FOOT), TP_W - 8, 1);
  drawTextC(SCR_W / 2, TP_FOOT_Y, tpt(TPS_FOOT), fs, TH_MUTE);
  flxFlushAll();
  tpSeedBand();
}

// #############################################################
// ##  PANTALLA 2  ·  REQUIERE MODULO IMU
// ##  ------------------------------------------------------
// ##  NO es una pantalla vacia con un mensaje de error: dice QUE
// ##  modulo hace falta, lo ENSENA -- el mismo grafico del TENSTAR
// ##  GY-BNO085 que dibuja Flex Device Care, por codigo y sin un byte
// ##  de imagen en flash -- y da un boton para volver a comprobar.
// ##
// ##  Mientras esta pantalla este a la vista, la proteccion NO se
// ##  puede activar, el clasificador NO corre y no se finge que haya
// ##  nada monitorizando.
// #############################################################
#define TPM_ICON_Y    92
#define TPM_TITLE_Y   152
#define TPM_BODY_Y    194
#define TPM_GFX_Y     258
#define TPM_GFX_W     286
#define TPM_GFX_H     178
#define TPM_NAME_Y    458
#define TPM_SUB_Y     494
#define TPM_STATE_Y   536
#define TPM_BTN_Y     626
#define TPM_BTN_W     240
#define TPM_BTN_H     52
#define TPM_PINS_Y    708

static void tpGfxGeom(int &gx, int &gy, int &gw, int &gh){
  gw = TPM_GFX_W; gh = TPM_GFX_H;
  gx = (SCR_W - gw) / 2; gy = TPM_GFX_Y;
}

// Parte el cuerpo del texto en lineas que QUEPAN, por palabras. Sin esto el
// idioma con la frase mas larga se saldria de la pantalla -- que es justo lo
// que no puede pasar en una maqueta fija de 480x800.
//
// DOS DETALLES QUE IMPORTAN:
//   · se copia un CODEPOINT entero cada vez. Partir una secuencia UTF-8 por
//     la mitad dibujaria un glifo roto, y este texto lleva acentos en los
//     cinco idiomas;
//   · si el texto no cabe en `maxLines`, la ultima linea termina en puntos
//     suspensivos en vez de cortarse en seco: el usuario ve que falta algo.
static int tpWrapCentered(const char* s, int cx, int y, int maxw, int fs,
                          uint16_t col, int maxLines){
  char line[160];
  int lines = 0;
  const char* p = s;
  while(*p && lines < maxLines){
    const char* lastSpace = NULL;
    int n = 0; line[0] = 0;
    const char* q = p;
    while(*q){
      unsigned char b = (unsigned char)*q;
      int cl = 1;
      if((b & 0xE0) == 0xC0)      cl = 2;
      else if((b & 0xF0) == 0xE0) cl = 3;
      else if((b & 0xF8) == 0xF0) cl = 4;
      if(n + cl >= (int)sizeof(line) - 1) break;
      for(int k = 0; k < cl && q[k]; k++) line[n++] = q[k];
      line[n] = 0;
      if(b == ' ') lastSpace = q;
      if(textW(line, fs) > maxw){
        if(lastSpace && lastSpace > p){ n = (int)(lastSpace - p); line[n] = 0; q = lastSpace + 1; }
        else { q += cl; }
        break;
      }
      q += cl;
    }
    p = q;
    while(*p == ' ') p++;
    // Ultima linea permitida y todavia queda texto: se marca con puntos.
    if(*p && lines == maxLines - 1){
      while(n > 0 && textW(line, fs) > maxw - textW("\xE2\x80\xA6", fs)){
        do { n--; } while(n > 0 && ((unsigned char)line[n] & 0xC0) == 0x80);
        line[n] = 0;
      }
      if(n + 4 < (int)sizeof(line)){ memcpy(line + n, "\xE2\x80\xA6", 4); }
    }
    drawTextC(cx, y, line, fs, col);
    y += uiLineH(fs) + 4;
    lines++;
  }
  return y;
}

static void tpRenderMissing(){
  setBuf(fb);
  tpHitClear();
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  tpHeader(tpt(TPS_TITLE));

  faWarnIcon(SCR_W / 2, TPM_ICON_Y, 26, TH_WARN, TH_PAGE);
  int fs = uiFontFit(tpt(TPS_NEEDIMU), SCR_W - 80, 4);
  drawTextC(SCR_W / 2, TPM_TITLE_Y, tpt(TPS_NEEDIMU), fs, TH_TXT);
  tpWrapCentered(tpt(TPS_NEEDBODY), SCR_W / 2, TPM_BODY_Y, SCR_W - 76, 1, TH_TXT2, 4);

  // EL MODULO, dibujado por codigo. Es el MISMO grafico que usa Flex Device
  // Care -> Deteccion de caidas, no una ilustracion generica de "un IMU":
  // el usuario tiene que reconocer la placa que necesita comprar.
  int gx, gy, gw, gh; tpGfxGeom(gx, gy, gw, gh);
  dcDrawBnoModule(gx, gy, gw, gh, millis() - tpAnimT0);

  drawTextC(SCR_W / 2, TPM_NAME_Y, "TENSTAR / GY-BNO085", 3, TH_TXT);
  drawTextC(SCR_W / 2, TPM_SUB_Y, "BNO085 \xC2\xB7 AHRS \xC2\xB7 9DOF", 2, TH_ACCS);

  drawTextC(SCR_W / 2, TPM_STATE_Y, tpt(TPS_STATE), 1, TH_MUTE);
  drawTextC(SCR_W / 2, TPM_STATE_Y + 16, tpSensorStateText(), 3, tpSensorStateColor());
  // Si la proteccion estaba ACTIVADA y el modulo se ha ido, hay que decir en
  // que queda la funcion -- no basta con decir que falta el sensor.
  if(tpOn)
    drawTextC(SCR_W / 2, TPM_STATE_Y + 50, tpt(TPS_PAUSED), 2, TH_WARN);

  int bx = (SCR_W - TPM_BTN_W) / 2;
  fillRoundRect(bx, TPM_BTN_Y, TPM_BTN_W, TPM_BTN_H, TPM_BTN_H / 2, TH_PRIM);
  int bfs = uiFontFit(tpt(TPS_RECHECK), TPM_BTN_W - 30, 2);
  drawTextC(SCR_W / 2, TPM_BTN_Y + (TPM_BTN_H - uiLineH(bfs)) / 2, tpt(TPS_RECHECK),
            bfs, TH_ONACC);
  tpHitAdd(bx, TPM_BTN_Y, TPM_BTN_W, TPM_BTN_H, TPH_RETRY);

  drawTextC(SCR_W / 2, TPM_PINS_Y, "3V3 \xC2\xB7 GND \xC2\xB7 SCL \xC2\xB7 SDA", 1, TH_MUTE);
  flxFlushAll();
}

// Sondeando: no se puede decir todavia si hay modulo o no, asi que no se dice.
static void tpRenderProbing(){
  setBuf(fb);
  tpHitClear();
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  tpHeader(tpt(TPS_TITLE));
  tpShield(SCR_W / 2, SCR_H / 2 - 90, 56, TH_TXT2, 255);
  uint32_t e = millis() - tpAnimT0;
  for(int i = 0; i < 3; i++){
    int ph = (int)((e / 220u + i) % 3u);
    fillCircle(SCR_W / 2 - 20 + i * 20, SCR_H / 2, ph == 0 ? 8 : 5,
               ph == 0 ? TH_PRIM : TH_TRACK);
  }
  drawTextC(SCR_W / 2, SCR_H / 2 + 30, tpt(TPS_DETECTING), 2, TH_TXT2);
  flxFlushAll();
}

// #############################################################
// ##  PANTALLA 3  ·  HISTORIAL DE SEGURIDAD
// ##  ------------------------------------------------------
// ##  Agrupado por dia, con HOY y AYER en claro. Cada tarjeta abre su
// ##  DETALLE: fecha, hora exacta, tipo, secuencia (los motivos que
// ##  sumaron), confianza, impacto posterior si lo hubo y estado del
// ##  bloqueo.
// ##
// ##  La lista tiene RECORTE PROPIO: una tarjeta a medio salir se corta
// ##  por donde toca en vez de dibujarse encima de la cabecera.
// #############################################################
#define TPH_TOP      (TP_HDR_H + 6)
#define TPH_BOT      (SCR_H - 16)
#define TPH_CARD_H   66
#define TPH_HEAD_H   28

static int tpHistContentH(){
  tpHistLoad();
  int h = 12;
  long lastDay = -0x7FFFFFFFL;
  for(int i = 0; i < tpHistN; i++){
    long d = tpLocalDay(tpHist[i].utc);
    if(d != lastDay){ h += TPH_HEAD_H; lastDay = d; }
    h += TPH_CARD_H + 8;
  }
  return h + 12;
}
static int tpHistMaxScroll(){
  int vis = TPH_BOT - TPH_TOP;
  int c = tpHistContentH();
  return (c > vis) ? (c - vis) : 0;
}

// Cabecera de dia: HOY / AYER / "12 sep". Se calcula contra el dia local de
// AHORA, no contra un texto guardado: manana la de hoy tiene que decir "ayer".
static void tpDayHeader(uint32_t utc, char* out, size_t n){
  long d   = tpLocalDay(utc);
  long now = tpLocalDay(clkNowUtc());
  if(d == now)     { snprintf(out, n, "%s", tpt(TPS_TODAY)); return; }
  if(d == now - 1) { snprintf(out, n, "%s", tpt(TPS_YESTERDAY)); return; }
  int yy, mo, dd; clkCivilFromDays(d, yy, mo, dd);
  snprintf(out, n, "%02d %s", dd, MO_SHORT[LI()][(mo - 1) % 12]);
}

static void tpRenderHist(){
  setBuf(fb);
  tpHitClear();
  tpHistLoad();
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  tpHeader(tpt(TPS_HISTORY));

  if(tpHistN <= 0){
    tpShield(SCR_W / 2, SCR_H / 3, 64, TH_MUTE, 255);
    drawTextC(SCR_W / 2, SCR_H / 3 + 60, tpt(TPS_NOEVENTS), 3, TH_TXT2);
    drawTextC(SCR_W / 2, SCR_H / 3 + 96, tpt(TPS_EMPTYHIST), 1, TH_MUTE);
    flxFlushAll();
    return;
  }

  int c0 = gClipY0, c1 = gClipY1;
  gClipY0 = TPH_TOP; gClipY1 = TPH_BOT;
  int x = TP_X, w = TP_W;
  int y = TPH_TOP + 12 - tpScroll;
  long lastDay = -0x7FFFFFFFL;
  for(int i = 0; i < tpHistN; i++){
    const TpRec* r = &tpHist[i];
    long d = tpLocalDay(r->utc);
    if(d != lastDay){
      lastDay = d;
      if(y + TPH_HEAD_H > TPH_TOP && y < TPH_BOT){
        char hd[32]; tpDayHeader(r->utc, hd, sizeof(hd));
        drawText(x + 6, y + 6, hd, 1, TH_MUTE);
      }
      y += TPH_HEAD_H;
    }
    if(y + TPH_CARD_H > TPH_TOP && y < TPH_BOT){
      uiSurface(x, y, w, TPH_CARD_H, 18, UIS_CARD);
      faWarnIcon(x + 28, y + 24, 13, TH_WARN, TH_SURF);
      int right = x + w - 16;
      drawTextClip(x + 50, y + 10, tpKindName(r->kind), 2, TH_TXT, right - 44);
      char hh[16]; tpFmtTime(r->utc, hh, sizeof(hh));
      drawTextR(right, y + 12, hh, 2, TH_TXT2);
      // El subtitulo NO repite el titulo: dice lo que el titulo no dice. En un
      // incidente correlacionado, la SECUENCIA (el arrebato fue antes del
      // golpe) es el dato que explica por que las dos cosas son una sola.
      char sub[96];
      if(r->kind == TP_EV_SNATCH_FALL)
        snprintf(sub, sizeof(sub), "%s", tpt(TPS_BEFOREIMPACT));
      else
        snprintf(sub, sizeof(sub), "%s \xC2\xB7 %s %u",
                 (r->flags & TP_F_LOCKED) ? tpt(TPS_LOCKON) : tpt(TPS_LOCKOFF),
                 tpt(TPS_CONF), (unsigned)r->conf);
      drawTextClip(x + 50, y + 38, sub, 1, TH_TXT2, right);
      tpHitAdd(x, y, w, TPH_CARD_H, (uint8_t)(TPH_ROW0 + i));
    }
    y += TPH_CARD_H + 8;
  }
  gClipY0 = c0; gClipY1 = c1;
  flxFlushAll();
}

// ---- Detalle de un evento ----
static int tpDetailRow(int y, const char* label, const char* value, uint16_t col){
  int x = TP_X + TP_PAD, right = TP_X + TP_W - TP_PAD;
  drawText(x, y + 4, label, 1, TH_MUTE);
  int vw = textW(value, 2);
  if(vw > (right - x - 140)) drawTextClip(x, y + 18, value, 2, col, right);
  else                       drawTextR(right, y + 2, value, 2, col);
  return y + 34;
}

static void tpRenderDetail(){
  setBuf(fb);
  tpHitClear();
  tpHistLoad();
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  tpHeader(tpt(TPS_DETAIL));
  if(tpHistSel < 0 || tpHistSel >= tpHistN){ flxFlushAll(); return; }
  const TpRec* r = &tpHist[tpHistSel];

  int y = TP_HDR_H + 18;
  int cardH = 96;
  uiSurface(TP_X, y, TP_W, cardH, 22, UIS_CARD);
  faWarnIcon(TP_X + TP_PAD + 16, y + 40, 20, TH_WARN, TH_SURF);
  int fs = uiFontFit(tpKindName(r->kind), TP_W - 2 * TP_PAD - 50, 3);
  drawTextClip(TP_X + TP_PAD + 44, y + 22, tpKindName(r->kind), fs, TH_TXT,
               TP_X + TP_W - TP_PAD);
  char d[16], h[16], line[48];
  tpFmtDate(r->utc, d, sizeof(d));
  tpFmtTime(r->utc, h, sizeof(h));
  snprintf(line, sizeof(line), "%s \xC2\xB7 %s", d, h);
  drawTextClip(TP_X + TP_PAD + 44, y + 22 + uiLineH(fs) + 6, line, 2, TH_TXT2,
               TP_X + TP_W - TP_PAD);
  y += cardH + 14;

  // Ficha de datos. Todos salen del registro; ninguno es de relleno.
  int listH = 5 * 34 + 24;
  uiSurface(TP_X, y, TP_W, listH, 22, UIS_CARD);
  int ry = y + 12;
  char buf[48];
  snprintf(buf, sizeof(buf), "%u / 100", (unsigned)r->conf);
  ry = tpDetailRow(ry, tpt(TPS_CONF), buf, TH_TXT);
  snprintf(buf, sizeof(buf), "%u.%u g", (unsigned)(r->pull / 10), (unsigned)(r->pull % 10));
  ry = tpDetailRow(ry, tpt(TPS_PULL), buf, TH_TXT);
  snprintf(buf, sizeof(buf), "%u ms", (unsigned)r->burst);
  ry = tpDetailRow(ry, tpt(TPS_BURST), buf, TH_TXT);
  snprintf(buf, sizeof(buf), "%u ms", (unsigned)r->escape);
  ry = tpDetailRow(ry, tpt(TPS_ESCAPE), buf, TH_TXT);
  if(r->impact){
    snprintf(buf, sizeof(buf), "%u.%u g", (unsigned)(r->impact / 10), (unsigned)(r->impact % 10));
    ry = tpDetailRow(ry, tpt(TPS_IMPACT), buf, TH_WARN);
  } else {
    ry = tpDetailRow(ry, tpt(TPS_IMPACT), tpt(TPS_NOIMPACT), TH_TXT2);
  }
  y += listH + 14;

  // LA SECUENCIA: que rasgos vio el clasificador, en el orden del patron.
  // Sin esto, una confianza de 82 no se puede revisar.
  int seqH = 132;
  uiSurface(TP_X, y, TP_W, seqH, 22, UIS_CARD);
  drawText(TP_X + TP_PAD, y + 12, tpt(TPS_SEQUENCE), 1, TH_MUTE);
  static const uint16_t SEQ_BIT[6] = {
    FLEXTHEFT_R_HELD, FLEXTHEFT_R_PULL, FLEXTHEFT_R_JERK,
    FLEXTHEFT_R_DIR,  FLEXTHEFT_R_TWIST, FLEXTHEFT_R_ESCAPE };
  static const char* SEQ_ES[6] = {
    "En la mano", "Tir\xC3\xB3n", "Arranque brusco",
    "Direcci\xC3\xB3n coherente", "Giro acoplado", "Separaci\xC3\xB3n" };
  static const char* SEQ_EN[6] = {
    "In hand", "Pull", "Sharp onset", "Coherent direction", "Coupled twist", "Separation" };
  int sy2 = y + 32;
  for(int i = 0; i < 6; i++){
    bool on = (r->reasons & SEQ_BIT[i]) != 0;
    int col = i % 2, row = i / 2;
    int ix = TP_X + TP_PAD + col * ((TP_W - 2 * TP_PAD) / 2);
    int iy = sy2 + row * 30;
    fillCircle(ix + 7, iy + 7, 6, on ? TH_OK : TH_TRACK);
    if(on){
      strokeSegAA(ix + 4, iy + 7, ix + 6, iy + 10, 1.6f, TH_ONACC);
      strokeSegAA(ix + 6, iy + 10, ix + 10, iy + 4, 1.6f, TH_ONACC);
    }
    drawTextClip(ix + 20, iy + 1, (LI() == 0 ? SEQ_ES[i] : SEQ_EN[i]), 1,
                 on ? TH_TXT : TH_MUTE, ix + (TP_W - 2 * TP_PAD) / 2 - 6);
  }
  y += seqH + 14;

  // Estado del bloqueo
  uiSurface(TP_X, y, TP_W, TP_ROW_H, 18, UIS_CARD);
  drawText(TP_X + TP_PAD, y + (TP_ROW_H - uiLineH(2)) / 2, tpt(TPS_LOCKFIELD), 2, TH_TXT);
  drawTextR(TP_X + TP_W - TP_PAD, y + (TP_ROW_H - uiLineH(2)) / 2,
            (r->flags & TP_F_LOCKED) ? tpt(TPS_LOCKON) : tpt(TPS_LOCKOFF), 2,
            (r->flags & TP_F_LOCKED) ? TH_DANGER : TH_TXT2);
  flxFlushAll();
}

// #############################################################
// ##  DIBUJO Y NAVEGACION
// #############################################################
static bool tpModuleReady(){
  int st = flexBnoState();
  return st == FLEXBNO_ST_READY;
}
static bool tpModuleMissing(){
  int st = flexBnoState();
  return st == FLEXBNO_ST_ABSENT || st == FLEXBNO_ST_LOST || !gtOk;
}

static void theftRender(){
  if(tpView == TPV_DETAIL){ tpRenderDetail(); return; }
  if(tpView == TPV_HIST){   tpRenderHist();   return; }
  tpShownSt = flexBnoState();
  if(tpModuleMissing())      tpRenderMissing();
  else if(tpModuleReady())   tpRenderMain();
  else                       tpRenderProbing();
}

static void theftEnter(){
  gState      = ST_THEFT;
  tpView      = TPV_MAIN;
  tpHistSel   = -1;
  tpScroll    = 0;
  tpDrag      = false;
  tpAnimT0    = millis();
  tpAnimMs    = 0;
  tpStatusMs  = millis();
  tpHistLoad();
  tpTickMs = millis();
  // SE ADQUIERE EL SENSOR SOLO PARA MIRAR. Si la proteccion esta apagada, el
  // servicio hay que encenderlo igualmente para poder decir si el modulo esta
  // o no -- pero sin alimentar el clasificador (tpSensorTick sale en su
  // primera linea con tpOn a false). Al salir se suelta si no hace falta.
  tpHoldImu(true);          // idempotente: si la proteccion ya lo tenia, no cuenta dos veces
  imuRetry(0);                      // un sondeo al entrar, como hace Device Care
  theftRender();
  touchDropAll();
}

static void theftExit(){
  // Si la proteccion esta apagada, el enganche era solo para ensenar el
  // estado del modulo: se suelta y el bus vuelve a ser del tactil.
  if(!tpOn && tpHold) tpHoldImu(false);
  tpHistSave();
  gState = ST_APP;
  settingsRender();
}

static void theftBack(){
  if(tpView == TPV_DETAIL){ tpView = TPV_HIST; tpHistSel = -1; theftRender(); touchDropAll(); return; }
  if(tpView == TPV_HIST){   tpView = TPV_MAIN; tpScroll = 0; tpAnimT0 = millis();
                            theftRender(); touchDropAll(); return; }
  theftExit();
}

// ---- Animacion y refrescos automaticos ----
// La pantalla se REHACE sola cuando el modulo aparece o desaparece: conectar
// el BNO085 con esto abierto cambia de pantalla sin tocar nada.
static void tpScreenTick(){
  uint32_t now = millis();
  int st = flexBnoState();
  if(tpView == TPV_MAIN){
    bool wasMissing = (tpShownSt == FLEXBNO_ST_ABSENT || tpShownSt == FLEXBNO_ST_LOST);
    bool isMissing  = (st == FLEXBNO_ST_ABSENT || st == FLEXBNO_ST_LOST);
    bool wasReady   = (tpShownSt == FLEXBNO_ST_READY);
    bool isReady    = (st == FLEXBNO_ST_READY);
    if(wasMissing != isMissing || wasReady != isReady){
      tpAnimT0 = now;
      theftRender();
      return;
    }
    tpShownSt = st;
    if(tpModuleReady()){
      if(now - tpAnimMs >= TP_ANIM_FRAME_MS){ tpAnimMs = now; tpAnimFrame(); }
      // La linea de estado y el punto del sensor se refrescan una vez por
      // segundo, y SOLO su banda: no hay ningun motivo para repintar la
      // pantalla entera porque cambie una palabra.
      if(now - tpStatusMs >= 1000u){
        tpStatusMs = now;
        setBuf(fb);
        int c0 = gClipY0, c1 = gClipY1;
        gClipY0 = TP_SENSOR_Y; gClipY1 = TP_STATE_Y + TP_STATE_H - 1;
        fillRect(0, TP_SENSOR_Y, SCR_W, TP_SENSOR_H, TH_PAGE);
        fillRect(0, TP_STATE_Y, SCR_W, TP_STATE_H, TH_PAGE);
        tpDrawSensorCard();
        tpDrawStateCard();
        gClipY0 = c0; gClipY1 = c1;
        flxFlush(TP_SENSOR_Y, TP_SENSOR_Y + TP_SENSOR_H - 1);
        flxFlush(TP_STATE_Y,  TP_STATE_Y + TP_STATE_H - 1);
        tpSeedBand();
      }
      return;
    }
    if(tpModuleMissing()){
      // Solo la banda del grafico: el enlace punteado es lo unico vivo.
      if(now - tpAnimMs < 40u) return;
      tpAnimMs = now;
      int gx, gy, gw, gh; tpGfxGeom(gx, gy, gw, gh);
      setBuf(fb);
      int c0 = gClipY0, c1 = gClipY1;
      gClipY0 = gy; gClipY1 = gy + gh;
      fillRect(0, gy, SCR_W, gh + 1, TH_PAGE);
      dcDrawBnoModule(gx, gy, gw, gh, now - tpAnimT0);
      gClipY0 = c0; gClipY1 = c1;
      flxFlush(gy, gy + gh);
      return;
    }
    if(now - tpAnimMs >= 200u){ tpAnimMs = now; tpRenderProbing(); }
  }
}

// ---- Desplazamiento del historial ----
static void tpScrollTouch(){
  int maxS = tpHistMaxScroll();
  if(maxS <= 0){ tpDrag = false; return; }
  if(T.pressed){ tpDrag = false; tpDragY0 = T.y; tpDragS0 = tpScroll; return; }
  if(T.down){
    int dy = tpDragY0 - T.y;
    if(!tpDrag && abs(dy) > 8) tpDrag = true;
    if(tpDrag){
      int ns = tpDragS0 + dy;
      if(ns < 0) ns = 0;
      if(ns > maxS) ns = maxS;
      if(ns != tpScroll){ tpScroll = ns; tpRenderHist(); }
    }
  }
}

static void theftTick(){
  tpTickMs = millis();        // le dice a tpIdleGuard() que esta pantalla sigue viva

  // ---- Volver: chevron o arrastre desde el borde izquierdo ----
  if(T.tap && T.x < 64 && T.y < TP_HDR_H){ theftBack(); return; }

  if(tpView == TPV_HIST) tpScrollTouch();

  if(T.tap && !tpDrag){
    int id = tpHitTest(T.x, T.y);
    if(id == TPH_BACK){ theftBack(); return; }
    if(id == TPH_TOGGLE){
      // SIN MODULO NO SE ACTIVA. No se finge que hay monitorizacion.
      if(!tpModuleReady() && !tpOn){ theftRender(); return; }
      tpSetEnabled(!tpOn);
      theftRender();
      return;
    }
    if(id == TPH_HIST){ tpView = TPV_HIST; tpScroll = 0; theftRender(); touchDropAll(); return; }
    if(id == TPH_SENS){
      tpSetSens((uint8_t)((tpSens + 1) % 3));
      theftRender();
      return;
    }
    if(id == TPH_RETRY){
      // Re-sondeo EXPLICITO. El driver no reintenta solo desde ABSENT/LOST a
      // proposito: un cable suelto no puede convertir el bucle del sistema en
      // un sondeo I2C permanente.
      imuRetry(0);
      tpAnimT0 = millis();
      theftRender();
      return;
    }
    if(id >= TPH_ROW0 && tpView == TPV_HIST){
      tpHistSel = id - TPH_ROW0;
      tpView = TPV_DETAIL;
      theftRender();
      touchDropAll();
      return;
    }
  }
  if(T.released) tpDrag = false;

  tpScreenTick();
}

// #############################################################
// ##  AVISO SOBRE LA PANTALLA DE BLOQUEO
// ##  ------------------------------------------------------
// ##  No es una notificacion ni un modal: es una tarjeta que forma
// ##  parte de la pantalla de bloqueo mientras la proteccion este
// ##  disparada. Por eso la dibuja renderLock() y no un overlay --
// ##  sobrevive a cada repintado del reloj, al cambio de minuto y al
// ##  ir y volver de la pantalla de clave, sin un temporizador que la
// ##  pueda dejar a medias.
// ##
// ##  Se coloca ENTRE la barra de estado (que acaba en y=52) y el
// ##  panel del reloj (que empieza en y=198), asi que no se solapa con
// ##  nada de lo que el bloqueo ya dibuja.
// #############################################################
#define TPL_X   24
#define TPL_W   (SCR_W - 48)
#define TPL_Y   84
#define TPL_H   104

static void tpDrawLockBanner(){
  if(!tpLocked) return;
  uiWallSurface(TPL_X, TPL_Y, TPL_W, TPL_H, 24, TH_WALLSURF, 10);
  drawRoundRect(TPL_X, TPL_Y, TPL_W, TPL_H, 24, TH_DANGER);

  // Candado cerrado: el mismo glifo de la animacion, en pequeno.
  tpDrawPadlock(TPL_X + 40, TPL_Y + TPL_H / 2 - 4, 34, 1.0f, 255, TH_DANGER, TH_WALLSURF);

  int tx = TPL_X + 72, right = TPL_X + TPL_W - 16;
  int fs = uiFontFit(tpt(TPS_LOCKTITLE), right - tx, 2);
  drawTextClip(tx, TPL_Y + 14, tpt(TPS_LOCKTITLE), fs, TH_ONWALL, right);
  drawTextClip(tx, TPL_Y + 38, tpt(TPS_LOCKBODY), 1, TH_ONWALL2, right);
  drawTextClip(tx, TPL_Y + 54, tpt(TPS_LOCKHINT), 1, TH_ONWALL2, right);

  // LA HORA EXACTA del evento. Es el dato con el que el usuario reconstruye
  // lo que paso, asi que no es un adorno: si el reloj nunca se puso en hora,
  // se dice que no se sabe en vez de ensenar las 00:00:00 del 1 de enero.
  if(tpLockUtc > 1000000000u){
    char d[16], h[16], line[48];
    tpFmtDate(tpLockUtc, d, sizeof(d));
    tpFmtTime(tpLockUtc, h, sizeof(h));
    snprintf(line, sizeof(line), "%s \xC2\xB7 %s", d, h);
    drawTextClip(tx, TPL_Y + 74, line, 2, TH_ONWALL, right);
  } else {
    drawTextClip(tx, TPL_Y + 74, tpKindName(tpLockKind), 2, TH_ONWALL, right);
  }
}
