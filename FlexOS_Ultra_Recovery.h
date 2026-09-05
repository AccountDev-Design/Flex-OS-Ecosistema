// #############################################################
// ##  FLEX OS ULTRA  ·  RESTABLECER DATOS DE FABRICA Y MODO SEGURO
// ##  ----------------------------------------------------------
// ##  El asistente de borrado por etapas (reanudable tras un corte) y la
// ##  pantalla del modo seguro con sus restricciones.
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
#include "FlexOS_Ultra_Vault.h"   // eslabon anterior de la cadena

// #############################################################
// ##  RESTABLECER DATOS DE FABRICA
// ##  ------------------------------------------------------
// ##  Cuatro pantallas y un motor por ETAPAS:
// ##    1) Aviso: que se borra, que NO se borra, Cancelar / Continuar.
// ##    2) Seguridad: PIN o contrasena con el MISMO verificador del
// ##       bloqueo del sistema (lsuStartVerifyFor). Si no hay clave
// ##       configurada, hay que escribir "RESTABLECER" a mano.
// ##    3) Confirmacion final: deslizador sostenido. Nunca dos toques.
// ##    4) Borrado por etapas, con marcador persistente para poder
// ##       retomarlo si se corta la corriente.
// ##
// ##  La clave NUNCA se lee ni se compara aqui: esta pantalla solo
// ##  PIDE la verificacion al subsistema de bloqueo y espera su
// ##  veredicto. Los intentos, la espera progresiva y el bloqueo por
// ##  fallos son los mismos que en el resto del sistema, sin excepcion
// ##  ni atajo. Si la clave es incorrecta no se toca ni un byte.
// #############################################################
enum { FRV_INTRO = 0, FRV_TYPE, FRV_SLIDE, FRV_RUN, FRV_FAIL };
static int      frView = FRV_INTRO;
static uint32_t frStageMs = 0;               // millis en que empezo la etapa en curso
// Tiempo MINIMO que se muestra cada etapa. No es una espera artificial del
// borrado (la etapa ya se ejecuto), es la cadencia con la que se PUBLICA su
// nombre: sin esto, las siete etapas pasarian en dos cuadros y el usuario no
// podria leer en cual se quedo si algo fallara. Se mide con millis() y no
// bloquea: el bucle sigue dando vueltas y alimentando el watchdog.
#define FR_STEP_MIN_MS 260
static char     frTyped[20] = "";
static const char* FR_WORD = "RESTABLECER";
static uint32_t gSafeMsgMs = 0;              // aviso de app no disponible en Modo seguro

// ---- Deslizador de confirmacion (mismo lenguaje que el del apagado) ----
#define FRS_TRACK_X   40
#define FRS_TRACK_Y   600
#define FRS_TRACK_H   72
#define FRS_TRACK_W   (SCR_W - 80)
#define FRS_KNOB_PAD  6
#define FRS_KNOB_D    (FRS_TRACK_H - 2 * FRS_KNOB_PAD)
#define FRS_RUN       (FRS_TRACK_W - 2 * FRS_KNOB_PAD - FRS_KNOB_D)
#define FRS_DONE_PCT  92
static int  frKnob = 0, frGrab = 0;
static bool frDrag = false;

static uint16_t frBg(){   return gDark ? rgb565(16,18,26)    : rgb565(246,248,252); }
static uint16_t frHi(){   return gDark ? rgb565(240,242,248) : rgb565(20,22,30); }
static uint16_t frLo(){   return gDark ? rgb565(160,166,182) : rgb565(110,116,132); }
static uint16_t frCard(){ return gDark ? rgb565(30,34,46)    : rgb565(255,255,255); }

// Botones inferiores de la pantalla de aviso
#define FR_BTN_H   58
#define FR_BTN_Y   (SCR_H - 96)
#define FR_CAN_X   28
#define FR_CAN_W   ((SCR_W - 76) / 2)
#define FR_GO_X    (FR_CAN_X + FR_CAN_W + 20)
#define FR_GO_W    FR_CAN_W

