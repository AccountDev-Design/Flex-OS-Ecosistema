// #############################################################
//  FLEX VECTOR PRO · NUCLEO VECTORIAL  ·  implementacion
//  ------------------------------------------------------------
//  Ver FlexOS_Vector.h para el contrato y docs/FLEX-VECTOR-PRO.md
//  para el reparto de memoria y la clasificacion de funciones.
//
//  TRES REGLAS QUE ATRAVIESAN TODO ESTE ARCHIVO
//  ------------------------------------------------------------
//  1. NO SE RESERVA MEMORIA. Ni una sola llamada a malloc/new/free.
//     Todos los bloques llegan de fuera (FlexVecArena, FlexVecRaster,
//     FlexVecBoolWork). Si falta uno, la operacion devuelve
//     FLEXVEC_E_NOMEM y el sistema sigue en pie.
//  2. TODA OPERACION ES ATOMICA. O cabe entera y se aplica, o el
//     documento queda EXACTAMENTE como estaba. No hay estados a
//     medias: un documento a medias es un documento corrupto.
//  3. TODO BUCLE TERMINA. No hay ni un bucle cuyo numero de vueltas
//     dependa de datos del usuario sin una cota superior fija. En un
//     MCU que ademas alimenta un watchdog, un bucle sin cota no es un
//     fallo de rendimiento: es un reinicio.
// #############################################################
#include "FlexOS_Vector.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

// -------------------------------------------------------------
//  Utilidades locales
// -------------------------------------------------------------
static inline float fvMin(float a, float b){ return a < b ? a : b; }
static inline float fvMax(float a, float b){ return a > b ? a : b; }
static inline int   fvIMin(int a, int b){ return a < b ? a : b; }
static inline int   fvIMax(int a, int b){ return a > b ? a : b; }
static inline float fvAbs(float v){ return v < 0 ? -v : v; }
static inline float fvClampF(float v, float lo, float hi){ return v < lo ? lo : (v > hi ? hi : v); }

// Un valor no finito (NaN, inf) que entre en la geometria envenena todo
// lo que toca despues: cajas delimitadoras absurdas, listas de bordes que
// no cierran y bucles de rasterizado que no acaban. Se filtra AQUI, en el
// unico sitio por donde entran las coordenadas.
static inline int fvFinite(float v){ return (v == v) && (v <= 3.0e38f) && (v >= -3.0e38f); }
static inline float fvSafe(float v){ return fvFinite(v) ? v : 0.0f; }

const char* flexVecErrText(int err){
  if(err < 0) err = -err;
  switch(err){
    case FLEXVEC_OK:            return "Correcto";
    case FLEXVEC_E_FULL_ELEMS:  return "No caben mas objetos";
    case FLEXVEC_E_FULL_NODES:  return "No caben mas nodos";
    case FLEXVEC_E_FULL_LAYERS: return "No caben mas capas";
    case FLEXVEC_E_FULL_TEXT:   return "No cabe mas texto";
    case FLEXVEC_E_FULL_GRADS:  return "No caben mas gradientes";
    case FLEXVEC_E_TOO_COMPLEX: return "Figura demasiado compleja";
    case FLEXVEC_E_LOCKED:      return "La capa esta bloqueada";
    case FLEXVEC_E_BADARG:      return "Operacion no valida";
    case FLEXVEC_E_NOMEM:       return "Memoria insuficiente";
    case FLEXVEC_E_OVERFLOW:    return "El documento no cabe en el archivo";
    case FLEXVEC_E_EMPTY:       return "No hay nada seleccionado";
  }
  return "Error desconocido";
}

// -------------------------------------------------------------
//  MATRICES AFINES 2D   [a b c d e f]
//  ------------------------------------------------------------
//   x' = a*x + c*y + e
//   y' = b*x + d*y + f
//  Es el mismo orden que usa el atributo transform="matrix(...)" de
//  SVG, asi que exportar una matriz es copiarla: no hay conversion
//  donde equivocarse.
// -------------------------------------------------------------
void flexVecMatIdentity(float* m){
  m[0] = 1; m[1] = 0; m[2] = 0; m[3] = 1; m[4] = 0; m[5] = 0;
}
void flexVecMatMul(const float* a, const float* b, float* out){
  // out = a . b  (primero b, despues a)
  float r[6];
  r[0] = a[0] * b[0] + a[2] * b[1];
  r[1] = a[1] * b[0] + a[3] * b[1];
  r[2] = a[0] * b[2] + a[2] * b[3];
  r[3] = a[1] * b[2] + a[3] * b[3];
  r[4] = a[0] * b[4] + a[2] * b[5] + a[4];
  r[5] = a[1] * b[4] + a[3] * b[5] + a[5];
  memcpy(out, r, sizeof(r));
}
int flexVecMatInvert(const float* m, float* out){
  float det = m[0] * m[3] - m[1] * m[2];
  if(fvAbs(det) < 1e-9f) return 0;              // singular: no se invierte
  float id = 1.0f / det;
  float r[6];
  r[0] =  m[3] * id;
  r[1] = -m[1] * id;
  r[2] = -m[2] * id;
  r[3] =  m[0] * id;
  r[4] = (m[2] * m[5] - m[3] * m[4]) * id;
  r[5] = (m[1] * m[4] - m[0] * m[5]) * id;
  memcpy(out, r, sizeof(r));
  return 1;
}
void flexVecMatApply(const float* m, float x, float y, float* ox, float* oy){
  *ox = m[0] * x + m[2] * y + m[4];
  *oy = m[1] * x + m[3] * y + m[5];
}
void flexVecMatApplyVec(const float* m, float x, float y, float* ox, float* oy){
  *ox = m[0] * x + m[2] * y;      // vector: sin traslacion
  *oy = m[1] * x + m[3] * y;
}

// -------------------------------------------------------------
//  POOL DE NODOS
//  ------------------------------------------------------------
//  Asignacion POR PILA: un elemento posee un rango contiguo
//  [first, first+count). Es lo que permite recorrer una geometria sin
//  saltos de puntero, aplanarla de una pasada y no fragmentar la PSRAM
//  con una reserva por nodo.
//
//  Borrar un elemento que no esta en la cima deja un hueco. No se
//  intenta reutilizarlo con una lista libre (eso SI fragmenta y ademas
//  hace el recorrido impredecible): se contabiliza en 'nodeFree' y se
//  recupera COMPACTANDO -- una sola pasada que deja el pool otra vez
//  contiguo. La compactacion se dispara sola cuando una reserva no
//  cabe, no antes: mientras haya sitio no se paga nada.
// -------------------------------------------------------------
static int fvElemValid(const FlexVecDoc* d, int e){
  return d && e >= 0 && e < FLEXVEC_MAX_ELEMS && d->elems[e].layer >= 0;
}
static int fvLayerValid(const FlexVecDoc* d, int l){
  return d && l >= 0 && l < FLEXVEC_MAX_LAYERS && d->layers[l].used;
}

// Compacta el pool: mueve cada rango a continuacion del anterior, en
// orden de 'first' creciente, y actualiza los indices de los elementos.
// Recorre a lo sumo FLEXVEC_MAX_ELEMS elementos: cota fija.
static void fvCompact(FlexVecDoc* d){
  if(!d->arena.nodes) return;
  int16_t order[FLEXVEC_MAX_ELEMS];
  int n = 0;
  for(int i = 0; i < FLEXVEC_MAX_ELEMS; i++)
    if(d->elems[i].layer >= 0 && d->elems[i].count > 0) order[n++] = (int16_t)i;
  // Ordenacion por insercion sobre 'first'. n <= 256 y la lista ya suele
  // venir casi ordenada: es lo mas barato aqui y no reserva nada.
  for(int i = 1; i < n; i++){
    int16_t v = order[i];
    uint16_t key = d->elems[v].first;
    int j = i - 1;
    while(j >= 0 && d->elems[order[j]].first > key){ order[j + 1] = order[j]; j--; }
    order[j + 1] = v;
  }
  uint16_t top = 0;
  for(int i = 0; i < n; i++){
    FlexVecElem* e = &d->elems[order[i]];
    if(e->first != top)
      memmove(d->arena.nodes + top, d->arena.nodes + e->first,
              (size_t)e->count * sizeof(FlexVecNode));
    e->first = top;
    top = (uint16_t)(top + e->count);
  }
  d->nodeTop  = top;
  d->nodeFree = 0;
}

// Reserva 'n' nodos en la cima. Compacta si hace falta. Devuelve el
// indice del primero, o -1 si de verdad no cabe.
static int fvNodeAlloc(FlexVecDoc* d, int n){
  if(!d->arena.nodes || n <= 0) return -1;
  if((int)d->nodeTop + n > FLEXVEC_MAX_NODES){
    if(d->nodeFree == 0) return -1;
    fvCompact(d);
    if((int)d->nodeTop + n > FLEXVEC_MAX_NODES) return -1;
  }
  int at = (int)d->nodeTop;
  d->nodeTop = (uint16_t)(d->nodeTop + n);
  memset(d->arena.nodes + at, 0, (size_t)n * sizeof(FlexVecNode));
  return at;
}

// Asegura que el elemento tenga sitio para 'extra' nodos MAS. Si su rango
// esta en la cima, crece en el sitio; si no, se reubica a la cima (una
// copia de como mucho 256 nodos, y solo cuando de verdad hace falta).
static int fvNodeGrow(FlexVecDoc* d, int e, int extra){
  FlexVecElem* el = &d->elems[e];
  if(extra <= 0) return FLEXVEC_OK;
  if((int)el->count + extra > FLEXVEC_MAX_NODES_PATH) return FLEXVEC_E_TOO_COMPLEX;
  int atTop = ((int)el->first + (int)el->count == (int)d->nodeTop);
  if(atTop){
    if((int)d->nodeTop + extra > FLEXVEC_MAX_NODES){
      if(d->nodeFree == 0) return FLEXVEC_E_FULL_NODES;
      fvCompact(d);
      atTop = ((int)el->first + (int)el->count == (int)d->nodeTop);
      if(!atTop || (int)d->nodeTop + extra > FLEXVEC_MAX_NODES) return FLEXVEC_E_FULL_NODES;
    }
    memset(d->arena.nodes + d->nodeTop, 0, (size_t)extra * sizeof(FlexVecNode));
    d->nodeTop = (uint16_t)(d->nodeTop + extra);
    return FLEXVEC_OK;
  }
  int want = (int)el->count + extra;
  int at = fvNodeAlloc(d, want);
  if(at < 0) return FLEXVEC_E_FULL_NODES;
  memcpy(d->arena.nodes + at, d->arena.nodes + el->first,
         (size_t)el->count * sizeof(FlexVecNode));
  d->nodeFree = (uint16_t)(d->nodeFree + el->count);
  el->first = (uint16_t)at;
  return FLEXVEC_OK;
}

static void fvNodeRelease(FlexVecDoc* d, int e){
  FlexVecElem* el = &d->elems[e];
  if(el->count == 0) return;
  if((int)el->first + (int)el->count == (int)d->nodeTop) d->nodeTop = el->first;
  else d->nodeFree = (uint16_t)(d->nodeFree + el->count);
  el->count = 0;
}

// -------------------------------------------------------------
//  CICLO DE VIDA DEL DOCUMENTO
// -------------------------------------------------------------
int flexVecAttach(FlexVecDoc* d, const FlexVecArena* a){
  if(!d || !a) return FLEXVEC_E_BADARG;
  memset(d, 0, sizeof(*d));
  d->arena = *a;
  if(!a->nodes || !a->undo || !a->text) return FLEXVEC_E_NOMEM;
  return FLEXVEC_OK;
}

void flexVecClear(FlexVecDoc* d){
  if(!d) return;
  FlexVecArena a = d->arena;
  memset(d, 0, sizeof(*d));
  d->arena = a;
}

int flexVecNew(FlexVecDoc* d, float artW, float artH, int layers){
  if(!d) return FLEXVEC_E_BADARG;
  if(!d->arena.nodes || !d->arena.undo || !d->arena.text) return FLEXVEC_E_NOMEM;
  if(!fvFinite(artW) || !fvFinite(artH) || artW <= 0 || artH <= 0) return FLEXVEC_E_BADARG;
  flexVecClear(d);
  d->artW = artW; d->artH = artH;
  snprintf(d->name, sizeof(d->name), "Documento");
  if(layers < 1) layers = 1;
  if(layers > FLEXVEC_MAX_LAYERS) layers = FLEXVEC_MAX_LAYERS;
  for(int i = 0; i < layers; i++){
    d->layers[i].used = 1;
    d->layers[i].visible = 1;
    d->layers[i].locked = 0;
    snprintf(d->layers[i].name, FLEXVEC_NAME_MAX, "Capa %d", i + 1);
  }
  d->layerN = layers;
  d->layerActive = 0;
  for(int i = 0; i < FLEXVEC_MAX_ELEMS; i++) d->elems[i].layer = -1;
  return FLEXVEC_OK;
}

int flexVecElemCount(const FlexVecDoc* d){
  if(!d) return 0;
  int n = 0;
  for(int i = 0; i < FLEXVEC_MAX_ELEMS; i++) if(d->elems[i].layer >= 0) n++;
  return n;
}
int flexVecNodeUsed(const FlexVecDoc* d){ return d ? (int)d->nodeTop - (int)d->nodeFree : 0; }
int flexVecNodeFree(const FlexVecDoc* d){
  if(!d) return 0;
  return FLEXVEC_MAX_NODES - ((int)d->nodeTop - (int)d->nodeFree);
}

// #############################################################
//  DIARIO DE DESHACER / REHACER
//  ------------------------------------------------------------
//  FORMATO DE UN REGISTRO (todo alineado a 4, todo por memcpy):
//
//    [uint32 total][uint16 nent][uint16 flags][char label[16]]
//    SECCION "ANTES":   nent entradas
//    SECCION "DESPUES": nent entradas, EN EL MISMO ORDEN
//    [uint32 total]                       <- pie, para recorrer hacia atras
//
//  Una entrada:
//    [FvEnt cabecera][carga util]
//      · FV_ENT_ELEM   -> FlexVecElem + n nodos
//      · FV_ENT_LAYERS -> la tabla de capas entera (512 B, es barata)
//      · FV_ENT_META   -> mesa de trabajo y nombre
//
//  POR QUE "ANTES" Y "DESPUES" EN EL MISMO REGISTRO. Porque asi
//  deshacer y rehacer son LA MISMA rutina recorrida en dos sentidos.
//  La alternativa clasica -- guardar solo el "antes" y reconstruir el
//  "despues" al deshacer -- tiene dos caminos distintos que hay que
//  mantener de acuerdo, y en cuanto se separan el documento se corrompe
//  en silencio. Aqui no hay dos caminos.
//
//  CUANDO NO CABE. Se expulsan los registros MAS VIEJOS (por delante):
//  no guardan offsets absolutos, asi que moverlos es un memmove. Si ni
//  siquiera vaciando el diario cabe la accion en curso, el historial se
//  reinicia ENTERO y se enciende 'histLost' para que la interfaz lo
//  diga. Lo que NO se hace jamas es dejar un historial a medias: un
//  historial incoherente corrompe el documento al deshacer, y eso es
//  peor que quedarse sin deshacer.
// #############################################################
#define FV_ENT_ELEM    1
#define FV_ENT_LAYERS  2
#define FV_ENT_META    3

typedef struct { uint8_t kind; uint8_t pad; uint16_t elem; uint16_t n; uint16_t pad2; } FvEnt;
typedef struct { FlexVecLayer layers[FLEXVEC_MAX_LAYERS]; int32_t layerN; int32_t layerActive; } FvLayersBlob;
typedef struct { float artW, artH; char name[FLEXVEC_NAME_MAX]; } FvMetaBlob;

#define FV_REC_HDR  (4 + 2 + 2 + 16)
#define FV_REC_FTR  4

static inline void fvPut(uint8_t* p, const void* src, size_t n){ memcpy(p, src, n); }

// Tamano del registro que empieza en 'off'.
static uint32_t fvRecTotal(const FlexVecDoc* d, uint32_t off){
  uint32_t t = 0;
  memcpy(&t, d->arena.undo + off, 4);
  return t;
}

// Expulsa el registro mas viejo. Devuelve 0 si ya no queda ninguno que
// se pueda expulsar (todo lo que hay es la transaccion en curso).
static int fvUndoDropOldest(FlexVecDoc* d){
  if(d->undoUsed == 0) return 0;
  uint32_t first = fvRecTotal(d, 0);
  if(first == 0 || first > d->undoUsed) return 0;
  // La transaccion en curso empieza en txnStart; si el registro mas viejo
  // ES la transaccion en curso no se toca.
  if(d->undoOpen && d->txnStart == 0) return 0;
  memmove(d->arena.undo, d->arena.undo + first, d->undoUsed - first);
  d->undoUsed -= first;
  d->undoCursor = (d->undoCursor > first) ? d->undoCursor - first : 0;
  if(d->txnStart > first) d->txnStart -= first; else d->txnStart = 0;
  if(d->undoAll)  d->undoAll--;
  if(d->undoDone) d->undoDone--;
  return 1;
}

// Deja sitio para 'need' bytes mas al final. 0 = imposible.
static int fvUndoEnsure(FlexVecDoc* d, uint32_t need){
  if(!d->arena.undo) return 0;
  if(need > FLEXVEC_UNDO_BYTES) return 0;
  while(d->undoUsed + need > FLEXVEC_UNDO_BYTES){
    if(!fvUndoDropOldest(d)) return 0;
  }
  return 1;
}

// Tira TODO el historial. Es una operacion sobre el DIARIO, nunca sobre
// el documento: los objetos del usuario no se tocan.
static void fvUndoReset(FlexVecDoc* d){
  d->undoUsed = d->undoCursor = d->txnStart = 0;
  d->undoDone = d->undoAll = 0;
  d->undoOpen = 0; d->txnLayers = 0;
  memset(d->txnMask, 0, sizeof(d->txnMask));
}

static inline int fvTxnSeen(const FlexVecDoc* d, int e){
  return (d->txnMask[e >> 5] >> (e & 31)) & 1u;
}
static inline void fvTxnMark(FlexVecDoc* d, int e){
  d->txnMask[e >> 5] |= (uint32_t)(1u << (e & 31));
}

// Escribe una entrada al final del diario. 'nodes' puede ser NULL.
static int fvUndoWriteEnt(FlexVecDoc* d, uint8_t kind, int elem,
                          const void* blob, size_t blobLen,
                          const FlexVecNode* nodes, int n){
  uint32_t need = (uint32_t)(sizeof(FvEnt) + blobLen + (size_t)n * sizeof(FlexVecNode));
  if(!fvUndoEnsure(d, need)) return 0;
  FvEnt ent; memset(&ent, 0, sizeof(ent));
  ent.kind = kind; ent.elem = (uint16_t)(elem < 0 ? 0 : elem); ent.n = (uint16_t)n;
  uint8_t* p = d->arena.undo + d->undoUsed;
  fvPut(p, &ent, sizeof(ent)); p += sizeof(ent);
  if(blobLen){ fvPut(p, blob, blobLen); p += blobLen; }
  if(n > 0 && nodes) fvPut(p, nodes, (size_t)n * sizeof(FlexVecNode));
  d->undoUsed += need;
  return 1;
}

// Fotografia el estado ACTUAL de un elemento en la seccion "antes", una
// sola vez por transaccion. Un elemento que aun no existe se fotografia
// con layer = -1, que es exactamente "no estaba".
static void fvUndoTouch(FlexVecDoc* d, int e){
  if(!d->undoOpen || d->histLost) return;
  if(e < 0 || e >= FLEXVEC_MAX_ELEMS) return;
  if(fvTxnSeen(d, e)) return;
  fvTxnMark(d, e);
  const FlexVecElem* el = &d->elems[e];
  const FlexVecNode* nd = (el->layer >= 0 && el->count > 0 && d->arena.nodes)
                          ? d->arena.nodes + el->first : NULL;
  int n = (el->layer >= 0) ? (int)el->count : 0;
  if(!fvUndoWriteEnt(d, FV_ENT_ELEM, e, el, sizeof(*el), nd, n)) d->histLost = 1;
}

static void fvUndoTouchLayers(FlexVecDoc* d){
  if(!d->undoOpen || d->histLost || d->txnLayers) return;
  d->txnLayers = 1;
  FvLayersBlob b;
  memcpy(b.layers, d->layers, sizeof(b.layers));
  b.layerN = d->layerN; b.layerActive = d->layerActive;
  if(!fvUndoWriteEnt(d, FV_ENT_LAYERS, 0, &b, sizeof(b), NULL, 0)) d->histLost = 1;
}

int flexVecUndoBegin(FlexVecDoc* d, const char* label){
  if(!d || !d->arena.undo) return FLEXVEC_E_NOMEM;
  if(d->undoOpen) return FLEXVEC_E_BADARG;      // una transaccion cada vez
  // Una accion nueva invalida lo REHACIBLE: se trunca.
  d->undoUsed = d->undoCursor;
  d->undoAll  = d->undoDone;
  d->histLost = 0;
  memset(d->txnMask, 0, sizeof(d->txnMask));
  d->txnLayers = 0;
  if(!fvUndoEnsure(d, FV_REC_HDR)){ fvUndoReset(d); d->histLost = 1; }
  d->txnStart = d->undoUsed;
  uint8_t* p = d->arena.undo + d->undoUsed;
  memset(p, 0, FV_REC_HDR);
  if(label) snprintf((char*)p + 8, 16, "%s", label);
  d->undoUsed += FV_REC_HDR;
  d->undoOpen = 1;
  return FLEXVEC_OK;
}

void flexVecUndoAbort(FlexVecDoc* d){
  if(!d || !d->undoOpen) return;
  d->undoUsed = d->txnStart;
  d->undoOpen = 0; d->txnLayers = 0;
  memset(d->txnMask, 0, sizeof(d->txnMask));
}

