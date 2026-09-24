// #############################################################
// ##  FLEX OS ULTRA  ·  KIT DE LISTAS DE MEDIOS
// ##  ----------------------------------------------------------
// ##  Seleccion, menus y acciones COMUNES de Galeria, Multimedia y
// ##  Musica: pulsacion larga, seleccion multiple, seleccionar todo,
// ##  bloquear con la clave del sistema, papelera, borrar, renombrar,
// ##  detalles y "Conectar con el movil".
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
#include "FlexOS_Ultra_FileKit.h"   // eslabon anterior de la cadena

// #############################################################
// ##  POR QUE UN SOLO CONTROLADOR
// ##  ------------------------------------------------------
// ##  Las tres apps pintan distinto (rejilla, filas, canciones) pero
// ##  deciden LO MISMO: que se puede hacer con un elemento, que pasa
// ##  con lo protegido, que entra en "Seleccionar todo", cuando se
// ##  pide la clave. Si cada una lo decidiera por su cuenta, tarde o
// ##  temprano una dejaria mandar a la papelera algo protegido. Aqui
// ##  vive una vez y las tres lo usan.
// ##
// ##  UNA SOLA APP A LA VEZ. Solo hay una app de medios en primer
// ##  plano, asi que el estado (seleccion, menu, dialogo) es uno; al
// ##  cambiar de app (mkBind) o al suspenderse (mkSuspend) se limpia.
// ##
// ##  LO QUE PONE CADA APP: su nombre (para los avisos), como
// ##  repintarse, como abrir un elemento ya autorizado y, si tiene,
// ##  sus acciones propias (Editar en la Galeria, Reproducir en
// ##  Musica).
// #############################################################

// Flex Web Server vive mas abajo en la cadena (necesita la red).
static void webSheetOpenFor(void (*back)());
static bool webSheetIsOpen();
static bool webSheetTick();
static void webSheetRender();
static void webSheetDismiss();

struct MediaListApp {
  const char* name;                          // "Galeria", "Multimedia", "Musica"
  void (*redraw)();                          // repinta la app entera (sin sus capas)
  void (*open)(uint32_t id);                 // abre un elemento ya autorizado
  bool (*extra)(int act, uint32_t id);       // acciones propias; NULL = ninguna
};

// Codigos de la barra de seleccion que no son una accion sobre elementos.
enum { MKB_NONE = 0, MKB_ALL = 100, MKB_EXIT, MKB_LOCKTOGGLE };

static const MediaListApp* mkApp = NULL;
static bool     mkMulti  = false;
static uint32_t mkSel[FML_CAP];
static uint16_t mkSelN   = 0;
static uint32_t mkMenuId = 0;                // elemento del menu contextual (0 = menu de la app)
static int      mkAskAct = MA_NONE;          // que confirma el dialogo de borrar
static uint32_t mkRenameId = 0;
static bool     mkNoLockDlg = false;         // el dialogo abierto es "configura un PIN"
static uint32_t mkIds[FML_CAP];              // lista de trabajo de una accion

// -------------------------------------------------------------
//  ESTADO
// -------------------------------------------------------------
static void mkCloseLayers(){
  mmOn = false; mmDlgOn = false; mkNoLockDlg = false;
  fkNameOn = false; fkAskOn = false; fkTrashOn = false; fkMenuOn = false;
  mkAskAct = MA_NONE; mkRenameId = 0;
}
static void mkReset(){
  mkCloseLayers();
  mkMulti = false; mkSelN = 0; mkMenuId = 0;
}
// La app que manda ahora. Cambiar de app empieza de cero: una seleccion
// hecha en la Galeria no puede aparecer marcada en Musica.
static void mkBind(const MediaListApp* a){
  if(mkApp != a){ mkApp = a; mkReset(); }
}
// Al pasar a segundo plano: ni capas modales ni seleccion a medias. La hoja
// del servidor se cierra (el servidor sigue encendido).
static void mkSuspend(){
  if(webSheetIsOpen()) webSheetDismiss();
  mkReset();
}
static bool mkOverlayOpen(){ return mmOn || mmDlgOn || fkNameOn || fkAskOn || fkTrashOn || webSheetIsOpen(); }

