// #############################################################
// ##  FLEX OS ULTRA  ·  TIPOS GLOBALES, INTERRUPTORES MAESTROS Y ESTADO TEMPRANO
// ##  ----------------------------------------------------------
// ##  Los tipos que viajan en la FIRMA de alguna funcion (FGlyph, PWin,
// ##  DexFit, Touch, QsCtl, QpItem, AppLife, AppHooks, SessHdr), los
// ##  interruptores maestros de cada fase (teclado, apagado de pantalla,
// ##  Liquid Glass, kiosco, bloqueo de apps), el estado y los tipos de la
// ##  isla dinamica, del cronometro, de los widgets del escritorio y de la
// ##  capa de transicion de apps, y el diagnostico Wi-Fi/microSD.
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
#include <Arduino.h>

// -------------------------------------------------------------
//  Tipos usados como PARAMETRO de alguna funcion (FGlyph en fgPix/
//  drawGlyphScaled, PWin en pcDrawWindow, DexFit en dexHostFit/
//  dexHostScale, Touch en dexHostRun). Van AQUI ARRIBA DEL TODO
//  a proposito: el IDE de Arduino auto-genera prototipos de todas
//  las funciones y los inserta al inicio del archivo compilado; si
//  el tipo se define mas abajo, ese prototipo autogenerado no lo
//  conoce todavia y la compilacion falla con "does not name a type"
//  aunque en este .ino el tipo aparezca "antes" de usarse. Definir
//  estos tipos aqui arriba evita el problema pase lo que pase.
// -------------------------------------------------------------
typedef struct { uint8_t w, h; int8_t bx; int8_t topoff; uint8_t adv; uint16_t off; } FGlyph;
// PWin: estado de una ventana de Modo PC / DeX. Los campos nuevos (min, snap,
// rx/ry/rw/rh) viven AQUI ARRIBA por el mismo motivo que el resto del tipo: el
// IDE de Arduino autogenera los prototipos al inicio del archivo compilado.
//   mini          -> minimizada a la barra de tareas (sigue open). Se llama
//                    "mini" y no "min" porque Arduino define min() como macro.
//   snap          -> SNAP_FREE / mitades / cuadrantes / maximizada
//   rx,ry,rw,rh   -> geometria a la que restaurar al des-anclar
struct PWin { bool open; int x, y, w, h, app; bool mini; uint8_t snap; int rx, ry, rw, rh; };
// DexFit: encaje de una app hospedada dentro del area de cliente de su ventana
// (origen y tamano del contenido con la proporcion respetada, pasos de escalado
// en coma fija 16.16, y tamano del lienzo de la app). Vive aqui por la misma
// razon que PWin: dexHostFit y dexHostScale lo reciben por referencia.
struct DexFit { int ox, oy, ow, oh; uint32_t stepX, stepY; int aw, ah; bool land; bool flex; };
// Touch: estado tactil de alto nivel. Vive aqui porque dexHostRun lo recibe por
// puntero para INYECTAR un toque traducido en una app hospedada.
struct Touch {
  bool down=false, pressed=false, released=false, tap=false, moved=false;
  bool swipeUp=false, swipeDown=false, swipeLeft=false, swipeRight=false;
  int  x=0, y=0, startX=0, startY=0, dx=0, dy=0;
  unsigned long downMs=0, lastMs=0;
};
// QsCtl / QpItem: el REGISTRO y el MODELO DE DATOS del Panel Rapido. Viven
// aqui arriba por la misma razon que PWin, DexFit y Touch: qpCtl() devuelve
// un 'const QsCtl*' y qpNormalize() recibe un 'QpItem*', o sea que los dos
// tipos salen en una FIRMA, y el prototipo que autogenera el IDE se inserta
// al principio del fichero. Definirlos donde se usan haria fallar la
// compilacion del sketch en el IDE con "does not name a type".
//
//   id      -> identificador ESTABLE del control (va a NVS)
//   name    -> etiqueta corta (circulo 1x1)
//   title   -> etiqueta larga (capsula / modulo)
//   type    -> QT_TOGGLE / QT_ACTION / QT_SLIDER
//   sizes   -> mascara de tamanos permitidos (QSZ_*)
//   oris    -> mascara de orientaciones permitidas (QOR_*)
//   cat     -> categoria del catalogo
//   avail   -> DISPONIBILIDAD REAL (hardware/SDK/estado)
//   state   -> lectura del estado REAL (NULL en las acciones)
//   tap     -> ejecucion real del toque
//   detail  -> accion secundaria (pulsacion larga)
//   sub     -> subtitulo con dato real
//   icon    -> glifo vectorial
struct QsCtl {
  uint8_t     id;
  const char* name;
  const char* title;
  uint8_t     type;
  uint8_t     sizes;
  uint8_t     oris;
  uint8_t     cat;
  bool (*avail)();
  bool (*state)();
  void (*tap)();
  void (*detail)();
  void (*sub)(char*, size_t);
  void (*icon)(int, int, int, uint16_t);
};
// Un elemento COLOCADO en el panel: que control, que tamano ocupa en la
// cuadricula logica de 4 columnas, con que orientacion y si esta visible.
struct QpItem { uint8_t id, w, h, ori, vis; };

