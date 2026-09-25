// #############################################################
// ##  FlexOS · BIBLIOTECA DE MEDIOS · el almacen (portable)
// #############################################################
//
//  QUE ES
//  ------
//  Todo lo que la biblioteca HACE con el disco, sin saber que disco es:
//  cargar y guardar el catalogo, reconciliarlo con las carpetas, hacer
//  las miniaturas persistentes, bloquear (mover a la carpeta protegida),
//  borrar, renombrar, sustituir un original y publicar lo que sube el
//  movil. El sistema de archivos, el cerrojo y el reloj entran por
//  punteros a funcion: en la placa, LittleFS + un mutex de FreeRTOS; en
//  el PC, memoria + contadores (tests/host/test_mediastore.cpp), con
//  fallos provocados.
//
//  REGLA DE ORO. Un cambio de DISCO que afecta a un registro se hace con
//  el cerrojo tomado, junto con el cambio del registro. Asi quien recorre
//  el disco a la vez nunca ve un archivo "a medio mover", y no puede
//  nacer un registro duplicado ni uno fantasma.
//
//  Todas las funciones publicas toman el cerrojo por su cuenta, salvo las
//  que terminan en "Locked" (el llamante ya lo tiene).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "FlexOS_MediaLib.h"
#include "FlexOS_JPEG.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char     name[64];
  uint32_t size;
  bool     dir;
} FlexMsEntry;

typedef struct {
  void*    (*open)(void* ctx, const char* path, bool write);   // escribir: crea o vacia (y su carpeta)
  int      (*read)(void* ctx, void* h, uint8_t* buf, size_t n); // bytes, 0 fin, -1 error
  bool     (*write)(void* ctx, void* h, const uint8_t* buf, size_t n);
  bool     (*seek)(void* ctx, void* h, uint32_t off);
  void     (*close)(void* ctx, void* h);
  uint32_t (*size)(void* ctx, const char* path);                // 0 si no existe
  bool     (*exists)(void* ctx, const char* path);
  bool     (*remove)(void* ctx, const char* path);              // archivo (o carpeta entera)
  bool     (*move)(void* ctx, const char* from, const char* to);// crea la carpeta; NO pisa
  bool     (*trash)(void* ctx, const char* path);               // a la papelera; NULL = no hay
  bool     (*mkdir)(void* ctx, const char* path);
  // Entradas de `dir` a partir de `skip` (orden estable). -1 = no existe
  // o no se puede leer.
  int      (*list)(void* ctx, const char* dir, FlexMsEntry* out, int maxn, int skip);
  // Escritura que nunca deja el archivo a medias (temporal + renombrado).
  bool     (*writeAtomic)(void* ctx, const char* path, const void* buf, size_t n);
  void*    ctx;
} FlexMsFs;

typedef struct {
  FlexMlLib     lib;
  FlexMsFs      fs;
  void        (*lock)(void* ctx);
  void        (*unlock)(void* ctx);
  void*         lockCtx;
  FlexJpegAlloc alloc;
  FlexJpegFree  free;
  uint32_t    (*now)(void* ctx);           // epoca UTC; 0 = no se sabe
  void*         nowCtx;
  // Dibujo de Paint -> pixeles (FLEXTH_SIDE x FLEXTH_SIDE, RGB565, ya
  // centrado sobre blanco). Su formato vive en FlexOS_FS; NULL = sin
  // miniatura de dibujos. w/h reciben el tamano del lienzo original.
  bool        (*paintThumb)(void* ctx, const char* path, uint16_t* px, int* w, int* h);
  void*         paintCtx;
  // Trabajo pesado de uno en uno en TODO el sistema (opcionales). Hacer la
  // miniatura de una foto es decodificarla entera: la placa lo serializa con
  // la validacion de las subidas y con el visor. heavyBegin false = ahora no
  // (el trabajo se aplaza y se reintenta, no se pierde).
  bool        (*heavyBegin)(void* ctx);
  void        (*heavyEnd)(void* ctx);
  void*         heavyCtx;
  // Se llama a menudo durante una decodificacion larga (en cada lectura); la
  // placa decide si cede la CPU. NULL = nunca.
  void        (*yield)(void* ctx);
  void*         yieldCtx;
  // ---- estado (lo leen la interfaz y la tarea de fondo) ----
  volatile bool     dirty;                 // hay cambios sin guardar
  volatile bool     scanning;
  volatile uint16_t pending;               // miniaturas por hacer
} FlexMediaStore;