static void frDrawIntro(){
  setBuf(bbuf);
  fillRect(0, 0, SCR_W, SCR_H, frBg());
  drawTextC(SCR_W / 2, 44, "Restablecer datos", 3, frHi());
  drawTextC(SCR_W / 2, 76, "de fabrica", 3, frHi());
  int cx = 20, cw = SCR_W - 40, cy = 124, ch = 300;
  fillRoundRect(cx, cy, cw, ch, 16, frCard());
  int y = cy + 16;
  drawText(cx + 16, y, "Se eliminaran de este dispositivo:", 2, frHi()); y += 30;
  const char* items[] = {
    "Cuentas locales y sus tokens",
    "Redes Wi-Fi guardadas y sus claves",
    "PIN o contrasena de bloqueo",
    "Ajustes, fondo y personalizacion",
    "Notas, dibujos y archivos del usuario",
    "Apps instaladas y sus datos",
    "Sesiones abiertas e historial",
    "Boveda local (Flex Vault) y sus claves"
  };
  for(unsigned i = 0; i < sizeof(items) / sizeof(items[0]); i++){
    fillCircle(cx + 22, y + 8, 3, rgb565(220,80,80));
    drawTextClip(cx + 34, y, items[i], 1, frLo(), cx + cw - 12);
    y += 22;
  }
  y = cy + ch + 16;
  fillRoundRect(cx, y, cw, 92, 16, frCard());
  drawText(cx + 16, y + 12, "NO se elimina:", 2, frHi());
  drawTextClip(cx + 16, y + 40, "El firmware instalado sigue siendo el mismo.", 1, frLo(), cx + cw - 12);
  drawTextClip(cx + 16, y + 60, "No se vuelve a una version anterior por OTA.", 1, frLo(), cx + cw - 12);
  // Botones reales
  fillRoundRect(FR_CAN_X, FR_BTN_Y, FR_CAN_W, FR_BTN_H, FR_BTN_H / 2, frCard());
  drawTextC(FR_CAN_X + FR_CAN_W / 2, FR_BTN_Y + (FR_BTN_H - 20) / 2, "Cancelar", 2, frHi());
  fillRoundRect(FR_GO_X, FR_BTN_Y, FR_GO_W, FR_BTN_H, FR_BTN_H / 2, rgb565(200,60,60));
  drawTextC(FR_GO_X + FR_GO_W / 2, FR_BTN_Y + (FR_BTN_H - 20) / 2, "Continuar", 2, rgb565(255,255,255));
  present(0, SCR_H - 1);
}

// Pantalla de escritura de "RESTABLECER" (solo cuando NO hay bloqueo
// configurado: sin clave que verificar, la barrera es escribir la palabra).
static void frDrawType(){
  setBuf(bbuf);
  // Se limpia la PANTALLA ENTERA, no solo hasta el teclado: con Liquid Glass
  // activo el panel del teclado muestrea lo que hay debajo, y si esa zona
  // conservara el cuadro anterior lo arrastraria dentro del vidrio.
  fillRect(0, 0, SCR_W, SCR_H, frBg());
  uint16_t c = frHi();
  strokeSegAA(30, 26, 18, 18, 2.4f, c); strokeSegAA(18, 18, 30, 10, 2.4f, c);
  drawTextC(SCR_W / 2, 46, "Confirma el borrado", 3, frHi());
  drawTextC(SCR_W / 2, 84, "Este equipo no tiene bloqueo configurado.", 1, frLo());
  drawTextC(SCR_W / 2, 104, "Escribe RESTABLECER para continuar.", 1, frLo());
  int fw = SCR_W - 64, fx = 32, fy = 150;
  fillRoundRect(fx, fy, fw, 56, 12, frCard());
  bool okWord = (strcmp(frTyped, FR_WORD) == 0);
  drawTextC(SCR_W / 2, fy + 18, frTyped[0] ? frTyped : "...", 3, okWord ? rgb565(90,190,130) : frHi());
  int by = fy + 80;
  fillRoundRect(32, by, SCR_W - 64, FR_BTN_H, FR_BTN_H / 2, okWord ? rgb565(200,60,60) : frCard());
  drawTextC(SCR_W / 2, by + (FR_BTN_H - 20) / 2, "Continuar", 2, okWord ? rgb565(255,255,255) : frLo());
  lsuDrawKb(0, 0);
  present(0, SCR_H - 1);
}
static int frTypeBtnY(){ return 150 + 80; }

