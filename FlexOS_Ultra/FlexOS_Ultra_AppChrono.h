// #############################################################
// ##  FLEX OS ULTRA  ·  CRONOMETRO  ·  app, capsula y tarjeta expandida
// ##  ----------------------------------------------------------
// ##  Cuatro piezas separadas con una sola responsabilidad cada una, y sus
// ##  dos enganches en loop().
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
#include "FlexOS_Ultra_Notif.h"   // eslabon anterior de la cadena

// #############################################################
// ##  CRONOMETRO  ·  APP, CAPSULA Y TARJETA EXPANDIDA
// ##  ------------------------------------------------------
// ##  Cuatro piezas SEPARADAS, cada una con una sola
// ##  responsabilidad, y una sola fuente de verdad para el
// ##  tiempo y para el formato:
// ##
// ##    1. LOGICA      cronoStart/Pause/Lap/Reset + cronoElapsed
// ##                   + cronoFmt. Nadie mas mide ni formatea.
// ##    2. APP         pestana Cronometro de Reloj (esfera de 60
// ##                   marcas, tabla de vueltas, dos botones).
// ##    3. CAPSULA     pildora violeta de la barra de estado.
// ##    4. TARJETA     overlay expandido al tocar la capsula.
// ##
// ##  Y dos enganches en loop(): cronoOverlayTouch() (tacto,
// ##  antes que nadie) y cronoCapsuleTick() (dibujo, al final).
// ##
// ##  PROPIEDAD DE PANTALLA: cada punto de entrada que dibuja
// ##  empieza comprobando flexOtaOwnsScreen(). Durante una
// ##  actualizacion la pantalla es del OTA en exclusiva y ni la
// ##  capsula ni la tarjeta tocan fb/bbuf. El cronometro sigue
// ##  contando -- solo deja de pintarse.
// #############################################################

// COLORES DEL CRONOMETRO: DEL TEMA, NO PROPIOS.
//
// Aqui habia tres literales -- un violeta, un gris de disco y un gris de
// tarjeta -- con un comentario que defendia que el violeta identificaba
// la actividad "igual que el rojo identifica una grabacion" y por tanto
// no debia seguir al tema. En la practica eso significaba que el
// cronometro era la unica pieza del sistema que ignoraba la
// personalizacion del usuario: con tema claro y acento verde, la tarjeta
// salia gris oscuro con botones violetas. Se ve en el video.
//
// Ahora salen de la paleta semantica como todo lo demas: el acento es el
// ACENTO DEL USUARIO y las superficies son las del tema. Los estados que
// SI tienen un significado propio -- "Detener" en rojo de peligro,
// "Parcial" apagado cuando no se puede -- siguen usando su token
// (TH_DANGER, TH_DIS), que es justo lo que esos tokens significan.
// ACENTO ACTIVO DEL SISTEMA, no el del tema. wallAccent() devuelve el color
// extraido del fondo cuando "Aplicar paleta al sistema" esta encendido y el
// acento del tema semantico cuando no: el cronometro sigue al usuario igual
// que el resto del sistema, sin saber cual de los dos manda.
#define CRONO_ACCENT   wallAccent()
#define CRONO_DISC     TH_SURF2    // disco central de la esfera
#define CRONO_CARDBG   TH_SURF     // superficie de la tarjeta expandida
#define CRONO_ONACC    TH_ONACC    // texto/glifo encima del acento

// ---- Capsula: geometria FIJA ----
// El ancho depende SOLO de la clase de formato (MM:SS o H:MM:SS), nunca de los
// digitos concretos. Eso es lo que permite repintarla encima de si misma cada
// segundo sin guardar ni restaurar el fondo: la pildora es opaca y ocupa
// exactamente los mismos pixeles, asi que se tapa a si misma.
#define CRONO_CAP_Y     12
#define CRONO_CAP_H     26
#define CRONO_CAP_ICON  18
#define CRONO_CAP_PADL   8
#define CRONO_CAP_GAP    6
#define CRONO_CAP_PADR  11

// ---- Tarjeta expandida: geometria FIJA ----
#define CRONO_CARD_X    20
#define CRONO_CARD_W    (SCR_W - 40)
#define CRONO_CARD_Y    30
#define CRONO_CARD_H    196
#define CRONO_CARD_RAD  42
#define CRONO_CARD_ANIM 190                              // ms de expansion/contraccion
#define CRONO_BAND_T    8                                // banda que la tarjeta puede tocar
#define CRONO_BAND_B    244
#define CRONO_BAND_H    (CRONO_BAND_B - CRONO_BAND_T)
// Sub-banda con lo UNICO que cambia por frame (tiempo grande + "Vuelta N").
// Repintar solo esto es lo que mantiene la tarjeta a 30 fps sin rehacer el
// vidrio (un blur de 440x196 por frame no cabe en el presupuesto).
#define CRONO_DYN_T     (CRONO_CARD_Y + 22)
#define CRONO_DYN_B     (CRONO_CARD_Y + 118)
#define CRONO_DYN_H     (CRONO_DYN_B - CRONO_DYN_T)

// =============================================================
//  1. LOGICA
// =============================================================

// UNICA fuente del tiempo transcurrido. Todo -- app, capsula y tarjeta --
// pregunta aqui. Resta sin signo: correcta aunque millis() desborde.
static uint32_t cronoElapsed(){
  if(gCronoSt == CRONO_RUN) return gCronoAccum + (uint32_t)(millis() - gCronoT0);
  return gCronoAccum;
}
// Tiempo de la vuelta en curso (desde la ultima marca de "Parcial").
static inline uint32_t cronoLapElapsed(){ return cronoElapsed() - gCronoLapBase; }
static inline bool cronoActive(){ return gCronoSt != CRONO_IDLE; }
// Numero visible de la vuelta en curso (la que aun no se ha marcado).
static inline int cronoCurLapNo(){ return (int)gCronoLap0 + (int)gCronoNLaps; }

// UNICO formateador. cent=true -> centesimas (dentro de la app y en la
// tarjeta); cent=false -> solo h/m/s (capsula compacta). Reserva 16 bytes.
static void cronoFmt(char* out, size_t n, uint32_t ms, bool cent){
  unsigned long h  = (unsigned long)( ms / 3600000UL);
  unsigned long m  = (unsigned long)((ms /   60000UL) % 60UL);
  unsigned long s  = (unsigned long)((ms /    1000UL) % 60UL);
  unsigned long cc = (unsigned long)((ms %    1000UL) /   10UL);
  if(h > 0){
    if(cent) snprintf(out, n, "%lu:%02lu:%02lu.%02lu", h, m, s, cc);
    else     snprintf(out, n, "%lu:%02lu:%02lu",       h, m, s);
  } else {
    if(cent) snprintf(out, n, "%02lu:%02lu.%02lu", m, s, cc);
    else     snprintf(out, n, "%02lu:%02lu",       m, s);
  }
}

