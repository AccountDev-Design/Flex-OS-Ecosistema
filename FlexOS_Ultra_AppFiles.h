// #############################################################
// ##  FLEX OS ULTRA  ·  EXPLORADOR DE ARCHIVOS  (ST_FILES)
// ##  ----------------------------------------------------------
// ##  Un solo explorador para los dos volumenes; en la tarjeta solo se lee.
// ##  Abrir un archivo lo manda a la app que corresponda.
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
#include "FlexOS_Ultra_AppStorage.h"   // eslabon anterior de la cadena

// #############################################################
// ##  EXPLORADOR DE ARCHIVOS  (ST_FILES)
// ##  ------------------------------------------------------
// ##  Lista lo que hay. El numero de elementos de cada carpeta
// ##  se obtiene ABRIENDO la carpeta y contando (flexFsCount
// ##  dentro de flexFsList), no de una tabla fija: por eso un
// ##  "6 elementos" baja a 5 en cuanto se borra uno.
// ##
// ##  El menu contextual es el mismo kit que usan Notas y Paint,
// ##  asi que las cuatro acciones se comportan igual en las tres
// ##  pantallas y operan sobre el fichero real.
// ##
// ##  DOS VOLUMENES, UN SOLO EXPLORADOR
// ##  ---------------------------------
// ##  La ruta lleva el volumen dentro: lo que empieza por /sdcard
// ##  es de la tarjeta y el resto de la particion interna. Por eso
// ##  no hace falta un explorador aparte ni una bandera al lado de
// ##  cada ruta, que es como se acaba borrando en el volumen
// ##  equivocado.
// ##
// ##  EN LA TARJETA SOLO SE LEE. Borrar, renombrar, mover a la
// ##  papelera y cifrar en la boveda quedan DESACTIVADOS sobre
// ##  /sdcard, y se dice por que. Son los archivos del usuario --
// ##  sus fotos, sus descargas -- y esta version no se mete con
// ##  ellos: se indexan y se abren, no se tocan. (La papelera y la
// ##  boveda ademas viven en la particion interna, asi que "mover"
// ##  ahi seria copiar entre volumenes, que es otra funcion.)
// ##
// ##  ABRIR UN ARCHIVO lo manda a la app que corresponde: una
// ##  imagen a Galeria, un video o un audio compatible a
// ##  Multimedia, y lo incompatible a una notificacion con el
// ##  motivo concreto -- no se intenta abrir "a ver si suena".
// #############################################################
#define FILES_MAX     24
#define FILES_TOP    128
// Primera fila del area con scroll: justo debajo de la cabecera comun y
// de la linea de la ruta.
#define FILES_VP_TOP (UIHDR_H + 34)
#define FILES_RH      70

static FlexFsEntry filesList[FILES_MAX];
static int         filesN = 0;
static char        filesDir[FLEXFS_PATH_MAX] = "/";
static int         filesSelIdx = -1;
static int         filesScroll = 0;
static int         filesDragY0 = 0, filesDragS0 = 0;
static bool        filesDragging = false, filesLongFired = false;
static bool        filesMulti = false;
static uint32_t    filesMask = 0;

static void filesRender();

// true si la carpeta que se esta viendo esta en la tarjeta.
static inline bool filesOnSd(){ return flexSdIsSdPath(filesDir); }

static void filesReload(){
  int n = mediaList(filesDir, filesList, FILES_MAX);
  // -1 = el volumen no esta (tarjeta retirada). Se distingue de una
  // carpeta vacia para poder decir cosas distintas.
  filesN = n > 0 ? n : 0;
  if(filesSelIdx >= filesN) filesSelIdx = -1;
  int maxRows = filesN;
  if(filesScroll > maxRows * FILES_RH) filesScroll = 0;
}

static void filesPathOf(int i, char* out, size_t n){
  if(!strcmp(filesDir, "/")) snprintf(out, n, "/%s", filesList[i].name);
  else                       snprintf(out, n, "%s/%s", filesDir, filesList[i].name);
}

