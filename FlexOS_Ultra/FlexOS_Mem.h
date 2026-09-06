#pragma once
// #############################################################
//  FLEX OS · NUCLEO DE MEMORIA Y MULTITAREA  (FlexOS_Mem.h/.cpp)
//  ------------------------------------------------------------
//  QUE ES. La parte PENSANTE de la multitarea: presupuesto de
//  memoria, clasificacion de presion, fragmentacion, veredicto de
//  "cabe o no cabe esta app" y la maquina de avisos con enfriamiento.
//
//  POR QUE VIVE FUERA DEL SKETCH. Es logica PURA -- no toca Arduino,
//  ni el framebuffer, ni el sistema de archivos, ni una sola reserva
//  de memoria -- exactamente como FlexOS_Media.cpp o FlexOS_FlexLink.cpp.
//  Eso permite compilarla y ejercitarla EN EL PC con sanitizers
//  (tests/host/test_mem.cpp): las reglas que deciden si el sistema se
//  protege o no son justo las que no pueden verificarse "a ojo" en placa.
//
//  LO QUE NO HACE, A PROPOSITO:
//    · no lee heap_caps_* (se lo pasan ya medido en un FlexMemSnap);
//    · no libera nada (las acciones son del sketch, que es quien tiene
//      los punteros);
//    · no dibuja ni formatea texto de interfaz salvo tamanos.
//  Asi una regla se puede cambiar aqui sin tocar ni un pixel, y un
//  buffer se puede soltar alli sin tocar ni una regla.
//
//  UNIDADES: todo en BYTES, en uint32_t. 32 MB caben de sobra.
// #############################################################
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// -------------------------------------------------------------
//  1) CLASE DE PESO DE UNA APP
//  ------------------------------------------------------------
//  No es una etiqueta decorativa: decide cuanta memoria hay que
//  tener libre ANTES de abrirla y si el sistema puede negarse.
// -------------------------------------------------------------
enum {
  FLEXMEM_W_LIGHT  = 0,   // Reloj, Calculadora, Notas, Calendario, Ajustes
  FLEXMEM_W_MEDIUM = 1,   // Paint, Educacion, Bienestar, Juegos
  FLEXMEM_W_HEAVY  = 2    // Navegador, Galeria, Multimedia, Camara, DeX
};

// -------------------------------------------------------------
//  2) NIVEL DE PRESION
//  ------------------------------------------------------------
//  Los cortes son los del diseno del sistema (32 MB de PSRAM):
//    > 10 MB  multitarea normal
//    6-10 MB  aviso discreto, se sigue abriendo
//    5-6 MB   aviso visible; una app pesada exige soltar recursos antes
//    < 5 MB   proteccion: no se abre una app pesada
//  El bloque contiguo y la SRAM interna pueden empujar a CRITICO por su
//  cuenta aunque haya MB libres: 8 MB partidos en trozos de 100 KB no
//  sirven para decodificar una foto.
// -------------------------------------------------------------
enum {
  FLEXMEM_LV_OK = 0,
  FLEXMEM_LV_NOTICE,
  FLEXMEM_LV_WARN,
  FLEXMEM_LV_CRITICAL
};

// Clase de fragmentacion (lo que se ensena al usuario).
enum { FLEXMEM_FRAG_LOW = 0, FLEXMEM_FRAG_MED, FLEXMEM_FRAG_HIGH };

// Salud global que se pinta con color (verde / ambar / rojo).
enum { FLEXMEM_H_GOOD = 0, FLEXMEM_H_WATCH, FLEXMEM_H_CRIT };

// -------------------------------------------------------------
//  3) VEREDICTO DE APERTURA
//  ------------------------------------------------------------
//  NEED_SHED no es un "no": es "todavia no". El sistema suelta primero
//  los recursos RECONSTRUIBLES de las apps menos recientes, vuelve a
//  medir y pregunta otra vez. Solo si sigue sin caber se niega, y
//  entonces el motivo dice QUE falta -- no un "sin memoria" generico.
// -------------------------------------------------------------
enum {
  FLEXMEM_OK = 0,
  FLEXMEM_NEED_SHED,     // cabe si antes se liberan recursos reconstruibles
  FLEXMEM_DENY_PSRAM,    // PSRAM libre por debajo del suelo de proteccion
  FLEXMEM_DENY_BLOCK,    // hay MB libres, pero ninguno contiguo suficiente
  FLEXMEM_DENY_SRAM      // SRAM interna en zona peligrosa (Wi-Fi, tactil, tareas)
};

