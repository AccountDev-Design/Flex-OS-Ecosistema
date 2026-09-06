// #############################################################
// ##  FLEX OS ULTRA  ·  APP SWITCHER  ·  Recientes
// ##  ----------------------------------------------------------
// ##  Carrusel horizontal con mini-captura en PSRAM, arrastre con inercia,
// ##  cierre de tarjeta y hoja modal de detalle. Es un SELECTOR DE APPS:
// ##  el diagnostico de memoria vive en Almacenamiento.
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
#include "FlexOS_Ultra_AppWeather.h"   // eslabon anterior de la cadena

// #############################################################
// ##  APP SWITCHER (Multitarea) · carrusel horizontal estilo iOS
// ##  Tarjetas con mini-captura en PSRAM. Arrastre + inercia,
// ##  swipe-arriba para cerrar (free), toque para maximizar.
// #############################################################
// UNA TARJETA POR APP, MINIATURAS RACIONADAS.
// ---------------------------------------------------------------------------
// SW_MAX ya no es un tope de multitarea: es el limite natural (no puede haber
// mas tarjetas que apps). Quien decide cuantas apps caben es el presupuesto de
// memoria medido, no este numero.
//
// Lo que SI sigue racionado son las MINIATURAS, y a proposito: cada una son
// 150x250x2 = 73 KB de PSRAM. Con 19 tarjetas serian 1,4 MB reservados solo en
// capturas -- justo lo que no se puede hacer en una placa cuyo margen de
// seguridad son 6 MB. Se conservan las SW_THUMB_MAX mas recientes y el resto
// de tarjetas caen al respaldo que ya existia: marco + icono de la app.
#define SW_MAX        APP_N
#define SW_THUMB_MAX  4
#define TH_W    150
#define TH_H    250
// GEOMETRIA. Al quitar la cabecera de memoria (132 px) la tarjeta sube y crece:
// de 384 a 480 px de alto, empezando 100 px mas arriba. El area de imagen queda
// en 244x404, que es exactamente la proporcion de la miniatura (150x250), asi
// que se ve mas grande SIN deformarse ni un pixel.
#define SW_CW   260
#define SW_CH   480
#define SW_STEP 288
#define SW_TOP  92

struct AppTask { uint8_t appID; bool used; uint16_t* thumb; };   // estado suspendido + miniatura
static AppTask swTasks[SW_MAX];
static int   swCount = 0;
static float swScrollPx = 0, swVel = 0, swLiftY = 0;
static int   swLiftCard = -1, swGesture = 0;                     // 0 nada, 1 horizontal, 2 vertical
static float swStartX, swStartY, swLastX2, swLastY2;
#define SW_LONG_MS  480                                          // mantener pulsado -> ficha de la app

