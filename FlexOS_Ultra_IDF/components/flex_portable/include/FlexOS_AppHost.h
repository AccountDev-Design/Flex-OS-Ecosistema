#ifndef FLEXOS_APPHOST_H
#define FLEXOS_APPHOST_H
// #############################################################
//  FLEX OS · GESTOR DE APLICACIONES flex-app-v1  (FlexAppManager)
//  ------------------------------------------------------------
//  QUE ES. El dueño del ciclo de vida de una app descargable: la
//  arranca, le entrega eventos, le da un presupuesto por tick, le
//  aplica los permisos LLAMADA A LLAMADA y la para —siempre soltando
//  todo lo suyo— pase lo que pase.
//
//  FLEX OS NUNCA PIERDE EL CONTROL. Cada tick la app recibe un
//  presupuesto doble (instrucciones y microsegundos). Si lo agota, el
//  control vuelve al sistema con el estado guardado y la app continúa
//  en el tick siguiente. Si insiste en pasarse, se la detiene con un
//  motivo concreto y Flex OS SIGUE FUNCIONANDO: no se reinicia nada.
//
//  LO QUE ESTE MODULO NO HACE, A PROPOSITO:
//    · no reserva memoria (las regiones las entrega quien lo usa);
//    · no dibuja (llama a la vtable del puente);
//    · no toca el sistema de archivos, ni el watchdog, ni FreeRTOS;
//    · no crea tareas: la app corre DENTRO del tick de la app, en la
//      misma tarea de interfaz que cualquier otra app del sistema.
//  Por eso es lógica pura y se prueba en el PC con sanitizers
//  (tests/host/test_apphost.cpp).
// #############################################################
#include <stdint.h>
#include <stddef.h>
#include "FlexOS_AppVM.h"
#include "FlexOS_AppGrant.h"

// ---- Techos del gestor ---------------------------------------------------
#define FLEXAPP_ID_MAX          96
#define FLEXAPP_VER_MAX         31
#define FLEXAPP_TEXT_MAX        192      // texto que una app puede pasar de una vez
#define FLEXAPP_NAME_MAX        32       // nombre de archivo en su carpeta privada
#define FLEXAPP_EVENT_QUEUE     16
#define FLEXAPP_TIMERS          8
#define FLEXAPP_PSRAM_SLOTS     4
#define FLEXAPP_TOUCH_POINTS    5
#define FLEXAPP_REASON_MAX      128

// Presupuestos por defecto. El manifest puede pedir MENOS, nunca más.
#define FLEXAPP_DEF_INSTR_TICK   120000u   // instrucciones por tick
#define FLEXAPP_DEF_US_TICK      6000u     // microsegundos de CPU por tick
#define FLEXAPP_DEF_DRAW_FRAME   1024u     // comandos de dibujo por cuadro
#define FLEXAPP_MAX_INSTR_TICK   400000u
#define FLEXAPP_MAX_US_TICK      12000u
#define FLEXAPP_MAX_DRAW_FRAME   4096u
// Techo de pixeles de SALIDA de un solo blit. El presupuesto por cuadro cobra
// por bloques de 4096 pixeles, pero eso solo no basta: un unico blit escalado
// a 4096x4096 serian 16 millones de pixeles, y el sistema se quedaria dentro
// de ese comando. Un sprite no puede pintar mas que una pantalla entera.
#define FLEXAPP_MAX_BLIT_PIXELS  (480u * 800u)
// Un manejador que no termina en este número de ticks seguidos deja de ser
// "lento" y pasa a ser "colgado": se detiene la app.
#define FLEXAPP_MAX_OVERRUN_TICKS 60
// Y un tope absoluto por invocación, para que un bucle no pueda comerse el
// sistema aunque ceda cortésmente cada tick.
#define FLEXAPP_MAX_INSTR_CALL   (24u * 1000u * 1000u)

// Valor que devuelve un servicio cuando el dato NO EXISTE (no hay sensor, no
// hay PSRAM, el panel no reporta multitáctil...). Nunca se inventa un número.
#define FLEXAPP_NO_VALUE  ((int32_t)0x80000000)

