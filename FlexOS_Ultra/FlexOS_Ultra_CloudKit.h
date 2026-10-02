// #############################################################
// ##  FLEX OS ULTRA  ·  KIT DE FLEX CLOUD  (interfaz de la nube)
// ##  ----------------------------------------------------------
// ##  La nube dentro de Archivos, Galeria y Multimedia: estado de la
// ##  conexion, cuota, lista o rejilla de la nube, menus, transferencias
// ##  y los avisos de subidas y descargas.
// ##
// ##  COMO ENCAJA ESTE ARCHIVO
// ##  ----------------------------------------------------------
// ##  Es una PARTE del sketch FlexOS_Ultra.ino, no una unidad de
// ##  traduccion independiente. FlexOS_Ultra.ino lo incluye en el
// ##  orden que fija la cadena de cabeceras (cada modulo incluye al
// ##  anterior), asi que todo el sistema sigue compilandose como UN
// ##  SOLO archivo. No lo incluyas por tu cuenta desde otro sitio.
// #############################################################
#pragma once
#include "FlexOS_Ultra_MediaViewer.h"   // eslabon anterior de la cadena

// #############################################################
// ##  REGLAS DE ESTE KIT
// ##  ------------------------------------------------------
// ##  1. LA RED NO DIBUJA Y LA INTERFAZ NO ESPERA. Todo lo de la nube lo
// ##     hace FlexOS_Cloud en sus tareas; aqui solo se lee su estado
// ##     (copias bajo su cerrojo) y se le piden cosas que se encolan.
// ##  2. LOCAL Y NUBE NO SE MEZCLAN. Lo que se ve aqui esta EN LA NUBE y
// ##     se dice: pestana "Nube", la pildora de estado y la cuota. Un
// ##     archivo de la nube nunca aparece como si estuviera en el P4.
// ##  3. CALIDAD ORIGINAL. Una foto se abre trayendo el ORIGINAL
// ##     (verificado con su SHA-256); un video AVI se reproduce por rangos
// ##     sin descargarlo. Nada se recomprime ni se transcodifica.
// ##  4. LIBERAR ESPACIO SOLO TRAS CONFIRMAR. El original de "Subir y
// ##     liberar espacio" se borra aqui SOLO cuando la nube confirmo el
// ##     archivo con la misma huella y el gestor volvio a leer el original
// ##     y sigue siendo ese (FCE_UPLOAD_DONE con FCL_JF_FREE_LOCAL).
// ##  5. VIDRIO SIN APILAR Y SIN DESENFOQUE POR CUADRO. Las tarjetas son
// ##     superficies planas resueltas por filas (uiSurfaceFlat sobre el
// ##     fondo que se acaba de pintar); el progreso se repinta como mucho
// ##     4 veces por segundo y nunca con el dedo apoyado.
// #############################################################

static void mediaOpenInPlayer(const char* path);              // Multimedia (mas abajo)
static void almFolderIcon(int x, int y, int s);                // Almacenamiento (mas abajo)
static void settingsJumpAccount();                             // Ajustes -> General -> Flex Account

#define CK_STATUS_H     72        // tarjeta de estado y cuota (tres filas)
#define CK_CRUMB_H      30        // linea de la ruta (solo en Archivos)
#define CK_ROW_H        66
#define CK_COLS          3
#define CK_ROWS_MAX     24        // elementos que se copian para un repintado (los visibles)
#define CK_REDRAW_MS   250        // progreso / miniaturas: como mucho 4 repintados por segundo
#define CK_THUMBS_PER_DRAW 8

enum { CKM_BROWSE = 0, CKM_MEDIA, CKM_VIDEO };

// Lo que pone cada app que ensena la nube.
struct CkHost {
  const char* name;                                       // "Archivos", "Galeria", "Multimedia" (avisos)
  uint8_t mode;                                           // CKM_*
  void (*box)(int& x, int& y, int& w, int& h);            // zona de la nube dentro de la app
  void (*redraw)();                                       // repinta la app entera
  void (*openLocal)(const char* path);                    // abre una foto YA traida, o "cloud:<id>/<nombre>"
  uint16_t bg;                                            // fondo de la zona (WIN_BG o TH_PAGE)
};

// ---- Estado (una sola vista de la nube a la vez: la de la app delante) ----
static const CkHost* ckHost = NULL;
static uint8_t  ckView = FCL_VIEW_FOLDER;
static char     ckFolder[FCL_ID_MAX] = "";               // "" = raiz
static int      ckScroll = 0, ckDragY0 = 0, ckDragS0 = 0;
static bool     ckDragging = false, ckLongFired = false;
static uint32_t ckSeenList = 0, ckSeenStatus = 0, ckSeenThumb = 0, ckSeenXfer = 0;
static uint32_t ckDrawMs = 0, ckVisibleMs = 0;
static uint32_t ckShownGen = 0;                          // lista que se pinto (los toques van contra ella)
static uint8_t  ckLastNet = 0xFF;                         // para pedir otra vez la lista al volver la conexion
static FclItem* ckRows = NULL;                           // CK_ROWS_MAX (PSRAM): los visibles
static int      ckRowsFrom = 0, ckRowsN = 0;
static FclItem  ckMenuItem;                              // elemento del menu abierto
static bool     ckMenuForItem = false;
static uint32_t ckViewOp = 0;                            // foto que se esta trayendo para el visor
static char     ckViewName[64] = "";
enum { CKA_NONE = 0, CKA_DELETE };
enum { CKN_NONE = 0, CKN_MKDIR, CKN_RENAME };
static uint8_t  ckAsk = CKA_NONE, ckName = CKN_NONE;
static bool     ckXfersOn = false;
static uint32_t ckXferIds[FLEX_CLOUD_XFERS];
static int      ckXferN = 0;

static bool ckActive(){ return ckHost != NULL; }

// ---- Sin una credencial que sirva, la nube se desactiva y se dice por que ----
// NULL = Flex Cloud se puede usar. Si no, el motivo para el usuario.
// flexAccountLinked() solo dice que HAY una credencial guardada;
// flexAccountUsable() que Flex Account no la ha rechazado (desvinculada desde la
// web, revocada o caducada). Con una cuenta rechazada, "linked" sigue siendo true:
// comprobar solo eso dejaba encolar subidas que esperaban "conexion" para siempre.
// Los textos caben en el aviso de la isla (DetectedModule::sub: 39 letras).
#define CK_MSG_LINK   "Vincula tu cuenta en Ajustes > General"
#define CK_MSG_RELINK "Vuelve a vincular en Ajustes > General"
static_assert(sizeof(CK_MSG_LINK) <= sizeof(((DetectedModule*)0)->sub) &&
              sizeof(CK_MSG_RELINK) <= sizeof(((DetectedModule*)0)->sub), "el aviso de la isla cortaria el texto");
static const char* ckCloudBlock(){
  if(!flexAccountLinked()) return CK_MSG_LINK;
  if(!flexAccountUsable()) return CK_MSG_RELINK;
  return NULL;
}
// Una orden de la nube no se pudo encolar: si es por la cuenta se dice eso; si no, `otherwise`.
static void ckNotifyFail(const char* title, const char* otherwise){
  const char* why = ckCloudBlock();
  sysNotify(title, why ? why : otherwise);
}

