#ifndef FLEXOS_CLOUDCORE_H
#define FLEXOS_CLOUDCORE_H

// #############################################################
//  FLEX CLOUD · NUCLEO PORTABLE
//  ------------------------------------------------------------
//  Lo que Flex Cloud DECIDE en el P4 sin tocar la red, la flash ni la
//  pantalla: leer las respuestas de la API, el diario de subidas que
//  sobrevive a un reinicio, la cache de bloques del streaming de video,
//  los nombres que caben en LittleFS y el SHA-256 por partes.
//
//  Es la frontera de siempre en este proyecto: el .ino dibuja, la tarea
//  mueve los bytes y ESTE fichero decide. Por eso se compila y se ejecuta
//  entero en el PC, con sanitizers (tests/host/test_cloudcore.cpp): aqui
//  se leen bytes que llegan de internet.
// #############################################################
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define FCL_ID_MAX      32      // "fil_" + 24 base32 (+ margen)
#define FCL_NAME_MAX    256     // 255 bytes UTF-8 + NUL (el limite del servidor)
#define FCL_MIME_MAX    64
#define FCL_SHA_HEX     65
#define FCL_CODE_MAX    40
#define FCL_MSG_MAX     160
#define FCL_CURSOR_MAX  96
#define FCL_PATH_MAX    112     // = FML_PATH_MAX: ruta local en LittleFS
#define FCL_CRUMBS      8       // niveles de la ruta que se ensenan
#define FCL_PARTS_MAX   512     // partes por subida que sigue el P4 (bitmap)

// Tipos de elemento (los de la API).
enum {
  FCL_K_FOLDER = 0, FCL_K_PHOTO, FCL_K_VIDEO, FCL_K_AUDIO, FCL_K_DOCUMENT, FCL_K_ARCHIVE, FCL_K_OTHER
};

typedef struct {
  char     id[FCL_ID_MAX];
  char     parentId[FCL_ID_MAX];
  char     name[FCL_NAME_MAX];
  char     mime[FCL_MIME_MAX];
  char     sha256[FCL_SHA_HEX];
  uint64_t size;
  int64_t  updatedAt;           // ms desde 1970 (0 = desconocido)
  int64_t  deletedAt;           // 0 = no esta en la papelera
  uint32_t durationMs;
  uint16_t width, height;
  uint16_t itemCount;           // carpeta en la papelera: archivos que contiene
  uint8_t  kind;                // FCL_K_*
  bool     isFolder;
  bool     hasThumb;
  bool     fromDevice;          // lo subio un Flex OS Ultra
} FclItem;

typedef struct { char id[FCL_ID_MAX]; char name[64]; } FclCrumb;

typedef struct {
  uint64_t totalBytes, usedBytes, reservedBytes, trashBytes, availableBytes;
  uint16_t permille;            // uso en tanto por mil (0..1000)
  uint8_t  state;               // FCL_Q_*
  char     plan[16];
} FclQuota;
enum { FCL_Q_OK = 0, FCL_Q_LOW, FCL_Q_FULL };

typedef struct {
  char     uploadId[FCL_ID_MAX];
  char     state[16];           // active / completing / completed / aborted / expired / failed
  char     fileId[FCL_ID_MAX];  // al completar
  char     sha256[FCL_SHA_HEX]; // del archivo, al completar
  uint64_t size, receivedBytes;
  uint32_t chunkSize, totalParts, receivedCount;
  bool     resumed;
  uint32_t parts[FCL_PARTS_MAX / 32];   // bitmap de partes recibidas (bit n-1)
} FclUpload;

// ---------------------------------------------------------------------------
//  Respuestas de la API (JSON). Todas aceptan cuerpos de cualquier forma y
//  devuelven false/-1 si no son lo esperado: nunca leen fuera de `len`.
// ---------------------------------------------------------------------------
// error.code y error.message de una respuesta {ok:false}. false si no hay.
bool fclParseError(const char* body, size_t len, char* code, size_t codeCap, char* msg, size_t msgCap);
// Cuota de /me o /quota (o de cualquier respuesta con "quota").
bool fclParseQuota(const char* body, size_t len, FclQuota* q);
// Cuenta de /me.
bool fclParseMe(const char* body, size_t len, char* address, size_t addrCap, char* displayName, size_t nameCap, FclQuota* q);
// Pagina de /files. Devuelve los elementos leidos (0..cap) o -1. `more` =
// hay mas paginas (cursor en `cursor`). `crumbs`/`nCrumbs` = ruta de la carpeta.
int  fclParseList(const char* body, size_t len, FclItem* items, int cap, char* cursor, size_t cursorCap,
                  bool* more, FclCrumb* crumbs, int crumbCap, int* nCrumbs);