// PRESUPUESTO DE MINIATURAS. Se sueltan las mas antiguas ANTES de reservar
// una nueva, no despues: asi el pico de PSRAM nunca llega a subir. 'keep' es
// cuantas se conservan contando desde la mas reciente (la lista esta ordenada
// por uso, indice 0 = la ultima usada). Una tarjeta sin miniatura no
// desaparece: se dibuja con su marco y el icono de la app.
static void swThumbTrim(int keep){
  if(keep < 0) keep = 0;
  for(int i = keep; i < swCount; i++)
    if(swTasks[i].thumb){ free(swTasks[i].thumb); swTasks[i].thumb = NULL; }
}
static uint16_t* swAllocThumb(){
  int keep = gEffMode ? 1 : SW_THUMB_MAX;
  swThumbTrim(keep - 1);               // hueco para la que se va a tomar ahora
  // Una captura son 73 KB. Si el sistema esta apurado, la miniatura es lo
  // primero que sobra: la tarjeta sigue existiendo con el icono de la app.
  if(memFreePsram() < FLEXMEM_CRIT_BYTES + (size_t)TH_W * TH_H * 2) return NULL;
  return (uint16_t*)heap_caps_malloc((size_t)TH_W * TH_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static void captureThumb(uint16_t* dst){                          // reduce fb 480x800 -> 150x250
  for(int j = 0; j < TH_H; j++){
    int sy = j * SCR_H / TH_H; uint16_t* d = dst + (size_t)j * TH_W;
    for(int i = 0; i < TH_W; i++) d[i] = fb[(size_t)sy * SCR_W + i * SCR_W / TH_W];
  }
}
static void swPush(uint8_t id){                                   // mueve al frente (o inserta)
  int at = -1; for(int i = 0; i < swCount; i++) if(swTasks[i].appID == id){ at = i; break; }
  if(at >= 0){ AppTask tmp = swTasks[at]; for(int i = at; i > 0; i--) swTasks[i] = swTasks[i - 1]; swTasks[0] = tmp; return; }
  if(swCount < SW_MAX){ for(int i = swCount; i > 0; i--) swTasks[i] = swTasks[i - 1]; swCount++; }
  else {
    // Lista llena: la tarjeta que cae es la mas antigua. No basta con soltar su
    // miniatura -- hay que CERRAR esa app de verdad (guardando su sesion), o su
    // estado quedaria vivo en RAM para siempre sin tarjeta que lo represente.
    appTerminate(swTasks[SW_MAX - 1].appID, true);
    if(swTasks[SW_MAX - 1].thumb) free(swTasks[SW_MAX - 1].thumb);
    for(int i = SW_MAX - 1; i > 0; i--) swTasks[i] = swTasks[i - 1];
  }
  swTasks[0].appID = id; swTasks[0].used = true; swTasks[0].thumb = NULL;
}
static void swPushAndCapture(uint8_t id){
  swPush(id);
  if(!swTasks[0].thumb) swTasks[0].thumb = swAllocThumb();
  if(swTasks[0].thumb) captureThumb(swTasks[0].thumb);
}
// Para apps que dibujan en LANDSCAPE (Modo PC, Juegos): captureThumb reduce fb
// tal cual, y fb contiene el frame ya ROTADO. Guardarlo daria una miniatura
// girada 90 dentro de una tarjeta vertical -- que es justo lo que se veia en el
// selector de recientes. Aqui se entra en la lista SIN miniatura (y se tira la
// que hubiera, que seria de una sesion anterior), asi swRenderCards cae a su
// respaldo: tarjeta + icono de la app, que si esta bien orientado.
static void swPushNoThumb(uint8_t id){
  swPush(id);
  if(swTasks[0].thumb){ free(swTasks[0].thumb); swTasks[0].thumb = NULL; }
}
// ---- Accesores publicos de la lista (los usa el ciclo de vida, mucho mas
// arriba en el archivo, sin tener que conocer AppTask ni swTasks) ----
static int  swCardCount(){ return swCount; }
static int  swCardApp(int idx){ return (idx >= 0 && idx < swCount) ? swTasks[idx].appID : -1; }
// Quita la tarjeta y libera SU miniatura. NO toca el ciclo de vida de la app:
// es la mitad grafica de un cierre, y la usa quien ya cerro la app por su cuenta.
static void swDropCard(int idx){
  if(idx < 0 || idx >= swCount) return;
  if(swTasks[idx].thumb){ free(swTasks[idx].thumb); swTasks[idx].thumb = NULL; }
  for(int i = idx; i < swCount - 1; i++) swTasks[i] = swTasks[i + 1];
  swCount--;
}
// Aviso breve del selector (por que una tarjeta no se ha podido cerrar). Es un
// mensaje REAL: solo aparece cuando una operacion se ha negado de verdad.
static char     swMsg[40] = "";
static uint32_t swMsgMs = 0;
static void swToast(const char* m){ snprintf(swMsg, sizeof(swMsg), "%s", m); swMsgMs = millis(); }

// Cierre COMPLETO de una tarjeta: cierra la app (guardando su sesion y
// liberando sus buffers) y solo entonces quita la tarjeta. Devuelve false si el
// cierre se ha negado -- trabajo esencial en curso, o una sesion que no se pudo
// escribir -- y en ese caso no se pierde nada.
static bool swCloseCard(int idx){
  if(idx < 0 || idx >= swCount) return false;
  int id = swTasks[idx].appID;
  if(appBgBusy(id)){ swToast("Tarea en curso: no se cierra"); return false; }
  if(!appTerminate(id, false)){ swToast("Sin espacio para guardar"); return false; }
  swDropCard(idx);
  return true;
}
// "Cerrar todo": cierra las apps normales y RESPETA la lista blanca -- una app
// con trabajo real en segundo plano (hoy: Ajustes mientras el OTA descarga o
// instala) se queda, con su tarjeta y su tarea intactas.
static void swCloseAll(){
  int kept = 0;
  for(int i = swCount - 1; i >= 0; i--){
    int id = swTasks[i].appID;
    if(appBgBusy(id)){ kept++; continue; }
    if(!appTerminate(id, false)){ kept++; continue; }
    appMemForget(id);
    swDropCard(i);
  }
  if(kept) swToast("Tarea en curso: no se cierra");
}
// Cuantas tarjetas llevan cambios del usuario sin guardar. Es lo que decide si
// "Cerrar todo" pide confirmacion: cerrar sin avisar una nota a medias no es
// "liberar recursos", es perder trabajo.
static int swUnsavedCount(){
  int n = 0;
  for(int i = 0; i < swCount; i++) if(appUnsaved(swTasks[i].appID)) n++;
  return n;
}
// Area de imagen de una tarjeta. Se define AQUI porque la tabla de escalado se
// dimensiona con ella: si el alto de la tarjeta cambia y esto no, la ruta
// rapida deja de coincidir y cada miniatura vuelve a escalarse con divisiones.
#define SW_TH_W  (SW_CW - 16)
#define SW_TH_H  (SW_CH - 76)
static uint16_t swSxLUT[SW_TH_W], swSyLUT[SW_TH_H]; static bool swLUTdone = false;
static void swBuildLUT(){
  for(int i = 0; i < SW_TH_W; i++) swSxLUT[i] = (uint16_t)(i * TH_W / SW_TH_W);
  for(int j = 0; j < SW_TH_H; j++) swSyLUT[j] = (uint16_t)(j * TH_H / SW_TH_H);
  swLUTdone = true;
}
static void blitThumbScaled(uint16_t* th, int dx, int dy, int dw, int dh){
  bool lut = (dw == SW_TH_W && dh == SW_TH_H);              // ruta rapida (tamano fijo)
  if(lut && !swLUTdone) swBuildLUT();
  for(int j = 0; j < dh; j++){ int yy = dy + j; if((unsigned)yy >= SCR_H) continue;
    int sy = lut ? swSyLUT[j] : j * TH_H / dh;
    uint16_t* s = th + (size_t)sy * TH_W; uint16_t* d = gBuf + (size_t)yy * SCR_W;
    int x0 = dx < 0 ? 0 : dx, x1 = dx + dw > SCR_W ? SCR_W : dx + dw;
    for(int xx = x0; xx < x1; xx++) d[xx] = s[lut ? swSxLUT[xx - dx] : (xx - dx) * TH_W / dw];
  }
}
// Marco tipo vidrio (barato: sobre el fondo oscuro uniforme el blur no aporta,
// asi el carrusel corre fluido). drawLiquidGlassPanel se reserva para superficies con contenido detras.
static void swCardFrame(int x, int y, int w, int h, int rad){
  fillRoundRect(x, y, w, h, rad, TH_SURF);
  drawRoundRect(x, y, w, h, rad, TH_BORDER);
}
// #############################################################
// ##  RECIENTES  ·  un SELECTOR DE APPS, no un panel de memoria
// ##  ------------------------------------------------------
// ##  QUE SE QUITO Y POR QUE. Aqui vivia una cabecera permanente con el
// ##  recuento de apps por estado, "PSRAM disponible: X / 32 MB", una
// ##  barra de ocupacion y un boton "Optimizar Flex OS". Ocupaba 132 px
// ##  -- una sexta parte de la pantalla -- en TODAS las aperturas, y lo
// ##  que informaba no era accionable: quien abre Recientes quiere
// ##  cambiar de app, no auditar la RAM.
// ##
// ##  El sistema de medida NO se ha tocado: memTick() sigue midiendo,
// ##  el presupuesto sigue decidiendo que se abre y memAlertTick() sigue
// ##  avisando -- pero solo cuando de verdad hace falta, y por la isla de
// ##  notificaciones, que es temporal y no cambia el layout. El
// ##  diagnostico completo vive donde corresponde: Almacenamiento ->
// ##  Detalles de memoria y sistema, junto al boton Optimizar.
// ##
// ##  Lo que queda: titulo, tarjetas (mas altas, porque ahora cabe),
// ##  nombre, un estado discreto, el punto de cambios sin guardar, los
// ##  gestos de siempre y "Cerrar todas".
// #############################################################

// Banda de las tarjetas: lo unico que se repinta al mover el carrusel. Antes
// habia que dejarla disjunta de la cabecera; ahora que no hay cabecera, cubre
// todo lo que puede moverse y ni una fila mas.
#define SWC_BAND0 (SW_TOP - 10)
#define SWC_BAND1 (SW_TOP + SW_CH + 18)

// Estado de UNA tarjeta, en las palabras del usuario. Es lo unico de la
// multitarea que sigue siendo visible de forma permanente, y lo es porque
// responde a una pregunta que el usuario SI se hace al mirar la tarjeta:
// "¿esto sigue como lo deje?".
static const char* swStateName(int id){
  if(gAppState[id] == ALIFE_RUNNING || gAppState[id] == ALIFE_RESUMING) return "Activa";
  return gAppShed[id] ? "Estado guardado" : "Pausada";
}

// ---- Boton "Cerrar todo" (geometria unica: dibujo y hit-test la comparten) ----
#define SWCA_W   200
#define SWCA_H   46
#define SWCA_X   ((SCR_W - SWCA_W) / 2)
#define SWCA_Y   600
static void swDrawCloseAll(){
  bool on = (swCount > 0);
  fillRoundRect(SWCA_X, SWCA_Y, SWCA_W, SWCA_H, SWCA_H / 2, on ? TH_SURF2 : TH_SURF);
  drawRoundRect(SWCA_X, SWCA_Y, SWCA_W, SWCA_H, SWCA_H / 2, on ? TH_BORDER : TH_DIV);
  drawTextC(SCR_W / 2, SWCA_Y + (SWCA_H - 18) / 2, "Cerrar todas", 2, on ? TH_ONWALL : TH_DIS);
}
static bool swCloseAllHit(int px, int py){
  return swCount > 0 && px >= SWCA_X && px <= SWCA_X + SWCA_W && py >= SWCA_Y && py <= SWCA_Y + SWCA_H;
}
// Aviso dentro de la banda de tarjetas: se borra solo con el siguiente repintado
// de esa misma banda, igual que el chip "Copiado" de Notas.
static void swDrawToast(){
  if(!swMsg[0]) return;
  if(millis() - swMsgMs > 1800){ swMsg[0] = 0; return; }
  int tw = textW(swMsg, 2) + 34, tx = (SCR_W - tw) / 2, ty = SW_TOP + SW_CH - 60;
  fillRoundRectA(tx, ty, tw, 34, 17, TH_WALLSURF, 235);
  drawTextC(SCR_W / 2, ty + 9, swMsg, 2, TH_ONWALL);
}

// ---- Contenido de UNA tarjeta (lo comparten la animacion de entrada y el
// ---- repintado por cuadro, para que no puedan dibujar cosas distintas) ----
static void swDrawCard(int i, int x, int y, int cw, int ch){
  int id = swTasks[i].appID;
  swCardFrame(x, y, cw, ch, 22);
  int iw = cw - 16, ih = ch - 76;
  if(swTasks[i].thumb) blitThumbScaled(swTasks[i].thumb, x + 8, y + 8, iw, ih);
  else { fillRoundRect(x + 8, y + 8, iw, ih, 14, TH_SURF2);
         drawAppIcon(id, x + cw / 2 - 30, y + ih / 2 - 22, 60); }
  // Nombre + punto de "cambios sin guardar". El punto es DISCRETO a proposito:
  // informa sin gritar, y solo aparece cuando la marca de sesion desfasada (o
  // la propia app) dice que hay algo sin escribir.
  const char* nm = appName(id);
  bool dirty = appUnsaved(id);
  int nw = textW(nm, 2);
  int nx = x + (cw - nw) / 2;
  if(dirty){
    nx -= 8;
    fillCircle(nx + nw + 12, y + ch - 52 + 9, 4, TH_WARN);
  }
  drawText(nx, y + ch - 52, nm, 2, TH_TXT);
  // ESTADO, y nada mas. El consumo en KB era diagnostico tecnico permanente:
  // se ha movido a la ficha que sale al mantener pulsada la tarjeta, que es
  // donde alguien lo busca a proposito.
  const char* st = swStateName(id);
  int sw2 = textW(st, 1);
  drawText(x + (cw - sw2) / 2, y + ch - 28, st, 1, TH_TXT2);
}

static void swRender(float scale){                        // completo (solo animacion de entrada)
  setBuf(bbuf);
  // Fondo = wallpaper desenfocado (contenido) -> los rotulos que caen encima
  // usan TH_ONWALL, no el texto de pagina.
  if(blurBg) memcpy(bbuf, blurBg, (size_t)SCR_W * SCR_H * 2); else fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  drawTextC(SCR_W / 2, 30, "Recientes", 3, TH_ONWALL);
  if(swCount == 0) drawTextC(SCR_W / 2, SW_TOP + SW_CH / 2, "Sin apps recientes", 2, TH_ONWALL2);
  int cw = (int)(SW_CW * scale), ch = (int)(SW_CH * scale);
  for(int i = 0; i < swCount; i++){
    int cx = SCR_W / 2 + i * SW_STEP - (int)swScrollPx;
    if(cx < -SW_CW || cx > SCR_W + SW_CW) continue;
    int x = cx - cw / 2, y = SW_TOP + (SW_CH - ch) / 2;
    if(i == swLiftCard) y -= (int)swLiftY;
    swDrawCard(i, x, y, cw, ch);
  }
  swDrawCloseAll();
  swDrawToast();
  drawTextC(SCR_W / 2, SCR_H - 28, "Desliza una tarjeta arriba para cerrar", 1, TH_ONWALL2);
  present(0, SCR_H - 1);
}
// por-frame: SOLO repinta y vuelca la banda de las tarjetas (mucho mas ligero)
static void swRenderCards(){
  setBuf(bbuf);
  if(blurBg){ for(int j = SWC_BAND0; j <= SWC_BAND1; j++) memcpy(bbuf + (size_t)j * SCR_W, blurBg + (size_t)j * SCR_W, SCR_W * 2); }
  else fillRect(0, SWC_BAND0, SCR_W, SWC_BAND1 - SWC_BAND0 + 1, TH_PAGE);
  if(swCount == 0) drawTextC(SCR_W / 2, SW_TOP + SW_CH / 2, "Sin apps recientes", 2, TH_ONWALL2);
  for(int i = 0; i < swCount; i++){
    int cx = SCR_W / 2 + i * SW_STEP - (int)swScrollPx;
    if(cx < -SW_CW || cx > SCR_W + SW_CW) continue;
    int x = cx - SW_CW / 2, y = SW_TOP;
    if(i == swLiftCard) y -= (int)swLiftY;
    swDrawCard(i, x, y, SW_CW, SW_CH);
  }
  swDrawToast();
  present(SWC_BAND0, SWC_BAND1);
}
// Repinta la fila del boton (su aspecto depende de si queda alguna tarjeta).
static void swRenderCloseAll(){
  setBuf(bbuf);
  if(blurBg){ for(int j = SWCA_Y - 6; j < SWCA_Y + SWCA_H + 6; j++) memcpy(bbuf + (size_t)j * SCR_W, blurBg + (size_t)j * SCR_W, SCR_W * 2); }
  else fillRect(0, SWCA_Y - 6, SCR_W, SWCA_H + 12, TH_PAGE);
  swDrawCloseAll();
  present(SWCA_Y - 6, SWCA_Y + SWCA_H + 5);
}

// #############################################################
// ##  HOJA MODAL DE RECIENTES  ·  detalle y confirmacion
// ##  ------------------------------------------------------
// ##  Dos usos, una sola superficie (dos capas dibujando en la misma
// ##  banda es justo el fallo que documenta la isla de notificaciones):
// ##   · MANTENER PULSADA una tarjeta -> ficha de la app: estado,
// ##     clase de peso, consumo medido, ultima actividad, si tiene
// ##     cambios sin guardar, y el boton Cerrar.
// ##   · "Cerrar todas" con trabajo sin guardar -> confirmacion que
// ##     DICE cuantas apps lo tienen. Sin cambios pendientes no
// ##     pregunta nada: seria una pregunta de adorno.
// #############################################################
enum { SWS_NONE = 0, SWS_DETAIL, SWS_CONFIRM_ALL };
static int      swSheet = SWS_NONE;
static int      swSheetIdx = -1;
static uint32_t swPressMs = 0;
static bool     swLongDone = false;
#define SWS_X   28
#define SWS_W   (SCR_W - 56)
#define SWS_Y   250
#define SWS_H   300
// Dos botones al pie de la hoja, misma geometria para dibujo y hit-test.
#define SWS_BH  46
#define SWS_BY  (SWS_Y + SWS_H - SWS_BH - 16)
#define SWS_B1X (SWS_X + 16)
#define SWS_BW  ((SWS_W - 48) / 2)
#define SWS_B2X (SWS_B1X + SWS_BW + 16)

static void swSheetRender(){
  setBuf(bbuf);
  if(blurBg) memcpy(bbuf, blurBg, (size_t)SCR_W * SCR_H * 2);
  else       fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  fillRectA(0, 0, SCR_W, SCR_H, TH_SCRIM, 150);
  uiWallSurface(SWS_X, SWS_Y, SWS_W, SWS_H, 24, uiGlass ? TH_WALLSURF2 : TH_WALLSURF, 12);

  int y = SWS_Y + 18;
  if(swSheet == SWS_CONFIRM_ALL){
    int n = swUnsavedCount();
    char t[80];
    drawTextC(SCR_W / 2, y, "Cerrar todas las apps", 2, TH_ONWALL); y += 34;
    snprintf(t, sizeof(t), "%d app%s tiene%s cambios sin guardar.", n, n == 1 ? "" : "s", n == 1 ? "" : "n");
    drawTextC(SCR_W / 2, y, t, 1, TH_ONWALL2); y += 22;
    drawTextC(SCR_W / 2, y, "Flex OS intentar\xC3\xA1 guardarlos antes de cerrar;", 1, TH_ONWALL2); y += 18;
    drawTextC(SCR_W / 2, y, "la que no pueda guardarse se queda abierta.", 1, TH_ONWALL2);
    fillRoundRect(SWS_B1X, SWS_BY, SWS_BW, SWS_BH, SWS_BH / 2, TH_SURF2);
    drawTextC(SWS_B1X + SWS_BW / 2, SWS_BY + (SWS_BH - 18) / 2, "Cancelar", 2, TH_ONWALL);
    fillRoundRect(SWS_B2X, SWS_BY, SWS_BW, SWS_BH, SWS_BH / 2, TH_DANGER);
    drawTextC(SWS_B2X + SWS_BW / 2, SWS_BY + (SWS_BH - 18) / 2, "Cerrar todas", 2, TH_ONACC);
    present(0, SCR_H - 1);
    return;
  }

  int id = (swSheetIdx >= 0 && swSheetIdx < swCount) ? swTasks[swSheetIdx].appID : -1;
  if(id < 0){ swSheet = SWS_NONE; swRender(1.0f); return; }
  drawAppIcon(id, SWS_X + 20, y, 48);
  drawText(SWS_X + 84, y + 2,  appName(id), 3, TH_ONWALL);
  drawText(SWS_X + 84, y + 30, swStateName(id), 1, TH_ONWALL2);
  y += 66;

  char v[64];
  // Consumo MEDIDO. Sin medida: "No disponible", que es la verdad.
  if(appMemHas(id)) flexMemFmt(appMemBytes(id), v, sizeof(v));
  else              snprintf(v, sizeof(v), "No disponible");
  drawText(SWS_X + 20, y, "Consumo estimado", 1, TH_ONWALL2);
  drawTextR(SWS_X + SWS_W - 20, y, v, 2, TH_ONWALL); y += 26;

  drawText(SWS_X + 20, y, "Clase", 1, TH_ONWALL2);
  drawTextR(SWS_X + SWS_W - 20, y, appWeightName(id), 2, TH_ONWALL); y += 26;

  drawText(SWS_X + 20, y, "Miniatura", 1, TH_ONWALL2);
  if(swSheetIdx >= 0 && swTasks[swSheetIdx].thumb) flexMemFmt((size_t)TH_W * TH_H * 2, v, sizeof(v));
  else snprintf(v, sizeof(v), "Sin captura");
  drawTextR(SWS_X + SWS_W - 20, y, v, 2, TH_ONWALL); y += 26;

  drawText(SWS_X + 20, y, "\xC3\x9Altima actividad", 1, TH_ONWALL2);
  if(gAppSeenMs[id]){
    uint32_t s = (millis() - gAppSeenMs[id]) / 1000u;
    if(s < 60)      snprintf(v, sizeof(v), "hace %u s", (unsigned)s);
    else if(s < 3600) snprintf(v, sizeof(v), "hace %u min", (unsigned)(s / 60));
    else            snprintf(v, sizeof(v), "hace %u h", (unsigned)(s / 3600));
  } else snprintf(v, sizeof(v), "No disponible");
  drawTextR(SWS_X + SWS_W - 20, y, v, 2, TH_ONWALL); y += 26;

  drawText(SWS_X + 20, y, "Cambios sin guardar", 1, TH_ONWALL2);
  drawTextR(SWS_X + SWS_W - 20, y, appUnsaved(id) ? "S\xC3\xAD" : "No", 2,
            appUnsaved(id) ? TH_WARN : TH_ONWALL);

  fillRoundRect(SWS_B1X, SWS_BY, SWS_BW, SWS_BH, SWS_BH / 2, TH_SURF2);
  drawTextC(SWS_B1X + SWS_BW / 2, SWS_BY + (SWS_BH - 18) / 2, "Volver", 2, TH_ONWALL);
  fillRoundRect(SWS_B2X, SWS_BY, SWS_BW, SWS_BH, SWS_BH / 2, TH_DANGER);
  drawTextC(SWS_B2X + SWS_BW / 2, SWS_BY + (SWS_BH - 18) / 2, "Cerrar", 2, TH_ONACC);
  present(0, SCR_H - 1);
}
static void swSheetClose(){ swSheet = SWS_NONE; swSheetIdx = -1; swRender(1.0f); }
// Toque dentro de la hoja. Devuelve true si lo consumio (siempre que la hoja
// este abierta: es modal, igual que el resto de modales del sistema).
static bool swSheetTouch(){
  if(swSheet == SWS_NONE) return false;
  if(!T.tap) return true;
  bool inB = (T.y >= SWS_BY && T.y <= SWS_BY + SWS_BH);
  bool b1 = inB && T.x >= SWS_B1X && T.x <= SWS_B1X + SWS_BW;
  bool b2 = inB && T.x >= SWS_B2X && T.x <= SWS_B2X + SWS_BW;
  if(swSheet == SWS_CONFIRM_ALL){
    if(b2){ swCloseAll(); swScrollPx = 0; swVel = 0; swSheetClose(); return true; }
    if(b1 || T.y < SWS_Y || T.y > SWS_Y + SWS_H){ swSheetClose(); return true; }
    return true;
  }
  if(b2){
    int idx = swSheetIdx;
    swSheet = SWS_NONE; swSheetIdx = -1;
    if(idx >= 0) swCloseCard(idx);
    float mx = (swCount > 0 ? (swCount - 1) * SW_STEP : 0);
    if(swScrollPx > mx) swScrollPx = mx;
    if(swScrollPx < 0) swScrollPx = 0;
    swRender(1.0f);
    return true;
  }
  if(b1 || T.y < SWS_Y || T.y > SWS_Y + SWS_H){ swSheetClose(); return true; }
  return true;
}

static int swCardIndexAt(int px){
  for(int i = 0; i < swCount; i++){ int cx = SCR_W / 2 + i * SW_STEP - (int)swScrollPx; if(px >= cx - SW_CW / 2 && px <= cx + SW_CW / 2) return i; }
  return -1;
}
static int swCenterIndex(){ int i = (int)roundf(swScrollPx / SW_STEP); if(i < 0) i = 0; if(i >= swCount) i = swCount - 1; return i; }
static void swExitToHome(){ gState = ST_HOME; renderHome(); showHome(); touchDropAll(); }
// Restaura la app a pantalla completa. enterApp la REANUDA si estaba suspendida:
// vuelve exactamente donde estaba, no a su pantalla inicial.
static void swMaximize(int idx){ if(idx >= 0 && idx < swCount) enterApp(swTasks[idx].appID); }

// Congela la app activa, cambia a MODO_MULTITAREA y hace la animacion elastica de entrada.
static void activarMultitarea(){
  if(KIOSK_ON && kioskOn) return;             // FASE 4: sin selector de apps en kiosco
  // FLEX VAULT: entrar en Recientes cierra la boveda. No es solo por politica:
  // el selector CAPTURA el ultimo cuadro de lo que hubiera en pantalla, asi que
  // llegar aqui con la boveda abierta es justo el camino por el que una
  // miniatura privada acabaria en PSRAM.
  vaultLockFromSystem(FXV_LOCK_EXIT);
  if(gHosted){ gHostReq = 3; return; }        // -> Recientes de DeX
  // Red de seguridad igual que en appClose: el selector se dibuja en portrait.
  // Si se llegara aqui con gLand=true, las tarjetas saldrian rotadas y a medias.
  gLand = false;
  gClipY0 = 0; gClipY1 = SCR_H - 1;
  gClipX0 = 0; gClipX1 = SCR_W - 1;
  setBuf(fb);
  ensureBlurBg();
  gState = ST_SWITCHER;
  swScrollPx = 0; swVel = 0; swLiftCard = -1; swLiftY = 0; swGesture = 0;
  swMsg[0] = 0;
  swSheet = SWS_NONE; swSheetIdx = -1; swLongDone = false; swPressMs = 0;
  // ENTRADA POR TIEMPO, no por numero de cuadros. Antes eran 10 pasos con
  // delay(14): en una placa lenta duraba mas y en una rapida se veia a saltos,
  // y ademas bloqueaba el bucle 140 ms enteros (con el tactil parado). Ahora
  // dura SW_IN_MS medidos con millis() y mete tantos cuadros como de el
  // compositor, sin un solo delay().
  if(gSafeMode){ swRender(1.0f); return; }        // Modo seguro: sin animacion de entrada
  const uint32_t SW_IN_MS = 150;
  uint32_t t0 = millis();
  for(;;){
    uint32_t e = millis() - t0; if(e > SW_IN_MS) e = SW_IN_MS;
    float p = (float)e / (float)SW_IN_MS, pm = p - 1.0f;
    float eob = 1.0f + 2.6f * pm * pm * pm + 1.6f * pm * pm;   // ease-out-back
    swRender(0.6f + 0.4f * eob);
    if(e >= SW_IN_MS) break;
  }
  swRender(1.0f);
}
static void swTick(){
  // La hoja modal manda: mientras esta abierta nadie mas ve el toque.
  if(swSheetTouch()) return;
  // El aviso caduca solo: sin esto se quedaria pegado hasta el siguiente gesto.
  if(swMsg[0] && millis() - swMsgMs > 1800){ swMsg[0] = 0; swRenderCards(); }
  if(T.pressed){
    swStartX = T.x; swStartY = T.y; swLastX2 = T.x; swLastY2 = T.y; swVel = 0; swGesture = 0;
    swLiftCard = swCardIndexAt(T.x); swLiftY = 0;
    swPressMs = millis(); swLongDone = false;
    return;
  }
  if(T.down){
    float dx = T.x - swLastX2, dy = T.y - swLastY2; (void)dy;
    if(swGesture == 0){
      if(fabsf(T.x - swStartX) > 12) swGesture = 1;
      else if(fabsf(T.y - swStartY) > 14) swGesture = 2;
    }
    // MANTENER PULSADA: solo si el dedo NO se ha movido (sin gesto) y esta
    // sobre una tarjeta. Se dispara una sola vez por pulsacion.
    if(swGesture == 0 && !swLongDone && swLiftCard >= 0 &&
       T.y >= SW_TOP && T.y <= SW_TOP + SW_CH &&
       millis() - swPressMs >= SW_LONG_MS){
      swLongDone = true;
      swSheet = SWS_DETAIL; swSheetIdx = swLiftCard;
      swLiftCard = -1; swLiftY = 0;
      swSheetRender();
      return;
    }
    if(swGesture == 1){                                  // scroll horizontal + velocidad
      swScrollPx -= dx; swVel = -dx;
      float mn = -90, mx = (swCount - 1) * SW_STEP + 90; if(swScrollPx < mn) swScrollPx = mn; if(swScrollPx > mx) swScrollPx = mx;
      swLiftY = 0; swRenderCards();
    } else if(swGesture == 2 && swLiftCard >= 0){        // levantar tarjeta (cerrar)
      swLiftY = swStartY - T.y; if(swLiftY < 0) swLiftY = 0; if(swLiftY > 116) swLiftY = 116; swRenderCards();
    }
    swLastX2 = T.x; swLastY2 = T.y;
    return;
  }
  if(T.released){
    if(swLongDone){ swLongDone = false; swLiftCard = -1; swLiftY = 0; return; }
    if(swGesture == 2 && swLiftCard >= 0 && swLiftY > 110){   // swipe-arriba -> cerrar de verdad
      bool closed = swCloseCard(swLiftCard);
      swLiftCard = -1; swLiftY = 0;
      float mx = (swCount > 0 ? (swCount - 1) * SW_STEP : 0); if(swScrollPx > mx) swScrollPx = mx; if(swScrollPx < 0) swScrollPx = 0;
      swRenderCards();
      if(closed) swRenderCloseAll();                          // el boton se atenua si ya no queda nada
      return;
    }
    if(swGesture == 0){                                  // toque
      if(swCloseAllHit(T.x, T.y)){
        // Con trabajo sin guardar se pregunta ANTES. Sin el, se cierra sin
        // molestar: una confirmacion que siempre sale deja de leerse.
        if(swUnsavedCount() > 0){ swSheet = SWS_CONFIRM_ALL; swSheetRender(); return; }
        swCloseAll(); swScrollPx = 0; swVel = 0; swRenderCards(); swRenderCloseAll(); return;
      }
      if(T.y > SCR_H - 60){ swExitToHome(); return; }
      int idx = swCardIndexAt(T.x);
      if(idx >= 0 && T.y >= SW_TOP && T.y <= SW_TOP + SW_CH){
        if(idx == swCenterIndex()) swMaximize(idx); else { swScrollPx = idx * SW_STEP; swRenderCards(); }
        return;
      }
      swExitToHome(); return;
    }
    swLiftCard = -1; swLiftY = 0;
    return;
  }
  // reposo: inercia + enganche elastico a la tarjeta mas cercana
  if(fabsf(swVel) > 0.4f){
    swScrollPx += swVel; swVel *= 0.90f;
    float mx = (swCount > 0 ? (swCount - 1) * SW_STEP : 0);
    if(swScrollPx < 0){ swScrollPx = 0; swVel = 0; } if(swScrollPx > mx){ swScrollPx = mx; swVel = 0; }
    swRenderCards();
  } else {
    int tgt = swCenterIndex() * SW_STEP;
    if((int)swScrollPx != tgt){ swScrollPx += (tgt - swScrollPx) * 0.25f; if(fabsf(tgt - swScrollPx) < 1) swScrollPx = tgt; swRenderCards(); }
  }
}
