// #############################################################
//  test_vector.cpp  ·  pruebas de host de FlexOS_Vector.cpp
//  ------------------------------------------------------------
//  El motor de FLEX VECTOR PRO, el editor vectorial de Flex OS Ultra.
//  El modulo bajo prueba es EXACTAMENTE el que va a la placa.
//
//  POR QUE ESTO SE PRUEBA AQUI Y NO EN LA PLACA
//  ------------------------------------------------------------
//  Tres motivos, y ninguno es comodidad:
//
//   1. AQUI HAY INDICES POR TODAS PARTES. Los nodos viven en un pool
//      compartido y cada objeto posee un rango. Un 'first' mal
//      calculado no da un dibujo raro: lee memoria de otro objeto. Con
//      AddressSanitizer eso salta en la linea que lo hace; en la placa
//      salta tres pantallas despues, con el documento ya corrupto.
//   2. LOS LIMITES HAY QUE ALCANZARLOS DE VERDAD. "Que pasa con 256
//      objetos, 4096 nodos, 8 capas y el diario de deshacer lleno" no
//      es una pregunta que se conteste dibujando a mano en una
//      pantalla de 480x800. Aqui se llenan en un bucle.
//   3. EL SVG TIENE QUE ABRIRSE FUERA. Lo que se exporta se lee en un
//      navegador, no en el dispositivo, asi que la unica comprobacion
//      util es sobre el TEXTO generado.
//
//  ESTAS PRUEBAS SON LOS CRITERIOS DE ACEPTACION DE LA FASE 1. Cada
//  seccion lleva el criterio que verifica, con el mismo nombre que en
//  docs/FLEX-VECTOR-PRO.md.
// #############################################################
#include "../../FlexOS_Ultra/FlexOS_Vector.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <zlib.h>   // SOLO para la prueba: descomprime el PNG que genera el firmware

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

// ---- El anfitrion de mentira: los bloques que el motor no reserva ----
static FlexVecDoc     D;
static FlexVecArena   A;
static FlexVecRaster  R;
static FlexVecNode*   g_nodes = NULL;
static uint8_t*       g_undo  = NULL;
static char*          g_text  = NULL;
static void*          g_ras   = NULL;
#define CANVAS_W 480
#define CANVAS_H 800

static void hostInit(){
  g_nodes = (FlexVecNode*)calloc(FLEXVEC_MAX_NODES, sizeof(FlexVecNode));
  g_undo  = (uint8_t*)calloc(FLEXVEC_UNDO_BYTES, 1);
  g_text  = (char*)calloc(FLEXVEC_TEXT_BYTES, 1);
  g_ras   = calloc(flexVecRasterBytes(CANVAS_W), 1);
  A.nodes = g_nodes; A.undo = g_undo; A.text = g_text;
  flexVecAttach(&D, &A);
  flexVecRasterInit(&R, g_ras, flexVecRasterBytes(CANVAS_W), CANVAS_W);
}
static void hostFree(){ free(g_nodes); free(g_undo); free(g_text); free(g_ras); }

static int fvElemAlive(int e){ return e >= 0 && e < FLEXVEC_MAX_ELEMS && D.elems[e].layer >= 0; }

static void viewIdentity(FlexVecView* v){
  flexVecMatIdentity(v->m);
  v->clipX0 = 0; v->clipY0 = 0; v->clipX1 = CANVAS_W - 1; v->clipY1 = CANVAS_H - 1;
}

// Huella del documento: solo lo que es CONTENIDO. No entra la caja
// delimitadora en cache ni la seleccion, que son estado de interfaz --
// compararlos haria fallar la prueba de deshacer por un motivo que no
// tiene nada que ver con el documento.
static uint32_t fingerprint(const FlexVecDoc* d){
  uint32_t h = 2166136261u;
  #define MIX(p, n) do { const uint8_t* _b = (const uint8_t*)(p); \
      for(size_t _i = 0; _i < (size_t)(n); _i++){ h ^= _b[_i]; h *= 16777619u; } } while(0)
  for(int l = 0; l < FLEXVEC_MAX_LAYERS; l++){
    MIX(&d->layers[l].used, 1); MIX(&d->layers[l].visible, 1); MIX(&d->layers[l].locked, 1);
    MIX(d->layers[l].name, strlen(d->layers[l].name));
  }
  for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++){
    const FlexVecElem* el = &d->elems[e];
    if(el->layer < 0){ uint8_t z = 0xFF; MIX(&z, 1); continue; }
    MIX(&el->layer, 2); MIX(&el->z, 2); MIX(&el->kind, 1);
    MIX(&el->fill, sizeof(el->fill)); MIX(&el->stroke, sizeof(el->stroke));
    MIX(&el->strokeW, 4); MIX(&el->alpha, 1); MIX(&el->cap, 1); MIX(&el->join, 1);
    MIX(el->m, sizeof(el->m));
    MIX(&el->p0, 4); MIX(&el->p1, 4); MIX(&el->n0, 2); MIX(&el->n1, 2);
    uint16_t c = el->count; MIX(&c, 2);
    for(int i = 0; i < (int)el->count; i++){
      const FlexVecNode* n = &d->arena.nodes[el->first + i];
      MIX(&n->x, 4); MIX(&n->y, 4); MIX(&n->ix, 4); MIX(&n->iy, 4);
      MIX(&n->ox, 4); MIX(&n->oy, 4);
      uint8_t f = (uint8_t)(n->flags & ~FLEXVEC_N_SEL);
      MIX(&f, 1);
    }
  }
  #undef MIX
  return h;
}

// Contador de cobertura: suma el area REAL que el rasterizador pinta y
// vigila que ni un tramo salga de la ventana de recorte.
typedef struct { double area; int outside; int minY, maxY, minX, maxX; const FlexVecView* v; } Cov;
static void covSpan(int y, int x0, int x1, uint8_t cov, void* user){
  Cov* c = (Cov*)user;
  if(y < c->v->clipY0 || y > c->v->clipY1 || x0 < c->v->clipX0 || x1 > c->v->clipX1) c->outside++;
  if(x1 < x0){ c->outside++; return; }
  c->area += (double)(x1 - x0 + 1) * (double)cov / 255.0;
  if(y < c->minY) c->minY = y;
  if(y > c->maxY) c->maxY = y;
  if(x0 < c->minX) c->minX = x0;
  if(x1 > c->maxX) c->maxX = x1;
}
static Cov covRun(int elem, const FlexVecView* v, int stroke){
  Cov c; memset(&c, 0, sizeof(c));
  c.minY = 1 << 30; c.minX = 1 << 30; c.maxY = -1; c.maxX = -1; c.v = v;
  int subN = 0;
  int np = flexVecFlatten(&D, elem, v, &R, &subN);
  if(np <= 0 || subN <= 0) return c;
  if(stroke) flexVecStrokeFlat(&R, subN, v, 4.0f, FLEXVEC_CAP_BUTT, FLEXVEC_JOIN_MITER, covSpan, &c);
  else       flexVecFillFlat(&R, subN, v, 0, covSpan, &c);
  return c;
}

// =============================================================
//  1) DOCUMENTO Y CAPAS
//     Criterio: "existen y funcionan 3 layers con visibilidad,
//     bloqueo y reordenacion".
// =============================================================
static void testLayers(){
  std::printf("-- documento y capas --\n");
  CHECK(flexVecNew(&D, 480, 800, 3) == FLEXVEC_OK, "documento nuevo con 3 capas");
  int live = 0;
  for(int i = 0; i < FLEXVEC_MAX_LAYERS; i++) if(D.layers[i].used) live++;
  CHECK(live == 3, "tres capas vivas (habia %d)", live);
  CHECK(D.layerActive == 0, "la capa activa es la primera");

  CHECK(flexVecLayerSetVisible(&D, 1, 0) == FLEXVEC_OK, "ocultar la capa 1");
  CHECK(D.layers[1].visible == 0, "la capa 1 queda oculta");
  CHECK(flexVecLayerSetVisible(&D, 1, 1) == FLEXVEC_OK, "volver a mostrarla");

  // Un objeto en una capa BLOQUEADA no se puede tocar. Es la razon de
  // ser del bloqueo: si se pudiera mover igual, no serviria de nada.
  flexVecLayerSetActive(&D, 2);
  int e = flexVecAddRect(&D, 10, 10, 40, 40, 0);
  CHECK(e >= 0, "objeto en la capa 2");
  CHECK(flexVecLayerSetLocked(&D, 2, 1) == FLEXVEC_OK, "bloquear la capa 2");
  CHECK(flexVecTranslate(&D, e, 5, 5) == FLEXVEC_E_LOCKED, "no se mueve un objeto de capa bloqueada");
  CHECK(flexVecDelete(&D, e) == FLEXVEC_E_LOCKED, "no se borra un objeto de capa bloqueada");
  CHECK(flexVecSelect(&D, e, 0) == FLEXVEC_E_LOCKED, "no se selecciona");
  CHECK(flexVecAddRect(&D, 0, 0, 10, 10, 0) == -FLEXVEC_E_LOCKED, "no se crea nada en una capa bloqueada");
  CHECK(flexVecLayerSetLocked(&D, 2, 0) == FLEXVEC_OK, "desbloquear");
  CHECK(flexVecTranslate(&D, e, 5, 5) == FLEXVEC_OK, "desbloqueada, si se mueve");

  // Reordenar: el objeto sigue a SU capa, no se queda en el indice.
  CHECK(flexVecLayerMove(&D, 2, -1) == FLEXVEC_OK, "subir la capa 2");
  CHECK(D.elems[e].layer == 1, "el objeto acompano a su capa (esta en %d)", D.elems[e].layer);

  // Nunca se puede quedar el documento sin capas.
  flexVecNew(&D, 480, 800, 1);
  CHECK(flexVecLayerDelete(&D, 0) == FLEXVEC_E_BADARG, "no se borra la ultima capa");

  // Techo de capas.
  flexVecNew(&D, 480, 800, 1);
  int added = 1;
  while(flexVecLayerAdd(&D, NULL) >= 0) added++;
  CHECK(added == FLEXVEC_MAX_LAYERS, "se llegan a crear %d capas (fueron %d)", FLEXVEC_MAX_LAYERS, added);
  CHECK(flexVecLayerAdd(&D, NULL) == -FLEXVEC_E_FULL_LAYERS, "la novena capa se niega con su motivo");
}

// =============================================================
//  2) PLUMA Y GEOMETRIA BEZIER
//     Criterio: "se puede dibujar con Pen Tool un path cerrado con
//     curvas y editarlo con Direct Selection sin errores".
// =============================================================
static void testPen(){
  std::printf("-- herramienta Pluma y edicion de nodos --\n");
  flexVecNew(&D, 480, 800, 1);
  int e = flexVecAddPath(&D);
  CHECK(e >= 0, "trazado vacio");
  CHECK(D.elems[e].fill.type == FLEXVEC_P_NONE, "la Pluma empieza sin relleno");
  // Tres anclas: dos de vertice y una suave con tirador (tap-arrastre).
  CHECK(flexVecNodeAppend(&D, e, 100, 100, 0, 0, 0) == FLEXVEC_OK, "ancla 1");
  CHECK(flexVecNodeAppend(&D, e, 200, 100, 240, 60, 1) == FLEXVEC_OK, "ancla 2 suave");
  CHECK(flexVecNodeAppend(&D, e, 200, 200, 0, 0, 0) == FLEXVEC_OK, "ancla 3");
  CHECK(D.elems[e].count == 3, "tres anclas");
  CHECK((D.arena.nodes[D.elems[e].first + 0].flags & FLEXVEC_N_START) != 0, "la primera abre el subtrazado");
  CHECK((D.arena.nodes[D.elems[e].first + 1].flags & FLEXVEC_N_SMOOTH) != 0, "la segunda es suave");

  CHECK(flexVecPathClose(&D, e, 1) == FLEXVEC_OK, "cerrar el trazado");
  CHECK((D.arena.nodes[D.elems[e].first].flags & FLEXVEC_N_CLOSE) != 0, "queda cerrado");
  CHECK(D.elems[e].fill.type == FLEXVEC_P_SOLID, "cerrar pide relleno");

  // Tirador simetrico: mover uno refleja el otro respecto del ancla.
  const FlexVecNode* n1 = &D.arena.nodes[D.elems[e].first + 1];
  CHECK(flexVecHandleMove(&D, e, 1, 1, 250, 80) == FLEXVEC_OK, "mover el tirador de salida");
  float sx = n1->x - n1->ix, sy = n1->y - n1->iy;   // vector al tirador de entrada, invertido
  float ox = n1->ox - n1->x, oy = n1->oy - n1->y;
  float cross = sx * oy - sy * ox;
  CHECK(fabsf(cross) < 0.5f, "los dos tiradores quedan alineados (producto cruzado %.3f)", (double)cross);
  CHECK((n1->ox - n1->x) * (n1->x - n1->ix) >= 0, "y en sentidos opuestos respecto del ancla");

  // Dividir un segmento NO cambia la forma: el punto medio de la curva
  // original tiene que seguir estando donde estaba.
  flexVecNew(&D, 480, 800, 1);
  e = flexVecAddPath(&D);
  flexVecNodeAppend(&D, e, 0, 0, 0, 0, 0);
  flexVecNodeAppend(&D, e, 100, 0, 0, 0, 0);
  FlexVecNode* nd = &D.arena.nodes[D.elems[e].first];
  nd[0].ox = 0;   nd[0].oy = 80;      // una curva de verdad
  nd[1].ix = 100; nd[1].iy = 80;
  CHECK(flexVecNodeSplit(&D, e, 0, 0.5f) == FLEXVEC_OK, "dividir por la mitad");
  CHECK(D.elems[e].count == 3, "un ancla mas");
  nd = &D.arena.nodes[D.elems[e].first];
  // Punto medio de la cubica original: (0,0) (0,80) (100,80) (100,0) en t=0,5
  // -> x = 50, y = 60.
  CHECK(fabsf(nd[1].x - 50.0f) < 0.01f && fabsf(nd[1].y - 60.0f) < 0.01f,
        "el ancla nueva cae en la curva original (%.2f, %.2f)", (double)nd[1].x, (double)nd[1].y);
  CHECK((nd[1].flags & FLEXVEC_N_SMOOTH) != 0, "el ancla de la division es suave");

  // Convertir esquina <-> suave.
  CHECK(flexVecNodeSetSmooth(&D, e, 0, 1) == FLEXVEC_OK, "suavizar");
  CHECK((D.arena.nodes[D.elems[e].first].flags & FLEXVEC_N_SMOOTH) != 0, "queda suave");
  CHECK(flexVecNodeSetSmooth(&D, e, 0, 0) == FLEXVEC_OK, "convertir a vertice");
  nd = &D.arena.nodes[D.elems[e].first];
  CHECK(nd[0].ix == nd[0].x && nd[0].oy == nd[0].y, "un vertice no tiene tiradores");

  // Borrar anclas hasta vaciar el trazado: el objeto desaparece solo, sin
  // dejar una geometria de un punto que no se puede ni ver ni tocar.
  int before = flexVecElemCount(&D);
  while(D.elems[e].layer >= 0 && D.elems[e].count > 0)
    CHECK(flexVecNodeDelete(&D, e, 0) == FLEXVEC_OK, "borrar ancla");
  CHECK(flexVecElemCount(&D) == before - 1, "el trazado vacio se retira solo");

  // Insertar delante conserva el papel de "abre el subtrazado".
  e = flexVecAddPath(&D);
  flexVecNodeAppend(&D, e, 10, 10, 0, 0, 0);
  flexVecNodeAppend(&D, e, 20, 20, 0, 0, 0);
  flexVecNodeAppend(&D, e, 20, 10, 0, 0, 0);   // cerrar exige al menos tres
  CHECK(flexVecPathClose(&D, e, 1) == FLEXVEC_OK, "cerrar antes de insertar");
  CHECK(flexVecNodeInsert(&D, e, 0, 5, 5) == FLEXVEC_OK, "insertar al principio");
  nd = &D.arena.nodes[D.elems[e].first];
  CHECK((nd[0].flags & FLEXVEC_N_START) != 0, "el nuevo primero abre el subtrazado");
  CHECK((nd[0].flags & FLEXVEC_N_CLOSE) != 0, "y hereda el cierre");
  CHECK((nd[1].flags & FLEXVEC_N_START) == 0, "el antiguo primero ya no lo abre");
}

