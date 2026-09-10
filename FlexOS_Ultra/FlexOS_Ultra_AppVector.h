// #############################################################
// ##  FLEX OS ULTRA  ·  FLEX VECTOR PRO  ·  editor vectorial
// ##  ----------------------------------------------------------
// ##  La APP: interfaz Liquid Glass, herramientas, gestos del GT911 y
// ##  el puente entre el motor y el framebuffer. El motor -- modelo de
// ##  documento, Bezier, rasterizador, booleanas, SVG -- vive en
// ##  FlexOS_Vector.cpp, que es codigo portable con pruebas de host.
// ##
// ##  QUE ES FLEX VECTOR PRO. Un editor vectorial de verdad: capas,
// ##  objetos con geometria Bezier cubica, Pluma con anclas y
// ##  tiradores, edicion de nodos, transformaciones afines,
// ##  apariencia separada de la geometria, deshacer/rehacer, y
// ##  exportacion a SVG que se descarga desde el movil por Wi-Fi. No es
// ##  un "Paint con formas": Paint guarda TRAZOS DE PINCEL y esto
// ##  guarda GEOMETRIA, que es la diferencia entre pintar y disenar.
// ##
// ##  EL REPARTO DE DECISIONES esta en docs/FLEX-VECTOR-PRO.md: que
// ##  funcion de Adobe Illustrator entra completa, cual entra recortada
// ##  y cual no puede entrar en este hardware, cada una con su motivo.
// ##
// ##  TRES COSAS QUE MANDAN SOBRE TODO LO DEMAS AQUI
// ##  ----------------------------------------------------------
// ##  1. NO HAY GPU. El ESP32-P4 pinta cada pixel con la CPU. Por eso
// ##     el documento se rasteriza UNA vez a una cache y lo que se
// ##     mueve se compone encima: arrastrar un objeto no vuelve a
// ##     rasterizar el documento entero, repinta la banda sucia desde
// ##     la cache y dibuja el objeto en curso.
// ##  2. NO SE RESERVA MEMORIA EN CALIENTE. Todo lo que el editor
// ##     necesita se reserva al abrir la app y se suelta al cerrarla.
// ##     El bucle de render no llama a malloc ni una vez.
// ##  3. TODO LIMITE FALLA BIEN. Nodos, objetos, capas, pasos de
// ##     deshacer, tamano del SVG: al llegar al tope sale un aviso y la
// ##     operacion no se hace. Nunca un reinicio con el trabajo dentro.
// ##
// ##  COMO ENCAJA ESTE ARCHIVO
// ##  ----------------------------------------------------------
// ##  Es una PARTE del sketch FlexOS_Ultra.ino y se incluye en la
// ##  cadena lineal de modulos detras de FlexOS_Ultra_HttpShare.h, que
// ##  es el servicio del sistema con el que comparte el SVG.
// #############################################################
#pragma once
#include "FlexOS_Ultra_HttpShare.h"   // eslabon anterior de la cadena

#include "FlexOS_Vector.h"
#include "FlexOS_QR.h"

// #############################################################
// ##  DISPOSICION EN 480x800
// ##  ------------------------------------------------------
// ##  Cabecera propia arriba (titulo + deshacer/rehacer + menu), tira
// ##  de herramientas abajo y el LIENZO en medio, que es todo lo que
// ##  queda. Los paneles (color, capas, organizar, compartir) suben
// ##  desde abajo sobre el lienzo y se cierran al tocar fuera: en una
// ##  pantalla de 480 px de ancho, un panel fijo se comeria el sitio
// ##  donde se dibuja.
// #############################################################
#define VEC_HEAD_H     88
#define VEC_TOOL_H     72
#define VEC_TOP        VEC_HEAD_H
#define VEC_BOT        (SCR_H - navBarH() - VEC_TOOL_H)
#define VEC_CANVAS_H   (VEC_BOT - VEC_TOP)

// Herramientas. El orden es el de la tira de abajo.
enum { VT_SELECT = 0, VT_NODE, VT_PEN, VT_RECT, VT_ELLIPSE, VT_POLY, VT_LINE, VT_TEXT, VT_N };
// Paneles deslizantes.
enum { VP_NONE = 0, VP_STYLE, VP_LAYERS, VP_ARRANGE, VP_SHARE, VP_N };

#define VEC_PANEL_H    358
#define VEC_HANDLE_R   11        // radio del tirador de la caja delimitadora
#define VEC_TOUCH_TOL  14        // holgura de dedo, EN PIXELES (se pasa a unidades con el zoom)
#define VEC_GRID_STEP  20.0f     // paso de la cuadricula, en unidades de documento
#define VEC_MIN_ZOOM   0.15f
#define VEC_MAX_ZOOM   16.0f
#define VEC_LIST_MAX   24        // documentos que lista la galeria

// #############################################################
// ##  MEMORIA  ·  presupuesto explicito, todo de una vez
// ##  ------------------------------------------------------
// ##  Se reserva al ABRIR la app y se suelta al CERRARLA. Ni una
// ##  reserva durante la edicion, y menos durante el render.
// ##
// ##   pool de nodos      4096 x 28 B  =  112 KB
// ##   diario de deshacer               =  256 KB
// ##   pool de texto                    =    4 KB
// ##   rasterizador (un bloque)         =   ~86 KB
// ##   cache de render 480x800 RGB565   =  768 KB
// ##   buffer de SVG                    =   96 KB
// ##                                       -------
// ##                                      ~1,32 MB
// ##
// ##  De los 32 MB de PSRAM de la placa, el sistema reserva 6 MB
// ##  (FLEXMEM_RESERVE_BYTES) y los cuatro framebuffers ocupan 3 MB.
// ##  1,3 MB para un editor vectorial completo deja el resto del
// ##  sistema exactamente como estaba, y por eso la app se declara
// ##  FLEXMEM_W_MEDIUM y no pesada.
// ##
// ##  LA CACHE ES LA PIEZA GRANDE Y ES LA QUE SE SUELTA. shed() la
// ##  libera cuando la app esta en segundo plano y el sistema necesita
// ##  PSRAM; resume() la rehace. El documento no se toca nunca.
// #############################################################
static FlexVecDoc     vecDoc;
static FlexVecArena   vecArena;
static FlexVecRaster  vecRas;
static FlexVecNode*   vecNodes   = NULL;
static uint8_t*       vecUndoBuf = NULL;
static char*          vecTextBuf = NULL;
static void*          vecRasBlk  = NULL;
static uint16_t*      vecCache   = NULL;    // SCR_W x SCR_H, RGB565
static char*          vecSvg     = NULL;    // buffer de exportacion
static int            vecSvgLen  = 0;
static bool           vecReady   = false;   // los bloques estan y el documento es valido
static int            vecMemErr  = 0;

// ---- Estado de la app ----
static int      vecView      = 0;           // 0 = galeria, 1 = lienzo
static int      vecTool      = VT_SELECT;
static int      vecPanel     = VP_NONE;
static float    vecZoom      = 1.0f;
static float    vecPanX      = 0.0f, vecPanY = 0.0f;   // documento en la esquina del lienzo
static bool     vecGrid      = true;
static bool     vecSnap      = true;
static bool     vecCacheOk   = false;       // la cache refleja el documento actual
static char     vecPath[FLEXFS_PATH_MAX] = "";
static char     vecMsg[64]   = "";
static uint32_t vecMsgMs     = 0;
static int      vecPolySides = 5;
static bool     vecPolyStar  = false;
static int      vecStyleTab  = 0;      // 0 color · 1 motivo · 2 apariencia extra
static int      vecArrPage   = 0;      // 0 organizar · 1 Fase 2
static int      vecRepMode   = 0;      // cicla las formas de repetir
static int      vecPatSel    = -1;     // motivo de trabajo
static int      vecPngLen    = 0;
static uint32_t vecFillRGB   = 0x3C6EF0;
static uint32_t vecStrokeRGB = 0x101018;
static float    vecStrokeW   = 2.0f;
static uint8_t  vecObjAlpha  = 255;

// Galeria
static FlexFsEntry vecList[VEC_LIST_MAX];
static int      vecListN     = 0;
static int      vecSelIdx    = -1;
static int      vecScroll    = 0;
static int      vecDragY0    = 0, vecDragS0 = 0;
static bool     vecDragging  = false;

// Gesto en curso
enum { VG_NONE = 0, VG_PAN, VG_MOVE, VG_HANDLE, VG_NODE, VG_HANDLEBAR, VG_MARQUEE, VG_PINCH, VG_PEN };
static int      vecGest      = VG_NONE;
static int      vecGestElem  = -1;
static int      vecGestNode  = -1;
static int      vecGestPart  = 0;
static int      vecGestHandle= -1;          // 0..7 tiradores de la caja
static float    vecGestX0, vecGestY0;       // punto inicial, en documento
static float    vecGestBX0, vecGestBY0, vecGestBX1, vecGestBY1;  // caja al empezar
static int      vecMarqX0, vecMarqY0, vecMarqX1, vecMarqY1;      // en pixeles
static int      vecDirtyX0, vecDirtyY0, vecDirtyX1, vecDirtyY1;  // banda a repintar
static bool     vecDirtyAny  = false;

// Pinza y rotacion de dos dedos
static bool     vecPinch     = false;
static float    vecPinchD0   = 0, vecPinchZ0 = 1;
static float    vecPinchCX0 = 0, vecPinchCY0 = 0;   // centro, en documento
static uint32_t vecPinchMs   = 0;

// Pluma
static int      vecPenElem   = -1;
static bool     vecPenDrag   = false;
static float    vecPenAX = 0, vecPenAY = 0;

// Compartir
static bool     vecShareOn   = false;
static uint8_t* vecQrMods    = NULL;
static int      vecQrSize    = 0;

static void vecRenderAll();
static void vecRenderGallery();
static void vecToast(const char* m);
static void vecInvalidate();

