// #############################################################
// ##  FLEX OS ULTRA  ·  MENU CONTEXTUAL DEL ESCRITORIO Y CAJA DE APLICACIONES
// ##  ----------------------------------------------------------
// ##  El action sheet de pulsacion larga sobre un icono y el cajon de apps
// ##  a pantalla completa, con su prioridad de toques.
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
#include "FlexOS_Ultra_PkgApps.h"   // eslabon anterior de la cadena

// #############################################################
// ##  FASE 2 - MENU CONTEXTUAL DE LONG-PRESS (estilo action sheet)
// ##  Aparece con escala+fundido interpolados sobre el escritorio. NO
// ##  recompone la pantalla entera: solo la banda inferior donde vive.
// #############################################################
// Panel unico anclado al icono, estilo hoja de acciones de iOS: una sola
// tarjeta redondeada, texto a la IZQUIERDA, glifo a la DERECHA y filas separadas
// por una linea de 1 px. No lleva fila "Cancelar": se cierra tocando fuera.
#define CTX_ROWS     3
#define CTX_ROW_H    58
#define CTX_W        244
#define CTX_RAD      20
#define CTX_GLYPH_S  26
#define CTX_PAD_L    18              // margen del texto
#define CTX_PAD_R    14              // margen del glifo
#define CTX_MARGIN   8               // aire minimo contra cualquier borde
#define CTX_GAPX     12              // separacion entre el icono y el panel
#define CTX_ICON_S   72              // lado del icono en la rejilla del Home
#define CTX_ANIM_MS  150
#define CTX_PANEL_H  (CTX_ROWS * CTX_ROW_H)
static int      ctxApp = -1, ctxAction = -1;
static bool     ctxClosing = false;
static uint32_t ctxAnimMs = 0;
static int      ctxPx = 0, ctxPy = 0;              // esquina del panel ya recortada
static int      ctxBandY0 = 0, ctxBandY1 = 0;      // banda que se recompone por frame
// Color del "hueco" de los glifos: el MISMO rol de superficie que pinta el
// panel (uiSurfaceA/UIS_ELEVATED), para que candado y rejilla no se recorten
// contra un color que el panel ya no usa.
static uint16_t ctxPanelCol(){ return uiGlass ? uiSurfTint(UIS_ELEVATED) : uiSurfFlat(UIS_ELEVATED); }
// Fila 0 = candado de app, 1 = Modo edicion, 2 = Modo kiosco. Las dos que
// necesitan una clave del sistema con la que verificar se dibujan atenuadas y
// son inertes si no hay ninguna configurada: se ve por que no se pueden usar,
// en vez de no hacer nada al tocarlas.
static bool ctxRowEnabled(int i){
  if(i == 0) return APPLOCK_ON && gLockType > 0;
  if(i == 2) return KIOSK_ON   && gLockType > 0;
  return true;
}
static const char* ctxLabel(int i){
  switch(i){
    case 0:  return appLockGet(ctxApp) ? "Desbloquear app" : "Bloquear app";
    case 1:  return "Modo edici\xC3\xB3" "n";
    default: return "Modo kiosko";
  }
}
// Glifos vectoriales de 26x26, dibujados con las primitivas que ya existen: no
// hacen falta bitmaps ni una fuente de iconos. El "hueco" de cada uno se pinta
// del color del panel, asi que siguen al tema sin logica aparte.
static void ctxGlyph(int kind, int x, int y, int s, uint8_t a){
  uint16_t hole = ctxPanelCol();
  if(kind == 0){                                        // candado
    uint16_t c = TH_DANGER;                            // bloquear app: accion destructiva
    fillCircleA(x + s / 2, y + s / 3, s / 4, c, a);     // arco
    fillCircleA(x + s / 2, y + s / 3, s / 6, hole, a);
    fillRoundRectA(x + 3, y + s / 2 - 2, s - 6, s / 2 + 1, 3, c, a);   // cuerpo
  } else if(kind == 1){                                 // rejilla de iconos (Modo edicion)
    uint16_t c = TH_TXT2;
    int q = (s - 5) / 2;
    fillRoundRectA(x,             y,             q, q, 2, c, a);
    fillRoundRectA(x + q + 5,     y,             q, q, 2, c, a);
    fillRoundRectA(x,             y + q + 5,     q, q, 2, c, a);
    fillRoundRectA(x + q + 5,     y + q + 5,     q, q, 2, c, a);
  } else {                                              // pantalla con candado (Modo kiosco)
    uint16_t c = TH_OK;
    fillRoundRectA(x, y + 1, s, s - 7, 3, c, a);        // marco
    fillRoundRectA(x + 3, y + 4, s - 6, s - 13, 2, hole, a);
    fillRoundRectA(x + s / 2 - 3, y + s / 2 - 5, 6, 7, 1, c, a);       // candado dentro
    fillRoundRectA(x + s / 3, y + s - 5, s / 3, 3, 1, c, a);           // pie
  }
}
static void ctxRender(float p){
  if(p < 0) p = 0; if(p > 1) p = 1;
  float ease = 1 - (1 - p) * (1 - p);                 // ease-out
  float sc = 0.88f + 0.12f * ease;                    // escala
  uint8_t a = (uint8_t)(255.0f * ease);               // fundido
  setBuf(bbuf);
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  // Solo la banda que ocupa el panel, no la pantalla entera. La banda se calcula
  // en ctxOpen contra la posicion FINAL (escala 1); como la escala encoge el
  // panel hacia su centro, ningun frame intermedio se sale de ella.
  for(int j = ctxBandY0; j <= ctxBandY1; j++)
    memcpy(bbuf + (size_t)j * SCR_W, homeBuf + (size_t)j * SCR_W, SCR_W * 2);
  float ccx = (float)ctxPx + CTX_W / 2.0f;
  float ccy = (float)ctxPy + CTX_PANEL_H / 2.0f;
  int px = (int)(ccx + ((float)ctxPx - ccx) * sc);
  int py = (int)(ccy + ((float)ctxPy - ccy) * sc);
  int pw = (int)(CTX_W * sc), ph = (int)(CTX_PANEL_H * sc);
  int rad = (int)(CTX_RAD * sc);
  // UNA sola tarjeta para las tres filas, con LA superficie del sistema.
  // Antes esto pintaba un relleno plano en todos los cuadros y solo el ultimo
  // (p >= 1) pasaba al vidrio real: con Liquid Glass activado, el menu de
  // pulsacion larga se veia plano y desvanecido durante toda la animacion --
  // el fallo que se reporto. Ahora es vidrio DESDE EL PRIMER CUADRO, porque
  // ctxOpen dejo la banda pre-desenfocada (uiGlassBandBegin) y uiSurfaceA solo
  // tiene que muestrearla. En Plano es el relleno solido de la paleta, con su
  // alpha: solido y visible, sin resto alguno de vidrio.
  uiSurfaceA(px, py, pw, ph, rad, UIS_ELEVATED, (uint8_t)(238 * (int)a / 255));
  int rh = ph / CTX_ROWS;
  int textMax = CTX_W - CTX_PAD_L - CTX_GLYPH_S - CTX_PAD_R - 10;
  for(int i = 0; i < CTX_ROWS; i++){
    int ry = py + i * rh;
    if(i > 0) fillRectA(px + 14, ry, pw - 28, 1, SET_TXT_MUTE, (uint8_t)(95 * (int)a / 255));  // separador
    bool en = ctxRowEnabled(i);
    const char* lb = ctxLabel(i);
    // El tamano se calcula contra el ancho FINAL, no el escalado: si se
    // recalculara por frame podria saltar a mitad de la animacion.
    int fs = uiFontFit(lb, textMax, 3);
    drawTextA(px + CTX_PAD_L, ry + rh / 2 - uiLineH(fs) / 2, lb, fs,
              en ? SET_TXT_HI : SET_TXT_MUTE, a);
    ctxGlyph(i, px + pw - CTX_PAD_R - CTX_GLYPH_S, ry + rh / 2 - CTX_GLYPH_S / 2,
             CTX_GLYPH_S, en ? a : (uint8_t)((int)a * 110 / 255));
  }
  present(ctxBandY0, ctxBandY1);
  setBuf(fb);
}
static void ctxOpen(int slot){
  // REJILLA REAL, NO UNA 4x3 FIJA. Esto daba por hecho la rejilla original
  // (12 ranuras, columnas de 120 px, filas de 112, origen 24/212). Desde que la
  // rejilla es configurable (4x3, 5x3, 4x4, 5x4 -> hasta 20 ranuras) eso era
  // doblemente incorrecto: con 5 columnas el panel se abria junto a un icono
  // que no era el pulsado, y con mas de 12 ranuras el long-press de las ultimas
  // no hacia NADA porque slot > 11 salia por la puerta de atras. Ahora la
  // geometria sale de homeSlotXY/homeGrid, las mismas que pintan el escritorio.
  if(!CTXMENU_ON || slot < 0 || slot >= homeSlotCount()) return;
  ctxApp = homeOrder[homeIdx(gHomePage, slot)];
  int ix, iy;   homeSlotXY(slot, ix, iy);
  int gS, ggx0, ggy0, gcs, grs, gcols, grows; homeGrid(gS, ggx0, ggy0, gcs, grs, gcols, grows);
  // Lado: se prefiere la DERECHA del icono, pero solo si el panel cabe entero
  // ahi; si no, la izquierda. Si no cabe en ninguno de los dos (panel mas ancho
  // de la cuenta), se elige el lado con MAS sitio antes de recortar -- asi el
  // recorte de abajo nunca acaba dejando el panel encima del icono pulsado, que
  // es justo el que el usuario necesita seguir viendo.
  int roomR = (SCR_W - CTX_MARGIN) - (ix + gS + CTX_GAPX);
  int roomL = (ix - CTX_GAPX) - CTX_MARGIN;
  bool toRight = (roomR >= CTX_W) ? true : (roomL >= CTX_W ? false : (roomR >= roomL));
  int px = toRight ? (ix + gS + CTX_GAPX) : (ix - CTX_GAPX - CTX_W);
  int py = iy;                                  // alineado con el borde superior del icono
  // RECORTE FINAL, incondicional: pase lo que pase con el lado elegido, el panel
  // entero queda dentro de pantalla. Es lo que garantiza que ningun icono de la
  // ultima fila o columna deje el menu a medias o fuera de alcance.
  if(px < CTX_MARGIN) px = CTX_MARGIN;
  if(px > SCR_W - CTX_MARGIN - CTX_W) px = SCR_W - CTX_MARGIN - CTX_W;
  if(py < CTX_MARGIN) py = CTX_MARGIN;
  if(py > SCR_H - CTX_MARGIN - CTX_PANEL_H) py = SCR_H - CTX_MARGIN - CTX_PANEL_H;
  ctxPx = px; ctxPy = py;
  ctxBandY0 = py - 2; if(ctxBandY0 < 0) ctxBandY0 = 0;
  ctxBandY1 = py + CTX_PANEL_H + 2; if(ctxBandY1 > SCR_H - 1) ctxBandY1 = SCR_H - 1;
  ctxAction = -1; ctxClosing = false;
  // VIDRIO DESDE EL PRIMER CUADRO. El fondo del menu es homeBuf y NO cambia
  // mientras dura la animacion, asi que su desenfoque se calcula UNA sola vez
  // aqui y cada cuadro solo lo muestrea (ver SUPERFICIES DEL SISTEMA). Es lo
  // que permite que el panel sea vidrio de verdad -- transparencia, tinte,
  // borde y blur -- durante toda la apertura, en vez de una capa plana
  // desvanecida hasta el ultimo cuadro.
  uiGlassBandEnd();
  if(uiGlass){
    uint16_t* ob = gBuf;
    setBuf(bbuf);
    for(int j = ctxBandY0; j <= ctxBandY1; j++)
      memcpy(bbuf + (size_t)j * SCR_W, homeBuf + (size_t)j * SCR_W, (size_t)SCR_W * 2);
    uiGlassBandBegin(ctxBandY0, ctxBandY1, uiSurfTint(UIS_ELEVATED));
    setBuf(ob);
  }
  ctxAnimMs = millis(); if(!ctxAnimMs) ctxAnimMs = 1;
  gRippleActive = false;
  gState = ST_CTX;
}
// La accion NO se ejecuta al tocar: se guarda y se ejecuta cuando termina la
// animacion de cierre, para que el panel nunca desaparezca de golpe.
static void ctxClose(int action){
  ctxAction = action; ctxClosing = true;
  ctxAnimMs = millis(); if(!ctxAnimMs) ctxAnimMs = 1;
}
static void ctxFinish(){
  int a = ctxAction, app = ctxApp;
  ctxAction = -1; ctxClosing = false; ctxAnimMs = 0; ctxApp = -1;
  uiGlassBandEnd();                    // la banda pre-desenfocada caduca con el menu
  gState = ST_HOME;
  showHome();                          // escritorio limpio en un solo volcado
  // a < 0 = cancelado (toque fuera del panel): no hay nada que hacer.
  if(a == 0 && ctxRowEnabled(0)){
    // Poner Y quitar el candado exigen clave: asi nadie desbloquea la app de
    // otro con solo tocar el icono. Misma ruta de verificacion que todo lo demas.
    lsuStartVerifyFor(appLockGet(app) ? LSU_AFTER_UNLOCKAPP : LSU_AFTER_LOCKAPP, app);
  } else if(a == 1){
    // Modo Edicion: el comportamiento de siempre, pero SIN agarrar el icono --
    // cuando se elige esta fila el dedo ya se levanto del icono hace rato.
    edEnter();
  } else if(a == 2 && ctxRowEnabled(2)){
    kioskSetEnter(app);
  }
}
static void ctxTick(){
  if(ctxAnimMs){
    uint32_t e = millis() - ctxAnimMs;
    float p = (float)e / (float)CTX_ANIM_MS; if(p > 1.0f) p = 1.0f;
    ctxRender(ctxClosing ? (1.0f - p) : p);
    if(e >= (uint32_t)CTX_ANIM_MS){
      ctxAnimMs = 0;
      if(ctxClosing) ctxFinish();
    }
    return;
  }
  if(T.tap){
    for(int i = 0; i < CTX_ROWS; i++){
      int y = ctxPy + i * CTX_ROW_H;
      if(T.x >= ctxPx && T.x <= ctxPx + CTX_W && T.y >= y && T.y <= y + CTX_ROW_H){
        if(!ctxRowEnabled(i)) return;                 // inerte: ni siquiera cierra el menu
        ctxClose(i); return;
      }
    }
    ctxClose(-1);                                     // fuera del panel -> cancelar
  }
}