int flexVecUndoCommit(FlexVecDoc* d){
  if(!d || !d->undoOpen) return FLEXVEC_E_BADARG;
  d->undoOpen = 0;
  d->dirty = 1;
  if(d->histLost){                       // no cupo: historial limpio y honesto
    fvUndoReset(d);
    d->histLost = 1;
    return FLEXVEC_OK;
  }
  uint32_t start = d->txnStart;
  uint32_t beforeEnd = d->undoUsed;
  if(beforeEnd <= start + FV_REC_HDR){   // nadie toco nada: no hay registro
    d->undoUsed = start;
    return FLEXVEC_OK;
  }
  // Recorre la seccion "antes" y escribe la "despues" con el estado de AHORA.
  // Se cuenta primero cuanto ocupara: si no cabe, se tira el registro entero
  // en vez de dejar medio escrito uno que luego nadie sabria leer.
  uint32_t off = start + FV_REC_HDR;
  uint16_t nent = 0;
  uint32_t needAfter = 0;
  while(off + sizeof(FvEnt) <= beforeEnd){
    FvEnt ent; memcpy(&ent, d->arena.undo + off, sizeof(ent));
    size_t blob = (ent.kind == FV_ENT_ELEM)   ? sizeof(FlexVecElem)
                : (ent.kind == FV_ENT_LAYERS) ? sizeof(FvLayersBlob)
                : sizeof(FvMetaBlob);
    off += (uint32_t)(sizeof(FvEnt) + blob + (size_t)ent.n * sizeof(FlexVecNode));
    if(ent.kind == FV_ENT_ELEM){
      int e = ent.elem;
      int n = (e < FLEXVEC_MAX_ELEMS && d->elems[e].layer >= 0) ? (int)d->elems[e].count : 0;
      needAfter += (uint32_t)(sizeof(FvEnt) + sizeof(FlexVecElem) + (size_t)n * sizeof(FlexVecNode));
    } else if(ent.kind == FV_ENT_LAYERS){
      needAfter += (uint32_t)(sizeof(FvEnt) + sizeof(FvLayersBlob));
    } else {
      needAfter += (uint32_t)(sizeof(FvEnt) + sizeof(FvMetaBlob));
    }
    nent++;
    if(nent > FLEXVEC_MAX_ELEMS + 2) break;    // cota dura: nunca un bucle abierto
  }
  if(d->undoUsed + needAfter + FV_REC_FTR > FLEXVEC_UNDO_BYTES){
    // Expulsar por delante moveria 'start' bajo nuestros pies; es mas
    // sencillo y mas seguro reiniciar el historial que reindexar a mano.
    fvUndoReset(d);
    d->histLost = 1;
    return FLEXVEC_OK;
  }
  off = start + FV_REC_HDR;
  while(off + sizeof(FvEnt) <= beforeEnd){
    FvEnt ent; memcpy(&ent, d->arena.undo + off, sizeof(ent));
    size_t blob = (ent.kind == FV_ENT_ELEM)   ? sizeof(FlexVecElem)
                : (ent.kind == FV_ENT_LAYERS) ? sizeof(FvLayersBlob)
                : sizeof(FvMetaBlob);
    off += (uint32_t)(sizeof(FvEnt) + blob + (size_t)ent.n * sizeof(FlexVecNode));
    if(ent.kind == FV_ENT_ELEM){
      int e = ent.elem;
      const FlexVecElem* el = &d->elems[e];
      const FlexVecNode* nd = (el->layer >= 0 && el->count > 0) ? d->arena.nodes + el->first : NULL;
      int n = (el->layer >= 0) ? (int)el->count : 0;
      fvUndoWriteEnt(d, FV_ENT_ELEM, e, el, sizeof(*el), nd, n);
    } else if(ent.kind == FV_ENT_LAYERS){
      FvLayersBlob b;
      memcpy(b.layers, d->layers, sizeof(b.layers));
      b.layerN = d->layerN; b.layerActive = d->layerActive;
      fvUndoWriteEnt(d, FV_ENT_LAYERS, 0, &b, sizeof(b), NULL, 0);
    } else {
      FvMetaBlob mb; mb.artW = d->artW; mb.artH = d->artH;
      memcpy(mb.name, d->name, sizeof(mb.name));
      fvUndoWriteEnt(d, FV_ENT_META, 0, &mb, sizeof(mb), NULL, 0);
    }
  }
  uint32_t total = d->undoUsed - start + FV_REC_FTR;
  memcpy(d->arena.undo + start, &total, 4);
  memcpy(d->arena.undo + start + 4, &nent, 2);
  memcpy(d->arena.undo + d->undoUsed, &total, 4);
  d->undoUsed += FV_REC_FTR;
  d->undoCursor = d->undoUsed;
  d->undoDone++; d->undoAll = d->undoDone;
  // Techo de PASOS, ademas del de bytes: un documento con objetos
  // diminutos podria acumular miles de registros baratos y hacer que el
  // recorrido hacia atras deje de ser instantaneo.
  while(d->undoDone > FLEXVEC_UNDO_STEPS && fvUndoDropOldest(d)) { }
  return FLEXVEC_OK;
}

// Aplica una seccion de entradas (la "antes" o la "despues").
static void fvUndoApplySection(FlexVecDoc* d, uint32_t off, uint32_t end){
  int guard = 0;
  while(off + sizeof(FvEnt) <= end && guard++ <= FLEXVEC_MAX_ELEMS + 2){
    FvEnt ent; memcpy(&ent, d->arena.undo + off, sizeof(ent));
    const uint8_t* p = d->arena.undo + off + sizeof(FvEnt);
    if(ent.kind == FV_ENT_ELEM){
      FlexVecElem el; memcpy(&el, p, sizeof(el));
      p += sizeof(el);
      int e = ent.elem;
      if(e >= 0 && e < FLEXVEC_MAX_ELEMS){
        fvNodeRelease(d, e);                      // suelta lo que hubiera
        if(el.layer >= 0 && ent.n > 0){
          int at = fvNodeAlloc(d, ent.n);
          if(at < 0){ fvCompact(d); at = fvNodeAlloc(d, ent.n); }
          if(at >= 0){
            memcpy(d->arena.nodes + at, p, (size_t)ent.n * sizeof(FlexVecNode));
            el.first = (uint16_t)at; el.count = ent.n;
          } else {
            // No cabe restaurar la geometria: se restaura el objeto SIN
            // nodos en vez de dejar indices apuntando a cualquier sitio.
            el.first = 0; el.count = 0;
          }
        } else { el.first = 0; el.count = 0; }
        el.bboxOk = 0;
        d->elems[e] = el;
      }
      off += (uint32_t)(sizeof(FvEnt) + sizeof(FlexVecElem) + (size_t)ent.n * sizeof(FlexVecNode));
    } else if(ent.kind == FV_ENT_LAYERS){
      FvLayersBlob b; memcpy(&b, p, sizeof(b));
      memcpy(d->layers, b.layers, sizeof(d->layers));
      d->layerN = b.layerN; d->layerActive = b.layerActive;
      off += (uint32_t)(sizeof(FvEnt) + sizeof(FvLayersBlob));
    } else {
      FvMetaBlob mb; memcpy(&mb, p, sizeof(mb));
      d->artW = mb.artW; d->artH = mb.artH;
      memcpy(d->name, mb.name, sizeof(d->name));
      off += (uint32_t)(sizeof(FvEnt) + sizeof(FvMetaBlob));
    }
  }
}

// Localiza las dos secciones de un registro: [antes) y [despues).
static void fvUndoSections(const FlexVecDoc* d, uint32_t start,
                           uint32_t* bOff, uint32_t* bEnd, uint32_t* aOff, uint32_t* aEnd){
  uint32_t total = fvRecTotal(d, start);
  uint16_t nent = 0; memcpy(&nent, d->arena.undo + start + 4, 2);
  uint32_t off = start + FV_REC_HDR;
  uint32_t recEnd = start + total - FV_REC_FTR;
  *bOff = off;
  for(uint16_t i = 0; i < nent && off + sizeof(FvEnt) <= recEnd; i++){
    FvEnt ent; memcpy(&ent, d->arena.undo + off, sizeof(ent));
    size_t blob = (ent.kind == FV_ENT_ELEM)   ? sizeof(FlexVecElem)
                : (ent.kind == FV_ENT_LAYERS) ? sizeof(FvLayersBlob)
                : sizeof(FvMetaBlob);
    off += (uint32_t)(sizeof(FvEnt) + blob + (size_t)ent.n * sizeof(FlexVecNode));
  }
  *bEnd = off; *aOff = off; *aEnd = recEnd;
}

int flexVecCanUndo(const FlexVecDoc* d){ return d && !d->undoOpen && d->undoDone > 0; }
int flexVecCanRedo(const FlexVecDoc* d){ return d && !d->undoOpen && d->undoAll > d->undoDone; }

int flexVecUndo(FlexVecDoc* d){
  if(!flexVecCanUndo(d)) return FLEXVEC_E_EMPTY;
  uint32_t total = 0;
  memcpy(&total, d->arena.undo + d->undoCursor - FV_REC_FTR, 4);
  if(total == 0 || total > d->undoCursor) return FLEXVEC_E_BADARG;
  uint32_t start = d->undoCursor - total;
  uint32_t b0, b1, a0, a1;
  fvUndoSections(d, start, &b0, &b1, &a0, &a1);
  fvUndoApplySection(d, b0, b1);
  d->undoCursor = start;
  d->undoDone--;
  d->dirty = 1;
  return FLEXVEC_OK;
}

int flexVecRedo(FlexVecDoc* d){
  if(!flexVecCanRedo(d)) return FLEXVEC_E_EMPTY;
  uint32_t start = d->undoCursor;
  uint32_t total = fvRecTotal(d, start);
  if(total == 0 || start + total > d->undoUsed) return FLEXVEC_E_BADARG;
  uint32_t b0, b1, a0, a1;
  fvUndoSections(d, start, &b0, &b1, &a0, &a1);
  fvUndoApplySection(d, a0, a1);
  d->undoCursor = start + total;
  d->undoDone++;
  d->dirty = 1;
  return FLEXVEC_OK;
}

uint32_t flexVecUndoBytes(const FlexVecDoc* d){ return d ? d->undoUsed : 0; }
int flexVecUndoSteps(const FlexVecDoc* d){ return d ? (int)d->undoDone : 0; }
int flexVecUndoHistoryLost(FlexVecDoc* d){
  if(!d || !d->histLost) return 0;
  d->histLost = 0;                    // se informa una sola vez
  return 1;
}
int  flexVecDirty(const FlexVecDoc* d){ return d ? (int)d->dirty : 0; }
void flexVecSetSaved(FlexVecDoc* d){ if(d) d->dirty = 0; }

// #############################################################
//  TRANSACCION IMPLICITA
//  ------------------------------------------------------------
//  Toda operacion que modifica el documento es deshacible POR SI SOLA.
//  Si el anfitrion ya abrio una transaccion (para agrupar, por ejemplo,
//  "mover los seis objetos seleccionados" en un solo paso), la
//  operacion se suma a la que hay abierta. Si no, se abre y se cierra
//  ella misma. Asi no existe la categoria "operacion que se me olvido
//  registrar", que es de donde salen los historiales incoherentes.
// #############################################################
static int fvAutoBegin(FlexVecDoc* d, const char* label){
  if(!d || d->undoOpen) return 0;
  flexVecUndoBegin(d, label);
  return 1;
}
static int fvAutoEnd(FlexVecDoc* d, int owned, int rc){
  if(owned){
    if(rc < 0) flexVecUndoAbort(d);
    else       flexVecUndoCommit(d);
  } else if(rc >= 0) d->dirty = 1;
  return rc;
}
// Cierra en FALLO una funcion que devuelve ESTADO (no un indice). Existe
// para que el codigo de error salga siempre con el MISMO signo: las
// funciones de estado devuelven FLEXVEC_E_* en positivo y las que
// devuelven un indice lo hacen en negativo. Mezclar los dos convenios
// hace que un "if(rc == FLEXVEC_E_TOO_COMPLEX)" del anfitrion falle
// justo en el caso que queria detectar.
static int fvAutoFail(FlexVecDoc* d, int owned, int err){
  if(owned) flexVecUndoAbort(d);
  return err < 0 ? -err : err;
}

// #############################################################
//  CAPAS
// #############################################################
int flexVecLayerAdd(FlexVecDoc* d, const char* name){
  if(!d) return -FLEXVEC_E_BADARG;
  for(int i = 0; i < FLEXVEC_MAX_LAYERS; i++){
    if(d->layers[i].used) continue;
    int owned = fvAutoBegin(d, "Capa nueva");
    fvUndoTouchLayers(d);
    d->layers[i].used = 1; d->layers[i].visible = 1; d->layers[i].locked = 0;
    if(name && name[0]) snprintf(d->layers[i].name, FLEXVEC_NAME_MAX, "%s", name);
    else                snprintf(d->layers[i].name, FLEXVEC_NAME_MAX, "Capa %d", i + 1);
    if(i >= d->layerN) d->layerN = i + 1;
    d->layerActive = i;
    fvAutoEnd(d, owned, FLEXVEC_OK);
    return i;
  }
  return -FLEXVEC_E_FULL_LAYERS;
}

int flexVecLayerDelete(FlexVecDoc* d, int layer){
  if(!fvLayerValid(d, layer)) return FLEXVEC_E_BADARG;
  int live = 0;
  for(int i = 0; i < FLEXVEC_MAX_LAYERS; i++) if(d->layers[i].used) live++;
  if(live <= 1) return FLEXVEC_E_BADARG;         // un documento siempre tiene una capa
  int owned = fvAutoBegin(d, "Borrar capa");
  fvUndoTouchLayers(d);
  for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++){
    if(d->elems[e].layer != layer) continue;
    fvUndoTouch(d, e);
    fvNodeRelease(d, e);
    d->elems[e].layer = -1;
  }
  d->layers[layer].used = 0;
  if(d->layerActive == layer)
    for(int i = 0; i < FLEXVEC_MAX_LAYERS; i++) if(d->layers[i].used){ d->layerActive = i; break; }
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}

// Reordena una capa. El orden de PINTADO es el orden del array, asi que
// mover una capa es intercambiarla con la vecina usada.
int flexVecLayerMove(FlexVecDoc* d, int layer, int delta){
  if(!fvLayerValid(d, layer) || delta == 0) return FLEXVEC_E_BADARG;
  int step = delta > 0 ? 1 : -1;
  int other = layer + step;
  while(other >= 0 && other < FLEXVEC_MAX_LAYERS && !d->layers[other].used) other += step;
  if(other < 0 || other >= FLEXVEC_MAX_LAYERS) return FLEXVEC_E_BADARG;
  int owned = fvAutoBegin(d, "Reordenar capa");
  fvUndoTouchLayers(d);
  FlexVecLayer t = d->layers[layer];
  d->layers[layer] = d->layers[other];
  d->layers[other] = t;
  for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++){
    if(d->elems[e].layer == layer){ fvUndoTouch(d, e); d->elems[e].layer = (int16_t)other; }
    else if(d->elems[e].layer == other){ fvUndoTouch(d, e); d->elems[e].layer = (int16_t)layer; }
  }
  if(d->layerActive == layer) d->layerActive = other;
  else if(d->layerActive == other) d->layerActive = layer;
  if(other >= d->layerN) d->layerN = other + 1;
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}

int flexVecLayerSetVisible(FlexVecDoc* d, int layer, int on){
  if(!fvLayerValid(d, layer)) return FLEXVEC_E_BADARG;
  int owned = fvAutoBegin(d, "Visibilidad");
  fvUndoTouchLayers(d);
  d->layers[layer].visible = on ? 1 : 0;
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}
int flexVecLayerSetLocked(FlexVecDoc* d, int layer, int on){
  if(!fvLayerValid(d, layer)) return FLEXVEC_E_BADARG;
  int owned = fvAutoBegin(d, "Bloqueo");
  fvUndoTouchLayers(d);
  d->layers[layer].locked = on ? 1 : 0;
  if(on){                                   // bloquear DESELECCIONA lo suyo
    for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++)
      if(d->elems[e].layer == layer) d->elems[e].flags &= (uint8_t)~FLEXVEC_EF_SEL;
  }
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}
int flexVecLayerSetActive(FlexVecDoc* d, int layer){
  if(!fvLayerValid(d, layer)) return FLEXVEC_E_BADARG;
  d->layerActive = layer;                   // la capa activa NO es del documento: no entra al diario
  return FLEXVEC_OK;
}

// #############################################################
//  CREACION DE ELEMENTOS
// #############################################################
static void fvElemDefaults(FlexVecElem* e, int layer, int kind){
  memset(e, 0, sizeof(*e));
  e->layer = (int16_t)layer;
  e->kind  = (uint8_t)kind;
  e->fill.type = FLEXVEC_P_SOLID; e->fill.rgb = 0x3C6EF0; e->fill.alpha = 255; e->fill.ref = -1;
  e->stroke.type = FLEXVEC_P_NONE; e->stroke.rgb = 0x101018; e->stroke.alpha = 255; e->stroke.ref = -1;
  e->strokeW = 2.0f;
  e->alpha = 255;
  e->cap = FLEXVEC_CAP_BUTT; e->join = FLEXVEC_JOIN_MITER;
  e->ref = -1;
  flexVecMatIdentity(e->m);
}

// Reserva una ranura de elemento y le da el z mas alto de su capa.
//
// EL ORDEN DE ESTAS LINEAS IMPORTA, Y MUCHO. La foto del "antes" para el
// diario (fvUndoTouch) tiene que tomarse ANTES de escribir nada en la
// ranura. Tomarla despues guarda como "antes" un objeto que ya existe:
// deshacer la creacion no lo borraria, lo dejaria ahi con la geometria a
// medio construir. Y como las ranuras se reutilizan, ese objeto fantasma
// aparece justo donde el usuario acaba de borrar otro. Ese fallo lo caza
// testUndo() comparando el documento paso a paso.
static int fvElemNew(FlexVecDoc* d, int kind, int nodes){
  if(!d || !d->arena.nodes) return -FLEXVEC_E_NOMEM;
  int layer = d->layerActive;
  if(!fvLayerValid(d, layer)) return -FLEXVEC_E_BADARG;
  if(d->layers[layer].locked) return -FLEXVEC_E_LOCKED;
  int slot = -1;
  for(int i = 0; i < FLEXVEC_MAX_ELEMS; i++) if(d->elems[i].layer < 0){ slot = i; break; }
  if(slot < 0) return -FLEXVEC_E_FULL_ELEMS;
  if(nodes > FLEXVEC_MAX_NODES_PATH) return -FLEXVEC_E_TOO_COMPLEX;
  fvUndoTouch(d, slot);                 // "antes" = la ranura VACIA
  int at = 0;
  if(nodes > 0){
    at = fvNodeAlloc(d, nodes);
    if(at < 0) return -FLEXVEC_E_FULL_NODES;
  }
  int zmax = -1;
  for(int i = 0; i < FLEXVEC_MAX_ELEMS; i++)
    if(d->elems[i].layer == layer && d->elems[i].z > zmax) zmax = d->elems[i].z;
  fvElemDefaults(&d->elems[slot], layer, kind);
  d->elems[slot].first = (uint16_t)at;
  d->elems[slot].count = (uint16_t)nodes;
  d->elems[slot].z = (int16_t)(zmax + 1);
  if(slot >= d->elemN) d->elemN = slot + 1;
  return slot;
}

static void fvNodeSet(FlexVecNode* n, float x, float y, uint8_t flags){
  n->x = fvSafe(x); n->y = fvSafe(y);
  n->ix = n->x; n->iy = n->y;
  n->ox = n->x; n->oy = n->y;
  n->flags = flags;
  n->pad[0] = n->pad[1] = n->pad[2] = 0;
}

int flexVecAddPath(FlexVecDoc* d){
  int owned = fvAutoBegin(d, "Trazado");
  int e = fvElemNew(d, FLEXVEC_K_PATH, 0);
  if(e < 0){ fvAutoEnd(d, owned, e); return e; }
  fvUndoTouch(d, e);
  d->elems[e].fill.type = FLEXVEC_P_NONE;      // la Pluma empieza SIN relleno:
  d->elems[e].stroke.type = FLEXVEC_P_SOLID;   // un trazado a medias con relleno
  d->elems[e].bboxOk = 0;                      // solo confunde mientras se dibuja
  fvAutoEnd(d, owned, FLEXVEC_OK);
  return e;
}

int flexVecAddRect(FlexVecDoc* d, float x, float y, float w, float h, float r){
  if(!fvFinite(x) || !fvFinite(y) || !fvFinite(w) || !fvFinite(h)) return -FLEXVEC_E_BADARG;
  if(w < 0){ x += w; w = -w; }
  if(h < 0){ y += h; h = -h; }
  if(!fvFinite(r) || r < 0) r = 0;
  float rmax = fvMin(w, h) * 0.5f;
  if(r > rmax) r = rmax;
  int owned = fvAutoBegin(d, "Rectangulo");
  int n = (r > 0.01f) ? 8 : 4;
  int e = fvElemNew(d, FLEXVEC_K_RECT, n);
  if(e < 0){ fvAutoEnd(d, owned, e); return e; }
  fvUndoTouch(d, e);
  FlexVecNode* nd = d->arena.nodes + d->elems[e].first;
  const float K = 0.5522847498f;               // circulo con Bezier cubica
  if(n == 4){
    fvNodeSet(&nd[0], x,     y,     FLEXVEC_N_START | FLEXVEC_N_CLOSE);
    fvNodeSet(&nd[1], x + w, y,     0);
    fvNodeSet(&nd[2], x + w, y + h, 0);
    fvNodeSet(&nd[3], x,     y + h, 0);
  } else {
    float k = r * K;
    fvNodeSet(&nd[0], x + r,     y,         FLEXVEC_N_START | FLEXVEC_N_CLOSE);
    nd[0].ox = x + r + k;   nd[0].oy = y;
    fvNodeSet(&nd[1], x + w - r, y,         0);
    nd[1].ix = x + w - r - k; nd[1].iy = y;
    nd[1].ox = x + w;         nd[1].oy = y + r - k;   /* esquina */
    fvNodeSet(&nd[2], x + w,     y + r,     0);
    nd[2].ix = x + w;         nd[2].iy = y + r - k;
    nd[2].ox = x + w;         nd[2].oy = y + h - r + k;
    fvNodeSet(&nd[3], x + w,     y + h - r, 0);
    nd[3].ix = x + w;         nd[3].iy = y + h - r - k;
    nd[3].ox = x + w - r + k; nd[3].oy = y + h;
    fvNodeSet(&nd[4], x + w - r, y + h,     0);
    nd[4].ix = x + w - r + k; nd[4].iy = y + h;
    nd[4].ox = x + r - k;     nd[4].oy = y + h;
    fvNodeSet(&nd[5], x + r,     y + h,     0);
    nd[5].ix = x + r - k;     nd[5].iy = y + h;
    nd[5].ox = x;             nd[5].oy = y + h - r + k;
    fvNodeSet(&nd[6], x,         y + h - r, 0);
    nd[6].ix = x;             nd[6].iy = y + h - r + k;
    nd[6].ox = x;             nd[6].oy = y + r - k;
    fvNodeSet(&nd[7], x,         y + r,     0);
    nd[7].ix = x;             nd[7].iy = y + r - k;
    nd[7].ox = x + r - k;     nd[7].oy = y;
    // Cierre: el tirador de entrada del primer nodo lo aporta el ultimo.
    nd[0].ix = x + r - k;     nd[0].iy = y;
  }
  // Los rectangulos rectos no necesitan tirador: ancla = control (recta).
  if(n == 4) for(int i = 0; i < 4; i++){ nd[i].ix = nd[i].x; nd[i].iy = nd[i].y; nd[i].ox = nd[i].x; nd[i].oy = nd[i].y; }
  d->elems[e].p0 = r; d->elems[e].p1 = r;
  d->elems[e].bboxOk = 0;
  fvAutoEnd(d, owned, FLEXVEC_OK);
  return e;
}

