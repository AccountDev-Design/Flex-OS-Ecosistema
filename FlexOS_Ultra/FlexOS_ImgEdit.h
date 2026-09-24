// #############################################################
// ##  FlexOS · EDITOR DE IMAGENES · el nucleo (portable)
// #############################################################
//
//  QUE ES
//  ------
//  La parte del editor de la Galeria que DECIDE que sale: recortar,
//  girar, voltear, ajustes de color, filtros, trazos, formas y textos,
//  con deshacer y rehacer. Sin pantalla, sin archivos y sin reservar
//  memoria: compila igual en el P4 y en el PC, donde se prueba
//  (tests/host/test_imgedit.cpp).
//
//  EDICION NO DESTRUCTIVA
//  ----------------------
//  La foto que se abre (la BASE, RGB888) no se toca nunca. Lo que se
//  guarda es un ESTADO: geometria, ajustes, filtro y cuantos trazos,
//  formas y textos hay. Deshacer es volver al estado anterior, no
//  reconstruir pixeles. Y la imagen editada se CALCULA fila a fila a
//  cualquier tamano: a tamano de pantalla para la vista previa, y a
//  tamano completo, por bandas, directamente hacia el codificador JPEG
//  al guardar. Por eso guardar no necesita una segunda copia de la foto.
//
//  COORDENADAS
//  -----------
//  Los trazos, formas y textos se guardan NORMALIZADOS (0..1) sobre la
//  base SIN girar: van pegados al contenido. Girar, voltear o recortar
//  despues los mueve con la foto. Los giros son de 90 grados, asi que un
//  rectangulo sigue siendo un rectangulo; el texto se mantiene derecho.
//
//  LIMITE REAL DEL P4
//  ------------------
//  La base cabe en PSRAM o no hay edicion: una foto de 12 MP en RGB888
//  son 36 MB y la placa tiene 32. El sketch la abre reducida (1/2, 1/4...
//  lo hace el propio decodificador) hasta FLEXIE_BASE_MAX_PX, y al
//  guardar dice a que resolucion sale.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "FlexOS_JPEG.h"
#include "FlexOS_JPEGEnc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLEXIE_BASE_MAX_PX  (2048u * 1536u)   // ~3,1 MP: 9,4 MB en RGB888
#define FLEXIE_HIST_MAX     40                // pasos de deshacer
#define FLEXIE_OV_MAX       48                // trazos + formas + textos
#define FLEXIE_PTS_MAX      4096              // puntos de todos los trazos
#define FLEXIE_TEXT_MAX     48
#define FLEXIE_MIN_CROP     0.02f             // el recorte nunca baja de un 2 % del lado

enum {
  FLEXIE_ADJ_BRIGHT = 0,   // brillo
  FLEXIE_ADJ_CONTRAST,     // contraste
  FLEXIE_ADJ_SATUR,        // saturacion
  FLEXIE_ADJ_EXPOSURE,     // exposicion
  FLEXIE_ADJ_TEMP,         // temperatura (calido +, frio -)
  FLEXIE_ADJ_SHADOWS,      // sombras
  FLEXIE_ADJ_HIGHLIGHTS,   // luces
  FLEXIE_ADJ_SHARP,        // nitidez (negativo = suavizar)
  FLEXIE_ADJ_N
};
enum {
  FLEXIE_FILTER_NONE = 0, FLEXIE_FILTER_BW, FLEXIE_FILTER_SEPIA, FLEXIE_FILTER_VIVID,
  FLEXIE_FILTER_COOL, FLEXIE_FILTER_WARM, FLEXIE_FILTER_VINTAGE, FLEXIE_FILTER_N
};
enum { FLEXIE_OV_STROKE = 1, FLEXIE_OV_RECT, FLEXIE_OV_ELLIPSE, FLEXIE_OV_LINE, FLEXIE_OV_ARROW, FLEXIE_OV_TEXT };

typedef struct { float x, y; } FlexIePt;