static void frDrawSlideKnob(){
  int kx = FRS_TRACK_X + FRS_KNOB_PAD + frKnob;
  int ky = FRS_TRACK_Y + FRS_KNOB_PAD;
  fillRoundRect(FRS_TRACK_X, FRS_TRACK_Y, FRS_TRACK_W, FRS_TRACK_H, FRS_TRACK_H / 2, frCard());
  drawTextC(SCR_W / 2, FRS_TRACK_Y + (FRS_TRACK_H - 20) / 2, "Desliza para restablecer", 2, frLo());
  fillCircle(kx + FRS_KNOB_D / 2, ky + FRS_KNOB_D / 2, FRS_KNOB_D / 2, rgb565(210,66,60));
  uint16_t w = rgb565(255,255,255);
  int cx = kx + FRS_KNOB_D / 2, cy = ky + FRS_KNOB_D / 2;
  strokeSegAA(cx - 6, cy - 8, cx + 4, cy, 2.6f, w);
  strokeSegAA(cx + 4, cy, cx - 6, cy + 8, 2.6f, w);
}
static void frDrawSlide(){
  setBuf(bbuf);
  fillRect(0, 0, SCR_W, SCR_H, frBg());
  drawTextC(SCR_W / 2, 120, "Ultimo paso", 3, frHi());
  int cx = 24, cw = SCR_W - 48;
  fillRoundRect(cx, 180, cw, 150, 16, frCard());
  drawTextC(SCR_W / 2, 202, "Se borrara todo el contenido", 2, frHi());
  drawTextC(SCR_W / 2, 230, "y la configuracion de este", 2, frHi());
  drawTextC(SCR_W / 2, 258, "dispositivo.", 2, frHi());
  drawTextC(SCR_W / 2, 292, "Esta accion no se puede deshacer.", 1, rgb565(220,110,90));
  frDrawSlideKnob();
  fillRoundRect(FR_CAN_X, FR_BTN_Y, SCR_W - 2 * FR_CAN_X, FR_BTN_H, FR_BTN_H / 2, frCard());
  drawTextC(SCR_W / 2, FR_BTN_Y + (FR_BTN_H - 20) / 2, "Cancelar", 2, frHi());
  present(0, SCR_H - 1);
}
static void frRenderSlideBand(){
  setBuf(bbuf);
  fillRect(0, FRS_TRACK_Y - 4, SCR_W, FRS_TRACK_H + 8, frBg());
  frDrawSlideKnob();
  present(FRS_TRACK_Y - 4, FRS_TRACK_Y + FRS_TRACK_H + 3);
}

// ---- Progreso del borrado: ETAPAS reales, sin porcentajes inventados ----
#define FR_STAGES 7
static const char* FR_STAGE_TXT[FR_STAGES] = {
  "Preparando el dispositivo",
  "Borrando credenciales y tokens",
  "Cerrando la sesion remota",
  "Borrando datos de aplicaciones",
  "Borrando archivos del usuario",
  "Borrando ajustes del sistema",
  "Restaurando valores de fabrica"
};
static int frStageIdx(){
  int shown = gFrErr ? gFrErr : gFrStage;
  int i = shown - (int)FR_ST_ARMED;
  if(i < 0) i = 0; if(i >= FR_STAGES) i = FR_STAGES - 1;
  return i;
}
static void frDrawRun(){
  setBuf(bbuf);
  fillRect(0, 0, SCR_W, SCR_H, frBg());
  drawTextC(SCR_W / 2, 240, "Restableciendo", 3, frHi());
  int idx = frStageIdx();
  char b[40]; snprintf(b, sizeof(b), "Paso %d de %d", idx + 1, FR_STAGES);
  drawTextC(SCR_W / 2, 300, b, 2, frLo());
  drawTextC(SCR_W / 2, 336, FR_STAGE_TXT[idx], 2, frHi());
  // Barra por PASOS: cada segmento es una etapa terminada de verdad.
  int bw = SCR_W - 96, bx = 48, by = 396, seg = (bw - (FR_STAGES - 1) * 6) / FR_STAGES;
  for(int i = 0; i < FR_STAGES; i++)
    fillRoundRect(bx + i * (seg + 6), by, seg, 10, 5, (i <= idx) ? rgb565(200,60,60) : frCard());
  drawTextC(SCR_W / 2, 440, "No apagues el dispositivo", 1, frLo());
  present(0, SCR_H - 1);
}
static void frDrawFail(){
  setBuf(bbuf);
  fillRect(0, 0, SCR_W, SCR_H, frBg());
  drawTextC(SCR_W / 2, 200, "Restablecimiento", 3, frHi());
  drawTextC(SCR_W / 2, 236, "incompleto", 3, frHi());
  int idx = frStageIdx();
  drawTextC(SCR_W / 2, 300, "Fallo en:", 2, frLo());
  drawTextC(SCR_W / 2, 330, FR_STAGE_TXT[idx], 2, rgb565(230,120,90));
  drawTextC(SCR_W / 2, 380, "El dispositivo se queda en recuperacion:", 1, frLo());
  drawTextC(SCR_W / 2, 400, "no arranca con datos a medias.", 1, frLo());
  fillRoundRect(FR_CAN_X, FR_BTN_Y - 76, SCR_W - 2 * FR_CAN_X, FR_BTN_H, FR_BTN_H / 2, rgb565(200,60,60));
  drawTextC(SCR_W / 2, FR_BTN_Y - 76 + (FR_BTN_H - 20) / 2, "Reintentar", 2, rgb565(255,255,255));
  fillRoundRect(FR_CAN_X, FR_BTN_Y, SCR_W - 2 * FR_CAN_X, FR_BTN_H, FR_BTN_H / 2, frCard());
  drawTextC(SCR_W / 2, FR_BTN_Y + (FR_BTN_H - 20) / 2, "Reiniciar", 2, frHi());
  present(0, SCR_H - 1);
}

