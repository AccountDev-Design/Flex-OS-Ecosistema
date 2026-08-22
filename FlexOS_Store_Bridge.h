#ifndef FLEXOS_STORE_BRIDGE_H
#define FLEXOS_STORE_BRIDGE_H

// Este puente se incluye al final de FlexOS_Ultra.ino, cuando las primitivas
// graficas, Touch, appClose() y el tema semantico ya existen. Los modulos .cpp
// no conocen el framebuffer: solo publican datos y estados verificables.

enum StoreView : uint8_t { SV_DISCOVER = 0, SV_INSTALLED, SV_DETAIL, SV_RUNTIME, SV_SEARCH };
static StoreView storeView = SV_DISCOVER;
static StoreView storeSearchSource = SV_DISCOVER;
static int storePage = 0;
static int storeSelected = -1;
static bool storeSelectedInstalled = false;
static bool storeConfirmDelete = false;
static FlexPkgInfo storeInstalled[FLEXPKG_MAX_INSTALLED];
static int storeInstalledN = 0;
static bool storeInstalledDirty = true;         // flexPkgList lee LittleFS: solo por evento
static FlexRuntimeApp storeRuntime;
static FlexStoreState storeLastState = FLEXSTORE_IDLE;
static uint8_t storeLastProgress = 255;
static uint32_t storeToastUntil = 0;
static char storeToastText[128] = "";
static char storeSearch[33] = "";

// CACHE DEL FILTRO. Antes cada repintado recorria el catalogo entero UNA VEZ
// para contar y OTRA por cada fila para traducir el indice filtrado al real:
// hasta 4 barridos de 24 items, con una copia de FlexStoreItem (y su mutex) en
// cada paso. Ahora el barrido se hace solo cuando cambia algo que puede alterar
// el resultado -- la busqueda, el catalogo o la lista de instaladas -- y se
// guarda en dos tablas de indices de tamano fijo (24 + 24 bytes, sin malloc).
static uint8_t storeFiltCat[FLEXSTORE_MAX_CATALOG];
static uint8_t storeFiltCatN = 0;
static uint8_t storeFiltInst[FLEXPKG_MAX_INSTALLED];
static uint8_t storeFiltInstN = 0;
static bool storeFiltDirty = true;
static int storeFiltCatalogN = -1;              // tamano del catalogo con el que se construyo

static void storeRender();

static uint16_t storeRgb(uint32_t c){ return rgb565((c >> 16) & 255, (c >> 8) & 255, c & 255); }

static void storeToast(const char* text){
  snprintf(storeToastText, sizeof(storeToastText), "%s", text ? text : "");
  storeToastUntil = millis() + 2600;
}

static inline void storeFilterInvalidate(){ storeFiltDirty = true; }

static void storeReloadInstalled(){
  storeInstalledN = flexPkgList(storeInstalled, FLEXPKG_MAX_INSTALLED);
  storeInstalledDirty = false;
  storeFilterInvalidate();
}
static inline void storeEnsureInstalled(){ if(storeInstalledDirty) storeReloadInstalled(); }

// Busqueda en la lista YA cargada, sin volver a tocar el sistema de archivos.
static int storeInstalledFind(const char* packageId){
  if(!packageId || !packageId[0]) return -1;
  for(int i = 0; i < storeInstalledN; i++) if(!strcmp(storeInstalled[i].id, packageId)) return i;
  return -1;
}

static char storeFold(char c){ return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c; }

static bool storeContains(const char* text, const char* query){
  if(!query || !query[0]) return true;
  if(!text) return false;
  for(const char* start = text; *start; start++){
    const char* a = start; const char* b = query;
    while(*a && *b && storeFold(*a) == storeFold(*b)){ a++; b++; }
    if(!*b) return true;
  }
  return false;
}

static bool storeCatalogMatch(const FlexStoreItem& item){
  return storeContains(item.name, storeSearch) || storeContains(item.summary, storeSearch) ||
         storeContains(item.category, storeSearch) || storeContains(item.packageId, storeSearch);
}

static bool storeInstalledMatch(const FlexPkgInfo& item){
  return storeContains(item.name, storeSearch) || storeContains(item.summary, storeSearch) ||
         storeContains(item.category, storeSearch) || storeContains(item.id, storeSearch);
}