typedef struct {
  uint8_t  kind;                 // FLEXIE_OV_*
  uint8_t  fill;                 // rectangulo/elipse rellenos
  uint32_t rgb;                  // 0xRRGGBB
  float    w;                    // grosor: fraccion del lado LARGO de la base
  float    x0, y0, x1, y1;       // base normalizada (texto: x0,y0 = esquina de arriba a la izquierda)
  uint16_t p0, pn;               // trazo: pts[p0 .. p0+pn)
  float    th;                   // texto: alto, fraccion del lado largo de la base
  char     text[FLEXIE_TEXT_MAX];
} FlexIeOverlay;

// Lo que deshacer/rehacer recorre. Pequeno a proposito: 40 pasos son 2 KB.
typedef struct {
  uint8_t  rot;                  // giro: 0..3 cuartos de vuelta a la derecha
  uint8_t  flipH, flipV;         // volteos DESPUES del giro
  uint8_t  filter, filterAmt;    // FLEXIE_FILTER_*, 0..100
  int8_t   adj[FLEXIE_ADJ_N];    // -100..100
  uint16_t longSide;             // lado largo de la salida (0 = el del recorte)
  float    c0x, c0y, c1x, c1y;   // recorte, normalizado sobre la imagen YA girada
  uint16_t nOv, nPts;            // cuantos trazos/formas/textos y puntos valen
  uint16_t ovStart;              // se ven ov[ovStart .. nOv): Restablecer esconde, no borra
} FlexIeState;

// Texto: cobertura 0..255 de la fila `row` (0 = arriba) de `s` a `hpx`
// pixeles de alto, para las columnas del texto [col0, col0 + covW) (col0
// puede ser negativa: el texto empieza fuera de la imagen). Devuelve el
// ANCHO del texto en pixeles (con row < 0 solo se pregunta el ancho). Lo
// pone el sketch (la fuente del sistema); sin el, los textos no se dibujan.
typedef int (*FlexIeTextFn)(void* ctx, const char* s, int hpx, int row, int col0, uint8_t* cov, int covW);

typedef struct {
  const uint8_t* base;           // RGB888, W x H, sin relleno entre filas
  int            W, H;
  const uint8_t* proxy;          // copia reducida de la base (opcional): vista previa rapida
  int            PW, PH;
  FlexIeState    st;             // estado vigente (puede ir por delante del historial)
  FlexIeState    hist[FLEXIE_HIST_MAX];
  int            cur, top;       // hist[cur] = el ultimo estado confirmado; top = el ultimo rehacible
  FlexIeOverlay  ov[FLEXIE_OV_MAX];
  FlexIePt       pts[FLEXIE_PTS_MAX];
  FlexIeTextFn   textFn;
  void*          textCtx;
  // ---- cache de color (se rehace cuando cambian ajustes o filtro) ----
  uint8_t        lutR[256], lutG[256], lutB[256];
  int32_t        mat[9];         // 3x3 en 1/4096
  int32_t        satK;           // saturacion en 1/4096
  int32_t        sharpK;         // nitidez en 1/4096 (negativo = suavizar)
  bool           matOn, vignette;
  uint32_t       colorKey;       // firma de lo que hay en la cache
} FlexImgEdit;

// ---- Abrir ----
void flexIeInit(FlexImgEdit* e, const uint8_t* base, int W, int H);
// Cambia la base SIN tocar el estado ni el historial: la foto se solto para
// devolver memoria y se vuelve a leer (todo esta normalizado, asi que vale
// aunque la nueva mida otra cosa). NULL = sin base: pintar y guardar no hacen nada.
void flexIeSetBase(FlexImgEdit* e, const uint8_t* base, int W, int H);
void flexIeSetTextFn(FlexImgEdit* e, FlexIeTextFn fn, void* ctx);
// Reduccion 1/1..1/8 que conviene pedir al decodificador para que la base
// no pase de FLEXIE_BASE_MAX_PX ni de `budgetBytes` (RGB888). 0 = no cabe.
int  flexIeDecodeScale(int w, int h, uint32_t budgetBytes);
// Vista previa rapida: una copia de la base reducida a `pw` x `ph` (media
// de cajas; `dst` = pw * ph * 3, la pone quien llama). Con ella puesta,
// pintar a tamano de pantalla lee la copia pequena en vez de la base
// entera, salvo que el recorte pida mas detalle del que la copia tiene.
// Guardar lee SIEMPRE la base.
void flexIeMakeProxy(const uint8_t* base, int W, int H, uint8_t* dst, int pw, int ph);
void flexIeSetProxy(FlexImgEdit* e, const uint8_t* proxy, int pw, int ph);

