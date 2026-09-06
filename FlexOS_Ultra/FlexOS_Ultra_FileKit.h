// #############################################################
// ##  FLEX OS ULTRA  ·  KIT DE ARCHIVOS COMPARTIDO
// ##  ----------------------------------------------------------
// ##  Las piezas que Notas, Paint y el Explorador usan sobre ficheros:
// ##  menu contextual, dialogo de nombre, confirmacion y papelera.
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
#include "FlexOS_Ultra_Keyboard.h"   // eslabon anterior de la cadena

// #############################################################
// ##  KIT DE ARCHIVOS  ·  piezas de interfaz compartidas por
// ##  Notas, Paint y el Explorador de archivos
// ##  ------------------------------------------------------
// ##  Las tres pantallas hacen LO MISMO sobre ficheros
// ##  distintos: una rejilla o lista de elementos, el menu
// ##  contextual de 4 acciones (Seleccionar / Eliminar /
// ##  Renombrar / Papelera) y un dialogo de nombre con teclado.
// ##  Escribirlo tres veces era garantizar que las tres se
// ##  comportaran distinto ante el mismo error, asi que vive
// ##  aqui una sola vez.
// ##
// ##  REGLA DEL KIT: estas piezas NO simulan nada. Cada accion
// ##  llama a flexFs* (que toca el fichero real) y devuelve lo
// ##  que de verdad paso; quien la usa vuelve a leer el
// ##  directorio y repinta con lo que hay. Si un borrado falla,
// ##  el elemento sigue en la lista -- porque sigue en el disco.
// #############################################################
#define FK_ACT_SEL    0
#define FK_ACT_DEL    1
#define FK_ACT_REN    2
#define FK_ACT_TRASH  3
// FLEX VAULT: quinta accion OPCIONAL. Solo la ofrecen las pantallas que
// manejan un elemento concreto y movible (Galeria, Notas, Archivos, Paint), y
// solo cuando la boveda tiene sentido ahi. El menu sigue teniendo cuatro filas
// en el resto de sitios: fkMenuN decide cuantas hay, asi que anadirla no
// cambia ni la geometria ni el comportamiento de las pantallas que no la piden.
#define FK_ACT_VAULT  4
#define FK_MENU_W    272
#define FK_MENU_RH    46
#define FK_MENU_PAD   10

static const char* FK_MENU_LBL[5] = { "Seleccionar", "Eliminar", "Renombrar", "Papelera",
                                      "Mover a Carpeta segura" };
static int fkMenuN = 4;                 // filas del menu abierto (4 o 5)

// ---- Texto ajustado a una caja (para la vista previa REAL de una nota) ----
// Corta por caracteres, no por palabras, a proposito: el contenido de una nota
// puede ser una sola cadena larguisima sin espacios (como en las capturas) y un
// ajuste por palabras la dejaria fuera de la tarjeta.
static void fkTextBox(int x, int y, int w, int h, const char* s, int size, uint16_t col){
  if(!s || !*s) return;
  int lh = uiLineH(size) + 4, cy = y, li = 0;
  char line[80];
  const char* p = s;
  while(*p && cy + lh <= y + h){
    unsigned char b = (unsigned char)*p;
    if(b == '\n' || b == '\r'){ line[li] = 0; if(li) drawText(x, cy, line, size, col); cy += lh; li = 0; p++; continue; }
    int cl = 1;
    if((b & 0xE0) == 0xC0) cl = 2; else if((b & 0xF0) == 0xE0) cl = 3; else if((b & 0xF8) == 0xF0) cl = 4;
    if(li + cl >= (int)sizeof(line) - 1){ line[li] = 0; drawText(x, cy, line, size, col); cy += lh; li = 0; }
    memcpy(line + li, p, cl); line[li + cl] = 0;
    if(textW(line, size) > w){
      if(li == 0){ p += cl; continue; }        // un solo caracter mas ancho que la caja: se descarta
      line[li] = 0; drawText(x, cy, line, size, col); cy += lh; li = 0; continue;
    }
    li += cl; p += cl;
  }
  if(li && cy + lh <= y + h){ line[li] = 0; drawText(x, cy, line, size, col); }
}