// #############################################################
// ##  RESERVA Y LIBERACION
// #############################################################
static bool vecArenaInit(){
  if(vecReady) return true;
  vecMemErr = 0;
  size_t rasBytes = flexVecRasterBytes(SCR_W);
  if(!vecNodes)   vecNodes   = (FlexVecNode*)heap_caps_malloc((size_t)FLEXVEC_MAX_NODES * sizeof(FlexVecNode), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!vecUndoBuf) vecUndoBuf = (uint8_t*)heap_caps_malloc(FLEXVEC_UNDO_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!vecTextBuf) vecTextBuf = (char*)heap_caps_malloc(FLEXVEC_TEXT_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!vecRasBlk)  vecRasBlk  = heap_caps_malloc(rasBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!vecSvg)     vecSvg     = (char*)heap_caps_malloc(FLEXVEC_SVG_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  // La cache es lo unico que puede faltar sin que la app deje de
  // funcionar: sin ella se rasteriza directo al framebuffer, que es mas
  // lento pero correcto. Todo lo demas es obligatorio.
  if(!vecCache)   vecCache   = (uint16_t*)heap_caps_aligned_alloc(64, (size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!vecNodes || !vecUndoBuf || !vecTextBuf || !vecRasBlk || !vecSvg){
    vecMemErr = 1;
    return false;
  }
  vecArena.nodes = vecNodes; vecArena.undo = vecUndoBuf; vecArena.text = vecTextBuf;
  if(flexVecAttach(&vecDoc, &vecArena) != FLEXVEC_OK){ vecMemErr = 1; return false; }
  if(flexVecRasterInit(&vecRas, vecRasBlk, rasBytes, SCR_W) != FLEXVEC_OK){ vecMemErr = 1; return false; }
  vecReady = true;
  return true;
}

static void vecArenaFree(){
  if(vecShareOn){ flexShareStop(); vecShareOn = false; }
  if(vecNodes){ free(vecNodes); vecNodes = NULL; }
  if(vecUndoBuf){ free(vecUndoBuf); vecUndoBuf = NULL; }
  if(vecTextBuf){ free(vecTextBuf); vecTextBuf = NULL; }
  if(vecRasBlk){ free(vecRasBlk); vecRasBlk = NULL; }
  if(vecSvg){ free(vecSvg); vecSvg = NULL; }
  if(vecCache){ free(vecCache); vecCache = NULL; }
  if(vecQrMods){ free(vecQrMods); vecQrMods = NULL; }
  memset(&vecDoc, 0, sizeof(vecDoc));
  memset(&vecRas, 0, sizeof(vecRas));
  vecReady = false; vecCacheOk = false; vecSvgLen = 0; vecQrSize = 0;
}

// #############################################################
// ##  VISTA: documento <-> pixel
// ##  ------------------------------------------------------
// ##  Una sola afin, y la MISMA que recibe el rasterizador. Que la
// ##  interfaz y el motor compartan transformacion es lo que hace que
// ##  el dedo caiga exactamente donde se ve el objeto: dos copias de
// ##  esta formula acabarian separandose y la seleccion empezaria a
// ##  fallar por unos pixeles justo al hacer zoom.
// #############################################################
static void vecViewMat(FlexVecView* v){
  v->m[0] = vecZoom; v->m[1] = 0; v->m[2] = 0; v->m[3] = vecZoom;
  v->m[4] = -vecPanX * vecZoom;
  v->m[5] = -vecPanY * vecZoom + (float)VEC_TOP;
  v->clipX0 = 0; v->clipY0 = VEC_TOP;
  v->clipX1 = SCR_W - 1; v->clipY1 = VEC_BOT - 1;
}
static inline float vecDocX(int px){ return vecPanX + (float)px / vecZoom; }
static inline float vecDocY(int py){ return vecPanY + (float)(py - VEC_TOP) / vecZoom; }
static inline int   vecPixX(float dx){ return (int)((dx - vecPanX) * vecZoom + 0.5f); }
static inline int   vecPixY(float dy){ return (int)((dy - vecPanY) * vecZoom + 0.5f) + VEC_TOP; }
static inline float vecTol(){ return (float)VEC_TOUCH_TOL / vecZoom; }

static void vecFitView(){
  float zx = (float)(SCR_W - 32) / (vecDoc.artW > 1 ? vecDoc.artW : 1);
  float zy = (float)(VEC_CANVAS_H - 32) / (vecDoc.artH > 1 ? vecDoc.artH : 1);
  vecZoom = zx < zy ? zx : zy;
  if(vecZoom < VEC_MIN_ZOOM) vecZoom = VEC_MIN_ZOOM;
  if(vecZoom > VEC_MAX_ZOOM) vecZoom = VEC_MAX_ZOOM;
  vecPanX = vecDoc.artW * 0.5f - (float)SCR_W / (2.0f * vecZoom);
  vecPanY = vecDoc.artH * 0.5f - (float)VEC_CANVAS_H / (2.0f * vecZoom);
  vecInvalidate();
}
static void vecClampView(){
  if(vecZoom < VEC_MIN_ZOOM) vecZoom = VEC_MIN_ZOOM;
  if(vecZoom > VEC_MAX_ZOOM) vecZoom = VEC_MAX_ZOOM;
  // Margen de una pantalla alrededor de la mesa de trabajo: se puede
  // sacar el documento de la vista, pero no perderlo de vista.
  float mx = (float)SCR_W / vecZoom, my = (float)VEC_CANVAS_H / vecZoom;
  if(vecPanX < -mx) vecPanX = -mx;
  if(vecPanY < -my) vecPanY = -my;
  if(vecPanX > vecDoc.artW) vecPanX = vecDoc.artW;
  if(vecPanY > vecDoc.artH) vecPanY = vecDoc.artH;
}

static void vecInvalidate(){ vecCacheOk = false; }
// El motor tiene su propia comprobacion de indices, pero es interna. La
// app necesita la suya para no preguntarle por un objeto que ya borro.
static inline bool vecElemOk(int e){
  return e >= 0 && e < FLEXVEC_MAX_ELEMS && vecDoc.elems[e].layer >= 0;
}
static void vecToast(const char* m){
  snprintf(vecMsg, sizeof(vecMsg), "%s", m ? m : "");
  vecMsgMs = millis();
}
// Un codigo de error del motor se ensena con SUS PALABRAS. "No se pudo"
// no le dice al usuario si tiene que borrar un objeto, simplificar una
// figura o cambiar de capa; "No caben mas nodos" si.
static void vecToastErr(int rc){
  if(rc < 0) rc = -rc;
  if(rc == FLEXVEC_OK) return;
  vecToast(flexVecErrText(rc));
}

// #############################################################
// ##  EL PUENTE MOTOR -> FRAMEBUFFER
// ##  ------------------------------------------------------
// ##  El motor no dibuja: emite TRAMOS (fila, x0..x1, cobertura). Aqui
// ##  cada tramo se convierte en un hLineA, que es UNA escritura por
// ##  tramo en vez de una por pixel. Ese detalle es la diferencia entre
// ##  que una figura grande cueste microsegundos o milisegundos.
// #############################################################
typedef struct {
  uint16_t col;
  uint8_t  alpha;              // opacidad de la pintura x la del objeto
  const FlexVecGrad* grad;     // NULL = ni gradiente
  const FlexVecPattern* pat;   // NULL = ni motivo
  float    inv[6];             // pixel -> espacio del objeto
} VecSpanCtx;

static inline uint16_t vecRgb(uint32_t rgb){
  return rgb565((uint8_t)((rgb >> 16) & 0xFF), (uint8_t)((rgb >> 8) & 0xFF), (uint8_t)(rgb & 0xFF));
}

static void vecSpanSolid(int y, int x0, int x1, uint8_t cov, void* user){
  VecSpanCtx* c = (VecSpanCtx*)user;
  uint32_t a = ((uint32_t)cov * c->alpha) / 255u;
  if(!a) return;
  hLineA(x0, y, x1 - x0 + 1, c->col, (uint8_t)a);
}

static void vecSpanGrad(int y, int x0, int x1, uint8_t cov, void* user){
  VecSpanCtx* c = (VecSpanCtx*)user;
  const FlexVecGrad* g = c->grad;
  if(!g){ vecSpanSolid(y, x0, x1, cov, user); return; }
  // Coordenada del objeto en el primer pixel del tramo, y su incremento
  // por pixel: la inversa es afin, asi que avanzar en x es una SUMA.
  float fx = (float)x0 + 0.5f, fy = (float)y + 0.5f;
  float dx = c->inv[0] * fx + c->inv[2] * fy + c->inv[4];
  float dy = c->inv[1] * fx + c->inv[3] * fy + c->inv[5];
  float sx = c->inv[0], sy = c->inv[1];
  for(int x = x0; x <= x1; x++, dx += sx, dy += sy){
    float t = flexVecGradAt(g, dx, dy);
    int i = (int)(t * 255.0f + 0.5f);
    if(i < 0) i = 0; if(i > 255) i = 255;
    uint32_t a = ((uint32_t)cov * c->alpha * g->lutA[i]) / (255u * 255u);
    if(!a) continue;
    pxA(x, y, vecRgb(g->lut[i]), (uint8_t)a);
  }
}

// MOTIVO. El color sale de una formula, no de una textura: cero memoria
// y coste constante por pixel. Como se evalua en coordenadas de
// DOCUMENTO, al ampliar se ven los mismos puntos mas grandes -- que es
// lo que se espera de un relleno vectorial y lo que una textura
// rasterizada no puede dar sin volver a generarse.
static void vecSpanPattern(int y, int x0, int x1, uint8_t cov, void* user){
  VecSpanCtx* c = (VecSpanCtx*)user;
  const FlexVecPattern* pt = c->pat;
  if(!pt){ vecSpanSolid(y, x0, x1, cov, user); return; }
  float fx = (float)x0 + 0.5f, fy = (float)y + 0.5f;
  float dx = c->inv[0] * fx + c->inv[2] * fy + c->inv[4];
  float dy = c->inv[1] * fx + c->inv[3] * fy + c->inv[5];
  float sx = c->inv[0], sy = c->inv[1];
  for(int x = x0; x <= x1; x++, dx += sx, dy += sy){
    uint8_t pa = 0;
    uint32_t rgb = flexVecPatternAt(pt, dx, dy, &pa);
    uint32_t a = ((uint32_t)cov * c->alpha * pa) / (255u * 255u);
    if(!a) continue;
    pxA(x, y, vecRgb(rgb), (uint8_t)a);
  }
}

// Prepara el contexto de una pintura (relleno o trazo) de un objeto.
static void vecCtxFor(VecSpanCtx* c, const FlexVecElem* el, const FlexVecPaint* p,
                      const FlexVecView* v){
  memset(c, 0, sizeof(*c));
  c->col = vecRgb(p->rgb);
  c->alpha = (uint8_t)(((uint32_t)p->alpha * el->alpha) / 255u);
  c->grad = NULL; c->pat = NULL;
  int wantsInv = (p->type == FLEXVEC_P_GRAD || p->type == FLEXVEC_P_PATTERN);
  if(!wantsInv) return;
  // La inversa lleva del pixel al espacio del OBJETO: es donde viven el
  // eje del gradiente y la celda del motivo. Sin ella, girar el objeto
  // dejaria el relleno quieto.
  float cm[6];
  flexVecMatMul(v->m, el->m, cm);
  if(!flexVecMatInvert(cm, c->inv)) return;            // degenerada: color plano
  if(p->type == FLEXVEC_P_GRAD && p->ref >= 0 && p->ref < FLEXVEC_MAX_GRADS &&
     vecDoc.grads[p->ref].used)
    c->grad = &vecDoc.grads[p->ref];
  else if(p->type == FLEXVEC_P_PATTERN && p->ref >= 0 && p->ref < FLEXVEC_MAX_PATTERNS &&
          vecDoc.pats[p->ref].used)
    c->pat = &vecDoc.pats[p->ref];
}
static FlexVecSpanCb vecCbFor(const VecSpanCtx* c){
  if(c->grad) return vecSpanGrad;
  if(c->pat)  return vecSpanPattern;
  return vecSpanSolid;
}

// #############################################################
// ##  DIBUJO DEL DOCUMENTO
// #############################################################
static void vecDrawArtboard(){
  int ax0 = vecPixX(0), ay0 = vecPixY(0);
  int ax1 = vecPixX(vecDoc.artW), ay1 = vecPixY(vecDoc.artH);
  if(ax1 <= ax0 || ay1 <= ay0) return;
  // Sombra corta bajo la mesa: es lo que hace que se lea como una hoja
  // sobre la mesa y no como un rectangulo blanco perdido.
  for(int i = 3; i >= 1; i--)
    fillRectA(ax0 + i, ay0 + i, ax1 - ax0, ay1 - ay0, TH_SHADOW, (uint8_t)(26 - i * 6));
  fillRect(ax0, ay0, ax1 - ax0, ay1 - ay0, TC(255, 255, 255));
  drawRect(ax0, ay0, ax1 - ax0, ay1 - ay0, TH_BORDER);
}

static void vecDrawGrid(){
  if(!vecGrid) return;
  float step = VEC_GRID_STEP;
  float px = step * vecZoom;
  // Si la cuadricula se aprieta por debajo de 9 px se sube de paso: una
  // rejilla mas fina que el dedo no ayuda a alinear, solo ensucia.
  while(px < 9.0f && step < 4096.0f){ step *= 2.0f; px = step * vecZoom; }
  uint16_t c = mix565(TC(255,255,255), TH_BORDER, 150);
  int ax0 = vecPixX(0), ay0 = vecPixY(0);
  int ax1 = vecPixX(vecDoc.artW), ay1 = vecPixY(vecDoc.artH);
  if(ax1 <= ax0 || ay1 <= ay0) return;
  int sy0 = ay0 < VEC_TOP ? VEC_TOP : ay0;
  int sy1 = ay1 > VEC_BOT ? VEC_BOT : ay1;
  for(float gx = 0; gx <= vecDoc.artW + 0.01f; gx += step){
    int x = vecPixX(gx);
    if(x < 0 || x >= SCR_W) continue;
    vLine(x, sy0, sy1 - sy0, c);
  }
  int sx0 = ax0 < 0 ? 0 : ax0;
  int sx1 = ax1 > SCR_W ? SCR_W : ax1;
  for(float gy = 0; gy <= vecDoc.artH + 0.01f; gy += step){
    int y = vecPixY(gy);
    if(y < VEC_TOP || y >= VEC_BOT) continue;
    hLine(sx0, y, sx1 - sx0, c);
  }
}

// Pinta UN objeto con la vista dada. Lo usan la cache y tambien el
// dibujo en caliente del objeto que se esta arrastrando.
// El tamano de drawText es un MULTIPLO (12 px por unidad), asi que el
// cuerpo en unidades de documento se lleva al escalon mas cercano. Con
// esa misma funcion se mide el texto para repartirlo en lineas: medir
// con una y dibujar con otra desalinearia el ajuste.
static int vecTextStep(float sizeDoc){
  int st = (int)(sizeDoc * vecZoom / 12.0f + 0.5f);
  if(st < 1) st = 1;
  if(st > 8) st = 8;
  return st;
}
// Medida que el motor usa para el texto de area. Mide con la fuente
// REAL del sistema, en unidades de DOCUMENTO (por eso divide por el
// zoom): el reparto en lineas no puede cambiar al ampliar.
static float vecMeasure(const char* utf8, int len, float size, void* user){
  (void)user;
  if(len <= 0) return 0;
  char tmp[128];
  if(len > (int)sizeof(tmp) - 1) len = (int)sizeof(tmp) - 1;
  memcpy(tmp, utf8, (size_t)len);
  tmp[len] = 0;
  int st = vecTextStep(size);
  float px = (float)textW(tmp, st);
  return (vecZoom > 0.0001f) ? px / vecZoom : px;
}
typedef struct { const FlexVecElem* el; const FlexVecView* v; uint16_t col; } VecTextCtx;
static void vecTextLineCb(const char* utf8, int len, float x, float y, float w, void* user){
  VecTextCtx* c = (VecTextCtx*)user;
  (void)w;
  char tmp[128];
  if(len > (int)sizeof(tmp) - 1) len = (int)sizeof(tmp) - 1;
  if(len < 0) len = 0;
  memcpy(tmp, utf8, (size_t)len);
  tmp[len] = 0;
  float sx, sy;
  flexVecMatApply(c->el->m, x, y, &sx, &sy);
  float px, py;
  flexVecMatApply(c->v->m, sx, sy, &px, &py);
  drawText((int)px, (int)py, tmp, vecTextStep(c->el->p0), c->col);
}

static void vecDrawElem(int e, const FlexVecView* v){
  if(e < 0 || e >= FLEXVEC_MAX_ELEMS) return;
  const FlexVecElem* el = &vecDoc.elems[e];
  if(el->layer < 0) return;
  int subN = 0;
  int np = flexVecFlatten(&vecDoc, e, v, &vecRas, &subN);
  // Escala de la vista COMPUESTA con la del objeto: un objeto al doble
  // tiene el trazo al doble, como en Illustrator con "Escalar trazos".
  float cm[6], sc = 1.0f;
  flexVecMatMul(v->m, el->m, cm);
  {
    float ux, uy, vx, vy;
    flexVecMatApplyVec(cm, 1.0f, 0.0f, &ux, &uy);
    flexVecMatApplyVec(cm, 0.0f, 1.0f, &vx, &vy);
    float k = sqrtf(fabsf(ux * vy - uy * vx));
    if(k > 0) sc = k;
  }
  // ORDEN DE PINTADO de la apariencia ampliada: relleno 2, relleno,
  // trazo 2, trazo. Es de abajo arriba, como una pila de apariencia, y
  // es lo que hace que un "trazo 2" mas grueso se vea como un contorno
  // por fuera del principal.
  if(np > 0 && subN > 0){
    if(el->fill2.type != FLEXVEC_P_NONE){
      VecSpanCtx c; vecCtxFor(&c, el, &el->fill2, v);
      flexVecFillFlat(&vecRas, subN, v, 0, vecCbFor(&c), &c);
    }
    if(el->fill.type != FLEXVEC_P_NONE){
      VecSpanCtx c; vecCtxFor(&c, el, &el->fill, v);
      flexVecFillFlat(&vecRas, subN, v, 0, vecCbFor(&c), &c);
    }
    if(el->stroke2.type != FLEXVEC_P_NONE && el->strokeW2 > 0){
      VecSpanCtx c; vecCtxFor(&c, el, &el->stroke2, v);
      flexVecStrokeFlat(&vecRas, subN, v, el->strokeW2 * sc, el->cap, el->join,
                        vecCbFor(&c), &c);
    }
    if(el->stroke.type != FLEXVEC_P_NONE && el->strokeW > 0){
      VecSpanCtx c; vecCtxFor(&c, el, &el->stroke, v);
      flexVecStrokeFlat(&vecRas, subN, v, el->strokeW * sc, el->cap, el->join,
                        vecCbFor(&c), &c);
    }
  }
  // TEXTO: lo dibuja la fuente del sistema, no el rasterizador. El motor
  // guarda la cadena y la caja; la unica fuente que hay en la placa es
  // un atlas 4bpp sin contornos, asi que no existen curvas que rellenar.
  if(el->kind == FLEXVEC_K_TEXT){
    const FlexVecNode* nd = vecDoc.arena.nodes + el->first;
    uint16_t col = vecRgb(el->fill.rgb);
    if(flexVecTextIsArea(&vecDoc, e)){
      // TEXTO DE AREA: el reparto en lineas lo hace el motor y la medida
      // la pone la fuente de verdad. Las mismas lineas que se ven aqui
      // son las que salen en el SVG (ver vecExportSvg).
      VecTextCtx tc; tc.el = el; tc.v = v; tc.col = col;
      flexVecTextLayout(&vecDoc, e, el->p0 * 1.25f, vecMeasure, NULL, vecTextLineCb, &tc);
    } else {
      float sx, sy;
      flexVecMatApply(el->m, nd[0].x, nd[0].y, &sx, &sy);
      float px, py;
      flexVecMatApply(v->m, sx, sy, &px, &py);
      drawText((int)px, (int)py, flexVecTextStr(&vecDoc, e), vecTextStep(el->p0), col);
    }
  }
}

// Reconstruye la cache entera. Es la operacion CARA del editor y por eso
// solo corre cuando el documento o la vista cambian de verdad: mover un
// objeto no pasa por aqui, pasa por vecPaintBand().
static void vecRenderDoc(){
  uint16_t* dst = vecCache ? vecCache : fb;
  setBuf(dst);
  int sx0 = gClipX0, sx1 = gClipX1, sy0 = gClipY0, sy1 = gClipY1;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = VEC_TOP; gClipY1 = VEC_BOT - 1;
  fillRect(0, VEC_TOP, SCR_W, VEC_CANVAS_H, TH_PAGE);
  vecDrawArtboard();
  vecDrawGrid();
  FlexVecView v; vecViewMat(&v);
  int16_t order[FLEXVEC_MAX_ELEMS];
  int n = flexVecPaintOrder(&vecDoc, order, FLEXVEC_MAX_ELEMS);
  for(int i = 0; i < n; i++){
    // El objeto que se esta arrastrando NO entra en la cache: se compone
    // encima en cada cuadro. Asi arrastrar cuesta una banda, no un
    // documento entero.
    if(vecGest == VG_MOVE && order[i] == vecGestElem) continue;
    vecDrawElem(order[i], &v);
  }
  gClipX0 = sx0; gClipX1 = sx1; gClipY0 = sy0; gClipY1 = sy1;
  setBuf(fb);
  vecCacheOk = true;
}

// #############################################################
// ##  CAPAS DE ENCIMA  ·  seleccion, nodos, marco y pluma
// ##  ------------------------------------------------------
// ##  Se dibujan SOBRE el framebuffer, nunca sobre la cache: cambian en
// ##  cada cuadro y meterlas en la cache obligaria a reconstruirla cada
// ##  vez que se mueve un tirador.
// #############################################################
static void vecHandleXY(int i, int x0, int y0, int x1, int y1, int* hx, int* hy){
  int mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
  switch(i){
    case 0: *hx = x0; *hy = y0; break;
    case 1: *hx = mx; *hy = y0; break;
    case 2: *hx = x1; *hy = y0; break;
    case 3: *hx = x1; *hy = my; break;
    case 4: *hx = x1; *hy = y1; break;
    case 5: *hx = mx; *hy = y1; break;
    case 6: *hx = x0; *hy = y1; break;
    default:*hx = x0; *hy = my; break;
  }
}

static void vecDrawSelection(){
  float bx0, by0, bx1, by1;
  if(flexVecSelBBox(&vecDoc, &bx0, &by0, &bx1, &by1) != FLEXVEC_OK) return;
  int x0 = vecPixX(bx0), y0 = vecPixY(by0), x1 = vecPixX(bx1), y1 = vecPixY(by1);
  if(x1 - x0 < 2) x1 = x0 + 2;
  if(y1 - y0 < 2) y1 = y0 + 2;
  uint16_t acc = TH_PRIM;
  drawRect(x0, y0, x1 - x0, y1 - y0, acc);
  if(vecTool != VT_NODE){
    for(int i = 0; i < 8; i++){
      int hx, hy; vecHandleXY(i, x0, y0, x1, y1, &hx, &hy);
      fillCircle(hx, hy, VEC_HANDLE_R, TC(255,255,255));
      drawCircle(hx, hy, VEC_HANDLE_R, acc);
      fillCircle(hx, hy, VEC_HANDLE_R - 5, acc);
    }
    // Tirador de GIRO, separado por encima del borde superior. Girar y
    // escalar con el mismo tirador es lo que hace imposible girar un
    // poco sin deformar; separarlos es un tirador mas y cero ambiguedad.
    int rx = (x0 + x1) / 2, ry = y0 - 34;
    if(ry > VEC_TOP + 4){
      vLine(rx, ry + VEC_HANDLE_R, y0 - ry - VEC_HANDLE_R, acc);
      fillCircle(rx, ry, VEC_HANDLE_R, TC(255,255,255));
      drawCircle(rx, ry, VEC_HANDLE_R, acc);
      drawCircle(rx, ry, VEC_HANDLE_R - 5, acc);
    }
  }
}

static void vecDrawNodes(){
  int e = flexVecSelFirst(&vecDoc);
  if(e < 0 || !vecDoc.arena.nodes) return;
  const FlexVecElem* el = &vecDoc.elems[e];
  const FlexVecNode* nd = vecDoc.arena.nodes + el->first;
  uint16_t acc = TH_PRIM;
  for(int i = 0; i < (int)el->count; i++){
    float ax, ay; flexVecMatApply(el->m, nd[i].x, nd[i].y, &ax, &ay);
    int px = vecPixX(ax), py = vecPixY(ay);
    if(py < VEC_TOP - 20 || py > VEC_BOT + 20) continue;
    if(nd[i].flags & FLEXVEC_N_SMOOTH){
      float hx, hy;
      flexVecMatApply(el->m, nd[i].ix, nd[i].iy, &hx, &hy);
      int qx = vecPixX(hx), qy = vecPixY(hy);
      lineTo(px, py, qx, qy, acc);
      fillCircle(qx, qy, 6, TC(255,255,255)); drawCircle(qx, qy, 6, acc);
      flexVecMatApply(el->m, nd[i].ox, nd[i].oy, &hx, &hy);
      qx = vecPixX(hx); qy = vecPixY(hy);
      lineTo(px, py, qx, qy, acc);
      fillCircle(qx, qy, 6, TC(255,255,255)); drawCircle(qx, qy, 6, acc);
    }
    // Ancla: cuadrada si es vertice, redonda si es suave. Es la misma
    // convencion que Illustrator, y se lee de un vistazo.
    if(nd[i].flags & FLEXVEC_N_SMOOTH){
      fillCircle(px, py, 8, TC(255,255,255)); drawCircle(px, py, 8, acc);
      fillCircle(px, py, 4, acc);
    } else {
      fillRect(px - 7, py - 7, 14, 14, TC(255,255,255));
      drawRect(px - 7, py - 7, 14, 14, acc);
      fillRect(px - 3, py - 3, 6, 6, acc);
    }
  }
}

static void vecDrawPenPreview(){
  if(vecTool != VT_PEN || vecPenElem < 0) return;
  if(!vecElemOk(vecPenElem)) return;
  const FlexVecElem* el = &vecDoc.elems[vecPenElem];
  if(el->count == 0) return;
  const FlexVecNode* nd = vecDoc.arena.nodes + el->first;
  int last = (int)el->count - 1;
  float ax, ay; flexVecMatApply(el->m, nd[last].x, nd[last].y, &ax, &ay);
  int px = vecPixX(ax), py = vecPixY(ay);
  fillCircle(px, py, 7, TH_PRIM);
  fillCircle(px, py, 3, TC(255,255,255));
  // El primer ancla se marca en verde: es donde hay que tocar para
  // cerrar la figura, y sin la marca hay que adivinarlo.
  float fx, fy; flexVecMatApply(el->m, nd[0].x, nd[0].y, &fx, &fy);
  int qx = vecPixX(fx), qy = vecPixY(fy);
  drawCircle(qx, qy, 10, TH_OK);
  drawCircle(qx, qy, 9, TH_OK);
}

// La goma elastica. Con la herramienta de seleccion es un marco de
// seleccion; con una herramienta de forma es la VISTA PREVIA de lo que
// se va a crear, dibujada con las primitivas del sistema y no con el
// rasterizador: es una silueta que cambia en cada cuadro y no merece
// una pasada del motor.
static void vecDrawMarquee(){
  if(vecGest != VG_MARQUEE) return;
  int x0 = vecMarqX0 < vecMarqX1 ? vecMarqX0 : vecMarqX1;
  int x1 = vecMarqX0 < vecMarqX1 ? vecMarqX1 : vecMarqX0;
  int y0 = vecMarqY0 < vecMarqY1 ? vecMarqY0 : vecMarqY1;
  int y1 = vecMarqY0 < vecMarqY1 ? vecMarqY1 : vecMarqY0;
  uint16_t acc = TH_PRIM;
  switch(vecTool){
    case VT_SELECT:
      fillRectA(x0, y0, x1 - x0, y1 - y0, acc, 40);
      drawRect(x0, y0, x1 - x0, y1 - y0, acc);
      break;
    case VT_RECT:
      drawRect(x0, y0, x1 - x0, y1 - y0, acc);
      drawRect(x0 + 1, y0 + 1, x1 - x0 - 2, y1 - y0 - 2, acc);
      break;
    case VT_ELLIPSE: {
      int cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
      int rx = (x1 - x0) / 2, ry = (y1 - y0) / 2;
      if(rx > 0 && ry > 0)
        for(int i = 0; i < 48; i++){
          float a0 = 6.28318f * i / 48.0f, a1 = 6.28318f * (i + 1) / 48.0f;
          strokeSegAA(cx + rx * cosf(a0), cy + ry * sinf(a0),
                      cx + rx * cosf(a1), cy + ry * sinf(a1), 1.2f, acc);
        }
      break; }
    case VT_LINE:
      strokeSegAA((float)vecMarqX0, (float)vecMarqY0, (float)vecMarqX1, (float)vecMarqY1, 1.6f, acc);
      break;
    case VT_POLY: {
      int cx = vecMarqX0, cy = vecMarqY0;
      float r = sqrtf((float)((vecMarqX1 - cx) * (vecMarqX1 - cx) + (vecMarqY1 - cy) * (vecMarqY1 - cy)));
      int n = vecPolyStar ? vecPolySides * 2 : vecPolySides;
      if(r > 2 && n >= 3)
        for(int i = 0; i < n; i++){
          float rr0 = (vecPolyStar && (i & 1)) ? r * 0.5f : r;
          float rr1 = (vecPolyStar && ((i + 1) & 1)) ? r * 0.5f : r;
          float a0 = -1.5708f + 6.28318f * i / n, a1 = -1.5708f + 6.28318f * (i + 1) / n;
          strokeSegAA(cx + rr0 * cosf(a0), cy + rr0 * sinf(a0),
                      cx + rr1 * cosf(a1), cy + rr1 * sinf(a1), 1.2f, acc);
        }
      break; }
    default: break;
  }
}

// #############################################################
// ##  COMPOSICION DE UN CUADRO
// ##  ------------------------------------------------------
// ##  REGIONES SUCIAS, que es lo que sostiene la fluidez. Un cuadro
// ##  normal copia del cache al framebuffer SOLO la banda que cambio,
// ##  dibuja encima lo que se esta manipulando y vuelca esa banda con
// ##  flxFlush(y0,y1). Repintar los 480x800 enteros son 768 KB por
// ##  cuadro por el bus de la PSRAM: a 60 Hz no cabe, y ademas no hace
// ##  ninguna falta.
// #############################################################
static void vecMarkDirty(int x0, int y0, int x1, int y1){
  (void)x0; (void)x1;                    // se vuelca por BANDAS de filas
  if(y0 > y1) return;
  if(!vecDirtyAny){ vecDirtyY0 = y0; vecDirtyY1 = y1; vecDirtyAny = true; return; }
  if(y0 < vecDirtyY0) vecDirtyY0 = y0;
  if(y1 > vecDirtyY1) vecDirtyY1 = y1;
}
static void vecMarkDirtyElem(int e){
  if(!vecElemOk(e)) return;
  FlexVecView v; vecViewMat(&v);
  int x0, y0, x1, y1;
  if(flexVecElemPixBBox(&vecDoc, e, &v, &x0, &y0, &x1, &y1) == FLEXVEC_OK)
    vecMarkDirty(x0, y0 - 40, x1, y1 + 40);   // margen: tiradores y marco de seleccion
}
static void vecMarkDirtySel(){
  for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++)
    if(vecElemOk(e) && (vecDoc.elems[e].flags & FLEXVEC_EF_SEL)) vecMarkDirtyElem(e);
}
static void vecMarkDirtyAll(){ vecMarkDirty(0, VEC_TOP, SCR_W - 1, VEC_BOT - 1); }

// Pinta la banda [y0,y1] del lienzo: cache -> framebuffer, encima las
// capas vivas, y al panel.
static void vecPaintBand(int y0, int y1){
  if(y0 < VEC_TOP) y0 = VEC_TOP;
  if(y1 > VEC_BOT - 1) y1 = VEC_BOT - 1;
  if(y0 > y1) return;
  if(!vecCacheOk) vecRenderDoc();
  setBuf(fb);
  int sx0 = gClipX0, sx1 = gClipX1, sy0 = gClipY0, sy1 = gClipY1;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = y0; gClipY1 = y1;
  if(vecCache) fbCopyBand(vecCache, y0, y1);
  // El objeto en movimiento no esta en la cache: se compone AQUI, y por
  // eso arrastrarlo cuesta una figura por cuadro y no un documento.
  if(vecGest == VG_MOVE && vecElemOk(vecGestElem)){
    FlexVecView v; vecViewMat(&v);
    vecDrawElem(vecGestElem, &v);
  }
  vecDrawSelection();
  if(vecTool == VT_NODE) vecDrawNodes();
  vecDrawPenPreview();
  vecDrawMarquee();
  gClipX0 = sx0; gClipX1 = sx1; gClipY0 = sy0; gClipY1 = sy1;
  flxFlush(y0, y1);
}
static void vecFlushDirty(){
  if(!vecDirtyAny) return;
  vecPaintBand(vecDirtyY0, vecDirtyY1);
  vecDirtyAny = false;
}

// #############################################################
// ##  INTERFAZ  ·  Liquid Glass del sistema, no un estilo propio
// ##  ------------------------------------------------------
// ##  Cabecera, tira de herramientas y paneles usan uiSurface(),
// ##  uiGlassPanelCached(), los tokens TH_* y drawText() -- lo mismo
// ##  que Ajustes, Notas o el Panel rapido. La UNICA parte con dibujo
// ##  propio es el LIENZO, y tiene que serlo: ahi manda el documento
// ##  del usuario, no la paleta del sistema. Una hoja blanca es blanca
// ##  tambien en modo oscuro, porque es papel, no interfaz.
// #############################################################
static void vecGlyphTool(int t, int cx, int cy, uint16_t c){
  switch(t){
    case VT_SELECT:                      // puntero
      fillTriangle(cx - 7, cy - 10, cx - 7, cy + 9, cx + 1, cy + 2, c);
      strokeSegAA((float)(cx - 1), (float)cy + 3.0f, (float)(cx + 6), (float)cy + 10.0f, 2.0f, c);
      break;
    case VT_NODE:                        // puntero hueco + nodo
      drawCircle(cx + 5, cy + 5, 4, c);
      fillTriangle(cx - 7, cy - 10, cx - 7, cy + 7, cx - 1, cy + 1, c);
      break;
    case VT_PEN:                         // plumilla
      fillTriangle(cx - 8, cy + 10, cx - 3, cy - 10, cx + 4, cy + 4, c);
      fillTriangle(cx - 8, cy + 10, cx + 4, cy + 4, cx - 2, cy + 8, c);
      break;
    case VT_RECT:   drawRoundRect(cx - 9, cy - 7, 18, 14, 3, c); drawRoundRect(cx - 8, cy - 6, 16, 12, 2, c); break;
    case VT_ELLIPSE:drawCircle(cx, cy, 9, c); drawCircle(cx, cy, 8, c); break;
    case VT_POLY: {
      const float TAU = 6.28318530718f;
      for(int i = 0; i < 5; i++){
        float a0 = -1.5708f + TAU * i / 5.0f, a1 = -1.5708f + TAU * (i + 1) / 5.0f;
        strokeSegAA(cx + 9 * cosf(a0), cy + 9 * sinf(a0), cx + 9 * cosf(a1), cy + 9 * sinf(a1), 1.3f, c);
      }
      break; }
    case VT_LINE:   strokeSegAA((float)(cx - 9), (float)(cy + 8), (float)(cx + 9), (float)(cy - 8), 1.8f, c); break;
    default:                             // texto
      hLine(cx - 8, cy - 8, 17, c); hLine(cx - 8, cy - 7, 17, c);
      vLine(cx - 1, cy - 6, 15, c); vLine(cx, cy - 6, 15, c);
      hLine(cx - 6, cy + 9, 13, c);
      break;
  }
}

static void vecDrawHeader(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, VEC_HEAD_H, TH_WIN);
  hLine(0, VEC_HEAD_H - 1, SCR_W, TH_DIV);
  // Chevron de "atras" con la misma geometria que el resto del sistema.
  strokeSegAA(30, 30, 18, 22, 2.4f, TH_NAV);
  strokeSegAA(18, 22, 30, 14, 2.4f, TH_NAV);
  char title[FLEXFS_NAME_MAX];
  if(vecPath[0]) flexFsStem(vecPath, title, sizeof(title));
  else           snprintf(title, sizeof(title), "%s", vecDoc.name);
  char fit[40]; uiLabelFit(title, 190, 3, fit, sizeof(fit));
  drawText(48, 12, fit, 3, TH_TXT);
  char sub[64];
  snprintf(sub, sizeof(sub), "%d objetos \xC2\xB7 %d%%",
           flexVecElemCount(&vecDoc), (int)(vecZoom * 100.0f + 0.5f));
  drawText(48, 44, sub, 1, TH_TXT2);
  // Deshacer / rehacer / menu. Un boton apagado se dibuja apagado: un
  // boton que parece activo y no hace nada es peor que no tenerlo.
  int bx = SCR_W - 158;
  for(int i = 0; i < 3; i++){
    int cx = bx + i * 50 + 20, cy = 34;
    bool on = (i == 0) ? flexVecCanUndo(&vecDoc)
            : (i == 1) ? flexVecCanRedo(&vecDoc) : true;
    uint16_t c = on ? TH_NAV : TH_DIS;
    uiSurface(cx - 20, cy - 20, 40, 40, 14, 0);
    if(i < 2){
      float dir = i ? 1.0f : -1.0f;
      for(int k = 0; k < 10; k++){
        float a = 3.1416f * (0.15f + 0.7f * k / 9.0f);
        float b = 3.1416f * (0.15f + 0.7f * (k + 1) / 9.0f);
        strokeSegAA(cx - dir * (9 * cosf(a)), cy + 2 - 7 * sinf(a),
                    cx - dir * (9 * cosf(b)), cy + 2 - 7 * sinf(b), 1.5f, c);
      }
      strokeSegAA(cx - dir * 9, cy + 2, cx - dir * 4, cy - 3, 1.5f, c);
      strokeSegAA(cx - dir * 9, cy + 2, cx - dir * 4, cy + 7, 1.5f, c);
    } else {
      for(int k = -1; k <= 1; k++) fillCircle(cx, cy + k * 8, 2, c);
    }
  }
}

