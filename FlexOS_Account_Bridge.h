#ifndef FLEXOS_ACCOUNT_BRIDGE_H
#define FLEXOS_ACCOUNT_BRIDGE_H

// Puente visual para FlexOS_Ultra.ino. Se incluye al final del .ino, cuando ya
// existen framebuffer, tactil, tema, Wi-Fi y las pantallas de Flex Store.

static bool accountFirstBoot = true;
static FlexAccountState accountLastState = (FlexAccountState)255;
static uint8_t accountLastProgress = 255;
static char accountLastStage[72] = "";

static void accountButton(int x, int y, int w, int h, const char* text, bool primary){
  fillRoundRectA(x, y, w, h, h / 2, primary ? rgb565(111,82,238) : rgb565(255,255,255), primary ? 245 : 62);
  drawTextC(x + w / 2, y + h / 2 - 8, text, 2, rgb565(255,255,255));
}

static void accountBackGlyph(){
  strokeSeg(38, 31, 24, 43, 3, rgb565(255,255,255));
  strokeSeg(24, 43, 38, 55, 3, rgb565(255,255,255));
}

static void accountRender(){
  FlexAccountSnapshot snapshot; flexAccountSnapshot(&snapshot);
  drawWallpaper(fb, false); setBuf(fb);

  // Halo y marca propios de Flex Account. Todo se dibuja con primitivas del
  // sistema: no hay capturas ni datos de demostracion incrustados.
  fillCircleAA(SCR_W / 2, 112, 54.0f, rgb565(102,72,230));
  fillCircleAA(SCR_W / 2 - 19, 102, 12.0f, rgb565(83,226,220));
  fillCircleAA(SCR_W / 2 + 17, 119, 19.0f, rgb565(255,255,255));
  drawTextC(SCR_W / 2, 187, "Flex Account", 4, rgb565(255,255,255));
  if(!accountFirstBoot) accountBackGlyph();

  int cardX = 28, cardY = 238, cardW = SCR_W - 56, cardH = 386;
  fillRoundRectA(cardX, cardY, cardW, cardH, 30, rgb565(18,22,42), 218);

  if(snapshot.state == FLEX_ACCOUNT_LINKED){
    fillCircleAA(SCR_W / 2, 310, 34.0f, rgb565(44,190,133));
    strokeSeg(SCR_W / 2 - 14, 310, SCR_W / 2 - 3, 323, 4, rgb565(255,255,255));
    strokeSeg(SCR_W / 2 - 3, 323, SCR_W / 2 + 18, 295, 4, rgb565(255,255,255));
    drawTextC(SCR_W / 2, 365, "Cuenta vinculada", 3, rgb565(255,255,255));
    drawTextC(SCR_W / 2, 411, snapshot.flexAddress, 2, rgb565(115,231,226));
    if(snapshot.displayName[0]) drawTextC(SCR_W / 2, 445, snapshot.displayName, 1, rgb565(205,210,228));
    drawTextC(SCR_W / 2, 490, "Tu correo de recuperacion nunca", 1, rgb565(174,181,205));
    drawTextC(SCR_W / 2, 512, "se muestra en FlexOS ni a otros usuarios.", 1, rgb565(174,181,205));
    accountButton(58, 548, SCR_W - 116, 58, accountFirstBoot ? "Continuar" : "Volver a Flex Store", true);
  } else if(snapshot.state == FLEX_ACCOUNT_REQUESTING){
    drawTextC(SCR_W / 2, 300, "Creando enlace seguro", 3, rgb565(255,255,255));
    int px = 60, py = 368, pw = SCR_W - 120;
    fillRoundRect(px, py, pw, 16, 8, rgb565(52,58,82));
    int filled = pw * snapshot.progress / 100; if(filled < 8) filled = 8;
    fillRoundRect(px, py, filled, 16, 8, rgb565(92,220,215));
    drawTextC(SCR_W / 2, 412, snapshot.stage, 1, rgb565(205,210,228));
    drawTextC(SCR_W / 2, 462, "FlexOS no guarda tu contrasena.", 1, rgb565(174,181,205));
    accountButton(110, 540, SCR_W - 220, 54, "Cancelar", false);
  } else if(snapshot.state == FLEX_ACCOUNT_CODE_READY){
    drawTextC(SCR_W / 2, 274, "Abre en tu celular", 2, rgb565(205,210,228));
    drawTextC(SCR_W / 2, 308, "flex-developer-studio", 1, rgb565(115,231,226));
    drawTextC(SCR_W / 2, 330, ".ralvarezsantos980.chatgpt.site/activate", 1, rgb565(115,231,226));
    drawTextC(SCR_W / 2, 375, "y escribe este codigo", 1, rgb565(174,181,205));
    fillRoundRectA(90, 405, SCR_W - 180, 86, 24, rgb565(255,255,255), 245);
    drawTextC(SCR_W / 2, 430, snapshot.code, 5, rgb565(47,34,104));
    drawTextC(SCR_W / 2, 514, "El codigo vence en 10 minutos", 1, rgb565(174,181,205));
    accountButton(110, 552, SCR_W - 220, 54, "Cancelar", false);
  } else {
    bool online = WiFi.status() == WL_CONNECTED;
    const char* title = snapshot.state == FLEX_ACCOUNT_ERROR ? "No se pudo vincular" :
                        snapshot.state == FLEX_ACCOUNT_EXPIRED ? "El codigo expiro" : "Una cuenta para todo FlexOS";
    drawTextC(SCR_W / 2, 278, title, 2, rgb565(255,255,255));
    if(snapshot.state == FLEX_ACCOUNT_ERROR && snapshot.error[0]){
      drawTextC(SCR_W / 2, 318, snapshot.error, 1, rgb565(255,154,166));
    } else {
      drawTextC(SCR_W / 2, 318, "Publica apps, comenta, recibe soporte", 1, rgb565(205,210,228));
      drawTextC(SCR_W / 2, 340, "y usa tu identidad usuario@flex.", 1, rgb565(205,210,228));
    }
    if(!online){
      fillRoundRectA(52, 378, SCR_W - 104, 48, 18, rgb565(255,181,61), 52);
      drawTextC(SCR_W / 2, 394, "Necesitas conectar Wi-Fi primero", 1, rgb565(255,223,161));
    }
    accountButton(52, 452, SCR_W - 104, 58, online ? "Iniciar sesion" : "Conectar Wi-Fi", true);
    accountButton(52, 526, SCR_W - 104, 54, online ? "Crear una cuenta" : "Configurar red", false);
  }

  const char* bottom = accountFirstBoot ? "Omitir por ahora" : "Volver sin cambios";
  drawTextC(SCR_W / 2, 704, bottom, 2, rgb565(255,255,255));
  drawTextC(SCR_W / 2, 750, "@flex es una identidad publica, no un buzon de correo.", 1, rgb565(210,214,229));
  flxFlushAll();

  accountLastState = snapshot.state;
  accountLastProgress = snapshot.progress;
  snprintf(accountLastStage, sizeof(accountLastStage), "%s", snapshot.stage);
}