// =============================================================
//  3) FORMAS PRIMITIVAS Y APARIENCIA
// =============================================================
static void testShapes(){
  std::printf("-- formas primitivas y apariencia --\n");
  flexVecNew(&D, 480, 800, 1);
  int r = flexVecAddRect(&D, 20, 30, 100, 50, 0);
  int el = flexVecAddEllipse(&D, 200, 200, 40, 20);
  int ln = flexVecAddLine(&D, 0, 0, 10, 10);
  int pg = flexVecAddPolygon(&D, 300, 300, 50, 6);
  int st = flexVecAddStar(&D, 400, 400, 50, 25, 5);
  CHECK(r >= 0 && el >= 0 && ln >= 0 && pg >= 0 && st >= 0, "las cinco primitivas");
  CHECK(D.elems[r].count == 4, "el rectangulo son 4 anclas");
  CHECK(D.elems[pg].count == 6, "el hexagono son 6 anclas");
  CHECK(D.elems[st].count == 10, "la estrella de 5 puntas son 10 anclas");
  CHECK(D.elems[ln].fill.type == FLEXVEC_P_NONE, "una linea no se rellena");

  float x0, y0, x1, y1;
  flexVecBBox(&D, r, &x0, &y0, &x1, &y1);
  CHECK(fabsf(x0 - 20) < 0.01f && fabsf(y0 - 30) < 0.01f &&
        fabsf(x1 - 120) < 0.01f && fabsf(y1 - 80) < 0.01f,
        "caja del rectangulo: (%.1f,%.1f)-(%.1f,%.1f)", (double)x0, (double)y0, (double)x1, (double)y1);
  flexVecBBox(&D, el, &x0, &y0, &x1, &y1);
  CHECK(fabsf(x0 - 160) < 0.5f && fabsf(y0 - 180) < 0.5f &&
        fabsf(x1 - 240) < 0.5f && fabsf(y1 - 220) < 0.5f,
        "caja de la elipse: (%.1f,%.1f)-(%.1f,%.1f)", (double)x0, (double)y0, (double)x1, (double)y1);

  // El rectangulo redondeado tiene la MISMA caja: las esquinas curvas no
  // pueden salirse del rectangulo, y si la caja las siguiera con los
  // puntos de control saldria mas grande de la cuenta.
  int rr = flexVecAddRect(&D, 20, 30, 100, 50, 12);
  flexVecBBox(&D, rr, &x0, &y0, &x1, &y1);
  CHECK(fabsf(x0 - 20) < 0.01f && fabsf(x1 - 120) < 0.01f &&
        fabsf(y0 - 30) < 0.01f && fabsf(y1 - 80) < 0.01f,
        "el redondeo no agranda la caja: (%.2f,%.2f)-(%.2f,%.2f)",
        (double)x0, (double)y0, (double)x1, (double)y1);
  CHECK(D.elems[rr].count == 8, "el rectangulo redondeado son 8 anclas");
  // El radio se acota a la mitad del lado corto: pedir 999 no deforma.
  int rr2 = flexVecAddRect(&D, 0, 0, 40, 20, 999);
  CHECK(rr2 >= 0 && D.elems[rr2].p0 <= 10.01f, "el radio se acota (%.2f)", (double)D.elems[rr2].p0);

  CHECK(flexVecSetFill(&D, r, 0xFF8800, 200) == FLEXVEC_OK, "relleno");
  CHECK(D.elems[r].fill.rgb == 0xFF8800u && D.elems[r].fill.alpha == 200, "el relleno queda");
  CHECK(flexVecSetStroke(&D, r, 0x112233, 255, 3.5f) == FLEXVEC_OK, "trazo");
  CHECK(D.elems[r].strokeW == 3.5f, "grosor");
  CHECK(flexVecSetAlpha(&D, r, 128) == FLEXVEC_OK, "opacidad");
  CHECK(D.elems[r].alpha == 128, "la opacidad queda");
  CHECK(flexVecSetNoFill(&D, r) == FLEXVEC_OK, "sin relleno");
  CHECK(D.elems[r].fill.type == FLEXVEC_P_NONE, "queda sin relleno");
  CHECK(flexVecSetCapJoin(&D, r, 99, 0) == FLEXVEC_E_BADARG, "un cap invalido se rechaza");

  // Ancho o alto negativos: se normalizan, no producen una caja al reves.
  int neg = flexVecAddRect(&D, 100, 100, -40, -30, 0);
  CHECK(neg >= 0, "rectangulo con medidas negativas");
  flexVecBBox(&D, neg, &x0, &y0, &x1, &y1);
  CHECK(x0 < x1 && y0 < y1, "la caja sale bien orientada");
}

// =============================================================
//  4) TRANSFORMACIONES AFINES
// =============================================================
static void testTransforms(){
  std::printf("-- transformaciones afines --\n");
  float a[6], b[6], c[6], inv[6];
  flexVecMatIdentity(a);
  CHECK(a[0] == 1 && a[3] == 1 && a[1] == 0 && a[4] == 0, "identidad");
  float t[6] = { 1, 0, 0, 1, 10, 20 };
  float s[6] = { 2, 0, 0, 3, 0, 0 };
  flexVecMatMul(t, s, c);                  // primero escalar, luego trasladar
  float ox, oy;
  flexVecMatApply(c, 1, 1, &ox, &oy);
  CHECK(fabsf(ox - 12) < 0.001f && fabsf(oy - 23) < 0.001f,
        "composicion: (1,1) -> (%.2f,%.2f), se esperaba (12,23)", (double)ox, (double)oy);
  CHECK(flexVecMatInvert(c, inv) == 1, "se puede invertir");
  float bx, by;
  flexVecMatApply(inv, ox, oy, &bx, &by);
  CHECK(fabsf(bx - 1) < 0.001f && fabsf(by - 1) < 0.001f, "la inversa devuelve el punto");
  float sing[6] = { 1, 2, 2, 4, 0, 0 };
  CHECK(flexVecMatInvert(sing, inv) == 0, "una matriz singular no se invierte");
  flexVecMatApplyVec(c, 1, 0, &ox, &oy);
  CHECK(fabsf(ox - 2) < 0.001f && fabsf(oy) < 0.001f, "un VECTOR no lleva traslacion");
  (void)b;

  flexVecNew(&D, 480, 800, 1);
  int e = flexVecAddRect(&D, 0, 0, 100, 50, 0);
  float x0, y0, x1, y1;
  CHECK(flexVecTranslate(&D, e, 30, 40) == FLEXVEC_OK, "mover");
  flexVecBBox(&D, e, &x0, &y0, &x1, &y1);
  CHECK(fabsf(x0 - 30) < 0.01f && fabsf(y0 - 40) < 0.01f, "movido a (%.1f,%.1f)", (double)x0, (double)y0);
  CHECK(D.elems[e].count == 4, "mover NO reescribe la geometria (sigue con 4 anclas)");

  CHECK(flexVecScale(&D, e, 30, 40, 2, 2) == FLEXVEC_OK, "escalar x2 desde su esquina");
  flexVecBBox(&D, e, &x0, &y0, &x1, &y1);
  CHECK(fabsf(x1 - x0 - 200) < 0.01f && fabsf(y1 - y0 - 100) < 0.01f,
        "escalado: %.1f x %.1f", (double)(x1 - x0), (double)(y1 - y0));

  // Girar 90 grados intercambia ancho y alto. Con la caja calculada a
  // partir de la caja anterior en vez de la geometria, esto fallaria.
  flexVecNew(&D, 480, 800, 1);
  e = flexVecAddRect(&D, 0, 0, 100, 50, 0);
  CHECK(flexVecRotate(&D, e, 50, 25, 1.5707963f) == FLEXVEC_OK, "girar 90 grados");
  flexVecBBox(&D, e, &x0, &y0, &x1, &y1);
  CHECK(fabsf((x1 - x0) - 50) < 0.1f && fabsf((y1 - y0) - 100) < 0.1f,
        "girado: %.1f x %.1f (se esperaba 50 x 100)", (double)(x1 - x0), (double)(y1 - y0));

  // Reflejar dos veces vuelve al sitio.
  uint32_t h0 = fingerprint(&D);
  flexVecMirror(&D, e, 50, 25, 1);
  flexVecMirror(&D, e, 50, 25, 1);
  flexVecBBox(&D, e, &x0, &y0, &x1, &y1);
  CHECK(fabsf((x1 - x0) - 50) < 0.1f, "reflejar dos veces conserva el tamano");
  (void)h0;

  // Escala CERO: se acota. Si la matriz se volviera singular, el objeto
  // no tendria vuelta atras -- no se podria invertir para deshacer.
  CHECK(flexVecScale(&D, e, 0, 0, 0, 0) == FLEXVEC_OK, "escala cero se acepta acotada");
  CHECK(flexVecMatInvert(D.elems[e].m, inv) == 1, "la matriz sigue siendo invertible");

  // Valores no finitos: se rechazan en la puerta.
  float nan = strtof("nan", NULL), inf = strtof("inf", NULL);
  CHECK(flexVecTranslate(&D, e, nan, 0) == FLEXVEC_E_BADARG, "NaN se rechaza");
  CHECK(flexVecTranslate(&D, e, 0, inf) == FLEXVEC_E_BADARG, "infinito se rechaza");
  CHECK(flexVecAddRect(&D, nan, 0, 10, 10, 0) == -FLEXVEC_E_BADARG, "un rectangulo con NaN se rechaza");
  CHECK(flexVecAddEllipse(&D, 0, 0, inf, 10) == -FLEXVEC_E_BADARG, "una elipse con infinito se rechaza");

  // Transformar cada uno: cada objeto respecto de SU centro.
  flexVecNew(&D, 480, 800, 1);
  int e1 = flexVecAddRect(&D, 0, 0, 20, 20, 0);
  int e2 = flexVecAddRect(&D, 200, 200, 20, 20, 0);
  flexVecSelect(&D, e1, 0); flexVecSelect(&D, e2, 1);
  float m2[6] = { 2, 0, 0, 2, 0, 0 };
  CHECK(flexVecTransformSel(&D, m2, 1) == FLEXVEC_OK, "transformar cada uno");
  flexVecBBox(&D, e2, &x0, &y0, &x1, &y1);
  CHECK(fabsf((x0 + x1) / 2 - 210) < 0.5f,
        "el segundo se escalo sobre SU centro (centro en %.1f)", (double)((x0 + x1) / 2));
}

// =============================================================
//  5) SELECCION, IMPACTO, ALINEAR Y ORDEN
// =============================================================
static void testSelection(){
  std::printf("-- seleccion, impacto y alineacion --\n");
  flexVecNew(&D, 480, 800, 1);
  int a = flexVecAddRect(&D, 0, 0, 100, 100, 0);
  int b = flexVecAddRect(&D, 200, 0, 50, 50, 0);
  flexVecSetFill(&D, a, 0x000000, 255);
  flexVecSetFill(&D, b, 0x000000, 255);

  CHECK(flexVecHitTest(&D, 50, 50, 2) == a, "impacto dentro del relleno");
  CHECK(flexVecHitTest(&D, 150, 150, 2) == -1, "impacto en el vacio");
  CHECK(flexVecHitTest(&D, 225, 25, 2) == b, "impacto en el segundo");
  // El de mas arriba gana: es lo que espera cualquiera al tocar dos
  // objetos superpuestos.
  int c = flexVecAddRect(&D, 25, 25, 50, 50, 0);
  flexVecSetFill(&D, c, 0x000000, 255);
  CHECK(flexVecHitTest(&D, 50, 50, 2) == c, "gana el objeto de encima");
  // Sin relleno solo se toca el CONTORNO, como en Illustrator.
  flexVecSetNoFill(&D, c);
  flexVecSetStroke(&D, c, 0, 255, 2);
  CHECK(flexVecHitTest(&D, 50, 50, 2) == a, "sin relleno, el interior no se toca");
  CHECK(flexVecHitTest(&D, 25, 50, 3) == c, "el contorno si");

  flexVecSelectNone(&D);
  CHECK(flexVecSelCount(&D) == 0, "sin seleccion");
  flexVecSelect(&D, a, 0);
  CHECK(flexVecSelCount(&D) == 1, "uno seleccionado");
  flexVecSelect(&D, b, 1);
  CHECK(flexVecSelCount(&D) == 2, "dos con 'add'");
  flexVecSelect(&D, a, 0);
  CHECK(flexVecSelCount(&D) == 1, "sin 'add' se reemplaza");

  // Marco de seleccion: solo lo que entra ENTERO, como Illustrator.
  flexVecSelectNone(&D);
  int n = flexVecSelectRect(&D, -10, -10, 110, 110, 0);
  CHECK(n == 2, "el marco cogio %d (a y c estan dentro enteros)", n);
  n = flexVecSelectRect(&D, -10, -10, 60, 60, 0);
  CHECK(n == 0, "un objeto a medias NO entra");

  float x0, y0, x1, y1;
  flexVecSelectNone(&D);
  CHECK(flexVecSelBBox(&D, &x0, &y0, &x1, &y1) == FLEXVEC_E_EMPTY, "caja de una seleccion vacia");
  flexVecSelect(&D, a, 0); flexVecSelect(&D, b, 1);
  CHECK(flexVecSelBBox(&D, &x0, &y0, &x1, &y1) == FLEXVEC_OK, "caja de la seleccion");
  CHECK(fabsf(x0) < 0.01f && fabsf(x1 - 250) < 0.01f, "la caja abarca los dos");

  CHECK(flexVecAlign(&D, FLEXVEC_AL_LEFT) == FLEXVEC_OK, "alinear a la izquierda");
  flexVecBBox(&D, b, &x0, &y0, &x1, &y1);
  CHECK(fabsf(x0) < 0.01f, "el segundo se fue al borde izquierdo (%.2f)", (double)x0);
  CHECK(flexVecAlign(&D, 99) == FLEXVEC_E_BADARG, "una alineacion inventada se rechaza");
  flexVecSelectNone(&D);
  CHECK(flexVecAlign(&D, FLEXVEC_AL_LEFT) == FLEXVEC_E_EMPTY, "alinear sin seleccion");
  CHECK(flexVecDistribute(&D, 1) == FLEXVEC_E_EMPTY, "distribuir sin seleccion");

  // Distribuir: con tres, el de en medio queda equidistante.
  flexVecNew(&D, 480, 800, 1);
  int p = flexVecAddRect(&D, 0, 0, 10, 10, 0);
  int q = flexVecAddRect(&D, 30, 0, 10, 10, 0);
  int rr = flexVecAddRect(&D, 300, 0, 10, 10, 0);
  flexVecSelect(&D, p, 0); flexVecSelect(&D, q, 1); flexVecSelect(&D, rr, 1);
  CHECK(flexVecDistribute(&D, 1) == FLEXVEC_OK, "distribuir en horizontal");
  flexVecBBox(&D, q, &x0, &y0, &x1, &y1);
  CHECK(fabsf((x0 + x1) / 2 - 155) < 1.0f,
        "el de en medio queda equidistante (centro %.1f, se esperaba 155)", (double)((x0 + x1) / 2));

  // Orden Z dentro de la capa.
  flexVecNew(&D, 480, 800, 1);
  int z1 = flexVecAddRect(&D, 0, 0, 10, 10, 0);
  int z2 = flexVecAddRect(&D, 0, 0, 10, 10, 0);
  int z3 = flexVecAddRect(&D, 0, 0, 10, 10, 0);
  int16_t ord[FLEXVEC_MAX_ELEMS];
  int cnt = flexVecPaintOrder(&D, ord, FLEXVEC_MAX_ELEMS);
  CHECK(cnt == 3 && ord[0] == z1 && ord[2] == z3, "orden de pintado inicial");
  CHECK(flexVecToBack(&D, z3) == FLEXVEC_OK, "al fondo");
  cnt = flexVecPaintOrder(&D, ord, FLEXVEC_MAX_ELEMS);
  CHECK(ord[0] == z3, "el ultimo paso a ser el primero en pintarse");
  CHECK(flexVecToFront(&D, z1) == FLEXVEC_OK, "al frente");
  cnt = flexVecPaintOrder(&D, ord, FLEXVEC_MAX_ELEMS);
  CHECK(ord[cnt - 1] == z1, "y el primero al final");
  CHECK(flexVecRaise(&D, z1) == FLEXVEC_E_EMPTY, "ya esta arriba del todo");
  (void)z2;
}

