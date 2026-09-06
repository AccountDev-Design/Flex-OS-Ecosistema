// #############################################################
// ##  FLEX OS ULTRA  ·  MODO PC / DeX  ·  modelo y estado
// ##  ----------------------------------------------------------
// ##  Escritorio horizontal 800x480: ventanas (PWin), barra de tareas,
// ##  anclajes, catalogo de apps y estado del subsistema (prefijo dex*).
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
#include "FlexOS_Ultra_AppsBasic.h"   // eslabon anterior de la cadena

// #############################################################
// ##  MODO PC  (Milestone 4)  ·  Samsung DeX STANDALONE
// ##  Escritorio LANDSCAPE 800x480 dibujado rotado sobre el panel
// ##  portrait (gLand). Clon del DeX que corre en la PROPIA pantalla
// ##  de las Galaxy Tab S: aqui NO hay salida a monitor externo,
// ##  cable DeX/HDMI ni casting -- todo vive en este panel.
// ##
// ##  Mapa del subsistema (prefijo dex*; los pc* que ya existian se
// ##  conservan como puntos de entrada de APP_REG):
// ##    · Geometria .............. dexWorkBottom/dexTbY/dexSnapRect
// ##    · Animador interrumpible . dexAnimStart*/dexAnimCur/dexAnimTick
// ##    · Ventanas ............... dexDrawWindow/dexOpenFrom/dexCloseWin/
// ##                               dexMinimize/dexRestore/dexApplySnap
// ##    · Barra de tareas ........ dexTbLayout/dexTaskbar (+auto-ocultar)
// ##    · Cajon de apps / Finder . dexDrawerDraw/dexFinderDraw
// ##    · Notificaciones ......... dexNotifDraw (cortina)
// ##    · Recientes .............. dexRecentsDraw
// ##    · Menu contextual ........ dexMenuDraw/dexMenuRun
// ##    · Touchpad virtual ....... dexPadDraw/dexCursorDraw
// ##    · Composicion + banda .... dexCompose/dexPaint/pcRender
// ##    · Entrada ................ dexPointer/dexInput/pcTick
// #############################################################

// ---- Geometria ----
#define DEX_TB_H       58          // alto de la barra de tareas
#define DEX_TTL_H      38          // alto de la barra de titulo de ventana
#define DEX_BTN_W      38          // ancho de cada control (min/max/cerrar).
                                   // Los controles se AGRANDAN de verdad en vez de
                                   // que su hitbox invada al vecino: ensanchar la
                                   // zona de toque hacia los lados hacia que un
                                   // toque cerca de "cerrar" activara "maximizar".
#define DEX_MIN_W      240         // tamano minimo de ventana
#define DEX_MIN_H      140
#define DEX_GRIP       22          // margen de agarre EXTERIOR para redimensionar.
                                   // Con un dedo sobre un panel capacitivo, 14 px
                                   // fuera del borde eran casi imposibles de
                                   // acertar; 22 da un blanco realista sin robarle
                                   // area util a la ventana (hacia DENTRO sigue
                                   // siendo minimo, ver dexResizeMask).
#define DEX_ANIM_MS    180         // duracion base de las animaciones
#define DEX_TB_ANIM    150         // slide de auto-ocultar la barra
#define DEX_LONG_MS    560         // mantener pulsado -> menu contextual.
                                   // >550 A PROPOSITO: tDoRelease() solo marca
                                   // T.tap por debajo de 550 ms, asi que una
                                   // pulsacion larga NUNCA dispara ademas un tap.
#define DEX_DTAP_MS    400         // ventana del doble toque
#define DEX_SNAP_EDGE  12          // franja de borde que dispara el snap
#define DEX_TB_IDLE_MS 1800        // inactividad antes de auto-ocultar la barra
#define DEX_TB_REVEAL  18          // franja inferior que vuelve a mostrar la barra
#define DEX_PAD_W      210         // touchpad virtual
#define DEX_PAD_H      140
// Tamano de los pop-ups. Viven aqui arriba porque dexOvMark() los necesita para
// calcular la banda sucia de cada overlay, bastante antes de donde se dibujan.
#define DEX_DRW_W      672         // cajon de apps
#define DEX_DRW_H      404
#define DEX_FND_W      576         // buscador tipo Finder
#define DEX_FND_H      404
#define DEX_NP_W       360         // panel de notificaciones / ajustes rapidos

// ---- Paleta. DeX NO tiene tema propio: son alias del TEMA SEMANTICO GLOBAL,
//      igual que PAGE_BG/SET_* en Ajustes. Los nombres se conservan porque los
//      usan decenas de llamantes del subsistema dex*. ----
#define DEX_ACCENT   TH_PRIM        // acento del escritorio = accion primaria del tema
#define DEX_TB_BG    TH_PAGE        // barra de tareas (fondo plano)
#define DEX_TB_GLASS TH_GLASS       // barra de tareas (tinte Liquid Glass)
#define DEX_PANEL    TH_SURF        // panel / cajon / menu
#define DEX_PANEL2   TH_SURF2       // panel elevado (fila resaltada, chip)
#define DEX_TXT_HI   TH_TXT
#define DEX_TXT_LO   TH_TXT2
#define DEX_BORDER   TH_BORDER
#define DEX_WIN_BG   TH_SURF        // marco de ventana
#define DEX_WIN_BODY TH_WIN         // cuerpo de ventana (lienzo de la app hospedada)
#define DEX_TTL_ACT  TH_SURF2       // barra de titulo de la ventana ACTIVA
// Inactiva: se APAGA respecto al marco (hacia el fondo de pagina), asi sigue
// distinguiendose de DEX_TTL_ACT en las dos apariencias sin un color propio.
#define DEX_TTL_INA  mix565(TH_SURF, TH_PAGE, 110)