int flexVecAddEllipse(FlexVecDoc* d, float cx, float cy, float rx, float ry){
  if(!fvFinite(cx) || !fvFinite(cy) || !fvFinite(rx) || !fvFinite(ry)) return -FLEXVEC_E_BADARG;
  rx = fvAbs(rx); ry = fvAbs(ry);
  if(rx < 0.01f) rx = 0.01f;
  if(ry < 0.01f) ry = 0.01f;
  int owned = fvAutoBegin(d, "Elipse");
  int e = fvElemNew(d, FLEXVEC_K_ELLIPSE, 4);
  if(e < 0){ fvAutoEnd(d, owned, e); return e; }
  fvUndoTouch(d, e);
  const float K = 0.5522847498f;
  float kx = rx * K, ky = ry * K;
  FlexVecNode* nd = d->arena.nodes + d->elems[e].first;
  fvNodeSet(&nd[0], cx,      cy - ry, FLEXVEC_N_START | FLEXVEC_N_CLOSE | FLEXVEC_N_SMOOTH);
  nd[0].ix = cx - kx; nd[0].iy = cy - ry; nd[0].ox = cx + kx; nd[0].oy = cy - ry;
  fvNodeSet(&nd[1], cx + rx, cy,      FLEXVEC_N_SMOOTH);
  nd[1].ix = cx + rx; nd[1].iy = cy - ky; nd[1].ox = cx + rx; nd[1].oy = cy + ky;
  fvNodeSet(&nd[2], cx,      cy + ry, FLEXVEC_N_SMOOTH);
  nd[2].ix = cx + kx; nd[2].iy = cy + ry; nd[2].ox = cx - kx; nd[2].oy = cy + ry;
  fvNodeSet(&nd[3], cx - rx, cy,      FLEXVEC_N_SMOOTH);
  nd[3].ix = cx - rx; nd[3].iy = cy + ky; nd[3].ox = cx - rx; nd[3].oy = cy - ky;
  d->elems[e].p0 = rx; d->elems[e].p1 = ry;
  d->elems[e].bboxOk = 0;
  fvAutoEnd(d, owned, FLEXVEC_OK);
  return e;
}

int flexVecAddLine(FlexVecDoc* d, float x0, float y0, float x1, float y1){
  if(!fvFinite(x0) || !fvFinite(y0) || !fvFinite(x1) || !fvFinite(y1)) return -FLEXVEC_E_BADARG;
  int owned = fvAutoBegin(d, "Linea");
  int e = fvElemNew(d, FLEXVEC_K_LINE, 2);
  if(e < 0){ fvAutoEnd(d, owned, e); return e; }
  fvUndoTouch(d, e);
  FlexVecNode* nd = d->arena.nodes + d->elems[e].first;
  fvNodeSet(&nd[0], x0, y0, FLEXVEC_N_START);
  fvNodeSet(&nd[1], x1, y1, 0);
  d->elems[e].fill.type = FLEXVEC_P_NONE;        // una linea no se rellena
  d->elems[e].stroke.type = FLEXVEC_P_SOLID;
  d->elems[e].bboxOk = 0;
  fvAutoEnd(d, owned, FLEXVEC_OK);
  return e;
}

static int fvAddRadial(FlexVecDoc* d, int kind, float cx, float cy,
                       float rOut, float rIn, int points){
  if(!fvFinite(cx) || !fvFinite(cy) || !fvFinite(rOut)) return -FLEXVEC_E_BADARG;
  if(points < 3) points = 3;
  int nodes = (kind == FLEXVEC_K_STAR) ? points * 2 : points;
  if(nodes > FLEXVEC_MAX_NODES_PATH) return -FLEXVEC_E_TOO_COMPLEX;
  rOut = fvAbs(rOut); if(rOut < 0.01f) rOut = 0.01f;
  if(kind == FLEXVEC_K_STAR){
    rIn = fvAbs(rIn);
    if(!fvFinite(rIn) || rIn < 0.01f) rIn = rOut * 0.5f;
    if(rIn > rOut) rIn = rOut * 0.5f;
  }
  int e = fvElemNew(d, kind, nodes);
  if(e < 0) return e;
  fvUndoTouch(d, e);
  FlexVecNode* nd = d->arena.nodes + d->elems[e].first;
  const float TAU = 6.28318530718f;
  for(int i = 0; i < nodes; i++){
    float r = (kind == FLEXVEC_K_STAR) ? ((i & 1) ? rIn : rOut) : rOut;
    float a = -1.57079632679f + TAU * (float)i / (float)nodes;
    fvNodeSet(&nd[i], cx + r * cosf(a), cy + r * sinf(a), i == 0 ? (FLEXVEC_N_START | FLEXVEC_N_CLOSE) : 0);
  }
  d->elems[e].p0 = rOut; d->elems[e].p1 = rIn;
  d->elems[e].n0 = (int16_t)points;
  d->elems[e].bboxOk = 0;
  return e;
}

int flexVecAddPolygon(FlexVecDoc* d, float cx, float cy, float r, int sides){
  int owned = fvAutoBegin(d, "Poligono");
  int e = fvAddRadial(d, FLEXVEC_K_POLY, cx, cy, r, 0, sides);
  fvAutoEnd(d, owned, e);
  return e;
}
int flexVecAddStar(FlexVecDoc* d, float cx, float cy, float rOut, float rIn, int points){
  int owned = fvAutoBegin(d, "Estrella");
  int e = fvAddRadial(d, FLEXVEC_K_STAR, cx, cy, rOut, rIn, points);
  fvAutoEnd(d, owned, e);
  return e;
}

int flexVecDelete(FlexVecDoc* d, int elem){
  if(!fvElemValid(d, elem)) return FLEXVEC_E_BADARG;
  if(d->layers[d->elems[elem].layer].locked) return FLEXVEC_E_LOCKED;
  int owned = fvAutoBegin(d, "Borrar");
  fvUndoTouch(d, elem);
  fvNodeRelease(d, elem);
  d->elems[elem].layer = -1;
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}

// NADA DE BUFFERS EN LA PILA. La version obvia de esto copiaba los nodos
// del original a un FlexVecNode tmp[256] local -- 7 KB de marco, casi la
// pila entera del loopTask de Arduino (8 KB). Ese error exacto ya provoco
// un reinicio en este proyecto y por eso existe tests/host/check_stack.py.
// Aqui se reserva PRIMERO la ranura nueva y se lee el original DESPUES:
// si fvElemNew tuvo que compactar el pool, 'first' del original ya viene
// actualizado, asi que la copia va de PSRAM a PSRAM y el marco no crece.
int flexVecDuplicate(FlexVecDoc* d, int elem){
  if(!fvElemValid(d, elem)) return -FLEXVEC_E_BADARG;
  int owned = fvAutoBegin(d, "Duplicar");
  int n    = (int)d->elems[elem].count;
  int kind = (int)d->elems[elem].kind;
  int e = fvElemNew(d, kind, n);
  if(e < 0){ fvAutoEnd(d, owned, e); return e; }
  fvUndoTouch(d, e);
  FlexVecElem src = d->elems[elem];             // ya despues de una posible compactacion
  uint16_t first = d->elems[e].first;
  int16_t  z     = d->elems[e].z;
  int16_t  layer = d->elems[e].layer;
  d->elems[e] = src;
  d->elems[e].first = first; d->elems[e].count = (uint16_t)n;
  d->elems[e].z = z; d->elems[e].layer = layer;
  d->elems[e].flags &= (uint8_t)~FLEXVEC_EF_SEL;
  if(n > 0) memmove(d->arena.nodes + first, d->arena.nodes + src.first,
                    (size_t)n * sizeof(FlexVecNode));
  d->elems[e].bboxOk = 0;
  fvAutoEnd(d, owned, FLEXVEC_OK);
  return e;
}

// #############################################################
//  RECORRIDO DE LA GEOMETRIA
//  ------------------------------------------------------------
//  Un elemento guarda sus nodos en un rango contiguo. Los SUBTRAZADOS
//  se delimitan con el bit FLEXVEC_N_START, y el bit FLEXVEC_N_CLOSE
//  del nodo que abre cada uno dice si ese subtrazado se cierra.
//
//  El patron de recorrido esta escrito a mano en cada sitio que lo
//  necesita, sin construir una lista intermedia, PORQUE ASI NO HACE
//  FALTA NINGUN BUFFER: el numero de subtrazados no tiene cota util
//  (un resultado de Pathfinder puede traer decenas), y reservar un
//  array para ellos seria justo el tipo de reserva que este nucleo se
//  ha prohibido.
// #############################################################
// Devuelve el numero de nodos del subtrazado que empieza en 's', y si es
// cerrado. 's' tiene que ser el inicio de un subtrazado.
static int fvSubLen(const FlexVecNode* nd, int count, int s, int* closed){
  if(closed) *closed = (nd[s].flags & FLEXVEC_N_CLOSE) ? 1 : 0;
  int j = s + 1;
  while(j < count && !(nd[j].flags & FLEXVEC_N_START)) j++;
  return j - s;
}

// Punto de una Bezier cubica.
static inline void fvBez(float x0, float y0, float x1, float y1,
                         float x2, float y2, float x3, float y3,
                         float t, float* ox, float* oy){
  float u = 1.0f - t;
  float a = u * u * u, b = 3.0f * u * u * t, c = 3.0f * u * t * t, e = t * t * t;
  *ox = a * x0 + b * x1 + c * x2 + e * x3;
  *oy = a * y0 + b * y1 + c * y2 + e * y3;
}

// Los cuatro puntos de control del segmento 'k' de un subtrazado, ya
// TRANSFORMADOS por la matriz del elemento. Devuelve 0 si no hay tal
// segmento. Los tiradores que coinciden con su ancla dan una recta, que
// es exactamente como se guardan las formas rectas.
static int fvSegPts(const FlexVecNode* nd, int s, int n, int closed, int k,
                    const float* m, float* p){
  int segs = closed ? n : n - 1;
  if(n < 2 || k < 0 || k >= segs) return 0;
  const FlexVecNode* A = &nd[s + k];
  const FlexVecNode* B = &nd[s + ((k + 1) % n)];
  flexVecMatApply(m, A->x,  A->y,  &p[0], &p[1]);
  flexVecMatApply(m, A->ox, A->oy, &p[2], &p[3]);
  flexVecMatApply(m, B->ix, B->iy, &p[4], &p[5]);
  flexVecMatApply(m, B->x,  B->y,  &p[6], &p[7]);
  return 1;
}

// Extremos exactos de una cubica en un eje: raices de la derivada.
// Se aplica sobre los puntos YA transformados, porque una afin lleva la
// curva de control a la curva de control -- pero NO lleva un extremo a
// un extremo, asi que calcularlos antes de transformar daria una caja
// equivocada en cuanto hubiera una rotacion.
static void fvCubicRange(float a, float b, float c, float e, float* lo, float* hi){
  float mn = fvMin(a, e), mx = fvMax(a, e);
  float d1 = b - a, d2 = c - b, d3 = e - c;
  float A = d1 - 2.0f * d2 + d3;
  float B = 2.0f * (d2 - d1);
  float C = d1;
  float ts[2]; int nt = 0;
  if(fvAbs(A) < 1e-6f){
    if(fvAbs(B) > 1e-9f) ts[nt++] = -C / B;
  } else {
    float disc = B * B - 4.0f * A * C;
    if(disc >= 0){
      float sq = sqrtf(disc);
      ts[nt++] = (-B + sq) / (2.0f * A);
      ts[nt++] = (-B - sq) / (2.0f * A);
    }
  }
  for(int i = 0; i < nt; i++){
    float t = ts[i];
    if(!fvFinite(t) || t <= 0.0f || t >= 1.0f) continue;
    float u = 1.0f - t;
    float v = u * u * u * a + 3.0f * u * u * t * b + 3.0f * u * t * t * c + t * t * t * e;
    mn = fvMin(mn, v); mx = fvMax(mx, v);
  }
  *lo = mn; *hi = mx;
}

int flexVecBBox(FlexVecDoc* d, int elem, float* x0, float* y0, float* x1, float* y1){
  if(!fvElemValid(d, elem)) return FLEXVEC_E_BADARG;
  FlexVecElem* el = &d->elems[elem];
  if(!el->bboxOk){
    if(el->count == 0 || !d->arena.nodes){
      el->bx0 = el->by0 = el->bx1 = el->by1 = 0;
    } else {
      const FlexVecNode* nd = d->arena.nodes + el->first;
      float mnx = 1e30f, mny = 1e30f, mxx = -1e30f, mxy = -1e30f;
      int i = 0;
      while(i < (int)el->count){
        int closed = 0;
        int n = fvSubLen(nd, el->count, i, &closed);
        if(n == 1){
          float px, py;
          flexVecMatApply(el->m, nd[i].x, nd[i].y, &px, &py);
          mnx = fvMin(mnx, px); mxx = fvMax(mxx, px);
          mny = fvMin(mny, py); mxy = fvMax(mxy, py);
        }
        int segs = closed ? n : n - 1;
        for(int k = 0; k < segs; k++){
          float p[8];
          if(!fvSegPts(nd, i, n, closed, k, el->m, p)) continue;
          float lo, hi;
          fvCubicRange(p[0], p[2], p[4], p[6], &lo, &hi);
          mnx = fvMin(mnx, lo); mxx = fvMax(mxx, hi);
          fvCubicRange(p[1], p[3], p[5], p[7], &lo, &hi);
          mny = fvMin(mny, lo); mxy = fvMax(mxy, hi);
        }
        i += n;
      }
      if(mnx > mxx){ mnx = mxx = mny = mxy = 0; }
      el->bx0 = mnx; el->by0 = mny; el->bx1 = mxx; el->by1 = mxy;
    }
    el->bboxOk = 1;
  }
  if(x0) *x0 = el->bx0;
  if(y0) *y0 = el->by0;
  if(x1) *x1 = el->bx1;
  if(y1) *y1 = el->by1;
  return FLEXVEC_OK;
}

// #############################################################
//  NODOS: HERRAMIENTA PLUMA Y EDICION DIRECTA
// #############################################################
static void fvDirty(FlexVecDoc* d, int e){ d->elems[e].bboxOk = 0; d->dirty = 1; }

// Un elemento cuyos nodos se editan a mano deja de ser "un rectangulo de
// parametros" y pasa a ser un trazado: si no, el siguiente cambio de
// radio o de numero de lados borraria la edicion del usuario sin avisar.
static void fvBecomePath(FlexVecDoc* d, int e){
  if(d->elems[e].kind != FLEXVEC_K_PATH && d->elems[e].kind != FLEXVEC_K_TEXT)
    d->elems[e].kind = FLEXVEC_K_PATH;
}

static int fvEditable(FlexVecDoc* d, int elem){
  if(!fvElemValid(d, elem)) return FLEXVEC_E_BADARG;
  if(d->layers[d->elems[elem].layer].locked) return FLEXVEC_E_LOCKED;
  if(d->elems[elem].flags & FLEXVEC_EF_LOCKED) return FLEXVEC_E_LOCKED;
  return FLEXVEC_OK;
}

int flexVecNodeAppend(FlexVecDoc* d, int elem, float x, float y,
                      float hx, float hy, int smooth){
  int rc = fvEditable(d, elem);
  if(rc != FLEXVEC_OK) return rc;
  if(!fvFinite(x) || !fvFinite(y)) return FLEXVEC_E_BADARG;
  int owned = fvAutoBegin(d, "Ancla");
  fvUndoTouch(d, elem);
  rc = fvNodeGrow(d, elem, 1);
  if(rc != FLEXVEC_OK) return fvAutoFail(d, owned, rc);
  FlexVecElem* el = &d->elems[elem];
  FlexVecNode* nd = d->arena.nodes + el->first;
  int i = el->count;
  fvNodeSet(&nd[i], x, y, i == 0 ? FLEXVEC_N_START : 0);
  if(smooth && fvFinite(hx) && fvFinite(hy)){
    nd[i].flags |= FLEXVEC_N_SMOOTH;
    nd[i].ox = x + (hx - x); nd[i].oy = y + (hy - y);
    nd[i].ix = x - (hx - x); nd[i].iy = y - (hy - y);
  }
  el->count = (uint16_t)(i + 1);
  fvDirty(d, elem);
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}

int flexVecNodeInsert(FlexVecDoc* d, int elem, int at, float x, float y){
  int rc = fvEditable(d, elem);
  if(rc != FLEXVEC_OK) return rc;
  FlexVecElem* el = &d->elems[elem];
  if(at < 0 || at > (int)el->count) return FLEXVEC_E_BADARG;
  if(!fvFinite(x) || !fvFinite(y)) return FLEXVEC_E_BADARG;
  int owned = fvAutoBegin(d, "Insertar ancla");
  fvUndoTouch(d, elem);
  rc = fvNodeGrow(d, elem, 1);
  if(rc != FLEXVEC_OK) return fvAutoFail(d, owned, rc);
  el = &d->elems[elem];
  FlexVecNode* nd = d->arena.nodes + el->first;
  memmove(nd + at + 1, nd + at, (size_t)((int)el->count - at) * sizeof(FlexVecNode));
  uint8_t startBit = 0;
  if(at == 0){                                  // insertar delante hereda el START
    startBit = (uint8_t)(nd[1].flags & (FLEXVEC_N_START | FLEXVEC_N_CLOSE));
    nd[1].flags &= (uint8_t)~(FLEXVEC_N_START | FLEXVEC_N_CLOSE);
  }
  fvNodeSet(&nd[at], x, y, startBit);
  el->count = (uint16_t)(el->count + 1);
  fvBecomePath(d, elem);
  fvDirty(d, elem);
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}

int flexVecNodeDelete(FlexVecDoc* d, int elem, int idx){
  int rc = fvEditable(d, elem);
  if(rc != FLEXVEC_OK) return rc;
  FlexVecElem* el = &d->elems[elem];
  if(idx < 0 || idx >= (int)el->count) return FLEXVEC_E_BADARG;
  int owned = fvAutoBegin(d, "Borrar ancla");
  fvUndoTouch(d, elem);
  el = &d->elems[elem];
  FlexVecNode* nd = d->arena.nodes + el->first;
  uint8_t keep = (uint8_t)(nd[idx].flags & (FLEXVEC_N_START | FLEXVEC_N_CLOSE));
  memmove(nd + idx, nd + idx + 1, (size_t)((int)el->count - idx - 1) * sizeof(FlexVecNode));
  el->count = (uint16_t)(el->count - 1);
  // El nodo borrado abria un subtrazado: el siguiente hereda el papel, o
  // el subtrazado desaparece entero. Sin esto quedaria una geometria sin
  // inicio, que es un documento corrupto.
  if((keep & FLEXVEC_N_START) && idx < (int)el->count) nd[idx].flags |= keep;
  if(el->count == 0){ fvNodeRelease(d, elem); d->elems[elem].layer = -1; }
  else fvDirty(d, elem);
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}

int flexVecNodeMove(FlexVecDoc* d, int elem, int idx, float x, float y){
  int rc = fvEditable(d, elem);
  if(rc != FLEXVEC_OK) return rc;
  FlexVecElem* el = &d->elems[elem];
  if(idx < 0 || idx >= (int)el->count) return FLEXVEC_E_BADARG;
  if(!fvFinite(x) || !fvFinite(y)) return FLEXVEC_E_BADARG;
  int owned = fvAutoBegin(d, "Mover ancla");
  fvUndoTouch(d, elem);
  FlexVecNode* nd = d->arena.nodes + d->elems[elem].first;
  float dx = x - nd[idx].x, dy = y - nd[idx].y;
  nd[idx].x = x; nd[idx].y = y;
  nd[idx].ix += dx; nd[idx].iy += dy;           // los tiradores acompanan al ancla
  nd[idx].ox += dx; nd[idx].oy += dy;
  fvBecomePath(d, elem);
  fvDirty(d, elem);
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}

int flexVecHandleMove(FlexVecDoc* d, int elem, int idx, int which, float x, float y){
  int rc = fvEditable(d, elem);
  if(rc != FLEXVEC_OK) return rc;
  FlexVecElem* el = &d->elems[elem];
  if(idx < 0 || idx >= (int)el->count) return FLEXVEC_E_BADARG;
  if(!fvFinite(x) || !fvFinite(y)) return FLEXVEC_E_BADARG;
  int owned = fvAutoBegin(d, "Tirador");
  fvUndoTouch(d, elem);
  FlexVecNode* nd = d->arena.nodes + d->elems[elem].first;
  if(which){ nd[idx].ox = x; nd[idx].oy = y; }
  else     { nd[idx].ix = x; nd[idx].iy = y; }
  if(nd[idx].flags & FLEXVEC_N_SMOOTH){
    // Simetrico: el otro tirador es el reflejo respecto del ancla. Se
    // conserva su LONGITUD, como en Illustrator: reflejar tambien la
    // longitud haria saltar el otro lado de la curva al tocar este.
    float ax = nd[idx].x, ay = nd[idx].y;
    float vx = x - ax, vy = y - ay;
    float len = sqrtf(vx * vx + vy * vy);
    float ox2, oy2;
    if(which){ ox2 = nd[idx].ix - ax; oy2 = nd[idx].iy - ay; }
    else     { ox2 = nd[idx].ox - ax; oy2 = nd[idx].oy - ay; }
    float other = sqrtf(ox2 * ox2 + oy2 * oy2);
    if(len > 1e-4f){
      float k = (other > 1e-4f ? other : len) / len;
      if(which){ nd[idx].ix = ax - vx * k; nd[idx].iy = ay - vy * k; }
      else     { nd[idx].ox = ax - vx * k; nd[idx].oy = ay - vy * k; }
    }
  }
  fvBecomePath(d, elem);
  fvDirty(d, elem);
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}