// ---- Iconos del menu contextual (vectoriales, como el resto del sistema) ----
static void fkMenuGlyph(int k, int cx, int cy){
  uint16_t w = rgb565(255,255,255), red = rgb565(228,60,60), gr = rgb565(90,96,110);
  if(k == FK_ACT_SEL){                                  // mano que pulsa
    fillRoundRect(cx - 5, cy - 10, 10, 14, 4, gr);
    strokeSegAA(cx - 11, cy - 12, cx - 14, cy - 16, 1.8f, gr);
    strokeSegAA(cx,      cy - 14, cx,      cy - 19, 1.8f, gr);
    strokeSegAA(cx + 11, cy - 12, cx + 14, cy - 16, 1.8f, gr);
    fillRoundRect(cx - 8, cy + 2, 16, 8, 3, gr);
  } else if(k == FK_ACT_DEL){                           // papelera roja (borrado definitivo)
    fillRect(cx - 10, cy - 12, 20, 3, red);
    fillRect(cx - 4,  cy - 16, 8,  3, red);
    fillRoundRect(cx - 8, cy - 8, 16, 20, 3, red);
    fillRect(cx - 3, cy - 4, 2, 12, rgb565(255,255,255));
    fillRect(cx + 1, cy - 4, 2, 12, rgb565(255,255,255));
  } else if(k == FK_ACT_REN){                           // campo de texto con cursor
    fillRoundRect(cx - 14, cy - 7, 20, 14, 3, rgb565(40,44,56));
    fillRect(cx + 8, cy - 10, 2, 20, rgb565(40,44,56));
    fillRect(cx + 5, cy - 10, 8, 2,  rgb565(40,44,56));
    fillRect(cx + 5, cy + 8,  8, 2,  rgb565(40,44,56));
  } else if(k == FK_ACT_TRASH){                         // papelera blanca (mover a Papelera)
    fillRect(cx - 10, cy - 12, 20, 3, w);
    fillRect(cx - 4,  cy - 16, 8,  3, w);
    drawRoundRect(cx - 8, cy - 8, 16, 20, 3, w);
    fillRect(cx - 3, cy - 4, 2, 12, w);
    fillRect(cx + 1, cy - 4, 2, 12, w);
  } else {                                              // candado cerrado (Flex Vault)
    uint16_t v = rgb565(120,80,190);
    fillRoundRect(cx - 10, cy - 3, 20, 16, 4, v);
    arcStroke(cx, cy - 3, 6, 180, 360, 3, v);
    fillCircle(cx, cy + 5, 2, rgb565(255,255,255));
  }
}

static bool fkMenuOn = false;
static int  fkMenuX = 0, fkMenuY = 0;

static void fkMenuGeom(int &x, int &y, int &w, int &h){
  w = FK_MENU_W;
  h = fkMenuN * FK_MENU_RH + 2 * FK_MENU_PAD;
  x = fkMenuX; y = fkMenuY;
  if(x + w > SCR_W - 8) x = SCR_W - 8 - w;
  if(x < 8) x = 8;
  if(y + h > SCR_H - 70) y = SCR_H - 70 - h;
  if(y < 30) y = 30;
}