// -------------------------------------------------------------
//  CICLO DE VIDA DE APPS Y SESIONES  ·  tipos
//  -------------------------------------------------------------
//  Viven AQUI ARRIBA por la MISMA razon que FGlyph, PWin, DexFit y
//  Touch: el IDE de Arduino autogenera los prototipos de todas las
//  funciones y los inserta al principio del archivo compilado. Como
//  appHooks() devuelve un const AppHooks*, y los prototipos autogenerados
//  necesitan conocer ese contrato antes de la primera funcion del sketch.
//
//  NO son clases ni objetos: el resto del sistema esta escrito con
//  structs planos, registros y punteros a funcion (FlexApp, APP_REG),
//  y esto sigue exactamente el mismo patron -- cero constructores,
//  cero asignacion dinamica, todo de tamano fijo.
// -------------------------------------------------------------

// Estados posibles de una FlexApp. Son EXCLUYENTES: una app esta en
// uno y solo uno de ellos en cada instante.
enum AppLife : uint8_t {
  ALIFE_CLOSED = 0,   // cerrada: no ocupa recursos, no sale en Recientes
  ALIFE_RUNNING,      // abierta y visible (como mucho UNA a la vez)
  ALIFE_SUSPENDED,    // en Recientes: estado logico vivo, sin dibujar ni leer toques
  ALIFE_RESUMING      // transitorio: restaurando estado, aun sin publicar frame
};

// Contrato de ciclo de vida de una app. TODOS los campos son opcionales
// (NULL = la app no necesita esa fase). El framework nunca llama a un
// puntero nulo, asi que las apps que no rellenen hooks siguen
// comportandose exactamente igual que antes de existir este sistema.
struct AppHooks {
  bool (*backLayer)();   // cierra teclado/menu/dialogo/overlay PROPIO. true = cerro una capa
  bool (*backScreen)();  // retrocede UNA pantalla interna. true = habia a donde volver
  void (*suspend)();     // congela: para animaciones, suelta buffers secundarios
  void (*resume)();      // reanuda: repinta desde el estado logico (NO reinicia nada)
  void (*close)();       // termina: libera TODO lo suyo y deja el estado en limpio
  bool (*saveSess)();    // vuelca a disco el minimo indispensable. true = escribio
  void (*loadSess)();    // lo relee al arrancar (una sola vez, antes del primer enter)
  bool (*bgWork)();      // LISTA BLANCA: true = tiene trabajo real en segundo plano
  // ---- Ganchos anadidos AL FINAL, a proposito ----
  // Van los ultimos para que ningun inicializador de los que ya existen
  // (H_NOTES, H_PAINT, H_GALLERY...) tenga que tocarse: los campos que no
  // se rellenan quedan a NULL, que es exactamente "esta app no lo necesita".
  size_t (*shed)();      // suelta recursos PESADOS y RECONSTRUIBLES sin perder
                         // estado. Solo se llama con la app SUSPENDIDA. Al
                         // volver, resume() los rehace. Devuelve una pista de
                         // lo soltado; quien manda es la medida real de PSRAM.
  bool (*dirty)();       // true = la app tiene cambios del usuario sin guardar
};

// Cabecera comun de todo archivo de sesion en LittleFS. Va delante de
// la carga util y es lo que permite (a) rechazar un archivo de otra
// version del formato sin interpretarlo, y (b) detectar un corte de
// corriente a mitad de escritura (crc o len que no cuadran).
struct SessHdr { uint32_t magic; uint16_t ver; uint16_t app; uint32_t len; uint32_t crc; };

// #############################################################
// ##  TECLADO FLEXOS  ·  FASES A-G  ·  INTERRUPTORES MAESTROS
// ##  ------------------------------------------------------
// ##  Cada fase tiene SU PROPIO interruptor y se puede apagar
// ##  sola, sin tocar las demas (mismo patron que GLASS_*_ON,
// ##  KIOSK_ON o APPLOCK_ON). Si algo falla en la placa: baja
// ##  a 0 de la G hacia la A, recompila y mira cual era.
// ##
// ##    KB_SIZE_CONFIG_ON   Fase A - tamano de teclado configurable
// ##                        (Compacto/Normal/Grande). A 0 el teclado
// ##                        usa los valores fijos de siempre 43/48/4/6.
// ##    KB_MULTITOUCH_ON    Fase B - escritura rapida: la tecla se
// ##                        dispara al TOCAR (por ID de contacto del
// ##                        GT911). A 0 vuelve el disparo al soltar.
// ##    KB_TOOLBAR_ON       Fase C - barra superior de 5 accesos.
// ##    KB_CLIPBOARD_MULTI_ON Fase D - portapapeles de 12 ranuras con
// ##                        pin/borrado. A 0 vuelve el buffer unico.
// ##    KB_SETTINGS_ON      Fase E - pantalla de Ajustes del teclado.
// ##    KB_AUTOCOMPLETE_ON  Fase F - chips de autocompletado (lista
// ##                        local fija, NO es un modelo de IA).
// ##    KB_ANIM_POLISH_ON   Fase G - animaciones (apertura, tecla
// ##                        presionada, chips, toast). A 0 todo
// ##                        sigue funcionando, pero con cortes secos.
// #############################################################
#define KB_SIZE_CONFIG_ON     1
#define KB_MULTITOUCH_ON      1
#define KB_TOOLBAR_ON         1
#define KB_CLIPBOARD_MULTI_ON 1
#define KB_SETTINGS_ON        1
#define KB_AUTOCOMPLETE_ON    1
#define KB_ANIM_POLISH_ON     1

