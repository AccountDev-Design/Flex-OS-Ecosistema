// #############################################################
// ##  FLEX OS ULTRA  ·  APP MULTIMEDIA  ·  reproductor real
// ##  ----------------------------------------------------------
// ##  Reproduccion de ficheros locales (JPEG, AVI/MJPEG, WAV), volumen,
// ##  orientacion, pantalla completa y liberacion al suspender.
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
#include "FlexOS_Ultra_MediaViewer.h"   // eslabon anterior de la cadena

// #############################################################

// #############################################################
// ##  APP MULTIMEDIA  ·  LISTA DE FOTOS, DIBUJOS Y VIDEOS
// ##  ------------------------------------------------------
// ##  La lista sale del catalogo de la biblioteca (el mismo de la
// ##  Galeria y de Flex Web Server). Abrir un elemento lo lleva al VISOR
// ##  DE MEDIOS (FlexOS_Ultra_MediaViewer.h), el mismo que usa la
// ##  Galeria: una sola implementacion de la foto ajustada, el zoom, el
// ##  video, las barras y la orientacion.
// ##
// ##  QUE REPRODUCE DE VERDAD: JPEG baseline, dibujos de Paint y video
// ##  AVI/MJPEG. El AUDIO se escucha en Musica, que sigue sonando aunque
// ##  se salga de ella: un audio que llegue aqui (desde el Explorador) se
// ##  pasa a Musica. MP4/H.264 no se anuncia ni se intenta: esta placa no
// ##  tiene decodificador de video por hardware y en software no da el
// ##  ritmo. Un archivo asi se abre y se explica, no se reproduce a medias.
// #############################################################

// Pantallas de la app.
#define VS_LIST   0
#define VS_VIEW   1

// ---- Estado de la lista ----
static int   vidScreen   = VS_LIST;
static int   vidFilter   = 0;          // 0 todo, 1 videos, 2 fotos (el audio va a Musica)
static int   vidListScroll = 0;
static int   vidListDragY0 = 0, vidListDragS0 = 0;
static bool  vidListDragging = false;
static uint32_t vidCurId = 0;          // lo ultimo abierto en el visor

static void vidRenderAll();
static void vidListRender();

// -------------------------------------------------------------
//  PANTALLA DE LISTA
//  ------------------------------------------------------------
//  Sale del catalogo de la biblioteca, el MISMO de la Galeria y de
//  Flex Web Server: si la Galeria ve una foto, Multimedia la ve, y al
//  reves. Seleccion, menus y acciones son los del kit comun (mk*).
// -------------------------------------------------------------
#define VID_ROW_H   64
#define VID_HEAD_H  104
#define VID_TABS_N  3
#define VID_THUMB_BUDGET 6            // miniaturas nuevas por repintado
static const char* VID_TABS[VID_TABS_N] = { "Todo", "V\xC3\xAD" "deos", "Fotos" };
static uint32_t vidTabMask(){
  switch(vidFilter){
    case 1: return FML_MASK(FML_K_VIDEO);
    case 2: return FML_MASK(FML_K_PHOTO) | FML_MASK(FML_K_DRAW);
  }
  return FML_MASK_VISUAL;
}

// Vista del catalogo (indices dentro de gMs.lib.recs): solo vale con el
// cerrojo tomado, como en la Galeria.
static FlexMlView vidView;                       // indices en mlTables()->vidView (PSRAM)
static uint32_t   vidViewMask = 0;
static bool       vidViewReady = false;
static int        vidCountCache = 0;
static bool       vidMorePending = false;
static uint32_t   vidSeenRev = 0, vidSeenMs = 0;

static void vidSyncLocked(){
  uint32_t m = vidTabMask();
  if(!vidViewReady || vidViewMask != m || !vidView.idx){
    MlTables* t = mlTables();
    flexMlViewInit(&vidView, t ? t->vidView : NULL, FML_CAP, m, FML_SORT_NEWEST);
    vidViewMask = m; vidViewReady = true;
    flexMlViewSync(&vidView, &gMs.lib, true);
  } else flexMlViewSync(&vidView, &gMs.lib, false);
}

static void vidOpenId(uint32_t id);
static const MediaListApp VID_APP = { "Multimedia", vidRenderAll, vidOpenId, NULL };

static int vidRowY(int i){ int bx, by, bw, bh; uiBox(bx, by, bw, bh); return by + VID_HEAD_H + i * VID_ROW_H - vidListScroll; }