static void fkMenuDraw(){
  int x, y, w, h; fkMenuGeom(x, y, w, h);
  if(uiGlass) drawLiquidGlassPanel(x, y, w, h, 18, rgb565(210,214,222));
  else        fillRoundRect(x, y, w, h, 18, rgb565(206,210,218));
  for(int i = 0; i < fkMenuN; i++){
    int ry = y + FK_MENU_PAD + i * FK_MENU_RH;
    // La fila de la boveda va con su color, para que no se confunda con
    // "Papelera": una lleva el elemento a un sitio del que se recupera, la otra
    // lo saca del sistema de archivos normal.
    uint16_t tc = (i == FK_ACT_VAULT) ? rgb565(90,50,160) : rgb565(16,18,24);
    drawTextClip(x + 16, ry + 10, FK_MENU_LBL[i], (i == FK_ACT_VAULT) ? 2 : 3, tc, x + w - 42);
    fkMenuGlyph(i, x + w - 32, ry + FK_MENU_RH / 2);
  }
  flxFlush(y - 2, y + h + 2);
}

// La version de siempre: cuatro filas, sin Flex Vault. La usan las pantallas
// que no mueven contenido (y asi no cambia nada de lo que ya funcionaba).
static void fkMenuOpen(int px, int py){ fkMenuOn = true; fkMenuN = 4; fkMenuX = px; fkMenuY = py; fkMenuDraw(); }
// Con la quinta fila. `withVault` lo decide quien llama: solo tiene sentido si
// hay un elemento concreto seleccionado y es un fichero (no una carpeta).
static void fkMenuOpenV(int px, int py, bool withVault){
  fkMenuOn = true; fkMenuN = withVault ? 5 : 4; fkMenuX = px; fkMenuY = py; fkMenuDraw();
}

// -1 = toque fuera del panel (cierra sin accion); 0..fkMenuN-1 = accion elegida.
static int fkMenuHit(int px, int py){
  int x, y, w, h; fkMenuGeom(x, y, w, h);
  if(px < x || px > x + w || py < y || py > y + h) return -1;
  int i = (py - y - FK_MENU_PAD) / FK_MENU_RH;
  if(i < 0) i = 0; if(i > fkMenuN - 1) i = fkMenuN - 1;
  return i;
}

// #############################################################
// ##  DIALOGO DE NOMBRE (renombrar / crear con nombre propio)
// ##  Reutiliza EL MISMO teclado del sistema (mapaActivo,
// ##  kbPaintKey, kbCellAt...) que usan Notas y la clave de
// ##  Wi-Fi: no hay un segundo teclado que mantener.
// #############################################################
static bool fkNameOn = false;
static char fkNameBuf[FLEXFS_NAME_MAX] = "";
static char fkNameTitle[40] = "";

static void fkNameDraw(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, kbPanelTop(), rgb565(14,16,24));
  drawTextC(SCR_W / 2, 40, fkNameTitle, 3, rgb565(255,255,255));
  strokeSegAA(30, 46, 18, 38, 2.4f, rgb565(255,255,255));      // chevron: cancelar
  strokeSegAA(18, 38, 30, 30, 2.4f, rgb565(255,255,255));
  int fy = 130;
  fillRoundRect(24, fy, SCR_W - 48, 56, 14, rgb565(30,34,48));
  drawTextClip(38, fy + 16, fkNameBuf, 2, rgb565(240,242,248), SCR_W - 40);
  int cw = textW(fkNameBuf, 2);
  fillRect(38 + cw + 2, fy + 14, 2, 28, rgb565(120,170,250));       // cursor
  drawTextC(SCR_W / 2, fy + 76, "Escribe el nombre y pulsa Guardar", 1, rgb565(140,148,168));

  int ky = KB_Y;
  if(uiGlass) drawLiquidGlassPanel(0, ky - 4, SCR_W, SCR_H - (ky - 4), 0, rgb565(36,40,58));
  else        fillRect(0, ky - 4, SCR_W, SCR_H - (ky - 4), rgb565(18,20,28));
  int fs = kbFontSize();
  for(int r = 0; r < KB_ROWS; r++) for(int c = 0; c < KB_COLS; c++){
    int x = KB_X + c * (KB_KW + KB_GAP), y = ky + r * (KB_KH + KB_GAP);
    char u[6];
    const char* k = kbResolveKey(mapaActivo[r][c], u, false);
    kbPaintKey(x, y, KB_KW, KB_KH, k, fs, kbColKey(), kbColKeyTxt(), false);
  }
  int fry = ky + 3 * (KB_KH + KB_GAP);
  const char* lb[KB_FKEYS] = { "shift", kbLayerLabel(), kbLangEs ? "ES" : "EN", "espacio", "<-", "Guardar" };
  for(int i = 0; i < KB_FKEYS; i++) kbFKey(kbFKeyX(i), fry, kbFKeyW(i), lb[i], (i == 0) && kbShift);
  flxFlushAll();
}

