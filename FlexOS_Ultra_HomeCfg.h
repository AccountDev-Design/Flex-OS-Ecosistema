// #############################################################
// ##  FLEX OS ULTRA  ·  MODO PERSONALIZACION DEL INICIO  (ST_HOMECFG)
// ##  ----------------------------------------------------------
// ##  Administrador de paginas, fondo, tema, paleta y widgets, con su
// ##  propio dibujo, sus toques y su transicion. Incluye el GESTO DE
// ##  PELLIZCO con dos dedos que es una de las dos formas de abrirlo.
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
#include "FlexOS_Ultra_Widgets.h"   // eslabon anterior de la cadena

// #############################################################
// ##  MODO PERSONALIZACION DEL INICIO  (ST_HOMECFG)
// ##  ------------------------------------------------------
// ##  Se abre de DOS formas, las dos reales:
// ##    1) pulsacion larga de 650 ms sobre un hueco VERDADERAMENTE
// ##       vacio del escritorio, con tolerancia de 12 px;
// ##    2) pellizco de DOS DEDOS hacia adentro, con la lectura
// ##       multipunto real del GT911 (gtPollMulti).
// ##
// ##  Contiene: administrador de paginas (crear, borrar, reordenar
// ##  y elegir principal), fondo de pantalla y estilo con paleta,
// ##  temas integrados, selector de widgets y ajustes de inicio.
// ##
// ##  DIBUJO: compone en bbuf y publica con present(), como todo el
// ##  resto del sistema. No crea tareas, no escribe al panel, no
// ##  usa delay() y no repinta si nada ha cambiado (hcDirty). El
// ##  fondo atenuado (blurBg) y la miniatura del fondo (hcThumb) se
// ##  generan UNA vez al entrar; por cuadro solo hay copias de
// ##  memoria y unos pocos iconos.
// #############################################################
enum { HCV_PAGES = 0, HCV_WALL, HCV_THEME, HCV_WIDGETS, HCV_SETTINGS, HCV_PICK };
enum { HCM_NONE = 0, HCM_DELPAGE, HCM_RESET, HCM_INFO };

#define HC_CARD_W   216
#define HC_CARD_H   360
#define HC_CARD_X   ((SCR_W - HC_CARD_W) / 2)
#define HC_CARD_Y   150
#define HC_STEP     (HC_CARD_W + 24)
#define HC_DOTS_Y   (HC_CARD_Y + HC_CARD_H + 42)
#define HC_BAR_Y    590
#define HC_BAR_H    112
#define HC_LIST_TOP 128
#define HC_LIST_BOT 700
#define HC_ANIM_MS  180
#define HC_PV_W     100
#define HC_PV_H     166
#define HC_WSHEET_Y 506

static bool      hcActive   = false;
static uint8_t   hcView     = HCV_PAGES;
static int       hcPageView = 0;         // pagina centrada (gHomePageN = tarjeta "+")
static float     hcSlide    = 0.0f;
static bool      hcDragging = false, hcDragMoved = false;
static int       hcDragX0   = 0;
static float     hcDragBase = 0.0f;
static int       hcReorder  = -1;
static uint8_t   hcAnim     = 0;         // 0 quieto · 1 entrando · 2 saliendo
static uint32_t  hcAnimT0   = 0;
static bool      hcDirty    = true;
static bool      hcIgnore   = false;
static uint32_t  hcFrameMs  = 0;
static uint8_t   hcModal    = HCM_NONE;
static char      hcMsg[72]  = "";
static int       hcScroll   = 0, hcScrollMax = 0;
static int       hcWallSel  = -1, hcThemeSel = -1, hcWgSel = -1;
static uint16_t* hcThumb    = NULL;      // fondo pre-escalado de la miniatura (216x360)
static uint16_t* hcWallPrev = NULL;      // cache de miniaturas del selector de fondos
static bool      hcWallPrevOk = false;
// Selector de imagen del almacenamiento
#define HC_PICK_MAX 24
static char      hcPickPath[HC_PICK_MAX][80];
static int       hcPickN = 0, hcPickSel = -1;

static void hcRender();
static void hcEnter();
static void hcBeginExit();
static void hcClose(bool save);

// ---- Utilidades de dibujo del modo -----------------------------------------
static void hcPanel(int x, int y, int w, int h, int rad, uint8_t alpha){
  fillRoundRectA(x, y, w, h, rad, TH_SURF, alpha);
  drawRoundRect(x, y, w, h, rad, TH_BORDER);
}
static bool hcHit(int px, int py, int x, int y, int w, int h){
  return px >= x && px < x + w && py >= y && py < y + h;
}
static void hcBtn(int x, int y, int w, int h, const char* label, bool primary){
  uint16_t bg = primary ? wallAccent() : TH_SURF2;
  fillRoundRect(x, y, w, h, h / 2, bg);
  drawTextC(x + w / 2, y + (h - 20) / 2, label, 2, onColor(bg));
}
// Fila de lista. kind: 0 = valor + chevron · 1 = interruptor · 2 = accion.
static void hcRow(int y, int h, const char* title, const char* value, int kind, bool on){
  hcPanel(16, y, SCR_W - 32, h, 16, 235);
  drawTextClip(32, y + (h - 20) / 2, title, 2, TH_TXT, SCR_W - 120);
  if(kind == 1){
    int sw = 46, sx = SCR_W - 32 - sw, sy = y + (h - 26) / 2;
    fillRoundRect(sx, sy, sw, 26, 13, on ? wallAccent() : TH_TRACK);
    fillCircle(on ? sx + sw - 13 : sx + 13, sy + 13, 10, TC(255,255,255));
  } else if(value && *value){
    drawTextR(SCR_W - 54, y + (h - 18) / 2, value, 2, TH_TXT2);
    if(kind == 0){
      int cx = SCR_W - 32, cy = y + h / 2;
      strokeSegAA(cx - 6.0f, cy - 6.0f, (float)cx - 1, (float)cy, 1.8f, TH_TXT2);
      strokeSegAA((float)cx - 1, (float)cy, cx - 6.0f, cy + 6.0f, 1.8f, TH_TXT2);
    }
  }
}
// ---- Iconos de la barra inferior (primitivas, nada importado) --------------
static void hcIcoWall(int cx, int cy, uint16_t c){
  drawRoundRect(cx - 15, cy - 12, 30, 24, 5, c);
  fillCircle(cx - 7, cy - 5, 3, c);
  fillTriangle(cx - 12, cy + 10, cx - 1, cy - 3, cx + 9, cy + 10, c);
  fillTriangle(cx + 1, cy + 10, cx + 8, cy + 1, cx + 14, cy + 10, c);
}
static void hcIcoTheme(int cx, int cy, uint16_t c){
  drawCircle(cx, cy, 13, c); drawCircle(cx, cy, 12, c);
  fillCircle(cx - 5, cy - 5, 2, c); fillCircle(cx + 4, cy - 6, 2, c);
  fillCircle(cx + 7, cy + 2, 2, c); fillCircle(cx - 6, cy + 4, 2, c);
  strokeSegAA(cx + 2.0f, cy + 6.0f, cx + 10.0f, cy + 13.0f, 2.0f, c);
}
static void hcIcoWidgets(int cx, int cy, uint16_t c){
  drawRoundRect(cx - 13, cy - 13, 12, 12, 3, c);
  drawRoundRect(cx + 1, cy - 13, 12, 12, 3, c);
  drawRoundRect(cx - 13, cy + 1, 12, 12, 3, c);
  fillRoundRect(cx + 1, cy + 1, 12, 12, 3, c);
}
static void hcIcoGear(int cx, int cy, uint16_t c){
  drawCircle(cx, cy, 12, c); drawCircle(cx, cy, 11, c); drawCircle(cx, cy, 5, c);
  for(int i = 0; i < 8; i++){
    float a = i * 0.7853982f;
    strokeSegAA(cx + sinf(a) * 11.0f, cy - cosf(a) * 11.0f,
                cx + sinf(a) * 15.0f, cy - cosf(a) * 15.0f, 2.0f, c);
  }
}
// Casita: tejado + cuerpo. Rellena = pagina principal; solo el contorno =
// pagina normal (tocarla la convierte en principal).
static void hcIcoHome(int cx, int cy, int s, uint16_t c, bool filled){
  int h = s / 2, mid = cy - s / 6, bh = cy + h - mid;
  if(bh < 3) bh = 3;
  fillTriangle(cx, cy - h, cx - h - 2, mid, cx + h + 2, mid, c);
  if(filled) fillRect(cx - h + 2, mid, s - 4, bh, c);
  else { drawRect(cx - h + 2, mid, s - 4, bh, c); drawRect(cx - h + 3, mid, s - 6, bh, c); }
}
static void hcIcoTrash(int cx, int cy, uint16_t c){
  fillRect(cx - 8, cy - 9, 16, 2, c);
  fillRect(cx - 3, cy - 12, 6, 2, c);
  drawRect(cx - 6, cy - 6, 12, 15, c);
  vLine(cx - 2, cy - 3, 9, c); vLine(cx + 2, cy - 3, 9, c);
}
static void hcIcoPlus(int cx, int cy, int s, uint16_t c){
  fillRoundRect(cx - s / 2, cy - 3, s, 6, 3, c);
  fillRoundRect(cx - 3, cy - s / 2, 6, s, 3, c);
}

