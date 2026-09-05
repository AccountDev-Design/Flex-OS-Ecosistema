// #############################################################
// ##  FLEX OS ULTRA  ·  DESBLOQUEO, SUSPENSION Y APAGADO COMPLETO
// ##  ----------------------------------------------------------
// ##  Punto unico de salida tras una verificacion correcta, el despertar
// ##  con la pantalla aun a oscuras y el apagado real (deep sleep) con su
// ##  filtro de encendido.
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
#include "FlexOS_Ultra_AppDrawer.h"   // eslabon anterior de la cadena

// #############################################################
// ##  FASE 3 - QUE HACER TRAS UNA VERIFICACION CORRECTA
// ##  Un unico punto de salida para las cuatro rutas nuevas, asi
// ##  lsuStartVerify sigue siendo la UNICA ruta de verificacion.
// #############################################################
static void lsuFinishAfter(){
  int what = lsuAfter, id = lsuAfterApp;
  lsuAfter = LSU_AFTER_UNLOCK; lsuAfterApp = -1;
  if(what == LSU_AFTER_KIOSKOUT){ kioskExitNow(); return; }
  // Apagado seguro: PIN correcto -> se continua con la animacion de apagado. No
  // se repinta el escritorio de abajo a proposito: la animacion arranca haciendo
  // un fundido a negro DESDE lo que ya hay en pantalla.
  if(what == LSU_AFTER_POWEROFF){ poffBeginAnim(); return; }
  // RESTABLECIMIENTO: la clave era correcta -> ultimo paso (el deslizador). El
  // borrado NO empieza aqui: todavia hay una confirmacion deliberada por delante.
  if(what == LSU_AFTER_FACTORY){ frAfterVerify(); return; }
  gState = ST_HOME;
  if(what == LSU_AFTER_LOCKAPP)        appLockSet(id, true);
  else if(what == LSU_AFTER_UNLOCKAPP) appLockSet(id, false);
  renderHome(); showHome();                            // borra la pantalla de verificacion
  if(what == LSU_AFTER_OPENAPP && id >= 0) enterApp(id);
}
static void lsuStartVerifyFor(int what, int id){
  if(gLockType == 0) return;                           // sin clave no hay nada que verificar
  lsuStartVerify();                                    // deja lsuAfter en UNLOCK; se ajusta justo aqui
  lsuAfter = what; lsuAfterApp = id;
}

static int  utf8Count(const char* s){ int n = 0; while(*s){ if((*s & 0xC0) != 0x80) n++; s++; } return n; }
static void lsuPassAppend(const char* s){ int L = strlen(lsuPass), sl = strlen(s); if(L + sl < (int)sizeof(lsuPass) - 1){ memcpy(lsuPass + L, s, sl); lsuPass[L + sl] = 0; } }
static void lsuExit(){
  // FASES 3 y 4: cancelar una verificacion de app o de kiosco NO debe bloquear la
  // pantalla (que es lo que hacia la rama de abajo). Y sobre todo: cancelar la
  // salida del kiosco tiene que devolver A LA APP, nunca al escritorio -- si no,
  // la propia flecha de "atras" seria la via de escape del modo kiosco.
  // SEGURIDAD: si la verificacion salio de la pantalla de Bloqueo, cancelar
  // vuelve al BLOQUEO, nunca al escritorio. Va lo PRIMERO porque el despertar de
  // una suspension usa LSU_AFTER_OPENAPP (para volver a la app donde estabas), y
  // sin esta rama caeria por la de abajo, que termina en ST_HOME + showHome():
  // el escritorio a la vista sin haber introducido la clave.
  if(lsuVerify && gLockVerifyLocked){
    gLockVerifyLocked = false;
    lsuVerify = false; lsuAfter = LSU_AFTER_UNLOCK; lsuAfterApp = -1;
    lsuShakeMs = 0; lockWaitReset();
    gState = ST_LOCK; lockOff = 0; lastLockOff = -1;
    renderLock(); showLock();
    return;
  }
  if(lsuVerify && lsuAfter != LSU_AFTER_UNLOCK){
    bool wasKiosk = (lsuAfter == LSU_AFTER_KIOSKOUT);
    bool wasPoff  = (lsuAfter == LSU_AFTER_POWEROFF);
    bool wasReset = (lsuAfter == LSU_AFTER_FACTORY);
    lsuVerify = false; lsuAfter = LSU_AFTER_UNLOCK; lsuAfterApp = -1;
    lsuShakeMs = 0; lockWaitReset();
    // Cancelar la verificacion del restablecimiento vuelve a Ajustes SIN haber
    // tocado un solo dato: el asistente todavia no habia armado nada.
    if(wasReset){ frCancelToSettings(); return; }
    // Cancelar la verificacion del apagado NO apaga y NO se va al escritorio:
    // devuelve a la pantalla de confirmacion, con el slider otra vez en reposo.
    if(wasPoff && POWEROFF_ON){ poffEnter(); return; }
    if(wasKiosk && KIOSK_ON && kioskOn && kioskApp >= 0){
      renderHome();                       // la transicion compone sobre homeBuf
      enterApp(kioskApp);
      kioskShowBadge();
    } else {
      gState = ST_HOME; renderHome(); showHome();
    }
    return;
  }
  if(lsuVerify){ lsuVerify = false; gState = ST_LOCK; lockOff = 0; lastLockOff = -1; renderLock(); showLock(); }
  else { gState = ST_APP; settingsRender(); }
}
static void lsuUnlock(){
  lockOnSuccess();                       // FASE 1: acierto -> contador de fallos a cero
  lsuShakeMs = 0;
  gLockVerifyLocked = false;             // clave correcta: ya no estamos "detras del bloqueo"
  lsuVerify = false; lsuWrong = 0; lockOff = 0; lastLockOff = -1;
  // FASES 3 y 4: si la verificacion no era para desbloquear la PANTALLA, el
  // destino lo decide lsuFinishAfter (abrir app, poner/quitar candado, salir del
  // kiosco). La animacion de revelado del escritorio de abajo no aplica ahi.
  if(lsuAfter != LSU_AFTER_UNLOCK){ lsuFinishAfter(); return; }
  gState = ST_HOME;
  renderHome();                          // compone el home en homeBuf
  uint32_t t0 = millis(), dur = 400;     // 0.4s: aparecer desvanecido + leve temblor
  for(;;){
    uint32_t e = millis() - t0; if(e > dur) e = dur;
    float p = (float)e / dur;
    uint8_t a = (uint8_t)(p * 255);
    int sh = (int)((1.0f - p) * 6.0f * sinf(e * 0.05f));   // temblor que decae
    for(int j = 0; j < SCR_H; j++){
      uint16_t* d  = bbuf + (size_t)j * SCR_W;
      uint16_t* bg = (blurBg ? blurBg : homeBuf) + (size_t)j * SCR_W;
      uint16_t* hm = homeBuf + (size_t)j * SCR_W;
      for(int i = 0; i < SCR_W; i++){
        int si = i - sh; if(si < 0) si = 0; if(si >= SCR_W) si = SCR_W - 1;
        d[i] = mix565(bg[i], hm[si], a);
      }
    }
    present(0, SCR_H - 1);
    if(e >= dur) break;
  }
  showHome();
}
// GUARDAR LA CLAVE DEL SISTEMA. Antes esto escribia el PIN y la
// contrasena TAL CUAL en NVS ("lockpin"/"lockpass"), asi que cualquiera
// que volcara la particion los leia. Ahora se delega en flexLockSet(),
// que guarda sal aleatoria + PBKDF2-HMAC-SHA256 y borra de NVS los
// restos en texto legible. El resto del flujo (gLockType, lsuExit) no
// cambia.
//
// El buffer de la clave se BORRA despues de guardarla: ya no hace falta
// para nada y no tiene por que seguir en RAM.
static void lsuSavePin(){
  if(flexLockSet(lsuPin, 1)) gLockType = 1;
  flexVaultWipe(lsuPin, sizeof(lsuPin));
  lsuExit();
}
static void lsuSavePass(){
  if(flexLockSet(lsuPass, 2)) gLockType = 2;
  flexVaultWipe(lsuPass, sizeof(lsuPass));
  lsuExit();
}
static void lsuBack(){ uint16_t c = lsuTxtHi(); strokeSegAA(30, 26, 18, 18, 2.4f, c); strokeSegAA(18, 18, 30, 10, 2.4f, c); }