static bool mkIsSel(uint32_t id){ for(int k = 0; k < mkSelN; k++) if(mkSel[k] == id) return true; return false; }
static void mkToggle(uint32_t id){
  for(int k = 0; k < mkSelN; k++) if(mkSel[k] == id){ mkSel[k] = mkSel[--mkSelN]; return; }
  if(mkSelN < FML_CAP) mkSel[mkSelN++] = id;
}
static void mkEnterMulti(uint32_t firstId){ mkMulti = true; mkSelN = 0; if(firstId) mkToggle(firstId); }
static void mkExitMulti(){ mkMulti = false; mkSelN = 0; }

// ---- Con el cerrojo del catalogo tomado y la vista ya sincronizada ----
// Lo que ya no esta en la vista (lo borro el movil, se reconcilio) sale de
// la seleccion.
static void mkPruneLocked(const FlexMlView* v){
  for(int k = 0; k < mkSelN; ){
    if(flexMlViewFindId(v, &gMs.lib, mkSel[k]) < 0) mkSel[k] = mkSel[--mkSelN]; else k++;
  }
}
static void mkCountLocked(const FlexMlView* v, int* selLocked, int* selOpen, int* selectable){
  int a = 0, b = 0, c = 0;
  for(int k = 0; k < mkSelN; k++){
    int i = flexMlFindId(&gMs.lib, mkSel[k]);
    if(i >= 0 && (gMs.lib.recs[i].flags & FML_R_LOCKED)) a++; else b++;
  }
  for(int i = 0; i < v->n; i++) if(!(gMs.lib.recs[v->idx[i]].flags & FML_R_LOCKED)) c++;
  if(selLocked) *selLocked = a;
  if(selOpen) *selOpen = b;
  if(selectable) *selectable = c;
}
// "Seleccionar todo" alterna: si ya estaba todo, no queda nada. Lo
// protegido NUNCA entra en "todo": se elige uno a uno, a proposito.
static void mkSelectAllLocked(const FlexMlView* v){
  int selectable = 0, have = 0;
  for(int i = 0; i < v->n; i++){
    const FlexMlRec* r = &gMs.lib.recs[v->idx[i]];
    if(r->flags & FML_R_LOCKED) continue;
    selectable++;
    if(mkIsSel(r->id)) have++;
  }
  bool all = selectable > 0 && have == selectable;
  mkSelN = 0;
  if(!all) for(int i = 0; i < v->n && mkSelN < FML_CAP; i++){
    const FlexMlRec* r = &gMs.lib.recs[v->idx[i]];
    if(!(r->flags & FML_R_LOCKED)) mkSel[mkSelN++] = r->id;
  }
}

// Ids sobre los que actua la accion: la seleccion, o el elemento del menu.
static int mkTargets(uint32_t* out, bool* anyLocked, bool* anyOpen){
  int n = 0;
  bool L = false, O = false;
  mlLock();
  if(mkMulti){
    for(int k = 0; k < mkSelN; k++){
      int i = flexMlFindId(&gMs.lib, mkSel[k]);
      if(i < 0) continue;
      out[n++] = mkSel[k];
      if(gMs.lib.recs[i].flags & FML_R_LOCKED) L = true; else O = true;
    }
  } else if(mkMenuId){
    int i = flexMlFindId(&gMs.lib, mkMenuId);
    if(i >= 0){ out[n++] = mkMenuId; if(gMs.lib.recs[i].flags & FML_R_LOCKED) L = true; else O = true; }
  }
  mlUnlock();
  if(anyLocked) *anyLocked = L;
  if(anyOpen) *anyOpen = O;
  return n;
}

static void mkRedraw(){ if(mkApp && mkApp->redraw) mkApp->redraw(); }
static const char* mkName(){ return mkApp && mkApp->name ? mkApp->name : "Medios"; }

// -------------------------------------------------------------
//  MENUS
// -------------------------------------------------------------
// Menu de UN elemento: SOLO lo que se puede hacer con el. Lo protegido no
// se renombra, no va a la papelera y no ensena detalles sin la clave.
static void mkOpenItemMenu(uint32_t id, int ax, int ay, const uint8_t* extra, int nExtra){
  FlexMlRec r;
  if(!mlGet(id, &r)) return;
  mkMenuId = id;
  bool locked = (r.flags & FML_R_LOCKED) != 0;
  uint8_t a[MM_MAX]; int n = 0;
  if(!locked) for(int i = 0; i < nExtra && n < MM_MAX - 5; i++) a[n++] = extra[i];
  a[n++] = MA_SELECT;
  a[n++] = locked ? MA_UNLOCK : MA_LOCK;
  if(!locked){ a[n++] = MA_RENAME; a[n++] = MA_INFO; a[n++] = MA_TRASH; }
  if(n < MM_MAX) a[n++] = MA_DELETE;
  mmOpen(ax, ay, a, n);
}
// Menu de la app (los tres puntos).
static void mkOpenAppMenu(int ax, int ay, const uint8_t* acts, int n){
  mkMenuId = 0;
  mmOpen(ax, ay, acts, n);
}

