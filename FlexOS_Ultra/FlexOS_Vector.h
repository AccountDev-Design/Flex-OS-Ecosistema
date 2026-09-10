#pragma once
// #############################################################
//  FLEX VECTOR PRO · NUCLEO VECTORIAL  (FlexOS_Vector.h/.cpp)
//  ------------------------------------------------------------
//  QUE ES. El motor del editor vectorial de Flex OS Ultra: el modelo
//  de documento (Documento -> Capas -> Elementos -> Geometria /
//  Apariencia / Transformacion), la geometria Bezier cubica, las
//  transformaciones afines, el rasterizador por lineas de barrido,
//  las operaciones booleanas, los gradientes, el diario de
//  deshacer/rehacer y la serializacion a SVG.
//
//  POR QUE VIVE FUERA DEL SKETCH. Es logica PURA: no toca Arduino, ni
//  el framebuffer, ni el sistema de archivos, ni reserva memoria por
//  su cuenta. Exactamente el mismo criterio que FlexOS_Mem.cpp,
//  FlexOS_Media.cpp o FlexOS_FlexLink.cpp. Eso permite compilarlo y
//  ejercitarlo ENTERO en el PC con AddressSanitizer y
//  UndefinedBehaviorSanitizer (tests/host/test_vector.cpp): aqui se
//  manejan indices de un pool compartido, listas de bordes y
//  aritmetica de matrices, y un desbordamiento en eso no puede
//  quedarse en "parece que funciona".
//
//  EL NUCLEO NO DIBUJA. El rasterizador emite TRAMOS horizontales por
//  callback (FlexVecSpanCb), igual que flexPaintReplay() emite
//  segmentos. Quien pinta es el modulo del sketch
//  (FlexOS_Ultra_AppVector.h), que traduce cada tramo a hLine/hLineA
//  sobre el framebuffer. Por eso el rasterizado se puede verificar en
//  el PC sin una placa delante.
//
//  MEMORIA. NADA se reserva aqui dentro. El anfitrion entrega los
//  bloques ya reservados en PSRAM (flexVecAttach) y el nucleo trabaja
//  solo sobre ellos: no hay malloc/new en ninguna ruta, y menos en la
//  de render. Los limites dela seccion "PRESUPUESTO" son duros: al
//  alcanzarlos la operacion falla de forma CONTROLADA (codigo de
//  error), nunca con un reinicio.
//
//  HARDWARE PARA EL QUE ESTA PENSADO: ESP32-P4 (dual-core RISC-V,
//  400 MHz), 32 MB de PSRAM, 16 MB de NOR Flash, pantalla 480x800.
//  Sin GPU: todo el render es CPU. Sin MicroSD: todo persiste en la
//  NOR Flash a traves de FlexOS_FS.
// #############################################################
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// -------------------------------------------------------------
//  1) PRESUPUESTO Y LIMITES DUROS
//  ------------------------------------------------------------
//  Salen del reparto de PSRAM documentado en docs/FLEX-VECTOR-PRO.md:
//  de los 32 MB, el sistema reserva 6 MB y los framebuffers ocupan
//  3 MB. Flex Vector Pro se compromete a no pasar de 2 MB, y con
//  estos numeros se queda en ~1,15 MB.
//
//  Ninguno de estos limites es decorativo: cada uno tiene su prueba
//  de "se alcanza y NO se rompe nada" en tests/host/test_vector.cpp.
// -------------------------------------------------------------
#define FLEXVEC_MAX_LAYERS       8      // capas por documento
#define FLEXVEC_MAX_ELEMS      256      // objetos por documento
#define FLEXVEC_MAX_NODES     4096      // pool GLOBAL de nodos (28 B cada uno = 112 KB)
#define FLEXVEC_MAX_NODES_PATH 256      // nodos por trazado
#define FLEXVEC_MAX_STOPS        8      // paradas por gradiente
#define FLEXVEC_MAX_GRADS        8      // gradientes por documento
#define FLEXVEC_TEXT_BYTES    4096      // pool de cadenas de texto
#define FLEXVEC_NAME_MAX        40      // nombre de documento / capa

// Rasterizador. FLEXVEC_MAX_EDGES acota la lista de bordes activos: un
// documento entero aplanado no puede pasar de aqui, y si lo hace el
// rasterizado se recorta (se dibuja lo que cabe) en vez de desbordar.
#define FLEXVEC_MAX_FLATPTS   2048      // puntos de la polilinea aplanada
#define FLEXVEC_MAX_EDGES     FLEXVEC_MAX_FLATPTS
#define FLEXVEC_MAX_SUBS       256      // subtrazados por elemento
#define FLEXVEC_AA_SUB           4      // sub-lineas de muestreo vertical (antialias)

// Deshacer/rehacer. El diario guarda en CADA registro el estado ANTES y
// el estado DESPUES, asi que deshacer y rehacer son la misma maquina
// recorrida en dos sentidos y no hay forma de que se desincronicen.
#define FLEXVEC_UNDO_BYTES  (256u * 1024u)
#define FLEXVEC_UNDO_STEPS     128

// Exportacion SVG.
#define FLEXVEC_SVG_BYTES    (96u * 1024u)

// Fase 2, todos acotados por el mismo motivo.
#define FLEXVEC_MAX_BLEND_STEPS  32
#define FLEXVEC_MAX_BRUSH_INST  128
#define FLEXVEC_MAX_REPEAT      64
#define FLEXVEC_MAX_APPEAR       3      // rellenos/trazos por objeto (Fase 2)
#define FLEXVEC_BOOL_GRID      256      // lado de la rejilla de las booleanas