int flexVecNodeSetSmooth(FlexVecDoc* d, int elem, int idx, int smooth){
  int rc = fvEditable(d, elem);
  if(rc != FLEXVEC_OK) return rc;
  if(idx < 0 || idx >= (int)d->elems[elem].count) return FLEXVEC_E_BADARG;
  int owned = fvAutoBegin(d, smooth ? "Suavizar" : "Vertice");
  fvUndoTouch(d, elem);
  FlexVecNode* nd = d->arena.nodes + d->elems[elem].first;
  if(smooth){
    nd[idx].flags |= FLEXVEC_N_SMOOTH;
    // Al suavizar una esquina, los tiradores se colocan sobre la
    // bisectriz de los dos segmentos vecinos: es lo que hace que la
    // curva salga "redonda a la primera" y no haya que ajustarla a mano.
    int n = (int)d->elems[elem].count;
    int prev = (idx > 0) ? idx - 1 : n - 1;
    int next = (idx + 1 < n) ? idx + 1 : 0;
    float vx = nd[next].x - nd[prev].x, vy = nd[next].y - nd[prev].y;
    float len = sqrtf(vx * vx + vy * vy);
    if(len > 1e-4f){
      float d1 = sqrtf((nd[idx].x - nd[prev].x) * (nd[idx].x - nd[prev].x) +
                       (nd[idx].y - nd[prev].y) * (nd[idx].y - nd[prev].y));
      float d2 = sqrtf((nd[next].x - nd[idx].x) * (nd[next].x - nd[idx].x) +
                       (nd[next].y - nd[idx].y) * (nd[next].y - nd[idx].y));
      float ux = vx / len, uy = vy / len;
      nd[idx].ix = nd[idx].x - ux * d1 * 0.33f; nd[idx].iy = nd[idx].y - uy * d1 * 0.33f;
      nd[idx].ox = nd[idx].x + ux * d2 * 0.33f; nd[idx].oy = nd[idx].y + uy * d2 * 0.33f;
    }
  } else {
    nd[idx].flags &= (uint8_t)~FLEXVEC_N_SMOOTH;
    nd[idx].ix = nd[idx].x; nd[idx].iy = nd[idx].y;   // esquina: sin tiradores
    nd[idx].ox = nd[idx].x; nd[idx].oy = nd[idx].y;
  }
  fvBecomePath(d, elem);
  fvDirty(d, elem);
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}

int flexVecPathClose(FlexVecDoc* d, int elem, int closed){
  int rc = fvEditable(d, elem);
  if(rc != FLEXVEC_OK) return rc;
  if(d->elems[elem].count < 3) return FLEXVEC_E_BADARG;
  int owned = fvAutoBegin(d, closed ? "Cerrar" : "Abrir");
  fvUndoTouch(d, elem);
  FlexVecNode* nd = d->arena.nodes + d->elems[elem].first;
  // Solo el ULTIMO subtrazado: cerrar el trazado en curso no puede abrir
  // los que ya estaban cerrados.
  int last = 0;
  for(int i = 0; i < (int)d->elems[elem].count; i++) if(nd[i].flags & FLEXVEC_N_START) last = i;
  if(closed) nd[last].flags |= FLEXVEC_N_CLOSE;
  else       nd[last].flags &= (uint8_t)~FLEXVEC_N_CLOSE;
  // Cerrar un trazado de la Pluma pide relleno: es lo que el usuario
  // espera ver al terminar la figura.
  if(closed && d->elems[elem].fill.type == FLEXVEC_P_NONE)
    d->elems[elem].fill.type = FLEXVEC_P_SOLID;
  fvDirty(d, elem);
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}

// Division de un segmento por de Casteljau: la curva NO cambia de forma.
int flexVecNodeSplit(FlexVecDoc* d, int elem, int seg, float t){
  int rc = fvEditable(d, elem);
  if(rc != FLEXVEC_OK) return rc;
  FlexVecElem* el = &d->elems[elem];
  int n = (int)el->count;
  if(seg < 0 || seg >= n) return FLEXVEC_E_BADARG;
  if(!fvFinite(t)) return FLEXVEC_E_BADARG;
  t = fvClampF(t, 0.01f, 0.99f);
  int nxt = (seg + 1) % n;
  if(nxt == 0 && !(d->arena.nodes[el->first].flags & FLEXVEC_N_CLOSE)) return FLEXVEC_E_BADARG;
  int owned = fvAutoBegin(d, "Dividir");
  fvUndoTouch(d, elem);
  rc = fvNodeGrow(d, elem, 1);
  if(rc != FLEXVEC_OK) return fvAutoFail(d, owned, rc);
  el = &d->elems[elem];
  FlexVecNode* nd = d->arena.nodes + el->first;
  n = (int)el->count;
  nxt = (seg + 1) % n;
  float p0x = nd[seg].x,  p0y = nd[seg].y;
  float p1x = nd[seg].ox, p1y = nd[seg].oy;
  float p2x = nd[nxt].ix, p2y = nd[nxt].iy;
  float p3x = nd[nxt].x,  p3y = nd[nxt].y;
  float ax = p0x + (p1x - p0x) * t, ay = p0y + (p1y - p0y) * t;
  float bx = p1x + (p2x - p1x) * t, by = p1y + (p2y - p1y) * t;
  float cx = p2x + (p3x - p2x) * t, cy = p2y + (p3y - p2y) * t;
  float dx = ax + (bx - ax) * t,    dy = ay + (by - ay) * t;
  float ex = bx + (cx - bx) * t,    ey = by + (cy - by) * t;
  float mx = dx + (ex - dx) * t,    my = dy + (ey - dy) * t;
  int at = seg + 1;
  memmove(nd + at + 1, nd + at, (size_t)(n - at) * sizeof(FlexVecNode));
  el->count = (uint16_t)(n + 1);
  nd[seg].ox = ax; nd[seg].oy = ay;
  fvNodeSet(&nd[at], mx, my, FLEXVEC_N_SMOOTH);
  nd[at].ix = dx; nd[at].iy = dy;
  nd[at].ox = ex; nd[at].oy = ey;
  int after = (at + 1) % (int)el->count;
  nd[after].ix = cx; nd[after].iy = cy;
  fvBecomePath(d, elem);
  fvDirty(d, elem);
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}

// #############################################################
//  APARIENCIA
//  ------------------------------------------------------------
//  Separada de la geometria, como en Illustrator: cambiar un color no
//  toca ni un nodo y por tanto no invalida la caja delimitadora ni la
//  geometria aplanada. Eso es lo que hace barato repintar solo lo que
//  de verdad cambio.
// #############################################################
int flexVecSetFill(FlexVecDoc* d, int elem, uint32_t rgb, uint8_t alpha){
  int rc = fvEditable(d, elem);
  if(rc != FLEXVEC_OK) return rc;
  int owned = fvAutoBegin(d, "Relleno");
  fvUndoTouch(d, elem);
  d->elems[elem].fill.type = FLEXVEC_P_SOLID;
  d->elems[elem].fill.rgb = rgb & 0xFFFFFFu;
  d->elems[elem].fill.alpha = alpha;
  d->elems[elem].fill.ref = -1;
  d->dirty = 1;
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}
int flexVecSetNoFill(FlexVecDoc* d, int elem){
  int rc = fvEditable(d, elem);
  if(rc != FLEXVEC_OK) return rc;
  int owned = fvAutoBegin(d, "Sin relleno");
  fvUndoTouch(d, elem);
  d->elems[elem].fill.type = FLEXVEC_P_NONE;
  d->dirty = 1;
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}
int flexVecSetStroke(FlexVecDoc* d, int elem, uint32_t rgb, uint8_t alpha, float w){
  int rc = fvEditable(d, elem);
  if(rc != FLEXVEC_OK) return rc;
  if(!fvFinite(w) || w < 0) return FLEXVEC_E_BADARG;
  int owned = fvAutoBegin(d, "Trazo");
  fvUndoTouch(d, elem);
  d->elems[elem].stroke.type = FLEXVEC_P_SOLID;
  d->elems[elem].stroke.rgb = rgb & 0xFFFFFFu;
  d->elems[elem].stroke.alpha = alpha;
  d->elems[elem].stroke.ref = -1;
  d->elems[elem].strokeW = fvClampF(w, 0.0f, 512.0f);
  d->dirty = 1;
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}
int flexVecSetNoStroke(FlexVecDoc* d, int elem){
  int rc = fvEditable(d, elem);
  if(rc != FLEXVEC_OK) return rc;
  int owned = fvAutoBegin(d, "Sin trazo");
  fvUndoTouch(d, elem);
  d->elems[elem].stroke.type = FLEXVEC_P_NONE;
  d->dirty = 1;
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}
int flexVecSetAlpha(FlexVecDoc* d, int elem, uint8_t alpha){
  int rc = fvEditable(d, elem);
  if(rc != FLEXVEC_OK) return rc;
  int owned = fvAutoBegin(d, "Opacidad");
  fvUndoTouch(d, elem);
  d->elems[elem].alpha = alpha;
  d->dirty = 1;
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}
int flexVecSetCapJoin(FlexVecDoc* d, int elem, int cap, int join){
  int rc = fvEditable(d, elem);
  if(rc != FLEXVEC_OK) return rc;
  if(cap < 0 || cap > FLEXVEC_CAP_SQUARE || join < 0 || join > FLEXVEC_JOIN_BEVEL)
    return FLEXVEC_E_BADARG;
  int owned = fvAutoBegin(d, "Extremos");
  fvUndoTouch(d, elem);
  d->elems[elem].cap = (uint8_t)cap;
  d->elems[elem].join = (uint8_t)join;
  d->dirty = 1;
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}

// #############################################################
//  TRANSFORMACIONES AFINES
//  ------------------------------------------------------------
//  SE COMPONEN EN LA MATRIZ, la geometria no se reescribe. Escalar mil
//  veces cuesta mil multiplicaciones de matrices 2x3 y ni un nodo mas;
//  reescribir la geometria costaria mil pasadas sobre el pool y ademas
//  acumularia error de redondeo en cada una.
// #############################################################
static int fvApplyM(FlexVecDoc* d, int elem, const float* mm, const char* label){
  int rc = fvEditable(d, elem);
  if(rc != FLEXVEC_OK) return rc;
  int owned = fvAutoBegin(d, label);
  fvUndoTouch(d, elem);
  flexVecMatMul(mm, d->elems[elem].m, d->elems[elem].m);
  fvDirty(d, elem);
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}
static void fvMatAbout(float* m, float cx, float cy,
                       float a, float b, float c, float dd){
  // T(c) . L . T(-c), escrito ya desarrollado: es la forma canonica de
  // "transformar alrededor de un punto" y evita dos multiplicaciones.
  m[0] = a; m[1] = b; m[2] = c; m[3] = dd;
  m[4] = cx - (a * cx + c * cy);
  m[5] = cy - (b * cx + dd * cy);
}

int flexVecTranslate(FlexVecDoc* d, int elem, float dx, float dy){
  if(!fvFinite(dx) || !fvFinite(dy)) return FLEXVEC_E_BADARG;
  float m[6] = { 1, 0, 0, 1, dx, dy };
  return fvApplyM(d, elem, m, "Mover");
}
int flexVecScale(FlexVecDoc* d, int elem, float cx, float cy, float sx, float sy){
  if(!fvFinite(cx) || !fvFinite(cy) || !fvFinite(sx) || !fvFinite(sy)) return FLEXVEC_E_BADARG;
  // Una escala de 0 aplasta el objeto a una linea sin retorno: la matriz
  // se vuelve singular y ya no se puede invertir para volver. Se acota.
  if(fvAbs(sx) < 1e-4f) sx = (sx < 0 ? -1e-4f : 1e-4f);
  if(fvAbs(sy) < 1e-4f) sy = (sy < 0 ? -1e-4f : 1e-4f);
  float m[6]; fvMatAbout(m, cx, cy, sx, 0, 0, sy);
  return fvApplyM(d, elem, m, "Escalar");
}
int flexVecRotate(FlexVecDoc* d, int elem, float cx, float cy, float rad){
  if(!fvFinite(cx) || !fvFinite(cy) || !fvFinite(rad)) return FLEXVEC_E_BADARG;
  float c = cosf(rad), s = sinf(rad);
  float m[6]; fvMatAbout(m, cx, cy, c, s, -s, c);
  return fvApplyM(d, elem, m, "Girar");
}
int flexVecMirror(FlexVecDoc* d, int elem, float cx, float cy, int horizontal){
  if(!fvFinite(cx) || !fvFinite(cy)) return FLEXVEC_E_BADARG;
  float m[6];
  if(horizontal) fvMatAbout(m, cx, cy, -1, 0, 0, 1);
  else           fvMatAbout(m, cx, cy, 1, 0, 0, -1);
  return fvApplyM(d, elem, m, "Reflejar");
}
int flexVecShear(FlexVecDoc* d, int elem, float cx, float cy, float kx, float ky){
  if(!fvFinite(cx) || !fvFinite(cy) || !fvFinite(kx) || !fvFinite(ky)) return FLEXVEC_E_BADARG;
  kx = fvClampF(kx, -8.0f, 8.0f);
  ky = fvClampF(ky, -8.0f, 8.0f);
  float m[6]; fvMatAbout(m, cx, cy, 1, ky, kx, 1);
  return fvApplyM(d, elem, m, "Inclinar");
}

// Transformar la SELECCION. Con 'each' cada objeto usa su propio centro
// (el "Transformar cada uno" de Illustrator); sin el, todos comparten el
// centro de la seleccion, que es lo que se espera al girar un grupo.
int flexVecTransformSel(FlexVecDoc* d, const float* mm, int each){
  if(!d || !mm) return FLEXVEC_E_BADARG;
  if(flexVecSelCount(d) == 0) return FLEXVEC_E_EMPTY;
  int owned = fvAutoBegin(d, each ? "Transformar cada uno" : "Transformar");
  for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++){
    if(!fvElemValid(d, e) || !(d->elems[e].flags & FLEXVEC_EF_SEL)) continue;
    if(fvEditable(d, e) != FLEXVEC_OK) continue;
    fvUndoTouch(d, e);
    if(each){
      float x0, y0, x1, y1;
      flexVecBBox(d, e, &x0, &y0, &x1, &y1);
      float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
      float back[6] = { 1, 0, 0, 1, -cx, -cy };
      float fwd[6]  = { 1, 0, 0, 1,  cx,  cy };
      float t[6];
      flexVecMatMul(mm, back, t);
      flexVecMatMul(fwd, t, t);
      flexVecMatMul(t, d->elems[e].m, d->elems[e].m);
    } else {
      flexVecMatMul(mm, d->elems[e].m, d->elems[e].m);
    }
    fvDirty(d, e);
  }
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}

// #############################################################
//  SELECCION Y PRUEBA DE IMPACTO
//  ------------------------------------------------------------
//  LA HOLGURA NO ES UN DETALLE. Un dedo sobre un GT911 marca una zona
//  de varios milimetros, no un pixel: sin holgura una linea de 1 px es
//  materialmente inseleccionable. Todas las pruebas de impacto de aqui
//  reciben 'tol' en unidades de DOCUMENTO, que la interfaz calcula a
//  partir del zoom -- asi la holgura son siempre los mismos milimetros
//  de dedo, se este mirando el documento entero o un detalle.
// #############################################################
void flexVecSelectNone(FlexVecDoc* d){
  if(!d) return;
  for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++){
    d->elems[e].flags &= (uint8_t)~FLEXVEC_EF_SEL;
    if(d->elems[e].layer >= 0 && d->elems[e].count && d->arena.nodes){
      FlexVecNode* nd = d->arena.nodes + d->elems[e].first;
      for(int i = 0; i < (int)d->elems[e].count; i++) nd[i].flags &= (uint8_t)~FLEXVEC_N_SEL;
    }
  }
}
int flexVecSelect(FlexVecDoc* d, int elem, int add){
  if(!fvElemValid(d, elem)) return FLEXVEC_E_BADARG;
  if(d->layers[d->elems[elem].layer].locked) return FLEXVEC_E_LOCKED;
  if(!add) flexVecSelectNone(d);
  d->elems[elem].flags |= FLEXVEC_EF_SEL;
  return FLEXVEC_OK;
}
int flexVecSelCount(const FlexVecDoc* d){
  if(!d) return 0;
  int n = 0;
  for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++)
    if(d->elems[e].layer >= 0 && (d->elems[e].flags & FLEXVEC_EF_SEL)) n++;
  return n;
}
int flexVecSelFirst(const FlexVecDoc* d){
  if(!d) return -1;
  for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++)
    if(d->elems[e].layer >= 0 && (d->elems[e].flags & FLEXVEC_EF_SEL)) return e;
  return -1;
}
int flexVecSelectRect(FlexVecDoc* d, float x0, float y0, float x1, float y1, int add){
  if(!d) return FLEXVEC_E_BADARG;
  if(x0 > x1){ float t = x0; x0 = x1; x1 = t; }
  if(y0 > y1){ float t = y0; y0 = y1; y1 = t; }
  if(!add) flexVecSelectNone(d);
  int n = 0;
  for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++){
    if(!fvElemValid(d, e)) continue;
    if(d->layers[d->elems[e].layer].locked || !d->layers[d->elems[e].layer].visible) continue;
    if(d->elems[e].flags & FLEXVEC_EF_HIDDEN) continue;
    float a, b, c, dd;
    flexVecBBox(d, e, &a, &b, &c, &dd);
    if(a >= x0 && c <= x1 && b >= y0 && dd <= y1){   // contenido ENTERO, como Illustrator
      d->elems[e].flags |= FLEXVEC_EF_SEL; n++;
    }
  }
  return n;
}
int flexVecSelBBox(FlexVecDoc* d, float* x0, float* y0, float* x1, float* y1){
  if(!d) return FLEXVEC_E_BADARG;
  float mnx = 1e30f, mny = 1e30f, mxx = -1e30f, mxy = -1e30f;
  int n = 0;
  for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++){
    if(!fvElemValid(d, e) || !(d->elems[e].flags & FLEXVEC_EF_SEL)) continue;
    float a, b, c, dd;
    flexVecBBox(d, e, &a, &b, &c, &dd);
    mnx = fvMin(mnx, a); mny = fvMin(mny, b);
    mxx = fvMax(mxx, c); mxy = fvMax(mxy, dd);
    n++;
  }
  if(!n) return FLEXVEC_E_EMPTY;
  if(x0) *x0 = mnx;
  if(y0) *y0 = mny;
  if(x1) *x1 = mxx;
  if(y1) *y1 = mxy;
  return FLEXVEC_OK;
}

// Distancia al cuadrado de un punto a un segmento.
static float fvDist2Seg(float px, float py, float ax, float ay, float bx, float by){
  float vx = bx - ax, vy = by - ay;
  float wx = px - ax, wy = py - ay;
  float len2 = vx * vx + vy * vy;
  float t = (len2 > 1e-12f) ? (wx * vx + wy * vy) / len2 : 0.0f;
  t = fvClampF(t, 0.0f, 1.0f);
  float dx = wx - vx * t, dy = wy - vy * t;
  return dx * dx + dy * dy;
}

// Impacto contra un elemento. NO usa ningun buffer: cada segmento se
// subdivide en FV_HIT_STEPS trozos sobre la marcha y se acumulan (a) el
// numero de cruces de la semirrecta horizontal -> dentro/fuera, y (b) la
// distancia minima al contorno -> impacto sobre el trazo.
#define FV_HIT_STEPS 8
static int fvElemHit(FlexVecDoc* d, int e, float x, float y, float tol){
  const FlexVecElem* el = &d->elems[e];
  if(el->count < 2 || !d->arena.nodes) return 0;
  const FlexVecNode* nd = d->arena.nodes + el->first;
  int crossings = 0;
  float best = 1e30f;
  int i = 0;
  while(i < (int)el->count){
    int closed = 0;
    int n = fvSubLen(nd, el->count, i, &closed);
    int segs = closed ? n : n - 1;
    float fx = 0, fy = 0, cxp = 0, cyp = 0;
    int first = 1;
    for(int k = 0; k < segs; k++){
      float p[8];
      if(!fvSegPts(nd, i, n, closed, k, el->m, p)) continue;
      if(first){ fx = cxp = p[0]; fy = cyp = p[1]; first = 0; }
      for(int t = 1; t <= FV_HIT_STEPS; t++){
        float qx, qy;
        fvBez(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], (float)t / FV_HIT_STEPS, &qx, &qy);
        float dd = fvDist2Seg(x, y, cxp, cyp, qx, qy);
        if(dd < best) best = dd;
        if((cyp > y) != (qy > y)){
          float xi = cxp + (y - cyp) * (qx - cxp) / ((qy - cyp) != 0 ? (qy - cyp) : 1e-9f);
          if(xi > x) crossings++;
        }
        cxp = qx; cyp = qy;
      }
    }
    if(closed && !first && (cyp > y) != (fy > y)){
      float xi = cxp + (y - cyp) * (fx - cxp) / ((fy - cyp) != 0 ? (fy - cyp) : 1e-9f);
      if(xi > x) crossings++;
    }
    i += n;
  }
  if(el->fill.type != FLEXVEC_P_NONE && (crossings & 1)) return 1;
  float half = (el->stroke.type != FLEXVEC_P_NONE) ? el->strokeW * 0.5f : 0.0f;
  float reach = tol + half;
  return best <= reach * reach;
}

int flexVecHitTest(FlexVecDoc* d, float x, float y, float tol){
  if(!d) return -1;
  if(tol < 0) tol = 0;
  int best = -1, bestLayer = -1, bestZ = -1;
  for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++){
    if(!fvElemValid(d, e)) continue;
    const FlexVecElem* el = &d->elems[e];
    if(!d->layers[el->layer].visible || d->layers[el->layer].locked) continue;
    if(el->flags & (FLEXVEC_EF_HIDDEN | FLEXVEC_EF_LOCKED)) continue;
    float a, b, c, dd;
    flexVecBBox(d, e, &a, &b, &c, &dd);
    float pad = tol + ((el->stroke.type != FLEXVEC_P_NONE) ? el->strokeW * 0.5f : 0.0f);
    if(x < a - pad || x > c + pad || y < b - pad || y > dd + pad) continue;   // criba barata
    if(!fvElemHit(d, e, x, y, tol)) continue;
    if(el->layer > bestLayer || (el->layer == bestLayer && el->z > bestZ)){
      best = e; bestLayer = el->layer; bestZ = el->z;
    }
  }
  return best;
}

int flexVecHitNode(FlexVecDoc* d, int elem, float x, float y, float tol, int* part){
  if(part) *part = 0;
  if(!fvElemValid(d, elem) || !d->arena.nodes) return -1;
  const FlexVecElem* el = &d->elems[elem];
  const FlexVecNode* nd = d->arena.nodes + el->first;
  float best = tol * tol; int bi = -1, bp = 0;
  for(int i = 0; i < (int)el->count; i++){
    float px, py, dx, dy, dd;
    // Los TIRADORES se prueban antes que el ancla: cuando coinciden (una
    // esquina), lo que el usuario quiere agarrar es el ancla, y por eso
    // el ancla gana el empate al compararse con "<=" mas abajo.
    flexVecMatApply(el->m, nd[i].ix, nd[i].iy, &px, &py);
    dx = px - x; dy = py - y; dd = dx * dx + dy * dy;
    if(dd < best){ best = dd; bi = i; bp = 1; }
    flexVecMatApply(el->m, nd[i].ox, nd[i].oy, &px, &py);
    dx = px - x; dy = py - y; dd = dx * dx + dy * dy;
    if(dd < best){ best = dd; bi = i; bp = 2; }
    flexVecMatApply(el->m, nd[i].x, nd[i].y, &px, &py);
    dx = px - x; dy = py - y; dd = dx * dx + dy * dy;
    if(dd <= best){ best = dd; bi = i; bp = 0; }
  }
  if(part) *part = bp;
  return bi;
}