static void fkNameOpen(const char* title, const char* initial){
  fkNameOn = true;
  strncpy(fkNameTitle, title, sizeof(fkNameTitle) - 1); fkNameTitle[sizeof(fkNameTitle) - 1] = 0;
  strncpy(fkNameBuf, initial ? initial : "", sizeof(fkNameBuf) - 1); fkNameBuf[sizeof(fkNameBuf) - 1] = 0;
  mapaActivo = LAYOUT_ES; kbLangEs = true; kbShift = false;
  kbExtrasOn = false; kbApplySize(); kbMtSurfaceReset();
  fkNameDraw();
}

static void fkNameAppend(const char* s){
  int L = strlen(fkNameBuf), sl = strlen(s);
  // '/' partiria la ruta y dejaria el fichero en otra carpeta sin avisar.
  if(strchr(s, '/')) return;
  if(L + sl < (int)sizeof(fkNameBuf) - 1){ memcpy(fkNameBuf + L, s, sl); fkNameBuf[L + sl] = 0; }
}
static void fkNameBack(){
  int L = strlen(fkNameBuf);
  if(L > 0){ int q = L - 1; while(q > 0 && (fkNameBuf[q] & 0xC0) == 0x80) q--; fkNameBuf[q] = 0; }
}

// 0 = sigue abierto, 1 = aceptado (fkNameBuf es valido), -1 = cancelado.
static int fkNameTick(){
  if(!T.released) return 0;
  int fi = kbFRowHit(T.x, T.y);
  if(fi >= 0){
    if(fi == 0){ kbShift = !kbShift; fkNameDraw(); return 0; }
    if(fi == 1){ mapaActivo = (mapaActivo == LAYOUT_NUM) ? LAYOUT_EMOJI
                             : (mapaActivo == LAYOUT_EMOJI) ? (kbLangEs ? LAYOUT_ES : LAYOUT_EN)
                             : LAYOUT_NUM; fkNameDraw(); return 0; }
    if(fi == 2){ kbLangEs = !kbLangEs;
                 if(mapaActivo == LAYOUT_ES || mapaActivo == LAYOUT_EN) mapaActivo = kbLangEs ? LAYOUT_ES : LAYOUT_EN;
                 fkNameDraw(); return 0; }
    if(fi == 3){ fkNameAppend(" "); fkNameDraw(); return 0; }
    if(fi == 4){ fkNameBack(); fkNameDraw(); return 0; }
    if(fi == 5){ fkNameOn = false; return strlen(fkNameBuf) > 0 ? 1 : -1; }
    return 0;
  }
  if(T.tap && T.y < 90 && T.x < 60){ fkNameOn = false; return -1; }   // esquina sup. izq. = cancelar
  int cell = kbCellAt(T.x, T.y);
  if(cell >= 0){
    char u[6];
    const char* k = kbResolveKey(mapaActivo[cell / KB_COLS][cell % KB_COLS], u, true);
    fkNameAppend(k);
    fkNameDraw();
  }
  return 0;
}

// #############################################################
// ##  CONFIRMACION (borrado definitivo, vaciar papelera...)
// ##  Un borrado real no se pregunta con un toast: si el
// ##  usuario dice que si, el fichero desaparece del disco y no
// ##  hay vuelta atras.
// #############################################################
static bool fkAskOn = false;
static char fkAskMsg[72] = "";
static char fkAskSub[72] = "";

