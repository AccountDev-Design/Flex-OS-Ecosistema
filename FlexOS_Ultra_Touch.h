// #############################################################
// ##  FLEX OS ULTRA  ·  TACTIL DE ALTO NIVEL Y SUSPENSION DE PANTALLA
// ##  ----------------------------------------------------------
// ##  Gestos sobre la struct Touch (tap, arrastre, deslizamientos) y el
// ##  apagado NORMAL de pantalla: doble toque con dos dedos para entrar,
// ##  con uno para salir. No es deep sleep (eso vive en Power).
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
#include "FlexOS_Ultra_Icons.h"   // eslabon anterior de la cadena

// #############################################################
// ##  TACTIL DE ALTO NIVEL (gestos)  ·  original
// #############################################################
static Touch T;
// NAVEGACION: candado de "tragar el episodio tactil en curso". Lo arma
// touchDropAll() en cada cambio de pantalla (inicio, atras, recientes,
// reanudar una app). Mientras el dedo siga apoyado del toque ANTERIOR, la
// pantalla nueva no ve ni pressed, ni down, ni tap: es lo que impide el toque
// fantasma de "he pulsado inicio y se ha abierto un icono del escritorio".
static bool gTouchSwallow = false;

// ---- FASE 4: Modo Kiosco (prestamo seguro) -------------------------------
// El estado vive AQUI ARRIBA, antes de flexPollTouch(), porque el filtro del
// area excluida tiene que actuar en el punto MAS ALTO del pipeline tactil: si
// se filtrara mas abajo, una app hospedada podria llegar a ver el toque.
#define KIOSK_ON 1                     // interruptor de toda la Fase 4
static bool kioskOn   = false;         // true = modo kiosco activo (NVS "kioskon")
static int  kioskApp  = -1;            // app "clavada" (indice de APP_REG, NVS "kioskapp")
static int  kioskExX  = 0, kioskExY = 0, kioskExW = 0, kioskExH = 0;  // area excluida (W=0 -> ninguna)
// Candado discreto + zona del gesto de salida. Coordenadas FISICAS del panel,
// no logicas: asi el gesto de salida funciona igual aunque la app corra en
// landscape (gLand).
//
// Va en la esquina SUPERIOR DERECHA, justo DEBAJO de la barra de estado. Antes
// estaba en (8,8) y se comia la hora, que renderHome() dibuja en (20,16). El
// hueco de aqui esta libre en todas las pantallas: el wifi ocupa x 403..425 y
// llega hasta y=28, la bateria x 434..466 e y 20..35, y los widgets del Home no
// empiezan hasta y=72. Con el candado en (448,44) quedan 9 px de margen bajo la
// bateria y 7 px hasta el borde derecho.
#define KIOSK_BADGE_X 448
#define KIOSK_BADGE_Y 44
#define KIOSK_BADGE_S 24
#define KIOSK_EXIT_PAD 14              // el hitbox del gesto es mas generoso que el dibujo
static bool kioskInExcluded(int px, int py){
  if(kioskExW <= 0 || kioskExH <= 0) return false;
  return px >= kioskExX && px < kioskExX + kioskExW && py >= kioskExY && py < kioskExY + kioskExH;
}
// Decide si un toque se descarta. Se define abajo del todo porque necesita
// gState, que se declara mas adelante; flexPollTouch la llama por prototipo.
static bool kioskTouchBlocked(int px, int py);
// GESTO DE DOS DEDOS del escritorio. Se declara aqui por el mismo motivo que el
// de arriba: el detector necesita gState, editMode y la cortina -- que se
// declaran mas abajo --, pero tiene que actuar en el punto MAS ALTO del
// pipeline tactil, antes de que ninguna capa vea el contacto.
static void hpzUpdate();
static bool hpzSwallowing();
// El hitbox de salida SIEMPRE gana sobre el area excluida: si el usuario dibuja
// un rectangulo encima del candado, el dueno del telefono seguiria pudiendo
// salir. Sin esto, el propio modo kiosco se podria convertir en un ladrillo.
static bool kioskInExit(int px, int py){
  return px >= KIOSK_BADGE_X - KIOSK_EXIT_PAD && px <= KIOSK_BADGE_X + KIOSK_BADGE_S + KIOSK_EXIT_PAD &&
         py >= KIOSK_BADGE_Y - KIOSK_EXIT_PAD && py <= KIOSK_BADGE_Y + KIOSK_BADGE_S + KIOSK_EXIT_PAD;
}

