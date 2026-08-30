// #############################################################
// ##  FlexOS · MEDIOS  ·  clasificacion, demultiplexado y
// ##  construccion INCREMENTAL del indice
// ##  Portable: compila igual en el P4 y en un PC.
// #############################################################
//
//  QUE HAY AQUI Y POR QUE ESTA SEPARADO
//  ------------------------------------
//  Tres cosas que son logica pura sobre bytes y que, por serlo, se
//  pueden PROBAR de verdad en el PC (tests/host/test_media.cpp):
//
//    1) CLASIFICACION. Que es cada fichero y, cuando no se puede
//       abrir, POR QUE no. Es la pieza que permite que la interfaz
//       diga "MP4/H.264 no tiene decodificador en esta placa" en vez
//       de intentar reproducirlo y dejar la pantalla en negro.
//    2) DEMULTIPLEXADO de AVI (MJPEG) y lectura de WAV. Aqui se
//       averigua donde empieza y cuanto mide cada fotograma, sin
//       decodificar nada y sin cargar el fichero en memoria.
//    3) INDICE INCREMENTAL. Recorre las carpetas por LOTES
//       PEQUENOS, guardando su propio punto de continuacion, para
//       que una tarjeta con miles de fotos no congele la interfaz.
//
//  Nada de este fichero toca Arduino, LittleFS, SD_MMC ni la
//  pantalla: el acceso a bytes entra por dos interfaces de
//  funciones (FlexMediaIO y FlexMediaVolume) que el sketch rellena
//  con el volumen real y las pruebas con uno de mentira.
//
//  QUE SE SOPORTA DE VERDAD (y nada mas)
//  -------------------------------------
//    SI · JPEG baseline  -> lo decodifica FlexOS_JPEG (ya probado).
//       · AVI con video MJPG/MJPEG/JPEG (un fotograma = un JPEG
//         completo). Es el unico formato de video que esta placa
//         puede reproducir de verdad: cada fotograma pasa por el
//         mismo decodificador JPEG, sin prediccion entre cuadros y
//         sin memoria de referencia.
//       · WAV PCM entero de 8 o 16 bits.
//    NO · MP4/H.264/HEVC, MKV, WebM, AVI con otros codecs, MP3,
//         AAC, FLAC, OGG, PNG, GIF, BMP, HEIC, WEBP.
//         Para todos ellos flexMediaClassify devuelve
//         FLEXMED_UNSUP y un MOTIVO concreto. No se intentan abrir.
//
//  POR QUE NO HAY MP4/H.264. No es una decision de gusto: H.264
//  necesita un decodificador con memoria de fotogramas de
//  referencia y transformada propia; el P4 no trae decodificador de
//  video por hardware, y en software no da el ritmo para nada
//  utilizable a esta resolucion. Anunciarlo como soportado seria
//  exactamente la clase de mentira que este proyecto no se permite.

#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// -------------------------------------------------------------
//  Limites. Todos fijos: no hay ni una reserva dinamica en este
//  modulo. Los buffers grandes (el del fotograma) los pone el
//  llamante, que es quien sabe si tiene PSRAM.
// -------------------------------------------------------------
#define FLEXMED_PATH_MAX   112     // "/sdcard/DCIM/100APPLE/IMG_0001.JPG" cabe de sobra
#define FLEXMED_NAME_MAX    64

// Clase de un fichero.
enum {
  FLEXMED_NONE = 0,   // no es un medio (o extension desconocida)
  FLEXMED_PHOTO,      // JPEG baseline
  FLEXMED_VIDEO,      // AVI MJPEG
  FLEXMED_AUDIO,      // WAV PCM
  FLEXMED_DRAW,       // .fxp: dibujo de Paint (lo pinta la Galeria)
  FLEXMED_UNSUP       // es un medio, pero NO se puede abrir en esta placa
};