// ---- Motor de borrado: UNA etapa por vuelta del bucle ----
// Ninguna etapa bloquea mas que lo que dura su propia operacion, el TWDT se
// alimenta a la entrada y a la salida, y el estado se persiste ANTES de pasar a
// la siguiente. Si se va la corriente en cualquier punto, el arranque siguiente
// retoma exactamente por la etapa anotada.
static bool frStageRun(uint8_t st){
  flexFeedWdt();
  switch(st){
    case FR_ST_ARMED: {
      // Cerrar todo lo abierto y cortar las escrituras normales.
      gSessDirtyApp = -1;                       // ningun guardado diferido va a dispararse ya
      vaultLockFromSystem(FXV_LOCK_EXIT);
      flexBrowserExit();
      storeExit();
      flexAccountCancel();
      for(int i = 0; i < APP_N; i++) if(gAppState[i] != ALIFE_CLOSED){
        const AppHooks* h = appHooks(i);
        if(h && h->close) h->close();
        gAppState[i] = ALIFE_CLOSED;
      }
      while(swCardCount() > 0) swDropCard(0);
      return true;
    }
    case FR_ST_TOKENS: {
      // Credenciales y tokens LOCALES. La red se suelta con eraseap para que el
      // driver no conserve su propia copia del perfil.
      wifiCredsForget();
      flexAccountForgetLocal();
      if(gWifiDriverOn){
        WiFi.disconnect(true, true);
        WiFi.mode(WIFI_OFF);
        gWifiDriverOn = false;
      }
      flexFeedWdt();                            // apagar la radio puede tardar: el TWDT come aqui
      Preferences p;
      if(p.begin("flexos", false)){
        p.remove("lockpin"); p.remove("lockpass"); p.remove("locktype"); p.remove("lockfails");
        p.end();
      }
      gLockType = 0; lockFails = 0;
      return true;
    }
    case FR_ST_REMOTE: {
      // CIERRE DE SESION REMOTA. Este firmware no tiene todavia ningun servicio
      // de cuenta con API de revocacion: no hay a quien llamar, asi que no se
      // finge una llamada ni se inventa un resultado. Cuando exista, va aqui --
      // y un fallo de red NO puede impedir el borrado local, por eso esta etapa
      // devuelve true siempre.
      Serial.println(F("[RESET] sin servicio de cuenta remota: no hay sesion que revocar"));
      return true;
    }
    case FR_ST_APPDATA: {
      if(flexFsReady()){ fsWipeDir(FS_DIR_SESS); fsWipeDir(FS_DIR_CACHE); }
      for(int i = 0; i < APP_N; i++){ gSessLoaded[i] = false; gSessNeedSave[i] = false; }
      return true;
    }
    case FR_ST_FILES: {
      if(!flexFsReady()) return true;            // sin particion montada no hay archivos que borrar
      flexFeedWdt();
      bool ok = flexFsFactoryErase();
      flexFeedWdt();
      return ok;
    }
    case FR_ST_NVS: {
      // Namespaces CONOCIDOS, uno a uno. NUNCA un borrado global de NVS: eso se
      // llevaria por delante el propio marcador de recuperacion ("flexreset"),
      // que es justo lo que permite retomar esto si se corta la corriente.
      static const char* NS[] = {
        "flexos", "flexos_wifi", "flexos_time", "flexota", "flexqs",
        "flexacct", "fxvault", SAFE_NVS_NS
      };
      for(unsigned i = 0; i < sizeof(NS) / sizeof(NS[0]); i++){
        Preferences p;
        if(p.begin(NS[i], false)){ p.clear(); p.end(); }
      }
      return true;
    }
    case FR_ST_DEFAULTS: {
      // Valores de fabrica en RAM, para que la pantalla siguiente ya sea la de
      // un dispositivo nuevo aunque el reinicio tarde un instante.
      cfgOobeDone = false; cfgLang = 0; g24h = false; uiGlass = false; gDark = true;
      gIconStyle = 0; gBright = 80; gLockType = 0; lockFails = 0;
      gAutoLockMs = AUTOLOCK_DEFAULT_MS; gPoffPin = false; gAppLock = 0;
      kioskOn = false; kioskApp = -1;
    gLockWidgets = LW_CLOCK; gNavMode = 0; gAnimStyle = 0;
      snprintf(cfgName, sizeof(cfgName), "%s", "FlexOS Ultra");
      kbShortcutsDefaults();
      for(int i = 0; i < 12; i++) homeOrder[i] = (uint8_t)i;
      noteBuffer[0] = 0; noteCur = 0; noteScroll = 0; noteEditorScroll = 0; noteClearSel();
      noteView = 0; notePath[0] = 0;
      paintView = 0; paintPath[0] = 0; paintScroll = 0;
      pColor = P_PAL[0]; pSizeIx = 1; pStrokeN = 0;
      return true;
    }
  }
  return true;
}
static void frAdvance(){
  if(!frStageRun(gFrStage)){
    gFrErr = gFrStage; gFrStage = FR_ST_FAIL; frSaveState();
    frView = FRV_FAIL; frDrawFail();
    return;
  }
  if(gFrStage >= FR_ST_DEFAULTS){
    // Ultimo paso ANTES de reiniciar: se anota que el borrado termino. El
    // marcador NO se borra aqui -- se borra al confirmar el arranque limpio.
    gFrStage = FR_ST_DONE;
    if(!frSaveState()){
      gFrErr = FR_ST_DEFAULTS; gFrStage = FR_ST_FAIL;
      frView = FRV_FAIL; frDrawFail();
      return;
    }
    Serial.println(F("[RESET] borrado completo: reiniciando como dispositivo nuevo"));
    setBuf(bbuf);
    fillRect(0, 0, SCR_W, SCR_H, frBg());
    drawTextC(SCR_W / 2, SCR_H / 2 - 20, "Listo", 3, frHi());
    drawTextC(SCR_W / 2, SCR_H / 2 + 20, "Reiniciando...", 2, frLo());
    present(0, SCR_H - 1);
    flexFeedWdt();
    esp_restart();                              // API adecuada de reinicio (no un watchdog forzado)
    return;
  }
  gFrStage++;
  if(!frSaveState()){
    gFrErr = gFrStage; gFrStage = FR_ST_FAIL;
    frView = FRV_FAIL; frDrawFail();
    return;
  }
  frStageMs = millis();
  frDrawRun();
}