// Fila 0 = ".." cuando no estamos en la raiz. Se cuenta en el layout para
// que el indice de la lista y la fila dibujada no puedan desalinearse.
// La raiz de cada volumen es su tope: desde /sdcard no se "sube" a
// la particion interna, que es un sitio distinto y no su padre.
static bool filesHasUp(){
  return strcmp(filesDir, "/") != 0 && strcmp(filesDir, FLEXSD_MOUNT) != 0;
}
static int  filesRowY(int row){ return FILES_TOP + row * FILES_RH - filesScroll; }
static int  filesMaxScroll(){
  int rows = filesN + (filesHasUp() ? 1 : 0);
  int need = FILES_TOP + rows * FILES_RH + 30;
  int m = need - (SCR_H - 80);
  return m > 0 ? m : 0;
}

static void filesRender(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  uiHdrDraw(filesOnSd() ? "Tarjeta SD:" : "Archivos:", 5, TH_TXT, TH_NAV, true);
  drawTextClip(16, UIHDR_H + 8, filesDir, 2, TH_TXT2, SCR_W - 60);

  // Cabecera y ruta son FIJAS; de FILES_VP_TOP para abajo manda el
  // viewport. Sin este recorte, una fila a medio salir por arriba se
  // dibujaba entera -- su tarjeta tapaba la ruta y el titulo --, porque
  // el unico filtro miraba el borde de abajo (y + FILES_RH < 40).
  uiClipViewport(FILES_VP_TOP, SCR_H - 1);
  int row = 0;
  if(filesHasUp()){
    int y = filesRowY(row++);
    if(y > 40 && y < SCR_H - 60){
      if(uiGlass) drawGlassCardFlat(12, y, SCR_W - 24, FILES_RH - 8, 12, TH_GLASS, TH_PAGE);
      else fillRoundRect(12, y, SCR_W - 24, FILES_RH - 8, 12, TH_SURF);
      almFolderIcon(28, y + 12, 38);
      drawText(84, y + 18, "..", 3, TH_TXT);
      drawTextR(SCR_W - 28, y + 22, "subir", 1, TH_TXT2);
    }
  }
  for(int i = 0; i < filesN; i++){
    int y = filesRowY(row++);
    if(y + FILES_RH < 40 || y > SCR_H - 50) continue;
    bool sel = filesMulti && (filesMask & (1UL << i));
    uint16_t rowCol = sel ? TH_SEL : thCard();
    if(uiGlass && !sel) drawGlassCardFlat(12, y, SCR_W - 24, FILES_RH - 8, 12, TH_GLASS, TH_PAGE);
    else fillRoundRect(12, y, SCR_W - 24, FILES_RH - 8, 12, rowCol);
    if(filesList[i].dir) almFolderIcon(28, y + 12, 38);
    else {
      fillRoundRect(30, y + 10, 30, 42, 5, rgb565(250,250,252));
      fillRect(30, y + 10, 30, 8, rgb565(200,204,214));
    }
    drawTextClip(84, y + 10, filesList[i].name, 3, TH_TXT, SCR_W - 150);
    char sub[32];
    // 0xFFFF = "no se sabe". La tarjeta no cuenta los elementos de
    // cada subcarpeta a proposito: hacerlo obligaria a abrirlas todas
    // y entrar en DCIM costaria segundos. Se dice "Carpeta" en vez de
    // dar un numero que no se ha contado.
    if(filesList[i].dir && filesList[i].items == 0xFFFF) snprintf(sub, sizeof(sub), "Carpeta");
    else if(filesList[i].dir) snprintf(sub, sizeof(sub), "%u elementos", (unsigned)filesList[i].items);
    else                 flexFsFmtSize(filesList[i].size, sub, sizeof(sub));
    drawTextR(SCR_W - 28, y + 40, sub, 2, TH_TXT2);
  }
  if(filesN == 0)
    drawTextC(SCR_W / 2, 320, "Carpeta vac\xC3\xAD" "a", 3, TH_MUTE);
  uiClipFull();                       // la barra de seleccion y el menu van encima

  if(filesMulti){
    int by = SCR_H - 128;
    if(uiGlass) drawLiquidGlassPanel(12, by, SCR_W - 24, 60, 16, TH_GLASS2);
    else fillRoundRect(12, by, SCR_W - 24, 60, 16, TH_SURF2);
    drawText(28, by + 20, "Selecci\xC3\xB3n", 2, TH_TXT);
    drawTextR(SCR_W - 140, by + 20, "Papelera", 2, TH_WARN);
    drawTextR(SCR_W - 28,  by + 20, "Salir", 2, TH_TXT2);
  }
  if(fkMenuOn) fkMenuDraw();
  flxFlushAll();
}