static void vidDrawRow(const FlexMlRec* r, int x, int y, int w, int &budget){
  int h = VID_ROW_H - 8;
  bool sel = mkMulti && mkIsSel(r->id);
  if(sel) fillRoundRect(x, y, w, h, 12, TH_SEL);
  else if(uiGlass) drawGlassCardFlat(x, y, w, h, 12, TH_GLASS, WIN_BG);
  else        fillRoundRect(x, y, w, h, 12, TH_SURF);
  int tx = x + 8, ty = y + (h - ML_SMALL) / 2;
  if(r->flags & FML_R_LOCKED){
    // Solo el candado: ni miniatura, ni nombre, ni tamano.
    fillRoundRect(tx, ty, ML_SMALL, ML_SMALL, 8, TH_SURF2);
    mlPadlock(tx + ML_SMALL / 2, ty + ML_SMALL / 2 - 2, 8, TH_TXT2, false);
    drawText(x + 64, y + (h - uiLineH(2)) / 2, "Protegido", 2, TH_TXT2);
  } else {
    bool more = false;
    const uint16_t* th = mlThumbGetLocked(r, &budget, &more);
    if(more) vidMorePending = true;
    if(th) mlBlitThumbSmall(th, tx, ty, 8);
    else {
      fillRoundRect(tx, ty, ML_SMALL, ML_SMALL, 8, TH_SURF2);
      if(r->kind == FML_K_VIDEO) fillTriangle(tx + 16, ty + 12, tx + 16, ty + 32, tx + 32, ty + 22, TH_TXT2);
      else { fillTriangle(tx + 7, ty + 34, tx + 18, ty + 18, tx + 29, ty + 34, TH_TXT2); fillCircle(tx + 32, ty + 13, 4, TH_TXT2); }
    }
    if(r->kind == FML_K_VIDEO && th){
      fillCircle(tx + 11, ty + 11, 8, rgb565(0, 0, 0));
      fillTriangle(tx + 8, ty + 6, tx + 8, ty + 16, tx + 16, ty + 11, rgb565(255, 255, 255));
    }
    char nm[FML_NAME_MAX]; flexMlDisplayName(r, nm, sizeof(nm));
    drawTextClip(x + 64, y + 8, nm, 2, TH_TXT, x + w - (mkMulti ? 44 : 12));
    // Lo que es de verdad: clase, duracion o medidas, tamano, y si esta
    // placa no lo abre, por que.
    char sub[72], sz[16], extra[24] = "";
    flexFsFmtSize(r->size, sz, sizeof(sz));
    if(r->kind == FML_K_VIDEO && r->durMs){ mlFmtDur(r->durMs, extra, sizeof(extra)); }
    else if(r->w && r->h) snprintf(extra, sizeof(extra), "%ux%u", (unsigned)r->w, (unsigned)r->h);
    const char* cls = r->kind == FML_K_VIDEO ? "V\xC3\xAD" "deo" : r->kind == FML_K_DRAW ? "Dibujo" : "Foto";
    if(r->state == FML_S_ERROR) snprintf(sub, sizeof(sub), "%s  \xC2\xB7  Archivo da\xC3\xB1" "ado", cls);
    else if(!(r->flags & FML_R_PLAYABLE) && !(r->flags & FML_R_NEED_THUMB))
      snprintf(sub, sizeof(sub), "%s  \xC2\xB7  %s  \xC2\xB7  Solo guardar", flexMlFmtName(r->fmt), sz);
    else snprintf(sub, sizeof(sub), "%s%s%s  \xC2\xB7  %s", cls, extra[0] ? "  \xC2\xB7  " : "", extra, sz);
    drawTextClip(x + 64, y + 34, sub, 1, r->state == FML_S_ERROR ? TH_WARN : TH_TXT2, x + w - (mkMulti ? 44 : 12));
  }
  if(mkMulti){
    int cx = x + w - 22, cy = y + h / 2;
    fillCircle(cx, cy, 11, sel ? TH_PRIM : rgb565(0, 0, 0));
    drawCircle(cx, cy, 11, rgb565(255, 255, 255));
    if(sel){
      strokeSegAA(cx - 5, cy, cx - 1, cy + 5, 2.0f, TH_ONACC);
      strokeSegAA(cx - 1, cy + 5, cx + 6, cy - 5, 2.0f, TH_ONACC);
    }
  }
}

