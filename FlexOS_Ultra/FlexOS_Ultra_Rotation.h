// #############################################################
// ##  FLEX OS ULTRA  ·  FLEX ROTATION  ·  auto-rotacion del sistema
// ##  ----------------------------------------------------------
// ##  La funcion NATIVA de auto-rotacion. No es una aplicacion: no
// ##  tiene icono, no sale en la Caja de aplicaciones, no abre
// ##  ventana y no tiene pantalla propia. Se enciende y se apaga
// ##  desde el control "Auto-rotacion" del Panel Rapido -- una fila
// ##  mas de QS_REG, igual que el Wi-Fi o el modo oscuro -- y todo
// ##  lo que hace vive aqui.
// ##
// ##  DE DONDE SALE LA ORIENTACION
// ##  ----------------------------------------------------------
// ##      FlexOS_BNO085.cpp        driver SHTP/SH-2 (bus del tactil)
// ##               |
// ##      Flex IMU Service         reparto del sensor + conversiones
// ##               |
// ##      +--------+---------------------------+
// ##      |                                    |
// ##  FLEX ROTATION                      FALL DETECTION
// ##  (este archivo)                     (FlexOS_FallDetect.cpp)
// ##      |                                    |
// ##  orientacion de la UI               Flex Device Care
// ##
// ##  LOS DOS MOTORES SON INDEPENDIENTES. Comparten el sensor a
// ##  traves del conteo de consumidores del servicio (imuAcquire /
// ##  imuRelease), que es justo lo que impide que encender o apagar
// ##  la auto-rotacion arranque o pare el BNO085 por debajo de la
// ##  deteccion de caidas. Aqui NO se llama nunca a flexBnoBegin(),
// ##  flexBnoStop() ni flexBnoRescan(), y NO se toca ni un parametro
// ##  del sensor: una funcion de comodidad no puede degradar una de
// ##  seguridad.
// ##
// ##  Y AL REVES: girar el aparato NO es caerse. Este motor no
// ##  alimenta ni consulta al detector de caidas; lo unico que hace
// ##  con el movimiento brusco es DEJAR DE GIRAR LA PANTALLA
// ##  mientras dura (ver FlexOS_RotationCore.h). Durante una caida la
// ##  deteccion sigue corriendo exactamente igual: mismo sensor,
// ##  misma cadencia, misma logica temporal.
// ##
// ##  QUE SUPERFICIES GIRAN, Y POR QUE NO TODAS
// ##  ----------------------------------------------------------
// ##  El compositor de Flex OS rasteriza sobre un panel VERTICAL de
// ##  480x800 y ya sabe girar 90 grados (gLand): es lo que usan Modo
// ##  PC y Juegos desde siempre. Lo que NO existe es una geometria
// ##  dinamica: el escritorio, el bloqueo, la cortina, la Caja de
// ##  aplicaciones y buena parte de las apps maquetan contra las
// ##  constantes SCR_W / SCR_H. Forzarlas a horizontal daria
// ##  exactamente lo que esta funcion tiene prohibido producir:
// ##  texto cortado, botones encima de otros y tactil desplazado.
// ##
// ##  Por eso Flex Rotation gira SOLO lo que el sistema ya sabe
// ##  maquetar contra un lienzo arbitrario: las apps APP_FLEX, que
// ##  son las que Modo PC ya dibuja 1:1 dentro de ventanas de
// ##  cualquier tamano todos los dias. No hay aqui un segundo motor
// ##  de ventanas ni un compositor nuevo: se reutiliza el MISMO
// ##  camino que dexHostRun/dexHostRelayout (gLand + gAppW/gAppH +
// ##  gRelayout + enter()), que es codigo probado.
// ##
// ##  COMO ENCAJA ESTE ARCHIVO
// ##  ----------------------------------------------------------
// ##  Es una PARTE del sketch FlexOS_Ultra.ino, no una unidad de
// ##  traduccion independiente. Se incluye en la cadena de modulos,
// ##  en su sitio; no lo incluyas por tu cuenta desde otro lado.
// #############################################################
#pragma once
#include "FlexOS_Ultra_IMU.h"        // eslabon anterior de la cadena
#include "FlexOS_RotationCore.h"     // decision de postura (logica pura, probada en el PC)

// #############################################################
// ##  INTERRUPTOR MAESTRO DE COMPILACION
// ##  ----------------------------------------------------------
// ##  Mismo criterio que SUSPEND_ON / KIOSK_ON / POWEROFF_ON: con
// ##  esto a 0 la funcion no existe -- el control no aparece en el
// ##  Panel Rapido, el motor no corre y la interfaz se comporta
// ##  exactamente igual que antes de que esto se escribiera.
// #############################################################
#define FLEXROT_ON 1