// ---- Estados -------------------------------------------------------------
enum FlexAppState : uint8_t {
  FLEXAPP_ST_INSTALLED = 0,      // registrada, nunca arrancada en esta sesión
  FLEXAPP_ST_STOPPED,            // parada de forma normal
  FLEXAPP_ST_STARTING,           // validando bytecode / permisos / onStart
  FLEXAPP_ST_RUNNING,
  FLEXAPP_ST_SUSPENDED,          // en segundo plano: no recibe ticks
  FLEXAPP_ST_STOPPED_SECURITY,   // el sistema la paró por pasarse de límites
  FLEXAPP_ST_ERROR,              // no arrancó, o murió por una trampa
  FLEXAPP_ST_UNINSTALLED
};

enum FlexAppStopReason : uint8_t {
  FLEXAPP_STOP_USER = 0,       // el usuario salió
  FLEXAPP_STOP_SYSTEM,         // el sistema necesita la pantalla o la memoria
  FLEXAPP_STOP_BUDGET,         // se pasó del presupuesto de tiempo/instrucciones
  FLEXAPP_STOP_TRAP,           // error de ejecución dentro de la app
  FLEXAPP_STOP_LOAD,           // no llegó a arrancar
  FLEXAPP_STOP_CANCELLED,      // cancelada durante el arranque
  FLEXAPP_STOP_UNINSTALL
};

// ---- Eventos que recibe onEvent(type, a, b) ------------------------------
enum FlexAppEventType : uint8_t {
  FLEXAPP_EV_NONE = 0,
  FLEXAPP_EV_TOUCH_DOWN,   // a = x, b = y
  FLEXAPP_EV_TOUCH_MOVE,
  FLEXAPP_EV_TOUCH_UP,
  FLEXAPP_EV_TOUCH_TAP,
  FLEXAPP_EV_BACK,         // botón atrás del sistema
  FLEXAPP_EV_PAUSE,        // pasa a segundo plano
  FLEXAPP_EV_RESUME,
  FLEXAPP_EV_TIMER,        // a = id del temporizador
  FLEXAPP_EV_CLOSE         // el sistema va a cerrarla: última oportunidad
};

// ---- Syscalls -----------------------------------------------------------
// Los identificadores son ESTABLES: el SDK escribe estos mismos números y hay
// vectores dorados que fijan la correspondencia (tests/host/test_apphost.cpp).
enum FlexAppSys : uint16_t {
  // -- Render 2D (sin permiso: es el lienzo de la propia app) --
  FLEXSYS_GFX_CLEAR        = 0x0001,  // (color)
  FLEXSYS_GFX_FILL_RECT    = 0x0002,  // (x,y,w,h,color)
  FLEXSYS_GFX_RECT         = 0x0003,  // (x,y,w,h,color)
  FLEXSYS_GFX_LINE         = 0x0004,  // (x0,y0,x1,y1,color)
  FLEXSYS_GFX_ROUND_RECT   = 0x0005,  // (x,y,w,h,r,color)
  FLEXSYS_GFX_FILL_RECT_A  = 0x0006,  // (x,y,w,h,color,alpha)
  FLEXSYS_GFX_TEXT_K       = 0x0007,  // (kOff,kLen,x,y,size,color)
  FLEXSYS_GFX_TEXT_M       = 0x0008,  // (addr,len,x,y,size,color)
  FLEXSYS_GFX_TEXT_WIDTH   = 0x0009,  // (kOff,kLen,size) -> px
  FLEXSYS_GFX_CLIP         = 0x000A,  // (x,y,w,h)
  FLEXSYS_GFX_CLIP_RESET   = 0x000B,  // ()
  FLEXSYS_GFX_BLIT         = 0x000C,  // (addr,w,h,x,y)
  FLEXSYS_GFX_BLIT_SCALED  = 0x000D,  // (addr,w,h,x,y,dw,dh)
  FLEXSYS_GFX_BLIT_ROT     = 0x000E,  // (addr,w,h,x,y,quadrant)
  FLEXSYS_GFX_BLIT_ALPHA   = 0x000F,  // (addr,w,h,x,y,alpha)
  FLEXSYS_GFX_WIDTH        = 0x0010,  // () -> px del lienzo
  FLEXSYS_GFX_HEIGHT       = 0x0011,
  FLEXSYS_GFX_COLOR        = 0x0012,  // (r,g,b) -> RGB565
  FLEXSYS_GFX_PIXEL        = 0x0013,  // (x,y,color)
  FLEXSYS_GFX_PRESENT      = 0x0014,  // () -> publica el cuadro