// =============================================================
//  6) DESHACER / REHACER
//     Criterio: "se puede deshacer y rehacer al menos N pasos sin
//     corrupcion del documento".
// =============================================================
static void testUndo(){
  std::printf("-- deshacer y rehacer --\n");
  flexVecNew(&D, 480, 800, 2);
  uint32_t empty = fingerprint(&D);

  // 60 acciones encadenadas de las cuatro clases que existen: crear,
  // transformar, cambiar apariencia y borrar.
  const int N = 60;
  uint32_t marks[N + 1];
  marks[0] = empty;
  int ids[N];
  for(int i = 0; i < N; i++){
    if(i % 4 == 3 && i > 3 && ids[i - 1] >= 0){
      flexVecDelete(&D, ids[i - 1]);
      ids[i] = -1;
    } else if(i % 4 == 1 && i > 0 && ids[i - 1] >= 0){
      flexVecTranslate(&D, ids[i - 1], 3.0f, 1.0f);
      ids[i] = ids[i - 1];
    } else if(i % 4 == 2 && i > 0 && ids[i - 1] >= 0){
      flexVecSetFill(&D, ids[i - 1], (uint32_t)(i * 7919) & 0xFFFFFFu, 255);
      ids[i] = ids[i - 1];
    } else {
      ids[i] = flexVecAddRect(&D, (float)(i * 3), (float)(i * 2), 20, 15, 0);
    }
    marks[i + 1] = fingerprint(&D);
  }
  CHECK(flexVecUndoSteps(&D) >= N, "se registraron %d pasos (%d)", N, flexVecUndoSteps(&D));
  CHECK(flexVecUndoBytes(&D) <= FLEXVEC_UNDO_BYTES, "el diario no pasa de su presupuesto");

  // Deshacer del todo, comprobando el estado EXACTO en cada paso.
  int okBack = 1;
  for(int i = N - 1; i >= 0; i--){
    if(flexVecUndo(&D) != FLEXVEC_OK){ okBack = 0; break; }
    if(fingerprint(&D) != marks[i]){ okBack = 0; std::printf("   (diverge al deshacer el paso %d)\n", i); break; }
  }
  CHECK(okBack, "los %d pasos se deshacen uno a uno y el documento coincide", N);
  CHECK(!flexVecCanUndo(&D), "ya no queda nada que deshacer");
  CHECK(flexVecUndo(&D) == FLEXVEC_E_EMPTY, "deshacer de mas no rompe nada");

  // Rehacer del todo.
  int okFwd = 1;
  for(int i = 0; i < N; i++){
    if(flexVecRedo(&D) != FLEXVEC_OK){ okFwd = 0; break; }
    if(fingerprint(&D) != marks[i + 1]){ okFwd = 0; std::printf("   (diverge al rehacer el paso %d)\n", i); break; }
  }
  CHECK(okFwd, "los %d pasos se rehacen y el documento vuelve a coincidir", N);
  CHECK(!flexVecCanRedo(&D), "ya no queda nada que rehacer");
  CHECK(flexVecRedo(&D) == FLEXVEC_E_EMPTY, "rehacer de mas no rompe nada");

  // Una accion nueva despues de deshacer TRUNCA lo rehacible. Si no lo
  // hiciera, rehacer aplicaria un "despues" que ya no corresponde a este
  // documento: eso es exactamente como se corrompe un historial.
  flexVecUndo(&D); flexVecUndo(&D);
  CHECK(flexVecCanRedo(&D), "hay algo que rehacer");
  flexVecAddEllipse(&D, 10, 10, 5, 5);
  CHECK(!flexVecCanRedo(&D), "una accion nueva tira lo rehacible");

  // Una transaccion agrupa varias operaciones en UN paso. Es lo que hace
  // que arrastrar seis objetos se deshaga de una vez y no seis.
  flexVecNew(&D, 480, 800, 1);
  int a = flexVecAddRect(&D, 0, 0, 10, 10, 0);
  int b = flexVecAddRect(&D, 20, 0, 10, 10, 0);
  uint32_t before = fingerprint(&D);
  int steps = flexVecUndoSteps(&D);
  flexVecUndoBegin(&D, "grupo");
  flexVecTranslate(&D, a, 5, 5);
  flexVecTranslate(&D, b, 5, 5);
  flexVecSetFill(&D, a, 0x123456, 255);
  flexVecUndoCommit(&D);
  CHECK(flexVecUndoSteps(&D) == steps + 1, "tres operaciones, UN paso");
  flexVecUndo(&D);
  CHECK(fingerprint(&D) == before, "deshacer el grupo devuelve las tres a la vez");

  // Abortar: no deja ni rastro en el diario.
  steps = flexVecUndoSteps(&D);
  flexVecUndoBegin(&D, "abortada");
  flexVecTranslate(&D, a, 1, 1);
  flexVecUndoAbort(&D);
  CHECK(flexVecUndoSteps(&D) == steps, "una transaccion abortada no deja paso");
  CHECK(flexVecUndoBegin(&D, "x") == FLEXVEC_OK, "se puede volver a empezar");
  CHECK(flexVecUndoBegin(&D, "y") == FLEXVEC_E_BADARG, "no se anidan transacciones");
  flexVecUndoAbort(&D);
}

// =============================================================
//  7) RASTERIZADOR
//     Criterio: el render sale donde tiene que salir, cubre el area
//     que tiene que cubrir y NO se sale de la ventana de recorte.
// =============================================================
static void testRaster(){
  std::printf("-- rasterizador por lineas de barrido --\n");
  flexVecNew(&D, 480, 800, 1);
  FlexVecView v; viewIdentity(&v);

  // Un rectangulo de 100x50 con los bordes en coordenadas enteras: el
  // area pintada tiene que ser EXACTAMENTE 5000 pixeles. Es la prueba
  // que caza un error de medio pixel en el barrido.
  int r = flexVecAddRect(&D, 10, 10, 100, 50, 0);
  Cov c = covRun(r, &v, 0);
  CHECK(fabs(c.area - 5000.0) < 1.0, "area del rectangulo: %.2f (se esperaba 5000)", c.area);
  CHECK(c.outside == 0, "ni un tramo fuera de la ventana");
  CHECK(c.minX == 10 && c.maxX == 109, "columnas 10..109 (fueron %d..%d)", c.minX, c.maxX);
  CHECK(c.minY == 10 && c.maxY == 59, "filas 10..59 (fueron %d..%d)", c.minY, c.maxY);

  // Un circulo: pi*r^2. El antialias tiene que dejar el area dentro del
  // 1%, que es lo que separa un borde suave de un borde mordido.
  flexVecNew(&D, 480, 800, 1);
  int el = flexVecAddEllipse(&D, 200, 200, 60, 60);
  c = covRun(el, &v, 0);
  double want = 3.14159265 * 60.0 * 60.0;
  CHECK(fabs(c.area - want) / want < 0.01, "area del circulo: %.0f (se esperaba %.0f)", c.area, want);

  // RECORTE: la ventana manda. Una figura que se sale no puede escribir
  // ni un pixel fuera -- en la placa eso seria pintar sobre la cabecera
  // de la app o sobre la barra del sistema.
  FlexVecView small = v;
  small.clipX0 = 100; small.clipY0 = 100; small.clipX1 = 200; small.clipY1 = 200;
  c = covRun(el, &small, 0);
  CHECK(c.outside == 0, "recorte respetado");
  CHECK(c.minX >= 100 && c.maxX <= 200 && c.minY >= 100 && c.maxY <= 200,
        "todo dentro de la ventana (%d..%d, %d..%d)", c.minX, c.maxX, c.minY, c.maxY);

  // El zoom escala el area por el cuadrado: es la comprobacion de que la
  // vista y la geometria se componen en la MISMA matriz.
  flexVecNew(&D, 480, 800, 1);
  r = flexVecAddRect(&D, 0, 0, 50, 50, 0);
  FlexVecView z; viewIdentity(&z);
  z.m[0] = 2; z.m[3] = 2;
  c = covRun(r, &z, 0);
  CHECK(fabs(c.area - 10000.0) < 20.0, "con zoom x2 el area se cuadruplica: %.0f", c.area);

  // El TRAZO cubre aproximadamente perimetro x grosor.
  flexVecNew(&D, 480, 800, 1);
  int ln = flexVecAddLine(&D, 50, 50, 250, 50);
  c = covRun(ln, &v, 1);                       // grosor 4 en covRun
  CHECK(fabs(c.area - 800.0) / 800.0 < 0.06, "area del trazo: %.0f (se esperaba ~800)", c.area);
  CHECK(c.outside == 0, "el trazo tampoco se sale");

  // Una figura ENTERAMENTE fuera de la ventana no emite ni un tramo.
  flexVecNew(&D, 480, 800, 1);
  int far = flexVecAddRect(&D, 5000, 5000, 10, 10, 0);
  c = covRun(far, &v, 0);
  CHECK(c.area == 0.0, "una figura fuera de pantalla no pinta nada");

  // Un trazado con dos subtrazados, el interior al reves: la regla de
  // NO-CERO tiene que dejar el hueco vacio. Es lo que hace que una
  // letra "O" tenga agujero.
  flexVecNew(&D, 480, 800, 1);
  int donut = flexVecAddPath(&D);
  const float OUT[4][2] = { {100,100}, {200,100}, {200,200}, {100,200} };
  const float IN[4][2]  = { {130,130}, {130,170}, {170,170}, {170,130} };   // sentido contrario
  for(int i = 0; i < 4; i++) flexVecNodeAppend(&D, donut, OUT[i][0], OUT[i][1], 0, 0, 0);
  flexVecPathClose(&D, donut, 1);
  {
    // Segundo subtrazado: se anaden los nodos y se marca el primero.
    for(int i = 0; i < 4; i++) flexVecNodeAppend(&D, donut, IN[i][0], IN[i][1], 0, 0, 0);
    FlexVecNode* nd = &D.arena.nodes[D.elems[donut].first];
    nd[4].flags |= (uint8_t)(FLEXVEC_N_START | FLEXVEC_N_CLOSE);
    D.elems[donut].bboxOk = 0;
  }
  c = covRun(donut, &v, 0);
  CHECK(fabs(c.area - (10000.0 - 1600.0)) < 60.0,
        "el hueco queda vacio: %.0f (se esperaba ~8400)", c.area);

  // Caja en PIXELES: es la que decide la banda sucia. Si se queda corta,
  // al mover un objeto quedan restos en pantalla.
  flexVecNew(&D, 480, 800, 1);
  r = flexVecAddRect(&D, 10, 10, 100, 50, 0);
  flexVecSetStroke(&D, r, 0, 255, 10);
  int bx0, by0, bx1, by1;
  CHECK(flexVecElemPixBBox(&D, r, &v, &bx0, &by0, &bx1, &by1) == FLEXVEC_OK, "caja en pixeles");
  CHECK(bx0 <= 4 && by0 <= 4 && bx1 >= 116 && by1 >= 66,
        "la caja incluye el trazo y el antialias (%d,%d)-(%d,%d)", bx0, by0, bx1, by1);
}

// =============================================================
//  8) AJUSTE A CUADRICULA
// =============================================================
static void testSnap(){
  std::printf("-- ajuste a la cuadricula --\n");
  CHECK(flexVecSnap(23.0f, 20.0f) == 20.0f, "23 -> 20");
  CHECK(flexVecSnap(31.0f, 20.0f) == 40.0f, "31 -> 40");
  CHECK(flexVecSnap(-9.0f, 20.0f) == -0.0f || flexVecSnap(-9.0f, 20.0f) == 0.0f, "-9 -> 0");
  CHECK(flexVecSnap(7.0f, 0.0f) == 7.0f, "un paso de cero no ajusta nada");
  flexVecNew(&D, 480, 800, 1);
  int e = flexVecAddRect(&D, 23, 37, 40, 40, 0);
  flexVecSelect(&D, e, 0);
  CHECK(flexVecSnapSel(&D, 20.0f) == FLEXVEC_OK, "ajustar la seleccion");
  float x0, y0, x1, y1;
  flexVecBBox(&D, e, &x0, &y0, &x1, &y1);
  CHECK(fabsf(x0 - 20) < 0.01f && fabsf(y0 - 40) < 0.01f,
        "esquina ajustada a (%.1f,%.1f)", (double)x0, (double)y0);
}

