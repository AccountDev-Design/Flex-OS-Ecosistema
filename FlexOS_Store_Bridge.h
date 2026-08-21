#ifndef FLEXOS_STORE_BRIDGE_H
#define FLEXOS_STORE_BRIDGE_H

// Este puente se incluye al final de FlexOS_Ultra.ino, cuando las primitivas
// graficas, Touch, appClose() y el tema semantico ya existen. Los modulos .cpp
// no conocen el framebuffer: solo publican datos y estados verificables.

enum StoreView : uint8_t { SV_DISCOVER = 0, SV_INSTALLED, SV_DETAIL, SV_RUNTIME };
static StoreView storeView = SV_DISCOVER;
static int storePage = 0;
static int storeSelected = -1;
static bool storeSelectedInstalled = false;
static bool storeConfirmDelete = false;
static FlexPkgInfo storeInstalled[FLEXPKG_MAX_INSTALLED];
static int storeInstalledN = 0;
static FlexRuntimeApp storeRuntime;
static FlexStoreState storeLastState = FLEXSTORE_IDLE;
static uint8_t storeLastProgress = 255;
static uint32_t storeToastUntil = 0;
static char storeToastText[128] = "";

static uint16_t storeRgb(uint32_t c){ return rgb565((c >> 16) & 255, (c >> 8) & 255, c & 255); }

static void storeToast(const char* text){
  snprintf(storeToastText, sizeof(storeToastText), "%s", text ? text : "");
  storeToastUntil = millis() + 2600;
}

static void storeReloadInstalled(){ storeInstalledN = flexPkgList(storeInstalled, FLEXPKG_MAX_INSTALLED); }

static void storeBackGlyph(int x, int y, uint16_t col){
  strokeSeg(x + 14, y, x, y + 12, 2, col);
  strokeSeg(x, y + 12, x + 14, y + 24, 2, col);
}

