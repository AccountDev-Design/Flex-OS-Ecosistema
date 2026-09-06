// #############################################################
// ##  FLEX OS ULTRA  ·  BLOQUEO DE SEGURIDAD Y MODO KIOSCO
// ##  ----------------------------------------------------------
// ##  PIN y contrasena (verificados contra el hash con sal de FlexOS_Vault),
// ##  la transicion previa en dos tiempos, el bloqueo global reforzado y el
// ##  modo kiosco de prestamo seguro.
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
#include "FlexOS_Ultra_AppSwitcher.h"   // eslabon anterior de la cadena

// #############################################################
// ##  SEGURIDAD -> BLOQUEO (PIN / Contraseña)
// ##  Todo compone en bbuf y presenta de una vez (anti-flicker).
// #############################################################
enum { LSU_SEL = 0, LSU_PIN, LSU_PASS };
static int lsuMode = LSU_SEL;
static char lsuPin[12] = "", lsuPass[64] = "";
static int lsuPress = -1; static uint32_t lsuPressMs = 0;
static uint32_t lsuKbAnim = 0;                       // millis al abrir el teclado (para el slide de 0.3s)
static uint32_t lsuAnimMs = 0;
static const char* PIN_KEYS[12] = { "1","2","3","4","5","6","7","8","9","<","0","OK" };
static bool lsuVerify = false;                       // true = desbloquear (verificar), false = crear
// LA CLAVE YA NO SE COPIA A RAM. Antes aqui vivia una copia en claro
// del PIN/contrasena guardado (lsuSaved) para compararla con strcmp.
// Ahora la comprobacion la hace flexLockVerify(), que deriva el hash de
// lo que escribe el usuario y lo compara en tiempo constante contra el
// hash de NVS: en ningun momento existe la clave del usuario en una
// variable del sketch. De la clave guardada solo se lee su LONGITUD,
// para poder autoconfirmar el PIN al completar sus digitos (lo mismo que
// ya revelan los puntos de la pantalla).
static int  lsuSavedLen = 0;
static uint32_t lsuWrong = 0;                        // millis del ultimo error (para el flash rojo)
// ---- Tema de las pantallas de clave (PIN / contrasena) ----
// Las dos ramas piden color POR SIGNIFICADO al tema global; lo que cambia es
// SOBRE QUE se dibuja, que es lo que decide el par de colores correcto:
//   · CREAR la clave (desde Ajustes): el fondo es el color SOLIDO de pagina, asi
//     que todo sale de la paleta activa -- en tema claro la pantalla es clara.
//   · VERIFICAR: el fondo es el WALLPAPER DESENFOCADO, o sea contenido del
//     usuario que el tema no retine (ver TH_ONWALL en el bloque TEMA SEMANTICO).
//     Ahi se usan las superficies "sobre wallpaper", identicas en las dos
//     apariencias: con la paleta clara habria tarjetas casi blancas y texto casi
//     negro flotando sobre una foto oscura, ilegibles.
// El MATERIAL (uiGlass) es ortogonal y se respeta en los dos casos.
static uint16_t lsuBgCol()   { return lsuVerify ? TH_SCRIM      : PAGE_BG; }
static uint16_t lsuCardCol() { return lsuVerify ? TH_WALLSURF   : SET_CARD_BG; }
static uint16_t lsuGlassCol(){ return lsuVerify ? TH_WALLSURF2  : SET_CARD_GLASS; }
static uint16_t lsuKbBgCol() { return lsuVerify ? TH_WALLPANEL  : PAGE_BG; }
static uint16_t lsuKbGlass() { return lsuVerify ? TH_WALLPANEL  : SET_CARD_GLASS; }
static uint16_t lsuKeyCol()  { return lsuVerify ? TH_WALLSURF   : SET_CARD_BG; }
static uint16_t lsuTxtHi()   { return lsuVerify ? TH_ONWALL     : SET_TXT_HI; }
static uint16_t lsuTxtLo()   { return lsuVerify ? TH_ONWALL2    : SET_TXT_LO; }
static uint16_t lsuKeyTxt()  { return lsuVerify ? TH_ONWALL     : SET_TXT_HI; }

static void lsuBg(){
  if(lsuVerify && blurBg) memcpy(gBuf, blurBg, (size_t)SCR_W * SCR_H * 2);   // wallpaper borroso
  else fillRect(0, 0, SCR_W, SCR_H, lsuBgCol());
}