enum { SNAP_FREE = 0, SNAP_L, SNAP_R, SNAP_TL, SNAP_TR, SNAP_BL, SNAP_BR, SNAP_MAX };
enum { DXA_NONE = 0, DXA_OPEN, DXA_CLOSE, DXA_MIN, DXA_RESTORE, DXA_GEOM };
enum { DXO_NONE = 0, DXO_DRAWER, DXO_FINDER, DXO_NOTIF, DXO_RECENTS };
enum { DXG_NONE = 0, DXG_MOVE, DXG_RESIZE };

static PWin pwins[4];
static bool pcStartOpen = false;              // se conserva (lo tocaba el menu Inicio viejo)

static uint8_t dexOrder[4] = { 0, 1, 2, 3 };  // z-order: [0] atras ... [3] al frente
static int     dexFocus = -1;                 // ventana activa (indice en pwins)

static bool  dexTbAuto   = false;             // auto-ocultar la barra de tareas
static bool  dexBigIcons = false;             // iconos grandes en la barra
static float dexTbOff    = 0;                 // 0 = visible, DEX_TB_H = oculta
static int   dexTbTgt    = 0;
static float dexTbFrom   = 0;
static uint32_t dexTbT0  = 0;
static uint32_t dexTbIdle = 0;
static uint8_t dexWall   = 0;                 // variante de fondo de DeX (0..2)

static uint8_t  dexOv = DXO_NONE;             // overlay activo
static bool     dexOvClosing = false;
static bool     dexOvDone = false;            // el overlay ya termino de abrirse
static uint32_t dexOvT0 = 0;

static bool dexExiting = false;               // se pidio salir de Modo PC (ver pcExit)
// Fondo de DeX cacheado. El degradado se pintaba con hLine por fila LOGICA, y en
// landscape eso son 384.000 putPhys por frame, cada uno en una linea de cache
// distinta de la PSRAM: por si solo se comia el presupuesto de frame entero.
// Cacheado, el fondo de cada frame es un memcpy por fila fisica.
static uint16_t* dexBg = NULL;
static uint8_t   dexBgWall = 0xFF;            // variante ya cacheada
static bool      dexBgDark = false;           // gDark con el que se cacheo

static char dexQuery[20] = { 0 };             // texto del buscador (cajon y Finder)
static int  dexQLen = 0;

static bool    dexMenuOn = false;             // menu contextual
static uint8_t dexMenuKind = 0;               // 0 escritorio · 1 barra · 2 barra de titulo
static int     dexMenuX = 0, dexMenuY = 0, dexMenuWin = -1;

static bool dexPadOn = false;                 // touchpad virtual (mejora condicional)
static int  dexCurX = LW / 2, dexCurY = LH / 2;
static bool dexPadGrab = false;
static int  dexPadLX = 0, dexPadLY = 0;

// Animador: UNA sola ranura. Con pwins[4] no hay dos ventanas animando a la vez
// en la practica; si se pide otra, la anterior se cierra en su estado FINAL
// (dexAnimFinish) -- nunca queda una a medias.
static uint8_t  dexAK = DXA_NONE;
static int      dexAW = -1;
static int      dexAF[4], dexAT[4];           // rect origen / destino (x,y,w,h)
static uint32_t dexAT0 = 0, dexADur = DEX_ANIM_MS;

static uint8_t dexGrab = DXG_NONE;            // arrastre / redimension
static int     dexGrabWin = -1, dexGrabDX = 0, dexGrabDY = 0;
static uint8_t dexRzMask = 0;                 // bit0 izq · bit1 der · bit2 arriba · bit3 abajo
static int     dexRzX0, dexRzY0, dexRzW0, dexRzH0;
static uint8_t dexSnapGhost = SNAP_FREE;      // contorno fantasma del snap
static int     dexRecDrag = -1;               // tarjeta de Recientes que se arrastra
static int     dexRecDY = 0, dexRecY0 = 0;

// Banda sucia en X LOGICA. La rotacion de gLand mapea lx -> fila FISICA
// (putPhys: y = lx), asi que un rango de lx es exactamente un rango de filas del
// panel: es el UNICO eje por el que se puede acotar el volcado. Por eso arrastrar
// una ventana solo sube su franja de columnas en vez de los 800 px.
static int  dexBX0 = 0, dexBX1 = LW - 1;
static bool dexDirty = true;
static uint32_t dexFrameMs = 0;