// ---- Selector PIN / Contraseña ----
static void lsuRenderSel(){
  setBuf(bbuf);
  fillRect(0, 0, SCR_W, SCR_H, lsuBgCol());
  lsuBack();
  drawTextC(SCR_W / 2, 74, "Bloqueo de pantalla", 3, lsuTxtHi());
  drawTextC(SCR_W / 2, 118, "Elige un metodo", 2, lsuTxtLo());
  int bw = SCR_W - 80, bh = 120, y1 = 220, y2 = y1 + bh + 30;
  const char* lbl[2] = { "PIN", "Contrase\xC3\xB1" "a" }; int ys[2] = { y1, y2 };
  for(int k = 0; k < 2; k++){
    if(uiGlass){ drawLiquidGlassPanel(40, ys[k], bw, bh, 22, mix565(TH_PRIM, TH_SURF, 60)); }
    else fillRoundRect(40, ys[k], bw, bh, 22, TH_PRIM);
    drawTextC(SCR_W / 2, ys[k] + bh / 2 - 18, lbl[k], 4, TH_ONACC);
  }
  present(0, SCR_H - 1);
}

// ---- Pantalla PIN (teclado numerico Liquid Glass + feedback de tecleo) ----
static void lsuPinRect(int i, int &x, int &y, int &w, int &h){
  int c = i % 3, r = i / 3, bw = 132, bh = 82, gap = 12;
  int tot = 3 * bw + 2 * gap, x0 = (SCR_W - tot) / 2, y0 = 300;
  x = x0 + c * (bw + gap); y = y0 + r * (bh + gap); w = bw; h = bh;
}
static void lsuComposePin(){                          // base de vidrio en lockBuf (SOLO una vez)
  setBuf(lockBuf);
  lsuBg();
  lsuBack();
  drawTextC(SCR_W / 2, 60, lsuVerify ? "Introduce el PIN" : "Crear PIN", lsuVerify ? 3 : 4, lsuTxtHi());
  for(int i = 0; i < 12; i++){
    int x, y, w, h; lsuPinRect(i, x, y, w, h);
    if(uiGlass){ drawLiquidGlassPanel(x, y, w, h, 16, lsuGlassCol()); }
    else fillRoundRect(x, y, w, h, 16, lsuCardCol());
    uint16_t col = (i == 9) ? TH_WARN : (i == 11) ? TH_OK : lsuTxtHi();   // '<' borrar, 'OK' confirmar
    drawTextC(x + w / 2, y + h / 2 - 12, PIN_KEYS[i], 3, col);
  }
  setBuf(bbuf);
}
// El primer cuadro del PIN entra con el fundido de la transicion de seguridad
// (si la hubo). authFadeIn devuelve false cuando no hay transicion pendiente
// -- p.ej. al CREAR el PIN desde Ajustes -- y entonces se publica de una vez,
// exactamente como antes.
static void lsuShowPin(){
  lsuComposePin();
  if(authFadeIn(lockBuf)) return;
  memcpy(bbuf, lockBuf, (size_t)SCR_W * SCR_H * 2); present(0, SCR_H - 1);
}
static void lsuAnimPin(){                              // puntos dinamicos + destello + flash (NO re-desenfoca)
  setBuf(bbuf);
  // FASE 1: sh != 0 solo durante los ~6 frames de la sacudida. La banda se
  // copia DESPLAZADA en horizontal, asi que puntos y teclado se mueven juntos
  // sin volver a dibujar ni un panel de vidrio; los bordes se rellenan
  // repitiendo la columna extrema (clamp), nunca con negro.
  int sh = lsuShakeOff();
  if(sh == 0){
    for(int j = 120; j < 700; j++) memcpy(bbuf + (size_t)j * SCR_W, lockBuf + (size_t)j * SCR_W, SCR_W * 2);
  } else {
    for(int j = 120; j < 700; j++){
      uint16_t* d = bbuf + (size_t)j * SCR_W;
      const uint16_t* s = lockBuf + (size_t)j * SCR_W;
      for(int i = 0; i < SCR_W; i++){
        int si = i - sh; if(si < 0) si = 0; if(si >= SCR_W) si = SCR_W - 1;
        d[i] = s[si];
      }
    }
  }
  int n = strlen(lsuPin);
  uint16_t dc = (lsuWrong && millis() - lsuWrong < 500) ? TH_ERR : TH_PRIM;
  for(int i = 0; i < 8; i++){ int cx = SCR_W / 2 - 4 * 28 + 14 + i * 28 + sh; if(i < n) fillCircle(cx, 150, 8, dc); else drawCircle(cx, 150, 8, lsuTxtLo()); }
  for(int i = 0; i < 12; i++){
    int x, y, w, h; lsuPinRect(i, x, y, w, h);
    if(i == lsuPress){ float p = (millis() - lsuPressMs) / 200.0f; if(p < 1) fillRoundRectA(x + sh, y, w, h, 16, lsuTxtHi(), (uint8_t)((1 - p) * 90)); else lsuPress = -1; }
  }
  present(120, 700);
}

