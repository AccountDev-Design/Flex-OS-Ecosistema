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
static uint32_t galResumeId = 0;
static int      galCountCache = 0;      // para la geometria del desplazamiento (sin cerrojo)
static void galRender();

// Seleccion, menus y acciones: los del kit de listas de medios (mk*), los
// MISMOS que Multimedia y Musica.
static void galOpenId(uint32_t id);
static const MediaListApp GAL_APP = { "Galer\xC3\xAD" "a", galRender, galOpenId, NULL };

// ---- Geometria ----
// La cabecera del sistema ya dice "Galeria" (con su flecha de volver): aqui
// solo el recuento con el menu, y las pestanas.
static int galHeadH(){ return 76; }
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
  int need = galHeadH() + rows * (h + 26) + (mkMulti ? MKB_H + 26 : 40);
  int m = need - bh;
  return m > 0 ? m : 0;
}

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
    const char* why = (r->flags & FML_R_NEED_THUMB) ? "Preparando..."
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

  for(int i = 0; i < 3; i++) fillCircle(bx + bw - pad - 4, by + 8 + i * 10, 3, TH_NAV);

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
  mkPruneLocked(&galView);

  { char cnt[48];
    if(mkMulti) snprintf(cnt, sizeof(cnt), "%u seleccionado%s", (unsigned)mkSelN, mkSelN == 1 ? "" : "s");
    else snprintf(cnt, sizeof(cnt), "%d elemento%s", n, n == 1 ? "" : "s");
    drawText(bx + pad, by + 12, cnt, 1, mkMulti ? TH_PRIM : TH_TXT2); }

  // ---- Pestanas ----
  const int tabY = by + 38, tw = (bw - 2 * pad) / GAL_TABS;
  for(int i = 0; i < GAL_TABS; i++){
    int tx = bx + pad + i * tw;
    if(i == galTab) fillRoundRect(tx + 2, tabY, tw - 4, 28, 14, TH_PRIM);
    drawTextC(tx + tw / 2, tabY + 8, GAL_TAB_NAME[i], 1, i == galTab ? TH_ONACC : TH_TXT2);
  }

  // ---- Estado REAL del catalogo ----
  const char* st = NULL; char stb[64];
  if(gMs.scanning) st = "Buscando archivos...";
  else if(gMs.pending){ snprintf(stb, sizeof(stb), "Preparando miniaturas (%u)...", (unsigned)gMs.pending); st = stb; }
  else if(gMs.lib.n >= FML_CAP) st = "La biblioteca est\xC3\xA1 llena: hay archivos que no caben";
  if(st) drawTextR(bx + bw - pad - 22, by + 12, st, 1, gMs.lib.n >= FML_CAP ? TH_WARN : TH_TXT2);

  if(n == 0){
    drawTextC(bx + bw / 2, by + bh / 2 - 30, "No hay nada aqu\xC3\xAD", 3, TH_TXT2);
    drawTextC(bx + bw / 2, by + bh / 2 + 6, "Sube fotos y v\xC3\xAD" "deos desde el m\xC3\xB3vil:", 1, TH_MUTE);
    drawTextC(bx + bw / 2, by + bh / 2 + 24, "men\xC3\xBA > Conectar con el m\xC3\xB3vil", 1, TH_MUTE);
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
    if(mkMulti){
      bool sel = mkIsSel(r->id);
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
  if(mkMulti) mkCountLocked(&galView, &selLocked, &selOpen, &selectable);
  mlUnlock();

  if(mkMulti) mkDrawBar(selLocked, selOpen, selectable);
  mkDrawOverlays();
  flxFlush(WIN_TOP, WIN_BOT);
}

static void galRender(){
  if(webSheetIsOpen()){ webSheetRender(); return; }   // la hoja del servidor manda mientras esta abierta
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

static void galSelectAll(){
  mlLock();
  galSyncLocked();
  mkSelectAllLocked(&galView);
  mlUnlock();
  galRender();
}

static void galTick(){
  if(mkTick()) return;                            // menu, dialogos, papelera, hoja del servidor

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
    if(id && !mkMulti){ mkOpenItemMenu(id, T.x, T.y + 10, NULL, 0); return; }
    if(id && mkMulti){ mkToggle(id); galRender(); return; }
  }
  if(!T.down) galLongFired = false;
  if(!T.tap) return;
  if(galDragging){ galDragging = false; return; }

  // --- Barra de seleccion ---
  if(mkMulti && mkBarTouch(galSelectAll)) return;
  // --- Menu de la app ---
  if(T.x > bx + bw - pad - 40 && T.y < by + 34){
    static const uint8_t acts[3] = { MA_CONNECT, MA_SELECT, MA_TRASHBIN };
    mkOpenAppMenu(bx + bw - MM_W / 2 - 16, by + 34, acts, 3);
    return;
  }
  // --- Pestanas ---
  const int tabY = by + 38, tw = (bw - 2 * pad) / GAL_TABS;
  if(T.y >= tabY && T.y <= tabY + 28){
    int k = (T.x - bx - pad) / (tw > 0 ? tw : 1);
    if(k >= 0 && k < GAL_TABS && k != galTab){ galTab = k; galScroll = 0; galRender(); }
    return;
  }
  // --- Toque sobre un elemento ---
  uint32_t id = galHitId(T.x, T.y);
  if(!id) return;
  if(mkMulti){ mkToggle(id); galRender(); return; }
  mkRequestOpen(id);                              // lo protegido pide antes la clave
}

static void galEnter(){
  mkBind(&GAL_APP);
  // gRelayout = "re-dibuja con la geometria nueva", no "empieza de cero".
  if(!gRelayout){
    galScroll = 0;
    mkReset();
    if(gMlOk) mlRequestScan();            // lo copiado por otras vias aparece al entrar
  }
  galRender();
}

// ATRAS deshace primero las capas (menu, dialogos, hoja) y la seleccion.
static bool galBackLayer(){ return mkBackLayer(); }
static bool galBackScreen(){ return false; }

static void galSuspend(){
  galDragging = false; galLongFired = false;
  mkSuspend();                                    // capas y seleccion fuera; el servidor sigue
}

static void galResume(){
  mkBind(&GAL_APP);
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
  galScroll = 0;
  if(mkApp == &GAL_APP) mkReset();
}