// ---- Historial ----
void flexIeCommit(FlexImgEdit* e);             // confirma st como un paso nuevo
bool flexIeUndo(FlexImgEdit* e);
bool flexIeRedo(FlexImgEdit* e);
bool flexIeCanUndo(const FlexImgEdit* e);
bool flexIeCanRedo(const FlexImgEdit* e);
void flexIeReset(FlexImgEdit* e);              // como se abrio (y se puede deshacer)
bool flexIeIsIdentity(const FlexImgEdit* e);   // nada cambiado respecto a la base

// ---- Cambios (todos confirman un paso, salvo los "Live") ----
void flexIeRotate(FlexImgEdit* e, int quarters);        // +1 = 90 a la derecha
void flexIeFlip(FlexImgEdit* e, bool horizontal);
void flexIeSetCrop(FlexImgEdit* e, float x0, float y0, float x1, float y1);
void flexIeSetLongSide(FlexImgEdit* e, int px);         // 0 = sin redimensionar
// Ajustes y filtro: "Live" mientras se arrastra (sin paso), luego Commit.
void flexIeAdjustLive(FlexImgEdit* e, int which, int value);
void flexIeFilterLive(FlexImgEdit* e, int filter, int amount);

// ---- Trazos, formas y textos ----
// Coordenadas en la SALIDA normalizada (0..1 sobre la imagen que se ve).
int  flexIeStrokeBegin(FlexImgEdit* e, uint32_t rgb, float widthOut, float u, float v);  // -1 = lleno
bool flexIeStrokeAdd(FlexImgEdit* e, int ov, float u, float v);
void flexIeStrokeEnd(FlexImgEdit* e, int ov);           // confirma el paso
int  flexIeAddShape(FlexImgEdit* e, int kind, uint32_t rgb, float widthOut, bool fill,
                    float u0, float v0, float u1, float v1);
int  flexIeAddText(FlexImgEdit* e, const char* text, uint32_t rgb, float heightOut, float u, float v);

// ---- Coordenadas ----
// Tamano de la salida a resolucion completa (recorte y redimension).
void flexIeOutSize(const FlexImgEdit* e, int* w, int* h);
// Mismo aspecto que la salida, dentro de maxW x maxH (para la vista previa).
void flexIeFitSize(const FlexImgEdit* e, int maxW, int maxH, int* w, int* h);
void flexIeOutToBase(const FlexImgEdit* e, float u, float v, float* s, float* t);
void flexIeBaseToOut(const FlexImgEdit* e, float s, float t, float* u, float* v);

// ---- Pintar ----
// Filas [y0, y0+rows) de la salida a outW x outH, en RGB888. `scratch`
// = outW * rows bytes (coberturas de trazos y textos) o NULL (sin ellos).
void flexIeRenderRows(FlexImgEdit* e, int outW, int outH, int y0, int rows, uint8_t* dst, uint8_t* scratch);
// true = pintar a outW x outH leeria la copia reducida (vista previa rapida).
bool flexIeUsesProxy(const FlexImgEdit* e, int outW, int outH);

// ---- Guardar ----
// La salida a resolucion completa, codificada a JPEG por bandas. `stop`
// (opcional) se consulta entre bandas: true = cancelar. `progress`
// (opcional) recibe filas hechas y totales.
typedef bool (*FlexIeStopFn)(void* ctx);
typedef void (*FlexIeProgressFn)(void* ctx, int done, int total);
int  flexIeSave(FlexImgEdit* e, int quality, FlexJeOutFn out, void* outCtx,
                FlexIeStopFn stop, FlexIeProgressFn progress, void* cbCtx,
                FlexJpegAlloc af, FlexJpegFree ff);

#ifdef __cplusplus
}
#endif