// ---- La nube esta a la vista: la cuota se mantiene al dia (FlexOS_Cloud) ----
static void ckMarkVisible(){ ckVisibleMs = millis() | 1u; }

// #############################################################
// ##  DIBUJO: piezas pequenas
// #############################################################
// Nube vectorial (sin imagenes, como el resto del sistema).
static void ckCloudGlyph(int cx, int cy, int s, uint16_t col){
  fillCircle(cx - s * 5 / 10, cy + s / 10, s * 4 / 10, col);
  fillCircle(cx + s / 10, cy - s * 2 / 10, s * 55 / 100, col);
  fillCircle(cx + s * 6 / 10, cy + s / 6, s * 35 / 100, col);
  fillRoundRect(cx - s * 5 / 10, cy + s / 10, s * 11 / 10, s * 4 / 10 + 1, s / 5, col);
}

// Color del estado de la conexion.
static uint16_t ckNetColor(uint8_t net){
  switch(net){
    case FCN_ONLINE:      return TH_OK;
    case FCN_CONNECTING:  return TH_PRIM;
    case FCN_AUTH:        return TH_DANGER;
    case FCN_UNAVAILABLE: return TH_WARN;
    default:              return TH_MUTE;
  }
}

// Tarjeta de estado: pildora + cuota. `x,y,w` de la tarjeta. Tres filas que
// no se tocan: estado (y transferencias en curso), cuota en texto y su barra.
static void ckDrawStatus(int x, int y, int w, const FlexCloudStatus& st){
  uiSurfaceFlat(x, y, w, CK_STATUS_H - 8, 14, UIS_CARD, ckHost ? ckHost->bg : WIN_BG);
  int r1 = y + 9;
  ckCloudGlyph(x + 24, r1 + 6, 12, wallAccent());
  fillCircle(x + 48, r1 + 4, 4, ckNetColor(st.net));
  // "Vuelve a vincular tu cuenta" en el color de alarma: no es un estado mas de la red.
  drawTextClip(x + 58, r1, flexCloudNetText(st.net), 1, st.net == FCN_AUTH ? TH_DANGER : TH_TXT, x + w / 2 + 40);
  if(st.activeXfers){
    char b[32]; snprintf(b, sizeof(b), "%u en curso", (unsigned)st.activeXfers);
    int tw = textW(b, 1) + 16;
    fillRoundRect(x + w - tw - 12, r1 - 5, tw, 20, 10, TH_ACCS);
    drawText(x + w - tw - 4, r1, b, 1, TH_TXT);
  }
  int r2 = y + 30, r3 = y + 46;
  // Sin cuenta, o con una que Flex Account ya no reconoce, la cuota de antes NO se
  // ensena: era de una cuenta que ya no sirve. En su lugar, el motivo.
  bool acctOk = st.net != FCN_NO_ACCOUNT && st.net != FCN_AUTH;
  if(acctOk && st.quotaValid){
    char q[64], hint[64];
    fclQuotaLine(&st.quota, q, sizeof(q));
    fclQuotaHint(&st.quota, hint, sizeof(hint));
    drawTextClip(x + 14, r2, q, 1, TH_TXT2, x + w / 2 + 30);
    uint16_t hc = st.quota.state == FCL_Q_FULL ? TH_DANGER : st.quota.state == FCL_Q_LOW ? TH_WARN : TH_TXT2;
    drawTextR(x + w - 14, r2, hint, 1, hc);
    int bw = w - 28;
    fillRoundRect(x + 14, r3, bw, 6, 3, TH_TRACK);
    uint16_t col = st.quota.state == FCL_Q_FULL ? TH_DANGER : st.quota.state == FCL_Q_LOW ? TH_WARN : wallAccent();
    int fw = (int)((int64_t)bw * st.quota.permille / 1000);
    if(st.quota.permille && fw < 6) fw = 6;
    if(fw > 0) fillRoundRect(x + 14, r3, fw, 6, 3, col);
  } else {
    drawTextClip(x + 14, r2 + 4, st.netText[0] ? st.netText : "Espacio: sin datos todav\xC3\xAD" "a", 1, TH_TXT2, x + w - 14);
  }
}

// Icono de un elemento (44x44): miniatura de la nube si la hay, si no, su clase.
struct CkBlit { int x, y, w, h, rad; uint16_t bg; bool small; };
static void ckBlitCb(const uint16_t* px, int side, void* u){
  (void)side;
  CkBlit* b = (CkBlit*)u;
  if(b->small) mlBlitThumbSmall(px, b->x, b->y, b->rad);
  else         mlBlitThumb(px, b->x, b->y, b->w, b->h, b->rad, b->bg);
}
static void ckKindGlyph(const FclItem& it, int x, int y, int s){
  fillRoundRect(x, y, s, s, 8, TH_SURF2);
  int cx = x + s / 2, cy = y + s / 2;
  if(it.kind == FCL_K_VIDEO) fillTriangle(cx - 6, cy - 9, cx - 6, cy + 9, cx + 9, cy, TH_TXT2);
  else if(it.kind == FCL_K_PHOTO){ fillTriangle(x + 7, y + s - 10, x + 18, y + s / 2 - 2, x + 29, y + s - 10, TH_TXT2); fillCircle(x + s - 12, y + 13, 4, TH_TXT2); }
  else if(it.kind == FCL_K_AUDIO){ fillCircle(cx - 4, cy + 7, 5, TH_TXT2); fillRect(cx, cy - 10, 2, 17, TH_TXT2); fillRect(cx, cy - 10, 8, 3, TH_TXT2); }
  else { fillRoundRect(cx - 9, cy - 12, 18, 24, 3, rgb565(250, 250, 252)); fillRect(cx - 9, cy - 12, 18, 6, rgb565(200, 204, 214)); }
}
static int ckThumbBudget = 0;
static bool ckDrawThumb(const FclItem& it, CkBlit& b){
  if(it.isFolder || !it.hasThumb) return false;
  if(flexCloudThumbDraw(it.id, ckBlitCb, &b)) return true;
  if(ckThumbBudget > 0){ ckThumbBudget--; flexCloudWantThumb(it.id); }
  return false;
}

