// #############################################################
// ##  FlexOS_Browser_Bridge.h  ·  PUENTE NAVEGADOR <-> FLEXOS
// ##  ------------------------------------------------------
// ##  QUE ES Y POR QUE EXISTE
// ##  ----------------------
// ##  Exactamente lo mismo que FlexOS_OTA_Bridge.h y por el mismo
// ##  motivo: las primitivas graficas de FlexOS (fillRect, drawText,
// ##  present, setBuf...) son `static` DENTRO de cada .ino, asi que
// ##  FlexOS_BrowserApp.cpp -- que el compilador trata como una
// ##  unidad de traduccion aparte -- no puede llamarlas.
// ##
// ##  Este fichero es la unica pieza que conoce los dos mundos. Se
// ##  incluye UNA sola vez, casi al final del .ino (cuando todas las
// ##  primitivas y el teclado ya estan definidos), y ahi implementa
// ##  en una linea cada funcion brHost* que el modulo declaro.
// ##
// ##  ES EL MISMO FICHERO PARA LAS TRES PLACAS: lo poco que difiere
// ##  se resuelve con #if aqui dentro.
// ##
// ##  ADEMAS trae el TECLADO DEL OMNIBOX. Va aqui y no en el modulo
// ##  porque el teclado de FlexOS (kbPaintKey, kbCellAt, kbFKey, las
// ##  cuatro capas, los tamanos configurables de la Fase A...) es
// ##  `static` del .ino: reimplementarlo en el modulo seria un
// ##  segundo teclado distinto del del resto del sistema, y ponerlo
// ##  aqui lo reutiliza tal cual sin duplicar ni una tecla.
// ##
// ##  COMO SE USA (3 lineas en el .ino):
// ##      arriba, con los includes  :  #include "FlexOS_Browser.h"
// ##      justo antes de setup()    :  #include "FlexOS_Browser_Bridge.h"
// ##      en APP_REG, entrada 7     :  { navEnter, navTick, APP_FLEX | APP_OWN_TOUCH }
// #############################################################

#ifndef FLEXOS_BROWSER_BRIDGE_H
#define FLEXOS_BROWSER_BRIDGE_H

#include "FlexOS_Browser.h"
#include "FlexOS_FS.h"

// Guardia de version (ver el bloque 0 de FlexOS_Browser.h).
static_assert(FLEXBR_BUILD == 5,
  "FlexOS_Browser_Bridge.h y FlexOS_Browser.h son de versiones distintas: "
  "copia otra vez LOS CUATRO ficheros del navegador a la carpeta del sketch.");

#if FLEXBR_ON

// =============================================================
//  TECLADO DEL OMNIBOX
//  -------------------------------------------------------------
//  Mismo patron que la pantalla de contrasena de Wi-Fi
//  (wifiRenderPass / WUI_PASS): el teclado se dibuja con las
//  primitivas kb* del sistema y kbExtrasOn se deja en FALSE a
//  proposito. Esto ultimo no es pereza: la barra de accesos trae el
//  PORTAPAPELES, y un portapapeles a un toque dentro de un campo que
//  puede ser una contrasena de un sitio web es justo lo que no se
//  quiere. El Wi-Fi lo apaga por la misma razon.
// =============================================================
static bool brKbWasOpen = false;

// Franja que ocupo el teclado la ultima vez que se dibujo. Se guarda
// porque al cerrarse HAY QUE BORRARLA, y para entonces brKbTop() ya
// devuelve el borde inferior (el teclado ya no esta abierto).
static int brKbLastTop = -1;
static int brKbLastBottom = -1;

// Borde inferior contra el que se ancla el teclado, y desplazamiento
// respecto a la geometria kb* del sistema (que es SIEMPRE absoluta a la
// pantalla).
//
// A pantalla completa el desplazamiento es EXACTAMENTE 0: el camino que
// de verdad se usa no cambia ni un pixel. Dentro de una ventana de Modo
// PC/DeX el lienzo de la app mide gAppH, no SCR_H, asi que sin esto el
// teclado se dibujaria por debajo del area visible de la ventana y
// pasaria lo mismo que en la pantalla completa: teclas que responden
// pero que no se ven.
//
// EL LIENZO DEL TECLADO. El teclado es del SISTEMA, pero vive dentro del
// lienzo LOGICO de la app que lo abre:
//   · pantalla completa vertical .... 480x800 (el de siempre: identidad);
//   · pantalla completa horizontal .. 800x480 (modo inmersivo girado, gLand);
//   · ventana de Modo PC/DeX ....... el area de cliente (gAppW x gAppH),
//                                     vertical u horizontal segun la ventana.
static int brKbCanvasW(){ if(gHosted) return gAppW; return gLand ? SCR_H : SCR_W; }
static int brKbCanvasH(){ if(gHosted) return gAppH; return gLand ? SCR_W : SCR_H; }
static int brKbBottom(){ return brKbCanvasH(); }
static int brKbDY(){ return brKbCanvasH() - SCR_H; }

// LO QUE HAY DEBAJO DEL TECLADO Y NO ES SUYO: la barra de navegacion del
// sistema. Con la barra ocupando su franja (modo botones, app a pantalla
// completa vertical) el teclado se apoya ENCIMA, igual que en Notas; sin ella
// (gestos, pantalla completa inmersiva, ventana de DeX) llega al borde.
// Antes el navegador no fijaba esto y heredaba el valor de la ULTIMA
// superficie que abrio un teclado: con 0, la fila de funciones (shift, ?123,
// idioma, espacio, borrar, Ir) caia DEBAJO de la barra. La barra se estampa
// encima y se queda esos toques ANTES que la app, asi que "espacio" llevaba a
// Inicio, "Ir" a Recientes y "shift" cerraba el teclado. Escribir una
// direccion era una carrera de obstaculos.
static void brKbSyncReserve(){
#ifdef NAV_H
  kbBotReserve = (!gHosted && navBarReservesSpace()) ? NAV_H : 0;
#endif
}

// Geometria horizontal del teclado AJUSTADA AL LIENZO: centrado si sobra
// ancho, y con teclas mas estrechas si falta (ventana estrecha de DeX). Dibujo
// y toque la ponen y la quitan con el MISMO par de llamadas, asi que lo que se
// ve y lo que se toca no pueden separarse. A pantalla completa vertical el
// resultado es exactamente la geometria que eligio el usuario.
#if KB_SIZE_CONFIG_ON
static int brKbSaveKW = 0, brKbSaveX = 0, brKbGeomDepth = 0;
#endif
static void brKbGeomBegin(){
  brKbSyncReserve();
#if KB_SIZE_CONFIG_ON
  if(brKbGeomDepth++ > 0) return;
  brKbSaveKW = kbKW; brKbSaveX = kbX;
  const int cw = brKbCanvasW();
  int gw = KB_COLS * kbKW + (KB_COLS - 1) * kbGap;
  if(gw > cw - 4){
    int kw = (cw - 4 - (KB_COLS - 1) * kbGap) / KB_COLS;
    if(kw < 14) kw = 14;
    kbKW = kw;
    gw = KB_COLS * kbKW + (KB_COLS - 1) * kbGap;
  }
  kbX = (cw - gw) / 2; if(kbX < 0) kbX = 0;
#endif
}
static void brKbGeomEnd(){
#if KB_SIZE_CONFIG_ON
  if(brKbGeomDepth <= 0) return;
  if(--brKbGeomDepth > 0) return;
  kbKW = brKbSaveKW; kbX = brKbSaveX;
#endif
}