// ---- Teclado alfanumerico para contraseña (con offset para el slide) ----
// xoff = sacudida horizontal de la FASE 1. El panel de fondo se dibuja SIN
// desplazar y solo las teclas se mueven dentro de el, asi que la sacudida nunca
// deja una franja vacia en los bordes de la pantalla.
static void lsuDrawKb(int yoff, int xoff){
  int ky = KB_Y + yoff;
  // Aqui NO hay barra superior ni chips (kbExtrasOn queda en false en esta
  // pantalla), asi que el panel empieza justo encima de las teclas, igual que
  // siempre. Lo unico que cambia con la Fase A es que el tamano ya no es fijo.
  if(uiGlass){ drawLiquidGlassPanel(0, ky - 4, SCR_W, SCR_H - (ky - 4), 0, lsuKbGlass()); }
  else fillRect(0, ky - 4, SCR_W, SCR_H - (ky - 4), lsuKbBgCol());
  int fs = kbFontSize();
  for(int r = 0; r < KB_ROWS; r++) for(int c = 0; c < KB_COLS; c++){
    int x = KB_X + c * (KB_KW + KB_GAP) + xoff, y = ky + r * (KB_KH + KB_GAP);
    char u[6];
    const char* k = kbResolveKey(mapaActivo[r][c], u, false);
    int cell = r * KB_COLS + c;
    kbPaintKey(x, y, KB_KW, KB_KH, k, fs, lsuKeyCol(), lsuKeyTxt(), kbCellHeld(cell) || kbFxLevel(cell) > 0);
  }
  int fy = ky + 3 * (KB_KH + KB_GAP);
  const char* lb[KB_FKEYS] = { "shift", kbLayerLabel(), kbLangEs ? "ES" : "EN", "espacio", "<-", "OK" };
  for(int i = 0; i < KB_FKEYS; i++) kbFKey(kbFKeyX(i) + xoff, fy, kbFKeyW(i), lb[i], (i == 0) && kbShift);
}
// Pintado de la pantalla de contrasena en el gBuf ACTUAL (sin publicar). Se
// separo del volcado para poder componerla fuera de pantalla y usarla como
// destino del fundido de entrada de la transicion de seguridad.
static void lsuPaintPass(int yoff, int xoff){
  lsuBg();
  lsuBack();
  drawTextC(SCR_W / 2, 50, lsuVerify ? "Introduce contrase\xC3\xB1" "a" : "Crear contrase\xC3\xB1" "a", 3, lsuTxtHi());
  int cnt = utf8Count(lsuPass);
  for(int i = 0; i < cnt && i < 18; i++) fillCircle(30 + i * 24 + xoff, 120, 7, TH_PRIM);
  lsuDrawKb(yoff, xoff);
}
static void lsuRenderPass(int yoff, int xoff){
  setBuf(bbuf);
  lsuPaintPass(yoff, xoff);
  present(0, SCR_H - 1);
}
// Primer cuadro de la contrasena: se compone con el teclado todavia FUERA de
// pantalla, se funde desde el fondo y a partir de ahi arranca el deslizamiento
// del teclado de siempre. Asi la aparicion es fundido + deslizamiento, no un
// salto seco.
static void lsuShowPassFirst(){
  if(authFadePending && authSnap){
    uint16_t* old = gBuf;
    gBuf = authSnap;                                  // lienzo fuera de pantalla (ya no hace falta la instantanea)
    lsuPaintPass(SCR_H - KB_Y, 0);
    gBuf = old;
    authFadeIn(authSnap);
  }
  lsuKbAnim = millis();
}

static void lsuEnter(){
  // FASE 4: en kiosco NO se puede cambiar la clave del sistema. Si la app clavada
  // fuera Ajustes, quien tenga el telefono entraria en Seguridad -> Bloqueo, se
  // pondria un PIN nuevo y saldria con el: la unica llave del kiosco es la clave
  // que ya estaba puesta antes de prestarlo.
  if(KIOSK_ON && kioskOn) return;
  gState = ST_LOCKSETUP; lsuMode = LSU_SEL; lsuPin[0] = 0; lsuPass[0] = 0; lsuPress = -1; lsuKbAnim = 0;
  mapaActivo = LAYOUT_ES; kbLangEs = true; kbShift = false;
  kbExtrasOn = false; kbBotReserve = 0; kbApplySize(); kbMtSurfaceReset();   // sin barra ni chips en la pantalla de clave
  lsuRenderSel();
}
static void lsuTick(){
  if(lsuMode == LSU_SEL){
    // El selector es ESTATICO (lo pinta lsuEnter): no hay nada que animar aqui.
    if(T.tap){
      if(T.x < 48 && T.y < 48){ lsuExit(); return; }
      int bw = SCR_W - 80, y1 = 220, bh = 120, y2 = y1 + bh + 30;
      if(T.x >= 40 && T.x <= 40 + bw && T.y >= y1 && T.y <= y1 + bh){ lsuMode = LSU_PIN; lsuShowPin(); return; }
      if(T.x >= 40 && T.x <= 40 + bw && T.y >= y2 && T.y <= y2 + bh){ lsuMode = LSU_PASS; lsuKbAnim = millis(); return; }
    }
    return;
  }
  if(lsuMode == LSU_PIN){
    // FASE 1 - la sacudida va PRIMERO: mientras dura (~6 frames) el teclado no
    // acepta pulsaciones, y solo cuando termina aparece el contador de espera.
    if(lsuShakeMs){
      if(millis() - lsuAnimMs > 30){ lsuAnimMs = millis(); lsuAnimPin(); }
      return;
    }
    // FASE 1 - espera forzada: el teclado esta inerte y solo se anima el
    // contador regresivo por diffing. Sin delay() bloqueante: esto es un
    // estado con marca de tiempo que se evalua en cada vuelta del loop.
    if(lockWaitActive()){
      // Se permite salir con la flecha: la espera NO se pierde al salir (sigue
      // viva en lockWaitUntil), asi que esto no es una via de escape -- solo
      // evita quedarse cinco minutos atrapado en una pantalla inerte.
      if(T.tap && T.x < 48 && T.y < 48){ lockWaitReset(); lsuExit(); return; }
      lockWaitTick();
      return;
    }
    // Se cumplio la espera: el contador se borra en el MISMO present() con el
    // que lsuAnimPin repinta su banda (120..700, que ya contiene esta zona),
    // no en un volcado aparte.
    if(lockWaitPainted){ lockWaitReset(); lsuAnimMs = 0; }
    if(T.tap){
      if(T.x < 48 && T.y < 48){ lsuExit(); return; }
      for(int i = 0; i < 12; i++){ int x, y, w, h; lsuPinRect(i, x, y, w, h);
        if(T.x >= x && T.x <= x + w && T.y >= y && T.y <= y + h){
          lsuPress = i; lsuPressMs = millis();
          if(i == 9){ int L = strlen(lsuPin); if(L > 0) lsuPin[L - 1] = 0; }                         // borrar
          else if(i == 11){                                                                          // OK
            if(lsuVerify){ if(flexLockVerify(lsuPin)) lsuUnlock(); else { lsuWrong = millis(); lsuPin[0] = 0; lockOnFail(); lsuShakeStart(); } return; }
            else if(strlen(lsuPin) >= 4){ lsuSavePin(); return; }
          }
          else if(strlen(lsuPin) < 8){                                                               // digito
            int L = strlen(lsuPin); lsuPin[L] = PIN_KEYS[i][0]; lsuPin[L + 1] = 0;
            if(lsuVerify && lsuSavedLen > 0 && (int)strlen(lsuPin) == lsuSavedLen){
              if(flexLockVerify(lsuPin)) lsuUnlock(); else { lsuWrong = millis(); lsuPin[0] = 0; lockOnFail(); lsuShakeStart(); }
              return;
            }
          }
          break;
        }
      }
    }
    if(millis() - lsuAnimMs > 30){ lsuAnimMs = millis(); lsuAnimPin(); }        // anim con throttle (responsivo)
    return;
  }
  // LSU_PASS
  if(lsuKbAnim){                                        // animacion de apertura del teclado = 0.3s EXACTOS
    float p = (millis() - lsuKbAnim) / 300.0f; if(p >= 1){ p = 1; lsuKbAnim = 0; }
    int kbh = SCR_H - KB_Y;
    lsuRenderPass((int)((1.0f - p) * kbh), 0);
    return;
  }
  // FASE 1 - misma secuencia que en el PIN: primero sacudir, luego esperar.
  // Aqui lo que se mueve son las TECLAS dentro de su panel (el campo de puntos
  // queda vacio al fallar, asi que sacudirlo no se veria).
  if(lsuShakeMs){
    if(millis() - lsuAnimMs > 30){ lsuAnimMs = millis(); lsuRenderPass(0, lsuShakeOff()); }
    return;
  }
  if(lockWaitActive()){
    if(T.tap && T.x < 48 && T.y < 48){ lockWaitReset(); lsuExit(); return; }
    lockWaitTick();
    return;
  }
  // Aqui el borrado sale gratis en el mismo volcado: lsuRenderPass recompone la
  // pantalla completa de una vez.
  if(lockWaitPainted){ lockWaitReset(); lsuRenderPass(0, 0); }
  // FASE G - apagar el destello de la ultima tecla al cumplir su tiempo.
  kbFxTick(lsuKeyCol(), lsuKeyTxt());
  // FASE B - via rapida tambien aqui: escribir la contrasena rapido no deberia
  // perder letras. Las teclas de FUNCION que confirman o borran (OK, <-) NO
  // entran por esta via: una confirmacion tiene que salir de un toque
  // deliberado, no de un roce mientras el dedo anterior se levanta.
  if(T.down && T.y >= KB_Y - 8) kbTypingMark();   // veto del gesto de suspension mientras se teclea
  if(KB_MULTITOUCH_ON && gKbFastType){
    int n = kbMtPoll();
    bool wrote = false;
    for(int e = 0; e < n; e++){
      int cell = kbEvCell[e];
      if(cell < 0) continue;
      char u[6];
      const char* k = kbResolveKey(mapaActivo[cell / KB_COLS][cell % KB_COLS], u, true);
      lsuPassAppend(k);
      kbFxStart(cell); wrote = true;
    }
    if(wrote) lsuRenderPass(0, 0);
  }
  // FASE G: mismo criterio que en Notas -- sin escritura rapida, el destello va
  // en el flanco de PRESIONAR, que si no la tecla no da ninguna senal.
  if(T.pressed && !(KB_MULTITOUCH_ON && gKbFastType)) kbFxPress(kbCellAt(T.x, T.y), lsuKeyCol(), lsuKeyTxt());
  if(T.tap){
    if(T.x < 48 && T.y < 48){ lsuExit(); return; }
    int fi = kbFRowHit(T.x, T.y);
    if(fi >= 0){
      if(fi == 0) kbShift = !kbShift;
      else if(fi == 1) mapaActivo = (mapaActivo == LAYOUT_NUM) ? LAYOUT_EMOJI : (mapaActivo == LAYOUT_EMOJI) ? (kbLangEs ? LAYOUT_ES : LAYOUT_EN) : LAYOUT_NUM;
      else if(fi == 2){ kbLangEs = !kbLangEs; if(mapaActivo == LAYOUT_ES || mapaActivo == LAYOUT_EN) mapaActivo = kbLangEs ? LAYOUT_ES : LAYOUT_EN; }
      else if(fi == 3) lsuPassAppend(" ");
      else if(fi == 4){ int L = strlen(lsuPass); if(L > 0){ int q = L - 1; while(q > 0 && (lsuPass[q] & 0xC0) == 0x80) q--; lsuPass[q] = 0; } }
      else { if(lsuVerify){ if(flexLockVerify(lsuPass)) lsuUnlock(); else { lsuWrong = millis(); lsuPass[0] = 0; lockOnFail(); lsuShakeStart(); lsuRenderPass(0, 0); } return; } else if(strlen(lsuPass) >= 4){ lsuSavePass(); return; } }
      lsuRenderPass(0, 0); return;
    }
    if(kbFastActive()) return;                          // ya la escribio la via rapida
    int cell = kbCellAt(T.x, T.y);
    if(cell >= 0){
      char u[6];
      const char* k = kbResolveKey(mapaActivo[cell / KB_COLS][cell % KB_COLS], u, true);
      lsuPassAppend(k);
      kbFxStart(cell);
      lsuRenderPass(0, 0);
    }
  }
}