// ---- Entradas del asistente ----
static bool frFromSafe = false;      // el asistente se abrio desde el Modo seguro
static void frCancelToSettings(){
  frView = FRV_INTRO; frKnob = 0; frDrag = false; frTyped[0] = 0;
  // Se vuelve por donde se entro: al Modo seguro si de ahi se venia, y a
  // Ajustes en cualquier otro caso. En los dos, sin haber modificado un dato.
  if(frFromSafe){ frFromSafe = false; safeEnter(); return; }
  gState = ST_APP; gAppId = IC_AJUSTES; gAppState[IC_AJUSTES] = ALIFE_RUNNING;
  settingsRender();
  touchDropAll();
}
static void frEnterWizard(){
  // No se puede empezar un restablecimiento con una actualizacion en curso: el
  // OTA esta escribiendo en la particion de firmware y cortarlo a media
  // instalacion es exactamente lo que no puede pasar.
  if(flexOtaBusy()){
    setBuf(bbuf);
    fillRect(0, 0, SCR_W, SCR_H, frBg());
    drawTextC(SCR_W / 2, SCR_H / 2 - 20, "Actualizacion en curso", 2, frHi());
    drawTextC(SCR_W / 2, SCR_H / 2 + 12, "Espera a que termine para restablecer", 1, frLo());
    present(0, SCR_H - 1);
    gSafeMsgMs = millis();                      // se retira sola (ver frTick)
    gState = ST_FACTORY; frView = FRV_INTRO; frKnob = -1;   // -1 = solo aviso, sin asistente
    return;
  }
  gLand = false;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  frView = FRV_INTRO; frKnob = 0; frDrag = false; frTyped[0] = 0;
  gState = ST_FACTORY;
  frDrawIntro();
  touchDropAll();
}
// Verificacion superada (la resuelve lsuFinishAfter): se pasa al ultimo paso.
static void frAfterVerify(){
  gLand = false;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  gState = ST_FACTORY; frView = FRV_SLIDE; frKnob = 0; frDrag = false;
  frDrawSlide();
  touchDropAll();
}
// Arranca el borrado: a partir de aqui no hay marcha atras ni navegacion.
static void frBeginWipe(){
  gFrPending = true; gFrStage = FR_ST_ARMED; gFrErr = 0;
  if(!frSaveState()){                           // MARCADOR ANTES de tocar un solo dato
    gFrPending = false; gFrStage = FR_ST_FAIL; gFrErr = FR_ST_ARMED;
    frView = FRV_FAIL; frDrawFail();
    return;
  }
  frView = FRV_RUN;
  gState = ST_FACTORY;
  frStageMs = millis();
  frDrawRun();
}
// Retoma un borrado interrumpido. Se llama en el arranque, ANTES de montar
// nada privado y antes de abrir Cuenta, boveda, tienda o cualquier app.
static void frResumeAfterBoot(){
  gLand = false;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  gState = ST_FACTORY;
  if(gFrStage == FR_ST_FAIL){ frView = FRV_FAIL; frDrawFail(); return; }
  frView = FRV_RUN;
  frStageMs = millis();
  Serial.printf("[RESET] borrado interrumpido: se retoma en la etapa %u\n", (unsigned)gFrStage);
  frDrawRun();
}

