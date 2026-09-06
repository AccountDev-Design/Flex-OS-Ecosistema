// #############################################################
// ##  FLEX OS ULTRA  ·  APP NOTAS
// ##  ----------------------------------------------------------
// ##  Lista de notas reales en /Notas, editor y continuidad de sesion.
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
// ##  APP NOTAS  ·  lista de notas REALES en /Notas
// ##  ------------------------------------------------------
// ##  Cada tarjeta es un fichero .txt de verdad: el titulo sale
// ##  del NOMBRE del fichero y la vista previa de sus primeros
// ##  bytes, leidos del disco. No hay texto de relleno en
// ##  ninguna parte -- una nota vacia se ve vacia.
// ##
// ##  El editor sigue siendo el de siempre (teclado de 4 capas,
// ##  seleccion, portapapeles): lo unico que cambia es que
// ##  ahora esta ATADO a un fichero. Se carga al abrirlo y se
// ##  guarda al salir y 2 s despues de la ultima tecla, para
// ##  que un apagon no se lleve lo escrito.
// #############################################################
#define NOTE_MAX_LIST   16
#define NOTE_PREV_LEN    96
#define NOTE_COLS        2
#define NOTE_CARD_TOP   96
#define NOTE_AUTOSAVE_MS 2000

static FlexFsEntry noteList[NOTE_MAX_LIST];
static char        notePrev[NOTE_MAX_LIST][NOTE_PREV_LEN];
static int         noteListN = 0;
static int         noteView  = 0;                        // 0 = lista, 1 = editor
static int         noteSelIdx = -1;                      // elemento sobre el que actua el menu
static char        notePath[FLEXFS_PATH_MAX] = "";       // fichero abierto en el editor
static int         noteScroll = 0;
static bool        noteMulti = false;                    // modo "Seleccionar"
static uint32_t    noteMask = 0;                         // marcados en modo seleccion
static uint32_t    noteDirtyMs = 0;                      // ultima tecla (autoguardado)
static bool        noteLongFired = false;
static int         noteDragY0 = 0, noteDragS0 = 0;
static bool        noteDragging = false;

static void noteRenderList();
static void noteEditorEnter();
static void noteEditorTick();
static int  noteMaxScroll();

// Relee /Notas del disco. Se llama al entrar y despues de CADA
// operacion: la lista siempre es un reflejo de lo que hay, nunca un
// estado en RAM que se actualiza "a mano" y se puede desincronizar.
static void noteReload(){
  noteListN = flexFsList(FLEXFS_DIR_NOTAS, noteList, NOTE_MAX_LIST);
  for(int i = 0; i < noteListN; i++){
    notePrev[i][0] = 0;
    if(noteList[i].dir) continue;
    char p[FLEXFS_PATH_MAX];
    snprintf(p, sizeof(p), "%s/%s", FLEXFS_DIR_NOTAS, noteList[i].name);
    flexFsReadText(p, notePrev[i], NOTE_PREV_LEN);      // contenido REAL del fichero
  }
  int maxS = noteMaxScroll();
  if(noteScroll < 0) noteScroll = 0;
  if(noteScroll > maxS) noteScroll = maxS;
}

static int noteCardH(){
  // Proporcional a la pantalla: el Pro tiene 640 px de alto logico y con una
  // altura fija de 300 solo cabia una fila.
  int h = (SCR_H - NOTE_CARD_TOP - 90) / 2 - 44;
  if(h < 120) h = 120;
  if(h > 300) h = 300;
  return h;
}
static void noteCardRect(int i, int &x, int &y, int &w, int &h){
  int col = i % NOTE_COLS, row = i / NOTE_COLS;
  w = (SCR_W - 3 * 16) / NOTE_COLS;
  h = noteCardH();
  x = 16 + col * (w + 16);
  y = NOTE_CARD_TOP + row * (h + 44) - noteScroll;
}

static int noteMaxScroll(){
  int rows = (noteListN + NOTE_COLS - 1) / NOTE_COLS;
  int need = NOTE_CARD_TOP + rows * (noteCardH() + 44) + 40;
  int m = need - (SCR_H - 60);
  return m > 0 ? m : 0;
}

// Boton flotante de nueva nota (abajo a la derecha, como en la captura).
static void noteFabRect(int &x, int &y, int &r){ r = 56; x = SCR_W - 84; y = SCR_H - 150; }