// ---- Detalles: lo que el catalogo sabe de verdad del elemento ----
static void mkInfoOpen(uint32_t id){
  FlexMlRec r;
  if(!mlGet(id, &r) || (r.flags & FML_R_LOCKED)) return;
  char nm[FML_NAME_MAX], sz[16], dim[24] = "", dur[16] = "", when[24] = "";
  flexMlDisplayName(&r, nm, sizeof(nm));
  flexFsFmtSize(r.size, sz, sizeof(sz));
  if(r.w && r.h) snprintf(dim, sizeof(dim), "  \xC2\xB7  %ux%u", (unsigned)r.w, (unsigned)r.h);
  if(r.durMs){ mlFmtDur(r.durMs, dur, sizeof(dur)); }
  uint32_t t = r.created ? r.created : r.added;
  if(t > 1000000000u){                               // la misma fecha civil que ensena el reloj
    int y, m, d; clkCivilFromDays((long)(t / 86400u), y, m, d);
    snprintf(when, sizeof(when), "%02d/%02d/%04d", d, m, y);
  }
  const char* why = !(r.flags & FML_R_PLAYABLE) ? flexMlWhyUnplayable(r.fmt) : NULL;
  char txt[300];
  snprintf(txt, sizeof(txt), "%s\n%s  \xC2\xB7  %s%s%s%s\n%s%s%s%s",
           nm, flexMlFmtName(r.fmt), sz, dim, dur[0] ? "  \xC2\xB7  " : "", dur,
           r.path, when[0] ? "\n" : "", when,
           r.state == FML_S_ERROR ? "\nEl archivo est\xC3\xA1 da\xC3\xB1" "ado" : why ? "\n" : "");
  if(why && r.state != FML_S_ERROR){ size_t L = strlen(txt); snprintf(txt + L, sizeof(txt) - L, "%s", why); }
  mmDlgOpen("Detalles", txt, "Cerrar", "", false);
}

// -------------------------------------------------------------
//  ACCIONES
// -------------------------------------------------------------
// Con la clave ya comprobada (o sin clave en el sistema).
static void mkAuthDone(bool ok, int act, const uint32_t* ids, int n){
  if(!ok){ mkRedraw(); return; }
  if(act == MA_OPEN){
    if(n == 1 && mkApp && mkApp->open) mkApp->open(ids[0]);
    else mkRedraw();
    return;
  }
  int done = 0;
  char why[64] = "";
  for(int k = 0; k < n; k++){
    if(act == MA_LOCK)        done += mlSetLock(ids[k], true, why, sizeof(why)) ? 1 : 0;
    else if(act == MA_UNLOCK) done += mlSetLock(ids[k], false, why, sizeof(why)) ? 1 : 0;
    else if(act == MA_DELETE) done += mlDelete(ids[k]) ? 1 : 0;
  }
  mkExitMulti();
  char msg[64];
  const char* verb = act == MA_LOCK ? "bloqueado" : act == MA_UNLOCK ? "desbloqueado" : "borrado";
  snprintf(msg, sizeof(msg), "%d elemento%s %s%s", done, done == 1 ? "" : "s", verb, done == 1 ? "" : "s");
  sysNotify(mkName(), done < n && why[0] ? why : msg);
  mkRedraw();
}

// Abrir un elemento: lo protegido pide antes la clave del sistema.
static void mkRequestOpen(uint32_t id){
  FlexMlRec r;
  if(!mlGet(id, &r)) return;
  if(r.flags & FML_R_LOCKED){ mkIds[0] = id; mediaAuthRequest(MA_OPEN, mkIds, 1, mkAuthDone); return; }
  if(mkApp && mkApp->open) mkApp->open(id);
}