static void frTick(){
  // Aviso "actualizacion en curso": se retira solo y vuelve a Ajustes.
  if(frKnob < 0){
    if(millis() - gSafeMsgMs > 2200 || T.tap){ frKnob = 0; frCancelToSettings(); }
    return;
  }
  if(frView == FRV_RUN){
    if(millis() - frStageMs < FR_STEP_MIN_MS) return;   // la etapa en curso se deja leer
    frAdvance();                                        // una etapa por vuelta: el TWDT respira
    return;
  }
  if(frView == FRV_FAIL){
    if(!T.tap) return;
    if(T.y >= FR_BTN_Y - 76 && T.y <= FR_BTN_Y - 76 + FR_BTN_H){
      // Si fallo el PRIMER marcador, todavia no se borro nada: vuelve a armar
      // la transaccion completa. En fallos posteriores, reanuda la etapa
      // idempotente anotada y exige de nuevo que NVS la confirme.
      if(!gFrPending){ frBeginWipe(); return; }
      uint8_t retry = (uint8_t)(gFrErr ? gFrErr : FR_ST_ARMED);
      gFrStage = retry; gFrErr = 0;
      if(!frSaveState()){
        gFrErr = retry; gFrStage = FR_ST_FAIL; frDrawFail();
        return;
      }
      frView = FRV_RUN; frStageMs = millis(); frDrawRun(); return;
    }
    if(T.y >= FR_BTN_Y && T.y <= FR_BTN_Y + FR_BTN_H){ flexFeedWdt(); esp_restart(); }
    return;
  }
  if(frView == FRV_INTRO){
    if(!T.tap) return;
    if(T.y >= FR_BTN_Y && T.y <= FR_BTN_Y + FR_BTN_H){
      if(T.x >= FR_CAN_X && T.x <= FR_CAN_X + FR_CAN_W){ frCancelToSettings(); return; }
      if(T.x >= FR_GO_X && T.x <= FR_GO_X + FR_GO_W){
        // SEGURIDAD: se delega enteramente en el verificador del bloqueo. Aqui
        // no se lee ninguna clave guardada ni se compara nada.
        if(gLockType > 0){ lsuStartVerifyFor(LSU_AFTER_FACTORY, -1); return; }
        frView = FRV_TYPE; frTyped[0] = 0;
        // lsuVerify a false: el teclado que se reutiliza aqui toma su paleta de
        // esa bandera, y en modo "verificar" pinta sobre el fondo desenfocado
        // del bloqueo, que no es el fondo de esta pantalla.
        lsuVerify = false;
        mapaActivo = LAYOUT_ES; kbLangEs = true; kbShift = true;
        kbExtrasOn = false; kbBotReserve = 0; kbApplySize(); kbMtSurfaceReset();
        frDrawType();
        return;
      }
    }
    return;
  }
  if(frView == FRV_TYPE){
    if(T.pressed && !(KB_MULTITOUCH_ON && gKbFastType)) kbFxPress(kbCellAt(T.x, T.y), lsuKeyCol(), lsuKeyTxt());
    if(!T.tap) return;
    if(T.x < 48 && T.y < 48){ frCancelToSettings(); return; }
    int by = frTypeBtnY();
    if(T.y >= by && T.y <= by + FR_BTN_H){
      if(strcmp(frTyped, FR_WORD) == 0) frAfterVerify();
      return;
    }
    int fi = kbFRowHit(T.x, T.y);
    if(fi >= 0){
      if(fi == 0) kbShift = !kbShift;
      else if(fi == 4){ int L = strlen(frTyped); if(L > 0) frTyped[L - 1] = 0; }
      frDrawType(); return;
    }
    int cell = kbCellAt(T.x, T.y);
    if(cell >= 0){
      const char* k = mapaActivo[cell / KB_COLS][cell % KB_COLS];
      // Solo letras A-Z: la palabra de confirmacion no lleva nada mas, y asi no
      // se pueden colar acentos ni simbolos que nunca coincidirian.
      if(k[1] == 0 && ((k[0] >= 'a' && k[0] <= 'z') || (k[0] >= 'A' && k[0] <= 'Z'))){
        int L = strlen(frTyped);
        if(L < (int)sizeof(frTyped) - 1){
          frTyped[L] = (char)((k[0] >= 'a') ? (k[0] - 32) : k[0]);   // siempre mayuscula
          frTyped[L + 1] = 0;
        }
        kbFxStart(cell);
      }
      frDrawType();
    }
    return;
  }
  // FRV_SLIDE: deslizador sostenido. Nunca se completa con un toque.
  int kx = FRS_TRACK_X + FRS_KNOB_PAD + frKnob;
  if(T.pressed && !frDrag){
    if(T.y >= FRS_TRACK_Y && T.y <= FRS_TRACK_Y + FRS_TRACK_H &&
       T.x >= kx - 24 && T.x <= kx + FRS_KNOB_D + 24){ frDrag = true; frGrab = T.x - kx; }
  }
  if(frDrag){
    if(T.down){
      int v = T.x - frGrab - (FRS_TRACK_X + FRS_KNOB_PAD);
      if(v < 0) v = 0; if(v > FRS_RUN) v = FRS_RUN;
      if(v != frKnob){ frKnob = v; frRenderSlideBand(); }
    } else {
      frDrag = false;
      if(FRS_RUN > 0 && frKnob * 100 / FRS_RUN >= FRS_DONE_PCT){ frBeginWipe(); return; }
      frKnob = 0; frRenderSlideBand();                    // no llego: vuelve a su sitio
    }
    return;
  }
  if(T.tap && T.y >= FR_BTN_Y && T.y <= FR_BTN_Y + FR_BTN_H){ frCancelToSettings(); return; }
}