static void filesEnterAt(const char* dir){
  const bool sd = flexSdIsSdPath(dir);
  if(!sd && !flexFsReady()){ fkNoFsScreen("Archivos"); gState = ST_FILES; return; }
  if(sd && !flexSdReady()){ fkNoFsScreen("Tarjeta SD"); gState = ST_FILES; return; }
  gState = ST_FILES;
  snprintf(filesDir, sizeof(filesDir), "%s", dir && dir[0] ? dir : "/");
  filesSelIdx = -1; filesScroll = 0; filesMulti = false; filesMask = 0;
  fkMenuOn = false; fkNameOn = false; fkAskOn = false; fkTrashOn = false;
  filesReload();
  filesRender();
}
static void filesEnter(){ filesEnterAt("/"); }

static void filesExit(){
  // Volver a Almacenamiento REPINTANDO: si desde aqui se borro o renombro
  // algo, los tamanos de la pantalla anterior ya no valen. Ademas hay que
  // rehacer el marco de la app entero (barra de estado y nav): el explorador
  // dibuja a pantalla completa y se lo ha llevado por delante.
  gState = ST_APP;
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, WIN_BG);
  appDrawChrome(gAppId);
  appDrawHeader(gAppId);
  almEnter();
  flxFlushAll();
}

static void filesGoUp(){
  char* s = strrchr(filesDir, '/');
  if(!s || s == filesDir){ snprintf(filesDir, sizeof(filesDir), "/"); }
  else *s = 0;
  // Nunca por encima de la raiz del volumen.
  if(filesDir[0] == 0) snprintf(filesDir, sizeof(filesDir), "/");
  if(flexSdIsSdPath(filesDir) && strlen(filesDir) < FLEXSD_MOUNT_LEN)
    snprintf(filesDir, sizeof(filesDir), FLEXSD_MOUNT);
  filesScroll = 0; filesSelIdx = -1; filesMulti = false; filesMask = 0;
  filesReload(); filesRender();
}

// -------------------------------------------------------------
//  ABRIR UN ARCHIVO DESDE EL EXPLORADOR
//  ------------------------------------------------------------
//  Cada clase va a su app. Lo que no se puede reproducir NO se
//  intenta: se avisa con el motivo concreto (que sale de
//  flexMediaUnsupportedReason, la misma tabla que usa el
//  reproductor), asi que el usuario sabe si el problema es el
//  formato o el archivo.
// -------------------------------------------------------------
static void galOpenPath(const char* path);          // Galeria (definida mas abajo)

