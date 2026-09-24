// #############################################################
// ##  FLEX OS ULTRA  ·  GALERIA  ·  la biblioteca visual real
// ##  ----------------------------------------------------------
// ##  Fotos, videos y dibujos de la biblioteca de medios, con sus
// ##  miniaturas persistentes, bloqueo con la clave del sistema,
// ##  seleccion multiple y apertura en el visor de Multimedia.
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
#include "FlexOS_Ultra_WebServer.h"   // eslabon anterior de la cadena

// #############################################################
// ##  GALERIA  ·  QUE ENSENA Y DE DONDE LO SACA
// ##  ------------------------------------------------------
// ##  De la BIBLIOTECA DE MEDIOS (FlexOS_Ultra_MediaLib.h): el mismo
// ##  catalogo que usan Multimedia, Musica y Flex Web Server. Una foto
// ##  que sube el movil aparece aqui en cuanto el P4 la ha comprobado,
// ##  y lo que se borra o se bloquea aqui deja de verse alli.
// ##
// ##  Fotos, videos y dibujos de Paint. El AUDIO no: va a Musica.
// ##
// ##  MINIATURAS. Cada celda pinta la miniatura PERSISTENTE (un JPEG
// ##  cuadrado de 132 px que hizo la tarea de fondo). Decodificarla
// ##  cuesta unos pocos milisegundos, asi que se hacen varias por
// ##  repintado sin frenar la interfaz. La foto grande no se abre aqui
// ##  nunca: antes se decodificaban JPEG de hasta 768 KB en este bucle.
// ##
// ##  PROTEGIDOS. Solo un candado: ni miniatura, ni nombre, ni la
// ##  primera imagen de un video. Abrirlos, desbloquearlos o borrarlos
// ##  pide la clave del sistema (la misma de la pantalla de bloqueo).
// #############################################################
#define GAL_COLS          3
#define GAL_THUMB_BUDGET  6        // miniaturas persistentes nuevas por repintado
#define GAL_REFRESH_MS    300      // cambios del catalogo: como mucho un repintado cada tanto

#define GAL_TABS 3
static const char* GAL_TAB_NAME[GAL_TABS] = { "Todas", "Fotos", "V\xC3\xAD" "deos" };
static int galTab = 0;
static uint32_t galTabMask(){
  switch(galTab){
    case 1: return FML_MASK(FML_K_PHOTO) | FML_MASK(FML_K_DRAW);
    case 2: return FML_MASK(FML_K_VIDEO);
  }
  return FML_MASK_VISUAL;
}

// ---- Vista del catalogo (indices dentro de gMs.lib.recs) ----
// Solo vale con el cerrojo del catalogo tomado: la tarea de fondo o el
// servidor pueden quitar registros y mover los de detras.
static uint16_t   galViewStore[FML_CAP];
static FlexMlView galView;
static uint32_t   galViewMask = 0;
static bool       galViewReady = false;

static void galSyncLocked(){
  uint32_t m = galTabMask();
  if(!galViewReady || galViewMask != m){
    flexMlViewInit(&galView, galViewStore, FML_CAP, m, FML_SORT_NEWEST);
    galViewMask = m; galViewReady = true;
    flexMlViewSync(&galView, &gMs.lib, true);
  } else flexMlViewSync(&galView, &gMs.lib, false);
}
static const FlexMlRec* galRecLocked(int nth){
  return (nth >= 0 && nth < galView.n) ? &gMs.lib.recs[galView.idx[nth]] : NULL;
}

// ---- Estado de la rejilla ----
static int      galScroll = 0;
static int      galDragY0 = 0, galDragS0 = 0;
static bool     galDragging = false, galLongFired = false;
static bool     galMorePending = false;
static uint32_t galSeenRev = 0, galSeenMs = 0;
static uint32_t galMenuId = 0;          // elemento sobre el que se abrio el menu (0 = menu de la app)
static uint32_t galResumeId = 0;
static int      galCountCache = 0;      // para la geometria del desplazamiento (sin cerrojo)
static void galRender();

