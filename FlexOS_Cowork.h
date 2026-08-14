// #############################################################
// ##  FlexOS_Cowork.h  ·  MOTOR DE TRABAJOS EN SEGUNDO PLANO
// ##  ------------------------------------------------------
// ##  QUE ES
// ##  ------
// ##  La cola de trabajos de Flex Intelligence: resumir notas,
// ##  traducir, corregir, explicar un error del Code IDE,
// ##  analizar una noticia o una foto. El usuario encola y SIGUE
// ##  usando Flex OS -- el trabajo avanza por detras y avisa por
// ##  una notificacion flotante cuando termina.
// ##
// ##  QUE NO ES
// ##  ---------
// ##  No es una tarea de FreeRTOS y no habla por red. Este modulo
// ##  es LOGICA PURA: una maquina de estados, una cola acotada y
// ##  un almacen de resultados. Quien lo hace avanzar es el .ino
// ##  (coworkPump desde su tarea de baja prioridad) y quien habla
// ##  con el servidor es FlexOS_AI. Esa separacion es lo que
// ##  permite probar AQUI, en el PC, la cancelacion, los
// ##  reintentos, los tiempos maximos y el choque de un resultado
// ##  contra un documento que el usuario cambio mientras tanto.
// ##
// ##  REGLA DE ORO DEL SNAPSHOT
// ##  -------------------------
// ##  Antes de trabajar, el motor COPIA la entrada y se queda con
// ##  una huella del documento de origen. Si al terminar la huella
// ##  ya no coincide -- el usuario siguio escribiendo -- el
// ##  resultado NO se aplica solo: queda en CW_DONE marcado como
// ##  "el origen cambio" y la UI pide confirmacion. Nunca se pisa
// ##  lo que el usuario acaba de escribir.
// ##
// ##  EL RELOJ ENTRA POR PARAMETRO
// ##  ----------------------------
// ##  Ninguna funcion de aqui llama a millis(). El tiempo se pasa
// ##  a coworkPump(nowMs) y a coworkSubmit(). Es lo que hace que
// ##  las esperas progresivas y los tiempos maximos se puedan
// ##  probar de verdad en el PC: la prueba mueve el reloj a mano,
// ##  sin dormir ni un milisegundo.
// #############################################################

#ifndef FLEXOS_COWORK_H
#define FLEXOS_COWORK_H

#include <stdint.h>
#include <stddef.h>

// -------------------------------------------------------------
//  LIMITES  (todos fijos: ni una reserva dinamica en el motor)
// -------------------------------------------------------------
#define CW_MAX_JOBS      8      // cola completa; encolar la novena falla y se dice
#define CW_TITLE_MAX     40
#define CW_IN_MAX      1024     // entrada de un trabajo (el snapshot)
#define CW_OUT_MAX     1024     // resultado
#define CW_ERR_MAX       64
#define CW_SRC_MAX       48     // ruta/origen del documento

// Cuantos resultados terminados se conservan. Al llenarse se
// descarta el mas viejo YA VISTO; si todos estan sin ver, el mas
// viejo de todos. Un resultado no desaparece por descartar su
// tarjeta flotante: eso solo quita la tarjeta (ver el .ino).
#define CW_HIST_MAX      6

// -------------------------------------------------------------
//  TIPOS DE TRABAJO
// -------------------------------------------------------------
enum {
  CW_KIND_CORRECT = 0,   // corregir ortografia/gramatica
  CW_KIND_SUMMARY,       // resumir
  CW_KIND_TRANSLATE,     // traducir
  CW_KIND_TONE,          // cambiar el tono
  CW_KIND_EXPLAIN,       // explicar un error del Code IDE
  CW_KIND_ANALYZE,       // analizar texto / noticia
  CW_KIND_IMAGE,         // analizar una imagen
  CW_KIND_FIND,          // localizar archivos
  CW_KIND_N
};

// -------------------------------------------------------------
//  ESTADOS  (los ocho del enunciado, ni uno inventado)
// -------------------------------------------------------------
enum {
  CW_QUEUED = 0,   // en cola
  CW_PREP,         // preparando (snapshot hecho, aun no salio)
  CW_WORKING,      // trabajando (parte local)
  CW_WAITING,      // esperando al servidor
  CW_PAUSED,       // pausado (bateria, app pesada, sin red)
  CW_DONE,         // terminado
  CW_ERROR,        // error
  CW_CANCELLED,    // cancelado
  CW_STATE_N
};