// Mejor y peor vuelta. Se recalcula SOLO al anadir una vuelta (no por frame).
// Con menos de tres vueltas no se colorea nada: comparar dos parciales -- uno
// "el mejor" y el otro "el peor" -- no informa de nada.
static void cronoRecalcExtremes(){
  gCronoBest = gCronoWorst = -1;
  if(gCronoNLaps < 3) return;
  int b = 0, w = 0;
  for(int i = 1; i < (int)gCronoNLaps; i++){
    if(gCronoLaps[i].split < gCronoLaps[b].split) b = i;
    if(gCronoLaps[i].split > gCronoLaps[w].split) w = i;
  }
  if(gCronoLaps[b].split == gCronoLaps[w].split) return;   // todas iguales: sin extremos
  gCronoBest  = (int8_t)b;
  gCronoWorst = (int8_t)w;
}

static void cronoStart(){
  if(gCronoSt == CRONO_RUN) return;
  if(gCronoSt == CRONO_IDLE){ gCronoAccum = 0; gCronoLapBase = 0; }
  gCronoT0 = millis();                       // se reancla en CADA reanudacion
  gCronoSt = CRONO_RUN;
  gCronoBtnDirty = gCronoDialDirty = gCronoBarDirty = true;
}
static void cronoPause(){
  if(gCronoSt != CRONO_RUN) return;
  gCronoAccum += (uint32_t)(millis() - gCronoT0);   // consolida lo corrido
  gCronoSt = CRONO_PAUSE;
  gCronoBtnDirty = gCronoDialDirty = gCronoBarDirty = true;
}
static void cronoReset(){
  gCronoSt = CRONO_IDLE;
  gCronoAccum = 0; gCronoT0 = 0; gCronoLapBase = 0;
  gCronoNLaps = 0; gCronoLap0 = 1;
  gCronoBest = gCronoWorst = -1;
  gCronoBtnDirty = gCronoDialDirty = gCronoLapsDirty = gCronoBarDirty = true;
}
// "Parcial": cierra la vuelta en curso y abre la siguiente.
static void cronoLapMark(){
  if(gCronoSt != CRONO_RUN) return;
  uint32_t tot = cronoElapsed();
  if(gCronoNLaps >= CRONO_MAX_LAPS){
    // Lista llena: cae la MAS ANTIGUA y se compacta. gCronoLap0 sube, asi que
    // los numeros que ve el usuario siguen creciendo (no se reciclan).
    for(int i = 0; i < CRONO_MAX_LAPS - 1; i++) gCronoLaps[i] = gCronoLaps[i + 1];
    gCronoNLaps = CRONO_MAX_LAPS - 1;
    if(gCronoLap0 < 0xFFFFu) gCronoLap0++;
  }
  gCronoLaps[gCronoNLaps].split = tot - gCronoLapBase;
  gCronoLaps[gCronoNLaps].total = tot;
  gCronoNLaps++;
  gCronoLapBase = tot;
  cronoRecalcExtremes();
  gCronoLapsDirty = true;
}
// Accion del boton izquierdo segun el estado (Parcial / Reiniciar).
static void cronoActionLeft(){
  if(gCronoSt == CRONO_RUN)   { cronoLapMark(); return; }
  if(gCronoSt == CRONO_PAUSE) { cronoReset();   return; }
  // CRONO_IDLE: no hay nada que marcar ni que reiniciar.
}
// Accion del boton derecho (Iniciar / Detener / Continuar).
static void cronoActionRight(){
  if(gCronoSt == CRONO_RUN) cronoPause();
  else                      cronoStart();
}
static inline int cronoLabelLeft(){  return (gCronoSt == CRONO_PAUSE) ? S_CRN_BRESET : S_CRN_BLAP; }
static inline int cronoLabelRight(){
  if(gCronoSt == CRONO_RUN)   return S_CRN_BSTOP;
  if(gCronoSt == CRONO_PAUSE) return S_CONTINUE;
  return S_CRN_BSTART;
}

// La app y la tarjeta se llaman entre si (la tarjeta abre Reloj, y abrir Reloj
// cierra la tarjeta), asi que una de las dos direcciones necesita declaracion
// adelantada.
static void cronoCardCloseNow();

// Ease-out cubica compartida por las dos animaciones del modulo.
static inline float cronoEase(float p){ float q = 1.0f - p; return 1.0f - q * q * q; }
// Interpolacion lineal entera (para el morfeo capsula -> tarjeta).
static inline int cronoLerp(int a, int b, float p){ return a + (int)((b - a) * p + 0.5f); }

// =============================================================
//  2. ICONO VECTORIAL DE CRONOMETRO  (recurso propio de FlexOS)
// =============================================================
// Un solo dibujante para los dos sitios donde aparece el glifo: la capsula
// (r pequeno) y el cuadrado violeta de la tarjeta (r grande). Sin imagenes ni
// recursos externos: circulo, corona, boton y aguja con las primitivas del
// motor.
static void cronoGlyph(int cx, int cy, int r, uint16_t col, float th){
  if(r < 4) r = 4;
  // Caja + corona: el pulsador de arriba y las dos "orejas" del cronometro.
  strokeSegAA((float)(cx - r * 0.42f), (float)(cy - r - r * 0.42f),
              (float)(cx + r * 0.42f), (float)(cy - r - r * 0.42f), th, col);
  strokeSegAA((float)cx, (float)(cy - r - r * 0.55f), (float)cx, (float)(cy - r * 0.92f), th, col);
  // Esfera
  int ring = (int)(th * 2.0f + 0.5f); if(ring < 2) ring = 2;
  fillRing(cx, cy, r, ring, col);
  // Aguja hacia las 2 en punto (la pose de la referencia)
  strokeSegAA((float)cx, (float)cy,
              (float)(cx + r * 0.52f), (float)(cy - r * 0.46f), th, col);
}

// =============================================================
//  3. CAPSULA COMPACTA DE LA BARRA DE ESTADO
// =============================================================