// ---- Seleccion multiple: por ID (no por posicion) ----
// Un id sigue siendo el mismo aunque la lista se reordene porque llego
// una foto nueva del movil. Una posicion no.
static bool     galMulti = false;
static uint32_t galSel[FML_CAP];
static uint16_t galSelN = 0;
static bool galIsSel(uint32_t id){ for(int k = 0; k < galSelN; k++) if(galSel[k] == id) return true; return false; }
static void galToggleSel(uint32_t id){
  for(int k = 0; k < galSelN; k++) if(galSel[k] == id){ galSel[k] = galSel[--galSelN]; return; }
  if(galSelN < FML_CAP) galSel[galSelN++] = id;
}
static void galClearSel(){ galSelN = 0; }

// Dialogos propios (el menu y el aviso son los del kit de medios).
enum { GDLG_NONE = 0, GDLG_NOLOCK, GDLG_INFO };
static int galDlgKind = GDLG_NONE;
static int galAskAct = MA_NONE;          // que confirma fkAsk
static uint32_t galRenameId = 0;

// ---- Geometria ----
static int galHeadH(){ return 104; }
static void galCellRect(int i, int &x, int &y, int &w, int &h){
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  int pad = uiPad(), gap = uiGap();
  w = (bw - 2 * pad - (GAL_COLS - 1) * gap) / GAL_COLS;
  h = w;
  int c = i % GAL_COLS, r = i / GAL_COLS;
  x = bx + pad + c * (w + gap);
  y = by + galHeadH() + r * (h + 26) - galScroll;
}
static int galMaxScroll(){
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  int x, y, w, h; galCellRect(0, x, y, w, h);
  int rows = (galCountCache + GAL_COLS - 1) / GAL_COLS;
  int need = galHeadH() + rows * (h + 26) + (galMulti ? 110 : 40);
  int m = need - bh;
  return m > 0 ? m : 0;
}
static int galMultiBarY(){ int bx, by, bw, bh; uiBox(bx, by, bw, bh); return by + bh - 96; }

// ---- Dibujo de una celda ----
static void galVideoBadge(int x, int y, int w, int h, uint32_t durMs){
  int cx = x + 18, cy = y + 18;
  fillCircle(cx, cy, 12, rgb565(0, 0, 0));
  fillTriangle(cx - 4, cy - 6, cx - 4, cy + 6, cx + 6, cy, rgb565(255, 255, 255));
  if(durMs){
    char d[16]; mlFmtDur(durMs, d, sizeof(d));
    int tw = textW(d, 1) + 12;
    fillRoundRect(x + w - tw - 6, y + h - 24, tw, 18, 9, rgb565(0, 0, 0));
    drawText(x + w - tw, y + h - 21, d, 1, rgb565(255, 255, 255));
  }
}

static void galDrawCell(const FlexMlRec* r, int x, int y, int w, int h, int &budget){
  int rad = w / 10; if(rad < 3) rad = 3;
  if(r->flags & FML_R_LOCKED){
    // Nada del contenido: ni miniatura, ni nombre, ni duracion.
    if(uiGlass) drawGlassCardFlat(x, y, w, h, rad, TH_GLASS, WIN_BG);
    else        fillRoundRect(x, y, w, h, rad, TH_SURF2);
    mlPadlock(x + w / 2, y + h / 2 - 10, w / 10 + 5, TH_TXT2, false);
    drawTextC(x + w / 2, y + h - 30, "Protegido", 1, TH_TXT2);
    return;
  }
  fillRoundRect(x, y, w, h, rad, TH_SURF2);
  bool more = false;
  const uint16_t* thumb = mlThumbGetLocked(r, &budget, &more);
  if(more) galMorePending = true;
  if(thumb){
    int ox0 = gClipX0, ox1 = gClipX1, oy0 = gClipY0, oy1 = gClipY1;
    if(gClipX0 < x) gClipX0 = x;
    if(gClipX1 > x + w - 1) gClipX1 = x + w - 1;
    if(gClipY0 < y) gClipY0 = y;
    if(gClipY1 > y + h - 1) gClipY1 = y + h - 1;
    mlBlitThumb(thumb, x, y, w, h, rad, WIN_BG);
    gClipX0 = ox0; gClipX1 = ox1; gClipY0 = oy0; gClipY1 = oy1;
  } else {
    // Sin miniatura todavia (o sin posibilidad): la clase y el motivo, nunca
    // una imagen inventada.
    const char* tag = r->kind == FML_K_VIDEO ? "V\xC3\xAD" "DEO" : r->kind == FML_K_DRAW ? "DIBUJO" : flexMlFmtName(r->fmt);
    drawTextC(x + w / 2, y + h / 2 - 12, tag, 2, rgb565(150, 156, 170));
    const char* why = (r->flags & FML_R_NEED_THUMB) ? "Preparando\xE2\x80\xA6"
                    : (r->state == FML_S_ERROR) ? "Archivo da\xC3\xB1" "ado" : "sin vista previa";
    drawTextC(x + w / 2, y + h / 2 + 12, why, 1, rgb565(120, 124, 140));
  }
  if(r->kind == FML_K_VIDEO) galVideoBadge(x, y, w, h, r->durMs);
  if(!(r->flags & FML_R_PLAYABLE) && !(r->flags & FML_R_NEED_THUMB)){
    // Se guarda y se descarga, pero esta placa no lo abre: se dice.
    const char* b = r->state == FML_S_ERROR ? "Da\xC3\xB1" "ado" : "Solo guardar";
    int tw = textW(b, 1) + 12;
    fillRoundRect(x + w - tw - 6, y + 6, tw, 18, 9, rgb565(242, 177, 75));
    drawText(x + w - tw, y + 9, b, 1, rgb565(35, 26, 0));
  }
}