// #############################################################
// ##  TRANSICION PREVIA AL BLOQUEO DE SEGURIDAD
// ##  ------------------------------------------------------
// ##  Antes, pedir la clave era un CORTE SECO: el cuadro que hubiera en
// ##  pantalla (el escritorio, la app que se estaba bloqueando, la pantalla de
// ##  Bloqueo) se sustituia de golpe por el teclado de la clave.
// ##  Ahora son dos tiempos, como en One UI o iOS:
// ##    1) la interfaz actual se DESVANECE hacia el fondo de la pantalla de
// ##       autenticacion (fade out);
// ##    2) el metodo de seguridad que el usuario tenga configurado -- PIN,
// ##       contrasena o el que se anada despues -- APARECE encima (fade in).
// ##  Vive en el camino comun (lsuStartVerify + las dos rutinas que pintan el
// ##  primer cuadro de cada metodo), asi que TODAS las rutas que piden clave la
// ##  heredan: bloquear/desbloquear una app, salir del kiosco, apagado seguro y
// ##  el desbloqueo de la pantalla.
// ##  Los dos fundidos son por TIEMPO (no por numero de pasos): duran lo mismo
// ##  vaya el sistema rapido o lento, y meten tantos cuadros como pueda dar el
// ##  compositor. Cada cuadro se publica entero con present(), o sea que no
// ##  puede aparecer a medias ni partido.
// #############################################################
#define AUTH_FADE_OUT_MS 190
#define AUTH_FADE_IN_MS  230
static uint16_t* authSnap = NULL;        // instantanea de la interfaz que se va
static bool authFadePending = false;     // el fade out ya corrio: toca el fade in

// Fundido de salida hacia el fondo de la pantalla de clave. Deja preparado el
// fade in. Si algo no esta disponible (PSRAM, landscape, app hospedada) no se
// anima y la verificacion sigue igual que siempre: la transicion es un adorno,
// nunca un requisito para poder introducir la clave.
static void authFadeOut(){
  authFadePending = false;
  if(gHosted || gLand) return;
  ensureBlurBg();
  if(!blurBg) return;
  if(!authSnap) authSnap = (uint16_t*)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!authSnap) return;
  memcpy(authSnap, fb, (size_t)SCR_W * SCR_H * 2);
  uint32_t t0 = millis();
  int last = -1;
  for(;;){
    uint32_t e = millis() - t0; if(e > (uint32_t)AUTH_FADE_OUT_MS) e = AUTH_FADE_OUT_MS;
    float p = (float)e / (float)AUTH_FADE_OUT_MS;
    p = p * p * (3.0f - 2.0f * p);                       // suavizado en las dos puntas
    uint8_t a = (uint8_t)(p * 255.0f);
    if((int)a == last){                                  // el reloj aun no ha movido el fundido
      if(e < (uint32_t)AUTH_FADE_OUT_MS){ delay(1); continue; }
      break;
    }
    last = a;
    for(int j = 0; j < SCR_H; j++){
      uint16_t* d = bbuf + (size_t)j * SCR_W;
      const uint16_t* s0 = authSnap + (size_t)j * SCR_W;
      const uint16_t* s1 = blurBg   + (size_t)j * SCR_W;
      for(int i = 0; i < SCR_W; i++) d[i] = mix565(s0[i], s1[i], a);
    }
    present(0, SCR_H - 1);
    if(e >= (uint32_t)AUTH_FADE_OUT_MS) break;
  }
  authFadePending = true;
}
// Fundido de entrada del metodo de seguridad ya compuesto en 'target'.
// Devuelve false si no habia transicion en curso, para que el llamante publique
// su cuadro como siempre.
static bool authFadeIn(const uint16_t* target){
  if(!authFadePending) return false;
  authFadePending = false;
  if(!target || !blurBg) return false;
  uint32_t t0 = millis();
  int last = -1;
  for(;;){
    uint32_t e = millis() - t0; if(e > (uint32_t)AUTH_FADE_IN_MS) e = AUTH_FADE_IN_MS;
    float p = (float)e / (float)AUTH_FADE_IN_MS;
    p = p * p * (3.0f - 2.0f * p);
    uint8_t a = (uint8_t)(p * 255.0f);
    if((int)a == last){
      if(e < (uint32_t)AUTH_FADE_IN_MS){ delay(1); continue; }
      break;
    }
    last = a;
    for(int j = 0; j < SCR_H; j++){
      uint16_t* d = bbuf + (size_t)j * SCR_W;
      const uint16_t* s0 = blurBg + (size_t)j * SCR_W;
      const uint16_t* s1 = target + (size_t)j * SCR_W;
      for(int i = 0; i < SCR_W; i++) d[i] = mix565(s0[i], s1[i], a);
    }
    present(0, SCR_H - 1);
    if(e >= (uint32_t)AUTH_FADE_IN_MS) break;
  }
  memcpy(bbuf, target, (size_t)SCR_W * SCR_H * 2);       // cuadro final exacto (sin redondeos del fundido)
  present(0, SCR_H - 1);
  return true;
}