// #############################################################
// ##  MODO SEGURO  ·  pantalla y restricciones
// ##  ------------------------------------------------------
// ##  Se entra solo despues de SAFE_FAIL_MAX reinicios ANORMALES
// ##  consecutivos (ver safeBootEval). Aqui no se borra nada por su
// ##  cuenta: se arranca con lo minimo, se dice la causa real y se
// ##  ofrecen las salidas.
// ##
// ##  Que sigue disponible: pantalla, tactil, Inicio, Ajustes,
// ##  Explorador (Almacenamiento), Reloj, Calculadora, el diagnostico
// ##  de esta misma pantalla y el Restablecimiento de fabrica.
// ##  Que no: apps de terceros, el resto de apps integradas no
// ##  esenciales, fondo y widgets del usuario, animaciones no
// ##  esenciales y cualquier tarea de red pesada.
// #############################################################
// Apps permitidas en Modo seguro. Es una lista blanca: lo que no esta,
// no se abre. Ninguna de ellas necesita red ni buffers grandes.
static bool safeAppAllowed(int id){
  if(!gSafeMode) return true;
  return id == IC_AJUSTES || id == IC_ALMACEN || id == IC_RELOJ || id == IC_CALC;
}
// Apps de TERCEROS instaladas. Este firmware todavia no tiene instalador de
// paquetes, asi que la mascara vale 0 y la fila correspondiente sale
// desactivada con su motivo: es un dato real, no un boton de adorno.
static uint16_t safeThirdPartyMask(){
  Preferences p; uint16_t m = 0;
  if(p.begin("flexos", true)){ m = (uint16_t)p.getInt("apps3rd", 0); p.end(); }
  return m;
}
static int safeThirdPartyCount(){
  uint16_t m = safeThirdPartyMask(); int n = 0;
  while(m){ n += (m & 1); m >>= 1; }
  return n;
}
// Aviso REAL al intentar abrir una app no permitida. Se compone sobre el
// escritorio ya pintado y se retira sola (safeToastTick).
static void safeDenyApp(int id){
  if(!homeBuf || !bbuf) return;
  int y0 = 300, h = 76;
  setBuf(bbuf);
  for(int j = y0; j < y0 + h; j++) memcpy(bbuf + (size_t)j * SCR_W, homeBuf + (size_t)j * SCR_W, SCR_W * 2);
  fillRoundRectA(28, y0 + 8, SCR_W - 56, h - 16, 16, rgb565(24,26,36), 240);
  drawTextC(SCR_W / 2, y0 + 18, "No disponible en Modo seguro", 2, rgb565(240,244,252));
  drawTextC(SCR_W / 2, y0 + 42, appName(id), 1, rgb565(170,178,196));
  present(y0, y0 + h - 1);
  gSafeMsgMs = millis();
}
static void safeToastTick(){
  if(!gSafeMsgMs || gState != ST_HOME) return;
  if(millis() - gSafeMsgMs < 1800) return;
  gSafeMsgMs = 0;
  if(!homeBuf) return;
  int y0 = 300, h = 76;
  setBuf(bbuf);
  for(int j = y0; j < y0 + h; j++) memcpy(bbuf + (size_t)j * SCR_W, homeBuf + (size_t)j * SCR_W, SCR_W * 2);
  present(y0, y0 + h - 1);
}

// Limpieza de caches SEGURAS: solo lo que el sistema sabe reconstruir solo.
// No toca notas, dibujos, ajustes ni credenciales.
static void safeClearCaches(){
  int n = fsWipeDir(FS_DIR_CACHE);
  if(blurBg){ free(blurBg); blurBg = NULL; }
  // El reproductor ya no tiene doble buffer de video: lo unico
  // grande que puede tener reservado es el fotograma comprimido y la
  // tira del volcado girado, y los dos los suelta vidReleaseMedia
  // por su unico camino de liberacion.
  if(gAppState[IC_MULTIMEDIA] == ALIFE_CLOSED) vidReleaseMedia(false);
  if(camScene){ free(camScene); camScene = NULL; }
  if(pStroke && gAppState[IC_PAINT] == ALIFE_CLOSED){ free(pStroke); pStroke = NULL; }
  qsDirty = true; gHomeDirty = true;
  Serial.printf("[SAFE] caches limpiadas (%d archivos)\n", n);
}