// Un archivo o carpeta suelto: {"file":{...}} / {"folder":{...}} / {"item":{...}}.
bool fclParseItem(const char* body, size_t len, FclItem* it);
// Sesion de subida: {"upload":{...}} (o la respuesta de una parte).
bool fclParseUpload(const char* body, size_t len, FclUpload* u);
bool fclUploadHasPart(const FclUpload* u, uint32_t n);

// Texto corto para una persona a partir de un codigo de la API (o de un
// fallo de red, code = "network"). Siempre devuelve algo legible.
const char* fclErrorText(const char* code);
uint8_t     fclKindOf(const char* kind);

// ---------------------------------------------------------------------------
//  SHA-256 incremental (mbedTLS en la placa, OpenSSL en el PC)
// ---------------------------------------------------------------------------
typedef struct { uint8_t opaque[256]; } FclSha;
void fclShaStart(FclSha* s);
void fclShaUpdate(FclSha* s, const void* data, size_t n);
void fclShaFinishHex(FclSha* s, char out[FCL_SHA_HEX]);
void fclShaHex(const void* data, size_t n, char out[FCL_SHA_HEX]);

// ---------------------------------------------------------------------------
//  DIARIO DE TRABAJOS (subidas y descargas que sobreviven a un reinicio)
//  ------------------------------------------------------------------------
//  Se escribe SOLO en los cambios de estado (encolado, sesion creada,
//  terminado, fallido), nunca por parte: las partes recibidas las sabe el
//  servidor. Formato binario con version y CRC32: un registro danado se
//  descarta entero en vez de interpretarse a medias.
// ---------------------------------------------------------------------------
#define FCL_JOBS_MAX 8
enum { FCL_JOB_FREE = 0, FCL_JOB_QUEUED, FCL_JOB_ACTIVE, FCL_JOB_DONE, FCL_JOB_FAILED, FCL_JOB_CANCELLED };
enum { FCL_JOB_UPLOAD = 1, FCL_JOB_DOWNLOAD = 2 };
#define FCL_JF_FREE_LOCAL   0x01u   // subir y liberar espacio: borrar lo local SOLO tras confirmar
#define FCL_JF_TO_LIBRARY   0x02u   // descarga: a la biblioteca de medios (Galeria)
#define FCL_JF_FROM_LIBRARY 0x04u   // subida desde la Galeria (mlId valido)
// Banderas internas del gestor (tambien van al diario):
#define FCL_JF_ABORT        0x20u   // subida cancelada: falta avisar al servidor para soltar la reserva
#define FCL_JF_CLEARED      0x40u   // el usuario la quito de la lista (se borra al terminar lo pendiente)
#define FCL_JF_DELIVERED    0x80u   // el aviso de "terminado" ya lo proceso la interfaz

typedef struct {
  uint8_t  state;               // FCL_JOB_*
  uint8_t  type;                // FCL_JOB_UPLOAD / FCL_JOB_DOWNLOAD
  uint8_t  flags;               // FCL_JF_*
  uint8_t  attempts;
  uint32_t id;                  // numero del trabajo (no se repite en esta instalacion)
  uint32_t mlId;                // elemento de la biblioteca (0 = ninguno)
  uint64_t size;
  char     localPath[FCL_PATH_MAX];
  char     name[FCL_NAME_MAX];  // nombre en la nube (subida) o nombre a mostrar
  char     parentId[FCL_ID_MAX];
  char     remoteId[FCL_ID_MAX];// uploadId (subida) o fileId (descarga)
  char     fileId[FCL_ID_MAX];  // archivo resultante en la nube (subida terminada)
  char     sha256[FCL_SHA_HEX]; // del archivo completo (verificado)
  char     error[FCL_MSG_MAX];
} FclJob;

typedef struct {
  uint32_t nextId;
  FclJob   jobs[FCL_JOBS_MAX];
} FclJournal;

void   fclJournalInit(FclJournal* j);
// Serializa a `buf`. Devuelve los bytes escritos o 0 si no cabe.
size_t fclJournalEncode(const FclJournal* j, uint8_t* buf, size_t cap);
size_t fclJournalMaxBytes();
// Lee lo serializado. Un registro con CRC o version incorrectos se ignora
// (se devuelve el diario vacio pero valido) y `damaged` lo dice.
bool   fclJournalDecode(FclJournal* j, const uint8_t* buf, size_t len, bool* damaged);
// Un hueco libre o el trabajo terminado mas antiguo que ya no le debe nada a
// nadie: NUNCA uno en curso, uno terminado cuyo aviso no proceso la interfaz
// (borraria el "libera espacio" de una subida) ni una cancelacion que aun
// tiene que soltar su reserva en el servidor. NULL si no hay.
FclJob* fclJournalAlloc(FclJournal* j);
FclJob* fclJournalFind(FclJournal* j, uint32_t id);
// El siguiente trabajo a ejecutar (cola en orden de alta).
FclJob* fclJournalNext(FclJournal* j);
int     fclJournalCount(const FclJournal* j, uint8_t state);