// #############################################################
// ##  FASE 1 - BLOQUEO GLOBAL REFORZADO
// ##  (a) sacudida amortiguada al fallar   LOCK_SHAKE_ON
// ##  (b) contador persistente + espera progresiva  LOCK_FAILS_ON
// ##  (c) auto-bloqueo por inactividad     AUTOLOCK_ON
// ##  Va aqui, entre lsuBg() y el resto del LSU, porque necesita conocer
// ##  lsuMode/lsuVerify (declarados justo arriba) y lo usan lsuTick /
// ##  lsuUnlock / lsuStartVerify (justo abajo).
// #############################################################

// ---- (a) Sacudida horizontal amortiguada -------------------------------
// Un fallo NUNCA se comunica con un parpadeo ni con un cambio brusco de color:
// se comunica moviendo. El offset es una senoidal de 3 ciclos cuya amplitud
// decae linealmente hasta 0, evaluada a ~30 ms por frame -> unos 6 frames.
// Al expirar devuelve exactamente 0, asi que el ultimo frame de la animacion
// deja la pantalla en su sitio sin ningun salto.
#define LSU_SHAKE_MS   180
#define LSU_SHAKE_AMP  12.0f
#define LSU_SHAKE_CYC  3.0f
static uint32_t lsuShakeMs = 0;                  // millis de inicio (0 = quieto)
static void lsuShakeStart(){
  if(!LOCK_SHAKE_ON) return;
  lsuShakeMs = millis();
  if(!lsuShakeMs) lsuShakeMs = 1;                // 0 es el centinela de "quieto"
}
static int lsuShakeOff(){
  if(!LOCK_SHAKE_ON || !lsuShakeMs) return 0;
  uint32_t e = millis() - lsuShakeMs;
  if(e >= (uint32_t)LSU_SHAKE_MS){ lsuShakeMs = 0; return 0; }
  float p = (float)e / (float)LSU_SHAKE_MS;                       // 0..1
  return (int)(LSU_SHAKE_AMP * (1.0f - p) * sinf(p * LSU_SHAKE_CYC * 6.2831853f));
}

// ---- (b) Espera progresiva tras intentos fallidos ----------------------
// Geometria de la zona del contador. Cae en el hueco que ya existe entre los
// puntos del PIN (y=150) y la primera fila del teclado numerico (y=300), asi
// que no tapa nada y sirve igual en la pantalla de contrasena.
#define LW_BAND_Y0  180
#define LW_BAND_Y1  276
#define LW_MSG_Y    186
#define LW_MSG2_Y   206
#define LW_NUM_Y0   226                      // banda que se repinta por diffing
#define LW_NUM_Y1   274
#define LW_NUM_Y    232
static uint32_t lockWaitUntil    = 0;        // millis en que expira la espera (0 = ninguna)
static bool     lockPenaltyServed = false;   // ya se cobro la espera del contador actual
static bool     lockWaitPainted  = false;    // el mensaje fijo ya esta en pantalla
static int      lockWaitLastSec  = -1;       // ultimo valor pintado (diffing)