// -------------------------------------------------------------
//  4) LA MEDIDA
//  ------------------------------------------------------------
//  TODO lo de aqui sale de una medicion real del SDK. Un campo a 0
//  con su bandera de validez a 0 significa "no disponible", y la
//  interfaz lo dice con esas palabras: nunca se rellena con un valor
//  plausible.
// -------------------------------------------------------------
typedef struct {
  uint32_t psTotal;      // PSRAM inicializada de verdad (no la del datasheet)
  uint32_t psFree;
  uint32_t psLargest;    // mayor bloque contiguo libre
  uint32_t psPeakUsed;   // pico de uso desde el arranque (maximo de total-free)
  uint32_t inTotal;      // SRAM interna
  uint32_t inFree;
  uint32_t inMin;        // minimo libre registrado desde el arranque
  uint32_t fsTotal;      // particion de datos (LittleFS)
  uint32_t fsUsed;
  uint8_t  fsValid;      // 0 = no medido en este ciclo (la lectura es cara)
} FlexMemSnap;

// -------------------------------------------------------------
//  5) PRESUPUESTO  (los numeros del diseno, en un solo sitio)
// -------------------------------------------------------------
#define FLEXMEM_RESERVE_BYTES  (6u * 1024u * 1024u)   // reserva del sistema
#define FLEXMEM_NOTICE_BYTES   (10u * 1024u * 1024u)
#define FLEXMEM_WARN_BYTES     (6u * 1024u * 1024u)
#define FLEXMEM_CRIT_BYTES     (5u * 1024u * 1024u)

// SRAM interna. Por debajo de LOW se limita lo pesado; por debajo de MIN
// no se abre nada pesado: ahi viven Wi-Fi, el tactil y las tareas de FreeRTOS.
#define FLEXMEM_SRAM_LOW_BYTES (64u * 1024u)
#define FLEXMEM_SRAM_MIN_BYTES (40u * 1024u)

// -------------------------------------------------------------
//  HISTERESIS
//  ------------------------------------------------------------
//  Los umbrales de ENTRADA (arriba) y los de SALIDA (aqui) NO son los
//  mismos, y esa es toda la idea: con un unico corte en 6 MB, una app que
//  oscila entre 5,99 y 6,01 MB haria entrar y salir del estado de aviso
//  varias veces por segundo -- y con el se encenderia y apagaria el modo
//  eficiente y se dispararian notificaciones en bucle.
//
//  EMPEORAR ES INMEDIATO (proteger tarde no protege). MEJORAR exige rebasar
//  un margen de 1 MB sobre el corte de entrada, que es aproximadamente lo
//  que cuesta abrir cualquier pantalla del sistema: asi el estado no puede
//  volver atras por el ruido de un repintado.
// -------------------------------------------------------------
#define FLEXMEM_EXIT_NOTICE_BYTES (11u * 1024u * 1024u)
#define FLEXMEM_EXIT_WARN_BYTES   (7u  * 1024u * 1024u)
#define FLEXMEM_EXIT_CRIT_BYTES   (6u  * 1024u * 1024u)
#define FLEXMEM_EXIT_SRAM_BYTES   (56u * 1024u)
#define FLEXMEM_EXIT_BLOCK_BYTES  (1536u * 1024u)

// Coste tipico que hay que poder cubrir ANTES de abrir. No es lo que la
// app va a gastar (eso se mide despues, al abrirla): es el colchon que
// hace falta para que su construccion no deje al sistema sin aire.
#define FLEXMEM_COST_LIGHT     (256u * 1024u)
#define FLEXMEM_COST_MEDIUM    (1024u * 1024u)
#define FLEXMEM_COST_HEAVY     (3072u * 1024u)

// Bloque CONTIGUO minimo. Una foto o un frame no se pueden repartir.
#define FLEXMEM_BLOCK_HEAVY    (1024u * 1024u)
#define FLEXMEM_BLOCK_MEDIUM   (384u * 1024u)

// -------------------------------------------------------------
//  6) CONSULTAS SOBRE UNA MEDIDA
// -------------------------------------------------------------
uint32_t flexMemUsed(const FlexMemSnap* s);          // PSRAM en uso
int      flexMemUsedPct(const FlexMemSnap* s);       // 0..100 (0 si no hay PSRAM)
uint32_t flexMemCost(int weight);                    // colchon por clase de peso
int      flexMemLevel(const FlexMemSnap* s);         // FLEXMEM_LV_*
int      flexMemHealth(const FlexMemSnap* s);        // FLEXMEM_H_*
int      flexMemFragPct(const FlexMemSnap* s);       // 0..100
int      flexMemFragClass(const FlexMemSnap* s);     // FLEXMEM_FRAG_*
int      flexMemFlashPct(const FlexMemSnap* s);      // 0..100, -1 si no medido

// EL VEREDICTO. 'weight' es FLEXMEM_W_*. Devuelve FLEXMEM_OK /
// FLEXMEM_NEED_SHED / FLEXMEM_DENY_*.
int      flexMemCanOpen(const FlexMemSnap* s, int weight);