static void storeFilterRebuild(){
  storeFiltCatN = 0;
  int total = flexStoreCatalogCount();
  for(int i = 0; i < total && storeFiltCatN < FLEXSTORE_MAX_CATALOG; i++){
    FlexStoreItem item;
    if(flexStoreCatalogItem(i, &item) && storeCatalogMatch(item)) storeFiltCat[storeFiltCatN++] = (uint8_t)i;
  }
  storeFiltInstN = 0;
  for(int i = 0; i < storeInstalledN && storeFiltInstN < FLEXPKG_MAX_INSTALLED; i++)
    if(storeInstalledMatch(storeInstalled[i])) storeFiltInst[storeFiltInstN++] = (uint8_t)i;
  storeFiltCatalogN = total;
  storeFiltDirty = false;
}

// Barata cuando no hay nada que rehacer: una lectura del contador del catalogo
// (un mutex) y una comparacion. Nunca se llama por cuadro, solo al dibujar o al
// resolver un toque sobre una tarjeta.
static void storeFilterEnsure(){
  if(storeFiltDirty || flexStoreCatalogCount() != storeFiltCatalogN) storeFilterRebuild();
}

static int storeCatalogFilteredCount(){ storeFilterEnsure(); return storeFiltCatN; }

static int storeCatalogIndexAt(int filteredIndex){
  storeFilterEnsure();
  if(filteredIndex < 0 || filteredIndex >= storeFiltCatN) return -1;
  return storeFiltCat[filteredIndex];
}

static int storeInstalledFilteredCount(){ storeFilterEnsure(); return storeFiltInstN; }

static int storeInstalledIndexAt(int filteredIndex){
  storeFilterEnsure();
  if(filteredIndex < 0 || filteredIndex >= storeFiltInstN) return -1;
  return storeFiltInst[filteredIndex];
}

static void storeBackGlyph(int x, int y, uint16_t col){
  strokeSeg(x + 14, y, x, y + 12, 2, col);
  strokeSeg(x, y + 12, x + 14, y + 24, 2, col);
}

static void storeHeader(const char* title, bool nested){
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  if(nested) storeBackGlyph(24, 30, TH_TXT);
  drawText(nested ? 66 : 24, 28, title, 3, TH_TXT);
  if(!nested){
    bool linked = flexAccountLinked();
    int ax = SCR_W - 132, ay = 21, aw = 112, ah = 40;
    int sx = ax - 50;
    fillRoundRect(sx, ay, 40, 40, 20, thCard());
    fillCircle(sx + 18, ay + 17, 8, TH_TXT2); fillCircle(sx + 18, ay + 17, 5, thCard());
    strokeSeg(sx + 24, ay + 23, sx + 30, ay + 29, 2, TH_TXT2);
    if(storeSearch[0]) fillCircle(sx + 33, ay + 7, 4, TH_PRIM);
    fillRoundRect(ax, ay, aw, ah, 20, linked ? rgb565(32,171,126) : thCard());
    drawTextC(ax + aw / 2, ay + 13, linked ? "@flex" : "Cuenta", 1,
              linked ? rgb565(255,255,255) : TH_TXT2);
  }
  hLine(20, 76, SCR_W - 40, TH_DIV);
}

static void storeTabs(){
  int y = 88, w = (SCR_W - 48) / 2;
  const char* names[2] = {"Descubrir", "Instaladas"};
  for(int i = 0; i < 2; i++){
    bool on = (storeView == (i ? SV_INSTALLED : SV_DISCOVER));
    fillRoundRect(20 + i * (w + 8), y, w, 42, 20, on ? TH_PRIM : thCard());
    drawTextC(20 + i * (w + 8) + w / 2, y + 13, names[i], 2, on ? rgb565(255,255,255) : TH_TXT2);
  }
}

static void storeAppMark(int x, int y, int s, const char* name, uint16_t accent){
  fillRoundRect(x, y, s, s, s / 5, accent);
  char letter[2] = {(name && name[0]) ? name[0] : 'F', 0};
  drawTextC(x + s / 2, y + s / 2 - 10, letter, 4, rgb565(255,255,255));
}

static void storeStars(int x, int y, uint16_t ratingX100){
  char r[24]; snprintf(r, sizeof(r), "%u.%02u / 5", ratingX100 / 100, ratingX100 % 100);
  drawText(x, y, r, 1, TH_MUTE);
}