static uint32_t lockPenaltyMs(){
  if(lockFails >= LOCK_FAILS_HARD) return LOCK_WAIT_HARD_MS;   // 6+  -> 5 min
  if(lockFails >= LOCK_FAILS_SOFT) return LOCK_WAIT_SOFT_MS;   // 4-5 -> 30 s
  return 0;                                                    // 1-3 -> sin espera
}
// La resta se hace en int32 con signo a proposito: asi sigue siendo correcta
// cuando millis() da la vuelta a los ~49 dias.
static bool lockWaitActive(){
  if(!LOCK_FAILS_ON || !lockWaitUntil) return false;
  if((int32_t)(millis() - lockWaitUntil) >= 0){
    lockWaitUntil = 0; lockPenaltyServed = true;               // cumplida
    return false;
  }
  return true;
}
// Restaura la banda [y0,y1] en bbuf desde la capa que corresponda: la base
// estatica del teclado numerico (lockBuf, que ya la tiene compuesta) o el
// wallpaper borroso de la pantalla de contrasena. NUNCA recompone la pantalla
// entera: solo estas filas.
static void lockWaitBase(int y0, int y1){
  setBuf(bbuf);
  // Recorte completo: si veniamos de una lista con scroll de Ajustes, gClip*
  // podria estar estrechado y el contador no se dibujaria -- y sin monitor
  // serie eso solo se ve como "la cuenta atras no aparece".
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  if(lsuMode == LSU_PIN){
    for(int j = y0; j <= y1; j++) memcpy(bbuf + (size_t)j * SCR_W, lockBuf + (size_t)j * SCR_W, SCR_W * 2);
  } else if(lsuVerify && blurBg){
    for(int j = y0; j <= y1; j++) memcpy(bbuf + (size_t)j * SCR_W, blurBg + (size_t)j * SCR_W, SCR_W * 2);
  } else {
    fillRect(0, y0, SCR_W, y1 - y0 + 1, lsuBgCol());
  }
}
// Contador regresivo con DIFFING: si el segundo que toca mostrar es el mismo
// que ya esta pintado, esta funcion no toca ni un pixel. Cuando cambia,
// repinta solo la franja del numero (LW_NUM_*), no la pantalla.
static void lockWaitTick(){
  uint32_t rem = lockWaitUntil - millis();                     // >0: lo garantiza lockWaitActive()
  int secs = (int)((rem + 999) / 1000);                        // redondea hacia arriba
  if(lockWaitPainted && secs == lockWaitLastSec) return;       // nada cambio
  bool first = !lockWaitPainted;
  int y0 = first ? LW_BAND_Y0 : LW_NUM_Y0;
  int y1 = first ? LW_BAND_Y1 : LW_NUM_Y1;
  lockWaitBase(y0, y1);
  if(first){
    const char* m1 = "Demasiados intentos fallidos";
    drawTextC(SCR_W / 2, LW_MSG_Y, m1, uiFontFit(m1, SCR_W - 40, 2), TH_ERR);      // estado: error
    if(lockFails >= LOCK_FAILS_HARD){                          // mensaje explicito del tramo largo
      const char* m2 = "Bloqueo temporal de 5 minutos";
      drawTextC(SCR_W / 2, LW_MSG2_Y, m2, uiFontFit(m2, SCR_W - 40, 2), lsuTxtLo());
    }
  }
  char cd[16];
  if(secs >= 60) snprintf(cd, sizeof(cd), "%d:%02d", secs / 60, secs % 60);
  else           snprintf(cd, sizeof(cd), "%d s", secs);
  drawTextC(SCR_W / 2, LW_NUM_Y, cd, 4, lsuTxtHi());
  present(y0, y1);
  setBuf(fb);
  lockWaitLastSec = secs; lockWaitPainted = true;
}
// Olvida lo que hay pintado del contador SIN dibujar nada: quien llama a esto
// va a recomponer la zona igualmente (lsuAnimPin, lsuRenderPass o lsuExit), y
// asi el borrado viaja en ese mismo present() en vez de en un volcado propio.
// No toca lockWaitUntil: salir de la pantalla no perdona la espera.
static void lockWaitReset(){
  lockWaitPainted = false; lockWaitLastSec = -1;
}
static void lockOnFail(){
  if(!LOCK_FAILS_ON) return;
  if(lockFails < 9999) lockFails++;
  lockFailsSave();                                  // persiste ANTES de cualquier espera
  lockPenaltyServed = false;
  uint32_t pen = lockPenaltyMs();
  lockWaitUntil = pen ? (millis() + pen) : 0;
  lockWaitPainted = false; lockWaitLastSec = -1;
}
static void lockOnSuccess(){
  lockWaitUntil = 0; lockWaitPainted = false; lockWaitLastSec = -1;
  lockPenaltyServed = true;
  if(!LOCK_FAILS_ON) return;
  if(lockFails != 0){ lockFails = 0; lockFailsSave(); }         // acierto -> contador a cero
}
// Se llama al ABRIR la pantalla de verificacion. Si el contador ya venia alto
// -por ejemplo porque se reinicio la placa para intentar saltarse la espera-
// la espera se cobra aqui, antes del primer intento. lockPenaltyServed arranca
// en false en cada arranque, asi que el castigo se aplica una vez tras el
// reinicio y no se repite cada vez que se entra y se sale de la pantalla.
static void lockArmPendingPenalty(){
  if(!LOCK_FAILS_ON || lockWaitUntil || lockPenaltyServed) return;
  uint32_t pen = lockPenaltyMs();
  if(!pen) return;
  lockWaitUntil = millis() + pen;
  lockWaitPainted = false; lockWaitLastSec = -1;
}

