#ifndef FLEXOS_ACCOUNT_BRIDGE_H
#define FLEXOS_ACCOUNT_BRIDGE_H

// Puente visual para FlexOS_Ultra.ino. Se incluye al final del .ino, cuando ya
// existen framebuffer, tactil, tema, Wi-Fi y las pantallas de Flex Store.

// A DONDE SE VUELVE AL SALIR. Antes solo habia un booleano de "primer
// arranque", y el retorno del configurador de Wi-Fi lo forzaba SIEMPRE a true:
// entrar a Wi-Fi desde el boton Cuenta de Flex Store devolvia la pantalla en
// modo OOBE y, al pulsar Continuar/Omitir, el equipo se iba al bloqueo y
// reescribia la marca de primera configuracion. Con un destino explicito cada
// via de entrada vuelve a su sitio y el retorno del Wi-Fi lo conserva.
enum AccountReturn : uint8_t { ACC_RET_OOBE = 0, ACC_RET_STORE, ACC_RET_SETTINGS };
static AccountReturn accountReturn = ACC_RET_OOBE;
static bool accountFirstBoot = true;              // == (accountReturn == ACC_RET_OOBE)
static FlexAccountState accountLastState = (FlexAccountState)255;
static uint8_t accountLastProgress = 255;
static char accountLastStage[72] = "";
static bool accountLastOnline = false;            // la pantalla depende del estado real del Wi-Fi
static uint32_t accountPollMs = 0;                // cadencia del sondeo de estado (no del tactil)
static FlexAccountLink accountLastLink = (FlexAccountLink)255;
static char accountLastError[144] = "";

// La cuenta esta guardada pero el servidor ya no la acepta: lo unico util es
// volver a vincular (la cuenta guardada se conserva si se cancela).
static bool accountNeedsRelink(const FlexAccountSnapshot& s){
  return s.linked && (s.link == FLEX_LINK_AUTH_REQUIRED || s.link == FLEX_LINK_TOKEN_EXPIRED);
}
// Color de la pildora de estado del vinculo. Sin red no es un error: ambar
// suave, nunca el rojo de "desvinculada".
static uint16_t accountLinkColor(FlexAccountLink link){
  switch(link){
    case FLEX_LINK_LINKED:              return rgb565(44,190,133);
    case FLEX_LINK_LINKED_OFFLINE:      return rgb565(120,146,196);
    case FLEX_LINK_NETWORK_UNAVAILABLE: return rgb565(232,170,66);
    case FLEX_LINK_AUTH_REQUIRED:
    case FLEX_LINK_TOKEN_EXPIRED:       return rgb565(240,128,92);
    case FLEX_LINK_ERROR:               return rgb565(236,96,110);
    default:                            return rgb565(150,156,176);
  }
}