// #############################################################
// ##  GEOMETRIA
// #############################################################
static void ckBox(int& x, int& y, int& w, int& h){
  if(ckHost && ckHost->box) ckHost->box(x, y, w, h); else uiBox(x, y, w, h);
}
static int ckHeadH(){ return CK_STATUS_H + (ckHost && ckHost->mode == CKM_BROWSE ? CK_CRUMB_H : 0) + 4; }
static bool ckGrid(){ return ckHost && ckHost->mode == CKM_MEDIA; }
// Fila 0 = "subir" (volver a la carpeta de arriba) dentro de una carpeta.
static bool ckHasUp(){ return ckHost && ckHost->mode == CKM_BROWSE && ckView == FCL_VIEW_FOLDER && ckFolder[0]; }
static void ckCellRect(int i, int& x, int& y, int& w, int& h){
  int bx, by, bw, bh; ckBox(bx, by, bw, bh);
  int pad = 12, top = by + ckHeadH();
  if(ckGrid()){
    int gap = 8;
    w = (bw - 2 * pad - (CK_COLS - 1) * gap) / CK_COLS; h = w;
    x = bx + pad + (i % CK_COLS) * (w + gap);
    y = top + (i / CK_COLS) * (h + 24) - ckScroll;
    return;
  }
  int row = i + (ckHasUp() ? 1 : 0);
  x = bx + pad; w = bw - 2 * pad; h = CK_ROW_H - 8;
  y = top + row * CK_ROW_H - ckScroll;
}
static int ckContentH(int n){
  if(ckGrid()){ int x, y, w, h; ckCellRect(0, x, y, w, h); return ((n + CK_COLS - 1) / CK_COLS) * (h + 24) + 40; }
  return (n + (ckHasUp() ? 1 : 0)) * CK_ROW_H + 40;
}
static int ckMaxScroll(){
  int bx, by, bw, bh; ckBox(bx, by, bw, bh);
  FlexCloudListInfo li; flexCloudListInfo(&li);
  int m = ckHeadH() + ckContentH(li.count) - bh;
  return m > 0 ? m : 0;
}

// #############################################################
// ##  PANTALLAS SIN LISTA (estado vacio, sin cuenta, sin red, error)
// #############################################################
// Boton de accion de un estado vacio (zona que se toca = zona que se pinta).
static int ckEmptyBtnY = -1, ckEmptyBtnAct = 0;     // 1 vincular, 2 reintentar
static void ckEmptyState(int bx, int by, int bw, int bh, const char* title, const char* sub, const char* btn, int act){
  int cy = by + bh / 2 - 40;
  ckCloudGlyph(bx + bw / 2, cy - 30, 30, TH_SURF2);
  drawTextC(bx + bw / 2, cy + 10, title, 2, TH_TXT);
  if(sub && sub[0]){
    // Ajustado por palabras: el mismo de los dialogos.
    mmWrap(bx + 30, cy + 40, bw - 60, sub, 1, TH_TXT2, true);
  }
  ckEmptyBtnY = -1; ckEmptyBtnAct = 0;
  if(btn){
    int y = cy + 96, w = 220;
    fillRoundRect(bx + bw / 2 - w / 2, y, w, 48, 16, wallAccent());
    drawTextC(bx + bw / 2, y + 14, btn, 2, TH_ONACC);
    ckEmptyBtnY = y; ckEmptyBtnAct = act;
  }
}

// #############################################################
// ##  LISTA / REJILLA
// #############################################################
static void ckDrawItem(int i, const FclItem& it){
  int x, y, w, h; ckCellRect(i, x, y, w, h);
  uint16_t bg = ckHost ? ckHost->bg : WIN_BG;
  if(ckGrid()){
    int rad = w / 10; if(rad < 3) rad = 3;
    fillRoundRect(x, y, w, h, rad, TH_SURF2);
    CkBlit b = { x, y, w, h, rad, bg, false };
    if(!ckDrawThumb(it, b)) ckKindGlyph(it, x + w / 2 - 22, y + h / 2 - 22, 44);
    if(it.kind == FCL_K_VIDEO){ fillCircle(x + 18, y + 18, 12, rgb565(0, 0, 0)); fillTriangle(x + 14, y + 12, x + 14, y + 24, x + 24, y + 18, rgb565(255, 255, 255)); }
    // Marca de nube: esto NO esta en el dispositivo.
    fillCircle(x + w - 15, y + 15, 11, rgb565(0, 0, 0));
    ckCloudGlyph(x + w - 15, y + 15, 9, rgb565(255, 255, 255));
    drawTextClip(x, y + h + 4, it.name, 1, TH_TXT2, x + w);
    return;
  }
  uiSurfaceFlat(x, y, w, h, 12, UIS_CARD, bg);
  int ix = x + 8, iy = y + (h - 44) / 2;
  if(it.isFolder) almFolderIcon(ix, iy + 2, 40);
  else {
    CkBlit b = { ix, iy, 44, 44, 8, bg, true };
    if(!ckDrawThumb(it, b)) ckKindGlyph(it, ix, iy, 44);
  }
  drawTextClip(x + 62, y + 9, it.name, 2, TH_TXT, x + w - 12);
  char sub[64]; fclItemSub(&it, sub, sizeof(sub));
  if(ckView == FCL_VIEW_TRASH) { size_t L = strlen(sub); snprintf(sub + L, sizeof(sub) - L, " \xC2\xB7 en la papelera"); }
  drawTextClip(x + 62, y + 36, sub, 1, TH_TXT2, x + w - 12);
}

static void ckCrumbs(int bx, int y, int bw, const FlexCloudListInfo& li){
  char path[160];
  // " / " y no un separador tipografico: la fuente del sistema no lo tiene.
  if(ckView == FCL_VIEW_TRASH) snprintf(path, sizeof(path), "Flex Cloud / Papelera");
  else if(ckView != FCL_VIEW_FOLDER) snprintf(path, sizeof(path), "Flex Cloud");
  else {
    int L = snprintf(path, sizeof(path), "Flex Cloud");
    for(int k = 0; k < li.nCrumbs && L < (int)sizeof(path) - 8; k++)
      L += snprintf(path + L, sizeof(path) - (size_t)L, " / %s", li.crumbs[k].name);
  }
  // Si no cabe se ensena el FINAL (donde se esta), no el principio.
  const char* p = path;
  while(textW(p, 1) > bw - 24 && *p){ p++; while((*p & 0xC0) == 0x80) p++; }
  drawText(bx + 14, y + 8, p, 1, TH_TXT2);
}