static void vidListRender(){
  if(webSheetIsOpen()){ webSheetRender(); return; }  // la hoja del servidor manda mientras esta abierta
  setBuf(fb);
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  fillRect(bx, by, bw, bh, WIN_BG);
  int pad = uiPad();
  vidMorePending = false;
  mlThumbNewPass();
  int budget = VID_THUMB_BUDGET;
  drawText(bx + pad, by + 14, "Multimedia", 4, TH_TXT);
  for(int i = 0; i < 3; i++) fillCircle(bx + bw - pad - 4, by + 22 + i * 12, 4, TH_NAV);

  if(!gMlOk){
    const char* t = !flexFsReady() ? "Sin almacenamiento" : gSafeMode ? "Modo seguro" : "Biblioteca no disponible";
    drawTextC(bx + bw / 2, by + bh / 2 - 20, t, 3, TH_TXT2);
    drawTextC(bx + bw / 2, by + bh / 2 + 16, !flexFsReady() ? flexFsError() : "La biblioteca no se abre ahora", 1, TH_MUTE);
    vidCountCache = 0;
    flxFlush(WIN_TOP, WIN_BOT);
    return;
  }

  mlLock();
  vidSyncLocked();
  const int n = vidView.n;
  vidCountCache = n;
  vidSeenRev = gMs.lib.rev; vidSeenMs = millis();
  mkPruneLocked(&vidView);
  { char cnt[48];
    if(mkMulti) snprintf(cnt, sizeof(cnt), "%u seleccionado%s", (unsigned)mkSelN, mkSelN == 1 ? "" : "s");
    else snprintf(cnt, sizeof(cnt), "%d elemento%s", n, n == 1 ? "" : "s");
    drawText(bx + pad, by + 52, cnt, 1, mkMulti ? TH_PRIM : TH_TXT2); }
  const char* st = NULL; char stb[64];
  if(gMs.scanning) st = "Buscando archivos...";
  else if(gMs.pending){ snprintf(stb, sizeof(stb), "Preparando miniaturas (%u)...", (unsigned)gMs.pending); st = stb; }
  if(st) drawTextR(bx + bw - pad - 18, by + 52, st, 1, TH_TXT2);

  // Pestanas
  const int tabY = by + 68, tw = (bw - 2 * pad) / VID_TABS_N;
  for(int i = 0; i < VID_TABS_N; i++){
    int tx = bx + pad + i * tw;
    if(i == vidFilter) fillRoundRect(tx + 2, tabY, tw - 4, 28, 14, TH_PRIM);
    drawTextC(tx + tw / 2, tabY + 8, VID_TABS[i], 1, i == vidFilter ? TH_ONACC : TH_TXT2);
  }

  if(n == 0){
    drawTextC(bx + bw / 2, by + bh / 2 - 30, "No hay fotos ni v\xC3\xAD" "deos", 3, TH_TXT2);
    drawTextC(bx + bw / 2, by + bh / 2 + 6, "S\xC3\xBA" "belos desde el m\xC3\xB3vil:", 1, TH_MUTE);
    drawTextC(bx + bw / 2, by + bh / 2 + 24, "men\xC3\xBA > Conectar con el m\xC3\xB3vil", 1, TH_MUTE);
  }

  uiClipViewport(by + VID_HEAD_H - 6, by + bh - 1);
  for(int i = 0; i < n; i++){
    int y = vidRowY(i);
    if(y + VID_ROW_H < by + VID_HEAD_H - 6 || y > by + bh) continue;   // solo las visibles
    vidDrawRow(&gMs.lib.recs[vidView.idx[i]], bx + pad, y, bw - 2 * pad, budget);
  }
  uiClipFull();
  int selLocked = 0, selOpen = 0, selectable = 0;
  if(mkMulti) mkCountLocked(&vidView, &selLocked, &selOpen, &selectable);
  mlUnlock();

  if(mkMulti) mkDrawBar(selLocked, selOpen, selectable);
  mkDrawOverlays();
  flxFlush(WIN_TOP, WIN_BOT);
}

static int vidListMaxScroll(){
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  int need = VID_HEAD_H + vidCountCache * VID_ROW_H + (mkMulti ? MKB_H + 26 : 30);
  int m = need - bh;
  return m > 0 ? m : 0;
}