  // -- Componentes de interfaz con el tema del sistema --
  FLEXSYS_UI_CARD          = 0x0020,  // (x,y,w,h)
  FLEXSYS_UI_TITLE         = 0x0021,  // (kOff,kLen,x,y)
  FLEXSYS_UI_LABEL         = 0x0022,
  FLEXSYS_UI_CAPTION       = 0x0023,
  FLEXSYS_UI_BUTTON        = 0x0024,  // (kOff,kLen,x,y,w,h,pressed)
  FLEXSYS_UI_LIST_ROW      = 0x0025,  // (kOff,kLen,x,y,w,h,selected)
  FLEXSYS_UI_NAV_TITLE     = 0x0026,  // (kOff,kLen) título de la cabecera

  // -- Tiempo y temporizadores --
  FLEXSYS_TIME_MS          = 0x0030,  // () -> ms desde onStart
  FLEXSYS_TIME_US_LO       = 0x0031,  // () -> 32 bits bajos del reloj monotónico
  FLEXSYS_TIME_US_HI       = 0x0032,
  FLEXSYS_TIMER_SET        = 0x0033,  // (id, periodMs, repeat) -> 1/0
  FLEXSYS_TIMER_CLEAR      = 0x0034,  // (id) -> 1/0
  FLEXSYS_BUDGET_LEFT      = 0x0035,  // () -> instrucciones que quedan este tick

  // -- Entrada --
  FLEXSYS_INPUT_POINTS     = 0x0038,  // () -> nº de contactos, o NO_VALUE si el panel no lo reporta
  FLEXSYS_INPUT_POINT_X    = 0x0039,  // (i)
  FLEXSYS_INPUT_POINT_Y    = 0x003A,  // (i)
  FLEXSYS_INPUT_POINT_ID   = 0x003B,  // (i)

  // -- Almacenamiento privado (permiso storage.app) --
  FLEXSYS_STORE_WRITE      = 0x0040,  // (nameOff,nameLen,addr,len) -> 1/0
  FLEXSYS_STORE_READ       = 0x0041,  // (nameOff,nameLen,addr,cap) -> bytes o -1
  FLEXSYS_STORE_SIZE       = 0x0042,  // (nameOff,nameLen) -> bytes o -1
  FLEXSYS_STORE_DELETE     = 0x0043,  // (nameOff,nameLen) -> 1/0
  FLEXSYS_STORE_USED       = 0x0044,  // () -> bytes usados
  FLEXSYS_STORE_QUOTA      = 0x0045,  // () -> bytes de cuota

  // -- Sistema --
  FLEXSYS_SCREEN_W         = 0x0050,
  FLEXSYS_SCREEN_H         = 0x0051,
  FLEXSYS_OS_VERSION       = 0x0052,  // (addr,cap) -> longitud escrita
  FLEXSYS_MODEL            = 0x0053,  // (addr,cap) -> longitud escrita
  FLEXSYS_CAPS             = 0x0054,  // () -> máscara FLEXAPP_CAP_*
  FLEXSYS_LOG              = 0x0055,  // (kOff,kLen)
  FLEXSYS_NOTIFY           = 0x0056,  // (tOff,tLen,sOff,sLen) -> 1/0
  FLEXSYS_EXIT             = 0x0057,  // () cierra la app de forma limpia

  // -- Pantalla (permisos display.*) --
  FLEXSYS_DISPLAY_LANDSCAPE = 0x0060, // (on) -> 1/0
  FLEXSYS_DISPLAY_EXCLUSIVE = 0x0061, // (on) -> 1/0
  FLEXSYS_DISPLAY_ORIENT    = 0x0062, // () -> 0 vertical, 1 horizontal