// -------------------------------------------------------------
//  2) CODIGOS DE ERROR
//  ------------------------------------------------------------
//  Toda operacion que pueda no caber devuelve uno de estos. NINGUNA
//  aborta el proceso ni deja el documento a medias: o la operacion se
//  completa entera, o el documento queda exactamente como estaba.
// -------------------------------------------------------------
enum {
  FLEXVEC_OK = 0,
  FLEXVEC_E_FULL_ELEMS,     // no caben mas objetos
  FLEXVEC_E_FULL_NODES,     // no caben mas nodos en el pool
  FLEXVEC_E_FULL_LAYERS,    // no caben mas capas
  FLEXVEC_E_FULL_TEXT,      // no cabe mas texto
  FLEXVEC_E_FULL_GRADS,     // no caben mas gradientes
  FLEXVEC_E_TOO_COMPLEX,    // la operacion supera el limite de complejidad
  FLEXVEC_E_LOCKED,         // la capa o el objeto estan bloqueados
  FLEXVEC_E_BADARG,         // indice fuera de rango o argumento invalido
  FLEXVEC_E_NOMEM,          // el anfitrion no entrego los bloques (flexVecAttach)
  FLEXVEC_E_OVERFLOW,       // el buffer de salida se queda corto (SVG)
  FLEXVEC_E_EMPTY           // no hay nada que hacer (seleccion vacia, etc.)
};
const char* flexVecErrText(int err);

// -------------------------------------------------------------
//  3) GEOMETRIA
// -------------------------------------------------------------
// Bits de FlexVecNode.flags
#define FLEXVEC_N_START   0x01   // este nodo ABRE un subtrazado
#define FLEXVEC_N_CLOSE   0x02   // el subtrazado que abrio este nodo es CERRADO
#define FLEXVEC_N_SMOOTH  0x04   // tiradores simetricos (mover uno mueve el otro)
#define FLEXVEC_N_SEL     0x08   // nodo seleccionado (edicion directa)

// Un nodo Bezier cubico: ancla + tirador de ENTRADA + tirador de SALIDA,
// los tres en coordenadas de DOCUMENTO (no de pantalla). 28 bytes.
typedef struct {
  float   x,  y;      // ancla
  float   ix, iy;     // tirador de entrada (control del segmento anterior)
  float   ox, oy;     // tirador de salida  (control del segmento siguiente)
  uint8_t flags;
  uint8_t pad[3];
} FlexVecNode;

// Tipo de objeto. Todos comparten la MISMA geometria Bezier por debajo:
// 'kind' solo dice como se REGENERA la geometria si se editan sus
// parametros (radio de esquina, numero de lados...) y como se exporta.
enum {
  FLEXVEC_K_PATH = 0,   // trazado libre (Pluma)
  FLEXVEC_K_RECT,
  FLEXVEC_K_ELLIPSE,
  FLEXVEC_K_POLY,       // poligono regular
  FLEXVEC_K_STAR,
  FLEXVEC_K_LINE,
  FLEXVEC_K_TEXT,       // texto puntual (Fase 1) / de area (Fase 2)
  FLEXVEC_K_SYMBOL      // instancia de simbolo (Fase 2)
};

// -------------------------------------------------------------
//  4) APARIENCIA
// -------------------------------------------------------------
enum { FLEXVEC_P_NONE = 0, FLEXVEC_P_SOLID, FLEXVEC_P_GRAD, FLEXVEC_P_PATTERN };
enum { FLEXVEC_CAP_BUTT = 0, FLEXVEC_CAP_ROUND, FLEXVEC_CAP_SQUARE };
enum { FLEXVEC_JOIN_MITER = 0, FLEXVEC_JOIN_ROUND, FLEXVEC_JOIN_BEVEL };

// Una pintura: solido, gradiente o motivo. El color va en RGB888 y no en
// RGB565 a proposito -- el SVG que se exporta tiene que llevar el color
// que el usuario eligio, no el que cabe en la pantalla.
typedef struct {
  uint8_t  type;      // FLEXVEC_P_*
  uint8_t  alpha;     // 0..255
  int8_t   ref;       // indice de gradiente o de motivo (-1 = ninguno)
  uint8_t  pad;
  uint32_t rgb;       // 0x00RRGGBB
} FlexVecPaint;

// Gradiente. La LUT de 256 entradas se precomputa una sola vez (al crear
// o al editar una parada) y el relleno cuesta un acceso a tabla por
// pixel: es lo que lo hace viable sin GPU.
enum { FLEXVEC_G_LINEAR = 0, FLEXVEC_G_RADIAL };
typedef struct { float t; uint32_t rgb; uint8_t alpha; uint8_t pad[3]; } FlexVecStop;
typedef struct {
  uint8_t     type;
  uint8_t     nstops;
  uint8_t     lutReady;
  uint8_t     used;
  float       x0, y0, x1, y1;    // lineal: eje. radial: centro (x0,y0) y radio x1
  FlexVecStop stops[FLEXVEC_MAX_STOPS];
  uint32_t    lut[256];          // RGB888 interpolado
  uint8_t     lutA[256];         // alpha interpolado
} FlexVecGrad;

// MOTIVOS (patterns) · Fase 2
// -------------------------------------------------------------
//  PROCEDURALES, y es una decision. Un motivo "de verdad" -- un trozo
//  de dibujo repetido en mosaico -- obliga a rasterizar ese trozo a un
//  bitmap y a muestrearlo por pixel: son ~92 KB de PSRAM por motivo
//  vivo, y hay que rehacerlo cada vez que el motivo o el zoom cambian.
//  Un motivo procedural se evalua con aritmetica pura: cero memoria,
//  coste constante por pixel, y se exporta a SVG como un <pattern> con
//  geometria de verdad, no como una imagen incrustada.
//
//  Los seis tipos cubren lo que se usa en un dibujo vectorial: puntos,
//  rayas, rejilla, damero, diagonales y trama cruzada.
// -------------------------------------------------------------
enum { FLEXVEC_PAT_DOTS = 0, FLEXVEC_PAT_LINES, FLEXVEC_PAT_GRID,
       FLEXVEC_PAT_CHECKER, FLEXVEC_PAT_DIAGONAL, FLEXVEC_PAT_CROSS, FLEXVEC_PAT_N };