static void storeEmpty(const char* title, const char* body){
  fillCircle(SCR_W / 2, 300, 44, rgb565(105,91,230));
  drawTextC(SCR_W / 2, 284, "F", 4, rgb565(255,255,255));
  drawTextC(SCR_W / 2, 372, title, 3, TH_TXT);
  drawTextC(SCR_W / 2, 414, body, 1, TH_TXT2);
}

static int storeRowsPerPage(){ return 3; }

// Aviso breve del resultado de una operacion (instalar, desinstalar, abrir).
// Antes solo se dibujaba dentro del runtime de una app, asi que desinstalar o
// terminar una instalacion no daba ninguna respuesta en pantalla.
static bool storeToastVisible(){ return storeToastUntil && (int32_t)(millis() - storeToastUntil) < 0; }

static void storeToastOverlay(){
  if(!storeToastVisible()) return;
  int y = SCR_H - 132;
  fillRoundRect(24, y, SCR_W - 48, 48, 18, thCard());
  drawTextClip(44, y + 16, storeToastText, 1, TH_TXT, SCR_W - 44);
  flxFlush(y - 2, y + 50);
}

static void storeSearchRender(){
  storeHeader("Buscar apps", true);
  fillRoundRect(24, 108, SCR_W - 48, 56, 16, thCard());
  drawText(42, 126, storeSearch[0] ? storeSearch : "Escribe un nombre o categoria", 2, storeSearch[0] ? TH_TXT : TH_MUTE);
  const char* rows[3] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
  const int starts[3] = {12, 34, 22}; const int widths[3] = {42, 42, 42}; const int ys[3] = {192, 254, 316};
  for(int row = 0; row < 3; row++){
    int n = (int)strlen(rows[row]);
    for(int i = 0; i < n; i++){
      int x = starts[row] + i * (widths[row] + 4);
      fillRoundRect(x, ys[row], widths[row], 48, 10, thCard());
      char key[2] = {rows[row][i], 0}; drawTextC(x + widths[row] / 2, ys[row] + 15, key, 2, TH_TXT);
    }
  }
  fillRoundRect(348, ys[2], 110, 48, 10, thCard()); drawTextC(403, ys[2] + 15, "Borrar", 1, TH_TXT2);
  fillRoundRect(24, 382, 210, 48, 12, thCard()); drawTextC(129, 397, "Espacio", 1, TH_TXT2);
  fillRoundRect(246, 382, 210, 48, 12, thCard()); drawTextC(351, 397, "Limpiar", 1, TH_TXT2);
  fillRoundRect(24, 448, SCR_W - 48, 52, 26, TH_PRIM); drawTextC(SCR_W / 2, 465, "Mostrar resultados", 2, rgb565(255,255,255));
  int results = storeSearchSource == SV_INSTALLED ? storeInstalledFilteredCount() : storeCatalogFilteredCount();
  char count[48]; snprintf(count, sizeof(count), "%d resultado%s", results, results == 1 ? "" : "s");
  drawTextC(SCR_W / 2, 540, count, 2, results ? TH_TXT2 : rgb565(224,111,120));
  drawTextC(SCR_W / 2, 580, storeSearchSource == SV_INSTALLED ? "Buscando en tus apps" : "Buscando en Flex Store", 1, TH_MUTE);
  flxFlushAll();
}

static bool storeSearchTapKey(int x, int y){
  const char* rows[3] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
  const int starts[3] = {12, 34, 22}; const int widths[3] = {42, 42, 42}; const int ys[3] = {192, 254, 316};
  for(int row = 0; row < 3; row++){
    if(y < ys[row] || y > ys[row] + 48) continue;
    int slot = (x - starts[row]) / (widths[row] + 4);
    int keyX = starts[row] + slot * (widths[row] + 4);
    int n = (int)strlen(rows[row]);
    if(slot >= 0 && slot < n && x >= keyX && x <= keyX + widths[row]){
      size_t len = strlen(storeSearch);
      if(len < sizeof(storeSearch) - 1){ storeSearch[len] = storeFold(rows[row][slot]); storeSearch[len + 1] = 0; }
      return true;
    }
  }
  return false;
}