// FASE B - un punto de contacto del GT911 tal cual sale del chip.
//   id     -> track ID que asigna el propio GT911 (byte 7 del bloque).
//             Es lo que permite saber que "este dedo" es el mismo entre
//             frames aunque se muevan las coordenadas.
//   x, y   -> ya en coordenadas de FlexOS (mismos flags SWAP/FLIP que gtPoll)
//   active -> ese hueco del array tiene un dedo vivo en este frame
// Vive aqui arriba, con struct Touch, por la restriccion de ctags: el
// generador de prototipos de Arduino no puede ver un tipo definido a
// mitad de archivo.
struct TouchPoint { int id; int x, y; bool active; };

// -------------------------------------------------------------
//  TIPOS DE MEDIOS QUE SALEN EN UNA FIRMA
//  ------------------------------------------------------------
//  Viven aqui arriba por la MISMA restriccion que struct Touch y
//  struct TouchPoint: el IDE de Arduino inserta el prototipo de
//  todas las funciones del .ino antes de la primera de ellas, asi
//  que un tipo que aparezca en la firma de cualquier funcion tiene
//  que estar definido antes. Definirlos junto a la Galeria o al
//  reproductor, que es donde se usan, compilaria con g++ y fallaria
//  en el IDE con "does not name a type".
// -------------------------------------------------------------

// LECTOR UNIFICADO. Un solo tipo para leer un fichero este donde
// este: en la particion interna o en la tarjeta. Es lo que evita
// tener dos reproductores y dos visores, uno por memoria.
//
// La diferencia real entre los dos volumenes esta en el coste de
// abrir: en la tarjeta abrir cuesta una busqueda en el directorio
// FAT, asi que el descriptor se mantiene ABIERTO (FlexSdFile) y se
// lee por bloques desde el. En LittleFS, que es flash mapeada, el
// coste de reabrir es despreciable, asi que se lee por
// desplazamiento (flexFsReadAt) y no se retiene ningun descriptor.
#define MSTREAM_NONE 0
#define MSTREAM_INT  1      // particion interna (LittleFS)
#define MSTREAM_SD   2      // tarjeta microSD
struct MediaStream {
  uint8_t    kind;
  FlexSdFile sd;                        // solo si kind == MSTREAM_SD
  char       path[FLEXMED_PATH_MAX];    // solo si kind == MSTREAM_INT
  uint32_t   pos, size;
};

// Una miniatura ya decodificada. La cache es un array fijo de estas
// (ver la Galeria): tamano constante, sin reserva por elemento y sin
// fragmentacion del heap por desplazarse por la rejilla.
#define GAL_THUMB_W   112
#define GAL_THUMB_H   112
struct GalThumb {
  char      path[FLEXMED_PATH_MAX];
  uint16_t* px;         // GAL_THUMB_W * GAL_THUMB_H en PSRAM
  uint16_t  w, h;       // area realmente util (respeta la proporcion)
  uint32_t  useMs;      // ultimo uso: la que lleva mas tiempo sin usarse cede su sitio
  uint32_t  pass;       // repintado en el que se uso (ver galCacheSlot)
  uint8_t   state;      // GTH_*
};
#define GTH_EMPTY 0
#define GTH_OK    1
#define GTH_FAIL  2     // se intento y no se pudo: no se reintenta en cada repintado

// ORIENTACION DE UN MEDIO. Los tres modos que pide el usuario, mas
// la orientacion EFECTIVA que se deduce de ellos y de la forma del
// archivo. Se separan a proposito: el modo es lo que el usuario
// eligio y la efectiva es lo que se esta pintando.
#define MORI_AUTO  0
#define MORI_PORT  1
#define MORI_LAND  2

// Centros de los botones del reproductor, en coordenadas del lienzo
// LOGICO (o sea, ya girados cuando toca). Dibujo y hit-test llaman a
// la misma funcion para rellenar esto, que es lo que impide que se
// vea horizontal y el dedo responda en vertical.
struct VidBtns { int cx, cy, prevX, back10X, fwd10X, nextX, oriX, oriY, oriW, oriH; };
#define KB_MAXPOINTS 5
static TouchPoint gKbPoints[KB_MAXPOINTS];

// FASE D - una ranura del portapapeles.
//   pinned -> fijada por el usuario: no se descarta al llenarse y
//             SOBREVIVE al reinicio (se guarda en NVS).
//   used   -> la ranura tiene contenido
//   ts     -> millis() de cuando se copio (para saber cual es la mas vieja)
#define CLIP_SLOTS   12
#define CLIP_TXT_MAX 200
struct ClipItem { char text[CLIP_TXT_MAX]; bool pinned; bool used; uint32_t ts; };
static ClipItem gClip[CLIP_SLOTS];


