// #############################################################
// ##  FLEX OS ULTRA  ·  PREFERENCIAS EN NVS, RELOJ INTERNO E IDIOMAS
// ##  ----------------------------------------------------------
// ##  Lectura y escritura de la configuracion en NVS (namespace flexos),
// ##  el idioma de la interfaz y todas las preferencias del teclado.
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
#include "FlexOS_Ultra_Touch.h"   // eslabon anterior de la cadena

// #############################################################
// ##  ALMACENAMIENTO (NVS)  ·  reloj interno  ·  idiomas
// #############################################################
static Preferences prefs;
static bool  cfgOobeDone = false;
static int   cfgLang     = 0;                 // 0=ES 1=EN 2=FR 3=PT 4=IT 5=ZH
static bool  g24h        = false;             // formato de hora 24h
static int   gLockType   = 0;                 // 0 ninguno, 1 PIN, 2 contraseña
// ---- FASE 1: bloqueo global reforzado ------------------------------------
// Tres funciones INDEPENDIENTES, cada una con su interruptor. Poner un
// interruptor a 0 desactiva SOLO esa funcion y deja el resto del sistema de
// PIN/contrasena exactamente como estaba (mismo patron que GLASS_*_ON).
#define LOCK_FAILS_ON 1               // contador persistente + espera progresiva
#define LOCK_SHAKE_ON 1               // sacudida horizontal amortiguada al fallar
#define AUTOLOCK_ON   1               // bloqueo automatico por inactividad
// Umbrales de la espera progresiva. 1-3 fallos no cuestan nada; del 4 al 5 se
// cobran 30 s antes de poder reintentar; a partir del 6 son 5 min con mensaje
// explicito en pantalla.
#define LOCK_FAILS_SOFT     4
#define LOCK_FAILS_HARD     6
#define LOCK_WAIT_SOFT_MS   30000UL
#define LOCK_WAIT_HARD_MS   300000UL
// Opciones que ofrece Ajustes -> Seguridad -> Bloqueo de inactividad. El valor
// vivo es gAutoLockMs (NVS "autolockms"); 0 = nunca se bloquea solo.
#define AUTOLOCK_NOPT 6
static const uint32_t AUTOLOCK_OPTS[AUTOLOCK_NOPT]  = { 30000UL, 60000UL, 300000UL, 600000UL, 1800000UL, 0UL };
static const char*    AUTOLOCK_NAMES[AUTOLOCK_NOPT] = { "30 segundos", "1 minuto", "5 minutos", "10 minutos", "30 minutos", "Nunca" };
#define AUTOLOCK_DEFAULT_IDX 1                        // 1 minuto
#define AUTOLOCK_DEFAULT_MS  60000UL
static int      lockFails    = 0;                     // fallos acumulados (NVS "lockfails")
static uint32_t gAutoLockMs  = AUTOLOCK_DEFAULT_MS;   // ventana de inactividad
static int  autoLockIdx(){
  for(int i = 0; i < AUTOLOCK_NOPT; i++) if(AUTOLOCK_OPTS[i] == gAutoLockMs) return i;
  return AUTOLOCK_DEFAULT_IDX;
}
static const char* autoLockName(){ return AUTOLOCK_NAMES[autoLockIdx()]; }
// Ajusta a una de las opciones ofrecidas. Hace falta porque el valor anterior
// por defecto eran 2 minutos, que ya no esta en la lista: sin esto, una placa
// que ya tenga ese valor guardado mostraria "1 minuto" en Ajustes mientras se
// sigue bloqueando a los 2.
static void autoLockNormalize(){
  for(int i = 0; i < AUTOLOCK_NOPT; i++) if(AUTOLOCK_OPTS[i] == gAutoLockMs) return;
  gAutoLockMs = AUTOLOCK_OPTS[AUTOLOCK_DEFAULT_IDX];
}
static uint32_t gLastTouchMs = 0;                     // millis del ultimo contacto real
// ---- APAGADO SEGURO: preferencia de usuario (Ajustes -> Seguridad) -------
// IMPORTANTE -- no confundir dos cosas distintas:
//
//  · gPoffPin (esto) es la confirmacion por clave del APAGADO COMPLETO: evita
//    que alguien apague el aparato de un deslizamiento. Se aplica UNICA Y
//    EXCLUSIVAMENTE ahi. Ni suspEnter() ni suspWake() lo consultan jamas:
//    SUSPENDER no pide nada, es un gesto de un segundo.
//
//  · El BLOQUEO DE PANTALLA al despertar de una suspension (SUSPEND_LOCK_ON) es
//    otra cosa y depende de gLockType, la clave del dispositivo de toda la
//    vida. Ese si protege la privacidad: si alguien coge el aparato suspendido
//    y lo enciende, se encuentra el bloqueo, no lo que estabas haciendo.
static bool gPoffPin = false;                         // NVS "poffpin"
static bool gBootCleanOff = false;                    // este arranque viene de un apagado limpio (NVS "cleanoff")
// ---- FASES 2 y 3: menu contextual + bloqueo por app ----------------------
#define CTXMENU_ON 1                  // menu de long-press (0 = long-press va directo a Modo Edicion)
#define APPLOCK_ON 1                  // candado por app + verificacion al abrirla
// Una sola clave NVS con un bitmask en vez de 16 claves "applock_<i>": el
// IDENTIFICADOR UNICO de cada FlexApp ya es su indice en APP_REG (0..15, el
// mismo que usan homeOrder[], drawAppIcon() y enterApp()), y 16 apps caben
// exactas en un uint16_t. Un solo getInt/putInt, cero snprintf de claves.
static uint32_t gAppLock = 0;                         // bit i = app i bloqueada (NVS "applockm")
// ---- CAJA DE APLICACIONES: visibilidad y favoritas -----------------------
// Mismo criterio que gAppLock: un bit por app, indexado por su id (= indice en
// APP_REG), un solo entero en NVS. gAppFav dice que apps se ven en el
// escritorio; gAppHidden, cuales se esconden tambien de la caja. Se declaran
// aqui, junto al resto de la configuracion, porque renderHome() y
// homeOrderLoad() -- ambos ANTES del bloque de la caja -- ya los necesitan.
// El valor de fabrica (todas favoritas, ninguna oculta) se calcula en
// drawerRegistryDefaults() a partir del campo 'dflt' de APP_REG.
static uint32_t gAppFav    = 0x0FFF;                  // bit i = app i en el escritorio (NVS "appfav")
static uint32_t gAppHidden = 0;                       // bit i = app i oculta        (NVS "apphide")
#define HOME_EMPTY 0xFF                               // ranura vacia en homeOrder[]
static inline bool appIsFav(int id){    return (id >= 0 && id < APP_N) && (gAppFav    & (uint32_t)(1u << id)) != 0; }
static inline bool appIsHidden(int id){ return (id >= 0 && id < APP_N) && (gAppHidden & (uint32_t)(1u << id)) != 0; }
// Ajustes NUNCA se puede ocultar: es la unica pantalla desde la que se
// recupera el resto del sistema, y una caja sin Ajustes deja al usuario sin
// salida. La fila del menu contextual se dibuja atenuada, no desaparece.
static inline bool appCanHide(int id){  return id != IC_AJUSTES; }
// Que hacer cuando la verificacion de PIN/contrasena ACIERTA. Es lo que permite
// reutilizar lsuStartVerify() para todo (pantalla, apps, kiosco) sin crear una
// segunda ruta de verificacion.
#define LSU_AFTER_UNLOCK    0         // desbloquear la pantalla (comportamiento de siempre)
#define LSU_AFTER_OPENAPP   1         // abrir la app bloqueada que se toco
#define LSU_AFTER_LOCKAPP   2         // confirmar que se pone el candado a una app
#define LSU_AFTER_UNLOCKAPP 3         // confirmar que se quita el candado a una app
#define LSU_AFTER_KIOSKOUT  4         // salir del Modo Kiosco
#define LSU_AFTER_POWEROFF  5         // apagar del todo (solo si el usuario activo "Apagado seguro")
#define LSU_AFTER_FACTORY   6         // continuar con el Restablecimiento de fabrica
static int lsuAfter    = LSU_AFTER_UNLOCK;
static int lsuAfterApp = -1;
#define LW_CLOCK   0x01   // reloj grande + fecha
#define LW_WEATHER 0x02   // clima (datos REALES del motor FlexOS Weather)
#define LW_CAL     0x04   // calendario (mock, sin eventos reales aun)
#define LW_NOTIF   0x08   // notificaciones (datos reales: gNotifs[])
static uint8_t gLockWidgets = LW_CLOCK;       // widgets activos en Bloqueo (por defecto: solo el reloj, igual que hoy)
static int   gNavMode    = 0;                 // 0 = botones clasicos, 1 = gestos iOS
static int   gAnimStyle  = 0;                 // transicion al abrir/cerrar apps: 0=zoom, 1=fundido, 2=deslizar
static char  cfgName[24]  = "FlexOS Ultra";