// Ancho RESERVADO. Depende solo de la clase de formato, no de los digitos:
// ver la nota de geometria fija arriba.
static int cronoCapsuleW(){
  const char* tmpl = (cronoElapsed() >= 3600000UL) ? "0:00:00" : "00:00";
  return CRONO_CAP_PADL + CRONO_CAP_ICON + CRONO_CAP_GAP + textW(tmpl, 2) + CRONO_CAP_PADR;
}
// Limite derecho: la capsula NUNCA puede pisar el Wi-Fi ni la bateria.
static int cronoCapsuleRight(){
  return SCR_W - 66 - 12;                                      // borde izq. del Wi-Fi
}
// Pinta la pildora en el buffer ACTIVO, en (x, CRONO_CAP_Y). Opaca a proposito.
// EL COLOR ES EL DEL USUARIO. Antes era CRONO_ACCENT (= TH_PRIM), o sea el
// acento del TEMA: con "Aplicar paleta al sistema" encendido, la capsula del
// cronometro se quedaba azul mientras el resto del sistema seguia el color
// extraido del fondo. wallAccent() es la fuente de verdad del acento activo y
// resuelve los dos casos sin que este dibujo tenga que saber cual manda.
static void cronoCapsuleDraw(int x){
  int w = cronoCapsuleW(), h = CRONO_CAP_H, y = CRONO_CAP_Y;
  uiSurface(x, y, w, h, h / 2, UIS_ACCENT);
  int ir = CRONO_CAP_ICON / 2;
  cronoGlyph(x + CRONO_CAP_PADL + ir, y + h / 2 + 1, ir - 2, CRONO_ONACC, 1.5f);
  char b[16]; cronoFmt(b, sizeof(b), cronoElapsed(), false);
  int tx = x + CRONO_CAP_PADL + CRONO_CAP_ICON + CRONO_CAP_GAP;
  drawText(tx, y + (h - uiLineH(2)) / 2 + 1, b, 2, CRONO_ONACC);
}
// HORA + CAPSULA de la barra de estado. UNICO punto donde se decide esa
// geometria: lo llaman el escritorio (renderHome, sobre homeBuf) y el marco de
// las apps (appDrawChrome, sobre fb).
//
// PRIORIDAD cuando no cabe todo: mandan la capsula, la conectividad y la
// bateria. Lo unico que se sacrifica es la hora normal, que pasa a no
// dibujarse y le cede el hueco a la capsula (el usuario ya ve la hora en el
// bloqueo, en el widget y en la app Reloj).
static void cronoBarClock(int y, uint16_t col){
  char cs[12]; clkStrBar(cs, sizeof(cs));
  if(!cronoActive()){
    drawText(20, y, cs, 2, col);
    gCronoCapOn = false;
    return;
  }
  int capW = cronoCapsuleW();
  int cx   = 20 + textW(cs, 2) + 12;
  bool showClock = true;
  if(cx + capW > cronoCapsuleRight()){ cx = 20; showClock = false; }   // manda la capsula
  if(showClock) drawText(20, y, cs, 2, col);
  cronoCapsuleDraw(cx);
  gCronoCapX       = cx;
  gCronoCapWDrawn  = capW;
  gCronoCapOn      = true;
  gCronoCapSec     = cronoElapsed() / 1000UL;
  gCronoCapStDrawn = (uint8_t)gCronoSt;
  gCronoBarDirty   = false;
}

// Superficie de barra de estado valida ahora mismo:
//   1 = escritorio (hay que pintar en homeBuf Y en fb)
//   2 = marco estandar de app (solo fb; el fondo es plano WIN_BG)
//   0 = ninguna (bloqueo, Modo PC, kiosco, cortina abierta, edicion, DeX...)
static int cronoBarSurface(){
  if(gSuspOn || gLand || gHosted) return 0;
  if(KIOSK_ON && kioskOn) return 0;
  if(gState == ST_HOME && qsPanelY == 0 && !qsAnimOn && !editMode) return 1;
  if(gState == ST_APP  && !(APP_REG[gAppId].flags & APP_CUSTOM_HEADER)) return 2;
  return 0;
}

// Repinta SOLO la pildora, sin tocar nada mas. Se apoya en que es opaca y de
// ancho fijo: dibujarla encima de si misma la borra y la reescribe.
static void cronoCapsuleStamp(int surface){
  uint16_t* prev = gBuf;
  int c0 = gClipY0, c1 = gClipY1, cx0 = gClipX0, cx1 = gClipX1;
  gClipY0 = 0; gClipY1 = SCR_H - 1; gClipX0 = 0; gClipX1 = SCR_W - 1;
  // El escritorio se sirve de homeBuf (la cortina y la isla lo releen), asi que
  // la capsula tiene que quedar en los DOS sitios o al primer repintado global
  // volveria la version vieja.
  if(surface == 1){ setBuf(homeBuf); cronoCapsuleDraw(gCronoCapX); }
  // El candado es el mismo que usa todo el compositor: mientras la DMA sube una
  // banda, nadie reescribe fb por debajo.
  fbLock();
  setBuf(fb);
  cronoCapsuleDraw(gCronoCapX);
  fbUnlock();
  gBuf = prev;
  gClipY0 = c0; gClipY1 = c1; gClipX0 = cx0; gClipX1 = cx1;
  flxFlush(CRONO_CAP_Y - 1, CRONO_CAP_Y + CRONO_CAP_H + 1);   // toma el candado por su cuenta
}

// Rehace la barra ENTERA de la superficie activa. Se usa solo cuando la
// capsula APARECE, DESAPARECE o cambia de ancho (al pasar de 1 h): son los
// unicos casos en que la geometria de la barra deja de ser la de antes.
static void cronoBarRebuild(int surface){
  if(surface == 1){ renderHome(); showHome(); return; }
  if(surface == 2){
    uint16_t* prev = gBuf;
    int c0 = gClipY0, c1 = gClipY1, cx0 = gClipX0, cx1 = gClipX1;
    gClipY0 = 0; gClipY1 = SCR_H - 1; gClipX0 = 0; gClipX1 = SCR_W - 1;
    fbLock();
    setBuf(fb);
    // El fondo del marco de app es plano: borrar es un rectangulo y ya. Se
    // corta en 46 px a proposito: la cabecera de la app (chevron y titulo)
    // empieza en y=50 y NO es nuestra.
    fillRect(0, 0, SCR_W, 46, WIN_BG);
    appDrawChrome(gAppId);
    fbUnlock();
    gBuf = prev;
    gClipY0 = c0; gClipY1 = c1; gClipX0 = cx0; gClipX1 = cx1;
    flxFlush(0, 46);
  }
}

// Enganche de loop(): mantiene la capsula al dia SIN repintar por frame.
static void cronoCapsuleTick(){
  if(flexOtaOwnsScreen()) return;             // la pantalla es del OTA en exclusiva
  if(gCronoCard != CC_HIDDEN) return;         // la tarjeta ya muestra el tiempo
  int surface = cronoBarSurface();
  bool want = cronoActive() && surface != 0;

  if(!want){
    // Debe desaparecer: solo hay trabajo si estaba pintada y la superficie
    // sigue siendo dibujable (si no, ya se rehara al volver a ella).
    if(gCronoCapOn && surface != 0){ cronoBarRebuild(surface); }
    else if(surface == 0) gCronoBarDirty = true;   // al volver, barra nueva
    return;
  }
  uint32_t sec = cronoElapsed() / 1000UL;
  int      w   = cronoCapsuleW();
  // Aparicion, cambio de ancho o barra invalidada -> barra completa.
  if(!gCronoCapOn || gCronoBarDirty || w != gCronoCapWDrawn){ cronoBarRebuild(surface); return; }
  // Solo el segundo (o el estado) cambio -> pildora y nada mas.
  if(sec == gCronoCapSec && (uint8_t)gCronoSt == gCronoCapStDrawn) return;
  gCronoCapSec     = sec;
  gCronoCapStDrawn = (uint8_t)gCronoSt;
  cronoCapsuleStamp(surface);
}