// ---------------------------------------------------------------------------
//  CACHE DE BLOQUES PARA EL STREAMING
//  ------------------------------------------------------------------------
//  Un video de la nube NUNCA se descarga entero: el lector (el demultiplexor
//  de AVI, en el hilo de la interfaz) lee de una arena FIJA de bloques que
//  una tarea de red va rellenando con peticiones Range, por delante de la
//  posicion de lectura. Si el lector pide algo que aun no esta, la lectura
//  falla SIN bloquear y queda anotada como "lo siguiente que hace falta":
//  el reproductor ensena "Cargando" y vuelve a intentarlo en el siguiente
//  cuadro. La arena se reserva una vez y se reutiliza entre videos.
// ---------------------------------------------------------------------------
#define FCL_CACHE_BLOCKS_MAX 64
enum { FCL_B_EMPTY = 0, FCL_B_LOADING, FCL_B_READY };

typedef struct {
  uint32_t off;                 // desplazamiento del bloque en el archivo (multiplo de blockSize)
  uint32_t len;                 // bytes validos (el ultimo bloque puede ser corto)
  uint32_t lru;
  uint8_t  state;
} FclBlock;

typedef struct {
  uint8_t* arena;
  uint32_t blockSize;
  uint16_t nBlocks;
  uint32_t fileSize;
  uint32_t tick;
  uint32_t readPos;             // ultima lectura (para leer por delante)
  uint32_t ahead;               // bytes por delante de readPos que se mantienen
  uint32_t behind;              // bytes por detras que no se desalojan
  bool     wantSet;             // hubo una lectura sin datos
  uint32_t wantOff;
  bool     pinSet;              // rango que hace falta ENTERO (p. ej. idx1 para buscar)
  uint32_t pinOff, pinLen;
  uint32_t misses, hits, fetched;
  FclBlock blocks[FCL_CACHE_BLOCKS_MAX];
} FclCache;

// `arena` = nBlocks * blockSize bytes. `ahead` se recorta a lo que cabe.
void fclCacheInit(FclCache* c, uint8_t* arena, uint32_t blockSize, uint16_t nBlocks, uint32_t fileSize);
// Copia [off, off+n) si esta ENTERO en la cache: devuelve n. Si falta algo,
// devuelve -1 y anota lo que falta (no bloquea nunca). 0 = fin del archivo.
int  fclCacheRead(FclCache* c, uint32_t off, void* buf, uint32_t n);
bool fclCacheReady(const FclCache* c, uint32_t off, uint32_t len);
// Pide que [off, off+len) este entero (y protegido) hasta fclCacheUnpin().
void fclCachePin(FclCache* c, uint32_t off, uint32_t len);
void fclCacheUnpin(FclCache* c);
// Cambio de posicion (buscar): lo que habia por delante ya no sirve igual.
void fclCacheSeek(FclCache* c, uint32_t pos);
// Para la tarea de red: el siguiente bloque que hay que traer. Reserva su
// hueco (LOADING) y devuelve su indice, o -1 si no hace falta nada ahora.
int  fclCacheNextFetch(FclCache* c, uint32_t* off, uint32_t* len);
uint8_t* fclCacheSlot(FclCache* c, int slot);
void fclCacheCommit(FclCache* c, int slot);     // el hueco ya tiene sus bytes
void fclCacheAbort(FclCache* c, int slot);      // no se pudo traer: queda vacio
// Bytes listos de forma contigua desde `pos` (para la barra de "cargado").
uint32_t fclCacheContiguous(const FclCache* c, uint32_t pos);

// ---------------------------------------------------------------------------
//  Nombres y utilidades
// ---------------------------------------------------------------------------
// Nombre de la nube -> nombre que cabe en LittleFS (FLEXFS_NAME_MAX = 48
// bytes con el terminador): se conserva la extension, no se parte nunca un
// caracter UTF-8 y se cambian los caracteres que el sistema de archivos no
// admite. Siempre deja algo valido.
void fclLocalName(const char* cloudName, char* out, size_t cap);
// Copia UTF-8 recortando SIN partir un caracter.
void fclCopyUtf8(char* out, size_t cap, const char* in);
// Codifica para una URL (consulta o ruta).
bool fclUrlEncode(const char* in, char* out, size_t cap);
// Escapa para una cadena JSON. false si no cabe.
bool fclJsonEscape(const char* in, char* out, size_t cap);
// Tamano para personas en base 1024, como el resto de Flex OS ("1,4 GB").
void fclFmtBytes(uint64_t n, char* out, size_t cap);
// Espera antes del reintento `failures` (1, 2, ...): 2 s, 4 s ... 60 s como mucho.
uint32_t fclBackoffMs(uint8_t failures);
// Plan de partes para una subida desde el P4: partes de 256 KB salvo que el
// archivo necesite mas de FCL_PARTS_MAX partes.
uint32_t fclChunkFor(uint64_t size);

#endif