// =============================================================
// GEOMETRIA NATIVA
// =============================================================
#define SCR_W   480
#define SCR_H   800
// Modo horizontal (PC): coords logicas landscape 800x480, rotadas 90 al panel
#define LW      SCR_H
#define LH      SCR_W

// =============================================================
// PINES (confirmados en la JC4880P443, reutilizados de ArduOS)
// =============================================================
#define PIN_LCD_RST   5     // reset del ST7701
#define PIN_LCD_BL    23    // backlight (encendido fijo)
#define PIN_TP_SDA    7     // GT911 SDA
#define PIN_TP_SCL    8     // GT911 SCL
#define PIN_TP_RST    3     // GT911 reset

// #############################################################
// ##  APAGADO DE PANTALLA  ·  interruptores maestros
// ##  ------------------------------------------------------
// ##  Cada sub-sistema se desactiva por separado (mismo patron que
// ##  GLASS_SHADOW_ON / KIOSK_ON / APPLOCK_ON) para poder aislar
// ##  cualquier problema en pruebas de hardware sin tocar el resto.
// #############################################################
#define SUSPEND_ON        1   // gesto de suspension (doble-tap 2 dedos) y de despertar (doble-tap 1 dedo)
#define SUSPEND_LOCK_ON   1   // al despertar de una suspension, caer en la pantalla de Bloqueo si el
                              // usuario tiene configurado PIN/contrasena (gLockType > 0). Sin clave
                              // configurada no cambia nada: se vuelve directo a donde estabas.
#define POWEROFF_ON       1   // apagado completo: icono en el Panel Rapido + confirmacion + deep sleep
#define POWEROFF_PIN_ON   1   // KILL-SWITCH de compilacion de la proteccion por PIN del apagado.
                              // El toggle real que ve el usuario vive en Ajustes -> Seguridad
                              // (gPoffPin, persistido en NVS). Con esta constante a 0 la
                              // proteccion no existe ni aunque el toggle este activado.
#define PANEL_DCS_SLEEP_ON 1  // comandos DCS de bajo consumo del ST7701 (0x28 DISPOFF / 0x10 SLPIN).
                              // Ponlo a 0 si tu panel no vuelve limpio de DISPON: el fundido de
                              // backlight por si solo ya deja la pantalla en negro absoluto.

// ---- Gesto de suspension / despertar (doble-tap) ------------------------
#define SUSP_TAP_WINDOW_MS 450  // ventana maxima entre el 1er y el 2o toque del doble-tap
#define SUSP_TAP_GAP_MS    45   // separacion MINIMA real entre los dos toques (filtra rebotes del GT911)
#define SUSP_TAP_MAX_MS    600  // duracion maxima de un toque para contar como "tap" (mas = long-press)
#define SUSP_TAP_FRAMES    2    // polls CONSECUTIVOS con n>=2 que confirman un toque de 2 dedos
#define SUSP_FADE_STEP_MS  10   // periodo de cada paso del fundido de backlight (no bloqueante)
#define SUSP_FADE_STEP     6    // puntos de brillo (0..100) por paso -> ~170 ms de fundido completo

// ---- Apagado completo: despertar desde deep sleep ------------------------
//
// PIN DE DESPERTAR (POFF_WAKE_GPIO)
// ---------------------------------
// Este proyecto NUNCA ha cableado la linea INT del GT911: el driver de arriba
// solo usa SDA(7) / SCL(8) / RST(3) y sondea por I2C. Como el numero de GPIO
// del INT en la JC4880P443C_I_W no esta confirmado en ninguna fuente que se
// haya podido verificar, NO se inventa aqui un pin: se deja en -1 y el codigo
// cae a la ruta alternativa (temporizador). Pon aqui el GPIO real del INT del
// GT911 de tu placa para activar la ruta buena.
//
// RESTRICCION DEL ESP32-P4 (verificada en soc_caps.h de ESP-IDF v5.4, que es
// la base de arduino-esp32 v3.2.0):
//   · SOC_DEEP_SLEEP_SUPPORTED    = 1  -> hay deep sleep real.
//   · SOC_PM_SUPPORT_EXT1_WAKEUP  = 1  -> ext1 SI existe en el P4.
//   · NO existe SOC_PM_SUPPORT_EXT0_WAKEUP -> ext0 NO existe en el P4.
//     (En el ESP32 clasico si; por eso la tarea decia "ext0/ext1 segun soporte".)
//   · SOC_RTCIO_PIN_COUNT         = 16 -> ext1 solo admite GPIO 0..15.
// Por eso el pin de despertar TIENE que estar en 0..15; el #if de abajo lo
// comprueba en compilacion y cae al temporizador si no lo esta.
#define POFF_WAKE_GPIO    -1  // GPIO del INT del GT911 (0..15). -1 = no cableado -> ruta por temporizador
#define POFF_WAKE_LEVEL    0  // nivel que despierta: 0 = flanco/nivel BAJO (INT del GT911 por defecto), 1 = ALTO
#define POFF_WAKE_POLL_MS 400 // ruta alternativa: cada cuanto despierta el temporizador a mirar el tactil
#define POFF_WAKE_HOLD_MS 3000 // presion sostenida necesaria para completar el arranque (requisito: ~3 s)
#define POFF_WAKE_GATE_MS 4200 // ventana total del filtro de arranque antes de rendirse y volver a dormir