// Altura reservada por el teclado. El navegador maqueta CONTRA esto
// (brHostKeyboardTop), asi que su contenido nunca queda debajo de las
// teclas y no hace falta desplazar nada a mano.
static int brKbTop(){
  if(!flexBrowserKeyboardOpen()) return brKbBottom();
  brKbSyncReserve();
  int top = kbPanelTop() - 34 + brKbDY();   // 34 px para la linea del campo
  int floorY = (gHosted || gLand) ? 0 : 80;
  if(top < floorY) top = floorY;
  return top;
}

// Coordenadas del toque EN EL LIENZO de la app. Dentro de una ventana de DeX
// ya llegan traducidas (DeX inyecta un Touch en coordenadas de lienzo); a
// pantalla completa horizontal hay que girarlas, con la MISMA convencion que
// putPhys/dexPointer: x logica = y fisica, y logica = (SCR_W-1) - x fisica.
static void brTouchToCanvas(int px, int py, int &x, int &y){
  if(gLand && !gHosted){ x = py; y = (SCR_W - 1) - px; }
  else { x = px; y = py; }
}

// -------------------------------------------------------------
//  DIAGNOSTICO DEL CAMINO FISICO DE DIBUJO
//  ------------------------------------------------------------
//  A 1 (por defecto mientras se depura el teclado en hardware),
//  brKbRender() deja constancia de lo que HACE DE VERDAD en la placa:
//
//   · Por Serial, una vez cada 700 ms: si se ejecuto, en QUE buffer
//     escribio (comparado con fb / bbuf / el lienzo de DeX), como
//     quedo la banda de recorte, la region exacta que se manda al
//     panel, si el motor estaba en landscape, y -- lo decisivo -- el
//     valor RELEIDO de un pixel del interior de una tecla. Si ese
//     pixel trae el color de la tecla, el dibujo llego al
//     framebuffer y el problema esta despues (flush/presenter); si
//     trae otra cosa, no llego, y el problema esta antes.
//
//   · En pantalla, dos marcas de 12x12: magenta en la esquina
//     superior izquierda de la franja del teclado y cian en la
//     inferior derecha. Se escriben DIRECTAMENTE en el framebuffer
//     (fb[y*SCR_W+x]), saltandose px(), la banda de recorte, gBuf y
//     cualquier rotacion. Si se ven las marcas pero no las teclas,
//     el transporte al panel funciona y el fallo esta en las
//     primitivas; si no se ve ninguna, el fallo esta en el flush o
//     en la region que se sube.
//
//  Se apaga con -DFLEXBR_KBDEBUG=0 (o poniendolo a 0 aqui) cuando ya
//  no haga falta: no deja rastro en el binario.
// -------------------------------------------------------------
//  A 0 desde que el teclado se confirmo visible en la ESP32-P4 (la fila
//  de funciones se veia pintada sobre el navegador, que es la prueba de
//  que brKbRender llega al panel). Se deja el codigo porque volver a
//  activarlo es cambiar este 0 por un 1.
#ifndef FLEXBR_KBDEBUG
#define FLEXBR_KBDEBUG 0
#endif

// Marca escrita a pelo en el framebuffer real. No pasa por px() ni por
// gBuf a proposito: es el unico dibujo del sistema que no puede fallar
// por culpa del estado grafico, y por eso sirve de referencia.
#if FLEXBR_KBDEBUG
static void brKbDebugMark(int x, int y, uint16_t c){
  if(!fb) return;
  for(int j = 0; j < 12; j++){
    int yy = y + j;
    if(yy < 0 || yy >= SCR_H) continue;
    for(int i = 0; i < 12; i++){
      int xx = x + i;
      if(xx < 0 || xx >= SCR_W) continue;
      fb[(size_t)yy * SCR_W + xx] = c;
    }
  }
}
#endif