static void vecDrawTools(){
  setBuf(fb);
  int ty = VEC_BOT;
  fillRect(0, ty, SCR_W, VEC_TOOL_H, TH_WIN);
  hLine(0, ty, SCR_W, TH_DIV);
  int n = VT_N, w = SCR_W / n;
  for(int i = 0; i < n; i++){
    int cx = i * w + w / 2, cy = ty + VEC_TOOL_H / 2;
    bool on = (i == vecTool);
    if(on) fillRoundRect(cx - w / 2 + 4, ty + 6, w - 8, VEC_TOOL_H - 14, 14, TH_ACCS);
    vecGlyphTool(i, cx, cy, on ? TH_PRIM : TH_TXT2);
  }
}

// Fila de color: los colores de trabajo. Doce bastan en una pantalla
// tactil de 480 px -- una rueda de color completa exige una precision de
// dedo que no existe, y ademas el color se puede afinar con los
// deslizadores de debajo.
static const uint32_t VEC_PAL[12] = {
  0x101018, 0x5A6478, 0xFFFFFF, 0xE23B3B, 0xF08A20, 0xF5CE30,
  0x50B478, 0x3C6EF0, 0x8A5CF0, 0xE060A8, 0x8B5E3C, 0x00A9A5
};

static void vecPanelRect(int* x, int* y, int* w, int* h){
  *w = SCR_W - 24; *h = VEC_PANEL_H;
  *x = 12; *y = VEC_BOT - VEC_PANEL_H - 10;
  if(*y < VEC_TOP + 8) *y = VEC_TOP + 8;
}

// Tres pestanas, porque en 456 px de ancho no caben de otra forma sin
// que todo quede por debajo del tamano de un dedo. La de COLOR es la que
// se usa siempre y por eso es la primera.
static const char* VEC_STYLE_TAB[3] = { "Color", "Motivo", "Extra" };
static const char* VEC_PAT_NAME[FLEXVEC_PAT_N] = {
  "Puntos", "Rayas", "Rejilla", "Damero", "Diagonal", "Trama" };

static void vecStyleTabRect(int i, int x, int y, int w, int* bx, int* by, int* bw, int* bh){
  int tw = (w - 40) / 3;
  *bx = x + 20 + i * tw; *by = y + 44; *bw = tw - 6; *bh = 34;
}

// Muestra de un motivo, dibujada con el MISMO evaluador que el relleno:
// lo que se ve en el boton es exactamente lo que se va a pintar.
static void vecPatSwatch(int kind, int x, int y, int w, int h){
  FlexVecPattern pt;
  memset(&pt, 0, sizeof(pt));
  pt.used = 1; pt.kind = (uint8_t)kind; pt.scale = 10;
  pt.fg = 0x101018; pt.fgA = 255; pt.bg = 0xFFFFFF; pt.bgA = 255;
  for(int j = 0; j < h; j++)
    for(int i = 0; i < w; i++){
      uint8_t a = 0;
      uint32_t c = flexVecPatternAt(&pt, (float)i, (float)j, &a);
      if(a) px(x + i, y + j, vecRgb(c));
    }
}

static void vecDrawStylePanel(int x, int y, int w, int h){
  drawText(x + 20, y + 14, "Apariencia", 3, TH_TXT);
  for(int i = 0; i < 3; i++){
    int bx, by, bw, bh;
    vecStyleTabRect(i, x, y, w, &bx, &by, &bw, &bh);
    bool on = (i == vecStyleTab);
    fillRoundRect(bx, by, bw, bh, 12, on ? TH_PRIM : TH_SURF2);
    drawTextC(bx + bw / 2, by + bh / 2 - 7, VEC_STYLE_TAB[i], 1, on ? TH_ONACC : TH_TXT2);
  }
  int ty = y + 92;
  if(vecStyleTab == 0){
    drawText(x + 20, ty, "Relleno", 1, TH_TXT2);
    for(int i = 0; i < 12; i++){
      int cx = x + 24 + (i % 6) * 40, cy = ty + 22 + (i / 6) * 40;
      fillCircle(cx + 14, cy + 14, 14, vecRgb(VEC_PAL[i]));
      if(VEC_PAL[i] == vecFillRGB) drawCircle(cx + 14, cy + 14, 17, TH_PRIM);
      else drawCircle(cx + 14, cy + 14, 14, TH_BORDER);
    }
    drawText(x + 240, ty, "Trazo", 1, TH_TXT2);
    for(int i = 0; i < 12; i++){
      int cx = x + 244 + (i % 6) * 34, cy = ty + 22 + (i / 6) * 40;
      fillCircle(cx + 12, cy + 14, 12, vecRgb(VEC_PAL[i]));
      if(VEC_PAL[i] == vecStrokeRGB) drawCircle(cx + 12, cy + 14, 15, TH_PRIM);
      else drawCircle(cx + 12, cy + 14, 12, TH_BORDER);
    }
    char t[24];
    drawText(x + 20, ty + 112, "Grosor", 1, TH_TXT2);
    snprintf(t, sizeof(t), "%.1f", (double)vecStrokeW);
    drawTextR(x + w - 20, ty + 112, t, 1, TH_TXT2);
    fillRoundRect(x + 20, ty + 136, w - 40, 8, 4, TH_TRACK);
    float fr = vecStrokeW / 24.0f; if(fr > 1) fr = 1;
    fillRoundRect(x + 20, ty + 136, (int)((w - 40) * fr), 8, 4, TH_PRIM);
    fillCircle(x + 20 + (int)((w - 40) * fr), ty + 140, 13, TH_PRIM);
    drawText(x + 20, ty + 166, "Opacidad", 1, TH_TXT2);
    snprintf(t, sizeof(t), "%d%%", (int)(vecObjAlpha * 100 / 255));
    drawTextR(x + w - 20, ty + 166, t, 1, TH_TXT2);
    fillRoundRect(x + 20, ty + 190, w - 40, 8, 4, TH_TRACK);
    fillRoundRect(x + 20, ty + 190, (int)((w - 40) * vecObjAlpha / 255), 8, 4, TH_PRIM);
    fillCircle(x + 20 + (int)((w - 40) * vecObjAlpha / 255), ty + 194, 13, TH_PRIM);
    fillRoundRect(x + 20, ty + 214, 130, 34, 12, TH_SURF2);
    drawTextC(x + 85, ty + 223, "Sin relleno", 1, TH_TXT);
    fillRoundRect(x + 162, ty + 214, 130, 34, 12, TH_SURF2);
    drawTextC(x + 227, ty + 223, "Sin trazo", 1, TH_TXT);
    fillRoundRect(x + 304, ty + 214, 130, 34, 12, TH_ACCS);
    drawTextC(x + 369, ty + 223, "Gradiente", 1, TH_PRIM);
  } else if(vecStyleTab == 1){
    drawText(x + 20, ty, "Motivo de relleno", 1, TH_TXT2);
    for(int i = 0; i < FLEXVEC_PAT_N; i++){
      int bx = x + 20 + (i % 3) * ((w - 40) / 3);
      int by = ty + 22 + (i / 3) * 76;
      int bw = (w - 40) / 3 - 8;
      bool on = (vecPatSel >= 0 && vecDoc.pats[vecPatSel].used &&
                 vecDoc.pats[vecPatSel].kind == i);
      fillRoundRect(bx, by, bw, 68, 12, on ? TH_ACCS : TH_SURF2);
      if(on) drawRoundRect(bx, by, bw, 68, 12, TH_PRIM);
      vecPatSwatch(i, bx + (bw - 44) / 2, by + 6, 44, 34);
      drawTextC(bx + bw / 2, by + 46, VEC_PAT_NAME[i], 1, on ? TH_PRIM : TH_TXT2);
    }
    char t[32];
    float sc = (vecPatSel >= 0 && vecDoc.pats[vecPatSel].used) ? vecDoc.pats[vecPatSel].scale : 12.0f;
    drawText(x + 20, ty + 180, "Tamano de la celda", 1, TH_TXT2);
    snprintf(t, sizeof(t), "%d", (int)sc);
    drawTextR(x + w - 20, ty + 180, t, 1, TH_TXT2);
    fillRoundRect(x + 20, ty + 204, w - 40, 8, 4, TH_TRACK);
    float fr = (sc - 2.0f) / 46.0f; if(fr < 0) fr = 0; if(fr > 1) fr = 1;
    fillRoundRect(x + 20, ty + 204, (int)((w - 40) * fr), 8, 4, TH_PRIM);
    fillCircle(x + 20 + (int)((w - 40) * fr), ty + 208, 13, TH_PRIM);
    fillRoundRect(x + 20, ty + 226, w - 40, 34, 12, TH_SURF2);
    drawTextC(x + w / 2, ty + 235, "Quitar el motivo", 1, TH_TXT);
  } else {
    drawText(x + 20, ty, "Segundo relleno", 1, TH_TXT2);
    for(int i = 0; i < 6; i++){
      int cx = x + 24 + i * 44, cy = ty + 22;
      fillCircle(cx + 16, cy + 16, 16, vecRgb(VEC_PAL[i * 2]));
      drawCircle(cx + 16, cy + 16, 16, TH_BORDER);
    }
    drawText(x + 20, ty + 70, "Segundo trazo", 1, TH_TXT2);
    for(int i = 0; i < 6; i++){
      int cx = x + 24 + i * 44, cy = ty + 92;
      fillCircle(cx + 16, cy + 16, 16, vecRgb(VEC_PAL[i * 2 + 1]));
      drawCircle(cx + 16, cy + 16, 16, TH_BORDER);
    }
    int sel = flexVecSelFirst(&vecDoc);
    bool has = (sel >= 0) && flexVecHasExtra(&vecDoc, sel);
    fillRoundRect(x + 20, ty + 146, w - 40, 36, 12, has ? TH_SURF2 : TH_TRACK);
    drawTextC(x + w / 2, ty + 156, "Quitar la apariencia extra", 1, has ? TH_TXT : TH_DIS);
    drawText(x + 20, ty + 196, "Un relleno y un trazo mas, por debajo de los", 1, TH_MUTE);
    drawText(x + 20, ty + 214, "principales. Un contorno doble se hace asi:", 1, TH_MUTE);
    drawText(x + 20, ty + 232, "segundo trazo mas grueso, y el principal encima.", 1, TH_MUTE);
  }
  (void)h;
}