// #############################################################
//  ALINEAR, DISTRIBUIR Y ORDEN Z
// #############################################################
int flexVecAlign(FlexVecDoc* d, int how){
  float sx0, sy0, sx1, sy1;
  if(flexVecSelBBox(d, &sx0, &sy0, &sx1, &sy1) != FLEXVEC_OK) return FLEXVEC_E_EMPTY;
  int owned = fvAutoBegin(d, "Alinear");
  for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++){
    if(!fvElemValid(d, e) || !(d->elems[e].flags & FLEXVEC_EF_SEL)) continue;
    if(fvEditable(d, e) != FLEXVEC_OK) continue;
    float a, b, c, dd;
    flexVecBBox(d, e, &a, &b, &c, &dd);
    float dx = 0, dy = 0;
    switch(how){
      case FLEXVEC_AL_LEFT:    dx = sx0 - a; break;
      case FLEXVEC_AL_HCENTER: dx = (sx0 + sx1) * 0.5f - (a + c) * 0.5f; break;
      case FLEXVEC_AL_RIGHT:   dx = sx1 - c; break;
      case FLEXVEC_AL_TOP:     dy = sy0 - b; break;
      case FLEXVEC_AL_VCENTER: dy = (sy0 + sy1) * 0.5f - (b + dd) * 0.5f; break;
      case FLEXVEC_AL_BOTTOM:  dy = sy1 - dd; break;
      default: return fvAutoEnd(d, owned, FLEXVEC_E_BADARG);
    }
    if(dx != 0 || dy != 0){
      fvUndoTouch(d, e);
      float m[6] = { 1, 0, 0, 1, dx, dy };
      flexVecMatMul(m, d->elems[e].m, d->elems[e].m);
      fvDirty(d, e);
    }
  }
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}

int flexVecDistribute(FlexVecDoc* d, int horizontal){
  int ids[FLEXVEC_MAX_ELEMS]; int n = 0;
  for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++)
    if(fvElemValid(d, e) && (d->elems[e].flags & FLEXVEC_EF_SEL)) ids[n++] = e;
  if(n < 3) return FLEXVEC_E_EMPTY;      // con dos objetos no hay nada que repartir
  // Ordenacion por insercion sobre el centro. n <= 256: cota fija y sin reservas.
  for(int i = 1; i < n; i++){
    int v = ids[i];
    float a, b, c, dd;
    flexVecBBox(d, v, &a, &b, &c, &dd);
    float key = horizontal ? (a + c) * 0.5f : (b + dd) * 0.5f;
    int j = i - 1;
    while(j >= 0){
      float a2, b2, c2, d2;
      flexVecBBox(d, ids[j], &a2, &b2, &c2, &d2);
      float k2 = horizontal ? (a2 + c2) * 0.5f : (b2 + d2) * 0.5f;
      if(k2 <= key) break;
      ids[j + 1] = ids[j]; j--;
    }
    ids[j + 1] = v;
  }
  float fa, fb, fc, fd, la, lb, lc, ld;
  flexVecBBox(d, ids[0], &fa, &fb, &fc, &fd);
  flexVecBBox(d, ids[n - 1], &la, &lb, &lc, &ld);
  float first = horizontal ? (fa + fc) * 0.5f : (fb + fd) * 0.5f;
  float last  = horizontal ? (la + lc) * 0.5f : (lb + ld) * 0.5f;
  float step = (last - first) / (float)(n - 1);
  int owned = fvAutoBegin(d, "Distribuir");
  for(int i = 1; i < n - 1; i++){
    int e = ids[i];
    if(fvEditable(d, e) != FLEXVEC_OK) continue;
    float a, b, c, dd;
    flexVecBBox(d, e, &a, &b, &c, &dd);
    float cur = horizontal ? (a + c) * 0.5f : (b + dd) * 0.5f;
    float want = first + step * (float)i;
    float dx = horizontal ? (want - cur) : 0.0f;
    float dy = horizontal ? 0.0f : (want - cur);
    if(dx == 0 && dy == 0) continue;
    fvUndoTouch(d, e);
    float m[6] = { 1, 0, 0, 1, dx, dy };
    flexVecMatMul(m, d->elems[e].m, d->elems[e].m);
    fvDirty(d, e);
  }
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}

static int fvZSwap(FlexVecDoc* d, int elem, int up){
  if(!fvElemValid(d, elem)) return FLEXVEC_E_BADARG;
  int layer = d->elems[elem].layer, z = d->elems[elem].z;
  int other = -1, bestZ = up ? 32767 : -32768;
  for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++){
    if(!fvElemValid(d, e) || e == elem || d->elems[e].layer != layer) continue;
    int oz = d->elems[e].z;
    if(up ? (oz > z && oz < bestZ) : (oz < z && oz > bestZ)){ bestZ = oz; other = e; }
  }
  if(other < 0) return FLEXVEC_E_EMPTY;
  int owned = fvAutoBegin(d, up ? "Traer adelante" : "Enviar atras");
  fvUndoTouch(d, elem); fvUndoTouch(d, other);
  d->elems[elem].z = (int16_t)bestZ;
  d->elems[other].z = (int16_t)z;
  d->dirty = 1;
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}
int flexVecRaise(FlexVecDoc* d, int elem){ return fvZSwap(d, elem, 1); }
int flexVecLower(FlexVecDoc* d, int elem){ return fvZSwap(d, elem, 0); }

// Renumera los z de una capa a 0..k-1 conservando el orden relativo.
// Existe porque los z son int16: llevar un objeto "al frente" miles de
// veces los desbordaria, y un z desbordado invierte el orden de pintado
// de golpe. La renumeracion es una pasada acotada por FLEXVEC_MAX_ELEMS.
static void fvZRenumber(FlexVecDoc* d, int layer){
  int16_t ids[FLEXVEC_MAX_ELEMS]; int n = 0;
  for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++)
    if(fvElemValid(d, e) && d->elems[e].layer == layer) ids[n++] = (int16_t)e;
  for(int i = 1; i < n; i++){
    int16_t v = ids[i]; int16_t key = d->elems[v].z;
    int j = i - 1;
    while(j >= 0 && d->elems[ids[j]].z > key){ ids[j + 1] = ids[j]; j--; }
    ids[j + 1] = v;
  }
  for(int i = 0; i < n; i++){
    if(d->elems[ids[i]].z == (int16_t)i) continue;
    fvUndoTouch(d, ids[i]);
    d->elems[ids[i]].z = (int16_t)i;
  }
}

static int fvZExtreme(FlexVecDoc* d, int elem, int front){
  if(!fvElemValid(d, elem)) return FLEXVEC_E_BADARG;
  int layer = d->elems[elem].layer;
  int owned = fvAutoBegin(d, front ? "Al frente" : "Al fondo");
  int mn = 32767, mx = -32768;
  for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++){
    if(!fvElemValid(d, e) || d->elems[e].layer != layer) continue;
    if(d->elems[e].z < mn) mn = d->elems[e].z;
    if(d->elems[e].z > mx) mx = d->elems[e].z;
  }
  if(mx + 1 > 30000 || mn - 1 < -30000){
    fvZRenumber(d, layer);
    mn = 0; mx = 0;
    for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++){
      if(!fvElemValid(d, e) || d->elems[e].layer != layer) continue;
      if(d->elems[e].z < mn) mn = d->elems[e].z;
      if(d->elems[e].z > mx) mx = d->elems[e].z;
    }
  }
  int target = front ? mx + 1 : mn - 1;
  if(d->elems[elem].z != (int16_t)target){
    fvUndoTouch(d, elem);
    d->elems[elem].z = (int16_t)target;
    d->dirty = 1;
  }
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}
int flexVecToFront(FlexVecDoc* d, int elem){ return fvZExtreme(d, elem, 1); }
int flexVecToBack(FlexVecDoc* d, int elem){ return fvZExtreme(d, elem, 0); }

int flexVecMoveToLayer(FlexVecDoc* d, int elem, int layer){
  if(!fvElemValid(d, elem) || !fvLayerValid(d, layer)) return FLEXVEC_E_BADARG;
  if(d->layers[layer].locked) return FLEXVEC_E_LOCKED;
  int owned = fvAutoBegin(d, "Cambiar de capa");
  fvUndoTouch(d, elem);
  int zmax = -1;
  for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++)
    if(fvElemValid(d, e) && d->elems[e].layer == layer && d->elems[e].z > zmax) zmax = d->elems[e].z;
  d->elems[elem].layer = (int16_t)layer;
  d->elems[elem].z = (int16_t)(zmax + 1);
  d->dirty = 1;
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}

int flexVecPaintOrder(const FlexVecDoc* d, int16_t* out, int maxn){
  if(!d || !out || maxn <= 0) return 0;
  int n = 0;
  for(int l = 0; l < FLEXVEC_MAX_LAYERS && n < maxn; l++){
    if(!d->layers[l].used || !d->layers[l].visible) continue;
    int start = n;
    for(int e = 0; e < FLEXVEC_MAX_ELEMS && n < maxn; e++){
      if(d->elems[e].layer != l) continue;
      if(d->elems[e].flags & FLEXVEC_EF_HIDDEN) continue;
      out[n++] = (int16_t)e;
    }
    for(int i = start + 1; i < n; i++){        // insercion por z ascendente
      int16_t v = out[i];
      int16_t key = d->elems[v].z;
      int j = i - 1;
      while(j >= start && d->elems[out[j]].z > key){ out[j + 1] = out[j]; j--; }
      out[j + 1] = v;
    }
  }
  return n;
}

// #############################################################
//  AJUSTE A CUADRICULA
// #############################################################
float flexVecSnap(float v, float grid){
  if(!fvFinite(v) || !fvFinite(grid) || grid <= 0.0001f) return v;
  return floorf(v / grid + 0.5f) * grid;
}
int flexVecSnapSel(FlexVecDoc* d, float grid){
  if(!d || grid <= 0.0001f) return FLEXVEC_E_BADARG;
  if(flexVecSelCount(d) == 0) return FLEXVEC_E_EMPTY;
  int owned = fvAutoBegin(d, "Ajustar");
  for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++){
    if(!fvElemValid(d, e) || !(d->elems[e].flags & FLEXVEC_EF_SEL)) continue;
    if(fvEditable(d, e) != FLEXVEC_OK) continue;
    float a, b, c, dd;
    flexVecBBox(d, e, &a, &b, &c, &dd);
    float dx = flexVecSnap(a, grid) - a, dy = flexVecSnap(b, grid) - b;
    if(dx == 0 && dy == 0) continue;
    fvUndoTouch(d, e);
    float m[6] = { 1, 0, 0, 1, dx, dy };
    flexVecMatMul(m, d->elems[e].m, d->elems[e].m);
    fvDirty(d, e);
  }
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}

// #############################################################
//  RASTERIZADOR POR LINEAS DE BARRIDO
//  ------------------------------------------------------------
//  ESTE ES EL SITIO DONDE SE GANA O SE PIERDE LA FLUIDEZ. El P4 no
//  tiene GPU: cada pixel que se ve lo ha puesto la CPU. El diseno se
//  eligio con eso delante:
//
//   · ORDENACION POR CUENTA, NO POR COMPARACION. Las aristas se
//     reparten en cubetas por su fila superior en O(A + filas). Una
//     ordenacion por comparacion sobre 2048 aristas costaria mas que
//     rasterizar la figura.
//   · LISTA DE ARISTAS ACTIVAS. Cada fila solo mira las aristas que la
//     cruzan de verdad. Sin esto, una figura de 500 aristas se
//     recorreria 800 x 4 veces entera: 1,6 millones de pruebas para
//     una sola forma.
//   · ANTIALIAS EN LOS DOS EJES. Verticalmente por FLEXVEC_AA_SUB
//     sub-lineas; horizontalmente por cobertura EXACTA en los dos
//     pixeles de los extremos del tramo. En una pantalla de 480x800 un
//     borde dentado se ve, y se ve mucho.
//   · SE EMITEN TRAMOS, NO PIXELES. El anfitrion recibe [x0,x1] con una
//     cobertura constante y lo resuelve con un hLine: una escritura por
//     tramo en vez de una por pixel.
//
//  EL NUCLEO NO DIBUJA: emite. Lo que hace el callback -- mezclar con
//  el fondo, aplicar un gradiente, ignorar el tramo -- es asunto del
//  anfitrion, y por eso todo esto se puede probar en el PC.
// #############################################################
size_t flexVecRasterBytes(int covW){
  if(covW < 1) covW = 1;
  size_t n = 0;
  n += (size_t)2 * FLEXVEC_MAX_FLATPTS * sizeof(float);      // pts
  n += (size_t)2 * FLEXVEC_MAX_FLATPTS * sizeof(float);      // pts2
  n += (size_t)FLEXVEC_MAX_SUBS * sizeof(int32_t) * 5;       // subStart/Count/Closed + sub2*
  n += (size_t)FLEXVEC_MAX_EDGES * sizeof(int32_t) * 4;      // edgeA/edgeB/order/active
  n += (size_t)FLEXVEC_MAX_EDGES * sizeof(float);            // xs
  n += (size_t)FLEXVEC_MAX_EDGES * sizeof(int8_t);           // dirs
  n += (size_t)(FLEXVEC_RASTER_ROWS + 2) * sizeof(int32_t);  // bucket
  n += (size_t)covW;                                         // cov
  n += 64;                                                   // holgura de alineado
  return n;
}

int flexVecRasterInit(FlexVecRaster* r, void* block, size_t size, int covW){
  if(!r || !block || covW < 1) return FLEXVEC_E_BADARG;
  if(size < flexVecRasterBytes(covW)) return FLEXVEC_E_NOMEM;
  memset(r, 0, sizeof(*r));
  uint8_t* p = (uint8_t*)block;
  // Alineado a 8: en el RISC-V del P4 un float o un int32 desalineado
  // cuesta una excepcion de alineado por acceso, y aqui se accede
  // millones de veces por cuadro.
  size_t mis = ((size_t)p) & 7u;
  if(mis) p += (8u - mis);
  r->pts       = (float*)p;   p += (size_t)2 * FLEXVEC_MAX_FLATPTS * sizeof(float);
  r->pts2      = (float*)p;   p += (size_t)2 * FLEXVEC_MAX_FLATPTS * sizeof(float);
  r->subStart  = (int32_t*)p; p += (size_t)FLEXVEC_MAX_SUBS * sizeof(int32_t);
  r->subCount  = (int32_t*)p; p += (size_t)FLEXVEC_MAX_SUBS * sizeof(int32_t);
  r->subClosed = (int32_t*)p; p += (size_t)FLEXVEC_MAX_SUBS * sizeof(int32_t);
  r->sub2Start = (int32_t*)p; p += (size_t)FLEXVEC_MAX_SUBS * sizeof(int32_t);
  r->sub2Count = (int32_t*)p; p += (size_t)FLEXVEC_MAX_SUBS * sizeof(int32_t);
  r->edgeA     = (int32_t*)p; p += (size_t)FLEXVEC_MAX_EDGES * sizeof(int32_t);
  r->edgeB     = (int32_t*)p; p += (size_t)FLEXVEC_MAX_EDGES * sizeof(int32_t);
  r->order     = (int32_t*)p; p += (size_t)FLEXVEC_MAX_EDGES * sizeof(int32_t);
  r->active    = (int32_t*)p; p += (size_t)FLEXVEC_MAX_EDGES * sizeof(int32_t);
  r->xs        = (float*)p;   p += (size_t)FLEXVEC_MAX_EDGES * sizeof(float);
  r->bucket    = (int32_t*)p; p += (size_t)(FLEXVEC_RASTER_ROWS + 2) * sizeof(int32_t);
  r->dirs      = (int8_t*)p;  p += (size_t)FLEXVEC_MAX_EDGES * sizeof(int8_t);
  r->cov       = (uint8_t*)p;
  r->covW      = covW;
  return FLEXVEC_OK;
}

// -------------------------------------------------------------
//  APLANADO
//  ------------------------------------------------------------
//  Cuantos trozos por segmento sale de la longitud del poligono de
//  control EN PIXELES: una curva que ocupa 4 px no necesita 32 trozos,
//  y una que cruza la pantalla con 8 se ve poligonal. Asi el coste
//  sigue al zoom en vez de ser constante, que es lo que permite que
//  alejar el documento entero cueste MENOS, no lo mismo.
// -------------------------------------------------------------
static int fvSegSteps(const float* p){
  float d = fvAbs(p[2] - p[0]) + fvAbs(p[3] - p[1])
          + fvAbs(p[4] - p[2]) + fvAbs(p[5] - p[3])
          + fvAbs(p[6] - p[4]) + fvAbs(p[7] - p[5]);
  int n = (int)(sqrtf(d * 0.75f) + 1.0f);
  if(n < 1)  n = 1;
  if(n > 32) n = 32;                 // cota dura: ningun segmento explota
  return n;
}

int flexVecFlatten(FlexVecDoc* d, int elem, const FlexVecView* v,
                   FlexVecRaster* r, int* subN){
  if(subN) *subN = 0;
  if(!fvElemValid(d, elem) || !v || !r || !r->pts || !d->arena.nodes) return -FLEXVEC_E_BADARG;
  const FlexVecElem* el = &d->elems[elem];
  if(el->count < 2) return 0;
  float cm[6];
  flexVecMatMul(v->m, el->m, cm);           // documento -> pixel, en una sola matriz
  const FlexVecNode* nd = d->arena.nodes + el->first;
  int np = 0, ns = 0;
  int i = 0;
  while(i < (int)el->count){
    int closed = 0;
    int n = fvSubLen(nd, el->count, i, &closed);
    if(n < 2){ i += n; continue; }
    if(ns >= FLEXVEC_MAX_SUBS) break;       // se dibuja lo que cabe, sin desbordar
    int start = np;
    float sx, sy;
    flexVecMatApply(cm, nd[i].x, nd[i].y, &sx, &sy);
    if(np >= FLEXVEC_MAX_FLATPTS) break;
    r->pts[np * 2] = sx; r->pts[np * 2 + 1] = sy; np++;
    int segs = closed ? n : n - 1;
    int over = 0;
    for(int k = 0; k < segs && !over; k++){
      float p[8];
      if(!fvSegPts(nd, i, n, closed, k, cm, p)) continue;
      int steps = fvSegSteps(p);
      for(int t = 1; t <= steps; t++){
        if(np >= FLEXVEC_MAX_FLATPTS){ over = 1; break; }
        float qx, qy;
        fvBez(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], (float)t / (float)steps, &qx, &qy);
        r->pts[np * 2] = qx; r->pts[np * 2 + 1] = qy; np++;
      }
    }
    r->subStart[ns] = start;
    r->subCount[ns] = np - start;
    r->subClosed[ns] = closed;
    if(r->subCount[ns] >= 2) ns++; else np = start;
    i += n;
    if(over) break;
  }
  if(subN) *subN = ns;
  return np;
}

// -------------------------------------------------------------
//  RELLENO DE UNA POLILINEA YA APLANADA
// -------------------------------------------------------------
// Suma cobertura a un tramo [xa, xb) en flotante, con los dos pixeles
// de los extremos a su cobertura EXACTA.
static void fvCovAdd(uint8_t* cov, int covW, float xa, float xb, int wgt,
                     int* minX, int* maxX){
  if(xb <= xa) return;
  if(xa < 0) xa = 0;
  if(xb > (float)covW) xb = (float)covW;
  if(xb <= xa) return;
  int ia = (int)xa, ib = (int)xb;
  if(ia < 0) ia = 0;
  if(ib > covW - 1) ib = covW - 1;
  if(ia < *minX) *minX = ia;
  if(ib > *maxX) *maxX = ib;
  if(ia == ib){
    int add = (int)((xb - xa) * (float)wgt + 0.5f);
    int nv = cov[ia] + add; cov[ia] = (uint8_t)(nv > 255 ? 255 : nv);
    return;
  }
  int add = (int)(((float)(ia + 1) - xa) * (float)wgt + 0.5f);
  int nv = cov[ia] + add; cov[ia] = (uint8_t)(nv > 255 ? 255 : nv);
  for(int x = ia + 1; x < ib; x++){
    nv = cov[x] + wgt; cov[x] = (uint8_t)(nv > 255 ? 255 : nv);
  }
  add = (int)((xb - (float)ib) * (float)wgt + 0.5f);
  nv = cov[ib] + add; cov[ib] = (uint8_t)(nv > 255 ? 255 : nv);
}

// Emite la fila acumulada como TRAMOS de cobertura constante y la deja
// a cero para la siguiente. Solo se recorre [minX, maxX]: una figura
// pequena no paga el ancho de la pantalla.
static void fvCovFlush(uint8_t* cov, int covW, int y, int ox,
                       int minX, int maxX, FlexVecSpanCb cb, void* user){
  if(minX > maxX) return;
  if(minX < 0) minX = 0;
  if(maxX > covW - 1) maxX = covW - 1;
  int x = minX;
  while(x <= maxX){
    uint8_t c = cov[x];
    int s = x;
    while(x <= maxX && cov[x] == c) x++;
    if(c) cb(y, ox + s, ox + x - 1, c, user);
    memset(cov + s, 0, (size_t)(x - s));
  }
}