#define FLEXVEC_MAX_PATTERNS 6
typedef struct {
  uint8_t  kind;
  uint8_t  used;
  uint8_t  fgA, bgA;
  float    scale;        // lado de la celda, en unidades de documento
  float    offX, offY;
  float    angle;        // radianes
  uint32_t fg, bg;
} FlexVecPattern;

// -------------------------------------------------------------
//  5) ELEMENTO, CAPA Y DOCUMENTO
// -------------------------------------------------------------
// Banderas de FlexVecElem.flags. Llevan prefijo EF_ y no E_ para que no
// puedan confundirse con los codigos de error de arriba: las dos series
// son enteros pequenos y una colision de nombres ahi la detecta el
// compilador solo si tiene suerte.
#define FLEXVEC_EF_SEL     0x01   // seleccionado
#define FLEXVEC_EF_HIDDEN  0x02
#define FLEXVEC_EF_LOCKED  0x04

typedef struct {
  int16_t  layer;        // capa duena. -1 = ranura libre
  int16_t  z;            // orden dentro de la capa (mayor = mas arriba)
  uint16_t first;        // primer nodo en el pool
  uint16_t count;        // numero de nodos
  uint8_t  kind;         // FLEXVEC_K_*
  uint8_t  flags;        // FLEXVEC_E_*
  uint8_t  cap, join;

  FlexVecPaint fill;
  FlexVecPaint stroke;
  float    strokeW;
  uint8_t  alpha;        // opacidad del objeto entero
  uint8_t  pad0[3];

  // APARIENCIA AMPLIADA (Fase 2). Un relleno y un trazo MAS, debajo de
  // los principales. No es la pila multinivel de Illustrator -- eso
  // exigiria un grafo de apariencia y efectos vivos, que estan
  // OMITIDOS (ver docs/FLEX-VECTOR-PRO.md) -- pero cubre lo que de
  // verdad se usa: un contorno doble, o un relleno de base bajo un
  // motivo. Orden de pintado: fill2, fill, stroke2, stroke.
  FlexVecPaint fill2;
  FlexVecPaint stroke2;
  float    strokeW2;

  float    m[6];         // matriz afin 2D: [a b c d e f] -> x' = a*x + c*y + e

  // Parametros de forma. Solo los usa el 'kind' correspondiente; para un
  // FLEXVEC_K_PATH no significan nada y valen 0.
  float    p0, p1;       // rect: radio x/y · poly/star: radio · text: tamano
  int16_t  n0, n1;       // poly/star: lados y puntas · text: offset y largo en el pool
  int16_t  ref;          // simbolo maestro (FLEXVEC_K_SYMBOL), -1 si no aplica
  int16_t  pad1;

  // Caja delimitadora en coordenadas de DOCUMENTO, ya transformada.
  // Se recalcula sola cuando bboxOk es 0: nunca se confia en un valor viejo.
  float    bx0, by0, bx1, by1;
  uint8_t  bboxOk;
  uint8_t  pad2[3];
} FlexVecElem;

typedef struct {
  char     name[FLEXVEC_NAME_MAX];
  uint8_t  used;
  uint8_t  visible;
  uint8_t  locked;
  uint8_t  pad;
} FlexVecLayer;

// Los bloques de memoria que el ANFITRION entrega. El nucleo no reserva
// ni libera nada: si un puntero llega a NULL, la operacion que lo
// necesitaba devuelve FLEXVEC_E_NOMEM y el sistema sigue vivo.
typedef struct {
  FlexVecNode* nodes;      // FLEXVEC_MAX_NODES
  uint8_t*     undo;       // FLEXVEC_UNDO_BYTES
  char*        text;       // FLEXVEC_TEXT_BYTES
} FlexVecArena;

typedef struct {
  char        name[FLEXVEC_NAME_MAX];
  float       artW, artH;               // mesa de trabajo, en unidades de documento
  FlexVecLayer layers[FLEXVEC_MAX_LAYERS];
  int         layerN;
  int         layerActive;
  FlexVecElem elems[FLEXVEC_MAX_ELEMS];
  int         elemN;                    // marca de agua alta (ranuras usadas o no)
  FlexVecGrad grads[FLEXVEC_MAX_GRADS];
  int         gradN;
  FlexVecPattern pats[FLEXVEC_MAX_PATTERNS];
  int         patN;

  FlexVecArena arena;
  uint16_t    nodeTop;                  // primer nodo libre del pool (asignacion por pila)
  uint16_t    nodeFree;                 // nodos recuperables por compactacion
  uint16_t    textTop;
  uint16_t    pad;

  // DIARIO DE DESHACER/REHACER.
  //  · 'undo' es una secuencia lineal de registros completos. Cada uno
  //    lleva su tamano al PRINCIPIO y al FINAL, asi que se puede
  //    recorrer hacia atras sin un indice aparte que pueda mentir.
  //  · 'undoCursor' es el byte donde acaba lo HECHO. Lo que queda por
  //    encima es lo REHACIBLE; una accion nueva lo trunca.
  //  · Cuando no cabe un registro nuevo se expulsan los mas VIEJOS
  //    (por delante). Los registros no guardan offsets absolutos, asi
  //    que desplazarlos es un memmove y nada mas.
  uint32_t    undoUsed;                 // bytes ocupados en total
  uint32_t    undoCursor;               // byte donde termina la parte "hecha"
  uint32_t    txnStart;                 // offset del registro en curso
  uint16_t    undoDone;                 // registros por debajo del cursor
  uint16_t    undoAll;                  // registros en total
  uint8_t     undoOpen;                 // hay una transaccion abierta
  uint8_t     dirty;                    // hay cambios sin guardar
  uint8_t     txnLayers;                // la transaccion toco la tabla de capas
  uint8_t     histLost;                 // hubo que tirar el historial (documento enorme)
  uint32_t    txnMask[(FLEXVEC_MAX_ELEMS + 31) / 32];   // elementos ya fotografiados
} FlexVecDoc;