// ---- (c) Auto-bloqueo por inactividad ---------------------------------
// No duplica NADA de la logica de bloqueo: cierra la app con appClose() (su
// animacion normal, que ya deja miniatura en Recientes y devuelve a ST_HOME) y
// luego deja caer el bloqueo con animateTo()/composeUnlock(), el mismo
// mecanismo interpolado del desbloqueo por gesto pero al reves.
static void autoLockNow(){
  if(hcActive) hcClose(true);                // personalizacion abierta: se guarda y se cierra en limpio
  // FLEX VAULT: si el sistema se bloquea por inactividad, la boveda se cierra
  // con el. Antes que nada, para que ni un frame de la pantalla de bloqueo se
  // componga con la boveda todavia abierta.
  vaultLockFromSystem(FXV_LOCK_SCREEN);
  if(editMode) edExit();                    // guarda el orden de iconos y repinta el Home
  if(gState == ST_APP) appClose();           // -> ST_HOME, con su animacion de cierre
  gRippleActive = false;
  renderHome();                              // capa de abajo de composeUnlock, al dia
  renderLock();                              // capa de arriba, con la hora actual
  gState = ST_LOCK;
  animateTo(SCR_H, 0);                       // el bloqueo baja interpolado (cero parpadeo)
  lockOff = 0; lastLockOff = -1;
  showLock();
  gLastTouchMs = millis();
}
static void autoLockTick(){
  if(!AUTOLOCK_ON) return;
  // FASE 4: en kiosco NO se auto-bloquea. El telefono esta prestado y en uso; y
  // ademas dejar caer el bloqueo sobre el kiosco mezclaria dos modos que se
  // pisan (appClose esta vetado en kiosco, asi que el cierre quedaria a medias).
  if(KIOSK_ON && kioskOn) return;
  // SUSPENSION Y AUTO-BLOQUEO SON INDEPENDIENTES, NO SE PISAN.
  // Mientras la pantalla esta suspendida no se deja caer el bloqueo desde aqui:
  // hacerlo contra una pantalla apagada seria trabajo invisible (animateTo
  // compone y vuelca un frame entero que nadie ve) y dejaria lockOff
  // desincronizado con lo que hay en el framebuffer.
  // No hace falta: del bloqueo al despertar ya se encarga suspWakeLockScreen(),
  // que lo pone SIEMPRE que haya clave configurada -- sin esperar a que venza
  // ninguna ventana de inactividad. Asi que suspender es, de hecho, mas estricto
  // que el auto-bloqueo, no menos.
  // Tambien se aplaza durante el apagado completo: ahi el destino ya esta
  // decidido y bloquear a medias solo puede romper la animacion.
  if(SUSPEND_ON && gSuspOn) return;
  if(POWEROFF_ON && (gState == ST_POWEROFF_CONFIRM || gState == ST_POWEROFF_ANIM)) return;
  // NUNCA auto-bloquear con una actualizacion en marcha. Una descarga no
  // genera toques, asi que el temporizador de inactividad vencia a mitad
  // de la OTA (30 s por defecto): caia la pantalla de bloqueo, se
  // repintaba a pantalla completa peleandose con la de progreso, y el
  // usuario se encontraba pidiendo el PIN mientras se estaba flasheando.
  // Tambien se respeta cualquier capa OTA a pantalla completa.
  if(flexOtaBusy() || flexOtaOwnsScreen()){ gLastTouchMs = millis(); return; }
  // Cualquier contacto, en CUALQUIER pantalla, rearma el temporizador. Va antes
  // del filtro de estados para que al volver del PIN al escritorio el contador
  // no arranque ya vencido y vuelva a bloquear al instante.
  if(T.down || T.pressed || T.released){ gLastTouchMs = millis(); return; }
  if(!gAutoLockMs) return;
  // La Caja de aplicaciones cuenta como escritorio: dejarla fuera de esta lista
  // seria un agujero -- el equipo se quedaria encendido para siempre con la
  // caja abierta y el temporizador de inactividad parado.
  if(gState != ST_HOME && gState != ST_APP && gState != ST_DRAWER && gState != ST_HOMECFG) return;
  if(gLand || gHosted) return;                // Modo PC / app hospedada: no se toca
  if(qsPanelY != 0) return;                   // cortina abierta: no bloquear a media interaccion
  if(!gLastTouchMs){ gLastTouchMs = millis(); return; }
  if(millis() - gLastTouchMs < gAutoLockMs) return;
  autoLockNow();
}

// #############################################################
// ##  FASE 4 - MODO KIOSCO (prestamo seguro)
// ##  El estado y el filtro tactil estan arriba, junto a flexPollTouch.
// ##  Aqui va todo lo que necesita dibujar o navegar.
// #############################################################