// -------------------------------------------------------------
//  DIBUJO DEL TECLADO
//  ------------------------------------------------------------
//  Aqui se toma el control COMPLETO del estado grafico y se devuelve
//  tal cual estaba. No se usa setBuf(): setBuf desvia a gRtTarget
//  cuando la app corre hospedada en una ventana de Modo PC, y el
//  teclado es del SISTEMA -- tiene que ir al framebuffer real pase lo
//  que pase. Tampoco se hereda la banda de recorte de nadie: se
//  instala la del teclado y se restaura la anterior al salir.
//
//  Esta funcion es la ULTIMA capa del cuadro (la llama navTick al
//  final), asi que lo que pinte no lo puede tapar ningun repintado
//  posterior de la app.
// -------------------------------------------------------------
// Firma de lo que el teclado ENSENA (texto, capa, mayusculas, idioma). En
// horizontal a pantalla completa la franja del teclado son COLUMNAS fisicas
// del panel: volcarla entera en cada vuelta seria mandar la pantalla completa
// por cuadro. Ahi solo se vuelca cuando algo cambio (o cada medio segundo,
// como red de seguridad contra un repintado que se haya colado debajo).
static uint32_t brKbLastSig = 0;
static uint32_t brKbLastFlushMs = 0;
static uint32_t brKbHostSig = 0;          // ultima firma volcada DENTRO de una ventana de DeX
static uint32_t brKbSig(){
  uint32_t h = 2166136261u;
  for(const char* p = flexBrowserEditText(); *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
  h = (h ^ (uint32_t)(uintptr_t)mapaActivo) * 16777619u;
  h = (h ^ (kbShift ? 1u : 0u) ^ (kbLangEs ? 2u : 0u)) * 16777619u;
  return h ? h : 1u;
}

static void brKbRender(){
  if(!flexBrowserKeyboardOpen()) return;

  brKbGeomBegin();
  const int dy       = brKbDY();
  const int bottom   = brKbBottom();
  const int ky       = KB_Y + dy;
  const int panelTop = brKbTop();
  const int cw       = brKbCanvasW();
  // DESTINO: el lienzo de la app. A pantalla completa es el framebuffer real;
  // dentro de una ventana de DeX es el lienzo de ESA ventana (gRtTarget). Antes
  // se escribia SIEMPRE en fb con coordenadas verticales: en DeX el teclado
  // caia girado encima del escritorio, fuera de su ventana, y dentro de la
  // ventana no se veia ninguna tecla.
  uint16_t* target = gRtTarget ? gRtTarget : fb;
  if(panelTop >= bottom || !target){ brKbGeomEnd(); return; }

  // Se apunta la franja EXACTA que se va a pintar. Hace falta al
  // cerrar: la fila de funciones (shift, ?123, Es, espacio, borrar, Ir)
  // es la ultima del lienzo, y esa franja no la repinta NADIE cuando el
  // teclado se cierra. brKbErase() la borra.
  brKbLastTop = panelTop;
  brKbLastBottom = bottom;

  // ---- se guarda TODO el estado grafico ----
  uint16_t* oBuf = gBuf;
  const int  oy0 = gClipY0, oy1 = gClipY1, ox0 = gClipX0, ox1 = gClipX1;

  gBuf = target;
  // gLand NO se toca: es la orientacion del LIENZO (ventana horizontal de DeX
  // o pantalla completa girada) y las primitivas ya giran con ella. En
  // vertical la banda de recorte es la del teclado; en horizontal la banda del
  // motor acota la X logica, asi que se deja el lienzo entero.
  if(gLand){ gClipY0 = 0; gClipY1 = SCR_H - 1; gClipX0 = 0; gClipX1 = SCR_W - 1; }
  else {
    gClipY0 = panelTop; gClipY1 = bottom - 1;
    gClipX0 = 0;        gClipX1 = (cw < SCR_W ? cw : SCR_W) - 1;
  }

  // Cabecera del campo: que se esta editando y el texto actual. Se pinta
  // aqui y no en el navegador porque este trozo de pantalla pertenece al
  // teclado (el area de contenido de la app termina en panelTop).
  // Los colores salen de brHostColor() y no de las macros TH_* del .ino:
  // esas solo existen en Ultra (el tema semantico aun no esta en las otras
  // dos placas) y este fichero es el MISMO para las tres.
  fillRect(0, panelTop, cw, bottom - panelTop, brHostColor(BRC_PAGE));
  drawText(12, panelTop + 4, flexBrowserEditLabel(), 1, brHostColor(BRC_MUTE));
  const char* txt = flexBrowserEditText();
  int tw = textW(txt, 2);
  if(tw <= cw - 24) drawText(12, panelTop + 16, txt, 2, brHostColor(BRC_TXT));
  else              drawTextR(cw - 12, panelTop + 16, txt, 2, brHostColor(BRC_TXT));

  // El panel de vidrio escribe en gBuf con indexacion directa y se salta
  // la banda de recorte (ver drawLiquidGlassPanelEx). Con el teclado eso
  // daria igual porque la franja es suya entera, pero se usa la ruta
  // plana cuando el vidrio esta apagado, como en el resto del sistema.
  if(uiGlass) drawLiquidGlassPanel(0, ky - 4, cw, bottom - (ky - 4), 0, kbColPanel());
  else        fillRect(0, ky - 4, cw, bottom - (ky - 4), kbColPanel());

  const int fs = kbFontSize();
  for(int r = 0; r < KB_ROWS; r++) for(int c = 0; c < KB_COLS; c++){
    int x = KB_X + c * (KB_KW + KB_GAP), y = ky + r * (KB_KH + KB_GAP);
    const char* k = mapaActivo[r][c];
    char u[6];
    if(kbShift && k[1] == 0 && k[0] >= 'a' && k[0] <= 'z'){ u[0] = (char)(k[0] - 32); u[1] = 0; k = u; }
    kbPaintKey(x, y, KB_KW, KB_KH, k, fs, kbColKey(), kbColKeyTxt(), false);
  }
  const int fy = ky + 3 * (KB_KH + KB_GAP);
  const char* lb[KB_FKEYS] = { "shift", kbLayerLabel(), kbLangEs ? "ES" : "EN", "espacio", "<-", "Ir" };
  for(int i = 0; i < KB_FKEYS; i++) kbFKey(kbFKeyX(i), fy, kbFKeyW(i), lb[i], (i == 0) && kbShift);

#if FLEXBR_KBDEBUG
  // Pixel de control: el centro de la primera tecla de la segunda fila.
  const int probeX = KB_X + KB_KW / 2;
  const int probeY = ky + (KB_KH + KB_GAP) + KB_KH / 2;
  uint16_t probe = 0xFFFF;
  if(!gLand && probeX >= 0 && probeX < SCR_W && probeY >= 0 && probeY < SCR_H)
    probe = target[(size_t)probeY * SCR_W + probeX];
  if(target == fb){
    brKbDebugMark(0, panelTop, rgb565(255, 0, 255));               // magenta: inicio de la franja
    brKbDebugMark(SCR_W - 12, bottom - 12, rgb565(0, 255, 255));   // cian: final de la franja
  }
  static uint32_t dbgLast = 0;
  static uint32_t dbgN = 0;
  dbgN++;
  if(millis() - dbgLast > 700){
    dbgLast = millis();
    Serial.printf("[KB] #%u buf=%s(%p) fb=%p land=%d hosted=%d gAppH=%d\n",
                  (unsigned)dbgN,
                  (gBuf == fb ? "fb" : (gBuf == bbuf ? "bbuf" : "lienzo")),
                  (void*)gBuf, (void*)fb, (int)gLand, (int)gHosted, gAppH);
    Serial.printf("     franja=%d..%d  ky=%d  clip=y[%d..%d] x[%d..%d]  kbSize=%d %dx%d\n",
                  panelTop, bottom - 1, ky, gClipY0, gClipY1, gClipX0, gClipX1,
                  gKbSize, KB_KW, KB_KH);
    Serial.printf("     pixel(%d,%d)=0x%04X  esperado tecla=0x%04X panel=0x%04X  opac=%d glass=%d hiCon=%d estilo=%d\n",
                  probeX, probeY, probe, kbColKey(), kbColPanel(),
                  gKbOpacity, (int)uiGlass, (int)gKbHiCon, gKbStyle);
  }
#endif

  // ---- se devuelve el estado grafico EXACTAMENTE como estaba ----
  gBuf = oBuf;
  gClipY0 = oy0; gClipY1 = oy1; gClipX0 = ox0; gClipX1 = ox1;
  brKbGeomEnd();

  // El volcado va DESPUES de restaurar: flxFlush no mira el recorte, y
  // asi la banda que se sube al panel es exactamente la del teclado. Dentro
  // de una ventana de DeX flxFlush solo marca el lienzo como sucio: es DeX
  // quien lo compone dentro de su ventana.
  if(gLand && !gRtTarget){
    const uint32_t sig = brKbSig();
    if(sig != brKbLastSig || millis() - brKbLastFlushMs > 500){
      brKbLastSig = sig; brKbLastFlushMs = millis();
      flxFlush(0, SCR_H - 1);
    }
  } else if(gRtTarget){
    // DENTRO DE UNA VENTANA DE DeX el volcado no sube nada al panel: marca el
    // lienzo como sucio y DeX lo recompone (reescalado + composicion de la
    // banda). La ventana recibe tick en CADA vuelta, asi que marcarlo siempre
    // recompondria la ventana a ritmo de frame mientras el teclado este
    // abierto aunque no cambie nada. Las teclas SI se redibujan en el lienzo
    // en cada vuelta (siguen siendo la ultima capa): si la app repinto algo,
    // ella misma marco el lienzo y la composicion ya las incluye. Solo falta
    // marcarlo cuando cambia el propio teclado: texto, capa, mayusculas,
    // idioma, su franja o la ventana en la que esta.
    uint32_t sig = brKbSig();
    sig = (sig ^ (uint32_t)panelTop) * 16777619u;
    sig = (sig ^ (uint32_t)bottom) * 16777619u;
    sig = (sig ^ (uint32_t)cw) * 16777619u;
    sig = (sig ^ (uint32_t)(uintptr_t)gRtTarget) * 16777619u;
    if(!sig) sig = 1u;
    if(sig != brKbHostSig){ brKbHostSig = sig; flxFlush(panelTop, bottom - 1); }
  } else {
    flxFlush(panelTop, bottom - 1);
  }
}

// -------------------------------------------------------------
//  BORRADO DE LA FRANJA DEL TECLADO
//  ------------------------------------------------------------
//  Cuando el teclado se cierra -- "Ir", atras, cambio de pantalla,
//  guardar ajustes, perder el foco -- no basta con dejar de dibujarlo:
//  sus pixeles siguen en el framebuffer y nadie los pisa.
//
//  Concretamente, a pantalla completa el area util de la app llega
//  hasta WIN_BOT (SCR_H-64 = 736) y la fila de funciones del teclado
//  ocupa hasta y=792. Al cerrarse, el navegador repintaba lo suyo hasta
//  736 y esos ultimos 56 px se quedaban intactos: la fila de shift /
//  ?123 / Es / espacio / borrar / Ir flotando sobre la pagina. Es
//  exactamente el sintoma que se vio en la placa.
//
//  Aqui se borra la franja entera y se repone lo que le corresponde a
//  esa zona: el marco del sistema (barra de navegacion). El contenido
//  de la app lo repinta el propio navegador en el mismo cuadro, porque
//  se le pide un repintado completo con el area ya crecida.
// -------------------------------------------------------------
static void brKbErase(){
  brKbHostSig = 0;                         // al volver a abrirse se vuelca seguro
  uint16_t* target = gRtTarget ? gRtTarget : fb;
  if(brKbLastTop < 0 || !target) { brKbLastTop = brKbLastBottom = -1; return; }
  int top = brKbLastTop, bottom = brKbLastBottom;
  brKbLastTop = brKbLastBottom = -1;
  const int ch = brKbCanvasH(), cw = brKbCanvasW();
  if(top < 0) top = 0;
  if(bottom > ch) bottom = ch;
  if(bottom <= top) return;

  uint16_t* oBuf = gBuf;
  const int  oy0 = gClipY0, oy1 = gClipY1, ox0 = gClipX0, ox1 = gClipX1;

  // Mismo lienzo y misma orientacion en los que se pinto (ver brKbRender).
  gBuf = target;
  if(gLand){ gClipY0 = 0; gClipY1 = SCR_H - 1; gClipX0 = 0; gClipX1 = SCR_W - 1; }
  else { gClipY0 = top; gClipY1 = bottom - 1; gClipX0 = 0; gClipX1 = SCR_W - 1; }

  fillRect(0, top, cw, bottom - top, brHostColor(BRC_WIN));

  gBuf = oBuf;
  gClipY0 = oy0; gClipY1 = oy1; gClipX0 = ox0; gClipX1 = ox1;

  // El marco del sistema (barra de estado arriba, barra de navegacion
  // abajo) se repinta entero: es barato y evita razonar sobre que trozo
  // cayo dentro de la franja borrada. Dentro de una ventana de DeX y a
  // pantalla completa no hay marco: no hace nada.
  appDrawChrome(IC_NAV);

  // Y el navegador vuelve a pintar su contenido con el area ya completa.
  flexBrowserForceRepaint();

  if(gLand && !gRtTarget) flxFlush(0, SCR_H - 1);
  else                    flxFlush(top, bottom - 1);
}

// Devuelve true si el toque se consumio dentro del teclado.
//
// LA TECLA SE RESUELVE DONDE SE APOYO EL DEDO, al soltar. Antes solo valia un
// "tap" perfecto (soltar sin moverse mas de 12 px y en menos de 550 ms): una
// pulsacion con un poco de deslizamiento -- lo normal al escribir deprisa con
// el pulgar -- se perdia entera, y el usuario tenia que repetirla. Ahora se
// acepta mientras el dedo no se haya ido mas de UNA tecla de donde se apoyo.
// Y un episodio que EMPIEZA en el teclado es del teclado hasta que se levanta
// el dedo: si sube por encima, no toca la pagina ni cierra el teclado.
static bool brKbTouch(){
  if(!flexBrowserKeyboardOpen()) return false;
  int cx, cy; brTouchToCanvas(T.x, T.y, cx, cy);
  // Donde se APOYO el dedo. En un toque el sistema ya deja T.x/T.y en ese
  // punto (ver tDoRelease); con el dedo abajo o al soltar tras moverse, es
  // T.startX/T.startY.
  int sx = cx, sy = cy;
  if(!T.tap && (T.down || T.released)) brTouchToCanvas(T.startX, T.startY, sx, sy);
  const int top = brKbTop();
  if(sy < top) return false;                     // empezo por encima: es de la app
  if(!T.released && !T.tap) return true;         // el teclado se queda el episodio hasta soltar

  brKbGeomBegin();
  // Las funciones kb* del sistema razonan en coordenadas ABSOLUTAS de
  // pantalla, asi que se deshace el desplazamiento antes de
  // preguntarles. A pantalla completa vertical dy es 0: la identidad.
  const int dy = brKbDY();
  const int px = sx, py = sy - dy;               // donde se APOYO
  const int ex = cx, ey = cy - dy;               // donde se levanto
  const int tolX = KB_KW + KB_GAP, tolY = KB_KH + KB_GAP;
  const bool stayed = (ex - px <= tolX && px - ex <= tolX && ey - py <= tolY && py - ey <= tolY);
  if(!stayed){ brKbGeomEnd(); return true; }     // se fue lejos: no es una pulsacion

  int fi = kbFRowHit(px, py);
  if(fi >= 0){
    brKbGeomEnd();
    if(fi == 0) kbShift = !kbShift;
    else if(fi == 1) mapaActivo = (mapaActivo == LAYOUT_NUM) ? LAYOUT_EMOJI
                                : (mapaActivo == LAYOUT_EMOJI) ? (kbLangEs ? LAYOUT_ES : LAYOUT_EN)
                                : LAYOUT_NUM;
    else if(fi == 2){ kbLangEs = !kbLangEs; if(mapaActivo == LAYOUT_ES || mapaActivo == LAYOUT_EN) mapaActivo = kbLangEs ? LAYOUT_ES : LAYOUT_EN; }
    else if(fi == 3) flexBrowserKeyText(" ");
    else if(fi == 4) flexBrowserKeyBackspace();
    else { flexBrowserKeyEnter(); return true; }
    // No se repinta aqui: lo hace navTick al FINAL del cuadro, que es
    // quien garantiza que el teclado sea la ultima capa. Repintar aqui
    // ademas seria dibujarlo dos veces por pulsacion.
    return true;
  }
  int cell = kbCellAt(px, py);
  brKbGeomEnd();
  if(cell >= 0){
    const char* k = mapaActivo[cell / KB_COLS][cell % KB_COLS];
    if(kbShift && k[1] == 0 && k[0] >= 'a' && k[0] <= 'z'){
      char u[2] = { (char)(k[0] - 32), 0 };
      flexBrowserKeyText(u);
      kbShift = false;
    } else {
      flexBrowserKeyText(k);
    }
  }
  return true;
}

// =============================================================
//  CONTEXTO
// =============================================================
int  brHostScrW(){ return SCR_W; }
int  brHostScrH(){ return SCR_H; }
bool brHostDark(){ return gDark; }
bool brHostGlass(){ return uiGlass; }
bool brHostHosted(){ return gHosted; }
bool brHostOnline(){ return gNetOnline; }
const char* brHostDeviceName(){ return cfgName; }
uint32_t brHostMillis(){ return (uint32_t)millis(); }
int  brHostKeyboardTop(){ return brKbTop(); }

// PANTALLA COMPLETA. El navegador la pide; quien la aplica es el marco de
// apps del sistema (immersive* en FlexOS_Ultra_AppFramework.h), que es el
// unico que decide el lienzo, las barras y la orientacion. Dentro de una
// ventana de Modo PC/DeX no se ofrece: alli se maximiza la VENTANA. En las
// placas sin modo inmersivo (S3, Pro) la opcion no existe.
int brHostFullscreenState(){
#ifdef FLEXOS_IMMERSIVE_ON
  if(gHosted) return BRFS_UNSUPPORTED;
  return immersiveState(IC_NAV);
#else
  return BRFS_UNSUPPORTED;
#endif
}
void brHostFullscreenRequest(int st){
#ifdef FLEXOS_IMMERSIVE_ON
  if(gHosted) return;
  if(st > 0) gImmPrefLand = (st == BRFS_LANDSCAPE);
  immersiveRequest(IC_NAV, st);
#else
  (void)st;
#endif
}

// Area util de la app. Sale de uiBox(), que es lo que usan TODAS las
// apps APP_FLEX: a pantalla completa es la ventana y dentro de una
// ventana de Modo PC/DeX es el area de cliente. Por eso el navegador se
// adapta a los dos sin una sola rama especifica.
// Ademas se recorta contra el teclado: si el teclado esta abierto, el
// contenido del navegador termina donde empieza el panel de teclas, asi
// que la barra de direcciones nunca queda tapada.
void brHostContentRect(int* x, int* y, int* w, int* h){
  int bx, by, bw, bh;
  uiBox(bx, by, bw, bh);
  int kbTop = brKbTop();
  if(kbTop < by + bh) bh = kbTop - by;
  if(bh < 64) bh = 64;
  *x = bx; *y = by; *w = bw; *h = bh;
}
// El lienzo que el gestor de ventanas le dio a la app, sin descontar el
// teclado (ver FlexOS_Browser.h).
void brHostLayoutRect(int* x, int* y, int* w, int* h){
  int bx, by, bw, bh;
  uiBox(bx, by, bw, bh);
  *x = bx; *y = by; *w = bw; *h = bh;
}

// =============================================================
//  TACTIL
//  -------------------------------------------------------------
//  Se COPIA el estado, nunca se escribe en T directamente (salvo los
//  flags que se consumen, igual que hacen la isla de notificaciones y
//  el OTA). T.down lo gobierna flexPollTouch() y tocarlo corromperia
//  la maquina de estados del tactil.
// =============================================================
void brHostGetTouch(BrTouch* t){
  t->down = T.down; t->pressed = T.pressed; t->released = T.released;
  t->tap = T.tap;   t->moved = T.moved;
  t->swipeUp = T.swipeUp; t->swipeDown = T.swipeDown;
  t->swipeLeft = T.swipeLeft; t->swipeRight = T.swipeRight;
  t->x = T.x; t->y = T.y; t->startX = T.startX; t->startY = T.startY;
  t->dx = T.dx; t->dy = T.dy; t->downMs = (uint32_t)T.downMs;
  // PANTALLA COMPLETA HORIZONTAL: el navegador maqueta en coordenadas del
  // lienzo girado (800x480) y el tactil llega en las del panel (480x800). Se
  // giran aqui, con la misma convencion que putPhys, para que tocar un boton
  // sea tocar ESE boton. (En una ventana de DeX ya llegan traducidas.)
  if(gLand && !gHosted){
    brTouchToCanvas(T.x, T.y, t->x, t->y);
    brTouchToCanvas(T.startX, T.startY, t->startX, t->startY);
    t->dx = t->x - t->startX; t->dy = t->y - t->startY;
    // Fisico arriba (y baja) = logico izquierda; fisico izquierda = logico abajo.
    t->swipeLeft = T.swipeUp;  t->swipeRight = T.swipeDown;
    t->swipeDown = T.swipeLeft; t->swipeUp = T.swipeRight;
  }
}
void brHostConsumeTouch(){
  T.pressed = false; T.released = false; T.tap = false; T.moved = false;
  T.swipeUp = false; T.swipeDown = false; T.swipeLeft = false; T.swipeRight = false;
}

// =============================================================
//  COLORES
//  -------------------------------------------------------------
//  El navegador NO ve la paleta del .ino (es `static`): pide colores
//  por indice semantico. Al cambiar el tema del sistema, la siguiente
//  vez que pinta, pinta con la apariencia nueva -- igual que el OTA.
// =============================================================
uint16_t brHostRGB(uint8_t r, uint8_t g, uint8_t b){ return rgb565(r, g, b); }

// La deteccion es por MACRO, no por placa: en cuanto el tema semantico
// global (TH_*) llegue tambien a Ultra S3 y Pro -- hoy solo esta en
// Ultra -- este mismo fichero empezara a usarlo sin tocar nada. Mientras
// tanto, en esas dos placas se usa su paleta de siempre (PAGE_BG /
// SET_TXT_*), que es la que ya siguen sus Ajustes, y los colores de
// ESTADO (verde/ambar/rojo) son fijos igual que en el resto del sistema.
#ifdef TH_PAGE
uint16_t brHostColor(int idx){
  switch(idx){
    case BRC_PAGE:   return TH_PAGE;
    case BRC_WIN:    return TH_WIN;
    case BRC_SURF:   return TH_SURF;
    case BRC_SURF2:  return TH_SURF2;
    case BRC_TXT:    return TH_TXT;
    case BRC_TXT2:   return TH_TXT2;
    case BRC_MUTE:   return TH_MUTE;
    case BRC_PRIM:   return TH_PRIM;
    case BRC_ONACC:  return TH_ONACC;
    case BRC_BORDER: return TH_BORDER;
    case BRC_DIV:    return TH_DIV;
    case BRC_OK:     return TH_OK;
    case BRC_WARN:   return TH_WARN;
    case BRC_ERR:    return TH_ERR;
    case BRC_DANGER: return TH_DANGER;
    case BRC_SEL:    return TH_SEL;
    case BRC_TRACK:  return TH_TRACK;
    case BRC_NAV:    return TH_NAV;
    default:         return TH_TXT;
  }
}
#else
uint16_t brHostColor(int idx){
  switch(idx){
    // El marco de la app (winRevealAnim / appDrawChrome) usa WIN_BG en
    // estas dos placas, asi que el navegador usa lo mismo: si aqui se
    // respetara gDark, en modo claro la barra del navegador saldria
    // clara pegada a una cabecera oscura.
    case BRC_PAGE:   return WIN_BG;
    case BRC_WIN:    return WIN_BG;
    case BRC_SURF:   return rgb565(34, 38, 50);
    case BRC_SURF2:  return rgb565(48, 54, 72);
    case BRC_TXT:    return rgb565(240, 242, 248);
    case BRC_TXT2:   return rgb565(160, 166, 182);
    case BRC_MUTE:   return rgb565(120, 126, 142);
    case BRC_PRIM:   return rgb565(60, 120, 235);   // el mismo azul del resto del sistema
    case BRC_ONACC:  return rgb565(255, 255, 255);
    case BRC_BORDER: return rgb565(60, 66, 84);
    case BRC_DIV:    return rgb565(44, 48, 62);
    case BRC_OK:     return rgb565(52, 199, 89);
    case BRC_WARN:   return rgb565(255, 159, 10);
    case BRC_ERR:    return rgb565(255, 69, 58);
    case BRC_DANGER: return rgb565(255, 99, 92);
    case BRC_SEL:    return rgb565(48, 72, 120);
    case BRC_TRACK:  return rgb565(60, 64, 78);
    case BRC_NAV:    return rgb565(240, 242, 248);
    default:         return rgb565(240, 242, 248);
  }
}
#endif

// =============================================================
//  DIBUJO  (una linea cada una)
// =============================================================
void brHostFillRect(int x, int y, int w, int h, uint16_t c){ fillRect(x, y, w, h, c); }
void brHostFillRoundRect(int x, int y, int w, int h, int r, uint16_t c){ fillRoundRect(x, y, w, h, r, c); }
void brHostDrawRoundRect(int x, int y, int w, int h, int r, uint16_t c){ drawRoundRect(x, y, w, h, r, c); }
void brHostFillCircle(int cx, int cy, int r, uint16_t c){ fillCircle(cx, cy, r, c); }
void brHostDrawCircle(int cx, int cy, int r, uint16_t c){ drawCircle(cx, cy, r, c); }
void brHostTriangle(int x0,int y0,int x1,int y1,int x2,int y2,uint16_t c){ fillTriangle(x0,y0,x1,y1,x2,y2,c); }
void brHostStroke(float x0, float y0, float x1, float y1, float w, uint16_t c){ strokeSegAA(x0, y0, x1, y1, w, c); }
void brHostText(int x, int y, const char* s, int size, uint16_t c){ drawText(x, y, s, size, c); }
void brHostTextC(int cx, int y, const char* s, int size, uint16_t c){ drawTextC(cx, y, s, size, c); }
void brHostTextR(int rx, int y, const char* s, int size, uint16_t c){ drawTextR(rx, y, s, size, c); }
void brHostTextClip(int x, int y, const char* s, int size, uint16_t c, int mr){ drawTextClip(x, y, s, size, c, mr); }
int  brHostTextW(const char* s, int size){ return textW(s, size); }
// VOLCADO Y RECORTE EN COORDENADAS DEL LIENZO.
// El navegador habla siempre de filas LOGICAS. En vertical son las filas del
// panel y no hay nada que traducir. En HORIZONTAL (pantalla completa girada)
// una fila logica es una COLUMNA del panel: el rango [y0,y1] cruza todas las
// filas fisicas, asi que se publica el lienzo entero. Y el recorte vertical
// pasa a la banda de la Y logica (gClipLY0/gClipLY1): con el de siempre
// (gClipY*), que en horizontal acota la X, la pagina se cortaba en x=480.
// (Dentro de una ventana de DeX flxFlush solo marca el lienzo como sucio.)
void brHostFlush(int y0, int y1){
  if(gLand && !gRtTarget){ flxFlush(0, SCR_H - 1); return; }
  flxFlush(y0, y1);
}

void brHostClip(int y0, int y1){
  if(y0 < 0) y0 = 0;
#ifdef FLEXOS_GFX_LANDCLIP
  if(gLand){
    if(y1 > SCR_W - 1) y1 = SCR_W - 1;
    gClipLY0 = y0; gClipLY1 = y1;
    gClipY0 = 0; gClipY1 = SCR_H - 1;
    return;
  }
#endif
  if(y1 > SCR_H - 1) y1 = SCR_H - 1;
  gClipY0 = y0; gClipY1 = y1;
}
void brHostClipReset(){
  gClipY0 = 0; gClipY1 = SCR_H - 1;
  gClipX0 = 0; gClipX1 = SCR_W - 1;
#ifdef FLEXOS_GFX_LANDCLIP
  gClipLY0 = 0; gClipLY1 = SCR_W - 1;
#endif
}

// -------------------------------------------------------------
//  EL UNICO CAMINO POR EL QUE LA PAGINA REMOTA LLEGA A LA PANTALLA
//  ------------------------------------------------------------
//  Aqui se recorta contra el AREA DE CONTENIDO de la app, no contra
//  la pantalla. Por eso una pagina externa no puede pintar sobre la
//  barra de estado, sobre la barra de navegacion, sobre la propia
//  barra del navegador ni sobre el teclado: aunque el servicio
//  mandara un frame mas alto de la cuenta, las filas de fuera se
//  descartan aqui. Es una comprobacion, no una convencion.
// -------------------------------------------------------------
void brHostBlitRow(int x, int y, int w, const uint16_t* src){
  if(!src || w <= 0 || !gBuf) return;

  // (a) Recorte contra el AREA DE CONTENIDO de la app.
  int cx, cy, cw, chh;
  brHostContentRect(&cx, &cy, &cw, &chh);
  if(y < cy || y >= cy + chh) return;
  if(x < cx){ int d = cx - x; src += d; w -= d; x = cx; }
  if(x + w > cx + cw) w = cx + cw - x;
  if(w <= 0) return;

  // (b) Recorte contra la banda activa del motor grafico, que es lo que
  //     respetan todas las demas primitivas. OJO A LA ORIENTACION: en un
  //     lienzo horizontal (ventana ancha de DeX, pantalla completa girada) la
  //     banda gClipY* acota la X LOGICA y la Y logica la acota gClipLY*. Antes
  //     se aplicaban SIEMPRE los limites verticales (gClipX* y SCR_W = 480):
  //     en una ventana de 780 de ancho cada fila de la pagina se cortaba en
  //     x=480 y el resto de la ventana se quedaba negro.
  const int lw = gLand ? SCR_H : SCR_W;            // ancho y alto del lienzo LOGICO
  const int lh = gLand ? SCR_W : SCR_H;
  if(gLand){
    if(x < gClipY0){ int d = gClipY0 - x; src += d; w -= d; x = gClipY0; }
    if(x + w - 1 > gClipY1) w = gClipY1 - x + 1;
#ifdef FLEXOS_GFX_LANDCLIP
    if(y < gClipLY0 || y > gClipLY1) return;
#endif
  } else {
    if(y < gClipY0 || y > gClipY1) return;
    if(x < gClipX0){ int d = gClipX0 - x; src += d; w -= d; x = gClipX0; }
    if(x + w - 1 > gClipX1) w = gClipX1 - x + 1;
  }
  if(w <= 0) return;

  // (c) Y un tope DURO contra el lienzo, sin depender de que (a) y (b) esten
  //     bien calculados. Los dos primeros recortes son de maquetacion; este es
  //     de seguridad de memoria: debajo hay escrituras directas sobre el
  //     framebuffer, y un descuadre en el area de contenido o en la banda de
  //     recorte no puede convertirse en una escritura fuera del buffer.
  if(y < 0 || y >= lh) return;
  if(x < 0){ int d = -x; src += d; w -= d; x = 0; }
  if(x >= lw) return;
  if(x + w > lw) w = lw - x;
  if(w <= 0) return;

#if FLEXBR_PLAT_PRO
  // ESP32 clasico: el lienzo LOGICO (480x640) no coincide con el
  // framebuffer FISICO (240x320) -- el escalado vive dentro de las
  // primitivas de dibujo. Hay que pasar por px() para que cada pixel
  // caiga donde toca. Es mas lento por pixel, pero en esta placa la
  // pagina llega ya al 50 % de escala (ver BrSettings::scalePct), asi
  // que son la mitad de pixeles.
  for(int i = 0; i < w; i++) px(x + i, y, src[i]);
#else
  // Ultra y Ultra S3: el lienzo logico ES el framebuffer (mismo paso de
  // SCR_W que usa px()), asi que una fila vertical es un memcpy. En
  // horizontal una fila logica es una COLUMNA fisica: (lx, ly) cae en
  // gBuf[lx * SCR_W + (SCR_W-1-ly)], con paso SCR_W. Ya recortada arriba,
  // se escribe directamente, sin repetir los recortes de px() por pixel.
  if(gLand){
    uint16_t* d = gBuf + (size_t)x * SCR_W + (size_t)((SCR_W - 1) - y);
    for(int i = 0; i < w; i++){ *d = src[i]; d += SCR_W; }
  }
  else memcpy(gBuf + (size_t)y * SCR_W + x, src, (size_t)w * 2);
#endif
}

// =============================================================
//  MEMORIA
// =============================================================
void* brHostAlloc(size_t n, bool preferPsram){
  void* p = NULL;
  if(preferPsram && heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0)
    p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!p) p = malloc(n);          // sin PSRAM (o llena): heap interno
  return p;
}
void  brHostFree(void* p){ if(p) free(p); }
// Heap INTERNO libre. No vale ESP.getFreeHeap() en las placas con
// PSRAM: alli suma la PSRAM y daria un numero enorme justo cuando lo
// que escasea es la RAM interna, que es la que necesita mbedTLS.
size_t brHostFreeHeap(){ return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); }
size_t brHostFreePsram(){ return heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }

// =============================================================
//  PERSISTENCIA
//  -------------------------------------------------------------
//  NVS en un espacio de nombres PROPIO ("flexos_br"), separado del de
//  los ajustes del sistema. Igual que hace el Wi-Fi con
//  "flexos_wifi", y por el mismo motivo: borrar los ajustes del
//  sistema no debe llevarse por delante la configuracion del
//  navegador, ni al reves.
// =============================================================
#define BR_NVS_NS "flexos_br"

bool brHostPrefsGetStr(const char* key, char* out, size_t n, const char* def){
  Preferences p;
  if(!p.begin(BR_NVS_NS, true)){ snprintf(out, n, "%s", def ? def : ""); return false; }
  String v = p.getString(key, def ? def : "");
  p.end();
  snprintf(out, n, "%s", v.c_str());
  return v.length() > 0;
}
void brHostPrefsPutStr(const char* key, const char* val){
  Preferences p;
  if(!p.begin(BR_NVS_NS, false)) return;
  p.putString(key, val ? val : "");
  p.end();
}
int brHostPrefsGetInt(const char* key, int def){
  Preferences p;
  if(!p.begin(BR_NVS_NS, true)) return def;
  int v = p.getInt(key, def);
  p.end();
  return v;
}
void brHostPrefsPutInt(const char* key, int v){
  Preferences p;
  if(!p.begin(BR_NVS_NS, false)) return;
  p.putInt(key, v);
  p.end();
}