// #############################################################
// ##  CAJA DE APLICACIONES  (cajon de apps, estilo One UI)
// ##  ------------------------------------------------------
// ##  Hoja a pantalla completa que sube desde el escritorio con un
// ##  deslizamiento hacia arriba y baja con uno hacia abajo, el
// ##  boton Atras o el boton Inicio. Muestra TODAS las apps
// ##  visibles del registro central (APP_REG), mientras que el
// ##  escritorio muestra solo las favoritas.
// ##
// ##  COMO DIBUJA (y por que no parpadea ni bloquea el bucle)
// ##  ------------------------------------------------------
// ##  · El fondo de la hoja -- wallpaper + capa oscura -- se compone
// ##    UNA vez en drwPage (PSRAM) y de ahi solo se hacen memcpy por
// ##    banda. Ningun filtro corre por cuadro: es la unica forma de
// ##    sostener el scroll a 60 fps en 480x800.
// ##  · Cada cuadro se compone en bbuf y se publica con present() de
// ##    una sola vez, igual que el resto del sistema.
// ##  · La animacion de subida/bajada avanza por RELOJ (millis()),
// ##    un paso por vuelta de loop(). No hay delay() ni bucles
// ##    propios: si el sistema va lento, la animacion pierde cuadros
// ##    pero NUNCA dura mas ni congela el tactil.
// ##  · Con la hoja quieta solo se recompone la BANDA de la rejilla,
// ##    y dentro de ella solo se dibujan las filas visibles.
// ##
// ##  PRIORIDAD DE TOQUES
// ##  ------------------------------------------------------
// ##  drawerTick() se llama desde el switch de gState, es decir
// ##  DESPUES de notifHandleTouch(), flexOtaTouchBridge() y
// ##  qsGlobalHandle(): cualquier overlay global (OTA, isla de
// ##  notificaciones, panel rapido) ya vio el toque antes. Ademas,
// ##  si una capa OTA es duena de la pantalla, la caja ni dibuja ni
// ##  escucha (ver la primera guarda de drawerTick).
// #############################################################

// ---- Geometria (fija para el panel vertical de 480x800) ----
#define DRW_ANIM_MS    240        // subida/bajada
#define DRW_ICON_S     72         // mismo tamano que la rejilla del escritorio
#define DRW_COL_X0     24
#define DRW_COL_STEP   120        // 4 columnas: 24 + 3*120 + 72 = 456 <= 480
#define DRW_ROW_STEP   116        // icono + nombre + aire
#define DRW_GRID_TOP   152
#define DRW_NAV_H      76         // banda inferior (botones o barra de gestos)
#define DRW_SEARCH_Y   74
#define DRW_SEARCH_H   58
#define DRW_SEARCH_R   29
#define DRW_SHEET_RAD  28         // esquinas superiores redondeadas de la hoja
#define DRW_KB_H       178        // teclado del buscador (solo cuando esta abierto)
#define DRW_LP_MS      600        // pulsacion larga -> menu contextual
#define DRW_CTX_ROWS   4
#define DRW_CTX_ROW_H  56
#define DRW_CTX_W      262
#define DRW_CTX_RAD    20
#define DRW_QMAX       14         // caracteres del buscador
#define DRW_LABEL_MAX  40         // bytes de la etiqueta ya truncada de un icono

// ---- Estado ----
static uint16_t* drwPage    = NULL;   // fondo de la hoja ya compuesto (PSRAM, se reutiliza)
static int       drwPageSig = -1;     // apariencia con la que se compuso drwPage
static bool      drwOn      = false;  // la caja es la pantalla activa
static int       drwAnim    = 0;      // 0 quieta · 1 subiendo · 2 bajando
static uint32_t  drwAnimMs  = 0;
static float     drwSlide   = (float)SCR_H;   // 0 = abierta del todo · SCR_H = fuera de pantalla
static int       drwLastSlide = SCR_H;        // para calcular la banda sucia del cuadro
static float     drwScroll  = 0.0f;   // desplazamiento de la rejilla (px)
static float     drwVel     = 0.0f;   // inercia (px/s)
static bool      drwDrag    = false;
static int       drwDragY0  = 0, drwDragLastY = 0;
static float     drwDragS0  = 0.0f;
static uint32_t  drwDragMs  = 0;
static bool      drwMoved   = false;
// LISTA VISUAL UNIFICADA. Una sola rejilla: las apps nativas y las descargadas
// van mezcladas y ordenadas por nombre, como aplicaciones normales. No hay
// seccion, ni titulo, ni contador, ni separador.
//
// CADA CELDA DICE DE QUE TIPO ES. `idx` significa una cosa u otra segun `kind`:
// para DRW_NATIVE es un id de APP_REG (0..APP_N-1) y para DRW_PKG es un indice
// dentro de pkgApps[]. Son dos espacios de numeros DISTINTOS que casualmente
// empiezan en 0, asi que nunca se guardan juntos en un mismo entero: el tipo
// viaja siempre al lado. Todo lo que hace la caja -- dibujar, abrir, el menu de
// pulsacion larga -- se decide por `kind`, jamas por el valor de `idx`.
enum DrwKind : uint8_t { DRW_NATIVE = 0, DRW_PKG = 1 };
struct DrwCell { uint8_t kind; int16_t idx; };
static DrwCell   drwCells[APP_N + PKGAPP_MAX];
static int       drwN = 0;                    // celdas visibles tras filtrar
static bool      drwAnyPkg = false;           // hay alguna descargada en la lista
static char      drwQuery[DRW_QMAX + 1] = { 0 };
static int       drwQLen    = 0;
static bool      drwKbOn    = false;  // teclado del buscador desplegado
static bool      drwShowHid = false;  // "ver apps ocultas" (unica via para volver a mostrarlas)
static bool      drwCtxOn   = false;  // menu contextual de pulsacion larga
static int       drwCtxApp  = -1;     // menu abierto sobre una NATIVA: id de APP_REG
static int       drwCtxPkg  = -1;     // ...o sobre una DESCARGADA: indice en pkgApps[]
static int       drwCtxX = 0, drwCtxY = 0;
static bool      drwConfOn  = false;  // confirmacion de desinstalacion
static const char* drwInfoErr = NULL; // motivo a la vista en la ficha (NULL = ninguno)
static bool      drwInfoOn  = false;  // ficha "Informacion"
static int       drwPendApp = -1;     // app a abrir cuando termine la bajada
static bool      drwPendSw  = false;  // ir a Recientes cuando termine la bajada
static bool      drwFull    = true;   // hay que recomponer la pantalla entera
static bool      drwDirty   = true;   // hay que recomponer la banda de la rejilla
static uint32_t  drwFrameMs = 0;
static int       drwLpCell  = -1;     // celda bajo el dedo (para no repetir el long-press)
static int       drwInfoPkg = -1;     // la ficha abierta es de una descargada (indice), -1 = nativa