static void galRenderGrid(){
  setBuf(fb);
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  fillRect(bx, by, bw, bh, WIN_BG);
  int pad = uiPad();
  galMorePending = false;
  mlThumbNewPass();
  int budget = GAL_THUMB_BUDGET;

  drawText(bx + pad, by + 14, "Galer\xC3\xAD" "a", 4, TH_TXT);
  for(int i = 0; i < 3; i++) fillCircle(bx + bw - pad - 4, by + 22 + i * 12, 4, TH_NAV);

  if(!gMlOk){
    const char* t = !flexFsReady() ? "Sin almacenamiento" : gSafeMode ? "Modo seguro" : "Biblioteca no disponible";
    const char* s = !flexFsReady() ? flexFsError() : gSafeMode ? "La biblioteca no se abre hasta reiniciar con normalidad"
                                                              : "No hubo memoria para el cat\xC3\xA1logo";
    drawTextC(bx + bw / 2, by + bh / 2 - 20, t, 3, TH_TXT2);
    drawTextC(bx + bw / 2, by + bh / 2 + 16, s, 1, TH_MUTE);
    galCountCache = 0;
    flxFlush(WIN_TOP, WIN_BOT);
    return;
  }

  mlLock();
  galSyncLocked();
  const int n = galView.n;
  galCountCache = n;
  galSeenRev = gMs.lib.rev; galSeenMs = millis();
  // Quita de la seleccion lo que ya no existe (lo borro el movil o la web).
  for(int k = 0; k < galSelN; ){
    if(flexMlViewFindId(&galView, &gMs.lib, galSel[k]) < 0) galSel[k] = galSel[--galSelN]; else k++;
  }

  { char cnt[48];
    if(galMulti) snprintf(cnt, sizeof(cnt), "%u seleccionado%s", (unsigned)galSelN, galSelN == 1 ? "" : "s");
    else snprintf(cnt, sizeof(cnt), "%d elemento%s", n, n == 1 ? "" : "s");
    drawText(bx + pad, by + 52, cnt, 1, galMulti ? TH_PRIM : TH_TXT2); }

  // ---- Pestanas ----
  const int tabY = by + 68, tw = (bw - 2 * pad) / GAL_TABS;
  for(int i = 0; i < GAL_TABS; i++){
    int tx = bx + pad + i * tw;
    if(i == galTab) fillRoundRect(tx + 2, tabY, tw - 4, 28, 14, TH_PRIM);
    drawTextC(tx + tw / 2, tabY + 8, GAL_TAB_NAME[i], 1, i == galTab ? TH_ONACC : TH_TXT2);
  }

  // ---- Estado REAL del catalogo ----
  const char* st = NULL; char stb[64];
  if(gMs.scanning) st = "Buscando archivos\xE2\x80\xA6";
  else if(gMs.pending){ snprintf(stb, sizeof(stb), "Preparando miniaturas (%u)\xE2\x80\xA6", (unsigned)gMs.pending); st = stb; }
  else if(gMs.lib.n >= FML_CAP) st = "La biblioteca est\xC3\xA1 llena: hay archivos que no caben";
  if(st) drawTextR(bx + bw - pad - 18, by + 52, st, 1, gMs.lib.n >= FML_CAP ? TH_WARN : TH_TXT2);

  if(n == 0){
    drawTextC(bx + bw / 2, by + bh / 2 - 30, "No hay nada aqu\xC3\xAD", 3, TH_TXT2);
    drawTextC(bx + bw / 2, by + bh / 2 + 6, "Sube fotos y v\xC3\xAD" "deos desde el m\xC3\xB3vil:", 1, TH_MUTE);
    drawTextC(bx + bw / 2, by + bh / 2 + 24, "men\xC3\xBA \xE2\x8B\xAE \xE2\x80\xBA Conectar con el m\xC3\xB3vil", 1, TH_MUTE);
  }

  for(int i = 0; i < n; i++){
    int x, y, w, h; galCellRect(i, x, y, w, h);
    // SOLO LAS VISIBLES: una galeria de 300 fotos cuesta lo mismo que una de 9.
    if(y + h < by + galHeadH() - 40 || y > by + bh) continue;
    const FlexMlRec* r = galRecLocked(i);
    int ox0 = gClipX0, ox1 = gClipX1, oy0 = gClipY0, oy1 = gClipY1;
    gClipY0 = by + galHeadH() - 6 > oy0 ? by + galHeadH() - 6 : oy0;
    galDrawCell(r, x, y, w, h, budget);
    gClipX0 = ox0; gClipX1 = ox1; gClipY0 = oy0; gClipY1 = oy1;
    if(galMulti){
      bool sel = galIsSel(r->id);
      int rad = w / 10; if(rad < 3) rad = 3;
      if(sel){ drawRoundRect(x, y, w, h, rad, TH_PRIM); drawRoundRect(x + 1, y + 1, w - 2, h - 2, rad, TH_PRIM); }
      int cx = x + w - 16, cy = y + h - 16;
      fillCircle(cx, cy, 11, sel ? TH_PRIM : rgb565(0, 0, 0));
      drawCircle(cx, cy, 11, rgb565(255, 255, 255));
      if(sel){
        strokeSegAA(cx - 5, cy, cx - 1, cy + 5, 2.0f, TH_ONACC);
        strokeSegAA(cx - 1, cy + 5, cx + 6, cy - 5, 2.0f, TH_ONACC);
      }
    }
    if(!(r->flags & FML_R_LOCKED)){
      char nm[FML_NAME_MAX]; flexMlDisplayName(r, nm, sizeof(nm));
      drawTextClip(x, y + h + 4, nm, 1, TH_TXT2, x + w);
    }
  }
  // Recuento de la barra de seleccion, dentro del cerrojo (necesita la vista).
  int selLocked = 0, selOpen = 0, selectable = 0;
  if(galMulti){
    for(int k = 0; k < galSelN; k++){
      int i = flexMlFindId(&gMs.lib, galSel[k]);
      if(i >= 0 && (gMs.lib.recs[i].flags & FML_R_LOCKED)) selLocked++; else selOpen++;
    }
    for(int i = 0; i < n; i++) if(!(galRecLocked(i)->flags & FML_R_LOCKED)) selectable++;
  }
  mlUnlock();

  if(galMulti){
    // Barra de acciones: lo que se puede hacer con LO SELECCIONADO.
    int aby = galMultiBarY();
    if(uiGlass) drawLiquidGlassPanel(bx + 10, aby, bw - 20, 84, 20, TH_GLASS2);
    else        fillRoundRect(bx + 10, aby, bw - 20, 84, 20, TH_SURF2);
    bool all = selectable > 0 && selOpen == selectable && !selLocked;
    fillRoundRect(bx + 24, aby + 10, 150, 28, 14, TH_SURF);
    drawTextC(bx + 24 + 75, aby + 17, all ? "Quitar selecci\xC3\xB3n" : "Seleccionar todo", 1, TH_TXT);
    drawTextR(bx + bw - 28, aby + 17, "Salir", 2, TH_TXT2);
    int ay = aby + 46, slots = 3, sw = (bw - 40) / slots;
    const char* lb[3] = { selLocked && !selOpen ? "Desbloquear" : "Bloquear", "Papelera", "Borrar" };
    uint16_t col[3] = { TH_TXT, selLocked ? TH_MUTE : TH_TXT, rgb565(228, 70, 70) };
    for(int k = 0; k < slots; k++){
      bool on = galSelN > 0 && !(k == 1 && selLocked);
      drawTextC(bx + 20 + k * sw + sw / 2, ay + 6, lb[k], 2, on ? col[k] : TH_MUTE);
    }
  }
  if(mmOn){ mmDraw(1.0f); mmAnimDone = true; }
  if(mmDlgOn) mmDlgDraw();
  if(fkAskOn) fkAskDraw();
  flxFlush(WIN_TOP, WIN_BOT);
}