// Toque sobre la lista -> elemento (o 0).
static uint32_t vidHitId(int tx, int ty){
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  if(ty < by + VID_HEAD_H - 6) return 0;
  uint32_t id = 0;
  mlLock();
  vidSyncLocked();
  for(int i = 0; i < vidView.n && !id; i++){
    int y = vidRowY(i);
    if(ty >= y && ty <= y + VID_ROW_H - 8 && tx >= bx && tx <= bx + bw) id = gMs.lib.recs[vidView.idx[i]].id;
  }
  mlUnlock();
  return id;
}

static void vidSelectAll(){
  mlLock();
  vidSyncLocked();
  mkSelectAllLocked(&vidView);
  mlUnlock();
  vidListRender();
}

// -------------------------------------------------------------
//  APERTURA DESDE OTRAS APPS
//  ------------------------------------------------------------
//  El Explorador llama aqui. La ruta se guarda y la app se abre por el
//  camino normal (enterApp), asi que la animacion, el historial de
//  recientes y el ciclo de vida son los de siempre. La Galeria ya NO
//  pasa por aqui para ver una foto: tiene el visor dentro (solo "Abrir
//  en Multimedia" lo hace, a proposito).
// -------------------------------------------------------------
static char    gMediaPending[FLEXMED_PATH_MAX] = "";
// De que app se salio para abrir el visor. "Volver" desde el visor regresa
// alli y no a la lista de Multimedia. 0xFF = se entro a Multimedia por su
// cuenta.
static uint8_t gMediaReturnApp = 0xFF;
static void musOpenPath(const char* path);        // Musica, mas abajo en la cadena

// Id del catalogo de una ruta (0 si no esta registrada).
static uint32_t vidIdOfPath(const char* path){
  mlLock();
  int i = flexMlFindPath(&gMs.lib, path);
  uint32_t id = i >= 0 ? gMs.lib.recs[i].id : 0;
  mlUnlock();
  return id;
}

// ---- El visor, visto desde Multimedia ----
static VwSession vidVwSess;
// El visor se cerro: a la app de la que se vino, o a la lista.
static void vidVwClosed(){
  vidScreen = VS_LIST;
  if(gMediaReturnApp != 0xFF){
    uint8_t back = gMediaReturnApp;
    gMediaReturnApp = 0xFF;
    appClose();
    enterApp(back);
    return;
  }
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, WIN_BG);
  appDrawChrome(IC_MULTIMEDIA);
  appDrawHeader(IC_MULTIMEDIA);
  vidListRender();
  flxFlushAll();
}
// Anterior/siguiente en la lista que el usuario ve, SALTANDO lo protegido:
// pasar de una foto a la siguiente no puede ensenar algo que pide clave.
static uint32_t vidVwNeighbour(uint32_t id, int delta){
  uint32_t out = 0;
  mlLock();
  vidSyncLocked();
  int n = vidView.n, cur = flexMlViewFindId(&vidView, &gMs.lib, id);
  for(int k = cur + delta; cur >= 0 && k >= 0 && k < n && !out; k += delta){
    const FlexMlRec* r = &gMs.lib.recs[vidView.idx[k]];
    if(!(r->flags & FML_R_LOCKED)) out = r->id;
  }
  mlUnlock();
  return out;
}
static const VwHost VID_VW = { IC_MULTIMEDIA, &vidVwSess, vidVwClosed, vidVwNeighbour, NULL, NULL };

static void mediaOpenInPlayer(const char* path){
  if(!path || !path[0]) return;
  // El audio se escucha en Musica: un solo reproductor, que ademas sigue
  // sonando en segundo plano.
  const char* nm = strrchr(path, '/');
  if(flexMediaClassify(nm ? nm + 1 : path) == FLEXMED_AUDIO){ musOpenPath(path); return; }
  if(gState == ST_APP && gAppId == IC_MULTIMEDIA){
    // Ya estamos dentro: se abre en el acto.
    vidScreen = VS_VIEW;
    vidCurId = vidIdOfPath(path);
    vwOpen(&VID_VW, vidCurId, path, NULL);
    return;
  }
  snprintf(gMediaPending, sizeof(gMediaPending), "%s", path);
  if(gState == ST_APP) appClose();
  enterApp(IC_MULTIMEDIA);
}