static void ckRenderList(){
  int bx, by, bw, bh; ckBox(bx, by, bw, bh);
  uint16_t bg = ckHost->bg;
  setBuf(fb);
  fillRect(bx, by, bw, bh, bg);
  ckEmptyBtnY = -1;
  FlexCloudStatus st; flexCloudStatus(&st);
  FlexCloudListInfo li; flexCloudListInfo(&li);
  ckSeenStatus = st.gen; ckSeenList = li.gen; ckSeenThumb = flexCloudThumbGen(); ckSeenXfer = st.xferGen;
  ckShownGen = li.gen;
  ckDrawStatus(bx + 12, by + 4, bw - 24, st);
  if(ckHost->mode == CKM_BROWSE) ckCrumbs(bx, by + CK_STATUS_H, bw, li);
  int top = by + ckHeadH();

  // Sin cuenta / credencial rechazada / sin red: se dice, no se ensena una lista vacia.
  if(st.net == FCN_NO_ACCOUNT){
    ckEmptyState(bx, top, bw, by + bh - top, "Sin Flex Account", "Vincula tu Flex Account para guardar fotos, v\xC3\xAD" "deos y archivos en Flex Cloud (5 GB incluidos).", "Vincular cuenta", 1);
    ckRowsN = 0; return;
  }
  if(st.net == FCN_AUTH){
    ckEmptyState(bx, top, bw, by + bh - top, "Vuelve a vincular tu cuenta", st.netText, "Abrir Flex Account", 1);
    ckRowsN = 0; return;
  }
  if(li.state == FCL_LIST_ERROR && li.count == 0){
    ckEmptyState(bx, top, bw, by + bh - top, st.net == FCN_OFFLINE ? "Sin conexi\xC3\xB3n" : "No se pudo cargar", li.error, "Reintentar", 2);
    ckRowsN = 0; return;
  }
  if((li.state == FCL_LIST_LOADING || li.state == FCL_LIST_IDLE) && li.count == 0){
    drawTextC(bx + bw / 2, top + 80, "Cargando Flex Cloud...", 2, TH_TXT2);
    ckRowsN = 0; return;
  }
  if(li.count == 0 && !ckHasUp()){
    const char* t = ckView == FCL_VIEW_TRASH ? "La papelera est\xC3\xA1 vac\xC3\xAD" "a" : ckView == FCL_VIEW_FOLDER ? "Esta carpeta est\xC3\xA1 vac\xC3\xAD" "a" : "A\xC3\xBAn no hay nada aqu\xC3\xAD";
    const char* s = ckView == FCL_VIEW_TRASH ? "Lo que se elimina se guarda aqu\xC3\xAD 30 d\xC3\xAD" "as y se puede restaurar."
                  : "Sube fotos y v\xC3\xAD" "deos desde la Galer\xC3\xAD" "a (mant\xC3\xA9n pulsado > Subir a Flex Cloud) o desde la web.";
    ckEmptyState(bx, top, bw, by + bh - top, t, s, NULL, 0);
    ckRowsN = 0; return;
  }

  // ---- elementos visibles: solo esos se copian y se pintan ----
  if(!ckRows) ckRows = (FclItem*)mediaAlloc(sizeof(FclItem) * CK_ROWS_MAX);
  int first = 0, x, y, w, h;
  for(; first < li.count; first++){ ckCellRect(first, x, y, w, h); if(y + h + 24 > top) break; }
  if(ckGrid()) first -= first % CK_COLS;
  ckRowsFrom = first;
  ckRowsN = ckRows ? flexCloudListCopy(ckRows, first, CK_ROWS_MAX) : 0;
  ckThumbBudget = CK_THUMBS_PER_DRAW;
  uiClipViewport(top, by + bh - 1);
  if(ckHasUp()){
    int ux = bx + 12, uy = top - ckScroll, uw = bw - 24;
    uiSurfaceFlat(ux, uy, uw, CK_ROW_H - 8, 12, UIS_CARD, bg);
    almFolderIcon(ux + 10, uy + 10, 38);
    drawText(ux + 62, uy + 18, "..", 3, TH_TXT);
    drawTextR(ux + uw - 16, uy + 22, "subir", 1, TH_TXT2);
  }
  for(int k = 0; k < ckRowsN; k++){
    ckCellRect(first + k, x, y, w, h);
    if(y > by + bh) break;
    ckDrawItem(first + k, ckRows[k]);
  }
  if(li.more && li.state == FCL_LIST_READY){
    ckCellRect(li.count, x, y, w, h);
    if(y < by + bh) drawTextC(bx + bw / 2, (ckGrid() ? y : y + 18), "Cargando m\xC3\xA1s...", 1, TH_TXT2);
    // Cerca del final se pide la pagina siguiente (sin esperar a un toque).
    if(y < by + bh + 200) flexCloudRequestMore();
  }
  uiClipFull();
}

// ---- Transferencias (pantalla propia dentro de la zona de la nube) ----
#define CKX_TOP  56
#define CKX_RH   78
static void ckRenderXfers(){
  int bx, by, bw, bh; ckBox(bx, by, bw, bh);
  uint16_t bg = ckHost->bg;
  setBuf(fb);
  fillRect(bx, by, bw, bh, bg);
  strokeSegAA(bx + 30, by + 26, bx + 18, by + 18, 2.4f, TH_TXT);
  strokeSegAA(bx + 18, by + 18, bx + 30, by + 10, 2.4f, TH_TXT);
  drawText(bx + 44, by + 8, "Transferencias", 3, TH_TXT);
  FlexCloudXfer xs[FLEX_CLOUD_XFERS];
  int n = flexCloudXfers(xs, FLEX_CLOUD_XFERS);
  FlexCloudStatus st; flexCloudStatus(&st);
  ckSeenXfer = st.xferGen;
  ckXferN = n;
  if(!n) drawTextC(bx + bw / 2, by + bh / 2 - 10, "No hay subidas ni descargas", 2, TH_TXT2);
  bool anyDone = false;
  for(int i = 0; i < n; i++){
    const FlexCloudXfer& x = xs[i];
    ckXferIds[i] = x.id;
    int y = by + CKX_TOP + i * CKX_RH, w = bw - 24, X = bx + 12;
    if(y + CKX_RH > by + bh - 70) break;
    uiSurfaceFlat(X, y, w, CKX_RH - 8, 12, UIS_CARD, bg);
    // Flecha: arriba = subida, abajo = descarga.
    int ax = X + 22, ay = y + 22;
    bool up = x.type == FCL_JOB_UPLOAD;
    fillRect(ax - 1, ay - 9, 3, 18, TH_TXT2);
    if(up) fillTriangle(ax - 8, ay - 3, ax + 9, ay - 3, ax + 1, ay - 12, TH_TXT2);
    else   fillTriangle(ax - 8, ay + 3, ax + 9, ay + 3, ax + 1, ay + 12, TH_TXT2);
    drawTextClip(X + 44, y + 8, x.name, 2, TH_TXT, X + w - 110);
    char line[96]; fclXferLine(x.phase, x.type, x.done, x.size, x.bytesPerSec, x.retryInMs, x.error, line, sizeof(line));
    uint16_t lc = x.phase == FCX_FAILED ? TH_DANGER : x.phase == FCX_DONE ? TH_OK : TH_TXT2;
    drawTextClip(X + 44, y + 34, line, 1, lc, X + w - 12);
    int pw = w - 56, pv = x.size ? (int)((uint64_t)pw * (x.done > x.size ? x.size : x.done) / x.size) : (x.phase == FCX_DONE ? pw : 0);
    fillRoundRect(X + 44, y + CKX_RH - 22, pw, 5, 2, TH_TRACK);
    if(pv > 0) fillRoundRect(X + 44, y + CKX_RH - 22, pv, 5, 2, x.phase == FCX_FAILED ? TH_DANGER : wallAccent());
    const char* b = (x.phase == FCX_FAILED) ? "Reintentar" : (x.phase == FCX_DONE || x.phase == FCX_CANCELLED) ? NULL : "Cancelar";
    if(b) drawTextR(X + w - 12, y + 10, b, 1, x.phase == FCX_FAILED ? TH_PRIM : TH_DANGER);
    if(x.phase == FCX_DONE || x.phase == FCX_FAILED || x.phase == FCX_CANCELLED) anyDone = true;
  }
  if(anyDone){
    int y = by + bh - 62;
    fillRoundRect(bx + bw / 2 - 120, y, 240, 48, 16, TH_SURF2);
    drawTextC(bx + bw / 2, y + 14, "Quitar terminadas", 2, TH_TXT);
  }
}

static void ckRender(){
  if(!ckHost) return;
  ckMarkVisible();
  ckDrawMs = millis();
  if(ckXfersOn) ckRenderXfers(); else ckRenderList();
  if(mmOn) mmDraw(1.0f);
  if(mmDlgOn) mmDlgDraw();
  if(fkAskOn) fkAskDraw();
  int bx, by, bw, bh; ckBox(bx, by, bw, bh);
  flxFlush(by, by + bh - 1);
}