// #############################################################
// ##  ISLA DINAMICA · tipos y estado  (FASE 1: solo la isla)
// ##  ------------------------------------------------------
// ##  Se definen ARRIBA (antes de cualquier funcion) a proposito:
// ##  las funciones de la isla toman punteros a estos structs, y
// ##  el auto-prototipado de Arduino (ctags) inserta prototipos al
// ##  principio del sketch. Si los tipos no existieran aun, esos
// ##  prototipos no compilarian. Definiendolos aqui, todo prototipo
// ##  generado ya conoce ModuleType/DetectedModule/Notification.
// #############################################################

// ---- Tipos de modulo (compartidos con la futura deteccion I2C, Fase 2) ----
enum ModuleType {
  MOD_UNKNOWN,
  MOD_ULTRASONIC,   // HC-SR04
  MOD_BME280,       // sensor I2C
  MOD_MPU6050,      // IMU I2C
  MOD_LED,
  MOD_BUTTON,
  MOD_SERVO,
  MOD_I2C_GENERIC,
  // Avisos del sistema que NO son un modulo I2C pero usan la misma
  // isla: es la unica cola de notificaciones que hay, y duplicarla
  // para la tarjeta habria dado dos capas dibujando en la misma
  // banda de bbuf (el fallo que ya documenta la cabecera de la isla).
  MOD_SDCARD,       // tarjeta insertada / retirada / error / indice listo
  MOD_MEDIA         // archivo incompatible, fin de reproduccion...
};

// Un modulo detectado por el hardware.
struct DetectedModule {
  ModuleType    type;
  char          name[72];       // nombre descriptivo del modulo
  char          sub[40];        // bus, direccion o detalle del modulo
  uint8_t       i2cAddr;        // 0 si no es I2C
  uint8_t       pins[4];        // reservado para Fase 2 (asignacion de pines)
  uint8_t       numPins;
  bool          active;
  unsigned long detectedAt;
};

// ---- Geometria de la isla ----
//
// UNA TARJETA A LA VEZ. NOTIF_MAX sigue siendo la profundidad de la
// COLA (lo que cabe esperando), pero solo NOTIF_VISIBLE tarjetas se
// dibujan. Apilar tres avisos encima del escritorio tapaba los widgets,
// daba dos botones de cerrar a la vez y -- lo peor -- estiraba la banda
// hasta y=284, que se solapa con la banda de la rejilla (HOME_BAND_TOP
// = 206). Dos compositores escribiendo en bbuf sobre las mismas filas
// es exactamente lo que dejaba mitades de pagina antigua durante el
// deslizamiento: la isla reponia ahi el escritorio SIN desplazar justo
// despues de que hpRenderFrame lo hubiera dejado desplazado.
//
// Con una sola tarjeta la banda acaba en y=126, muy por encima de la
// rejilla: las dos zonas ya no pueden pisarse, y de paso cada frame de
// la isla mueve 110 filas en vez de 258.
#define NOTIF_MAX         3        // profundidad de la COLA
#define NOTIF_VISIBLE     1        // tarjetas dibujadas a la vez
#define NOTIF_MARGIN_X    16
#define NOTIF_CARD_W      (SCR_W - 2 * NOTIF_MARGIN_X)                        // 448
#define NOTIF_CARD_H      64
#define NOTIF_GAP         10
#define NOTIF_RAD         28
#define NOTIF_Y0          56                                                  // borde sup. de la 1a tarjeta
#define NOTIF_ENTER_DROP  24                                                  // caida de la animacion de entrada (px)
#define NOTIF_HOLD_MS     5000                                                // ms visible antes de auto-descartarse
#define NOTIF_BAND_TOP    (NOTIF_Y0 - NOTIF_ENTER_DROP - 6)                   // 26
#define NOTIF_BAND_BOT    (NOTIF_Y0 + NOTIF_VISIBLE * (NOTIF_CARD_H + NOTIF_GAP) + 6) // 126
#define NOTIF_BAND_H      (NOTIF_BAND_BOT - NOTIF_BAND_TOP)                   // 100

// ---- Fase de animacion de cada notificacion ----
enum NotifPhase { NP_IN, NP_IDLE, NP_DRAG, NP_OUT, NP_SPRING };

struct Notification {
  DetectedModule mod;
  bool           active;
  NotifPhase     phase;
  uint32_t       bornMs;        // inicio de la animacion de entrada (se fija al ARMARSE en Home)
  float          slideX;        // desplazamiento horizontal (descarte); 0 = en su sitio
  bool           armed;         // false = encolada pero aun no mostrada; se arma al verse en Home
  uint32_t       key;           // huella para no duplicar un aviso real ya encolado
};