// ---- Geometria derivada ----
// Una sola rejilla de 4 columnas y paso uniforme, como antes de que existieran
// las apps descargadas. Al unificar la lista desaparece la fila de cabecera de
// altura distinta, y con ella toda la aritmetica especial que hacia falta para
// saber donde caia cada fila.
static inline int drwGridBot(){ return drwKbOn ? (SCR_H - DRW_KB_H - 8) : (SCR_H - DRW_NAV_H); }
static inline int drwRows(){ return (drwN + 3) / 4; }
static int drwMaxScroll(){
  int content = drwRows() * DRW_ROW_STEP + 16;
  int view    = drwGridBot() - DRW_GRID_TOP;
  int m = content - view;
  return m > 0 ? m : 0;
}
static void drwClampScroll(){
  int m = drwMaxScroll();
  if(drwScroll < 0){ drwScroll = 0; drwVel = 0; }
  if(drwScroll > (float)m){ drwScroll = (float)m; drwVel = 0; }
}
// Rectangulo de la celda i EN COORDENADAS DE CONTENIDO (sin scroll ni hoja).
static void drwCellXY(int i, int &x, int &y){
  x = DRW_COL_X0 + (i % 4) * DRW_COL_STEP;
  y = DRW_GRID_TOP + (i / 4) * DRW_ROW_STEP;
}
// Indice de la celda bajo (px,py) en coordenadas de PANTALLA, o -1.
static int drwHitCell(int px, int py){
  if(py < DRW_GRID_TOP || py >= drwGridBot()) return -1;
  int cy = py + (int)drwScroll - (int)drwSlide;
  int r = (cy - DRW_GRID_TOP) / DRW_ROW_STEP;
  if(r < 0 || (cy - DRW_GRID_TOP) < 0) return -1;
  int c = (px - DRW_COL_X0) / DRW_COL_STEP;
  if(px < DRW_COL_X0 || c < 0 || c > 3) return -1;
  int cellX = DRW_COL_X0 + c * DRW_COL_STEP;
  if(px > cellX + DRW_ICON_S + 12) return -1;             // pasillo entre columnas
  int i = r * 4 + c;
  return (i >= 0 && i < drwN) ? i : -1;
}

// ---- Acceso a una celda, SIEMPRE por su tipo ----------------------------
// Estas cuatro funciones son la unica puerta entre la rejilla y las dos fuentes
// de apps. Fuera de aqui nadie vuelve a mirar `idx` a pelo.
static inline bool drwCellOk(int i){ return i >= 0 && i < drwN; }
static inline bool drwIsPkg(int i){ return drwCellOk(i) && drwCells[i].kind == DRW_PKG; }
// Id de APP_REG de una celda nativa, o -1 si la celda no es nativa.
static int drwNativeId(int i){
  if(!drwCellOk(i) || drwCells[i].kind != DRW_NATIVE) return -1;
  int id = drwCells[i].idx;
  return (id >= 0 && id < APP_N) ? id : -1;
}
// Indice VIVO en pkgApps[] de una celda descargada, o -1. Se revalida contra
// pkgAppsN porque el registro puede haberse rehecho desde que se filtro.
static int drwPkgIndex(int i){
  if(!drwCellOk(i) || drwCells[i].kind != DRW_PKG) return -1;
  int e = drwCells[i].idx;
  return (e >= 0 && e < pkgAppsN) ? e : -1;
}
// Nombre visible de una celda. NULL si la celda ya no apunta a nada real.
static const char* drwCellName(int i){
  int id = drwNativeId(i); if(id >= 0) return appName(id);
  int e  = drwPkgIndex(i); if(e  >= 0) return pkgApps[e].name;
  return NULL;
}

// ---- Filtro: el UNICO sitio donde se decide que apps entran en la caja ----
// Sale del registro central: id 0..15, fuera las ocultas (salvo en modo "ver
// ocultas") y fuera las que no casen con el buscador. dexMatch() ya existe y
// hace exactamente esta comparacion sin distinguir mayusculas; se reutiliza en
// vez de escribir una segunda.
static void drwFilter(){
  drwN = 0; drwAnyPkg = false;
  // 1. Las nativas, del registro central de siempre.
  for(int id = 0; id < APP_N && drwN < (int)(sizeof(drwCells) / sizeof(drwCells[0])); id++){
    if(appIsHidden(id) && !drwShowHid) continue;
    if(!dexMatch(appName(id), drwQuery, drwQLen)) continue;
    drwCells[drwN].kind = DRW_NATIVE; drwCells[drwN].idx = (int16_t)id; drwN++;
  }
  // 2. Las descargadas, del registro real de /FlexApps. pkgAppsEnsure() solo
  //    recorre el almacenamiento cuando la revision cambio (instalar,
  //    actualizar, desinstalar, detener, arrancar); el resto de las veces
  //    compara un entero y vuelve. Filtrar despues es recorrer un vector de 24
  //    entradas que ya esta en RAM.
  pkgAppsEnsure();
  for(int i = 0; i < pkgAppsN && drwN < (int)(sizeof(drwCells) / sizeof(drwCells[0])); i++){
    if(pkgAppHidden(i) && !drwShowHid) continue;
    if(!dexMatch(pkgApps[i].name, drwQuery, drwQLen)) continue;
    drwCells[drwN].kind = DRW_PKG; drwCells[drwN].idx = (int16_t)i; drwN++;
    drwAnyPkg = true;
  }
  // 3. UN SOLO ORDEN, POR NOMBRE, SIN DISTINGUIR ORIGEN. Una app descargada se
  //    coloca entre las nativas como cualquier otra. La ordenacion es estable:
  //    a igualdad de nombre manda el tipo y luego el indice, asi que la rejilla
  //    no puede bailar entre dos aperturas.
  for(int i = 1; i < drwN; i++){
    DrwCell key = drwCells[i];
    const char* kn = (key.kind == DRW_NATIVE) ? appName(key.idx) : pkgApps[key.idx].name;
    int j = i - 1;
    while(j >= 0){
      const DrwCell& a = drwCells[j];
      const char* an = (a.kind == DRW_NATIVE) ? appName(a.idx) : pkgApps[a.idx].name;
      int cmp = pkgAppNameCmp(an, kn);
      if(cmp == 0) cmp = (int)a.kind - (int)key.kind;
      if(cmp == 0) cmp = (int)a.idx - (int)key.idx;
      if(cmp <= 0) break;
      drwCells[j + 1] = drwCells[j]; j--;
    }
    drwCells[j + 1] = key;
  }
}

// ---- Fondo de la hoja -------------------------------------------------
// Wallpaper + capa oscura, compuesto UNA vez en PSRAM y reutilizado en cada
// apertura. La firma (tema + vidrio) detecta el unico caso en que hay que
// rehacerlo: que el usuario haya cambiado la apariencia en Ajustes. Si no hay
// PSRAM para la pagina se cae con elegancia a blurBg (el velo que ya usan
// Recientes y el apagado) y, en ultimo extremo, al propio escritorio: la caja
// sigue funcionando, solo pierde el oscurecido extra.
static inline int drwSig(){ return (gDark ? 1 : 0) | (uiGlass ? 2 : 0); }
static void drwBuildPage(){
  if(drwPage && drwPageSig == drwSig()) return;
  if(!drwPage) drwPage = (uint16_t*)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!drwPage) return;                                    // sin PSRAM: se usara blurBg/homeBuf
  ensureBlurBg();
  uiRenderCooperate();
  const uint16_t* src = blurBg ? blurBg : homeBuf;
  if(!src){ heap_caps_free(drwPage); drwPage = NULL; return; }
  memcpy(drwPage, src, (size_t)SCR_W * SCR_H * 2);
  // Velo extra: la caja tapa el escritorio entero y lleva mucho texto encima,
  // asi que necesita mas contraste que Recientes. Se aplica AQUI, una sola vez,
  // y no por cuadro (que es justo lo que no se puede hacer a 60 fps).
  uint16_t* old = gBuf;
  bool wl = gLand; gLand = false;
  int sx0 = gClipX0, sx1 = gClipX1, sy0 = gClipY0, sy1 = gClipY1;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  setBuf(drwPage);
  fillRectA(0, 0, SCR_W, SCR_H, rgb565(6, 8, 16), 96);
  uiRenderCooperate();
  setBuf(old);
  gLand = wl;
  gClipX0 = sx0; gClipX1 = sx1; gClipY0 = sy0; gClipY1 = sy1;
  drwPageSig = drwSig();
}
static inline const uint16_t* drwBgSrc(){
  if(drwPage) return drwPage;
  if(blurBg)  return blurBg;
  return homeBuf;
}