// =============================================================
//  9) EXPORTACION A SVG
//     Criterio: "el SVG resultante se abre correctamente en un visor
//     SVG estandar externo" -- aqui se verifica que es SVG bien
//     formado y que lleva la geometria, el relleno, el trazo y la
//     opacidad de verdad.
// =============================================================
static int countStr(const char* hay, const char* needle){
  int n = 0;
  for(const char* p = strstr(hay, needle); p; p = strstr(p + 1, needle)) n++;
  return n;
}
static void testSVG(){
  std::printf("-- exportacion a SVG --\n");
  static char buf[FLEXVEC_SVG_BYTES];
  flexVecNew(&D, 480, 800, 2);
  snprintf(D.layers[0].name, FLEXVEC_NAME_MAX, "Fondo");
  int r = flexVecAddRect(&D, 10, 20, 100, 50, 0);
  flexVecSetFill(&D, r, 0xFF8800, 255);
  flexVecSetStroke(&D, r, 0x112233, 128, 3.0f);
  flexVecSetAlpha(&D, r, 200);
  flexVecSetCapJoin(&D, r, FLEXVEC_CAP_ROUND, FLEXVEC_JOIN_ROUND);
  flexVecLayerSetActive(&D, 1);
  int e = flexVecAddEllipse(&D, 200, 200, 40, 20);
  flexVecSetNoFill(&D, e);                    // solo contorno: tiene que salir fill="none"
  flexVecSetStroke(&D, e, 0x00AA00, 255, 1.0f);

  int n = flexVecExportSVG(&D, buf, sizeof(buf), 0);
  CHECK(n > 0, "exportar (devolvio %d)", n);
  CHECK((int)strlen(buf) == n, "la longitud devuelta es la real");
  CHECK(strstr(buf, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>") == buf, "declaracion XML");
  CHECK(strstr(buf, "xmlns=\"http://www.w3.org/2000/svg\"") != NULL, "espacio de nombres");
  CHECK(strstr(buf, "viewBox=\"0 0 480 800\"") != NULL, "viewBox con la mesa de trabajo");
  CHECK(countStr(buf, "<path") == 2, "dos trazados (%d)", countStr(buf, "<path"));
  CHECK(countStr(buf, "<g id=") == 2, "una capa por grupo");
  CHECK(countStr(buf, "<g ") == countStr(buf, "</g>"), "los grupos se cierran");
  CHECK(strstr(buf, "</svg>") != NULL, "cierre del documento");
  CHECK(strstr(buf, "fill=\"#ff8800\"") != NULL, "el relleno sale en hexadecimal");
  CHECK(strstr(buf, "stroke=\"#112233\"") != NULL, "el trazo tambien");
  CHECK(strstr(buf, "stroke-width=\"3\"") != NULL, "grosor sin decimales de relleno");
  CHECK(strstr(buf, "stroke-opacity=\"0.502\"") != NULL, "opacidad del trazo");
  CHECK(strstr(buf, "opacity=\"0.784\"") != NULL, "opacidad del objeto");
  CHECK(strstr(buf, "stroke-linecap=\"round\"") != NULL, "extremo redondo");
  CHECK(strstr(buf, "stroke-linejoin=\"round\"") != NULL, "union redonda");
  CHECK(strstr(buf, "fill=\"none\"") != NULL, "'sin relleno' se escribe como none");
  CHECK(strstr(buf, "id=\"Fondo\"") != NULL, "el nombre de la capa va al grupo");
  // Un rectangulo recto se escribe con rectas, no con cubicas: son tres
  // veces menos bytes y se lee igual.
  CHECK(strstr(buf, "M10 20 L110 20") != NULL, "las rectas salen como 'L': %.60s", strstr(buf, "M10 20"));
  CHECK(strstr(buf, " Z") != NULL, "el trazado cerrado lleva 'Z'");
  // La elipse SI tiene que llevar curvas.
  CHECK(countStr(buf, " C") >= 4, "la elipse se exporta con cubicas");

  // Solo la seleccion.
  flexVecSelectNone(&D);
  flexVecSelect(&D, e, 0);
  n = flexVecExportSVG(&D, buf, sizeof(buf), 1);
  CHECK(n > 0 && countStr(buf, "<path") == 1, "exportar solo la seleccion");

  // TRANSFORMACION: se exporta como matrix(...), no se hornea en los
  // puntos. Asi el SVG conserva la edicion no destructiva.
  flexVecNew(&D, 480, 800, 1);
  r = flexVecAddRect(&D, 0, 0, 10, 10, 0);
  flexVecTranslate(&D, r, 7, 9);
  n = flexVecExportSVG(&D, buf, sizeof(buf), 0);
  CHECK(n > 0 && strstr(buf, "transform=\"matrix(1,0,0,1,7,9)\"") != NULL,
        "la matriz se exporta tal cual");

  // Texto: se exporta como <text>, con su contenido escapado.
  flexVecNew(&D, 480, 800, 1);
  int t = flexVecAddText(&D, 10, 100, "Hola <Flex> & \"OS\"", 24);
  CHECK(t >= 0, "elemento de texto");
  CHECK(!strcmp(flexVecTextStr(&D, t), "Hola <Flex> & \"OS\""), "la cadena se guarda entera");
  CHECK(flexVecTextSize(&D, t) == 24.0f, "el tamano se guarda");
  n = flexVecExportSVG(&D, buf, sizeof(buf), 0);
  CHECK(n > 0 && strstr(buf, "<text") != NULL, "el texto se exporta como <text>");
  CHECK(strstr(buf, "Hola &lt;Flex&gt; &amp; &quot;OS&quot;") != NULL, "y va escapado");
  CHECK(strstr(buf, "font-size=\"24\"") != NULL, "con su tamano");
  CHECK(flexVecTextSetBox(&D, t, 120, 24) == FLEXVEC_OK, "el anfitrion corrige la caja medida");
  float bx0, by0, bx1, by1;
  flexVecBBox(&D, t, &bx0, &by0, &bx1, &by1);
  CHECK(fabsf(bx1 - bx0 - 120) < 0.01f, "la caja del texto es la medida (%.1f)", (double)(bx1 - bx0));

  // Un nombre de capa con '<' no puede romper el documento.
  flexVecNew(&D, 480, 800, 1);
  snprintf(D.layers[0].name, FLEXVEC_NAME_MAX, "a<b>&\"c\"");
  flexVecAddRect(&D, 0, 0, 10, 10, 0);
  n = flexVecExportSVG(&D, buf, sizeof(buf), 0);
  CHECK(n > 0 && strstr(buf, "a&lt;b&gt;&amp;&quot;c&quot;") != NULL, "el nombre de capa se escapa");

  // BUFFER CORTO: error explicito, nunca un SVG cortado que parece bueno.
  static char tiny[200];
  flexVecNew(&D, 480, 800, 1);
  for(int i = 0; i < 40; i++) flexVecAddRect(&D, (float)i, (float)i, 10, 10, 0);
  CHECK(flexVecExportSVG(&D, tiny, sizeof(tiny), 0) == -FLEXVEC_E_OVERFLOW,
        "un buffer corto devuelve desbordamiento");

  // La version compacta dice lo mismo con menos bytes.
  int full = flexVecExportSVG(&D, buf, sizeof(buf), 0);
  static char buf2[FLEXVEC_SVG_BYTES];
  int comp = flexVecExportSVGCompact(&D, buf2, sizeof(buf2), 0);
  CHECK(full > 0 && comp > 0 && comp < full, "el SVG compacto es mas corto (%d < %d)", comp, full);
  CHECK(countStr(buf2, "<path") == countStr(buf, "<path"), "y lleva los mismos objetos");
}

// =============================================================
// 10) SERIALIZACION NATIVA (.fxv)
//     Criterio: guardar y volver a abrir devuelve EL MISMO documento,
//     y un archivo estropeado se rechaza en vez de interpretarse.
// =============================================================
static void testSerialize(){
  std::printf("-- serializacion nativa y archivos estropeados --\n");
  flexVecNew(&D, 480, 800, 3);
  snprintf(D.name, FLEXVEC_NAME_MAX, "Mi documento");
  int r = flexVecAddRect(&D, 10, 20, 100, 50, 8);
  flexVecSetFill(&D, r, 0x336699, 200);
  flexVecSetStroke(&D, r, 0xFF0000, 255, 2.5f);
  flexVecTranslate(&D, r, 5, 5);
  flexVecLayerSetActive(&D, 1);
  int e = flexVecAddEllipse(&D, 200, 200, 40, 20);
  flexVecAddText(&D, 30, 300, "texto", 18);
  flexVecLayerSetVisible(&D, 2, 0);
  int g = flexVecGradAdd(&D, FLEXVEC_G_LINEAR);
  flexVecSetFillGrad(&D, e, g);
  uint32_t h0 = fingerprint(&D);

  size_t need = flexVecSerializeSize(&D);
  CHECK(need > 0, "tamano de serializacion");
  uint8_t* buf = (uint8_t*)malloc(need);
  int n = flexVecSerialize(&D, buf, need);
  CHECK(n > 0 && (size_t)n == need, "serializar: %d de %zu", n, need);
  CHECK(flexVecSerialize(&D, buf, need - 1) == -FLEXVEC_E_OVERFLOW, "buffer corto -> desbordamiento");

  flexVecNew(&D, 100, 100, 1);                 // se destruye el documento a proposito
  CHECK(flexVecDeserialize(&D, buf, (size_t)n) == FLEXVEC_OK, "deserializar");
  CHECK(fingerprint(&D) == h0, "el documento vuelve identico");
  CHECK(D.artW == 480 && D.artH == 800, "la mesa de trabajo vuelve");
  CHECK(!strcmp(D.name, "Mi documento"), "el nombre vuelve");
  CHECK(D.layers[2].visible == 0, "la visibilidad de la capa vuelve");
  CHECK(D.grads[g].used && D.grads[g].lutReady, "el gradiente vuelve, con su tabla rehecha");
  CHECK(D.dirty == 0, "un documento recien abierto no esta 'sin guardar'");
  CHECK(!strcmp(flexVecTextStr(&D, 2), "texto"), "el texto vuelve");

  std::printf("-- archivos estropeados: se rechazan, no se interpretan --\n");
  CHECK(flexVecDeserialize(&D, buf, 8) == FLEXVEC_E_BADARG, "archivo truncado");
  CHECK(flexVecDeserialize(&D, buf, (size_t)n - 40) == FLEXVEC_E_BADARG, "archivo cortado por la mitad");
  uint8_t* bad = (uint8_t*)malloc(need);
  memcpy(bad, buf, need);
  bad[0] ^= 0xFF;
  CHECK(flexVecDeserialize(&D, bad, need) == FLEXVEC_E_BADARG, "numero magico cambiado");
  memcpy(bad, buf, need);
  bad[4] = 99;                                  // version del formato
  CHECK(flexVecDeserialize(&D, bad, need) == FLEXVEC_E_BADARG, "otra version del formato");

  // Un bit cambiado EN LOS DATOS no puede dejar indices que apunten
  // fuera del pool: eso seria una lectura fuera de rango en el primer
  // repintado. Se acepta el archivo y se SANEAN los objetos imposibles.
  int survived = 0, crashed = 0;
  for(size_t off = 24; off < need; off += (need / 400) + 1){
    memcpy(bad, buf, need);
    bad[off] ^= 0xA5;
    int rc = flexVecDeserialize(&D, bad, need);
    if(rc == FLEXVEC_OK){
      survived++;
      // Recorrer el documento entero: con ASan, un indice malo salta aqui.
      for(int i = 0; i < FLEXVEC_MAX_ELEMS; i++){
        if(D.elems[i].layer < 0) continue;
        if((uint32_t)D.elems[i].first + D.elems[i].count > FLEXVEC_MAX_NODES){ crashed++; break; }
        if(D.elems[i].layer >= FLEXVEC_MAX_LAYERS){ crashed++; break; }
        float a, b2, c2, d2;
        flexVecBBox(&D, i, &a, &b2, &c2, &d2);
      }
      static char sv[FLEXVEC_SVG_BYTES];
      flexVecExportSVG(&D, sv, sizeof(sv), 0);
      FlexVecView v; viewIdentity(&v);
      int16_t ord[FLEXVEC_MAX_ELEMS];
      int cnt = flexVecPaintOrder(&D, ord, FLEXVEC_MAX_ELEMS);
      for(int i = 0; i < cnt; i++) covRun(ord[i], &v, 0);
    }
  }
  CHECK(crashed == 0, "ningun archivo estropeado deja indices fuera de rango (%d aceptados)", survived);
  free(bad);
  free(buf);
}

// =============================================================
// 11) LIMITES DUROS
//     Criterio: "el sistema no crashea al alcanzar los limites
//     definidos de nodos/objetos/capas/undo; muestra un error
//     controlado".
// =============================================================
static void testLimits(){
  std::printf("-- limites: se alcanzan de verdad y fallan con su motivo --\n");
  // OBJETOS.
  flexVecNew(&D, 480, 800, 1);
  int made = 0;
  while(1){
    int e = flexVecAddRect(&D, 0, 0, 4, 4, 0);
    if(e < 0){
      CHECK(e == -FLEXVEC_E_FULL_ELEMS || e == -FLEXVEC_E_FULL_NODES,
            "el objeto que no cabe dice por que (%d)", e);
      break;
    }
    made++;
    if(made > FLEXVEC_MAX_ELEMS + 8) break;
  }
  CHECK(made == FLEXVEC_MAX_ELEMS, "caben %d objetos (cupieron %d)", FLEXVEC_MAX_ELEMS, made);
  CHECK(flexVecElemCount(&D) == FLEXVEC_MAX_ELEMS, "y estan todos");
  // Con el documento lleno, todo lo demas sigue funcionando.
  float x0, y0, x1, y1;
  CHECK(flexVecBBox(&D, 0, &x0, &y0, &x1, &y1) == FLEXVEC_OK, "la caja sigue funcionando");
  CHECK(flexVecHitTest(&D, 2, 2, 1) >= 0, "el impacto sigue funcionando");
  static char sv[FLEXVEC_SVG_BYTES];
  CHECK(flexVecExportSVG(&D, sv, sizeof(sv), 0) > 0, "y se sigue pudiendo exportar");

  // NODOS POR TRAZADO.
  flexVecNew(&D, 480, 800, 1);
  int p = flexVecAddPath(&D);
  int nodes = 0;
  while(1){
    int rc = flexVecNodeAppend(&D, p, (float)(nodes % 400), (float)(nodes % 300), 0, 0, 0);
    if(rc != FLEXVEC_OK){
      CHECK(rc == FLEXVEC_E_TOO_COMPLEX || rc == FLEXVEC_E_FULL_NODES,
            "el ancla que no cabe dice por que (%d)", rc);
      break;
    }
    nodes++;
    if(nodes > FLEXVEC_MAX_NODES_PATH + 8) break;
  }
  CHECK(nodes == FLEXVEC_MAX_NODES_PATH, "caben %d anclas por trazado (cupieron %d)",
        FLEXVEC_MAX_NODES_PATH, nodes);
  // Un trazado en el limite se sigue pudiendo dibujar, exportar y tocar.
  FlexVecView v; viewIdentity(&v);
  Cov c = covRun(p, &v, 0);
  CHECK(c.outside == 0, "un trazado en el limite se rasteriza sin salirse");
  CHECK(flexVecExportSVG(&D, sv, sizeof(sv), 0) > 0, "y se exporta");

  // POOL GLOBAL DE NODOS. Se llena con trazados grandes; el motor
  // compacta cuando puede y, cuando de verdad no cabe, lo dice.
  flexVecNew(&D, 480, 800, 1);
  int paths = 0, denied = 0;
  for(int i = 0; i < FLEXVEC_MAX_ELEMS; i++){
    int e = flexVecAddPolygon(&D, 100, 100, 40, 30);   // 30 nodos cada uno
    if(e < 0){ denied = -e; break; }
    paths++;
  }
  CHECK(denied == FLEXVEC_E_FULL_NODES || paths == FLEXVEC_MAX_ELEMS,
        "el pool se agota con su motivo (creados %d, motivo %d)", paths, denied);
  CHECK(flexVecNodeUsed(&D) <= FLEXVEC_MAX_NODES, "el pool nunca se pasa de su tamano");

  // COMPACTACION: borrar por el medio y volver a crear tiene que
  // recuperar el hueco. Sin eso, el pool se degrada solo con editar.
  int freed = 0;
  for(int i = 0; i < FLEXVEC_MAX_ELEMS; i += 2)
    if(D.elems[i].layer >= 0 && flexVecDelete(&D, i) == FLEXVEC_OK) freed++;
  CHECK(freed > 0, "se borraron %d objetos por el medio", freed);
  int again = 0;
  for(int i = 0; i < freed; i++)
    if(flexVecAddPolygon(&D, 50, 50, 20, 30) >= 0) again++;
  CHECK(again >= freed / 2, "el hueco se recupera compactando (%d de %d)", again, freed);
  CHECK(flexVecNodeUsed(&D) <= FLEXVEC_MAX_NODES, "y el pool sigue dentro");
  // Todo lo que quedo vivo sigue siendo coherente despues de compactar.
  int bad = 0;
  for(int i = 0; i < FLEXVEC_MAX_ELEMS; i++){
    if(D.elems[i].layer < 0) continue;
    if((uint32_t)D.elems[i].first + D.elems[i].count > FLEXVEC_MAX_NODES) bad++;
  }
  CHECK(bad == 0, "ningun objeto quedo apuntando fuera tras compactar");

  // TEXTO.
  flexVecNew(&D, 480, 800, 1);
  int texts = 0, textDenied = 0;
  char big[200];
  memset(big, 'a', sizeof(big) - 1);
  big[sizeof(big) - 1] = 0;
  for(int i = 0; i < 200; i++){
    int e = flexVecAddText(&D, 0, 0, big, 12);
    if(e < 0){ textDenied = -e; break; }
    texts++;
  }
  CHECK(textDenied == FLEXVEC_E_FULL_TEXT || textDenied == FLEXVEC_E_FULL_ELEMS,
        "el pool de texto se agota con su motivo (%d tras %d)", textDenied, texts);

  // GRADIENTES.
  flexVecNew(&D, 480, 800, 1);
  int gr = 0;
  while(flexVecGradAdd(&D, FLEXVEC_G_LINEAR) >= 0) gr++;
  CHECK(gr == FLEXVEC_MAX_GRADS, "caben %d gradientes (cupieron %d)", FLEXVEC_MAX_GRADS, gr);
  CHECK(flexVecGradAdd(&D, FLEXVEC_G_LINEAR) == -FLEXVEC_E_FULL_GRADS, "el noveno se niega");

  // DIARIO DE DESHACER. Se llena con acciones caras (trazados enteros) y
  // se comprueba que ni se pasa del presupuesto ni deja el documento a
  // medias: eso es lo que de verdad importa cuando se queda sin sitio.
  flexVecNew(&D, 480, 800, 1);
  for(int i = 0; i < 400; i++){
    int e = flexVecAddPolygon(&D, 100, 100, 30, 12);
    if(e < 0){
      // Con el documento lleno se libera sitio para seguir probando el diario.
      for(int k = 0; k < FLEXVEC_MAX_ELEMS; k += 3) if(D.elems[k].layer >= 0) flexVecDelete(&D, k);
      continue;
    }
    flexVecTranslate(&D, e, 1, 1);
  }
  CHECK(flexVecUndoBytes(&D) <= FLEXVEC_UNDO_BYTES, "el diario nunca pasa de %u bytes (%u)",
        (unsigned)FLEXVEC_UNDO_BYTES, (unsigned)flexVecUndoBytes(&D));
  CHECK(flexVecUndoSteps(&D) <= FLEXVEC_UNDO_STEPS, "ni de %d pasos (%d)",
        FLEXVEC_UNDO_STEPS, flexVecUndoSteps(&D));
  // Y deshacer hasta el fondo no rompe nada.
  int undone = 0;
  while(flexVecCanUndo(&D) && undone < FLEXVEC_UNDO_STEPS + 10){ flexVecUndo(&D); undone++; }
  CHECK(undone > 0, "se pudieron deshacer %d pasos con el diario lleno", undone);
  int broken = 0;
  for(int i = 0; i < FLEXVEC_MAX_ELEMS; i++)
    if(D.elems[i].layer >= 0 && (uint32_t)D.elems[i].first + D.elems[i].count > FLEXVEC_MAX_NODES) broken++;
  CHECK(broken == 0, "el documento sigue coherente tras vaciar el diario");

  std::printf("-- indices imposibles: se rechazan, no se creen --\n");
  flexVecNew(&D, 480, 800, 1);
  CHECK(flexVecTranslate(&D, -1, 1, 1) == FLEXVEC_E_BADARG, "elemento -1");
  CHECK(flexVecTranslate(&D, 99999, 1, 1) == FLEXVEC_E_BADARG, "elemento fuera de rango");
  CHECK(flexVecDelete(&D, 5) == FLEXVEC_E_BADARG, "borrar un elemento que no existe");
  CHECK(flexVecLayerSetVisible(&D, 7, 1) == FLEXVEC_E_BADARG, "capa que no existe");
  CHECK(flexVecNodeMove(&D, 0, 0, 1, 1) == FLEXVEC_E_BADARG, "nodo de un elemento que no existe");
  int e2 = flexVecAddRect(&D, 0, 0, 10, 10, 0);
  CHECK(flexVecNodeMove(&D, e2, 99, 1, 1) == FLEXVEC_E_BADARG, "nodo fuera de rango");
  CHECK(flexVecNodeDelete(&D, e2, -1) == FLEXVEC_E_BADARG, "nodo negativo");
  CHECK(flexVecNodeSplit(&D, e2, 99, 0.5f) == FLEXVEC_E_BADARG, "segmento fuera de rango");
  CHECK(flexVecGradSetStop(&D, 0, 0, 0.5f, 0, 255) == FLEXVEC_E_BADARG, "gradiente que no existe");
  CHECK(flexVecErrText(FLEXVEC_E_FULL_NODES)[0] != 0, "todos los errores tienen texto");
  CHECK(flexVecErrText(-FLEXVEC_E_FULL_NODES)[0] != 0, "y tambien en negativo");
}

// =============================================================
// 12) GRADIENTES
// =============================================================
static void testGradients(){
  std::printf("-- gradientes --\n");
  flexVecNew(&D, 480, 800, 1);
  int g = flexVecGradAdd(&D, FLEXVEC_G_LINEAR);
  CHECK(g >= 0, "gradiente lineal");
  CHECK(D.grads[g].nstops == 2, "nace con dos paradas");
  CHECK(flexVecGradSetAxis(&D, g, 0, 0, 100, 0) == FLEXVEC_OK, "eje");
  CHECK(flexVecGradSetStop(&D, g, 0, 0.0f, 0x000000, 255) == FLEXVEC_OK, "parada 0 negra");
  CHECK(flexVecGradSetStop(&D, g, 1, 1.0f, 0xFFFFFF, 255) == FLEXVEC_OK, "parada 1 blanca");
  CHECK(D.grads[g].lut[0] == 0x000000u, "la tabla empieza en negro");
  CHECK(D.grads[g].lut[255] == 0xFFFFFFu, "y acaba en blanco");
  uint32_t mid = D.grads[g].lut[128];
  int mr = (int)((mid >> 16) & 0xFF);
  CHECK(mr > 100 && mr < 160, "el medio es gris (%d)", mr);

  CHECK(fabsf(flexVecGradAt(&D.grads[g], 0, 0)) < 0.01f, "t=0 en el origen del eje");
  CHECK(fabsf(flexVecGradAt(&D.grads[g], 100, 0) - 1.0f) < 0.01f, "t=1 al final");
  CHECK(fabsf(flexVecGradAt(&D.grads[g], 50, 0) - 0.5f) < 0.01f, "t=0,5 en el medio");
  CHECK(flexVecGradAt(&D.grads[g], -50, 0) == 0.0f, "por detras se acota a 0");
  CHECK(flexVecGradAt(&D.grads[g], 500, 0) == 1.0f, "por delante se acota a 1");
  float t0 = 0, dt = 0;
  CHECK(flexVecGradRow(&D.grads[g], 0, 0, &t0, &dt) == 1, "la fila de un lineal es afin");
  CHECK(fabsf(dt - 0.01f) < 1e-5f, "avanzar un pixel suma 1/100 (%.5f)", (double)dt);

  int gr = flexVecGradAdd(&D, FLEXVEC_G_RADIAL);
  flexVecGradSetAxis(&D, gr, 100, 100, 50, 0);
  CHECK(fabsf(flexVecGradAt(&D.grads[gr], 100, 100)) < 0.01f, "el centro del radial es t=0");
  CHECK(fabsf(flexVecGradAt(&D.grads[gr], 150, 100) - 1.0f) < 0.01f, "el radio es t=1");
  CHECK(flexVecGradRow(&D.grads[gr], 0, 0, &t0, &dt) == 0, "un radial NO es afin por fila");

  // Paradas desordenadas: se ordenan solas. Sin eso, la tabla saldria
  // con una franja de color plano donde deberia haber degradado.
  int g2 = flexVecGradAdd(&D, FLEXVEC_G_LINEAR);
  flexVecGradSetStop(&D, g2, 0, 0.9f, 0xFF0000, 255);
  flexVecGradSetStop(&D, g2, 1, 0.1f, 0x0000FF, 255);
  CHECK(D.grads[g2].stops[0].t <= D.grads[g2].stops[1].t, "las paradas quedan ordenadas");
  CHECK(flexVecGradAddStop(&D, g2, 0.5f, 0x00FF00, 255) == FLEXVEC_OK, "anadir parada");
  CHECK(D.grads[g2].nstops == 3, "tres paradas");
  while(flexVecGradAddStop(&D, g2, 0.3f, 0, 255) == FLEXVEC_OK) { }
  CHECK(D.grads[g2].nstops == FLEXVEC_MAX_STOPS, "el techo de paradas se respeta");
  while(flexVecGradDelStop(&D, g2, 0) == FLEXVEC_OK) { }
  CHECK(D.grads[g2].nstops == 2, "no se puede bajar de dos paradas");

  // Un objeto con relleno de gradiente se exporta con su <linearGradient>.
  int e = flexVecAddRect(&D, 0, 0, 100, 100, 0);
  CHECK(flexVecSetFillGrad(&D, e, g) == FLEXVEC_OK, "asignar el gradiente");
  static char sv[FLEXVEC_SVG_BYTES];
  int n = flexVecExportSVG(&D, sv, sizeof(sv), 0);
  CHECK(n > 0, "exportar con gradiente");
  CHECK(strstr(sv, "<linearGradient") != NULL, "el <defs> lleva el gradiente");
  CHECK(strstr(sv, "gradientUnits=\"userSpaceOnUse\"") != NULL, "en el espacio del objeto");
  CHECK(strstr(sv, "url(#g0)") != NULL, "y el relleno lo referencia");
  CHECK(strstr(sv, "<radialGradient") == NULL, "los gradientes sin usar no se exportan");
}

// =============================================================
// 13) PATHFINDER
// =============================================================
static void testPathfinder(){
  std::printf("-- pathfinder (booleanas) --\n");
  size_t bn = flexVecBoolBytes();
  void* blk = calloc(bn, 1);
  FlexVecBoolWork W;
  CHECK(flexVecBoolInit(&W, blk, bn) == FLEXVEC_OK, "reparto de los buffers");
  CHECK(flexVecBoolInit(&W, blk, bn - 1) == FLEXVEC_E_NOMEM, "un bloque corto se rechaza");
  FlexVecView v; viewIdentity(&v);

  // Dos cuadrados de 100x100 solapados 50: union 15000, interseccion 2500.
  #define SETUP2() do { \
      flexVecNew(&D, 480, 800, 1); \
      int a = flexVecAddRect(&D, 50, 50, 100, 100, 0); \
      int b = flexVecAddRect(&D, 100, 100, 100, 100, 0); \
      flexVecSetFill(&D, a, 0x336699, 255); \
      flexVecSelect(&D, a, 0); flexVecSelect(&D, b, 1); \
    } while(0)

  SETUP2();
  CHECK(flexVecPathfinder(&D, FLEXVEC_B_UNITE, &R, &W) == FLEXVEC_OK, "unir");
  CHECK(flexVecElemCount(&D) == 1, "queda un solo objeto");
  int res = flexVecSelFirst(&D);
  CHECK(res >= 0 && D.elems[res].fill.rgb == 0x336699u, "hereda la apariencia del de abajo");
  Cov c = covRun(res, &v, 0);
  CHECK(fabs(c.area - 17500.0) / 17500.0 < 0.05, "area de la union: %.0f (se esperaba ~17500)", c.area);

  SETUP2();
  CHECK(flexVecPathfinder(&D, FLEXVEC_B_INTERSECT, &R, &W) == FLEXVEC_OK, "intersecar");
  res = flexVecSelFirst(&D);
  c = covRun(res, &v, 0);
  CHECK(fabs(c.area - 2500.0) / 2500.0 < 0.10, "area de la interseccion: %.0f (se esperaba ~2500)", c.area);

  SETUP2();
  CHECK(flexVecPathfinder(&D, FLEXVEC_B_MINUS_FRONT, &R, &W) == FLEXVEC_OK, "menos frente");
  res = flexVecSelFirst(&D);
  c = covRun(res, &v, 0);
  CHECK(fabs(c.area - 7500.0) / 7500.0 < 0.08, "area de la resta: %.0f (se esperaba ~7500)", c.area);

  SETUP2();
  CHECK(flexVecPathfinder(&D, FLEXVEC_B_EXCLUDE, &R, &W) == FLEXVEC_OK, "excluir");
  res = flexVecSelFirst(&D);
  c = covRun(res, &v, 0);
  CHECK(fabs(c.area - 15000.0) / 15000.0 < 0.08, "area de la exclusion: %.0f (se esperaba ~15000)", c.area);
  // Excluir deja un HUECO, asi que el resultado tiene mas de un subtrazado.
  int subs = 0;
  for(int i = 0; i < (int)D.elems[res].count; i++)
    if(D.arena.nodes[D.elems[res].first + i].flags & FLEXVEC_N_START) subs++;
  CHECK(subs >= 1, "el resultado se compone de %d subtrazado(s)", subs);

  // Deshacer una booleana devuelve LOS DOS originales.
  SETUP2();
  uint32_t h0 = fingerprint(&D);
  flexVecPathfinder(&D, FLEXVEC_B_UNITE, &R, &W);
  CHECK(flexVecUndo(&D) == FLEXVEC_OK, "deshacer la booleana");
  CHECK(fingerprint(&D) == h0, "los dos originales vuelven, identicos");

  std::printf("-- pathfinder: casos que no se pueden resolver --\n");
  flexVecNew(&D, 480, 800, 1);
  int only = flexVecAddRect(&D, 0, 0, 10, 10, 0);
  flexVecSelect(&D, only, 0);
  CHECK(flexVecPathfinder(&D, FLEXVEC_B_UNITE, &R, &W) == FLEXVEC_E_EMPTY, "con un solo objeto no hay booleana");
  // Sin solape, intersecar da vacio: se dice y NO se borra nada.
  flexVecNew(&D, 480, 800, 1);
  int f1 = flexVecAddRect(&D, 0, 0, 20, 20, 0);
  int f2 = flexVecAddRect(&D, 300, 300, 20, 20, 0);
  flexVecSelect(&D, f1, 0); flexVecSelect(&D, f2, 1);
  CHECK(flexVecPathfinder(&D, FLEXVEC_B_INTERSECT, &R, &W) == FLEXVEC_E_EMPTY, "interseccion vacia");
  CHECK(flexVecElemCount(&D) == 2, "y los dos objetos siguen ahi");
  // Capa bloqueada: no se toca nada.
  flexVecLayerSetLocked(&D, 0, 1);
  flexVecSelect(&D, f1, 0);
  CHECK(flexVecPathfinder(&D, FLEXVEC_B_UNITE, &R, &W) == FLEXVEC_E_EMPTY ||
        flexVecPathfinder(&D, FLEXVEC_B_UNITE, &R, &W) == FLEXVEC_E_LOCKED, "capa bloqueada");
  CHECK(flexVecPathfinder(&D, 99, &R, &W) == FLEXVEC_E_BADARG, "operacion inventada");
  CHECK(flexVecPathfinder(&D, FLEXVEC_B_UNITE, &R, NULL) == FLEXVEC_E_NOMEM, "sin buffers");
  #undef SETUP2
  free(blk);
}

// =============================================================
// 14) EL ANFITRION NO ENTREGA MEMORIA
//     El motor no reserva nada: si los bloques no estan, se dice y el
//     sistema sigue vivo. Es el camino que recorre la app cuando la
//     PSRAM esta ocupada por otras aplicaciones.
// =============================================================
static void testNoMemory(){
  std::printf("-- sin los bloques del anfitrion --\n");
  FlexVecDoc d2;
  FlexVecArena a2; memset(&a2, 0, sizeof(a2));
  CHECK(flexVecAttach(&d2, &a2) == FLEXVEC_E_NOMEM, "sin bloques, attach lo dice");
  CHECK(flexVecNew(&d2, 480, 800, 3) == FLEXVEC_E_NOMEM, "y no se puede crear un documento");
  CHECK(flexVecAddRect(&d2, 0, 0, 10, 10, 0) < 0, "ni un objeto");
  CHECK(flexVecUndoBegin(&d2, "x") == FLEXVEC_E_NOMEM, "ni abrir una transaccion");
  CHECK(flexVecAttach(NULL, &a2) == FLEXVEC_E_BADARG, "attach con documento nulo");

  // Solo el pool de nodos, sin diario: se edita, y lo que no hay es
  // deshacer. Degradar asi es mejor que negarse a abrir.
  FlexVecArena a3;
  a3.nodes = g_nodes; a3.undo = NULL; a3.text = g_text;
  CHECK(flexVecAttach(&d2, &a3) == FLEXVEC_E_NOMEM, "falta el diario: se dice");

  // Rasterizador sin bloque.
  FlexVecRaster r2; memset(&r2, 0, sizeof(r2));
  CHECK(flexVecRasterInit(&r2, NULL, 0, 480) == FLEXVEC_E_BADARG, "rasterizador sin bloque");
  void* small = calloc(64, 1);
  CHECK(flexVecRasterInit(&r2, small, 64, 480) == FLEXVEC_E_NOMEM, "bloque demasiado pequeno");
  free(small);
  // Y con el documento bueno, un rasterizador vacio no dibuja ni revienta.
  flexVecNew(&D, 480, 800, 1);
  int e = flexVecAddRect(&D, 0, 0, 10, 10, 0);
  FlexVecView v; viewIdentity(&v);
  int subN = 0;
  CHECK(flexVecFlatten(&D, e, &v, &r2, &subN) < 0, "aplanar sin buffers se niega");
}

// =============================================================
//  F A S E   2
// =============================================================
static void testAppearance2(){
  std::printf("-- apariencia ampliada: dos rellenos y dos trazos --\n");
  flexVecNew(&D, 480, 800, 1);
  int e = flexVecAddRect(&D, 10, 10, 60, 40, 0);
  CHECK(!flexVecHasExtra(&D, e), "un objeto nuevo no tiene apariencia extra");
  CHECK(flexVecSetFill2(&D, e, 0x223344, 200) == FLEXVEC_OK, "relleno 2");
  CHECK(flexVecSetStroke2(&D, e, 0x998877, 255, 9.0f) == FLEXVEC_OK, "trazo 2");
  CHECK(flexVecHasExtra(&D, e), "ahora si la tiene");
  CHECK(D.elems[e].fill2.rgb == 0x223344u && D.elems[e].strokeW2 == 9.0f, "los valores quedan");
  static char sv[FLEXVEC_SVG_BYTES];
  int n = flexVecExportSVG(&D, sv, sizeof(sv), 0);
  CHECK(n > 0, "exportar");
  // SVG no admite dos rellenos en un elemento: el segundo par sale como
  // un <path> identico debajo.
  CHECK(countStr(sv, "<path") == 2, "dos trazados para un objeto (%d)", countStr(sv, "<path"));
  CHECK(strstr(sv, "fill=\"#223344\"") != NULL, "el relleno de abajo esta");
  CHECK(strstr(sv, "stroke-width=\"9\"") != NULL, "y el trazo de abajo tambien");
  // Serializacion: la apariencia extra viaja.
  size_t need = flexVecSerializeSize(&D);
  uint8_t* buf = (uint8_t*)malloc(need);
  int sn = flexVecSerialize(&D, buf, need);
  uint32_t h0 = fingerprint(&D);
  flexVecNew(&D, 100, 100, 1);
  CHECK(flexVecDeserialize(&D, buf, (size_t)sn) == FLEXVEC_OK, "reabrir");
  CHECK(fingerprint(&D) == h0, "la apariencia extra sobrevive al archivo");
  free(buf);
  CHECK(flexVecClearExtra(&D, e) == FLEXVEC_OK, "quitar la extra");
  CHECK(!flexVecHasExtra(&D, e), "queda quitada");
  CHECK(flexVecSetStroke2(&D, e, 0, 255, strtof("nan", NULL)) == FLEXVEC_E_BADARG, "grosor NaN");
}

static void testPatterns(){
  std::printf("-- motivos procedurales --\n");
  flexVecNew(&D, 480, 800, 1);
  int p = flexVecPatternAdd(&D, FLEXVEC_PAT_CHECKER);
  CHECK(p >= 0, "motivo de damero");
  CHECK(flexVecPatternSet(&D, p, FLEXVEC_PAT_CHECKER, 10, 0, 0, 0) == FLEXVEC_OK, "parametros");
  CHECK(flexVecPatternColors(&D, p, 0x000000, 255, 0xFFFFFF, 255) == FLEXVEC_OK, "colores");
  uint8_t a = 0;
  uint32_t c1 = flexVecPatternAt(&D.pats[p], 5, 5, &a);      // celda (0,0) -> primer plano
  uint32_t c2 = flexVecPatternAt(&D.pats[p], 15, 5, &a);     // celda (1,0) -> fondo
  uint32_t c3 = flexVecPatternAt(&D.pats[p], 15, 15, &a);    // celda (1,1) -> primer plano
  CHECK(c1 == 0x000000u && c2 == 0xFFFFFFu && c3 == 0x000000u,
        "el damero alterna: %06x %06x %06x", c1, c2, c3);
  // Coordenadas NEGATIVAS: el motivo no se puede partir en el eje. Es el
  // fallo clasico de usar un molde a int en vez de floorf.
  uint32_t cn = flexVecPatternAt(&D.pats[p], -5, -5, &a);
  uint32_t cp = flexVecPatternAt(&D.pats[p], -15, -5, &a);
  CHECK(cn != cp, "sigue alternando en el lado negativo");
  CHECK(flexVecPatternAt(&D.pats[p], -25, -5, &a) == cn, "y con el mismo periodo");
  // El desplazamiento mueve el mosaico entero.
  flexVecPatternSet(&D, p, FLEXVEC_PAT_CHECKER, 10, 10, 0, 0);
  CHECK(flexVecPatternAt(&D.pats[p], 15, 5, &a) == c1, "el desplazamiento corre el mosaico");
  // Los seis tipos responden y ninguno se sale de sus dos colores.
  for(int k = 0; k < FLEXVEC_PAT_N; k++){
    flexVecPatternSet(&D, p, k, 8, 0, 0, 0);
    int fg = 0, bg = 0;
    for(int i = 0; i < 64; i++)
      for(int j = 0; j < 64; j++){
        uint32_t c = flexVecPatternAt(&D.pats[p], (float)i * 0.5f, (float)j * 0.5f, &a);
        if(c == 0x000000u) fg++; else if(c == 0xFFFFFFu) bg++; else fg = -100000;
      }
    CHECK(fg > 0 && bg > 0, "el tipo %d tiene los dos colores (fg=%d bg=%d)", k, fg, bg);
  }
  flexVecPatternSet(&D, p, FLEXVEC_PAT_DOTS, 12, 0, 0, 0);
  int e = flexVecAddRect(&D, 0, 0, 100, 100, 0);
  CHECK(flexVecSetFillPattern(&D, e, p) == FLEXVEC_OK, "asignar el motivo");
  CHECK(D.elems[e].fill.type == FLEXVEC_P_PATTERN, "el relleno es de motivo");
  static char sv[FLEXVEC_SVG_BYTES];
  int n = flexVecExportSVG(&D, sv, sizeof(sv), 0);
  CHECK(n > 0 && strstr(sv, "<pattern id=\"p0\"") != NULL, "el <defs> lleva el <pattern>");
  CHECK(strstr(sv, "patternUnits=\"userSpaceOnUse\"") != NULL, "en el espacio del documento");
  CHECK(strstr(sv, "url(#p0)") != NULL, "y el relleno lo referencia");
  CHECK(strstr(sv, "<circle") != NULL, "el motivo de puntos exporta geometria de verdad");
  // Limites y argumentos.
  int made = 1;
  while(flexVecPatternAdd(&D, FLEXVEC_PAT_LINES) >= 0) made++;
  CHECK(made == FLEXVEC_MAX_PATTERNS, "caben %d motivos (cupieron %d)", FLEXVEC_MAX_PATTERNS, made);
  CHECK(flexVecPatternAdd(&D, 99) == -FLEXVEC_E_BADARG, "tipo invalido");
  CHECK(flexVecPatternSet(&D, 99, 0, 10, 0, 0, 0) == FLEXVEC_E_BADARG, "motivo que no existe");
  CHECK(flexVecPatternSet(&D, p, 0, strtof("nan", NULL), 0, 0, 0) == FLEXVEC_E_BADARG, "escala NaN");
  flexVecPatternSet(&D, p, FLEXVEC_PAT_DOTS, 0.001f, 0, 0, 0);
  CHECK(D.pats[p].scale >= 1.0f, "una celda diminuta se acota (%.3f)", (double)D.pats[p].scale);
}

static void testSymbols(){
  std::printf("-- simbolos: instancias VINCULADAS --\n");
  flexVecNew(&D, 480, 800, 1);
  int m = flexVecAddRect(&D, 0, 0, 40, 20, 0);
  int usedBefore = flexVecNodeUsed(&D);
  int i1 = flexVecSymbolInstance(&D, m, 100, 0);
  int i2 = flexVecSymbolInstance(&D, m, 200, 50);
  CHECK(i1 >= 0 && i2 >= 0, "dos instancias");
  CHECK(flexVecNodeUsed(&D) == usedBefore, "una instancia NO gasta nodos (%d)", flexVecNodeUsed(&D));
  CHECK(flexVecSymbolMaster(&D, i1) == m, "la instancia conoce a su maestro");
  CHECK(flexVecSymbolMaster(&D, m) == -1, "el maestro no es instancia de nadie");
  CHECK(flexVecSymbolCount(&D, m) == 2, "el maestro tiene dos instancias");

  float x0, y0, x1, y1;
  flexVecBBox(&D, i1, &x0, &y0, &x1, &y1);
  CHECK(fabsf(x0 - 100) < 0.01f && fabsf(x1 - 140) < 0.01f,
        "la caja de la instancia sigue al maestro: %.1f..%.1f", (double)x0, (double)x1);
  CHECK(flexVecHitTest(&D, 120, 10, 2) >= 0, "y se puede tocar donde se ve");

  // EDITAR EL MAESTRO CAMBIA LAS INSTANCIAS. Es todo el valor de un
  // simbolo, y lo unico que distingue una instancia de una copia.
  flexVecNodeMove(&D, m, 1, 80, 0);            // se estira el maestro
  D.elems[i1].bboxOk = 0;
  flexVecBBox(&D, i1, &x0, &y0, &x1, &y1);
  CHECK(fabsf(x1 - 180) < 0.01f, "la instancia se estiro con el maestro (%.1f)", (double)x1);

  // La instancia tiene su propia matriz y su propia apariencia.
  flexVecSetFill(&D, i1, 0xFF0000, 255);
  CHECK(D.elems[m].fill.rgb != 0xFF0000u, "cambiar la instancia no toca al maestro");
  flexVecRotate(&D, i1, 100, 0, 0.5f);
  CHECK(D.elems[m].m[0] == 1.0f, "girar la instancia no gira al maestro");

  // Instanciar una instancia instancia a SU maestro: la cadena nunca
  // pasa de un eslabon, asi que resolverla es O(1) y no puede ciclar.
  int i3 = flexVecSymbolInstance(&D, i1, 10, 10);
  CHECK(i3 >= 0 && flexVecSymbolMaster(&D, i3) == m, "una instancia de una instancia apunta al maestro");

  static char sv[FLEXVEC_SVG_BYTES];
  int n = flexVecExportSVG(&D, sv, sizeof(sv), 0);
  CHECK(n > 0 && countStr(sv, "<path") == 4, "las instancias se exportan expandidas (%d)",
        countStr(sv, "<path"));

  // BORRAR EL MAESTRO se lleva las instancias, en UN paso de deshacer.
  int before = flexVecElemCount(&D);
  CHECK(before == 4, "cuatro objetos antes de borrar (%d)", before);
  CHECK(flexVecDelete(&D, m) == FLEXVEC_OK, "borrar el maestro");
  CHECK(flexVecElemCount(&D) == 0, "se llevo las tres instancias (%d)", flexVecElemCount(&D));
  CHECK(flexVecUndo(&D) == FLEXVEC_OK, "deshacer");
  CHECK(flexVecElemCount(&D) == 4, "y vuelven las cuatro de una vez (%d)", flexVecElemCount(&D));

  CHECK(flexVecSymbolInstance(&D, 999, 0, 0) == -FLEXVEC_E_BADARG, "maestro que no existe");
  int t = flexVecAddText(&D, 0, 0, "hola", 12);
  CHECK(flexVecSymbolInstance(&D, t, 0, 0) == -FLEXVEC_E_BADARG, "el texto no se instancia");
}

static void testBlendRepeatBrush(){
  std::printf("-- fusion, repeticion y pincel --\n");
  flexVecNew(&D, 480, 800, 1);
  int a = flexVecAddRect(&D, 0, 0, 40, 40, 0);
  int b = flexVecAddEllipse(&D, 300, 200, 20, 20);
  flexVecSetFill(&D, a, 0xFF0000, 255);
  flexVecSetFill(&D, b, 0x0000FF, 255);
  int before = flexVecElemCount(&D);
  CHECK(flexVecBlend(&D, a, b, 5, &R) == FLEXVEC_OK, "fusion de 5 pasos");
  CHECK(flexVecElemCount(&D) == before + 5, "cinco objetos nuevos (%d)", flexVecElemCount(&D));
  // El paso central esta A MEDIO CAMINO en sitio y en color.
  int mid = -1;
  for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++){
    if(D.elems[e].layer < 0 || e == a || e == b) continue;
    float x0, y0, x1, y1;
    flexVecBBox(&D, e, &x0, &y0, &x1, &y1);
    float cx = (x0 + x1) * 0.5f;
    if(cx > 130 && cx < 175) mid = e;
  }
  CHECK(mid >= 0, "hay un paso a medio camino");
  if(mid >= 0){
    uint32_t c = D.elems[mid].fill.rgb;
    int r = (int)((c >> 16) & 0xFF), bl = (int)(c & 0xFF);
    CHECK(r > 60 && r < 200 && bl > 60 && bl < 200,
          "y su color esta interpolado (#%06x)", c);
  }
  CHECK(flexVecBlend(&D, a, a, 3, &R) == FLEXVEC_E_BADARG, "fusionar algo consigo mismo");
  CHECK(flexVecBlend(&D, a, 999, 3, &R) == FLEXVEC_E_BADARG, "objeto que no existe");
  // El techo de pasos se respeta aunque se pidan mil.
  flexVecNew(&D, 480, 800, 1);
  a = flexVecAddRect(&D, 0, 0, 20, 20, 0);
  b = flexVecAddRect(&D, 200, 0, 20, 20, 0);
  before = flexVecElemCount(&D);
  flexVecBlend(&D, a, b, 1000, &R);
  CHECK(flexVecElemCount(&D) - before <= FLEXVEC_MAX_BLEND_STEPS,
        "no se pasa de %d pasos (creo %d)", FLEXVEC_MAX_BLEND_STEPS, flexVecElemCount(&D) - before);

  std::printf("-- repetir --\n");
  flexVecNew(&D, 480, 800, 1);
  int e = flexVecAddRect(&D, 0, 0, 20, 20, 0);
  CHECK(flexVecRepeat(&D, e, FLEXVEC_REP_GRID, 3, 2, 30, 30) == FLEXVEC_OK, "rejilla 3x2");
  CHECK(flexVecElemCount(&D) == 6, "seis objetos (%d)", flexVecElemCount(&D));
  float bx0, by0, bx1, by1;
  flexVecSelectRect(&D, -10, -10, 500, 500, 0);
  flexVecSelBBox(&D, &bx0, &by0, &bx1, &by1);
  CHECK(fabsf(bx1 - 80) < 0.5f && fabsf(by1 - 50) < 0.5f,
        "la rejilla ocupa lo que debe: %.1f x %.1f", (double)bx1, (double)by1);
  CHECK(flexVecUndo(&D) == FLEXVEC_OK, "deshacer");
  CHECK(flexVecElemCount(&D) == 1, "la repeticion entera es UN paso (%d)", flexVecElemCount(&D));
  CHECK(flexVecRepeat(&D, e, FLEXVEC_REP_GRID, 40, 40, 5, 5) == FLEXVEC_E_TOO_COMPLEX,
        "1600 copias se niegan");
  CHECK(flexVecRepeat(&D, e, FLEXVEC_REP_GRID, 1, 1, 5, 5) == FLEXVEC_E_EMPTY, "1x1 no repite nada");
  CHECK(flexVecRepeat(&D, e, 99, 2, 2, 5, 5) == FLEXVEC_E_BADARG, "modo inventado");
  flexVecNew(&D, 480, 800, 1);
  e = flexVecAddRect(&D, 0, 0, 20, 20, 0);
  CHECK(flexVecRepeat(&D, e, FLEXVEC_REP_MIRROR, 2, 1, 20, 0) == FLEXVEC_OK, "espejo");
  CHECK(flexVecElemCount(&D) == 2, "dos objetos");

  std::printf("-- pincel: estampar a lo largo de un trazado --\n");
  flexVecNew(&D, 480, 800, 1);
  int path = flexVecAddLine(&D, 0, 100, 400, 100);
  int stamp = flexVecAddRect(&D, 0, 0, 8, 8, 0);
  before = flexVecElemCount(&D);
  CHECK(flexVecBrush(&D, path, stamp, 20.0f, 1.0f, 0, &R) == FLEXVEC_OK, "pincel");
  CHECK(flexVecElemCount(&D) == before + 1, "TODAS las estampas caben en UN objeto");
  int brush = -1;
  for(int i = 0; i < FLEXVEC_MAX_ELEMS; i++)
    if(D.elems[i].layer >= 0 && i != path && i != stamp) brush = i;
  CHECK(brush >= 0, "esta el objeto del pincel");
  if(brush >= 0){
    int subs = 0;
    for(int i = 0; i < (int)D.elems[brush].count; i++)
      if(D.arena.nodes[D.elems[brush].first + i].flags & FLEXVEC_N_START) subs++;
    CHECK(subs >= 15 && subs <= 21, "una estampa por subtrazado, ~21 (%d)", subs);
    flexVecBBox(&D, brush, &bx0, &by0, &bx1, &by1);
    CHECK(by0 > 90 && by1 < 110, "las estampas siguen al trazado (%.1f..%.1f)", (double)by0, (double)by1);
  }
  // Un espaciado diminuto no puede reventar el limite de nodos: se ABRE
  // el espaciado hasta que cabe, en vez de negarse.
  flexVecNew(&D, 480, 800, 1);
  path = flexVecAddLine(&D, 0, 100, 400, 100);
  stamp = flexVecAddRect(&D, 0, 0, 8, 8, 0);
  CHECK(flexVecBrush(&D, path, stamp, 0.01f, 1.0f, 1, &R) == FLEXVEC_OK, "espaciado diminuto");
  for(int i = 0; i < FLEXVEC_MAX_ELEMS; i++)
    if(D.elems[i].layer >= 0 && i != path && i != stamp)
      CHECK(D.elems[i].count <= FLEXVEC_MAX_NODES_PATH, "y sigue dentro del limite de nodos");
  CHECK(flexVecBrush(&D, path, path, 10, 1, 0, &R) == FLEXVEC_E_BADARG, "no se estampa consigo mismo");
  CHECK(flexVecBrush(&D, path, stamp, 10, -1, 0, &R) == FLEXVEC_E_BADARG, "escala negativa");
}