static void noteDrawFab(){
  int fx, fy, fr; noteFabRect(fx, fy, fr);
  fillCircle(fx, fy, fr, TH_BORDER);
  fillCircle(fx, fy, fr - 5, TH_SURF);
  fillRoundRect(fx - 22, fy - 24, 34, 44, 4, TH_TXT);
  for(int i = 0; i < 3; i++) fillRect(fx - 17, fy - 17 + i * 10, 24 - i * 6, 4, TH_SURF);
  strokeSegAA(fx + 4, fy + 18, fx + 24, fy - 6, 5.0f, TH_TXT);   // lapiz
  strokeSegAA(fx + 22, fy - 8, fx + 27, fy - 13, 4.0f, TH_TXT);
}

static void noteRenderList(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  // Cabecera comun: chevron centrado en su zona, titulo empezando DESPUES
  // de esa zona y tres puntos con su propio margen a la derecha. Antes el
  // titulo arrancaba en x=16 y el chevron ocupaba x=18..30, asi que la
  // flecha se dibujaba dentro de la "N" de "Notas".
  uiHdrDraw("Notas:", 5, TH_TXT, TH_NAV, true);

  // De aqui abajo manda el viewport. El titulo de cada tarjeta se dibuja
  // 34 px POR ENCIMA de ella, asi que una tarjeta a medio salir escribia
  // su nombre dentro de la cabecera aunque la tarjeta ya no se viera.
  uiClipViewport(UIHDR_ZONE + 4, SCR_H - 1);
  if(noteListN == 0){
    drawTextC(SCR_W / 2, 320, "No hay notas todav\xC3\xAD" "a", 3, TH_TXT2);
    drawTextC(SCR_W / 2, 364, "Pulsa el boton de abajo para crear una", 1, TH_MUTE);
  }
  for(int i = 0; i < noteListN; i++){
    int x, y, w, h; noteCardRect(i, x, y, w, h);
    if(y + h < 60 || y > SCR_H) continue;
    char title[FLEXFS_NAME_MAX];
    flexFsStem(noteList[i].name, title, sizeof(title));      // titulo = nombre REAL del fichero
    drawTextC(x + w / 2, y - 34, title, 2, TH_TXT);
    if(uiGlass) drawGlassCardFlat(x, y, w, h, 14, TH_GLASS, TH_PAGE);
    else fillRoundRect(x, y, w, h, 14, TH_SURF);
    if(noteMulti && (noteMask & (1UL << i))){
      drawRoundRect(x, y, w, h, 14, TH_PRIM);
      drawRoundRect(x + 1, y + 1, w - 2, h - 2, 13, TH_PRIM);
      fillCircle(x + w - 20, y + 20, 11, TH_PRIM);
      strokeSegAA(x + w - 26, y + 20, x + w - 22, y + 25, 2.4f, TH_ONACC);
      strokeSegAA(x + w - 22, y + 25, x + w - 14, y + 14, 2.4f, TH_ONACC);
    }
    if(notePrev[i][0]) fkTextBox(x + 10, y + 10, w - 20, h - 20, notePrev[i], 2, TH_TXT);
    else               drawText(x + 10, y + 10, "(vac\xC3\xAD" "a)", 2, TH_MUTE);
  }
  uiClipFull();                     // barra de seleccion, boton flotante y menu, encima
  if(noteMulti){
    // Barra de acciones del modo seleccion: opera sobre TODOS los marcados.
    int by = SCR_H - 128;
    if(uiGlass) drawLiquidGlassPanel(12, by, SCR_W - 24, 60, 16, TH_GLASS2);
    else fillRoundRect(12, by, SCR_W - 24, 60, 16, TH_SURF2);
    drawText(28, by + 20, "Selecci\xC3\xB3n", 2, TH_TXT);
    drawTextR(SCR_W - 140, by + 20, "Papelera", 2, TH_WARN);
    drawTextR(SCR_W - 28,  by + 20, "Salir", 2, TH_TXT2);
  } else {
    noteDrawFab();
  }
  if(fkMenuOn) fkMenuDraw();
  flxFlushAll();
}

// Ruta completa del elemento i de la lista.
static void notePathOf(int i, char* out, size_t n){
  snprintf(out, n, "%s/%s", FLEXFS_DIR_NOTAS, noteList[i].name);
}

