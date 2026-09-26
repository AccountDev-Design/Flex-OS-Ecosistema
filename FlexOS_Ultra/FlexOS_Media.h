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
//       que una carpeta con miles de fotos no congele la interfaz.
//
//  Nada de este fichero toca Arduino, LittleFS ni la
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
//       · WAV IMA ADPCM de 4 bits (disposicion de Microsoft, mono o
//         estereo). Es lo que produce la web de Flex Web Server al
//         convertir un MP3: ocupa la CUARTA parte que el PCM de 16 bits,
//         que en 10,9 MB de flash es la diferencia entre dos canciones y
//         ocho. Se decodifica por bloques con flexImaDecodeBlock.
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
#define FLEXMED_PATH_MAX   112
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

// -------------------------------------------------------------
//  ACCESO A BYTES
//  ------------------------------------------------------------
//  Un solo interfaz de lectura para el almacenamiento interno. El sketch lo
//  rellena con LittleFS; las
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
//
//  La muestra se construye la PRIMERA VEZ que se busca, no al abrir:
//  una miniatura o la validacion de una subida solo leen el primer
//  fotograma y no tienen por que recorrer 480 KB de idx1. Un AVI SIN
//  idx1 (una grabacion que se corto) aprende las posiciones a medida
//  que se reproduce, con el mismo tope de memoria.
//
//  TRABAJO ACOTADO POR LLAMADA. Ninguna llamada recorre un numero
//  ilimitado de trozos: un archivo danado o hecho a proposito (miles
//  de trozos vacios) no puede dejar el bucle principal parado ni
//  disparar el watchdog. Buscar sin indice avanza como mucho
//  `maxSkips` fotogramas y devuelve donde quedo (antes del pedido,
//  nunca despues): quien busca puede volver a llamar en la vuelta
//  siguiente hasta llegar.
// -------------------------------------------------------------
#define FLEXAVI_IDX_MAX       512
#define FLEXAVI_SCAN_MAX      16384u        // trozos que no son video, por llamada
#define FLEXAVI_SEEK_SKIPS    2048u         // fotogramas saltados por flexAviSeekFrame
// Mayor fotograma MJPEG que se acepta leer (reproductor, miniatura y
// validacion de subidas usan el MISMO tope). Un 1080p de camara ronda
// 300-600 KB; lo que pase de aqui no es un video que esta placa pueda
// mover y se trata como formato no reproducible, no como archivo danado.
#define FLEXAVI_FRAME_MAX     (1024u * 1024u)

enum {
  FLEXAVI_OK = 0,
  FLEXAVI_ERR_IO       = -1,   // el medio fallo o desaparecio
  FLEXAVI_ERR_FORMAT   = -2,   // no es un RIFF/AVI valido
  FLEXAVI_ERR_CODEC    = -3,   // es AVI, pero el video no es MJPEG
  FLEXAVI_ERR_NOVIDEO  = -4,   // no tiene pista de video
  FLEXAVI_ERR_EOF      = -5,   // no quedan mas fotogramas
  FLEXAVI_ERR_TOOBIG   = -6    // un fotograma no cabe en el buffer dado (ver needBytes)
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
  // idxFrame[i] dentro del fichero. Con idx1 la posicion es la de la
  // tabla (se resuelve al usarla); aprendida, es absoluta.
  uint32_t idxOff[FLEXAVI_IDX_MAX];
  uint32_t idxFrame[FLEXAVI_IDX_MAX];
  uint16_t idxN;
  bool     idxFromFile;        // el archivo trae idx1 util (busqueda fiable)
  uint8_t  idxState;           // interno: pendiente / de idx1 / aprendido
  uint32_t idx1Off, idx1Len;   // interno: donde esta idx1 (se lee al buscar)
  uint32_t learnStride;        // interno: se aprende 1 de cada learnStride fotogramas