// -------------------------------------------------------------
//  6) CICLO DE VIDA DEL DOCUMENTO
// -------------------------------------------------------------
// Entrega los bloques de PSRAM y deja el documento vacio y valido.
// Devuelve FLEXVEC_E_NOMEM si falta alguno de los tres.
int  flexVecAttach(FlexVecDoc* d, const FlexVecArena* a);
// Documento nuevo: una mesa de trabajo y las capas que se pidan (>=1).
int  flexVecNew(FlexVecDoc* d, float artW, float artH, int layers);
void flexVecClear(FlexVecDoc* d);
// Ocupacion, para la interfaz y para las pruebas de limites.
int  flexVecElemCount(const FlexVecDoc* d);
int  flexVecNodeUsed(const FlexVecDoc* d);
int  flexVecNodeFree(const FlexVecDoc* d);

// -------------------------------------------------------------
//  7) CAPAS
// -------------------------------------------------------------
int  flexVecLayerAdd(FlexVecDoc* d, const char* name);
int  flexVecLayerDelete(FlexVecDoc* d, int layer);
int  flexVecLayerMove(FlexVecDoc* d, int layer, int delta);   // reordena
int  flexVecLayerSetVisible(FlexVecDoc* d, int layer, int on);
int  flexVecLayerSetLocked(FlexVecDoc* d, int layer, int on);
int  flexVecLayerSetActive(FlexVecDoc* d, int layer);

// -------------------------------------------------------------
//  8) ELEMENTOS: creacion
//  ------------------------------------------------------------
//  Todas devuelven el indice del elemento (>=0) o un codigo de error
//  NEGATIVO (-FLEXVEC_E_*). Todas respetan el limite de nodos y el
//  bloqueo de la capa activa.
// -------------------------------------------------------------
int  flexVecAddPath(FlexVecDoc* d);                                   // trazado vacio (Pluma)
int  flexVecAddRect(FlexVecDoc* d, float x, float y, float w, float h, float r);
int  flexVecAddEllipse(FlexVecDoc* d, float cx, float cy, float rx, float ry);
int  flexVecAddLine(FlexVecDoc* d, float x0, float y0, float x1, float y1);
int  flexVecAddPolygon(FlexVecDoc* d, float cx, float cy, float r, int sides);
int  flexVecAddStar(FlexVecDoc* d, float cx, float cy, float rOut, float rIn, int points);
int  flexVecAddText(FlexVecDoc* d, float x, float y, const char* utf8, float size);
// El nucleo no tiene fuente: la unica del sistema es un atlas 4bpp sin
// contornos (FlexOS_Ultra_Font.h). El anfitrion MIDE el texto con esa
// fuente y corrige aqui la caja, que es lo que hacen la seleccion, la
// prueba de impacto y la caja delimitadora del SVG.
int  flexVecTextSetBox(FlexVecDoc* d, int elem, float w, float h);
const char* flexVecTextStr(const FlexVecDoc* d, int elem);
float flexVecTextSize(const FlexVecDoc* d, int elem);
int  flexVecDelete(FlexVecDoc* d, int elem);
int  flexVecDuplicate(FlexVecDoc* d, int elem);

// -------------------------------------------------------------
//  9) NODOS: la herramienta Pluma y la edicion directa
// -------------------------------------------------------------
// Anade un ancla al final del trazado. 'smooth' pone los dos tiradores
// alineados; con hx/hy se da el tirador de SALIDA (tap-arrastre).
int  flexVecNodeAppend(FlexVecDoc* d, int elem, float x, float y,
                       float hx, float hy, int smooth);
int  flexVecNodeInsert(FlexVecDoc* d, int elem, int at, float x, float y);
int  flexVecNodeDelete(FlexVecDoc* d, int elem, int idx);
int  flexVecNodeMove(FlexVecDoc* d, int elem, int idx, float x, float y);
// Mueve un tirador. which: 0 = entrada, 1 = salida. Si el nodo es SMOOTH,
// el otro tirador se refleja.
int  flexVecHandleMove(FlexVecDoc* d, int elem, int idx, int which, float x, float y);
int  flexVecNodeSetSmooth(FlexVecDoc* d, int elem, int idx, int smooth);
int  flexVecPathClose(FlexVecDoc* d, int elem, int closed);
// Divide un segmento por su punto medio parametrico (de Casteljau): el
// ancla nueva se inserta SIN cambiar la forma de la curva.
int  flexVecNodeSplit(FlexVecDoc* d, int elem, int seg, float t);

// -------------------------------------------------------------
// 10) APARIENCIA
// -------------------------------------------------------------
int  flexVecSetFill(FlexVecDoc* d, int elem, uint32_t rgb, uint8_t alpha);
int  flexVecSetNoFill(FlexVecDoc* d, int elem);
int  flexVecSetStroke(FlexVecDoc* d, int elem, uint32_t rgb, uint8_t alpha, float w);
int  flexVecSetNoStroke(FlexVecDoc* d, int elem);
int  flexVecSetAlpha(FlexVecDoc* d, int elem, uint8_t alpha);
int  flexVecSetCapJoin(FlexVecDoc* d, int elem, int cap, int join);

// -------------------------------------------------------------
// 11) TRANSFORMACIONES AFINES
//  ------------------------------------------------------------
//  Se COMPONEN en la matriz del objeto: la geometria no se reescribe.
//  Eso es lo que permite escalar mil veces sin perder precision ni
//  gastar un nodo mas.
// -------------------------------------------------------------
void flexVecMatIdentity(float* m);
void flexVecMatMul(const float* a, const float* b, float* out);  // out = a . b
int  flexVecMatInvert(const float* m, float* out);               // 0 si es singular
void flexVecMatApply(const float* m, float x, float y, float* ox, float* oy);
void flexVecMatApplyVec(const float* m, float x, float y, float* ox, float* oy);