static void storeSearchTap(int x, int y){
  // Toda modificacion de la consulta invalida el filtro y devuelve la lista a
  // la primera pagina: sin esto, borrar letras podia dejar la vista en una
  // pagina que ya no existe.
  if(storeSearchTapKey(x, y)){ storeFilterInvalidate(); storePage = 0; storeSearchRender(); return; }
  size_t len = strlen(storeSearch);
  if(y >= 316 && y <= 364 && x >= 348){
    if(len) storeSearch[len - 1] = 0;
    storeFilterInvalidate(); storePage = 0; storeSearchRender(); return;
  }
  if(y >= 382 && y <= 430 && x < 240){
    if(len && len < sizeof(storeSearch) - 1){ storeSearch[len] = ' '; storeSearch[len + 1] = 0; }
    storeFilterInvalidate(); storePage = 0; storeSearchRender(); return;
  }
  if(y >= 382 && y <= 430 && x >= 240){
    storeSearch[0] = 0; storeFilterInvalidate(); storePage = 0; storeSearchRender(); return;
  }
  if(y >= 448 && y <= 510){ storeView = storeSearchSource; storePage = 0; storeRender(); }
}

// Solo la banda del progreso. Durante una descarga el estado NO cambia -- solo
// el porcentaje -- y repintar los 480x800 en cada paso era el unico redibujado
// completo repetitivo que quedaba en la tienda.
static void storeProgressBand(){
  setBuf(fb);
  fillRect(0, 244, SCR_W, 132, TH_PAGE);
  drawTextC(SCR_W / 2, 260, flexStoreStage(), 2, TH_TXT);
  int x = 42, y = 318, w = SCR_W - 84;
  uint8_t pct = flexStoreProgress();
  fillRoundRect(x, y, w, 18, 9, thCard());
  fillRoundRect(x, y, w * pct / 100, 18, 9, TH_PRIM);
  char p[16]; snprintf(p, sizeof(p), "%u%%", pct); drawTextC(SCR_W / 2, y + 36, p, 2, TH_TXT2);
  flxFlush(244, 375);
  storeLastState = flexStoreState(); storeLastProgress = pct;
}

static void storeDiscoverRender(){
  storeHeader("Flex Store", false); storeTabs();
  FlexStoreState st = flexStoreState();
  if(st == FLEXSTORE_LOADING || st == FLEXSTORE_DOWNLOADING || st == FLEXSTORE_INSTALLING){
    drawTextC(SCR_W / 2, 260, flexStoreStage(), 2, TH_TXT);
    int x = 42, y = 318, w = SCR_W - 84;
    fillRoundRect(x, y, w, 18, 9, thCard());
    fillRoundRect(x, y, w * flexStoreProgress() / 100, 18, 9, TH_PRIM);
    char p[16]; snprintf(p, sizeof(p), "%u%%", flexStoreProgress()); drawTextC(SCR_W / 2, y + 36, p, 2, TH_TXT2);
    fillRoundRect(130, 430, SCR_W - 260, 48, 24, rgb565(120,120,130));
    drawTextC(SCR_W / 2, 446, "Cancelar", 2, rgb565(255,255,255));
    flxFlushAll(); return;
  }
  if(st == FLEXSTORE_ERROR){
    storeEmpty("No se pudo abrir Flex Store", flexStoreError());
    fillRoundRect(118, 474, SCR_W - 236, 50, 25, TH_PRIM);
    drawTextC(SCR_W / 2, 490, "Reintentar", 2, rgb565(255,255,255));
    flxFlushAll(); return;
  }
  if(st == FLEXSTORE_SUCCESS && storeLastState != FLEXSTORE_SUCCESS){
    storeInstalledDirty = true;                  // la app nueva ya esta activa en LittleFS
    storeToast("Aplicacion instalada y verificada");
  }
  storeEnsureInstalled();
  int catalogCount = flexStoreCatalogCount();
  if(catalogCount == 0){
    storeEmpty("Aun no hay apps publicadas", "Desliza hacia abajo para actualizar");
    fillRoundRect(118, 474, SCR_W - 236, 50, 25, TH_PRIM);
    drawTextC(SCR_W / 2, 490, "Actualizar", 2, rgb565(255,255,255));
    flxFlushAll(); return;
  }
  int count = storeCatalogFilteredCount();
  if(count == 0){ storeEmpty("No encontramos apps", "Toca la lupa para cambiar tu busqueda"); flxFlushAll(); return; }
  int per = storeRowsPerPage(), start = storePage * per;
  if(start >= count){ storePage = 0; start = 0; }
  for(int row = 0; row < per && start + row < count; row++){
    int catalogIndex = storeCatalogIndexAt(start + row); FlexStoreItem item;
    if(catalogIndex < 0 || !flexStoreCatalogItem(catalogIndex, &item)) continue;
    int y = 148 + row * 174;
    fillRoundRect(18, y, SCR_W - 36, 154, 22, thCard());
    storeAppMark(34, y + 24, 76, item.name, rgb565(105 + row * 20, 91, 230 - row * 24));
    drawText(126, y + 22, item.name, 2, TH_TXT);
    drawTextClip(126, y + 53, item.summary[0] ? item.summary : item.category, 1, TH_TXT2, SCR_W - 32);
    storeStars(126, y + 80, item.ratingX100);
    // La version local sale de la lista ya cargada. Antes se abria y se parseaba
    // el manifiesto de LittleFS por FILA y por REPINTADO (flexPkgGet).
    int local = storeInstalledFind(item.packageId);
    const char* action = local >= 0 ? (item.versionCode > storeInstalled[local].versionCode ? "Actualizar" : "Abrir") : "Instalar";
    fillRoundRect(SCR_W - 140, y + 104, 104, 36, 18, TH_PRIM);
    drawTextC(SCR_W - 88, y + 115, action, 1, rgb565(255,255,255));
  }
  if(count > per){
    char pg[24]; int pages = (count + per - 1) / per;
    snprintf(pg, sizeof(pg), "%d / %d", storePage + 1, pages);
    drawTextC(SCR_W / 2, 690, pg, 1, TH_MUTE);
  }
  flxFlushAll();
}