// #############################################################
// ##  SUSPENSION (apagado NORMAL de pantalla, sin deep sleep)
// ##  ------------------------------------------------------
// ##  Entrar: doble-tap con DOS dedos en cualquier parte.
// ##  Salir : doble-tap con UN dedo en cualquier parte.
// ##
// ##  Que es y que NO es:
// ##   · El ESP32 NO se duerme. loop() sigue corriendo igual (WiFi,
// ##     reloj, animaciones internas). Lo unico que se apaga es la
// ##     SALIDA VISUAL: backlight a 0 por PWM + DISPOFF del panel.
// ##   · NO se toca gState. El sistema sigue "siendo" lo que era
// ##     (Home, App, Juegos, Modo PC...). La suspension es una capa
// ##     de driver superpuesta, no una pantalla de la aplicacion.
// ##   · NO se toca fb ni bbuf. La UI queda intacta en memoria, asi
// ##     que al despertar reaparece sola: el propio fundido del
// ##     backlight YA produce visualmente un fade-in desde negro
// ##     sobre lo que sigue estando en el framebuffer. Por eso NO se
// ##     compone ningun overlay negro de aparicion -- seria pintar
// ##     encima de la UI para conseguir un efecto que el backlight
// ##     regala gratis, y ademas obligaria a redibujar para limpiarlo.
// ##
// ##  El detector NO usa struct Touch: trabaja directamente sobre
// ##  gtFingers (cuenta de contactos del GT911). Asi es inmune a que
// ##  el filtro de abajo anule los flags de T, y el contrato de T
// ##  para el resto del sistema no cambia en absoluto.
// #############################################################
static bool gSuspOn      = false;   // suspension activa (desde el gesto hasta que termina el fundido de vuelta)
static bool gSuspDark    = false;   // backlight ya en 0 y DISPOFF enviado
static int  gSuspBright  = 80;      // brillo del usuario al suspender (para restaurarlo exacto)
static int  gSuspFade    = -1;      // -1 = sin fundido en curso; si no, brillo actual del fundido (0..100)
static int  gSuspFadeTo  = 0;       // destino del fundido
static uint32_t gSuspFadeMs = 0;    // millis() del ultimo paso del fundido
static bool gSuspSwallow = false;   // este poll pertenece al gesto -> no lo ve nadie mas

// ---- Estado del detector de doble-tap ----------------------------------
// Un "toque" (episodio) va desde que baja el primer dedo hasta que se levantan
// todos. Durante el episodio se anota cuantos dedos llego a haber.
static bool     gEpAct   = false;   // hay un episodio de toque en curso
static uint32_t gEpT0    = 0;       // millis() del inicio del episodio
static uint8_t  gEpRun2  = 0;       // polls CONSECUTIVOS con n>=2 dentro del episodio
static bool     gEpHad2  = false;   // el episodio quedo confirmado como "de 2 dedos"
static bool     gEpHad3  = false;   // llego a 3+ dedos -> no cuenta ni como 1 ni como 2
static uint32_t gTap2Ms  = 0;       // fin del ultimo toque valido de 2 dedos (0 = cadena vacia)
static uint32_t gTap1Ms  = 0;       // fin del ultimo toque valido de 1 dedo