// ---- Estado global de la cola ----
static Notification gNotifs[NOTIF_MAX];
static int          gNotifCount  = 0;
static bool         notifBandOn  = false;   // la banda tiene isla activa (o le debe un ultimo frame de limpieza)
static int          notifDragIdx = -1;      // tarjeta que el dedo esta arrastrando
static uint32_t     notifLastMs  = 0;       // throttle de animacion (~30 fps)
static bool         notifPaused    = false; // true mientras gState != ST_HOME (fases de la isla congeladas)
static uint32_t     notifPauseT0   = 0;     // millis() en que empezo la pausa (ver notifTick)

// #############################################################
// ##  CRONOMETRO  ·  MODELO DE ESTADO
// ##  ------------------------------------------------------
// ##  Subsistema PROPIO. NO reutiliza las notificaciones de
// ##  modulos: no toca gNotifs[], no usa NOTIF_HOLD_MS y no se
// ##  auto-descarta a los 5 s. La cola de la isla sigue siendo
// ##  exactamente lo que era.
// ##
// ##  Vive AQUI ARRIBA por la misma restriccion de ctags que
// ##  Touch y Notification: el IDE de Arduino genera
// ##  un prototipo de CADA funcion del sketch y los inserta
// ##  todos ANTES de la primera funcion, asi que cualquier tipo
// ##  que aparezca en una firma tiene que estar declarado antes.
// ##
// ##  REGLA DE TIEMPO (la unica que importa): el cronometro NO
// ##  se incrementa por frame. El tiempo real SIEMPRE sale de
// ##  cronoElapsed() = acumulado + (millis() - t0). Si la tasa
// ##  de refresco cae -- una app pesada, una descarga OTA, el
// ##  blur del vidrio -- el tiempo sigue siendo exacto porque
// ##  nadie lo va sumando a mano.
// ##
// ##  DESBORDAMIENTO DE millis(): todas las restas son entre
// ##  uint32_t. En aritmetica modulo 2^32, (ahora - antes) da el
// ##  intervalo correcto aunque el contador haya dado la vuelta
// ##  (cada ~49,7 dias). Por eso NO hay ninguna comparacion del
// ##  tipo "if(ahora > antes)" en todo el modulo.
// ##
// ##  MEMORIA: lista de vueltas de tamano FIJO. Ni un malloc en
// ##  marcha. Al llegar al tope se descarta la mas antigua y se
// ##  compacta, y gCronoLap0 recuerda que numero tiene la vuelta
// ##  que quedo en el indice 0 (asi la numeracion visible nunca
// ##  retrocede).
// #############################################################
#define CRONO_MAX_LAPS 20                  // tope de vueltas guardadas (array estatico)
enum CronoState { CRONO_IDLE = 0, CRONO_RUN = 1, CRONO_PAUSE = 2 };
// Una vuelta ya cerrada: su duracion propia (parcial) y el total acumulado
// del cronometro en el instante en que se marco.
struct CronoLap { uint32_t split; uint32_t total; };

// #############################################################
// ##  WIDGETS DEL ESCRITORIO · tipos
// ##  ------------------------------------------------------
// ##  Aqui arriba por la MISMA restriccion de ctags que Touch,
// ##  Notification y CronoLap: el IDE de Arduino genera
// ##  un prototipo de cada funcion del sketch y los inserta todos
// ##  antes de la primera funcion, asi que cualquier tipo que
// ##  aparezca en una firma tiene que estar declarado antes.
// ##  HomeWidget se pasa por puntero a wgDrawCell() y a wgRect().
// ##  Lo vigila tests/host/check_protos.py.
// #############################################################
#define HOME_WG_MAX 3                       // widgets colocados por pagina
// Un widget COLOCADO. col/row/w/h son CELDAS de la rejilla, no pixeles: asi
// cambiar de rejilla o de tamano de icono no invalida ninguna posicion.
struct HomeWidget { uint8_t type, col, row, w, h; };
// Catalogo. Solo entran widgets con un dato REAL detras en esta placa.
// El valor 7 queda retirado para que los widgets posteriores conserven su ID
// en NVS. homeWgNormalize() descarta automaticamente ese tipo sin mostrarlo.
enum { WG_NONE = 0, WG_CLOCK, WG_CLOCK_A, WG_DATE, WG_WIFI, WG_MEM,
       WG_STORAGE, WG_RETIRED_7, WG_CRONO, WG_CAM, WG_CLIMA, WG_COUNT };
struct WgDesc { const char* name; const char* cat; uint8_t w, h; };
// WxScene: paleta e intensidades de una escena meteorologica de la app Clima.
// Vive AQUI ARRIBA por la misma restriccion de ctags que HomeWidget o Touch:
// wxSceneOf() y wxSceneBlend() la reciben por puntero, y el prototipo que
// autogenera el IDE de Arduino se inserta antes que cualquier definicion de
// mitad de fichero. Lo vigila tests/host/check_protos.py.
struct WxScene {
  uint16_t skyTop, skyMid, skyBot;   // degradado del cielo
  uint16_t land, land2;              // montanas / horizonte
  uint16_t glow;                     // color del astro
  uint8_t  stars, clouds, rain, snow, fog, bolt;   // intensidades 0..255
  uint8_t  sun;                      // 0 = ninguno, 1 = sol, 2 = luna
  uint8_t  sunFade;                  // alpha del astro durante el cruce
};
// Fases de la tarjeta expandida (animacion propia, independiente de NotifPhase)
enum CronoCardPhase { CC_HIDDEN = 0, CC_OPENING, CC_OPEN, CC_CLOSING };

