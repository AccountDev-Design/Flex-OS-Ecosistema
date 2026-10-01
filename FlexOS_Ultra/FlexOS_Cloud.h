#ifndef FLEXOS_CLOUD_H
#define FLEXOS_CLOUD_H

// #############################################################
//  FLEX CLOUD MANAGER · el cliente de Flex Cloud de Flex OS Ultra
//  ------------------------------------------------------------
//  UN solo modulo para toda la nube del P4. Archivos, Galeria y
//  Multimedia no hablan con la red: piden cosas aqui y leen el estado.
//
//  COMO ESTA HECHO
//    · Dos tareas FreeRTOS propias: "flex-cloud" (API, subidas,
//      descargas y miniaturas, de una en una) y "flex-cloud-st" (el
//      streaming de video por rangos). NINGUNA dibuja: publican estado
//      bajo un mutex y la interfaz lo lee cuando le toca pintar.
//    · Ninguna funcion de esta cabecera bloquea: o copia estado, o deja
//      una peticion en cola y devuelve.
//    · La identidad es la de Flex Account (FlexOS_Account): la misma
//      credencial del dispositivo, siempre por TLS verificado
//      (FlexOS_CloudTLS). Sin cuenta no se toca la red.
//    · Subidas y descargas viven en un DIARIO en LittleFS que sobrevive a
//      un apagado; las partes recibidas las sabe el servidor, asi que el
//      diario solo se escribe en los cambios de estado.
//    · Memoria ACOTADA: buffers fijos y reutilizados. Un video de 160 MB
//      se reproduce a traves de una arena de bloques de tamano fijo.
// #############################################################
#include <stddef.h>
#include <stdint.h>
#include "FlexOS_CloudCore.h"

#ifndef FLEX_CLOUD_BASE_URL
#define FLEX_CLOUD_BASE_URL "https://flex-developer-studio.ralvarezsantos980.chatgpt.site/api/cloud"
#endif

// Streaming: 48 bloques de 64 KB = 3 MB de PSRAM, reservados la PRIMERA vez
// que se reproduce un video de la nube y reutilizados despues (se sueltan con
// flexCloudShed() si el sistema necesita memoria).
#ifndef FLEX_CLOUD_STREAM_BLOCK
#define FLEX_CLOUD_STREAM_BLOCK  (64u * 1024u)
#endif
#ifndef FLEX_CLOUD_STREAM_BLOCKS
#define FLEX_CLOUD_STREAM_BLOCKS 48
#endif

#define FLEX_CLOUD_LIST_MAX   200     // elementos de una vista en memoria (paginas de 40)
#define FLEX_CLOUD_XFERS      FCL_JOBS_MAX
#define FLEX_CLOUD_THUMB_SIDE 132     // = miniatura de la Galeria
#define FLEX_CLOUD_THUMBS     24      // miniaturas decodificadas a la vez (LRU)

// Estado de la conexion con la nube (la pildora de la interfaz).
enum FlexCloudNet : uint8_t {
  FCN_NO_ACCOUNT = 0,   // no hay Flex Account vinculada
  FCN_OFFLINE,          // sin Wi-Fi
  FCN_CONNECTING,       // primera consulta en curso
  FCN_ONLINE,           // la ultima consulta salio bien
  FCN_AUTH,             // la credencial fue rechazada: hay que volver a vincular
  FCN_UNAVAILABLE       // el servidor no responde o no se pudo verificar (se reintenta)
};

enum { FCL_VIEW_FOLDER = 0, FCL_VIEW_RECENT, FCL_VIEW_MEDIA, FCL_VIEW_VIDEO, FCL_VIEW_TRASH, FCL_VIEW_SEARCH };
enum { FCL_LIST_IDLE = 0, FCL_LIST_LOADING, FCL_LIST_READY, FCL_LIST_ERROR };

typedef struct {
  uint8_t  net;                 // FlexCloudNet
  char     netText[96];         // por que (legible)
  bool     quotaValid;
  FclQuota quota;
  char     address[48];         // usuario@flex
  uint32_t gen;                 // cambia cuando cambia algo de lo de arriba
  uint8_t  activeXfers;         // subidas/descargas en curso o en cola
  uint32_t xferGen;             // cambia con cualquier progreso de transferencias
} FlexCloudStatus;

typedef struct {
  uint32_t gen;                 // cambia con cada pagina nueva
  uint8_t  view, state;         // FCL_VIEW_*, FCL_LIST_*
  int      count;               // elementos en memoria
  bool     more;                // hay mas paginas
  char     folderId[FCL_ID_MAX];
  char     error[96];
  FclCrumb crumbs[FCL_CRUMBS];
  int      nCrumbs;
} FlexCloudListInfo;

// Fases de una transferencia para la interfaz: FCX_* (FlexOS_CloudCore.h).

typedef struct {
  uint32_t id;
  uint8_t  type;                // FCL_JOB_UPLOAD / FCL_JOB_DOWNLOAD
  uint8_t  phase;               // FCX_*
  uint8_t  flags;               // FCL_JF_*
  char     name[64];
  uint64_t size, done;
  uint32_t bytesPerSec;
  uint32_t retryInMs;
  char     error[96];
} FlexCloudXfer;