static int fvFillPolys(FlexVecRaster* r, const float* pts,
                       const int32_t* ss, const int32_t* sc, int subN,
                       const FlexVecView* v, int evenOdd,
                       FlexVecSpanCb cb, void* user){
  if(!r || !pts || !ss || !sc || !v || !cb || subN <= 0) return FLEXVEC_E_BADARG;
  int cx0 = v->clipX0, cx1 = v->clipX1, cy0 = v->clipY0, cy1 = v->clipY1;
  if(cx1 < cx0 || cy1 < cy0) return FLEXVEC_OK;
  if(cx1 - cx0 + 1 > r->covW) cx1 = cx0 + r->covW - 1;

  // 1) Aristas. Las horizontales no cruzan ninguna linea de barrido y se
  //    descartan aqui: dejarlas dentro solo anade trabajo por fila.
  int E = 0;
  float ymin = 1e30f, ymax = -1e30f;
  for(int s = 0; s < subN; s++){
    int st = ss[s], n = sc[s];
    if(n < 2) continue;
    for(int k = 0; k < n; k++){
      int a = st + k;
      int b = st + ((k + 1) % n);       // el ultimo cierra contra el primero
      if(pts[a * 2 + 1] == pts[b * 2 + 1]) continue;
      if(E >= FLEXVEC_MAX_EDGES) break;
      r->edgeA[E] = a; r->edgeB[E] = b;
      ymin = fvMin(ymin, fvMin(pts[a * 2 + 1], pts[b * 2 + 1]));
      ymax = fvMax(ymax, fvMax(pts[a * 2 + 1], pts[b * 2 + 1]));
      E++;
    }
  }
  if(E == 0) return FLEXVEC_OK;
  int y0 = fvIMax(cy0, (int)floorf(ymin));
  int y1 = fvIMin(cy1, (int)ceilf(ymax));
  if(y1 < y0) return FLEXVEC_OK;
  int rows = y1 - y0 + 1;
  if(rows > FLEXVEC_RASTER_ROWS) { y1 = y0 + FLEXVEC_RASTER_ROWS - 1; rows = FLEXVEC_RASTER_ROWS; }

  // 2) Ordenacion POR CUENTA sobre la fila superior de cada arista:
  //    O(A + filas), sin una sola comparacion entre aristas.
  memset(r->bucket, 0, (size_t)(rows + 2) * sizeof(int32_t));
  for(int e = 0; e < E; e++){
    float ya = pts[r->edgeA[e] * 2 + 1], yb = pts[r->edgeB[e] * 2 + 1];
    float top = fvMin(ya, yb);
    int row = (int)floorf(top) - y0;
    if(row < 0) row = 0;
    if(row > rows) row = rows;           // 'rows' = cubeta de "ya no entra"
    r->bucket[row]++;
  }
  int acc = 0;
  for(int i = 0; i <= rows; i++){ int c = r->bucket[i]; r->bucket[i] = acc; acc += c; }
  for(int e = 0; e < E; e++){
    float ya = pts[r->edgeA[e] * 2 + 1], yb = pts[r->edgeB[e] * 2 + 1];
    float top = fvMin(ya, yb);
    int row = (int)floorf(top) - y0;
    if(row < 0) row = 0;
    if(row > rows) row = rows;
    r->order[r->bucket[row]++] = e;
  }
  // Deshacer el desplazamiento que el reparto dejo en las cuentas.
  for(int i = rows; i > 0; i--) r->bucket[i] = r->bucket[i - 1];
  r->bucket[0] = 0;

  // 3) Barrido con lista de aristas activas.
  int minX = r->covW, maxX = -1;
  memset(r->cov, 0, (size_t)r->covW);
  int nAct = 0;
  const float wgt = 255.0f / (float)FLEXVEC_AA_SUB;
  int wgti = (int)(wgt + 0.5f);
  for(int y = y0; y <= y1; y++){
    int row = y - y0;
    for(int i = r->bucket[row]; i < ((row + 1 <= rows) ? r->bucket[row + 1] : E); i++){
      if(nAct < FLEXVEC_MAX_EDGES) r->active[nAct++] = r->order[i];
    }
    // Retirar las que ya quedaron por encima de esta fila.
    int w = 0;
    for(int i = 0; i < nAct; i++){
      int e = r->active[i];
      float ya = pts[r->edgeA[e] * 2 + 1], yb = pts[r->edgeB[e] * 2 + 1];
      if(fvMax(ya, yb) > (float)y) r->active[w++] = e;
    }
    nAct = w;
    if(nAct == 0){ fvCovFlush(r->cov, r->covW, y, cx0, minX, maxX, cb, user); minX = r->covW; maxX = -1; continue; }
    for(int k = 0; k < FLEXVEC_AA_SUB; k++){
      float sy = (float)y + ((float)k + 0.5f) / (float)FLEXVEC_AA_SUB;
      int nx = 0;
      for(int i = 0; i < nAct; i++){
        int e = r->active[i];
        int a = r->edgeA[e], b = r->edgeB[e];
        float ya = pts[a * 2 + 1], yb = pts[b * 2 + 1];
        int dir = 1;
        float xa = pts[a * 2], xb = pts[b * 2];
        if(ya > yb){ float t; t = ya; ya = yb; yb = t; t = xa; xa = xb; xb = t; dir = -1; }
        if(sy < ya || sy >= yb) continue;
        float x = xa + (sy - ya) * (xb - xa) / (yb - ya);
        if(nx >= FLEXVEC_MAX_EDGES) break;
        r->xs[nx] = x - (float)cx0;
        r->dirs[nx] = (int8_t)dir;
        nx++;
      }
      if(nx < 2) continue;
      for(int i = 1; i < nx; i++){       // insercion: la lista ya viene casi ordenada
        float xv = r->xs[i]; int8_t dv = r->dirs[i];
        int j = i - 1;
        while(j >= 0 && r->xs[j] > xv){ r->xs[j + 1] = r->xs[j]; r->dirs[j + 1] = r->dirs[j]; j--; }
        r->xs[j + 1] = xv; r->dirs[j + 1] = dv;
      }
      if(evenOdd){
        for(int i = 0; i + 1 < nx; i += 2)
          fvCovAdd(r->cov, cx1 - cx0 + 1, r->xs[i], r->xs[i + 1], wgti, &minX, &maxX);
      } else {
        int wind = 0;
        for(int i = 0; i + 1 < nx; i++){
          wind += r->dirs[i];
          if(wind != 0) fvCovAdd(r->cov, cx1 - cx0 + 1, r->xs[i], r->xs[i + 1], wgti, &minX, &maxX);
        }
      }
    }
    fvCovFlush(r->cov, r->covW, y, cx0, minX, maxX, cb, user);
    minX = r->covW; maxX = -1;
  }
  return FLEXVEC_OK;
}

int flexVecFillFlat(FlexVecRaster* r, int subN, const FlexVecView* v,
                    int evenOdd, FlexVecSpanCb cb, void* user){
  if(!r) return FLEXVEC_E_BADARG;
  return fvFillPolys(r, r->pts, r->subStart, r->subCount, subN, v, evenOdd, cb, user);
}

// -------------------------------------------------------------
//  TRAZO
//  ------------------------------------------------------------
//  El contorno del trazo se construye como UNION de piezas convexas --
//  un cuadrilatero por segmento, una pieza por union y una por extremo
//  -- y se rellena con la regla de NO-CERO, que es la que convierte esa
//  pila de piezas superpuestas en una sola silueta.
//
//  Para que la union funcione, TODAS las piezas tienen que girar en el
//  mismo sentido: si una va al reves, la regla de no-cero la RESTA y
//  aparece un agujero justo en la union. En vez de razonar el sentido
//  caso por caso (que depende de hacia donde gire el trazado y es de
//  donde salen esos agujeros), cada pieza se mide con su area con signo
//  y se invierte si hace falta. Es una pasada por pieza y quita la
//  clase entera de fallo.
//
//  CUANDO EL CONTORNO NO CABE se vuelca lo generado hasta ahora y se
//  sigue. La costura cae siempre en una UNION, no en mitad de un
//  segmento, asi que con un trazo opaco no se ve; con uno translucido
//  esa union se mezcla dos veces. Es el unico compromiso de esta
//  rutina, y es preferible a negarse a dibujar un trazado largo.
// -------------------------------------------------------------
typedef struct {
  FlexVecRaster* r;
  int np, ns;
} FvStrokeBuf;

static void fvSbReset(FvStrokeBuf* b){ b->np = 0; b->ns = 0; }

// Anade una pieza cerrada, orientandola como las demas.
static int fvSbPush(FvStrokeBuf* b, const float* xy, int n){
  if(n < 3) return 1;
  if(b->ns >= FLEXVEC_MAX_SUBS || b->np + n > FLEXVEC_MAX_FLATPTS) return 0;
  double area = 0;
  for(int i = 0; i < n; i++){
    int j = (i + 1) % n;
    area += (double)xy[i * 2] * xy[j * 2 + 1] - (double)xy[j * 2] * xy[i * 2 + 1];
  }
  float* dst = b->r->pts2 + b->np * 2;
  if(area > 0){                                  // al reves: se invierte
    for(int i = 0; i < n; i++){
      dst[i * 2]     = xy[(n - 1 - i) * 2];
      dst[i * 2 + 1] = xy[(n - 1 - i) * 2 + 1];
    }
  } else {
    memcpy(dst, xy, (size_t)n * 2 * sizeof(float));
  }
  b->r->sub2Start[b->ns] = b->np;
  b->r->sub2Count[b->ns] = n;
  b->ns++; b->np += n;
  return 1;
}

static void fvSbFlush(FvStrokeBuf* b, const FlexVecView* v, FlexVecSpanCb cb, void* user){
  if(b->ns > 0)
    fvFillPolys(b->r, b->r->pts2, b->r->sub2Start, b->r->sub2Count, b->ns, v, 0, cb, user);
  fvSbReset(b);
}

#define FV_JOIN_SEG 10                 // lados de una union/extremo redondo

static void fvDisc(float cx, float cy, float rad, float* out, int n){
  const float TAU = 6.28318530718f;
  for(int i = 0; i < n; i++){
    float a = -TAU * (float)i / (float)n;        // sentido horario, como los cuadrilateros
    out[i * 2]     = cx + rad * cosf(a);
    out[i * 2 + 1] = cy + rad * sinf(a);
  }
}

int flexVecStrokeFlat(FlexVecRaster* r, int subN, const FlexVecView* v,
                      float w, int cap, int join, FlexVecSpanCb cb, void* user){
  if(!r || !r->pts || !r->pts2 || !v || !cb) return FLEXVEC_E_BADARG;
  if(!fvFinite(w) || w <= 0) return FLEXVEC_OK;
  float hw = w * 0.5f;
  if(hw < 0.35f) hw = 0.35f;           // por debajo de esto una linea desaparece
  FvStrokeBuf b; b.r = r; fvSbReset(&b);
  float quad[8], disc[FV_JOIN_SEG * 2];
  for(int s = 0; s < subN; s++){
    int st = r->subStart[s], n = r->subCount[s];
    int closed = r->subClosed ? r->subClosed[s] : 0;
    if(n < 2) continue;
    int segs = closed ? n : n - 1;
    for(int k = 0; k < segs; k++){
      int a = st + k, c = st + ((k + 1) % n);
      float ax = r->pts[a * 2], ay = r->pts[a * 2 + 1];
      float bx = r->pts[c * 2], by = r->pts[c * 2 + 1];
      float dx = bx - ax, dy = by - ay;
      float len = sqrtf(dx * dx + dy * dy);
      if(len < 1e-5f) continue;
      float ux = dx / len, uy = dy / len;
      if(cap == FLEXVEC_CAP_SQUARE && !closed){
        if(k == 0){ ax -= ux * hw; ay -= uy * hw; }
        if(k == segs - 1){ bx += ux * hw; by += uy * hw; }
      }
      float nx = -uy * hw, ny = ux * hw;
      quad[0] = ax + nx; quad[1] = ay + ny;
      quad[2] = bx + nx; quad[3] = by + ny;
      quad[4] = bx - nx; quad[5] = by - ny;
      quad[6] = ax - nx; quad[7] = ay - ny;
      if(!fvSbPush(&b, quad, 4)){ fvSbFlush(&b, v, cb, user); fvSbPush(&b, quad, 4); }
      // Union con el segmento siguiente. Un disco cubre cualquier angulo
      // sin casos particulares; el bisel y el angulo se aproximan con el
      // mismo disco a menor resolucion, que a este tamano de pantalla es
      // indistinguible y no puede degenerar.
      int isLast = (k == segs - 1);
      if(!isLast || closed){
        int seg = (join == FLEXVEC_JOIN_ROUND) ? FV_JOIN_SEG : 6;
        fvDisc(bx, by, hw, disc, seg);
        if(!fvSbPush(&b, disc, seg)){ fvSbFlush(&b, v, cb, user); fvSbPush(&b, disc, seg); }
      }
    }
    if(!closed && cap == FLEXVEC_CAP_ROUND){
      float ex[2][2] = { { r->pts[st * 2], r->pts[st * 2 + 1] },
                         { r->pts[(st + n - 1) * 2], r->pts[(st + n - 1) * 2 + 1] } };
      for(int i = 0; i < 2; i++){
        fvDisc(ex[i][0], ex[i][1], hw, disc, FV_JOIN_SEG);
        if(!fvSbPush(&b, disc, FV_JOIN_SEG)){ fvSbFlush(&b, v, cb, user); fvSbPush(&b, disc, FV_JOIN_SEG); }
      }
    }
  }
  fvSbFlush(&b, v, cb, user);
  return FLEXVEC_OK;
}

int flexVecElemPixBBox(FlexVecDoc* d, int elem, const FlexVecView* v,
                       int* x0, int* y0, int* x1, int* y1){
  if(!fvElemValid(d, elem) || !v) return FLEXVEC_E_BADARG;
  float a, b, c, e;
  flexVecBBox(d, elem, &a, &b, &c, &e);
  // Las cuatro esquinas transformadas: con rotacion, la caja de la caja
  // NO es la caja transformada, y quedarse corto aqui deja restos en
  // pantalla al mover un objeto girado.
  float xs[4], ys[4];
  flexVecMatApply(v->m, a, b, &xs[0], &ys[0]);
  flexVecMatApply(v->m, c, b, &xs[1], &ys[1]);
  flexVecMatApply(v->m, c, e, &xs[2], &ys[2]);
  flexVecMatApply(v->m, a, e, &xs[3], &ys[3]);
  float mnx = xs[0], mxx = xs[0], mny = ys[0], mxy = ys[0];
  for(int i = 1; i < 4; i++){
    mnx = fvMin(mnx, xs[i]); mxx = fvMax(mxx, xs[i]);
    mny = fvMin(mny, ys[i]); mxy = fvMax(mxy, ys[i]);
  }
  // Margen del trazo, ya en pixeles, mas 2 px de antialias.
  float sw = 0;
  if(d->elems[elem].stroke.type != FLEXVEC_P_NONE){
    float ux, uy;
    flexVecMatApplyVec(v->m, 1.0f, 0.0f, &ux, &uy);
    float sc = sqrtf(ux * ux + uy * uy);
    sw = d->elems[elem].strokeW * 0.5f * sc;
  }
  float pad = sw + 2.0f;
  if(x0) *x0 = (int)floorf(mnx - pad);
  if(y0) *y0 = (int)floorf(mny - pad);
  if(x1) *x1 = (int)ceilf(mxx + pad);
  if(y1) *y1 = (int)ceilf(mxy + pad);
  return FLEXVEC_OK;
}

int flexVecRenderElem(FlexVecDoc* d, int elem, const FlexVecView* v,
                      FlexVecRaster* r, FlexVecSpanCb fillCb, FlexVecSpanCb strokeCb,
                      void* user){
  if(!fvElemValid(d, elem) || !v || !r) return FLEXVEC_E_BADARG;
  const FlexVecElem* el = &d->elems[elem];
  if(el->flags & FLEXVEC_EF_HIDDEN) return FLEXVEC_OK;
  if(el->layer >= 0 && !d->layers[el->layer].visible) return FLEXVEC_OK;
  int subN = 0;
  int np = flexVecFlatten(d, elem, v, r, &subN);
  if(np <= 0 || subN <= 0) return FLEXVEC_OK;
  if(el->fill.type != FLEXVEC_P_NONE && fillCb)
    fvFillPolys(r, r->pts, r->subStart, r->subCount, subN, v, 0, fillCb, user);
  if(el->stroke.type != FLEXVEC_P_NONE && strokeCb && el->strokeW > 0){
    // El grosor se da en unidades de DOCUMENTO y hay que llevarlo a
    // pixeles con la escala de la vista COMPUESTA con la del objeto: un
    // objeto escalado al doble tiene el trazo al doble, como en
    // Illustrator con "Escalar trazos y efectos".
    float cm[6];
    flexVecMatMul(v->m, el->m, cm);
    float ux, uy, vx, vy;
    flexVecMatApplyVec(cm, 1.0f, 0.0f, &ux, &uy);
    flexVecMatApplyVec(cm, 0.0f, 1.0f, &vx, &vy);
    float sc = sqrtf(fvAbs(ux * vy - uy * vx));      // media geometrica de la escala
    if(!fvFinite(sc) || sc <= 0) sc = 1.0f;
    flexVecStrokeFlat(r, subN, v, el->strokeW * sc, el->cap, el->join, strokeCb, user);
  }
  return FLEXVEC_OK;
}

// #############################################################
//  TEXTO PUNTUAL
//  ------------------------------------------------------------
//  El nucleo guarda la CADENA y una CAJA; no rasteriza ni una letra.
//  No puede: la unica fuente del sistema es un atlas 4bpp sin
//  contornos, asi que no hay curvas que convertir. El anfitrion dibuja
//  el texto con drawText() y corrige la caja con flexVecTextSetBox()
//  despues de medirlo con textW(); la caja es lo que usan la seleccion,
//  la prueba de impacto y el SVG.
//
//  Por eso el SVG lleva un <text>, no contornos: exportar un texto como
//  curvas cuando no se tienen las curvas seria inventarselas.
// #############################################################
int flexVecAddText(FlexVecDoc* d, float x, float y, const char* utf8, float size){
  if(!d || !d->arena.text || !utf8) return -FLEXVEC_E_BADARG;
  if(!fvFinite(x) || !fvFinite(y) || !fvFinite(size) || size <= 0) return -FLEXVEC_E_BADARG;
  size_t len = strlen(utf8);
  if(len == 0) return -FLEXVEC_E_BADARG;
  if(len > 255) len = 255;
  if(d->textTop + len + 1 > FLEXVEC_TEXT_BYTES) return -FLEXVEC_E_FULL_TEXT;
  int owned = fvAutoBegin(d, "Texto");
  int e = fvElemNew(d, FLEXVEC_K_TEXT, 4);
  if(e < 0){ fvAutoEnd(d, owned, e); return e; }
  fvUndoTouch(d, e);
  int off = (int)d->textTop;
  memcpy(d->arena.text + off, utf8, len);
  d->arena.text[off + len] = 0;
  d->textTop = (uint16_t)(off + len + 1);
  d->elems[e].n1 = (int16_t)off;
  d->elems[e].n0 = (int16_t)len;
  d->elems[e].p0 = size;
  // Caja provisional: 0,6 em de ancho por caracter. La corrige el
  // anfitrion en cuanto mide de verdad; hasta entonces la seleccion ya
  // tiene algo razonable que agarrar.
  float w = size * 0.6f * (float)len, h = size;
  FlexVecNode* nd = d->arena.nodes + d->elems[e].first;
  fvNodeSet(&nd[0], x,     y - h, FLEXVEC_N_START | FLEXVEC_N_CLOSE);
  fvNodeSet(&nd[1], x + w, y - h, 0);
  fvNodeSet(&nd[2], x + w, y,     0);
  fvNodeSet(&nd[3], x,     y,     0);
  d->elems[e].fill.type = FLEXVEC_P_SOLID;
  d->elems[e].fill.rgb = 0x101018;
  d->elems[e].stroke.type = FLEXVEC_P_NONE;
  d->elems[e].bboxOk = 0;
  fvAutoEnd(d, owned, FLEXVEC_OK);
  return e;
}

int flexVecTextSetBox(FlexVecDoc* d, int elem, float w, float h){
  if(!fvElemValid(d, elem) || d->elems[elem].kind != FLEXVEC_K_TEXT) return FLEXVEC_E_BADARG;
  if(!fvFinite(w) || !fvFinite(h) || w <= 0 || h <= 0) return FLEXVEC_E_BADARG;
  if(d->elems[elem].count < 4) return FLEXVEC_E_BADARG;
  FlexVecNode* nd = d->arena.nodes + d->elems[elem].first;
  float x = nd[0].x, y = nd[3].y;               // ancla = esquina inferior izquierda
  fvNodeSet(&nd[0], x,     y - h, FLEXVEC_N_START | FLEXVEC_N_CLOSE);
  fvNodeSet(&nd[1], x + w, y - h, 0);
  fvNodeSet(&nd[2], x + w, y,     0);
  fvNodeSet(&nd[3], x,     y,     0);
  d->elems[elem].bboxOk = 0;
  return FLEXVEC_OK;                            // medir NO es editar: fuera del diario
}
const char* flexVecTextStr(const FlexVecDoc* d, int elem){
  if(!d || !d->arena.text || elem < 0 || elem >= FLEXVEC_MAX_ELEMS) return "";
  const FlexVecElem* el = &d->elems[elem];
  if(el->layer < 0 || el->kind != FLEXVEC_K_TEXT) return "";
  if(el->n1 < 0 || el->n1 >= FLEXVEC_TEXT_BYTES) return "";
  return d->arena.text + el->n1;
}
float flexVecTextSize(const FlexVecDoc* d, int elem){
  if(!d || elem < 0 || elem >= FLEXVEC_MAX_ELEMS) return 0;
  return d->elems[elem].p0;
}

// #############################################################
//  GRADIENTES
//  ------------------------------------------------------------
//  LA TABLA ES LO QUE LOS HACE VIABLES. Interpolar entre paradas por
//  pixel serian dos multiplicaciones y una division por canal y por
//  pixel; con la tabla de 256 entradas es un indice y una lectura. La
//  tabla se recalcula solo cuando se toca una parada, no por cuadro.
// #############################################################
static void fvGradBuildLut(FlexVecGrad* g){
  if(g->nstops < 1){ g->lutReady = 1; return; }
  for(int i = 0; i < 256; i++){
    float t = (float)i / 255.0f;
    int a = 0;
    while(a + 1 < g->nstops && g->stops[a + 1].t <= t) a++;
    int b = (a + 1 < g->nstops) ? a + 1 : a;
    float t0 = g->stops[a].t, t1 = g->stops[b].t;
    float u = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;
    u = fvClampF(u, 0.0f, 1.0f);
    uint32_t c0 = g->stops[a].rgb, c1 = g->stops[b].rgb;
    int r = (int)(((c0 >> 16) & 0xFF) + (((int)((c1 >> 16) & 0xFF) - (int)((c0 >> 16) & 0xFF)) * u));
    int gg = (int)(((c0 >> 8)  & 0xFF) + (((int)((c1 >> 8)  & 0xFF) - (int)((c0 >> 8)  & 0xFF)) * u));
    int bb = (int)(( c0        & 0xFF) + (((int)( c1        & 0xFF) - (int)( c0        & 0xFF)) * u));
    int al = (int)(g->stops[a].alpha + ((int)g->stops[b].alpha - (int)g->stops[a].alpha) * u);
    g->lut[i] = ((uint32_t)(r & 0xFF) << 16) | ((uint32_t)(gg & 0xFF) << 8) | (uint32_t)(bb & 0xFF);
    g->lutA[i] = (uint8_t)(al < 0 ? 0 : (al > 255 ? 255 : al));
  }
  g->lutReady = 1;
}