// #############################################################
// ##  ACCIONES
// #############################################################
static void ckRequest(uint8_t view, const char* folder){
  ckView = view;
  snprintf(ckFolder, sizeof(ckFolder), "%s", folder && strcmp(folder, "root") ? folder : "");
  ckScroll = 0;
  flexCloudRequestList(view, ckFolder[0] ? ckFolder : "root", NULL);
}

static void ckGoUp(){
  FlexCloudListInfo li; flexCloudListInfo(&li);
  // La carpeta de arriba es la penultima de la ruta (o la raiz).
  const char* up = li.nCrumbs >= 2 ? li.crumbs[li.nCrumbs - 2].id : "";
  ckRequest(FCL_VIEW_FOLDER, up);
}

static void ckOpenItem(const FclItem& it){
  // La cuenta dejo de servir mientras la lista seguia a la vista (el repintado
  // llega en el siguiente cuarto de segundo): no se pide nada.
  if(const char* block = ckCloudBlock()){ sysNotify(it.name, block); return; }
  const char* why = NULL;
  switch(fclOpenAction(&it, &why)){
    case FCL_OPEN_FOLDER: ckRequest(FCL_VIEW_FOLDER, it.id); ckRender(); return;
    case FCL_OPEN_PHOTO: {
      ckViewOp = flexCloudFetchForView(&it);
      snprintf(ckViewName, sizeof(ckViewName), "%s", it.name);
      if(ckViewOp) sysNotify(it.name, "Abriendo desde Flex Cloud...");
      return;
    }
    case FCL_OPEN_STREAM: {
      // Se reproduce por rangos: el visor lee de la arena de bloques.
      gMediaCloudItem = it;
      char p[FLEXMED_PATH_MAX];
      char nm[48]; fclLocalName(it.name, nm, sizeof(nm));
      snprintf(p, sizeof(p), MEDIA_CLOUD_PREFIX "%s/%s", it.id, nm);
      if(ckHost && ckHost->openLocal) ckHost->openLocal(p);
      return;
    }
    default:
      if(why) sysNotify(it.name, why);
      ckMenuItem = it; ckMenuForItem = true;
      {
        uint8_t a[4] = { MA_CL_DOWNLOAD, MA_INFO, MA_RENAME, MA_TRASH };
        int bx, by, bw, bh; ckBox(bx, by, bw, bh);
        mmOpen(bx + bw / 2, by + bh / 3, a, 4);
      }
      return;
  }
}

static void ckInfo(const FclItem& it){
  char sz[24], dt[16], txt[300];
  fclFmtBytes(it.size, sz, sizeof(sz));
  fclFmtDate(it.updatedAt, dt, sizeof(dt));
  if(it.isFolder) snprintf(txt, sizeof(txt), "%s\nCarpeta de Flex Cloud%s%s", it.name, dt[0] ? "\nModificada: " : "", dt);
  else {
    char dim[32] = "";
    if(it.width && it.height) snprintf(dim, sizeof(dim), " \xC2\xB7 %ux%u", (unsigned)it.width, (unsigned)it.height);
    snprintf(txt, sizeof(txt), "%s\n%s%s \xC2\xB7 %s\n%s%s\nSHA-256: %.16s...\n%s",
             it.name, sz, dim, it.mime[0] ? it.mime : "archivo",
             dt[0] ? "Modificado: " : "", dt, it.sha256,
             it.fromDevice ? "Subido desde un Flex OS Ultra" : "Subido desde la web");
  }
  mmDlgOpen("Detalles", txt, "Cerrar", "", false);
}

static void ckItemMenu(const FclItem& it, int ax, int ay){
  if(const char* block = ckCloudBlock()){ sysNotify(it.name, block); return; }
  ckMenuItem = it; ckMenuForItem = true;
  uint8_t a[6]; int n = 0;
  if(ckView == FCL_VIEW_TRASH){ a[n++] = MA_CL_RESTORE; a[n++] = MA_DELETE; }
  else {
    if(!it.isFolder) a[n++] = MA_CL_DOWNLOAD;
    a[n++] = MA_RENAME;
    if(!it.isFolder) a[n++] = MA_INFO;
    a[n++] = MA_TRASH;
  }
  mmOpen(ax, ay, a, n);
}

static void ckAppMenu(int ax, int ay){
  ckMenuForItem = false;
  uint8_t a[5]; int n = 0;
  // Sin una cuenta que sirva solo queda "Transferencias" (para ver y cancelar lo
  // que esperaba): lo demas necesita a Flex Account y no se ofrece.
  bool usable = ckCloudBlock() == NULL;
  if(usable && ckHost && ckHost->mode == CKM_BROWSE){
    if(ckView == FCL_VIEW_TRASH) a[n++] = MA_CL_MYFILES;
    else { a[n++] = MA_CL_NEWFOLDER; a[n++] = MA_CL_TRASHVIEW; }
  }
  a[n++] = MA_CL_XFERS;
  if(usable) a[n++] = MA_CL_REFRESH;
  mmOpen(ax, ay, a, n);
}

static void ckMenuAction(int act){
  FclItem& it = ckMenuItem;
  // El menu pudo abrirse con la cuenta buena y perderla antes de elegir: todo lo
  // que habla con Flex Cloud se corta aqui (Transferencias y Detalles no).
  if(act != MA_CL_XFERS && act != MA_INFO){
    if(const char* block = ckCloudBlock()){ sysNotify("Flex Cloud", block); ckRender(); return; }
  }
  switch(act){
    case MA_CL_DOWNLOAD:
      if(ckMenuForItem){
        // A la biblioteca si es un medio que el P4 cataloga; si no, a /Descargas.
        uint8_t fl = flexMlKindFromExt(it.name) != FML_K_NONE ? FCL_JF_TO_LIBRARY : 0;
        uint32_t id = flexCloudDownload(&it, fl);
        if(id) sysNotify(it.name, "Descarga en cola (Transferencias)");
        else ckNotifyFail(it.name, "No se pudo poner en cola");
      }
      break;
    case MA_INFO:   if(ckMenuForItem){ ckInfo(it); return; } break;
    case MA_RENAME:
      if(ckMenuForItem){ ckName = CKN_RENAME; fkNameOpen("Renombrar", it.name); return; }
      break;
    case MA_TRASH:
      if(ckMenuForItem){ if(flexCloudTrash(&it)) sysNotify(it.name, "A la papelera de Flex Cloud"); }
      break;
    case MA_CL_RESTORE:
      if(ckMenuForItem) flexCloudRestore(&it);
      break;
    case MA_DELETE:
      if(ckMenuForItem){ ckAsk = CKA_DELETE; ckRender(); fkAskOpen("\xC2\xBF" "Borrar para siempre?", it.name); return; }
      break;
    case MA_CL_NEWFOLDER: ckName = CKN_MKDIR; fkNameOpen("Nueva carpeta", ""); return;
    case MA_CL_TRASHVIEW: ckRequest(FCL_VIEW_TRASH, NULL); break;
    case MA_CL_MYFILES:   ckRequest(FCL_VIEW_FOLDER, NULL); break;
    case MA_CL_XFERS:     ckXfersOn = true; break;
    case MA_CL_REFRESH:   flexCloudRefresh(); break;
  }
  ckRender();
}