// ---- Dibujo de las piezas ---------------------------------------------
// Lupa vectorial del buscador (sin bitmaps, como el resto del sistema).
static void drwGlyphSearch(int cx, int cy, int r, uint16_t col){
  drawCircle(cx, cy, r, col);
  drawCircle(cx, cy, r - 1, col);
  strokeSegAA((float)(cx + r * 7 / 10), (float)(cy + r * 7 / 10),
              (float)(cx + r * 15 / 10), (float)(cy + r * 15 / 10), 1.6f, col);
}
// Ojo: interruptor de "ver apps ocultas". Tachado cuando esta apagado.
static void drwGlyphEye(int cx, int cy, int s, bool on, uint16_t col){
  int w = s, h = s / 2;
  for(int i = -w / 2; i <= w / 2; i++){
    float t = (float)i / (float)(w / 2);
    int dy = (int)(h * 0.5f * (1.0f - t * t));
    pxA(cx + i, cy - dy, col, 235);
    pxA(cx + i, cy + dy, col, 235);
  }
  fillCircleA(cx, cy, s / 6, col, 235);
  if(!on) strokeSegAA((float)(cx - w / 2), (float)(cy + h / 2), (float)(cx + w / 2), (float)(cy - h / 2), 1.6f, col);
}
// Buscador grande estilo One UI. 'settled' distingue el cuadro final (vidrio
// real) de los de la animacion (relleno plano): drawLiquidGlassPanel hace un
// blur de verdad y no puede correr en cada cuadro de una animacion.
static void drwDrawSearch(int sy, bool settled){
  int x = 24, y = DRW_SEARCH_Y + sy, w = SCR_W - 48, h = DRW_SEARCH_H;
  // Si el pildoro no cae en la banda que se esta componiendo, ni se toca. Es lo
  // que evita que un cuadro de scroll -- que solo recompone la rejilla -- pague
  // el blur del vidrio, y ademas drawLiquidGlassPanel no respeta gClipY: sin
  // esta guarda escribiria en bbuf fuera de la banda publicada.
  if(y + h - 1 < gClipY0 || y > gClipY1) return;
  if(uiGlass && settled) drawLiquidGlassPanel(x, y, w, h, DRW_SEARCH_R, TH_GLASS2);
  else                   fillRoundRectA(x, y, w, h, DRW_SEARCH_R, TH_SURF, 150);
  drwGlyphSearch(x + 30, y + h / 2, 10, TH_ONWALL);
  if(drwQLen > 0) drawText(x + 54, y + h / 2 - 8, drwQuery, 2, TH_ONWALL);
  else            drawText(x + 54, y + h / 2 - 8, "Buscar aplicaciones", 2, TH_ONWALL2);
  drwGlyphEye(x + w - 32, y + h / 2, 22, drwShowHid, drwShowHid ? TH_PRIM : TH_ONWALL2);
}
// Teclado compacto del buscador. Es propio de la caja: no toca ni el teclado
// del sistema (Notas) ni el del OOBE, asi que no puede desconfigurarlos.
static const char* DRW_KB_ROW[3] = { "qwertyuiop", "asdfghjkl", "zxcvbnm" };
static void drwKbKeyRect(int row, int col, int sy, int &x, int &y, int &w, int &h){
  int n = (int)strlen(DRW_KB_ROW[row]);
  w = 42; h = 46;
  int gap = 4, total = n * w + (n - 1) * gap;
  x = (SCR_W - total) / 2 + col * (w + gap);
  y = SCR_H - DRW_KB_H + 14 + row * (h + 6) + sy;
  if(row == 2){ x += 54; }                 // hueco a los lados para cerrar y borrar
}
static void drwKbSideRect(bool erase, int sy, int &x, int &y, int &w, int &h){
  int kx, ky, kw, kh; drwKbKeyRect(2, 0, sy, kx, ky, kw, kh);
  w = 50; h = kh; y = ky;
  x = erase ? (SCR_W - 12 - w) : 12;
}
static void drwDrawKeyboard(int sy){
  int top = SCR_H - DRW_KB_H + sy;
  if(top + DRW_KB_H - 1 < gClipY0 || top > gClipY1) return;
  fillRectA(0, top, SCR_W, DRW_KB_H, rgb565(0, 0, 0), 120);
  for(int r = 0; r < 3; r++){
    int n = (int)strlen(DRW_KB_ROW[r]);
    for(int c = 0; c < n; c++){
      int x, y, w, h; drwKbKeyRect(r, c, sy, x, y, w, h);
      fillRoundRectA(x, y, w, h, 10, TH_SURF, 170);
      char k[2] = { DRW_KB_ROW[r][c], 0 };
      drawTextC(x + w / 2, y + h / 2 - 8, k, 2, TH_ONWALL);
    }
  }
  int x, y, w, h;
  drwKbSideRect(false, sy, x, y, w, h);                   // cerrar teclado
  fillRoundRectA(x, y, w, h, 10, TH_SURF, 120);
  strokeSegAA((float)(x + w / 2 - 9), (float)(y + h / 2 - 4), (float)(x + w / 2), (float)(y + h / 2 + 5), 2.0f, TH_ONWALL2);
  strokeSegAA((float)(x + w / 2 + 9), (float)(y + h / 2 - 4), (float)(x + w / 2), (float)(y + h / 2 + 5), 2.0f, TH_ONWALL2);
  drwKbSideRect(true, sy, x, y, w, h);                    // borrar
  fillRoundRectA(x, y, w, h, 10, TH_SURF, 120);
  strokeSegAA((float)(x + 14), (float)(y + h / 2), (float)(x + w - 14), (float)(y + h / 2), 2.0f, TH_ONWALL);
}
// Banda inferior: los MISMOS botones (o la misma barra de gestos) que el
// escritorio, para que Atras e Inicio esten donde el usuario ya los busca.
static void drwDrawNav(int sy){
  if(drwKbOn) return;
  if(SCR_H - DRW_NAV_H + sy > gClipY1 || SCR_H - 1 + sy < gClipY0) return;
  if(gNavMode == 0){
    int ny = SCR_H - 52 + sy; uint16_t nv = TH_ONWALL;
    int bx = SCR_W / 6;
    fillTriangle(bx - 10, ny + 8, bx + 8, ny - 2, bx + 8, ny + 18, nv);
    drawCircle(SCR_W / 2, ny + 8, 12, nv); drawCircle(SCR_W / 2, ny + 8, 11, nv);
    int rx = SCR_W * 5 / 6;
    drawRoundRect(rx - 11, ny - 3, 22, 22, 4, nv);
  } else drawHomeIndicator(SCR_H + sy, 220);
}
// Cabecera: hora, fecha corta y estado, como en el escritorio.
static void drwDrawStatus(int sy){
  if(64 + sy < gClipY0 || sy > gClipY1) return;
  char cs[12]; clkStrBar(cs, sizeof(cs));
  drawText(20, 16 + sy, cs, 2, TH_ONWALL);
  char sd[48]; buildShortDate(sd, sizeof(sd));
  drawText(20, 40 + sy, sd, 1, TH_ONWALL2);
  drawWifi(SCR_W - 66, 28 + sy, 11, TH_ONWALL);
  drawBattery(SCR_W - 46, 20 + sy, 30, 15, 82, TH_ONWALL);
}
// Rejilla: SOLO las filas que caen dentro de la banda visible. Con 16 apps la
// diferencia es pequena, pero la regla es la que hace que la caja siga siendo
// fluida cuando el registro crezca.
static void drwDrawGrid(int sy, int y0, int y1){
  int gTop = DRW_GRID_TOP + sy, gBot = drwGridBot() + sy;
  int c0 = gClipY0, c1 = gClipY1;
  gClipY0 = (y0 > gTop) ? y0 : gTop;
  gClipY1 = (y1 < gBot - 1) ? y1 : gBot - 1;
  if(gClipY0 <= gClipY1){
    // ICONOS PLANOS A PROPOSITO. drawLiquidGlassPanel indexa el buffer a pelo y
    // NO respeta gClipY, asi que un icono a medio salir de la banda pintaria
    // fuera de ella; y ademas son hasta 20 blancos por cuadro, que no caben en
    // el presupuesto de un scroll a 60 fps. El estilo Vidrio se conserva donde
    // si es viable: buscador, menu contextual y ficha de informacion.
    int oldStyle = gIconStyle; gIconStyle = 0;
    int scroll = (int)drwScroll;
    // UNA sola consulta a Flex Store por cuadro, aqui, fuera del bucle de
    // iconos. Preguntarlo por icono tomaba su mutex con espera infinita desde
    // el hilo grafico (ver pkgAppSampleBusy).
    if(drwAnyPkg) pkgAppSampleBusy();
    int r0 = (scroll - DRW_ROW_STEP) / DRW_ROW_STEP; if(r0 < 0) r0 = 0;
    int r1 = (scroll + (gBot - gTop)) / DRW_ROW_STEP + 1;
    int rows = drwRows(); if(r1 > rows - 1) r1 = rows - 1;
    for(int r = r0; r <= r1; r++){
      for(int c = 0; c < 4; c++){
        int i = r * 4 + c;
        if(i < 0 || i >= drwN) break;
        int cx, cy; drwCellXY(i, cx, cy);
        int ix = cx, iy = cy - scroll + sy;
        if(iy > gClipY1 || iy + DRW_ROW_STEP < gClipY0) continue;

        // EL TIPO DE LA CELDA DECIDE TODO. Nunca se usa `idx` sin saber antes
        // si es un id de APP_REG o un indice de pkgApps.
        const char* nm = NULL;
        bool atenuada = false;
        int id = drwNativeId(i);
        if(id >= 0){
          drawAppIcon(id, ix, iy, DRW_ICON_S);
          if(appIsHidden(id)){
            fillRoundRectA(ix, iy, DRW_ICON_S, DRW_ICON_S, DRW_ICON_S * 22 / 100, rgb565(0,0,0), 130);
            atenuada = true;
          }
          if(APPLOCK_ON && appLockGet(id))
            fillRoundRectA(ix + DRW_ICON_S - 18, iy + DRW_ICON_S - 18, 16, 16, 5, TH_DANGER, 230);
          nm = appName(id);
        } else {
          int e = drwPkgIndex(i);
          if(e < 0) continue;                      // el registro se rehizo: celda muerta
          pkgAppDrawIcon(e, ix, iy, DRW_ICON_S);
          if(pkgAppHidden(e)){
            fillRoundRectA(ix, iy, DRW_ICON_S, DRW_ICON_S, DRW_ICON_S * 22 / 100, rgb565(0,0,0), 130);
            atenuada = true;
          }
          // ESTADO, SIN BLOQUEAR NADA. Una app que se esta actualizando o que
          // no se puede abrir se marca con un punto y sigue en su sitio: la
          // rejilla no se reordena ni se para por ella.
          uint8_t st = pkgAppStatus(e);
          if(st != PKGAPP_ST_OK){
            uint16_t dot = (st == PKGAPP_ST_UPDATING) ? TH_PRIM : TH_DANGER;
            fillCircleA(ix + DRW_ICON_S - 9, iy + 9, 7, dot, 235);
            if(st == PKGAPP_ST_ERROR) atenuada = true;
          }
          nm = pkgApps[e].name;
        }

        // ETIQUETA QUE NO SE SALE DE SU COLUMNA. Primero se intenta encoger la
        // fuente (uiFontFit, como siempre); si aun asi no cabe, uiLabelFit
        // corta por un limite de caracter UTF-8 y termina en "...". Mismo
        // tratamiento para nativas y descargadas.
        const int lblW = DRW_COL_STEP - 14;
        int fs = uiFontFit(nm, lblW, 2);
        char lbl[DRW_LABEL_MAX];
        uiLabelFit(nm, lblW, fs, lbl, sizeof(lbl));
        drawTextC(ix + DRW_ICON_S / 2, iy + DRW_ICON_S + 8, lbl, fs,
                  atenuada ? TH_ONWALL2 : TH_ONWALL);
      }
    }
    gIconStyle = oldStyle;
    if(drwN == 0)
      drawTextC(SCR_W / 2, gTop + 60, "Sin resultados", 3, TH_ONWALL2);
  }
  gClipY0 = c0; gClipY1 = c1;
}

// ---- Composicion de una banda ------------------------------------------
// Unico punto que sabe como se ve la caja. Lo usan el cuadro normal, la
// animacion, el scroll, el menu contextual y la ficha de informacion, asi que
// no hay dos maneras distintas de dibujar lo mismo.
static void drwCompose(int y0, int y1, bool settled){
  if(y0 < 0) y0 = 0; if(y1 > SCR_H - 1) y1 = SCR_H - 1; if(y0 > y1) return;
  setBuf(bbuf);
  int c0 = gClipY0, c1 = gClipY1, cx0 = gClipX0, cx1 = gClipX1;
  gClipY0 = y0; gClipY1 = y1; gClipX0 = 0; gClipX1 = SCR_W - 1;
  int sy = (int)drwSlide;
  const uint16_t* bg = drwBgSrc();
  for(int j = y0; j <= y1; j++){
    const uint16_t* src = (j < sy) ? homeBuf : bg;
    if(!src) src = homeBuf;
    if(src) memcpy(bbuf + (size_t)j * SCR_W, src + (size_t)j * SCR_W, SCR_W * 2);
  }
  // Esquinas superiores redondeadas: en las primeras filas de la hoja se
  // devuelve el escritorio a los lados, asi el borde no es un corte recto.
  if(homeBuf && sy > 0 && sy < SCR_H){
    for(int k = 0; k < DRW_SHEET_RAD; k++){
      int j = sy + k; if(j < y0 || j > y1 || j >= SCR_H) continue;
      int ins = rrInset(k, SCR_H, DRW_SHEET_RAD);
      if(ins <= 0) continue;
      memcpy(bbuf + (size_t)j * SCR_W, homeBuf + (size_t)j * SCR_W, (size_t)ins * 2);
      memcpy(bbuf + (size_t)j * SCR_W + (SCR_W - ins), homeBuf + (size_t)j * SCR_W + (SCR_W - ins), (size_t)ins * 2);
    }
  }
  drwDrawStatus(sy);
  drwDrawSearch(sy, settled);
  drwDrawGrid(sy, y0, y1);
  if(drwKbOn) drwDrawKeyboard(sy);
  drwDrawNav(sy);
  gClipY0 = c0; gClipY1 = c1; gClipX0 = cx0; gClipX1 = cx1;
}