static void filesOpenEntry(const char* path, const char* name){
  int kind = flexMediaClassify(name);
  if(kind == FLEXMED_PHOTO || kind == FLEXMED_DRAW
  || kind == FLEXMED_VIDEO || kind == FLEXMED_AUDIO){
    // ESTADO LOGICO PRIMERO. El explorador es una pantalla a
    // pantalla completa POR ENCIMA de Almacenamiento (ST_FILES), no
    // una app: sin volver a ST_APP, mediaOpenInPlayer no cerraria
    // Almacenamiento y quedarian dos apps abiertas a la vez.
    gState = ST_APP;
    gMediaReturnApp = IC_ALMACEN;          // se salio de aqui, aqui se vuelve
    mediaOpenInPlayer(path);
    return;
  }
  if(kind == FLEXMED_UNSUP){
    const char* why = flexMediaUnsupportedReason(name);
    mediaNotify(MOD_MEDIA, name, why ? why : "Formato no compatible");
    return;
  }
  // Ni medio ni conocido: se ofrecen las acciones de siempre sobre
  // el fichero, que es lo unico honesto que se puede hacer con el.
  fkMenuOpenV(SCR_W / 2 - 60, 200, true);
}

// En la tarjeta esta version SOLO LEE. Cuando una accion no aplica
// se dice por que, en vez de dejar un boton que no hace nada.
static bool filesSdReadOnlyGuard(){
  if(!filesOnSd()) return false;
  mediaNotify(MOD_SDCARD, "Solo lectura en la tarjeta",
              "Flex OS no borra ni mueve tus archivos");
  return true;
}

static void filesMenuAction(int act){
  char p[FLEXFS_PATH_MAX];
  if(filesSelIdx >= 0 && filesSelIdx < filesN) filesPathOf(filesSelIdx, p, sizeof(p));
  else p[0] = 0;
  if(act == FK_ACT_SEL){
    filesMulti = true; filesMask = 0;
    if(filesSelIdx >= 0) filesMask |= (1UL << filesSelIdx);
  } else if(act == FK_ACT_DEL){
    if(filesSdReadOnlyGuard()){ filesRender(); return; }
    if(p[0]){ fkAskOpen("\xC2\xBF" "Borrar definitivamente?", filesList[filesSelIdx].name); return; }
  } else if(act == FK_ACT_REN){
    if(filesSdReadOnlyGuard()){ filesRender(); return; }
    if(p[0]){
      char stem[FLEXFS_NAME_MAX]; flexFsStem(filesList[filesSelIdx].name, stem, sizeof(stem));
      fkNameOpen("Renombrar", stem);
      return;
    }
  } else if(act == FK_ACT_TRASH){
    if(filesSdReadOnlyGuard()){ filesRender(); return; }
    if(p[0]){ flexFsTrash(p); filesSelIdx = -1; filesReload(); }
    else { fkTrashOpen(); return; }               // sin seleccion: abre la papelera
  } else if(act == FK_ACT_VAULT){
    if(filesSdReadOnlyGuard()){ filesRender(); return; }
    // FLEX VAULT: el fichero se cifra dentro de la boveda y desaparece del
    // explorador. La clase se deduce de la extension, para que una foto acabe
    // en Galeria privada y un .txt en Notas privadas.
    if(p[0] && filesSelIdx >= 0 && !filesList[filesSelIdx].dir){
      filesSelIdx = -1;
      if(vaultMoveRequest(p, -1)){
        if(gState == ST_VAULT) return;            // se fue a pedir la clave
        filesReload();
      }
    }
  }
  filesRender();
}