// Accion elegida en un menu o en la barra de seleccion.
static void mkDoAction(int act){
  bool anyLocked = false, anyOpen = false;
  switch(act){
    case MA_CONNECT:  webSheetOpenFor(mkApp ? mkApp->redraw : NULL); return;
    case MA_TRASHBIN: fkTrashOpen(); return;
    case MA_SELECT:   mkEnterMulti(mkMenuId); mkRedraw(); return;
    case MA_INFO:     mkInfoOpen(mkMenuId); return;
    case MKB_LOCKTOGGLE:
      mkTargets(mkIds, &anyLocked, &anyOpen);
      act = anyOpen ? MA_LOCK : MA_UNLOCK;
      break;
    case MA_LOCK: case MA_UNLOCK: case MA_TRASH: case MA_DELETE: case MA_RENAME:
      break;
    default:                                          // accion propia de la app
      if(!(mkApp && mkApp->extra && mkApp->extra(act, mkMenuId))) mkRedraw();
      return;
  }
  int n = mkTargets(mkIds, &anyLocked, &anyOpen);
  if(n == 0){ mkRedraw(); return; }
  switch(act){
    case MA_LOCK: {
      // Sin PIN ni contrasena en el sistema no hay con que proteger: se dice
      // y se ofrece el camino, sin bloquear nada.
      if(gLockType == 0){ mkRedraw(); mkNoLockDlg = true; mediaNoLockDialog(); return; }
      int m = 0;
      for(int k = 0; k < n; k++){ FlexMlRec r; if(mlGet(mkIds[k], &r) && !(r.flags & FML_R_LOCKED)) mkIds[m++] = mkIds[k]; }
      mediaAuthRequest(MA_LOCK, mkIds, m, mkAuthDone);
      return;
    }
    case MA_UNLOCK: {
      int m = 0;
      for(int k = 0; k < n; k++){ FlexMlRec r; if(mlGet(mkIds[k], &r) && (r.flags & FML_R_LOCKED)) mkIds[m++] = mkIds[k]; }
      mediaAuthRequest(MA_UNLOCK, mkIds, m, mkAuthDone);
      return;
    }
    case MA_TRASH: {
      if(anyLocked){ sysNotify(mkName(), "Lo protegido no va a la papelera: usa Borrar"); mkRedraw(); return; }
      int done = 0;
      for(int k = 0; k < n; k++) done += mlTrash(mkIds[k]) ? 1 : 0;
      mkExitMulti();
      char msg[48]; snprintf(msg, sizeof(msg), "%d a la papelera", done);
      sysNotify(mkName(), msg);
      mkRedraw();
      return;
    }
    case MA_DELETE: {
      char sub[64];
      if(n == 1 && !anyLocked){ FlexMlRec r; mlGet(mkIds[0], &r); flexMlDisplayName(&r, sub, sizeof(sub)); }
      else snprintf(sub, sizeof(sub), "%d elemento%s", n, n == 1 ? "" : "s");
      mkAskAct = MA_DELETE;
      mkRedraw();
      fkAskOpen("\xC2\xBF" "Borrar definitivamente?", sub);
      return;
    }
    case MA_RENAME: {
      if(anyLocked || n != 1){ mkRedraw(); return; }
      FlexMlRec r; mlGet(mkIds[0], &r);
      char stem[FLEXFS_NAME_MAX]; flexFsStem(r.name[0] ? r.name : r.path, stem, sizeof(stem));
      mkRenameId = mkIds[0];
      fkNameOpen("Renombrar", stem);
      return;
    }
  }
  mkRedraw();
}

// -------------------------------------------------------------
//  CAPAS MODALES (una vuelta del tick de la app)
//  ------------------------------------------------------------
//  true = una capa se llevo este cuadro: la app no hace nada mas.
// -------------------------------------------------------------
static bool mkTick(){
  if(webSheetTick()) return true;                   // "Conectar con el movil" abierta
  if(fkTrashOn){ if(!fkTrashTick()){ mlRequestScan(); mkRedraw(); } return true; }
  if(fkNameOn){
    int r = fkNameTick();
    if(r == 1 && mkRenameId){
      char why[64];
      if(!mlRename(mkRenameId, fkNameBuf, why, sizeof(why))) sysNotify(mkName(), why);
    }
    if(r != 0){ mkRenameId = 0; mkRedraw(); }
    return true;
  }
  if(fkAskOn){
    int r = fkAskTick();
    if(r == 1 && mkAskAct == MA_DELETE){
      bool anyLocked = false, anyOpen = false;
      int n = mkTargets(mkIds, &anyLocked, &anyOpen);
      mkAskAct = MA_NONE;
      // Borrar algo protegido pide la clave; lo demas se borra ya.
      if(anyLocked){ mediaAuthRequest(MA_DELETE, mkIds, n, mkAuthDone); return true; }
      mkAuthDone(true, MA_DELETE, mkIds, n);
      return true;
    }
    if(r != 0){ mkAskAct = MA_NONE; mkRedraw(); }
    return true;
  }
  if(mmDlgOn){
    int r = mmDlgTick();
    if(r != 0){
      bool toSec = (r == 1 && mkNoLockDlg);
      mkNoLockDlg = false;
      if(toSec){ settingsJumpSecurity(); return true; }
      mkRedraw();
    }
    return true;
  }
  if(mmOn){
    mmAnimTick();
    if(T.tap){
      int a = mmHit(T.x, T.y);
      if(a == 0) return true;                        // dentro, entre filas
      mmOn = false;
      if(a > 0) mkDoAction(a); else mkRedraw();
    }
    return true;
  }
  return false;
}