// ---- ORIENTACIONES DE LA INTERFAZ -----------------------------------------
// Son las que el compositor SABE pintar, no las que el aparato puede adoptar.
// El nucleo de decision distingue cuatro POSTURAS fisicas; aqui solo hay dos
// orientaciones porque solo hay dos rasterizaciones reales.
//
// POR QUE NO HAY "LANDSCAPE IZQUIERDA" Y "LANDSCAPE DERECHA"
// ---------------------------------------------------------
// La rotacion del motor es UNA transformacion fija:
//     putPhys(lx,ly) -> x = (SCR_W-1) - ly ,  y = lx
// y esa misma expresion esta replicada, literal, en el escalador y el tactil
// de Modo PC (FlexOS_Ultra_DeXDraw/DeXInput), en el rasterizador de Jumper
// (Juegos), en el volcado girado del reproductor, en el aviso de caida de
// Device Care y en el puente de las apps flex-app-v1. Anadir la
// transformacion espejo obligaria a tocar los seis sitios -- incluidos
// Juegos, Modo PC y una pantalla de SEGURIDAD -- para ganar una sola
// direccion de giro. La prioridad de esta funcion es al reves: estabilidad
// primero. Asi que la postura horizontal OPUESTA se detecta, se reconoce y
// NO se aplica: la interfaz se queda como esta en vez de pintarse del reves.
#define FROT_ORI_PORTRAIT  0
#define FROT_ORI_LAND      1     // el borde DERECHO del panel arriba (FROT_POS_LAND_A)

// ---- ESTADO DE LA FUNCION -------------------------------------------------
// Excluyentes a proposito: no existe "encendida y sin IMU". Si el sensor no
// esta, el estado es NO_IMU y el control del panel se ve en OFF.
enum {
  FROT_ST_OFF = 0,     // el usuario la tiene apagada
  FROT_ST_PROBING,     // se pidio encenderla: sondeando si hay IMU compatible
  FROT_ST_ON,          // encendida y con sensor detras
  FROT_ST_NO_IMU       // se intento encender (o se perdio el sensor) y no hay IMU
};

// ---- PARAMETROS DEL MOTOR (los del nucleo van en FlexOS_RotationCore.h) ----
#define FROT_PROBE_MS     3200   // espera maxima a que el driver identifique el modulo
#define FROT_TIMEOUT_MS   1200   // ROTATION_TIMEOUT: una transicion no puede durar mas
#define FROT_MIN_GAP_MS    450   // separacion minima entre dos transiciones
#define FROT_FAIL_MAX        3   // fallos seguidos tras los que se deja de intentar
#define FROT_NOTICE_MS    3400   // cuanto se ve el aviso "requiere modulo IMU"

// ---- DECLARACIONES ADELANTADAS --------------------------------------------
// El aviso global de posible caida (Flex Device Care) vive DESPUES de este
// modulo en la cadena, pero la politica de rotacion tiene que preguntarle si
// esta a la vista: mientras hay un aviso de caida delante del usuario, el
// suelo no se le mueve. Se declara a mano, igual que hace el Panel Rapido con
// todo lo que su registro necesita y esta mas abajo en el archivo.
static bool faVisible();

// ---- ESTADO VIVO ----------------------------------------------------------
static uint8_t  gRotState    = FROT_ST_OFF;
// DOS VARIABLES, NO UNA. gRotOri es lo que esta PINTADO; gRotWant es lo que el
// sistema quiere que se pinte. Separarlas es lo que permite que una peticion
// sobreviva a no poder atenderse en el momento: apagar la auto-rotacion con la
// cortina abierta, o girar el aparato mientras hay un juego delante, deja una
// intencion pendiente que se aplica sola en cuanto hay una superficie que
// pueda recibirla. Con una sola variable esa intencion se perdia.
static uint8_t  gRotOri      = FROT_ORI_PORTRAIT;  // orientacion APLICADA
static uint8_t  gRotWant     = FROT_ORI_PORTRAIT;  // orientacion PEDIDA
static bool     gRotHold     = false;              // el servicio IMU esta adquirido por nosotros
static bool     gRotBusy     = false;              // ROTATION_TRANSITION: transicion en curso
static uint32_t gRotT0       = 0;                  // millis del inicio de la transicion
static uint32_t gRotLastMs   = 0;                  // millis de la ultima transicion terminada
static uint32_t gRotProbeMs  = 0;                  // millis en que empezo el sondeo
static uint8_t  gRotFails    = 0;                  // ROTATION_FAILED: transiciones abortadas seguidas
static FrotStab gRotStab;                          // maquina de estabilidad (nucleo puro)
static uint8_t  gRotAppOri[APP_N];                 // con que orientacion se maqueto cada app
static int      gRotPendNav  = 0;                  // 0 nada · 1 atras · 2 inicio · 3 recientes
static uint32_t gRotNoticeMs = 0;                  // aviso "requiere IMU" a la vista desde...
static uint8_t  gRotNoticeKind = 0;                // 0 sin IMU · 1 sensor perdido