// Abrir desde la lista (ya autorizado si estaba protegido).
static void vidOpenId(uint32_t id){
  FlexMlRec r;
  if(!mlGet(id, &r)) return;
  vidCurId = id;
  vidScreen = VS_VIEW;
  vwOpen(&VID_VW, id, NULL, NULL);
}

static void vidListTouch(){
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  int pad = uiPad();
  const int tabY = by + 68, tw = (bw - 2 * pad) / VID_TABS_N;

  // Arrastre vertical de la lista.
  if(T.pressed){ vidListDragging = false; vidListDragY0 = T.y; vidListDragS0 = vidListScroll; }
  if(T.down && !vidListDragging && abs(T.y - vidListDragY0) > 8) vidListDragging = true;
  if(T.down && vidListDragging){
    int ns = vidListDragS0 - (T.y - vidListDragY0);
    int mx = vidListMaxScroll();
    if(ns < 0) ns = 0;
    if(ns > mx) ns = mx;
    if(ns != vidListScroll){ vidListScroll = ns; vidListRender(); }
    return;
  }
  // Pulsacion larga: el menu del elemento (no activa la seleccion sola).
  static bool longFired = false;
  if(!gHosted && T.down && !longFired && (millis() - T.downMs) > 550
     && abs(T.x - T.startX) < 14 && abs(T.y - T.startY) < 14){
    longFired = true;
    uint32_t id = vidHitId(T.startX, T.startY);
    if(id && !mkMulti){ mkOpenItemMenu(id, T.x, T.y + 10, NULL, 0); return; }
    if(id && mkMulti){ mkToggle(id); vidListRender(); return; }
  }
  if(!T.down) longFired = false;
  if(!T.tap) return;
  if(vidListDragging){ vidListDragging = false; return; }

  if(mkMulti && mkBarTouch(vidSelectAll)) return;
  // Menu de la app (los tres puntos).
  if(T.x > bx + bw - pad - 30 && T.y < by + 60){
    static const uint8_t acts[3] = { MA_CONNECT, MA_SELECT, MA_TRASHBIN };
    mkOpenAppMenu(bx + bw - MM_W / 2 - 16, by + 50, acts, 3);
    return;
  }
  if(T.y >= tabY && T.y <= tabY + 28){
    int k = (T.x - bx - pad) / (tw > 0 ? tw : 1);
    if(k >= 0 && k < VID_TABS_N && k != vidFilter){
      vidFilter = k; vidListScroll = 0; vidListRender();
    }
    return;
  }
  uint32_t id = vidHitId(T.x, T.y);
  if(!id) return;
  if(mkMulti){ mkToggle(id); vidListRender(); return; }
  mkRequestOpen(id);                              // lo protegido pide antes la clave
}

// -------------------------------------------------------------
//  CICLO DE VIDA
// -------------------------------------------------------------
static void vidRenderAll(){
  if(vwHostOpen(&VID_VW)){
    vidScreen = VS_VIEW;
    if(vwActiveFor(&VID_VW)) vwRender();
    else if(!vwEnsure(&VID_VW)) vidVwClosed();
    return;
  }
  vidScreen = VS_LIST;
  vidListRender();
}

static void vidEnter(){
  mkBind(&VID_APP);
  appLoadSessionOnce(IC_MULTIMEDIA);
  if(!gRelayout) mkReset();
  if(gMediaPending[0]){
    vidScreen = VS_VIEW;
    vidCurId = vidIdOfPath(gMediaPending);
    char p[FLEXMED_PATH_MAX];
    snprintf(p, sizeof(p), "%s", gMediaPending);
    gMediaPending[0] = 0;
    vwOpen(&VID_VW, vidCurId, p, NULL);
    if(!vwHostOpen(&VID_VW)) vidVwClosed();       // no se pudo abrir: la lista (o la app de origen)
    return;
  }
  if(gRelayout && vwHostOpen(&VID_VW)){ vidRenderAll(); return; }
  if(gMlOk && !gRelayout) mlRequestScan();      // lo copiado por otras vias aparece al entrar
  vidScreen = VS_LIST;
  vidListRender();
}