// #############################################################
// ##  API PARA LAS APPS
// #############################################################
// Entra en la vista de la nube de `h` (mantiene la carpeta si ya era suya).
static void ckBind(const CkHost* h){
  bool same = ckHost == h;
  ckHost = h;
  ckDragging = false; ckLongFired = false;
  if(!same){
    ckXfersOn = false;
    uint8_t v = h->mode == CKM_MEDIA ? FCL_VIEW_MEDIA : h->mode == CKM_VIDEO ? FCL_VIEW_VIDEO : FCL_VIEW_FOLDER;
    ckRequest(v, NULL);
  } else {
    // Solo si nunca se pidio. Un ERROR se queda en pantalla con su motivo:
    // volver a pedir en cada repintado seria un bucle (sin red, 4 veces por
    // segundo). Se reintenta con "Reintentar" o al volver la conexion.
    FlexCloudListInfo li; flexCloudListInfo(&li);
    if(li.state == FCL_LIST_IDLE) ckRequest(ckView, ckFolder);
  }
  ckMarkVisible();
  flexCloudSetActive(true);
}
// La app deja de ensenar la nube (otra pestana, segundo plano, se cierra).
static void ckUnbind(const CkHost* h){
  if(ckHost != h) return;
  if(mmOn) mmClose();
  mmDlgOn = false;
  if(ckName != CKN_NONE){ fkNameOn = false; ckName = CKN_NONE; }
  if(ckAsk != CKA_NONE){ fkAskOn = false; ckAsk = CKA_NONE; }
  ckHost = NULL; ckXfersOn = false; ckViewOp = 0;
  ckVisibleMs = 0;                                    // ya no se ve: la cuota deja de refrescarse
  flexCloudSetActive(false);
  if(ckRows){ mediaFree(ckRows); ckRows = NULL; }
  ckRowsN = 0;
}
// ATRAS dentro de la nube: capas, transferencias, carpeta de arriba. true = se uso.
static bool ckBack(){
  if(!ckHost) return false;
  if(mmOn){ mmClose(); ckRender(); return true; }
  if(mmDlgOn){ mmDlgOn = false; ckRender(); return true; }
  if(fkNameOn){ fkNameOn = false; ckName = CKN_NONE; if(ckHost->redraw) ckHost->redraw(); return true; }
  if(fkAskOn){ fkAskOn = false; ckAsk = CKA_NONE; ckRender(); return true; }
  if(ckXfersOn){ ckXfersOn = false; ckRender(); return true; }
  if(ckView == FCL_VIEW_TRASH && ckHost->mode == CKM_BROWSE){ ckRequest(FCL_VIEW_FOLDER, NULL); ckRender(); return true; }
  if(ckHasUp()){ ckGoUp(); ckRender(); return true; }
  return false;
}

// Toque sobre la lista -> indice del elemento (o -1). Contra lo PINTADO.
static int ckHitIndex(int tx, int ty){
  FlexCloudListInfo li; flexCloudListInfo(&li);
  if(li.gen != ckShownGen) return -1;                         // la lista cambio debajo: nada
  int bx, by, bw, bh; ckBox(bx, by, bw, bh);
  if(ty < by + ckHeadH() || ty > by + bh) return -1;
  for(int k = 0; k < ckRowsN; k++){
    int x, y, w, h; ckCellRect(ckRowsFrom + k, x, y, w, h);
    if(tx >= x && tx <= x + w && ty >= y && ty <= y + h) return k;
  }
  return -1;
}

// Una vuelta: capas, repintados por cambios y tacto. La app la llama desde su
// tick mientras ensena la nube (despues de cerrar SUS capas).
static void ckTick(){
  if(!ckHost) return;
  ckMarkVisible();
  // ---- capas ----
  if(fkNameOn){
    int r = fkNameTick();
    if(r == 1){
      if(ckName == CKN_MKDIR){ if(!flexCloudMkdir(ckFolder[0] ? ckFolder : "root", fkNameBuf)) ckNotifyFail("Flex Cloud", "No se pudo crear la carpeta"); }
      else if(ckName == CKN_RENAME){
        // Se conserva la extension si el nuevo nombre no trae una.
        char nn[FCL_NAME_MAX];
        const char* dot = strrchr(ckMenuItem.name, '.');
        if(!ckMenuItem.isFolder && dot && !strchr(fkNameBuf, '.')) snprintf(nn, sizeof(nn), "%s%s", fkNameBuf, dot);
        else snprintf(nn, sizeof(nn), "%s", fkNameBuf);
        if(!flexCloudRename(&ckMenuItem, nn)) ckNotifyFail(ckMenuItem.name, "No se pudo renombrar");
      }
    }
    if(r != 0){ ckName = CKN_NONE; if(ckHost->redraw) ckHost->redraw(); }
    return;
  }
  if(fkAskOn){
    int r = fkAskTick();
    if(r == 1 && ckAsk == CKA_DELETE && !flexCloudDeleteForever(&ckMenuItem)) ckNotifyFail(ckMenuItem.name, "No se pudo borrar");
    if(r != 0){ ckAsk = CKA_NONE; ckRender(); }
    return;
  }
  if(mmDlgOn){ if(mmDlgTick() != 0) ckRender(); return; }
  if(mmOn){
    mmAnimTick();
    if(T.tap){
      int a = mmHit(T.x, T.y);
      if(a == 0) return;
      mmClose();
      if(a > 0) ckMenuAction(a); else ckRender();
    }
    return;
  }

  // ---- repintados por cambios (nunca con el dedo apoyado) ----
  if(!mkTouchBusy() && millis() - ckDrawMs >= CK_REDRAW_MS){
    FlexCloudStatus st; flexCloudStatus(&st);
    FlexCloudListInfo li; flexCloudListInfo(&li);
    // Vuelve la conexion con la lista en error: se pide otra vez, UNA vez.
    if(st.net != ckLastNet){
      bool back = st.net == FCN_ONLINE && ckLastNet != 0xFF && ckLastNet != FCN_ONLINE;
      ckLastNet = st.net;
      if(back && li.state == FCL_LIST_ERROR){ flexCloudRefresh(); }
    }
    bool changed = st.gen != ckSeenStatus || li.gen != ckSeenList || flexCloudThumbGen() != ckSeenThumb ||
                   (st.xferGen != ckSeenXfer && (ckXfersOn || st.activeXfers));
    if(changed){ ckRender(); return; }
  }

  int bx, by, bw, bh; ckBox(bx, by, bw, bh);
  // ---- transferencias ----
  if(ckXfersOn){
    if(!T.tap) return;
    if(T.x < bx + 60 && T.y < by + 40){ ckXfersOn = false; ckRender(); return; }
    if(T.y >= by + bh - 62 && T.y <= by + bh - 14 && abs(T.x - (bx + bw / 2)) < 120){ flexCloudClearFinished(); ckRender(); return; }
    for(int i = 0; i < ckXferN; i++){
      int y = by + CKX_TOP + i * CKX_RH;
      if(T.y >= y && T.y < y + CKX_RH - 8 && T.x > bx + bw - 130){
        FlexCloudXfer xs[FLEX_CLOUD_XFERS]; int n = flexCloudXfers(xs, FLEX_CLOUD_XFERS);
        for(int k = 0; k < n; k++) if(xs[k].id == ckXferIds[i]){
          if(xs[k].phase == FCX_FAILED){ if(!flexCloudRetry(xs[k].id)) ckNotifyFail(xs[k].name, "No se pudo reintentar"); }
          else if(xs[k].phase != FCX_DONE && xs[k].phase != FCX_CANCELLED) flexCloudCancel(xs[k].id);
        }
        ckRender(); return;
      }
    }
    return;
  }

  // ---- desplazamiento ----
  int maxS = ckMaxScroll();
  if(T.pressed){ ckDragY0 = T.y; ckDragS0 = ckScroll; ckDragging = false; }
  if(T.down && maxS > 0){
    if(!ckDragging && abs(T.y - ckDragY0) > 10) ckDragging = true;
    if(ckDragging){
      int ns = ckDragS0 + (ckDragY0 - T.y);
      if(ns < 0) ns = 0; if(ns > maxS) ns = maxS;
      if(ns != ckScroll){ ckScroll = ns; ckRender(); }
      ckLongFired = true;
      return;
    }
  }
  if(T.released && ckDragging){ ckDragging = false; return; }

  // ---- pulsacion larga: menu del elemento ----
  if(T.down && !ckLongFired && (millis() - T.downMs) > 550 && abs(T.x - T.startX) < 14 && abs(T.y - T.startY) < 14){
    ckLongFired = true;
    int k = ckHitIndex(T.startX, T.startY);
    if(k >= 0){ ckItemMenu(ckRows[k], T.x, T.y + 10); return; }
  }
  if(!T.down) ckLongFired = false;
  if(!T.tap) return;

  // ---- botones de los estados vacios ----
  if(ckEmptyBtnY >= 0 && T.y >= ckEmptyBtnY && T.y <= ckEmptyBtnY + 48 && abs(T.x - (bx + bw / 2)) < 110){
    if(ckEmptyBtnAct == 1){ const CkHost* h = ckHost; ckUnbind(h); if(gState != ST_APP) gState = ST_APP; settingsJumpAccount(); return; }
    if(ckEmptyBtnAct == 2){ flexCloudRefresh(); ckRender(); return; }
  }
  // ---- chip "N en curso" de la tarjeta de estado: abre transferencias ----
  if(T.y >= by + 4 && T.y <= by + 30 && T.x > bx + bw - 140){
    FlexCloudStatus st; flexCloudStatus(&st);
    if(st.activeXfers){ ckXfersOn = true; ckRender(); return; }
  }
  // ---- "subir" ----
  if(ckHasUp()){
    int y = by + ckHeadH() - ckScroll;
    if(T.y >= y && T.y < y + CK_ROW_H - 8 && T.y >= by + ckHeadH()){ ckGoUp(); ckRender(); return; }
  }
  int k = ckHitIndex(T.x, T.y);
  if(k >= 0) ckOpenItem(ckRows[k]);
}

