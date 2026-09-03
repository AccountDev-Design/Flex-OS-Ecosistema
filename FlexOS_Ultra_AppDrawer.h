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
#include "FlexOS_Ultra_Lock.h"   // eslabon anterior de la cadena

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
static int       drwList[APP_N], drwN = 0;    // ids visibles tras filtrar (del registro)
static char      drwQuery[DRW_QMAX + 1] = { 0 };
static int       drwQLen    = 0;
static bool      drwKbOn    = false;  // teclado del buscador desplegado
static bool      drwShowHid = false;  // "ver apps ocultas" (unica via para volver a mostrarlas)
static bool      drwCtxOn   = false;  // menu contextual de pulsacion larga
static int       drwCtxApp  = -1, drwCtxX = 0, drwCtxY = 0;
static bool      drwInfoOn  = false;  // ficha "Informacion"
static int       drwPendApp = -1;     // app a abrir cuando termine la bajada
static bool      drwPendSw  = false;  // ir a Recientes cuando termine la bajada
static bool      drwFull    = true;   // hay que recomponer la pantalla entera
static bool      drwDirty   = true;   // hay que recomponer la banda de la rejilla
static uint32_t  drwFrameMs = 0;
static int       drwLpApp   = -1;     // app bajo el dedo (para no repetir el long-press)

// ---- Geometria derivada ----
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