static void storeInstalledRender(){
  storeHeader("Flex Store", false); storeTabs(); storeEnsureInstalled();
  if(storeInstalledN == 0){ storeEmpty("No tienes apps instaladas", "Instala una desde la pestana Descubrir"); flxFlushAll(); return; }
  int count = storeInstalledFilteredCount();
  if(count == 0){ storeEmpty("No encontramos apps", "Toca la lupa para cambiar tu busqueda"); flxFlushAll(); return; }
  int per = storeRowsPerPage(), start = storePage * per;
  if(start >= count){ storePage = 0; start = 0; }
  for(int row = 0; row < per && start + row < count; row++){
    int installedIndex = storeInstalledIndexAt(start + row); if(installedIndex < 0) continue;
    const FlexPkgInfo& app = storeInstalled[installedIndex]; int y = 148 + row * 174;
    fillRoundRect(18, y, SCR_W - 36, 154, 22, thCard());
    storeAppMark(34, y + 24, 76, app.name, rgb565(80,150 + row * 18,210));
    drawText(126, y + 22, app.name, 2, TH_TXT);
    char ver[64]; snprintf(ver, sizeof(ver), "Version %s - %lu KB", app.versionName, (unsigned long)(app.installedBytes / 1024u));
    drawText(126, y + 55, ver, 1, TH_TXT2);
    drawTextClip(126, y + 80, app.summary[0] ? app.summary : app.id, 1, TH_MUTE, SCR_W - 32);
    fillRoundRect(SCR_W - 132, y + 106, 96, 34, 17, TH_PRIM);
    drawTextC(SCR_W - 84, y + 116, "Abrir", 1, rgb565(255,255,255));
  }
  if(count > per){ char pg[24]; int pages = (count + per - 1) / per; snprintf(pg, sizeof(pg), "%d / %d", storePage + 1, pages); drawTextC(SCR_W / 2, 690, pg, 1, TH_MUTE); }
  flxFlushAll();
}