static void lsuStartVerify(){
  // RED DE SEGURIDAD (misma que appClose/activarMultitarea): la UI de
  // verificacion SIEMPRE se compone en portrait y a pantalla completa. Sin esto,
  // llegar aqui desde una app landscape -Juegos es la unica con APP_LAND-
  // pintaba el teclado girado y
  // recortado; y como el remapeo del tactil tambien depende de gLand, los
  // digitos no caian donde se veian: no habia forma de escribir el PIN ni de
  // salir de esa pantalla. No se restaura al terminar A PROPOSITO: quien vuelva
  // a una app landscape lo hace mediante su funcion de entrada, que pone gLand=true
  // por su cuenta, en vez de heredarlo de la pantalla de verificacion.
  gLand = false;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  setBuf(fb);
  // FLEX VAULT: cualquier verificacion de la clave DEL SISTEMA (desbloquear la
  // pantalla, abrir una app con candado, salir del kiosco, apagado seguro)
  // cierra tambien la Carpeta segura. Son dos claves distintas a proposito, y
  // acertar la del sistema no puede dar acceso a la boveda.
  vaultLockFromSystem(FXV_LOCK_SCREEN);
  // AQUI YA NO SE LEE LA CLAVE. Antes se sacaba de NVS en texto legible
  // y se dejaba en lsuSaved durante toda la pantalla de verificacion --
  // o sea, la clave del usuario viva en RAM mientras la pedia. Ahora
  // solo se lee su LONGITUD (para autoconfirmar el PIN al completarlo);
  // la comprobacion la hace flexLockVerify() con el hash de NVS.
  lsuSavedLen = flexLockLen();
  lsuVerify = true; lsuWrong = 0; lsuPin[0] = 0; lsuPass[0] = 0; lsuPress = -1;
  mapaActivo = LAYOUT_ES; kbLangEs = true; kbShift = false;
  kbExtrasOn = false; kbBotReserve = 0; kbApplySize(); kbMtSurfaceReset();   // sin barra ni chips en la verificacion
  ensureBlurBg();
  gState = ST_LOCKSETUP;
  // Por defecto esta verificacion es la de la PANTALLA. lsuStartVerifyFor lo
  // reajusta despues de llamar aqui, asi que ninguna ruta puede heredar por
  // accidente el destino de una verificacion anterior.
  lsuAfter = LSU_AFTER_UNLOCK; lsuAfterApp = -1;
  lsuShakeMs = 0;
  lockWaitPainted = false; lockWaitLastSec = -1;
  lockArmPendingPenalty();               // FASE 1: contador ya alto -> se cobra antes del primer intento
  // TRANSICION: primero se va la interfaz actual, luego entra el metodo de
  // seguridad configurado. Va justo aqui, despues de dejar listo el estado y
  // ANTES de pintar el primer cuadro de la clave.
  authFadeOut();
  if(gLockType == 1){ lsuMode = LSU_PIN; lsuShowPin(); }
  else { lsuMode = LSU_PASS; lsuShowPassFirst(); }
}