static void fkAskGeom(int &x, int &y, int &w, int &h){ w = SCR_W - 72; h = 220; x = 36; y = (SCR_H - h) / 2; }

static void fkAskDraw(){
  int x, y, w, h; fkAskGeom(x, y, w, h);
  setBuf(fb);
  if(uiGlass) drawLiquidGlassPanel(x, y, w, h, 24, rgb565(60,64,88));
  else        fillRoundRect(x, y, w, h, 24, rgb565(34,38,50));
  drawTextC(SCR_W / 2, y + 26, fkAskMsg, 2, rgb565(255,255,255));
  if(fkAskSub[0]) drawTextC(SCR_W / 2, y + 62, fkAskSub, 1, rgb565(170,178,196));
  int by = y + h - 76, bw = (w - 48) / 2;
  fillRoundRect(x + 16, by, bw, 56, 16, rgb565(70,74,90));
  drawTextC(x + 16 + bw / 2, by + 18, "Cancelar", 2, rgb565(240,242,248));
  fillRoundRect(x + 32 + bw, by, bw, 56, 16, rgb565(220,70,70));
  drawTextC(x + 32 + bw + bw / 2, by + 18, "Borrar", 2, rgb565(255,255,255));
  flxFlush(y - 2, y + h + 2);
}

static void fkAskOpen(const char* msg, const char* sub){
  fkAskOn = true;
  strncpy(fkAskMsg, msg, sizeof(fkAskMsg) - 1); fkAskMsg[sizeof(fkAskMsg) - 1] = 0;
  strncpy(fkAskSub, sub ? sub : "", sizeof(fkAskSub) - 1); fkAskSub[sizeof(fkAskSub) - 1] = 0;
  fkAskDraw();
}

// 0 = sigue abierto, 1 = confirmado, -1 = cancelado.
static int fkAskTick(){
  if(!T.tap) return 0;
  int x, y, w, h; fkAskGeom(x, y, w, h);
  int by = y + h - 76, bw = (w - 48) / 2;
  if(T.y >= by && T.y <= by + 56){
    if(T.x >= x + 16 && T.x <= x + 16 + bw){ fkAskOn = false; return -1; }
    if(T.x >= x + 32 + bw && T.x <= x + 32 + 2 * bw){ fkAskOn = false; return 1; }
  }
  if(T.x < x || T.x > x + w || T.y < y || T.y > y + h){ fkAskOn = false; return -1; }
  return 0;
}

// ---- Aviso de "sin almacenamiento" ---------------------------
// Cuando LittleFS no monta, estas pantallas NO tienen nada real que
// ensenar. Antes que inventar una lista vacia bonita, se dice el
// motivo exacto y como arreglarlo.
static void fkNoFsScreen(const char* titulo){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, rgb565(14,16,24));
  strokeSegAA(30, 26, 18, 18, 2.4f, rgb565(255,255,255));
  strokeSegAA(18, 18, 30, 10, 2.4f, rgb565(255,255,255));
  drawTextC(SCR_W / 2, 40, titulo, 3, rgb565(255,255,255));
  drawTextC(SCR_W / 2, 300, "Sin almacenamiento", 3, rgb565(240,140,140));
  drawTextC(SCR_W / 2, 344, flexFsError(), 1, rgb565(170,178,196));
  drawTextC(SCR_W / 2, 380, "Arduino IDE > Herramientas > Partition Scheme", 1, rgb565(140,148,168));
  flxFlushAll();
}

// #############################################################
// ##  PAPELERA  ·  navegador compartido de /Papelera
// ##  ------------------------------------------------------
// ##  Lo abren Notas, Paint y el Explorador. Lista lo que hay
// ##  DE VERDAD en /Papelera y ofrece las dos unicas cosas que
// ##  se pueden hacer con un fichero tirado: devolverlo a su
// ##  sitio o borrarlo para siempre.
// ##
// ##  La ruta original no se guarda en un indice aparte: viaja
// ##  DENTRO del nombre del fichero ('/' se codifica como '@'),
// ##  asi que restaurar es exacto aunque el sistema se apague a
// ##  media operacion. Ver flexFsTrash() en FlexOS_FS.cpp.
// #############################################################
#define FK_TRASH_MAX  16
#define FK_TRASH_TOP  120
#define FK_TRASH_RH    66