int  flexVecTranslate(FlexVecDoc* d, int elem, float dx, float dy);
int  flexVecScale(FlexVecDoc* d, int elem, float cx, float cy, float sx, float sy);
int  flexVecRotate(FlexVecDoc* d, int elem, float cx, float cy, float rad);
int  flexVecMirror(FlexVecDoc* d, int elem, float cx, float cy, int horizontal);
int  flexVecShear(FlexVecDoc* d, int elem, float cx, float cy, float kx, float ky);
// Igual que las anteriores pero sobre TODA la seleccion, con el centro
// comun de la seleccion (o el propio de cada objeto si 'each').
int  flexVecTransformSel(FlexVecDoc* d, const float* m, int each);

// -------------------------------------------------------------
// 12) SELECCION, CAJAS Y PRUEBA DE IMPACTO
// -------------------------------------------------------------
void flexVecSelectNone(FlexVecDoc* d);
int  flexVecSelect(FlexVecDoc* d, int elem, int add);
int  flexVecSelectRect(FlexVecDoc* d, float x0, float y0, float x1, float y1, int add);
int  flexVecSelCount(const FlexVecDoc* d);
int  flexVecSelFirst(const FlexVecDoc* d);
int  flexVecBBox(FlexVecDoc* d, int elem, float* x0, float* y0, float* x1, float* y1);
int  flexVecSelBBox(FlexVecDoc* d, float* x0, float* y0, float* x1, float* y1);
// Devuelve el elemento mas alto bajo el punto, o -1. 'tol' es la holgura
// en unidades de documento (un dedo no es un raton: hace falta holgura).
int  flexVecHitTest(FlexVecDoc* d, float x, float y, float tol);
// Nodo mas cercano al punto dentro de 'tol'. Devuelve el indice de nodo
// o -1; 'part' sale a 0 = ancla, 1 = tirador de entrada, 2 = de salida.
int  flexVecHitNode(FlexVecDoc* d, int elem, float x, float y, float tol, int* part);

// -------------------------------------------------------------
// 13) ALINEAR Y DISTRIBUIR
// -------------------------------------------------------------
enum { FLEXVEC_AL_LEFT = 0, FLEXVEC_AL_HCENTER, FLEXVEC_AL_RIGHT,
       FLEXVEC_AL_TOP, FLEXVEC_AL_VCENTER, FLEXVEC_AL_BOTTOM };
int  flexVecAlign(FlexVecDoc* d, int how);
int  flexVecDistribute(FlexVecDoc* d, int horizontal);

// -------------------------------------------------------------
// 14) ORDEN Z
// -------------------------------------------------------------
int  flexVecRaise(FlexVecDoc* d, int elem);
int  flexVecLower(FlexVecDoc* d, int elem);
int  flexVecToFront(FlexVecDoc* d, int elem);
int  flexVecToBack(FlexVecDoc* d, int elem);
int  flexVecMoveToLayer(FlexVecDoc* d, int elem, int layer);
// Recorrido en orden de PINTADO (capa por capa, z ascendente). Devuelve
// el numero de elementos escritos en 'out'.
int  flexVecPaintOrder(const FlexVecDoc* d, int16_t* out, int maxn);

// -------------------------------------------------------------
// 15) APLANADO Y RASTERIZADO
//  ------------------------------------------------------------
//  El nucleo no conoce el framebuffer. Emite TRAMOS horizontales de
//  cobertura constante y quien pinta es el anfitrion.
// -------------------------------------------------------------
// Un tramo: fila 'y', de 'x0' a 'x1' INCLUSIVE, con cobertura 0..255.
typedef void (*FlexVecSpanCb)(int y, int x0, int x1, uint8_t cov, void* user);

// Vista: transformacion documento -> pixel. Es una afin (zoom + encuadre),
// mas la ventana de recorte en pixeles.
typedef struct {
  float m[6];               // documento -> pixel
  int   clipX0, clipY0, clipX1, clipY1;   // en pixeles, INCLUSIVE
} FlexVecView;

// Buffers de trabajo del rasterizador. Los entrega el anfitrion UNA vez,
// al abrir la app, y se reutilizan en cada cuadro: el bucle de render no
// reserva ni libera memoria nunca. El reparto exacto esta en
// flexVecRasterBytes() y en docs/FLEX-VECTOR-PRO.md.
//
// UN BORDE ES UN INDICE, NO CUATRO FLOTANTES. Las aristas del poligono
// aplanado ya estan en 'pts': guardar el indice del primer punto (4 B)
// en vez de copiar x0,y0,x1,y1 (16 B) ahorra 48 KB y evita que los dos
// sitios puedan discrepar.
typedef struct {
  float*   pts;        // 2 * FLEXVEC_MAX_FLATPTS: la polilinea, en PIXELES
  int32_t* subStart;   // FLEXVEC_MAX_SUBS: primer punto de cada subtrazado
  int32_t* subCount;   // FLEXVEC_MAX_SUBS: cuantos puntos tiene
  int32_t* subClosed;  // FLEXVEC_MAX_SUBS: 1 si el subtrazado es cerrado
  float*   pts2;       // 2 * FLEXVEC_MAX_FLATPTS: CONTORNO del trazo
  int32_t* sub2Start;  // FLEXVEC_MAX_SUBS
  int32_t* sub2Count;  // FLEXVEC_MAX_SUBS
  int32_t* edgeA;      // FLEXVEC_MAX_EDGES: punto inicial de cada arista
  int32_t* edgeB;      // FLEXVEC_MAX_EDGES: punto final
  int32_t* order;      // FLEXVEC_MAX_EDGES: aristas ordenadas por fila superior
  int32_t* active;     // FLEXVEC_MAX_EDGES: lista de aristas activas
  float*   xs;         // FLEXVEC_MAX_EDGES: cruces de la sub-linea en curso
  int8_t*  dirs;       // FLEXVEC_MAX_EDGES: sentido del cruce (regla de no-cero)
  int32_t* bucket;     // FLEXVEC_RASTER_ROWS + 2: cuentas de la ordenacion
  uint8_t* cov;        // covW bytes: acumulador de cobertura de la fila
  int      covW;
} FlexVecRaster;