// Suelta lo que se puede rehacer (las miniaturas y las filas copiadas).
static size_t ckShed(){
  size_t n = 0;
  if(ckRows && !ckHost){ mediaFree(ckRows); ckRows = NULL; n += sizeof(FclItem) * CK_ROWS_MAX; }
  return n + flexCloudShed();
}

// #############################################################
// ##  AVISOS DE LA NUBE (cada vuelta de loop, tambien sin la nube a la vista)
// #############################################################
// Coloca una descarga YA VERIFICADA: a la biblioteca (Galeria/Musica) si es un
// medio que el P4 cataloga, si no a /Descargas. El temporal se MUEVE (no se copia).
static void ckPlaceDownload(const FlexCloudEvent& e){
  if(!flexFsExists(e.localPath)) return;                       // ya colocado (aviso repetido tras un reinicio)
  char why[64] = "";
  int kind = flexMlKindFromExt(e.name);
  if((e.flags & FCL_JF_TO_LIBRARY) && kind != FML_K_NONE && gMlOk){
    if(mlAddFile(e.localPath, kind, e.name, FML_O_CLOUD, 0, why, sizeof(why))){
      sysNotify(e.name, kind == FML_K_AUDIO ? "Descargado en M\xC3\xBAsica" : "Descargado en la Galer\xC3\xAD" "a");
      return;
    }
  }
  char local[FLEXFS_NAME_MAX], stem[FLEXFS_NAME_MAX], dst[FLEXFS_PATH_MAX];
  fclLocalName(e.name, local, sizeof(local));
  flexFsMkdir("/Descargas");
  snprintf(dst, sizeof(dst), "/Descargas/%s", local);
  if(flexFsExists(dst)){
    // Nunca se pisa nada: "nombre 2.ext", "nombre 3.ext"...
    const char* dot = strrchr(local, '.');
    flexFsStem(local, stem, sizeof(stem));
    if(!flexFsNewName("/Descargas", stem, dot ? dot : "", dst, sizeof(dst))) dst[0] = 0;
  }
  if(dst[0] && flexFsMove(e.localPath, dst)) sysNotify(e.name, "Descargado en Archivos > Descargas");
  else sysNotify(e.name, why[0] ? why : "No se pudo guardar la descarga");
}

// "Subir y liberar espacio": el original se borra AQUI y solo si el aviso lo
// permite (la nube lo confirmo con la misma huella y el gestor lo releyo).
// Ademas: sigue existiendo, mide lo mismo y es el mismo elemento.
static void ckFreeLocal(const FlexCloudEvent& e){
  char sz[24]; fclFmtBytes(e.size, sz, sizeof(sz));
  bool freed = false;
  if(flexFsExists(e.localPath) && flexFsSize(e.localPath) == e.size){
    FlexMlRec r;
    if(e.mlId){
      if(mlGet(e.mlId, &r) && !strcmp(r.path, e.localPath) && !(r.flags & FML_R_LOCKED) && r.size == e.size) freed = mlDelete(e.mlId);
    } else freed = flexFsDelete(e.localPath);
  }
  char msg[96];
  if(freed) snprintf(msg, sizeof(msg), "En Flex Cloud. Liberados %s del dispositivo", sz);
  else snprintf(msg, sizeof(msg), "En Flex Cloud. El original se conserva");
  sysNotify(e.name, msg);
}