static void galRender(){
  if(webSheetOn){ webSheetRender(); return; }     // la hoja del servidor manda mientras esta abierta
  galRenderGrid();
}

// -------------------------------------------------------------
//  ABRIR UN ELEMENTO
//  ------------------------------------------------------------
//  Se lo lleva el visor de Multimedia, el unico del sistema. A donde se
//  vuelve lo decide quien abre (gMediaReturnApp): esta misma funcion la
//  usa el Explorador.
// -------------------------------------------------------------
static void galOpenPath(const char* path){
  if(!path || !path[0]) return;
  mediaOpenInPlayer(path);
}
static void galOpenId(uint32_t id){
  FlexMlRec r;
  if(!mlGet(id, &r)) return;
  galResumeId = id;
  gMediaReturnApp = IC_GALERIA;
  galOpenPath(r.path);
}

// ---- Acciones con la clave ya comprobada ----
static void galAuthDone(bool ok, int act, const uint32_t* ids, int n){
  if(!ok){ galRender(); return; }
  int done = 0;
  char why[64] = "";
  if(act == MA_OPEN && n == 1){ galOpenId(ids[0]); return; }
  for(int k = 0; k < n; k++){
    if(act == MA_LOCK)        done += mlSetLock(ids[k], true, why, sizeof(why)) ? 1 : 0;
    else if(act == MA_UNLOCK) done += mlSetLock(ids[k], false, why, sizeof(why)) ? 1 : 0;
    else if(act == MA_DELETE) done += mlDelete(ids[k]) ? 1 : 0;
  }
  galMulti = false; galClearSel();
  char msg[64];
  const char* verb = act == MA_LOCK ? "bloqueado" : act == MA_UNLOCK ? "desbloqueado" : "borrado";
  snprintf(msg, sizeof(msg), "%d elemento%s %s%s", done, done == 1 ? "" : "s", verb, done == 1 ? "" : "s");
  sysNotify("Galer\xC3\xAD" "a", done < n && why[0] ? why : msg);
  galRender();
}

