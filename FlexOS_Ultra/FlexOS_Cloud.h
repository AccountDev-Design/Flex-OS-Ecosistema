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
//    · DOS DESTINOS, uno a la vez y cada uno con su diario: el servicio de
//      Internet de siempre o el TELEFONO emparejado con Flex Storage
//      (FlexOS_StorageLink: misma API, red local, sesion propia). Sin
//      telefono todo es exactamente como antes.
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
enum { FCE_NONE = 0, FCE_OP_DONE, FCE_UPLOAD_DONE, FCE_UPLOAD_FAILED, FCE_DOWNLOAD_DONE, FCE_DOWNLOAD_FAILED };
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

// ---------------------------------------------------------------- destino
//   FCD_INTERNET  el servicio de siempre (Flex Account + TLS verificado)
//   FCD_PHONE     el telefono emparejado con Flex Storage (red local)
// Lo elige FlexOS_StorageLink: telefono emparejado y conectado => telefono.
// Antes de flexCloudBegin() solo fija cual diario se carga. Despues NO
// bloquea: el cambio lo aplica la tarea (cierra el streaming, suelta lo que
// estaba en marcha SIN perderlo -- queda en el diario de su destino y sigue al
// volver -- y carga el diario del otro).
enum FlexCloudDest : uint8_t { FCD_INTERNET = 0, FCD_PHONE = 1 };
void    flexCloudSetDest(uint8_t dest);
uint8_t flexCloudDest();        // el que esta en uso ahora
// El telefono emparejado se olvido (o se emparejo OTRO): lo que quedaba en su
// diario ya no puede terminar y se cancela (los originales no se tocan).
void    flexCloudPhoneForgotten();

// ---------------------------------------------------------------- ciclo
void flexCloudBegin();          // carga el diario y crea las tareas; no toca la radio
// La nube esta a la vista (Archivos/Galeria/Multimedia en primer plano):
// mantiene la cuota al dia. Fuera de ahi solo corren las transferencias.
void flexCloudSetActive(bool active);
void flexCloudStatus(FlexCloudStatus* out);
const char* flexCloudNetText(uint8_t net);
// La cuenta se desvinculo en ESTE aparato: cancela lo que estaba en cola o en marcha
// (era de esa cuenta) y suelta su cuota, su direccion y su lista. Lo llama quien
// desvincula, justo despues de flexAccountForgetLocal(). No toca la red. Con el
// destino en el telefono, lo hace la tarea sobre el diario de Internet.
void flexCloudAccountUnlinked();

// ------------------------------------------------------------- listados
bool flexCloudRequestList(uint8_t view, const char* folderId, const char* query);
bool flexCloudRequestMore();
void flexCloudRefresh();
void flexCloudListInfo(FlexCloudListInfo* out);
// Copia elementos [start, start+cap) de la vista actual. Devuelve los copiados.
int  flexCloudListCopy(FclItem* dst, int start, int cap);

// ------------------------------------------- vigilar lo que se esta preparando
// La lista que el P4 ensena es una FOTO de un instante, y el telefono tarda segundos (o minutos) en analizar y preparar un
// video para el P4: sin analizar -> en cola -> preparando N % -> listo / fallido; la miniatura y la duracion llegan despues.
// La interfaz dice QUE ids de lo que tiene a la vista estan sin acabar (hasta FLEX_CLOUD_WATCH_MAX) y la tarea los consulta
// de UNO en uno (GET /files/<id>) y actualiza la lista en su sitio (gen++ => la interfaz repinta). Solo con la nube a la vista,
// pronto si hay cambios, cada vez mas espaciado si no, y con un tope de tiempo: nada que vigilar = nada que pedir.
// Pasar el MISMO conjunto en cada repintado no reinicia nada.
#define FLEX_CLOUD_WATCH_MAX 8
void flexCloudWatch(const char* const* ids, int n);       // sustituye lo vigilado (n = 0: nada)
void flexCloudWatchKick(const char* id);                  // "mira ESTE ahora": el usuario toco algo que se estaba preparando

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
// FCL_JF_MOVE_REMOTE: MOVER; la interfaz manda el original a la papelera de
// la nube solo despues de colocar la copia verificada.
uint32_t flexCloudDownload(const FclItem* it, uint8_t flags);
bool     flexCloudCancel(uint32_t jobId);
bool     flexCloudRetry(uint32_t jobId);
void     flexCloudClearFinished();
int      flexCloudXfers(FlexCloudXfer* out, int cap);
bool     flexCloudPollEvent(FlexCloudEvent* ev);