// Aviso UNICO cuando la cuenta deja de servir (desvinculada desde la web,
// revocada o caducada), con o sin la nube a la vista. Una subida larga o un video
// siguen su camino sin que nadie mire la tarjeta de estado: sin esto el usuario
// solo se enteraba al abrir Flex Cloud o Ajustes. Se mira como mucho 2 veces por
// segundo y avisa en el FLANCO (de "sirve" a "no sirve"), no en cada vuelta.
static FlexAccountLink ckAcctLink = (FlexAccountLink)255;
static uint32_t        ckAcctMs = 0;
static void ckAccountNoticeTick(){
  uint32_t now = millis();
  if(ckAcctMs && (uint32_t)(now - ckAcctMs) < 500u) return;
  ckAcctMs = now | 1u;
  FlexAccountLink l = flexAccountLinkState();
  if(l == ckAcctLink) return;
  bool wasLost = ckAcctLink == FLEX_LINK_AUTH_REQUIRED || ckAcctLink == FLEX_LINK_TOKEN_EXPIRED;
  ckAcctLink = l;
  if(wasLost || !flexAccountLinked()) return;
  if(l == FLEX_LINK_AUTH_REQUIRED)      sysNotify("Flex Account: sesi\xC3\xB3n perdida", CK_MSG_RELINK);
  else if(l == FLEX_LINK_TOKEN_EXPIRED) sysNotify("Flex Account: sesi\xC3\xB3n caduc\xC3\xB3", CK_MSG_RELINK);
}

static void cloudUiTick(){
  ckAccountNoticeTick();
  // La cuota se refresca solo mientras alguien ensena la nube.
  flexCloudSetActive(ckVisibleMs && millis() - ckVisibleMs < 2000u);
  FlexCloudEvent e;
  int budget = 3;                                            // pocos por vuelta: el bucle no se detiene
  while(budget-- > 0 && flexCloudPollEvent(&e)){
    switch(e.kind){
      case FCE_UPLOAD_DONE:
        if(e.flags & FCL_JF_FREE_LOCAL) ckFreeLocal(e);
        else sysNotify(e.name, e.text[0] ? e.text : "Subido a Flex Cloud");
        break;
      case FCE_UPLOAD_FAILED:   sysNotify(e.name[0] ? e.name : "Flex Cloud", e.text); break;
      case FCE_DOWNLOAD_DONE:   ckPlaceDownload(e); break;
      case FCE_DOWNLOAD_FAILED: sysNotify(e.name[0] ? e.name : "Flex Cloud", e.text); break;
      case FCE_OP_DONE:         if(!e.ok) sysNotify("Flex Cloud", e.text); break;
      case FCE_VIEW_READY:
        // Solo si quien la pidio sigue delante (si no, se queda para la proxima vez).
        if(e.opId == ckViewOp && ckHost && ckHost->openLocal && millis() - ckVisibleMs < 1000u){ ckViewOp = 0; ckHost->openLocal(e.localPath); }
        break;
      case FCE_VIEW_FAILED:
        if(e.opId == ckViewOp){ ckViewOp = 0; sysNotify(ckViewName[0] ? ckViewName : "Flex Cloud", e.text); }
        break;
    }
  }
}

// Subir un elemento de la biblioteca (Galeria / Multimedia). Lo protegido no
// se sube nunca (el menu ni lo ofrece).
static void ckUploadMl(uint32_t id, bool freeLocal){
  FlexMlRec r;
  if(!mlGet(id, &r) || (r.flags & FML_R_LOCKED)) return;
  char nm[FML_NAME_MAX]; flexMlDisplayName(&r, nm, sizeof(nm));
  if(const char* block = ckCloudBlock()){ sysNotify("Flex Cloud", block); return; }
  // El nombre visible conserva la extension real del archivo.
  const char* ext = strrchr(r.path, '.');
  char name[FCL_NAME_MAX];
  if(ext && !strrchr(nm, '.')) snprintf(name, sizeof(name), "%s%s", nm, ext); else snprintf(name, sizeof(name), "%s", nm);
  uint32_t job = flexCloudUpload(r.path, name, "root", id, freeLocal ? FCL_JF_FREE_LOCAL : 0);
  if(!job){ ckNotifyFail(nm, "No se pudo poner en cola (demasiadas transferencias)"); return; }
  sysNotify(nm, freeLocal ? "Subiendo. Se borrar\xC3\xA1 del P4 cuando Flex Cloud confirme la copia"
                          : "Subiendo a Flex Cloud (Transferencias)");
}

// #############################################################
// ##  "SUBIR A FLEX CLOUD" desde la Galeria o Multimedia
// ##  ------------------------------------------------------
// ##  Una sola entrada en el menu del elemento y aqui la eleccion, con
// ##  las tres salidas claras: subir y conservar, subir y liberar espacio
// ##  (se borra del P4 solo cuando la nube confirma la copia) o nada.
// ##  Se pinta UNA vez (vidrio sin apilar); el tick solo mira toques.
// #############################################################
static bool     ckUpAskOn = false;
static uint32_t ckUpAskId = 0;
static void ckUpAskGeom(int& x, int& y, int& w, int& h){ w = SCR_W - 64; h = 330; x = 32; y = (SCR_H - h) / 2; }
static void ckUpAskDraw(){
  int x, y, w, h; ckUpAskGeom(x, y, w, h);
  setBuf(fb);
  uiSurface(x, y, w, h, 24, UIS_ELEVATED);
  ckCloudGlyph(x + w / 2, y + 34, 18, wallAccent());
  drawTextC(SCR_W / 2, y + 62, "Subir a Flex Cloud", 2, uiSurfOn(UIS_ELEVATED));
  FlexMlRec r; char nm[FML_NAME_MAX] = "", sz[24] = "", line[96];
  if(mlGet(ckUpAskId, &r)){ flexMlDisplayName(&r, nm, sizeof(nm)); fclFmtBytes(r.size, sz, sizeof(sz)); }
  snprintf(line, sizeof(line), "%s \xC2\xB7 %s", nm, sz);
  drawTextC(SCR_W / 2, y + 94, line, 1, TH_TXT2);
  drawTextC(SCR_W / 2, y + 112, "Se sube el ORIGINAL, sin perder calidad.", 1, TH_TXT2);
  int bx = x + 16, bw = w - 32, by = y + 136;
  fillRoundRect(bx, by, bw, 54, 16, wallAccent());
  drawTextC(SCR_W / 2, by + 17, "Subir y conservar", 2, TH_ONACC);
  by += 64;
  fillRoundRect(bx, by, bw, 64, 16, TH_SURF);
  drawTextC(SCR_W / 2, by + 10, "Subir y liberar espacio", 2, TH_TXT);
  drawTextC(SCR_W / 2, by + 40, "Se borra del P4 cuando la nube confirme la copia", 1, TH_TXT2);
  by += 74;
  drawTextC(SCR_W / 2, by + 12, "Cancelar", 2, TH_TXT2);
  flxFlush(y - 2, y + h + 2);
}
static void ckUpAskOpen(uint32_t mlId){
  if(const char* block = ckCloudBlock()){ sysNotify("Flex Cloud", block); return; }
  ckUpAskId = mlId; ckUpAskOn = true;
  ckUpAskDraw();
}
// true mientras siga abierta (el llamante no hace nada mas este cuadro).
static bool ckUpAskTick(void (*redraw)()){
  if(!ckUpAskOn) return false;
  if(!T.tap) return true;
  int x, y, w, h; ckUpAskGeom(x, y, w, h);
  int by = y + 136;
  ckUpAskOn = false;
  if(T.x >= x + 16 && T.x <= x + w - 16){
    if(T.y >= by && T.y <= by + 54) ckUploadMl(ckUpAskId, false);
    else if(T.y >= by + 64 && T.y <= by + 128) ckUploadMl(ckUpAskId, true);
  }
  if(redraw) redraw();
  return true;
}