// ---- Candado discreto de "kiosco activo" -------------------------------
// El candado NO se refresca por temporizador. Se ESTAMPA dentro de flxFlush(),
// que es el unico punto por el que absolutamente todo acaba llegando al panel,
// justo antes de publicar la banda sucia. Asi el candado esta en fb POR
// CONSTRUCCION antes de cualquier subida DMA.
//
// Por que no basta con repintarlo desde kioskTick: una app puede
// componer en bbuf y hacer present(0, SCR_H-1), mientras flxFlush sube
// fb por DMA2D. Repintando despues del present habia una ventana en la que la
// transferencia ya habia empezado sin el candado. Estampando dentro de flxFlush esa
// ventana no existe: la banda nunca se publica sin el candado dentro.
// Recuadro que envuelve al candado con margen. Es la banda que se comprueba en
// flxFlush y la que publica kioskShowBadge, asi que tiene que seguir a
// KIOSK_BADGE_X/Y: candado en 448..471 x 44..67, recuadro 444..479 x 40..71.
#define KIOSK_BOX_X 444
#define KIOSK_BOX_Y 40
#define KIOSK_BOX_W 36
#define KIOSK_BOX_H 32
static void kioskBadgePaint(){
  int bx = KIOSK_BADGE_X, by = KIOSK_BADGE_Y, bs = KIOSK_BADGE_S;
  // El candado se sella sobre CUALQUIER app (Paint, Juegos, la Camara...), asi
  // que la pastilla lleva su propio contraste: fondo del tema y glifo del tema.
  fillRoundRectA(bx, by, bs, bs, 7, TH_PAGE, 200);
  fillCircleA(bx + bs / 2, by + 9, 5, TH_TXT, 255);            // arco del candado
  fillCircleA(bx + bs / 2, by + 9, 3, TH_PAGE, 255);
  fillRoundRectA(bx + 5, by + 11, bs - 10, 9, 2, TH_TXT, 255); // cuerpo
}
// Llamada desde flxFlush con la banda que se va a publicar. Sale enseguida en el
// caso normal (kiosco apagado, o banda que no toca la esquina del candado), asi
// que no encarece el camino de dibujo del resto del sistema. Dibuja y ya: NO
// llama a flxFlush, de modo que no hay recursion.
static void kioskStampBadge(int y0, int y1){
  if(!KIOSK_ON || !kioskOn) return;
  if(gState != ST_APP) return;                                   // solo sobre la app clavada
  if(y1 < KIOSK_BOX_Y || y0 > KIOSK_BOX_Y + KIOSK_BOX_H - 1) return;
  // gLand fuera y recorte completo: el candado va en la esquina FISICA del panel,
  // pase lo que pase con la orientacion logica de la app (Juegos dibuja en
  // landscape y pone gLand=true en cada frame).
  uint16_t* ob = gBuf; bool wl = gLand;
  int sx0 = gClipX0, sx1 = gClipX1, sy0 = gClipY0, sy1 = gClipY1;
  gLand = false;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  setBuf(fb);
  kioskBadgePaint();
  setBuf(ob); gLand = wl;
  gClipX0 = sx0; gClipX1 = sx1; gClipY0 = sy0; gClipY1 = sy1;
}
// Publica la banda del candado al ENTRAR en kiosco, para que aparezca de
// inmediato aunque la app no vuelva a dibujar por su cuenta. El estampado en si
// lo hace flxFlush. A partir de ahi el candado se mantiene solo.
static void kioskShowBadge(){
  if(!KIOSK_ON || !kioskOn) return;
  flxFlush(KIOSK_BOX_Y, KIOSK_BOX_Y + KIOSK_BOX_H - 1);
}