// Abre el editor sobre un fichero REAL: lo lee entero a la memoria de
// trabajo (PSRAM) y deja el cursor al final.
static void noteOpen(int i){
  if(i < 0 || i >= noteListN) return;
  notePathOf(i, notePath, sizeof(notePath));
  noteBufInit();
  flexFsReadText(notePath, noteBuffer, noteBufMax);
  flexFsStem(noteList[i].name, noteTitleBar, sizeof(noteTitleBar));
  noteView = 1; noteDirtyMs = 0;
  noteEditorEnter();
  sessMarkDirty(IC_NOTAS);
}

// Guardado REAL del editor al fichero abierto.
static void noteSave(){
  if(noteView != 1 || !notePath[0]) return;
  flexFsWriteText(notePath, noteBuffer);
  noteDirtyMs = 0;
}

static void noteBackToList(){
  noteSave();
  noteView = 0; notePath[0] = 0;
  noteReload();
  noteRenderList();
  sessMarkDirty(IC_NOTAS);
}

// Nueva nota: crea el FICHERO ya, vacio, con el primer numero libre de
// la carpeta. Si el fichero no se crea (disco lleno), no aparece nada en
// la lista -- porque no existe.
static void noteNew(){
  char full[FLEXFS_PATH_MAX];
  if(!flexFsNewName(FLEXFS_DIR_NOTAS, "Sin t\xC3\xAD" "tulo", FLEXFS_EXT_NOTE, full, sizeof(full))) return;
  if(!flexFsWriteText(full, "")) return;
  noteReload();
  for(int i = 0; i < noteListN; i++){
    char p[FLEXFS_PATH_MAX]; notePathOf(i, p, sizeof(p));
    if(!strcmp(p, full)){ noteOpen(i); return; }
  }
  noteRenderList();
}

// Accion del menu contextual sobre la nota seleccionada. TODAS tocan el
// fichero real; ninguna se limita a cambiar la pantalla.
static void noteMenuAction(int act){
  char p[FLEXFS_PATH_MAX];
  if(noteSelIdx >= 0 && noteSelIdx < noteListN) notePathOf(noteSelIdx, p, sizeof(p));
  else p[0] = 0;
  if(act == FK_ACT_SEL){
    noteMulti = true; noteMask = 0;
    if(noteSelIdx >= 0) noteMask |= (1UL << noteSelIdx);
  } else if(act == FK_ACT_DEL){
    if(p[0]){
      char stem[FLEXFS_NAME_MAX]; flexFsStem(noteList[noteSelIdx].name, stem, sizeof(stem));
      fkAskOpen("\xC2\xBF" "Borrar definitivamente?", stem);
      return;                                  // el borrado ocurre al confirmar
    }
  } else if(act == FK_ACT_REN){
    if(p[0]){
      char stem[FLEXFS_NAME_MAX]; flexFsStem(noteList[noteSelIdx].name, stem, sizeof(stem));
      fkNameOpen("Renombrar nota", stem);
      return;
    }
  } else if(act == FK_ACT_TRASH){
    if(p[0]){ flexFsTrash(p); noteSelIdx = -1; noteReload(); }  // a /Papelera de verdad
    else { fkTrashOpen(); return; }            // sin seleccion: abre la papelera
  } else if(act == FK_ACT_VAULT){
    // FLEX VAULT: la nota se cifra y se mueve DENTRO de la boveda. Deja de
    // existir en /Notas, asi que desaparece de esta lista y solo se abre desde
    // Flex Vault. Si la boveda esta cerrada, vaultMoveRequest se lleva la
    // pantalla para pedir la clave y completa el movimiento despues.
    if(p[0]){
      noteSelIdx = -1;
      if(vaultMoveRequest(p, FXV_KIND_NOTE)){
        if(gState == ST_VAULT) return;         // se fue a pedir la clave
        noteReload();
      }
    }
  }
  noteRenderList();
}