// ---- Buffers transitorios y caches -----------------------------------------
static void hcFreeBuffers(){
  if(hcThumb){ heap_caps_free(hcThumb); hcThumb = NULL; }
  if(hcWallPrev){ heap_caps_free(hcWallPrev); hcWallPrev = NULL; }
  hcWallPrevOk = false;
}
// Regenera blurBg (fondo atenuado compartido con el desbloqueo y Recientes) con
// el fondo ACTUAL. Sin esto, cambiar de fondo dejaria el desenfoque antiguo para
// toda la sesion, porque ensureBlurBg() solo lo compone una vez.
static void hcRebuildBlur(){
  ensureBlurBg();
  if(!blurBg) return;
  uint16_t* old = gBuf; bool wl = gLand; gLand = false;
  int c0 = gClipX0, c1 = gClipX1, r0 = gClipY0, r1 = gClipY1;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  drawWallpaperRowsId(blurBg, gWallHome, true, 0, SCR_H - 1);
  setBuf(blurBg);
  fillRectA(0, 0, SCR_W, SCR_H, TH_SCRIM, 70);
  setBuf(old); gLand = wl;
  gClipX0 = c0; gClipX1 = c1; gClipY0 = r0; gClipY1 = r1;
}
// Miniatura del fondo y paleta, en UNA sola pasada. bbuf se usa de borrador (se
// recompone entero justo despues), asi que no hace falta un buffer extra a
// tamano completo.
static void hcBuildThumb(){
  if(!bbuf) return;
  if(!hcThumb)
    hcThumb = (uint16_t*)heap_caps_malloc((size_t)HC_CARD_W * HC_CARD_H * 2,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  uint16_t* old = gBuf; bool wl = gLand; gLand = false;
  int c0 = gClipX0, c1 = gClipX1, r0 = gClipY0, r1 = gClipY1;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  drawWallpaperRowsId(bbuf, gWallHome, true, 0, SCR_H - 1);
  wallPaletteBuild(bbuf);                        // muestreo reducido, una sola vez
  if(hcThumb){
    static uint16_t xm[HC_CARD_W];
    for(int x = 0; x < HC_CARD_W; x++) xm[x] = (uint16_t)((x * SCR_W) / HC_CARD_W);
    for(int y = 0; y < HC_CARD_H; y++){
      const uint16_t* srow = bbuf + (size_t)((y * SCR_H) / HC_CARD_H) * SCR_W;
      uint16_t* drow = hcThumb + (size_t)y * HC_CARD_W;
      for(int x = 0; x < HC_CARD_W; x++) drow[x] = srow[xm[x]];
    }
  }
  setBuf(old); gLand = wl;
  gClipX0 = c0; gClipX1 = c1; gClipY0 = r0; gClipY1 = r1;
}
// Miniaturas del selector de fondos: se generan UNA vez al abrir esa vista.
static void hcBuildWallPreviews(){
  if(hcWallPrevOk || !bbuf) return;
  if(!hcWallPrev)
    hcWallPrev = (uint16_t*)heap_caps_malloc((size_t)WALL_N * HC_PV_W * HC_PV_H * 2,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!hcWallPrev) return;
  uint16_t* old = gBuf; bool wl = gLand; gLand = false;
  int c0 = gClipX0, c1 = gClipX1, r0 = gClipY0, r1 = gClipY1;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  static uint16_t xm[HC_PV_W];
  for(int x = 0; x < HC_PV_W; x++) xm[x] = (uint16_t)((x * SCR_W) / HC_PV_W);
  for(int i = 0; i < WALL_N; i++){
    drawWallpaperRowsId(bbuf, i, true, 0, SCR_H - 1);
    uint16_t* dst = hcWallPrev + (size_t)i * HC_PV_W * HC_PV_H;
    for(int y = 0; y < HC_PV_H; y++){
      const uint16_t* srow = bbuf + (size_t)((y * SCR_H) / HC_PV_H) * SCR_W;
      uint16_t* drow = dst + (size_t)y * HC_PV_W;
      for(int x = 0; x < HC_PV_W; x++) drow[x] = srow[xm[x]];
    }
    uiRenderCooperate();                      // ocho fondos seguidos: se cede CPU entre ellos
  }
  setBuf(old); gLand = wl;
  gClipX0 = c0; gClipX1 = c1; gClipY0 = r0; gClipY1 = r1;
  hcWallPrevOk = true;
}
// Copia con esquinas redondeadas y recorte, para tarjetas y miniaturas.
static void hcBlitRounded(int x, int y, int w, int h, int rad, const uint16_t* src){
  if(!src) return;
  for(int j = 0; j < h; j++){
    int yy = y + j;
    if(yy < 0 || yy >= SCR_H || yy < gClipY0 || yy > gClipY1) continue;
    int inset = rrInset(j, h, rad);
    int xs = x + inset, xe = x + w - inset - 1, sx = inset;
    if(xs < gClipX0){ sx += gClipX0 - xs; xs = gClipX0; }
    if(xs < 0){ sx += -xs; xs = 0; }
    if(xe > gClipX1) xe = gClipX1;
    if(xe > SCR_W - 1) xe = SCR_W - 1;
    if(xs > xe) continue;
    memcpy(gBuf + (size_t)yy * SCR_W + xs, src + (size_t)j * w + sx, (size_t)(xe - xs + 1) * 2);
  }
}
// Miniatura de una pagina: se dibuja del MODELO real (iconos y widgets a
// escala), no de una captura. Cero buffers por pagina.
static void hcDrawThumb(int page, int x, int y, int tw, int th){
  if(page < 0 || page >= gHomePageN) return;
  int S, gx0, gy0, cs, rs, cols, rows; homeGrid(S, gx0, gy0, cs, rs, cols, rows);
  int oStyle = gIconStyle;
  gIconStyle = 0;              // en miniatura no se hace un blur Liquid Glass por icono
  // banda superior de clima/calendario y dock, en esquema
  fillRoundRectA(x + 24 * tw / SCR_W, y + 72 * th / SCR_H, 432 * tw / SCR_W, 120 * th / SCR_H,
                 6, TC(255,255,255), 46);
  fillRoundRectA(x + 24 * tw / SCR_W, y + (SCR_H - 176) * th / SCR_H, 432 * tw / SCR_W,
                 96 * th / SCR_H, 8, TC(255,255,255), 60);
  for(int k = 0; k < gHomeWgN[page] && k < HOME_WG_MAX; k++){
    if(gHomeWg[page][k].type == WG_NONE) continue;
    int wx, wy, ww, wh; wgRect(&gHomeWg[page][k], wx, wy, ww, wh);
    wgDrawCell(&gHomeWg[page][k], x + wx * tw / SCR_W, y + wy * th / SCR_H,
               ww * tw / SCR_W, wh * th / SCR_H, true);
  }
  int n = homeSlotCount(), ms = S * tw / SCR_W;
  for(int i = 0; i < n; i++){
    uint8_t id = homeOrder[homeIdx(page, i)];
    if(id == HOME_EMPTY) continue;
    int sx, sy; homeSlotXY(i, sx, sy);
    drawAppIcon(id, x + sx * tw / SCR_W, y + sy * th / SCR_H, ms);
  }
  gIconStyle = oStyle;
}

// #############################################################
// ##  OPERACIONES REALES SOBRE EL MODELO
// ##  Todas guardan al terminar la operacion (homeOrderSave), nunca
// ##  por cuadro ni desde un bucle de arrastre.
// #############################################################
static void hcInfo(const char* m){
  snprintf(hcMsg, sizeof(hcMsg), "%s", m);
  hcModal = HCM_INFO; hcDirty = true;
}
static int homePageItems(int page){
  if(page < 0 || page >= gHomePageN) return 0;
  int n = homeSlotCount(), k = 0;
  for(int i = 0; i < n; i++) if(homeOrder[homeIdx(page, i)] != HOME_EMPTY) k++;
  for(int w = 0; w < gHomeWgN[page] && w < HOME_WG_MAX; w++)
    if(gHomeWg[page][w].type != WG_NONE) k++;
  return k;
}
static bool homePageAdd(){
  if(!homePageAppendQuiet()){ hcInfo("Maximo de 5 paginas"); return false; }
  homeOrderSave();
  return true;
}
// Borra la pagina. Sus iconos NO se pierden: homeOrderNormalize los recoloca en
// el primer hueco libre de las paginas que quedan (y si de verdad no cabe
// ninguno, la app deja de ser favorita, que es lo que ya hacia el sistema).
static bool homePageDelete(int page){
  if(gHomePageN <= 1){ hcInfo("No puedes eliminar la ultima pagina"); return false; }
  if(page < 0 || page >= gHomePageN) return false;
  for(int p = page; p < gHomePageN - 1; p++){
    for(int i = 0; i < HOME_STRIDE; i++) homeOrder[homeIdx(p, i)] = homeOrder[homeIdx(p + 1, i)];
    memcpy(gHomeWg[p], gHomeWg[p + 1], sizeof(gHomeWg[p]));
    gHomeWgN[p] = gHomeWgN[p + 1];
  }
  for(int i = 0; i < HOME_STRIDE; i++) homeOrder[homeIdx(gHomePageN - 1, i)] = HOME_EMPTY;
  gHomeWgN[gHomePageN - 1] = 0;
  gHomePageN--;
  // Pagina principal: si se borro ELLA, se elige la vecina mas cercana de forma
  // predecible (la de la izquierda si existe).
  if(gHomeMain == page)      gHomeMain = (uint8_t)(page > 0 ? page - 1 : 0);
  else if(gHomeMain > page)  gHomeMain--;
  if(gHomeMain >= gHomePageN) gHomeMain = (uint8_t)(gHomePageN - 1);
  if(gHomePage >= gHomePageN) gHomePage = gHomeMain;
  homeOrderNormalize();
  homeOrderSave();
  return true;
}
static void homePageSwap(int a, int b){
  if(a < 0 || b < 0 || a >= gHomePageN || b >= gHomePageN || a == b) return;
  for(int i = 0; i < HOME_STRIDE; i++){
    uint8_t t = homeOrder[homeIdx(a, i)];
    homeOrder[homeIdx(a, i)] = homeOrder[homeIdx(b, i)];
    homeOrder[homeIdx(b, i)] = t;
  }
  HomeWidget tw[HOME_WG_MAX]; memcpy(tw, gHomeWg[a], sizeof(tw));
  memcpy(gHomeWg[a], gHomeWg[b], sizeof(tw)); memcpy(gHomeWg[b], tw, sizeof(tw));
  uint8_t tn = gHomeWgN[a]; gHomeWgN[a] = gHomeWgN[b]; gHomeWgN[b] = tn;
  if(gHomeMain == a)      gHomeMain = (uint8_t)b;
  else if(gHomeMain == b) gHomeMain = (uint8_t)a;
  homeOrderSave();
}
static void homeSetMain(int page){
  if(page < 0 || page >= gHomePageN) return;
  gHomeMain = (uint8_t)page;
  homeOrderSave();
}
// Cambio de rejilla SIN perder elementos: homeOrderNormalize rescata los iconos
// de las ranuras que dejan de existir y los recoloca; homeWgNormalize retira
// los widgets que ya no caben.
static void homeSetGrid(int cols, int rows){
  if(cols < 4) cols = 4;
  if(cols > HOME_COLS_MAX) cols = HOME_COLS_MAX;
  if(rows < 3) rows = 3;
  if(rows > HOME_ROWS_MAX) rows = HOME_ROWS_MAX;
  gHomeCols = (uint8_t)cols; gHomeRows = (uint8_t)rows;
  homeOrderNormalize();
  homeOrderSave();
  gHomeDirty = true;
}
static void homeResetLayout(){
  for(int i = 0; i < HOME_TOTAL; i++) homeOrder[i] = HOME_EMPTY;
  for(int p = 0; p < HOME_PAGES_MAX; p++) gHomeWgN[p] = 0;
  gHomePageN = HOME_LEGACY_PAGES; gHomeMain = 0; gHomePage = 0;
  gHomeCols = 4; gHomeRows = 3; gHomeIconSz = 1;
  drawerRegistryDefaults();                 // el reparto de fabrica sale del registro de apps
  homeOrderNormalize();
  homeOrderSave();
  gHomeDirty = true;
}
// ---- Aplicar fondo ----------------------------------------------------------
// dest: bit0 = inicio · bit1 = bloqueo
static void hcApplyWall(int id, int dest){
  if(id != WALL_IMG && (id < 0 || id >= WALL_N)){ hcInfo("Ese fondo no esta disponible"); return; }
  uint8_t oh = gWallHome, ol = gWallLock;
  if(dest & 1) gWallHome = (uint8_t)id;
  if(dest & 2) gWallLock = (uint8_t)id;
  wallEnsureImage();
  if(id == WALL_IMG && !wallImgOk){          // no se pudo cargar: nada cambia y se dice por que
    gWallHome = oh; gWallLock = ol;
    hcInfo(gWallErr[0] ? gWallErr : "No se pudo usar esa imagen");
    return;
  }
  hcRebuildBlur();
  hcBuildThumb();                            // regenera miniatura y paleta con el fondo nuevo
  gHomeDirty = true; qsDirty = true;
  homeCfgSave();
  hcDirty = true;
}
// ---- Aplicar tema -----------------------------------------------------------
// Operacion COHERENTE: si algo no valida, se restaura el estado anterior entero.
static bool hcApplyLook(int i){
  if(i < 0 || i >= LOOK_N) return false;
  bool oDark = gDark, oGlass = uiGlass, oPal = gWallPalOn;
  int  oIcon = gIconStyle;
  uint8_t oWh = gWallHome, oWl = gWallLock, oLook = gHomeLook;
  gDark = LOOKS[i].dark != 0; uiGlass = LOOKS[i].glass != 0; gIconStyle = LOOKS[i].iconStyle;
  gWallHome = LOOKS[i].wall; gWallLock = LOOKS[i].wall;
  gWallPalOn = LOOKS[i].palette != 0; gHomeLook = (uint8_t)i;
  bool ok = (gWallHome < WALL_N && gWallLock < WALL_N && gIconStyle >= 0 && gIconStyle <= 1);
  if(ok){
    wallEnsureImage();
    hcRebuildBlur(); hcBuildThumb();
    if(!gWallPalOn){ gWallAcc = lookAcc(i); gWallAcc2 = lookAcc2(i); gWallPalOk = true; }
  }
  if(!ok){                                   // vuelta atras completa
    gDark = oDark; uiGlass = oGlass; gIconStyle = oIcon;
    gWallHome = oWh; gWallLock = oWl; gWallPalOn = oPal; gHomeLook = oLook;
    wallEnsureImage(); hcRebuildBlur(); hcBuildThumb();
    hcInfo("No se pudo aplicar el tema");
    return false;
  }
  homeCfgSave();
  themeChanged(true);                        // NVS + caches + repintado, por la via de siempre
  hcDirty = true;
  return true;
}
// ---- Selector de imagen del almacenamiento ---------------------------------
// Recorre las carpetas donde el sistema guarda imagenes y se queda con los JPEG.
// Acotado a HC_PICK_MAX: la lista es de tamano fijo, sin heap.
static void hcScanImages(){
  hcPickN = 0; hcPickSel = -1;
  if(!flexFsReady()) return;
  static const char* DIRS[3] = { "/Imagenes", "/Camara", "/Descargas" };
  FlexFsEntry ents[24];
  for(int d = 0; d < 3 && hcPickN < HC_PICK_MAX; d++){
    int n = flexFsList(DIRS[d], ents, 24);
    for(int i = 0; i < n && hcPickN < HC_PICK_MAX; i++){
      if(ents[i].dir) continue;
      const char* nm = ents[i].name;
      int L = (int)strlen(nm);
      if(L < 5) continue;
      const char* ext = nm + L - 4;
      if(strcmp(ext, ".jpg") && strcmp(ext, ".JPG") &&
         (L < 6 || (strcmp(nm + L - 5, ".jpeg") && strcmp(nm + L - 5, ".JPEG")))) continue;
      snprintf(hcPickPath[hcPickN], sizeof(hcPickPath[0]), "%s/%s", DIRS[d], nm);
      hcPickN++;
    }
  }
}

// #############################################################
// ##  DIBUJO DEL MODO PERSONALIZACION
// ##  Todo compone en bbuf y publica con un unico present().
// #############################################################
static const char* HC_BAR_LBL[4] = { "Fondo", "Temas", "Widgets", "Ajustes" };

static int hcCardX(int i){
  int base = HC_CARD_X + (i - hcPageView) * HC_STEP;
  // Al REORDENAR solo viaja la tarjeta agarrada (las demas se quedan quietas,
  // que es lo que hace legible el hueco de destino); al deslizar, todas.
  if(hcReorder >= 0) return base - (i == hcReorder ? (int)hcSlide : 0);
  return base - (int)hcSlide;
}
static void hcChrome(const char* title, const char* sub){
  cronoBarClock(16, TH_ONWALL);
  drawWifi(SCR_W - 66, 28, 11, TH_ONWALL);
  drawBattery(SCR_W - 46, 20, 30, 15, 82, TH_ONWALL);
  drawTextC(SCR_W / 2, 66, title, 3, TH_ONWALL);
  if(sub && *sub) drawTextC(SCR_W / 2, 98, sub, 2, TH_ONWALL2);
}
static void hcNavBar(){
  if(gNavMode == 0){
    int ny = SCR_H - 52; uint16_t nv = TH_ONWALL;
    int bx = SCR_W / 6;
    fillTriangle(bx - 10, ny + 8, bx + 8, ny - 2, bx + 8, ny + 18, nv);
    drawCircle(SCR_W / 2, ny + 8, 12, nv); drawCircle(SCR_W / 2, ny + 8, 11, nv);
    int rx = SCR_W * 5 / 6;
    drawRoundRect(rx - 11, ny - 3, 22, 22, 4, nv);
  } else drawHomeIndicator(SCR_H, 220);
}
static void hcBackChip(){
  fillRoundRectA(14, 52, 96, 36, 18, TH_SCRIM, 130);
  fillTriangle(34, 70, 46, 61, 46, 79, TH_ONWALL);
  drawText(52, 62, "Atr\xC3\xA1s", 2, TH_ONWALL);
}
static bool hcBackChipHit(int x, int y){ return x >= 14 && x <= 110 && y >= 52 && y <= 88; }
static void hcDrawBar(int active){
  hcPanel(12, HC_BAR_Y, SCR_W - 24, HC_BAR_H, 26, 225);
  int iw = (SCR_W - 24) / 4;
  for(int i = 0; i < 4; i++){
    int x = 12 + iw * i, cx = x + iw / 2, cy = HC_BAR_Y + 40;
    uint16_t c = (i == active) ? wallAccent() : TH_TXT;
    if(i == active) fillRoundRectA(x + 6, HC_BAR_Y + 8, iw - 12, HC_BAR_H - 16, 18, wallAccent(), 46);
    switch(i){
      case 0: hcIcoWall(cx, cy, c);    break;
      case 1: hcIcoTheme(cx, cy, c);   break;
      case 2: hcIcoWidgets(cx, cy, c); break;
      default: hcIcoGear(cx, cy, c);   break;
    }
    drawTextC(cx, HC_BAR_Y + 70, HC_BAR_LBL[i], 2, c);
  }
}
static int hcBarHit(int x, int y){
  if(y < HC_BAR_Y || y > HC_BAR_Y + HC_BAR_H || x < 12 || x > SCR_W - 12) return -1;
  int iw = (SCR_W - 24) / 4, i = (x - 12) / iw;
  return (i >= 0 && i < 4) ? i : -1;
}
// ---- Vista: paginas ---------------------------------------------------------
static void hcDrawPageCard(int page, int x, int y, bool center){
  if(hcThumb) hcBlitRounded(x, y, HC_CARD_W, HC_CARD_H, 26, hcThumb);
  else        uiSurface(x, y, HC_CARD_W, HC_CARD_H, 26, UIS_CARD);
  hcDrawThumb(page, x, y, HC_CARD_W, HC_CARD_H);
  uint16_t bc = center ? wallAccent() : TH_BORDER;
  drawRoundRect(x, y, HC_CARD_W, HC_CARD_H, 26, bc);
  if(center) drawRoundRect(x + 1, y + 1, HC_CARD_W - 2, HC_CARD_H - 2, 25, bc);
  int hx = x + HC_CARD_W - 26, hy = y + 26;
  fillCircleA(hx, hy, 16, TH_SCRIM, 140);
  hcIcoHome(hx, hy + 3, 15, gHomeMain == page ? TC(255,214,80) : TH_ONWALL, gHomeMain == page);
  if(gHomePageN > 1 && !gHomeLocked){
    int tx = x + 26;
    fillCircleA(tx, hy, 16, TH_SCRIM, 140);
    hcIcoTrash(tx, hy + 2, TC(252,214,214));
  }
  char nb[24]; snprintf(nb, sizeof(nb), "P\xC3\xA1gina %d", page + 1);
  drawTextC(x + HC_CARD_W / 2, y + HC_CARD_H + 6, nb, 2, TH_ONWALL);
}
static void hcDrawAddCard(int x, int y){
  fillRoundRectA(x, y, HC_CARD_W, HC_CARD_H, 26, TC(255,255,255), 34);
  drawRoundRect(x, y, HC_CARD_W, HC_CARD_H, 26, TH_ONWALL2);
  hcIcoPlus(x + HC_CARD_W / 2, y + HC_CARD_H / 2, 56, TH_ONWALL);
  drawTextC(x + HC_CARD_W / 2, y + HC_CARD_H + 6, "Nueva p\xC3\xA1gina", 2, TH_ONWALL);
}
static void hcDrawPageDots(){
  int n = gHomePageN, step = 26, x0 = SCR_W / 2 - (n - 1) * step / 2;
  for(int i = 0; i < n; i++){
    int x = x0 + i * step;
    if(i == gHomeMain) hcIcoHome(x, HC_DOTS_Y, 13, i == hcPageView ? wallAccent() : TH_ONWALL, true);
    else fillCircleA(x, HC_DOTS_Y, 5, i == hcPageView ? wallAccent() : TH_ONWALL, i == hcPageView ? 255 : 140);
  }
}
static void hcDrawPagesView(){
  int total = gHomePageN + ((gHomePageN < HOME_PAGES_MAX && !gHomeLocked) ? 1 : 0);
  for(int i = hcPageView - 1; i <= hcPageView + 1; i++){
    if(i < 0 || i >= total) continue;
    int x = hcCardX(i);
    if(x > SCR_W || x + HC_CARD_W < 0) continue;
    if(i < gHomePageN) hcDrawPageCard(i, x, HC_CARD_Y, i == hcPageView);
    else               hcDrawAddCard(x, HC_CARD_Y);
  }
  hcDrawPageDots();
  hcDrawBar(-1);
}
// ---- Vista: fondo de pantalla y estilo -------------------------------------
static void hcDrawWallView(){
  hcChrome("Fondo de pantalla", "Integrados de Flex OS e im\xC3\xA1genes tuyas");
  hcBackChip();
  for(int i = 0; i < WALL_N; i++){
    int c = i % 4, r = i / 4;
    int x = 18 + c * 116, y = 130 + r * 186;
    if(hcWallPrev) hcBlitRounded(x, y, HC_PV_W, HC_PV_H, 12, hcWallPrev + (size_t)i * HC_PV_W * HC_PV_H);
    else           uiSurface(x, y, HC_PV_W, HC_PV_H, 12, UIS_CARD);
    bool sel = (hcWallSel == i);
    drawRoundRect(x, y, HC_PV_W, HC_PV_H, 12, sel ? wallAccent() : TH_BORDER);
    if(sel){
      drawRoundRect(x + 1, y + 1, HC_PV_W - 2, HC_PV_H - 2, 11, wallAccent());
      fillCircle(x + HC_PV_W - 14, y + 14, 9, wallAccent());
      strokeSegAA(x + HC_PV_W - 18.0f, (float)y + 14, x + HC_PV_W - 15.0f, (float)y + 18, 1.8f, onColor(wallAccent()));
      strokeSegAA(x + HC_PV_W - 15.0f, (float)y + 18, x + HC_PV_W - 9.0f, (float)y + 9, 1.8f, onColor(wallAccent()));
    }
    if(gWallHome == i) fillCircle(x + 12, y + HC_PV_H - 12, 5, wallAccent());
    drawTextC(x + HC_PV_W / 2, y + HC_PV_H + 5, WALL_NAME[i], 1, TH_ONWALL);
  }
  int y = HC_WSHEET_Y;
  hcPanel(12, y, SCR_W - 24, 148, 22, 235);
  if(hcWallSel < 0) drawTextC(SCR_W / 2, y + 18, "Elige un fondo para ver las opciones", 2, TH_TXT2);
  else {
    char tb[56];
    snprintf(tb, sizeof(tb), "Aplicar \"%s\" a:", hcWallSel == WALL_IMG ? "tu imagen" : WALL_NAME[hcWallSel]);
    drawTextClip(28, y + 12, tb, 2, TH_TXT, SCR_W - 28);
    int bw = (SCR_W - 24 - 4 * 12) / 3;
    hcBtn(24, y + 38, bw, 42, "Inicio", true);
    hcBtn(24 + bw + 12, y + 38, bw, 42, "Bloqueo", true);
    hcBtn(24 + 2 * (bw + 12), y + 38, bw, 42, "Ambos", true);
  }
  hcBtn(24, y + 88, (SCR_W - 60) / 2, 42, "Mis im\xC3\xA1genes", false);
  hcBtn(36 + (SCR_W - 60) / 2, y + 88, (SCR_W - 60) / 2, 42, "Restaurar", false);
  hcRow(y + 158, 44, "Aplicar paleta al sistema", NULL, 1, gWallPalOn);
  hcNavBar();
}
// ---- Vista: elegir imagen del almacenamiento -------------------------------
static void hcDrawPickView(){
  hcChrome("Mis im\xC3\xA1genes", "JPEG de /Imagenes, /Camara y /Descargas");
  hcBackChip();
  if(hcPickN == 0){
    drawTextC(SCR_W / 2, 230, "No hay imagenes JPEG guardadas", 2, TH_ONWALL2);
    drawTextC(SCR_W / 2, 256, "Guarda alguna desde Camara o Archivos", 1, TH_ONWALL2);
    hcNavBar();
    return;
  }
  int oc0 = gClipY0, oc1 = gClipY1;
  gClipY0 = HC_LIST_TOP; gClipY1 = HC_LIST_BOT;
  int y = HC_LIST_TOP + 4 - hcScroll;
  for(int i = 0; i < hcPickN; i++){
    bool sel = (hcPickSel == i);
    hcPanel(16, y, SCR_W - 32, 52, 14, sel ? 250 : 225);
    if(sel) drawRoundRect(16, y, SCR_W - 32, 52, 14, wallAccent());
    drawTextClip(32, y + 16, hcPickPath[i], 2, TH_TXT, SCR_W - 150);
    if(sel) hcBtn(SCR_W - 126, y + 8, 100, 36, "Usar", true);
    y += 58;
  }
  hcScrollMax = y + hcScroll - HC_LIST_BOT + 10;
  if(hcScrollMax < 0) hcScrollMax = 0;
  gClipY0 = oc0; gClipY1 = oc1;
  if(gWallErr[0]) drawTextC(SCR_W / 2, HC_LIST_BOT + 6, gWallErr, 1, TH_ERR);
  hcNavBar();
}
// ---- Vista: temas ----------------------------------------------------------
static void hcDrawThemeView(){
  hcChrome("Temas", "Temas integrados de Flex OS");
  hcBackChip();
  for(int i = 0; i < LOOK_N; i++){
    int y = 130 + i * 54;
    bool sel = (hcThemeSel == i);
    hcPanel(16, y, SCR_W - 32, 48, 16, sel ? 250 : 225);
    if(sel) drawRoundRect(16, y, SCR_W - 32, 48, 16, wallAccent());
    fillRoundRect(28, y + 10, 28, 28, 8, lookAcc(i));
    fillRoundRect(60, y + 10, 14, 28, 6, lookAcc2(i));
    fillRoundRect(78, y + 10, 14, 28, 6, LOOKS[i].dark ? TC(30,34,46) : TC(240,243,249));
    drawText(104, y + 15, LOOKS[i].name, 2, TH_TXT);
    if(gHomeLook == i) drawTextR(SCR_W - 32, y + 16, "Activo", 1, wallAccent());
  }
  int by = 130 + LOOK_N * 54 + 8, bw = (SCR_W - 60) / 2;
  if(hcThemeSel >= 0){
    hcBtn(24, by, bw, 44, "Aplicar", true);
    hcBtn(36 + bw, by, bw, 44, "Cancelar", false);
  } else drawTextC(SCR_W / 2, by + 12, "Elige un tema para verlo y aplicarlo", 2, TH_ONWALL2);
  hcBtn(24, by + 54, SCR_W - 48, 42, "Restaurar tema predeterminado", false);
  hcNavBar();
}
// ---- Vista: widgets --------------------------------------------------------
static void hcDrawWidgetView(){
  char sb[56];
  snprintf(sb, sizeof(sb), "Se colocan en la p\xC3\xA1gina %d", (hcPageView < gHomePageN ? hcPageView : gHomeMain) + 1);
  hcChrome("Widgets", sb);
  hcBackChip();
  int oc0 = gClipY0, oc1 = gClipY1;
  gClipY0 = HC_LIST_TOP; gClipY1 = HC_LIST_BOT;
  int y = HC_LIST_TOP + 4 - hcScroll;
  const char* lastCat = NULL;
  for(int i = 1; i < WG_COUNT; i++){
    if(i == WG_RETIRED_7) continue;
    if(!lastCat || strcmp(lastCat, WG_REG[i].cat)){
      lastCat = WG_REG[i].cat;
      drawText(24, y + 4, lastCat, 2, TH_ONWALL2);
      y += 26;
    }
    bool sel = (hcWgSel == i);
    hcPanel(16, y, SCR_W - 32, 76, 16, sel ? 250 : 225);
    if(sel) drawRoundRect(16, y, SCR_W - 32, 76, 16, wallAccent());
    // vista previa REAL del widget, recortada a su caja
    int px0 = 26, py0 = y + 8, pw = 108, ph = 60;
    int sx0 = gClipX0, sx1 = gClipX1, sy0 = gClipY0, sy1 = gClipY1;
    gClipX0 = px0; gClipX1 = px0 + pw - 1;
    if(gClipY0 < py0) gClipY0 = py0;
    if(gClipY1 > py0 + ph - 1) gClipY1 = py0 + ph - 1;
    HomeWidget pv; pv.type = (uint8_t)i; pv.col = 0; pv.row = 0; pv.w = WG_REG[i].w; pv.h = WG_REG[i].h;
    wgDrawCell(&pv, px0, py0, pw, ph, false);
    gClipX0 = sx0; gClipX1 = sx1; gClipY0 = sy0; gClipY1 = sy1;
    drawTextClip(148, y + 18, WG_REG[i].name, 2, TH_TXT, SCR_W - 130);
    char sz[24]; snprintf(sz, sizeof(sz), "%dx%d celdas", WG_REG[i].w, WG_REG[i].h);
    drawText(148, y + 42, sz, 1, TH_TXT2);
    if(sel) hcBtn(SCR_W - 122, y + 20, 96, 36, "A\xC3\xB1" "adir", true);
    y += 82;
  }
  hcScrollMax = y + hcScroll - HC_LIST_BOT + 10;
  if(hcScrollMax < 0) hcScrollMax = 0;
  gClipY0 = oc0; gClipY1 = oc1;
  hcNavBar();
}
// ---- Vista: ajustes de inicio ----------------------------------------------
// Solo se ofrecen rejillas que CABEN de verdad en 480x800 con su tamano de
// icono: la validacion la hace homeGrid() y aqui se listan las ya comprobadas.
#define HC_GRID_OPTS 4
static const uint8_t HC_GRID_C[HC_GRID_OPTS] = { 4, 5, 4, 5 };
static const uint8_t HC_GRID_R[HC_GRID_OPTS] = { 3, 3, 4, 4 };
static int hcGridIdx(){
  for(int i = 0; i < HC_GRID_OPTS; i++) if(HC_GRID_C[i] == gHomeCols && HC_GRID_R[i] == gHomeRows) return i;
  return 0;
}
static void hcDrawSettingsView(){
  hcChrome("Ajustes de inicio", NULL);
  hcBackChip();
  int oc0 = gClipY0, oc1 = gClipY1;
  gClipY0 = HC_LIST_TOP; gClipY1 = HC_LIST_BOT;
  int y = HC_LIST_TOP + 4 - hcScroll;
  char v[32];
  snprintf(v, sizeof(v), "%dx%d", gHomeCols, gHomeRows);
  hcRow(y, 52, "Cuadr\xC3\xAD" "cula del inicio", v, 0, false); y += 58;
  static const char* SZN[3] = { "Peque\xC3\xB1o", "Normal", "Grande" };
  hcRow(y, 52, "Tama\xC3\xB1o de iconos", SZN[gHomeIconSz > 2 ? 1 : gHomeIconSz], 0, false); y += 58;
  hcRow(y, 52, "Nombres de aplicaciones", NULL, 1, gHomeLabels); y += 58;
  hcRow(y, 52, "Bloquear dise\xC3\xB1o del inicio", NULL, 1, gHomeLocked); y += 58;
  hcRow(y, 52, "Indicadores de p\xC3\xA1gina", NULL, 1, gHomeDots); y += 58;
  snprintf(v, sizeof(v), "P\xC3\xA1gina %d", gHomeMain + 1);
  hcRow(y, 52, "P\xC3\xA1gina principal", v, 0, false); y += 58;
  hcRow(y, 52, "Gesto de pellizco", NULL, 1, gHomePinch); y += 58;
  hcRow(y, 52, "Reducir animaciones", NULL, 1, gHomeReduce); y += 58;
  const char* fitn = gWallFit == 1 ? "Ajustar" : gWallFit == 2 ? "Centrar" : "Rellenar";
  hcRow(y, 52, "Encuadre de la imagen", fitn, 0, false); y += 58;
  hcRow(y, 52, "Restablecer dise\xC3\xB1o del inicio", NULL, 2, false); y += 58;
  hcScrollMax = y + hcScroll - HC_LIST_BOT + 10;
  if(hcScrollMax < 0) hcScrollMax = 0;
  gClipY0 = oc0; gClipY1 = oc1;
  hcNavBar();
}
// ---- Modales ---------------------------------------------------------------
static void hcDrawModal(){
  if(hcModal == HCM_NONE) return;
  fillRectA(0, 0, SCR_W, SCR_H, TH_SCRIM, 150);
  int w = SCR_W - 64, h = 200, x = 32, y = (SCR_H - h) / 2;
  uiSurface(x, y, w, h, 26, UIS_CARD);          // dialogo: material del sistema (Plano o Vidrio)
  drawRoundRect(x, y, w, h, 26, TH_BORDER);
  const char* ttl = hcModal == HCM_DELPAGE ? "Eliminar esta p\xC3\xA1gina"
                  : hcModal == HCM_RESET   ? "Restablecer el inicio" : "Aviso";
  drawTextC(SCR_W / 2, y + 26, ttl, 3, TH_TXT);
  if(hcModal == HCM_INFO) drawTextC(SCR_W / 2, y + 80, hcMsg, 2, TH_TXT2);
  else if(hcModal == HCM_RESET){
    drawTextC(SCR_W / 2, y + 70, "Vuelve al reparto de fabrica:", 2, TH_TXT2);
    drawTextC(SCR_W / 2, y + 92, "tres p\xC3\xA1ginas y rejilla 4x3", 2, TH_TXT2);
  } else {
    drawTextC(SCR_W / 2, y + 70, "Sus iconos se recolocan solos", 2, TH_TXT2);
    drawTextC(SCR_W / 2, y + 92, "en las paginas que queden", 2, TH_TXT2);
  }
  int bw = (w - 3 * 16) / 2, by = y + h - 62;
  if(hcModal == HCM_INFO) hcBtn(x + (w - 140) / 2, by, 140, 46, "Entendido", true);
  else {
    hcBtn(x + 16, by, bw, 46, "Cancelar", false);
    hcBtn(x + 32 + bw, by, bw, 46, hcModal == HCM_RESET ? "Restablecer" : "Eliminar", true);
  }
}
static void hcRender(){
  if(!bbuf || !blurBg) return;
  setBuf(bbuf);
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  memcpy(bbuf, blurBg, (size_t)SCR_W * SCR_H * 2);   // fondo atenuado, generado una sola vez
  switch(hcView){
    case HCV_WALL:     hcDrawWallView();     break;
    case HCV_PICK:     hcDrawPickView();     break;
    case HCV_THEME:    hcDrawThemeView();    break;
    case HCV_WIDGETS:  hcDrawWidgetView();   break;
    case HCV_SETTINGS: hcDrawSettingsView(); break;
    default: {
      char sub[40];
      snprintf(sub, sizeof(sub), "P\xC3\xA1gina %d de %d",
               (hcPageView < gHomePageN ? hcPageView : gHomePageN - 1) + 1, gHomePageN);
      hcChrome("Personalizar inicio", sub);
      hcDrawPagesView();
      hcNavBar();
      break;
    }
  }
  hcDrawModal();
  present(0, SCR_H - 1);
  setBuf(fb);
  hcDirty = false;
}

// #############################################################
// ##  ENTRADA, SALIDA Y TRANSICION
// #############################################################
static void hcEnter(){
  if(hcActive || !bbuf || !homeBuf) return;
  hcRebuildBlur();
  if(!blurBg) return;              // sin PSRAM para el fondo atenuado: no se entra a medias
  hcBuildThumb();                  // miniatura del fondo y paleta (usa bbuf de borrador)
  hcActive   = true;
  hcView     = HCV_PAGES;
  hcPageView = gHomePage;
  hcSlide    = 0.0f; hcDragging = false; hcDragMoved = false; hcReorder = -1;
  hcModal    = HCM_NONE; hcScroll = 0;
  hcWallSel  = -1; hcThemeSel = -1; hcWgSel = -1; hcPickSel = -1;
  hcIgnore   = true; hcDirty = true;
  gRippleActive = false;
  gState     = ST_HOMECFG;
  hcAnim     = gHomeReduce ? 0 : 1;
  hcAnimT0   = millis();
  if(!hcAnim) hcRender();
}
// Cierre SEGURO desde cualquier ruta (auto-bloqueo, OTA, suspension, vuelta al
// escritorio). No anima ni pinta: solo deja el estado y la memoria limpios.
static void hcClose(bool save){
  if(!hcActive) return;
  hcActive = false; hcAnim = 0; hcModal = HCM_NONE;
  hcFreeBuffers();
  if(hcPageView < gHomePageN) gHomePage = hcPageView;
  if(save){ homeOrderNormalize(); homeOrderSave(); homeCfgSave(); }
}
static void hcFinishExit(){
  hcClose(true);
  gState = ST_HOME;
  gHomeDirty = true;
  renderHome(); showHome();
}
static void hcBeginExit(){
  if(!hcActive) return;
  // homeBuf tiene que reflejar YA lo que el usuario acaba de configurar (pagina
  // centrada, fondo, tema, rejilla, widgets): la animacion de salida lo
  // MUESTREA. renderHome() compone fuera de pantalla, asi que no dibuja nada.
  if(hcPageView < gHomePageN) gHomePage = hcPageView; else gHomePage = gHomeMain;
  homeOrderNormalize();
  renderHome();
  if(gHomeReduce){ hcFinishExit(); return; }
  hcAnim = 2; hcAnimT0 = millis();
}
// Transicion: el escritorio real (homeBuf) se reduce hacia el centro sobre el
// fondo atenuado. Se muestrea homeBuf por vecino mas cercano dentro del
// rectangulo destino: ni un framebuffer extra por pagina, ~78 k pixeles por
// cuadro y unos seis cuadros en total.
static void hcAnimTick(){
  if(!bbuf || !blurBg || !homeBuf){ if(hcAnim == 2) hcFinishExit(); else { hcAnim = 0; hcDirty = true; } return; }
  uint32_t e = millis() - hcAnimT0;
  if(e > HC_ANIM_MS) e = HC_ANIM_MS;
  float p = (float)e / (float)HC_ANIM_MS;
  p = 1.0f - (1.0f - p) * (1.0f - p);                 // ease-out
  if(hcAnim == 2) p = 1.0f - p;                       // saliendo: al reves
  int w = (int)(SCR_W + (HC_CARD_W - SCR_W) * p);
  int h = (int)(SCR_H + (HC_CARD_H - SCR_H) * p);
  if(w < 8) w = 8;
  if(h < 8) h = 8;
  int x = (SCR_W - w) / 2, y = (int)(HC_CARD_Y * p);
  setBuf(bbuf);
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  memcpy(bbuf, blurBg, (size_t)SCR_W * SCR_H * 2);
  int rad = (int)(26 * p);
  for(int j = 0; j < h; j++){
    int yy = y + j;
    if(yy < 0 || yy >= SCR_H) continue;
    int inset = rad > 1 ? rrInset(j, h, rad) : 0;
    const uint16_t* srow = homeBuf + (size_t)((j * SCR_H) / h) * SCR_W;
    uint16_t* drow = bbuf + (size_t)yy * SCR_W;
    for(int i = inset; i < w - inset; i++){
      int xx = x + i;
      if(xx < 0 || xx >= SCR_W) continue;
      drow[xx] = srow[(i * SCR_W) / w];
    }
  }
  present(0, SCR_H - 1);
  setBuf(fb);
  if(e >= HC_ANIM_MS){
    if(hcAnim == 2){ hcFinishExit(); return; }
    hcAnim = 0; hcDirty = true;
  }
}
static void hcOpenView(int v){
  hcView = (uint8_t)v; hcScroll = 0; hcDirty = true;
  if(v == HCV_WALL){ hcWallSel = -1; hcBuildWallPreviews(); }
  if(v == HCV_THEME) hcThemeSel = -1;
  if(v == HCV_WIDGETS) hcWgSel = -1;
  if(v == HCV_PICK) hcScanImages();
}
static void hcBack(){
  if(hcModal != HCM_NONE){ hcModal = HCM_NONE; hcDirty = true; return; }
  if(hcView == HCV_PICK){ hcOpenView(HCV_WALL); return; }
  if(hcView != HCV_PAGES){ hcView = HCV_PAGES; hcScroll = 0; hcDirty = true; return; }
  hcBeginExit();
}

// #############################################################
// ##  TOQUES DEL MODO PERSONALIZACION
// #############################################################
static void hcModalTouch(){
  if(!T.tap) return;
  int w = SCR_W - 64, h = 200, x = 32, y = (SCR_H - h) / 2;
  int bw = (w - 3 * 16) / 2, by = y + h - 62;
  if(hcModal == HCM_INFO){
    if(hcHit(T.x, T.y, x + (w - 140) / 2, by, 140, 46)){ hcModal = HCM_NONE; hcDirty = true; }
    return;
  }
  if(hcHit(T.x, T.y, x + 16, by, bw, 46)){ hcModal = HCM_NONE; hcDirty = true; return; }
  if(!hcHit(T.x, T.y, x + 32 + bw, by, bw, 46)) return;
  uint8_t m = hcModal; hcModal = HCM_NONE; hcDirty = true;
  if(m == HCM_DELPAGE){
    if(homePageDelete(hcPageView) && hcPageView >= gHomePageN) hcPageView = gHomePageN - 1;
  } else if(m == HCM_RESET){
    homeResetLayout(); hcPageView = 0; hcBuildThumb();
  }
}
static void hcPagesTouch(){
  int total = gHomePageN + ((gHomePageN < HOME_PAGES_MAX && !gHomeLocked) ? 1 : 0);
  if(T.tap){
    int b = hcBarHit(T.x, T.y);
    if(b >= 0){
      hcOpenView(b == 0 ? HCV_WALL : b == 1 ? HCV_THEME : b == 2 ? HCV_WIDGETS : HCV_SETTINGS);
      return;
    }
    int n = gHomePageN, step = 26, x0 = SCR_W / 2 - (n - 1) * step / 2;
    if(T.y >= HC_DOTS_Y - 18 && T.y <= HC_DOTS_Y + 18){
      for(int i = 0; i < n; i++)
        if(abs(T.x - (x0 + i * step)) <= 13){ hcPageView = i; hcSlide = 0; hcDirty = true; return; }
    }
  }
  // --- reordenar: pulsacion larga sobre la tarjeta centrada ---
  if(!gHomeLocked && hcReorder < 0 && T.down && !hcDragMoved && hcPageView < gHomePageN &&
     (millis() - T.downMs) > 650 && abs(T.x - T.startX) <= 12 && abs(T.y - T.startY) <= 12 &&
     hcHit(T.startX, T.startY, hcCardX(hcPageView), HC_CARD_Y, HC_CARD_W, HC_CARD_H)){
    hcReorder = hcPageView; hcDragX0 = T.x; hcDragging = false; hcSlide = 0; hcDirty = true; return;
  }
  if(hcReorder >= 0){
    if(T.down){ hcSlide = (float)(hcDragX0 - T.x); hcDirty = true; return; }
    if(T.released){
      int dst = hcReorder - (int)((hcSlide + (hcSlide < 0 ? -HC_STEP / 2 : HC_STEP / 2)) / HC_STEP);
      if(dst < 0) dst = 0;
      if(dst > gHomePageN - 1) dst = gHomePageN - 1;
      while(dst > hcReorder){ homePageSwap(hcReorder, hcReorder + 1); hcReorder++; }
      while(dst < hcReorder){ homePageSwap(hcReorder, hcReorder - 1); hcReorder--; }
      hcPageView = hcReorder; hcReorder = -1; hcSlide = 0; hcDirty = true;
      return;
    }
  }
  if(T.tap && !hcDragMoved){
    int hy = HC_CARD_Y + 26;
    // casita de la tarjeta centrada o de una vecina visible -> pagina principal
    for(int i = hcPageView - 1; i <= hcPageView + 1; i++){
      if(i < 0 || i >= gHomePageN) continue;
      if(abs(T.x - (hcCardX(i) + HC_CARD_W - 26)) <= 20 && abs(T.y - hy) <= 20){
        homeSetMain(i); hcDirty = true; return;
      }
    }
    if(hcPageView < gHomePageN && gHomePageN > 1 && !gHomeLocked &&
       abs(T.x - (hcCardX(hcPageView) + 26)) <= 20 && abs(T.y - hy) <= 20){
      hcModal = HCM_DELPAGE; hcDirty = true; return;
    }
    // tarjeta "+" -> crear pagina (solo si el toque cae DENTRO de la tarjeta)
    if(total > gHomePageN && hcHit(T.x, T.y, hcCardX(gHomePageN), HC_CARD_Y, HC_CARD_W, HC_CARD_H)){
      if(homePageAdd()){ hcPageView = gHomePageN - 1; hcSlide = 0; }
      hcDirty = true; return;
    }
    for(int i = hcPageView - 1; i <= hcPageView + 1; i += 2){
      if(i < 0 || i >= total) continue;
      if(hcHit(T.x, T.y, hcCardX(i), HC_CARD_Y, HC_CARD_W, HC_CARD_H)){
        hcPageView = i; hcSlide = 0; hcDirty = true; return;
      }
    }
  }
  // --- arrastre horizontal entre paginas ---
  if(T.pressed && T.y >= HC_CARD_Y - 40 && T.y <= HC_CARD_Y + HC_CARD_H + 40){
    hcDragging = true; hcDragMoved = false; hcDragX0 = T.x; hcDragBase = hcSlide; return;
  }
  if(hcDragging && T.down){
    int dx = T.x - hcDragX0;
    if(!hcDragMoved){ if(abs(dx) < 14) return; hcDragMoved = true; }
    float sv = hcDragBase - dx, lim = (float)HC_STEP * 0.42f;
    if(hcPageView == 0 && sv < -lim) sv = -lim;
    if(hcPageView >= total - 1 && sv > lim) sv = lim;
    hcSlide = sv; hcDirty = true;
    return;
  }
  if(hcDragging && !T.down){
    hcDragging = false;
    if(hcDragMoved){
      if(hcSlide > HC_STEP / 3 && hcPageView < total - 1) hcPageView++;
      else if(hcSlide < -HC_STEP / 3 && hcPageView > 0)   hcPageView--;
    }
    hcSlide = 0; hcDirty = true;
  }
}
static void hcListScroll(){
  static bool dragging = false; static int y0 = 0, base = 0;
  if(T.pressed && T.y > HC_LIST_TOP && T.y < HC_LIST_BOT){ dragging = true; y0 = T.y; base = hcScroll; return; }
  if(dragging && T.down){
    int sv = base - (T.y - y0);
    if(sv < 0) sv = 0;
    if(sv > hcScrollMax) sv = hcScrollMax;
    if(sv != hcScroll){ hcScroll = sv; hcDirty = true; }
    return;
  }
  if(dragging && !T.down) dragging = false;
}
static void hcWallTouch(){
  if(!T.tap) return;
  if(hcBackChipHit(T.x, T.y)){ hcBack(); return; }
  for(int i = 0; i < WALL_N; i++){
    int c = i % 4, r = i / 4;
    if(hcHit(T.x, T.y, 18 + c * 116, 130 + r * 186, HC_PV_W, HC_PV_H)){ hcWallSel = i; hcDirty = true; return; }
  }
  int y = HC_WSHEET_Y;
  if(hcWallSel >= 0){
    int bw = (SCR_W - 24 - 4 * 12) / 3;
    if(hcHit(T.x, T.y, 24, y + 38, bw, 42)){ hcApplyWall(hcWallSel, 1); return; }
    if(hcHit(T.x, T.y, 24 + bw + 12, y + 38, bw, 42)){ hcApplyWall(hcWallSel, 2); return; }
    if(hcHit(T.x, T.y, 24 + 2 * (bw + 12), y + 38, bw, 42)){ hcApplyWall(hcWallSel, 3); return; }
  }
  int hw = (SCR_W - 60) / 2;
  if(hcHit(T.x, T.y, 24, y + 88, hw, 42)){ hcOpenView(HCV_PICK); return; }
  if(hcHit(T.x, T.y, 36 + hw, y + 88, hw, 42)){ hcWallSel = 0; hcApplyWall(0, 3); return; }
  if(hcHit(T.x, T.y, 16, y + 158, SCR_W - 32, 44)){
    gWallPalOn = !gWallPalOn;
    if(gWallPalOn) hcBuildThumb();               // recalcula la paleta una sola vez
    else { gWallAcc = lookAcc(gHomeLook); gWallAcc2 = lookAcc2(gHomeLook); }
    homeCfgSave(); gHomeDirty = true; qsDirty = true; hcDirty = true;
    return;
  }
}
static void hcPickTouch(){
  hcListScroll();
  if(!T.tap) return;
  if(hcBackChipHit(T.x, T.y)){ hcBack(); return; }
  int y = HC_LIST_TOP + 4 - hcScroll;
  for(int i = 0; i < hcPickN; i++){
    if(hcHit(T.x, T.y, 16, y, SCR_W - 32, 52)){
      if(hcPickSel == i && hcHit(T.x, T.y, SCR_W - 126, y + 8, 100, 36)){
        snprintf(gWallPath, sizeof(gWallPath), "%s", hcPickPath[i]);
        wallImgDrop();
        hcWallSel = WALL_IMG;
        hcApplyWall(WALL_IMG, 3);
        if(wallImgOk) hcOpenView(HCV_WALL);
      } else hcPickSel = i;
      hcDirty = true; return;
    }
    y += 58;
  }
}
static void hcThemeTouch(){
  if(!T.tap) return;
  if(hcBackChipHit(T.x, T.y)){ hcBack(); return; }
  for(int i = 0; i < LOOK_N; i++)
    if(hcHit(T.x, T.y, 16, 130 + i * 54, SCR_W - 32, 48)){ hcThemeSel = i; hcDirty = true; return; }
  int by = 130 + LOOK_N * 54 + 8, bw = (SCR_W - 60) / 2;
  if(hcThemeSel >= 0){
    if(hcHit(T.x, T.y, 24, by, bw, 44)){ hcApplyLook(hcThemeSel); return; }
    if(hcHit(T.x, T.y, 36 + bw, by, bw, 44)){ hcThemeSel = -1; hcDirty = true; return; }
  }
  if(hcHit(T.x, T.y, 24, by + 54, SCR_W - 48, 42)){ hcThemeSel = 0; hcApplyLook(0); return; }
}
static void hcWidgetTouch(){
  hcListScroll();
  if(!T.tap) return;
  if(hcBackChipHit(T.x, T.y)){ hcBack(); return; }
  int y = HC_LIST_TOP + 4 - hcScroll;
  const char* lastCat = NULL;
  for(int i = 1; i < WG_COUNT; i++){
    if(i == WG_RETIRED_7) continue;
    if(!lastCat || strcmp(lastCat, WG_REG[i].cat)){ lastCat = WG_REG[i].cat; y += 26; }
    if(hcHit(T.x, T.y, 16, y, SCR_W - 32, 76)){
      if(hcWgSel == i && hcHit(T.x, T.y, SCR_W - 122, y + 20, 96, 36)){
        int pg = (hcPageView < gHomePageN) ? hcPageView : gHomeMain;
        int r = homeWgAdd(pg, i);
        if(r == 1)      hcInfo("Esta pagina ya tiene 3 widgets");
        else if(r == 2) hcInfo("Sin espacio: elige otra pagina");
        else { homeOrderNormalize(); homeOrderSave(); hcView = HCV_PAGES; }
      } else hcWgSel = i;
      hcDirty = true; return;
    }
    y += 82;
  }
}
static void hcSettingsTouch(){
  hcListScroll();
  if(!T.tap) return;
  if(hcBackChipHit(T.x, T.y)){ hcBack(); return; }
  int rel = T.y + hcScroll - (HC_LIST_TOP + 4);
  if(rel < 0) return;
  int idx = rel / 58;
  if(rel % 58 > 52) return;                     // ha caido en el hueco entre filas
  switch(idx){
    case 0: { int g = (hcGridIdx() + 1) % HC_GRID_OPTS; homeSetGrid(HC_GRID_C[g], HC_GRID_R[g]); hcBuildThumb(); break; }
    case 1:  gHomeIconSz = (uint8_t)((gHomeIconSz + 1) % 3); homeOrderSave(); break;
    case 2:  gHomeLabels = !gHomeLabels; homeOrderSave(); break;
    case 3:  gHomeLocked = !gHomeLocked; homeOrderSave(); break;
    case 4:  gHomeDots   = !gHomeDots;   homeOrderSave(); break;
    case 5:  hcView = HCV_PAGES; hcPageView = gHomeMain; hcSlide = 0; break;
    case 6:  gHomePinch  = !gHomePinch;  homeOrderSave(); break;
    case 7:  gHomeReduce = !gHomeReduce; homeOrderSave(); break;
    case 8:  gWallFit = (uint8_t)((gWallFit + 1) % 3);
             homeCfgSave();
             if(gWallHome == WALL_IMG || gWallLock == WALL_IMG){
               wallImgDrop(); wallEnsureImage(); hcRebuildBlur(); hcBuildThumb();
             }
             break;
    case 9:  hcModal = HCM_RESET; break;
    default: return;
  }
  gHomeDirty = true; hcDirty = true;
}
static void hcTick(){
  if(!hcActive){ gState = ST_HOME; renderHome(); showHome(); return; }
  if(hcAnim){ hcAnimTick(); return; }
  if(hcIgnore){ if(T.down) return; hcIgnore = false; }
  if(hcModal != HCM_NONE){ hcModalTouch(); if(hcDirty) hcRender(); return; }
  // Barra de navegacion: Atras vuelve una pantalla; Inicio sale del modo.
  if(T.tap && gNavMode == 0 && T.y > SCR_H - 72){
    if(T.x < SCR_W / 3){ hcBack(); if(hcActive && hcDirty) hcRender(); return; }
    if(T.x < SCR_W * 2 / 3){ hcBeginExit(); return; }
  }
  if(T.swipeUp && gNavMode == 1 && T.startY > SCR_H - 44){ hcBeginExit(); return; }
  switch(hcView){
    case HCV_WALL:     hcWallTouch();     break;
    case HCV_PICK:     hcPickTouch();     break;
    case HCV_THEME:    hcThemeTouch();    break;
    case HCV_WIDGETS:  hcWidgetTouch();   break;
    case HCV_SETTINGS: hcSettingsTouch(); break;
    default:           hcPagesTouch();    break;
  }
  if(!hcActive) return;                         // alguna accion pidio salir
  if(hcDirty && millis() - hcFrameMs >= 24){ hcFrameMs = millis(); hcRender(); }
}

// #############################################################
// ##  GESTO MULTITACTIL DE DOS DEDOS  (pellizco)
// ##  ------------------------------------------------------
// ##  Usa gtPollMulti(), la lectura MULTIPUNTO real del GT911
// ##  (bloques 0x8150+i*8, con TRACK ID en el byte 7), no una
// ##  aproximacion sobre el punto unico de struct Touch. Respeta
// ##  GT911_SWAP_XY / FLIP_X / FLIP_Y, porque esa correccion vive
// ##  DENTRO de gtPollMulti: aqui no se toca el driver.
// ##
// ##  Se pide multitouch SOLO cuando el propio chip ya ha dicho
// ##  que hay >= 2 contactos (gtFingers, que gtPoll actualiza
// ##  gratis en cada vuelta), asi que en uso normal a un dedo no
// ##  se anade ni una transaccion I2C.
// #############################################################
#define HPZ_MIN_START_D2 (140 * 140)   // separacion inicial minima para "cerrar"
#define HPZ_MIN_OPEN_D2  (45 * 45)     // separacion inicial minima para "abrir"
#define HPZ_MIN_MS       120
#define HPZ_MAX_MS       700
#define HPZ_ABS_PX       50            // ademas del %, un recorrido absoluto real

static uint8_t  hpzState = 0;          // 0 en reposo · 1 siguiendo · 2 consumido
static int      hpzIdA = -1, hpzIdB = -1;
static int32_t  hpzD2Ref = 0;
static uint32_t hpzT0 = 0;
static bool     hpzSwallow = false;

static bool hpzSwallowing(){ return hpzSwallow; }
static void hpzReset(){ hpzState = 0; hpzIdA = hpzIdB = -1; hpzD2Ref = 0; }
static bool hpzAllowedHome(){
  if(!gHomePinch) return false;
  // drawerCanOpen() ya es la UNICA definicion de "el escritorio esta libre":
  // ST_HOME, sin Modo Edicion, sin Modo PC ni app hospedada, sin kiosco, sin
  // capa OTA y sin panel rapido. Se reutiliza tal cual en vez de repetir aqui
  // la misma lista y arriesgarse a que las dos se separen.
  if(!drawerCanOpen()) return false;
  if(hpDragging || hpSettling) return false;             // hay un gesto de pagina en curso
  if(SUSPEND_ON && gSuspOn) return false;
  if(cronoCardVisible()) return false;                   // tarjeta del cronometro: manda ella
  if(notifDragIdx >= 0) return false;                    // se esta arrastrando una notificacion
  return true;
}
static bool hpzAllowedCfg(){
  return gState == ST_HOMECFG && hcActive && hcView == HCV_PAGES &&
         hcModal == HCM_NONE && !hcAnim && !hcDragging && hcReorder < 0;
}
static void hpzUpdate(){
  bool inHome = hpzAllowedHome(), inCfg = hpzAllowedCfg();
  if(!inHome && !inCfg){ if(hpzState) hpzReset(); hpzSwallow = false; return; }
  uint8_t fingers = (millis() - gtFingersMs > 150) ? 0 : gtFingers;
  if(hpzState == 2){                       // gesto ya resuelto: tragar hasta soltar los dos dedos
    if(fingers == 0){ hpzReset(); hpzSwallow = false; }
    else hpzSwallow = true;
    return;
  }
  if(fingers < 2){ if(hpzState) hpzReset(); hpzSwallow = false; return; }
  int n = gtPollMulti();
  if(n < 2){ hpzReset(); hpzSwallow = false; return; }
  if(n > 2){ hpzState = 2; hpzSwallow = true; return; }   // tercer contacto: cancelacion segura
  int ax = 0, ay = 0, bx = 0, by = 0, ida = -1, idb = -1, cnt = 0;
  for(int i = 0; i < KB_MAXPOINTS; i++){
    if(!gKbPoints[i].active) continue;
    if(cnt == 0){ ax = gKbPoints[i].x; ay = gKbPoints[i].y; ida = gKbPoints[i].id; }
    else if(cnt == 1){ bx = gKbPoints[i].x; by = gKbPoints[i].y; idb = gKbPoints[i].id; }
    cnt++;
  }
  if(cnt < 2){ hpzReset(); hpzSwallow = false; return; }
  int lo = ida < idb ? ida : idb, hi = ida < idb ? idb : ida;   // orden independiente del dedo
  int32_t dx = ax - bx, dy = ay - by, d2 = dx * dx + dy * dy;
  if(hpzState == 0){
    if(d2 < (inHome ? HPZ_MIN_START_D2 : HPZ_MIN_OPEN_D2)){ hpzSwallow = false; return; }
    hpzState = 1; hpzIdA = lo; hpzIdB = hi; hpzD2Ref = d2; hpzT0 = millis();
    hpzSwallow = true;                     // con dos contactos validos ya nadie mas ve el toque
    return;
  }
  if(lo != hpzIdA || hi != hpzIdB){        // cambio de dedos: se rearma sin abrir nada
    hpzIdA = lo; hpzIdB = hi; hpzD2Ref = d2; hpzT0 = millis(); hpzSwallow = true; return;
  }
  hpzSwallow = true;
  // La referencia sigue el EXTREMO del recorrido: dos dedos que solo se apoyan
  // y se asientan no acumulan un falso pellizco.
  if(inHome){ if(d2 > hpzD2Ref){ hpzD2Ref = d2; hpzT0 = millis(); } }
  else      { if(d2 < hpzD2Ref){ hpzD2Ref = d2; hpzT0 = millis(); } }
  uint32_t el = millis() - hpzT0;
  if(el > HPZ_MAX_MS){ hpzState = 2; return; }        // fuera de ventana: se consume, no abre
  if(el < HPZ_MIN_MS) return;
  if(inHome){
    // Cierre: -28 % de distancia (0,72^2 = 0,518 -> 52/100 en cuadrados) Y >= 50 px reales.
    if((int64_t)d2 * 100 <= (int64_t)hpzD2Ref * 52){
      int d = isqrt32(d2), d0 = isqrt32(hpzD2Ref);
      if(d0 - d >= HPZ_ABS_PX){ hpzState = 2; hcEnter(); }
    }
  } else {
    if((int64_t)d2 * 100 >= (int64_t)hpzD2Ref * 190){
      int d = isqrt32(d2), d0 = isqrt32(hpzD2Ref);
      if(d - d0 >= HPZ_ABS_PX){ hpzState = 2; hcBeginExit(); }
    }
  }
}
// ---- Espacio VERDADERAMENTE vacio del escritorio ---------------------------
// Condicion de la pulsacion larga que abre la personalizacion. Excluye iconos,
// widgets, dock, barra de estado, barra de navegacion, la banda fija superior
// y las tarjetas de notificacion visibles.
static bool homeEmptySpaceAt(int px, int py){
  if(py < HOME_BAND_TOP || py >= homeBandBot()) return false;
  int id;
  if(hitHomeIcon(px, py, id)) return false;
  if(homeWgAt(gHomePage, px, py) >= 0) return false;
  if(gNotifCount > 0 && py >= NOTIF_BAND_TOP && py <= NOTIF_BAND_BOT) return false;
  return true;
}

// ORIGEN ALTERNATIVO DE LA ANIMACION. Una app abierta desde la Caja de
// aplicaciones puede no estar en el escritorio (no es favorita) o estar en otra
// ranura; sin esto, la transicion la haria crecer desde un sitio que no es el
// icono que el usuario acaba de tocar. La caja anota aqui el rectangulo REAL
// del icono pulsado y getIconRect lo prefiere, pero SOLO para esa app: asi el
// dato no se queda pegado y afectando a la siguiente apertura desde Inicio.
static int gIconOvrApp = -1, gIconOvrX = 0, gIconOvrY = 0, gIconOvrS = 72;
static void homeTick(){
  if(editMode){ edTick(); return; }
  // TRANSICION EN CURSO: Inicio SIGUE atendiendo el toque (es justo lo que hace
  // que se acepte la app siguiente antes de que la anterior termine de cerrarse),
  // pero NO dibuja: la capa de transicion es la unica duena de la pantalla
  // mientras dura, igual que la cortina o el OTA. Un showHome() aqui volcaria el
  // escritorio entero encima de la tarjeta que esta encogiendo -> parpadeo.
  if(appTrOwnsScreen()){
    if(gHomeDirty && !hpDragging && !hpSettling) renderHome();   // offscreen: no toca la pantalla
  } else if(gHomeDirty && !hpDragging && !hpSettling){
    renderHome(); showHome();
  }
  if(gSafeMode && T.tap && T.x >= 136 && T.x <= 344 && T.y >= 48 && T.y <= 104){ safeEnter(); return; }
  // PAGINAS DEL ESCRITORIO. Va lo PRIMERO de todo (antes incluso que los
  // gestos iOS) porque una vez que el dedo esta arrastrando una pagina, el
  // toque es suyo hasta que se suelte: si el gesto de la barra inferior o
  // el de la Caja de aplicaciones pudieran robarlo a mitad de recorrido, la
  // pagina se quedaria a medias en pantalla.
  if(hpTick()) return;
  if(hpTryStart()){ hpTick(); return; }
  if(gNavMode == 1 && handleiOSGestures()) return;   // gestos iOS antes que los toques normales
  // destello Liquid Glass al posar el dedo sobre un icono (solo estilo "Vidrio")
  if(T.pressed && gIconStyle == 1){
    int rid;
    if(hitHomeIcon(T.x, T.y, rid)){ gRippleActive = true; gRippleX = T.x; gRippleY = T.y; gRippleStart = millis(); }
  }
  // pulsacion larga (>1000 ms sin mover) sobre un icono de la rejilla.
  // FASE 2: ya no salta directo a Modo Edicion -- abre primero el menu
  // contextual. Con CTXMENU_ON en 0 se recupera exactamente el comportamiento
  // anterior (jiggle + agarre del icono bajo el dedo).
  if(T.down && edSlotAt(T.startX, T.startY) >= 0 && (millis() - T.downMs) > 1000
     && abs(T.x - T.startX) < 12 && abs(T.y - T.startY) < 12){
    int slot = edSlotAt(T.startX, T.startY);
    if(homeOrder[edSlot(slot)] == HOME_EMPTY) return;   // hueco de la rejilla: no hay app sobre la que actuar
    if(CTXMENU_ON){ ctxOpen(slot); return; }
    edEnter(); edDrag = slot; edSetDrag(T.x, T.y); return;
  }
  // CAJA DE APLICACIONES: deslizar hacia ARRIBA desde el escritorio. Va
  // DESPUES del long-press y del gesto de la barra iOS, y ANTES del tap, para
  // no pisar a ninguno de los dos:
  //   · T.swipeUp solo lo pone tDoRelease() cuando el dedo recorrio mas de 55
  //     px en vertical, asi que jamas coincide con un tap (que exige <16 px);
  //   · el panel rapido ya se atendio en loop() -- qsGlobalHandle() -- y solo
  //     escucha gestos que EMPIEZAN en el borde superior, por eso aqui se exige
  //     que el dedo arranque por debajo de la barra de estado;
  //   · en modo gestos (gNavMode==1) la franja inferior es de handleiOSGestures,
  //     que se ejecuta arriba y devuelve true si se queda el gesto.
  // PULSACION LARGA EN UN HUECO VACIO (650 ms, tolerancia 12 px) -> Personalizar
  // inicio. Va DESPUES del long-press sobre icono y ANTES del gesto de la Caja
  // de aplicaciones. Son mutuamente excluyentes por construccion:
  // homeEmptySpaceAt() descarta iconos, widgets, dock, barras y tarjetas de
  // notificacion, y se comprueba tanto el punto de INICIO como el actual, asi
  // que un dedo que se haya ido a un icono por el camino no abre nada.
  if(T.down && (millis() - T.downMs) > 650
     && abs(T.x - T.startX) <= 12 && abs(T.y - T.startY) <= 12
     && homeEmptySpaceAt(T.startX, T.startY) && homeEmptySpaceAt(T.x, T.y)){
    hcEnter(); return;
  }
  if(T.swipeUp && drawerCanOpen() && T.startY > 96){ drawerOpen(); return; }
  if(T.tap){
    if(T.x > SCR_W * 2 / 3 && T.y > SCR_H - 72){ sysRecents(); return; }          // boton Recientes
    int id;
    if(hitHomeIcon(T.x, T.y, id)){
      gIconOvrApp = -1;              // se abre desde su ranura real, no desde la caja
      // FASE 3: si la app tiene candado, la verificacion va ANTES de abrirla.
      // Se reutiliza lsuStartVerify (misma UI, mismo contador de fallos, misma
      // espera progresiva de la Fase 1); al acertar, lsuFinishAfter abre la app
      // por el camino normal (enterApp).
      if(APPLOCK_ON && appLockGet(id) && gLockType > 0){ lsuStartVerifyFor(LSU_AFTER_OPENAPP, id); return; }
      enterApp(id);
      return;
    }
    // Widget con accion real: el acceso a Camara abre la app, y el de Clima
    // abre Clima. Los demas son informativos y no fingen ser botones.
    int wi = homeWgAt(gHomePage, T.x, T.y);
    if(wi >= 0 && gHomeWg[gHomePage][wi].type == WG_CAM){   gIconOvrApp = -1; enterApp(IC_CAMARA); return; }
    if(wi >= 0 && gHomeWg[gHomePage][wi].type == WG_CLIMA){ gIconOvrApp = -1; enterApp(IC_CLIMA);  return; }
    // Widgets fijos de la franja superior: cada tarjeta abre su app real.
    int fixedApp = homeFixedWidgetAppAt(T.x, T.y);
    if(fixedApp >= 0){ gIconOvrApp = -1; enterApp(fixedApp); return; }
  }
}