#define SAFE_ROWS   7
#define SAFE_ROW_H  66
#define SAFE_ROW_Y0 292
static const char* SAFE_ROW_T[SAFE_ROWS] = {
  "Reiniciar normalmente",
  "Apps de terceros",
  "Limpiar caches seguras",
  "Ajustes",
  "Explorador de archivos",
  "Ir al escritorio (limitado)",
  "Restablecer datos de fabrica"
};
static bool safeRowEnabled(int i){
  if(i == 1) return safeThirdPartyCount() > 0;
  return true;
}
static const char* safeRowVal(int i, char* buf, size_t n){
  switch(i){
    case 0: return "Sale del Modo seguro y arranca normal";
    case 1:
      if(safeThirdPartyCount() == 0) return "Ninguna instalada: nada que desactivar";
      snprintf(buf, n, "Desactivar las %d instaladas", safeThirdPartyCount());
      return buf;
    case 2: return "Vistas previas y datos temporales";
    case 3: return "Disponible en Modo seguro";
    case 4: return "Disponible en Modo seguro";
    case 5: return "Toca \"Modo seguro\" en el escritorio para volver";
    default: return "Borra todo el contenido del dispositivo";
  }
}
static void safeRender(){
  // Respeta el tema claro/oscuro del sistema, igual que Ajustes: el Modo seguro
  // reduce lo PESADO (fondo, widgets, animaciones), no la coherencia visual.
  uint16_t BG = PAGE_BG, CARD = SET_CARD_BG, HI = SET_TXT_HI, LO = SET_TXT_LO, MUTE = SET_TXT_MUTE;
  setBuf(bbuf);
  fillRect(0, 0, SCR_W, SCR_H, BG);
  drawTextC(SCR_W / 2, 44, "Modo seguro", 3, rgb565(226,160,70));
  drawTextC(SCR_W / 2, 82, "FlexOS ha arrancado con lo minimo", 1, LO);
  int cx = 20, cw = SCR_W - 40;
  fillRoundRect(cx, 112, cw, 148, 16, CARD);
  drawText(cx + 16, 126, "Motivo del ultimo fallo", 2, HI);
  drawTextClip(cx + 16, 154, safeCauseText(), 2, rgb565(214,104,74), cx + cw - 12);
  char b[56];
  snprintf(b, sizeof(b), "Reinicios anormales seguidos: %u", (unsigned)gSafeFails);
  drawTextClip(cx + 16, 184, b, 1, LO, cx + cw - 12);
  drawTextClip(cx + 16, 206, "Apps no esenciales y personalizacion", 1, LO, cx + cw - 12);
  drawTextClip(cx + 16, 226, "desactivadas mientras dure este modo.", 1, LO, cx + cw - 12);
  for(int i = 0; i < SAFE_ROWS; i++){
    int y = SAFE_ROW_Y0 + i * SAFE_ROW_H;
    bool on = safeRowEnabled(i);
    fillRoundRect(cx, y, cw, SAFE_ROW_H - 8, 14, CARD);
    uint16_t tc = on ? ((i == SAFE_ROWS - 1) ? rgb565(214,84,74) : HI) : MUTE;
    drawTextClip(cx + 16, y + 10, SAFE_ROW_T[i], 2, tc, cx + cw - 24);
    char vb[48];
    drawTextClip(cx + 16, y + 34, safeRowVal(i, vb, sizeof(vb)), 1, on ? LO : MUTE, cx + cw - 24);
  }
  drawTextC(SCR_W / 2, SCR_H - 40, "Un arranque estable limpia el contador solo", 1, MUTE);
  present(0, SCR_H - 1);
}
static void safeEnter(){
  gLand = false;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  gState = ST_SAFE;
  safeRender();
  touchDropAll();
}
static void safeTick(){
  if(!T.tap) return;
  for(int i = 0; i < SAFE_ROWS; i++){
    int y = SAFE_ROW_Y0 + i * SAFE_ROW_H;
    if(T.y < y || T.y > y + SAFE_ROW_H - 8) continue;
    if(!safeRowEnabled(i)) return;                 // fila inerte: ni siquiera repinta
    switch(i){
      case 0: safeExitAndReboot(); return;
      case 1: {                                    // desactivar apps de terceros
        Preferences p;
        if(p.begin("flexos", false)){ p.putInt("apps3rd", 0); p.end(); }
        safeRender(); return;
      }
      case 2: safeClearCaches(); safeRender(); return;
      case 3: renderHome(); enterApp(IC_AJUSTES); return;
      case 4: renderHome(); enterApp(IC_ALMACEN); return;
      case 5: gHomeDirty = true; enterHome(); touchDropAll(); return;
      default: frFromSafe = true; frEnterWizard(); return;
    }
  }
}