// #############################################################
// ##  POLITICA: QUE SUPERFICIE PUEDE GIRAR
// ##  ----------------------------------------------------------
// ##  Una sola funcion, consultada desde todos lados. No hay una
// ##  segunda lista de excepciones repartida por el sistema.
// #############################################################
static bool rotSurfaceAllows(){
#if !FLEXROT_ON
  return false;
#else
  if(gState != ST_APP) return false;          // Inicio, bloqueo, Recientes, Wi-Fi, Archivos... : vertical
  if(gHosted) return false;                   // dentro de una ventana de Modo PC: manda DeX
  if(gAppId < 0 || gAppId >= APP_N) return false;
  if(gAppId == IC_MODOPC) return false;       // MODO PC (DeX) EXCLUIDO: tiene su propia gestion
  const FlexApp* a = &APP_REG[gAppId];
  if(a->flags & APP_LAND) return false;       // JUEGOS EXCLUIDOS: conservan su orientacion
  // La condicion de fondo: solo gira lo que maqueta contra el lienzo REAL.
  // Es la misma propiedad que hace que una app se vea bien dentro de una
  // ventana de Modo PC, y la garantiza el propio registro de apps.
  if(!(a->flags & APP_FLEX)) return false;
  // FLEX STORE, FUERA. No por su maqueta -- es APP_FLEX y Modo PC la hospeda
  // sin problemas -- sino porque dentro de ella puede estar corriendo una app
  // flex-app-v1, que gestiona su PROPIA orientacion (av1SetLandscape escribe
  // gLand por su cuenta) y que storeEnter() cierra al re-entrar. Girar ahi
  // cerraria la app del usuario y dejaria dos duenos de la rotacion.
  if(gAppId == IC_FLEXSTORE) return false;
  if(KIOSK_ON && kioskOn) return false;       // modo kiosco: geometria clavada
  if(gSafeMode || gFrPending) return false;
  if(appTrVisible()) return false;            // hay una transicion de app dibujando
  if(qsPanelY > 0 || qsAnimOn) return false;  // la cortina posee la pantalla
  if(gSuspOn) return false;                   // pantalla apagada
  if(faVisible()) return false;               // aviso de caida modal: no se le mueve el suelo
  if(flexOtaOwnsScreen() || optActive()) return false;
  return true;
#endif
}

// ORIENTACION EFECTIVA DE LO QUE SE ESTA PINTANDO.
// Se calcula, no se cachea: asi no puede quedarse obsoleta entre que el
// usuario pulsa "inicio" y el escritorio se dibuja. El caso comun -- sistema
// en vertical -- sale en la primera comparacion.
static bool rotApplied(){
  if(gRotOri != FROT_ORI_LAND) return false;
  return rotSurfaceAllows();
}
// Fondo del area de ventana cuando manda la rotacion. En horizontal el lienzo
// mide LH de alto y la barra de navegacion, si la hay, vive DENTRO de el.
static int rotWinBot(){
  int bot = LH - ((gNavMode == 0) ? NAV_H : 0);
  return bot < 64 ? 64 : bot;
}

// #############################################################
// ##  ALCANCE ROTADO
// ##  ----------------------------------------------------------
// ##  Mismo patron EXACTO que dexHostRun: se desvia el estado global
// ##  del motor (rotacion, lienzo logico y tactil), se ejecuta la
// ##  app, y se restaura pase lo que pase. Fuera del alcance el
// ##  sistema sigue siendo vertical, asi que ninguna capa que dibuje
// ##  por su cuenta (la isla, el candado del kiosco, el OTA) ve un
// ##  estado a medias.
// #############################################################
typedef struct { bool land; bool clipLog; int aw, ah; Touch t; } RotScope;
static RotScope gRotSc;
static int      gRotScDepth = 0;

// FISICO -> LOGICO. Es el mismo mapeo que usan Modo PC (dexPointer), el
// reproductor y el aviso de caida; no hay aqui una segunda convencion.
static inline void rotPhysToLogical(int px, int py, int &lx, int &ly){
  lx = py;
  ly = (SCR_W - 1) - px;
}
static void rotRemapTouch(Touch &t, const Touch &src){
  rotPhysToLogical(src.x, src.y, t.x, t.y);
  rotPhysToLogical(src.startX, src.startY, t.startX, t.startY);
  t.dx =  src.dy;
  t.dy = -src.dx;
  // Los deslizamientos tambien giran: un barrido fisico hacia abajo es, en el
  // lienzo horizontal, un barrido hacia la DERECHA. Sin esto el gesto de
  // "atras" de una app se activaria con el dedo en la direccion equivocada.
  t.swipeRight = src.swipeDown;
  t.swipeLeft  = src.swipeUp;
  t.swipeUp    = src.swipeRight;
  t.swipeDown  = src.swipeLeft;
}
// Devuelve al tactil FISICO los consumos que hizo la app (poner tap/swipe a
// false es como una capa dice "este toque es mio"). Se propagan los flags, no
// las coordenadas: las fisicas no las ha tocado nadie.
static void rotUnmapTouchFlags(Touch &dst, const Touch &logical){
  dst.down = logical.down; dst.pressed = logical.pressed;
  dst.released = logical.released; dst.tap = logical.tap; dst.moved = logical.moved;
  dst.swipeDown  = logical.swipeRight;
  dst.swipeUp    = logical.swipeLeft;
  dst.swipeRight = logical.swipeUp;
  dst.swipeLeft  = logical.swipeDown;
}

// UN SOLO DUENO. Devuelve true si ESTE llamante abrio el alcance, y solo el
// puede cerrarlo. Quien reciba false no abrio nada y no tiene nada que
// restaurar: asi la pareja begin/end no se puede desequilibrar por un camino
// que salga antes de tiempo, que es como se quedan los estados globales a
// medias. La anidacion no se permite -- no hay ninguna razon para rotar
// dentro de una rotacion -- y si llega a darse, la valida rotValidate() y la
// transicion se deshace en vez de dejar el motor girado.
static bool rotScopeBegin(){
  if(gRotOri != FROT_ORI_LAND) return false;
  if(gRotScDepth > 0) return false;                     // ya estabamos dentro
  gRotScDepth = 1;
  gRotSc.land = gLand; gRotSc.clipLog = gClipLogical;
  gRotSc.aw = gAppW; gRotSc.ah = gAppH; gRotSc.t = T;
  gLand = true;
  gClipLogical = true;      // recortes y bandas pasan a ser de la MAQUETA (ver Gfx)
  gAppW = LW; gAppH = LH;
  Touch lt = T;
  rotRemapTouch(lt, gRotSc.t);
  T = lt;
  return true;
}
static void rotScopeEnd(bool owned){
  if(!owned) return;
  gRotScDepth = 0;
  Touch phys = gRotSc.t;
  rotUnmapTouchFlags(phys, T);
  T = phys;
  gLand = gRotSc.land; gClipLogical = gRotSc.clipLog;
  gAppW = gRotSc.aw; gAppH = gRotSc.ah;
}