// =============================================================
//  4. APP: PESTANA CRONOMETRO
// =============================================================
// Geometria calculada UNA vez por repintado y guardada aqui: la esfera se
// redibuja ~30 veces por segundo y no puede estar recalculando la maqueta.
// El tipo no aparece en ninguna firma, asi que puede vivir aqui abajo.
struct CronoLayout {
  int bx, by, bw, bh;      // cuerpo de la pestana
  int cx, cy;              // centro de la esfera
  int rTick, rDisc;        // radio de las marcas y del disco central
  int dialY0, dialY1;      // banda vertical que ocupa la esfera
  int tblY, rowH, rows;    // tabla de vueltas
  int colLap, colSplit, colTot;
  int btnCy, btnR, btnLx, btnRx;
  bool hasTable;
};
static CronoLayout gCL;

static void cronoLayout(){
  relojBody(gCL.bx, gCL.by, gCL.bw, gCL.bh);
  int pad = uiPad();
  // Botones: anclados abajo. El radio se acota para que NUNCA lleguen a la
  // zona del boton "atras" de la barra de navegacion (appTick lo mira antes
  // que el tick de la app, y un toque compartido cerraria Reloj).
  gCL.btnR = gCL.bw / 5; if(gCL.btnR > 48) gCL.btnR = 48; if(gCL.btnR < 22) gCL.btnR = 22;
  gCL.btnCy = gCL.by + gCL.bh - gCL.btnR - pad;
  gCL.btnLx = gCL.bx + gCL.bw / 4;
  gCL.btnRx = gCL.bx + gCL.bw * 3 / 4;
  // Esfera: la mitad superior del cuerpo, acotada por el ancho.
  int maxR = gCL.bw / 2 - pad;
  int byH  = (gCL.bh - 2 * gCL.btnR - 2 * pad) / 2;
  gCL.rTick = maxR < byH ? maxR : byH;
  if(gCL.rTick > 150) gCL.rTick = 150;
  if(gCL.rTick < 40)  gCL.rTick = 40;
  gCL.rDisc = gCL.rTick * 78 / 100;
  gCL.cx = gCL.bx + gCL.bw / 2;
  gCL.cy = gCL.by + gCL.rTick + pad;
  gCL.dialY0 = gCL.cy - gCL.rTick - 3;
  gCL.dialY1 = gCL.cy + gCL.rTick + 3;
  // Tabla de vueltas: lo que quede entre la esfera y los botones.
  gCL.tblY = gCL.dialY1 + pad;
  gCL.rowH = 32;
  int free = (gCL.btnCy - gCL.btnR - pad) - (gCL.tblY + 30);
  gCL.rows = free / gCL.rowH;
  if(gCL.rows > 4) gCL.rows = 4;
  if(gCL.rows < 0) gCL.rows = 0;
  gCL.hasTable = (gCL.rows > 0 && gCL.bw >= 260);
  gCL.colLap   = gCL.bx + gCL.bw * 15 / 100;
  gCL.colSplit = gCL.bx + gCL.bw * 50 / 100;
  gCL.colTot   = gCL.bx + gCL.bw * 84 / 100;
}

// Tamano de fuente mas grande (hasta maxSize) que cabe en maxw. uiFontFit
// tapa en 5 y el tiempo central de la esfera necesita mas.
static int cronoFitBig(const char* s, int maxw, int maxSize){
  int fs = maxSize;
  while(fs > 1 && textW(s, fs) > maxw) fs--;
  return fs;
}

// ESFERA. Se compone SIEMPRE en bbuf y se publica con present(): DMA2D
// nunca ve un fb a medio pintar (mismo patron que la isla de notificaciones).
static void cronoDrawDial(){
  int y0 = gCL.dialY0, y1 = gCL.dialY1;
  if(y0 < gCL.by) y0 = gCL.by;
  if(y1 > gCL.by + gCL.bh - 1) y1 = gCL.by + gCL.bh - 1;
  if(y1 <= y0) return;
  // Recorte completo antes de componer (por si alguien lo dejo estrecho), mismo
  // resguardo que toma notifTick.
  int c0 = gClipY0, c1 = gClipY1, cx0 = gClipX0, cx1 = gClipX1;
  gClipY0 = 0; gClipY1 = SCR_H - 1; gClipX0 = 0; gClipX1 = SCR_W - 1;
  // Componer en bbuf y publicar con present() es lo correcto a pantalla
  // completa: DMA2D nunca ve un fb a medio pintar. Pero present() copia
  // FILAS ENTERAS, y dentro de una ventana de DeX (o en horizontal) el lienzo
  // de la app no ocupa la fila entera: se acabaria volcando lo que hubiera en
  // bbuf fuera del area util. Ahi se dibuja directo, que es lo que hacen las
  // demas apps hospedadas.
  bool viaBack = (!gLand && !gHosted);
  setBuf(viaBack ? bbuf : fb);
  fillRect(gCL.bx, y0, gCL.bw, y1 - y0 + 1, WIN_BG);
  // 60 marcas: las de cada 5 mas largas, mas gruesas y mas claras.
  for(int i = 0; i < 60; i++){
    bool major = (i % 5 == 0);
    float a  = (i * 6.0f - 90.0f) * 0.0174532925f;
    float ca = cosf(a), sa = sinf(a);
    float rO = (float)gCL.rTick;
    float rI = rO - (major ? gCL.rTick * 0.13f : gCL.rTick * 0.07f);
    strokeSegAA(gCL.cx + rI * ca, gCL.cy + rI * sa,
                gCL.cx + rO * ca, gCL.cy + rO * sa,
                major ? 1.9f : 1.1f, major ? TH_TXT2 : TH_MUTE);
  }
  // Disco central
  fillCircleAA((float)gCL.cx, (float)gCL.cy, (float)gCL.rDisc, CRONO_DISC);
  // Agujas. La violeta marca el TOTAL (una vuelta de esfera por minuto); la
  // gris, la vuelta en curso. Ambas con una cola corta detras del centro,
  // como en la referencia.
  uint32_t ms = cronoElapsed();
  float hLen  = gCL.rTick * 0.93f, tail = gCL.rTick * 0.14f;
  float aLap  = ((cronoLapElapsed() % 60000UL) / 60000.0f) * 360.0f - 90.0f;
  float aTot  = ((ms                % 60000UL) / 60000.0f) * 360.0f - 90.0f;
  float rl = aLap * 0.0174532925f, rt = aTot * 0.0174532925f;
  strokeSegAA(gCL.cx - tail * cosf(rl), gCL.cy - tail * sinf(rl),
              gCL.cx + hLen * cosf(rl), gCL.cy + hLen * sinf(rl), 1.4f, TH_TXT2);
  strokeSegAA(gCL.cx - tail * cosf(rt), gCL.cy - tail * sinf(rt),
              gCL.cx + hLen * cosf(rt), gCL.cy + hLen * sinf(rt), 2.3f, CRONO_ACCENT);
  fillCircleAA((float)gCL.cx, (float)gCL.cy, gCL.rTick * 0.055f + 3.0f, CRONO_ACCENT);
  // Tiempo central MM:SS.cc
  char b[16]; cronoFmt(b, sizeof(b), ms, true);
  int fs = cronoFitBig(b, gCL.rDisc * 2 - 18, 7);
  drawTextC(gCL.cx, gCL.cy - (int)(2.3f * fs), b, fs, TH_TXT);
  if(viaBack) present(y0, y1);   // volcado atomico bbuf -> fb de una banda terminada
  else        flxFlush(y0, y1);
  gClipY0 = c0; gClipY1 = c1; gClipX0 = cx0; gClipX1 = cx1;
  setBuf(fb);               // el destino vuelve a la pantalla: nadie hereda bbuf
}