// ---- Vuelta a donde estabas tras desbloquear ---------------------------
// Al despertar con clave configurada se cae en la pantalla de Bloqueo, pero el
// sitio donde estaba el usuario NO se pierde: se anota aqui y, al acertar el
// PIN, se restaura. -1 = nada pendiente.
static int  gSuspRetState = -1;     // gState que habia al suspender
static int  gSuspRetApp   = -1;     // gAppId que habia al suspender (si era ST_APP)
// true mientras corre una verificacion que SALIO de la pantalla de Bloqueo. Sin
// esto, cancelar el teclado de PIN caeria por la rama de lsuExit que lleva a
// ST_HOME, y el escritorio quedaria a la vista SIN haber desbloqueado -- justo
// el agujero que este cambio viene a cerrar.
static bool gLockVerifyLocked = false;
// Se define mucho mas abajo (necesita gState, renderLock y showLock, que aun no
// existen aqui). Mismo patron que kioskTouchBlocked: prototipo arriba, cuerpo
// abajo. Solo primitivos en la firma, como exige el auto-prototipado de Arduino.
// FLEX VAULT (Carpeta segura). Se define abajo del todo (necesita el teclado,
// los dialogos de ficheros y el teclado numerico del bloqueo), pero Ajustes y
// los caminos de cierre del sistema -- todos ANTES en el archivo -- necesitan
// llamarla. Firmas con tipos primitivos, como exige el auto-prototipado.
static void vaultSettingsEnter();                         // Ajustes -> Seguridad y privacidad -> Flex Vault
static void vaultTick();                                  // su tick, desde loop()
static void vaultRender();                                // repintado completo
static void vaultLockFromSystem(int reason);              // cierre desde fuera (pantalla, apagado, bloqueo...)
static void vaultStatusText(char* out, size_t n);          // texto de la fila de Ajustes
static bool vaultMoveRequest(const char* path, int kind);  // "Mover a Carpeta segura" desde una app
static const char* vaultMoveError();                       // motivo si vaultMoveRequest devolvio false
static void suspWakeLockScreen();
// true mientras hay dedos sobre la rejilla del teclado. Se define abajo, con el
// teclado; aqui solo el prototipo (primitivos en la firma). Lo necesita el
// gesto de suspension: ver el VETO AL TECLEAR en suspGestureUpdate.
static bool kbTypingNow();
// PANEL RAPIDO GLOBAL: cierre limpio y obligatorio. Se define con el resto de
// la cortina (mucho mas abajo), pero lo llaman TODOS los cambios de estado que
// estan antes en el archivo -- suspender, abrir o cerrar una app, volver al
// escritorio, bloquear, apagar, Modo PC y kiosco. Firma sin tipos propios, como
// exige el auto-prototipado de Arduino.
static void qsForceClose();

