// #############################################################
// ##  FlexOS · FLEX WEB SERVER  ·  la aplicacion del servidor
// ##  Portable: compila igual en el P4 y en un PC.
// #############################################################
//
//  QUE ES
//  ------
//  Todo lo que Flex Web Server DECIDE: que rutas hay, quien puede ver
//  que, como se recibe un archivo del movil, como se valida antes de
//  publicarlo, como se sirve una descarga y como se arma un ZIP. El
//  protocolo HTTP lo pone FlexOS_HttpShare; el catalogo, FlexOS_MediaLib;
//  las miniaturas y la validacion de lo recibido, FlexOS_MediaThumb.
//
//  QUE NO ES
//  ---------
//  No abre sockets, no crea tareas y no toca LittleFS: todo eso entra por
//  tres interfaces de funciones (conexion, sistema de archivos y
//  anfitrion) que la placa rellena en FlexOS_Ultra_WebServer.h y las
//  pruebas con memoria. Por eso el servidor ENTERO -- subida, descarga,
//  ZIP, sesiones, bloqueo -- se ejecuta de verdad en el PC
//  (tests/host/test_mediaweb.cpp), con sanitizers y con fallos
//  provocados: desconexion a mitad, disco lleno, CRC que no cuadra.
//
//  SEGURIDAD, EN CAPAS
//  -------------------
//  1) Codigo de acceso de 6 cifras que se ENSENA EN LA PANTALLA del P4.
//     Sin el no hay sesion, y sin sesion solo se sirve la pagina. El QR
//     lo lleva dentro: escanear es emparejar.
//  2) Sesion en cookie HttpOnly + SameSite=Strict, y toda peticion que
//     cambia algo exige la cabecera X-Flex (defensa CSRF). El Host tiene
//     que ser la IP propia (DNS rebinding).
//  3) Contenido bloqueado: solo con sesion de PROPIETARIO, que se obtiene
//     con el PIN o la contrasena del sistema (los mismos del bloqueo de
//     pantalla, verificados contra su hash). El nivel de propietario
//     caduca a los 5 minutos sin uso y cae en cuanto el P4 se bloquea.
//  4) Limitador de intentos para el codigo y para la clave: 5 fallos y
//     espera creciente (30 s, 1 min, 2 min... hasta 16 min).
//  La clave del usuario no se guarda, no se registra y se borra del
//  buffer en cuanto se ha comprobado.

#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "FlexOS_MediaLib.h"
#include "FlexOS_HttpShare.h"
#include "FlexOS_JPEG.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLEXWEB_PORT            8080
#define FLEXWEB_SESS_N             6
#define FLEXWEB_CODE_LEN           6
#define FLEXWEB_HDR_BUF         4096      // = FLEXHTTP_MAX_REQUEST
#define FLEXWEB_IO_BUF    (16u * 1024u)   // trozo de lectura/escritura: 4 bloques de LittleFS
#define FLEXWEB_SESS_IDLE_MS   (30u * 60u * 1000u)
#define FLEXWEB_OWNER_IDLE_MS   (5u * 60u * 1000u)
#define FLEXWEB_HDR_TIMEOUT_MS  5000u
#define FLEXWEB_KEEP_IDLE_MS    4000u
#define FLEXWEB_IDLE_SLICE_MS    100u     // la espera keep-alive se hace a trozos (ver othersWaiting)
#define FLEXWEB_BODY_TIMEOUT_MS 15000u    // sin un byte en 15 s: la subida se da por perdida
#define FLEXWEB_ZIP_MAX           64
#define FLEXWEB_TRIES             5
#define FLEXWEB_SMALL_BODY       512

// ---- Conexion (la placa: WiFiClient; las pruebas: memoria) -------------
typedef struct {
  // Hasta n bytes, esperando como mucho timeoutMs. >0 bytes, 0 = plazo
  // vencido sin datos, -1 = cerrada o error.
  int  (*read)(void* ctx, uint8_t* buf, size_t n, uint32_t timeoutMs);
  // Escribe TODO o devuelve false (el otro lado se fue).
  bool (*write)(void* ctx, const uint8_t* buf, size_t n);
  void* ctx;
} FlexWebConn;