static void vecDrawLayersPanel(int x, int y, int w, int h){
  drawText(x + 20, y + 16, "Capas", 3, TH_TXT);
  fillRoundRect(x + w - 116, y + 14, 96, 34, 12, TH_ACCS);
  drawTextC(x + w - 68, y + 23, "Nueva", 1, TH_PRIM);
  int ry = y + 62;
  for(int l = 0; l < FLEXVEC_MAX_LAYERS; l++){
    if(!vecDoc.layers[l].used) continue;
    if(ry + 44 > y + h - 8) break;
    bool act = (l == vecDoc.layerActive);
    fillRoundRect(x + 16, ry, w - 32, 40, 12, act ? TH_ACCS : TH_SURF2);
    // Ojo (visibilidad)
    uint16_t ic = vecDoc.layers[l].visible ? TH_TXT : TH_DIS;
    int ex = x + 40, ey = ry + 20;
    drawCircle(ex, ey, 7, ic); fillCircle(ex, ey, 3, ic);
    if(!vecDoc.layers[l].visible) strokeSegAA(ex - 8, ey + 8, ex + 8, ey - 8, 1.6f, TH_DANGER);
    // Candado (bloqueo)
    int lx = x + w - 46;
    uint16_t lc = vecDoc.layers[l].locked ? TH_DANGER : TH_DIS;
    fillRoundRect(lx - 6, ey - 2, 13, 11, 3, lc);
    drawCircle(lx, ey - 4, 5, lc);
    int n = 0;
    for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++) if(vecDoc.elems[e].layer == l) n++;
    char t[48]; snprintf(t, sizeof(t), "%s", vecDoc.layers[l].name);
    drawText(x + 62, ry + 6, t, 2, act ? TH_PRIM : TH_TXT);
    snprintf(t, sizeof(t), "%d objeto%s", n, n == 1 ? "" : "s");
    drawText(x + 62, ry + 24, t, 1, TH_TXT2);
    ry += 46;
  }
  drawText(x + 20, y + h - 26, "Toca el ojo o el candado \xC2\xB7 pulsa el nombre para activarla", 1, TH_MUTE);
}

// Botonera del panel "Organizar". Se declara como tabla y no como
// veinte llamadas seguidas para que el dibujo y el toque salgan del
// MISMO sitio: un boton que se pinta donde no se toca es el error
// clasico de una botonera, y asi no puede ocurrir.
typedef struct { const char* label; uint8_t row, col, span; } VecBtn;
static const VecBtn VEC_ARR_BTN[] = {
  { "Izquierda",   0, 0, 1 }, { "Centro",      0, 1, 1 }, { "Derecha",  0, 2, 1 },
  { "Arriba",      1, 0, 1 }, { "Medio",       1, 1, 1 }, { "Abajo",    1, 2, 1 },
  { "Al frente",   2, 0, 1 }, { "Adelante",    2, 1, 1 }, { "Atras",    2, 2, 1 },
  { "Unir",        3, 0, 1 }, { "Restar",      3, 1, 1 }, { "Intersecar",3, 2, 1 },
  { "Excluir",     4, 0, 1 }, { "Duplicar",    4, 1, 1 }, { "Borrar",   4, 2, 1 },
  { "Reflejar H",  5, 0, 1 }, { "Reflejar V",  5, 1, 1 }, { "Distribuir",5, 2, 1 },
};
#define VEC_ARR_N ((int)(sizeof(VEC_ARR_BTN) / sizeof(VEC_ARR_BTN[0])))

// Segunda pagina: lo que anade la Fase 2. Va en su propia pagina y no
// mezclada con la primera porque son operaciones de otra naturaleza --
// estas CREAN objetos nuevos a partir de los seleccionados, y tenerlas
// junto a "alinear a la izquierda" invitaria a tocarlas sin querer.
static const VecBtn VEC_ARR2_BTN[] = {
  { "Dividir",     0, 0, 1 }, { "Recortar",   0, 1, 1 }, { "Mascara",   0, 2, 1 },
  { "Fusion",      1, 0, 1 }, { "Repetir",    1, 1, 1 }, { "Pincel",    1, 2, 1 },
  { "Instancia",   2, 0, 1 }, { "Texto area", 2, 1, 1 }, { "Inclinar",  2, 2, 1 },
};
#define VEC_ARR2_N ((int)(sizeof(VEC_ARR2_BTN) / sizeof(VEC_ARR2_BTN[0])))

static void vecArrBtnRect(int i, int px, int py, int pw, int* x, int* y, int* w, int* h){
  const VecBtn* b = (vecArrPage == 0) ? &VEC_ARR_BTN[i] : &VEC_ARR2_BTN[i];
  int gap = 10, cols = 3;
  int bw = (pw - 32 - gap * (cols - 1)) / cols;
  *w = bw * b->span + gap * (b->span - 1);
  *h = 38;
  *x = px + 16 + b->col * (bw + gap);
  *y = py + 54 + b->row * (*h + 8);
}

static void vecDrawArrangePanel(int x, int y, int w, int h){
  drawText(x + 20, y + 14, vecArrPage == 0 ? "Organizar" : "Crear", 3, TH_TXT);
  int nsel = flexVecSelCount(&vecDoc);
  char t[48];
  snprintf(t, sizeof(t), "%d seleccionado%s", nsel, nsel == 1 ? "" : "s");
  drawTextR(x + w - 116, y + 20, t, 1, TH_TXT2);
  fillRoundRect(x + w - 104, y + 12, 84, 32, 12, TH_ACCS);
  drawTextC(x + w - 62, y + 21, vecArrPage == 0 ? "Crear" : "Alinear", 1, TH_PRIM);
  int nb = (vecArrPage == 0) ? VEC_ARR_N : VEC_ARR2_N;
  for(int i = 0; i < nb; i++){
    int bx, by, bw, bh;
    vecArrBtnRect(i, x, y, w, &bx, &by, &bw, &bh);
    if(by + bh > y + h - 6) break;
    const char* label = (vecArrPage == 0) ? VEC_ARR_BTN[i].label : VEC_ARR2_BTN[i].label;
    // Un boton que ahora mismo no se puede usar se ve APAGADO. Que
    // parezca activo y suelte un aviso al tocarlo enseña lo mismo, pero
    // despues de haber hecho perder el gesto.
    bool on;
    if(vecArrPage == 0){
      bool boolOp = (i >= 9 && i <= 12);
      on = boolOp ? (nsel >= 2) : (nsel >= 1);
      if(i == 17) on = (nsel >= 3);
    } else {
      on = (i == 6 || i == 7 || i == 8) ? (nsel >= 1) : (nsel >= 2);
      if(i == 4) on = (nsel >= 1);            // repetir necesita uno
      if(i == 7) on = (nsel >= 1);            // texto de area, uno
    }
    fillRoundRect(bx, by, bw, bh, 12, on ? TH_SURF2 : TH_TRACK);
    drawTextC(bx + bw / 2, by + bh / 2 - 7, label, 1, on ? TH_TXT : TH_DIS);
  }
  if(vecArrPage == 1){
    static const char* HINT[4] = {
      "Fusion: dos objetos -> pasos intermedios",
      "Pincel: selecciona el trazado y la figura",
      "Mascara: recorta con el objeto de arriba",
      "Repetir: toca varias veces para cambiar",
    };
    for(int i = 0; i < 4; i++)
      drawText(x + 20, y + h - 92 + i * 20, HINT[i], 1, TH_MUTE);
  }
}

static void vecDrawSharePanel(int x, int y, int w, int h){
  drawText(x + 20, y + 14, "Exportar y compartir", 3, TH_TXT);
  if(!vecShareOn){
    drawText(x + 20, y + 52, "Exporta el documento a SVG y descargalo", 1, TH_TXT2);
    drawText(x + 20, y + 72, "desde el movil, por Wi-Fi. Sin cables.", 1, TH_TXT2);
    fillRoundRect(x + 20, y + 104, w - 40, 46, 16, TH_PRIM);
    drawTextC(x + w / 2, y + 118, "Exportar SVG y compartir", 2, TH_ONACC);
    fillRoundRect(x + 20, y + 162, w - 40, 42, 14, TH_SURF2);
    drawTextC(x + w / 2, y + 175, "Guardar SVG en el dispositivo", 1, TH_TXT);
    fillRoundRect(x + 20, y + 212, w - 40, 42, 14, TH_SURF2);
    drawTextC(x + w / 2, y + 225, "Guardar PNG (imagen)", 1, TH_TXT);
    const char* net = gNetOnline ? wifiConnIP : "sin conexion Wi-Fi";
    char t[64]; snprintf(t, sizeof(t), "Red: %s", net);
    drawText(x + 20, y + 268, t, 1, TH_MUTE);
    if(vecSvgLen > 0){
      snprintf(t, sizeof(t), "Ultimo SVG: %d KB", (vecSvgLen + 512) / 1024);
      drawText(x + 20, y + 290, t, 1, TH_MUTE);
    }
    if(vecPngLen > 0){
      snprintf(t, sizeof(t), "Ultimo PNG: %d KB", (vecPngLen + 512) / 1024);
      drawText(x + 240, y + 290, t, 1, TH_MUTE);
    }
    drawText(x + 20, y + 312, "El SVG sigue siendo editable; el PNG es una foto.", 1, TH_MUTE);
    return;
  }
  // Compartiendo: el QR manda. Es lo que evita teclear una URL larga en
  // el movil, que es exactamente el trabajo que nadie quiere hacer.
  int qs = 0, cell = 0, ox = 0, oy = 0;
  if(vecQrMods && vecQrSize > 0){
    qs = vecQrSize;
    int avail = h - 118;
    if(avail > w - 40) avail = w - 40;
    cell = avail / (qs + 8);
    if(cell < 2) cell = 2;
    int side = cell * (qs + 8);
    ox = x + (w - side) / 2; oy = y + 46;
    fillRoundRect(ox, oy, side, side, 10, TC(255,255,255));
    for(int j = 0; j < qs; j++)
      for(int i = 0; i < qs; i++)
        if(vecQrMods[j * qs + i] & FLEXQR_DARK)
          fillRect(ox + (i + 4) * cell, oy + (j + 4) * cell, cell, cell, TC(0,0,0));
    int by = oy + side + 10;
    char t[128];
    uiLabelFit(flexShareUrl(), w - 40, 1, t, sizeof(t));
    drawTextC(x + w / 2, by, t, 1, TH_TXT2);
    snprintf(t, sizeof(t), "%u descarga%s", (unsigned)flexShareHits(),
             flexShareHits() == 1 ? "" : "s");
    drawTextC(x + w / 2, by + 20, t, 1, TH_MUTE);
    fillRoundRect(x + 20, y + h - 56, w - 40, 42, 14, TH_SURF2);
    drawTextC(x + w / 2, y + h - 43, "Dejar de compartir", 1, TH_DANGER);
  }
}

static void vecDrawPanel(){
  if(vecPanel == VP_NONE) return;
  setBuf(fb);
  int x, y, w, h; vecPanelRect(&x, &y, &w, &h);
  // Velo sobre el lienzo: separa el panel del documento sin taparlo.
  fillRectA(0, VEC_TOP, SCR_W, VEC_CANVAS_H, TH_SCRIM, 90);
  // Liquid Glass del sistema. Con el material plano activo, la misma
  // llamada cae en la tarjeta plana: la app no decide el material, lo
  // decide el tema -- que es justo el punto de tener un tema.
  if(uiGlass) drawLiquidGlassPanel(x, y, w, h, 26, thCard());
  else        drawGlassCardFlat(x, y, w, h, 26, thCard(), TH_WIN);
  fillRoundRect(x + w / 2 - 22, y + 7, 44, 4, 2, TH_DIV);
  switch(vecPanel){
    case VP_STYLE:   vecDrawStylePanel(x, y, w, h);   break;
    case VP_LAYERS:  vecDrawLayersPanel(x, y, w, h);  break;
    case VP_ARRANGE: vecDrawArrangePanel(x, y, w, h); break;
    case VP_SHARE:   vecDrawSharePanel(x, y, w, h);   break;
    default: break;
  }
}

// Menu del boton "..." de la cabecera.
static bool vecMenuOn = false;
static const char* VEC_MENU[] = { "Apariencia", "Capas", "Organizar", "Compartir",
                                  "Cuadricula", "Ajustar a rejilla", "Ajustar vista",
                                  "Guardar", "Documentos" };
#define VEC_MENU_N ((int)(sizeof(VEC_MENU) / sizeof(VEC_MENU[0])))
static void vecMenuGeom(int* x, int* y, int* w, int* h){
  *w = 232; *h = VEC_MENU_N * 42 + 16;
  *x = SCR_W - *w - 12; *y = VEC_HEAD_H - 6;
}
static void vecDrawMenu(){
  if(!vecMenuOn) return;
  setBuf(fb);
  int x, y, w, h; vecMenuGeom(&x, &y, &w, &h);
  if(uiGlass) drawLiquidGlassPanel(x, y, w, h, 20, thCard2());
  else        drawGlassCardFlat(x, y, w, h, 20, thCard2(), TH_WIN);
  for(int i = 0; i < VEC_MENU_N; i++){
    int ry = y + 8 + i * 42;
    drawText(x + 20, ry + 12, VEC_MENU[i], 2, TH_TXT);
    // Los interruptores ensenan su estado REAL, no un icono decorativo.
    if(i == 4 || i == 5){
      bool on = (i == 4) ? vecGrid : vecSnap;
      fillRoundRect(x + w - 62, ry + 9, 42, 24, 12, on ? TH_PRIM : TH_TRACK);
      fillCircle(x + w - 62 + (on ? 30 : 12), ry + 21, 9, TC(255,255,255));
    }
    if(i + 1 < VEC_MENU_N) hLine(x + 16, ry + 41, w - 32, TH_DIV);
  }
}

static void vecDrawToast(){
  if(!vecMsg[0]) return;
  uint32_t age = millis() - vecMsgMs;
  if(age > 2600){ vecMsg[0] = 0; return; }
  setBuf(fb);
  int w = textW(vecMsg, 2) + 48, x = (SCR_W - w) / 2, y = VEC_BOT - 74;
  uint8_t a = (age > 2100) ? (uint8_t)(255 - (age - 2100) * 255 / 500) : 255;
  fillRoundRectA(x, y, w, 42, 21, TH_SURF2, a);
  drawTextCA(SCR_W / 2, y + 13, vecMsg, 2, TH_TXT, a);
}

static void vecRenderAll(){
  setBuf(fb);
  vecRenderDoc();
  if(vecCache) fbCopyBand(vecCache, VEC_TOP, VEC_BOT - 1);
  setBuf(fb);
  vecDrawSelection();
  if(vecTool == VT_NODE) vecDrawNodes();
  vecDrawPenPreview();
  vecDrawHeader();
  vecDrawTools();
  vecDrawPanel();
  vecDrawMenu();
  vecDrawToast();
  flxFlushAll();
  vecDirtyAny = false;
}

// #############################################################
// ##  DOCUMENTOS EN LA NOR FLASH  ·  /Vector/*.fxv
// ##  ------------------------------------------------------
// ##  NO HAY MICROSD EN ESTA PLACA y no se usa ninguna: todo vive en
// ##  los 16 MB de NOR Flash, a traves de LittleFS y de FlexOS_FS, que
// ##  es el unico camino del sistema al almacenamiento.
// ##
// ##  El formato .fxv es el volcado nativo del documento
// ##  (flexVecSerialize). Un documento tipico -- unas decenas de
// ##  objetos -- ronda los 30 KB; el techo absoluto, con el pool de
// ##  nodos lleno, son ~145 KB. En una particion de datos de varios
// ##  MB eso da sitio de sobra, y el SVG exportado es aparte y mas
// ##  pequeno todavia.
// #############################################################
#define VEC_SESS_PATH  FS_DIR_SESS "/vector.bin"
#define VEC_SESS_VER   1

typedef struct {
  uint8_t  view, tool, grid, snap;
  float    zoom, panX, panY;
  uint32_t fill, stroke;
  float    strokeW;
  uint8_t  alpha, pad[3];
  char     path[FLEXFS_PATH_MAX];
} VecSessV1;

static void vecReload(){
  vecListN = 0;
  if(!flexFsReady()) return;
  vecListN = flexFsList(FLEXFS_DIR_VECTOR, vecList, VEC_LIST_MAX);
  if(vecListN < 0) vecListN = 0;
  if(vecSelIdx >= vecListN) vecSelIdx = -1;
}
static void vecPathOf(int i, char* out, size_t n){
  if(i < 0 || i >= vecListN){ if(n) out[0] = 0; return; }
  snprintf(out, n, "%s/%s", FLEXFS_DIR_VECTOR, vecList[i].name);
}

// Guarda el documento en su archivo. El buffer se pide AQUI y se suelta
// al salir: guardar es una accion puntual, y tener 145 KB de PSRAM
// reservados toda la sesion "por si el usuario guarda" seria pagar el
// peor caso todo el rato.
static bool vecSaveDoc(){
  if(!vecReady || !vecPath[0] || !flexFsReady()) return false;
  size_t need = flexVecSerializeSize(&vecDoc);
  if(need == 0) return false;
  uint8_t* buf = (uint8_t*)heap_caps_malloc(need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!buf) buf = (uint8_t*)malloc(need);
  if(!buf){ vecToast("Memoria insuficiente para guardar"); return false; }
  int n = flexVecSerialize(&vecDoc, buf, need);
  bool ok = false;
  // ATOMICO: se escribe a un temporal y se renombra. Un corte de
  // corriente a mitad deja el documento ANTERIOR entero, nunca uno a
  // medias -- que para el usuario es exactamente lo mismo que perderlo.
  if(n > 0) ok = flexFsWriteBinAtomic(vecPath, buf, (size_t)n);
  free(buf);
  if(ok) flexVecSetSaved(&vecDoc);
  else   vecToast("No se pudo guardar");
  return ok;
}