// SALIDA DE EMERGENCIA DEL ALCANCE.
//
// Las salidas del sistema que pasan por la barra de navegacion o por el
// chevron ya se aparcan (ver rotDeferNav) y se ejecutan fuera del alcance.
// Pero una app puede navegar DESDE DENTRO de su propio tick -- un gesto de
// cierre, abrir otra app desde un enlace --, y entonces lo que venga despues
// (el escritorio, el selector, otra app) se compone en VERTICAL con el motor
// todavia girado: el escritorio se dibujaria ROTADO dentro de homeBuf, y ese
// fallo sobrevive a la navegacion que lo provoco, porque homeBuf se reutiliza.
//
// Esto NO cierra el alcance: solo devuelve el motor a vertical. El
// rotScopeEnd() del llamante original sigue siendo quien restaura el tactil y
// el lienzo, y restaurara exactamente estos mismos valores verticales -- eran
// los que habia antes de abrirlo.
static void rotScopeSuspendForSystem(){
  if(gRotScDepth <= 0) return;
  gLand = false; gClipLogical = false;
  gAppW = SCR_W; gAppH = SCR_H;
}

// NAVEGACION APARCADA. sysBack/sysHome/sysRecents repintan el escritorio, el
// selector o una pantalla del sistema -- todo ello VERTICAL --, asi que no
// pueden ejecutarse con el motor girado. navBarHandle() los deriva aqui
// mientras el alcance esta abierto y rotAppTick() los despacha ya fuera.
// Fuera del alcance devuelve false y el sistema se comporta EXACTAMENTE como
// siempre: esta es la unica linea de la barra de navegacion que cambia.
static bool rotDeferNav(int action){
  if(gRotScDepth <= 0) return false;
  if(gRotPendNav == 0) gRotPendNav = action;
  return true;
}

// #############################################################
// ##  TRANSICION DE ORIENTACION  ·  atomica, validada y reversible
// #############################################################

// Reconstruye la superficie de primer plano con la geometria ACTUAL.
// Es el mismo camino que dexHostRelayout: enter() con gRelayout, o sea
// "re-dibuja con el lienzo nuevo", nunca "re-inicializa".
static bool rotRebuildForeground(){
  int id = gAppId;
  if(gState != ST_APP || id < 0 || id >= APP_N) return false;
  if(!APP_REG[id].enter) return false;

  bool owned = rotScopeBegin();
  setBuf(fb);
  // Recorte abierto de par en par ANTES de limpiar: gAppH-1 en la maqueta
  // girada, SCR_H-1 en vertical. Un recorte heredado de la orientacion
  // anterior dejaria sin borrar justo la franja que hay que borrar.
  gClipX0 = 0; gClipX1 = SCR_W - 1;
  gClipY0 = 0; gClipY1 = (gRotOri == FROT_ORI_LAND) ? (LH - 1) : (SCR_H - 1);
  // SUPERFICIE LIMPIA. En coordenadas LOGICAS el lienzo cubre el panel entero
  // en las dos orientaciones, asi que este unico relleno borra TODO lo de la
  // orientacion anterior: ni un texto, ni un boton, ni una tarjeta puede
  // sobrevivir para apilarse debajo del layout nuevo.
  fillRect(0, 0, gAppW, gAppH, WIN_BG);
  bool wasRelayout = gRelayout;
  gRelayout = true;
  if(!(APP_REG[id].flags & APP_CUSTOM_HEADER)){
    appDrawChrome(id);
    appDrawHeader(id);
  }
  APP_REG[id].enter();
  gRelayout = wasRelayout;
  rotScopeEnd(owned);

  gRotAppOri[id] = gRotOri;
  // UN solo volcado al panel por transicion. Nada de borrar y repintar en
  // cadena: el usuario ve el layout nuevo, no el proceso.
  flxFlushAll();
  return true;
}

// VALIDACION DE LA TRANSICION. Contesta a "¿la interfaz quedo en un estado
// coherente?", que es distinto de "¿el codigo no se rompio?". Si cualquiera de
// estas invariantes no se cumple, lo que hay en pantalla no se puede confiar y
// se vuelve atras.
static bool rotValidate(){
  if(gLand || gClipLogical) return false;               // el alcance no se cerro
  if(gRotScDepth != 0) return false;                    // ...ni se equilibro
  if(gAppW != SCR_W || gAppH != SCR_H) return false;    // el lienzo global no volvio
  if(!fb || !bbuf) return false;                        // sin framebuffer no hay nada valido
  if(gState != ST_APP) return false;                    // la app navego fuera durante el redibujo
  if(gRotOri == FROT_ORI_LAND && !rotSurfaceAllows()) return false;
  if((uint32_t)(millis() - gRotT0) > FROT_TIMEOUT_MS) return false;   // ROTATION_TIMEOUT
  return true;
}