static void storeHeader(const char* title, bool nested){
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  if(nested) storeBackGlyph(24, 30, TH_TXT);
  drawText(nested ? 66 : 24, 28, title, 3, TH_TXT);
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
    storeReloadInstalled();
    storeToast("Aplicacion instalada y verificada");
  }
  int count = flexStoreCatalogCount();
  if(count == 0){
    storeEmpty("Aun no hay apps publicadas", "Desliza hacia abajo para actualizar");
    fillRoundRect(118, 474, SCR_W - 236, 50, 25, TH_PRIM);
    drawTextC(SCR_W / 2, 490, "Actualizar", 2, rgb565(255,255,255));
    flxFlushAll(); return;
  }
  int per = storeRowsPerPage(), start = storePage * per;
  if(start >= count){ storePage = 0; start = 0; }
  for(int row = 0; row < per && start + row < count; row++){
    FlexStoreItem item; flexStoreCatalogItem(start + row, &item);
    int y = 148 + row * 174;
    fillRoundRect(18, y, SCR_W - 36, 154, 22, thCard());
    storeAppMark(34, y + 24, 76, item.name, rgb565(105 + row * 20, 91, 230 - row * 24));
    drawText(126, y + 22, item.name, 2, TH_TXT);
    drawTextClip(126, y + 53, item.summary[0] ? item.summary : item.category, 1, TH_TXT2, SCR_W - 32);
    storeStars(126, y + 80, item.ratingX100);
    FlexPkgInfo local; bool installed = flexPkgGet(item.packageId, &local);
    const char* action = installed ? (item.versionCode > local.versionCode ? "Actualizar" : "Abrir") : "Instalar";
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
  storeHeader("Flex Store", false); storeTabs(); storeReloadInstalled();
  if(storeInstalledN == 0){ storeEmpty("No tienes apps instaladas", "Instala una desde la pestana Descubrir"); flxFlushAll(); return; }
  int per = storeRowsPerPage(), start = storePage * per;
  if(start >= storeInstalledN){ storePage = 0; start = 0; }
  for(int row = 0; row < per && start + row < storeInstalledN; row++){
    const FlexPkgInfo& app = storeInstalled[start + row]; int y = 148 + row * 174;
    fillRoundRect(18, y, SCR_W - 36, 154, 22, thCard());
    storeAppMark(34, y + 24, 76, app.name, rgb565(80,150 + row * 18,210));
    drawText(126, y + 22, app.name, 2, TH_TXT);
    char ver[64]; snprintf(ver, sizeof(ver), "Version %s - %lu KB", app.versionName, (unsigned long)(app.installedBytes / 1024u));
    drawText(126, y + 55, ver, 1, TH_TXT2);
    drawTextClip(126, y + 80, app.summary[0] ? app.summary : app.id, 1, TH_MUTE, SCR_W - 32);
    fillRoundRect(SCR_W - 132, y + 106, 96, 34, 17, TH_PRIM);
    drawTextC(SCR_W - 84, y + 116, "Abrir", 1, rgb565(255,255,255));
  }
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
  if(installed && storeSelected >= 0 && storeSelected < storeInstalledN){
    FlexPkgInfo& a = storeInstalled[storeSelected];
    snprintf(name, sizeof(name), "%s", a.name); snprintf(summary, sizeof(summary), "%s", a.summary);
    snprintf(version, sizeof(version), "Version %s", a.versionName); snprintf(packageId, sizeof(packageId), "%s", a.id);
  } else {
    FlexStoreItem a;
    if(flexStoreCatalogItem(storeSelected, &a)){
      snprintf(name, sizeof(name), "%s", a.name); snprintf(summary, sizeof(summary), "%s", a.summary);
      snprintf(version, sizeof(version), "Version %s - %lu KB", a.versionName, (unsigned long)(a.packageBytes / 1024u));
      snprintf(packageId, sizeof(packageId), "%s", a.packageId); update = flexStoreHasUpdate(&a);
      FlexPkgInfo local; installed = flexPkgGet(a.packageId, &local);
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
  if(storeToastUntil && (int32_t)(storeToastUntil - millis()) > 0){
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
  else storeRuntimeRender();
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
  flexRuntimeUnload(&storeRuntime); storeReloadInstalled();
  storeView = SV_DISCOVER; storePage = 0; storeSelected = -1; storeConfirmDelete = false;
  if(flexStoreCatalogCount() == 0 && WiFi.status() == WL_CONNECTED && flexStoreState() != FLEXSTORE_LOADING) flexStoreRefresh();
  storeRender();
}

static void storeExit(){
  flexStoreCancel();
  flexRuntimeUnload(&storeRuntime);
  storeToastUntil = 0;
}

static void storeBack(){
  if(storeView == SV_RUNTIME){ flexRuntimeUnload(&storeRuntime); storeView = SV_INSTALLED; storePage = 0; storeRender(); return; }
  if(storeView == SV_DETAIL){ storeView = storeSelectedInstalled ? SV_INSTALLED : SV_DISCOVER; storeConfirmDelete = false; storeRender(); return; }
  flexRuntimeUnload(&storeRuntime); appClose();
}

static void storeTick(){
  FlexStoreState state = flexStoreState(); uint8_t pct = flexStoreProgress();
  if(storeView == SV_DISCOVER && (state != storeLastState || pct != storeLastProgress)) storeRender();
  if(storeView == SV_RUNTIME && storeToastUntil && (int32_t)(millis() - storeToastUntil) >= 0){ storeToastUntil = 0; storeRuntimeRender(); }
  if(!T.tap && !T.swipeUp && !T.swipeDown) return;
  if(T.tap && ((T.x < 76 && T.y < 92) || (T.y > SCR_H - 64 && T.x < SCR_W / 3))){ storeBack(); return; }
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
        int idx = storePage * storeRowsPerPage() + row; FlexStoreItem item;
        if(!flexStoreCatalogItem(idx, &item)) return;
        FlexPkgInfo local; bool installed = flexPkgGet(item.packageId, &local);
        if(T.x >= SCR_W - 156){
          if(installed && item.versionCode <= local.versionCode){
            storeReloadInstalled(); for(int i = 0; i < storeInstalledN; i++) if(!strcmp(storeInstalled[i].id, item.packageId)){ storeOpenInstalled(i); break; }
          } else { flexStoreInstall(idx); storeRender(); }
        } else { storeSelected = idx; storeSelectedInstalled = false; storeView = SV_DETAIL; storeConfirmDelete = false; storeRender(); }
      } else {
        int idx = storePage * storeRowsPerPage() + row;
        if(idx >= storeInstalledN) return;
        if(T.x >= SCR_W - 156) storeOpenInstalled(idx);
        else { storeSelected = idx; storeSelectedInstalled = true; storeView = SV_DETAIL; storeConfirmDelete = false; storeRender(); }
      }
    }
    return;
  }
  if(storeView == SV_DETAIL && T.tap){
    if(T.y >= 456 && T.y <= 532){
      if(storeSelectedInstalled) storeOpenInstalled(storeSelected);
      else {
        FlexStoreItem item; if(!flexStoreCatalogItem(storeSelected, &item)) return;
        FlexPkgInfo local;
        if(flexPkgGet(item.packageId, &local) && item.versionCode <= local.versionCode){
          storeReloadInstalled(); for(int i = 0; i < storeInstalledN; i++) if(!strcmp(storeInstalled[i].id, item.packageId)){ storeOpenInstalled(i); break; }
        } else { storeView = SV_DISCOVER; flexStoreInstall(storeSelected); storeRender(); }
      }
      return;
    }
    if(T.y >= 530 && T.y <= 606){
      char id[FLEXPKG_ID_MAX + 1] = "";
      if(storeSelectedInstalled && storeSelected < storeInstalledN) snprintf(id, sizeof(id), "%s", storeInstalled[storeSelected].id);
      else { FlexStoreItem item; if(flexStoreCatalogItem(storeSelected, &item)) snprintf(id, sizeof(id), "%s", item.packageId); }
      if(!storeConfirmDelete){ storeConfirmDelete = true; storeRender(); }
      else if(id[0]){
        if(flexPkgUninstall(id)){ storeReloadInstalled(); storeView = SV_INSTALLED; storePage = 0; storeToast("Aplicacion desinstalada"); }
        else storeToast(flexPkgError());
        storeConfirmDelete = false; storeRender();
      }
    }
  }
}

#endif