static bool        fkTrashOn = false;
static FlexFsEntry fkTrashList[FK_TRASH_MAX];
static int         fkTrashN = 0;
static int         fkTrashSel = -1;
static int         fkTrashScroll = 0;
static int         fkTrashDragY0 = 0, fkTrashDragS0 = 0;
static bool        fkTrashDragging = false;

static void fkTrashReload(){
  fkTrashN = flexFsList(FLEXFS_DIR_TRASH, fkTrashList, FK_TRASH_MAX);
  if(fkTrashSel >= fkTrashN) fkTrashSel = -1;
}

static int fkTrashRowY(int i){ return FK_TRASH_TOP + i * FK_TRASH_RH - fkTrashScroll; }

static int fkTrashMaxScroll(){
  int need = FK_TRASH_TOP + fkTrashN * FK_TRASH_RH + 30;
  int m = need - (SCR_H - 140);
  return m > 0 ? m : 0;
}

static void fkTrashRender(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, rgb565(14,16,24));
  strokeSegAA(30, 26, 18, 18, 2.4f, rgb565(255,255,255));
  strokeSegAA(18, 18, 30, 10, 2.4f, rgb565(255,255,255));
  drawTextC(SCR_W / 2, 34, "Papelera", 4, rgb565(255,255,255));

  char hdr[64];
  uint32_t used = flexFsDirSize(FLEXFS_DIR_TRASH);
  char sz[24]; flexFsFmtSize(used, sz, sizeof(sz));
  snprintf(hdr, sizeof(hdr), "%d elementos  ·  %s", fkTrashN, sz);
  drawTextC(SCR_W / 2, 86, hdr, 1, rgb565(150,158,178));

  if(fkTrashN == 0){
    drawTextC(SCR_W / 2, 320, "La papelera est\xC3\xA1 vac\xC3\xAD" "a", 2, rgb565(150,158,178));
  }
  for(int i = 0; i < fkTrashN; i++){
    int y = fkTrashRowY(i);
    if(y + FK_TRASH_RH < FK_TRASH_TOP - 40 || y > SCR_H - 130) continue;
    bool sel = (i == fkTrashSel);
    fillRoundRect(16, y, SCR_W - 32, FK_TRASH_RH - 8, 14, sel ? rgb565(46,56,84) : rgb565(30,34,48));
    // Se muestra la ruta ORIGINAL decodificada: es lo unico que le dice al
    // usuario de donde salio ese fichero.
    char origen[FLEXFS_PATH_MAX];
    if(!flexFsTrashOrigin(fkTrashList[i].name, origen, sizeof(origen)))
      snprintf(origen, sizeof(origen), "%s", fkTrashList[i].name);
    const char* nm = strrchr(origen, '/');
    nm = nm ? nm + 1 : origen;
    drawTextClip(30, y + 8, nm, 2, rgb565(240,242,248), SCR_W - 40);
    char sub[FLEXFS_PATH_MAX + 24], s2[24];
    flexFsFmtSize(fkTrashList[i].size, s2, sizeof(s2));
    snprintf(sub, sizeof(sub), "%s  ·  %s", origen, s2);
    drawTextClip(30, y + 34, sub, 1, rgb565(140,148,168), SCR_W - 40);
  }

  int by = SCR_H - 122;
  if(fkTrashSel >= 0){
    int bw = (SCR_W - 48) / 2;
    fillRoundRect(16, by, bw, 56, 16, rgb565(60,150,110));
    drawTextC(16 + bw / 2, by + 18, "Restaurar", 2, rgb565(255,255,255));
    fillRoundRect(32 + bw, by, bw, 56, 16, rgb565(200,60,60));
    drawTextC(32 + bw + bw / 2, by + 18, "Borrar", 2, rgb565(255,255,255));
  } else if(fkTrashN > 0){
    fillRoundRect(SCR_W / 2 - 120, by, 240, 56, 16, rgb565(70,74,90));
    drawTextC(SCR_W / 2, by + 18, "Vaciar papelera", 2, rgb565(250,190,190));
  }
  if(fkAskOn) fkAskDraw();
  flxFlushAll();
}