// Arranca un fundido de backlight NO bloqueante hacia 'to' (0..100).
static void suspFadeTo(int to){
  // Si ya habia un fundido en curso, sigue desde donde iba; si no, arranca desde
  // el brillo REAL del PWM (gBlPct), NO desde gBright. Al despertar gBright
  // sigue valiendo lo de siempre (p.ej. 80) mientras el PWM esta a 0: arrancar
  // desde gBright dejaria el fundido ya en su destino y la pantalla se
  // encenderia de golpe -- justo el fogonazo que hay que evitar.
  if(gSuspFade < 0) gSuspFade = gBlPct;
  gSuspFadeTo = to;
  gSuspFadeMs = millis();
}
static void suspEnter(){
  if(gSuspOn) return;
  qsForceClose();                    // apagar la pantalla no deja la cortina a medias
  // FLEX VAULT: apagar la pantalla CIERRA la Carpeta segura, siempre. Va aqui
  // arriba, antes del fundido, para que las claves ya no esten en RAM cuando la
  // pantalla acabe de apagarse.
  vaultLockFromSystem(FXV_LOCK_SCREEN);
  gSuspBright = gBright;             // brillo del usuario, intacto (blWritePct no lo toca)
  gSuspOn = true; gSuspDark = false;
  suspFadeTo(0);
}
static void suspWake(){
  if(!gSuspOn) return;
  // ORDEN IMPORTANTE (privacidad): la pantalla de Bloqueo se compone MIENTRAS
  // sigue todo a oscuras -- backlight a 0 y el panel aun en DISPOFF. Asi lo que
  // habia antes de suspender no llega a verse ni un frame: cuando el backlight
  // empieza a subir, lo que hay en el framebuffer YA es el bloqueo.
  suspWakeLockScreen();
  if(gSuspDark){ panelDisplayOn(); gSuspDark = false; }   // revertir el DCS ANTES de subir el backlight
  suspFadeTo(gSuspBright);
}
// Un paso del fundido por vuelta de loop(). Sin delay(), igual de suave que el
// resto de animaciones del sistema (interpolado en varios ticks).
static void suspFadeTick(){
  if(gSuspFade < 0) return;
  uint32_t now = millis();
  if(now - gSuspFadeMs < SUSP_FADE_STEP_MS) return;
  gSuspFadeMs = now;
  if(gSuspFade > gSuspFadeTo){ gSuspFade -= SUSP_FADE_STEP; if(gSuspFade < gSuspFadeTo) gSuspFade = gSuspFadeTo; }
  else if(gSuspFade < gSuspFadeTo){ gSuspFade += SUSP_FADE_STEP; if(gSuspFade > gSuspFadeTo) gSuspFade = gSuspFadeTo; }
  blWritePct(gSuspFade);
  if(gSuspFade != gSuspFadeTo) return;
  gSuspFade = -1;                                   // fundido terminado
  if(gSuspFadeTo == 0){
    if(gSuspOn && !gSuspDark){ gSuspDark = true; panelDisplayOff(); }   // DCS DESPUES del negro
  } else {
    setBacklight(gSuspBright);      // deja gBright coherente y el PWM en su valor exacto
    gSuspOn = false;                // a partir de aqui el tactil vuelve a fluir normal
  }
}
// Detector de doble-tap. Se llama DESDE flexPollTouch(), al final, con gtFingers
// ya actualizado por gtPoll(). Decide ademas si este poll se lo traga el gesto.
static void suspGestureUpdate(){
#if SUSPEND_ON
  uint32_t now = millis();
  // Cuenta de dedos con la MISMA red de seguridad de 90 ms que usa flexPollTouch:
  // si el GT911 deja de reportar sin mandar el frame de "0 dedos", el episodio
  // no se queda colgado para siempre.
  int n = (now - gtFingersMs > 90) ? 0 : (int)gtFingers;

  // Caducidad de las cadenas de doble-tap (el 2o toque llego tarde -> se olvida).
  if(gTap2Ms && now - gTap2Ms > SUSP_TAP_WINDOW_MS) gTap2Ms = 0;
  if(gTap1Ms && now - gTap1Ms > SUSP_TAP_WINDOW_MS) gTap1Ms = 0;

  if(n > 0){
    if(!gEpAct){ gEpAct = true; gEpT0 = now; gEpRun2 = 0; gEpHad2 = false; gEpHad3 = false; }
    if(n >= 3) gEpHad3 = true;
    // ANTI FALSO-POSITIVO: n>=2 tiene que sostenerse SUSP_TAP_FRAMES polls
    // consecutivos. El instante en que el segundo dedo esta aterrizando es
    // ruidoso y puede dar un unico frame con n=2 espurio.
    // TOLERANCIA A DESINCRONIZACION: no se exige que los dos dedos bajen en el
    // mismo poll; basta con que en ALGUN momento del episodio n llegue a 2 el
    // numero de frames pedido, y la marca gEpHad2 ya no se pierde aunque luego
    // uno de los dedos se levante antes que el otro.
    if(n >= 2){ if(gEpRun2 < 255) gEpRun2++; if(gEpRun2 >= SUSP_TAP_FRAMES) gEpHad2 = true; }
    else gEpRun2 = 0;
  } else if(gEpAct){
    gEpAct = false;
    uint32_t dur = now - gEpT0;
    bool tooLong = (dur > SUSP_TAP_MAX_MS);          // long-press: no es un tap
    if(tooLong || gEpHad3){ gTap1Ms = 0; gTap2Ms = 0; }
    else if(gEpHad2){                                 // ---- toque valido de 2 dedos ----
      // VETO EN KIOSCO: mismo criterio que autoLockTick. En modo kiosco el
      // telefono esta prestado y quien lo tiene en la mano no sabe que existe el
      // gesto de despertar; una pantalla que se queda negra de golpe se lee como
      // "se ha roto" y el dueno acaba teniendo que rescatarlo. Ademas la
      // suspension no aporta nada ahi: el kiosco es para uso activo, no para
      // guardar el aparato.
      //
      // VETO AL TECLEAR (Fase B): escribir con los dos pulgares produce
      // continuamente episodios de 2 dedos. Sin esto pasaban dos cosas, las dos
      // malas: teclear a dos manos apagaba la pantalla sola, y -- peor -- el
      // gSuspSwallow de mas abajo anulaba TODOS los eventos de T mientras habia
      // dos dedos, asi que el teclado se quedaba mudo justo cuando se escribia
      // rapido. El gesto de suspender sigue existiendo igual en todas partes;
      // solo se calla mientras hay dedos sobre las teclas.
      bool veto = (KIOSK_ON && kioskOn) || kbTypingNow();
      if(gTap2Ms && (now - gTap2Ms) >= SUSP_TAP_GAP_MS){ gTap2Ms = 0; if(!gSuspOn && !veto) suspEnter(); }
      else gTap2Ms = now;
      gTap1Ms = 0;                                    // las dos cadenas son excluyentes
    } else {                                          // ---- toque valido de 1 dedo ----
      // El doble-tap de 1 dedo SOLO se escucha con la pantalla suspendida. Con la
      // pantalla encendida un doble-tap normal es un gesto legitimo de la UI y no
      // debe significar nada aqui.
      if(gSuspOn){
        if(gTap1Ms && (now - gTap1Ms) >= SUSP_TAP_GAP_MS){ gTap1Ms = 0; suspWake(); }
        else gTap1Ms = now;
      } else gTap1Ms = 0;
      gTap2Ms = 0;
    }
  }
  // Que toques se traga el gesto:
  //  · Suspendido: TODOS. Mientras la pantalla esta apagada, el doble-tap de 1
  //    dedo es lo unico que se lee; nadie mas debe ver el tactil, para que el
  //    toque de despertar no dispare de rebote algo de la UI que hay debajo.
  //  · Despierto: solo los episodios ya confirmados como de 2 dedos, para que el
  //    gesto de suspension no abra una app ni dispare el long-press de ST_CTX.
  //  · Tecleando: NUNCA. Ver el veto de arriba -- si el episodio de 2 dedos es
  //    de alguien escribiendo, T tiene que seguir llegando al teclado entero.
  gSuspSwallow = gSuspOn || (gEpHad2 && !kbTypingNow());
#else
  gSuspSwallow = false;
#endif
}