// ---- Menu contextual de pulsacion larga --------------------------------
// Cuatro acciones, las cuatro reales: abrir, favorita si/no, visible si/no e
// informacion. Vive DENTRO de ST_DRAWER (no es un gState nuevo) para no tocar
// el menu contextual del escritorio, que sigue exactamente igual.
static bool drwHomeHasSlot(){ return homeFirstFree() >= 0; }

// MENU DE PULSACION LARGA, PARA LOS DOS TIPOS DE APP.
// Una app nativa tiene cuatro acciones (las de siempre). Una app descargada
// tiene ademas Desinstalar, porque es lo unico que se puede hacer con ella y no
// con una del sistema. Todo lo demas -- el aspecto, el vidrio, la colocacion
// junto al icono, las filas atenuadas -- es identico: para el usuario es el
// mismo menu.
#define DRW_CTX_OPEN    0
#define DRW_CTX_HOME    1
#define DRW_CTX_HIDE    2
#define DRW_CTX_INFO    3
#define DRW_CTX_UNINST  4
static inline int drwCtxRows(){ return (drwCtxPkg >= 0) ? 5 : 4; }

// Nombre de la app del menu, sea del tipo que sea.
static const char* drwCtxName(){
  if(drwCtxPkg >= 0 && drwCtxPkg < pkgAppsN) return pkgApps[drwCtxPkg].name;
  if(drwCtxApp >= 0 && drwCtxApp < APP_N)    return appName(drwCtxApp);
  return "";
}
static bool drwCtxInHome(){
  if(drwCtxPkg >= 0) return pkgAppInHome(drwCtxPkg);
  return appIsFav(drwCtxApp);
}
static bool drwCtxIsHidden(){
  if(drwCtxPkg >= 0) return pkgAppHidden(drwCtxPkg);
  return appIsHidden(drwCtxApp);
}
static const char* drwCtxLabel(int i){
  switch(i){
    case DRW_CTX_OPEN: return "Abrir";
    case DRW_CTX_HOME: if(drwCtxInHome()) return "Quitar de inicio";
                       return drwHomeHasSlot() ? "A\xC3\xB1" "adir a inicio" : "Inicio completo";
    case DRW_CTX_HIDE: return drwCtxIsHidden() ? "Mostrar" : "Ocultar";
    case DRW_CTX_INFO: return "Informaci\xC3\xB3n";
    default:           return "Desinstalar";
  }
}
static bool drwCtxEnabled(int i){
  // Una fila inerte se dibuja atenuada y no responde: se VE por que no se
  // puede usar, en vez de aceptar el toque y no hacer nada.
  if(i == DRW_CTX_HOME) return drwCtxInHome() || (drwHomeHasSlot() && !drwCtxIsHidden());
  if(i == DRW_CTX_HIDE) return (drwCtxPkg >= 0) ? true : appCanHide(drwCtxApp);   // Ajustes no se puede ocultar
  // Desinstalar SOLO existe para una app descargada. Ni Flex Store ni ninguna
  // otra app del sistema llega nunca a esta fila: no se dibuja siquiera.
  if(i == DRW_CTX_UNINST) return drwCtxPkg >= 0;
  return true;
}
static void drwCtxGlyph(int kind, int x, int y, int s, uint16_t col){
  if(kind == DRW_CTX_OPEN){                          // flecha de abrir
    strokeSegAA((float)x, (float)(y + s / 2), (float)(x + s), (float)(y + s / 2), 2.0f, col);
    strokeSegAA((float)(x + s - 9), (float)(y + s / 2 - 8), (float)(x + s), (float)(y + s / 2), 2.0f, col);
    strokeSegAA((float)(x + s - 9), (float)(y + s / 2 + 8), (float)(x + s), (float)(y + s / 2), 2.0f, col);
  } else if(kind == DRW_CTX_HOME){                   // circulo (en inicio)
    fillCircleA(x + s / 2, y + s / 2, s / 3, col, 230);
    fillCircleA(x + s / 2, y + s / 2, s / 3 - 4, uiGlass ? TH_GLASS : TH_SURF, 200);
  } else if(kind == DRW_CTX_HIDE){                   // ojo
    drwGlyphEye(x + s / 2, y + s / 2, s - 4, !drwCtxIsHidden(), col);
  } else if(kind == DRW_CTX_INFO){                   // "i" de informacion
    drawCircle(x + s / 2, y + s / 2, s / 2 - 1, col);
    fillRect(x + s / 2 - 1, y + s / 2 - 5, 2, 10, col);
    fillRect(x + s / 2 - 1, y + s / 2 - 9, 2, 2, col);
  } else {                                           // papelera
    int w = s - 8, hh = s - 8, bx = x + 4, by = y + 6;
    drawRoundRect(bx, by + 4, w, hh - 4, 3, col);
    fillRect(bx - 2, by, w + 4, 2, col);
    fillRect(bx + w / 2 - 4, by - 3, 8, 3, col);
    fillRect(bx + w / 3, by + 9, 2, hh - 16, col);
    fillRect(bx + 2 * w / 3, by + 9, 2, hh - 16, col);
  }
}
static void drwCtxDraw(){
  int rows = drwCtxRows();
  int h = rows * DRW_CTX_ROW_H;
  if(uiGlass) drawLiquidGlassPanel(drwCtxX, drwCtxY, DRW_CTX_W, h, DRW_CTX_RAD, TH_GLASS);
  else        fillRoundRectA(drwCtxX, drwCtxY, DRW_CTX_W, h, DRW_CTX_RAD, TH_SURF, 242);
  for(int i = 0; i < rows; i++){
    int ry = drwCtxY + i * DRW_CTX_ROW_H;
    if(i > 0) fillRectA(drwCtxX + 14, ry, DRW_CTX_W - 28, 1, TH_TXT2, 80);
    bool en = drwCtxEnabled(i);
    // Desinstalar se dibuja en rojo: es la unica accion del menu que borra algo.
    uint16_t col = !en ? TH_TXT2 : (i == DRW_CTX_UNINST ? TH_DANGER : TH_TXT);
    const char* lb = drwCtxLabel(i);
    int fs = uiFontFit(lb, DRW_CTX_W - 76, 3);
    char lbl[DRW_LABEL_MAX];
    uiLabelFit(lb, DRW_CTX_W - 76, fs, lbl, sizeof(lbl));
    drawText(drwCtxX + 18, ry + DRW_CTX_ROW_H / 2 - uiLineH(fs) / 2, lbl, fs, col);
    drwCtxGlyph(i, drwCtxX + DRW_CTX_W - 44, ry + DRW_CTX_ROW_H / 2 - 13, 26, col);
  }
}
static void drwCtxBand(int &y0, int &y1){
  y0 = drwCtxY - 4; y1 = drwCtxY + drwCtxRows() * DRW_CTX_ROW_H + 4;
  if(y0 < 0) y0 = 0; if(y1 > SCR_H - 1) y1 = SCR_H - 1;
}
static void drwCtxOpen(int cell){
  if(!drwCellOk(cell)) return;
  // AQUI SE FIJA EL TIPO, Y NO SE VUELVE A ADIVINAR. A partir de este punto el
  // menu entero trabaja con drwCtxApp (id de APP_REG) o con drwCtxPkg (indice
  // en pkgApps), y exactamente uno de los dos vale.
  drwCtxApp = drwNativeId(cell);
  drwCtxPkg = drwPkgIndex(cell);
  if(drwCtxApp < 0 && drwCtxPkg < 0) return;
  int cx, cy; drwCellXY(cell, cx, cy);
  int iy = cy - (int)drwScroll;
  int px = cx + DRW_ICON_S + 12;
  int h  = drwCtxRows() * DRW_CTX_ROW_H;
  if(px + DRW_CTX_W > SCR_W - 8) px = cx - 12 - DRW_CTX_W;   // no cabe a la derecha
  if(px < 8) px = 8;
  if(px > SCR_W - 8 - DRW_CTX_W) px = SCR_W - 8 - DRW_CTX_W;
  int py = iy;
  if(py < DRW_GRID_TOP) py = DRW_GRID_TOP;
  if(py > SCR_H - 8 - h) py = SCR_H - 8 - h;
  drwCtxX = px; drwCtxY = py;
  drwCtxOn = true; drwVel = 0; drwDrag = false;
  drwFull = true;
}

// ---- Confirmacion de desinstalacion -------------------------------------
// Borrar una app es lo unico irreversible que se puede hacer desde la caja, asi
// que se pregunta antes y se dice CUAL, con su nombre: "Desinstalar" a secas
// sobre una rejilla de iconos es demasiado facil de tocar sin querer.
#define DRW_CONF_W 348
#define DRW_CONF_H 208
static void drwConfBand(int &y0, int &y1){
  y0 = (SCR_H - DRW_CONF_H) / 2 - 4; y1 = y0 + DRW_CONF_H + 8;
  if(y0 < 0) y0 = 0; if(y1 > SCR_H - 1) y1 = SCR_H - 1;
}
static void drwConfBtnRects(int &cx0, int &cy0, int &cw, int &ch, int &ux0){
  int x = (SCR_W - DRW_CONF_W) / 2, y = (SCR_H - DRW_CONF_H) / 2;
  cw = (DRW_CONF_W - 48) / 2; ch = 46;
  cy0 = y + DRW_CONF_H - 26 - ch;
  cx0 = x + 16; ux0 = x + DRW_CONF_W - 16 - cw;
}
static void drwConfDraw(){
  int x = (SCR_W - DRW_CONF_W) / 2, y = (SCR_H - DRW_CONF_H) / 2;
  if(uiGlass) drawLiquidGlassPanel(x, y, DRW_CONF_W, DRW_CONF_H, 24, TH_GLASS);
  else        fillRoundRectA(x, y, DRW_CONF_W, DRW_CONF_H, 24, TH_SURF, 245);
  drawTextC(SCR_W / 2, y + 26, "\xC2\xBF" "Desinstalar?", 3, TH_TXT);
  // El NOMBRE de la app, truncado a lo que cabe en la tarjeta.
  const char* nm = drwCtxName();
  char lbl[DRW_LABEL_MAX];
  uiLabelFit(nm, DRW_CONF_W - 40, 2, lbl, sizeof(lbl));
  drawTextC(SCR_W / 2, y + 62, lbl, 2, TH_TXT);
  drawTextC(SCR_W / 2, y + 94,  "Se borrara la aplicacion", 1, TH_TXT2);
  drawTextC(SCR_W / 2, y + 112, "y todos sus datos.", 1, TH_TXT2);
  int cx0, cy0, cw, ch, ux0; drwConfBtnRects(cx0, cy0, cw, ch, ux0);
  fillRoundRectA(cx0, cy0, cw, ch, ch / 2, TH_TXT2, 60);
  drawTextC(cx0 + cw / 2, cy0 + ch / 2 - 8, "Cancelar", 2, TH_TXT);
  fillRoundRect(ux0, cy0, cw, ch, ch / 2, TH_DANGER);
  drawTextC(ux0 + cw / 2, cy0 + ch / 2 - 8, "Desinstalar", 2, rgb565(255,255,255));
}