// ---- Sistema de archivos por flujos --------------------------------------
typedef struct {
  void*    (*open)(void* ctx, const char* path, bool write);   // NULL si no se pudo
  int      (*read)(void* ctx, void* h, uint8_t* buf, size_t n);  // bytes, 0 fin, -1 error
  bool     (*write)(void* ctx, void* h, const uint8_t* buf, size_t n);
  bool     (*seek)(void* ctx, void* h, uint32_t off);
  void     (*close)(void* ctx, void* h);
  uint32_t (*size)(void* ctx, const char* path);                // 0 si no existe
  bool     (*remove)(void* ctx, const char* path);
  uint32_t (*freeBytes)(void* ctx);
  uint32_t (*totalBytes)(void* ctx);
  void* ctx;
} FlexWebFs;

// Lo que una subida terminada y validada entrega al anfitrion.
typedef struct {
  char     tmpPath[FML_PATH_MAX];     // el archivo, ya completo y comprobado
  char     thumbTmp[FML_PATH_MAX];    // su miniatura (o "" si el P4 no pudo hacerla)
  char     name[FML_NAME_MAX];        // nombre original (limpio, UTF-8)
  char     title[FML_TITLE_MAX], artist[FML_ARTIST_MAX], album[FML_ALBUM_MAX];
  int      kind, fmt;
  bool     playable;
  uint32_t size, crc, created;
  uint16_t w, h;
  uint32_t durMs;
} FlexWebUpload;

// Eventos para la interfaz del P4 (tarjetas de transferencia, avisos).
enum {
  FLEXWEB_EV_UP_START = 1, FLEXWEB_EV_UP_PROGRESS, FLEXWEB_EV_UP_CHECK,
  FLEXWEB_EV_UP_DONE, FLEXWEB_EV_UP_FAIL,
  FLEXWEB_EV_DL_START, FLEXWEB_EV_DL_PROGRESS, FLEXWEB_EV_DL_DONE, FLEXWEB_EV_DL_FAIL,
  FLEXWEB_EV_PAIRED, FLEXWEB_EV_OWNER, FLEXWEB_EV_OWNER_FAIL
};
typedef struct {
  int      ev;
  int      kind;              // FML_K_* (0 si no aplica)
  uint32_t done, total;       // bytes reales
  uint32_t id;                // media_id (al terminar)
  char     name[FML_NAME_MAX];
  char     msg[96];
} FlexWebXfer;