static void filesTick(){
  // Se comprueba el volumen QUE SE ESTA VIENDO. Antes solo miraba
  // LittleFS, asi que navegando por la tarjeta con la particion
  // interna sana no se detectaba nada.
  if(filesOnSd()){
    if(!flexSdReady()){
      // La tarjeta se fue mientras se navegaba: se cierra la ruta y
      // se vuelve a la memoria interna, sin quedarse en una carpeta
      // que ya no existe.
      mediaNotify(MOD_SDCARD, "Tarjeta retirada", "Se cerro la carpeta abierta");
      snprintf(filesDir, sizeof(filesDir), "/");
      filesSelIdx = -1; filesScroll = 0; filesMulti = false; filesMask = 0;
      fkMenuOn = fkNameOn = fkAskOn = fkTrashOn = false;
      filesReload(); filesRender();
      return;
    }
  } else if(!flexFsReady()){
    if(T.tap && T.x < 60 && T.y < 60) filesExit();
    return;
  }
  if(fkTrashOn){ if(!fkTrashTick()){ filesReload(); filesRender(); } return; }
  if(fkAskOn){
    int r = fkAskTick();
    if(r == 1 && filesSelIdx >= 0 && filesSelIdx < filesN){
      char p[FLEXFS_PATH_MAX]; filesPathOf(filesSelIdx, p, sizeof(p));
      flexFsDelete(p);
      filesSelIdx = -1; filesReload();
    }
    if(r != 0) filesRender();
    return;
  }
  if(fkNameOn){
    int r = fkNameTick();
    if(r == 1 && filesSelIdx >= 0 && filesSelIdx < filesN){
      char p[FLEXFS_PATH_MAX]; filesPathOf(filesSelIdx, p, sizeof(p));
      flexFsRename(p, fkNameBuf);
      filesSelIdx = -1; filesReload();
    }
    if(r != 0) filesRender();
    return;
  }
  if(fkMenuOn){
    if(T.tap){
      int a = fkMenuHit(T.x, T.y);
      fkMenuOn = false;
      if(a >= 0) filesMenuAction(a);
      else       filesRender();
    }
    return;
  }

  int maxS = filesMaxScroll();
  if(T.pressed){ filesDragY0 = T.y; filesDragS0 = filesScroll; filesDragging = false; }
  if(T.down && maxS > 0){
    int dy = filesDragY0 - T.y;
    if(!filesDragging && abs(dy) > 8) filesDragging = true;
    if(filesDragging){
      int ns = filesDragS0 + dy;
      if(ns < 0) ns = 0; if(ns > maxS) ns = maxS;
      if(ns != filesScroll){ filesScroll = ns; filesRender(); }
      filesLongFired = true;
      return;
    }
  }
  if(T.released && filesDragging){ filesDragging = false; return; }

  int base = filesHasUp() ? 1 : 0;
  if(T.down && !filesLongFired && (millis() - T.downMs) > 550
     && abs(T.x - T.startX) < 14 && abs(T.y - T.startY) < 14){
    for(int i = 0; i < filesN; i++){
      int y = filesRowY(base + i);
      if(T.startY >= y && T.startY < y + FILES_RH - 8){
        filesLongFired = true; filesSelIdx = i;
        // Solo los FICHEROS pueden ir a la boveda: mover una carpeta entera
        // pediria cifrar su arbol, y prometerlo sin hacerlo seria peor que no
        // ofrecerlo. Ver vaultMoveRequest.
        fkMenuOpenV(T.x, T.y - 40, !filesList[i].dir);
        return;
      }
    }
    filesLongFired = true;
  }
  if(!T.down) filesLongFired = false;
  if(!T.tap) return;

  // Zonas tactiles de la cabecera: las MISMAS que dibuja uiHdrDraw. Se
  // preguntan por funcion para que no puedan separarse del dibujo.
  if(uiHdrMenuHit(T.x, T.y)){ filesSelIdx = -1; fkMenuOpen(SCR_W - 40, UIHDR_ZONE); return; }
  if(uiHdrBackHit(T.x, T.y)){ filesExit(); return; }

  if(filesMulti){
    int by = SCR_H - 128;
    if(T.y >= by && T.y <= by + 60){
      if(T.x > SCR_W - 100){ filesMulti = false; filesMask = 0; filesRender(); return; }
      if(T.x > SCR_W - 230){
        if(filesSdReadOnlyGuard()){ filesMulti = false; filesMask = 0; filesRender(); return; }
        for(int i = 0; i < filesN; i++) if(filesMask & (1UL << i)){
          char p[FLEXFS_PATH_MAX]; filesPathOf(i, p, sizeof(p));
          flexFsTrash(p);
        }
        filesMulti = false; filesMask = 0; filesReload(); filesRender(); return;
      }
    }
  }
  if(filesHasUp()){
    int y = filesRowY(0);
    if(T.y >= y && T.y < y + FILES_RH - 8){ filesGoUp(); return; }
  }
  for(int i = 0; i < filesN; i++){
    int y = filesRowY(base + i);
    if(T.y >= y && T.y < y + FILES_RH - 8){
      if(filesMulti){ filesMask ^= (1UL << i); filesRender(); return; }
      if(filesList[i].dir){
        char p[FLEXFS_PATH_MAX]; filesPathOf(i, p, sizeof(p));
        strncpy(filesDir, p, sizeof(filesDir) - 1); filesDir[sizeof(filesDir) - 1] = 0;
        filesScroll = 0; filesSelIdx = -1;
        filesReload(); filesRender();
      } else {
        filesSelIdx = i;
        // Un TOQUE CORTO abre; la pulsacion larga (mas arriba) sigue
        // sacando el menu de acciones. Antes tocar un fichero solo
        // abria el menu: no habia forma de abrir nada desde aqui.
        char p[FLEXFS_PATH_MAX]; filesPathOf(i, p, sizeof(p));
        filesOpenEntry(p, filesList[i].name);
      }
      return;
    }
  }
}