static void storeDetailRender(){
  storeHeader("Detalles", true);
  char name[FLEXPKG_NAME_MAX + 1] = "Aplicacion";
  char summary[FLEXPKG_SUMMARY_MAX + 1] = "";
  char version[64] = "";
  char packageId[FLEXPKG_ID_MAX + 1] = "";
  bool installed = storeSelectedInstalled;
  bool update = false;
  storeEnsureInstalled();
  if(installed && storeSelected >= 0 && storeSelected < storeInstalledN){
    FlexPkgInfo& a = storeInstalled[storeSelected];
    snprintf(name, sizeof(name), "%s", a.name); snprintf(summary, sizeof(summary), "%s", a.summary);
    snprintf(version, sizeof(version), "Version %s", a.versionName); snprintf(packageId, sizeof(packageId), "%s", a.id);
  } else {
    FlexStoreItem a;
    if(flexStoreCatalogItem(storeSelected, &a)){
      snprintf(name, sizeof(name), "%s", a.name); snprintf(summary, sizeof(summary), "%s", a.summary);
      snprintf(version, sizeof(version), "Version %s - %lu KB", a.versionName, (unsigned long)(a.packageBytes / 1024u));
      snprintf(packageId, sizeof(packageId), "%s", a.packageId);
      int local = storeInstalledFind(a.packageId);
      installed = local >= 0;
      update = installed && a.versionCode > storeInstalled[local].versionCode;
    }
  }
  storeAppMark(28, 116, 104, name, rgb565(105,91,230));
  drawText(154, 126, name, 3, TH_TXT); drawText(154, 170, version, 1, TH_TXT2);
  drawText(28, 254, "Acerca de esta app", 2, TH_TXT);
  drawTextClip(28, 292, summary[0] ? summary : packageId, 1, TH_TXT2, SCR_W - 28);
  drawText(28, 350, "Identificador firmado", 1, TH_MUTE);
  drawTextClip(28, 374, packageId, 1, TH_TXT2, SCR_W - 28);
  const char* mainAction = installed && !update ? "Abrir" : update ? "Actualizar" : "Instalar";
  fillRoundRect(28, 468, SCR_W - 56, 54, 27, TH_PRIM);
  drawTextC(SCR_W / 2, 485, mainAction, 2, rgb565(255,255,255));
  if(installed){
    fillRoundRect(28, 540, SCR_W - 56, 50, 25, storeConfirmDelete ? rgb565(190,45,55) : thCard());
    drawTextC(SCR_W / 2, 556, storeConfirmDelete ? "Confirmar desinstalacion" : "Desinstalar", 2,
              storeConfirmDelete ? rgb565(255,255,255) : rgb565(225,70,80));
  }
  flxFlushAll();
}

static void storeRuntimeRender(){
  uint16_t bg = storeRgb(storeRuntime.theme.background), surface = storeRgb(storeRuntime.theme.surface);
  uint16_t textCol = storeRgb(storeRuntime.theme.text), accent = storeRgb(storeRuntime.theme.accent);
  fillRect(0, 0, SCR_W, SCR_H, bg);
  fillRect(0, 0, SCR_W, 88, surface); storeBackGlyph(24, 30, textCol);
  drawText(66, 28, storeRuntime.screenTitle, 3, textCol);
  for(int i = 0; i < storeRuntime.componentCount; i++){
    const FlexUiComponent& c = storeRuntime.components[i];
    if(c.type == FLEXUI_TEXT){
      int sz = c.style == FLEXUI_STYLE_TITLE ? 3 : c.style == FLEXUI_STYLE_CAPTION ? 1 : 2;
      drawTextClip(c.x, c.y, c.text, sz, textCol, c.x + c.width);
    } else if(c.type == FLEXUI_BUTTON){
      fillRoundRect(c.x, c.y, c.width, c.height, c.height / 2, accent);
      drawTextC(c.x + c.width / 2, c.y + c.height / 2 - 8, c.text, 2, rgb565(255,255,255));
    }
  }
  if(storeToastVisible()){
    int w = SCR_W - 48, y = SCR_H - 120;
    fillRoundRect(24, y, w, 54, 18, surface);
    drawTextC(SCR_W / 2, y + 18, storeToastText, 1, textCol);
  }
  flxFlushAll();
}

static void storeRender(){
  setBuf(fb);
  if(storeView == SV_DISCOVER) storeDiscoverRender();
  else if(storeView == SV_INSTALLED) storeInstalledRender();
  else if(storeView == SV_DETAIL) storeDetailRender();
  else if(storeView == SV_RUNTIME) storeRuntimeRender();
  else storeSearchRender();
  if(storeView != SV_RUNTIME) storeToastOverlay();   // el runtime pinta el suyo con su tema
  storeLastState = flexStoreState(); storeLastProgress = flexStoreProgress();
}

static void storeOpenInstalled(int index){
  if(index < 0 || index >= storeInstalledN) return;
  if(!flexRuntimeLoad(storeInstalled[index].id, &storeRuntime)){
    storeToast(flexRuntimeError()); storeRender(); return;
  }
  storeView = SV_RUNTIME; storeRuntimeRender();
}

