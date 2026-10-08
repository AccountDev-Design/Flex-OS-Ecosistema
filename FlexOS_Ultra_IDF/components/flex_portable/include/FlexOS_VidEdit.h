// #############################################################
// ##  FlexOS · EDITOR DE VIDEO DE LA GALERIA · el nucleo (portable)
// #############################################################
//
//  QUE ES
//  ------
//  La parte del editor de video que DECIDE que sale: que tramos del
//  original se conservan (recortar el principio y el final, dividir y
//  quitar partes), el encuadre, el giro, la velocidad, el volumen del
//  audio ORIGINAL, un texto, un filtro y el fotograma de portada. Y el
//  exportador que lo convierte en un AVI MJPEG nuevo, fotograma a
//  fotograma. Compila igual en el P4 y en el PC, donde se prueba con
//  AddressSanitizer y UndefinedBehaviorSanitizer (tests/host/test_videdit).
//
//  LO QUE EL P4 SABE HACER CON UN VIDEO, Y NADA MAS
//  ------------------------------------------------
//  El P4 no tiene decodificador de video por hardware: el UNICO video que
//  reproduce es AVI con fotogramas MJPEG (cada fotograma es un JPEG que
//  pasa por FlexOS_JPEG). Asi que se edita AVI MJPEG y se exporta AVI
//  MJPEG. El audio que se puede ajustar es el que el propio P4 decodifica
//  (PCM de 8/16 bits e IMA ADPCM); sale como PCM de 16 bits. Un audio en
//  otro codec (MP3 dentro del AVI, por ejemplo) no se puede tocar aqui: el
//  video editado sale sin el, y la interfaz lo dice ANTES de exportar.
//  No hay audio externo: el editor solo cambia el volumen del que ya trae.
//
//  EDICION NO DESTRUCTIVA
//  ----------------------
//  Mover un extremo de la linea de tiempo, arrastrar el recorte o elegir
//  una velocidad solo cambia PARAMETROS (FlexVeParams, unos cientos de
//  bytes con su historial). Ni un fotograma se procesa hasta exportar.
//
//  EXPORTAR SIN CARGAR EL VIDEO EN MEMORIA
//  ---------------------------------------
//  Un fotograma cada vez, en bloques:
//
//     archivo -> trozo MJPEG (buffer reutilizable)
//             -> decodificar SOLO la region del recorte, a la escala justa
//             -> recorte/giro/filtro/texto (FlexOS_ImgEdit, el mismo motor
//                que el editor de fotos: mismo aspecto, sin duplicar nada)
//             -> JPEG por bandas (FlexOS_JPEGEnc)
//             -> trozo '00dc' en el AVI de salida
//
//  Toda la memoria se pide UNA vez al empezar (flexVeExportBegin) y se
//  suelta al final (flexVeExportFree). La que el decodificador y el
//  codificador piden en cada fotograma sale de una ARENA fija que se
//  vacia entre fotogramas: cero malloc/free por fotograma, cero
//  fragmentacion. Si nada cambia en los pixeles (solo tiempo, velocidad,
//  volumen o portada), los fotogramas se COPIAN tal cual: sin perdida y
//  sin decodificar.
//
//  La salida se escribe en orden y, al final, se vuelve al principio para
//  poner los tamanos reales en la cabecera (la cabecera mide siempre lo
//  mismo). El indice 'idx1' va al final, como en cualquier AVI.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "FlexOS_Media.h"
#include "FlexOS_JPEG.h"
#include "FlexOS_JPEGEnc.h"
#include "FlexOS_ImgEdit.h"