// ---- Ficha de informacion ----------------------------------------------
#define DRW_INFO_W 344
#define DRW_INFO_H 244
static void drwInfoBand(int &y0, int &y1){
  y0 = (SCR_H - DRW_INFO_H) / 2 - 4; y1 = y0 + DRW_INFO_H + 8;
  if(y0 < 0) y0 = 0; if(y1 > SCR_H - 1) y1 = SCR_H - 1;
}
// Ficha de una app DESCARGADA. Enséña lo que el sistema sabe de verdad de ella:
// lo que dice su manifiesto ya validado, su version, su runtime y su estado. No
// ofrece "anadir a inicio" ni "ocultar" porque esas dos son bitmasks sobre los
// ids del registro nativo (gAppFav / gAppHidden, un bit por app) y un paquete no
// tiene id ahi. Preferimos no ofrecer una accion antes que ofrecerla rota.
static void drwInfoDrawPkg(int x, int y, int e){
  const PkgAppEntry& a = pkgApps[e];
  { int oldStyle = gIconStyle; gIconStyle = 0;
    pkgAppDrawIcon(e, x + 20, y + 20, 56);
    gIconStyle = oldStyle; }
  drawText(x + 88, y + 28, a.name, uiFontFit(a.name, DRW_INFO_W - 108, 3), TH_TXT);
  drawText(x + 88, y + 54, "Descargada", 2, TH_TXT2);
  char ln[96];
  int ty = y + 96;
  snprintf(ln, sizeof(ln), "Version: %s (%u)", a.version, (unsigned)a.versionCode);
  drawText(x + 20, ty, ln, 2, TH_TXT2); ty += 26;
  snprintf(ln, sizeof(ln), "Runtime: %s", a.runtime == FLEXPKG_RT_APP1 ? "flex-app-v1" : "flex-ui-1");
  drawText(x + 20, ty, ln, 2, TH_TXT2); ty += 26;
  uint8_t st = pkgAppStatusLive(e);
  snprintf(ln, sizeof(ln), "Estado: %s", st == PKGAPP_ST_OK ? "lista" : pkgAppStatusText(st));
  drawText(x + 20, ty, ln, 2, st == PKGAPP_ST_OK ? TH_TXT2 : TH_DANGER); ty += 26;
  drawTextClip(x + 20, ty, a.id, 1, TH_TXT2, x + DRW_INFO_W - 20);
  drawTextC(x + DRW_INFO_W / 2, y + DRW_INFO_H - 30, "Toca para cerrar", 1, TH_TXT2);
}
static void drwInfoDraw(){
  int x = (SCR_W - DRW_INFO_W) / 2, y = (SCR_H - DRW_INFO_H) / 2;
  if(uiGlass) drawLiquidGlassPanel(x, y, DRW_INFO_W, DRW_INFO_H, 24, TH_GLASS);
  else        fillRoundRectA(x, y, DRW_INFO_W, DRW_INFO_H, 24, TH_SURF, 245);
  // Motivo de un fallo (por ejemplo, una desinstalacion que el gestor de
  // paquetes rechazo). Se ensena tal cual lo dio el gestor: no se traga.
  if(drwInfoErr){
    drawTextC(SCR_W / 2, y + 40, "No se pudo completar", 3, TH_DANGER);
    char m[DRW_LABEL_MAX * 2];
    snprintf(m, sizeof(m), "%s", drwInfoErr);
    int fs = uiFontFit(m, DRW_INFO_W - 40, 2);
    char lbl[DRW_LABEL_MAX * 2];
    uiLabelFit(m, DRW_INFO_W - 40, fs, lbl, sizeof(lbl));
    drawTextC(SCR_W / 2, y + 96, lbl, fs, TH_TXT);
    drawTextC(SCR_W / 2, y + DRW_INFO_H - 30, "Toca para cerrar", 1, TH_TXT2);
    return;
  }
  if(drwInfoPkg >= 0 && drwInfoPkg >= pkgAppsN){ drwInfoPkg = -1; drwInfoOn = false; return; }
  if(drwInfoPkg >= 0){
    // El registro pudo rehacerse con la ficha abierta (una instalacion que
    // termina). Si el indice ya no existe, se cierra la ficha en vez de leer
    // una entrada que no es la que el usuario abrio.
    if(drwInfoPkg < pkgAppsN){ drwInfoDrawPkg(x, y, drwInfoPkg); return; }
    drwInfoPkg = -1; drwInfoOn = false; return;
  }
  int id = drwCtxApp;
  if(id < 0 || id >= APP_N) return;
  { int oldStyle = gIconStyle; gIconStyle = 0;
    drawAppIcon(id, x + 20, y + 20, 56);
    gIconStyle = oldStyle; }
  const char* nm = appName(id);
  drawText(x + 88, y + 28, nm, uiFontFit(nm, DRW_INFO_W - 108, 3), TH_TXT);
  drawText(x + 88, y + 54, appCatName(id), 2, TH_TXT2);
  char ln[48];
  int ty = y + 96;
  snprintf(ln, sizeof(ln), "Id de registro: %d", id);
  drawText(x + 20, ty, ln, 2, TH_TXT2); ty += 26;
  snprintf(ln, sizeof(ln), "En inicio: %s", appIsFav(id) ? "s\xC3\xAD" : "no");
  drawText(x + 20, ty, ln, 2, TH_TXT2); ty += 26;
  snprintf(ln, sizeof(ln), "Visible: %s", appIsHidden(id) ? "no" : "s\xC3\xAD");
  drawText(x + 20, ty, ln, 2, TH_TXT2); ty += 26;
  snprintf(ln, sizeof(ln), "Bloqueada: %s", (APPLOCK_ON && appLockGet(id)) ? "s\xC3\xAD" : "no");
  drawText(x + 20, ty, ln, 2, TH_TXT2);
  drawTextC(x + DRW_INFO_W / 2, y + DRW_INFO_H - 30, "Toca para cerrar", 1, TH_TXT2);
}

// ---- Acciones del menu -------------------------------------------------
// Cada accion que cambia el registro guarda EN EL ACTO (una sola apertura de
// NVS, tres claves) y renumera la caja. No hay escrituras por cuadro.
// INICIO, PARA UNA APP DESCARGADA. La ranura de pkgPrefs es lo que se guarda en
// homeOrder[] (HOME_PKG_BASE + ranura): un numero ESTABLE que sobrevive a que
// pkgApps[] se reordene o cambie de tamano. Nunca se guarda un indice de
// pkgApps, que es justo lo que se movería bajo los pies.
static void drwPkgHomeToggle(int e){
  if(e < 0 || e >= pkgAppsN) return;
  const char* id = pkgApps[e].id;
  if(pkgAppInHome(e)){
    int slot = pkgPrefSlot(id, false);
    if(slot >= 0)
      for(int i = 0; i < HOME_TOTAL; i++)
        if(homeOrder[i] == (uint8_t)(HOME_PKG_BASE + slot)) homeOrder[i] = HOME_EMPTY;
    pkgPrefSet(id, PKGPREF_HOME, false);
  } else {
    if(pkgAppHidden(e)) return;                    // una app oculta no puede estar en Inicio
    int slot = pkgPrefSlot(id, true);
    if(slot < 0) return;                           // tabla de anclajes llena
    int cell = homeFirstFreeGrow();                // respeta el limite de paginas
    if(slot >= 0 && cell < 0){
      // No cabe: no se deja la marca puesta ni una ranura a medias.
      if(!(pkgPrefFlags(id) & PKGPREF_HIDDEN)) pkgPrefForget(id);
      return;
    }
    pkgPrefSet(id, PKGPREF_HOME, true);
    homeOrder[cell] = (uint8_t)(HOME_PKG_BASE + slot);
  }
  homeOrderNormalize();
  homeOrderSave();
  gHomeDirty = true;
}
static void drwPkgHideToggle(int e){
  if(e < 0 || e >= pkgAppsN) return;
  const char* id = pkgApps[e].id;
  if(pkgAppHidden(e)){
    pkgPrefSet(id, PKGPREF_HIDDEN, false);
  } else {
    // Fuera de la caja: tambien fuera de Inicio, igual que una nativa.
    if(pkgAppInHome(e)) drwPkgHomeToggle(e);
    pkgPrefSet(id, PKGPREF_HIDDEN, true);
  }
  drwFilter();
  drwClampScroll();
}
// DESINSTALAR POR LA RUTA OFICIAL. flexPkgUninstall() es la transaccion del
// gestor de paquetes: se lleva la version activa, los temporales, el registro y
// la carpeta privada. Aqui no se borra ni una carpeta a mano.
static bool drwPkgUninstall(int e){
  if(e < 0 || e >= pkgAppsN) return false;
  char id[FLEXPKG_ID_MAX + 1];
  snprintf(id, sizeof(id), "%s", pkgApps[e].id);
  // Si estaba anclada, primero se suelta su ranura de Inicio: si no, quedaria
  // una referencia a una app que ya no existe.
  if(pkgAppInHome(e)) drwPkgHomeToggle(e);
  if(!flexPkgUninstall(id)) return false;          // el error se muestra, no se traga
  pkgPrefForget(id);
  pkgAppsInvalidate();
  drwFilter();
  drwClampScroll();
  homeOrderNormalize();
  homeOrderSave();
  gHomeDirty = true;                               // Inicio se rehace en el acto
  return true;
}

// ABRIR UNA APP DESCARGADA. Un unico camino, lo pida el toque simple o la fila
// "Abrir" del menu. La caja NO abre una segunda puerta al runtime: anota la
// peticion y abre Flex Store, que es quien sabe arrancar cada runtime con su
// validacion de grant, su presupuesto por tick y su ciclo de vida. Al salir de
// la app se vuelve al escritorio (ver pkgAppLaunchFromDrawer).
static void drwStartClose(int pendApp, bool pendSwitcher);   // mas abajo, en el ciclo de vida
static void drwOpenPkg(int e){
  if(e < 0 || e >= pkgAppsN) return;
  // Una app que no esta lista no se abre y lo dice en su ficha, en vez de
  // fallar por dentro o dejar la caja sin responder.
  if(pkgAppStatusLive(e) != PKGAPP_ST_OK){
    drwInfoPkg = e; drwInfoErr = NULL; drwInfoOn = true;
    drwVel = 0; drwDrag = false; drwFull = true;
    return;
  }
  pkgAppRequestLaunch(pkgApps[e].id);
  drwStartClose(IC_FLEXSTORE, false);
}