static void storeEnter(){
  flexRuntimeUnload(&storeRuntime);
  storeInstalledDirty = true; storeReloadInstalled();
  storeView = SV_DISCOVER; storeSearchSource = SV_DISCOVER; storeSearch[0] = 0; storePage = 0; storeSelected = -1; storeConfirmDelete = false;
  storeFilterInvalidate();
  if(flexStoreCatalogCount() == 0 && WiFi.status() == WL_CONNECTED && flexStoreState() != FLEXSTORE_LOADING) flexStoreRefresh();
  storeRender();
}

static void storeExit(){
  // Una descarga ya verificada o una instalacion en curso NO se aborta por
  // salir de la tienda: flexPkgInstall es transaccional (solo activa la version
  // nueva cuando esta completa y verificada) y flexPkgBegin() recupera una
  // transaccion interrumpida en el arranque siguiente. Lo que si se cancela es
  // una consulta de catalogo: no deja nada a medias y ya no le sirve a nadie.
  FlexStoreState state = flexStoreState();
  if(state != FLEXSTORE_DOWNLOADING && state != FLEXSTORE_INSTALLING) flexStoreCancel();
  flexRuntimeUnload(&storeRuntime);
  storeToastUntil = 0;
}

static void storeBack(){
  if(storeView == SV_RUNTIME){ flexRuntimeUnload(&storeRuntime); storeView = SV_INSTALLED; storePage = 0; storeRender(); return; }
  if(storeView == SV_SEARCH){ storeView = storeSearchSource; storePage = 0; storeRender(); return; }
  if(storeView == SV_DETAIL){ storeView = storeSelectedInstalled ? SV_INSTALLED : SV_DISCOVER; storeConfirmDelete = false; storeRender(); return; }
  flexRuntimeUnload(&storeRuntime); appClose();
}