  // -- Servicios privilegiados --
  FLEXSYS_PERF_METRIC      = 0x0070,  // (campo) -> valor      [system.performance.metrics]
  FLEXSYS_PSRAM_FREE       = 0x0071,  // () -> KB              [system.psram.measure]
  FLEXSYS_PSRAM_TOTAL      = 0x0072,  // () -> KB
  FLEXSYS_PSRAM_LARGEST    = 0x0073,  // () -> KB
  FLEXSYS_PSRAM_RESERVE    = 0x0074,  // (kb) -> handle>0, o 0 si NO hay
  FLEXSYS_PSRAM_RELEASE    = 0x0075,  // (handle) -> 1/0
  FLEXSYS_PSRAM_POKE       = 0x0076,  // (handle,off,valor) -> 1/0
  FLEXSYS_PSRAM_PEEK       = 0x0077,  // (handle,off) -> valor o NO_VALUE
  FLEXSYS_TEMP_MILLIC      = 0x0078,  // () -> m°C o NO_VALUE  [system.temperature.read]
  FLEXSYS_CPU_STRESS_BEGIN = 0x0079,  // (kind,slices) -> 1/0  [system.cpu.stress]
  FLEXSYS_CPU_STRESS_SLICE = 0x007A,  // () -> slices hechos, -1 si terminó/cancelado
  FLEXSYS_CPU_STRESS_LO    = 0x007B,  // () -> 32 bits bajos del acumulador
  FLEXSYS_CPU_STRESS_HI    = 0x007C,
  FLEXSYS_CPU_STRESS_STOP  = 0x007D,  // () cancela ya
  FLEXSYS_BENCH_BEGIN      = 0x007E,  // (id) -> 1/0           [benchmark.run]
  FLEXSYS_BENCH_END        = 0x007F   // () -> 1
};

// Capacidades REALES del equipo (FLEXSYS_CAPS). Un bit a 0 significa "este
// equipo no lo tiene", no "no lo hemos mirado".
enum : uint32_t {
  FLEXAPP_CAP_PSRAM        = 1u << 0,
  FLEXAPP_CAP_MULTITOUCH   = 1u << 1,
  FLEXAPP_CAP_TEMP_SENSOR  = 1u << 2,
  FLEXAPP_CAP_LANDSCAPE    = 1u << 3,
  FLEXAPP_CAP_STORAGE      = 1u << 4,
  FLEXAPP_CAP_NOTIFICATIONS= 1u << 5
};

// Campos de FLEXSYS_PERF_METRIC.
enum : int32_t {
  FLEXAPP_PERF_LOOP_RATE   = 0,   // vueltas de loop() por segundo
  FLEXAPP_PERF_CPU_MHZ     = 1,
  FLEXAPP_PERF_HEAP_FREE_KB= 2,
  FLEXAPP_PERF_UPTIME_S    = 3,
  FLEXAPP_PERF_FRAME_US    = 4    // coste del último cuadro compuesto
};

struct FlexAppPoint { int32_t x, y, id; };

// ---- Vtable del anfitrión ------------------------------------------------
// La rellena el puente del sketch. Un puntero a NULL significa "este equipo no
// ofrece ese servicio", y el gestor devuelve el valor de "no disponible" en vez
// de inventarse uno.
struct FlexAppHostApi {
  uint64_t (*nowUs)(void* u);

  void (*gfxClear)(void* u, uint16_t color);
  void (*gfxFillRect)(void* u, int x, int y, int w, int h, uint16_t color, int alpha);
  void (*gfxRect)(void* u, int x, int y, int w, int h, uint16_t color);
  void (*gfxLine)(void* u, int x0, int y0, int x1, int y1, uint16_t color);
  void (*gfxRoundRect)(void* u, int x, int y, int w, int h, int r, uint16_t color);
  void (*gfxPixel)(void* u, int x, int y, uint16_t color);
  void (*gfxText)(void* u, const char* text, int x, int y, int size, uint16_t color);
  int  (*gfxTextWidth)(void* u, const char* text, int size);
  void (*gfxClip)(void* u, int x, int y, int w, int h);   // w<0 => restaurar
  // Un sprite es memoria de la APP ya validada por el gestor: `px` apunta
  // dentro de su memoria lineal y `w*h` cabe entero. quadrant 0/1/2/3 = 0/90/180/270.
  void (*gfxBlit)(void* u, const uint8_t* px, int w, int h, int x, int y,
                  int dw, int dh, int quadrant, int alpha);
  void (*gfxPresent)(void* u);
  int  (*gfxCanvasW)(void* u);
  int  (*gfxCanvasH)(void* u);