static void testDivideTrimClip(){
  std::printf("-- dividir, recortar y mascara de recorte --\n");
  size_t bn = flexVecBoolBytes();
  void* blk = calloc(bn, 1);
  FlexVecBoolWork W;
  CHECK(flexVecBoolInit(&W, blk, bn) == FLEXVEC_OK, "buffers");

  flexVecNew(&D, 480, 800, 1);
  int a = flexVecAddRect(&D, 50, 50, 100, 100, 0);
  int b = flexVecAddRect(&D, 100, 100, 100, 100, 0);
  flexVecSelect(&D, a, 0); flexVecSelect(&D, b, 1);
  CHECK(flexVecPathfinder(&D, FLEXVEC_B_DIVIDE, &R, &W) == FLEXVEC_OK, "dividir");
  CHECK(flexVecElemCount(&D) == 3, "tres regiones: comun y las dos propias (%d)", flexVecElemCount(&D));
  FlexVecView v; viewIdentity(&v);
  double tot = 0;
  for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++){
    if(D.elems[e].layer < 0) continue;
    Cov c = covRun(e, &v, 0);
    tot += c.area;
  }
  CHECK(fabs(tot - 17500.0) / 17500.0 < 0.06, "las tres suman la union: %.0f", tot);
  CHECK(flexVecUndo(&D) == FLEXVEC_OK, "deshacer dividir");
  CHECK(flexVecElemCount(&D) == 2, "vuelven las dos originales");

  flexVecNew(&D, 480, 800, 1);
  a = flexVecAddRect(&D, 50, 50, 100, 100, 0);
  b = flexVecAddRect(&D, 100, 100, 100, 100, 0);
  flexVecSelect(&D, a, 0); flexVecSelect(&D, b, 1);
  CHECK(flexVecPathfinder(&D, FLEXVEC_B_TRIM, &R, &W) == FLEXVEC_OK, "recortar");
  CHECK(flexVecElemCount(&D) == 2, "el de arriba se CONSERVA (%d)", flexVecElemCount(&D));
  CHECK(fvElemAlive(b), "y es exactamente el de arriba");
  // Tres figuras: dividir se niega, porque el numero de regiones explota.
  flexVecNew(&D, 480, 800, 1);
  for(int i = 0; i < 3; i++){
    int e = flexVecAddRect(&D, (float)(i * 30), 0, 60, 60, 0);
    flexVecSelect(&D, e, i > 0);
  }
  CHECK(flexVecPathfinder(&D, FLEXVEC_B_DIVIDE, &R, &W) == FLEXVEC_E_TOO_COMPLEX,
        "dividir tres figuras se niega con su motivo");

  std::printf("-- mascara de recorte de forma arbitraria --\n");
  flexVecNew(&D, 480, 800, 1);
  int o1 = flexVecAddRect(&D, 0, 0, 200, 60, 0);
  int o2 = flexVecAddRect(&D, 0, 80, 200, 60, 0);
  int mask = flexVecAddEllipse(&D, 100, 70, 90, 90);     // la de MAS ARRIBA
  flexVecSelect(&D, o1, 0); flexVecSelect(&D, o2, 1); flexVecSelect(&D, mask, 1);
  CHECK(flexVecClipWithTop(&D, &R, &W) == FLEXVEC_OK, "recortar con la forma superior");
  CHECK(flexVecElemCount(&D) == 2, "quedan los dos objetos, la mascara se consume (%d)",
        flexVecElemCount(&D));
  double areaAfter = 0;
  for(int e = 0; e < FLEXVEC_MAX_ELEMS; e++){
    if(D.elems[e].layer < 0) continue;
    Cov c = covRun(e, &v, 0);
    areaAfter += c.area;
    float x0, y0, x1, y1;
    flexVecBBox(&D, e, &x0, &y0, &x1, &y1);
    CHECK(x0 >= 5 && x1 <= 195, "lo recortado cabe en la mascara (%.1f..%.1f)", (double)x0, (double)x1);
  }
  CHECK(areaAfter < 24000.0 && areaAfter > 8000.0, "el area recortada es la comun (%.0f)", areaAfter);
  flexVecSelectNone(&D);
  CHECK(flexVecClipWithTop(&D, &R, &W) == FLEXVEC_E_EMPTY, "sin seleccion no hay recorte");
  CHECK(flexVecClipWithTop(&D, &R, NULL) == FLEXVEC_E_NOMEM, "sin buffers");
  free(blk);
}