// #############################################################
// ##  SUSPENSION + BLOQUEO
// ##  ------------------------------------------------------
// ##  Cuerpo de suspWakeLockScreen(), cuyo prototipo esta arriba
// ##  junto al detector de doble-tap. Va AQUI ABAJO porque necesita
// ##  gState, editMode, gRippleActive, renderLock y showLock, que
// ##  todavia no existen alla arriba.
// ##
// ##  La llama suspWake() con la pantalla TODAVIA a oscuras, antes
// ##  del DISPON y antes de que el backlight empiece a subir. Por
// ##  eso el contenido anterior nunca llega a verse: cuando hay luz,
// ##  lo que hay en el framebuffer ya es el bloqueo.
// #############################################################
static void suspWakeLockScreen(){
#if SUSPEND_ON && SUSPEND_LOCK_ON
  // FLEX VAULT: por si se llegara aqui sin haber pasado por suspEnter (por
  // ejemplo, un despertar tras un corte). Es idempotente, asi que repetirlo no
  // cuesta nada y cierra el hueco.
  vaultLockFromSystem(FXV_LOCK_SCREEN);
  // Sin PIN/contrasena configurada NO se bloquea nada: seria pedirle al usuario
  // que "desbloquee" con una clave que no existe. Se despierta donde estaba,
  // que es el comportamiento de siempre.
  if(gLockType == 0) return;
  // Ya estaba en el bloqueo (o metiendo la clave) al suspender: nada que hacer y
  // nada que restaurar.
  if(gState == ST_LOCK || gState == ST_LOCKSETUP){ gSuspRetState = -1; gSuspRetApp = -1; return; }
  // A donde volver cuando acierte la clave (lo consume lockStartVerify).
  gSuspRetState = gState;
  gSuspRetApp   = gAppId;
  // Dejar el sistema en un estado limpio antes de tapar con el bloqueo. NO se
  // llama a appClose(): cerrar la app aqui es justo lo que haria perder el sitio
  // -- la app se deja viva y se vuelve a ella con enterApp() tras desbloquear.
  if(editMode) edExit();
  gRippleActive = false;
  qsPanelY = 0;                     // la cortina no puede quedar a medio abrir bajo el bloqueo
  gLand = false;                    // el bloqueo SIEMPRE se compone en portrait (apps landscape)
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  gState = ST_LOCK; lockOff = 0; lastLockOff = -1;
  renderLock();
  showLock();
#endif
}

// #############################################################
// ##  APAGADO COMPLETO (deep sleep real)
// ##  ------------------------------------------------------
// ##  Ruta: Panel Rapido -> circulo "Apagar" -> ST_POWEROFF_CONFIRM
// ##  (slider "desliza para apagar" + Cancelar) -> [PIN opcional]
// ##  -> ST_POWEROFF_ANIM (fundido a negro + "Flex OS" + fundido del
// ##  texto) -> backlight a 0 -> DCS de bajo consumo -> deep sleep.
// ##
// ##  Todo interpolado por millis(), cero delay() bloqueante, cero
// ##  fillScreen() de pantalla completa como mecanismo de apagado, y
// ##  toda composicion pasa por bbuf + present().
// #############################################################

// ---- Geometria de la pantalla de confirmacion ---------------------------
#define POFF_TRACK_X   40                      // pista del slider
#define POFF_TRACK_Y   352
#define POFF_TRACK_W   (SCR_W - 80)            // 400
#define POFF_TRACK_H   96
#define POFF_TRACK_R   (POFF_TRACK_H / 2)
#define POFF_KNOB_PAD  6                       // margen del pomo dentro de la pista
#define POFF_KNOB_D    (POFF_TRACK_H - 2 * POFF_KNOB_PAD)   // 84
#define POFF_KNOB_R    (POFF_KNOB_D / 2)
#define POFF_RUN       (POFF_TRACK_W - 2 * POFF_KNOB_PAD - POFF_KNOB_D)   // recorrido util del pomo
#define POFF_DONE_PCT  92                      // % del recorrido que cuenta como "completado"
#define POFF_BAND_Y0   (POFF_TRACK_Y - 8)      // banda que se repinta cada frame
#define POFF_BAND_Y1   (POFF_TRACK_Y + POFF_TRACK_H + 8)
#define POFF_BAND_H    (POFF_BAND_Y1 - POFF_BAND_Y0 + 1)
#define POFF_CAN_X     (SCR_W / 2 - 110)       // boton Cancelar
#define POFF_CAN_Y     560
#define POFF_CAN_W     220
#define POFF_CAN_H     72
#define POFF_CAN_R     (POFF_CAN_H / 2)

// ---- Tiempos de la animacion de apagado (todo interpolado) --------------
#define POFF_FADE_MS   520                     // 1) fundido a negro de la pantalla de confirmacion
#define POFF_TIN_MS    260                     // 2) aparicion del texto "Flex OS"
#define POFF_HOLD_MS   700                     //    ... y cuanto se queda quieto
#define POFF_TOUT_MS   620                     // 3) disolucion del texto
#define POFF_TXT_Y     (SCR_H / 2 - 26)        //    linea base del texto centrado
#define POFF_TXT_SZ    5
// Banda del texto: lo UNICO que cambia en las fases 2 y 3. A size 5 la caja de
// linea mide FONT_LINEH * fontSc(5) = 51 * 8*5/51 = 40 px; se deja margen de
// sobra por arriba (ascendentes) y por abajo (descendentes) para que ningun
// glifo pueda quedar recortado por el clip.
#define POFF_TXT_BY0   (POFF_TXT_Y - 24)
#define POFF_TXT_BY1   (POFF_TXT_Y + 80)

// ---- Estado ------------------------------------------------------------
static uint16_t* poffBand   = NULL;   // cache de la banda ESTATICA del slider (pista vacia + vidrio)
static int   poffKnob   = 0;          // posicion actual del pomo (0..POFF_RUN), interpolada
static int   poffTarget = 0;          // objetivo del pomo
static bool  poffDrag   = false;      // arrastre en curso
static int   poffGrab   = 0;          // offset dedo-pomo al agarrar (evita el salto inicial)
static int   poffLastKnob = -1;       // ultimo pomo dibujado (para no repintar de balde)
static uint8_t poffPhase = 0;         // 0=fundido 1=texto in 2=hold 3=texto out 4=backlight 5=dormir
static uint32_t poffPhaseMs = 0;      // millis() del inicio de la fase actual

// La proteccion por PIN aplica SOLO aqui. Ver el comentario de gPoffPin: la
// SUSPENSION nunca la consulta.
static bool poffPinRequired(){
#if POWEROFF_PIN_ON
  return gPoffPin && gLockType > 0;
#else
  return false;
#endif
}