static CronoState gCronoSt        = CRONO_IDLE;
static uint32_t   gCronoAccum     = 0;      // ms consolidados (todo lo corrido antes de la pausa actual)
static uint32_t   gCronoT0        = 0;      // millis() del ultimo arranque/reanudacion
static uint32_t   gCronoLapBase   = 0;      // total (ms) en que empezo la vuelta en curso
static CronoLap   gCronoLaps[CRONO_MAX_LAPS];
static uint8_t    gCronoNLaps     = 0;      // vueltas guardadas ahora mismo
static uint16_t   gCronoLap0      = 1;      // numero visible de la vuelta del indice 0
static int8_t     gCronoBest      = -1;     // indice de la mas rapida (-1 = aun no procede compararlas)
static int8_t     gCronoWorst     = -1;     // indice de la mas lenta
static bool       gCronoLapsDirty = true;   // la tabla de vueltas debe repintarse
static bool       gCronoBtnDirty  = true;   // los botones deben repintarse (cambio de estado)
static bool       gCronoDialDirty = true;   // la esfera debe repintarse aunque no corra
static bool       gCronoBarDirty  = true;   // la barra de estado debe rehacerse (aparecer/desaparecer)

// ---- Pestanas de la app Reloj ----
static uint8_t    gRelojTab       = 0;      // 0 Hora · 1 Cronometro · 2 Temporizador

// ---- Capsula compacta de la barra de estado ----
static int        gCronoCapX      = 0;      // x donde la pinto la ultima barra que se dibujo entera
static int        gCronoCapWDrawn = 0;      // ancho con el que se pinto (cambia al pasar de 1 h)
static uint32_t   gCronoCapSec    = 0xFFFFFFFFUL;  // ultimo segundo estampado
static uint8_t    gCronoCapStDrawn= 0xFF;   // ultimo estado estampado
static bool       gCronoCapOn     = false;  // hay capsula pintada en la barra ahora mismo

// ---- Tarjeta expandida ----
static CronoCardPhase gCronoCard   = CC_HIDDEN;
static uint32_t       gCronoCardT0 = 0;     // millis() del inicio de la animacion en curso
static bool           gCronoCardBg = false; // la cache de fondo de la tarjeta esta al dia
static uint16_t*      gCronoCardBak = NULL; // banda de PANTALLA capturada al abrir (para cerrar sin parpadeo)
static uint16_t*      gCronoCardCache = NULL; // sub-banda con la tarjeta compuesta SIN los textos que cambian

// PERSISTENCIA (pendiente, estructurada a proposito): para sobrevivir a un
// reinicio bastaria con volcar a NVS { gCronoSt, cronoElapsed(), gCronoLapBase,
// gCronoLap0, gCronoNLaps, gCronoLaps[] } al pausar/parar y al apagar -- NUNCA
// cada segundo, que quemaria la flash -- y al arrancar recomponer gCronoAccum
// con el valor guardado y gCronoT0 = millis(). Todo el estado necesario esta en
// estas variables justamente para que ese paso sea un volcado y nada mas.

// ---- Deteccion real de hardware I2C ----
#define MAX_MODULES_DETECTED  8
#define I2C_SCAN_LO           0x08     // rango 7-bit valido (evita direcciones reservadas)
#define I2C_SCAN_HI           0x77
#define I2C_SCAN_PER_TICK     8        // direcciones sondeadas por vuelta de loop (no bloquea)
static const uint32_t I2C_SWEEP_INTERVAL = 3000;   // ms entre barridos completos

static DetectedModule detectedModules[MAX_MODULES_DETECTED];
static int      detectedCount = 0;
static uint16_t modSweepId[MAX_MODULES_DETECTED];  // ultimo barrido en que se vio cada modulo
static uint8_t  i2cScanCursor = 0;                 // direccion actual dentro del barrido
static bool     i2cSweeping   = false;             // hay un barrido en curso
static uint32_t i2cLastSweep  = 0;                 // fin del ultimo barrido
static uint16_t i2cSweepId    = 0;                 // id del barrido (para reconciliar presencia)

// Radio (WiFi por el co-procesador ESP32-C6/esp-hosted). Declarado aqui
// ARRIBA -a proposito- porque Ajustes (mas abajo en el archivo, pero
// ANTES que la seccion de radio al final) necesita leerlo para mostrar
// el estado real de la conexion. La logica de arranque/escaneo/conexion
// vive toda junto a bootInitRadioSafe(), al final del archivo.
// El fabricante exige 3.2.1 para esta placa. En 3.2.0 WiFi ignora los
// pines SDIO del variant y puede abortar la tarea hosted al despertar el C6
// (arduino-esp32 #11404/#11513). Es preferible dejar Wi-Fi no disponible a
// volver a producir un bucle de PANIC imposible de recuperar desde la UI.
#if defined(CONFIG_IDF_TARGET_ESP32P4) && \
    (ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 2, 1))
  #define FLEXOS_ENABLE_WIFI 0
  #define FLEXOS_WIFI_CORE_UNSAFE 1
