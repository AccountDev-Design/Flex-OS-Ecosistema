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
static int brKbBottom(){ return gHosted ? gAppH : SCR_H; }
static int brKbDY(){ return gHosted ? (gAppH - SCR_H) : 0; }

// Altura reservada por el teclado. El navegador maqueta CONTRA esto
// (brHostKeyboardTop), asi que su contenido nunca queda debajo de las
// teclas y no hace falta desplazar nada a mano.
static int brKbTop(){
  if(!flexBrowserKeyboardOpen()) return brKbBottom();
  int top = kbPanelTop() - 34 + brKbDY();   // 34 px para la linea del campo
  int floorY = gHosted ? 0 : 80;
  if(top < floorY) top = floorY;
  return top;
}

static void brKbRender(){
  if(!flexBrowserKeyboardOpen()) return;
  setBuf(fb);
  int dy = brKbDY();
  int bottom = brKbBottom();
  int ky = KB_Y + dy;
  int panelTop = brKbTop();
  if(panelTop >= bottom) return;            // ventana demasiado baja: nada que pintar

  // EL TECLADO ABRE SU PROPIA BANDA DE RECORTE.
  //
  // No es redundante con que el navegador reponga la suya: es la
  // garantia de que el teclado se ve SIEMPRE, sin depender de quien
  // haya dibujado antes ni de en que orden. El fallo que motivo esto
  // (ESP32-P4: el teclado escribia pero no se veia) era exactamente
  // eso -- otra capa habia dejado la banda estrecha y px() descartaba
  // en silencio cada pixel del teclado.
  //
  // Se recorta a la franja del teclado, ni mas ni menos: asi el teclado
  // tampoco puede pintar por encima del contenido de la app.
  gClipY0 = panelTop < 0 ? 0 : panelTop;
  gClipY1 = bottom - 1;
  gClipX0 = 0;
  gClipX1 = SCR_W - 1;
  // Cabecera del campo: que se esta editando y el texto actual. Se pinta
  // aqui y no en el navegador porque este trozo de pantalla pertenece al
  // teclado (el area de contenido de la app termina en panelTop).
  // Los colores salen de brHostColor() y no de las macros TH_* del .ino:
  // esas solo existen en Ultra (el tema semantico aun no esta en las otras
  // dos placas) y este fichero es el MISMO para las tres.
  fillRect(0, panelTop, SCR_W, bottom - panelTop, brHostColor(BRC_PAGE));
  drawText(12, panelTop + 4, flexBrowserEditLabel(), 1, brHostColor(BRC_MUTE));
  const char* txt = flexBrowserEditText();
  int tw = textW(txt, 2);
  if(tw <= SCR_W - 24) drawText(12, panelTop + 16, txt, 2, brHostColor(BRC_TXT));
  else                 drawTextR(SCR_W - 12, panelTop + 16, txt, 2, brHostColor(BRC_TXT));

  if(uiGlass) drawLiquidGlassPanel(0, ky - 4, SCR_W, bottom - (ky - 4), 0, kbColPanel());
  else        fillRect(0, ky - 4, SCR_W, bottom - (ky - 4), kbColPanel());

  int fs = kbFontSize();
  for(int r = 0; r < KB_ROWS; r++) for(int c = 0; c < KB_COLS; c++){
    int x = KB_X + c * (KB_KW + KB_GAP), y = ky + r * (KB_KH + KB_GAP);
    const char* k = mapaActivo[r][c];
    char u[6];
    if(kbShift && k[1] == 0 && k[0] >= 'a' && k[0] <= 'z'){ u[0] = (char)(k[0] - 32); u[1] = 0; k = u; }
    kbPaintKey(x, y, KB_KW, KB_KH, k, fs, kbColKey(), kbColKeyTxt(), false);
  }
  int fy = ky + 3 * (KB_KH + KB_GAP);
  const char* lb[KB_FKEYS] = { "shift", kbLayerLabel(), kbLangEs ? "ES" : "EN", "espacio", "<-", "Ir" };
  for(int i = 0; i < KB_FKEYS; i++) kbFKey(kbFKeyX(i), fy, kbFKeyW(i), lb[i], (i == 0) && kbShift);
  flxFlush(panelTop, bottom - 1);
  brHostClipReset();          // se deja la pantalla entera para el siguiente
}