// ---- Render ------------------------------------------------------------
// Dibuja la parte ESTATICA (fondo de vidrio, titulo, pista vacia, Cancelar) en
// el buffer activo. Se llama una sola vez por entrada a la pantalla.
static void poffDrawStatic(){
  // Fondo: wallpaper borroso ya cacheado (el mismo que usa la verificacion de
  // PIN) + un velo oscuro para que el panel de vidrio tenga contra que destacar.
  if(blurBg) memcpy(gBuf, blurBg, (size_t)SCR_W * SCR_H * 2);
  else       fillRect(0, 0, SCR_W, SCR_H, TH_SCRIM);
  fillRectA(0, 0, SCR_W, SCR_H, TH_SCRIM, 150);

  // Panel Liquid Glass envolvente: se REUTILIZA drawLiquidGlassPanelEx tal cual
  // (con su sombra/blur variable/especular/refraccion segun las flags GLASS_*),
  // no se reinventa ningun sistema de vidrio nuevo.
  // El velo de arriba deja el fondo oscuro en las DOS apariencias (es wallpaper
  // desenfocado, contenido), asi que el panel y sus textos usan las superficies
  // "sobre wallpaper" del tema -- misma regla que la verificacion de PIN.
  uiWallSurface(28, 232, SCR_W - 56, 424, 44, TH_WALLPANEL, 9);

  drawTextC(SCR_W / 2, 268, "\xC2\xBF" "Apagar FlexOS?", 3, TH_ONWALL);
  drawTextC(SCR_W / 2, 306, "El sistema entrar\xC3\xA1 en reposo profundo", 1, TH_ONWALL2);

  // Pista del slider (vacia). El relleno y el pomo son dinamicos.
  fillRoundRectA(POFF_TRACK_X + 2, POFF_TRACK_Y + 4, POFF_TRACK_W, POFF_TRACK_H, POFF_TRACK_R, TH_SHADOW, 60);
  uiWallSurface(POFF_TRACK_X, POFF_TRACK_Y, POFF_TRACK_W, POFF_TRACK_H, POFF_TRACK_R, TH_WALLSURF, 7);
  drawRoundRect(POFF_TRACK_X, POFF_TRACK_Y, POFF_TRACK_W, POFF_TRACK_H, POFF_TRACK_R, TH_ONWALL2);

  // Boton Cancelar (vuelve al estado previo sin ningun efecto secundario).
  fillRoundRectA(POFF_CAN_X + 2, POFF_CAN_Y + 4, POFF_CAN_W, POFF_CAN_H, POFF_CAN_R, TH_SHADOW, 60);
  uiWallSurface(POFF_CAN_X, POFF_CAN_Y, POFF_CAN_W, POFF_CAN_H, POFF_CAN_R, TH_WALLSURF, 7);
  drawRoundRect(POFF_CAN_X, POFF_CAN_Y, POFF_CAN_W, POFF_CAN_H, POFF_CAN_R, TH_ONWALL2);
  drawTextC(SCR_W / 2, POFF_CAN_Y + POFF_CAN_H / 2 - 9, "Cancelar", 2, TH_ONWALL);
}
// Pinta pomo + relleno + rotulo sobre la banda ya restaurada del buffer activo.
static void poffDrawKnob(){
  int kx = POFF_TRACK_X + POFF_KNOB_PAD + poffKnob;          // esquina izq. del pomo
  int p  = POFF_RUN > 0 ? (poffKnob * 100 / POFF_RUN) : 0;   // % del recorrido

  // Estela: el trozo de pista ya recorrido se tine de rojo, cada vez mas solido.
  int fw = poffKnob + POFF_KNOB_D + 2 * POFF_KNOB_PAD;
  if(fw > POFF_TRACK_W) fw = POFF_TRACK_W;
  if(poffKnob > 0)
    fillRoundRectA(POFF_TRACK_X, POFF_TRACK_Y, fw, POFF_TRACK_H, POFF_TRACK_R,
                   TH_DANGER, (uint8_t)(40 + p * 130 / 100));      // apagar: accion destructiva

  // Rotulo "desliza para apagar": se desvanece conforme el pomo avanza (a los
  // 100% ya no estorba al pomo, que esta justo encima).
  int lblA = 235 - p * 2;
  if(lblA > 0)
    drawTextCA(POFF_TRACK_X + POFF_TRACK_W / 2 + 18, POFF_TRACK_Y + POFF_TRACK_H / 2 - 9,
               "desliza para apagar", 2, TH_ONWALL, (uint8_t)lblA);

  // Pomo blanco con el simbolo de apagado en rojo (referencia: iOS).
  fillCircleA(kx + POFF_KNOB_R + 1, POFF_TRACK_Y + POFF_TRACK_H / 2 + 2, POFF_KNOB_R, TH_SHADOW, 70);
  fillCircle(kx + POFF_KNOB_R, POFF_TRACK_Y + POFF_TRACK_H / 2, POFF_KNOB_R, TH_ONWALL);
  // Mismo glifo IEC 5009 que usa el Panel Rapido: una sola fuente de dibujo.
  qpIcoPower(kx + POFF_KNOB_R, POFF_TRACK_Y + POFF_TRACK_H / 2, 30, TH_DANGER);
}
// Un frame de la pantalla de confirmacion. SOLO se recompone la banda del
// slider: el resto (titulo, vidrio, Cancelar) es estatico y ya esta en fb desde
// poffEnter(), asi que no hay nada que repintar ni nada que pueda parpadear.
static void poffRenderBand(){
  // Sin la cache no se puede BORRAR el pomo anterior, asi que se prefiere no
  // animar antes que dejar un reguero de pomos pegados. La pantalla sigue
  // siendo usable: el boton Cancelar se atiende en poffTick, antes de llegar
  // aqui. (Solo pasaria si PSRAM se quedara sin los ~135 KB del buffer.)
  if(!poffBand) return;
  if(poffKnob == poffLastKnob) return;              // nada que hacer este frame
  poffLastKnob = poffKnob;
  memcpy(bbuf + (size_t)POFF_BAND_Y0 * SCR_W, poffBand, (size_t)POFF_BAND_H * SCR_W * 2);
  uint16_t* old = gBuf; setBuf(bbuf);
  int c0 = gClipY0, c1 = gClipY1;
  gClipY0 = POFF_BAND_Y0; gClipY1 = POFF_BAND_Y1;   // nada puede salirse de la banda
  poffDrawKnob();
  gClipY0 = c0; gClipY1 = c1;
  setBuf(old);
  present(POFF_BAND_Y0, POFF_BAND_Y1);
}