static void drwFavToggle(int id){
  if(id < 0 || id >= APP_N) return;
  if(appIsFav(id)){
    gAppFav &= (uint32_t)~(1u << id);
    for(int i = 0; i < HOME_TOTAL; i++) if(homeOrder[i] == (uint8_t)id) homeOrder[i] = HOME_EMPTY;
  } else {
    if(appIsHidden(id)) return;                    // una app oculta no puede estar en Inicio
    // Si no queda hueco se CREA otra pagina: anadir a Inicio no puede fallar
    // por falta de espacio mientras queden paginas por crear.
    int slot = homeFirstFreeGrow();
    if(slot < 0) return;                           // maximo de paginas Y todas llenas: no se miente al usuario
    gAppFav |= (uint32_t)(1u << id);
    homeOrder[slot] = (uint8_t)id;
  }
  homeOrderNormalize();
  homeOrderSave();
  gHomeDirty = true;                               // homeBuf ya no refleja el escritorio real
}
static void drwHideToggle(int id){
  if(id < 0 || id >= APP_N || !appCanHide(id)) return;
  if(appIsHidden(id)){
    gAppHidden &= (uint32_t)~(1u << id);
  } else {
    gAppHidden |= (uint32_t)(1u << id);
    gAppFav    &= (uint32_t)~(1u << id);           // fuera de la caja: tambien fuera de Inicio
    for(int i = 0; i < HOME_TOTAL; i++)
      if(homeOrder[i] == (uint8_t)id) homeOrder[i] = HOME_EMPTY;
  }
  homeOrderNormalize();
  homeOrderSave();
  gHomeDirty = true;
  drwFilter();
  drwClampScroll();
}

// ---- Apertura, cierre y animacion --------------------------------------
static bool drawerCanOpen(){
  if(gLand || gHosted) return false;              // Modo PC / app hospedada: no es su escritorio
  if(editMode) return false;                      // Modo Edicion tiene su propio arrastre
  if(KIOSK_ON && kioskOn) return false;           // kiosco: no se sale de la app clavada
  if(flexOtaOwnsScreen() || flexOtaOverlayActive()) return false;
  if(qsPanelY != 0 || qsAnimOn || qsDragging) return false;   // manda el panel rapido
  return gState == ST_HOME;
}
static void drwResetView(){
  drwScroll = 0; drwVel = 0; drwDrag = false; drwMoved = false;
  drwCtxOn = false; drwInfoOn = false; drwCtxApp = -1; drwCtxPkg = -1;
  drwLpCell = -1; drwInfoPkg = -1; drwConfOn = false;
  drwKbOn = false; drwQLen = 0; drwQuery[0] = 0; drwShowHid = false;
}
static void drawerOpen(){
  if(!drawerCanOpen()) return;
  // La isla usa translucencia calculada sobre el escritorio. Si se dejara
  // visible mientras la hoja sube, conservaria ese fondo viejo sobre la caja
  // y produciria exactamente el rectangulo partido de la foto. Se limpia su
  // banda, pero la notificacion sigue en la cola y reaparece al volver a Home.
  notifPauseForDrawer();
  // El fondo se compone ANTES de arrancar la animacion, con el escritorio aun
  // en pantalla: es la unica reserva grande de la caja y se hace una vez por
  // sesion (o al cambiar la apariencia), nunca durante el movimiento.
  if(gHomeDirty) renderHome();                    // la hoja se compone sobre homeBuf: no puede estar viejo
  drwBuildPage();
  drwResetView();
  drwFilter();
  drwPendApp = -1; drwPendSw = false;
  drwOn = true;
  drwSlide = (float)SCR_H; drwLastSlide = SCR_H;
  drwAnim = 1; drwAnimMs = millis(); if(!drwAnimMs) drwAnimMs = 1;
  drwFull = true; drwDirty = true;
  gRippleActive = false;                          // el destello del icono no sobrevive al gesto
  gState = ST_DRAWER;
#if FLEXDRW_DIAG
  pkgDiagLine("abre", (int)drwScroll, drwRows(), 0, SCR_H - 1, 0);
  pkgDiagLastMs = millis();
#endif
}
// Empieza la bajada. La accion pendiente (abrir una app, ir a Recientes) se
// ejecuta cuando la hoja ha terminado de salir, no al tocar: asi la caja nunca
// desaparece de golpe y la app recibe el toque siguiente ya limpia.
static void drwStartClose(int pendApp, bool pendSwitcher){
  if(drwAnim == 2) return;
  drwPendApp = pendApp; drwPendSw = pendSwitcher;
  drwCtxOn = false; drwInfoOn = false; drwKbOn = false;
  drwDrag = false; drwVel = 0;
  drwAnim = 2; drwAnimMs = millis(); if(!drwAnimMs) drwAnimMs = 1;
}
static void drwFinishClose(){
  drwOn = false; drwAnim = 0; drwSlide = (float)SCR_H;
  int app = drwPendApp; bool sw = drwPendSw;
  drwPendApp = -1; drwPendSw = false;
  gState = ST_HOME;
  if(gHomeDirty) renderHome();
  showHome();                                     // escritorio limpio, de un solo volcado
  if(app >= 0){
    // Candado por app: la verificacion va ANTES de abrir, por la MISMA ruta que
    // usa el escritorio (lsuStartVerifyFor -> lsuFinishAfter -> enterApp). La
    // caja no abre una segunda puerta.
    if(APPLOCK_ON && appLockGet(app) && gLockType > 0){ lsuStartVerifyFor(LSU_AFTER_OPENAPP, app); return; }
    enterApp(app);
    return;
  }
  if(sw) activarMultitarea();
}
// Un paso de la animacion. Interpolada por TIEMPO (no por cuadro): si el
// sistema pierde cuadros la hoja llega igual de rapido, solo con menos pasos.
static void drwAnimStep(){
  // Una hoja completa implica fondo + hasta 17 iconos + transferencia DPI.
  // A 60 fps saturaba PSRAM/DMA2D y la animacion podia quedarse congelada a
  // media pantalla. A 30 fps la posicion sigue dependiendo del reloj, pero
  // cada cuadro termina antes de iniciar el siguiente.
  uint32_t now = millis();
  if(drwFrameMs && now - drwFrameMs < 33) return;
  drwFrameMs = now;
  uint32_t e = millis() - drwAnimMs;
  float p = (float)e / (float)DRW_ANIM_MS; if(p > 1.0f) p = 1.0f;
  float ease = 1.0f - (1.0f - p) * (1.0f - p) * (1.0f - p);     // ease-out cubico
  drwSlide = (drwAnim == 1) ? (float)SCR_H * (1.0f - ease) : (float)SCR_H * ease;
  int sy = (int)drwSlide;
  int band0 = sy < drwLastSlide ? sy : drwLastSlide;
  drwLastSlide = sy;
  bool done = (p >= 1.0f);
  if(done && drwAnim == 1){
    drwAnim = 0; drwSlide = 0; band0 = 0;
    drwFull = false; drwDirty = false;   // este mismo cuadro ya es el definitivo
  }
  drwCompose(band0 - 2, SCR_H - 1, drwAnim == 0);
  present(band0 - 2, SCR_H - 1);
  setBuf(fb);
  if(done && drwAnim == 2) drwFinishClose();
}

// ---- Toques -------------------------------------------------------------
static void drwKbTouch(){
  if(!T.tap) return;
  int sy = 0;
  for(int r = 0; r < 3; r++){
    int n = (int)strlen(DRW_KB_ROW[r]);
    for(int c = 0; c < n; c++){
      int x, y, w, h; drwKbKeyRect(r, c, sy, x, y, w, h);
      if(T.x >= x && T.x < x + w && T.y >= y && T.y < y + h){
        if(drwQLen < DRW_QMAX){ drwQuery[drwQLen++] = DRW_KB_ROW[r][c]; drwQuery[drwQLen] = 0; }
        drwFilter(); drwScroll = 0; drwClampScroll(); drwFull = true; return;
      }
    }
  }
  int x, y, w, h;
  drwKbSideRect(true, sy, x, y, w, h);
  if(T.x >= x && T.x < x + w && T.y >= y && T.y < y + h){
    if(drwQLen > 0) drwQuery[--drwQLen] = 0;
    drwFilter(); drwScroll = 0; drwClampScroll(); drwFull = true; return;
  }
  drwKbSideRect(false, sy, x, y, w, h);
  if(T.x >= x && T.x < x + w && T.y >= y && T.y < y + h){
    drwKbOn = false; drwClampScroll(); drwFull = true; return;
  }
}
// Devuelve true si el toque se consumio en la cabecera (buscador / ojo).
static bool drwHeaderTouch(){
  if(!T.tap) return false;
  int x = 24, y = DRW_SEARCH_Y, w = SCR_W - 48, h = DRW_SEARCH_H;
  if(T.y < y || T.y >= y + h || T.x < x || T.x >= x + w) return false;
  if(T.x >= x + w - 54){                          // ojo: ver / esconder las ocultas
    drwShowHid = !drwShowHid;
    drwFilter(); drwClampScroll(); drwFull = true; return true;
  }
  drwKbOn = !drwKbOn;                             // el resto del pildoro abre el teclado
  drwClampScroll(); drwFull = true;
  return true;
}
static void drwScrollTouch(){
  int gTop = DRW_GRID_TOP, gBot = drwGridBot();
  uint32_t now = millis();
  if(T.pressed && T.y >= gTop && T.y < gBot){
    drwDrag = true; drwMoved = false; drwVel = 0;
    drwDragY0 = T.y; drwDragLastY = T.y; drwDragS0 = drwScroll; drwDragMs = now;
    return;
  }
  if(drwDrag && T.down){
    int dy = T.y - drwDragY0;
    // MISMO umbral que usa tDoRelease para decidir que un toque es un tap
    // (<16 px). Con uno mas estrecho, un dedo que tiembla 12 px marcaba el
    // gesto como arrastre y el toque ya no abria la app.
    if(abs(dy) >= 16) drwMoved = true;
    float ns = drwDragS0 - (float)dy;
    float old = drwScroll;
    drwScroll = ns; drwClampScroll();
    uint32_t dt = now - drwDragMs;
    if(dt >= 12){                                  // velocidad para la inercia
      drwVel = (float)(drwDragLastY - T.y) * 1000.0f / (float)dt;
      drwDragLastY = T.y; drwDragMs = now;
    }
    if((int)old != (int)drwScroll) drwDirty = true;
    return;
  }
  if(drwDrag && !T.down){
    drwDrag = false;
    if(!drwMoved) drwVel = 0;                      // fue un toque, no un arrastre
  }
}
static void drwInertiaStep(uint32_t dt){
  if(drwDrag || drwVel == 0.0f) return;
  float d = drwVel * (float)dt / 1000.0f;
  float old = drwScroll;
  drwScroll += d;
  drwClampScroll();
  float k = 1.0f - (float)dt / 160.0f; if(k < 0) k = 0;
  drwVel *= k;
  if(drwVel > -24.0f && drwVel < 24.0f) drwVel = 0;
  if((int)old != (int)drwScroll) drwDirty = true;
}