// Puntero normalizado: sale del tactil directo o del touchpad virtual, de modo
// que TODA la logica de abajo es identica con y sin touchpad.
static int  pX = 0, pY = 0;
static bool pDown = false, pPressed = false, pReleased = false;
static bool pTap = false, pLong = false, pDTap = false;
static bool dexLongFired = false;
static uint32_t dexTapMs = 0;
static int  dexTapX = 0, dexTapY = 0;
static int  dexPressX = 0, dexPressY = 0;   // origen del gesto en curso

static const uint8_t DEX_PIN[6] = { IC_NAV, IC_NOTAS, IC_CALC, IC_AJUSTES, IC_ALMACEN, IC_GALERIA };
#define DEX_PINN 6

// Entradas de Ajustes que indexa el buscador tipo Finder. Solo apps y ajustes:
// este OS no tiene agenda de contactos y fabricar una lista falsa iria contra el
// criterio del resto del archivo (por eso mismo se quitaron los toggles de
// Wi-Fi/BT del panel rapido: no habia radio real detras).
static const char* DEX_SET[8] = { "Pantalla", "Sonido", "Red e Internet", "Bateria",
                                  "Aplicaciones", "Almacenamiento", "Seguridad", "Acerca de" };
static const char* DEX_KB[3] = { "qwertyuiop", "asdfghjkl", "zxcvbnm" };

// Rects calculados por dexTbLayout() y reutilizados por DIBUJO y TACTIL, para que
// la geometria viva en un solo sitio. Formato { x, y, w, h }.
static int dexRDrw[4], dexRFnd[4], dexRPad[4], dexRBell[4], dexRGear[4], dexRBat[4];
static int dexRClkX = 0, dexRWifiX = 0;

static void pcRender();
static void pcExit();
static void dexOpenFrom(int app, int sx, int sy, int ss);
static void dexOpen(int app);
// Hosting de apps reales en ventanas (definido mas abajo; se usa desde el ciclo
// de vida de las ventanas, que va antes).
static void dexHostOpen(int i);
static void dexHostClose(int i);
static void dexHostRun(int i, bool doEnter, bool doTick, const Touch* inject);
static void dexHostServe(int i);
static void dexClientRect(int i, int &cx, int &cy, int &cw, int &ch);
static void dexHostMinSize(int app, int &mw, int &mh);
static void dexHostDefaultSize(int app, int &w, int &h);

// -------------------------------------------------------------
//  Utilidades
// -------------------------------------------------------------
static inline bool dexIn(int x, int y, const int* r){
  return x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3];
}
static inline bool dexInBox(int x, int y, int bx, int by, int bw, int bh){
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}
static inline float dexEase(float t){ t = 1.0f - t; return 1.0f - t * t * t; }   // ease-out cubico

static void dexMark(int x0, int x1){
  if(x0 < dexBX0) dexBX0 = x0;
  if(x1 > dexBX1) dexBX1 = x1;
}
static inline void dexMarkAll(){ dexBX0 = 0; dexBX1 = LW - 1; }
static inline void dexMarkWin(int x, int w){ dexMark(x - 10, x + w + 10); }

// gClipY0/gClipY1 son, en landscape, la banda de X LOGICA que se esta pintando.
// dexCull descarta de golpe lo que cae fuera; dexBand recorta un tramo horizontal
// a la banda. Sin esto el bucle de cada hLine recorreria los 800 px igualmente
// (putPhys rechaza pixel a pixel, pero el bucle se paga entero).
static inline int dexBandLo(){ return gClipY0 < 0 ? 0 : gClipY0; }
static inline int dexBandHi(){ return gClipY1 > LW - 1 ? LW - 1 : gClipY1; }
static inline bool dexCull(int x, int w){ return (x + w < dexBandLo()) || (x > dexBandHi()); }
static bool dexBand(int &x, int &w){
  int b0 = dexBandLo(), b1 = dexBandHi();
  if(x < b0){ w -= (b0 - x); x = b0; }
  if(x + w > b1 + 1) w = b1 + 1 - x;
  return w > 0;
}