// Ids (de la seleccion o del elemento del menu) sobre los que actuar.
static int galTargets(uint32_t* out, bool* anyLocked, bool* anyOpen){
  int n = 0;
  *anyLocked = *anyOpen = false;
  mlLock();
  if(galMulti){
    for(int k = 0; k < galSelN; k++){
      int i = flexMlFindId(&gMs.lib, galSel[k]);
      if(i < 0) continue;
      out[n++] = galSel[k];
      if(gMs.lib.recs[i].flags & FML_R_LOCKED) *anyLocked = true; else *anyOpen = true;
    }
  } else if(galMenuId){
    int i = flexMlFindId(&gMs.lib, galMenuId);
    if(i >= 0){ out[n++] = galMenuId; if(gMs.lib.recs[i].flags & FML_R_LOCKED) *anyLocked = true; else *anyOpen = true; }
  }
  mlUnlock();
  return n;
}

static uint32_t galTmpIds[FML_CAP];

static void galDoAction(int act){
  bool anyLocked = false, anyOpen = false;
  if(act == MA_CONNECT){ webSheetBack = galRender; webSheetOpen(); return; }
  if(act == MA_TRASHBIN){ fkTrashOpen(); return; }
  if(act == MA_SELECT){
    galMulti = true; galClearSel();
    if(galMenuId) galToggleSel(galMenuId);
    galRender(); return;
  }
  int n = galTargets(galTmpIds, &anyLocked, &anyOpen);
  if(n == 0){ galRender(); return; }
  switch(act){
    case MA_LOCK: {
      if(gLockType == 0){ galDlgKind = GDLG_NOLOCK; galRender(); mediaNoLockDialog(); return; }
      // Solo lo que no estaba bloqueado.
      int m = 0;
      for(int k = 0; k < n; k++){ FlexMlRec r; if(mlGet(galTmpIds[k], &r) && !(r.flags & FML_R_LOCKED)) galTmpIds[m++] = galTmpIds[k]; }
      mediaAuthRequest(MA_LOCK, galTmpIds, m, galAuthDone);
      return;
    }
    case MA_UNLOCK: {
      int m = 0;
      for(int k = 0; k < n; k++){ FlexMlRec r; if(mlGet(galTmpIds[k], &r) && (r.flags & FML_R_LOCKED)) galTmpIds[m++] = galTmpIds[k]; }
      mediaAuthRequest(MA_UNLOCK, galTmpIds, m, galAuthDone);
      return;
    }
    case MA_TRASH: {
      if(anyLocked){ sysNotify("Galer\xC3\xAD" "a", "Lo protegido no va a la papelera: usa Borrar"); galRender(); return; }
      int done = 0;
      for(int k = 0; k < n; k++) done += mlTrash(galTmpIds[k]) ? 1 : 0;
      galMulti = false; galClearSel();
      char msg[48]; snprintf(msg, sizeof(msg), "%d a la papelera", done);
      sysNotify("Galer\xC3\xAD" "a", msg);
      galRender();
      return;
    }
    case MA_DELETE: {
      char sub[64];
      if(n == 1 && !anyLocked){
        FlexMlRec r; mlGet(galTmpIds[0], &r);
        flexMlDisplayName(&r, sub, sizeof(sub));
      } else snprintf(sub, sizeof(sub), "%d elemento%s", n, n == 1 ? "" : "s");
      galAskAct = MA_DELETE;
      galRender();
      fkAskOpen("\xC2\xBF" "Borrar definitivamente?", sub);
      return;
    }
    case MA_RENAME: {
      if(anyLocked || n != 1){ galRender(); return; }
      FlexMlRec r; mlGet(galTmpIds[0], &r);
      char stem[FLEXFS_NAME_MAX]; flexFsStem(r.name[0] ? r.name : r.path, stem, sizeof(stem));
      galRenameId = galTmpIds[0];
      fkNameOpen("Renombrar", stem);
      return;
    }
  }
  galRender();
}