// ---- Entrar / salir ----------------------------------------------------
static void kioskStart(int id, int ex, int ey, int ew, int eh){
  if(!KIOSK_ON || gLockType == 0 || id < 0 || id >= APP_N) return;   // sin clave no habria salida: no se activa
  kioskOn = true; kioskApp = id;
  kioskExX = ex; kioskExY = ey; kioskExW = ew; kioskExH = eh;
  kioskSave();
  renderHome();                 // la transicion compone sobre homeBuf
  enterApp(id);                 // apertura con la animacion normal del sistema
  kioskShowBadge();
}
static void kioskExitNow(){
  kioskOn = false; kioskApp = -1;
  kioskExX = kioskExY = kioskExW = kioskExH = 0;
  kioskSave();
  gLand = false;                                       // misma red de seguridad que appClose
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  setBuf(fb);
  renderHome();
  enterHome();                                         // deja gState en ST_HOME y vuelca el escritorio
  gLastTouchMs = millis();
}
// Gesto de salida: mantener pulsado el candado > 1 s. Es el mismo gesto de
// long-press con el que se entro (desde el menu contextual), y solo abre la
// verificacion: sin PIN/contrasena correcta no se sale.
// El area excluida SOLO filtra mientras la app clavada esta en pantalla.
//
// Antes filtraba en cualquier estado, y eso convertia el telefono en un ladrillo
// irrecuperable: si el usuario dibujaba el area encima del teclado del PIN (o
// simplemente grande), al pedir la salida del kiosco la pantalla de verificacion
// aparecia pero el teclado no respondia -- y como el kiosco persiste en NVS,
// reiniciar volvia a entrar en el mismo sitio. No habia ninguna salida.
//
// El area excluida existe para que la APP no reciba esos toques; la UI del
// propio sistema (verificacion, menus) nunca fue su objetivo.
static bool kioskTouchBlocked(int px, int py){
  if(!KIOSK_ON || !kioskOn) return false;
  if(gState != ST_APP) return false;              // verificacion del PIN, menus, etc: sin filtro
  if(!kioskInExcluded(px, py)) return false;
  return !kioskInExit(px, py);                    // el candado de salida siempre gana
}
static bool kioskExitFired = false;                    // ya se disparo con ESTE contacto
static void kioskTick(){
  if(!KIOSK_ON || !kioskOn){ kioskExitFired = false; return; }
  // El rearme va ANTES del filtro de estado: mientras se escribe la clave
  // seguimos en ST_LOCKSETUP, y si el dedo se levanta ahi hay que poder volver a
  // intentarlo. Sin esto, un dedo que siguiera apoyado al cancelar la salida
  // reabriria la verificacion en bucle.
  if(!T.down) kioskExitFired = false;
  if(gState != ST_APP) return;
  // Ya no repinta el candado: de eso se encarga kioskStampBadge desde flxFlush.
  // Aqui solo queda escuchar el gesto de salida.
  if(!kioskExitFired && T.down && kioskInExit(T.startX, T.startY) && (millis() - T.downMs) > 1000
     && abs(T.x - T.startX) < 12 && abs(T.y - T.startY) < 12){
    kioskExitFired = true;
    lsuStartVerifyFor(LSU_AFTER_KIOSKOUT, kioskApp);
  }
}