typedef struct {
  // --- catalogo (la placa toma su mutex dentro) ---
  int      (*snapshot)(void* ctx, FlexMlRec* dst, int cap, uint32_t* rev);
  bool     (*getRec)(void* ctx, uint32_t id, FlexMlRec* out);
  uint32_t (*rev)(void* ctx);
  uint32_t (*findDup)(void* ctx, uint32_t size, uint32_t crc);   // id o 0
  // Mueve la subida a su sitio y la registra. Devuelve el id nuevo o 0
  // (con el motivo). Si falla, los temporales siguen donde estaban.
  uint32_t (*commit)(void* ctx, const FlexWebUpload* up, char* why, size_t whyCap);
  // Miniatura aportada por el navegador (ya normalizada a JPEG cuadrado).
  bool     (*setThumb)(void* ctx, uint32_t id, const char* tmpPath);
  // Ruta de la miniatura persistente de un registro ("" si no tiene).
  void     (*thumbPath)(void* ctx, const FlexMlRec* r, char* out, size_t cap);
  // --- seguridad del sistema ---
  bool     (*verifySecret)(void* ctx, const char* secret);          // reentrante
  int      (*lockType)(void* ctx);                                  // 0 nada, 1 PIN, 2 contrasena
  // --- tiempo, azar, eventos ---
  uint32_t (*nowMs)(void* ctx);
  uint32_t (*nowEpoch)(void* ctx);                                   // 0 = no se sabe
  void     (*random)(void* ctx, uint8_t* out, size_t n);
  void     (*event)(void* ctx, const FlexWebXfer* x);                // puede ser NULL
  void     (*yield)(void* ctx);                                      // bucles largos; puede ser NULL
  // Hay OTRA conexion esperando turno (la placa: WiFiServer::hasClient()).
  // El servidor atiende de una en una; con esto una conexion keep-alive
  // ociosa cede el sitio en vez de retener a las demas hasta 4 s, y la
  // respuesta sale con "Connection: close" para que el navegador no la
  // reutilice. Puede ser NULL (entonces nunca se cede).
  bool     (*othersWaiting)(void* ctx);
  void* ctx;
  // TRABAJO PESADO, DE UNO EN UNO (opcionales; van detras de ctx para no
  // mover los campos de siempre). Validar una foto recien subida es
  // decodificarla entera: la placa lo serializa con la tarea de miniaturas y
  // el visor para que dos decodificaciones grandes nunca coincidan. Si
  // heavyBegin devuelve false (sistema ocupado demasiado rato), la subida
  // se rechaza con 503 y el movil la reintenta; nunca se valida a la vez.
  bool     (*heavyBegin)(void* ctx);
  void     (*heavyEnd)(void* ctx);
} FlexWebHost;

typedef struct { uint8_t fails; uint32_t untilMs; } FlexWebLimiter;

typedef struct {
  char     tok[33];           // 32 hex + 0 ("" = ranura libre)
  uint8_t  owner;             // 1 = autenticado con la clave del sistema
  uint32_t ownerEpoch;        // debe coincidir con FlexWebCtx::ownerEpoch
  uint32_t lastMs, ownerMs;
} FlexWebSess;

typedef struct {
  FlexWebFs     fs;
  FlexWebHost   host;
  FlexJpegAlloc alloc;
  FlexJpegFree  free;
  char          ip[24];
  int           port;
  bool          allowUpload;
  // ---- estado ----
  char          code[FLEXWEB_CODE_LEN + 1];
  FlexWebSess   sess[FLEXWEB_SESS_N];
  FlexWebLimiter pairLim, loginLim;
  volatile uint32_t ownerEpoch;   // la placa lo sube al bloquearse: todos pierden el nivel
  bool          uploading;
  uint32_t      served, uploads, downloads;
} FlexWebCtx;

// Deja el contexto listo (codigo nuevo, sin sesiones). fs/host/alloc/ip
// los rellena el llamante ANTES.
void flexWebInit(FlexWebCtx* w);
void flexWebNewCode(FlexWebCtx* w);
// El P4 se ha bloqueado: ninguna sesion conserva el nivel de propietario.
// Se puede llamar desde otra tarea (solo sube un contador).
void flexWebDropOwners(FlexWebCtx* w);
void flexWebDropAll(FlexWebCtx* w);
int  flexWebSessionCount(const FlexWebCtx* w);

// Atiende UNA conexion: una o varias peticiones (keep-alive) hasta que se
// cierre o quede ociosa. hdrBuf = FLEXWEB_HDR_BUF bytes, ioBuf =
// FLEXWEB_IO_BUF. Devuelve cuantas peticiones sirvio.
int  flexWebServeConn(FlexWebCtx* w, const FlexWebConn* c, uint8_t* hdrBuf, uint8_t* ioBuf);

// ---- piezas publicas para las pruebas ----
// Fecha y hora MS-DOS (la de ZIP) a partir de la epoca UTC.
void     flexWebDosTime(uint32_t epoch, uint16_t* dosTime, uint16_t* dosDate);
// Tipo MIME de un formato de la biblioteca.
const char* flexWebMimeOf(int fmt);

#ifdef __cplusplus
}
#endif