// Volumen del que sale un elemento. Se guarda junto a la ruta
// porque la interfaz filtra por el ("Interna" / "Tarjeta SD") y
// porque un elemento de la tarjeta caduca cuando esta se retira.
enum { FLEXMED_VOL_INT = 0, FLEXMED_VOL_SD = 1 };

// -------------------------------------------------------------
//  CLASIFICACION
// -------------------------------------------------------------
// Devuelve FLEXMED_* mirando SOLO la extension. Es lo que se usa al
// construir el indice: abrir cada fichero para mirar su cabecera
// convertiria un indice de 900 fotos en 900 aperturas.
int flexMediaClassify(const char* name);

// Motivo legible de por que un fichero no se puede abrir. Devuelve
// NULL si `name` si es reproducible. La cadena es estatica y esta
// pensada para ensenarse tal cual en una notificacion.
const char* flexMediaUnsupportedReason(const char* name);

// Extension en minusculas, sin punto ("jpg"). Para la ficha de
// detalles del explorador.
void flexMediaExt(const char* name, char* out, size_t n);

// true si la ruta esta dentro del volumen de la tarjeta.
bool flexMediaPathIsSd(const char* path);

// -------------------------------------------------------------
//  ACCESO A BYTES
//  ------------------------------------------------------------
//  Un solo interfaz de lectura para las dos memorias. El sketch lo
//  rellena con el descriptor de la SD o con el de LittleFS; las
//  pruebas, con un bloque de memoria. Asi el demultiplexor de AVI
//  es EL MISMO codigo en la placa y en el PC, que es lo que hace
//  que probarlo en el PC signifique algo.
// -------------------------------------------------------------
typedef struct {
  // Lee hasta `n` bytes en la posicion actual. Devuelve los leidos
  // (0 = fin) o -1 si hubo error o el medio desaparecio.
  int      (*read)(void* ctx, void* buf, uint32_t n);
  // Coloca la posicion. false si no se pudo.
  bool     (*seek)(void* ctx, uint32_t off);
  // Tamano total en bytes.
  uint32_t (*size)(void* ctx);
  void*    ctx;
} FlexMediaIO;

// -------------------------------------------------------------
//  AVI / MJPEG
//  ------------------------------------------------------------
//  INDICE DISPERSO. Un AVI de 10 min a 25 fps tiene 15.000
//  fotogramas; su tabla idx1 ocupa 240 KB. Guardarla entera para
//  poder buscar seria gastar un cuarto de mega en algo que casi no
//  se usa. En su lugar se guarda una MUESTRA de como mucho
//  FLEXAVI_IDX_MAX posiciones repartidas por todo el fichero (2 KB):
//  buscar un instante salta a la muestra anterior mas cercana y
//  avanza leyendo cabeceras de trozo, que son 8 bytes cada una.
//  Precision peor, memoria acotada y sin sorpresas.
// -------------------------------------------------------------
#define FLEXAVI_IDX_MAX  512

enum {
  FLEXAVI_OK = 0,
  FLEXAVI_ERR_IO       = -1,   // el medio fallo o desaparecio
  FLEXAVI_ERR_FORMAT   = -2,   // no es un RIFF/AVI valido
  FLEXAVI_ERR_CODEC    = -3,   // es AVI, pero el video no es MJPEG
  FLEXAVI_ERR_NOVIDEO  = -4,   // no tiene pista de video
  FLEXAVI_ERR_EOF      = -5,   // no quedan mas fotogramas
  FLEXAVI_ERR_TOOBIG   = -6    // un fotograma no cabe en el buffer dado
};