static void fkTrashOpen(){
  fkTrashOn = true; fkTrashSel = -1; fkTrashScroll = 0; fkAskOn = false;
  fkTrashReload();
  fkTrashRender();
}

// Devuelve true mientras la papelera siga abierta (el llamante no debe
// hacer nada mas); false cuando el usuario ha salido.
static bool fkTrashTick(){
  if(!fkTrashOn) return false;

  if(fkAskOn){
    int r = fkAskTick();
    if(r == 1){
      if(fkTrashSel >= 0 && fkTrashSel < fkTrashN){
        char p[FLEXFS_PATH_MAX];
        snprintf(p, sizeof(p), "%s/%s", FLEXFS_DIR_TRASH, fkTrashList[fkTrashSel].name);
        flexFsDelete(p);                       // definitivo: ya no hay vuelta atras
      } else {
        flexFsEmptyTrash();
      }
      fkTrashSel = -1; fkTrashReload();
    }
    if(r != 0) fkTrashRender();
    return true;
  }

  int maxS = fkTrashMaxScroll();
  if(T.pressed){ fkTrashDragY0 = T.y; fkTrashDragS0 = fkTrashScroll; fkTrashDragging = false; }
  if(T.down && maxS > 0){
    int dy = fkTrashDragY0 - T.y;
    if(!fkTrashDragging && abs(dy) > 8) fkTrashDragging = true;
    if(fkTrashDragging){
      int ns = fkTrashDragS0 + dy;
      if(ns < 0) ns = 0; if(ns > maxS) ns = maxS;
      if(ns != fkTrashScroll){ fkTrashScroll = ns; fkTrashRender(); }
      return true;
    }
  }
  if(T.released && fkTrashDragging){ fkTrashDragging = false; return true; }
  if(!T.tap) return true;

  if(T.x < 60 && T.y < 60){ fkTrashOn = false; return false; }      // volver

  int by = SCR_H - 122;
  if(T.y >= by && T.y <= by + 56){
    if(fkTrashSel >= 0){
      int bw = (SCR_W - 48) / 2;
      if(T.x <= 16 + bw){
        char nm[FLEXFS_NAME_MAX];
        strncpy(nm, fkTrashList[fkTrashSel].name, sizeof(nm) - 1); nm[sizeof(nm) - 1] = 0;
        flexFsRestore(nm);                     // vuelve a su carpeta original, de verdad
        fkTrashSel = -1; fkTrashReload(); fkTrashRender(); return true;
      }
      if(T.x >= 32 + bw){
        char stem[FLEXFS_NAME_MAX]; flexFsStem(fkTrashList[fkTrashSel].name, stem, sizeof(stem));
        fkAskOpen("\xC2\xBF" "Borrar definitivamente?", stem);
        return true;
      }
    } else if(fkTrashN > 0 && T.x > SCR_W / 2 - 120 && T.x < SCR_W / 2 + 120){
      fkAskOpen("\xC2\xBF" "Vaciar la papelera?", "Se borrar\xC3\xA1 todo su contenido");
      return true;
    }
  }
  for(int i = 0; i < fkTrashN; i++){
    int y = fkTrashRowY(i);
    if(T.y >= y && T.y < y + FK_TRASH_RH - 8){ fkTrashSel = (fkTrashSel == i) ? -1 : i; fkTrashRender(); return true; }
  }
  return true;
}