// Texto recortado con "..": las primitivas solo recortan contra el lienzo logico,
// no contra la ventana, asi que un nombre largo en una ventana estrecha se
// saldria por el borde. Corta SIN partir una secuencia UTF-8.
static void dexTextFit(int x, int y, const char* s, int size, uint16_t col, int maxw){
  if(maxw <= 0) return;
  if(textW(s, size) <= maxw){ drawText(x, y, s, size, col); return; }
  char b[48];
  int n = 0; while(s[n] && n < (int)sizeof(b) - 3) n++;
  for(int len = n; len > 0; len--){
    if(((uint8_t)s[len] & 0xC0) == 0x80) continue;      // no cortar a mitad de un caracter
    memcpy(b, s, len); b[len] = '.'; b[len + 1] = '.'; b[len + 2] = 0;
    if(textW(b, size) <= maxw){ drawText(x, y, b, size, col); return; }
  }
}
static void dexTextFitC(int cx, int y, const char* s, int size, uint16_t col, int maxw){
  if(textW(s, size) <= maxw){ drawTextC(cx, y, s, size, col); return; }
  dexTextFit(cx - maxw / 2, y, s, size, col, maxw);
}
static inline char dexLower(char c){ return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
// Subcadena sin distinguir mayusculas (byte a byte: las consultas se teclean en
// ASCII, los acentos del nombre simplemente no casan).
static bool dexMatch(const char* name, const char* q, int qn){
  if(qn <= 0) return true;
  for(int i = 0; name[i]; i++){
    int k = 0;
    while(k < qn && name[i + k] && dexLower(name[i + k]) == dexLower(q[k])) k++;
    if(k == qn) return true;
  }
  return false;
}
static int dexFilterApps(int* out, int maxn){
  int n = 0;
  for(int i = 0; i < APP_N && n < maxn; i++) if(dexMatch(appName(i), dexQuery, dexQLen)) out[n++] = i;
  return n;
}

// -------------------------------------------------------------
//  Geometria del escritorio
// -------------------------------------------------------------
static inline int dexTbY(){ return LH - DEX_TB_H + (int)dexTbOff; }
// Con auto-ocultar el area util llega abajo del todo (la barra se superpone al
// aparecer); sin el, termina justo encima de la barra.
static inline int dexWorkBottom(){ return dexTbAuto ? LH : LH - DEX_TB_H; }

static void dexSnapRect(uint8_t s, int &x, int &y, int &w, int &h){
  int H = dexWorkBottom(), W = LW;
  switch(s){
    case SNAP_L:  x = 0;     y = 0;     w = W / 2; h = H;     break;
    case SNAP_R:  x = W / 2; y = 0;     w = W / 2; h = H;     break;
    case SNAP_TL: x = 0;     y = 0;     w = W / 2; h = H / 2; break;
    case SNAP_TR: x = W / 2; y = 0;     w = W / 2; h = H / 2; break;
    case SNAP_BL: x = 0;     y = H / 2; w = W / 2; h = H / 2; break;
    case SNAP_BR: x = W / 2; y = H / 2; w = W / 2; h = H / 2; break;
    default:      x = 0;     y = 0;     w = W;     h = H;     break;   // SNAP_MAX
  }
}
// Que anclaje sugiere la posicion del puntero (estilo Aero Snap).
static uint8_t dexSnapHit(int lx, int ly){
  int H = dexWorkBottom(), cw = 170, ch = 110;
  if(ly <= DEX_SNAP_EDGE){
    if(lx <= cw) return SNAP_TL;
    if(lx >= LW - cw) return SNAP_TR;
    return SNAP_MAX;
  }
  if(lx <= DEX_SNAP_EDGE){
    if(ly <= ch) return SNAP_TL;
    if(ly >= H - ch) return SNAP_BL;
    return SNAP_L;
  }
  if(lx >= LW - DEX_SNAP_EDGE){
    if(ly <= ch) return SNAP_TR;
    if(ly >= H - ch) return SNAP_BR;
    return SNAP_R;
  }
  if(ly >= H - DEX_SNAP_EDGE){
    if(lx <= cw) return SNAP_BL;
    if(lx >= LW - cw) return SNAP_BR;
  }
  return SNAP_FREE;
}

// px de ventana que quedan SIEMPRE agarrables dentro del area util.
#define DEX_KEEP 96
// Unica puerta de saneado de la geometria de una ventana: la usan la apertura,
// el arrastre, el resize, el fin de animacion y el cambio de area util.
static void dexClampWin(PWin* wn){
  int mw = DEX_MIN_W, mh = DEX_MIN_H;
  // El suelo de legibilidad solo se impone cuando la ventana es libre. Si esta
  // anclada (mitad, cuadrante, maximizada) manda el anclaje: el letterbox se
  // encarga de que siga sin deformarse, solo se ve mas pequena.
  if(wn->snap == SNAP_FREE) dexHostMinSize(wn->app, mw, mh);
  flxClampRect(wn->x, wn->y, wn->w, wn->h, LW, dexWorkBottom(), mw, mh, DEX_KEEP);
}
// El area util cambia de alto al activar/desactivar el auto-ocultar de la barra
// (dexWorkBottom). Sin re-encajar, una ventana colocada abajo con la barra
// oculta se quedaba DEBAJO de la barra al volver a fijarla: sin barra de titulo
// accesible, no habia forma de moverla ni cerrarla salvo por Recientes.
static void dexClampAll(){
  int H = dexWorkBottom();
  for(int i = 0; i < 4; i++){
    if(!pwins[i].open) continue;
    if(pwins[i].snap != SNAP_FREE)
      dexSnapRect(pwins[i].snap, pwins[i].x, pwins[i].y, pwins[i].w, pwins[i].h);
    else dexClampWin(&pwins[i]);
    flxClampRect(pwins[i].rx, pwins[i].ry, pwins[i].rw, pwins[i].rh,
                 LW, H, DEX_MIN_W, DEX_MIN_H, DEX_KEEP);   // tambien el rect de restaurar
  }
  dexMarkAll(); dexDirty = true;
}

static int dexTopWin(){                        // ventana visible mas al frente
  for(int k = 3; k >= 0; k--){ int i = dexOrder[k]; if(pwins[i].open && !pwins[i].mini) return i; }
  return -1;
}
static void dexRaise(int i){
  if(i < 0 || i > 3) return;
  int at = -1; for(int k = 0; k < 4; k++) if(dexOrder[k] == i){ at = k; break; }
  if(at >= 0) for(int k = at; k < 3; k++) dexOrder[k] = dexOrder[k + 1];
  dexOrder[3] = (uint8_t)i;
  dexFocus = i;
  dexMarkAll();
}

// -------------------------------------------------------------
//  Barra de tareas: geometria (un solo sitio para dibujo y tactil)
// -------------------------------------------------------------
static inline int dexTbIconS(){ return dexBigIcons ? 40 : 32; }
static inline int dexTbStep(){ return dexTbIconS() + 20; }

// Lista de la barra: primero las FIJADAS, despues las abiertas que no lo esten.
static int dexTbItems(int* app, bool* open){
  int n = 0;
  for(int i = 0; i < DEX_PINN; i++){ app[n] = DEX_PIN[i]; open[n] = false; n++; }
  for(int i = 0; i < 4; i++) if(pwins[i].open){
    bool found = false;
    for(int k = 0; k < n; k++) if(app[k] == pwins[i].app){ open[k] = true; found = true; break; }
    if(!found && n < 10){ app[n] = pwins[i].app; open[n] = true; n++; }
  }
  return n;
}
// El grupo central va centrado en pantalla, pero NO puede invadir ni los iconos
// de la izquierda ni el bloque de estado de la derecha. Con las 6 fijadas mas
// hasta 4 ventanas abiertas, el ancho nominal se pasaba del hueco disponible y
// los iconos acababan pisando la campana y el reloj. Aqui el paso (y, si hace
// falta, el tamano del icono) se estrecha hasta caber, y el grupo se desplaza
// solo cuando ya no puede seguir centrado.
static void dexTbGroup(int n, int &x0, int &step, int &s){
  int lft = dexRFnd[0] + dexRFnd[2] + 16;
  int rgt = dexRPad[0] - 16;
  int avail = rgt - lft; if(avail < 80) avail = 80;
  step = dexTbStep();
  if(n > 0 && n * step > avail) step = avail / n;
  s = dexTbIconS();
  if(step < s + 4){ s = step - 4; if(s < 18) s = 18; }
  int tot = n * step;
  x0 = LW / 2 - tot / 2;
  if(x0 + tot > rgt) x0 = rgt - tot;
  if(x0 < lft) x0 = lft;
}
static void dexTbItemRect(int idx, int n, int &x, int &y, int &s){
  int x0, step; dexTbGroup(n, x0, step, s);
  x = x0 + idx * step + (step - s) / 2;
  y = dexTbY() + (DEX_TB_H - s) / 2 - 4;
}
// Rect del icono de una app en la barra: ORIGEN/DESTINO de las animaciones de
// abrir, minimizar y restaurar.
static bool dexTbAppRect(int appId, int &x, int &y, int &s){
  int app[10]; bool op[10];
  int n = dexTbItems(app, op);
  for(int i = 0; i < n; i++) if(app[i] == appId){ dexTbItemRect(i, n, x, y, s); return true; }
  s = dexTbIconS(); x = LW / 2 - s / 2; y = dexTbY() + (DEX_TB_H - s) / 2 - 3;
  return false;
}
static void dexTbLayout(){
  int ty = dexTbY(), h = DEX_TB_H;
  const int LS = 34, RS = 30;                       // iconos: izquierda / estado
  dexRDrw[0] = 12; dexRDrw[1] = ty + (h - LS) / 2; dexRDrw[2] = LS; dexRDrw[3] = LS;
  dexRFnd[0] = 12 + LS + 10; dexRFnd[1] = ty + (h - LS) / 2; dexRFnd[2] = LS; dexRFnd[3] = LS;
  char cs[12]; clkStrBar(cs, sizeof(cs));
  char sd[40]; buildShortDate(sd, sizeof(sd));
  int cw = textW(cs, 2), dw = textW(sd, 1); if(dw > cw) cw = dw;
  int rx = LW - 16;
  dexRClkX = rx;  rx -= cw + 18;
  dexRBat[0] = rx - 30; dexRBat[1] = ty + (h - 15) / 2; dexRBat[2] = 30; dexRBat[3] = 15; rx -= 44;
  dexRWifiX = rx - 11; rx -= 32;
  int riy = ty + (h - RS) / 2;
  dexRGear[0] = rx - RS; dexRGear[1] = riy; dexRGear[2] = RS; dexRGear[3] = RS; rx -= RS + 8;
  dexRBell[0] = rx - RS; dexRBell[1] = riy; dexRBell[2] = RS; dexRBell[3] = RS; rx -= RS + 8;
  dexRPad[0]  = rx - RS; dexRPad[1]  = riy; dexRPad[2]  = RS; dexRPad[3]  = RS;
}

// -------------------------------------------------------------
//  Animador (interrumpible)
// -------------------------------------------------------------
static float dexAP(){
  if(dexAK == DXA_NONE) return 1.0f;
  uint32_t e = millis() - dexAT0;
  if(e >= dexADur) return 1.0f;
  return dexEase((float)e / (float)dexADur);
}
static inline bool dexAnimIs(int w){ return dexAK != DXA_NONE && dexAW == w; }
// Banda que barre la animacion: union del rect de origen y el de destino. Antes
// marcaba dexMarkAll() en CADA frame, asi que animar una ventana de 430 px
// obligaba a recomponer y volcar los 800 -- casi el doble de trabajo por frame.
static void dexAnimMark(){
  int x0 = dexAF[0] < dexAT[0] ? dexAF[0] : dexAT[0];
  int e1 = dexAF[0] + dexAF[2], e2 = dexAT[0] + dexAT[2];
  int x1 = e1 > e2 ? e1 : e2;
  dexMark(x0 - 12, x1 + 12);
}
static void dexAnimCur(int &x, int &y, int &w, int &h, uint8_t &a){
  float p = dexAP();
  x = dexAF[0] + (int)((dexAT[0] - dexAF[0]) * p + 0.5f);
  y = dexAF[1] + (int)((dexAT[1] - dexAF[1]) * p + 0.5f);
  w = dexAF[2] + (int)((dexAT[2] - dexAF[2]) * p + 0.5f);
  h = dexAF[3] + (int)((dexAT[3] - dexAF[3]) * p + 0.5f);
  float fa = (dexAK == DXA_CLOSE || dexAK == DXA_MIN) ? (1.0f - p)
           : (dexAK == DXA_GEOM ? 1.0f : p);
  int v = 40 + (int)(215 * fa); if(v > 255) v = 255; if(v < 0) v = 0;
  a = (uint8_t)v;
}
// Cierra la animacion en curso APLICANDO su estado final. Se llama al terminar y
// tambien al interrumpirla con otra ventana: nunca queda una a medio camino.
static void dexAnimFinish(){
  if(dexAK == DXA_NONE) return;
  int w = dexAW; uint8_t k = dexAK;
  dexAK = DXA_NONE; dexAW = -1;
  if(w < 0 || w > 3) return;
  if(k == DXA_CLOSE){
    pwins[w].open = false; pwins[w].mini = false;
    dexHostClose(w);                                // libera el lienzo de la app
    if(dexFocus == w) dexFocus = dexTopWin();
  } else if(k == DXA_MIN){
    pwins[w].mini = true;
    if(dexFocus == w) dexFocus = dexTopWin();
  } else {
    if(k == DXA_RESTORE) pwins[w].mini = false;
    pwins[w].x = dexAT[0]; pwins[w].y = dexAT[1];
    pwins[w].w = dexAT[2]; pwins[w].h = dexAT[3];
  }
  dexMark(dexAF[0] - 12, dexAF[0] + dexAF[2] + 12);   // solo lo que toco la animacion
  dexMark(dexAT[0] - 12, dexAT[0] + dexAT[2] + 12);
  if(w >= 0 && w <= 3) dexClampWin(&pwins[w]);        // el destino nunca queda fuera
  // Con Recientes abierto, cerrar o minimizar una ventana CAMBIA la lista, y las
  // tarjetas van centradas: todas se recolocan de golpe. Marcar solo la banda de
  // la animacion (que es la de la VENTANA, no la de las tarjetas) dejaba mitades
  // de tarjeta congeladas donde estaban antes -- la tarjeta fantasma. La lista
  // solo cambia aqui, y es un evento puntual, asi que se recompone entera.
  if(dexOv == DXO_RECENTS) dexMarkAll();
}
static void dexAnimStartFT(uint8_t kind, int win,
                           int fx, int fy, int fw, int fh,
                           int tx, int ty, int tw, int th, uint32_t dur){
  if(dexAK != DXA_NONE && dexAW != win) dexAnimFinish();
  dexAK = kind; dexAW = win;
  dexAF[0] = fx; dexAF[1] = fy; dexAF[2] = fw; dexAF[3] = fh;
  dexAT[0] = tx; dexAT[1] = ty; dexAT[2] = tw; dexAT[3] = th;
  dexAT0 = millis(); dexADur = dur ? dur : 1;
  dexAnimMark(); dexDirty = true;
}
// Version "hacia": el origen es el rect ACTUAL. Si esa misma ventana ya estaba
// animando se toma su rect INTERPOLADO de este instante, de modo que la nueva
// animacion continua desde donde iba en vez de reiniciar (requisito de "todas
// deben poder interrumpirse a medio camino").
static void dexAnimStartTo(uint8_t kind, int win, int tx, int ty, int tw, int th, uint32_t dur){
  int fx, fy, fw, fh;
  if(dexAnimIs(win)){ uint8_t a; dexAnimCur(fx, fy, fw, fh, a); }
  else { fx = pwins[win].x; fy = pwins[win].y; fw = pwins[win].w; fh = pwins[win].h; }
  dexAnimStartFT(kind, win, fx, fy, fw, fh, tx, ty, tw, th, dur);
}
static void dexAnimTick(){
  if(dexAK == DXA_NONE) return;
  dexAnimMark(); dexDirty = true;
  if(millis() - dexAT0 >= dexADur) dexAnimFinish();
}

// -------------------------------------------------------------
//  Ciclo de vida de las ventanas
// -------------------------------------------------------------
static void dexRestore(int i){
  if(i < 0 || i > 3 || !pwins[i].open) return;
  bool wasMin = pwins[i].mini;
  dexRaise(i);
  if(!wasMin){ dexDirty = true; return; }
  int ix, iy, is; dexTbAppRect(pwins[i].app, ix, iy, is);
  dexAnimStartFT(DXA_RESTORE, i, ix, iy, is, is,
                 pwins[i].x, pwins[i].y, pwins[i].w, pwins[i].h, DEX_ANIM_MS);
}
static void dexMinimize(int i){
  if(i < 0 || i > 3 || !pwins[i].open || pwins[i].mini) return;
  int ix, iy, is; dexTbAppRect(pwins[i].app, ix, iy, is);
  dexAnimStartTo(DXA_MIN, i, ix, iy, is, is, DEX_ANIM_MS);
}
static void dexCloseWin(int i){
  if(i < 0 || i > 3 || !pwins[i].open) return;
  int ix, iy, is; dexTbAppRect(pwins[i].app, ix, iy, is);
  dexAnimStartTo(DXA_CLOSE, i, ix, iy, is, is, DEX_ANIM_MS);
}
static void dexApplySnap(int i, uint8_t s){
  PWin* wn = &pwins[i];
  if(wn->snap == SNAP_FREE){ wn->rx = wn->x; wn->ry = wn->y; wn->rw = wn->w; wn->rh = wn->h; }
  wn->snap = s;
  int x, y, w, h; dexSnapRect(s, x, y, w, h);
  dexAnimStartTo(DXA_GEOM, i, x, y, w, h, DEX_ANIM_MS);
}
static void dexToggleMax(int i){
  PWin* wn = &pwins[i];
  if(wn->snap != SNAP_FREE){
    wn->snap = SNAP_FREE;
    dexAnimStartTo(DXA_GEOM, i, wn->rx, wn->ry, wn->rw, wn->rh, DEX_ANIM_MS);
  } else dexApplySnap(i, SNAP_MAX);
}
static void dexOpenFrom(int app, int sx, int sy, int ss){
  for(int i = 0; i < 4; i++) if(pwins[i].open && pwins[i].app == app){
    if(pwins[i].mini) dexRestore(i); else dexRaise(i);
    dexDirty = true; return;
  }
  int slot = -1;
  for(int i = 0; i < 4; i++) if(!pwins[i].open){ slot = i; break; }
  if(slot < 0){                                     // tope de pwins[4]: recicla la mas antigua
    slot = (int)dexOrder[0];
    if(slot < 0 || slot > 3) slot = 0;              // pwins[4]: nunca escribir fuera del array
    if(dexAnimIs(slot)) dexAnimFinish();            // no dejar una animacion apuntando al slot reciclado
    dexHostClose(slot);                             // y libera el lienzo del que se recicla
    pwins[slot].open = false; pwins[slot].mini = false;
  }
  int n = 0; for(int j = 0; j < 4; j++) if(pwins[j].open) n++;
  PWin* wn = &pwins[slot];
  wn->open = true; wn->mini = false; wn->app = app; wn->snap = SNAP_FREE;
  dexHostDefaultSize(app, wn->w, wn->h);            // nace con la proporcion de la app
  wn->x = 66 + n * 38; wn->y = 30 + n * 26;
  int H = dexWorkBottom();
  if(wn->x + wn->w > LW - 10) wn->x = LW - 10 - wn->w;
  if(wn->y + wn->h > H - 10)  wn->y = H - 10 - wn->h;
  if(wn->x < 10) wn->x = 10;
  if(wn->y < 8)  wn->y = 8;
  dexClampWin(wn);
  wn->rx = wn->x; wn->ry = wn->y; wn->rw = wn->w; wn->rh = wn->h;
  dexRaise(slot);
  dexHostOpen(slot);                                // arranca la app REAL en la ventana
  if(ss <= 0) dexTbAppRect(app, sx, sy, ss);        // sin origen -> icono de la barra
  dexAnimStartFT(DXA_OPEN, slot, sx, sy, ss, ss, wn->x, wn->y, wn->w, wn->h, DEX_ANIM_MS);
}
static void dexOpen(int app){ dexOpenFrom(app, 0, 0, 0); }

static void dexCascade(){                           // menu contextual del escritorio
  int n = 0, H = dexWorkBottom();
  for(int k = 0; k < 4; k++){
    int i = dexOrder[k];
    if(!pwins[i].open) continue;
    pwins[i].mini = false; pwins[i].snap = SNAP_FREE;
    dexHostDefaultSize(pwins[i].app, pwins[i].w, pwins[i].h);
    pwins[i].x = 66 + n * 44; pwins[i].y = 26 + n * 30;
    if(pwins[i].x + pwins[i].w > LW - 10) pwins[i].x = LW - 10 - pwins[i].w;
    if(pwins[i].y + pwins[i].h > H - 10)  pwins[i].y = H - 10 - pwins[i].h;
    dexClampWin(&pwins[i]);
    pwins[i].rx = pwins[i].x; pwins[i].ry = pwins[i].y;
    pwins[i].rw = pwins[i].w; pwins[i].rh = pwins[i].h;
    n++;
  }
  dexAnimFinish(); dexMarkAll(); dexDirty = true;
}

// -------------------------------------------------------------
//  Overlays (cajon, Finder, notificaciones, recientes)
// -------------------------------------------------------------
// Banda que ocupa cada overlay. Marcar solo lo suyo (y no toda la pantalla) es
// lo que hace que abrir el cajon o la cortina no cueste un frame completo.
static void dexOvMark(uint8_t o){
  int w;
  switch(o){
    case DXO_DRAWER: w = DEX_DRW_W; break;
    case DXO_FINDER: w = DEX_FND_W; break;
    case DXO_NOTIF:  dexMark(LW - DEX_NP_W - 20, LW - 1); return;
    default:         dexMarkAll(); return;              // Recientes atenua todo
  }
  dexMark((LW - w) / 2 - 10, (LW + w) / 2 + 10);
}
static void dexOvOpen(uint8_t o){
  if(dexOv == o && !dexOvClosing) return;
  uint8_t prev = dexOv;
  dexOv = o; dexOvClosing = false; dexOvDone = false; dexOvT0 = millis();
  dexQLen = 0; dexQuery[0] = 0;
  if(prev != DXO_NONE) dexOvMark(prev);
  dexOvMark(o); dexDirty = true;
}
static void dexOvClose(){
  if(dexOv == DXO_NONE || dexOvClosing) return;
  dexOvClosing = true; dexOvDone = false; dexOvT0 = millis();
  dexOvMark(dexOv); dexDirty = true;
}
static float dexOvProg(){
  if(dexOv == DXO_NONE) return 0;
  uint32_t e = millis() - dexOvT0;
  float p = e >= DEX_ANIM_MS ? 1.0f : dexEase((float)e / (float)DEX_ANIM_MS);
  return dexOvClosing ? 1.0f - p : p;
}
static inline bool dexOvSettled(){
  return dexOv != DXO_NONE && !dexOvClosing && millis() - dexOvT0 >= DEX_ANIM_MS;
}
static void dexOvTick(){
  if(dexOv == DXO_NONE) return;
  if(millis() - dexOvT0 < DEX_ANIM_MS){       // creciendo o encogiendo
    dexOvMark(dexOv); dexDirty = true; dexOvDone = false; return;
  }
  if(dexOvClosing){
    uint8_t o = dexOv;
    dexOv = DXO_NONE; dexOvClosing = false; dexOvDone = false;
    dexOvMark(o); dexDirty = true; return;
  }
  // Ya asentado. Hace falta UN frame mas, y este es el motivo: dexPopupFrame
  // solo pinta el contenido cuando el progreso llega a 1, pero el ultimo frame
  // que se pedia era el de progreso < 1 -- o sea la caja vacia. Sin este pulso,
  // el cajon se quedaba en blanco hasta que otra cosa ensuciara la pantalla
  // (abrir una app), que es exactamente el sintoma que se veia.
  if(!dexOvDone){ dexOvDone = true; dexOvMark(dexOv); dexDirty = true; }
}

// -------------------------------------------------------------
//  Auto-ocultar la barra de tareas
// -------------------------------------------------------------
static void dexTbShow(){
  dexTbIdle = millis();
  if(dexTbTgt != 0){ dexTbFrom = dexTbOff; dexTbTgt = 0; dexTbT0 = millis(); }
}
static void dexTbHide(){
  if(!dexTbAuto || dexTbTgt == DEX_TB_H) return;
  dexTbFrom = dexTbOff; dexTbTgt = DEX_TB_H; dexTbT0 = millis();
}
static void dexTbAnimTick(){
  if(!dexTbAuto && dexTbTgt != 0){ dexTbFrom = dexTbOff; dexTbTgt = 0; dexTbT0 = millis(); }
  float d = dexTbOff - (float)dexTbTgt; if(d < 0) d = -d;
  if(d > 0.4f){
    uint32_t e = millis() - dexTbT0;
    float p = e >= DEX_TB_ANIM ? 1.0f : dexEase((float)e / (float)DEX_TB_ANIM);
    dexTbOff = dexTbFrom + ((float)dexTbTgt - dexTbFrom) * p;
    if(p >= 1.0f) dexTbOff = (float)dexTbTgt;
    dexMarkAll(); dexDirty = true;                  // la barra ocupa TODO el ancho logico
  }
  if(dexTbAuto && dexTbTgt == 0 && dexOv == DXO_NONE && !dexMenuOn &&
     dexGrab == DXG_NONE && millis() - dexTbIdle > DEX_TB_IDLE_MS) dexTbHide();
}