// Medida de mentira: cada caracter mide 0,6 em. Con una medida
// PREDECIBLE se puede comprobar EXACTAMENTE donde tiene que cortar cada
// linea; con la fuente de verdad solo se podria comprobar "mas o menos".
static float fakeMeasure(const char* s, int len, float size, void* user){
  (void)s; (void)user;
  return (float)len * size * 0.6f;
}
typedef struct { char line[8][64]; int n; float x[8]; } LineBag;
static void bagLine(const char* s, int len, float x, float y, float w, void* user){
  LineBag* b = (LineBag*)user;
  (void)y; (void)w;
  if(b->n >= 8) return;
  if(len > 63) len = 63;
  memcpy(b->line[b->n], s, (size_t)len);
  b->line[b->n][len] = 0;
  b->x[b->n] = x;
  b->n++;
}
static void testAreaText(){
  std::printf("-- texto de area: reparto en lineas --\n");
  flexVecNew(&D, 480, 800, 1);
  // "uno dos tres cuatro": con 10 de cuerpo, cada caracter mide 6.
  int t = flexVecAddText(&D, 0, 0, "uno dos tres cuatro", 10);
  CHECK(t >= 0, "texto");
  CHECK(!flexVecTextIsArea(&D, t), "nace como texto puntual");
  CHECK(flexVecTextSetArea(&D, t, 60, 100, FLEXVEC_TA_LEFT) == FLEXVEC_OK, "convertir a area");
  CHECK(flexVecTextIsArea(&D, t), "ahora es de area");
  LineBag bag; bag.n = 0;
  int n = flexVecTextLayout(&D, t, 12.0f, fakeMeasure, NULL, bagLine, &bag);
  CHECK(n == bag.n && n >= 2, "se repartio en %d lineas", n);
  // 60 px / 6 px por caracter = 10 caracteres por linea.
  CHECK(!strcmp(bag.line[0], "uno dos"), "primera linea: \"%s\"", bag.line[0]);
  CHECK(!strcmp(bag.line[1], "tres"), "segunda linea: \"%s\"", bag.line[1]);
  CHECK(!strcmp(bag.line[2], "cuatro"), "tercera linea: \"%s\"", bag.line[2]);
  // Centrado y derecha mueven la x de cada linea.
  flexVecTextSetArea(&D, t, 60, 100, FLEXVEC_TA_CENTER);
  LineBag c; c.n = 0;
  flexVecTextLayout(&D, t, 12.0f, fakeMeasure, NULL, bagLine, &c);
  CHECK(c.x[0] > bag.x[0], "centrado corre la linea a la derecha (%.1f > %.1f)",
        (double)c.x[0], (double)bag.x[0]);
  flexVecTextSetArea(&D, t, 60, 100, FLEXVEC_TA_RIGHT);
  LineBag rg; rg.n = 0;
  flexVecTextLayout(&D, t, 12.0f, fakeMeasure, NULL, bagLine, &rg);
  CHECK(rg.x[0] > c.x[0], "alineado a la derecha, mas todavia");
  // La caja MANDA: si solo caben dos lineas, solo se pintan dos.
  flexVecTextSetArea(&D, t, 60, 26, FLEXVEC_TA_LEFT);
  LineBag two; two.n = 0;
  int n2 = flexVecTextLayout(&D, t, 12.0f, fakeMeasure, NULL, bagLine, &two);
  CHECK(n2 == 2, "en una caja de 26 px caben dos lineas (%d)", n2);
  // Saltos de linea explicitos.
  flexVecNew(&D, 480, 800, 1);
  t = flexVecAddText(&D, 0, 0, "a\nb\nc", 10);
  flexVecTextSetArea(&D, t, 200, 100, FLEXVEC_TA_LEFT);
  LineBag hard; hard.n = 0;
  CHECK(flexVecTextLayout(&D, t, 12.0f, fakeMeasure, NULL, bagLine, &hard) == 3,
        "los saltos explicitos cortan linea");
  // Una palabra mas ancha que la caja se coloca igual: partirla sin
  // reglas de guionizacion se lee peor que el desbordamiento.
  flexVecNew(&D, 480, 800, 1);
  t = flexVecAddText(&D, 0, 0, "palabralarguisima", 10);
  flexVecTextSetArea(&D, t, 20, 100, FLEXVEC_TA_LEFT);
  LineBag big; big.n = 0;
  CHECK(flexVecTextLayout(&D, t, 12.0f, fakeMeasure, NULL, bagLine, &big) == 1,
        "una palabra que no cabe se coloca entera");
  CHECK(flexVecTextLayout(&D, t, 12.0f, NULL, NULL, bagLine, &big) < 0, "sin medida no hay reparto");
  CHECK(flexVecTextSetArea(&D, t, 0, 0, FLEXVEC_TA_LEFT) == FLEXVEC_E_BADARG, "caja de cero");
  CHECK(flexVecTextSetArea(&D, t, 10, 10, 99) == FLEXVEC_E_BADARG, "alineacion inventada");

  // Y en el SVG: con medida salen <tspan>; sin medida, una sola linea.
  flexVecNew(&D, 480, 800, 1);
  t = flexVecAddText(&D, 10, 10, "uno dos tres cuatro", 10);
  flexVecTextSetArea(&D, t, 60, 100, FLEXVEC_TA_LEFT);
  static char sv[FLEXVEC_SVG_BYTES];
  int len = flexVecExportSVGEx(&D, sv, sizeof(sv), 0, 0, fakeMeasure, NULL);
  CHECK(len > 0 && countStr(sv, "<tspan") == 3, "el SVG lleva las mismas 3 lineas (%d)",
        countStr(sv, "<tspan"));
  len = flexVecExportSVG(&D, sv, sizeof(sv), 0);
  CHECK(len > 0 && countStr(sv, "<tspan") == 0, "sin medida, una sola linea");
}