// Motivos de pausa. Se guardan para poder decir la verdad en la
// notificacion ("Tarea pausada para ahorrar bateria") en vez de un
// generico "pausado".
enum { CW_PAUSE_NONE = 0, CW_PAUSE_USER, CW_PAUSE_BATTERY, CW_PAUSE_LOAD, CW_PAUSE_NET };

// Codigos de error. El texto largo va en job->err.
enum { CW_ERR_NONE = 0, CW_ERR_NOCFG, CW_ERR_NONET, CW_ERR_TLS, CW_ERR_HTTP,
       CW_ERR_PARSE, CW_ERR_TIMEOUT, CW_ERR_TOOBIG, CW_ERR_MEM, CW_ERR_DENIED };

// -------------------------------------------------------------
//  UN TRABAJO
// -------------------------------------------------------------
struct CoworkJob {
  uint32_t id;                  // identificador unico, jamas reutilizado
  uint8_t  kind;
  uint8_t  state;
  uint8_t  pauseWhy;
  uint8_t  err;
  uint8_t  tries;               // intentos ya gastados
  uint8_t  progress;            // 0..100, o 0xFF = "no se puede calcular"
  bool     seen;                // el usuario ya vio el resultado
  bool     srcChanged;          // el documento de origen cambio mientras se trabajaba
  bool     needsConfirm;        // el resultado propone una accion que exige confirmacion
  bool     usedVault;           // la entrada salio de Flex Vault (para el registro de seguridad)

  uint32_t bornMs;              // cuando se encolo
  uint32_t startMs;             // cuando empezo a trabajar (0 = aun no)
  uint32_t retryAtMs;           // no reintentar antes de este instante
  uint32_t budgetMs;            // tiempo maximo total de este trabajo

  char     title[CW_TITLE_MAX];
  char     src[CW_SRC_MAX];     // ruta del documento de origen ("" si no lo hay)
  uint32_t srcHash;             // huella del origen EN EL MOMENTO del snapshot

  uint16_t inLen;
  uint16_t outLen;
  char     in [CW_IN_MAX];      // SNAPSHOT: copia propia, no un puntero al documento
  char     out[CW_OUT_MAX];
  char     err_[CW_ERR_MAX];
};

// -------------------------------------------------------------
//  CICLO DE VIDA
// -------------------------------------------------------------

// Vacia la cola y el historial. La memoria de los trabajos la aporta
// el llamante (ver coworkAttachStorage): el motor no reserva nada.
void coworkBegin();

// Conecta el almacen de trabajos. En Flex OS Ultra son CW_MAX_JOBS
// estructuras en PSRAM (unos 17 KB): demasiado para la RAM interna,
// que es el recurso escaso, y perfectamente normal para la PSRAM.
// Sin almacen conectado, coworkSubmit() falla limpiamente.
void coworkAttachStorage(CoworkJob* slots, int n);
bool coworkReady();

// -------------------------------------------------------------
//  ENCOLAR
// -------------------------------------------------------------

// Encola un trabajo con su SNAPSHOT ya tomado.
//   in/inLen  -> se COPIA aqui dentro; el llamante puede liberar su buffer
//   src       -> ruta del documento de origen, o NULL
//   srcHash   -> huella del origen ahora mismo (ver coworkHash)
//   budgetMs  -> tiempo maximo total (0 = el de fabrica, CW_DEFAULT_BUDGET_MS)
// Devuelve el id del trabajo, o 0 si no se pudo (cola llena, entrada
// demasiado grande, sin almacen). Nunca bloquea.
uint32_t coworkSubmit(uint8_t kind, const char* title,
                      const char* in, size_t inLen,
                      const char* src, uint32_t srcHash,
                      uint32_t budgetMs, uint32_t nowMs);

#define CW_DEFAULT_BUDGET_MS 45000u
#define CW_MAX_TRIES         3
// Espera progresiva entre reintentos: 2 s, 6 s, 18 s. Multiplicar por
// tres y no por dos es deliberado -- un servidor que acaba de fallar
// suele necesitar mas de un par de segundos, y con 3 intentos el
// techo se queda en 18 s, que sigue siendo una espera razonable.
#define CW_BACKOFF_BASE_MS   2000u
#define CW_BACKOFF_FACTOR    3u