// El anfitrion reserva UN bloque de PSRAM y esto lo reparte. Asi la app
// tiene una sola reserva que pedir, una sola que comprobar y una sola que
// liberar -- y el reparto no se puede descuadrar entre los dos sitios.
#define FLEXVEC_RASTER_ROWS  1024
size_t flexVecRasterBytes(int covW);
int    flexVecRasterInit(FlexVecRaster* r, void* block, size_t size, int covW);

// Aplana la geometria de un elemento a polilineas en coordenadas de
// PIXEL. Devuelve el numero de puntos, o un error negativo si no cabe.
int  flexVecFlatten(FlexVecDoc* d, int elem, const FlexVecView* v,
                    FlexVecRaster* r, int* subN);
// Rellena la polilinea ya aplanada. 'evenOdd' elige la regla.
int  flexVecFillFlat(FlexVecRaster* r, int subN, const FlexVecView* v,
                     int evenOdd, FlexVecSpanCb cb, void* user);
// Traza la polilinea ya aplanada con grosor 'w' en PIXELES.
int  flexVecStrokeFlat(FlexVecRaster* r, int subN, const FlexVecView* v,
                       float w, int cap, int join, FlexVecSpanCb cb, void* user);
// Atajo: aplana y pinta relleno y trazo de un elemento.
int  flexVecRenderElem(FlexVecDoc* d, int elem, const FlexVecView* v,
                       FlexVecRaster* r, FlexVecSpanCb fillCb, FlexVecSpanCb strokeCb,
                       void* user);
// Caja delimitadora del elemento EN PIXELES (para las regiones sucias).
int  flexVecElemPixBBox(FlexVecDoc* d, int elem, const FlexVecView* v,
                        int* x0, int* y0, int* x1, int* y1);

// -------------------------------------------------------------
// 16) GRADIENTES
// -------------------------------------------------------------
int  flexVecGradAdd(FlexVecDoc* d, int type);
int  flexVecGradSetAxis(FlexVecDoc* d, int g, float x0, float y0, float x1, float y1);
int  flexVecGradSetStop(FlexVecDoc* d, int g, int i, float t, uint32_t rgb, uint8_t alpha);
int  flexVecGradAddStop(FlexVecDoc* d, int g, float t, uint32_t rgb, uint8_t alpha);
int  flexVecGradDelStop(FlexVecDoc* d, int g, int i);
int  flexVecSetFillGrad(FlexVecDoc* d, int elem, int g);
// Parametro 0..1 del gradiente en un punto de DOCUMENTO. El anfitrion lo
// usa como indice de la LUT: un acceso a tabla por pixel, sin division.
float flexVecGradAt(const FlexVecGrad* g, float x, float y);
// Para un gradiente LINEAL, t es afin en x dentro de una fila: se calcula
// una vez por tramo y se avanza con una suma. Devuelve 0 si no es lineal.
int  flexVecGradRow(const FlexVecGrad* g, float x, float y, float* t0, float* dt);

// -------------------------------------------------------------
// 17) PATHFINDER (booleanas)
//  ------------------------------------------------------------
//  SIMPLIFICADO a proposito, y el porque esta en el .cpp: una booleana
//  exacta sobre Bezier es un problema de degeneraciones sin cota
//  superior de tiempo. Aqui se resuelve sobre una rejilla de cobertura
//  y se vuelve a trazar el contorno: es APROXIMADO (a la resolucion de
//  la rejilla) pero SIEMPRE termina, SIEMPRE en memoria acotada y
//  nunca puede colgar la interfaz.
// -------------------------------------------------------------
enum { FLEXVEC_B_UNITE = 0, FLEXVEC_B_MINUS_FRONT, FLEXVEC_B_INTERSECT,
       FLEXVEC_B_EXCLUDE, FLEXVEC_B_DIVIDE, FLEXVEC_B_TRIM };
// Buffers de la booleana: los entrega el anfitrion (no se reserva nada).
// Son ~128 KB y NO se tienen reservados todo el rato: una booleana es una
// accion explicita y puntual del usuario, asi que la app pide el bloque en
// ese momento y lo suelta al terminar. Si no hay memoria, la operacion se
// niega con un aviso -- que es mucho mejor que tener 128 KB de PSRAM
// retenidos durante toda la sesion por si acaso.
typedef struct {
  uint8_t* gridA;      // FLEXVEC_BOOL_GRID * FLEXVEC_BOOL_GRID
  uint8_t* gridB;      // idem: acumulador del resultado
  uint8_t* gridC;      // idem: hace falta para DIVIDIR, que necesita las
                       //   dos figuras a la vez ademas del acumulador
  float*   trace;      // 2 * FLEXVEC_MAX_FLATPTS
  uint8_t* mark;       // FLEXVEC_MAX_FLATPTS (marcas de la simplificacion)
} FlexVecBoolWork;
size_t flexVecBoolBytes(void);
int    flexVecBoolInit(FlexVecBoolWork* w, void* block, size_t size);
int    flexVecPathfinder(FlexVecDoc* d, int op, FlexVecRaster* r, FlexVecBoolWork* w);