// ATRAS: primero las capas, luego la seleccion. true = habia algo que cerrar.
static bool mkBackLayer(){
  if(webSheetIsOpen()){ webSheetDismiss(); mkRedraw(); return true; }
  if(mmOn || mmDlgOn || fkNameOn || fkAskOn || fkTrashOn){ mkCloseLayers(); mkRedraw(); return true; }
  if(mkMulti){ mkExitMulti(); mkRedraw(); return true; }
  return false;
}

// Encima de lo que la app acaba de pintar (antes de su flxFlush).
static void mkDrawOverlays(){
  if(mmOn){ mmDraw(1.0f); mmAnimDone = true; }
  if(mmDlgOn) mmDlgDraw();
  if(fkAskOn) fkAskDraw();
}

// -------------------------------------------------------------
//  BARRA DE SELECCION MULTIPLE (abajo, dentro del area de la app)
// -------------------------------------------------------------
#define MKB_H 84
static int mkBarTop(){ int bx, by, bw, bh; uiBox(bx, by, bw, bh); return by + bh - MKB_H - 12; }

static void mkDrawBar(int selLocked, int selOpen, int selectable){
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  int y = mkBarTop();
  if(uiGlass) drawLiquidGlassPanel(bx + 10, y, bw - 20, MKB_H, 20, TH_GLASS2);
  else        fillRoundRect(bx + 10, y, bw - 20, MKB_H, 20, TH_SURF2);
  bool all = selectable > 0 && selOpen == selectable && !selLocked;
  fillRoundRect(bx + 24, y + 10, 150, 28, 14, TH_SURF);
  drawTextC(bx + 24 + 75, y + 17, all ? "Quitar selecci\xC3\xB3n" : "Seleccionar todo", 1, TH_TXT);
  drawTextR(bx + bw - 28, y + 17, "Salir", 2, TH_TXT2);
  int ay = y + 46, sw = (bw - 40) / 3;
  const char* lb[3] = { selLocked && !selOpen ? "Desbloquear" : "Bloquear", "Papelera", "Borrar" };
  uint16_t col[3] = { TH_TXT, selLocked ? TH_MUTE : TH_TXT, rgb565(228, 70, 70) };
  for(int k = 0; k < 3; k++){
    bool on = mkSelN > 0 && !(k == 1 && selLocked);
    drawTextC(bx + 20 + k * sw + sw / 2, ay + 6, lb[k], 2, on ? col[k] : TH_MUTE);
  }
}
// Que se toco de la barra: MKB_ALL, MKB_EXIT, MKB_LOCKTOGGLE, MA_TRASH,
// MA_DELETE; MKB_NONE si fue en la barra sin acierto; -1 si fuera de ella.
static int mkBarHit(int tx, int ty){
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  int y = mkBarTop();
  if(ty < y || ty > y + MKB_H) return -1;
  if(ty < y + 42){
    if(tx < bx + 180) return MKB_ALL;
    if(tx > bx + bw - 100) return MKB_EXIT;
    return MKB_NONE;
  }
  int sw = (bw - 40) / 3, k = (tx - bx - 20) / (sw > 0 ? sw : 1);
  if(mkSelN == 0 || k < 0 || k > 2) return MKB_NONE;
  if(k == 0) return MKB_LOCKTOGGLE;
  return k == 1 ? (int)MA_TRASH : (int)MA_DELETE;
}
// Atiende un toque en la barra. true = era de la barra.
static bool mkBarTouch(void (*selectAll)()){
  int r = mkBarHit(T.x, T.y);
  if(r < 0) return false;
  if(r == MKB_ALL){ if(selectAll) selectAll(); return true; }
  if(r == MKB_EXIT){ mkExitMulti(); mkRedraw(); return true; }
  if(r != MKB_NONE){ mkMenuId = 0; mkDoAction(r); }
  return true;
}