  // kind: 0 card · 1 title · 2 label · 3 caption · 4 button · 5 list row
  void (*uiWidget)(void* u, int kind, const char* text, int x, int y, int w, int h, int state);
  void (*uiNavTitle)(void* u, const char* text);

  void (*logLine)(void* u, const char* text);
  bool (*notify)(void* u, const char* title, const char* sub);
  int  (*osInfo)(void* u, int which, char* out, int cap);   // 0 = versión, 1 = modelo
  uint32_t (*caps)(void* u);
  int  (*screenW)(void* u);
  int  (*screenH)(void* u);

  bool (*setLandscape)(void* u, bool on);
  bool (*setExclusive)(void* u, bool on);

  // Almacenamiento privado. El gestor ya saneó `name` y comprobó la cuota.
  int  (*storeWrite)(void* u, const char* name, const uint8_t* data, uint32_t len);
  int  (*storeRead)(void* u, const char* name, uint8_t* out, uint32_t cap);
  int  (*storeSize)(void* u, const char* name);
  int  (*storeDelete)(void* u, const char* name);
  uint32_t (*storeUsed)(void* u);

  int  (*touchPoints)(void* u, FlexAppPoint* out, int maxn);  // <0 = sin dato fiable

  int32_t (*perfMetric)(void* u, int32_t field);
  int32_t (*psramKb)(void* u, int which);                     // 0 libre · 1 total · 2 mayor bloque
  int32_t (*psramReserve)(void* u, uint32_t kb);              // handle>0, o 0
  bool    (*psramRelease)(void* u, int32_t handle);
  bool    (*psramPoke)(void* u, int32_t handle, uint32_t off, int32_t value);
  bool    (*psramPeek)(void* u, int32_t handle, uint32_t off, int32_t* out);
  int32_t (*tempMilliC)(void* u);                             // NO_VALUE si no hay sensor real
  // UNA rodaja acotada de carga. Devuelve el trabajo hecho y acumula en `acc`.
  // Nunca bloquea más de `budgetUs`. El gestor cede entre rodaja y rodaja.
  int32_t (*cpuStressSlice)(void* u, int32_t kind, uint32_t budgetUs, uint64_t* acc);
};

// ---- Lo que hace falta para lanzar una app -------------------------------
struct FlexAppLaunch {
  const char* packageId;
  const char* versionName;
  uint32_t    versionCode;
  uint8_t     packageSha256[32];
  uint8_t     developerKeySha256[32];
  uint32_t    manifestPermissions;   // lo que el manifest DECLARA pedir
  uint64_t    nowEpoch;              // 0 = reloj no fiable todavía

  const uint8_t* image;   uint32_t imageLen;     // bytecode .flxb
  const uint8_t* grant;   uint32_t grantLen;     // 0 = sin permisos privilegiados
  const uint8_t* trustedPub;                     // 65 bytes pinneados de Flex Store

  // Regiones de trabajo, ya reservadas por el puente (una sola vez).
  uint8_t*     mem;      uint32_t memBytes;
  int32_t*     globals;  uint16_t globalCount;
  int32_t*     stack;    uint16_t stackSlots;
  int32_t*     locals;   uint16_t frameSlots;
  FlexVmFrame* frames;   uint16_t callDepth;
  uint8_t*     scratch;  uint32_t scratchLen;    // validación: (codeLen+7)/8

  // Presupuestos pedidos por el manifest (0 = usar el valor por defecto).
  uint32_t instrPerTick;
  uint32_t usPerTick;
  uint32_t drawPerFrame;
  uint32_t storageQuota;   // bytes
};

struct FlexAppTimer { uint32_t periodMs, nextMs; uint8_t repeat, active; };
struct FlexAppEvent { uint8_t type; int32_t a, b; };