static void vidTick(){
  if(vwHostOpen(&VID_VW)){
    vidScreen = VS_VIEW;
    if(vwActiveFor(&VID_VW) || vwEnsure(&VID_VW)) vwTick();
    else vidVwClosed();
    return;
  }
  vidScreen = VS_LIST;
  if(mkTick()) return;                            // menu, dialogos, papelera, hoja del servidor
  // El catalogo cambio (subida del movil, miniatura lista, reconciliacion):
  // como mucho un repintado cada 300 ms, y nunca a mitad de un arrastre.
  if(gMlOk && !vidListDragging && millis() - vidSeenMs >= 300 && mlRev() != vidSeenRev){ vidListRender(); return; }
  if(vidMorePending && !T.down){ vidListRender(); return; }
  vidListTouch();
}

// #############################################################
// ##  MULTIMEDIA · SESION Y LIBERACION
// ##  ------------------------------------------------------
// ##  Suspender PARA la reproduccion de verdad (una app en segundo
// ##  plano no decodifica ni suena) y cerrar suelta TODO lo del visor.
// ##  Lo que se conserva es solo lo que se estaba viendo y la POSICION
// ##  de cada video, que es lo unico que hace falta para retomar.
// #############################################################
#define VID_SESS_VER   2
#define VID_SESS_PATH  FS_DIR_SESS "/media.bin"
struct VidSessV2 {
  int32_t  filter;
  uint32_t resumeKey[VW_RESUME_N];
  uint32_t resumeFrame[VW_RESUME_N];
};

// ATRAS (barra del sistema o gesto) desde el visor vuelve a la lista,
// no expulsa la app: es la misma regla que ya sigue la Galeria con su
// menu, y evita que ver una foto y pulsar atras te saque al
// escritorio perdiendo la lista donde estabas.
static bool vidBackScreen(){
  if(!vwHostOpen(&VID_VW)) return false;
  if(vwActiveFor(&VID_VW)) vwClose();
  else { vidVwSess.open = false; vidVwClosed(); }
  return true;
}
// En la lista, ATRAS cierra primero el menu, los dialogos y la seleccion.
static bool vidBackLayer(){ return !vwHostOpen(&VID_VW) && mkBackLayer(); }

static void vidSuspend(){
  // Una app en segundo plano no decodifica. Se guarda la posicion y se
  // suelta todo; al volver, vidResume reabre por donde iba.
  mkSuspend();
  if(vwActiveFor(&VID_VW)) vwSuspend();
  gLand = false;                       // el framework tambien lo hace; aqui por si acaso
}
// SOLTAR SIN CERRAR. vidSuspend ya suelta el fotograma y el descriptor al pasar
// a segundo plano, asi que aqui casi siempre no queda nada -- y entonces se
// devuelve 0, que es lo que hace que el optimizador no se apunte bytes que no
// libero. Existe para el caso en que la app quedara suspendida por otra via.
static size_t vidShed(){ return vwShed(); }
static void vidResume(){
  mkBind(&VID_APP);
  if(vwHostOpen(&VID_VW)){
    vidScreen = VS_VIEW;
    if(!vwEnsure(&VID_VW)) vidVwClosed();
    return;
  }
  vidScreen = VS_LIST;
  vidListRender();
}
static void vidCloseApp(){
  vwForget(&VID_VW);
  if(mkApp == &VID_APP) mkReset();
  gLand = false;
  vidScreen = VS_LIST;
  gSessLoaded[IC_MULTIMEDIA] = false;
}
static bool vidSaveSess(){
  if(!flexFsReady()) return true;
  VidSessV2 v;
  memset(&v, 0, sizeof(v));
  v.filter = vidFilter;
  for(int i = 0; i < VW_RESUME_N; i++){
    v.resumeKey[i]   = vwResumeTab[i].key;
    v.resumeFrame[i] = vwResumeTab[i].frame;
  }
  return sessWrite(VID_SESS_PATH, VID_SESS_VER, IC_MULTIMEDIA, &v, sizeof(v));
}
static void vidLoadSess(){
  if(!flexFsReady()) return;
  VidSessV2 v;
  if(sessRead(VID_SESS_PATH, VID_SESS_VER, IC_MULTIMEDIA, &v, sizeof(v)) != sizeof(v)) return;
  // (La pestana 3 era "Audio", que ahora vive en Musica: se vuelve a "Todo".)
  vidFilter = (v.filter >= 0 && v.filter < VID_TABS_N) ? (int)v.filter : 0;
  for(int i = 0; i < VW_RESUME_N; i++){
    vwResumeTab[i].key    = v.resumeKey[i];
    vwResumeTab[i].frame  = v.resumeFrame[i];
    vwResumeTab[i].whenMs = 0;
  }
}