#ifdef __cplusplus
extern "C" {
#endif

// -------------------------------------------------------------
//  Limites
// -------------------------------------------------------------
#define FLEXVE_SEG_MAX        8          // tramos conservados como mucho (dividir crea tramos)
#define FLEXVE_HIST_MAX       24         // pasos de deshacer (FlexVeParams ~ 200 B: 5 KB)
#define FLEXVE_TEXT_MAX       FLEXIE_TEXT_MAX
#define FLEXVE_OUT_FRAMES_MAX 36000u     // tope de fotogramas de salida (el de 8 MB llega antes)
#define FLEXVE_SCAN_MAX       262144u    // trozos que se recorren al analizar el archivo
#define FLEXVE_QUALITY        85         // JPEG de cada fotograma recodificado
#define FLEXVE_NO_COVER       0xFFFFFFFFu
#define FLEXVE_SPEED_N        7
#define FLEXVE_AUD_WIN_MIN    2048u      // muestras (por canal) de la ventana de audio decodificado
#define FLEXVE_THUMB_MAX_DIV  8

// Velocidades que ofrece la interfaz (tanto por ciento).
extern const uint16_t FLEXVE_SPEEDS[FLEXVE_SPEED_N];
// Alto del texto: fraccion del lado LARGO de la salida (pequeno, mediano, grande).
extern const float FLEXVE_TEXT_H[3];

enum {
  FLEXVE_OK         =   0,
  FLEXVE_ERR_ARG    =  -1,
  FLEXVE_ERR_IO     =  -2,   // no se pudo leer el original
  FLEXVE_ERR_FORMAT =  -3,   // AVI danado o que no se entiende
  FLEXVE_ERR_MEMORY =  -4,
  FLEXVE_ERR_WRITE  =  -5,   // no se pudo escribir (almacenamiento lleno)
  FLEXVE_ERR_CANCEL =  -6,
  FLEXVE_ERR_FRAME  =  -7,   // un fotograma de salida pasa de FLEXAVI_FRAME_MAX
  FLEXVE_ERR_LIMIT  =  -8,   // el archivo de salida pasaria del tope pedido
  FLEXVE_ERR_CODEC  =  -9,   // AVI sin video MJPEG
  FLEXVE_ERR_VERIFY = -10    // lo escrito no pasa la comprobacion
};
const char* flexVeErrStr(int err);

// Se llama entre fotogramas (y cada tanto al analizar): devolver false
// CANCELA. Es tambien donde la tarea cede la CPU.
typedef bool (*FlexVeTickFn)(void* ctx);

// -------------------------------------------------------------
//  EL ORIGINAL
// -------------------------------------------------------------
enum { FLEXVE_AUD_NONE = 0, FLEXVE_AUD_PCM, FLEXVE_AUD_IMA, FLEXVE_AUD_OTHER };

typedef struct {
  uint8_t  kind;               // FLEXVE_AUD_*
  uint8_t  stream;             // numero de pista ('01' -> 1)
  uint16_t tag;                // wFormatTag (0x0001 PCM, 0x0011 IMA, 0x0055 MP3...)
  uint16_t channels;           // 1 o 2 (los que se procesan)
  uint16_t bits;               // 8 / 16 (PCM) o 4 (IMA)
  uint16_t blockAlign;         // bytes por bloque (IMA) o por muestra (PCM)
  uint16_t samplesPerBlock;    // IMA
  uint32_t rate;               // muestras por segundo
  uint32_t bytes;              // bytes de audio en el archivo (analisis)
  uint32_t chunks;             // trozos de audio (analisis)
} FlexVeAudio;

typedef struct {
  uint16_t w, h;
  uint32_t frames;             // fotogramas REALES (analisis; una grabacion cortada tiene menos que los declarados)
  uint32_t usPerFrame;
  uint32_t videoBytes;         // suma de los fotogramas
  uint32_t maxFrame;           // el mayor fotograma
  uint32_t emptyFrames;        // trozos vacios (repiten el anterior)
  uint32_t fileBytes;
  uint32_t moviStart, moviEnd;
  uint8_t  videoStream;
  FlexVeAudio aud;
} FlexVeSource;

// Analiza el AVI: cabecera (flexAviOpen sobre `ctx`, que pone el llamante y
// se puede seguir usando despues para buscar fotogramas), formato del audio y
// un recorrido de 'movi' que solo lee las cabeceras de 8 bytes de cada trozo
// (acotado a FLEXVE_SCAN_MAX). `tick` puede cancelar.
int flexVeProbe(const FlexMediaIO* io, FlexVeSource* src, FlexAviCtx* ctx, FlexVeTickFn tick, void* tickCtx);
uint32_t flexVeSrcDurationMs(const FlexVeSource* s);

// -------------------------------------------------------------
//  LA EDICION (parametros + historial)
// -------------------------------------------------------------
typedef struct { uint32_t a, b; } FlexVeSeg;     // fotogramas [a, b) del original

enum { FLEXVE_ASP_FREE = 0, FLEXVE_ASP_ORIG, FLEXVE_ASP_16_9, FLEXVE_ASP_4_3, FLEXVE_ASP_1_1, FLEXVE_ASP_9_16, FLEXVE_ASP_N };

typedef struct {
  uint8_t   nSeg;                          // >= 1
  FlexVeSeg seg[FLEXVE_SEG_MAX];           // en orden, sin solaparse (pueden tocarse: una division)
  uint8_t   rot;                           // cuartos de vuelta a la derecha (0..3)
  uint8_t   aspect;                        // FLEXVE_ASP_* (lo que eligio el usuario; bloquea la proporcion)
  float     c0x, c0y, c1x, c1y;            // recorte, normalizado sobre el fotograma YA girado
  uint16_t  speedPct;                      // 25..200
  uint8_t   volPct;                        // 0..100
  uint8_t   mute;
  uint8_t   filter;                        // FLEXIE_FILTER_*
  uint8_t   textSize;                      // 0..2
  uint32_t  textRgb;                       // 0xRRGGBB
  float     textU, textV;                  // esquina de arriba a la izquierda, normalizada sobre la SALIDA
  char      text[FLEXVE_TEXT_MAX];         // "" = sin texto
  uint32_t  cover;                         // fotograma del original para la portada (FLEXVE_NO_COVER = el primero)
} FlexVeParams;

typedef struct {
  FlexVeParams p;                          // estado vigente (puede ir por delante del historial)
  FlexVeParams hist[FLEXVE_HIST_MAX];
  int          cur, top;
  uint32_t     frames;                     // fotogramas del original
  uint32_t     usPerFrame;
  uint32_t     minSeg;                     // un tramo no baja de esto (~0,2 s)
  int          srcW, srcH;                 // para las proporciones del recorte
} FlexVeEdit;

void flexVeInit(FlexVeEdit* e, uint32_t frames, uint32_t usPerFrame, int srcW, int srcH);
void flexVeCommit(FlexVeEdit* e);          // confirma p como un paso nuevo (si cambio algo)
bool flexVeCanUndo(const FlexVeEdit* e);
bool flexVeCanRedo(const FlexVeEdit* e);
bool flexVeUndo(FlexVeEdit* e);
bool flexVeRedo(FlexVeEdit* e);
void flexVeReset(FlexVeEdit* e);           // como se abrio (se puede deshacer)
// Nada cambiado respecto al original (exportar no tendria sentido).
bool flexVeIsIdentity(const FlexVeEdit* e);
// Algo cambia en los PIXELES (recorte, giro, filtro, texto): hay que recodificar.
bool flexVeTouchesPixels(const FlexVeParams* p);

// ---- Tiempo ----
// Mueve el principio (which = 0) o el final (which = 1) de lo que se conserva,
// SIN confirmar (arrastre); se confirma con flexVeCommit al soltar.
void flexVeTrimLive(FlexVeEdit* e, int which, uint32_t frame);
bool flexVeCanSplitAt(const FlexVeEdit* e, uint32_t at);
bool flexVeSplit(FlexVeEdit* e, uint32_t at);          // divide el tramo que contiene `at` (confirma)
bool flexVeRemoveSeg(FlexVeEdit* e, int i);            // quita un tramo (siempre queda uno; confirma)
int  flexVeSegAt(const FlexVeParams* p, uint32_t frame); // tramo que contiene el fotograma, -1 = quitado
uint32_t flexVeKeptFrames(const FlexVeParams* p);
uint32_t flexVeFirstKept(const FlexVeParams* p);
uint32_t flexVeLastKept(const FlexVeParams* p);        // el ultimo fotograma conservado (incluido)

// ---- Geometria ----
void flexVeRotate(FlexVeEdit* e, int quarters);         // el recorte gira con la imagen (confirma)
void flexVeSetRot(FlexVeEdit* e, int rot);              // giro absoluto (confirma)
// Proporcion fija: recorte centrado y lo mas grande posible (confirma).
void flexVeSetAspect(FlexVeEdit* e, int aspect);
// Proporcion (ancho/alto) que impone `aspect` sobre el fotograma girado; 0 = libre.
float flexVeAspectRatio(const FlexVeEdit* e, int aspect);
// Recorte normalizado sobre el fotograma girado (acota y ordena; sin confirmar).
void flexVeCropLive(FlexVeEdit* e, float x0, float y0, float x1, float y1);

// ---- Mapa de tiempos ----
// Salida de un tramo: con velocidad >= 1x se toma un fotograma de cada
// velocidad (mismos fps, menos fotogramas); por debajo de 1x se conservan
// todos y cada uno dura mas (menos fps). Sin fotogramas inventados.
uint32_t flexVeSegOutFrames(const FlexVeParams* p, const FlexVeSeg* s);
uint32_t flexVeOutFrames(const FlexVeParams* p, int s0, int s1);      // tramos [s0, s1]
uint32_t flexVeOutPeriodUs(const FlexVeParams* p, uint32_t srcUs);
uint64_t flexVeOutDurationUs(const FlexVeParams* p, uint32_t srcUs, int s0, int s1);
uint32_t flexVeSrcFrameOf(const FlexVeParams* p, const FlexVeSeg* s, uint32_t k);
// Fotograma de salida en el que cae `src` (o el primero posterior conservado), -1 si ninguno.
int32_t  flexVeOutFrameOf(const FlexVeParams* p, int s0, int s1, uint32_t src);

// ---- Region y tamano de salida ----
// Region del ORIGINAL (sin girar, pixeles) que cubre el recorte.
void flexVeCropRect(const FlexVeParams* p, int W, int H, int* x0, int* y0, int* x1, int* y1);
// Tamano del recorte ya girado, a resolucion completa.
void flexVeRegionSize(const FlexVeParams* p, int W, int H, int* rw, int* rh);
// Tamano de salida con el lado CORTO en `shortSide` (0 = el del recorte). false =
// seria mas grande que el recorte (no se amplia).
bool flexVeFitShort(int rw, int rh, int shortSide, int* ow, int* oh);
// Mayor divisor 1/2/4/8 que aun deja la region (rw x rh, sin girar) >= lo pedido.
int  flexVePickDiv(int rw, int rh, int needW, int needH);

// -------------------------------------------------------------
//  PINTAR UN FOTOGRAMA (vista previa y exportacion)
// -------------------------------------------------------------
// Donde esta la base dentro del original cuando es solo la REGION del
// recorte (la exportacion): medidas del original, divisor y esquina a 1/div.
typedef struct { int srcW, srcH, div, rx0, ry0; } FlexVeRegion;
// Prepara el motor del editor de fotos para pintar un fotograma con estos
// parametros. `base` (bw x bh, RGB888) es el fotograma ENTERO (region = NULL)
// o solo la region del recorte (la exportacion). outLong = lado largo de la
// salida (0 = el de la base). cropView = la vista de Encuadre: el fotograma
// entero girado, sin recorte y sin texto. El texto usa la fuente que se le
// haya puesto a `ie` (flexIeSetTextFn).
void flexVeSetupRender(FlexImgEdit* ie, const uint8_t* base, int bw, int bh, const FlexVeParams* p,
                       const FlexVeRegion* region, int outLong, bool cropView);

// ---- Arena: la memoria de trabajo de cada fotograma ----
typedef struct { uint8_t* p; uint32_t cap, used, peak; } FlexVeArena;
void  flexVeArenaInit(FlexVeArena* a, void* mem, uint32_t cap);
void* flexVeArenaAlloc(FlexVeArena* a, size_t n);          // alineado a 16; NULL si no cabe
void  flexVeArenaReset(FlexVeArena* a);
// Lo que pide decodificar un fotograma de `srcW` de ancho, y codificar uno de
// `outW` (con texto). De sobra: se calcula con el peor submuestreo.
uint32_t flexVeDecodeWork(int srcW);
uint32_t flexVeEncodeWork(int outW);

// Decodifica un JPEG a 1/div dejando SOLO la region [rx0,rx1) x [ry0,ry1)
// (pixeles de la imagen YA reducida) en `dst` (RGB888, (rx1-rx0)*3 por fila).
// expW/expH: medidas que tiene que tener el fotograma (otras = danado). Deja de
// decodificar en cuanto tiene la ultima fila de la region. `ar` = arena de
// trabajo (NULL = malloc).
int flexVeDecode(const uint8_t* jpg, uint32_t len, int expW, int expH, int div,
                 int rx0, int ry0, int rx1, int ry1, uint8_t* dst, FlexVeArena* ar);
// Miniatura tw x th (RGB565, recortada al centro) de un fotograma JPEG de
// expW x expH, sin buffer intermedio: se muestrea a medida que salen las filas.
int flexVeThumb(const uint8_t* jpg, uint32_t len, int expW, int expH, int tw, int th, uint16_t* dst, FlexVeArena* ar);

// -------------------------------------------------------------
//  EXPORTAR
// -------------------------------------------------------------
typedef bool (*FlexVeWriteFn)(void* ctx, const void* data, size_t n);   // TODO o false
typedef bool (*FlexVeSeekFn)(void* ctx, uint32_t off);

typedef struct {
  FlexMediaIO         io;              // el original
  const FlexVeSource* src;
  const FlexVeParams* params;          // se copia al empezar
  int                 seg0, seg1;      // tramos que se exportan [seg0, seg1] (una parte, o todos)
  FlexVeWriteFn       write;           // la salida (secuencial) ...
  FlexVeSeekFn        seek;            // ... y volver al principio para la cabecera
  void*               outCtx;
  int                 outW, outH;      // tamano pedido (ya girado); 0 = el del recorte
  int                 quality;         // JPEG (0 = FLEXVE_QUALITY)
  uint32_t            maxBytes;        // tope del archivo (0 = sin tope)
  FlexImgEdit*        ie;              // motor de pintar (con su fuente puesta); solo si hay que recodificar
  FlexJpegAlloc       alloc;           // reservas LARGAS (una vez por exportacion)
  FlexJpegFree        free;
  FlexVeTickFn        tick;
  void*               tickCtx;
} FlexVeExportCfg;

// Lector del audio original: las cargas de los trozos '##wb' de su pista,
// como un solo flujo de bytes, decodificado a PCM de 16 bits en una ventana.
typedef struct {
  FlexMediaIO io;
  FlexVeAudio fmt;
  uint32_t cursor, moviEnd;            // proximo trozo de 'movi'
  uint32_t chunkOff, chunkLeft;        // bytes pendientes del trozo actual
  uint8_t* raw;  uint32_t rawCap;      // un bloque IMA o un lote de PCM
  int16_t* win;  uint32_t winCap;      // muestras (por canal) de la ventana
  uint32_t winN;
  uint64_t winStart;                   // indice de la primera muestra de la ventana
  uint64_t decoded;                    // muestras entregadas a la ventana hasta ahora
  bool     eof, err;
} FlexVeAudioRd;

typedef struct {
  FlexVeExportCfg c;
  FlexVeParams    p;
  // plan
  uint32_t outTotal, outDone;
  uint32_t periodUs;                   // fotograma de salida
  uint16_t spd;
  int      seg;                        // tramo en curso
  uint32_t kInSeg;
  int32_t  coverOut;                   // fotograma de salida de la portada (-1 = el primero)
  bool     hasInfo;                    // la cabecera lleva 'LIST INFO' con la portada
  int32_t  lastRealOut;                // ultimo fotograma de salida CON imagen (no repetido)
  bool     copy;                       // se copian los fotogramas tal cual
  bool     audioOn;
  bool     audioDropped;               // el original trae audio que no se puede procesar
  int      outW, outH;                 // de verdad (el motor de pintar manda)
  // video de entrada
  uint32_t vCursor;                    // proximo trozo de 'movi'
  uint32_t vFrame;                     // indice del proximo fotograma que se lee
  uint32_t curOff, curLen;             // imagen vigente: el ultimo fotograma con datos
  bool     haveCur;
  uint32_t lastOutOff;                 // imagen del ultimo fotograma escrito
  bool     haveOut;
  uint32_t badFrames;                  // danados (se repitio el anterior)
  // buffers (una vez)
  uint8_t* inBuf;   uint32_t inCap;
  uint8_t* outBuf;  uint32_t outCap, outLen;
  bool     outOver;                    // un fotograma no cupo
  uint8_t* base;    int bw, bh, div;
  int      rx0, ry0, rx1, ry1;         // region a 1/div
  uint8_t* arenaMem;
  FlexVeArena arena;
  uint32_t* idx;    uint32_t idxCap, idxN;   // (desplazamiento, tamano | bit de audio)
  int16_t* aOut;    uint32_t aOutCap;        // muestras de un trozo de audio de salida
  FlexVeAudioRd ar;
  uint64_t aPos;                       // posicion en el audio original (32.32)
  uint64_t aStep;
  uint64_t aOutSamples;                // muestras de salida escritas
  // salida
  uint32_t pos;                        // bytes escritos
  uint32_t hdrBytes;
  uint32_t moviList;                   // desplazamiento del 'LIST' de 'movi'
  uint32_t moviEndPos;                 // donde acaba 'movi' (empieza 'idx1')
  uint32_t vChunks, aChunks, maxV, maxA;
  uint64_t aBytes;
  bool     finished;
  int      err;
} FlexVeExport;

// Prepara la exportacion: plan, memoria (TODA la que se va a usar) y la
// cabecera. FLEXVE_ERR_MEMORY si algo no cabe: no se escribe nada mas.
int  flexVeExportBegin(FlexVeExport* x, const FlexVeExportCfg* cfg);
// Un fotograma de salida (con su audio). 1 = quedan mas, 0 = hecho, <0 = error.
int  flexVeExportStep(FlexVeExport* x);
// Indice y cabecera definitiva. Solo tras un Step que devolvio 0.
int  flexVeExportFinish(FlexVeExport* x);
// Suelta todo (siempre, tambien tras un error o una cancelacion).
void flexVeExportFree(FlexVeExport* x);
// Progreso 0..1000.
int  flexVeExportProgress(const FlexVeExport* x);

// PSRAM que pedira flexVeExportBegin (de sobra), para comprobarla ANTES.
uint32_t flexVeExportWorkBytes(const FlexVeSource* s, const FlexVeParams* p, int seg0, int seg1, int outW, int outH);
// Tamano aproximado del archivo (por arriba), para comprobar el espacio.
uint32_t flexVeEstimateBytes(const FlexVeSource* s, const FlexVeParams* p, int seg0, int seg1, int outW, int outH);
// true = con estos parametros y este tamano los fotogramas se copian tal cual.
bool flexVeCopyMode(const FlexVeSource* s, const FlexVeParams* p, int outW, int outH);

// Comprueba lo escrito: cabecera, medidas, fotogramas, indice, primer y
// ultimo fotograma legibles y, si se espera, la pista de audio. `ctx` lo pone
// el llamante (al monton: lleva el indice disperso).
int flexVeVerify(const FlexMediaIO* io, uint32_t fileBytes, uint32_t expFrames, int expW, int expH,
                 bool expAudio, FlexAviCtx* ctx, FlexJpegAlloc af, FlexJpegFree ff);

#ifdef __cplusplus
}
#endif