struct FlexAppManager {
  const FlexAppHostApi* api;
  void* user;

  FlexVm vm;
  uint8_t state;
  uint8_t stopReason;
  char    reason[FLEXAPP_REASON_MAX];

  char     id[FLEXAPP_ID_MAX + 1];
  char     versionName[FLEXAPP_VER_MAX + 1];
  uint32_t versionCode;

  uint32_t granted;        // permisos REALMENTE concedidos por el grant
  uint8_t  grantStatus;
  uint8_t  grantWindowChecked;

  uint32_t instrPerTick, usPerTick, drawPerFrame, storageQuota;

  uint64_t startUs;        // reloj monotónico en onStart
  uint64_t lastTickUs;
  uint32_t drawUsed, drawDropped;
  uint32_t instrThisTick;
  uint32_t overrunTicks;
  uint64_t instrThisCall;
  uint64_t instrTotal;
  uint32_t ticks, events, frames;

  FlexAppTimer timers[FLEXAPP_TIMERS];
  FlexAppEvent queue[FLEXAPP_EVENT_QUEUE];
  uint8_t qHead, qTail, qCount, qDropped;

  int32_t  psram[FLEXAPP_PSRAM_SLOTS];   // handles vivos (0 = libre)
  uint32_t psramKbHeld;

  uint8_t  stressActive, stressKind;
  uint32_t stressSlices, stressDone;
  uint64_t stressAcc;

  uint8_t  benchActive;
  int32_t  benchId;

  uint8_t  wantLandscape, wantExclusive;
  uint8_t  landscapeApplied, exclusiveApplied;

  uint8_t  exitRequested;      // la app pidió cerrarse
  uint8_t  presentRequested;   // pidió publicar el cuadro
  uint8_t  inSyscall;          // reentrada: una syscall no puede llamar a la VM

  char textBuf[FLEXAPP_TEXT_MAX + 1];
};

// ---- API del gestor ------------------------------------------------------
void flexAppInit(FlexAppManager* m, const FlexAppHostApi* api, void* user);

// Valida bytecode + grant y ejecuta onStart. Devuelve false y deja el estado
// en ERROR (con motivo legible) si algo falla; NO deja nada a medias.
bool flexAppStart(FlexAppManager* m, const FlexAppLaunch* launch);

// Encola un evento. Si la cola está llena se descarta el más antiguo y se
// cuenta: una app lenta no puede hacer crecer memoria del sistema.
void flexAppPostEvent(FlexAppManager* m, FlexAppEventType type, int32_t a, int32_t b);

// Un tick del sistema. Ejecuta como mucho el presupuesto del tick y vuelve.
void flexAppTick(FlexAppManager* m);

// Segundo plano: la app deja de recibir ticks y suelta la pantalla exclusiva
// y la orientación. No conserva recursos privilegiados sin aprobación.
void flexAppSuspend(FlexAppManager* m);
void flexAppResume(FlexAppManager* m);

// Parada. Ejecuta onStop con presupuesto propio y libera TODO: reservas de
// PSRAM, orientación, pantalla exclusiva y sesión de carga. Es idempotente y
// funciona desde cualquier estado, incluido un arranque a medias.
void flexAppStop(FlexAppManager* m, FlexAppStopReason reason);

FlexAppState flexAppState(const FlexAppManager* m);
const char*  flexAppStateName(uint8_t state);
const char*  flexAppReason(const FlexAppManager* m);
bool         flexAppWantsPresent(FlexAppManager* m);   // consume la petición
bool         flexAppExitRequested(const FlexAppManager* m);
uint32_t     flexAppGranted(const FlexAppManager* m);
bool         flexAppHasPermission(const FlexAppManager* m, uint32_t bit);

// Aridad de una syscall (0xFF = no existe). Se exporta porque el validador de
// bytecode la necesita ANTES de ejecutar nada.
uint8_t flexAppSyscallArity(uint16_t id);
const char* flexAppSyscallName(uint16_t id);
// Permiso que exige una syscall (0 = ninguno).
uint32_t flexAppSyscallPermission(uint16_t id);

#endif