// Texto del boton principal de la pantalla de cuenta sin vincular. Tras un fallo
// o un codigo caducado dice "Reintentar": se vuelve a pedir un enlace SIN
// reiniciar el aparato.
static const char* accountPrimaryLabel(FlexAccountState state, bool online){
  if(!online) return "Conectar Wi-Fi";
  return (state == FLEX_ACCOUNT_ERROR || state == FLEX_ACCOUNT_EXPIRED) ? "Reintentar" : "Iniciar sesion";
}
// Linea bajo el codigo: si la consulta de aprobacion NO se pudo hacer (sin Wi-Fi,
// sin memoria...) dice por que, en lugar de seguir "esperando" a ciegas hasta
// que caduque el codigo.
static const char* accountCodeNote(const FlexAccountSnapshot& s){
  return s.error[0] ? s.error : "El codigo vence en 10 minutos";
}

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
  bool online = WiFi.status() == WL_CONNECTED;
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
    // VINCULADA. El estado del vinculo (conectada, sin conexion, servicio no
    // disponible, hay que volver a iniciar sesion) va en una pildora: sin red
    // la cuenta SIGUE vinculada y la pantalla lo dice asi.
    bool relink = accountNeedsRelink(snapshot);
    uint16_t lc = accountLinkColor(snapshot.link);
    fillCircleAA(SCR_W / 2, 300, 34.0f, relink ? lc : rgb565(44,190,133));
    if(relink){
      fillRoundRect(SCR_W / 2 - 3, 280, 6, 26, 3, rgb565(255,255,255));
      fillCircleAA(SCR_W / 2, 316, 3.5f, rgb565(255,255,255));
    } else {
      strokeSeg(SCR_W / 2 - 14, 300, SCR_W / 2 - 3, 313, 4, rgb565(255,255,255));
      strokeSeg(SCR_W / 2 - 3, 313, SCR_W / 2 + 18, 285, 4, rgb565(255,255,255));
    }
    drawTextC(SCR_W / 2, 350, relink ? (snapshot.link == FLEX_LINK_TOKEN_EXPIRED ? "Sesion caducada" : "Vuelve a iniciar sesion")
                                     : "Cuenta vinculada", 3, rgb565(255,255,255));
    drawTextC(SCR_W / 2, 392, snapshot.flexAddress, 2, rgb565(115,231,226));
    if(snapshot.displayName[0]) drawTextC(SCR_W / 2, 422, snapshot.displayName, 1, rgb565(205,210,228));
    const char* pill = flexAccountLinkLabel(snapshot.link);
    int pw = textW(pill, 1) + 40; if(pw > cardW - 40) pw = cardW - 40;
    int px = SCR_W / 2 - pw / 2;
    fillRoundRectA(px, 446, pw, 28, 14, lc, 60);
    fillCircleAA(px + 16, 460, 4.0f, lc);
    drawTextC(SCR_W / 2 + 6, 453, pill, 1, rgb565(255,255,255));
    if(snapshot.error[0]) drawTextC(SCR_W / 2, 486, snapshot.error, 1, rgb565(255,154,166));
    else if(snapshot.linkDetail[0]) drawTextC(SCR_W / 2, 486, snapshot.linkDetail, 1, rgb565(174,181,205));
    drawTextC(SCR_W / 2, 512, "Tu correo de recuperacion nunca se muestra.", 1, rgb565(150,158,184));
    if(relink){
      accountButton(58, 548, SCR_W - 116, 58, online ? "Volver a vincular" : "Conectar Wi-Fi", true);
    } else {
      accountButton(58, 548, SCR_W - 116, 58,
                    accountReturn == ACC_RET_OOBE ? "Continuar" :
                    accountReturn == ACC_RET_SETTINGS ? "Volver a Ajustes" : "Volver a Flex Store", true);
    }
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
    drawTextC(SCR_W / 2, 514, accountCodeNote(snapshot), 1, snapshot.error[0] ? rgb565(255,181,61) : rgb565(174,181,205));
    accountButton(110, 552, SCR_W - 220, 54, "Cancelar", false);
  } else {
    const char* title = snapshot.state == FLEX_ACCOUNT_ERROR ? "No se pudo vincular" :
                        snapshot.state == FLEX_ACCOUNT_EXPIRED ? "El codigo expiro" :
                        snapshot.link == FLEX_LINK_ERROR ? "No se pudo leer la cuenta" : "Una cuenta para todo FlexOS";
    drawTextC(SCR_W / 2, 278, title, 2, rgb565(255,255,255));
    if(snapshot.state == FLEX_ACCOUNT_ERROR && snapshot.error[0]){
      drawTextC(SCR_W / 2, 318, snapshot.error, 1, rgb565(255,154,166));
    } else if(snapshot.link == FLEX_LINK_ERROR && snapshot.linkDetail[0]){
      drawTextC(SCR_W / 2, 318, snapshot.linkDetail, 1, rgb565(255,154,166));
    } else {
      drawTextC(SCR_W / 2, 318, "Publica apps, comenta, recibe soporte", 1, rgb565(205,210,228));
      drawTextC(SCR_W / 2, 340, "y usa tu identidad usuario@flex.", 1, rgb565(205,210,228));
    }
    if(!online){
      fillRoundRectA(52, 378, SCR_W - 104, 48, 18, rgb565(255,181,61), 52);
      drawTextC(SCR_W / 2, 394, "Necesitas conectar Wi-Fi primero", 1, rgb565(255,223,161));
    }
    accountButton(52, 452, SCR_W - 104, 58, accountPrimaryLabel(snapshot.state, online), true);
    accountButton(52, 526, SCR_W - 104, 54, online ? "Crear una cuenta" : "Configurar red", false);
  }

  const char* bottom = accountFirstBoot ? "Omitir por ahora" : "Volver sin cambios";
  drawTextC(SCR_W / 2, 704, bottom, 2, rgb565(255,255,255));
  drawTextC(SCR_W / 2, 750, "@flex es una identidad publica, no un buzon de correo.", 1, rgb565(210,214,229));
  flxFlushAll();

  accountLastState = snapshot.state;
  accountLastProgress = snapshot.progress;
  accountLastOnline = online;
  accountLastLink = snapshot.link;
  snprintf(accountLastStage, sizeof(accountLastStage), "%s", snapshot.stage);
  snprintf(accountLastError, sizeof(accountLastError), "%s", snapshot.error);
}

static void accountEnter(AccountReturn where){
  accountReturn = where;
  accountFirstBoot = (where == ACC_RET_OOBE);
  accountLastState = (FlexAccountState)255;
  accountLastLink = (FlexAccountLink)255;
  accountPollMs = millis();
  // Entrar en la pantalla de la cuenta con Wi-Fi pide comprobarla (no bloquea:
  // la red va en la tarea de Flex Account, con su propio freno).
  if(flexAccountLinked() && WiFi.status() == WL_CONNECTED) flexAccountRequestValidation();
  gState = ST_OOBE_ACCOUNT;
  accountRender();
}

static void accountOobeEnter(){     accountEnter(ACC_RET_OOBE); }
static void accountStoreEnter(){    accountEnter(ACC_RET_STORE); }
static void accountSettingsEnter(){ accountEnter(ACC_RET_SETTINGS); }
// Vuelta desde el configurador de Wi-Fi: repinta SIN cambiar el destino de
// salida que tenia la pantalla antes de ir a configurar la red.
static void accountResumeEnter(){   accountEnter(accountReturn); }