// TABLA DE VUELTAS. Se repinta SOLO cuando cambia la lista (gCronoLapsDirty),
// no por frame. Las mas recientes arriba.
static void cronoDrawLaps(){
  if(!gCL.hasTable) return;
  setBuf(fb);
  int h = 26 + gCL.rows * gCL.rowH;
  fillRect(gCL.bx, gCL.tblY, gCL.bw, h, WIN_BG);
  // Cabeceras
  int fsH = uiFontFit(t(S_CRN_SPLIT), gCL.bw / 3 - 8, 2);
  drawTextC(gCL.colLap,   gCL.tblY, t(S_CRN_LAP),   fsH, TH_TXT2);
  drawTextC(gCL.colSplit, gCL.tblY, t(S_CRN_SPLIT), fsH, TH_TXT2);
  drawTextC(gCL.colTot,   gCL.tblY, t(S_CRN_TOTAL), fsH, TH_TXT2);
  int yLine = gCL.tblY + uiLineH(fsH) + 6;
  fillRect(gCL.bx + uiPad(), yLine, gCL.bw - 2 * uiPad(), 1, TH_DIV);
  int y = yLine + 8;
  if(gCronoNLaps == 0){
    drawTextC(gCL.bx + gCL.bw / 2, y + 6, t(S_CRN_NOLAPS), 2, TH_MUTE);
    return;
  }
  // Recorrido de la MAS RECIENTE hacia atras, acotado a las filas que caben.
  // i siempre en [0, gCronoNLaps): la resta se hace con enteros con signo y
  // el bucle corta en cuanto se sale por abajo.
  for(int r = 0; r < gCL.rows; r++){
    int i = (int)gCronoNLaps - 1 - r;
    if(i < 0) break;
    uint16_t col = TH_TXT;
    if(i == (int)gCronoBest)  col = CRONO_ACCENT;    // mejor vuelta
    if(i == (int)gCronoWorst) col = TH_DANGER;       // peor vuelta
    char no[8];  snprintf(no, sizeof(no), "%02d", (int)(gCronoLap0 + i));
    char sp[16]; cronoFmt(sp, sizeof(sp), gCronoLaps[i].split, true);
    char to[16]; cronoFmt(to, sizeof(to), gCronoLaps[i].total, true);
    drawTextC(gCL.colLap,   y, no, 2, col);
    drawTextC(gCL.colSplit, y, sp, 2, col);
    drawTextC(gCL.colTot,   y, to, 2, col);
    y += gCL.rowH;
  }
}

// Dos botones circulares grandes, como la referencia.
static void cronoDrawButtons(){
  setBuf(fb);
  int r = gCL.btnR, cy = gCL.btnCy;
  fillRect(gCL.bx, cy - r - 2, gCL.bw, 2 * r + 4, WIN_BG);
  bool leftOn = (gCronoSt != CRONO_IDLE);
  // Izquierdo: gris (Parcial / Reinic.). Apagado cuando no hay nada que hacer.
  fillCircle(gCL.btnLx, cy, r, thCard());
  int fsL = uiFontFit(t(cronoLabelLeft()), 2 * r - 12, 3);
  drawTextC(gCL.btnLx, cy - uiLineH(fsL) / 2, t(cronoLabelLeft()), fsL,
            leftOn ? TH_TXT : TH_DIS);
  // Derecho: rojo de peligro al detener, acento del usuario al
  // iniciar/continuar. El rojo SI es propio -- significa "esto para lo que
  // esta corriendo" --; el otro sale del tema, como el resto del sistema.
  bool stop = (gCronoSt == CRONO_RUN);
  fillCircleA(gCL.btnRx, cy, r, stop ? TH_DANGER : CRONO_ACCENT, 70);
  drawCircle(gCL.btnRx, cy, r, stop ? TH_DANGER : CRONO_ACCENT);
  int fsR = uiFontFit(t(cronoLabelRight()), 2 * r - 12, 3);
  drawTextC(gCL.btnRx, cy - uiLineH(fsR) / 2, t(cronoLabelRight()), fsR,
            stop ? TH_DANGER : CRONO_ACCENT);
}

// Repintado COMPLETO de la pestana (lo llama el contenedor de Reloj, que ya
// limpio el lienzo y hace el flush final: aqui no se vuelca nada mas).
static void cronoTabRender(){
  cronoLayout();
  cronoDrawLaps();
  cronoDrawButtons();
  gCronoLapsDirty = gCronoBtnDirty = false;
  cronoDrawDial();                       // deja la esfera ya visible en este mismo frame
  gCronoDialDirty = false;
}

// Toque de la pestana: solo los dos botones. Cae DESPUES del control de
// pestanas y del chequeo de "atras" del framework, y consume T.tap para que
// un toque en un boton no se propague a nada mas.
static bool cronoTabTouch(){
  if(!T.tap) return false;
  int r = gCL.btnR + 6, cy = gCL.btnCy;
  int dyl = T.y - cy;
  if(abs(dyl) > r) return false;
  int dxl = T.x - gCL.btnLx, dxr = T.x - gCL.btnRx;
  if(dxl * dxl + dyl * dyl <= r * r){
    cronoActionLeft();  T.tap = false; T.pressed = false; return true;
  }
  if(dxr * dxr + dyl * dyl <= r * r){
    cronoActionRight(); T.tap = false; T.pressed = false; return true;
  }
  return false;
}