// -------------------------------------------------------------
// 18) DESHACER / REHACER
//  ------------------------------------------------------------
//  Cada registro guarda el estado ANTES y el DESPUES. Deshacer aplica
//  el "antes"; rehacer aplica el "despues". Es la misma maquina en dos
//  sentidos, asi que no puede desincronizarse -- que es exactamente el
//  fallo que corrompe un documento.
// -------------------------------------------------------------
int  flexVecUndoBegin(FlexVecDoc* d, const char* label);
int  flexVecUndoCommit(FlexVecDoc* d);
void flexVecUndoAbort(FlexVecDoc* d);
int  flexVecCanUndo(const FlexVecDoc* d);
int  flexVecCanRedo(const FlexVecDoc* d);
int  flexVecUndo(FlexVecDoc* d);
int  flexVecRedo(FlexVecDoc* d);
uint32_t flexVecUndoBytes(const FlexVecDoc* d);
int  flexVecUndoSteps(const FlexVecDoc* d);
// true una sola vez: la ultima accion no cupo en el diario y el historial
// se reinicio. La interfaz lo dice; el documento NO se toca.
int  flexVecUndoHistoryLost(FlexVecDoc* d);
// Marca de cambios sin guardar (la usa el gancho dirty() del sistema).
int  flexVecDirty(const FlexVecDoc* d);
void flexVecSetSaved(FlexVecDoc* d);

// -------------------------------------------------------------
// 19) AJUSTE A CUADRICULA
// -------------------------------------------------------------
float flexVecSnap(float v, float grid);
int   flexVecSnapSel(FlexVecDoc* d, float grid);

// El typedef de la medida vive en la seccion H (texto de area) pero lo
// necesita ya la exportacion: se adelanta aqui.
typedef float (*FlexVecMeasureFn)(const char* utf8, int len, float size, void* user);

// -------------------------------------------------------------
// 20) EXPORTACION SVG
//  ------------------------------------------------------------
//  Texto plano: el formato de intercambio ideal para un embebido -- se
//  serializa sin reservar memoria, se abre en cualquier navegador y no
//  necesita un codificador binario que probar.
// -------------------------------------------------------------
// 'selOnly' exporta solo la seleccion. Devuelve BYTES escritos, o un
// error negativo (-FLEXVEC_E_OVERFLOW si el buffer se queda corto).
int  flexVecExportSVG(FlexVecDoc* d, char* out, size_t cap, int selOnly);
// SVG compacto (Fase 2): menos decimales, sin sangrado, atributos
// heredados en el grupo. Mismo contrato.
int  flexVecExportSVGCompact(FlexVecDoc* d, char* out, size_t cap, int selOnly);
// Igual, pero con la funcion de MEDIDA del anfitrion. Solo cambia una
// cosa, y es la que importa: el texto de area se exporta repartido en
// las MISMAS lineas que se ven en la pantalla, en <tspan>. Sin la
// medida, un texto de area sale como una sola linea -- SVG valido, pero
// no lo que el usuario compuso.
int  flexVecExportSVGEx(FlexVecDoc* d, char* out, size_t cap, int selOnly,
                        int compact, FlexVecMeasureFn measure, void* mUser);

// -------------------------------------------------------------
// 21) SERIALIZACION NATIVA  (.fxv, para la sesion y para el disco)
// -------------------------------------------------------------
#define FLEXVEC_MAGIC   0x31565846u   /* "FXV1" */
#define FLEXVEC_VERSION 2
int  flexVecSerialize(const FlexVecDoc* d, uint8_t* out, size_t cap);
int  flexVecDeserialize(FlexVecDoc* d, const uint8_t* in, size_t len);
size_t flexVecSerializeSize(const FlexVecDoc* d);

// #############################################################
//  FASE 2  ·  EXPANSION PROFESIONAL
//  ------------------------------------------------------------
//  Todo lo de aqui comparte tres reglas con la Fase 1: no reserva
//  memoria, tiene un limite duro y falla con un motivo legible.
//
//  Y una cuarta, que es la que hace que estas funciones no cuesten
//  nada por cuadro: LO CARO SE HORNEA. Fusion, repeticion y pincel
//  producen GEOMETRIA de una vez, cuando el usuario los aplica. No hay
//  un grafo que re-evaluar en cada repintado -- que es exactamente lo
//  que un editor de escritorio puede permitirse y un MCU sin GPU no.
// #############################################################

// -------------------------------------------------------------
//  A) APARIENCIA AMPLIADA
// -------------------------------------------------------------
int flexVecSetFill2(FlexVecDoc* d, int elem, uint32_t rgb, uint8_t alpha);
int flexVecSetStroke2(FlexVecDoc* d, int elem, uint32_t rgb, uint8_t alpha, float w);
int flexVecClearExtra(FlexVecDoc* d, int elem);
int flexVecHasExtra(const FlexVecDoc* d, int elem);

// -------------------------------------------------------------
//  B) MOTIVOS
// -------------------------------------------------------------
int flexVecPatternAdd(FlexVecDoc* d, int kind);
int flexVecPatternSet(FlexVecDoc* d, int p, int kind, float scale,
                      float offX, float offY, float angle);
int flexVecPatternColors(FlexVecDoc* d, int p, uint32_t fg, uint8_t fgA,
                         uint32_t bg, uint8_t bgA);
int flexVecSetFillPattern(FlexVecDoc* d, int elem, int p);
// Color del motivo en un punto de DOCUMENTO. Devuelve RGB888 y escribe
// la opacidad. Es una funcion pura: la llama el anfitrion por pixel.
uint32_t flexVecPatternAt(const FlexVecPattern* p, float x, float y, uint8_t* alphaOut);