// ---- Filtro: el UNICO sitio donde se decide que apps entran en la caja ----
// Sale del registro central: id 0..15, fuera las ocultas (salvo en modo "ver
// ocultas") y fuera las que no casen con el buscador. dexMatch() ya existe y
// hace exactamente esta comparacion sin distinguir mayusculas; se reutiliza en
// vez de escribir una segunda.
static void drwFilter(){
  drwN = 0;
  for(int id = 0; id < APP_N; id++){
    if(appIsHidden(id) && !drwShowHid) continue;
    if(!dexMatch(appName(id), drwQuery, drwQLen)) continue;
    drwList[drwN++] = id;
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
    int r0 = (scroll - DRW_ROW_STEP) / DRW_ROW_STEP; if(r0 < 0) r0 = 0;
    int r1 = (scroll + (gBot - gTop)) / DRW_ROW_STEP + 1;
    int rows = drwRows(); if(r1 > rows - 1) r1 = rows - 1;
    for(int r = r0; r <= r1; r++){
      for(int c = 0; c < 4; c++){
        int i = r * 4 + c; if(i >= drwN) break;
        int id = drwList[i];
        int cx, cy; drwCellXY(i, cx, cy);
        int ix = cx, iy = cy - scroll + sy;
        if(iy > gClipY1 || iy + DRW_ROW_STEP < gClipY0) continue;
        drawAppIcon(id, ix, iy, DRW_ICON_S);
        if(appIsHidden(id)) fillRoundRectA(ix, iy, DRW_ICON_S, DRW_ICON_S, DRW_ICON_S * 22 / 100, rgb565(0,0,0), 130);
        if(APPLOCK_ON && appLockGet(id))
          fillRoundRectA(ix + DRW_ICON_S - 18, iy + DRW_ICON_S - 18, 16, 16, 5, TH_DANGER, 230);
        const char* nm = appName(id);
        int fs = uiFontFit(nm, DRW_COL_STEP - 14, 2);
        drawTextC(ix + DRW_ICON_S / 2, iy + DRW_ICON_S + 8, nm, fs,
                  appIsHidden(id) ? TH_ONWALL2 : TH_ONWALL);
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
static const char* drwCtxLabel(int i){
  switch(i){
    case 0:  return "Abrir";
    case 1:  if(appIsFav(drwCtxApp)) return "Quitar de inicio";
             return drwHomeHasSlot() ? "A\xC3\xB1" "adir a inicio" : "Inicio completo";
    case 2:  return appIsHidden(drwCtxApp) ? "Mostrar" : "Ocultar";
    default: return "Informaci\xC3\xB3n";
  }
}
static bool drwCtxEnabled(int i){
  // Una fila inerte se dibuja atenuada y no responde: se VE por que no se
  // puede usar, en vez de aceptar el toque y no hacer nada.
  if(i == 1) return appIsFav(drwCtxApp) || (drwHomeHasSlot() && !appIsHidden(drwCtxApp));
  if(i == 2) return appCanHide(drwCtxApp);          // Ajustes no se puede ocultar
  return true;
}
static void drwCtxGlyph(int kind, int x, int y, int s, uint16_t col){
  if(kind == 0){                                     // flecha de abrir
    strokeSegAA((float)x, (float)(y + s / 2), (float)(x + s), (float)(y + s / 2), 2.0f, col);
    strokeSegAA((float)(x + s - 9), (float)(y + s / 2 - 8), (float)(x + s), (float)(y + s / 2), 2.0f, col);
    strokeSegAA((float)(x + s - 9), (float)(y + s / 2 + 8), (float)(x + s), (float)(y + s / 2), 2.0f, col);
  } else if(kind == 1){                              // estrella (favorita)
    fillCircleA(x + s / 2, y + s / 2, s / 3, col, 230);
    fillCircleA(x + s / 2, y + s / 2, s / 3 - 4, uiGlass ? TH_GLASS : TH_SURF, 200);
  } else if(kind == 2){                              // ojo
    drwGlyphEye(x + s / 2, y + s / 2, s - 4, !appIsHidden(drwCtxApp), col);
  } else {                                           // "i" de informacion
    drawCircle(x + s / 2, y + s / 2, s / 2 - 1, col);
    fillRect(x + s / 2 - 1, y + s / 2 - 5, 2, 10, col);
    fillRect(x + s / 2 - 1, y + s / 2 - 9, 2, 2, col);
  }
}
static void drwCtxDraw(){
  int h = DRW_CTX_ROWS * DRW_CTX_ROW_H;
  if(uiGlass) drawLiquidGlassPanel(drwCtxX, drwCtxY, DRW_CTX_W, h, DRW_CTX_RAD, TH_GLASS);
  else        fillRoundRectA(drwCtxX, drwCtxY, DRW_CTX_W, h, DRW_CTX_RAD, TH_SURF, 242);
  for(int i = 0; i < DRW_CTX_ROWS; i++){
    int ry = drwCtxY + i * DRW_CTX_ROW_H;
    if(i > 0) fillRectA(drwCtxX + 14, ry, DRW_CTX_W - 28, 1, TH_TXT2, 80);
    bool en = drwCtxEnabled(i);
    uint16_t col = en ? TH_TXT : TH_TXT2;
    const char* lb = drwCtxLabel(i);
    int fs = uiFontFit(lb, DRW_CTX_W - 76, 3);
    drawText(drwCtxX + 18, ry + DRW_CTX_ROW_H / 2 - uiLineH(fs) / 2, lb, fs, col);
    drwCtxGlyph(i, drwCtxX + DRW_CTX_W - 44, ry + DRW_CTX_ROW_H / 2 - 13, 26, col);
  }
}
static void drwCtxBand(int &y0, int &y1){
  y0 = drwCtxY - 4; y1 = drwCtxY + DRW_CTX_ROWS * DRW_CTX_ROW_H + 4;
  if(y0 < 0) y0 = 0; if(y1 > SCR_H - 1) y1 = SCR_H - 1;
}
static void drwCtxOpen(int cell){
  if(cell < 0 || cell >= drwN) return;
  drwCtxApp = drwList[cell];
  int cx, cy; drwCellXY(cell, cx, cy);
  int iy = cy - (int)drwScroll;
  int px = cx + DRW_ICON_S + 12;
  int h  = DRW_CTX_ROWS * DRW_CTX_ROW_H;
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

// ---- Ficha de informacion ----------------------------------------------
#define DRW_INFO_W 344
#define DRW_INFO_H 244
static void drwInfoBand(int &y0, int &y1){
  y0 = (SCR_H - DRW_INFO_H) / 2 - 4; y1 = y0 + DRW_INFO_H + 8;
  if(y0 < 0) y0 = 0; if(y1 > SCR_H - 1) y1 = SCR_H - 1;
}
static void drwInfoDraw(){
  int x = (SCR_W - DRW_INFO_W) / 2, y = (SCR_H - DRW_INFO_H) / 2;
  if(uiGlass) drawLiquidGlassPanel(x, y, DRW_INFO_W, DRW_INFO_H, 24, TH_GLASS);
  else        fillRoundRectA(x, y, DRW_INFO_W, DRW_INFO_H, 24, TH_SURF, 245);
  int id = drwCtxApp;
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
  drwCtxOn = false; drwInfoOn = false; drwCtxApp = -1; drwLpApp = -1;
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
  if(T.pressed) drwLpApp = drwHitCell(T.x, T.y);
  // El final del arrastre se cierra AQUI, antes de repartir el toque: el dedo
  // puede levantarse sobre el teclado o la cabecera, y alli drwScrollTouch() ni
  // siquiera se llama. Sin esto drwDrag se quedaba encallado en true y la
  // inercia -- que se calla mientras hay un dedo arrastrando -- no volvia.
  if(drwDrag && !T.down){ drwDrag = false; if(!drwMoved) drwVel = 0; }

  // 3. FICHA DE INFORMACION: modal. Consume el toque y SALE -- si dejara pasar
  //    el gesto, cerrar la ficha hacia abajo cerraria tambien la caja.
  if(drwInfoOn){
    if(T.tap || T.swipeDown || T.swipeUp){ drwInfoOn = false; drwFull = true; return; }
    int y0, y1; drwInfoBand(y0, y1);
    drwCompose(y0, y1, true); drwInfoDraw(); present(y0, y1); setBuf(fb);
    return;
  }

  // 4. MENU CONTEXTUAL: modal sobre la caja. Mismo criterio que la ficha --
  //    pase lo que pase con el toque, aqui se acaba el cuadro: una fila que
  //    caiga sobre la barra de navegacion no puede ademas cerrar la caja.
  if(drwCtxOn){
    if(T.tap){
      int hit = -1;
      if(T.x >= drwCtxX && T.x < drwCtxX + DRW_CTX_W && T.y >= drwCtxY){
        int r = (T.y - drwCtxY) / DRW_CTX_ROW_H;
        if(r >= 0 && r < DRW_CTX_ROWS) hit = r;
      }
      int app = drwCtxApp;
      if(hit < 0){ drwCtxOn = false; drwFull = true; }            // fuera del panel: cancelar
      else if(!drwCtxEnabled(hit)){ return; }                     // fila inerte: ni siquiera cierra
      else if(hit == 0){ drwCtxOn = false; drwStartClose(app, false); return; }
      else if(hit == 1){ drwFavToggle(app);  drwCtxOn = false; drwFull = true; }
      else if(hit == 2){ drwHideToggle(app); drwCtxOn = false; drwFull = true; }
      else             { drwCtxOn = false; drwInfoOn = true;   drwFull = true; }
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
    // 7. PULSACION LARGA sobre un icono -> menu contextual.
    if(T.down && drwLpApp >= 0 && !drwMoved && (now - T.downMs) > DRW_LP_MS
       && abs(T.x - T.startX) < 12 && abs(T.y - T.startY) < 12){
      int cell = drwLpApp; drwLpApp = -1;
      drwCtxOpen(cell);
      return;
    }
    // 8. SCROLL con inercia.
    drwScrollTouch();
    // 9. TOQUE SIMPLE -> abrir la app (la caja se cierra primero, ver drwStartClose).
    if(T.tap && !drwMoved){
      int cell = drwHitCell(T.x, T.y);
      if(cell >= 0){
        int id = drwList[cell];
        // Origen de la animacion de apertura: el icono REAL que se acaba de
        // tocar, aunque la app no este en el escritorio (ver gIconOvrApp).
        int cx, cy; drwCellXY(cell, cx, cy);
        gIconOvrApp = id; gIconOvrX = cx; gIconOvrY = cy - (int)drwScroll; gIconOvrS = DRW_ICON_S;
        drwStartClose(id, false);
        return;
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
  if(drwFull){
    drwCompose(0, SCR_H - 1, true); present(0, SCR_H - 1);
    drwFull = false; drwDirty = false;
  } else {
    int y0 = DRW_GRID_TOP, y1 = drwGridBot() - 1;
    drwCompose(y0, y1, true); present(y0, y1);
    drwDirty = false;
  }
  setBuf(fb);
}