// #############################################################
// ##  PREFERENCIAS DEL TECLADO (Fases A-G)  ·  todas en NVS
// ##  ------------------------------------------------------
// ##  Viven aqui, con el resto de la configuracion, para que
// ##  cfgLoad()/cfgSavePrefs() las traten EXACTAMENTE igual que
// ##  gDark o gAnimStyle: mismo sitio, mismo momento, misma
// ##  namespace "flexos". Los valores por defecto son los del
// ##  teclado de siempre, asi que una placa que actualice no
// ##  nota ningun cambio hasta que el usuario toque Ajustes.
// #############################################################
#define KB_SIZE_COMPACT 0
#define KB_SIZE_NORMAL  1
#define KB_SIZE_BIG     2
static int  gKbSize     = KB_SIZE_NORMAL;   // NVS "kbsize"   Fase A
static bool gKbFastType = true;             // NVS "kbfast"   Fase B (escritura rapida / multitoque)
static bool gKbToolbar  = true;             // NVS "kbtool"   Fase C (barra superior)
static bool gKbPredict  = true;             // NVS "kbpred"   Fase F (texto predictivo)
static bool gKbSpell    = false;            // NVS "kbspell"  Fase F (revision ortografica basica)
static bool gKbEmojiSug = false;            // NVS "kbemoji"  Fase F (sugerir emojis)
static bool gKbHiCon    = false;            // NVS "kbhicon"  Fase E (teclado de contraste alto)
static int  gKbOpacity  = 100;              // NVS "kbopa"    Fase E (opacidad del panel, 40..100)
static int  gKbStyle    = 0;                // NVS "kbstyle"  Fase E (0 redondeada, 1 cuadrada, 2 contorno)
static int  gKbFontSc   = 1;                // NVS "kbfont"   Fase E (0 pequena, 1 normal, 2 grande)
static int  gKbLpMs     = 500;              // NVS "kblp"     Fase E (umbral de long-press: 350/500/700)
static int  gKbFxMs     = 100;              // NVS "kbfx"     Fase E/G (duracion del destello de tecla)
// Fila de simbolos personalizados (Fase E). Se guardan como INDICES dentro de
// KB_SYM_POOL (ver mas abajo) y no como texto libre: asi es imposible acabar
// con un caracter que la fuente 5x7 no sepa dibujar.
#define KB_SYMS 4
static int gKbSym[KB_SYMS] = { 0, 1, 2, 3 };   // NVS "kbsyms" (4 bytes)
// Atajos de texto (Fase E, los usa la Fase F al completar palabra). Sin struct
// a proposito: dos matrices de char[] planas se guardan en NVS con un solo
// putBytes cada una y no obligan a declarar un tipo nuevo.
#define KB_SC_MAX  8
#define KB_SC_ABR  10
#define KB_SC_EXP  24
static char gKbScAbr[KB_SC_MAX][KB_SC_ABR];
static char gKbScExp[KB_SC_MAX][KB_SC_EXP];
// Atajos de fabrica: se escriben la primera vez que arranca (o al restablecer).
static void kbShortcutsDefaults(){
  memset(gKbScAbr, 0, sizeof(gKbScAbr));
  memset(gKbScExp, 0, sizeof(gKbScExp));
  snprintf(gKbScAbr[0], KB_SC_ABR, "xq");  snprintf(gKbScExp[0], KB_SC_EXP, "porque");
  snprintf(gKbScAbr[1], KB_SC_ABR, "q");   snprintf(gKbScExp[1], KB_SC_EXP, "que");
  snprintf(gKbScAbr[2], KB_SC_ABR, "tb");  snprintf(gKbScExp[2], KB_SC_EXP, "tambi\xC3\xA9n");
  snprintf(gKbScAbr[3], KB_SC_ABR, "pf");  snprintf(gKbScExp[3], KB_SC_EXP, "por favor");
}
static void kbPrefsNormalize(){
  if(gKbSize < 0 || gKbSize > 2) gKbSize = KB_SIZE_NORMAL;
  if(gKbStyle < 0 || gKbStyle > 2) gKbStyle = 0;
  if(gKbFontSc < 0 || gKbFontSc > 2) gKbFontSc = 1;
  if(gKbOpacity < 40) gKbOpacity = 40; if(gKbOpacity > 100) gKbOpacity = 100;
  if(gKbLpMs != 350 && gKbLpMs != 500 && gKbLpMs != 700) gKbLpMs = 500;
  if(gKbFxMs != 60 && gKbFxMs != 100 && gKbFxMs != 160) gKbFxMs = 100;
  for(int i = 0; i < KB_SYMS; i++) if(gKbSym[i] < 0 || gKbSym[i] > 15) gKbSym[i] = i;
  for(int i = 0; i < KB_SC_MAX; i++){ gKbScAbr[i][KB_SC_ABR - 1] = 0; gKbScExp[i][KB_SC_EXP - 1] = 0; }
}
static void kbPrefsLoad(){
  gKbSize     = prefs.getInt("kbsize", KB_SIZE_NORMAL);
  gKbFastType = prefs.getBool("kbfast", true);
  gKbToolbar  = prefs.getBool("kbtool", true);
  gKbPredict  = prefs.getBool("kbpred", true);
  gKbSpell    = prefs.getBool("kbspell", false);
  gKbEmojiSug = prefs.getBool("kbemoji", false);
  gKbHiCon    = prefs.getBool("kbhicon", false);
  gKbOpacity  = prefs.getInt("kbopa", 100);
  gKbStyle    = prefs.getInt("kbstyle", 0);
  gKbFontSc   = prefs.getInt("kbfont", 1);
  gKbLpMs     = prefs.getInt("kblp", 500);
  gKbFxMs     = prefs.getInt("kbfx", 100);
  { uint8_t sb[KB_SYMS]; size_t n = prefs.getBytes("kbsyms", sb, KB_SYMS);
    if(n == KB_SYMS) for(int i = 0; i < KB_SYMS; i++) gKbSym[i] = (int)sb[i]; }
  size_t na = prefs.getBytes("kbscabr", gKbScAbr, sizeof(gKbScAbr));
  size_t ne = prefs.getBytes("kbscexp", gKbScExp, sizeof(gKbScExp));
  if(na != sizeof(gKbScAbr) || ne != sizeof(gKbScExp)) kbShortcutsDefaults();
  kbPrefsNormalize();
}
static void kbPrefsSaveOpen(){        // se llama con prefs YA abierto en escritura
  prefs.putInt("kbsize", gKbSize);
  prefs.putBool("kbfast", gKbFastType);
  prefs.putBool("kbtool", gKbToolbar);
  prefs.putBool("kbpred", gKbPredict);
  prefs.putBool("kbspell", gKbSpell);
  prefs.putBool("kbemoji", gKbEmojiSug);
  prefs.putBool("kbhicon", gKbHiCon);
  prefs.putInt("kbopa", gKbOpacity);
  prefs.putInt("kbstyle", gKbStyle);
  prefs.putInt("kbfont", gKbFontSc);
  prefs.putInt("kblp", gKbLpMs);
  prefs.putInt("kbfx", gKbFxMs);
  { uint8_t sb[KB_SYMS]; for(int i = 0; i < KB_SYMS; i++) sb[i] = (uint8_t)gKbSym[i];
    prefs.putBytes("kbsyms", sb, KB_SYMS); }
  prefs.putBytes("kbscabr", gKbScAbr, sizeof(gKbScAbr));
  prefs.putBytes("kbscexp", gKbScExp, sizeof(gKbScExp));
}
// Guarda SOLO las preferencias del teclado (abre y cierra por su cuenta). Lo
// usan las filas de la pantalla de Ajustes del teclado, que cambian una cosa
// cada vez y no tienen por que reescribir las doce claves del sistema.
static void kbPrefsSave(){
  prefs.begin("flexos", false);
  kbPrefsSaveOpen();
  prefs.end();
}