static bool vecLoadDoc(const char* path){
  if(!vecReady || !path || !path[0] || !flexFsReady()) return false;
  uint32_t sz = flexFsSize(path);
  if(sz == 0 || sz > 1024u * 1024u) return false;
  uint8_t* buf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!buf) buf = (uint8_t*)malloc(sz);
  if(!buf){ vecToast("Memoria insuficiente"); return false; }
  int n = flexFsReadBin(path, buf, sz);
  bool ok = (n > 0) && (flexVecDeserialize(&vecDoc, buf, (size_t)n) == FLEXVEC_OK);
  free(buf);
  if(!ok){
    // Un .fxv corrupto NO tumba la app: se dice y se deja un documento
    // vacio valido, que es un estado del que el usuario puede salir.
    vecToast("El documento no se pudo leer");
    flexVecNew(&vecDoc, 480, 800, 3);
    return false;
  }
  snprintf(vecPath, sizeof(vecPath), "%s", path);
  vecFitView();
  return true;
}

static void vecOpen(int i){
  char p[FLEXFS_PATH_MAX];
  vecPathOf(i, p, sizeof(p));
  if(!p[0]) return;
  if(!vecArenaInit()){ vecToast("Memoria insuficiente"); return; }
  vecLoadDoc(p);
  vecView = 1; vecPanel = VP_NONE; vecMenuOn = false;
  vecTool = VT_SELECT; vecPenElem = -1; vecGest = VG_NONE;
  vecInvalidate();
  sessMarkDirty(IC_VECTOR);
  vecRenderAll();
}

static void vecNewDoc(){
  if(!vecArenaInit()){ vecToast("Memoria insuficiente"); return; }
  char full[FLEXFS_PATH_MAX];
  if(!flexFsNewName(FLEXFS_DIR_VECTOR, "Vector", FLEXFS_EXT_VECTOR, full, sizeof(full))){
    vecToast("No se pudo crear el documento");
    return;
  }
  flexVecNew(&vecDoc, 480, 800, 3);
  snprintf(vecDoc.name, sizeof(vecDoc.name), "Vector");
  snprintf(vecPath, sizeof(vecPath), "%s", full);
  if(!vecSaveDoc()) return;
  vecReload();
  vecView = 1; vecPanel = VP_NONE; vecMenuOn = false;
  vecTool = VT_SELECT; vecPenElem = -1; vecGest = VG_NONE;
  vecFitView();
  sessMarkDirty(IC_VECTOR);
  vecRenderAll();
}

// #############################################################
// ##  EXPORTAR A SVG Y COMPARTIR POR WI-FI
// ##  ------------------------------------------------------
// ##  El SVG se genera en PSRAM y desde ahi se hacen las dos cosas:
// ##  se guarda en /Vector y se sirve por HTTP. No se copia dos veces
// ##  ni se vuelve a leer del disco para enviarlo.
// ##
// ##  El QR codifica la URL con el testigo de sesion. Es lo que
// ##  convierte "apunta esta direccion" en "apunta con la camara".
// #############################################################
static void vecSvgName(char* out, size_t n){
  char stem[FLEXFS_NAME_MAX];
  if(vecPath[0]) flexFsStem(vecPath, stem, sizeof(stem));
  else snprintf(stem, sizeof(stem), "Documento");
  // El nombre viaja en una URL y en una cabecera HTTP: se deja en
  // caracteres seguros aqui, en el origen, y no se "arregla" despues.
  size_t w = 0;
  for(const char* p = stem; *p && w + 5 < n; p++){
    char c = *p;
    if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
       c == '-' || c == '_') out[w++] = c;
    else if(c == ' ') out[w++] = '_';
  }
  if(w == 0){ snprintf(out, n, "documento.svg"); return; }
  out[w] = 0;
  snprintf(out + w, n - w, ".svg");
}

static bool vecExportSvg(bool selOnly){
  if(!vecReady || !vecSvg) return false;
  int n = flexVecExportSVGEx(&vecDoc, vecSvg, FLEXVEC_SVG_BYTES, selOnly ? 1 : 0, 0,
                             vecMeasure, NULL);
  if(n < 0){
    vecToastErr(n);
    vecSvgLen = 0;
    return false;
  }
  vecSvgLen = n;
  return true;
}

static bool vecSaveSvgToDisk(){
  if(!vecExportSvg(false)) return false;
  char name[FLEXFS_NAME_MAX]; vecSvgName(name, sizeof(name));
  char path[FLEXFS_PATH_MAX];
  snprintf(path, sizeof(path), "%s/%s", FLEXFS_DIR_VECTOR, name);
  if(!flexFsWriteBinAtomic(path, vecSvg, (size_t)vecSvgLen)){
    vecToast("No se pudo guardar el SVG");
    return false;
  }
  char t[64]; snprintf(t, sizeof(t), "SVG guardado (%d KB)", (vecSvgLen + 512) / 1024);
  vecToast(t);
  vecReload();
  return true;
}