bool brHostFsReady(){ return flexFsReady(); }
int  brHostFileRead(const char* path, void* buf, size_t n){ return flexFsReadBin(path, buf, n); }
bool brHostFileWrite(const char* path, const void* buf, size_t n){ return flexFsWriteBin(path, buf, n); }
bool brHostFileDelete(const char* path){ return flexFsDelete(path); }

// =============================================================
//  ENGANCHE CON LA APP  ·  navEnter / navTick
//  -------------------------------------------------------------
//  La app 7 del escritorio (IC_NAV) se registra con
//  APP_FLEX | APP_OWN_TOUCH:
//    · APP_FLEX      -> el navegador maqueta contra el lienzo real, asi
//                       que funciona igual a pantalla completa y dentro
//                       de una ventana de Modo PC/DeX.
//    · APP_OWN_TOUCH -> gestiona TODOS sus toques. El boton "atras" del
//                       sistema y el chevron de la cabecera se atienden
//                       aqui abajo a mano, para que primero retrocedan
//                       en el historial y SOLO cierren la app cuando ya
//                       no hay nada a lo que volver -- que es lo que
//                       hace cualquier navegador.
//    NO lleva APP_CUSTOM_HEADER: asi el framework sigue pintando la
//    barra de estado, la de navegacion y la cabecera con el nombre de
//    la app, y el navegador se ve como una app mas de Flex OS.
// =============================================================
static void navEnter(){
  // gRelayout es true cuando enter() se re-ejecuta SOLO para volver a
  // maquetar tras cambiar de tamano (arrastrando el borde de una ventana
  // de DeX). En ese caso no hay que reiniciar la sesion: basta repintar.
  // El teclado, si esta abierto, se vuelve a pintar ENCIMA: es la ultima capa
  // de cualquier cuadro, tambien del que sale de re-maquetar.
  if(gRelayout){ flexBrowserTick(); if(flexBrowserKeyboardOpen()) brKbRender(); return; }
  // Si FlexOS_BrowserApp.cpp fuera de otra version, esto no enlaza y el
  // error dice el nombre de la funcion -- que es la instruccion.
  flexBrVersionGuard_v5_copia_los_4_ficheros_del_navegador();
  brKbWasOpen = false;
  brKbHostSig = 0;
  brKbLastTop = brKbLastBottom = -1;
  kbExtrasOn = false;                      // sin portapapeles en el omnibox
  mapaActivo = LAYOUT_ES; kbLangEs = true; kbShift = false;
  kbApplySize();
  flexBrowserEnter();
}