static void noteListTick(){
  // --- Dialogos modales: se comen el toque hasta que se cierran ---
  if(fkTrashOn){ if(!fkTrashTick()){ noteReload(); noteRenderList(); } return; }
  if(fkAskOn){
    int r = fkAskTick();
    if(r == 1 && noteSelIdx >= 0 && noteSelIdx < noteListN){
      char p[FLEXFS_PATH_MAX]; notePathOf(noteSelIdx, p, sizeof(p));
      flexFsDelete(p);                          // borrado DEFINITIVO real
      noteSelIdx = -1; noteReload();
    }
    if(r != 0) noteRenderList();
    return;
  }
  if(fkNameOn){
    int r = fkNameTick();
    if(r == 1 && noteSelIdx >= 0 && noteSelIdx < noteListN){
      char p[FLEXFS_PATH_MAX]; notePathOf(noteSelIdx, p, sizeof(p));
      flexFsRename(p, fkNameBuf);               // renombrado REAL en el disco
      noteSelIdx = -1; noteReload();
    }
    if(r != 0) noteRenderList();
    return;
  }
  if(fkMenuOn){
    if(T.tap){
      int a = fkMenuHit(T.x, T.y);
      fkMenuOn = false;
      if(a >= 0) noteMenuAction(a);
      else       noteRenderList();
    }
    return;
  }

  // --- Scroll ---
  // T.dx/T.dy solo se rellenan AL SOLTAR (ver tDoRelease), asi que el arrastre
  // se sigue con el mismo patron que la lista de Ajustes: ancla al presionar y
  // delta vivo contra esa ancla. Umbral de 8 px para no confundir toque con
  // arrastre.
  int maxS = noteMaxScroll();
  if(T.pressed){ noteDragY0 = T.y; noteDragS0 = noteScroll; noteDragging = false; }
  if(T.down && maxS > 0){
    int dy = noteDragY0 - T.y;
    if(!noteDragging && abs(dy) > 8) noteDragging = true;
    if(noteDragging){
      int ns = noteDragS0 + dy;
      if(ns < 0) ns = 0; if(ns > maxS) ns = maxS;
      if(ns != noteScroll){ noteScroll = ns; noteRenderList(); }
      noteLongFired = true;
      return;
    }
  }
  if(T.released && noteDragging){ noteDragging = false; sessMarkDirty(IC_NOTAS); return; }

  // --- Long-press sobre una tarjeta -> menu contextual ---
  if(T.down && !noteLongFired && (millis() - T.downMs) > 550
     && abs(T.x - T.startX) < 14 && abs(T.y - T.startY) < 14){
    for(int i = 0; i < noteListN; i++){
      int x, y, w, h; noteCardRect(i, x, y, w, h);
      if(T.startX >= x && T.startX <= x + w && T.startY >= y && T.startY <= y + h){
        noteLongFired = true; noteSelIdx = i;
        fkMenuOpenV(T.x, T.y - 40, true);   // con "Mover a Carpeta segura"
        return;
      }
    }
    noteLongFired = true;
  }
  if(!T.down) noteLongFired = false;

  if(!T.tap) return;

  // --- Tres puntos de la cabecera ---
  if(uiHdrMenuHit(T.x, T.y)){ noteSelIdx = -1; fkMenuOpen(SCR_W - 40, UIHDR_ZONE); return; }
  // --- Volver al escritorio ---
  if(uiHdrBackHit(T.x, T.y)){ appClose(); return; }

  // --- Barra del modo seleccion ---
  if(noteMulti){
    int by = SCR_H - 128;
    if(T.y >= by && T.y <= by + 60){
      if(T.x > SCR_W - 100){ noteMulti = false; noteMask = 0; noteRenderList(); return; }
      if(T.x > SCR_W - 230){
        for(int i = 0; i < noteListN; i++) if(noteMask & (1UL << i)){
          char p[FLEXFS_PATH_MAX]; notePathOf(i, p, sizeof(p));
          flexFsTrash(p);                        // a la papelera, de verdad, uno a uno
        }
        noteMulti = false; noteMask = 0; noteReload(); noteRenderList(); return;
      }
    }
  } else {
    int fx, fy, fr; noteFabRect(fx, fy, fr);
    long ddx = T.x - fx, ddy = T.y - fy;
    if(ddx * ddx + ddy * ddy <= (long)fr * fr){ noteNew(); return; }
  }

  // --- Tarjetas ---
  for(int i = 0; i < noteListN; i++){
    int x, y, w, h; noteCardRect(i, x, y, w, h);
    if(T.x >= x && T.x <= x + w && T.y >= y && T.y <= y + h){
      if(noteMulti){ noteMask ^= (1UL << i); noteRenderList(); }
      else noteOpen(i);
      return;
    }
  }
}

// ---- Puntos de entrada de la app (los que ve APP_REG) ----
static void noteEnter(){
  noteBufInit();
  if(!flexFsReady()){ fkNoFsScreen("Notas"); return; }
  bool restoreEditor = (noteView == 1 && notePath[0] && flexFsExists(notePath));
  noteMulti = false; noteMask = 0; noteSelIdx = -1;
  fkMenuOn = false; fkNameOn = false; fkAskOn = false; fkTrashOn = false;
  if(restoreEditor){
    flexFsReadText(notePath, noteBuffer, noteBufMax);
    flexFsStem(notePath, noteTitleBar, sizeof(noteTitleBar));
    noteResume();
    return;
  }
  noteView = 0; notePath[0] = 0;
  noteReload();
  noteRenderList();
}