// #############################################################
// ##  EXPORTAR PNG
// ##  ------------------------------------------------------
// ##  Sin un solo megabyte de pico. El truco es que la app YA tiene el
// ##  cuadro rasterizado en su cache RGB565 (768 KB, que estan ahi de
// ##  todas formas): se rasteriza la mesa de trabajo ahi y se va
// ##  convirtiendo FILA A FILA a RGB888 para el compresor. Lo unico que
// ##  se pide prestado son el estado del compresor y el buffer de
// ##  salida, y los dos se sueltan al terminar.
// ##
// ##  El PNG es un COMPLEMENTO del SVG, no su sustituto: un PNG es una
// ##  foto y un SVG sigue siendo editable. Por eso el boton principal
// ##  de compartir sigue siendo el de SVG.
// #############################################################
static bool vecExportPng(){
  if(!vecReady) return false;
  if(!vecCache){
    vecCache = (uint16_t*)heap_caps_aligned_alloc(64, (size_t)SCR_W * SCR_H * 2,
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(!vecCache){ vecToast("Memoria insuficiente"); return false; }
  }
  float aw = vecDoc.artW, ah = vecDoc.artH;
  if(aw < 1 || ah < 1) return false;
  float sc = (float)SCR_W / aw;
  if((float)SCR_H / ah < sc) sc = (float)SCR_H / ah;
  int pw = (int)(aw * sc), ph = (int)(ah * sc);
  if(pw < 1) pw = 1; if(pw > SCR_W) pw = SCR_W;
  if(ph < 1) ph = 1; if(ph > SCR_H) ph = SCR_H;

  // La mesa de trabajo, sobre blanco y SIN cuadricula ni marco: lo que
  // se exporta es el dibujo, no la interfaz del editor.
  FlexVecView v;
  v.m[0] = sc; v.m[1] = 0; v.m[2] = 0; v.m[3] = sc; v.m[4] = 0; v.m[5] = 0;
  v.clipX0 = 0; v.clipY0 = 0; v.clipX1 = pw - 1; v.clipY1 = ph - 1;
  setBuf(vecCache);
  int sx0 = gClipX0, sx1 = gClipX1, sy0 = gClipY0, sy1 = gClipY1;
  gClipX0 = 0; gClipX1 = pw - 1; gClipY0 = 0; gClipY1 = ph - 1;
  fillRect(0, 0, pw, ph, TC(255,255,255));
  int16_t order[FLEXVEC_MAX_ELEMS];
  int n = flexVecPaintOrder(&vecDoc, order, FLEXVEC_MAX_ELEMS);
  for(int i = 0; i < n; i++) vecDrawElem(order[i], &v);
  gClipX0 = sx0; gClipX1 = sx1; gClipY0 = sy0; gClipY1 = sy1;
  setBuf(fb);
  vecCacheOk = false;                     // la cache ya no es la del lienzo

  size_t cap = 512u * 1024u;              // con compresion sobra; si no, se dice
  uint8_t* out = (uint8_t*)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  FlexVecPng* png = (FlexVecPng*)heap_caps_malloc(sizeof(FlexVecPng),
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  uint8_t* row = (uint8_t*)malloc((size_t)pw * 3);
  if(!out || !png || !row){
    free(out); free(png); free(row);
    vecToast("Memoria insuficiente para el PNG");
    return false;
  }
  bool ok = (flexVecPngBegin(png, pw, ph, out, cap) == FLEXVEC_OK);
  for(int y = 0; y < ph && ok; y++){
    const uint16_t* src = vecCache + (size_t)y * SCR_W;
    for(int x = 0; x < pw; x++){
      uint16_t c = src[x];
      // RGB565 -> RGB888 replicando los bits altos: es la conversion que
      // deja el blanco en 255 y el negro en 0 exactos.
      uint8_t r = (uint8_t)(((c >> 11) & 0x1F) * 255 / 31);
      uint8_t g = (uint8_t)(((c >> 5) & 0x3F) * 255 / 63);
      uint8_t b = (uint8_t)((c & 0x1F) * 255 / 31);
      row[x * 3] = r; row[x * 3 + 1] = g; row[x * 3 + 2] = b;
    }
    ok = (flexVecPngRow(png, row) == FLEXVEC_OK);
  }
  int total = ok ? flexVecPngEnd(png) : -1;
  free(row); free(png);
  if(total <= 0){
    free(out);
    vecToast("La imagen no cabe: exporta SVG");
    return false;
  }
  char name[FLEXFS_NAME_MAX]; vecSvgName(name, sizeof(name));
  char* dot = strrchr(name, '.');
  if(dot) snprintf(dot, (size_t)(name + sizeof(name) - dot), ".png");
  char path[FLEXFS_PATH_MAX];
  snprintf(path, sizeof(path), "%s/%s", FLEXFS_DIR_VECTOR, name);
  bool wrote = flexFsWriteBinAtomic(path, out, (size_t)total);
  free(out);
  vecPngLen = wrote ? total : 0;
  if(!wrote){ vecToast("No se pudo guardar el PNG"); return false; }
  char m[64];
  snprintf(m, sizeof(m), "PNG guardado (%d KB)", (total + 512) / 1024);
  vecToast(m);
  vecReload();
  vecInvalidate();
  return true;
}

static void vecShareStop(){
  if(vecShareOn){ flexShareStop(); vecShareOn = false; }
  if(vecQrMods){ free(vecQrMods); vecQrMods = NULL; }
  vecQrSize = 0;
}

static void vecShareStart(){
  vecShareStop();
  if(!gNetOnline){ vecToast(flexShareErrText(FLEXSHARE_E_OFFLINE)); return; }
  if(!vecExportSvg(false)) return;
  char name[FLEXFS_NAME_MAX]; vecSvgName(name, sizeof(name));
  int rc = flexShareStart(name, (const uint8_t*)vecSvg, (size_t)vecSvgLen);
  if(rc != FLEXSHARE_OK){ vecToast(flexShareErrText(rc)); return; }
  if(!vecQrMods) vecQrMods = (uint8_t*)heap_caps_malloc(FLEXQR_BUF_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!vecQrMods) vecQrMods = (uint8_t*)malloc(FLEXQR_BUF_BYTES);
  if(vecQrMods){
    int qs = 0, qv = 0;
    // Correccion M: el punto medio. Con L el codigo es mas pequeno pero
    // una pantalla con reflejos lo estropea; con Q o H crece tanto que
    // los modulos se quedan por debajo de lo que la camara resuelve.
    if(flexQrEncode(flexShareUrl(), FLEXQR_ECC_M, 1, vecQrMods, FLEXQR_BUF_BYTES, &qs, &qv) == FLEXQR_OK)
      vecQrSize = qs;
    else { free(vecQrMods); vecQrMods = NULL; vecQrSize = 0; }
  }
  vecShareOn = true;
  vecPanel = VP_SHARE;
}

// #############################################################
// ##  GALERIA DE DOCUMENTOS
// ##  ------------------------------------------------------
// ##  Misma estructura que la galeria de Paint -- lista real de la
// ##  carpeta, kit de archivos del sistema para renombrar, borrar y
// ##  papelera -- porque es la que el usuario ya conoce del resto de
// ##  Flex OS. Lo que NO se hace es una miniatura por documento:
// ##  dibujarla obligaria a rasterizar cada archivo al entrar, y en una
// ##  lista de veinte eso son veinte rasterizados antes de ver nada.
// ##  La ficha ensena el nombre, el tamano y los objetos, que salen de
// ##  leer la cabecera del archivo: 64 bytes en vez de un documento.
// #############################################################
#define VEC_CARD_TOP  118
#define VEC_CARD_H     92

static int vecMaxScroll(){
  int rows = vecListN + 1;                 // +1: la ficha de "nuevo"
  int total = VEC_CARD_TOP + rows * (VEC_CARD_H + 12) + 24;
  int vis = SCR_H - navBarH();
  return total > vis ? total - vis : 0;
}
static int vecCardY(int i){ return VEC_CARD_TOP + i * (VEC_CARD_H + 12) - vecScroll; }

// Objetos que declara un .fxv, leyendo SOLO su cabecera.
static int vecFileObjects(const char* path){
  uint8_t hdr[96];
  int n = flexFsReadAt(path, 0, hdr, sizeof(hdr));
  if(n < 24) return -1;
  uint32_t magic = 0; memcpy(&magic, hdr, 4);
  if(magic != FLEXVEC_MAGIC) return -1;
  uint16_t nodeN = 0; memcpy(&nodeN, hdr + 22, 2);
  return (int)nodeN;                        // nodos, que es la medida honesta que hay ahi
}

static void vecRenderGallery(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  strokeSegAA(30, 30, 18, 22, 2.4f, TH_NAV);
  strokeSegAA(18, 22, 30, 14, 2.4f, TH_NAV);
  drawTextC(SCR_W / 2, 22, "Flex Vector Pro", 3, TH_TXT);
  drawTextC(SCR_W / 2, 56, "Editor vectorial", 1, TH_TXT2);
  if(!flexFsReady()){
    fkNoFsScreen("Flex Vector Pro");
    return;
  }
  // Ficha de documento nuevo, siempre la primera.
  int y = vecCardY(0);
  if(y > -VEC_CARD_H && y < SCR_H){
    fillRoundRect(16, y, SCR_W - 32, VEC_CARD_H, 20, TH_ACCS);
    drawRoundRect(16, y, SCR_W - 32, VEC_CARD_H, 20, TH_PRIM);
    int cx = 62, cy = y + VEC_CARD_H / 2;
    fillRect(cx - 13, cy - 2, 26, 4, TH_PRIM);
    fillRect(cx - 2, cy - 13, 4, 26, TH_PRIM);
    drawText(100, y + 26, "Documento nuevo", 3, TH_PRIM);
    drawText(100, y + 56, "Lienzo de 480 x 800 \xC2\xB7 3 capas", 1, TH_TXT2);
  }
  for(int i = 0; i < vecListN; i++){
    y = vecCardY(i + 1);
    if(y <= -VEC_CARD_H || y >= SCR_H) continue;
    bool sel = (i == vecSelIdx);
    uiSurface(16, y, SCR_W - 32, VEC_CARD_H, 20, sel ? 1 : 0);
    if(sel) drawRoundRect(16, y, SCR_W - 32, VEC_CARD_H, 20, TH_PRIM);
    char stem[FLEXFS_NAME_MAX]; flexFsStem(vecList[i].name, stem, sizeof(stem));
    char fit[40]; uiLabelFit(stem, SCR_W - 150, 3, fit, sizeof(fit));
    drawText(96, y + 22, fit, 3, TH_TXT);
    char path[FLEXFS_PATH_MAX]; vecPathOf(i, path, sizeof(path));
    char sz[24]; flexFsFmtSize(vecList[i].size, sz, sizeof(sz));
    int nodes = vecFileObjects(path);
    char sub[64];
    if(nodes >= 0) snprintf(sub, sizeof(sub), "%s \xC2\xB7 %d nodos", sz, nodes);
    else           snprintf(sub, sizeof(sub), "%s", sz);
    drawText(96, y + 56, sub, 1, TH_TXT2);
    // Marca del documento: un trazado en miniatura, dibujado con las
    // primitivas del sistema. No es el contenido del archivo y no
    // pretende serlo -- es un icono, y por eso no cuesta leer nada.
    int ix = 36, iy = y + VEC_CARD_H / 2;
    strokeSegAA(ix - 14, iy + 10, ix - 2, iy - 10, 2.0f, TH_PRIM);
    strokeSegAA(ix - 2, iy - 10, ix + 14, iy + 4, 2.0f, TH_PRIM);
    fillCircle(ix - 14, iy + 10, 4, TH_PRIM);
    fillCircle(ix + 14, iy + 4, 4, TH_PRIM);
  }
  if(vecListN == 0)
    drawTextC(SCR_W / 2, VEC_CARD_TOP + VEC_CARD_H + 40 - vecScroll,
              "Aun no hay documentos", 2, TH_MUTE);
  flxFlushAll();
}

// #############################################################
// ##  GESTOS DEL LIENZO
// ##  ------------------------------------------------------
// ##  UN DEDO NO ES UN RATON. Todo lo que aqui se decide sale de eso:
// ##   · los tiradores de la caja son circulos de 11 px de radio, no
// ##     cuadrados de 4 px;
// ##   · la holgura de seleccion se mide en PIXELES y se convierte a
// ##     unidades con el zoom, para que sea siempre el mismo dedo;
// ##   · un arrastre entero (mover, escalar, girar, mover un nodo) es
// ##     UN paso de deshacer, no cincuenta -- la transaccion se abre al
// ##     tocar y se cierra al levantar;
// ##   · la pinza se lee del multitactil REAL del GT911, no se simula.
// #############################################################
static void vecGestBegin(const char* label){
  if(!vecDoc.undoOpen) flexVecUndoBegin(&vecDoc, label);
}
static void vecGestEnd(){
  if(vecDoc.undoOpen) flexVecUndoCommit(&vecDoc);
  if(flexVecUndoHistoryLost(&vecDoc)) vecToast("Historial reiniciado: documento muy grande");
  sessMarkDirty(IC_VECTOR);
}

// Pinza y desplazamiento a dos dedos. Devuelve true si el gesto es
// suyo, y entonces NADIE mas ve ese contacto en esta vuelta.
static bool vecPinchUpdate(){
  if(millis() - gtFingersMs > 150 || gtFingers < 2){
    if(vecPinch){ vecPinch = false; vecPinchMs = millis(); }
    return false;
  }
  int n = gtPollMulti();
  if(n < 2){
    if(vecPinch){ vecPinch = false; vecPinchMs = millis(); }
    return false;
  }
  int i0 = -1, i1 = -1;
  for(int i = 0; i < KB_MAXPOINTS; i++){
    if(!gKbPoints[i].active) continue;
    if(i0 < 0) i0 = i; else if(i1 < 0){ i1 = i; break; }
  }
  if(i0 < 0 || i1 < 0) return vecPinch;
  float ax = (float)gKbPoints[i0].x, ay = (float)gKbPoints[i0].y;
  float bx = (float)gKbPoints[i1].x, by = (float)gKbPoints[i1].y;
  float dist = sqrtf((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
  float cx = (ax + bx) * 0.5f, cy = (ay + by) * 0.5f;
  if(cy < VEC_TOP || cy >= VEC_BOT) return false;      // fuera del lienzo: no es nuestro
  if(!vecPinch){
    vecPinch = true;
    vecPinchD0 = dist > 8 ? dist : 8;
    vecPinchZ0 = vecZoom;
    vecPinchCX0 = vecDocX((int)cx);
    vecPinchCY0 = vecDocY((int)cy);
    vecGest = VG_PINCH;
    return true;
  }
  float k = dist / vecPinchD0;
  vecZoom = vecPinchZ0 * k;
  if(vecZoom < VEC_MIN_ZOOM) vecZoom = VEC_MIN_ZOOM;
  if(vecZoom > VEC_MAX_ZOOM) vecZoom = VEC_MAX_ZOOM;
  // El punto del documento que estaba bajo el centro de los dos dedos se
  // queda ahi: es lo que hace que la pinza se sienta "pegada" al papel.
  vecPanX = vecPinchCX0 - cx / vecZoom;
  vecPanY = vecPinchCY0 - ((float)cy - (float)VEC_TOP) / vecZoom;
  vecClampView();
  vecInvalidate();
  vecMarkDirtyAll();
  return true;
}

// Tirador de la caja delimitadora bajo el dedo: 0..7 escalado, 8 giro.
static int vecHitHandle(int px, int py){
  float bx0, by0, bx1, by1;
  if(flexVecSelBBox(&vecDoc, &bx0, &by0, &bx1, &by1) != FLEXVEC_OK) return -1;
  int x0 = vecPixX(bx0), y0 = vecPixY(by0), x1 = vecPixX(bx1), y1 = vecPixY(by1);
  int rx = (x0 + x1) / 2, ry = y0 - 34;
  int r2 = (VEC_HANDLE_R + 8) * (VEC_HANDLE_R + 8);
  if((px - rx) * (px - rx) + (py - ry) * (py - ry) <= r2) return 8;
  for(int i = 0; i < 8; i++){
    int hx, hy; vecHandleXY(i, x0, y0, x1, y1, &hx, &hy);
    if((px - hx) * (px - hx) + (py - hy) * (py - hy) <= r2) return i;
  }
  return -1;
}

// Aplica el estilo de trabajo a la seleccion. Si no hay nada
// seleccionado, el estilo se queda para lo SIGUIENTE que se dibuje --
// que es como se comporta un editor vectorial y evita tener que
// seleccionar algo solo para elegir un color.
static void vecApplyStyle(int what){
  int n = 0;
  for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++){
    if(!vecElemOk(e) || !(vecDoc.elems[e].flags & FLEXVEC_EF_SEL)) continue;
    if(n == 0) flexVecUndoBegin(&vecDoc, "Estilo");
    switch(what){
      case 0: flexVecSetFill(&vecDoc, e, vecFillRGB, 255); break;
      case 1: flexVecSetStroke(&vecDoc, e, vecStrokeRGB, 255, vecStrokeW); break;
      case 2: flexVecSetAlpha(&vecDoc, e, vecObjAlpha); break;
      case 3: flexVecSetNoFill(&vecDoc, e); break;
      case 4: flexVecSetNoStroke(&vecDoc, e); break;
      default: break;
    }
    n++;
  }
  if(n){ flexVecUndoCommit(&vecDoc); vecInvalidate(); sessMarkDirty(IC_VECTOR); }
}

// Estilo de trabajo -> objeto recien creado.
static void vecStyleNew(int e){
  if(!vecElemOk(e)) return;
  flexVecSetFill(&vecDoc, e, vecFillRGB, 255);
  flexVecSetStroke(&vecDoc, e, vecStrokeRGB, 255, vecStrokeW);
  flexVecSetAlpha(&vecDoc, e, vecObjAlpha);
}

// Entrada de texto pendiente: por que se abrio el dialogo del teclado.
enum { VEC_ASK_NONE = 0, VEC_ASK_TEXT, VEC_ASK_RENAME };
static int   vecAsk = VEC_ASK_NONE;
static float vecAskX = 0, vecAskY = 0;
static uint32_t vecPressMs = 0;
static bool  vecLongFired = false;

static void vecFinishPen(){
  if(vecPenElem >= 0 && vecElemOk(vecPenElem) && vecDoc.elems[vecPenElem].count < 2){
    flexVecDelete(&vecDoc, vecPenElem);        // un ancla suelta no es un trazado
    vecInvalidate();
  }
  vecPenElem = -1;
  vecPenDrag = false;
}

static void vecCanvasPress(int px, int py){
  float dx = vecDocX(px), dy = vecDocY(py);
  vecGestX0 = dx; vecGestY0 = dy;
  vecPressMs = millis();
  vecLongFired = false;
  vecMarqX0 = vecMarqX1 = px; vecMarqY0 = vecMarqY1 = py;

  if(vecTool == VT_SELECT){
    int h = vecHitHandle(px, py);
    if(h >= 0){
      flexVecSelBBox(&vecDoc, &vecGestBX0, &vecGestBY0, &vecGestBX1, &vecGestBY1);
      vecGestHandle = h;
      vecGest = VG_HANDLE;
      vecGestBegin(h == 8 ? "Girar" : "Escalar");
      return;
    }
    int e = flexVecHitTest(&vecDoc, dx, dy, vecTol());
    if(e >= 0){
      if(!(vecDoc.elems[e].flags & FLEXVEC_EF_SEL)) flexVecSelect(&vecDoc, e, 0);
      vecGestElem = e;
      vecGest = VG_MOVE;
      vecGestBegin("Mover");
      vecInvalidate();                    // el objeto sale de la cache mientras se mueve
      return;
    }
    flexVecSelectNone(&vecDoc);
    vecGest = VG_MARQUEE;
    vecMarkDirtyAll();
    return;
  }

  if(vecTool == VT_NODE){
    int e = flexVecSelFirst(&vecDoc);
    if(e >= 0){
      int part = 0;
      int nd = flexVecHitNode(&vecDoc, e, dx, dy, vecTol() * 1.4f, &part);
      if(nd >= 0){
        vecGestElem = e; vecGestNode = nd; vecGestPart = part;
        vecGest = (part == 0) ? VG_NODE : VG_HANDLEBAR;
        vecGestBegin(part == 0 ? "Mover ancla" : "Tirador");
        return;
      }
    }
    int hit = flexVecHitTest(&vecDoc, dx, dy, vecTol());
    if(hit >= 0){ flexVecSelect(&vecDoc, hit, 0); vecMarkDirtyAll(); }
    else { flexVecSelectNone(&vecDoc); vecMarkDirtyAll(); }
    vecGest = VG_NONE;
    return;
  }

  if(vecTool == VT_PEN){
    if(vecSnap){ dx = flexVecSnap(dx, VEC_GRID_STEP); dy = flexVecSnap(dy, VEC_GRID_STEP); }
    if(vecPenElem < 0 || !vecElemOk(vecPenElem)){
      int e = flexVecAddPath(&vecDoc);
      if(e < 0){ vecToastErr(e); return; }
      vecStyleNew(e);
      flexVecSetNoFill(&vecDoc, e);
      vecPenElem = e;
      flexVecSelect(&vecDoc, e, 0);
    } else {
      // Tocar sobre el PRIMER ancla cierra la figura. Es el gesto de
      // Illustrator y el unico que no obliga a buscar un boton.
      const FlexVecElem* el = &vecDoc.elems[vecPenElem];
      if(el->count >= 3){
        const FlexVecNode* nd = vecDoc.arena.nodes + el->first;
        float fx, fy; flexVecMatApply(el->m, nd[0].x, nd[0].y, &fx, &fy);
        float d = (fx - dx) * (fx - dx) + (fy - dy) * (fy - dy);
        if(d <= vecTol() * vecTol() * 2.5f){
          flexVecPathClose(&vecDoc, vecPenElem, 1);
          vecFinishPen();
          vecInvalidate(); vecMarkDirtyAll();
          sessMarkDirty(IC_VECTOR);
          return;
        }
      }
    }
    int rc = flexVecNodeAppend(&vecDoc, vecPenElem, dx, dy, dx, dy, 0);
    if(rc != FLEXVEC_OK){ vecToastErr(rc); return; }
    vecPenAX = dx; vecPenAY = dy;
    vecPenDrag = false;
    vecGest = VG_PEN;
    vecInvalidate(); vecMarkDirtyAll();
    return;
  }

  if(vecTool == VT_TEXT){
    vecAskX = dx; vecAskY = dy;
    vecAsk = VEC_ASK_TEXT;
    fkNameOpen("Texto", "");
    return;
  }
  // Herramientas de forma: goma elastica.
  vecGest = VG_MARQUEE;
}

static void vecCanvasMove(int px, int py){
  float dx = vecDocX(px), dy = vecDocY(py);
  switch(vecGest){
    case VG_MOVE: {
      float ndx = dx - vecGestX0, ndy = dy - vecGestY0;
      if(vecSnap){
        // El AJUSTE se aplica al desplazamiento acumulado, no a cada
        // incremento: acumular redondeos por cuadro haria que el objeto
        // se fuera desviando mientras se arrastra.
        float bx0, by0, bx1, by1;
        if(flexVecSelBBox(&vecDoc, &bx0, &by0, &bx1, &by1) == FLEXVEC_OK){
          float want = flexVecSnap(bx0 + ndx, VEC_GRID_STEP);
          ndx = want - bx0;
          want = flexVecSnap(by0 + ndy, VEC_GRID_STEP);
          ndy = want - by0;
        }
      }
      if(ndx == 0 && ndy == 0) break;
      vecMarkDirtySel();
      float m[6] = { 1, 0, 0, 1, ndx, ndy };
      flexVecTransformSel(&vecDoc, m, 0);
      vecGestX0 = dx - (ndx - (dx - vecGestX0));   // el resto no aplicado se conserva
      vecGestY0 = dy - (ndy - (dy - vecGestY0));
      vecMarkDirtySel();
      break; }
    case VG_HANDLE: {
      float cx = (vecGestBX0 + vecGestBX1) * 0.5f, cy = (vecGestBY0 + vecGestBY1) * 0.5f;
      vecMarkDirtySel();
      if(vecGestHandle == 8){
        float a0 = atan2f(vecGestY0 - cy, vecGestX0 - cx);
        float a1 = atan2f(dy - cy, dx - cx);
        float d = a1 - a0;
        if(d != 0){
          float m[6];
          float c = cosf(d), s = sinf(d);
          m[0] = c; m[1] = s; m[2] = -s; m[3] = c;
          m[4] = cx - (c * cx - s * cy);
          m[5] = cy - (s * cx + c * cy);
          flexVecTransformSel(&vecDoc, m, 0);
          vecGestX0 = dx; vecGestY0 = dy;
        }
      } else {
        float w = vecGestBX1 - vecGestBX0, h = vecGestBY1 - vecGestBY0;
        if(w < 1e-3f) w = 1e-3f;
        if(h < 1e-3f) h = 1e-3f;
        // El tirador opuesto es el ancla: se queda quieto, como en
        // cualquier editor. Con los tiradores de los lados solo cambia
        // un eje.
        int opp = (vecGestHandle + 4) & 7;
        float ox, oy;
        switch(opp){
          case 0: ox = vecGestBX0; oy = vecGestBY0; break;
          case 1: ox = (vecGestBX0 + vecGestBX1) * 0.5f; oy = vecGestBY0; break;
          case 2: ox = vecGestBX1; oy = vecGestBY0; break;
          case 3: ox = vecGestBX1; oy = (vecGestBY0 + vecGestBY1) * 0.5f; break;
          case 4: ox = vecGestBX1; oy = vecGestBY1; break;
          case 5: ox = (vecGestBX0 + vecGestBX1) * 0.5f; oy = vecGestBY1; break;
          case 6: ox = vecGestBX0; oy = vecGestBY1; break;
          default:ox = vecGestBX0; oy = (vecGestBY0 + vecGestBY1) * 0.5f; break;
        }
        bool horiz = (vecGestHandle == 3 || vecGestHandle == 7);
        bool vert  = (vecGestHandle == 1 || vecGestHandle == 5);
        float sx = 1, sy = 1;
        if(!vert)  sx = (fabsf(vecGestX0 - ox) > 1e-3f) ? (dx - ox) / (vecGestX0 - ox) : 1;
        if(!horiz) sy = (fabsf(vecGestY0 - oy) > 1e-3f) ? (dy - oy) / (vecGestY0 - oy) : 1;
        if(fabsf(sx) < 1e-3f) sx = (sx < 0 ? -1e-3f : 1e-3f);
        if(fabsf(sy) < 1e-3f) sy = (sy < 0 ? -1e-3f : 1e-3f);
        float m[6];
        m[0] = sx; m[1] = 0; m[2] = 0; m[3] = sy;
        m[4] = ox - sx * ox; m[5] = oy - sy * oy;
        flexVecTransformSel(&vecDoc, m, 0);
        vecGestX0 = dx; vecGestY0 = dy;
      }
      vecMarkDirtySel();
      break; }
    case VG_NODE:
      if(vecSnap){ dx = flexVecSnap(dx, VEC_GRID_STEP); dy = flexVecSnap(dy, VEC_GRID_STEP); }
      vecMarkDirtyElem(vecGestElem);
      {
        // El motor guarda la geometria en el espacio LOCAL del objeto:
        // el punto del dedo hay que llevarlo alli con la inversa de su
        // matriz, o mover un nodo de un objeto girado lo mandaria lejos.
        float inv[6];
        if(flexVecMatInvert(vecDoc.elems[vecGestElem].m, inv)){
          float lx, ly; flexVecMatApply(inv, dx, dy, &lx, &ly);
          flexVecNodeMove(&vecDoc, vecGestElem, vecGestNode, lx, ly);
        }
      }
      vecMarkDirtyElem(vecGestElem);
      vecInvalidate();
      break;
    case VG_HANDLEBAR:
      vecMarkDirtyElem(vecGestElem);
      {
        float inv[6];
        if(flexVecMatInvert(vecDoc.elems[vecGestElem].m, inv)){
          float lx, ly; flexVecMatApply(inv, dx, dy, &lx, &ly);
          flexVecHandleMove(&vecDoc, vecGestElem, vecGestNode, vecGestPart == 2 ? 1 : 0, lx, ly);
        }
      }
      vecMarkDirtyElem(vecGestElem);
      vecInvalidate();
      break;
    case VG_PEN:
      // Tap-arrastre = ancla SUAVE: el recorrido del dedo es el tirador.
      if(vecPenElem >= 0 && vecElemOk(vecPenElem)){
        float d2 = (dx - vecPenAX) * (dx - vecPenAX) + (dy - vecPenAY) * (dy - vecPenAY);
        if(d2 > (vecTol() * 0.6f) * (vecTol() * 0.6f)){
          int idx = (int)vecDoc.elems[vecPenElem].count - 1;
          if(idx >= 0){
            FlexVecNode* nd = vecDoc.arena.nodes + vecDoc.elems[vecPenElem].first;
            nd[idx].flags |= FLEXVEC_N_SMOOTH;
            flexVecHandleMove(&vecDoc, vecPenElem, idx, 1, dx, dy);
            vecPenDrag = true;
            vecInvalidate(); vecMarkDirtyAll();
          }
        }
      }
      break;
    case VG_MARQUEE:
      vecMarqX1 = px; vecMarqY1 = py;
      vecMarkDirtyAll();
      break;
    default: break;
  }
}

static void vecShapeCreate(){
  float x0 = vecDocX(vecMarqX0), y0 = vecDocY(vecMarqY0);
  float x1 = vecDocX(vecMarqX1), y1 = vecDocY(vecMarqY1);
  if(vecSnap){
    x0 = flexVecSnap(x0, VEC_GRID_STEP); y0 = flexVecSnap(y0, VEC_GRID_STEP);
    x1 = flexVecSnap(x1, VEC_GRID_STEP); y1 = flexVecSnap(y1, VEC_GRID_STEP);
  }
  int e = -1;
  switch(vecTool){
    case VT_RECT:    e = flexVecAddRect(&vecDoc, x0, y0, x1 - x0, y1 - y0, 0); break;
    case VT_ELLIPSE: e = flexVecAddEllipse(&vecDoc, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f,
                                           fabsf(x1 - x0) * 0.5f, fabsf(y1 - y0) * 0.5f); break;
    case VT_LINE:    e = flexVecAddLine(&vecDoc, x0, y0, x1, y1); break;
    case VT_POLY: {
      float r = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
      if(vecPolyStar) e = flexVecAddStar(&vecDoc, x0, y0, r, r * 0.5f, vecPolySides);
      else            e = flexVecAddPolygon(&vecDoc, x0, y0, r, vecPolySides);
      break; }
    default: return;
  }
  if(e < 0){ vecToastErr(e); return; }
  vecStyleNew(e);
  if(vecTool == VT_LINE) flexVecSetNoFill(&vecDoc, e);
  flexVecSelect(&vecDoc, e, 0);
  vecInvalidate();
  sessMarkDirty(IC_VECTOR);
}

static void vecCanvasRelease(int px, int py){
  switch(vecGest){
    case VG_MOVE:
    case VG_HANDLE:
    case VG_NODE:
    case VG_HANDLEBAR:
      vecGestEnd();
      vecInvalidate();
      vecMarkDirtyAll();
      break;
    case VG_PEN:
      vecPenDrag = false;
      break;
    case VG_MARQUEE: {
      int dx = px - vecMarqX0, dy = py - vecMarqY0;
      bool tiny = (dx * dx + dy * dy) < 100;
      if(vecTool == VT_SELECT){
        if(!tiny)
          flexVecSelectRect(&vecDoc, vecDocX(vecMarqX0), vecDocY(vecMarqY0),
                            vecDocX(px), vecDocY(py), 0);
      } else if(!tiny){
        vecShapeCreate();
      }
      vecMarkDirtyAll();
      break; }
    default: break;
  }
  vecGest = VG_NONE;
  vecGestElem = -1; vecGestNode = -1; vecGestHandle = -1;
}

// Pulsacion larga. Con la herramienta de nodos alterna vertice/suave,
// que es la operacion que mas se repite al ajustar una curva y no
// merece un viaje a un panel.
static void vecCanvasLong(int px, int py){
  if(vecTool != VT_NODE) return;
  int e = flexVecSelFirst(&vecDoc);
  if(e < 0) return;
  float dx = vecDocX(px), dy = vecDocY(py);
  int part = 0;
  int nd = flexVecHitNode(&vecDoc, e, dx, dy, vecTol() * 1.4f, &part);
  if(nd < 0 || part != 0) return;
  bool wasSmooth = (vecDoc.arena.nodes[vecDoc.elems[e].first + nd].flags & FLEXVEC_N_SMOOTH) != 0;
  flexVecNodeSetSmooth(&vecDoc, e, nd, wasSmooth ? 0 : 1);
  vecToast(wasSmooth ? "Vertice" : "Suavizado");
  vecGest = VG_NONE;
  vecInvalidate(); vecMarkDirtyAll();
  sessMarkDirty(IC_VECTOR);
}

// #############################################################
// ##  TOQUES DE LA INTERFAZ
// #############################################################
static void vecApplyPattern(int kind){
  if(vecPatSel < 0 || !vecDoc.pats[vecPatSel].used){
    int p = flexVecPatternAdd(&vecDoc, kind);
    if(p < 0){ vecToastErr(p); return; }
    vecPatSel = p;
  }
  flexVecPatternSet(&vecDoc, vecPatSel, kind, vecDoc.pats[vecPatSel].scale, 0, 0, 0);
  flexVecPatternColors(&vecDoc, vecPatSel, vecStrokeRGB, 255, vecFillRGB, 255);
  int n = 0;
  for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++){
    if(!vecElemOk(e) || !(vecDoc.elems[e].flags & FLEXVEC_EF_SEL)) continue;
    if(n == 0) flexVecUndoBegin(&vecDoc, "Motivo");
    flexVecSetFillPattern(&vecDoc, e, vecPatSel);
    n++;
  }
  if(n){ flexVecUndoCommit(&vecDoc); vecInvalidate(); sessMarkDirty(IC_VECTOR); }
  else vecToast("Selecciona un objeto");
}

static void vecApplyGradient(){
  int sel = flexVecSelFirst(&vecDoc);
  if(sel < 0){ vecToast("Selecciona un objeto"); return; }
  int g = flexVecGradAdd(&vecDoc, FLEXVEC_G_LINEAR);
  if(g < 0){ vecToastErr(g); return; }
  float x0, y0, x1, y1;
  flexVecBBox(&vecDoc, sel, &x0, &y0, &x1, &y1);
  flexVecGradSetAxis(&vecDoc, g, x0, y0, x1, y1);
  flexVecGradSetStop(&vecDoc, g, 0, 0.0f, vecFillRGB, 255);
  flexVecGradSetStop(&vecDoc, g, 1, 1.0f, vecStrokeRGB, 255);
  flexVecUndoBegin(&vecDoc, "Gradiente");
  for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++)
    if(vecElemOk(e) && (vecDoc.elems[e].flags & FLEXVEC_EF_SEL))
      flexVecSetFillGrad(&vecDoc, e, g);
  flexVecUndoCommit(&vecDoc);
  vecInvalidate(); sessMarkDirty(IC_VECTOR);
  vecToast("Gradiente aplicado");
}

static bool vecStylePanelTouch(int px, int py, int x, int y, int w, int h){
  (void)h;
  for(int i = 0; i < 3; i++){
    int bx, by, bw, bh;
    vecStyleTabRect(i, x, y, w, &bx, &by, &bw, &bh);
    if(px >= bx && px <= bx + bw && py >= by && py <= by + bh){ vecStyleTab = i; return true; }
  }
  int ty = y + 92;
  if(vecStyleTab == 0){
    for(int i = 0; i < 12; i++){
      int cx = x + 24 + (i % 6) * 40 + 14, cy = ty + 22 + (i / 6) * 40 + 14;
      if((px - cx) * (px - cx) + (py - cy) * (py - cy) <= 20 * 20){
        vecFillRGB = VEC_PAL[i]; vecApplyStyle(0); return true;
      }
      cx = x + 244 + (i % 6) * 34 + 12; cy = ty + 22 + (i / 6) * 40 + 14;
      if((px - cx) * (px - cx) + (py - cy) * (py - cy) <= 18 * 18){
        vecStrokeRGB = VEC_PAL[i]; vecApplyStyle(1); return true;
      }
    }
    if(py >= ty + 126 && py <= ty + 156){
      float fr = (float)(px - (x + 20)) / (float)(w - 40);
      vecStrokeW = fr * 24.0f;
      if(vecStrokeW < 0) vecStrokeW = 0;
      if(vecStrokeW > 24) vecStrokeW = 24;
      vecApplyStyle(1);
      return true;
    }
    if(py >= ty + 180 && py <= ty + 210){
      float fr = (float)(px - (x + 20)) / (float)(w - 40);
      int a = (int)(fr * 255.0f);
      vecObjAlpha = (uint8_t)(a < 12 ? 12 : (a > 255 ? 255 : a));
      vecApplyStyle(2);
      return true;
    }
    if(py >= ty + 214 && py <= ty + 248){
      if(px >= x + 20 && px <= x + 150){ vecApplyStyle(3); return true; }
      if(px >= x + 162 && px <= x + 292){ vecApplyStyle(4); return true; }
      if(px >= x + 304 && px <= x + 434){ vecApplyGradient(); return true; }
    }
    return false;
  }
  if(vecStyleTab == 1){
    for(int i = 0; i < FLEXVEC_PAT_N; i++){
      int bx = x + 20 + (i % 3) * ((w - 40) / 3);
      int by = ty + 22 + (i / 3) * 76;
      int bw = (w - 40) / 3 - 8;
      if(px >= bx && px <= bx + bw && py >= by && py <= by + 68){
        vecApplyPattern(i);
        return true;
      }
    }
    if(py >= ty + 194 && py <= ty + 222 && vecPatSel >= 0){
      float fr = (float)(px - (x + 20)) / (float)(w - 40);
      float sc = 2.0f + fr * 46.0f;
      flexVecPatternSet(&vecDoc, vecPatSel, vecDoc.pats[vecPatSel].kind, sc, 0, 0, 0);
      vecInvalidate(); sessMarkDirty(IC_VECTOR);
      return true;
    }
    if(py >= ty + 226 && py <= ty + 260){
      vecApplyStyle(0);                        // vuelve a relleno solido
      vecToast("Motivo quitado");
      return true;
    }
    return false;
  }
  // Pestana "Extra": segundo relleno y segundo trazo.
  for(int i = 0; i < 6; i++){
    int cx = x + 24 + i * 44 + 16;
    if(px >= cx - 18 && px <= cx + 18){
      if(py >= ty + 22 && py <= ty + 54){
        int n = 0;
        for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++){
          if(!vecElemOk(e) || !(vecDoc.elems[e].flags & FLEXVEC_EF_SEL)) continue;
          if(n == 0) flexVecUndoBegin(&vecDoc, "Relleno 2");
          flexVecSetFill2(&vecDoc, e, VEC_PAL[i * 2], 255);
          n++;
        }
        if(n){ flexVecUndoCommit(&vecDoc); vecInvalidate(); sessMarkDirty(IC_VECTOR); }
        else vecToast("Selecciona un objeto");
        return true;
      }
      if(py >= ty + 92 && py <= ty + 124){
        int n = 0;
        for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++){
          if(!vecElemOk(e) || !(vecDoc.elems[e].flags & FLEXVEC_EF_SEL)) continue;
          if(n == 0) flexVecUndoBegin(&vecDoc, "Trazo 2");
          // El segundo trazo nace mas GRUESO que el principal: es lo que
          // hace que se vea como un contorno por fuera y no tapado.
          flexVecSetStroke2(&vecDoc, e, VEC_PAL[i * 2 + 1], 255, vecStrokeW + 6.0f);
          n++;
        }
        if(n){ flexVecUndoCommit(&vecDoc); vecInvalidate(); sessMarkDirty(IC_VECTOR); }
        else vecToast("Selecciona un objeto");
        return true;
      }
    }
  }
  if(py >= ty + 146 && py <= ty + 182){
    int n = 0;
    for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++){
      if(!vecElemOk(e) || !(vecDoc.elems[e].flags & FLEXVEC_EF_SEL)) continue;
      if(n == 0) flexVecUndoBegin(&vecDoc, "Quitar extra");
      flexVecClearExtra(&vecDoc, e);
      n++;
    }
    if(n){ flexVecUndoCommit(&vecDoc); vecInvalidate(); sessMarkDirty(IC_VECTOR); }
    return true;
  }
  return false;
}