// Tick de la pestana. Cada zona se repinta por su cuenta y solo cuando le toca.
static void cronoTabTick(){
  cronoTabTouch();
  if(gCronoBtnDirty){  cronoDrawButtons(); flxFlush(gCL.btnCy - gCL.btnR - 2, gCL.btnCy + gCL.btnR + 2); gCronoBtnDirty = false; }
  if(gCronoLapsDirty){ cronoDrawLaps();    flxFlush(gCL.tblY, gCL.tblY + 26 + gCL.rows * gCL.rowH);      gCronoLapsDirty = false; }
  // Esfera: ~30 fps mientras corre. Parada o inactiva no se repinta salvo que
  // alguien la haya invalidado (cambio de estado), y entonces una vez basta.
  static uint32_t last = 0;
  uint32_t now = millis();
  if(gCronoSt != CRONO_RUN && !gCronoDialDirty) return;
  if((uint32_t)(now - last) < 33) return;
  last = now;
  cronoDrawDial();
  gCronoDialDirty = false;
}

// Abre (o trae al frente) la pestana Cronometro de Reloj.
static void cronoOpenApp(){
  // La tarjeta no puede sobrevivir a un cambio de pantalla: su banda capturada
  // dejaria de describir lo que hay debajo (mismo criterio que qsForceClose).
  cronoCardCloseNow();
  if(gState == ST_APP && gAppId == IC_RELOJ){
    if(gRelojTab != 1){ gRelojTab = 1; appRelojRender(); }
    return;
  }
  gRelojTab = 1;
  if(gState == ST_APP){ appClose(); }        // vuelve al escritorio con su animacion
  if(gState != ST_HOME) return;
  // FASE 3: este atajo NO puede saltarse el candado de la app. Misma via que
  // el escritorio y la caja de aplicaciones: se verifica y luego se abre.
  if(APPLOCK_ON && appLockGet(IC_RELOJ) && gLockType > 0){
    lsuStartVerifyFor(LSU_AFTER_OPENAPP, IC_RELOJ);
    return;
  }
  enterApp(IC_RELOJ);                        // enterApp ya respeta kiosco y DeX
}

// =============================================================
//  5. TARJETA EXPANDIDA  (overlay propio, NO es una notificacion)
// =============================================================
// NO se encola en gNotifs[], no usa NOTIF_HOLD_MS y no caduca sola: se cierra
// solo cuando el usuario la cierra.
//
// COMPOSICION Y FONDO. notifRestoreBg() no vale aqui: restaura desde homeBuf,
// y esta tarjeta tambien tiene que poder abrirse encima de cualquier app. Lo
// que se hace es capturar la BANDA REAL de fb al abrir y restaurarla al
// cerrar, asi que el fondo siempre es el que habia debajo -- nunca el
// escritorio pegado sobre otra pantalla.
//
// Mientras esta abierta, loop() le cede la pantalla (igual que a la cortina):
// nadie repinta por debajo, asi que la banda capturada no se queda obsoleta.