// ---- Pantalla para definir el area excluida (ST_KIOSKSET) --------------
// El fondo (titulo, icono, textos y botones) es ESTATICO y se compone una sola
// vez en lockBuf -- el mismo uso de scratch que ya hace lsuComposePin. Cada
// frame del arrastre solo copia esa base y dibuja el rectangulo encima: un
// unico present() por frame, cero parpadeo.
#define KS_BTN_Y (SCR_H - 96)
#define KS_BTN_H 64
#define KS_BTN_W 180
static int  kioskSetApp = -1;
static int  kioskSetX0 = 0, kioskSetY0 = 0, kioskSetX1 = 0, kioskSetY1 = 0;
static bool kioskSetHas = false;
static uint32_t kioskSetMs = 0;
static void kioskSetBase(int id){
  memcpy(lockBuf, homeBuf, (size_t)SCR_W * SCR_H * 2);
  setBuf(lockBuf);
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  // Velo del color de pagina (gDark) + TARJETA de vidrio con el contenido, que
  // es como usa el vidrio el resto del sistema. Antes el velo era un gris
  // azulado fijo, que es justo por lo que parecia que aqui "se quitaba" el
  // Liquid Glass. El vidrio va en la tarjeta y no a pantalla completa a
  // proposito: drawLiquidGlassPanelEx reparte su degradado de luz sobre TODA la
  // altura del panel, y a 800 px eso seria una banda blanca-a-negra enorme en
  // vez de un cristal.
  fillRectA(0, 0, SCR_W, SCR_H, PAGE_BG, 208);
  if(uiGlass) drawLiquidGlassPanel(24, 40, SCR_W - 48, 350, 24, SET_CARD_GLASS);
  else        fillRoundRectA(24, 40, SCR_W - 48, 350, 24, SET_CARD_BG, 235);
  drawTextC(SCR_W / 2, 64, "Modo Kiosco", 4, SET_TXT_HI);
  const char* s1 = "Arrastra para excluir una zona del tactil";
  drawTextC(SCR_W / 2, 122, s1, uiFontFit(s1, SCR_W - 40, 2), SET_TXT_LO);
  const char* s2 = "Si no arrastras, toda la pantalla queda activa";
  drawTextC(SCR_W / 2, 146, s2, uiFontFit(s2, SCR_W - 40, 2), SET_TXT_MUTE);
  drawAppIcon(id, SCR_W / 2 - 36, 196, 72);
  drawTextC(SCR_W / 2, 282, appName(id), 3, SET_TXT_HI);
  const char* s3 = "Para salir: manten pulsado el candado de la";
  drawTextC(SCR_W / 2, 344, s3, uiFontFit(s3, SCR_W - 40, 2), SET_TXT_LO);
  const char* s4 = "esquina y escribe tu clave del sistema";
  drawTextC(SCR_W / 2, 366, s4, uiFontFit(s4, SCR_W - 40, 2), SET_TXT_LO);
  // "Cancelar" es una tarjeta normal del tema; "Iniciar" es la ACCION PRIMARIA,
  // asi que toma TH_PRIM (y su texto TH_ONACC) de la paleta activa.
  if(uiGlass) drawLiquidGlassPanel(30, KS_BTN_Y, KS_BTN_W, KS_BTN_H, 20, SET_CARD_GLASS);
  else        fillRoundRect(30, KS_BTN_Y, KS_BTN_W, KS_BTN_H, 20, SET_CARD_BG);
  drawTextC(30 + KS_BTN_W / 2, KS_BTN_Y + KS_BTN_H / 2 - 9, "Cancelar", 2, SET_TXT_HI);
  fillRoundRect(SCR_W - 30 - KS_BTN_W, KS_BTN_Y, KS_BTN_W, KS_BTN_H, 20, TH_PRIM);
  drawTextC(SCR_W - 30 - KS_BTN_W / 2, KS_BTN_Y + KS_BTN_H / 2 - 9, "Iniciar", 2, TH_ONACC);
  setBuf(fb);
}
static void kioskSetRender(){
  memcpy(bbuf, lockBuf, (size_t)SCR_W * SCR_H * 2);
  setBuf(bbuf);
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  if(kioskSetHas){
    int x = kioskSetX0 < kioskSetX1 ? kioskSetX0 : kioskSetX1;
    int y = kioskSetY0 < kioskSetY1 ? kioskSetY0 : kioskSetY1;
    int w = kioskSetX1 - kioskSetX0; if(w < 0) w = -w;
    int h = kioskSetY1 - kioskSetY0; if(h < 0) h = -h;
    // Zona EXCLUIDA del tactil: rojo de estado (significa "aqui no se toca"),
    // se conserva en las dos apariencias.
    fillRectA(x, y, w, h, TH_ERR, 95);
    drawRoundRect(x, y, w, h, 4, TH_ERR);
    char b[24]; snprintf(b, sizeof(b), "%d x %d", w, h);
    drawTextC(x + w / 2, y + h / 2 - 9, b, 2, SET_TXT_HI);
  }
  present(0, SCR_H - 1);
  setBuf(fb);
}
static void kioskSetEnter(int id){
  kioskSetApp = id; kioskSetHas = false; kioskSetMs = 0;
  kioskSetX0 = kioskSetY0 = kioskSetX1 = kioskSetY1 = 0;
  gState = ST_KIOSKSET;
  kioskSetBase(id);
  kioskSetRender();
}
static void kioskSetTick(){
  // El arrastre solo cuenta si EMPIEZA por encima de la fila de botones, asi que
  // pulsar "Iniciar" o "Cancelar" nunca se interpreta como dibujar.
  bool dragZone = (T.startY < KS_BTN_Y - 8);
  if(T.pressed && dragZone){
    kioskSetX0 = kioskSetX1 = T.x; kioskSetY0 = kioskSetY1 = T.y;
    kioskSetHas = false; return;
  }
  if(T.down && dragZone){
    kioskSetX1 = T.x; kioskSetY1 = T.y;
    int w = kioskSetX1 - kioskSetX0; if(w < 0) w = -w;
    int h = kioskSetY1 - kioskSetY0; if(h < 0) h = -h;
    kioskSetHas = (w >= 20 && h >= 20);                 // por debajo de eso es un toque, no un area
    if(millis() - kioskSetMs > 30){ kioskSetMs = millis(); kioskSetRender(); }
    return;
  }
  if(T.released && dragZone){ kioskSetRender(); return; }   // fija el rectangulo dibujado
  if(T.tap && T.y >= KS_BTN_Y && T.y <= KS_BTN_Y + KS_BTN_H){
    if(T.x >= 30 && T.x <= 30 + KS_BTN_W){                  // Cancelar
      gState = ST_HOME; renderHome(); showHome(); return;
    }
    if(T.x >= SCR_W - 30 - KS_BTN_W && T.x <= SCR_W - 30){  // Iniciar
      int ex = 0, ey = 0, ew = 0, eh = 0;
      if(kioskSetHas){
        ex = kioskSetX0 < kioskSetX1 ? kioskSetX0 : kioskSetX1;
        ey = kioskSetY0 < kioskSetY1 ? kioskSetY0 : kioskSetY1;
        ew = kioskSetX1 - kioskSetX0; if(ew < 0) ew = -ew;
        eh = kioskSetY1 - kioskSetY0; if(eh < 0) eh = -eh;
      }
      kioskStart(kioskSetApp, ex, ey, ew, eh);
      return;
    }
  }
}