int flexVecGradAdd(FlexVecDoc* d, int type){
  if(!d) return -FLEXVEC_E_BADARG;
  if(type != FLEXVEC_G_LINEAR && type != FLEXVEC_G_RADIAL) return -FLEXVEC_E_BADARG;
  for(int i = 0; i < FLEXVEC_MAX_GRADS; i++){
    if(d->grads[i].used) continue;
    FlexVecGrad* g = &d->grads[i];
    memset(g, 0, sizeof(*g));
    g->used = 1; g->type = (uint8_t)type;
    g->x0 = 0; g->y0 = 0; g->x1 = 100; g->y1 = 0;
    g->nstops = 2;
    g->stops[0].t = 0.0f; g->stops[0].rgb = 0xFFFFFF; g->stops[0].alpha = 255;
    g->stops[1].t = 1.0f; g->stops[1].rgb = 0x3C6EF0; g->stops[1].alpha = 255;
    fvGradBuildLut(g);
    if(i >= d->gradN) d->gradN = i + 1;
    return i;
  }
  return -FLEXVEC_E_FULL_GRADS;
}
static int fvGradValid(const FlexVecDoc* d, int g){
  return d && g >= 0 && g < FLEXVEC_MAX_GRADS && d->grads[g].used;
}
int flexVecGradSetAxis(FlexVecDoc* d, int g, float x0, float y0, float x1, float y1){
  if(!fvGradValid(d, g)) return FLEXVEC_E_BADARG;
  if(!fvFinite(x0) || !fvFinite(y0) || !fvFinite(x1) || !fvFinite(y1)) return FLEXVEC_E_BADARG;
  d->grads[g].x0 = x0; d->grads[g].y0 = y0; d->grads[g].x1 = x1; d->grads[g].y1 = y1;
  d->dirty = 1;
  return FLEXVEC_OK;
}
int flexVecGradSetStop(FlexVecDoc* d, int g, int i, float t, uint32_t rgb, uint8_t alpha){
  if(!fvGradValid(d, g)) return FLEXVEC_E_BADARG;
  FlexVecGrad* gr = &d->grads[g];
  if(i < 0 || i >= gr->nstops) return FLEXVEC_E_BADARG;
  gr->stops[i].t = fvClampF(fvSafe(t), 0.0f, 1.0f);
  gr->stops[i].rgb = rgb & 0xFFFFFFu;
  gr->stops[i].alpha = alpha;
  // Las paradas se mantienen ORDENADAS por t: la construccion de la
  // tabla recorre en orden y una parada fuera de sitio daria una franja
  // de color plano donde deberia haber degradado.
  for(int k = 1; k < gr->nstops; k++){
    FlexVecStop v = gr->stops[k];
    int j = k - 1;
    while(j >= 0 && gr->stops[j].t > v.t){ gr->stops[j + 1] = gr->stops[j]; j--; }
    gr->stops[j + 1] = v;
  }
  fvGradBuildLut(gr);
  d->dirty = 1;
  return FLEXVEC_OK;
}
int flexVecGradAddStop(FlexVecDoc* d, int g, float t, uint32_t rgb, uint8_t alpha){
  if(!fvGradValid(d, g)) return FLEXVEC_E_BADARG;
  FlexVecGrad* gr = &d->grads[g];
  if(gr->nstops >= FLEXVEC_MAX_STOPS) return FLEXVEC_E_FULL_GRADS;
  int i = gr->nstops++;
  gr->stops[i].t = 0; gr->stops[i].rgb = 0; gr->stops[i].alpha = 255;
  return flexVecGradSetStop(d, g, i, t, rgb, alpha);
}
int flexVecGradDelStop(FlexVecDoc* d, int g, int i){
  if(!fvGradValid(d, g)) return FLEXVEC_E_BADARG;
  FlexVecGrad* gr = &d->grads[g];
  if(i < 0 || i >= gr->nstops) return FLEXVEC_E_BADARG;
  if(gr->nstops <= 2) return FLEXVEC_E_BADARG;    // un gradiente sin dos paradas no es un gradiente
  for(int k = i; k + 1 < gr->nstops; k++) gr->stops[k] = gr->stops[k + 1];
  gr->nstops--;
  fvGradBuildLut(gr);
  d->dirty = 1;
  return FLEXVEC_OK;
}
int flexVecSetFillGrad(FlexVecDoc* d, int elem, int g){
  int rc = fvEditable(d, elem);
  if(rc != FLEXVEC_OK) return rc;
  if(!fvGradValid(d, g)) return FLEXVEC_E_BADARG;
  int owned = fvAutoBegin(d, "Gradiente");
  fvUndoTouch(d, elem);
  d->elems[elem].fill.type = FLEXVEC_P_GRAD;
  d->elems[elem].fill.ref = (int8_t)g;
  d->dirty = 1;
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}

float flexVecGradAt(const FlexVecGrad* g, float x, float y){
  if(!g) return 0;
  if(g->type == FLEXVEC_G_RADIAL){
    float dx = x - g->x0, dy = y - g->y0;
    float rad = g->x1;
    if(rad <= 1e-5f) return 0;
    float t = sqrtf(dx * dx + dy * dy) / rad;
    return fvClampF(t, 0.0f, 1.0f);
  }
  float ax = g->x1 - g->x0, ay = g->y1 - g->y0;
  float len2 = ax * ax + ay * ay;
  if(len2 <= 1e-9f) return 0;
  float t = ((x - g->x0) * ax + (y - g->y0) * ay) / len2;
  return fvClampF(t, 0.0f, 1.0f);
}

int flexVecGradRow(const FlexVecGrad* g, float x, float y, float* t0, float* dt){
  if(!g || g->type != FLEXVEC_G_LINEAR) return 0;
  float ax = g->x1 - g->x0, ay = g->y1 - g->y0;
  float len2 = ax * ax + ay * ay;
  if(len2 <= 1e-9f) return 0;
  if(t0) *t0 = ((x - g->x0) * ax + (y - g->y0) * ay) / len2;
  if(dt) *dt = ax / len2;              // avanzar un pixel en x es SUMAR esto
  return 1;
}

// #############################################################
//  EXPORTACION SVG
//  ------------------------------------------------------------
//  SVG es texto: se serializa sin reservar nada, se abre en cualquier
//  navegador y no hay un codificador binario que probar. Para un
//  dispositivo de 16 MB de NOR Flash sin ranura de tarjeta es
//  exactamente el formato de intercambio que hace falta.
//
//  TODA escritura pasa por fvW*(), que NUNCA escribe fuera del buffer:
//  si no cabe, se marca desbordamiento y se sigue CONTANDO. Asi la
//  llamada devuelve un error honesto en vez de un SVG truncado que
//  parece valido hasta que alguien intenta abrirlo.
// #############################################################
typedef struct { char* buf; size_t cap, len; int over; } FvOut;

static void fvWs(FvOut* o, const char* s){
  size_t n = strlen(s);
  if(o->len + n >= o->cap){ o->over = 1; o->len += n; return; }
  memcpy(o->buf + o->len, s, n);
  o->len += n;
}
static void fvWc(FvOut* o, char c){
  if(o->len + 1 >= o->cap){ o->over = 1; o->len++; return; }
  o->buf[o->len++] = c;
}
// Numero con hasta 3 decimales y sin ceros de relleno. Tres decimales
// son ~1/1000 de unidad de documento: por debajo del pixel a cualquier
// zoom razonable, y recorta el archivo a la mitad frente a "%f".
static void fvWf(FvOut* o, float v){
  if(!fvFinite(v)) v = 0;
  char tmp[32];
  snprintf(tmp, sizeof(tmp), "%.3f", (double)v);
  char* p = tmp;
  if(strchr(tmp, '.')){
    char* e = tmp + strlen(tmp) - 1;
    while(e > tmp && *e == '0') *e-- = 0;
    if(e > tmp && *e == '.') *e = 0;
  }
  if(!strcmp(p, "-0")) p = (char*)"0";
  fvWs(o, p);
}
static void fvWi(FvOut* o, int v){
  char tmp[16];
  snprintf(tmp, sizeof(tmp), "%d", v);
  fvWs(o, tmp);
}
static void fvWhex(FvOut* o, uint32_t rgb){
  static const char H[] = "0123456789abcdef";
  fvWc(o, '#');
  for(int i = 20; i >= 0; i -= 4) fvWc(o, H[(rgb >> i) & 0xF]);
}
// Texto XML: los cinco caracteres que rompen un documento se escapan.
// Sin esto, un titulo con "<" produce un SVG que ningun visor abre.
static void fvWxml(FvOut* o, const char* s){
  for(const unsigned char* p = (const unsigned char*)s; *p; p++){
    switch(*p){
      case '&':  fvWs(o, "&amp;");  break;
      case '<':  fvWs(o, "&lt;");   break;
      case '>':  fvWs(o, "&gt;");   break;
      case '"':  fvWs(o, "&quot;"); break;
      case '\'': fvWs(o, "&apos;"); break;
      default:   fvWc(o, (char)*p); break;
    }
  }
}

static void fvWPaint(FvOut* o, const FlexVecPaint* p, const char* which, int compact){
  fvWc(o, ' '); fvWs(o, which); fvWs(o, "=\"");
  if(p->type == FLEXVEC_P_NONE) fvWs(o, "none");
  else if(p->type == FLEXVEC_P_GRAD && p->ref >= 0){
    fvWs(o, "url(#g"); fvWi(o, p->ref); fvWc(o, ')');
  } else fvWhex(o, p->rgb);
  fvWc(o, '"');
  if(p->type != FLEXVEC_P_NONE && p->alpha != 255){
    fvWc(o, ' '); fvWs(o, which); fvWs(o, "-opacity=\"");
    fvWf(o, (float)p->alpha / 255.0f);
    fvWc(o, '"');
  }
  (void)compact;
}

// La geometria, en el atributo "d". Se emite 'L' cuando los dos
// tiradores coinciden con sus anclas: una recta escrita como cubica
// ocupa el triple y no se ve distinta.
static void fvWPathData(FvOut* o, const FlexVecDoc* d, const FlexVecElem* el){
  const FlexVecNode* nd = d->arena.nodes + el->first;
  int i = 0;
  while(i < (int)el->count){
    int closed = 0;
    int n = fvSubLen(nd, el->count, i, &closed);
    if(n < 2){ i += n; continue; }
    fvWc(o, 'M'); fvWf(o, nd[i].x); fvWc(o, ' '); fvWf(o, nd[i].y);
    int segs = closed ? n : n - 1;
    for(int k = 0; k < segs; k++){
      const FlexVecNode* A = &nd[i + k];
      const FlexVecNode* B = &nd[i + ((k + 1) % n)];
      int straight = (A->ox == A->x && A->oy == A->y && B->ix == B->x && B->iy == B->y);
      if(closed && k == segs - 1 && straight) break;   // la 'Z' ya traza esa recta
      if(straight){
        fvWs(o, " L"); fvWf(o, B->x); fvWc(o, ' '); fvWf(o, B->y);
      } else {
        fvWs(o, " C"); fvWf(o, A->ox); fvWc(o, ' '); fvWf(o, A->oy);
        fvWc(o, ' ');  fvWf(o, B->ix); fvWc(o, ' '); fvWf(o, B->iy);
        fvWc(o, ' ');  fvWf(o, B->x);  fvWc(o, ' '); fvWf(o, B->y);
      }
    }
    if(closed) fvWs(o, " Z");
    i += n;
  }
}

static void fvWTransform(FvOut* o, const float* m){
  if(m[0] == 1 && m[1] == 0 && m[2] == 0 && m[3] == 1 && m[4] == 0 && m[5] == 0) return;
  fvWs(o, " transform=\"matrix(");
  for(int i = 0; i < 6; i++){ if(i) fvWc(o, ','); fvWf(o, m[i]); }
  fvWs(o, ")\"");
}

static int fvExportSVG(FlexVecDoc* d, char* out, size_t cap, int selOnly, int compact){
  if(!d || !out || cap < 64 || !d->arena.nodes) return -FLEXVEC_E_BADARG;
  FvOut o; o.buf = out; o.cap = cap; o.len = 0; o.over = 0;
  const char* NL = compact ? "" : "\n";
  fvWs(&o, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"); fvWs(&o, NL);
  fvWs(&o, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"");
  fvWf(&o, d->artW); fvWs(&o, "\" height=\""); fvWf(&o, d->artH);
  fvWs(&o, "\" viewBox=\"0 0 "); fvWf(&o, d->artW); fvWc(&o, ' '); fvWf(&o, d->artH);
  fvWs(&o, "\">"); fvWs(&o, NL);

  // Gradientes usados. Solo se emiten los que alguna pintura referencia:
  // un <defs> con gradientes huerfanos engorda el archivo sin pintar nada.
  int used[FLEXVEC_MAX_GRADS];
  memset(used, 0, sizeof(used));
  for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++){
    if(!fvElemValid(d, e)) continue;
    if(selOnly && !(d->elems[e].flags & FLEXVEC_EF_SEL)) continue;
    const FlexVecElem* el = &d->elems[e];
    if(el->fill.type == FLEXVEC_P_GRAD && el->fill.ref >= 0 && el->fill.ref < FLEXVEC_MAX_GRADS)
      used[el->fill.ref] = 1;
    if(el->stroke.type == FLEXVEC_P_GRAD && el->stroke.ref >= 0 && el->stroke.ref < FLEXVEC_MAX_GRADS)
      used[el->stroke.ref] = 1;
  }
  int anyGrad = 0;
  for(int g = 0; g < FLEXVEC_MAX_GRADS; g++) if(used[g] && d->grads[g].used) anyGrad = 1;
  if(anyGrad){
    fvWs(&o, "<defs>"); fvWs(&o, NL);
    for(int g = 0; g < FLEXVEC_MAX_GRADS; g++){
      if(!used[g] || !d->grads[g].used) continue;
      const FlexVecGrad* gr = &d->grads[g];
      if(gr->type == FLEXVEC_G_RADIAL){
        fvWs(&o, "<radialGradient id=\"g"); fvWi(&o, g);
        fvWs(&o, "\" gradientUnits=\"userSpaceOnUse\" cx=\""); fvWf(&o, gr->x0);
        fvWs(&o, "\" cy=\""); fvWf(&o, gr->y0);
        fvWs(&o, "\" r=\""); fvWf(&o, gr->x1); fvWs(&o, "\">");
      } else {
        fvWs(&o, "<linearGradient id=\"g"); fvWi(&o, g);
        fvWs(&o, "\" gradientUnits=\"userSpaceOnUse\" x1=\""); fvWf(&o, gr->x0);
        fvWs(&o, "\" y1=\""); fvWf(&o, gr->y0);
        fvWs(&o, "\" x2=\""); fvWf(&o, gr->x1);
        fvWs(&o, "\" y2=\""); fvWf(&o, gr->y1); fvWs(&o, "\">");
      }
      for(int i = 0; i < gr->nstops; i++){
        fvWs(&o, "<stop offset=\""); fvWf(&o, gr->stops[i].t);
        fvWs(&o, "\" stop-color=\""); fvWhex(&o, gr->stops[i].rgb); fvWc(&o, '"');
        if(gr->stops[i].alpha != 255){
          fvWs(&o, " stop-opacity=\""); fvWf(&o, (float)gr->stops[i].alpha / 255.0f); fvWc(&o, '"');
        }
        fvWs(&o, "/>");
      }
      fvWs(&o, gr->type == FLEXVEC_G_RADIAL ? "</radialGradient>" : "</linearGradient>");
      fvWs(&o, NL);
    }
    fvWs(&o, "</defs>"); fvWs(&o, NL);
  }

  int16_t order[FLEXVEC_MAX_ELEMS];
  int n = flexVecPaintOrder(d, order, FLEXVEC_MAX_ELEMS);
  int curLayer = -1;
  for(int k = 0; k < n; k++){
    int e = order[k];
    const FlexVecElem* el = &d->elems[e];
    if(selOnly && !(el->flags & FLEXVEC_EF_SEL)) continue;
    if(el->count < 2) continue;
    if(el->layer != curLayer){
      if(curLayer >= 0){ fvWs(&o, "</g>"); fvWs(&o, NL); }
      fvWs(&o, "<g id=\""); fvWxml(&o, d->layers[el->layer].name); fvWs(&o, "\">"); fvWs(&o, NL);
      curLayer = el->layer;
    }
    if(el->kind == FLEXVEC_K_TEXT){
      const FlexVecNode* nd = d->arena.nodes + el->first;
      fvWs(&o, "<text x=\""); fvWf(&o, nd[0].x);
      fvWs(&o, "\" y=\""); fvWf(&o, nd[3].y);
      fvWs(&o, "\" font-size=\""); fvWf(&o, el->p0);
      fvWs(&o, "\" font-family=\"Outfit, system-ui, sans-serif\"");
      fvWPaint(&o, &el->fill, "fill", compact);
      fvWTransform(&o, el->m);
      if(el->alpha != 255){ fvWs(&o, " opacity=\""); fvWf(&o, (float)el->alpha / 255.0f); fvWc(&o, '"'); }
      fvWc(&o, '>');
      fvWxml(&o, flexVecTextStr(d, e));
      fvWs(&o, "</text>"); fvWs(&o, NL);
      continue;
    }
    fvWs(&o, "<path d=\"");
    fvWPathData(&o, d, el);
    fvWc(&o, '"');
    fvWPaint(&o, &el->fill, "fill", compact);
    if(el->stroke.type != FLEXVEC_P_NONE){
      fvWPaint(&o, &el->stroke, "stroke", compact);
      fvWs(&o, " stroke-width=\""); fvWf(&o, el->strokeW); fvWc(&o, '"');
      if(el->cap == FLEXVEC_CAP_ROUND)  fvWs(&o, " stroke-linecap=\"round\"");
      if(el->cap == FLEXVEC_CAP_SQUARE) fvWs(&o, " stroke-linecap=\"square\"");
      if(el->join == FLEXVEC_JOIN_ROUND) fvWs(&o, " stroke-linejoin=\"round\"");
      if(el->join == FLEXVEC_JOIN_BEVEL) fvWs(&o, " stroke-linejoin=\"bevel\"");
    }
    if(el->alpha != 255){ fvWs(&o, " opacity=\""); fvWf(&o, (float)el->alpha / 255.0f); fvWc(&o, '"'); }
    fvWTransform(&o, el->m);
    fvWs(&o, "/>"); fvWs(&o, NL);
  }
  if(curLayer >= 0){ fvWs(&o, "</g>"); fvWs(&o, NL); }
  fvWs(&o, "</svg>"); fvWs(&o, NL);
  if(o.over) return -FLEXVEC_E_OVERFLOW;
  o.buf[o.len] = 0;
  return (int)o.len;
}

int flexVecExportSVG(FlexVecDoc* d, char* out, size_t cap, int selOnly){
  return fvExportSVG(d, out, cap, selOnly, 0);
}
int flexVecExportSVGCompact(FlexVecDoc* d, char* out, size_t cap, int selOnly){
  return fvExportSVG(d, out, cap, selOnly, 1);
}

// #############################################################
//  SERIALIZACION NATIVA (.fxv)
//  ------------------------------------------------------------
//  Volcado directo de las estructuras, con cabecera propia. Lo usan la
//  SESION de la app (para que cerrar y volver no pierda el trabajo) y el
//  guardado en /Vector. La cabecera lleva los tamanos de las
//  estructuras: un archivo escrito por otra version del formato se
//  RECHAZA en vez de interpretarse mal, que es como se pierde un
//  documento sin enterarse.
// #############################################################
typedef struct {
  uint32_t magic, version;
  uint16_t elemSize, nodeSize, layerSize, gradSize;
  uint16_t layerN, elemN, gradN, nodeN;
  uint16_t textN, pad;
  float    artW, artH;
  char     name[FLEXVEC_NAME_MAX];
  int32_t  layerActive;
} FvFileHdr;

size_t flexVecSerializeSize(const FlexVecDoc* d){
  if(!d) return 0;
  return sizeof(FvFileHdr)
       + (size_t)FLEXVEC_MAX_LAYERS * sizeof(FlexVecLayer)
       + (size_t)FLEXVEC_MAX_ELEMS  * sizeof(FlexVecElem)
       + (size_t)d->nodeTop * sizeof(FlexVecNode)
       + (size_t)d->gradN   * sizeof(FlexVecGrad)
       + (size_t)d->textTop;
}

int flexVecSerialize(const FlexVecDoc* d, uint8_t* out, size_t cap){
  if(!d || !out) return -FLEXVEC_E_BADARG;
  size_t need = flexVecSerializeSize(d);
  if(need > cap) return -FLEXVEC_E_OVERFLOW;
  FvFileHdr h; memset(&h, 0, sizeof(h));
  h.magic = FLEXVEC_MAGIC; h.version = FLEXVEC_VERSION;
  h.elemSize = (uint16_t)sizeof(FlexVecElem);
  h.nodeSize = (uint16_t)sizeof(FlexVecNode);
  h.layerSize = (uint16_t)sizeof(FlexVecLayer);
  h.gradSize = (uint16_t)sizeof(FlexVecGrad);
  h.layerN = FLEXVEC_MAX_LAYERS; h.elemN = FLEXVEC_MAX_ELEMS;
  h.gradN = (uint16_t)d->gradN; h.nodeN = d->nodeTop; h.textN = d->textTop;
  h.artW = d->artW; h.artH = d->artH;
  h.layerActive = d->layerActive;
  memcpy(h.name, d->name, sizeof(h.name));
  uint8_t* p = out;
  memcpy(p, &h, sizeof(h)); p += sizeof(h);
  memcpy(p, d->layers, (size_t)FLEXVEC_MAX_LAYERS * sizeof(FlexVecLayer));
  p += (size_t)FLEXVEC_MAX_LAYERS * sizeof(FlexVecLayer);
  memcpy(p, d->elems, (size_t)FLEXVEC_MAX_ELEMS * sizeof(FlexVecElem));
  p += (size_t)FLEXVEC_MAX_ELEMS * sizeof(FlexVecElem);
  if(d->nodeTop && d->arena.nodes){
    memcpy(p, d->arena.nodes, (size_t)d->nodeTop * sizeof(FlexVecNode));
    p += (size_t)d->nodeTop * sizeof(FlexVecNode);
  }
  if(d->gradN){
    memcpy(p, d->grads, (size_t)d->gradN * sizeof(FlexVecGrad));
    p += (size_t)d->gradN * sizeof(FlexVecGrad);
  }
  if(d->textTop && d->arena.text){
    memcpy(p, d->arena.text, (size_t)d->textTop);
    p += (size_t)d->textTop;
  }
  return (int)(p - out);
}