typedef struct {
  FlexMediaIO io;

  uint16_t width, height;      // del avih / strf
  uint32_t frames;             // fotogramas de video declarados
  uint32_t usPerFrame;         // microsegundos por fotograma (avih)
  uint32_t maxFrameBytes;      // el mayor trozo declarado (dwSuggestedBufferSize)
  char     codec[5];           // 'MJPG', 'JPEG'...

  // Region 'movi' (donde viven los fotogramas).
  uint32_t moviStart, moviEnd;

  // Cursor de lectura secuencial.
  uint32_t cursor;             // desplazamiento del proximo trozo
  uint32_t frameNo;            // indice del proximo fotograma a entregar

  // Muestra dispersa: posicion del trozo de video numero
  // idxFrame[i] dentro del fichero.
  uint32_t idxOff[FLEXAVI_IDX_MAX];
  uint32_t idxFrame[FLEXAVI_IDX_MAX];
  uint16_t idxN;
  bool     idxFromFile;        // true si vino de idx1 (busqueda fiable)

  uint8_t  videoStream;        // numero de pista de video ('00'..'09')
} FlexAviCtx;

// Analiza la cabecera. NO lee ni un fotograma y no reserva memoria.
// `io` se copia dentro del contexto.
int  flexAviOpen(FlexAviCtx* a, const FlexMediaIO* io);

// Duracion total en milisegundos (0 si no se puede saber).
uint32_t flexAviDurationMs(const FlexAviCtx* a);

// Deja el cursor listo para entregar el fotograma >= `frame`.
// Devuelve el numero de fotograma en el que quedo de verdad, que
// con indice disperso puede ser ANTERIOR al pedido (nunca
// posterior: nunca se salta contenido sin querer). <0 = error.
int  flexAviSeekFrame(FlexAviCtx* a, uint32_t frame);

// Lee el siguiente fotograma de video en `buf`. Devuelve los bytes
// escritos, o un FLEXAVI_ERR_*. Los trozos que no son de la pista
// de video (audio) se saltan sin leerlos. `frameOut`, si no es
// NULL, recibe el numero del fotograma entregado.
int  flexAviReadFrame(FlexAviCtx* a, void* buf, uint32_t bufCap, uint32_t* frameOut);

// Salta el siguiente fotograma sin leer sus bytes (solo mueve el
// cursor). Es lo que usa el reproductor cuando va tarde: cuesta una
// lectura de 8 bytes en vez de decodificar un JPEG entero.
int  flexAviSkipFrame(FlexAviCtx* a);

// Texto corto y estable de un codigo de error.
const char* flexAviErrStr(int err);

// -------------------------------------------------------------
//  WAV (PCM entero)
// -------------------------------------------------------------
enum {
  FLEXWAV_OK = 0,
  FLEXWAV_ERR_IO     = -1,
  FLEXWAV_ERR_FORMAT = -2,
  FLEXWAV_ERR_CODEC  = -3    // comprimido (ADPCM, mu-law, MP3 dentro de WAV)
};

typedef struct {
  uint32_t sampleRate;
  uint16_t channels;
  uint16_t bits;          // 8 o 16
  uint32_t dataStart;     // desplazamiento del primer byte de muestra
  uint32_t dataBytes;     // bytes de muestras
} FlexWavInfo;

int      flexWavParse(const FlexMediaIO* io, FlexWavInfo* w);
uint32_t flexWavDurationMs(const FlexWavInfo* w);

// -------------------------------------------------------------
//  INDICE DE MEDIOS
//  ------------------------------------------------------------
//  El indice es un array que pone el LLAMANTE (en PSRAM, en el
//  sketch). Este modulo solo lo rellena y lleva la cuenta de por
//  donde iba. Ni una reserva aqui dentro.
// -------------------------------------------------------------
typedef struct {
  char     path[FLEXMED_PATH_MAX];
  uint32_t size;
  uint8_t  kind;         // FLEXMED_*
  uint8_t  vol;          // FLEXMED_VOL_*
} FlexMediaItem;

// Una entrada de directorio, tal cual la entrega el volumen.
typedef struct {
  char     name[FLEXMED_NAME_MAX];
  uint32_t size;
  bool     dir;
} FlexMediaDirent;