static void tDoRelease(unsigned long now){
  T.down = false; T.released = true;
  T.dx = T.x - T.startX; T.dy = T.y - T.startY;
  unsigned long dur = now - T.downMs;
  int adx = abs(T.dx), ady = abs(T.dy);
  if(adx < 16 && ady < 16 && dur < 550) T.tap = true;
  else if(ady > 55 && ady >= adx){ if(T.dy < 0) T.swipeUp = true; else T.swipeDown = true; }
  else if(adx > 55 && adx > ady){ if(T.dx < 0) T.swipeLeft = true; else T.swipeRight = true; }
}
static void flexPollTouch(){
  T.pressed = T.released = T.tap = false;
  T.swipeUp = T.swipeDown = T.swipeLeft = T.swipeRight = false;
  uint16_t gx = 0, gy = 0;
  int8_t ev = gtPoll(gx, gy);
  // FASE 4: descarte SILENCIOSO del area excluida, en el punto mas alto del
  // pipeline. Se convierte en "sin dato" (-1), no en "soltado" (0): asi, si el
  // dedo entro arrastrando desde fuera, el gesto muere por el timeout normal de
  // 90 ms que ya existe abajo, y ninguna capa de arriba -ni el framework, ni una
  // app hospedada- llega a ver nunca esas coordenadas.
  if(ev == 1 && kioskTouchBlocked((int)gx, (int)gy)) ev = -1;
  unsigned long now = millis();
  bool wasDown = T.down;
  if(ev == 1){
    T.x = gx; T.y = gy; T.lastMs = now;
    if(!wasDown){ T.down = true; T.pressed = true; T.startX = gx; T.startY = gy; T.downMs = now; T.moved = false; }
    else if(abs((int)gx - T.startX) > 12 || abs((int)gy - T.startY) > 12) T.moved = true;
  } else if(ev == 0){
    if(wasDown) tDoRelease(now);
  } else {
    if(wasDown && now - T.lastMs > 90) tDoRelease(now);
  }
  // NAVEGACION: el episodio tactil heredado de la pantalla anterior se anula
  // aqui, en el mismo punto alto del pipeline que el filtro del kiosco, para
  // que no lo vea NINGUNA capa. El candado se suelta solo cuando el dedo se
  // levanta de verdad (ev != 1 y T.down ya en false).
  if(gTouchSwallow){
    if(T.down || ev == 1){
      T.pressed = T.released = T.tap = false;
      T.swipeUp = T.swipeDown = T.swipeLeft = T.swipeRight = false;
      T.down = false; T.moved = false;
    } else {
      gTouchSwallow = false;
    }
  }
  // SUSPENSION: el detector de doble-tap va AQUI, en el mismo punto alto del
  // pipeline que el filtro del kiosco y por el mismo motivo -- si filtrara mas
  // abajo, alguna capa ya habria visto el toque. Trabaja sobre gtFingers, no
  // sobre T, asi que anular los flags de T de abajo no le afecta.
  suspGestureUpdate();
  // GESTO DE DOS DEDOS (pellizco). Va justo detras del detector de suspension y
  // por el mismo motivo: este es el unico punto donde se puede consumir un
  // contacto ANTES de que ninguna capa lo vea. Depende de que gtPoll() ya haya
  // dejado gtFingers/gtFingersMs al dia en esta misma vuelta, y solo hace
  // trabajo real (una lectura multipunto por I2C) cuando el chip ya ha
  // reportado dos contactos: en uso normal a un dedo no cuesta nada.
  //
  // NO interfiere con el teclado multitactil: gtPollMulti() solo se llama con
  // gState en ST_HOME o ST_HOMECFG (ver hpzAllowedHome/hpzAllowedCfg), y el
  // teclado nunca esta activo ahi. Tampoco con el gesto de suspension, que
  // trabaja sobre gtFingers y ya ha corrido en la linea anterior.
  hpzUpdate();
  if(hpzSwallowing()){
    // Se anulan TODOS los eventos, incluido T.down, exactamente igual que hace
    // gSuspSwallow justo debajo. Anular T.down es lo que impide que el mismo
    // contacto abra una app, arrastre una pagina, active un icono, abra la
    // Caja de aplicaciones o llegue a cualquier otro consumidor: para el resto
    // del sistema el dedo ya no esta apoyado, y todos los temporizadores de
    // pulsacion larga se reinician solos.
    T.pressed = T.released = T.tap = false;
    T.swipeUp = T.swipeDown = T.swipeLeft = T.swipeRight = false;
    T.down = false; T.moved = false;
  }
  if(gSuspSwallow){
    // Se anulan TODOS los eventos, incluido T.down. Anular T.down tambien mata
    // el long-press del menu contextual (ST_CTX), que es justo lo que hace que
    // el gesto de 2 dedos y el long-press no puedan dispararse mutuamente: en
    // cuanto el episodio queda confirmado como de 2 dedos, para el resto del
    // sistema el dedo ya no esta abajo y el temporizador del long-press se
    // reinicia solo. Vale igual para el drag de la cortina de Ajustes rapidos
    // (necesita T.pressed) y para apps con control continuo (leen T.down).
    T.pressed = T.released = T.tap = false;
    T.swipeUp = T.swipeDown = T.swipeLeft = T.swipeRight = false;
    T.down = false; T.moved = false;
  }
}