static bool vecLayersPanelTouch(int px, int py, int x, int y, int w, int h){
  if(px >= x + w - 116 && px <= x + w - 20 && py >= y + 14 && py <= y + 48){
    int l = flexVecLayerAdd(&vecDoc, NULL);
    if(l < 0) vecToastErr(l); else vecToast("Capa nueva");
    sessMarkDirty(IC_VECTOR);
    return true;
  }
  int ry = y + 62;
  for(int l = 0; l < FLEXVEC_MAX_LAYERS; l++){
    if(!vecDoc.layers[l].used) continue;
    if(ry + 44 > y + h - 8) break;
    if(py >= ry && py <= ry + 40){
      if(px <= x + 60){
        flexVecLayerSetVisible(&vecDoc, l, !vecDoc.layers[l].visible);
        vecInvalidate();
      } else if(px >= x + w - 66){
        flexVecLayerSetLocked(&vecDoc, l, !vecDoc.layers[l].locked);
        vecInvalidate();
      } else {
        flexVecLayerSetActive(&vecDoc, l);
      }
      sessMarkDirty(IC_VECTOR);
      return true;
    }
    ry += 46;
  }
  return false;
}

// Reserva y suelta los 128 KB de la booleana SOLO mientras dura la
// operacion. Ver la nota de FlexVecBoolWork: tenerlos tomados toda la
// sesion seria pagar el peor caso todo el rato.
static void vecPathfinder(int op){
  if(flexVecSelCount(&vecDoc) < 2){ vecToast("Selecciona dos objetos"); return; }
  size_t need = flexVecBoolBytes();
  void* blk = heap_caps_malloc(need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!blk) blk = malloc(need);
  if(!blk){ vecToast("Memoria insuficiente"); return; }
  FlexVecBoolWork w;
  int rc = flexVecBoolInit(&w, blk, need);
  if(rc == FLEXVEC_OK) rc = flexVecPathfinder(&vecDoc, op, &vecRas, &w);
  free(blk);
  if(rc != FLEXVEC_OK) vecToastErr(rc);
  else { vecToast("Hecho"); vecInvalidate(); sessMarkDirty(IC_VECTOR); }
}

// Recortar con la forma de arriba: mismos buffers y misma politica de
// "pedir, usar y soltar" que el Pathfinder.
static void vecClipMask(){
  if(flexVecSelCount(&vecDoc) < 2){ vecToast("Selecciona el objeto y la mascara"); return; }
  size_t need = flexVecBoolBytes();
  void* blk = heap_caps_malloc(need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!blk) blk = malloc(need);
  if(!blk){ vecToast("Memoria insuficiente"); return; }
  FlexVecBoolWork w;
  int rc = flexVecBoolInit(&w, blk, need);
  if(rc == FLEXVEC_OK) rc = flexVecClipWithTop(&vecDoc, &vecRas, &w);
  free(blk);
  if(rc != FLEXVEC_OK) vecToastErr(rc);
  else { vecToast("Recortado"); vecInvalidate(); sessMarkDirty(IC_VECTOR); }
}

// Los dos primeros seleccionados, en orden de pintado. Fusion y pincel
// necesitan saber CUAL es cual, no solo cuantos hay.
static int vecSelPair(int* a, int* b){
  int16_t order[FLEXVEC_MAX_ELEMS];
  int n = flexVecPaintOrder(&vecDoc, order, FLEXVEC_MAX_ELEMS);
  int got = 0;
  for(int i = 0; i < n && got < 2; i++)
    if(vecDoc.elems[order[i]].flags & FLEXVEC_EF_SEL){
      if(got == 0) *a = order[i]; else *b = order[i];
      got++;
    }
  return got;
}

static void vecArrangeFase2(int i){
  int nsel = flexVecSelCount(&vecDoc);
  int sel = flexVecSelFirst(&vecDoc);
  int rc = FLEXVEC_OK;
  switch(i){
    case 0: vecPathfinder(FLEXVEC_B_DIVIDE); return;
    case 1: vecPathfinder(FLEXVEC_B_TRIM); return;
    case 2: vecClipMask(); return;
    case 3: {                                  // Fusion
      int a = -1, b = -1;
      if(vecSelPair(&a, &b) < 2){ vecToast("Selecciona dos objetos"); return; }
      rc = flexVecBlend(&vecDoc, a, b, 4, &vecRas);
      break; }
    case 4: {                                  // Repetir, ciclando la forma
      if(sel < 0){ vecToast("Selecciona un objeto"); return; }
      float x0, y0, x1, y1;
      flexVecBBox(&vecDoc, sel, &x0, &y0, &x1, &y1);
      float dx = (x1 - x0) + 12.0f, dy = (y1 - y0) + 12.0f;
      static const int REP[4][4] = { {0,3,1,0}, {0,1,3,1}, {0,2,2,2}, {1,2,1,0} };
      const int* rp = REP[vecRepMode & 3];
      rc = flexVecRepeat(&vecDoc, sel, rp[0], rp[1], rp[2],
                         rp[1] > 1 ? dx : 0.0f, rp[2] > 1 ? dy : 0.0f);
      if(rc == FLEXVEC_OK){
        static const char* NAME[4] = { "Fila de 3", "Columna de 3", "Rejilla 2x2", "Espejo" };
        vecToast(NAME[vecRepMode & 3]);
        vecRepMode++;
      }
      break; }
    case 5: {                                  // Pincel: trazado + figura
      int a = -1, b = -1;
      if(vecSelPair(&a, &b) < 2){ vecToast("Selecciona el trazado y la figura"); return; }
      rc = flexVecBrush(&vecDoc, a, b, 18.0f, 1.0f, 1, &vecRas);
      break; }
    case 6: {                                  // Instancia de simbolo
      if(sel < 0){ vecToast("Selecciona un objeto"); return; }
      float x0, y0, x1, y1;
      flexVecBBox(&vecDoc, sel, &x0, &y0, &x1, &y1);
      int e = flexVecSymbolInstance(&vecDoc, sel, (x1 - x0) + 16.0f, 0);
      if(e < 0) rc = -e;
      else { flexVecSelect(&vecDoc, e, 0); vecToast("Instancia vinculada"); }
      break; }
    case 7: {                                  // Texto de area
      if(sel < 0 || vecDoc.elems[sel].kind != FLEXVEC_K_TEXT){
        vecToast("Selecciona un texto");
        return;
      }
      float x0, y0, x1, y1;
      flexVecBBox(&vecDoc, sel, &x0, &y0, &x1, &y1);
      float bw = (x1 - x0) > 40 ? (x1 - x0) : 160.0f;
      rc = flexVecTextSetArea(&vecDoc, sel, bw, vecDoc.elems[sel].p0 * 5.0f, FLEXVEC_TA_LEFT);
      if(rc == FLEXVEC_OK) vecToast("Texto de area");
      break; }
    default: {                                 // Inclinar
      if(nsel < 1){ vecToast("Selecciona un objeto"); return; }
      float bx0, by0, bx1, by1;
      if(flexVecSelBBox(&vecDoc, &bx0, &by0, &bx1, &by1) != FLEXVEC_OK) return;
      float cx = (bx0 + bx1) * 0.5f, cy = (by0 + by1) * 0.5f;
      float m[6];
      float kx = 0.25f;
      m[0] = 1; m[1] = 0; m[2] = kx; m[3] = 1;
      m[4] = cx - (cx + kx * cy); m[5] = 0;
      rc = flexVecTransformSel(&vecDoc, m, 0);
      break; }
  }
  if(rc != FLEXVEC_OK) vecToastErr(rc);
  else { vecInvalidate(); sessMarkDirty(IC_VECTOR); }
}

static bool vecArrangePanelTouch(int px, int py, int x, int y, int w, int h){
  // Cambio de pagina.
  if(px >= x + w - 104 && px <= x + w - 20 && py >= y + 12 && py <= y + 44){
    vecArrPage = vecArrPage ? 0 : 1;
    return true;
  }
  if(vecArrPage == 1){
    for(int i = 0; i < VEC_ARR2_N; i++){
      int bx, by, bw, bh;
      vecArrBtnRect(i, x, y, w, &bx, &by, &bw, &bh);
      if(by + bh > y + h - 6) break;
      if(px < bx || px > bx + bw || py < by || py > by + bh) continue;
      vecArrangeFase2(i);
      return true;
    }
    return false;
  }
  for(int i = 0; i < VEC_ARR_N; i++){
    int bx, by, bw, bh;
    vecArrBtnRect(i, x, y, w, &bx, &by, &bw, &bh);
    if(by + bh > y + h - 6) break;
    if(px < bx || px > bx + bw || py < by || py > by + bh) continue;
    int sel = flexVecSelFirst(&vecDoc);
    int rc = FLEXVEC_OK;
    switch(i){
      case 0: rc = flexVecAlign(&vecDoc, FLEXVEC_AL_LEFT); break;
      case 1: rc = flexVecAlign(&vecDoc, FLEXVEC_AL_HCENTER); break;
      case 2: rc = flexVecAlign(&vecDoc, FLEXVEC_AL_RIGHT); break;
      case 3: rc = flexVecAlign(&vecDoc, FLEXVEC_AL_TOP); break;
      case 4: rc = flexVecAlign(&vecDoc, FLEXVEC_AL_VCENTER); break;
      case 5: rc = flexVecAlign(&vecDoc, FLEXVEC_AL_BOTTOM); break;
      case 6: rc = (sel >= 0) ? flexVecToFront(&vecDoc, sel) : FLEXVEC_E_EMPTY; break;
      case 7: rc = (sel >= 0) ? flexVecRaise(&vecDoc, sel) : FLEXVEC_E_EMPTY; break;
      case 8: rc = (sel >= 0) ? flexVecToBack(&vecDoc, sel) : FLEXVEC_E_EMPTY; break;
      case 9:  vecPathfinder(FLEXVEC_B_UNITE); return true;
      case 10: vecPathfinder(FLEXVEC_B_MINUS_FRONT); return true;
      case 11: vecPathfinder(FLEXVEC_B_INTERSECT); return true;
      case 12: vecPathfinder(FLEXVEC_B_EXCLUDE); return true;
      case 13: {
        int n = 0;
        flexVecUndoBegin(&vecDoc, "Duplicar");
        for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++){
          if(!vecElemOk(e) || !(vecDoc.elems[e].flags & FLEXVEC_EF_SEL)) continue;
          int c = flexVecDuplicate(&vecDoc, e);
          if(c < 0){ rc = -c; break; }
          flexVecTranslate(&vecDoc, c, 12, 12);
          n++;
        }
        flexVecUndoCommit(&vecDoc);
        if(!n && rc == FLEXVEC_OK) rc = FLEXVEC_E_EMPTY;
        break; }
      case 14: {
        int n = 0;
        flexVecUndoBegin(&vecDoc, "Borrar");
        for(int e = FLEXVEC_MAX_ELEMS - 1; e >= 0; e--){
          if(!vecElemOk(e) || !(vecDoc.elems[e].flags & FLEXVEC_EF_SEL)) continue;
          if(flexVecDelete(&vecDoc, e) == FLEXVEC_OK) n++;
        }
        flexVecUndoCommit(&vecDoc);
        if(!n) rc = FLEXVEC_E_EMPTY;
        break; }
      case 15:
      case 16: {
        float bx0, by0, bx1, by1;
        if(flexVecSelBBox(&vecDoc, &bx0, &by0, &bx1, &by1) != FLEXVEC_OK){ rc = FLEXVEC_E_EMPTY; break; }
        float cx = (bx0 + bx1) * 0.5f, cy = (by0 + by1) * 0.5f;
        float m[6];
        if(i == 15){ m[0] = -1; m[1] = 0; m[2] = 0; m[3] = 1; m[4] = 2 * cx; m[5] = 0; }
        else       { m[0] = 1; m[1] = 0; m[2] = 0; m[3] = -1; m[4] = 0; m[5] = 2 * cy; }
        rc = flexVecTransformSel(&vecDoc, m, 0);
        break; }
      case 17: rc = flexVecDistribute(&vecDoc, 1); break;
      default: break;
    }
    if(rc != FLEXVEC_OK) vecToastErr(rc);
    else { vecInvalidate(); sessMarkDirty(IC_VECTOR); }
    return true;
  }
  return false;
}