static bool cronoCardAlloc(){
  if(gCronoCardBak && gCronoCardCache) return true;
  // Reserva UNICA y permanente: no se libera al cerrar, para no estar pidiendo
  // y soltando PSRAM cada vez que el usuario abre la tarjeta.
  if(!gCronoCardBak)
    gCronoCardBak = (uint16_t*)heap_caps_aligned_alloc(64, (size_t)SCR_W * CRONO_BAND_H * 2,
                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!gCronoCardCache)
    gCronoCardCache = (uint16_t*)heap_caps_aligned_alloc(64, (size_t)SCR_W * CRONO_DYN_H * 2,
                                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return gCronoCardBak && gCronoCardCache;
}
static inline bool cronoCardVisible(){ return gCronoCard != CC_HIDDEN; }

// Textos que cambian por frame: tiempo grande con centesimas y "Vuelta N".
static void cronoCardDynText(uint8_t alpha){
  int ix = CRONO_CARD_X + 24, is = 62;
  int tx = ix + is + 20;
  char b[16]; cronoFmt(b, sizeof(b), cronoElapsed(), true);
  int fs = cronoFitBig(b, (CRONO_CARD_X + CRONO_CARD_W - 20) - tx, 7);
  drawTextA(tx, CRONO_CARD_Y + 30, b, fs, TH_TXT, alpha);
  char lap[24];
  snprintf(lap, sizeof(lap), "%s %d", t(S_CRN_LAP), cronoCurLapNo());
  drawTextA(tx, CRONO_CARD_Y + 82, lap, 2, TH_TXT2, alpha);
}
// Todo lo que NO cambia por frame: icono, separador y etiquetas de los botones.
static void cronoCardStatic(uint8_t alpha){
  int ix = CRONO_CARD_X + 24, iy = CRONO_CARD_Y + 26, is = 62;
  uiRectA(ix, iy, is, is, 20, CRONO_ACCENT, alpha);
  // El glifo es opaco (strokeSegAA/fillRing no toman alfa): entra solo cuando
  // el cuadrado violeta ya esta casi solido, o se verian trazos blancos
  // flotando sobre un fondo aun transparente.
  if(alpha > 200) cronoGlyph(ix + is / 2, iy + is / 2 + 2, is / 4, CRONO_ONACC, 2.2f);
  // Separador vertical entre los dos botones
  int by0 = CRONO_CARD_Y + 138, by1 = CRONO_CARD_Y + 178;
  uiRectA(SCR_W / 2 - 1, by0, 2, by1 - by0, 1, TH_DIV, alpha);
  int lx = CRONO_CARD_X + CRONO_CARD_W / 4, rx = CRONO_CARD_X + CRONO_CARD_W * 3 / 4;
  uiTextC(lx, CRONO_CARD_Y + 148, t(cronoLabelLeft()),  3,
          (gCronoSt == CRONO_IDLE) ? TH_DIS : TH_TXT, alpha);
  uiTextC(rx, CRONO_CARD_Y + 148, t(cronoLabelRight()), 3,
          (gCronoSt == CRONO_RUN) ? TH_DANGER : CRONO_ACCENT, alpha);
}

// Compone un frame COMPLETO de la tarjeta en bbuf y lo publica.
//   p = 0 -> pildora de la capsula · p = 1 -> tarjeta entera
//   cacheBg = true -> guarda la sub-banda dinamica ANTES de los textos, para
//   que los frames siguientes solo tengan que reescribir esos textos.
static void cronoCardCompose(float p, bool cacheBg){
  // Recorte completo: la tarjeta se abre encima de CUALQUIER app, y alguna
  // pudo dejar una banda estrecha activa para su lista con scroll.
  gClipY0 = 0; gClipY1 = SCR_H - 1; gClipX0 = 0; gClipX1 = SCR_W - 1;
  setBuf(bbuf);
  memcpy(bbuf + (size_t)CRONO_BAND_T * SCR_W, gCronoCardBak,
         (size_t)SCR_W * CRONO_BAND_H * 2);
  // LA CAPSULA DE LA BARRA, VIVA. gCronoCardBak es una foto de la banda
  // tomada al abrir la tarjeta, y en esa foto sale la pildora con la hora
  // que marcaba ENTONCES. Como la tarjeta empieza en y=30 y la pildora
  // vive en y=12..38, sus primeras filas se quedan a la vista: en el
  // video se lee "00:07" en la barra mientras la tarjeta dice "00:18.56".
  // Repintarla aqui la pone al dia -- es opaca y ocupa exactamente los
  // mismos pixeles, asi que se tapa a si misma sin guardar nada.
  if(gCronoCapOn) cronoCapsuleDraw(gCronoCapX);
  int capW = gCronoCapWDrawn > 0 ? gCronoCapWDrawn : cronoCapsuleW();
  int x = cronoLerp(gCronoCapX,   CRONO_CARD_X,   p);
  int y = cronoLerp(CRONO_CAP_Y,  CRONO_CARD_Y,   p);
  int w = cronoLerp(capW,         CRONO_CARD_W,   p);
  int h = cronoLerp(CRONO_CAP_H,  CRONO_CARD_H,   p);
  int r = cronoLerp(CRONO_CAP_H / 2, CRONO_CARD_RAD, p);
  // LA MISMA SUPERFICIE EN LOS DOS ESTADOS Y EN LA TRANSICION. Antes solo el
  // cuadro en reposo (p >= 1) pagaba el vidrio y los de animacion eran rellenos
  // PLANOS: con Liquid Glass activado, la barra desplegada del cronometro se
  // veia plana y desvanecida mientras crecia -- el fallo que se reporto. Ahora
  // uiSurfaceA resuelve material, tinte y borde una sola vez para colapsado,
  // desplegado y la transicion entre ambos; con la banda pre-desenfocada que
  // preparo cronoCardOpen, el vidrio cuesta lo mismo que costaba el plano.
  uiSurfaceA(x, y, w, h, r, UIS_ELEVATED, uiGlass ? 200 : 240);
  // Morfeo de color desde el acento de la capsula hasta la superficie de la
  // tarjeta: sigue siendo funcion de p, y ahora el acento es el del USUARIO.
  if(p < 1.0f){
    uint8_t va = (uint8_t)(255.0f * (1.0f - p));
    if(va) fillRoundRectA(x, y, w, h, r, CRONO_ACCENT, va);
  }
  // El contenido entra en la segunda mitad de la animacion.
  uint8_t ca = 0;
  if(p >= 0.55f) ca = (uint8_t)(255.0f * ((p - 0.55f) / 0.45f));
  if(p >= 1.0f)  ca = 255;
  if(ca){
    cronoCardStatic(ca);
    if(cacheBg) memcpy(gCronoCardCache, bbuf + (size_t)CRONO_DYN_T * SCR_W,
                       (size_t)SCR_W * CRONO_DYN_H * 2);
    cronoCardDynText(ca);
  } else if(cacheBg){
    memcpy(gCronoCardCache, bbuf + (size_t)CRONO_DYN_T * SCR_W,
           (size_t)SCR_W * CRONO_DYN_H * 2);
  }
  present(CRONO_BAND_T, CRONO_BAND_B - 1);
  setBuf(fb);
}
// Frame barato en reposo: repone la sub-banda cacheada y reescribe los textos.
static void cronoCardComposeDyn(){
  gClipY0 = 0; gClipY1 = SCR_H - 1; gClipX0 = 0; gClipX1 = SCR_W - 1;
  setBuf(bbuf);
  memcpy(bbuf + (size_t)CRONO_DYN_T * SCR_W, gCronoCardCache,
         (size_t)SCR_W * CRONO_DYN_H * 2);
  cronoCardDynText(255);
  present(CRONO_DYN_T, CRONO_DYN_B - 1);
  setBuf(fb);
}
// Devuelve la banda a como estaba antes de abrir la tarjeta.
static void cronoCardRestore(){
  if(!gCronoCardBak) return;
  fbLock();
  memcpy(fb + (size_t)CRONO_BAND_T * SCR_W, gCronoCardBak,
         (size_t)SCR_W * CRONO_BAND_H * 2);
  fbUnlock();
  flxFlush(CRONO_BAND_T, CRONO_BAND_B - 1);
}

static void cronoCardOpen(){
  if(gCronoCard != CC_HIDDEN) return;
  if(flexOtaOwnsScreen() || gLand || gHosted) return;
  // Sin PSRAM para la tarjeta no se inventa nada a medias: se abre la app.
  if(!cronoCardAlloc()){ cronoOpenApp(); return; }
  fbLock();
  memcpy(gCronoCardBak, fb + (size_t)CRONO_BAND_T * SCR_W,
         (size_t)SCR_W * CRONO_BAND_H * 2);
  fbUnlock();
  gCronoCardBg = false;
  // VIDRIO DURANTE TODA LA TRANSICION. El fondo de la tarjeta es la banda que se
  // acaba de capturar y NO cambia mientras dura la animacion (loop() le cede la
  // pantalla en exclusiva, ver cronoCardVisible), asi que su desenfoque se
  // calcula UNA sola vez aqui. A partir de ahi uiSurfaceA solo lo muestrea: el
  // vidrio de los cuadros de animacion cuesta lo mismo que costaba el relleno
  // plano que sustituye.
  uiGlassBandEnd();
  if(uiGlass){
    uint16_t* ob = gBuf;
    setBuf(bbuf);
    memcpy(bbuf + (size_t)CRONO_BAND_T * SCR_W, gCronoCardBak,
           (size_t)SCR_W * CRONO_BAND_H * 2);
    uiGlassBandBegin(CRONO_BAND_T, CRONO_BAND_B - 1, uiSurfTint(UIS_ELEVATED));
    setBuf(ob);
  }
  gCronoCard   = CC_OPENING;
  gCronoCardT0 = millis();
}
static void cronoCardClose(){
  if(gCronoCard != CC_OPEN && gCronoCard != CC_OPENING) return;
  gCronoCard   = CC_CLOSING;
  gCronoCardT0 = millis();
}
// Cierre inmediato (al saltar a la app): sin animacion, restaurando la banda.
static void cronoCardCloseNow(){
  if(gCronoCard == CC_HIDDEN) return;
  cronoCardRestore();
  gCronoCard   = CC_HIDDEN;
  gCronoCardBg = false;
  gCronoBarDirty = true;              // la barra de debajo se rehace al volver
}

// Abandono SIN restaurar: la banda capturada ya no describe lo que hay debajo
// (otra pantalla tomo el fb), asi que devolverla pintaria un fotograma muerto.
// Quien tomo la pantalla es responsable de sus propios pixeles.
static void cronoCardDrop(){
  gCronoCard   = CC_HIDDEN;
  gCronoCardBg = false;
  gCronoBarDirty = true;
}

static void cronoCardTick(){
  if(gCronoCard == CC_HIDDEN) return;
  if(flexOtaOwnsScreen()){ cronoCardDrop(); return; }   // la pantalla pasa a ser del OTA
  // Bloqueo, apagado, Modo PC o cualquier otra pantalla que se haya adueñado
  // del fb por debajo (autoLockTick corre ANTES que este bloque en loop).
  if(gState != ST_HOME && gState != ST_APP){ cronoCardDrop(); return; }
  if(gSuspOn) return;                              // pantalla apagandose: no se dibuja, pero la tarjeta sigue ahi
  uint32_t e = (uint32_t)(millis() - gCronoCardT0);
  if(gCronoCard == CC_OPENING){
    float p = (float)e / (float)CRONO_CARD_ANIM; if(p > 1.0f) p = 1.0f;
    if(p >= 1.0f){ cronoCardCompose(1.0f, true); gCronoCard = CC_OPEN; gCronoCardBg = true; }
    else           cronoCardCompose(cronoEase(p), false);
    return;
  }
  if(gCronoCard == CC_CLOSING){
    float p = (float)e / (float)CRONO_CARD_ANIM; if(p > 1.0f) p = 1.0f;
    if(p >= 1.0f){ cronoCardRestore(); gCronoCard = CC_HIDDEN; gCronoCardBg = false; gCronoBarDirty = true; uiGlassBandEnd(); }
    else          cronoCardCompose(cronoEase(1.0f - p), false);
    return;
  }
  // CC_OPEN: ~30 fps y SOLO la sub-banda dinamica.
  static uint32_t last = 0;
  uint32_t now = millis();
  if((uint32_t)(now - last) < 33) return;
  last = now;
  if(!gCronoCardBg){ cronoCardCompose(1.0f, true); gCronoCardBg = true; return; }
  cronoCardComposeDyn();
  // LA BARRA SUPERIOR VUELVE A SU SITIO Y SIGUE VIVA. La tarjeta empieza
  // en y=30 y la pildora vive en y=12..38: sus primeras filas quedan a la
  // vista siempre. Con la tarjeta abierta, loop() no llega a
  // cronoCapsuleTick(), asi que la pildora se quedaba con la hora de
  // cuando se abrio -- en el video la barra dice "00:07" mientras la
  // tarjeta dice "00:18.56".
  //
  // Se estampa aqui, y SOLO se estampa: cronoBarRebuild() repintaria el
  // escritorio entero y se llevaria la tarjeta por delante. La pildora es
  // opaca y de ancho fijo, asi que dibujarla encima de si misma basta.
  // Su banda (11..39) no se solapa con la sub-banda dinamica de la
  // tarjeta (52..147), asi que las dos publicaciones no compiten.
  if(gCronoCapOn && cronoActive()){
    int surface = cronoBarSurface();
    if(surface != 0 && cronoCapsuleW() == gCronoCapWDrawn){
      uint32_t sec = cronoElapsed() / 1000UL;
      if(sec != gCronoCapSec || (uint8_t)gCronoSt != gCronoCapStDrawn){
        gCronoCapSec     = sec;
        gCronoCapStDrawn = (uint8_t)gCronoSt;
        cronoCapsuleStamp(surface);
      }
    }
  }
}

// =============================================================
//  6. TACTO GLOBAL DEL MODULO
// =============================================================
// Se llama en loop() ANTES de notifHandleTouch() y del switch de estado: la
// tarjeta es un overlay modal y tiene que quedarse el toque antes que la
// pantalla de debajo, igual que hace la isla con sus tarjetas. Consume solo
// los flags de evento que usa; NUNCA toca T.down (lo gestiona flexPollTouch).
static void cronoOverlayTouch(){
  static bool longFired = false;
  if(!T.down) longFired = false;
  if(flexOtaOwnsScreen()){ if(gCronoCard != CC_HIDDEN) cronoCardDrop(); return; }
  if(gSuspOn) return;

  // --- Tarjeta a la vista: se queda con TODO ---
  if(gCronoCard != CC_HIDDEN){
    if(gCronoCard != CC_OPEN){ T.tap = T.pressed = T.released = false; return; }  // animando
    int cx0 = CRONO_CARD_X, cx1 = CRONO_CARD_X + CRONO_CARD_W;
    int cy0 = CRONO_CARD_Y, cy1 = CRONO_CARD_Y + CRONO_CARD_H;
    if(T.swipeUp){ cronoCardClose(); T.swipeUp = T.tap = false; return; }        // gesto arriba: contraer
    if(T.tap){
      bool inside = (T.x >= cx0 && T.x <= cx1 && T.y >= cy0 && T.y <= cy1);
      T.tap = false; T.pressed = false;
      if(!inside){ cronoCardClose(); return; }                                    // fuera: contraer
      if(T.y >= CRONO_CARD_Y + 128){                                              // fila de botones
        if(T.x < SCR_W / 2) cronoActionLeft(); else cronoActionRight();
        // "Reinic." deja el cronometro inactivo: la capsula desaparece, asi que
        // la tarjeta que colgaba de ella se contrae con ella.
        if(!cronoActive()){ cronoCardClose(); return; }
        gCronoCardBg = false;                                                     // etiquetas y colores cambian
        return;
      }
      cronoOpenApp();                                                             // tiempo o icono: abre Reloj
      return;
    }
    T.pressed = T.released = T.swipeLeft = T.swipeRight = T.swipeDown = false;
    return;
  }

  // --- Solo la capsula escucha ---
  if(!gCronoCapOn || !cronoActive()) return;
  if(cronoBarSurface() == 0) return;
  int x0 = gCronoCapX - 6, x1 = gCronoCapX + gCronoCapWDrawn + 6;
  int y0 = CRONO_CAP_Y - 6, y1 = CRONO_CAP_Y + CRONO_CAP_H + 6;
  // Pulsacion larga sobre la capsula -> directo a la pestana Cronometro.
  if(!longFired && T.down && (uint32_t)(millis() - T.downMs) > 700 &&
     T.startX >= x0 && T.startX <= x1 && T.startY >= y0 && T.startY <= y1 &&
     abs(T.x - T.startX) < 12 && abs(T.y - T.startY) < 12){
    longFired = true;
    T.tap = T.pressed = false;
    cronoOpenApp();
    return;
  }
  if(T.tap && T.x >= x0 && T.x <= x1 && T.y >= y0 && T.y <= y1){
    T.tap = false; T.pressed = false;
    // Toque sobre el ICONO: atajo a la app. En el resto de la pildora: tarjeta.
    if(T.x <= gCronoCapX + CRONO_CAP_PADL + CRONO_CAP_ICON + 3) cronoOpenApp();
    else                                                        cronoCardOpen();
  }
}