static void poffEnter(){
  // FLEX VAULT: apagar cierra la boveda antes de cualquier animacion, para que
  // las claves no sigan en RAM mientras se compone el apagado.
  vaultLockFromSystem(FXV_LOCK_SCREEN);
#if !POWEROFF_ON
  return;
#else
  // Misma red de seguridad que lsuStartVerify: esta pantalla SIEMPRE se compone
  // en portrait y a pantalla completa, venga de donde venga (Modo PC o una app
  // landscape dejan gLand=true y el tactil remapeado).
  gLand = false;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  ensureBlurBg();
  poffKnob = poffTarget = 0; poffDrag = false; poffGrab = 0; poffLastKnob = -1;

  setBuf(bbuf);
  poffDrawStatic();
  // Cache de la banda del slider TAL CUAL queda de fabrica (pista vacia). Se
  // reserva UNA sola vez en toda la sesion, fuera del loop de render, en PSRAM
  // -- igual que qsBuf. ~135 KB (480 x 112 x 2), no un framebuffer entero.
  if(!poffBand) poffBand = (uint16_t*)heap_caps_malloc((size_t)POFF_BAND_H * SCR_W * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(poffBand) memcpy(poffBand, bbuf + (size_t)POFF_BAND_Y0 * SCR_W, (size_t)POFF_BAND_H * SCR_W * 2);
  poffDrawKnob();                       // pomo en reposo, dentro del mismo frame
  setBuf(fb);
  present(0, SCR_H - 1);                // un unico volcado atomico: cero parpadeo
  poffLastKnob = 0;
  gState = ST_POWEROFF_CONFIRM;
#endif
}

// Arrastre del slider. Mismo enfoque que el drag de la cortina de Ajustes
// rapidos (qsHandle): mientras el dedo esta abajo el pomo SIGUE al dedo 1:1, y
// al soltar sin completar el recorrido vuelve a 0 interpolado.
static void poffTick(){
#if POWEROFF_ON
  int kx = POFF_TRACK_X + POFF_KNOB_PAD + poffKnob;
  if(T.pressed && !poffDrag){
    // Agarre generoso: todo el alto de la pista, y en X desde el pomo hacia la
    // izquierda un poco, para que empezar el gesto no exija precision.
    if(T.y >= POFF_TRACK_Y && T.y <= POFF_TRACK_Y + POFF_TRACK_H &&
       T.x >= kx - 24 && T.x <= kx + POFF_KNOB_D + 24){
      poffDrag = true; poffGrab = T.x - kx;
    }
  }
  if(poffDrag){
    if(T.down){
      int v = T.x - poffGrab - (POFF_TRACK_X + POFF_KNOB_PAD);
      if(v < 0) v = 0; if(v > POFF_RUN) v = POFF_RUN;
      poffTarget = poffKnob = v;                       // 1:1 con el dedo
    } else {
      poffDrag = false;
      if(POFF_RUN > 0 && poffKnob * 100 / POFF_RUN >= POFF_DONE_PCT){
        poffKnob = poffTarget = POFF_RUN;              // completado
        poffRenderBand();
        // PIN opcional ANTES de proceder. Si falla o se cancela, se vuelve aqui
        // (ver lsuExit) y no se apaga nada.
        if(poffPinRequired()){ lsuStartVerifyFor(LSU_AFTER_POWEROFF, -1); return; }
        poffBeginAnim(); return;
      }
      poffTarget = 0;                                  // no llego: vuelve solo
    }
  } else if(T.tap && T.x >= POFF_CAN_X && T.x <= POFF_CAN_X + POFF_CAN_W &&
                     T.y >= POFF_CAN_Y && T.y <= POFF_CAN_Y + POFF_CAN_H){
    // Cancelar: sin ningun efecto secundario. Se vuelve al escritorio, que es de
    // donde se llega siempre (el icono vive en el Panel Rapido, que solo se abre
    // desde ST_HOME).
    gState = ST_HOME; renderHome(); showHome(); return;
  }
  // Interpolacion de vuelta (mismo estilo que el resto de resortes del sistema:
  // acercamiento proporcional + enganche final para que termine de verdad).
  if(!poffDrag && poffKnob != poffTarget){
    int d = poffTarget - poffKnob;
    poffKnob += (d > 0 ? (d + 3) / 4 : (d - 3) / 4);
    if(abs(poffTarget - poffKnob) < 3) poffKnob = poffTarget;
  }
  poffRenderBand();
#endif
}

// ---- Deep sleep --------------------------------------------------------
// Arma la fuente de despertar y entra en deep sleep. NO retorna nunca.
//
// FUENTE DE DESPERTAR -- ver el bloque POFF_WAKE_GPIO de arriba del archivo:
//   · Ruta buena (INT del GT911 cableado y en GPIO 0..15): ext1. Es la unica
//     forma soportada en el ESP32-P4 de despertar por un pin -- ext0 NO existe
//     en este chip (no hay SOC_PM_SUPPORT_EXT0_WAKEUP en su soc_caps.h).
//   · Ruta alternativa (pin no configurado): temporizador. El chip despierta
//     cada POFF_WAKE_POLL_MS, mira el tactil y se vuelve a dormir si no hay
//     dedo. Funciona en cualquier placa sin saber ningun pin, pero consume
//     bastante mas que el deep sleep de verdad porque el chip arranca a cada
//     rato. Es un modo DEGRADADO, no el objetivo.
static void poffEnterDeepSleep(){
  // El GT911 tiene que seguir escaneando mientras el P4 duerme, o su INT no
  // llegaria nunca. Su linea de reset la maneja PIN_TP_RST (GPIO 3), que esta
  // dentro del rango LP/RTC del P4 (0..15) y por tanto admite hold: se congela
  // en alto para que el tactil no se quede reseteado durante el sueno.
  //
  // OJO -- diferencia real del ESP32-P4 frente al ESP32 clasico: aqui NO se
  // llama a gpio_deep_sleep_hold_en(). Esa funcion NO EXISTE en este chip: en
  // soc_caps.h del P4 esta SOC_GPIO_SUPPORT_HOLD_SINGLE_IO_IN_DSLP=1, y
  // driver/gpio.h declara gpio_deep_sleep_hold_en/dis dentro de un
  // "#if !SOC_GPIO_SUPPORT_HOLD_SINGLE_IO_IN_DSLP" -> compilar una llamada a
  // ellas en el P4 es un error de compilacion. En este chip gpio_hold_en() por
  // si sola ya mantiene el pin durante el deep sleep, que es justo lo que dice
  // esa capability. (Espressif la marca como valida en silicio P4 rev >= 3.0.)
  pinMode(PIN_TP_RST, OUTPUT);
  digitalWrite(PIN_TP_RST, HIGH);
  gpio_hold_en((gpio_num_t)PIN_TP_RST);

#if (POFF_WAKE_GPIO >= 0) && (POFF_WAKE_GPIO <= 15)
  esp_sleep_enable_ext1_wakeup_io(1ULL << POFF_WAKE_GPIO,
                                  POFF_WAKE_LEVEL ? ESP_EXT1_WAKEUP_ANY_HIGH : ESP_EXT1_WAKEUP_ANY_LOW);
#else
  esp_sleep_enable_timer_wakeup((uint64_t)POFF_WAKE_POLL_MS * 1000ULL);
#endif
  Serial.println(F("[PWR] deep sleep"));
  Serial.flush();
  esp_deep_sleep_start();               // no retorna
}
// Marca en NVS que este apagado fue LIMPIO (lo pidio el usuario), para que
// setup() pueda distinguir un arranque normal de un despertar de deep sleep sin
// depender solo de esp_reset_reason().
static void poffSaveCleanFlag(){
  prefs.begin("flexos", false);
  prefs.putBool("cleanoff", true);
  prefs.putInt("bright", gBright);      // el brillo del usuario, para restaurarlo al encender
  prefs.end();
  // La hora tambien: es el momento MAS tardio en que se conoce, asi que al
  // volver a encender (quiza sin internet) el reloj arranca donde se quedo.
  clkSaveNvs();
}

// ---- Animacion de apagado ----------------------------------------------
static void poffBeginAnim(){
#if POWEROFF_ON
  gLand = false;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  // Instantanea del frame actual para el fundido a negro. Se usa lockBuf (la
  // cache de la pantalla de bloqueo) en vez de reservar 750 KB mas: renderLock()
  // la regenera entera cada vez que hace falta, y desde aqui ya no se vuelve a
  // ninguna pantalla -- el destino es el deep sleep.
  if(lockBuf) memcpy(lockBuf, fb, (size_t)SCR_W * SCR_H * 2);
  poffPhase = 0; poffPhaseMs = millis();
  gState = ST_POWEROFF_ANIM;
#endif
}
static void poffAnimTick(){
#if POWEROFF_ON
  uint32_t e = millis() - poffPhaseMs;
  switch(poffPhase){
    case 0: {           // 1) fundido a negro (overlay compuesto en bbuf, NUNCA fillScreen)
      if(e > POFF_FADE_MS) e = POFF_FADE_MS;
      uint8_t a = (uint8_t)(e * 255 / POFF_FADE_MS);
      const uint16_t* src = lockBuf ? lockBuf : fb;
      for(int j = 0; j < SCR_H; j++){
        const uint16_t* s = src  + (size_t)j * SCR_W;
        uint16_t*       d = bbuf + (size_t)j * SCR_W;
        for(int i = 0; i < SCR_W; i++) d[i] = mix565(s[i], 0, a);   // 0 = negro en RGB565
      }
      present(0, SCR_H - 1);
      if(e >= POFF_FADE_MS){ poffPhase = 1; poffPhaseMs = millis(); }
      break;
    }
    case 1:             // 2) el texto "Flex OS" aparece sobre el negro
    case 2:             //    ... se queda quieto
    case 3: {           // 3) ... y se disuelve (alpha decreciente)
      uint8_t a = 255;
      if(poffPhase == 1){ if(e > POFF_TIN_MS) e = POFF_TIN_MS; a = (uint8_t)(e * 255 / POFF_TIN_MS); }
      else if(poffPhase == 3){ if(e > POFF_TOUT_MS) e = POFF_TOUT_MS; a = (uint8_t)(255 - e * 255 / POFF_TOUT_MS); }
      // Solo se recompone la banda del texto: el resto de la pantalla ya es
      // negro solido en fb desde la fase 0 y no cambia.
      for(int j = POFF_TXT_BY0; j <= POFF_TXT_BY1; j++)
        memset(bbuf + (size_t)j * SCR_W, 0, SCR_W * 2);
      uint16_t* old = gBuf; setBuf(bbuf);
      int c0 = gClipY0, c1 = gClipY1;
      gClipY0 = POFF_TXT_BY0; gClipY1 = POFF_TXT_BY1;
      if(a) drawTextCA(SCR_W / 2, POFF_TXT_Y, "Flex OS", POFF_TXT_SZ, TH_ONWALL, a);   // sobre el fundido a negro del apagado
      gClipY0 = c0; gClipY1 = c1;
      setBuf(old);
      present(POFF_TXT_BY0, POFF_TXT_BY1);
      uint32_t dur = poffPhase == 1 ? POFF_TIN_MS : poffPhase == 2 ? POFF_HOLD_MS : POFF_TOUT_MS;
      if(millis() - poffPhaseMs >= dur){ poffPhase++; poffPhaseMs = millis(); }
      break;
    }
    case 4:             // 4) fundido del backlight a 0 (mismo mecanismo que la suspension)
      if(gSuspFade < 0 && !gSuspOn){ gSuspOn = true; gSuspDark = false; gSuspBright = gBright; suspFadeTo(0); }
      if(gSuspDark){ poffPhase = 5; poffPhaseMs = millis(); }
      break;
    case 5:             // 5) DCS de bajo consumo -> NVS -> deep sleep
      panelSleepIn();
      poffSaveCleanFlag();
      poffEnterDeepSleep();   // no retorna
      break;
  }
#endif
}

// -------------------------------------------------------------
//  ALIMENTACION DEFENSIVA DEL TASK WATCHDOG
//  -------------------------------------------------------------
//  esp_task_wdt_reset() solo es valido si la tarea que llama esta
//  SUSCRITA al TWDT. Si no lo esta devuelve ESP_ERR_NOT_FOUND y el
//  componente task_wdt imprime un ERROR por CADA llamada:
//
//      E (17336) task_wdt: esp_task_wdt_reset(705): task not found
//
//  Desde loop(), que da unas 200 vueltas por segundo, eso satura el
//  puerto serie y deja el log inservible para diagnosticar nada mas.
//
//  Por que se puede perder la suscripcion: levantar la pila WiFi (en el
//  P4, el enlace esp-hosted/SDIO con el C6) reinicializa el TWDT y con
//  el se va la lista de suscriptores. Antes no se notaba porque la
//  radio no se encendia nunca sola; en cuanto el arranque empezo a
//  levantarla, el bucle aparecio en cada boot.
//
//  Diseño: en el caso normal esto es UNA sola llamada por vuelta, igual
//  de barato que antes. En cuanto falla UNA vez se deja de llamar y se
//  pasa a reintentar la suscripcion cada 5 s -- asi el log ve una linea
//  en la transicion y como mucho una cada 5 s, nunca un bucle. Y si el
//  TWDT vuelve a estar disponible, la vigilancia se recupera sola: no
//  se abandona nunca de forma permanente.
// -------------------------------------------------------------
static void flexFeedWdt(){
  static bool     subscribed = true;      // Arduino suscribe el loopTask al arrancar
  static uint32_t nextTry    = 0;
  if(subscribed){
    if(esp_task_wdt_reset() == ESP_OK) return;          // caso normal
    subscribed = false;                                 // se perdio la suscripcion
    nextTry    = millis() + 5000;
    Serial.println(F("[WDT] loopTask ya no esta suscrito al Task Watchdog; reintentando cada 5 s"));
    return;
  }
  uint32_t now = millis();
  if(now < nextTry) return;                             // sin insistir: eso es lo que inundaba
  nextTry = now + 5000;
  if(esp_task_wdt_add(NULL) == ESP_OK){
    subscribed = true;
    Serial.println(F("[WDT] loopTask resuscrito al Task Watchdog"));
    esp_task_wdt_reset();
  }
}

// ---- Filtro de arranque: 3 segundos de presion sostenida ----------------
// POR QUE ASI (es el punto mas delicado de todo el apagado):
// En deep sleep el loop() no existe, asi que NO hay forma de cronometrar los 3
// segundos con la logica habitual de millis() -- el chip esta apagado. El pin
// de despertar tampoco sirve para medir duracion: el INT del GT911 es un PULSO
// por reporte, no un nivel que se mantenga mientras el dedo esta apoyado, asi
// que ext1 solo puede decir "alguien ha tocado", nunca "lleva 3 s tocando".
// Solucion (la que sugeria la propia tarea, y la unica soportada de verdad):
//   despertar al PRIMER contacto -> verificar la duracion YA DESPIERTO, por I2C,
//   en los primeros instantes de setup(), con el panel y el backlight todavia
//   apagados -> si no se sostiene, volver a dormir sin llegar a arrancar.
// El usuario nunca ve un destello: nada de esto ocurre despues de flexPanelInit.
static void poffWakeGate(){
#if POWEROFF_ON
  if(esp_reset_reason() != ESP_RST_DEEPSLEEP) return;    // no venimos de deep sleep
  // Liberar el hold del reset del GT911. La API pide dejar el pin ya
  // configurado en el mismo nivel ANTES de soltar el hold, o el pad daria un
  // glitch al pasar a su estado por defecto (y eso resetearia el tactil justo
  // cuando lo necesitamos). gpio_deep_sleep_hold_dis() NO se llama: no existe
  // en el P4 (ver poffEnterDeepSleep).
  pinMode(PIN_TP_RST, OUTPUT);
  digitalWrite(PIN_TP_RST, HIGH);
  gpio_hold_dis((gpio_num_t)PIN_TP_RST);
  flexTouchInit();                                       // I2C + GT911 (el panel sigue apagado)
  if(!gtOk) return;                                      // sin tactil no hay forma de confirmar: se arranca
  uint32_t t0 = millis(), heldFrom = 0;
  while(millis() - t0 < POFF_WAKE_GATE_MS){
    flexFeedWdt();                                       // el TWDT se alimenta igual que en loop() (defensivo)
    uint16_t gx = 0, gy = 0;
    gtPoll(gx, gy);
    int n = (millis() - gtFingersMs > 120) ? 0 : (int)gtFingers;
    if(n >= 1){
      if(!heldFrom) heldFrom = millis();
      if(millis() - heldFrom >= POFF_WAKE_HOLD_MS) return;   // 3 s sostenidos -> arranque completo
    } else heldFrom = 0;
    delay(10);
  }
  poffEnterDeepSleep();                                  // no se sostuvo: de vuelta a dormir
#endif
}