// APLICA UNA ORIENTACION. Devuelve true si quedo aplicada y validada.
// Es el UNICO sitio que escribe gRotOri con la pantalla delante.
static bool rotApplyOrientation(uint8_t want){
  if(gRotBusy) return false;                            // no se anidan transiciones
  if(want != FROT_ORI_PORTRAIT && want != FROT_ORI_LAND) return false;
  if(want == gRotOri) return true;
  if(gRotFails >= FROT_FAIL_MAX) return false;          // se dejo de intentar: ver rotClearFailures

  uint8_t prev = gRotOri;
  gRotBusy = true;
  gRotT0   = millis();

  gRotOri = want;
  bool ok = rotRebuildForeground() && rotValidate();

  if(!ok){
    // ROLLBACK. Se restaura la orientacion anterior y se recompone con ella:
    // el usuario nunca se queda dentro de una interfaz a medio girar.
    gRotOri = prev;
    gRotScDepth = 0;                                    // por si el alcance quedo descuadrado
    gLand = false; gClipLogical = false; gAppW = SCR_W; gAppH = SCR_H;
    gRotT0 = millis();                                  // el rollback tiene su propio plazo
    (void)rotRebuildForeground();
    if(gRotFails < 255) gRotFails++;
    gRotBusy = false;
    gRotLastMs = millis();
    Serial.println(F("[ROT] transicion abortada; se restauro la orientacion anterior"));
    return false;
  }
  gRotFails  = 0;
  gRotBusy   = false;
  gRotLastMs = millis();
  return true;
}

// La cuenta de fallos se limpia cuando cambia la superficie: un layout que no
// valido en una app no puede condenar a las demas para siempre.
static void rotClearFailures(){ gRotFails = 0; }

// WATCHDOG DE ROTACION. rotApplyOrientation es sincrona, asi que el unico
// modo de que gRotBusy sobreviva a la llamada es que algo saliera por un
// camino imprevisto. Este tick lo suelta y deja el motor coherente en vez de
// bloquear la rotacion -- y el tactil -- para siempre.
//
// LIMITE HONESTO: si un enter() se quedara colgado DENTRO de la transicion,
// esto no lo puede interrumpir (corre en el mismo hilo). De eso se encarga el
// TWDT del sistema, como con cualquier otro bucle que no vuelve.
static void rotWatchdogTick(){
  if(!gRotBusy) return;
  if((uint32_t)(millis() - gRotT0) <= FROT_TIMEOUT_MS) return;
  gRotBusy = false;
  gRotScDepth = 0;
  gLand = false; gClipLogical = false; gAppW = SCR_W; gAppH = SCR_H;
  if(gRotFails < 255) gRotFails++;
  Serial.println(F("[ROT] watchdog: transicion sin terminar; motor liberado"));
}

// #############################################################
// ##  ENTRADA / TICK DE UNA APP ROTADA
// #############################################################

// Llamada desde appTrFinishOpen, justo antes de componer la app. Si el
// sistema esta en horizontal y la app puede girar, abre el alcance para que
// la cabecera y el enter() se dibujen YA con la geometria buena -- sin pintar
// primero en vertical para corregirlo despues.
// Ademas veta un resume() cuya maquetacion pertenezca a la OTRA orientacion:
// repintar un estado logico maquetado a 480x800 dentro de 800x480 es
// exactamente como se apilan los elementos.
static bool rotEnterSurface(int id, bool &resuming, bool &relayoutSaved){
  relayoutSaved = gRelayout;
  if(id < 0 || id >= APP_N) return false;
  const uint8_t want = rotApplied() ? FROT_ORI_LAND : FROT_ORI_PORTRAIT;
  // El veto vale en LAS DOS DIRECCIONES. Volver a una app que quedo maquetada
  // en horizontal estando ahora en vertical es tan malo como lo contrario: el
  // estado logico se conserva, pero las coordenadas con las que se dibujo ya
  // no describen el lienzo que hay.
  if(resuming && gRotAppOri[id] != want){
    resuming  = false;        // se re-maqueta...
    gRelayout = true;         // ...pero NO se re-inicializa (la nota, el lienzo y el scroll siguen)
  }
  gRotAppOri[id] = want;
  return (want == FROT_ORI_LAND) ? rotScopeBegin() : false;
}
static void rotLeaveSurface(bool owned, bool relayoutSaved){
  rotScopeEnd(owned);
  gRelayout = relayoutSaved;
}

// TICK DE UNA APP ROTADA. Reparte el toque igual que appTick, pero con el
// tactil ya traducido, y deja las salidas del sistema para el final -- fuera
// del alcance -- porque el escritorio, Recientes y las pantallas del sistema
// se componen SIEMPRE en vertical.
static void rotAppTick(){
  int id = gAppId;
  if(id < 0 || id >= APP_N) return;

  // GESTOS iOS: se atienden con el tactil FISICO, a proposito. El gesto vive
  // en el borde de la PLACA (la franja de abajo del panel), no en el borde de
  // la maqueta, asi que traducirlo lo moveria de sitio en horizontal.
  if(gNavMode == 1 && handleiOSGestures()) return;

  gRotPendNav = 0;
  bool owned = rotScopeBegin();
  bool consumed = navBarHandle();                 // franja del sistema, ya en coords logicas
  if(!consumed && T.tap && !(APP_REG[id].flags & APP_CUSTOM_HEADER) &&
     T.y <= WIN_TOP && T.x < 72){
    consumed = true;
    gRotPendNav = 1;                              // chevron "atras" de la cabecera estandar
  }
  if(!consumed && APP_REG[id].tick) APP_REG[id].tick();
  rotScopeEnd(owned);

  // Ya en vertical: ahora si se puede navegar.
  int nav = gRotPendNav; gRotPendNav = 0;
  if(nav == 1)      sysBack();
  else if(nav == 2) sysHome();
  else if(nav == 3) sysRecents();
}