// Menu de un elemento: SOLO las acciones que se pueden hacer con el.
static void galOpenItemMenu(uint32_t id, int ax, int ay){
  FlexMlRec r;
  if(!mlGet(id, &r)) return;
  galMenuId = id;
  uint8_t a[MM_MAX]; int n = 0;
  bool locked = (r.flags & FML_R_LOCKED) != 0;
  a[n++] = MA_SELECT;
  a[n++] = locked ? MA_UNLOCK : MA_LOCK;
  if(!locked){ a[n++] = MA_RENAME; a[n++] = MA_TRASH; }
  a[n++] = MA_DELETE;
  mmOpen(ax, ay, a, n);
}
static void galOpenAppMenu(){
  galMenuId = 0;
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  uint8_t a[3] = { MA_CONNECT, MA_SELECT, MA_TRASHBIN };
  mmOpen(bx + bw - MM_W / 2 - 16, by + 50, a, 3);
}

// Toque sobre la rejilla -> elemento (o 0).
static uint32_t galHitId(int tx, int ty){
  uint32_t id = 0;
  mlLock();
  galSyncLocked();
  for(int i = 0; i < galView.n && !id; i++){
    int x, y, w, h; galCellRect(i, x, y, w, h);
    if(tx >= x && tx <= x + w && ty >= y && ty <= y + h) id = galRecLocked(i)->id;
  }
  mlUnlock();
  return id;
}