static void testPng(){
  std::printf("-- exportacion PNG (deflate real) --\n");
  const int W = 64, H = 48;
  static uint8_t row[64 * 3];
  size_t cap = flexVecPngMaxBytes(W, H);
  uint8_t* out = (uint8_t*)malloc(cap);
  FlexVecPng* png = (FlexVecPng*)malloc(sizeof(FlexVecPng));
  CHECK(flexVecPngBegin(png, W, H, out, cap) == FLEXVEC_OK, "cabecera");
  // Una imagen como la que produce el editor: zonas planas y un borde.
  static uint8_t src[64 * 48 * 3];
  for(int y = 0; y < H; y++)
    for(int x = 0; x < W; x++){
      int inside = (x > 8 && x < 40 && y > 6 && y < 30);
      uint8_t* p = src + ((size_t)y * W + x) * 3;
      p[0] = inside ? 0x3C : 0xFF;
      p[1] = inside ? 0x6E : 0xFF;
      p[2] = inside ? 0xF0 : 0xFF;
    }
  for(int y = 0; y < H; y++){
    memcpy(row, src + (size_t)y * W * 3, (size_t)W * 3);
    CHECK(flexVecPngRow(png, row) == FLEXVEC_OK, "fila %d", y);
  }
  int total = flexVecPngEnd(png);
  CHECK(total > 0, "cerrar: %d bytes", total);
  CHECK((size_t)total < (size_t)W * H * 3 / 2, "comprime de verdad (%d de %d crudos)",
        total, W * H * 3);
  // Firma y trozos obligatorios.
  CHECK(out[0] == 0x89 && !memcmp(out + 1, "PNG", 3), "firma PNG");
  CHECK(!memcmp(out + 12, "IHDR", 4), "IHDR");
  CHECK(memmem(out, (size_t)total, "IDAT", 4) != NULL, "IDAT");
  CHECK(!memcmp(out + total - 8, "IEND", 4), "IEND al final");

  // LA PRUEBA DE VERDAD: se descomprime con zlib y se comparan los
  // pixeles. Un PNG con la firma correcta y el flujo mal se ve bien en
  // un visor tolerante y no se abre en el siguiente; esto no perdona.
  const uint8_t* idat = (const uint8_t*)memmem(out, (size_t)total, "IDAT", 4);
  CHECK(idat != NULL, "se localiza el IDAT");
  if(idat){
    uint32_t ilen = ((uint32_t)idat[-4] << 24) | ((uint32_t)idat[-3] << 16) |
                    ((uint32_t)idat[-2] << 8) | (uint32_t)idat[-1];
    uLongf dstLen = (uLongf)((size_t)H * ((size_t)W * 3 + 1)) + 64;
    uint8_t* dst = (uint8_t*)malloc(dstLen);
    int zr = uncompress(dst, &dstLen, idat + 4, (uLong)ilen);
    CHECK(zr == Z_OK, "zlib descomprime el flujo (rc=%d)", zr);
    CHECK(dstLen == (uLongf)((size_t)H * ((size_t)W * 3 + 1)),
          "sale el numero exacto de bytes (%lu)", (unsigned long)dstLen);
    if(zr == Z_OK){
      int bad = 0;
      for(int y = 0; y < H && !bad; y++){
        const uint8_t* r = dst + (size_t)y * (W * 3 + 1);
        if(r[0] != 1){ bad = 1; break; }                 // filtro Sub
        uint8_t prev[3] = { 0, 0, 0 };
        for(int x = 0; x < W; x++){
          for(int c = 0; c < 3; c++){
            uint8_t val = (uint8_t)(r[1 + x * 3 + c] + (x ? prev[c] : 0));
            if(val != src[((size_t)y * W + x) * 3 + c]){ bad = 1; }
            prev[c] = val;
          }
          if(bad) break;
        }
      }
      CHECK(!bad, "los pixeles vuelven EXACTAMENTE iguales");
    }
    free(dst);
  }
  // Buffer corto: se dice, no se escribe fuera. Con menos de lo minimo
  // ni siquiera se empieza; con sitio para la cabecera pero no para los
  // datos, el fallo aparece al escribir y se arrastra hasta el cierre.
  CHECK(flexVecPngBegin(png, W, H, out, 64) == FLEXVEC_E_NOMEM,
        "un buffer por debajo del minimo se rechaza de entrada");
  {
    uint8_t tiny[140];
    FlexVecPng* p2 = (FlexVecPng*)malloc(sizeof(FlexVecPng));
    flexVecPngBegin(p2, W, H, tiny, sizeof(tiny));
    int over = 0;
    for(int y = 0; y < H; y++){
      memcpy(row, src + (size_t)y * W * 3, (size_t)W * 3);
      if(flexVecPngRow(p2, row) != FLEXVEC_OK) over = 1;
    }
    CHECK(over, "un buffer corto se detecta al escribir");
    CHECK(flexVecPngEnd(p2) < 0, "y cerrar devuelve error");
    free(p2);
  }
  CHECK(flexVecPngBegin(png, 0, 10, out, cap) == FLEXVEC_E_BADARG, "ancho cero");
  CHECK(flexVecPngBegin(NULL, 10, 10, out, cap) == FLEXVEC_E_BADARG, "sin estado");
  free(png);
  free(out);
}