// #############################################################
// ##  EL SENSOR  ·  adquisicion, sondeo y perdida
// #############################################################

// ¿Hay un IMU compatible AHORA MISMO? No se contesta "puede que si": mientras
// el driver esta identificando el modulo la respuesta es "todavia no se sabe".
static bool rotImuPresent(){
  uint8_t s = imuState();
  return s == FIMU_CONNECTED || s == FIMU_AHRS_ACTIVE;
}
static bool rotImuAbsent(){
  uint8_t s = imuState();
  return s == FIMU_NO_IMU || s == FIMU_ERROR || s == FIMU_DISCONNECTED;
}
static void rotHoldImu(bool on){
  if(on == gRotHold) return;
  gRotHold = on;
  if(on) imuAcquire();       // 0->1 arranca el sensor; n->n+1 no toca nada
  else   imuRelease();       // ...y soltar NO lo apaga si Device Care lo sigue usando
}

// #############################################################
// ##  AVISO "REQUIERE UN MODULO IMU"
// ##  ----------------------------------------------------------
// ##  Se compone sobre la cortina ya pintada y se retira solo, con
// ##  el mismo patron que safeDenyApp(). No abre pantalla, no cambia
// ##  gState y no deja nada reservado.
// #############################################################
// Solo se abre si hay donde ensenarlo: el aviso vive DENTRO del cuadro de la
// cortina. Sin cortina delante, quien informa es el subtitulo del propio
// control (qpSubRotate), que es donde el usuario va a mirar.
static void rotNoticeOpen(uint8_t kind){
  if(qsPanelY < SCR_H) return;
  gRotNoticeKind = kind;
  gRotNoticeMs   = millis();
}
static inline bool rotNoticeVisible(){ return gRotNoticeMs != 0; }
static void rotNoticeClose(){
  if(!gRotNoticeMs) return;
  gRotNoticeMs = 0;
  qpInvalidateAll();                 // la cortina se recompone entera: no queda ni un pixel del aviso
}
// Dibuja el aviso DENTRO del cuadro de la cortina (lo llama qsTick justo
// despues de qsRender, que es el unico punto de render del panel).
static void rotNoticeDraw(){
#if FLEXROT_ON
  if(!gRotNoticeMs) return;
  if(qsPanelY < SCR_H){ rotNoticeClose(); return; }      // la cortina se cerro: el aviso se va con ella
  if((uint32_t)(millis() - gRotNoticeMs) >= FROT_NOTICE_MS){ rotNoticeClose(); return; }

  const int h  = 148;
  const int y0 = (SCR_H - h) / 2;
  uint16_t* ob = gBuf; bool wl = gLand;
  int cx0 = gClipX0, cx1 = gClipX1, cy0 = gClipY0, cy1 = gClipY1;
  gLand = false;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = y0; gClipY1 = y0 + h - 1;
  setBuf(fb);

  fillRoundRectA(24, y0, SCR_W - 48, h, 24, gDark ? rgb565(16,18,26) : rgb565(250,251,255), 246);
  drawRoundRect(24, y0, SCR_W - 48, h, 24, TH_BORDER);
  if(gRotNoticeKind == 0){
    drawTextC(SCR_W / 2, y0 + 26, "Auto-rotaci\xC3\xB3n requiere", 3, TH_TXT);
    drawTextC(SCR_W / 2, y0 + 54, "un m\xC3\xB3" "dulo IMU", 3, TH_TXT);
    drawTextC(SCR_W / 2, y0 + 92, "Conecta un m\xC3\xB3" "dulo TENSTAR BNO085", 1, TH_TXT2);
    drawTextC(SCR_W / 2, y0 + 112, "para utilizar esta funci\xC3\xB3n.", 1, TH_TXT2);
  }else{
    drawTextC(SCR_W / 2, y0 + 26, "Se perdi\xC3\xB3 el m\xC3\xB3" "dulo IMU", 3, TH_TXT);
    drawTextC(SCR_W / 2, y0 + 62, "Auto-rotaci\xC3\xB3n se ha desactivado.", 1, TH_TXT2);
    drawTextC(SCR_W / 2, y0 + 82, "El resto de Flex OS sigue funcionando", 1, TH_TXT2);
    drawTextC(SCR_W / 2, y0 + 102, "y la orientaci\xC3\xB3n actual se conserva.", 1, TH_TXT2);
  }
  flxFlush(y0, y0 + h - 1);

  setBuf(ob); gLand = wl;
  gClipX0 = cx0; gClipX1 = cx1; gClipY0 = cy0; gClipY1 = cy1;
#endif
}
// El aviso es MODAL mientras se ve: un toque lo cierra y NO llega a los
// controles que tiene debajo. Lo consulta qpPanelTouch en su primera linea.
static bool rotNoticeTouch(){
#if FLEXROT_ON
  if(!gRotNoticeMs) return false;
  if(T.tap || T.pressed || T.released){
    T.tap = T.pressed = T.released = false;
    rotNoticeClose();
  }
  return true;
#else
  return false;
#endif
}