static bool vecSharePanelTouch(int px, int py, int x, int y, int w, int h){
  if(!vecShareOn){
    if(px >= x + 20 && px <= x + w - 20){
      if(py >= y + 104 && py <= y + 150){ vecShareStart(); return true; }
      if(py >= y + 162 && py <= y + 204){ vecSaveSvgToDisk(); return true; }
      if(py >= y + 212 && py <= y + 254){ vecExportPng(); return true; }
    }
    return false;
  }
  if(py >= y + h - 56 && py <= y + h - 14 && px >= x + 20 && px <= x + w - 20){
    vecShareStop();
    vecToast("Se dejo de compartir");
    return true;
  }
  return false;
}

static bool vecMenuTouch(int px, int py){
  int x, y, w, h; vecMenuGeom(&x, &y, &w, &h);
  if(px < x || px > x + w || py < y || py > y + h){ vecMenuOn = false; return true; }
  int i = (py - y - 8) / 42;
  if(i < 0 || i >= VEC_MENU_N) return true;
  vecMenuOn = false;
  switch(i){
    case 0: vecPanel = VP_STYLE;   break;
    case 1: vecPanel = VP_LAYERS;  break;
    case 2: vecPanel = VP_ARRANGE; break;
    case 3: vecPanel = VP_SHARE;   break;
    case 4: vecGrid = !vecGrid; vecInvalidate(); break;
    case 5: vecSnap = !vecSnap; vecToast(vecSnap ? "Ajuste activado" : "Ajuste desactivado"); break;
    case 6: vecFitView(); break;
    case 7: if(vecSaveDoc()) vecToast("Guardado"); break;
    case 8:
      vecFinishPen();
      vecSaveDoc();
      vecShareStop();
      vecView = 0; vecPanel = VP_NONE;
      vecReload();
      break;
    default: break;
  }
  return true;
}

static bool vecHeaderTouch(int px, int py){
  if(py >= VEC_HEAD_H) return false;
  if(px < 60){                     // chevron: volver a la lista de documentos
    vecFinishPen();
    vecSaveDoc();
    vecShareStop();
    vecView = 0; vecPanel = VP_NONE; vecMenuOn = false;
    vecReload();
    return true;
  }
  int bx = SCR_W - 158;
  for(int i = 0; i < 3; i++){
    int cx = bx + i * 50 + 20;
    if(px < cx - 22 || px > cx + 22) continue;
    if(i == 0){
      vecFinishPen();
      if(flexVecUndo(&vecDoc) == FLEXVEC_OK){ vecInvalidate(); sessMarkDirty(IC_VECTOR); }
      else vecToast("Nada que deshacer");
    } else if(i == 1){
      if(flexVecRedo(&vecDoc) == FLEXVEC_OK){ vecInvalidate(); sessMarkDirty(IC_VECTOR); }
      else vecToast("Nada que rehacer");
    } else {
      vecMenuOn = !vecMenuOn;
    }
    return true;
  }
  return true;                     // el resto de la cabecera se traga el toque
}

static bool vecToolsTouch(int px, int py){
  if(py < VEC_BOT || py >= VEC_BOT + VEC_TOOL_H) return false;
  int w = SCR_W / VT_N;
  int t = px / w;
  if(t < 0 || t >= VT_N) return true;
  if(t == VT_POLY && vecTool == VT_POLY){
    // Tocar de nuevo el poligono alterna poligono/estrella y sube los
    // lados. Es un boton con tres funciones porque no hay sitio para
    // tres botones, y el aviso dice en cual esta.
    if(!vecPolyStar) vecPolyStar = true;
    else { vecPolyStar = false; vecPolySides++; if(vecPolySides > 12) vecPolySides = 3; }
    char m[40];
    snprintf(m, sizeof(m), vecPolyStar ? "Estrella de %d puntas" : "Poligono de %d lados", vecPolySides);
    vecToast(m);
    return true;
  }
  if(t != vecTool){
    vecFinishPen();
    vecTool = t;
    if(t == VT_NODE || t == VT_PEN) vecPanel = VP_NONE;
    vecMarkDirtyAll();
  }
  return true;
}

// #############################################################
// ##  TOQUES DE LA GALERIA
// #############################################################
static void vecGalleryTick(){
  if(fkTrashOn){ if(fkTrashTick()) vecReload(); vecRenderGallery(); return; }
  if(fkAskOn){
    int r = fkAskTick();
    if(r != 0){
      if(r == 1 && vecSelIdx >= 0){
        char p[FLEXFS_PATH_MAX]; vecPathOf(vecSelIdx, p, sizeof(p));
        if(p[0]) flexFsDelete(p);
        vecSelIdx = -1; vecReload();
      }
      vecRenderGallery();
    }
    return;
  }
  if(fkNameOn){
    int r = fkNameTick();
    if(r == 1 && vecAsk == VEC_ASK_RENAME && vecSelIdx >= 0){
      char p[FLEXFS_PATH_MAX]; vecPathOf(vecSelIdx, p, sizeof(p));
      char nn[FLEXFS_NAME_MAX];
      snprintf(nn, sizeof(nn), "%s%s", fkNameBuf, FLEXFS_EXT_VECTOR);
      if(p[0] && !flexFsRename(p, nn)) vecToast("No se pudo renombrar");
      vecReload();
    }
    if(r != 0){ vecAsk = VEC_ASK_NONE; vecRenderGallery(); }
    return;
  }
  if(fkMenuOn){
    if(T.tap){
      int a = fkMenuHit(T.x, T.y);
      fkMenuOn = false;
      if(a == FK_ACT_REN && vecSelIdx >= 0){
        char stem[FLEXFS_NAME_MAX]; flexFsStem(vecList[vecSelIdx].name, stem, sizeof(stem));
        vecAsk = VEC_ASK_RENAME;
        fkNameOpen("Renombrar documento", stem);
        return;
      }
      if(a == FK_ACT_DEL && vecSelIdx >= 0){
        char stem[FLEXFS_NAME_MAX]; flexFsStem(vecList[vecSelIdx].name, stem, sizeof(stem));
        fkAskOpen("\xC2\xBF" "Borrar definitivamente?", stem);
        return;
      }
      if(a == FK_ACT_TRASH){
        if(vecSelIdx >= 0){
          char p[FLEXFS_PATH_MAX]; vecPathOf(vecSelIdx, p, sizeof(p));
          if(p[0]) flexFsTrash(p);
          vecSelIdx = -1; vecReload();
        } else { fkTrashOpen(); return; }
      }
      vecRenderGallery();
    }
    return;
  }
  if(T.tap && T.x < 60 && T.y < 60){ appClose(); return; }
  // Desplazamiento vertical de la lista.
  if(T.pressed){ vecDragY0 = T.y; vecDragS0 = vecScroll; vecDragging = false; vecPressMs = millis(); vecLongFired = false; }
  if(T.down && !vecLongFired && millis() - vecPressMs > 620 && abs(T.y - vecDragY0) < 14){
    vecLongFired = true;
    int i = -1;
    for(int k = 0; k < vecListN; k++){
      int y = vecCardY(k + 1);
      if(T.y >= y && T.y < y + VEC_CARD_H){ i = k; break; }
    }
    vecSelIdx = i;
    fkMenuOpen(T.x, T.y);
    return;
  }
  if(T.down && abs(T.y - vecDragY0) > 10){
    vecDragging = true;
    vecScroll = vecDragS0 - (T.y - vecDragY0);
    if(vecScroll < 0) vecScroll = 0;
    int mx = vecMaxScroll();
    if(vecScroll > mx) vecScroll = mx;
    vecRenderGallery();
  }
  if(T.released && !vecDragging && !vecLongFired){
    int y0 = vecCardY(0);
    if(T.y >= y0 && T.y < y0 + VEC_CARD_H){ vecNewDoc(); return; }
    for(int k = 0; k < vecListN; k++){
      int y = vecCardY(k + 1);
      if(T.y >= y && T.y < y + VEC_CARD_H){ vecOpen(k); return; }
    }
  }
  if(T.released) vecDragging = false;
}

// #############################################################
// ##  TICK DE LA APP
// #############################################################
static void vecCanvasTick(){
  // 1) PINZA. Va lo primero: si hay dos dedos, ninguna otra capa puede
  //    ver ese contacto o el documento se movera y ademas se creara una
  //    figura sin querer.
  if(vecPinchUpdate()){ vecFlushDirty(); return; }
  if(millis() - vecPinchMs < 260 && !T.pressed) { vecFlushDirty(); return; }

  if(fkNameOn){
    int r = fkNameTick();
    if(r != 0){
      if(r == 1 && vecAsk == VEC_ASK_TEXT && fkNameBuf[0]){
        int e = flexVecAddText(&vecDoc, vecAskX, vecAskY, fkNameBuf, 24.0f);
        if(e < 0) vecToastErr(e);
        else {
          // El motor no sabe cuanto MIDE el texto: la fuente es del
          // sistema. Se mide aqui con textW() y se corrige la caja, que
          // es lo que usan la seleccion y el SVG.
          float w = (float)textW(fkNameBuf, 2) * (24.0f / 24.0f);
          flexVecTextSetBox(&vecDoc, e, w > 4 ? w : 4, 24.0f);
          flexVecSelect(&vecDoc, e, 0);
          vecTool = VT_SELECT;
          sessMarkDirty(IC_VECTOR);
        }
      }
      vecAsk = VEC_ASK_NONE;
      vecInvalidate();
      vecRenderAll();
    }
    return;
  }

  // 2) Menu y panel: mientras uno esta abierto, es el dueno del toque.
  if(vecMenuOn){
    if(T.tap){ vecMenuTouch(T.x, T.y); vecRenderAll(); }
    return;
  }
  if(vecPanel != VP_NONE){
    if(T.tap){
      int x, y, w, h; vecPanelRect(&x, &y, &w, &h);
      if(T.x < x || T.x > x + w || T.y < y || T.y > y + h){
        // Fuera del panel: se cierra. Salvo en la tira de herramientas,
        // que sigue viva para poder cambiar de herramienta sin cerrar.
        if(!vecToolsTouch(T.x, T.y)) vecPanel = VP_NONE;
        vecRenderAll();
        return;
      }
      bool used = false;
      switch(vecPanel){
        case VP_STYLE:   used = vecStylePanelTouch(T.x, T.y, x, y, w, h); break;
        case VP_LAYERS:  used = vecLayersPanelTouch(T.x, T.y, x, y, w, h); break;
        case VP_ARRANGE: used = vecArrangePanelTouch(T.x, T.y, x, y, w, h); break;
        case VP_SHARE:   used = vecSharePanelTouch(T.x, T.y, x, y, w, h); break;
        default: break;
      }
      (void)used;
      vecRenderAll();
    }
    return;
  }

  // 3) Cabecera y herramientas.
  if(T.tap && T.y < VEC_HEAD_H){ vecHeaderTouch(T.x, T.y); vecRenderAll(); return; }
  if(T.tap && T.y >= VEC_BOT){ vecToolsTouch(T.x, T.y); vecRenderAll(); return; }
  if(T.pressed && (T.y < VEC_HEAD_H || T.y >= VEC_BOT)) return;

  // 4) El lienzo.
  if(T.pressed && T.y >= VEC_TOP && T.y < VEC_BOT) vecCanvasPress(T.x, T.y);
  else if(T.down && vecGest != VG_NONE) vecCanvasMove(T.x, T.y);
  else if(T.down && !vecLongFired && vecGest == VG_NONE && millis() - vecPressMs > 620)
    { vecLongFired = true; vecCanvasLong(T.x, T.y); }
  if(T.released) vecCanvasRelease(T.x, T.y);
  vecFlushDirty();
  // El aviso se apaga solo: hay que repintar su banda cuando caduca, o
  // se queda pegado en pantalla hasta el siguiente toque.
  if(vecMsg[0] && millis() - vecMsgMs > 2600){ vecMsg[0] = 0; vecPaintBand(VEC_BOT - 80, VEC_BOT - 1); }
}

static void vecTick(){
  if(!flexFsReady()){ if(T.tap && T.x < 60 && T.y < 60) appClose(); return; }
  if(vecMemErr){ if(T.tap && T.x < 60 && T.y < 60) appClose(); return; }
  if(vecView == 0) vecGalleryTick();
  else             vecCanvasTick();
}

static void vecEnter(){
  gAppW = SCR_W; gAppH = SCR_H;
  if(!vecArenaInit()){
    setBuf(fb);
    fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
    strokeSegAA(30, 30, 18, 22, 2.4f, TH_NAV);
    strokeSegAA(18, 22, 30, 14, 2.4f, TH_NAV);
    drawTextC(SCR_W / 2, SCR_H / 2 - 40, "Flex Vector Pro", 3, TH_TXT);
    drawTextC(SCR_W / 2, SCR_H / 2, "No hay memoria suficiente", 2, TH_TXT2);
    drawTextC(SCR_W / 2, SCR_H / 2 + 28, "Cierra alguna app y vuelve a intentarlo", 1, TH_MUTE);
    flxFlushAll();
    return;
  }
  if(!gRelayout){
    vecReload();
    if(vecView == 1 && vecPath[0] && flexFsExists(vecPath)){
      if(!vecLoadDoc(vecPath)){ vecView = 0; vecPath[0] = 0; }
    } else if(vecView == 1){
      vecView = 0; vecPath[0] = 0;
    }
    if(vecDoc.artW <= 0) flexVecNew(&vecDoc, 480, 800, 3);
    vecGest = VG_NONE; vecPenElem = -1; vecMenuOn = false; vecPanel = VP_NONE;
  }
  vecInvalidate();
  if(vecView == 0) vecRenderGallery();
  else             vecRenderAll();
}

// #############################################################
// ##  CICLO DE VIDA  ·  los ganchos del sistema
// ##  ------------------------------------------------------
// ##  Flex Vector Pro se comporta como cualquier otra app de Flex OS:
// ##  el framework la suspende, la reanuda, le pide memoria cuando el
// ##  sistema aprieta y le pregunta si tiene trabajo sin guardar. Aqui
// ##  no hay ni un caso especial.
// #############################################################
static bool vecBackLayer(){
  if(fkNameOn || fkAskOn || fkMenuOn || fkTrashOn) return false;   // los cierra el kit
  if(vecMenuOn){ vecMenuOn = false; vecRenderAll(); return true; }
  if(vecPanel != VP_NONE){ vecPanel = VP_NONE; vecRenderAll(); return true; }
  if(vecView == 1 && vecPenElem >= 0){ vecFinishPen(); vecInvalidate(); vecRenderAll(); return true; }
  return false;
}
static bool vecBackScreen(){
  if(vecView != 1) return false;                 // ya en la lista: que se cierre la app
  vecFinishPen();
  vecSaveDoc();
  vecShareStop();
  vecView = 0; vecPanel = VP_NONE; vecMenuOn = false;
  vecReload();
  vecRenderGallery();
  return true;
}

static void vecSuspend(){
  // Compartir se corta SIEMPRE al pasar a segundo plano: el servidor
  // apunta a un buffer que shed() puede soltar en cualquier momento, y
  // servir memoria liberada es la peor forma posible de fallar.
  vecShareStop();
  vecFinishPen();
  if(vecView == 1 && vecPath[0] && flexVecDirty(&vecDoc)) vecSaveDoc();
}
static void vecResume(){
  gAppW = SCR_W; gAppH = SCR_H;
  // La cache pudo soltarla shed(): se rehace desde el DOCUMENTO, que no
  // se toco. Reanudar repinta, no reinicia.
  if(!vecCache)
    vecCache = (uint16_t*)heap_caps_aligned_alloc(64, (size_t)SCR_W * SCR_H * 2,
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  vecInvalidate();
  if(vecView == 0){ vecReload(); vecRenderGallery(); }
  else vecRenderAll();
}
static void vecCloseApp(){
  vecShareStop();
  vecFinishPen();
  if(vecView == 1 && vecPath[0] && flexVecDirty(&vecDoc)) vecSaveDoc();
  vecArenaFree();
  vecView = 0; vecPanel = VP_NONE; vecMenuOn = false;
  vecGest = VG_NONE; vecPenElem = -1;
}

// SUELTA MEMORIA SIN PERDER NADA. Lo unico reconstruible que hay aqui es
// la cache de render: 768 KB que se rehacen solos en el siguiente
// repintado. El documento, el diario de deshacer y el pool de nodos NO
// se tocan -- son el trabajo del usuario, no una cache.
static size_t vecShed(){
  size_t freed = 0;
  if(vecCache){ free(vecCache); vecCache = NULL; freed += (size_t)SCR_W * SCR_H * 2; }
  if(vecQrMods && !vecShareOn){ free(vecQrMods); vecQrMods = NULL; vecQrSize = 0; freed += FLEXQR_BUF_BYTES; }
  freed += flexShareShed();
  vecCacheOk = false;
  return freed;
}
static bool vecDirtyHook(){ return vecReady && flexVecDirty(&vecDoc); }

static bool vecSaveSess(){
  if(!flexFsReady()) return true;
  if(vecReady && vecView == 1 && vecPath[0] && flexVecDirty(&vecDoc)) vecSaveDoc();
  VecSessV1 v; memset(&v, 0, sizeof(v));
  v.view = (uint8_t)((vecView == 1 && vecPath[0]) ? 1 : 0);
  v.tool = (uint8_t)vecTool; v.grid = vecGrid ? 1 : 0; v.snap = vecSnap ? 1 : 0;
  v.zoom = vecZoom; v.panX = vecPanX; v.panY = vecPanY;
  v.fill = vecFillRGB; v.stroke = vecStrokeRGB; v.strokeW = vecStrokeW;
  v.alpha = vecObjAlpha;
  snprintf(v.path, sizeof(v.path), "%s", vecPath);
  return sessWrite(VEC_SESS_PATH, VEC_SESS_VER, IC_VECTOR, &v, sizeof(v));
}
static void vecLoadSess(){
  if(!flexFsReady()) return;
  VecSessV1 v;
  if(sessRead(VEC_SESS_PATH, VEC_SESS_VER, IC_VECTOR, &v, sizeof(v)) != sizeof(v)) return;
  v.path[sizeof(v.path) - 1] = 0;
  // Todo lo que viene del disco se valida antes de usarse: un archivo de
  // sesion de otra version, o con un bit cambiado, no puede dejar la app
  // con una herramienta que no existe o un zoom de cero.
  vecView = (v.view == 1 && v.path[0] && flexFsExists(v.path)) ? 1 : 0;
  vecTool = (v.tool < VT_N) ? (int)v.tool : (int)VT_SELECT;
  vecGrid = v.grid != 0; vecSnap = v.snap != 0;
  vecZoom = (v.zoom >= VEC_MIN_ZOOM && v.zoom <= VEC_MAX_ZOOM) ? v.zoom : 1.0f;
  vecPanX = v.panX; vecPanY = v.panY;
  if(!(vecPanX > -1e6f && vecPanX < 1e6f)) vecPanX = 0;
  if(!(vecPanY > -1e6f && vecPanY < 1e6f)) vecPanY = 0;
  vecFillRGB = v.fill & 0xFFFFFFu;
  vecStrokeRGB = v.stroke & 0xFFFFFFu;
  vecStrokeW = (v.strokeW >= 0 && v.strokeW <= 64) ? v.strokeW : 2.0f;
  vecObjAlpha = v.alpha ? v.alpha : 255;
  snprintf(vecPath, sizeof(vecPath), "%s", vecView == 1 ? v.path : "");
}