int flexVecDeserialize(FlexVecDoc* d, const uint8_t* in, size_t len){
  if(!d || !in) return FLEXVEC_E_BADARG;
  if(!d->arena.nodes || !d->arena.text) return FLEXVEC_E_NOMEM;
  if(len < sizeof(FvFileHdr)) return FLEXVEC_E_BADARG;
  FvFileHdr h; memcpy(&h, in, sizeof(h));
  if(h.magic != FLEXVEC_MAGIC || h.version != FLEXVEC_VERSION) return FLEXVEC_E_BADARG;
  if(h.elemSize != sizeof(FlexVecElem) || h.nodeSize != sizeof(FlexVecNode) ||
     h.layerSize != sizeof(FlexVecLayer) || h.gradSize != sizeof(FlexVecGrad))
    return FLEXVEC_E_BADARG;
  if(h.layerN != FLEXVEC_MAX_LAYERS || h.elemN != FLEXVEC_MAX_ELEMS) return FLEXVEC_E_BADARG;
  if(h.nodeN > FLEXVEC_MAX_NODES || h.textN > FLEXVEC_TEXT_BYTES ||
     h.gradN > FLEXVEC_MAX_GRADS) return FLEXVEC_E_BADARG;
  size_t need = sizeof(FvFileHdr)
              + (size_t)FLEXVEC_MAX_LAYERS * sizeof(FlexVecLayer)
              + (size_t)FLEXVEC_MAX_ELEMS  * sizeof(FlexVecElem)
              + (size_t)h.nodeN * sizeof(FlexVecNode)
              + (size_t)h.gradN * sizeof(FlexVecGrad)
              + (size_t)h.textN;
  if(len < need) return FLEXVEC_E_BADARG;
  FlexVecArena a = d->arena;
  memset(d, 0, sizeof(*d));
  d->arena = a;
  d->artW = fvFinite(h.artW) && h.artW > 0 ? h.artW : 480;
  d->artH = fvFinite(h.artH) && h.artH > 0 ? h.artH : 800;
  memcpy(d->name, h.name, sizeof(d->name));
  d->name[FLEXVEC_NAME_MAX - 1] = 0;
  const uint8_t* p = in + sizeof(FvFileHdr);
  memcpy(d->layers, p, (size_t)FLEXVEC_MAX_LAYERS * sizeof(FlexVecLayer));
  p += (size_t)FLEXVEC_MAX_LAYERS * sizeof(FlexVecLayer);
  memcpy(d->elems, p, (size_t)FLEXVEC_MAX_ELEMS * sizeof(FlexVecElem));
  p += (size_t)FLEXVEC_MAX_ELEMS * sizeof(FlexVecElem);
  if(h.nodeN){ memcpy(a.nodes, p, (size_t)h.nodeN * sizeof(FlexVecNode)); p += (size_t)h.nodeN * sizeof(FlexVecNode); }
  if(h.gradN){ memcpy(d->grads, p, (size_t)h.gradN * sizeof(FlexVecGrad)); p += (size_t)h.gradN * sizeof(FlexVecGrad); }
  if(h.textN){ memcpy(a.text, p, (size_t)h.textN); p += (size_t)h.textN; }
  d->nodeTop = h.nodeN; d->textTop = h.textN; d->gradN = h.gradN;
  d->layerN = FLEXVEC_MAX_LAYERS;
  d->layerActive = (h.layerActive >= 0 && h.layerActive < FLEXVEC_MAX_LAYERS) ? h.layerActive : 0;
  // SANEADO. Un archivo puede venir de un corte de corriente o de una
  // memoria con un bit cambiado. Todo indice se comprueba contra los
  // limites REALES antes de que nadie lo use: un 'first' fuera de rango
  // es una lectura fuera del pool en el primer repintado.
  for(int i = 0; i < FLEXVEC_MAX_ELEMS; i++){
    FlexVecElem* el = &d->elems[i];
    if(el->layer < -1 || el->layer >= FLEXVEC_MAX_LAYERS){ el->layer = -1; el->count = 0; continue; }
    if(el->layer >= 0 && !d->layers[el->layer].used){ el->layer = -1; el->count = 0; continue; }
    if(el->layer < 0){ el->count = 0; continue; }
    if(el->count > FLEXVEC_MAX_NODES_PATH ||
       (uint32_t)el->first + el->count > (uint32_t)h.nodeN){ el->layer = -1; el->count = 0; continue; }
    if(el->kind > FLEXVEC_K_SYMBOL) el->kind = FLEXVEC_K_PATH;
    if(el->cap > FLEXVEC_CAP_SQUARE) el->cap = FLEXVEC_CAP_BUTT;
    if(el->join > FLEXVEC_JOIN_BEVEL) el->join = FLEXVEC_JOIN_MITER;
    if(el->fill.type > FLEXVEC_P_PATTERN) el->fill.type = FLEXVEC_P_NONE;
    if(el->stroke.type > FLEXVEC_P_PATTERN) el->stroke.type = FLEXVEC_P_NONE;
    if(el->fill.ref >= (int)h.gradN) el->fill.type = FLEXVEC_P_SOLID;
    if(el->stroke.ref >= (int)h.gradN) el->stroke.type = FLEXVEC_P_SOLID;
    if(!fvFinite(el->strokeW) || el->strokeW < 0) el->strokeW = 1.0f;
    for(int k = 0; k < 6; k++) if(!fvFinite(el->m[k])) flexVecMatIdentity(el->m);
    if(el->kind == FLEXVEC_K_TEXT && (el->n1 < 0 || el->n1 >= (int)h.textN)) el->kind = FLEXVEC_K_PATH;
    el->flags &= (uint8_t)(FLEXVEC_EF_SEL | FLEXVEC_EF_HIDDEN | FLEXVEC_EF_LOCKED);
    el->bboxOk = 0;
    if(i >= d->elemN) d->elemN = i + 1;
  }
  for(int i = 0; i < FLEXVEC_MAX_LAYERS; i++){
    d->layers[i].name[FLEXVEC_NAME_MAX - 1] = 0;
    if(d->layers[i].used > 1) d->layers[i].used = 1;
  }
  int anyLayer = 0;
  for(int i = 0; i < FLEXVEC_MAX_LAYERS; i++) if(d->layers[i].used) anyLayer = 1;
  if(!anyLayer){                                 // ni una capa valida: se rehace la base
    d->layers[0].used = 1; d->layers[0].visible = 1;
    snprintf(d->layers[0].name, FLEXVEC_NAME_MAX, "Capa 1");
    d->layerActive = 0;
  }
  for(int g = 0; g < (int)h.gradN; g++){
    if(d->grads[g].nstops > FLEXVEC_MAX_STOPS) d->grads[g].nstops = FLEXVEC_MAX_STOPS;
    if(d->grads[g].nstops < 2) d->grads[g].used = 0;
    if(d->grads[g].used) fvGradBuildLut(&d->grads[g]);
  }
  if(h.textN) d->arena.text[h.textN - 1] = 0;    // el pool SIEMPRE acaba en cero
  d->dirty = 0;
  return FLEXVEC_OK;
}

// #############################################################
//  PATHFINDER (operaciones booleanas)
//  ------------------------------------------------------------
//  POR QUE NO SE HACE "BIEN", Y POR QUE ESTO ES MEJOR AQUI.
//
//  La forma exacta de intersecar dos trazados es cortar cada Bezier
//  contra cada Bezier, clasificar los trozos y volver a coserlos. En un
//  PC eso ya es delicado: los casos degenerados -- dos bordes que se
//  tocan sin cruzarse, un vertice justo encima de una arista, dos
//  curvas tangentes -- son la mitad del codigo, y cuando uno se escapa
//  el resultado no es "un poco peor": es un trazado que no cierra, un
//  bucle infinito o un fallo de memoria. En un MCU que ademas alimenta
//  un watchdog cada vuelta, eso no es un fallo de calidad: es un
//  reinicio con el trabajo del usuario dentro.
//
//  Aqui se resuelve sobre una REJILLA DE COBERTURA: se rasterizan los
//  operandos, se combinan con AND/OR/XOR/NOT -- que no tienen casos
//  degenerados -- y se vuelve a trazar el contorno del resultado, que
//  despues se simplifica. Es APROXIMADO a la resolucion de la rejilla
//  (FLEXVEC_BOOL_GRID sobre el lado mayor de la seleccion), pero:
//    · SIEMPRE termina, en un numero de pasos que se sabe de antemano;
//    · SIEMPRE usa la misma memoria, reservada antes de empezar;
//    · no tiene un solo caso degenerado que pueda dejar el documento
//      a medias.
//  Por eso en docs/FLEX-VECTOR-PRO.md esta clasificada como
//  SIMPLIFICADA y no como completa. Es una decision, no un descuido.
// #############################################################
size_t flexVecBoolBytes(void){
  return (size_t)FLEXVEC_BOOL_GRID * FLEXVEC_BOOL_GRID * 2
       + (size_t)2 * FLEXVEC_MAX_FLATPTS * sizeof(float)
       + (size_t)FLEXVEC_MAX_FLATPTS
       + 32;
}
int flexVecBoolInit(FlexVecBoolWork* w, void* block, size_t size){
  if(!w || !block) return FLEXVEC_E_BADARG;
  if(size < flexVecBoolBytes()) return FLEXVEC_E_NOMEM;
  uint8_t* p = (uint8_t*)block;
  size_t mis = ((size_t)p) & 7u;
  if(mis) p += (8u - mis);
  w->gridA = p; p += (size_t)FLEXVEC_BOOL_GRID * FLEXVEC_BOOL_GRID;
  w->gridB = p; p += (size_t)FLEXVEC_BOOL_GRID * FLEXVEC_BOOL_GRID;
  w->trace = (float*)p; p += (size_t)2 * FLEXVEC_MAX_FLATPTS * sizeof(float);
  w->mark  = p;
  return FLEXVEC_OK;
}

typedef struct { uint8_t* g; int gw, gh; } FvGridCtx;
static void fvGridSpan(int y, int x0, int x1, uint8_t cov, void* user){
  FvGridCtx* c = (FvGridCtx*)user;
  if(y < 0 || y >= c->gh) return;
  if(cov < 128) return;                     // media cobertura = dentro
  if(x0 < 0) x0 = 0;
  if(x1 > c->gw - 1) x1 = c->gw - 1;
  if(x1 < x0) return;
  memset(c->g + (size_t)y * c->gw + x0, 1, (size_t)(x1 - x0 + 1));
}

static inline int fvIn(const uint8_t* g, int gw, int gh, int x, int y){
  return (x >= 0 && y >= 0 && x < gw && y < gh) ? (g[(size_t)y * gw + x] != 0) : 0;
}

// Simplificacion Douglas-Peucker ITERATIVA. Recursiva seria elegante y
// tendria una profundidad que depende de los datos: en una pila de 8 KB
// eso es exactamente lo que no se puede hacer. La pila explicita esta
// acotada y, si se llena, se deja de subdividir -- se conservan menos
// puntos, que es una degradacion visible pero inofensiva.
#define FV_DP_STACK 96
static int fvSimplify(float* pts, int n, uint8_t* keep, float eps){
  if(n <= 2) return n;
  memset(keep, 0, (size_t)n);
  keep[0] = keep[n - 1] = 1;
  int32_t st[FV_DP_STACK * 2]; int sp = 0;
  st[sp * 2] = 0; st[sp * 2 + 1] = n - 1; sp++;
  while(sp > 0){
    sp--;
    int a = st[sp * 2], b = st[sp * 2 + 1];
    if(b - a < 2) continue;
    float ax = pts[a * 2], ay = pts[a * 2 + 1];
    float bx = pts[b * 2], by = pts[b * 2 + 1];
    float best = -1; int bi = -1;
    for(int i = a + 1; i < b; i++){
      float dd = fvDist2Seg(pts[i * 2], pts[i * 2 + 1], ax, ay, bx, by);
      if(dd > best){ best = dd; bi = i; }
    }
    if(bi < 0 || best <= eps * eps) continue;
    keep[bi] = 1;
    if(sp + 2 <= FV_DP_STACK){
      st[sp * 2] = a;  st[sp * 2 + 1] = bi; sp++;
      st[sp * 2] = bi; st[sp * 2 + 1] = b;  sp++;
    }
  }
  int w = 0;
  for(int i = 0; i < n; i++){
    if(!keep[i]) continue;
    pts[w * 2] = pts[i * 2]; pts[w * 2 + 1] = pts[i * 2 + 1];
    w++;
  }
  return w;
}

int flexVecPathfinder(FlexVecDoc* d, int op, FlexVecRaster* r, FlexVecBoolWork* w){
  if(!d || !r || !w || !w->gridA || !w->gridB || !w->trace || !w->mark) return FLEXVEC_E_NOMEM;
  if(op < FLEXVEC_B_UNITE || op > FLEXVEC_B_TRIM) return FLEXVEC_E_BADARG;
  int16_t sel[FLEXVEC_MAX_ELEMS]; int ns = 0;
  {
    int16_t order[FLEXVEC_MAX_ELEMS];
    int n = flexVecPaintOrder(d, order, FLEXVEC_MAX_ELEMS);
    for(int i = 0; i < n; i++)
      if(d->elems[order[i]].flags & FLEXVEC_EF_SEL) sel[ns++] = order[i];
  }
  if(ns < 2) return FLEXVEC_E_EMPTY;
  for(int i = 0; i < ns; i++)
    if(fvEditable(d, sel[i]) != FLEXVEC_OK) return FLEXVEC_E_LOCKED;

  // Caja comun y transformacion documento -> rejilla, con la MISMA escala
  // en los dos ejes: una rejilla anisotropa deformaria el resultado.
  float bx0 = 1e30f, by0 = 1e30f, bx1 = -1e30f, by1 = -1e30f;
  for(int i = 0; i < ns; i++){
    float a, b, c, e;
    flexVecBBox(d, sel[i], &a, &b, &c, &e);
    bx0 = fvMin(bx0, a); by0 = fvMin(by0, b);
    bx1 = fvMax(bx1, c); by1 = fvMax(by1, e);
  }
  float bw = bx1 - bx0, bh = by1 - by0;
  if(bw <= 1e-4f || bh <= 1e-4f) return FLEXVEC_E_TOO_COMPLEX;
  float pad = fvMax(bw, bh) * 0.01f + 1.0f;
  bx0 -= pad; by0 -= pad; bx1 += pad; by1 += pad;
  bw = bx1 - bx0; bh = by1 - by0;
  float sc = fvMin((float)(FLEXVEC_BOOL_GRID - 2) / bw, (float)(FLEXVEC_BOOL_GRID - 2) / bh);
  int gw = (int)(bw * sc) + 1, gh = (int)(bh * sc) + 1;
  if(gw < 2 || gh < 2) return FLEXVEC_E_TOO_COMPLEX;
  if(gw > FLEXVEC_BOOL_GRID) gw = FLEXVEC_BOOL_GRID;
  if(gh > FLEXVEC_BOOL_GRID) gh = FLEXVEC_BOOL_GRID;
  FlexVecView gv;
  gv.m[0] = sc; gv.m[1] = 0; gv.m[2] = 0; gv.m[3] = sc;
  gv.m[4] = -bx0 * sc; gv.m[5] = -by0 * sc;
  gv.clipX0 = 0; gv.clipY0 = 0; gv.clipX1 = gw - 1; gv.clipY1 = gh - 1;
  if(gw > r->covW) return FLEXVEC_E_TOO_COMPLEX;

  size_t cells = (size_t)gw * gh;
  memset(w->gridB, 0, cells);
  FvGridCtx ctx; ctx.gw = gw; ctx.gh = gh;
  for(int i = 0; i < ns; i++){
    memset(w->gridA, 0, cells);
    ctx.g = w->gridA;
    int subN = 0;
    int np = flexVecFlatten(d, sel[i], &gv, r, &subN);
    if(np > 0 && subN > 0)
      fvFillPolys(r, r->pts, r->subStart, r->subCount, subN, &gv, 0, fvGridSpan, &ctx);
    if(i == 0 && (op == FLEXVEC_B_MINUS_FRONT || op == FLEXVEC_B_INTERSECT ||
                  op == FLEXVEC_B_TRIM)){
      memcpy(w->gridB, w->gridA, cells);
      continue;
    }
    for(size_t k = 0; k < cells; k++){
      uint8_t a = w->gridA[k], b = w->gridB[k];
      switch(op){
        case FLEXVEC_B_UNITE:       w->gridB[k] = (uint8_t)(a | b); break;
        case FLEXVEC_B_EXCLUDE:     w->gridB[k] = (uint8_t)(a ^ b); break;
        case FLEXVEC_B_INTERSECT:   w->gridB[k] = (uint8_t)(a & b); break;
        case FLEXVEC_B_MINUS_FRONT:
        case FLEXVEC_B_TRIM:        w->gridB[k] = (uint8_t)(b & (uint8_t)!a); break;
        case FLEXVEC_B_DIVIDE:      w->gridB[k] = (uint8_t)(a | b); break;   // ver nota abajo
        default: break;
      }
    }
  }
  // DIVIDE en Illustrator parte la seleccion en TODAS las regiones que
  // definen sus cruces. Aqui se entrega la region comun como objeto
  // propio -- que es la parte util del gesto en una pantalla tactil, sin
  // dejar veinte trozos que hay que separar con el dedo uno a uno.
  if(op == FLEXVEC_B_DIVIDE){
    memset(w->gridB, 0, cells);
    memset(w->gridA, 0, cells);
    ctx.g = w->gridA;
    for(int i = 0; i < ns; i++){
      memset(w->gridA, 0, cells);
      int subN = 0;
      int np = flexVecFlatten(d, sel[i], &gv, r, &subN);
      if(np > 0 && subN > 0)
        fvFillPolys(r, r->pts, r->subStart, r->subCount, subN, &gv, 0, fvGridSpan, &ctx);
      if(i == 0) memcpy(w->gridB, w->gridA, cells);
      else for(size_t k = 0; k < cells; k++) w->gridB[k] = (uint8_t)(w->gridB[k] & w->gridA[k]);
    }
  }

  int any = 0;
  for(size_t k = 0; k < cells; k++) if(w->gridB[k]){ any = 1; break; }
  if(!any) return FLEXVEC_E_EMPTY;      // el resultado esta vacio: no se toca nada

  // Seguimiento de contornos por "grietas": se camina por el borde entre
  // celdas dentro y fuera, no por los centros. Da poligonos cerrados
  // exactos sobre la rejilla y las islas y los huecos salen con
  // orientaciones opuestas, que es justo lo que la regla de no-cero
  // necesita para dejar el hueco vacio.
  memset(w->gridA, 0, cells);           // gridA pasa a ser "inicios ya usados"
  float inv = 1.0f / sc;
  int outN = 0, outSubs = 0;
  int32_t subS[FLEXVEC_MAX_SUBS], subC[FLEXVEC_MAX_SUBS];
  for(int sy = 0; sy < gh && outSubs < FLEXVEC_MAX_SUBS; sy++){
    for(int sx = 0; sx < gw && outSubs < FLEXVEC_MAX_SUBS; sx++){
      if(!fvIn(w->gridB, gw, gh, sx, sy)) continue;
      if(fvIn(w->gridB, gw, gh, sx, sy - 1)) continue;      // no es borde superior
      if(w->gridA[(size_t)sy * gw + sx]) continue;          // ese contorno ya se recorrio
      int px = sx, py = sy, dir = 0;                        // 0=der 1=abajo 2=izq 3=arriba
      int start = outN, guard = 0;
      int maxSteps = 4 * (gw + gh) * 4;
      for(;;){
        if(outN >= FLEXVEC_MAX_FLATPTS) break;
        w->trace[outN * 2]     = bx0 + (float)px * inv;
        w->trace[outN * 2 + 1] = by0 + (float)py * inv;
        outN++;
        // Celdas "delante-izquierda" y "delante-derecha" segun el rumbo.
        int flx, fly, frx, fry;
        switch(dir){
          case 0: flx = px;     fly = py - 1; frx = px;     fry = py;     break;
          case 1: flx = px;     fly = py;     frx = px - 1; fry = py;     break;
          case 2: flx = px - 1; fly = py;     frx = px - 1; fry = py - 1; break;
          default:flx = px - 1; fly = py - 1; frx = px;     fry = py - 1; break;
        }
        int nd;
        if(fvIn(w->gridB, gw, gh, flx, fly))      nd = (dir + 3) & 3;   // girar a la izquierda
        else if(fvIn(w->gridB, gw, gh, frx, fry)) nd = dir;             // seguir recto
        else                                      nd = (dir + 1) & 3;   // girar a la derecha
        if(nd == 0) w->gridA[(size_t)py * gw + px] = 1;   // este inicio queda consumido
        if(px == sx && py == sy && nd == 0 && guard > 0) break;
        dir = nd;
        switch(dir){
          case 0: px++; break;
          case 1: py++; break;
          case 2: px--; break;
          default: py--; break;
        }
        if(++guard > maxSteps) break;
      }
      int n = outN - start;
      if(n < 4){ outN = start; continue; }
      // Simplificacion adaptativa: se sube la tolerancia hasta que el
      // contorno cabe en el limite de nodos. Cota fija de 12 vueltas.
      float eps = inv * 0.55f;
      uint8_t* keep = w->mark;
      n = fvSimplify(w->trace + start * 2, n, keep, eps);
      for(int t = 0; t < 12 && n > FLEXVEC_MAX_NODES_PATH / 2; t++){
        eps *= 1.8f;
        n = fvSimplify(w->trace + start * 2, n, keep, eps);
      }
      if(n < 3){ outN = start; continue; }
      subS[outSubs] = start; subC[outSubs] = n; outSubs++;
      outN = start + n;
    }
  }
  if(outSubs == 0) return FLEXVEC_E_EMPTY;
  int total = 0;
  for(int i = 0; i < outSubs; i++) total += subC[i];
  if(total > FLEXVEC_MAX_NODES_PATH) return FLEXVEC_E_TOO_COMPLEX;

  // Se aplica: un objeto nuevo con la apariencia del de mas ABAJO (lo que
  // hace Illustrator) y los operandos desaparecen. Todo dentro de UNA
  // transaccion: deshacer devuelve los originales de una vez.
  int owned = fvAutoBegin(d, "Pathfinder");
  FlexVecElem proto = d->elems[sel[0]];
  int save = d->layerActive;
  d->layerActive = proto.layer;
  int e = fvElemNew(d, FLEXVEC_K_PATH, total);
  d->layerActive = save;
  if(e < 0){ fvAutoEnd(d, owned, e); return e < 0 ? -e : e; }
  fvUndoTouch(d, e);
  FlexVecNode* nd = d->arena.nodes + d->elems[e].first;
  int at = 0;
  for(int i = 0; i < outSubs; i++){
    for(int k = 0; k < subC[i]; k++){
      const float* q = w->trace + (subS[i] + k) * 2;
      fvNodeSet(&nd[at], q[0], q[1], k == 0 ? (FLEXVEC_N_START | FLEXVEC_N_CLOSE) : 0);
      at++;
    }
  }
  d->elems[e].fill = proto.fill;
  d->elems[e].stroke = proto.stroke;
  d->elems[e].strokeW = proto.strokeW;
  d->elems[e].alpha = proto.alpha;
  d->elems[e].cap = proto.cap; d->elems[e].join = proto.join;
  d->elems[e].flags |= FLEXVEC_EF_SEL;
  d->elems[e].bboxOk = 0;
  // La geometria del resultado ya esta en coordenadas de DOCUMENTO (se
  // trazo sobre la rejilla y se deshizo la escala), asi que la matriz
  // parte de la identidad: heredar la del original la aplicaria dos veces.
  flexVecMatIdentity(d->elems[e].m);
  for(int i = 0; i < ns; i++){
    fvUndoTouch(d, sel[i]);
    fvNodeRelease(d, sel[i]);
    d->elems[sel[i]].layer = -1;
  }
  return fvAutoEnd(d, owned, FLEXVEC_OK);
}