// ---- Tick ---------------------------------------------------------------
static void drawerTick(){
  // 1. OVERLAYS GLOBALES PRIMERO. El panel rapido, la isla y el puente tactil
  //    del OTA ya corrieron en loop() antes del switch; lo unico que falta es
  //    apartarse cuando una capa OTA es DUENA de la pantalla: ni se dibuja ni
  //    se escucha, y al volver se repinta entera.
  if(flexOtaOwnsScreen() || flexOtaOverlayActive()){ drwFull = true; return; }
  if(!drwOn){ gState = ST_HOME; showHome(); return; }   // defensivo: nunca deberia pasar

  // 2. ANIMACION EN CURSO: el toque no llega a nadie (igual que en la cortina).
  if(drwAnim){ drwAnimStep(); return; }

  uint32_t now = millis();

  // Cambio de minuto: la cabecera de la caja lleva el mismo reloj que el
  // escritorio y tiene que seguirlo.
  if(gMinChanged) drwFull = true;
  // Cada pulsacion nueva decide sobre que celda (si alguna) puede actuar la
  // pulsacion larga. Reevaluarlo AQUI y no dentro del scroll es lo que impide
  // que mantener el dedo sobre la cabecera abra el menu de la ultima app que
  // se toco en la rejilla.
  if(T.pressed) drwLpCell = drwHitCell(T.x, T.y);
  // El final del arrastre se cierra AQUI, antes de repartir el toque: el dedo
  // puede levantarse sobre el teclado o la cabecera, y alli drwScrollTouch() ni
  // siquiera se llama. Sin esto drwDrag se quedaba encallado en true y la
  // inercia -- que se calla mientras hay un dedo arrastrando -- no volvia.
  if(drwDrag && !T.down){ drwDrag = false; if(!drwMoved) drwVel = 0; }

  // 3. FICHA DE INFORMACION: modal. Consume el toque y SALE -- si dejara pasar
  //    el gesto, cerrar la ficha hacia abajo cerraria tambien la caja.
  if(drwInfoOn){
    if(T.tap || T.swipeDown || T.swipeUp){ drwInfoOn = false; drwInfoPkg = -1; drwInfoErr = NULL; drwFull = true; return; }
    int y0, y1; drwInfoBand(y0, y1);
    drwCompose(y0, y1, true); drwInfoDraw(); present(y0, y1); setBuf(fb);
    return;
  }

  // 4. MENU CONTEXTUAL: modal sobre la caja. Mismo criterio que la ficha --
  //    pase lo que pase con el toque, aqui se acaba el cuadro: una fila que
  //    caiga sobre la barra de navegacion no puede ademas cerrar la caja.
  // 4bis. CONFIRMACION DE DESINSTALAR: modal sobre el menu. Va ANTES que el
  //       menu para que el toque no se reparta entre los dos.
  if(drwConfOn){
    if(T.tap){
      int cx0, cy0, cw, ch, ux0; drwConfBtnRects(cx0, cy0, cw, ch, ux0);
      bool enFila = (T.y >= cy0 && T.y < cy0 + ch);
      if(enFila && T.x >= ux0 && T.x < ux0 + cw){
        int e = drwCtxPkg;
        bool ok = drwPkgUninstall(e);
        drwConfOn = false; drwCtxOn = false; drwCtxPkg = -1; drwCtxApp = -1;
        drwFull = true;
        if(!ok){
          // El gestor de paquetes no pudo: se dice, con su motivo, y la lista
          // se queda como estaba en vez de fingir que la app ya no esta.
          drwInfoErr = flexPkgError();
          drwInfoOn = true; drwInfoPkg = -1;
        }
        return;
      }
      // Cualquier otro toque cancela: no se borra nada por tocar al lado.
      drwConfOn = false; drwFull = true;
      return;
    }
    if(T.swipeDown || T.swipeUp){ drwConfOn = false; drwFull = true; return; }
    int y0, y1; drwConfBand(y0, y1);
    drwCompose(y0, y1, true); drwConfDraw(); present(y0, y1); setBuf(fb);
    return;
  }

  if(drwCtxOn){
    if(T.tap){
      int rows = drwCtxRows();
      int hit = -1;
      if(T.x >= drwCtxX && T.x < drwCtxX + DRW_CTX_W && T.y >= drwCtxY){
        int r = (T.y - drwCtxY) / DRW_CTX_ROW_H;
        if(r >= 0 && r < rows) hit = r;
      }
      int app = drwCtxApp, pkg = drwCtxPkg;
      if(hit < 0){ drwCtxOn = false; drwFull = true; }            // fuera del panel: cancelar
      else if(!drwCtxEnabled(hit)){ return; }                     // fila inerte: ni siquiera cierra
      else if(hit == DRW_CTX_OPEN){
        drwCtxOn = false;
        if(pkg >= 0){ drwOpenPkg(pkg); return; }
        drwStartClose(app, false); return;
      }
      else if(hit == DRW_CTX_HOME){
        if(pkg >= 0) drwPkgHomeToggle(pkg); else drwFavToggle(app);
        drwCtxOn = false; drwFull = true;
      }
      else if(hit == DRW_CTX_HIDE){
        if(pkg >= 0) drwPkgHideToggle(pkg); else drwHideToggle(app);
        drwCtxOn = false; drwFull = true;
      }
      else if(hit == DRW_CTX_INFO){
        drwCtxOn = false; drwInfoOn = true; drwInfoPkg = pkg; drwInfoErr = NULL; drwFull = true;
      }
      else { drwConfOn = true; drwFull = true; }                  // Desinstalar -> confirmar
      return;
    }
    if(T.swipeDown || T.swipeUp){ drwCtxOn = false; drwFull = true; return; }
    int y0, y1; drwCtxBand(y0, y1);
    drwCompose(y0, y1, true); drwCtxDraw(); present(y0, y1); setBuf(fb);
    return;
  }

  // 5. CIERRES. Atras / Inicio / Recientes de la barra inferior, gesto hacia
  //    abajo y gesto de la barra iOS. El gesto hacia abajo SOLO cierra con la
  //    rejilla arriba del todo: si hay scroll, deslizar hacia abajo es
  //    desplazar la lista, que es lo que espera cualquiera.
  if(!drwKbOn && T.tap && T.y > SCR_H - DRW_NAV_H && gNavMode == 0){
    if(T.x < SCR_W / 3)            { drwStartClose(-1, false); return; }   // Atras
    else if(T.x < SCR_W * 2 / 3)   { drwStartClose(-1, false); return; }   // Inicio
    else                           { drwStartClose(-1, true);  return; }   // Recientes
  }
  if(gNavMode == 1 && T.released && T.startY > SCR_H - 44 && (T.startY - T.y) > 30){
    drwStartClose(-1, false); return;                                      // barra de gestos
  }
  if(T.swipeDown && !drwKbOn && (drwScroll <= 0.5f || T.startY < DRW_GRID_TOP)){
    drwStartClose(-1, false); return;
  }

  // 6. TECLADO Y CABECERA (antes que la rejilla: estan por encima de ella).
  if(drwKbOn && T.y >= SCR_H - DRW_KB_H){ drwKbTouch(); }
  else if(drwHeaderTouch()){ /* consumido */ }
  else {
    // 7. PULSACION LARGA sobre un icono -> menu contextual. El mismo gesto y el
    //    mismo menu para los dos tipos de app; lo que cambia es lo que ofrece.
    bool lpLong = T.down && !drwMoved && (now - T.downMs) > DRW_LP_MS
                  && abs(T.x - T.startX) < 12 && abs(T.y - T.startY) < 12;
    if(lpLong && drwLpCell >= 0){
      int cell = drwLpCell; drwLpCell = -1;
      drwCtxOpen(cell);
      return;
    }
    // 8. SCROLL con inercia.
    drwScrollTouch();
    // 9. TOQUE SIMPLE -> abrir la app (la caja se cierra primero, ver drwStartClose).
    if(T.tap && !drwMoved){
      int cell = drwHitCell(T.x, T.y);
      if(cell >= 0){
        int cx, cy; drwCellXY(cell, cx, cy);
        int id = drwNativeId(cell);
        if(id >= 0){
          // Origen de la animacion de apertura: el icono REAL que se acaba de
          // tocar, aunque la app no este en el escritorio (ver gIconOvrApp).
          gIconOvrApp = id; gIconOvrX = cx; gIconOvrY = cy - (int)drwScroll; gIconOvrS = DRW_ICON_S;
          drwStartClose(id, false);
          return;
        }
        int e = drwPkgIndex(cell);
        if(e >= 0){
          gIconOvrApp = IC_FLEXSTORE; gIconOvrX = cx; gIconOvrY = cy - (int)drwScroll; gIconOvrS = DRW_ICON_S;
          drwOpenPkg(e);
          return;
        }
      }
    }
  }

  // 10. INERCIA + PINTADO. Sin cambios que dibujar no se toca la pantalla:
  //     la caja quieta no gasta ancho de banda de PSRAM ni del panel.
  static uint32_t drwPrevMs = 0;
  uint32_t dt = drwPrevMs ? (now - drwPrevMs) : 16;
  if(dt > 100) dt = 100;
  drwPrevMs = now;
  drwInertiaStep(dt);
  if(!drwFull && !drwDirty) return;
  if(now - drwFrameMs < 33) return;                 // 30 fps: presupuesto real de PSRAM/DMA2D
  drwFrameMs = now;
#if FLEXDRW_DIAG
  uint32_t diagT0 = micros();
  int diagB0, diagB1;
#endif
  if(drwFull){
    drwCompose(0, SCR_H - 1, true); present(0, SCR_H - 1);
    drwFull = false; drwDirty = false;
#if FLEXDRW_DIAG
    diagB0 = 0; diagB1 = SCR_H - 1;
#endif
  } else {
    int y0 = DRW_GRID_TOP, y1 = drwGridBot() - 1;
    drwCompose(y0, y1, true); present(y0, y1);
    drwDirty = false;
#if FLEXDRW_DIAG
    diagB0 = y0; diagB1 = y1;
#endif
  }
  setBuf(fb);
#if FLEXDRW_DIAG
  // Un cuadro de mas de 60 ms es el que puede acercarse al watchdog de tarea:
  // ese SIEMPRE se cuenta. El resto, como mucho uno cada 500 ms.
  uint32_t diagUs = micros() - diagT0;
  if(diagUs > 60000u){
    pkgDiagLine("CUADRO LENTO", (int)drwScroll, drwRows(), diagB0, diagB1, diagUs);
    pkgDiagLastMs = now;
  } else if(now - pkgDiagLastMs >= 500u){
    pkgDiagLine("scroll", (int)drwScroll, drwRows(), diagB0, diagB1, diagUs);
    pkgDiagLastMs = now;
  }
#endif
}