static void accountEnter(bool firstBoot){
  accountFirstBoot = firstBoot;
  accountLastState = (FlexAccountState)255;
  gState = ST_OOBE_ACCOUNT;
  accountRender();
}

static void accountOobeEnter(){ accountEnter(true); }
static void accountStoreEnter(){ accountEnter(false); }

static void accountFinish(){
  flexAccountCancel();
  if(accountFirstBoot){
    cfgSaveOobe();
    renderHome(); renderLock(); showLock();
    gState = ST_LOCK; lockOff = 0; lastLockOff = -1;
  } else {
    gState = ST_APP;
    storeEnter();
  }
}

static void accountOobeTick(){
  FlexAccountSnapshot snapshot; flexAccountSnapshot(&snapshot);
  if(snapshot.state != accountLastState || snapshot.progress != accountLastProgress || strcmp(snapshot.stage, accountLastStage)) accountRender();
  if(!T.tap) return;

  // Barra inferior / flecha: omite solamente la vinculacion, nunca la
  // configuracion basica. Flex Store seguira mostrando el acceso a Cuenta.
  if(T.y >= 670 || (!accountFirstBoot && T.x < 64 && T.y < 78)){ accountFinish(); return; }

  if(snapshot.state == FLEX_ACCOUNT_LINKED){
    if(T.y >= 530 && T.y <= 622) accountFinish();
    return;
  }
  if(snapshot.state == FLEX_ACCOUNT_REQUESTING || snapshot.state == FLEX_ACCOUNT_CODE_READY){
    if(T.y >= 520 && T.y <= 622){ flexAccountCancel(); accountRender(); }
    return;
  }
  if(T.y >= 430 && T.y <= 520){
    if(WiFi.status() != WL_CONNECTED) wifiOobeEnter();
    else if(flexAccountRequestCode(cfgName)) accountRender();
    return;
  }
  if(T.y >= 515 && T.y <= 610){
    if(WiFi.status() != WL_CONNECTED) wifiOobeEnter();
    else if(flexAccountRequestCode(cfgName)) accountRender();
  }
}

#endif