static void storeTick(){
  FlexStoreState state = flexStoreState(); uint8_t pct = flexStoreProgress();
  if(storeView == SV_DISCOVER && (state != storeLastState || pct != storeLastProgress)){
    bool busy = (state == FLEXSTORE_LOADING || state == FLEXSTORE_DOWNLOADING || state == FLEXSTORE_INSTALLING);
    if(busy && state == storeLastState) storeProgressBand();   // mismo estado, solo el %
    else { storeFilterInvalidate(); storeRender(); }           // el catalogo puede haber cambiado
  }
  // El aviso caduca en CUALQUIER vista, no solo dentro del runtime.
  if(storeToastUntil && (int32_t)(millis() - storeToastUntil) >= 0){ storeToastUntil = 0; storeRender(); }
  if(!T.tap && !T.swipeUp && !T.swipeDown) return;
  if(T.tap && (storeView == SV_DISCOVER || storeView == SV_INSTALLED) && T.x >= SCR_W - 132 && T.y <= 76){
    accountStoreEnter(); return;
  }
  if(T.tap && (storeView == SV_DISCOVER || storeView == SV_INSTALLED) && T.x >= SCR_W - 190 && T.x < SCR_W - 132 && T.y <= 76){
    storeSearchSource = storeView; storeView = SV_SEARCH; storeSearchRender(); return;
  }
  // La franja de "atras" de la cabecera termina en la linea divisoria (y=76).
  // Antes llegaba a 92 y se comia la esquina izquierda de la pestana Descubrir
  // (88..130): un toque ahi cerraba la tienda en vez de cambiar de pestana.
  if(T.tap && ((T.x < 76 && T.y < 76) || (T.y > SCR_H - 64 && T.x < SCR_W / 3))){ storeBack(); return; }
  if(storeView == SV_SEARCH){ if(T.tap) storeSearchTap(T.x, T.y); return; }
  if(storeView == SV_RUNTIME){
    if(T.tap){
      FlexUiAction action;
      if(flexRuntimeHit(&storeRuntime, T.x, T.y, &action)){
        if(action.type == FLEXUI_ACTION_NOTIFY){ storeToast(action.value); storeRuntimeRender(); }
        else if(action.type == FLEXUI_ACTION_NAVIGATE){
          if(!flexRuntimeNavigate(&storeRuntime, action.value)) storeToast(flexRuntimeError());
          storeRuntimeRender();
        }
      }
    }
    return;
  }
  if(storeView == SV_DISCOVER || storeView == SV_INSTALLED){
    if(T.tap && T.y >= 84 && T.y <= 138){
      storeView = T.x < SCR_W / 2 ? SV_DISCOVER : SV_INSTALLED; storePage = 0; storeRender(); return;
    }
    if(T.swipeUp){ storePage++; storeRender(); return; }
    if(T.swipeDown){ if(storePage > 0) storePage--; else if(storeView == SV_DISCOVER) flexStoreRefresh(); storeRender(); return; }
    if(T.tap && storeView == SV_DISCOVER && (state == FLEXSTORE_LOADING || state == FLEXSTORE_DOWNLOADING || state == FLEXSTORE_INSTALLING) && T.y >= 420 && T.y <= 492){ flexStoreCancel(); return; }
    if(T.tap && storeView == SV_DISCOVER && (state == FLEXSTORE_ERROR || flexStoreCatalogCount() == 0) && T.y >= 460 && T.y <= 540){ flexStoreRefresh(); storeRender(); return; }
    if(T.tap && T.y >= 148 && T.y < 670){
      int row = (T.y - 148) / 174;
      if(row < 0 || row >= storeRowsPerPage()) return;
      if(storeView == SV_DISCOVER){
        int idx = storeCatalogIndexAt(storePage * storeRowsPerPage() + row); FlexStoreItem item;
        if(idx < 0) return;
        if(!flexStoreCatalogItem(idx, &item)) return;
        storeEnsureInstalled();
        int local = storeInstalledFind(item.packageId);
        if(T.x >= SCR_W - 156){
          if(local >= 0 && item.versionCode <= storeInstalled[local].versionCode) storeOpenInstalled(local);
          else { flexStoreInstall(idx); storeRender(); }
        } else { storeSelected = idx; storeSelectedInstalled = false; storeView = SV_DETAIL; storeConfirmDelete = false; storeRender(); }
      } else {
        int idx = storeInstalledIndexAt(storePage * storeRowsPerPage() + row);
        if(idx < 0 || idx >= storeInstalledN) return;
        if(T.x >= SCR_W - 156) storeOpenInstalled(idx);
        else { storeSelected = idx; storeSelectedInstalled = true; storeView = SV_DETAIL; storeConfirmDelete = false; storeRender(); }
      }
    }
    return;
  }
  if(storeView == SV_DETAIL && T.tap){
    // Las dos franjas siguen a los botones dibujados (468..522 y 540..590) y ya
    // no se solapan: antes 530..532 caia en las dos y ganaba la accion
    // principal, aunque en pantalla ese punto estuviera en el hueco.
    if(T.y >= 460 && T.y <= 528){
      if(storeSelectedInstalled) storeOpenInstalled(storeSelected);
      else {
        FlexStoreItem item; if(!flexStoreCatalogItem(storeSelected, &item)) return;
        storeEnsureInstalled();
        int local = storeInstalledFind(item.packageId);
        if(local >= 0 && item.versionCode <= storeInstalled[local].versionCode) storeOpenInstalled(local);
        else { storeView = SV_DISCOVER; flexStoreInstall(storeSelected); storeRender(); }
      }
      return;
    }
    if(T.y >= 534 && T.y <= 596){
      char id[FLEXPKG_ID_MAX + 1] = "";
      storeEnsureInstalled();
      if(storeSelectedInstalled && storeSelected >= 0 && storeSelected < storeInstalledN)
        snprintf(id, sizeof(id), "%s", storeInstalled[storeSelected].id);
      else {
        FlexStoreItem item;
        if(flexStoreCatalogItem(storeSelected, &item) && storeInstalledFind(item.packageId) >= 0)
          snprintf(id, sizeof(id), "%s", item.packageId);
      }
      // Si la app NO esta instalada, "Desinstalar" no se dibuja: su area no
      // debe responder. Antes si lo hacia, y el segundo toque acababa pidiendo
      // desinstalar un paquete que no existia.
      if(!id[0]) return;
      if(!storeConfirmDelete){ storeConfirmDelete = true; storeRender(); }
      else {
        if(flexPkgUninstall(id)){ storeInstalledDirty = true; storeView = SV_INSTALLED; storePage = 0; storeToast("Aplicacion desinstalada"); }
        else storeToast(flexPkgError());
        storeConfirmDelete = false; storeRender();
      }
    }
  }
}

#endif