// El volumen, visto por el indexador. `list` DEBE devolver las
// entradas en un orden ESTABLE (el orden fisico del directorio
// vale) y saltar las `skip` primeras: es lo que permite recorrer
// una carpeta enorme en lotes sin repetir ni perder ninguna.
//   ->  n >= 0 : entradas escritas (0 = ya no quedan)
//   ->  -1     : el directorio no se pudo leer
typedef struct {
  int   (*list)(void* ctx, const char* dir, FlexMediaDirent* out, int maxn, int skip);
  bool  (*alive)(void* ctx);        // false = el volumen ya no esta
  void* ctx;
} FlexMediaVolume;

#define FLEXMED_ROOTS_MAX   10
#define FLEXMED_DEPTH_MAX    3     // /sdcard/DCIM/100APPLE/foto.jpg

enum { FLEXMED_SCAN_IDLE = 0, FLEXMED_SCAN_RUNNING, FLEXMED_SCAN_DONE, FLEXMED_SCAN_ABORTED };

typedef struct {
  const char* path;
  uint8_t     vol;
} FlexMediaRoot;

typedef struct {
  // ---- salida ----
  FlexMediaItem* items;
  uint16_t       cap;
  uint16_t       n;
  bool           full;          // se lleno: hay mas medios de los que caben

  // ---- entrada ----
  FlexMediaRoot  roots[FLEXMED_ROOTS_MAX];
  uint8_t        rootN;
  FlexMediaVolume volInt, volSd;

  // ---- estado del recorrido ----
  uint8_t  state;
  uint8_t  rootI;                              // raiz en curso
  int8_t   depth;                              // -1 = entre raices
  char     stackPath[FLEXMED_DEPTH_MAX][FLEXMED_PATH_MAX];
  uint32_t stackSkip[FLEXMED_DEPTH_MAX];       // entradas ya consumidas del nivel
  uint32_t seen;                               // entradas miradas (para el progreso)
  uint32_t sdGen;                              // generacion de la SD al empezar
} FlexMediaIndex;

// Prepara el indice sobre el array del llamante. No recorre nada.
void flexMediaIndexInit(FlexMediaIndex* ix, FlexMediaItem* store, uint16_t cap);

// Anade una raiz que se recorrera. El orden importa: las primeras
// se indexan antes, asi que las carpetas de Flex OS van delante.
void flexMediaIndexAddRoot(FlexMediaIndex* ix, const char* path, uint8_t vol);

// Empieza (o reempieza) el recorrido. Vacia lo indexado.
void flexMediaIndexStart(FlexMediaIndex* ix, uint32_t sdGen);

// Avanza el recorrido consumiendo COMO MUCHO `budget` entradas de
// directorio, y para. Devuelve el estado. Pensada para llamarse una
// vez por vuelta de loop() con un presupuesto pequeno (8-24): asi el
// indice se construye en segundo plano sin que la interfaz pierda
// un solo cuadro.
int  flexMediaIndexStep(FlexMediaIndex* ix, int budget);

// Corta el recorrido (la tarjeta se fue, se cierra la app...).
void flexMediaIndexAbort(FlexMediaIndex* ix);

// Quita del indice todo lo que venga de la tarjeta. Se llama al
// detectar la retirada: los elementos internos siguen siendo
// validos y no hay que reindexarlos.
void flexMediaIndexDropSd(FlexMediaIndex* ix);

// Cuenta los elementos de una clase (FLEXMED_PHOTO...) o de todas
// si `kind` es 0; y opcionalmente de un solo volumen (`vol` < 0 =
// los dos).
int  flexMediaIndexCount(const FlexMediaIndex* ix, int kind, int vol);

// Indice real dentro de items[] del elemento `nth` que cumple el
// filtro, o -1. Es lo que permite a la Galeria pintar la pestana
// "Videos" sin construir una segunda lista.
int  flexMediaIndexNth(const FlexMediaIndex* ix, int kind, int vol, int nth);

#ifdef __cplusplus
}
#endif