// =============================================================
//  PRESUPUESTO DE RENDER
//  ------------------------------------------------------------
//  No mide milisegundos: un PC no dice nada sobre lo que tarda un
//  ESP32-P4. Mide TRABAJO -- cuantos tramos y cuantos pixeles emite el
//  rasterizador -- que es lo que si se traslada, porque el coste en la
//  placa es proporcional a eso.
//
//  Sirve de RED DE SEGURIDAD contra regresiones: si manana alguien
//  cambia el aplanado o el antialias y el numero de tramos se dispara,
//  esta prueba lo dice aqui, y no un usuario notando que arrastrar va a
//  tirones. Los topes salen del presupuesto de 60 FPS: 16,6 ms por
//  cuadro en una CPU de 400 MHz sin GPU.
// =============================================================
typedef struct { long spans; double px; } Budget;
static void budgetSpan(int y, int x0, int x1, uint8_t cov, void* user){
  Budget* b = (Budget*)user;
  (void)y; (void)cov;
  b->spans++;
  b->px += (double)(x1 - x0 + 1);
}
static void testRenderBudget(){
  std::printf("-- presupuesto de render (trabajo, no milisegundos) --\n");
  FlexVecView v; viewIdentity(&v);

  // Un circulo a pantalla completa: es la figura mas cara de las
  // simples, porque cada fila cambia de anchura.
  flexVecNew(&D, 480, 800, 1);
  int e = flexVecAddEllipse(&D, 240, 400, 235, 235);
  Budget b; memset(&b, 0, sizeof(b));
  int subN = 0;
  flexVecFlatten(&D, e, &v, &R, &subN);
  flexVecFillFlat(&R, subN, &v, 0, budgetSpan, &b);
  double perSpan = b.px / (double)(b.spans ? b.spans : 1);
  std::printf("   circulo de 470 px: %ld tramos, %.0f pixeles, %.0f px por tramo\n",
              b.spans, b.px, perSpan);
  // LO QUE IMPORTA ES LA RELACION, no el numero absoluto. Un relleno
  // macizo emite por fila: el interior de una pasada y los dos pixeles
  // del borde a su cobertura exacta (el antialias). Eso son ~4 tramos
  // por fila y ~90 pixeles por tramo. Si alguna vez bajara a ~1 pixel
  // por tramo, seria que el rasterizador ha vuelto a emitir pixel a
  // pixel -- y eso multiplica por cien las llamadas a hLineA.
  CHECK(perSpan > 40.0, "el relleno emite TRAMOS largos, no pixeles (%.0f px/tramo)", perSpan);
  CHECK(b.spans < 470 * 6, "unos pocos tramos por fila (%ld en 470 filas)", b.spans);
  CHECK(b.px > 160000.0, "y cubre el area del circulo (%.0f)", b.px);

  // El APLANADO sigue al zoom: alejar tiene que costar MENOS, no lo
  // mismo. Es lo que hace que ver el documento entero sea barato.
  flexVecNew(&D, 480, 800, 1);
  e = flexVecAddEllipse(&D, 100, 100, 90, 90);
  FlexVecView far_ = v; far_.m[0] = far_.m[3] = 0.2f;
  int nNear = flexVecFlatten(&D, e, &v, &R, &subN);
  int nFar  = flexVecFlatten(&D, e, &far_, &R, &subN);
  std::printf("   aplanado: %d puntos de cerca, %d de lejos\n", nNear, nFar);
  CHECK(nFar < nNear, "alejar cuesta menos (%d < %d)", nFar, nNear);
  CHECK(nNear <= FLEXVEC_MAX_FLATPTS, "y nunca se pasa del buffer");

  // Un documento LLENO: el peor caso realista de un repintado completo.
  flexVecNew(&D, 480, 800, 3);
  for(int i = 0; i < 120; i++){
    int q = flexVecAddEllipse(&D, (float)(20 + (i % 12) * 38), (float)(20 + (i / 12) * 60), 16, 16);
    if(q < 0) break;
    flexVecSetStroke(&D, q, 0, 255, 2);
  }
  Budget tot; memset(&tot, 0, sizeof(tot));
  int16_t order[FLEXVEC_MAX_ELEMS];
  int n = flexVecPaintOrder(&D, order, FLEXVEC_MAX_ELEMS);
  for(int i = 0; i < n; i++){
    int sn = 0;
    if(flexVecFlatten(&D, order[i], &v, &R, &sn) <= 0) continue;
    flexVecFillFlat(&R, sn, &v, 0, budgetSpan, &tot);
    flexVecStrokeFlat(&R, sn, &v, 2.0f, FLEXVEC_CAP_BUTT, FLEXVEC_JOIN_MITER, budgetSpan, &tot);
  }
  std::printf("   120 circulos con trazo: %ld tramos, %.0f pixeles\n", tot.spans, tot.px);
  // Este es el peor caso de un repintado COMPLETO, y solo ocurre cuando
  // el documento cambia -- no en cada cuadro: para eso esta la cache.
  // El tope deja mas del 50% de margen sobre lo medido hoy, para que la
  // prueba avise de una regresion de verdad y no de un cambio menor.
  CHECK(tot.spans < 80000, "un repintado completo cabe en el presupuesto (%ld tramos)", tot.spans);
  CHECK(tot.px < 400000.0, "y no se pinta el mismo pixel diez veces (%.0f)", tot.px);

  // LA BANDA SUCIA ES LO QUE SOSTIENE LA FLUIDEZ. Mover un objeto solo
  // puede costar SU banda, no la pantalla. Se comprueba con la caja en
  // pixeles, que es lo que el anfitrion usa para decidir que repinta.
  flexVecNew(&D, 480, 800, 1);
  e = flexVecAddRect(&D, 100, 300, 40, 30, 0);
  int x0, y0, x1, y1;
  flexVecElemPixBBox(&D, e, &v, &x0, &y0, &x1, &y1);
  int band = y1 - y0 + 1;
  CHECK(band < 60, "la banda de un objeto pequeno es pequena (%d filas de 800)", band);
  std::printf("   objeto de 40x30: banda sucia de %d filas\n", band);
}

int main(){
  std::printf("=== FlexOS · Flex Vector Pro: motor vectorial ===\n");
  hostInit();
  testLayers();
  testPen();
  testShapes();
  testTransforms();
  testSelection();
  testUndo();
  testRaster();
  testSnap();
  testSVG();
  testSerialize();
  testLimits();
  testGradients();
  testPathfinder();
  testNoMemory();
  testAppearance2();
  testPatterns();
  testSymbols();
  testBlendRepeatBrush();
  testDivideTrimClip();
  testAreaText();
  testPng();
  testRenderBudget();
  hostFree();
  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