// Deja el almacen listo sobre `store` (cap registros). fs/lock/alloc/now
// los rellena el llamante ANTES.
void     flexMsInit(FlexMediaStore* ms, FlexMlRec* store, uint16_t cap);
// Crea las carpetas de la biblioteca.
void     flexMsMakeDirs(FlexMediaStore* ms);
// Carga el catalogo: 1 = archivo, 2 = su .bak, 3 = su .tmp, 0 = vacio.
int      flexMsLoad(FlexMediaStore* ms);
// Guarda (escritura atomica). false = no se pudo (sigue sucio).
bool     flexMsSave(FlexMediaStore* ms);
// Vacia la carpeta de temporales (subidas y guardados que no terminaron).
void     flexMsCleanTmp(FlexMediaStore* ms);

// Reconcilia el catalogo con el disco. `yieldFn` se llama entre lotes.
// false = alguna carpeta que existe no se pudo leer (y entonces NO se
// retira nada del catalogo).
bool     flexMsScan(FlexMediaStore* ms, void (*yieldFn)(void*), void* yctx);

// ---- Miniaturas persistentes y metadatos (tarea de fondo) ----
typedef struct { uint32_t id, size; int kind; char path[FML_PATH_MAX]; } FlexMsJob;
// Siguiente trabajo con id > afterId (0 = desde el principio). Actualiza
// ms->pending. false = no hay.
bool     flexMsNextJob(FlexMediaStore* ms, uint32_t afterId, FlexMsJob* job);
enum { FLEXMS_JOB_DONE = 0, FLEXMS_JOB_RETRY = 1, FLEXMS_JOB_GONE = 2 };
// Hace el trabajo: formato real, miniatura, dimensiones, duracion. Una
// foto se decodifica ENTERA, pero LEYENDOLA POR TROZOS (nunca entera en
// RAM): hace falta como mucho ~1 MB de trabajo sea cual sea su tamano. Si
// ni eso sobra por encima de memReserve, o el trabajo pesado del sistema
// esta ocupado, FLEXMS_JOB_RETRY (se deja para luego).
int      flexMsRunJob(FlexMediaStore* ms, const FlexMsJob* job, uint32_t memFree, uint32_t memReserve);

// ---- Rutas ----
void        flexMsThumbPath(const FlexMlRec* r, char* out, size_t cap);
const char* flexMsDestDir(int kind);

// ---- Cambios (la interfaz y el servidor) ----
bool     flexMsGet(FlexMediaStore* ms, uint32_t id, FlexMlRec* out);
uint32_t flexMsRev(FlexMediaStore* ms);
int      flexMsSnapshot(FlexMediaStore* ms, FlexMlRec* dst, int cap, uint32_t* rev);
uint32_t flexMsFindDup(FlexMediaStore* ms, uint32_t size, uint32_t crc);
bool     flexMsDelete(FlexMediaStore* ms, uint32_t id);
bool     flexMsTrash(FlexMediaStore* ms, uint32_t id);                // nunca un protegido
bool     flexMsSetLock(FlexMediaStore* ms, uint32_t id, bool lock, char* why, size_t whyCap);
bool     flexMsRename(FlexMediaStore* ms, uint32_t id, const char* newName, char* why, size_t whyCap);
// `parent` = el elemento del que sale (copia editada) o 0.
uint32_t flexMsAddFile(FlexMediaStore* ms, const char* tmp, int kind, const char* shownName,
                       uint8_t origin, uint32_t parent, char* why, size_t whyCap);
bool     flexMsReplace(FlexMediaStore* ms, uint32_t id, const char* tmp, char* why, size_t whyCap);

// ---- Lo que sube el movil (Flex Web Server) ----
// Mueve la subida ya comprobada a su carpeta y la registra. 0 = no (y los
// temporales quedan donde estaban, para que el servidor los borre).
struct FlexMsUpload {
  const char* tmpPath; const char* thumbTmp;       // thumbTmp "" = sin miniatura
  const char* name; const char* title; const char* artist; const char* album;
  int kind, fmt; bool playable;
  uint32_t size, crc, created, durMs; uint16_t w, h;
};
uint32_t flexMsCommitUpload(FlexMediaStore* ms, const struct FlexMsUpload* up, char* why, size_t whyCap);
// Miniatura aportada desde fuera (ya normalizada). Nunca a un protegido.
bool     flexMsSetExtThumb(FlexMediaStore* ms, uint32_t id, const char* tmp);

#ifdef __cplusplus
}
#endif