// Texto de la fila de Ajustes -> General -> Flex Account. Dice el estado REAL:
// nunca inventa una direccion ni afirma que hay sesion si NVS no la trajo.
static void accountSettingsText(char* out, size_t n){
  FlexAccountSnapshot snapshot; flexAccountSnapshot(&snapshot);
  if(snapshot.state == FLEX_ACCOUNT_REQUESTING || snapshot.state == FLEX_ACCOUNT_CODE_READY)
    snprintf(out, n, "Vinculacion en curso");
  else if(snapshot.linked && snapshot.flexAddress[0]){
    // Vinculada: la direccion SIEMPRE, y el matiz solo si lo hay. Sin red no
    // se dice "sin cuenta": se dice que no hay conexion.
    if(snapshot.link == FLEX_LINK_LINKED) snprintf(out, n, "%s", snapshot.flexAddress);
    else if(snapshot.link == FLEX_LINK_AUTH_REQUIRED || snapshot.link == FLEX_LINK_TOKEN_EXPIRED)
      snprintf(out, n, "%s", flexAccountLinkLabel(snapshot.link));
    else snprintf(out, n, "%s \xC2\xB7 sin conexi\xC3\xB3n", snapshot.flexAddress);
  }
  else if(snapshot.link == FLEX_LINK_ERROR) snprintf(out, n, "%s", flexAccountLinkLabel(snapshot.link));
  else snprintf(out, n, "Sin cuenta vinculada");
}

static void accountFinish(){
  // Cancelar solo si hay un enlace EN CURSO. Antes se llamaba siempre, y eso
  // dejaba la peticion de cancelacion levantada despues de vincular con exito.
  FlexAccountState state = flexAccountState();
  if(state == FLEX_ACCOUNT_REQUESTING || state == FLEX_ACCOUNT_CODE_READY) flexAccountCancel();
  if(accountReturn == ACC_RET_OOBE){
    cfgSaveOobe();
    renderHome(); renderLock(); showLock();
    gState = ST_LOCK; lockOff = 0; lastLockOff = -1;
  } else if(accountReturn == ACC_RET_SETTINGS){
    gState = ST_APP;
    settingsRender();
  } else {
    gState = ST_APP;
    storeEnter();
  }
}

static void accountOobeTick(){
  // El estado del enlace cambia como mucho cada 3 s (cadencia del sondeo de la
  // tarea de red): copiar el snapshot bajo mutex en CADA cuadro no aporta nada.
  // El tactil, en cambio, se sigue atendiendo en todos los cuadros.
  uint32_t now = millis();
  if((uint32_t)(now - accountPollMs) >= 80){
    accountPollMs = now;
    FlexAccountSnapshot snapshot; flexAccountSnapshot(&snapshot);
    bool online = WiFi.status() == WL_CONNECTED;
    if(snapshot.state != accountLastState || snapshot.progress != accountLastProgress ||
       online != accountLastOnline || snapshot.link != accountLastLink ||
       strcmp(snapshot.stage, accountLastStage) || strcmp(snapshot.error, accountLastError)) accountRender();
  }
  if(!T.tap) return;

  // Barra inferior / flecha: omite solamente la vinculacion, nunca la
  // configuracion basica. Flex Store y Ajustes siguen mostrando el acceso a
  // Cuenta, asi que omitir aqui no deja el dispositivo sin via de vuelta.
  if(T.y >= 670 || (!accountFirstBoot && T.x < 64 && T.y < 78)){ accountFinish(); return; }

  FlexAccountState state = accountLastState;
  if(state == FLEX_ACCOUNT_LINKED){
    if(T.y >= 530 && T.y <= 622){
      FlexAccountSnapshot snapshot; flexAccountSnapshot(&snapshot);
      if(accountNeedsRelink(snapshot)){
        // Volver a vincular CONSERVA la cuenta guardada hasta que la nueva
        // credencial este aprobada y guardada.
        if(WiFi.status() != WL_CONNECTED) wifiOobeEnter();
        else if(flexAccountRequestCode(cfgName)) accountRender();
      } else accountFinish();
    }
    return;
  }
  if(state == FLEX_ACCOUNT_REQUESTING || state == FLEX_ACCOUNT_CODE_READY){
    if(T.y >= 520 && T.y <= 622){ flexAccountCancel(); accountRender(); }
    return;
  }
  // Las dos franjas siguen EXACTAMENTE a los dos botones dibujados (452..510 y
  // 526..580), sin solaparse entre si ni con la barra inferior.
  if(T.y >= 444 && T.y <= 518){
    if(WiFi.status() != WL_CONNECTED) wifiOobeEnter();
    else if(flexAccountRequestCode(cfgName)) accountRender();
    return;
  }
  if(T.y >= 520 && T.y <= 588){
    if(WiFi.status() != WL_CONNECTED) wifiOobeEnter();
    else if(flexAccountRequestCode(cfgName)) accountRender();
  }
}

#endif