static void galSelectAllToggle(){
  mlLock();
  galSyncLocked();
  int selectable = 0, have = 0;
  for(int i = 0; i < galView.n; i++){
    const FlexMlRec* r = galRecLocked(i);
    if(r->flags & FML_R_LOCKED) continue;             // lo protegido nunca entra en "todo"
    selectable++;
    if(galIsSel(r->id)) have++;
  }
  bool all = selectable > 0 && have == selectable;
  galClearSel();
  if(!all) for(int i = 0; i < galView.n; i++){
    const FlexMlRec* r = galRecLocked(i);
    if(!(r->flags & FML_R_LOCKED)) galSel[galSelN++] = r->id;
  }
  mlUnlock();
  galRender();
}

static void galTick(){
  if(webSheetTick()) return;                      // "Conectar con el movil" abierta
  // --- Capas modales, de arriba abajo ---
  if(fkTrashOn){ if(!fkTrashTick()){ mlRequestScan(); galRender(); } return; }
  if(fkNameOn){
    int r = fkNameTick();
    if(r == 1 && galRenameId){
      char why[64];
      if(!mlRename(galRenameId, fkNameBuf, why, sizeof(why))) sysNotify("Galer\xC3\xAD" "a", why);
    }
    if(r != 0){ galRenameId = 0; galRender(); }
    return;
  }
  if(fkAskOn){
    int r = fkAskTick();
    if(r == 1 && galAskAct == MA_DELETE){
      bool anyLocked = false, anyOpen = false;
      int n = galTargets(galTmpIds, &anyLocked, &anyOpen);
      galAskAct = MA_NONE;
      // Borrar algo protegido pide la clave; lo demas se borra ya.
      if(anyLocked){ mediaAuthRequest(MA_DELETE, galTmpIds, n, galAuthDone); return; }
      galAuthDone(true, MA_DELETE, galTmpIds, n);
      return;
    }
    if(r != 0){ galAskAct = MA_NONE; galRender(); }
    return;
  }
  if(mmDlgOn){
    int r = mmDlgTick();
    if(r != 0){
      int kind = galDlgKind; galDlgKind = GDLG_NONE;
      if(r == 1 && kind == GDLG_NOLOCK){ settingsJumpSecurity(); return; }
      galRender();
    }
    return;
  }
  if(mmOn){
    mmAnimTick();
    if(T.tap){
      int a = mmHit(T.x, T.y);
      if(a == 0) return;                                 // dentro, entre filas
      mmOn = false;
      if(a > 0) galDoAction(a); else galRender();
    }
    return;
  }

  // --- El catalogo cambio (subida del movil, miniatura lista, recorrido) ---
  if(gMlOk && !galDragging && millis() - galSeenMs >= GAL_REFRESH_MS && mlRev() != galSeenRev){ galRender(); return; }
  if(galMorePending && !T.down){ galRender(); return; }

  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  int pad = uiPad();

  // --- Desplazamiento ---
  int maxS = galMaxScroll();
  if(T.pressed){ galDragY0 = T.y; galDragS0 = galScroll; galDragging = false; }
  if(T.down && maxS > 0){
    if(!galDragging && abs(T.y - galDragY0) > 10) galDragging = true;
    if(galDragging){
      int ns = galDragS0 + (galDragY0 - T.y);
      if(ns < 0) ns = 0;
      if(ns > maxS) ns = maxS;
      if(ns != galScroll){ galScroll = ns; galRender(); }
      return;
    }
  }

  // --- Pulsacion larga: menu del elemento (no activa la seleccion sola) ---
  if(!gHosted && !gLand && T.down && !galLongFired && (millis() - T.downMs) > 550
     && abs(T.x - T.startX) < 14 && abs(T.y - T.startY) < 14 && T.startY > by + galHeadH() - 6){
    galLongFired = true;
    uint32_t id = galHitId(T.startX, T.startY);
    if(id && !galMulti){ galOpenItemMenu(id, T.x, T.y + 10); return; }
    if(id && galMulti){ galToggleSel(id); galRender(); return; }
  }
  if(!T.down) galLongFired = false;
  if(!T.tap) return;
  if(galDragging){ galDragging = false; return; }

  // --- Barra de seleccion ---
  if(galMulti){
    int aby = galMultiBarY();
    if(T.y >= aby && T.y <= aby + 84){
      if(T.y < aby + 42){
        if(T.x < bx + 180){ galSelectAllToggle(); return; }
        if(T.x > bx + bw - 100){ galMulti = false; galClearSel(); galRender(); return; }
        return;
      }
      int sw = (bw - 40) / 3, k = (T.x - bx - 20) / (sw > 0 ? sw : 1);
      if(galSelN == 0 || k < 0 || k > 2) return;
      galMenuId = 0;
      if(k == 0){
        bool anyLocked = false, anyOpen = false;
        galTargets(galTmpIds, &anyLocked, &anyOpen);
        galDoAction(anyOpen ? MA_LOCK : MA_UNLOCK);
      } else if(k == 1) galDoAction(MA_TRASH);
      else galDoAction(MA_DELETE);
      return;
    }
  }
  // --- Menu de la app ---
  if(T.x > bx + bw - pad - 30 && T.y < by + 60){ galOpenAppMenu(); return; }
  // --- Pestanas ---
  const int tabY = by + 68, tw = (bw - 2 * pad) / GAL_TABS;
  if(T.y >= tabY && T.y <= tabY + 28){
    int k = (T.x - bx - pad) / (tw > 0 ? tw : 1);
    if(k >= 0 && k < GAL_TABS && k != galTab){ galTab = k; galScroll = 0; galRender(); }
    return;
  }
  // --- Toque sobre un elemento ---
  uint32_t id = galHitId(T.x, T.y);
  if(!id) return;
  if(galMulti){ galToggleSel(id); galRender(); return; }
  FlexMlRec r;
  if(!mlGet(id, &r)) return;
  if(r.flags & FML_R_LOCKED){ mediaAuthRequest(MA_OPEN, &id, 1, galAuthDone); return; }
  galOpenId(id);
}