  // FLEXAVI_ERR_TOOBIG: bytes del fotograma que no cupo. Ese
  // fotograma NO se consume: quien lee puede ampliar su buffer y
  // volver a pedirlo, o saltarlo con flexAviSkipFrame.
  uint32_t needBytes;

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
// Si el archivo se acaba antes (grabacion cortada), queda en el
// ultimo fotograma que existe y `frames` pasa a ser el numero real.
int  flexAviSeekFrame(FlexAviCtx* a, uint32_t frame);
// Igual, saltando como mucho `maxSkips` fotogramas en esta llamada.
int  flexAviSeekFrameMax(FlexAviCtx* a, uint32_t frame, uint32_t maxSkips);

// Lee el siguiente fotograma de video en `buf`. Devuelve los bytes
// escritos, o un FLEXAVI_ERR_*. Los trozos que no son de la pista
// de video (audio) se saltan sin leerlos. `frameOut`, si no es
// NULL, recibe el numero del fotograma entregado.
// 0 = trozo de video VACIO: en AVI significa "se repite el anterior"
// (lo escriben ffmpeg y las camaras al duplicar un fotograma); el
// fotograma cuenta y el llamante deja la imagen que ya tenia.
// FLEXAVI_ERR_TOOBIG: no cabe en `bufCap`; ver `needBytes`.
int  flexAviReadFrame(FlexAviCtx* a, void* buf, uint32_t bufCap, uint32_t* frameOut);

// Salta el siguiente fotograma sin leer sus bytes (solo mueve el
// cursor). Es lo que usa el reproductor cuando va tarde: cuesta una
// lectura de 8 bytes en vez de decodificar un JPEG entero.
int  flexAviSkipFrame(FlexAviCtx* a);

// Texto corto y estable de un codigo de error.
const char* flexAviErrStr(int err);

// -------------------------------------------------------------
//  WAV (PCM entero e IMA ADPCM)
// -------------------------------------------------------------
enum {
  FLEXWAV_OK = 0,
  FLEXWAV_ERR_IO     = -1,
  FLEXWAV_ERR_FORMAT = -2,
  FLEXWAV_ERR_CODEC  = -3    // otro codec (mu-law, MP3 dentro de WAV, 24 bits, ADPCM mal formado)
};

#define FLEXWAV_FMT_PCM  0x0001
#define FLEXWAV_FMT_IMA  0x0011

typedef struct {
  uint32_t sampleRate;
  uint16_t channels;
  uint16_t bits;            // 8 o 16 (PCM) · 4 (IMA ADPCM)
  uint32_t dataStart;       // desplazamiento del primer byte de muestra
  uint32_t dataBytes;       // bytes de muestras
  uint16_t format;          // FLEXWAV_FMT_*
  uint16_t blockAlign;      // IMA: bytes por bloque · PCM: bytes por muestra (todos los canales)
  uint16_t samplesPerBlock; // IMA: muestras por canal en cada bloque (0 en PCM)
  uint32_t frames;          // muestras por canal (chunk 'fact' o calculado); 0 = no se sabe
} FlexWavInfo;

int      flexWavParse(const FlexMediaIO* io, FlexWavInfo* w);
uint32_t flexWavDurationMs(const FlexWavInfo* w);

// Decodifica UN bloque IMA ADPCM (disposicion de Microsoft) a PCM de 16
// bits intercalado. `n` son los bytes del bloque (el ultimo del fichero
// puede venir corto). Devuelve las muestras por canal escritas (como
// mucho `maxFrames`) o -1 si el bloque no es valido.
int      flexImaDecodeBlock(const uint8_t* blk, size_t n, int channels,
                            int16_t* out, int maxFrames);

// -------------------------------------------------------------
//  REPRODUCCION POR BLOQUES (WAV PCM de 8/16 bits e IMA ADPCM)
//  ------------------------------------------------------------
//  Lo que suena sale SIEMPRE como PCM de 16 bits con signo,
//  intercalado: el PCM de 8 bits (sin signo en WAV) se convierte y el
//  IMA ADPCM se decodifica bloque a bloque. Asi el destino (el DMA de
//  I2S en la placa) tiene un solo formato que atender.
//
//  El destino acepta lo que pueda AHORA (una escritura que no espera):
//  lo que no acepta se queda pendiente y sale en la siguiente llamada,
//  sin perder ni repetir una muestra. Memoria: la pone el llamante
//  (flexAsWorkBytes) y no se reserva nada aqui.
// -------------------------------------------------------------
// Bytes aceptados (0 = lleno ahora mismo), o -1 si el destino fallo.
typedef int (*FlexAsSink)(void* ctx, const void* pcm, size_t n);
typedef struct {
  FlexMediaIO io;
  FlexWavInfo wav;
  uint32_t    pos, end;          // proximo byte del archivo y fin de los datos
  uint32_t    ioPos;             // donde esta el descriptor (evita buscar por nada)
  uint8_t*    blk; uint32_t blkCap;   // lectura cruda (bloque IMA o PCM de 8 bits)
  uint8_t*    buf; uint32_t bufCap;   // PCM de 16 bits listo para salir
  uint32_t    bufLen, bufOff;
  uint64_t    produced;          // bytes de PCM de salida ya preparados
  uint64_t    delivered;         // ...y ya aceptados por el destino (la posicion)
  uint64_t    limit;             // tope de salida ('fact' del IMA), o ~0
  uint16_t    outFrame;          // bytes por muestra de salida (canales x 2)
  bool        ended;
} FlexAudioStream;

// Memoria de trabajo que necesita ese WAV (0 = formato que no se reproduce).
size_t   flexAsWorkBytes(const FlexWavInfo* w);
// Prepara la reproduccion de un WAV ya analizado (flexWavParse) desde el
// principio. `work` debe tener al menos flexAsWorkBytes(w) bytes.
bool     flexAsOpen(FlexAudioStream* s, const FlexMediaIO* io, const FlexWavInfo* w,
                    uint8_t* work, size_t cap);
// Entrega al destino lo que acepte ahora, como mucho `budget` bytes.
// Devuelve los entregados (>= 0), o -1 si el archivo o el destino fallaron.
// s->ended = ya no queda nada que entregar.
int      flexAsPump(FlexAudioStream* s, FlexAsSink sink, void* ctx, uint32_t budget);
uint32_t flexAsPosMs(const FlexAudioStream* s);
uint32_t flexAsDurMs(const FlexAudioStream* s);
// Salta a `ms` (al principio del bloque que lo contiene, en IMA). Lo
// pendiente se descarta. false si no hay nada abierto.
bool     flexAsSeekMs(FlexAudioStream* s, uint32_t ms);

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
#define FLEXMED_DEPTH_MAX    3

enum { FLEXMED_SCAN_IDLE = 0, FLEXMED_SCAN_RUNNING, FLEXMED_SCAN_DONE, FLEXMED_SCAN_ABORTED };

typedef struct {
  const char* path;
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
  FlexMediaVolume volume;

  // ---- estado del recorrido ----
  uint8_t  state;
  uint8_t  rootI;                              // raiz en curso
  int8_t   depth;                              // -1 = entre raices
  char     stackPath[FLEXMED_DEPTH_MAX][FLEXMED_PATH_MAX];
  uint32_t stackSkip[FLEXMED_DEPTH_MAX];       // entradas ya consumidas del nivel
  uint32_t seen;                               // entradas miradas (para el progreso)
} FlexMediaIndex;

// Prepara el indice sobre el array del llamante. No recorre nada.
void flexMediaIndexInit(FlexMediaIndex* ix, FlexMediaItem* store, uint16_t cap);

// Anade una raiz que se recorrera. El orden importa: las primeras
// se indexan antes, asi que las carpetas de Flex OS van delante.
void flexMediaIndexAddRoot(FlexMediaIndex* ix, const char* path);

// Empieza (o reempieza) el recorrido. Vacia lo indexado.
void flexMediaIndexStart(FlexMediaIndex* ix);

// Avanza el recorrido consumiendo COMO MUCHO `budget` entradas de
// directorio, y para. Devuelve el estado. Pensada para llamarse una
// vez por vuelta de loop() con un presupuesto pequeno (8-24): asi el
// indice se construye en segundo plano sin que la interfaz pierda
// un solo cuadro.
int  flexMediaIndexStep(FlexMediaIndex* ix, int budget);

// Corta el recorrido (el almacenamiento fallo o se cierra la app).
void flexMediaIndexAbort(FlexMediaIndex* ix);

// Cuenta los elementos de una clase (FLEXMED_PHOTO...) o de todas
// si `kind` es 0.
int  flexMediaIndexCount(const FlexMediaIndex* ix, int kind);

// Indice real dentro de items[] del elemento `nth` que cumple el
// filtro, o -1. Es lo que permite a la Galeria pintar la pestana
// "Videos" sin construir una segunda lista.
int  flexMediaIndexNth(const FlexMediaIndex* ix, int kind, int nth);

#ifdef __cplusplus
}
#endif