// #############################################################
// ##  PERSISTENCIA  ·  espacio de nombres PROPIO
// ##  ----------------------------------------------------------
// ##  Mismo criterio que el Panel Rapido ("flexqs") y Device Care
// ##  ("flexcare"): una clave nueva no se cuela en el blob de
// ##  preferencias del sistema, asi que no puede corromper nada de
// ##  lo que ya habia guardado.
// #############################################################
#define FROT_NVS_NS  "flexrot"
static void rotSavePrefs(){
  Preferences p;
  if(!p.begin(FROT_NVS_NS, false)) return;
  p.putBool("auto", gRotState == FROT_ST_ON || gRotState == FROT_ST_PROBING);
  p.end();
}
static bool rotLoadWanted(){
  Preferences p;
  if(!p.begin(FROT_NVS_NS, true)) return false;
  bool w = p.getBool("auto", false);
  p.end();
  return w;
}

// #############################################################
// ##  ENCENDIDO / APAGADO
// #############################################################
// CONVERGENCIA. Un unico sitio lleva lo pintado hacia lo pedido, y solo
// cuando se puede hacer sin romper nada. Corre SIEMPRE -- tambien con la
// funcion apagada -- porque apagarla con la cortina abierta deja justo eso:
// una vuelta a vertical pendiente de que haya una superficie que repintar.
static void rotConverge(){
  if(gRotWant == gRotOri) return;
  if(gRotBusy) return;
  // Con la funcion apagada o sin sensor, lo unico que se admite es VOLVER a
  // vertical (la peticion que dejo pendiente apagarla). Girar hacia horizontal
  // exige un motor vivo detras: sin el, la postura que hubiera guardada ya no
  // describe nada.
  if(gRotState != FROT_ST_ON && gRotWant != FROT_ORI_PORTRAIT) return;
  if(!rotSurfaceAllows()) return;                     // Inicio, juego, Modo PC, cortina, aviso de caida...
  if(frotStabMotion(&gRotStab)) return;               // MOTION_EVENT_ACTIVE: la UI no se mueve
  if(gRotLastMs && (uint32_t)(millis() - gRotLastMs) < FROT_MIN_GAP_MS) return;
  (void)rotApplyOrientation(gRotWant);
}

static void rotDisable(bool save){
  gRotState = FROT_ST_OFF;
  frotStabReset(&gRotStab);
  rotHoldImu(false);
  gRotWant = FROT_ORI_PORTRAIT;   // se aplica en cuanto haya donde (ver rotConverge)
  rotClearFailures();
  if(save) rotSavePrefs();
}

// Lo llama el control del Panel Rapido. NUNCA enciende sin sensor real.
static void rotToggleRequest(){
#if FLEXROT_ON
  if(gRotState == FROT_ST_ON || gRotState == FROT_ST_PROBING){
    rotDisable(true);
    return;
  }
  // Apagada (o NO_IMU): se intenta encender.
  rotHoldImu(true);                       // adquiere el servicio: si ya lo tenia Device Care, no toca el sensor
  imuServiceTick();                       // una vuelta inmediata para no esperar al siguiente loop
  if(rotImuPresent()){
    gRotState = FROT_ST_ON;
    frotStabReset(&gRotStab);
    rotClearFailures();
    rotSavePrefs();
    return;
  }
  if(rotImuAbsent()){
    rotHoldImu(false);
    gRotState = FROT_ST_NO_IMU;
    rotNoticeOpen(0);                     // el control se queda en OFF
    rotSavePrefs();
    return;
  }
  // El driver esta identificando el modulo: ni se afirma que hay IMU ni se
  // afirma que no. El sondeo termina en rotEngineTick, como mucho en
  // FROT_PROBE_MS, y de ahi sale ON o NO_IMU -- nunca un ON a medias.
  gRotState   = FROT_ST_PROBING;
  gRotProbeMs = millis();
#endif
}