static void galEnter(){
  // gRelayout = "re-dibuja con la geometria nueva", no "empieza de cero".
  if(!gRelayout){
    galScroll = 0; galMulti = false; galClearSel();
    mmOn = false; mmDlgOn = false; fkNameOn = false; fkAskOn = false; fkTrashOn = false; fkMenuOn = false;
    galDlgKind = GDLG_NONE; galAskAct = MA_NONE;
    if(gMlOk) mlRequestScan();            // lo copiado por otras vias aparece al entrar
  }
  galRender();
}

// ATRAS deshace primero las capas propias de la Galeria.
static bool galBackLayer(){
  if(webSheetOn){ webSheetClose(); return true; }
  if(mmOn || mmDlgOn || fkNameOn || fkAskOn || fkTrashOn){
    mmOn = false; mmDlgOn = false; fkNameOn = false; fkAskOn = false; fkTrashOn = false;
    galDlgKind = GDLG_NONE; galAskAct = MA_NONE;
    galRender();
    return true;
  }
  if(galMulti){ galMulti = false; galClearSel(); galRender(); return true; }
  return false;
}
static bool galBackScreen(){ return false; }

static void galSuspend(){
  galDragging = false; galLongFired = false;
  webSheetOn = false;                             // la hoja es una capa; el servidor sigue
  // El kit de archivos y el de medios son globales: no pueden quedar
  // modales mientras otra app los usa.
  mmOn = false; mmDlgOn = false; fkNameOn = false; fkAskOn = false; fkTrashOn = false; fkMenuOn = false;
  galDlgKind = GDLG_NONE; galAskAct = MA_NONE;
  galMulti = false; galClearSel();
}

static void galResume(){
  int maxS = galMaxScroll();
  if(galScroll < 0) galScroll = 0;
  if(galScroll > maxS) galScroll = maxS;
  galDragging = false; galLongFired = false;
  galRender();
}

// SOLTAR SIN CERRAR: las miniaturas decodificadas se rehacen solas.
static size_t galShed(){ return mlThumbDropAll(); }

static void galCloseApp(){
  mlThumbDropAll();
  galScroll = 0; galMulti = false; galClearSel();
}