// EDUCACION (y cualquier app de lista de tarjetas) · adaptativa.
//   Esencial   : la lista de tarjetas, repartida en COLUMNAS segun el ancho
//                (una columna necesita >= 220 px), de modo que al ensanchar la
//                ventana no queda medio lienzo en blanco: pasa a 2 o 3 columnas.
//   Opcional 1 : subtitulo "Proximamente" dentro de cada tarjeta -- aparece
//                cuando la tarjeta tiene >= 56 px de alto (si no, solo el
//                titulo, que es lo esencial).
static void simpCards(const char* title, const char* items[], int n){
  setBuf(fb);
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  fillRect(bx, by, bw, bh, WIN_BG);
  int pad = uiPad(), gap = uiGap();
  int y0 = by + pad;
  int fsT = uiFontFit(title, bw - 2 * pad, uiFontH(bh / 12));
  drawTextC(bx + bw / 2, y0, title, fsT, TH_TXT);
  y0 += uiLineH(fsT) + gap;
  int cols = (bw - 2 * pad + gap) / (220 + gap); if(cols < 1) cols = 1; if(cols > 3) cols = 3;
  int rows = (n + cols - 1) / cols;
  int cw = (bw - 2 * pad - (cols - 1) * gap) / cols;
  int availH = (by + bh) - y0 - pad;
  int chh = (availH - (rows - 1) * gap) / (rows > 0 ? rows : 1);
  if(chh > 110) chh = 110;
  if(chh < 26) chh = 26;
  uint8_t aSub = uiSection(0, chh >= 56);
  int rad = uiPad();
  for(int i = 0; i < n; i++){
    int c = i % cols, r = i / cols;
    int x = bx + pad + c * (cw + gap), y = y0 + r * (chh + gap);
    if(y + chh > by + bh) break;                     // nunca fuera del marco
    if(uiGlass && !gLand) drawLiquidGlassPanel(x, y, cw, chh, rad, TH_GLASS);
    else fillRoundRect(x, y, cw, chh, rad, TH_SURF);
    int fsI = uiFontFit(items[i], cw - 2 * pad, uiFontH(chh / 2));
    int ty = aSub ? (y + chh / 2 - uiLineH(fsI)) : (y + chh / 2 - uiLineH(fsI) / 2);
    drawText(x + pad, ty, items[i], fsI, TH_TXT);
    if(aSub) uiText(x + pad, ty + uiLineH(fsI) + 4, "Proximamente", 1, TH_TXT2, aSub);
  }
  flxFlush(WIN_TOP, WIN_BOT);
}
static void eduEnter(){ const char* it[4] = { "Electr\xC3\xB3nica b\xC3\xA1sica", "Programaci\xC3\xB3n C++", "Redes y WiFi", "Sensores I2C" }; simpCards("Educaci\xC3\xB3n", it, 4); }