// Devuelve true si el toque se consumio dentro del teclado.
static bool brKbTouch(){
  if(!flexBrowserKeyboardOpen()) return false;
  if(T.y < brKbTop()) return false;              // por encima: es de la app
  if(!T.tap){ return true; }                     // el area del teclado consume todo
  // Las funciones kb* del sistema razonan en coordenadas ABSOLUTAS de
  // pantalla, asi que se deshace el desplazamiento antes de
  // preguntarles. A pantalla completa dy es 0: la identidad.
  const int ty = T.y - brKbDY();
  int fi = kbFRowHit(T.x, ty);
  if(fi >= 0){
    if(fi == 0) kbShift = !kbShift;
    else if(fi == 1) mapaActivo = (mapaActivo == LAYOUT_NUM) ? LAYOUT_EMOJI
                                : (mapaActivo == LAYOUT_EMOJI) ? (kbLangEs ? LAYOUT_ES : LAYOUT_EN)
                                : LAYOUT_NUM;
    else if(fi == 2){ kbLangEs = !kbLangEs; if(mapaActivo == LAYOUT_ES || mapaActivo == LAYOUT_EN) mapaActivo = kbLangEs ? LAYOUT_ES : LAYOUT_EN; }
    else if(fi == 3) flexBrowserKeyText(" ");
    else if(fi == 4) flexBrowserKeyBackspace();
    else { flexBrowserKeyEnter(); return true; }
    brKbRender();
    return true;
  }
  int cell = kbCellAt(T.x, ty);
  if(cell >= 0){
    const char* k = mapaActivo[cell / KB_COLS][cell % KB_COLS];
    if(kbShift && k[1] == 0 && k[0] >= 'a' && k[0] <= 'z'){
      char u[2] = { (char)(k[0] - 32), 0 };
      flexBrowserKeyText(u);
      kbShift = false;
    } else {
      flexBrowserKeyText(k);
    }
    brKbRender();
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
void brHostFlush(int y0, int y1){ flxFlush(y0, y1); }

void brHostClip(int y0, int y1){
  if(y0 < 0) y0 = 0;
  if(y1 > SCR_H - 1) y1 = SCR_H - 1;
  gClipY0 = y0; gClipY1 = y1;
}
void brHostClipReset(){
  gClipY0 = 0; gClipY1 = SCR_H - 1;
  gClipX0 = 0; gClipX1 = SCR_W - 1;
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

  // (b) Recorte contra la banda activa del motor grafico (gClip*), que es
  //     lo que respetan todas las demas primitivas.
  if(y < gClipY0 || y > gClipY1) return;
  if(x < gClipX0){ int d = gClipX0 - x; src += d; w -= d; x = gClipX0; }
  if(x + w - 1 > gClipX1) w = gClipX1 - x + 1;
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
  // SCR_W que usa px()), asi que una fila entera es un memcpy. Es la
  // ruta que de verdad importa: se ejecuta una vez por fila de cada
  // frame. En horizontal (gLand) el mapeo no es lineal, asi que ahi se
  // cae al camino por pixel -- el navegador nunca es landscape, pero no
  // se deja el caso abierto.
  if(gLand){ for(int i = 0; i < w; i++) px(x + i, y, src[i]); }
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
  if(gRelayout){ flexBrowserTick(); return; }
  brKbWasOpen = false;
  kbExtrasOn = false;                      // sin portapapeles en el omnibox
  mapaActivo = LAYOUT_ES; kbLangEs = true; kbShift = false;
  kbApplySize();
  flexBrowserEnter();
}

static void navTick(){
  // 1) El teclado se queda con sus toques antes que nadie.
  bool kbOpen = flexBrowserKeyboardOpen();
  if(kbOpen && brKbTouch()) brHostConsumeTouch();

  // 2) Boton "atras" del sistema y chevron de la cabecera. Con
  //    APP_OWN_TOUCH el framework ya no los mira, asi que se atienden
  //    aqui: primero retroceden dentro del navegador y solo cierran la
  //    app cuando no queda historial ni capas abiertas.
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
  // 3) El navegador.
  flexBrowserTick();

  // 4) EL TECLADO SE DIBUJA SIEMPRE DESPUES DEL CONTENIDO DE LA APP.
  //    Se repinta cuando se abre y cada vez que el navegador ha
  //    repintado su area: asi ningun redibujado posterior de la app lo
  //    puede dejar tapado o a medias. Mientras nada de eso ocurre solo
  //    se repinta al pulsar una tecla (brKbTouch), no por cuadro --
  //    redibujar 60 teclas en cada frame costaria demasiado.
  bool repainted = flexBrowserRepaintedFull();
  bool nowOpen = flexBrowserKeyboardOpen();
  if(nowOpen && (!brKbWasOpen || repainted)) brKbRender();
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

#endif // FLEXBR_ON
#endif // FLEXOS_BROWSER_BRIDGE_H