// -------------------------------------------------------------
//  7) AVISOS CON ENFRIAMIENTO
//  ------------------------------------------------------------
//  Un aviso solo sirve si no se repite en bucle. Cada clase tiene su
//  propio enfriamiento y ademas hay una separacion global minima, para
//  que dos condiciones distintas no encadenen dos tarjetas seguidas.
//  El estado es del llamante (una struct plana, cero reservas).
// -------------------------------------------------------------
enum {
  FLEXMEM_AL_NONE = 0,
  FLEXMEM_AL_PS_NOTICE,   // 6-10 MB libres
  FLEXMEM_AL_PS_WARN,     // 5-6 MB libres
  FLEXMEM_AL_PS_CRIT,     // < 5 MB o sin bloque contiguo suficiente
  FLEXMEM_AL_FRAG,        // fragmentacion alta
  FLEXMEM_AL_SRAM,        // SRAM interna baja
  FLEXMEM_AL_FLASH80,
  FLEXMEM_AL_FLASH90,
  FLEXMEM_AL_N
};

// Enfriamientos (ms). Publicos para que la prueba de host los use.
#define FLEXMEM_CD_MEM     (5u * 60u * 1000u)
#define FLEXMEM_CD_FRAG    (10u * 60u * 1000u)
#define FLEXMEM_CD_FLASH   (15u * 60u * 1000u)
#define FLEXMEM_CD_GLOBAL  (30u * 1000u)        // separacion minima entre avisos

typedef struct {
  uint32_t lastMs[FLEXMEM_AL_N];
  uint16_t seen;                 // bit i = el aviso i ya se dio alguna vez
  uint32_t lastAnyMs;
  uint8_t  lastAnySeen;
  // ---- Maquina de nivel con histeresis (ver flexMemLevelStep) ----
  uint8_t  level;                // nivel PUBLICADO (ya con histeresis)
  uint8_t  levelSeen;            // 0 = todavia sin primera evaluacion
  uint8_t  rose;                 // 1 = la ultima evaluacion EMPEORO el nivel
  uint32_t reliefMs;             // millis del ultimo alivio automatico
  uint8_t  reliefSeen;
} FlexMemAlerts;

void flexMemAlertsReset(FlexMemAlerts* a);

// -------------------------------------------------------------
//  NIVEL CON HISTERESIS
//  ------------------------------------------------------------
//  flexMemLevel() es el nivel INSTANTANEO y sigue siendo lo que usan las
//  decisiones puntuales ("¿cabe esta app?"). flexMemLevelStep() es el nivel
//  SOSTENIDO: el que gobierna lo que el usuario VE, y por eso no puede
//  cambiar con el ruido. Actualiza el estado y devuelve el nivel nuevo;
//  a->rose queda a 1 solo si esta evaluacion lo ha EMPEORADO, que es el
//  unico instante en el que tiene sentido actuar o avisar.
// -------------------------------------------------------------
int  flexMemLevelStep(const FlexMemSnap* s, FlexMemAlerts* a);
int  flexMemLevelHyst(const FlexMemSnap* s, int prevLevel);   // sin tocar el estado

// ALIVIO AUTOMATICO. No es un aviso: no se ve. Mientras el sistema siga
// apretado se puede repetir, pero como mucho cada FLEXMEM_RELIEF_MS, para que
// una app que reserva sin parar no convierta el alivio en un bucle.
#define FLEXMEM_RELIEF_MS (60u * 1000u)
int  flexMemReliefDue(const FlexMemAlerts* a, uint32_t nowMs);
void flexMemReliefDone(FlexMemAlerts* a, uint32_t nowMs);
// AVISOS SECUNDARIOS: fragmentacion, SRAM interna y flash. Los de
// PSRAM ya NO salen de aqui -- los gobierna la maquina de nivel con histeresis
// (flexMemLevelStep), que es lo que impide que se repitan mientras la
// condicion dura. Devuelve FLEXMEM_AL_NONE si no toca ninguno.
int  flexMemAlertPick(const FlexMemSnap* s, uint32_t nowMs, FlexMemAlerts* a);

// -------------------------------------------------------------
//  8) FORMATO DE CIFRAS
//  ------------------------------------------------------------
//  Sin float en la ruta de dibujo y sin malloc: escribe en el buffer
//  que le den. "642 KB", "8.4 MB", "1.2 GB".
// -------------------------------------------------------------
void flexMemFmt(uint64_t bytes, char* out, size_t n);
// "7.6 MB / 32 MB"
void flexMemFmtPair(uint64_t a, uint64_t b, char* out, size_t n);

#ifdef __cplusplus
}
#endif