// Huella de un texto (FNV-1a de 32 bits). Es lo que compara el motor
// para saber si el documento de origen cambio. No es criptografica y
// no pretende serlo: aqui solo hay que detectar una EDICION del
// usuario, no defenderse de nadie.
uint32_t coworkHash(const char* s, size_t n);

// -------------------------------------------------------------
//  CONSULTA
// -------------------------------------------------------------
int        coworkCount();                   // trabajos vivos (cola + historial)
CoworkJob* coworkAt(int i);                 // por indice, 0..coworkCount()-1
CoworkJob* coworkFind(uint32_t id);
int        coworkActiveCount();             // los que aun no terminaron
int        coworkUnseenCount();             // terminados que el usuario no ha visto
const char* coworkStateName(uint8_t st);    // etiqueta corta, en espanol
const char* coworkKindName(uint8_t kind);

// El trabajo que debe atender AHORA la tarea de red, o NULL. Es UNO
// SOLO a proposito: una sola operacion pesada de red a la vez, como
// pide el enunciado. Devuelve el mas antiguo listo para salir.
CoworkJob* coworkNextForNetwork(uint32_t nowMs);

// -------------------------------------------------------------
//  CONTROL
// -------------------------------------------------------------
bool coworkCancel(uint32_t id);
bool coworkPause(uint32_t id, uint8_t why);
bool coworkResume(uint32_t id);
bool coworkMarkSeen(uint32_t id);
bool coworkDelete(uint32_t id);             // borra el resultado DE VERDAD

// Pausa/reanuda todo lo que no sea urgente. Lo llama el .ino cuando se
// abre la camara, un juego o cualquier app pesada: Cowork baja el
// ritmo en vez de competir por la CPU con el presenter.
void coworkPauseAll(uint8_t why);
void coworkResumeAll(uint8_t why);          // solo los pausados POR ESE motivo
bool coworkAnyPaused(uint8_t why);

// -------------------------------------------------------------
//  AVANCE  (lo llama la tarea de fondo, nunca loop() ni el render)
// -------------------------------------------------------------

// Un paso del motor. Aplica tiempos maximos, esperas de reintento y
// transiciones de estado. Devuelve cuantos trabajos cambiaron de
// estado, para que el llamante sepa si tiene que avisar a la UI.
int coworkPump(uint32_t nowMs);

// Transiciones que dispara quien hace el trabajo de verdad.
void coworkBeginWork(uint32_t id, uint32_t nowMs);   // -> CW_WORKING
void coworkWaitServer(uint32_t id, uint32_t nowMs);  // -> CW_WAITING
void coworkProgress(uint32_t id, uint8_t pct);       // 0..100 o 0xFF

// Termina bien. `srcHashNow` es la huella ACTUAL del documento de
// origen: si no coincide con la del snapshot, el trabajo queda
// marcado con srcChanged y la UI pedira confirmacion antes de
// aplicar nada. Pasar 0 significa "no hay documento de origen".
void coworkFinish(uint32_t id, const char* out, size_t outLen,
                  uint32_t srcHashNow, bool needsConfirm, uint32_t nowMs);

// Termina mal. Si quedan intentos y el error es reintentable,
// vuelve a CW_QUEUED con su espera progresiva; si no, a CW_ERROR.
void coworkFail(uint32_t id, uint8_t errCode, const char* msg, uint32_t nowMs);

// -------------------------------------------------------------
//  PERSISTENCIA
//  -------------------------------------------------------------
//  El motor NO toca LittleFS: serializa a un buffer y el .ino lo
//  escribe donde toque (/System/cowork.dat). Asi este modulo sigue
//  siendo probable en el PC y el sistema de archivos sigue teniendo
//  un solo dueno.
//
//  Solo se guardan los trabajos TERMINADOS (CW_DONE): un trabajo a
//  medias no sobrevive a un reinicio -- su servidor ya no le espera
//  y revivirlo solo produciria un error diferido. Se dice asi en la
//  app en vez de fingir que continua.
// -------------------------------------------------------------
size_t coworkExport(uint8_t* out, size_t n);
bool   coworkImport(const uint8_t* blob, size_t n);

// Version del formato en disco. Al subirla, un fichero viejo se
// ignora limpiamente en vez de leerse mal.
#define CW_BLOB_MAGIC   0x43574B31u   // "CWK1"
#define CW_BLOB_VERSION 1

#endif // FLEXOS_COWORK_H