static void noteTick(){
  if(!flexFsReady()){ if(T.tap && T.x < 60 && T.y < 60) appClose(); return; }
  if(noteView == 0){ noteListTick(); return; }

  // Editor: la esquina superior izquierda vuelve a la lista GUARDANDO.
  if(T.tap && T.x < 60 && T.y < 44 && !clipPanelOn && !noteMenu && !kbPopup && !kbMoreOn){
    noteBackToList(); return;
  }
  int lenBefore = strlen(noteBuffer);
  noteEditorTick();
  // Autoguardado: 2 s despues de la ultima modificacion real del texto.
  if((int)strlen(noteBuffer) != lenBefore){ noteDirtyMs = millis(); sessMarkDirty(IC_NOTAS); }
  if(noteDirtyMs && millis() - noteDirtyMs > NOTE_AUTOSAVE_MS) noteSave();
}

// #############################################################
// ##  NOTAS · CICLO DE VIDA Y CONTINUIDAD
// #############################################################
#define NOTE_SESS_VER   3
#define NOTE_SESS_PATH  FS_DIR_SESS "/notas.bin"
struct NoteSessV3 {
  uint8_t view;
  uint8_t kbLayout;
  uint8_t kbFlags;
  uint8_t reserved;
  uint16_t cursor;
  int16_t selA, selB;
  int32_t editorScroll;
  int32_t listScroll;
  char path[FLEXFS_PATH_MAX];
};
static NoteSessV3 gNoteSess;

// Estado privado del teclado de Notas. bits de noteKbFlags:
// 0 idioma ES · 1 mayusculas · 2 portapapeles abierto · 3 menu "mas"
// · 4 menu contextual de seleccion.
static void noteCaptureKbState(){
  if(mapaActivo == LAYOUT_EN)         noteKbLayout = 1;
  else if(mapaActivo == LAYOUT_NUM)   noteKbLayout = 2;
  else if(mapaActivo == LAYOUT_EMOJI) noteKbLayout = 3;
  else                                noteKbLayout = 0;
  noteKbFlags = (kbLangEs ? 1u : 0u) | (kbShift ? 2u : 0u) |
                (clipPanelOn ? 4u : 0u) | (kbMoreOn ? 8u : 0u) |
                (noteMenu ? 16u : 0u);
  noteKbStateValid = true;
}
static void noteRestoreKbState(){
  if(!noteKbStateValid){ noteKbLayout = 0; noteKbFlags = 1; }
  switch(noteKbLayout){
    case 1: mapaActivo = LAYOUT_EN; break;
    case 2: mapaActivo = LAYOUT_NUM; break;
    case 3: mapaActivo = LAYOUT_EMOJI; break;
    default: mapaActivo = LAYOUT_ES; break;
  }
  kbLangEs = (noteKbFlags & 1u) != 0;
  kbShift = (noteKbFlags & 2u) != 0;
  clipPanelOn = (noteKbFlags & 4u) != 0;
  kbMoreOn = (noteKbFlags & 8u) != 0;
  noteMenu = (noteKbFlags & 16u) != 0 && noteHasSel();
  noteKbStateValid = true;
}

static bool noteSaveSess(){
  if(!flexFsReady()) return true;
  if(noteView == 1) noteSave();
  noteCaptureKbState();
  memset(&gNoteSess, 0, sizeof(gNoteSess));
  gNoteSess.view = (noteView == 1 && notePath[0]) ? 1 : 0;
  gNoteSess.kbLayout = noteKbLayout;
  gNoteSess.kbFlags = noteKbFlags;
  int L = (int)strlen(noteBuffer);
  int c = noteCur; if(c < 0) c = 0; if(c > L) c = L;
  gNoteSess.cursor = (uint16_t)c;
  gNoteSess.selA = (int16_t)noteSelA;
  gNoteSess.selB = (int16_t)noteSelB;
  gNoteSess.editorScroll = noteEditorScroll;
  gNoteSess.listScroll = noteScroll;
  snprintf(gNoteSess.path, sizeof(gNoteSess.path), "%s", notePath);
  return sessWrite(NOTE_SESS_PATH, NOTE_SESS_VER, IC_NOTAS, &gNoteSess, sizeof(gNoteSess));
}