#else
  #define FLEXOS_ENABLE_WIFI 1
  #define FLEXOS_WIFI_CORE_UNSAFE 0
#endif
static volatile bool gNetOnline = false;   // true tras un WiFi.begin() exitoso; lo lee la UI (Ajustes, icono, etc.)
// En el P4 la radio remota y la microSD usan SDIO/SDMMC. No se consulta
// WiFi.getMode() para saber si el controlador esta vivo porque ESA consulta
// ya puede despertar esp-hosted y reclamar el bus. Esta bandera pertenece a
// Flex OS sin consultar continuamente el controlador remoto del C6.
static volatile bool gWifiDriverOn = false;
static volatile bool gWifiAutoBusy = false;   // tarea de conexion manual con red guardada
static volatile bool gWifiAutoDone = true;    // solo se habilita tras validar el transporte a mano
static bool gWifiHostedPinsOk = false;        // los siete pines del C6 quedaron fijados antes del driver
static bool gWifiAutoTrusted = false;         // hubo al menos una conexion manual correcta
static bool gWifiAutoInterrupted = false;     // el ultimo intento automatico termino en reset
static bool gWifiBootAttempt = false;         // la tarea actual fue lanzada por el arranque
// Estado de las OTRAS dos radios, aqui arriba por el mismo motivo que
// gNetOnline: la pantalla de Ajustes lee los tres para pintar la categoria
// "Red e Internet", y esta ANTES en el archivo que la seccion de radio.
//   gAirplane -> modo avion (persistido en NVS, clave "airpl")
//   gBleOn    -> hay advertising BLE activo AHORA MISMO
static bool gAirplane = false;
static bool gBleOn    = false;

// #############################################################
// ##  TRANSICIONES DE APP · tipo de capa visual
// ##  ------------------------------------------------------
// ##  Aqui arriba por la MISMA restriccion de ctags que Touch,
// ##  Notification, CronoLap y HomeWidget: el IDE de Arduino
// ##  genera los prototipos en la linea 699 y un tipo que salga
// ##  en una firma tiene que estar definido ANTES. AppTrLayer
// ##  aparece en appTrP(), appTrRect(), appTrAim() y appTrFill().
// ##  El MOTOR (estado, geometria y composicion) vive junto al
// ##  framework de ventanas, que es donde se usa.
// ##  Lo vigila tests/host/check_protos.py.
// #############################################################
// Una capa visual de transicion. p = 0 es el rectangulo del icono,
// p = 1 la pantalla entera.
struct AppTrLayer {
  bool     on;
  int      app;
  int      ix, iy, is;      // rectangulo del icono (origen al abrir, destino al cerrar)
  uint16_t bg;              // color de la tarjeta (fondo de ventana de esa app)
  float    p0, p1;          // progreso de partida y destino
  uint32_t t0us, durus;     // reloj monotonico real
  int      by0, by1;        // banda que ocupo en el cuadro anterior
};

// #############################################################
// ##  DIAGNOSTICO WI-FI / microSD  ·  APAGADO por defecto
// ##  ------------------------------------------------------
// ##  Con FLEXOS_DIAG_WIFI_SD en 0 todo esto compila a NADA: ni una
// ##  variable, ni una rama, ni un byte. A 1 imprime por Serial la foto
// ##  de memoria y el estado de tarjeta y radio en cada punto critico del
// ##  arranque del enlace con el C6.
// ##
// ##  Para que sirve: el PANIC al encender el Wi-Fi no se puede reproducir
// ##  en el PC (no hay C6, ni SDIO, ni watchdog de hardware). Esto es lo
// ##  que permite sacar de la placa la traza que si lo demuestra: si el
// ##  ultimo checkpoint impreso antes del reinicio es "antes de
// ##  WiFi.mode(STA)", el bloqueo esta en el arranque del transporte; si
// ##  es otro, esta donde diga.
// ##
// ##  Nunca imprime dentro de una ISR ni de una seccion critica.
// #############################################################
#define FLEXOS_DIAG_WIFI_SD 0

#if FLEXOS_DIAG_WIFI_SD
static void flexDiagWifi(const char* where){
  Serial.printf("[DIAG %8lu] %-28s PSRAM %u KB (bloque %u KB)  SRAM %u KB (bloque %u KB)  "
                "SD est=%d busy=%d  radio=%d pines=%d\n",
                (unsigned long)millis(), where ? where : "",
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024u),
                (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024u),
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024u),
                (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024u),
                flexSdState(), (int)flexSdBusyGet(),
                (int)gWifiDriverOn, (int)gWifiHostedPinsOk);
}
#define FLEXDIAG_WIFI(w) flexDiagWifi(w)
#else
#define FLEXDIAG_WIFI(w) ((void)0)
#endif