// -------------------------------------------------------------
//  C) SIMBOLOS  ·  instancias VINCULADAS
//  ------------------------------------------------------------
//  Una instancia no copia la geometria: apunta al maestro. Editar el
//  maestro cambia todas las instancias, que es justo el valor de un
//  simbolo. Cada instancia tiene su propia matriz y su propia
//  apariencia -- ese es el unico "override", y es deliberado: la pila
//  de sustituciones por instancia de Illustrator esta OMITIDA.
// -------------------------------------------------------------
int flexVecSymbolInstance(FlexVecDoc* d, int master, float dx, float dy);
// Indice del maestro si 'elem' es una instancia; -1 si no lo es o si el
// maestro ya no existe.
int flexVecSymbolMaster(const FlexVecDoc* d, int elem);
// Cuantas instancias tiene un maestro.
int flexVecSymbolCount(const FlexVecDoc* d, int master);

// -------------------------------------------------------------
//  D) FUSION (blend)
//  ------------------------------------------------------------
//  Interpola forma y color entre dos objetos con un numero ACOTADO de
//  pasos. Las dos siluetas se remuestrean a un mismo numero de puntos,
//  asi que el resultado es poligonal: es la version SIMPLIFICADA que
//  documenta docs/FLEX-VECTOR-PRO.md.
// -------------------------------------------------------------
int flexVecBlend(FlexVecDoc* d, int a, int b, int steps, FlexVecRaster* r);

// -------------------------------------------------------------
//  E) REPETIR
// -------------------------------------------------------------
enum { FLEXVEC_REP_GRID = 0, FLEXVEC_REP_MIRROR };
int flexVecRepeat(FlexVecDoc* d, int elem, int mode, int cols, int rows,
                  float dx, float dy);

// -------------------------------------------------------------
//  F) PINCEL DE DISPERSION / DE MOTIVO
//  ------------------------------------------------------------
//  Estampa copias de 'stamp' a lo largo de 'path', cada 'spacing'
//  unidades. Se HORNEA a un solo objeto con una copia por subtrazado:
//  asi cien estampas cuestan un objeto, no cien.
// -------------------------------------------------------------
int flexVecBrush(FlexVecDoc* d, int path, int stamp, float spacing,
                 float scale, int rotate, FlexVecRaster* r);

// -------------------------------------------------------------
//  G) MASCARA DE RECORTE, de forma arbitraria
//  ------------------------------------------------------------
//  Recorta la seleccion con el objeto de MAS ARRIBA. Usa el mismo
//  motor de rejilla que el Pathfinder, con sus mismos limites, y es
//  DESTRUCTIVA: la geometria recortada sustituye a la original. La
//  mascara no destructiva de Illustrator necesitaria componer dos
//  coberturas por fila en el mismo repintado; ver el apartado de
//  mascaras en docs/FLEX-VECTOR-PRO.md.
// -------------------------------------------------------------
int flexVecClipWithTop(FlexVecDoc* d, FlexVecRaster* r, FlexVecBoolWork* w);

// -------------------------------------------------------------
//  H) TEXTO DE AREA
//  ------------------------------------------------------------
//  El nucleo NO tiene fuente: el anfitrion presta una funcion de
//  medida. Asi el reparto en lineas se prueba en el PC con una medida
//  de mentira, y en la placa se mide con la fuente de verdad.
// -------------------------------------------------------------
enum { FLEXVEC_TA_LEFT = 0, FLEXVEC_TA_CENTER, FLEXVEC_TA_RIGHT };
typedef void  (*FlexVecLineCb)(const char* utf8, int len, float x, float y,
                               float w, void* user);
int flexVecTextSetArea(FlexVecDoc* d, int elem, float w, float h, int align);
int flexVecTextIsArea(const FlexVecDoc* d, int elem);
// Reparte el texto en lineas dentro de la caja y las entrega por
// callback. Devuelve el numero de lineas, o un error negativo.
int flexVecTextLayout(FlexVecDoc* d, int elem, float lineH,
                      FlexVecMeasureFn measure, void* mUser,
                      FlexVecLineCb cb, void* cbUser);

// -------------------------------------------------------------
//  I) EXPORTACION PNG
//  ------------------------------------------------------------
//  Escritor por FILAS: no hace falta tener la imagen entera en RGB888
//  en memoria (1,1 MB para 480x800). El anfitrion ya tiene el cuadro
//  rasterizado en su cache RGB565 y va convirtiendo fila a fila.
//
//  Deflate de verdad (LZ77 + Huffman fijo) con ventana corta: un
//  dibujo vectorial son grandes zonas planas y comprime muchisimo. Con
//  bloques sin comprimir el archivo seria de mas de un mega, que en
//  16 MB de NOR Flash no es aceptable.
// -------------------------------------------------------------
#define FLEXVEC_PNG_WINDOW  8192
#define FLEXVEC_PNG_HASH    2048
typedef struct {
  uint8_t* out; size_t cap, len;
  int      w, h, rows;
  uint32_t adler;               // checksum zlib
  uint32_t crc;                 // CRC del trozo IDAT en curso
  size_t   idatStart;           // donde empieza la longitud del IDAT
  uint32_t bitBuf; int bitCnt;  // acumulador de bits del deflate
  uint8_t  win[FLEXVEC_PNG_WINDOW];
  int      winPos;              // bytes ya metidos en la ventana (total)
  int32_t  head[FLEXVEC_PNG_HASH];
  int32_t  prev[FLEXVEC_PNG_WINDOW];
  int      over;                // se quedo sin sitio
} FlexVecPng;
// Cota superior del tamano de salida. Si el buffer es al menos esto,
// el PNG cabe siempre, comprima o no.
size_t flexVecPngMaxBytes(int w, int h);
int flexVecPngBegin(FlexVecPng* p, int w, int h, uint8_t* out, size_t cap);
int flexVecPngRow(FlexVecPng* p, const uint8_t* rgb);   // w * 3 bytes
int flexVecPngEnd(FlexVecPng* p);                       // bytes totales, o error negativo

#ifdef __cplusplus
}
#endif