// ------------------------------------------------- foto para el visor (RAM)
// La foto de la nube se trae a un buffer de PSRAM -- NUNCA a la flash -- y el visor la decodifica de ahi.
// Escribirla en LittleFS costaba un borrado de sector por cada 4 KB, y cada borrado apaga la cache: el
// panel DSI se quedaba sin datos y se veia azul/cian durante todo el tiempo de la descarga (docs/FLEX-
// MEDIA-ECOSYSTEM.md §11). El buffer tiene UN solo duenyo en cada momento: la tarea de la nube lo llena
// (con el tamano y el SHA-256 que el servidor DICE en esa respuesta, verificados) y quien lo pidio lo
// TOMA; si nadie lo toma, se suelta solo. Hay una sola peticion viva: pedir otra anula la anterior, y
// anularla (flexCloudViewCancel) corta la descarga en curso.
// PSRAM que tiene que SEGUIR libre despues de reservar la foto: lo mismo que el visor exige para decodificarla (reserva del
// sistema 6 MB + trabajo del decodificador 1 MB + el minimo de pixeles 1 MB). Si no, la foto se bajaria entera, ocuparia la
// memoria y el visor diria "no hay memoria": se rechaza ANTES de bajar un solo byte. (El visor lo comprueba con static_assert.)
#define FLEX_CLOUD_VIEW_HEADROOM (8u * 1024u * 1024u)
enum { FCV_NONE = 0, FCV_FETCHING, FCV_READY, FCV_FAILED };
uint32_t flexCloudViewStart(const FclItem* it);                 // 0 = no se pudo encolar
// Estado de la peticion `op`. FCV_NONE = ya no es la vigente (la anulo otra o se cancelo).
uint8_t  flexCloudViewState(uint32_t op, uint32_t* got, uint32_t* total, char* err, size_t cap);
// Entrega el JPEG verificado: la PROPIEDAD pasa a quien llama, que lo suelta con flexCloudViewFree.
uint8_t* flexCloudViewTake(uint32_t op, uint32_t* len);
void     flexCloudViewFree(uint8_t* p);
void     flexCloudViewCancel(uint32_t op);

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
// ¿Sigue abierto el flujo de ESE archivo? (Musica y el visor comparten una sola arena: quien cierra comprueba que el flujo aun es el suyo.)
bool     flexCloudStreamIs(const char* fileId);
uint8_t  flexCloudStreamState(char* err, size_t cap);
uint32_t flexCloudStreamSize();
// -1 = aun no ha llegado (NO bloquea; el reproductor ensena "Cargando").
int      flexCloudStreamRead(uint32_t off, void* buf, uint32_t n);
bool     flexCloudStreamReady(uint32_t off, uint32_t len);
void     flexCloudStreamPin(uint32_t off, uint32_t len);
void     flexCloudStreamUnpin();
void     flexCloudStreamSeek(uint32_t pos);
uint32_t flexCloudStreamBuffered(uint32_t pos);
// Lo que mide el flujo abierto (para decidir cuanto colchon pedir y para el diagnostico FLEXOS_DIAG_MEDIA).
struct FlexCloudStreamStats {
  uint32_t bps;       // bajada reciente en bytes/s (media movil de los ultimos bloques); 0 = aun sin medir
  uint32_t blocks;    // bloques traidos en este flujo
  uint32_t misses;    // lecturas del reproductor que no estaban en la cache
  uint32_t conns;     // conexiones abiertas en este flujo (cada salto a otro sitio del archivo abre una)
};
bool     flexCloudStreamStats(FlexCloudStreamStats* out);     // false = no hay flujo abierto

// Suelta lo que se puede rehacer (miniaturas, arena del streaming si no se
// esta reproduciendo, buffers). Devuelve los bytes liberados.
size_t   flexCloudShed();

#ifdef FLEXOS_HOST_TEST
void flexCloudTestSetBase(const char* base);
void flexCloudTestStep();          // una vuelta de la tarea principal
uint32_t flexCloudTestStreamStep(); // una vuelta de la tarea de streaming: 0 = hizo trabajo; si no, los ms que la tarea dormiria hasta un aviso
void flexCloudTestPowerCycle();    // se pierde la RAM; la flash (fsstub) y la NVS siguen
void flexCloudTestPowerOff();      // lo mismo sin volver a arrancar (para el orden de setup())
uint32_t flexCloudTestJournalSaves();
#endif

#endif