// Resultado de una operacion (hilo grafico: flexCloudPollEvent).
enum { FCE_NONE = 0, FCE_OP_DONE, FCE_UPLOAD_DONE, FCE_UPLOAD_FAILED, FCE_DOWNLOAD_DONE, FCE_DOWNLOAD_FAILED, FCE_VIEW_READY, FCE_VIEW_FAILED };
typedef struct {
  uint8_t  kind;                // FCE_*
  uint32_t opId;                // el devuelto al pedirlo
  bool     ok;
  char     code[FCL_CODE_MAX];
  char     text[96];
  uint32_t mlId;                // subida/descarga ligada a la biblioteca
  uint8_t  flags;
  uint64_t size;
  char     localPath[FCL_PATH_MAX];   // subida: el original; descarga: el temporal verificado
  char     name[FCL_NAME_MAX];
  char     fileId[FCL_ID_MAX];
  char     sha256[FCL_SHA_HEX];
} FlexCloudEvent;

// ---------------------------------------------------------------- ciclo
void flexCloudBegin();          // carga el diario y crea las tareas; no toca la radio
// La nube esta a la vista (Archivos/Galeria/Multimedia en primer plano):
// mantiene la cuota al dia. Fuera de ahi solo corren las transferencias.
void flexCloudSetActive(bool active);
void flexCloudStatus(FlexCloudStatus* out);
const char* flexCloudNetText(uint8_t net);

// ------------------------------------------------------------- listados
bool flexCloudRequestList(uint8_t view, const char* folderId, const char* query);
bool flexCloudRequestMore();
void flexCloudRefresh();
void flexCloudListInfo(FlexCloudListInfo* out);
// Copia elementos [start, start+cap) de la vista actual. Devuelve los copiados.
int  flexCloudListCopy(FclItem* dst, int start, int cap);

// ----------------------------------------------------------- operaciones
// Devuelven un id de operacion (0 = no se pudo encolar).
uint32_t flexCloudMkdir(const char* parentId, const char* name);
uint32_t flexCloudRename(const FclItem* it, const char* newName);
uint32_t flexCloudTrash(const FclItem* it);
uint32_t flexCloudRestore(const FclItem* it);
uint32_t flexCloudDeleteForever(const FclItem* it);

// -------------------------------------------------------- transferencias
// Subir un archivo local con el nombre `name` (NULL = el del archivo).
// `mlId` = elemento de la biblioteca (0 si no). El original NO se toca.
// FCL_JF_FREE_LOCAL: cuando la nube CONFIRME el archivo y su SHA-256
// coincida con el local, se avisa (FCE_UPLOAD_DONE) para que la interfaz
// borre la copia local. Nunca antes.
uint32_t flexCloudUpload(const char* localPath, const char* name, const char* parentId, uint32_t mlId, uint8_t flags);
// Descargar al dispositivo (cabe en LittleFS o se dice que no). Se baja a un
// temporal y se verifica el SHA-256 de la nube antes de avisar
// (FCE_DOWNLOAD_DONE con la ruta del temporal: la interfaz lo coloca).
uint32_t flexCloudDownload(const FclItem* it, uint8_t flags);
bool     flexCloudCancel(uint32_t jobId);
bool     flexCloudRetry(uint32_t jobId);
void     flexCloudClearFinished();
int      flexCloudXfers(FlexCloudXfer* out, int cap);
// Copia temporal de una FOTO de la nube para abrirla en el visor (un solo
// hueco: /System/Cloud/view/<nombre>, verificada con su SHA-256). Evento
// FCE_VIEW_READY con la ruta. Nunca se recomprime: es el original.
uint32_t flexCloudFetchForView(const FclItem* it);
bool     flexCloudPollEvent(FlexCloudEvent* ev);

// ------------------------------------------------------------ miniaturas
void flexCloudWantThumb(const char* fileId);
// Llama a `draw` con la miniatura (FLEX_CLOUD_THUMB_SIDE^2 RGB565) bajo el
// cerrojo, si esta lista. false = aun no (o no hay).
bool flexCloudThumbDraw(const char* fileId, void (*draw)(const uint16_t* px, int side, void* user), void* user);
uint32_t flexCloudThumbGen();

// ------------------------------------------------------------- streaming
enum { FCS_CLOSED = 0, FCS_OPENING, FCS_STREAMING, FCS_WAITING_NET, FCS_ERROR };
bool     flexCloudStreamOpen(const FclItem* it);
void     flexCloudStreamClose();
uint8_t  flexCloudStreamState(char* err, size_t cap);
uint32_t flexCloudStreamSize();
// -1 = aun no ha llegado (NO bloquea; el reproductor ensena "Cargando").
int      flexCloudStreamRead(uint32_t off, void* buf, uint32_t n);
bool     flexCloudStreamReady(uint32_t off, uint32_t len);
void     flexCloudStreamPin(uint32_t off, uint32_t len);
void     flexCloudStreamUnpin();
void     flexCloudStreamSeek(uint32_t pos);
uint32_t flexCloudStreamBuffered(uint32_t pos);

// Suelta lo que se puede rehacer (miniaturas, arena del streaming si no se
// esta reproduciendo, buffers). Devuelve los bytes liberados.
size_t   flexCloudShed();

#ifdef FLEXOS_HOST_TEST
void flexCloudTestSetBase(const char* base);
void flexCloudTestStep();          // una vuelta de la tarea principal
void flexCloudTestStreamStep();    // una vuelta de la tarea de streaming
void flexCloudTestPowerCycle();    // se pierde la RAM; la flash (fsstub) y la NVS siguen
uint32_t flexCloudTestJournalSaves();
#endif

#endif