// #############################################################
// ##  EL TICK DEL MOTOR  ·  una vuelta de loop()
// ##  ----------------------------------------------------------
// ##  Corre DESPUES de dcSensorTick(): la deteccion de caidas se
// ##  queda siempre con la muestra primero. Esta funcion no puede
// ##  retrasarla ni consumirla -- las lecturas del driver no son
// ##  destructivas, pero el orden deja escrito quien tiene prioridad.
// #############################################################
static void rotEngineTick(){
#if FLEXROT_ON
  rotWatchdogTick();
  rotConverge();          // atiende lo que quedara pendiente, tambien con la funcion apagada

  // Sondeo en curso tras pulsar el control.
  if(gRotState == FROT_ST_PROBING){
    if(rotImuPresent()){
      gRotState = FROT_ST_ON;
      frotStabReset(&gRotStab);
      rotClearFailures();
      rotSavePrefs();
    }else if(rotImuAbsent() || (uint32_t)(millis() - gRotProbeMs) > FROT_PROBE_MS){
      rotHoldImu(false);
      gRotState = FROT_ST_NO_IMU;
      rotNoticeOpen(0);
      rotSavePrefs();
    }
    return;
  }
  if(gRotState != FROT_ST_ON) return;

  // PERDIDA DEL SENSOR EN CALIENTE. No se reinicia nada, no se bloquea la
  // pantalla y no se cierra ninguna app: se deja de girar, se CONSERVA la
  // orientacion actual y el control pasa a "no disponible".
  if(rotImuAbsent()){
    gRotState = FROT_ST_NO_IMU;
    frotStabReset(&gRotStab);
    rotHoldImu(false);
    // SE CONGELA LA INTENCION. Una rotacion que estuviera pendiente de
    // aplicarse saldria de una postura que ya nadie esta midiendo: la
    // orientacion que hay en pantalla es la que se queda.
    gRotWant = gRotOri;
    rotNoticeOpen(1);
    // La preferencia NO se toca. El usuario quiere auto-rotacion; lo que falta
    // es el modulo. Si vuelve a conectarlo y reinicia, la funcion vuelve sola;
    // y si en ese arranque sigue sin haber modulo, el sondeo la apaga entonces
    // (ver FROT_ST_PROBING), que es cuando de verdad se sabe.
    Serial.println(F("[ROT] IMU no disponible; auto-rotacion desactivada (la UI no se toca)"));
    return;
  }

  // Sin vector de rotacion todavia (enlace listo pero el sensor fusion aun no
  // publica): no se inventa una postura.
  float tilt = 0.0f, dotN = 1.0f;
  if(!imuScreenTilt(&tilt, &dotN)) return;

  float a[3], g[3];
  float accMag = 1.0f, gyrMag = 0.0f;
  if(flexBnoAccel(a)) accMag = flexFallMagG(a[0], a[1], a[2]);
  if(flexBnoGyro(g))  gyrMag = sqrtf(g[0]*g[0] + g[1]*g[1] + g[2]*g[2]);

  // El nucleo puro decide cual es la postura ESTABLE. Todo el filtrado
  // (planitud, evento de movimiento, histeresis y permanencia) vive alli.
  (void)frotStabFeed(&gRotStab, millis(), tilt, dotN, accMag, gyrMag);

  // POSTURA -> INTENCION. Solo hay dos rasterizaciones reales, asi que la
  // horizontal OPUESTA y la vertical invertida no cambian nada: se conserva lo
  // que hay en pantalla en vez de pintarlo del reves.
  //
  // La intencion se deriva de la postura CONFIRMADA en cada vuelta, no del
  // instante en que el nucleo la confirmo. Es lo que hace que la orientacion
  // se ponga al dia sola al salir de un juego, de Modo PC o del escritorio:
  // ahi el nucleo no confirma nada nuevo -- el aparato lleva rato quieto --
  // pero la postura sigue siendo la que es.
  int pos = frotStabPos(&gRotStab);
  if(pos == FROT_POS_PORTRAIT)    gRotWant = FROT_ORI_PORTRAIT;
  else if(pos == FROT_POS_LAND_A) gRotWant = FROT_ORI_LAND;

  if(!rotSurfaceAllows()){
    // Juego, Modo PC, escritorio, cortina, aviso de caida... Se sigue
    // MIDIENDO (para no llegar en frio al volver) pero no se pinta nada: la
    // superficie que hay delante se dibuja ella sola, y rotApplied() ya
    // devuelve false, asi que la geometria que ve es la vertical.
    rotClearFailures();
    return;
  }
  rotConverge();
#endif
}

// Arranque. Se llama desde setup(), junto a dcBegin(): la auto-rotacion es
// una preferencia del usuario y tiene que sobrevivir al reinicio.
static void rotBegin(){
  frotStabInit(&gRotStab);
  for(int i = 0; i < APP_N; i++) gRotAppOri[i] = FROT_ORI_PORTRAIT;
#if FLEXROT_ON
  if(!rotLoadWanted()) return;
  // Encendida en la sesion anterior: se vuelve a sondear. Si el modulo ya no
  // esta, el estado queda en NO_IMU y el control se ve en OFF -- nunca en ON
  // sin hardware detras.
  rotHoldImu(true);
  gRotState   = FROT_ST_PROBING;
  gRotProbeMs = millis();
#endif
}

// #############################################################
// ##  CONTROL DEL PANEL RAPIDO
// ##  ----------------------------------------------------------
// ##  Cuatro funciones y ni una linea mas: el dibujo, el hit-test,
// ##  el catalogo, el editor y la persistencia del panel ya salen
// ##  todos de QS_REG. Auto-rotacion es una fila mas de esa tabla,
// ##  con el mismo aspecto y el mismo comportamiento que los demas
// ##  interruptores.
// #############################################################
static bool qpAvRotate(){
#if FLEXROT_ON
  return true;      // el control existe SIEMPRE: sin IMU lo que hace es explicarlo
#else
  return false;
#endif
}
// ESTADO REAL. ON solo cuando la funcion esta de verdad activa: ni el sondeo
// ni "no hay IMU" pintan el interruptor encendido.
static bool qpStRotate(){ return gRotState == FROT_ST_ON; }
static void qpTapRotate(){ rotToggleRequest(); }
static void qpSubRotate(char* o, size_t n){
  if(!o || !n) return;
  switch(gRotState){
    case FROT_ST_ON:
      snprintf(o, n, "%s", gRotOri == FROT_ORI_LAND ? "Horizontal" : "Vertical");
      break;
    case FROT_ST_PROBING: snprintf(o, n, "%s", "Detectando IMU\xE2\x80\xA6"); break;
    case FROT_ST_NO_IMU:  snprintf(o, n, "%s", "Requiere m\xC3\xB3" "dulo IMU"); break;
    default:              snprintf(o, n, "%s", "Desactivada"); break;
  }
}