// SUSPENDER / REANUDAR (multitarea real). navEnter() reinicia la sesion, asi
// que NO puede usarse para volver de Recientes: el desplazamiento y la pestana
// se perderian. Aqui se prepara lo mismo que prepara navEnter -- el teclado del
// sistema, que es del puente y no del navegador -- y se llama a Resume, que
// repinta desde el estado vivo.
static void navSuspend(){ flexBrowserSuspend(); }
static void navResume(){
  if(gRelayout){ flexBrowserTick(); if(flexBrowserKeyboardOpen()) brKbRender(); return; }
  brKbWasOpen = false;
  brKbHostSig = 0;
  brKbLastTop = brKbLastBottom = -1;
  kbExtrasOn = false;
  mapaActivo = LAYOUT_ES; kbLangEs = true; kbShift = false;
  kbApplySize();
  flexBrowserResume();
}

static void navTick(){
  // 0) ¿SE CERRO EL TECLADO DESDE LA VUELTA ANTERIOR?
  //    Se comprueba ANTES de nada para que el borrado y el repintado
  //    del navegador ocurran en el MISMO cuadro: si se hiciera al
  //    final, habria un cuadro con la franja en blanco.
  //    Cubre TODAS las salidas por igual, porque mira el estado y no el
  //    camino: "Ir", atras del sistema, chevron, guardar ajustes,
  //    cambio de pagina interna o perdida de foco.
  if(brKbWasOpen && !flexBrowserKeyboardOpen()){
    brKbErase();
    flexBrowserCancelDrag();     // el toque que cerro no deja gesto vivo
    brHostConsumeTouch();        // ni genera un tap en lo que hay debajo
    brKbWasOpen = false;
  }

  // 1) El teclado se queda con sus toques antes que nadie.
  bool kbOpen = flexBrowserKeyboardOpen();
  if(kbOpen){
    // CAPTURA EXCLUSIVA: mientras el teclado esta abierto, el toque es
    // suyo entero -- este dentro o fuera de la franja de teclas. Un
    // toque por encima cierra el teclado en vez de accionar lo que haya
    // detras, que es lo que hace cualquier teclado del sistema, y sobre
    // todo evita que el mismo dedo escriba y desplace Ajustes a la vez.
    if(!brKbTouch()){
      // Toque POR ENCIMA del teclado. Sobre el propio campo (la barra de
      // direcciones) o, escribiendo en la pagina, sobre la pagina: el
      // navegador se lo queda y se SIGUE escribiendo. Fuera de eso, se
      // cierra el teclado, como cualquier teclado del sistema.
      if(T.tap){
        int cx, cy; brTouchToCanvas(T.x, T.y, cx, cy);
        if(!flexBrowserKeyboardTapAbove(cx, cy)) flexBrowserKeyCancel();
      }
    }
    brHostConsumeTouch();
    // Si acaba de cerrarse por el toque de arriba, se borra ya: asi el
    // repintado del navegador de este mismo cuadro llega con el area
    // completa.
    if(!flexBrowserKeyboardOpen()){
      brKbErase();
      flexBrowserCancelDrag();
      brKbWasOpen = false;
      kbOpen = false;
    }
  }

  // 2) Boton "atras" del sistema y chevron de la cabecera. Con
  //    APP_OWN_TOUCH el framework ya no los mira, asi que se atienden
  //    aqui: primero retroceden dentro del navegador y solo cierran la
  //    app cuando no queda historial ni capas abiertas.
  //    En Flex OS Ultra NO: alli la barra y el chevron son del SISTEMA
  //    (navBarHandle va antes que la app y llama al gancho backLayer del
  //    navegador, que hace exactamente esto). Mirarlo aqui tambien convertia
  //    un toque en la parte baja de la PAGINA a pantalla completa -- donde no
  //    hay barra -- en un "atras".
#ifndef NAV_H
  if(!gHosted && T.tap){
    int ny = SCR_H - 52;
    bool navBack = (gNavMode == 0 && T.y >= ny - 10 && T.y <= ny + 22 && T.x < SCR_W / 3);
    bool chevron = (T.y <= WIN_TOP && T.x < 72);
    if(navBack || chevron){
      brHostConsumeTouch();
      if(!flexBrowserHandleSystemBack()){ flexBrowserExit(); appClose(); }
      return;
    }
  }
#endif
  // 3) El navegador.
  flexBrowserTick();

  // 4) EL TECLADO ES LA ULTIMA CAPA DEL CUADRO, SIEMPRE.
  //
  //    Se repinta en CADA vuelta mientras esta abierto, no solo al
  //    abrirse ni solo al pulsar. Es a proposito, y despues de que la
  //    version "solo cuando hace falta" fallara en la placa: cualquier
  //    repintado de la app, del marco de ventana o de una capa del
  //    sistema que caiga sobre la franja del teclado queda tapado por
  //    este dibujo antes de que el cuadro llegue al panel. Con la
  //    version condicional bastaba con que UN camino de repintado se
  //    escapara del analisis para que el teclado desapareciera y ya no
  //    volviera nunca.
  //
  //    Coste: unas 36 teclas por cuadro. En la practica el sistema esta
  //    parado mientras se escribe, y la certeza vale mas que esos
  //    milisegundos. Si hiciera falta afinarlo, el sitio es este -- no
  //    dentro de brKbRender.
  (void)flexBrowserRepaintedFull();          // se consume: ya no decide nada
  bool nowOpen = flexBrowserKeyboardOpen();
  if(nowOpen) brKbRender();
  else if(brKbWasOpen) brKbErase();          // cerrado dentro de flexBrowserTick()
  brKbWasOpen = nowOpen;

  // 5) Cierre pedido desde el menu del propio navegador.
  if(flexBrowserWantsClose()){ flexBrowserExit(); appClose(); }
}

#else   // ------------- navegador desactivado en compilacion -------------

// Con FLEXBR_ON = 0 la app queda como un aviso, no como una maqueta que
// finja funcionar. Mismo patron que el resto de interruptores maestros.
static void navEnter(){
  setBuf(fb);
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  // Colores literales: con FLEXBR_ON = 0 no se enlaza brHostColor(), y las
  // macros TH_* solo existen en Ultra.
  fillRect(bx, by, bw, bh, WIN_BG);
  drawTextC(bx + bw / 2, by + bh / 2 - 20, "Navegador desactivado", 3, rgb565(240, 242, 248));
  drawTextC(bx + bw / 2, by + bh / 2 + 16, "(FLEXBR_ON = 0 en este build)", 1, rgb565(120, 126, 142));
  flxFlush(WIN_TOP, WIN_BOT);
}
static void navTick(){}
static void navSuspend(){}
static void navResume(){ navEnter(); }

#endif // FLEXBR_ON
#endif // FLEXOS_BROWSER_BRIDGE_H