static void cfgLoad(){
  prefs.begin("flexos", true);
  cfgOobeDone = prefs.getBool("oobe", false);
  cfgLang     = prefs.getInt("lang", 0);
  g24h        = prefs.getBool("h24", false);
  uiGlass     = prefs.getBool("glass", false);
  gDark       = prefs.getBool("dark", true);
  gIconStyle  = prefs.getInt("iconstyle", 0);
  gBright     = prefs.getInt("bright", 80);
  gLockType   = prefs.getInt("locktype", 0);
  // FASE 1: el contador de fallos vive en NVS a proposito -- reiniciar la placa
  // NO es una via de escape para saltarse la espera progresiva.
  lockFails   = prefs.getInt("lockfails", 0);
  if(lockFails < 0) lockFails = 0;                                        // prefs corruptas
  gAutoLockMs = (uint32_t)prefs.getInt("autolockms", (int)AUTOLOCK_DEFAULT_MS);
  autoLockNormalize();
  // APAGADO SEGURO: preferencia de usuario (Ajustes -> Seguridad). Por defecto
  // DESACTIVADA para no cambiar el comportamiento de nadie al actualizar.
  gPoffPin    = prefs.getBool("poffpin", false);
  gAppLock    = (uint16_t)prefs.getInt("applockm", 0);                    // FASE 2: candados por app
  // FASE 4: el kiosco sobrevive al reinicio A PROPOSITO -- apagar el telefono no
  // puede ser la via facil de escape. Al arrancar se vuelve a entrar en la misma
  // app y solo el PIN/contrasena permite salir.
  kioskOn     = prefs.getBool("kioskon", false);
  kioskApp    = prefs.getInt("kioskapp", -1);
  kioskExX    = prefs.getInt("kioskx", 0);
  kioskExY    = prefs.getInt("kiosky", 0);
  kioskExW    = prefs.getInt("kioskw", 0);
  kioskExH    = prefs.getInt("kioskh", 0);
  if(kioskApp < 0 || kioskApp >= APP_N) { kioskOn = false; kioskApp = -1; }   // prefs corruptas
  if(gLockType == 0) kioskOn = false;   // sin clave configurada no habria forma de salir: no se activa
  gLockWidgets = (uint8_t)prefs.getInt("lockwidgets", LW_CLOCK);
  gNavMode    = prefs.getInt("navmode", 0);
  gAnimStyle  = prefs.getInt("animstyle", 0);
  kbPrefsLoad();                     // Fases A-G: preferencias del teclado
  String n = prefs.getString("name", "FlexOS Ultra");
  n.toCharArray(cfgName, sizeof(cfgName));
  prefs.end();
}
// Guarda las preferencias personalizables (idioma, formato, estilo, brillo)
static void cfgSavePrefs(){
  prefs.begin("flexos", false);
  prefs.putInt("lang", cfgLang);
  prefs.putBool("h24", g24h);
  prefs.putBool("glass", uiGlass);
  prefs.putBool("dark", gDark);
  prefs.putInt("iconstyle", gIconStyle);
  prefs.putInt("bright", gBright);
  prefs.putInt("lockwidgets", gLockWidgets);
  prefs.putInt("navmode", gNavMode);
  prefs.putInt("animstyle", gAnimStyle);
  prefs.putInt("autolockms", (int)gAutoLockMs);
  prefs.putBool("poffpin", gPoffPin);
  kbPrefsSaveOpen();                 // Fases A-G: preferencias del teclado
  prefs.end();
}
// FASE 1: se guarda SOLO el contador de fallos. Aparte de cfgSavePrefs porque se
// escribe en momentos muy distintos (cada fallo / cada acierto) y no queremos
// reescribir doce claves por cada digito equivocado.
static void lockFailsSave(){
  prefs.begin("flexos", false);
  prefs.putInt("lockfails", lockFails);
  prefs.end();
}
// ---- FASE 2: candado por app (bitmask indexado por indice de APP_REG) ----
static bool appLockGet(int id){
  if(!APPLOCK_ON || id < 0 || id >= APP_N) return false;
  return (gAppLock & (uint32_t)(1u << id)) != 0;
}
static void appLockSet(int id, bool on){
  if(id < 0 || id >= APP_N) return;
  if(on) gAppLock |=  (uint32_t)(1u << id);
  else   gAppLock &= (uint32_t)~(1u << id);
  prefs.begin("flexos", false);
  prefs.putInt("applockm", (int)gAppLock);
  prefs.end();
}
// ---- FASE 4: persistencia del Modo Kiosco --------------------------------
static void kioskSave(){
  prefs.begin("flexos", false);
  prefs.putBool("kioskon", kioskOn);
  prefs.putInt("kioskapp", kioskApp);
  prefs.putInt("kioskx", kioskExX);
  prefs.putInt("kiosky", kioskExY);
  prefs.putInt("kioskw", kioskExW);
  prefs.putInt("kioskh", kioskExH);
  prefs.end();
}
static void cfgSaveOobe(){
  prefs.begin("flexos", false);
  prefs.putBool("oobe", true);
  prefs.putInt("lang", cfgLang);
  prefs.putString("name", cfgName);
  prefs.end();
  cfgOobeDone = true;
}