static void noteLoadSess(){
  memset(&gNoteSess, 0, sizeof(gNoteSess));
  if(!flexFsReady()) return;
  if(sessRead(NOTE_SESS_PATH, NOTE_SESS_VER, IC_NOTAS, &gNoteSess, sizeof(gNoteSess)) != sizeof(gNoteSess)) return;
  gNoteSess.path[sizeof(gNoteSess.path) - 1] = 0;
  noteView = (gNoteSess.view == 1 && gNoteSess.path[0] && flexFsExists(gNoteSess.path)) ? 1 : 0;
  noteKbLayout = gNoteSess.kbLayout < 4 ? gNoteSess.kbLayout : 0;
  noteKbFlags = gNoteSess.kbFlags;
  noteKbStateValid = true;
  noteEditorScroll = gNoteSess.editorScroll > 0 ? gNoteSess.editorScroll : 0;
  noteScroll = gNoteSess.listScroll > 0 ? gNoteSess.listScroll : 0;
  noteCur = gNoteSess.cursor;
  noteSelA = gNoteSess.selA;
  noteSelB = gNoteSess.selB;
  snprintf(notePath, sizeof(notePath), "%s", noteView == 1 ? gNoteSess.path : "");
}

static bool noteBackLayer(){
  if(kbPopup){ kbPopup = false; kbLpKey = -1; noteRenderAll(); return true; }
  if(kbMoreOn){ kbMoreOn = false; noteRenderAll(); return true; }
  if(clipPanelOn){ clipPanelOn = false; clipAskClear = false; noteRenderAll(); return true; }
  if(noteMenu || noteHasSel()){ noteClearSel(); noteRenderAll(); return true; }
  if(fkMenuOn){ fkMenuOn = false; if(noteView == 0) noteRenderList(); else noteRenderAll(); return true; }
  return false;
}

static bool noteBackScreen(){
  if(noteView != 1) return false;
  noteBackToList();
  sessMarkDirty(IC_NOTAS);
  return true;
}

static void noteSuspend(){
  if(noteView == 1) noteSave();
  noteCaptureKbState();
  noteKbAnim = 0; kbToastMs = 0; kbChipMs = 0;
  // Los dialogos de archivo son una superficie GLOBAL compartida con Paint,
  // Galeria y Archivos; no pueden quedar vivos mientras otra app la reutiliza.
  // El editor, teclado, cursor, seleccion y scroll si permanecen intactos.
  fkMenuOn = false; fkNameOn = false; fkAskOn = false; fkTrashOn = false;
  noteDragging = false; noteHandleDrag = 0;
  kbMtSurfaceReset();
}

static void noteResume(){
  if(noteView == 1 && notePath[0] && flexFsExists(notePath)){
    noteRestoreKbState();
    kbExtrasOn = true; kbApplySize(); kbMtSurfaceReset();
    kbBotReserve = navBarVisible() ? NAV_H : 0;
    noteKbAnim = 0;
    int L = (int)strlen(noteBuffer);
    if(noteCur < 0 || noteCur > L) noteCur = L;
    if(noteSelA < 0 || noteSelB <= noteSelA || noteSelB > L) noteClearSel();
    noteEditorClampScroll();
    noteRenderAll();
  } else {
    noteView = 0; notePath[0] = 0;
    noteReload(); noteRenderList();
  }
}

// CAMBIOS SIN GUARDAR. noteDirtyMs es el instante de la ultima tecla y se pone
// a 0 en cuanto noteSave() escribe: mientras no sea 0 hay texto que todavia no
// esta en disco. Es la misma marca que ya gobierna el autoguardado, no una
// segunda contabilidad que pudiera contradecirla.
static bool noteDirty(){ return noteView == 1 && noteDirtyMs != 0; }

static void noteCloseApp(){
  noteSuspend();
  noteClearSel();
  kbPopup = false; kbLpKey = -1; kbMoreOn = false; clipPanelOn = false;
  noteKbStateValid = false; noteKbLayout = 0; noteKbFlags = 0;
  if(noteBuffer != noteBufStatic){
    free(noteBuffer);
    noteBuffer = noteBufStatic;
    noteBufMax = sizeof(noteBufStatic);
    noteBufStatic[0] = 0;
  }
  noteView = 0; notePath[0] = 0; noteScroll = 0; noteEditorScroll = 0;
  gSessLoaded[IC_NOTAS] = false;
}
