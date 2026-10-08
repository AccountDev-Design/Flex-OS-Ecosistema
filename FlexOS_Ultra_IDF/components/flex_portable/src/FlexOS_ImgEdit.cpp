// #############################################################
// ##  FlexOS · EDITOR DE IMAGENES · implementacion
// ##  Ver FlexOS_ImgEdit.h. Todo en coma fija o flotante simple, sin
// ##  reservas: la memoria la pone quien llama.
// #############################################################
#include "FlexOS_ImgEdit.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

static inline float clampf(float v, float a, float b){ return v < a ? a : (v > b ? b : v); }
static inline int   clampi(int v, int a, int b){ return v < a ? a : (v > b ? b : v); }
static inline uint8_t u8(int v){ return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

static void stIdentity(FlexIeState* s){
  memset(s, 0, sizeof(*s));
  s->c1x = s->c1y = 1.0f;
  s->filterAmt = 100;
}

void flexIeInit(FlexImgEdit* e, const uint8_t* base, int W, int H){
  memset(e, 0, sizeof(*e));
  e->base = base; e->W = W; e->H = H;
  stIdentity(&e->st);
  e->hist[0] = e->st;
  e->cur = e->top = 0;
  e->colorKey = 0xFFFFFFFFu;
}
void flexIeSetBase(FlexImgEdit* e, const uint8_t* base, int W, int H){
  bool ok = base && W > 0 && H > 0;
  e->base = ok ? base : NULL; e->W = ok ? W : 0; e->H = ok ? H : 0;
  e->proxy = NULL; e->PW = e->PH = 0;              // la copia reducida era de la base anterior
  e->colorKey = 0xFFFFFFFFu;
}
void flexIeSetTextFn(FlexImgEdit* e, FlexIeTextFn fn, void* ctx){ e->textFn = fn; e->textCtx = ctx; }

void flexIeMakeProxy(const uint8_t* base, int W, int H, uint8_t* dst, int pw, int ph){
  if(!base || !dst || W <= 0 || H <= 0 || pw <= 0 || ph <= 0) return;
  for(int y = 0; y < ph; y++){
    int sy0 = (int)((int64_t)y * H / ph), sy1 = (int)((int64_t)(y + 1) * H / ph);
    if(sy1 <= sy0) sy1 = sy0 + 1;
    if(sy1 > H) sy1 = H;
    for(int x = 0; x < pw; x++){
      int sx0 = (int)((int64_t)x * W / pw), sx1 = (int)((int64_t)(x + 1) * W / pw);
      if(sx1 <= sx0) sx1 = sx0 + 1;
      if(sx1 > W) sx1 = W;
      uint32_t a0 = 0, a1 = 0, a2 = 0, n = (uint32_t)((sx1 - sx0) * (sy1 - sy0));
      for(int sy = sy0; sy < sy1; sy++){
        const uint8_t* p = base + ((size_t)sy * W + sx0) * 3;
        for(int sx = sx0; sx < sx1; sx++, p += 3){ a0 += p[0]; a1 += p[1]; a2 += p[2]; }
      }
      uint8_t* d = dst + ((size_t)y * pw + x) * 3;
      d[0] = (uint8_t)((a0 + n / 2) / n); d[1] = (uint8_t)((a1 + n / 2) / n); d[2] = (uint8_t)((a2 + n / 2) / n);
    }
  }
}
void flexIeSetProxy(FlexImgEdit* e, const uint8_t* proxy, int pw, int ph){
  bool ok = proxy && pw > 0 && ph > 0;
  e->proxy = ok ? proxy : NULL; e->PW = ok ? pw : 0; e->PH = ok ? ph : 0;
}

int flexIeDecodeScale(int w, int h, uint32_t budgetBytes){
  if(w <= 0 || h <= 0) return 0;
  for(int s = 1; s <= 8; s <<= 1){
    uint64_t ow = (uint64_t)(w + s - 1) / s, oh = (uint64_t)(h + s - 1) / s;
    uint64_t px = ow * oh;
    if(px <= FLEXIE_BASE_MAX_PX && px * 3u <= budgetBytes) return s;
  }
  return 0;
}

// -------------------------------------------------------------
//  HISTORIAL
// -------------------------------------------------------------
void flexIeCommit(FlexImgEdit* e){
  if(e->cur == FLEXIE_HIST_MAX - 1){                 // lleno: se olvida el paso mas viejo
    memmove(&e->hist[0], &e->hist[1], sizeof(FlexIeState) * (FLEXIE_HIST_MAX - 1));
    e->cur--;
  }
  e->hist[++e->cur] = e->st;
  e->top = e->cur;                                   // lo que se podia rehacer ya no vale
}
bool flexIeCanUndo(const FlexImgEdit* e){ return e->cur > 0; }
bool flexIeCanRedo(const FlexImgEdit* e){ return e->cur < e->top; }
bool flexIeUndo(FlexImgEdit* e){
  if(e->cur <= 0) return false;
  e->st = e->hist[--e->cur];
  return true;
}
bool flexIeRedo(FlexImgEdit* e){
  if(e->cur >= e->top) return false;
  e->st = e->hist[++e->cur];
  return true;
}
// Restablecer esconde los trazos, formas y textos (ovStart) en vez de vaciar
// las tablas: los estados de antes siguen apuntando a ellos, y deshacer los
// recupera. Las tablas solo se reescriben por el final (ovNew).
void flexIeReset(FlexImgEdit* e){
  uint16_t nOv = e->st.nOv, nPts = e->st.nPts;
  stIdentity(&e->st);
  e->st.nOv = nOv; e->st.nPts = nPts; e->st.ovStart = nOv;
  flexIeCommit(e);
}
bool flexIeIsIdentity(const FlexImgEdit* e){
  const FlexIeState* s = &e->st;
  if(s->rot || s->flipH || s->flipV || s->longSide || s->nOv > s->ovStart) return false;
  if(s->filter != FLEXIE_FILTER_NONE && s->filterAmt) return false;
  for(int i = 0; i < FLEXIE_ADJ_N; i++) if(s->adj[i]) return false;
  return s->c0x == 0.0f && s->c0y == 0.0f && s->c1x == 1.0f && s->c1y == 1.0f;
}

// -------------------------------------------------------------
//  GEOMETRIA: base -> (giro, volteo) -> orientada -> recorte -> salida
// -------------------------------------------------------------
static void orientToBase(const FlexIeState* st, float ox, float oy, float* s, float* t){
  if(st->flipH) ox = 1.0f - ox;
  if(st->flipV) oy = 1.0f - oy;
  switch(st->rot & 3){
    case 0:  *s = ox;        *t = oy;        break;
    case 1:  *s = oy;        *t = 1.0f - ox; break;
    case 2:  *s = 1.0f - ox; *t = 1.0f - oy; break;
    default: *s = 1.0f - oy; *t = ox;        break;
  }
}
static void baseToOrient(const FlexIeState* st, float s, float t, float* ox, float* oy){
  float x, y;
  switch(st->rot & 3){
    case 0:  x = s;        y = t;        break;
    case 1:  x = 1.0f - t; y = s;        break;
    case 2:  x = 1.0f - s; y = 1.0f - t; break;
    default: x = t;        y = 1.0f - s; break;
  }
  if(st->flipH) x = 1.0f - x;
  if(st->flipV) y = 1.0f - y;
  *ox = x; *oy = y;
}
static void orientDims(const FlexImgEdit* e, const FlexIeState* s, int* w, int* h){
  int W = e->W > 0 ? e->W : 1, H = e->H > 0 ? e->H : 1;   // sin base: 1x1, nunca una division por cero
  if(s->rot & 1){ *w = H; *h = W; } else { *w = W; *h = H; }
}
static void cropPx(const FlexImgEdit* e, const FlexIeState* s, float* cw, float* ch){
  int ow, oh; orientDims(e, s, &ow, &oh);
  *cw = (s->c1x - s->c0x) * ow; *ch = (s->c1y - s->c0y) * oh;
}
static void outSizeOf(const FlexImgEdit* e, const FlexIeState* s, int* w, int* h){
  float cw, ch; cropPx(e, s, &cw, &ch);
  float lng = cw > ch ? cw : ch;
  float k = (s->longSide && s->longSide < lng) ? s->longSide / lng : 1.0f;
  int ow = (int)(cw * k + 0.5f), oh = (int)(ch * k + 0.5f);
  *w = ow < 1 ? 1 : ow; *h = oh < 1 ? 1 : oh;
}
void flexIeOutSize(const FlexImgEdit* e, int* w, int* h){ outSizeOf(e, &e->st, w, h); }
void flexIeFitSize(const FlexImgEdit* e, int maxW, int maxH, int* w, int* h){
  float cw, ch; cropPx(e, &e->st, &cw, &ch);
  float k = maxW / cw; if(ch * k > maxH) k = maxH / ch;
  int ow = (int)(cw * k + 0.5f), oh = (int)(ch * k + 0.5f);
  *w = ow < 1 ? 1 : ow; *h = oh < 1 ? 1 : oh;
}
void flexIeOutToBase(const FlexImgEdit* e, float u, float v, float* s, float* t){
  const FlexIeState* st = &e->st;
  orientToBase(st, st->c0x + u * (st->c1x - st->c0x), st->c0y + v * (st->c1y - st->c0y), s, t);
}
void flexIeBaseToOut(const FlexImgEdit* e, float s, float t, float* u, float* v){
  const FlexIeState* st = &e->st;
  float ox, oy; baseToOrient(st, s, t, &ox, &oy);
  *u = (ox - st->c0x) / (st->c1x - st->c0x);
  *v = (oy - st->c0y) / (st->c1y - st->c0y);
}

// Un giro o un volteo cambia la orientacion: el recorte se lleva con el
// contenido (mismas esquinas en la base, nuevas coordenadas orientadas).
static void reorient(FlexImgEdit* e, uint8_t rot, uint8_t fh, uint8_t fv){
  FlexIeState* st = &e->st;
  float s0, t0, s1, t1;
  orientToBase(st, st->c0x, st->c0y, &s0, &t0);
  orientToBase(st, st->c1x, st->c1y, &s1, &t1);
  st->rot = rot & 3; st->flipH = fh; st->flipV = fv;
  float ax, ay, bx, by;
  baseToOrient(st, s0, t0, &ax, &ay);
  baseToOrient(st, s1, t1, &bx, &by);
  st->c0x = ax < bx ? ax : bx; st->c1x = ax < bx ? bx : ax;
  st->c0y = ay < by ? ay : by; st->c1y = ay < by ? by : ay;
}
void flexIeRotate(FlexImgEdit* e, int quarters){
  int r = ((int)e->st.rot + quarters) % 4; if(r < 0) r += 4;
  // Girar con un volteo puesto invierte el sentido del volteo respecto a la
  // pantalla; el usuario gira LO QUE VE: se compensa intercambiando volteos.
  uint8_t fh = e->st.flipH, fv = e->st.flipV;
  if(quarters & 1){ uint8_t t = fh; fh = fv; fv = t; }
  reorient(e, (uint8_t)r, fh, fv);
  flexIeCommit(e);
}
void flexIeFlip(FlexImgEdit* e, bool horizontal){
  reorient(e, e->st.rot, horizontal ? !e->st.flipH : e->st.flipH, horizontal ? e->st.flipV : !e->st.flipV);
  flexIeCommit(e);
}
void flexIeSetCrop(FlexImgEdit* e, float x0, float y0, float x1, float y1){
  if(x1 < x0){ float t = x0; x0 = x1; x1 = t; }
  if(y1 < y0){ float t = y0; y0 = y1; y1 = t; }
  x0 = clampf(x0, 0, 1); x1 = clampf(x1, 0, 1); y0 = clampf(y0, 0, 1); y1 = clampf(y1, 0, 1);
  if(x1 - x0 < FLEXIE_MIN_CROP){ x1 = clampf(x0 + FLEXIE_MIN_CROP, 0, 1); x0 = x1 - FLEXIE_MIN_CROP; }
  if(y1 - y0 < FLEXIE_MIN_CROP){ y1 = clampf(y0 + FLEXIE_MIN_CROP, 0, 1); y0 = y1 - FLEXIE_MIN_CROP; }
  e->st.c0x = x0; e->st.c0y = y0; e->st.c1x = x1; e->st.c1y = y1;
  flexIeCommit(e);
}
void flexIeSetLongSide(FlexImgEdit* e, int px){
  e->st.longSide = (uint16_t)clampi(px, 0, 16384);
  flexIeCommit(e);
}
void flexIeAdjustLive(FlexImgEdit* e, int which, int value){
  if(which < 0 || which >= FLEXIE_ADJ_N) return;
  e->st.adj[which] = (int8_t)clampi(value, -100, 100);
}
void flexIeFilterLive(FlexImgEdit* e, int filter, int amount){
  if(filter < 0 || filter >= FLEXIE_FILTER_N) filter = FLEXIE_FILTER_NONE;
  e->st.filter = (uint8_t)filter;
  e->st.filterAmt = (uint8_t)clampi(amount, 0, 100);
}

// -------------------------------------------------------------
//  TRAZOS, FORMAS Y TEXTOS
// -------------------------------------------------------------
static float baseLong(const FlexImgEdit* e){ return (float)(e->W > e->H ? e->W : e->H); }
// Escala de pixeles de la base a pixeles de la salida (a resolucion completa).
static float outPerBase(const FlexImgEdit* e, const FlexIeState* s, int outW){
  float cw, ch; cropPx(e, s, &cw, &ch);
  return outW / cw;
}
// Grosor/alto dado en fraccion de la SALIDA -> fraccion del lado largo de la base.
static float outFracToBase(const FlexImgEdit* e, float f){
  int ow, oh; outSizeOf(e, &e->st, &ow, &oh);
  float olong = (float)(ow > oh ? ow : oh);
  return f * olong / outPerBase(e, &e->st, ow) / baseLong(e);
}
// Escribe en la tabla por el final: lo que se podia rehacer apuntaba ahi y
// deja de valer (como cualquier cambio nuevo).
static FlexIeOverlay* ovNew(FlexImgEdit* e, int kind, uint32_t rgb){
  if(e->st.nOv >= FLEXIE_OV_MAX) return NULL;
  e->top = e->cur;
  FlexIeOverlay* o = &e->ov[e->st.nOv];
  memset(o, 0, sizeof(*o));
  o->kind = (uint8_t)kind; o->rgb = rgb & 0xFFFFFFu;
  return o;
}
int flexIeStrokeBegin(FlexImgEdit* e, uint32_t rgb, float widthOut, float u, float v){
  if(e->st.nPts >= FLEXIE_PTS_MAX) return -1;
  FlexIeOverlay* o = ovNew(e, FLEXIE_OV_STROKE, rgb);
  if(!o) return -1;
  o->w = outFracToBase(e, widthOut);
  o->p0 = e->st.nPts; o->pn = 0;
  int idx = e->st.nOv++;
  flexIeStrokeAdd(e, idx, u, v);
  return idx;
}
bool flexIeStrokeAdd(FlexImgEdit* e, int ov, float u, float v){
  if(ov < 0 || ov >= e->st.nOv || e->ov[ov].kind != FLEXIE_OV_STROKE) return false;
  FlexIeOverlay* o = &e->ov[ov];
  if(o->p0 + o->pn != e->st.nPts || e->st.nPts >= FLEXIE_PTS_MAX) return false;   // solo el ultimo trazo crece
  float s, t; flexIeOutToBase(e, clampf(u, 0, 1), clampf(v, 0, 1), &s, &t);
  if(o->pn){                                         // puntos casi iguales no aportan nada
    const FlexIePt* q = &e->pts[o->p0 + o->pn - 1];
    float dx = (s - q->x) * e->W, dy = (t - q->y) * e->H;
    if(dx * dx + dy * dy < 1.0f) return true;
  }
  e->pts[e->st.nPts].x = s; e->pts[e->st.nPts].y = t;
  e->st.nPts++; o->pn++;
  return true;
}
void flexIeStrokeEnd(FlexImgEdit* e, int ov){
  if(ov >= 0 && ov < e->st.nOv) flexIeCommit(e);
}
int flexIeAddShape(FlexImgEdit* e, int kind, uint32_t rgb, float widthOut, bool fill,
                   float u0, float v0, float u1, float v1){
  if(kind != FLEXIE_OV_RECT && kind != FLEXIE_OV_ELLIPSE && kind != FLEXIE_OV_LINE && kind != FLEXIE_OV_ARROW) return -1;
  FlexIeOverlay* o = ovNew(e, kind, rgb);
  if(!o) return -1;
  o->w = outFracToBase(e, widthOut);
  o->fill = fill && (kind == FLEXIE_OV_RECT || kind == FLEXIE_OV_ELLIPSE);
  flexIeOutToBase(e, clampf(u0, 0, 1), clampf(v0, 0, 1), &o->x0, &o->y0);
  flexIeOutToBase(e, clampf(u1, 0, 1), clampf(v1, 0, 1), &o->x1, &o->y1);
  int idx = e->st.nOv++;
  flexIeCommit(e);
  return idx;
}
int flexIeAddText(FlexImgEdit* e, const char* text, uint32_t rgb, float heightOut, float u, float v){
  if(!text || !text[0]) return -1;
  FlexIeOverlay* o = ovNew(e, FLEXIE_OV_TEXT, rgb);
  if(!o) return -1;
  snprintf(o->text, sizeof(o->text), "%s", text);
  o->th = outFracToBase(e, heightOut);
  flexIeOutToBase(e, clampf(u, 0, 1), clampf(v, 0, 1), &o->x0, &o->y0);
  int idx = e->st.nOv++;
  flexIeCommit(e);
  return idx;
}

// -------------------------------------------------------------
//  COLOR: curvas por canal + saturacion + matriz del filtro
// -------------------------------------------------------------
static uint32_t colorKeyOf(const FlexIeState* s){
  uint32_t h = 2166136261u;
  for(int i = 0; i < FLEXIE_ADJ_N; i++){ h ^= (uint8_t)s->adj[i]; h *= 16777619u; }
  h ^= s->filter; h *= 16777619u;
  h ^= s->filterAmt; h *= 16777619u;
  return h;
}
static void buildColor(FlexImgEdit* e){
  const FlexIeState* s = &e->st;
  uint32_t key = colorKeyOf(s);
  if(key == e->colorKey) return;
  e->colorKey = key;
  float ev = s->adj[FLEXIE_ADJ_EXPOSURE] / 100.0f * 1.5f;          // +-1,5 pasos
  float g = powf(2.0f, ev);
  float tp = s->adj[FLEXIE_ADJ_TEMP] / 100.0f;
  float gain[3] = { g * (1.0f + 0.14f * tp), g, g * (1.0f - 0.14f * tp) };
  float bri = s->adj[FLEXIE_ADJ_BRIGHT] / 100.0f * 0.25f;          // +-64 niveles
  float con = powf(2.0f, s->adj[FLEXIE_ADJ_CONTRAST] / 100.0f);   // x0,5 .. x2
  float sh = s->adj[FLEXIE_ADJ_SHADOWS] / 100.0f, hl = s->adj[FLEXIE_ADJ_HIGHLIGHTS] / 100.0f;
  uint8_t* lut[3] = { e->lutR, e->lutG, e->lutB };
  for(int c = 0; c < 3; c++){
    for(int i = 0; i < 256; i++){
      float x = clampf(i * gain[c] / 255.0f, 0.0f, 1.0f);
      x += bri;
      x = (x - 0.5f) * con + 0.5f;
      x = clampf(x, 0.0f, 1.0f);
      float d = 1.0f - x;
      x += sh * 0.35f * d * d * d;                                    // sombras: sobre todo lo oscuro
      x += hl * 0.35f * x * x * x;                                    // luces: sobre todo lo claro
      lut[c][i] = u8((int)(x * 255.0f + 0.5f));
    }
  }
  e->satK = (int32_t)((1.0f + s->adj[FLEXIE_ADJ_SATUR] / 100.0f) * 4096.0f);
  e->sharpK = (int32_t)(s->adj[FLEXIE_ADJ_SHARP] / 100.0f * 1.2f * 4096.0f);
  // Filtro: matriz 3x3, mezclada con la identidad segun la intensidad.
  static const float L[3] = { 0.299f, 0.587f, 0.114f };
  float M[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
  e->vignette = false;
  switch(s->filter){
    case FLEXIE_FILTER_BW: for(int r = 0; r < 3; r++) for(int c = 0; c < 3; c++) M[r * 3 + c] = L[c]; break;
    case FLEXIE_FILTER_SEPIA: {
      static const float S[9] = { 0.393f, 0.769f, 0.189f, 0.349f, 0.686f, 0.168f, 0.272f, 0.534f, 0.131f };
      memcpy(M, S, sizeof(M)); break;
    }
    case FLEXIE_FILTER_VIVID:
      for(int r = 0; r < 3; r++) for(int c = 0; c < 3; c++) M[r * 3 + c] = (1.0f - 1.4f) * L[c] + (r == c ? 1.4f : 0.0f);
      break;
    case FLEXIE_FILTER_COOL: M[0] = 0.90f; M[4] = 1.0f; M[8] = 1.12f; break;
    case FLEXIE_FILTER_WARM: M[0] = 1.10f; M[4] = 1.0f; M[8] = 0.86f; break;
    case FLEXIE_FILTER_VINTAGE: {
      static const float S[9] = { 0.393f, 0.769f, 0.189f, 0.349f, 0.686f, 0.168f, 0.272f, 0.534f, 0.131f };
      for(int i = 0; i < 9; i++) M[i] = 0.55f * S[i] + 0.45f * ((i % 4) == 0 ? 1.0f : 0.0f);
      e->vignette = s->filterAmt > 0;
      break;
    }
  }
  float a = s->filter == FLEXIE_FILTER_NONE ? 0.0f : s->filterAmt / 100.0f;
  e->matOn = a > 0.0f;
  for(int i = 0; i < 9; i++){
    float id = (i % 4) == 0 ? 1.0f : 0.0f;
    e->mat[i] = (int32_t)((id + a * (M[i] - id)) * 4096.0f);
  }
}

// -------------------------------------------------------------
//  PINTAR
// -------------------------------------------------------------
// De donde se leen los pixeles: la base o su copia reducida.
typedef struct { const uint8_t* p; int W, H; } IeSrc;

// Muestra bilineal (coordenadas en pixeles de la fuente, 16.16).
static inline void sampleBase(const IeSrc* e, int32_t fx, int32_t fy, int* rgb){
  int W = e->W, H = e->H;
  int x0 = fx >> 16, y0 = fy >> 16;
  int ax = (fx >> 8) & 255, ay = (fy >> 8) & 255;
  if(fx < 0){ x0 = 0; ax = 0; }
  if(fy < 0){ y0 = 0; ay = 0; }
  if(x0 >= W - 1){ x0 = W - 1; ax = 0; }
  if(y0 >= H - 1){ y0 = H - 1; ay = 0; }
  int x1 = x0 + (ax ? 1 : 0), y1 = y0 + (ay ? 1 : 0);
  const uint8_t* p00 = e->p + ((size_t)y0 * W + x0) * 3;
  const uint8_t* p10 = e->p + ((size_t)y0 * W + x1) * 3;
  const uint8_t* p01 = e->p + ((size_t)y1 * W + x0) * 3;
  const uint8_t* p11 = e->p + ((size_t)y1 * W + x1) * 3;
  for(int c = 0; c < 3; c++){
    int top = p00[c] * (256 - ax) + p10[c] * ax;
    int bot = p01[c] * (256 - ax) + p11[c] * ax;
    rgb[c] = (top * (256 - ay) + bot * ay + 32768) >> 16;
  }
}

// Coordenada en la fuente (pixeles) de la salida (px, py) a outW x outH.
static void outPxToBase(const FlexImgEdit* e, const IeSrc* src, int outW, int outH, float px, float py, float* bx, float* by){
  float s, t;
  flexIeOutToBase(e, px / outW, py / outH, &s, &t);
  *bx = s * src->W - 0.5f; *by = t * src->H - 0.5f;
}

// La copia reducida basta si da al menos ~un pixel por pixel de salida.
bool flexIeUsesProxy(const FlexImgEdit* e, int outW, int outH){
  if(!e->proxy || outW <= 0 || outH <= 0) return false;
  float cw, ch; cropPx(e, &e->st, &cw, &ch);        // pixeles de la base
  float k = (float)e->PW / (float)e->W;             // copia / base
  return cw * k >= outW * 0.9f && ch * k >= outH * 0.9f;
}

// Cobertura antialias de una distancia a un borde de grosor r.
static inline int covOf(float r, float d){
  float c = r + 0.5f - d;
  if(c <= 0.0f) return 0;
  if(c >= 1.0f) return 255;
  return (int)(c * 255.0f);
}
static inline float distSeg(float px, float py, float ax, float ay, float bx, float by){
  float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
  float l2 = vx * vx + vy * vy;
  float k = l2 > 0.0f ? (wx * vx + wy * vy) / l2 : 0.0f;
  k = clampf(k, 0.0f, 1.0f);
  float dx = wx - k * vx, dy = wy - k * vy;
  return sqrtf(dx * dx + dy * dy);
}
// Capsula (segmento con grosor r) en la banda de coberturas: max.
static void rasterSeg(uint8_t* cov, int outW, int y0, int rows, float ax, float ay, float bx, float by, float r){
  int xa = (int)floorf((ax < bx ? ax : bx) - r - 1), xb = (int)ceilf((ax > bx ? ax : bx) + r + 1);
  int ya = (int)floorf((ay < by ? ay : by) - r - 1), yb = (int)ceilf((ay > by ? ay : by) + r + 1);
  if(xa < 0) xa = 0;
  if(xb > outW - 1) xb = outW - 1;
  if(ya < y0) ya = y0;
  if(yb > y0 + rows - 1) yb = y0 + rows - 1;
  for(int y = ya; y <= yb; y++){
    uint8_t* row = cov + (size_t)(y - y0) * outW;
    for(int x = xa; x <= xb; x++){
      int c = covOf(r, distSeg(x + 0.5f, y + 0.5f, ax, ay, bx, by));
      if(c > row[x]) row[x] = (uint8_t)c;
    }
  }
}

static void blendCov(uint8_t* dst, const uint8_t* cov, int outW, int x0, int x1, int ry0, int ry1, int y0, uint32_t rgb){
  int cr = (rgb >> 16) & 255, cg = (rgb >> 8) & 255, cb = rgb & 255;
  for(int y = ry0; y <= ry1; y++){
    const uint8_t* cr0 = cov + (size_t)(y - y0) * outW;
    uint8_t* d = dst + (size_t)(y - y0) * outW * 3;
    for(int x = x0; x <= x1; x++){
      int a = cr0[x];
      if(!a) continue;
      uint8_t* p = d + (size_t)x * 3;
      p[0] = (uint8_t)(p[0] + ((cr - p[0]) * a + 127) / 255);
      p[1] = (uint8_t)(p[1] + ((cg - p[1]) * a + 127) / 255);
      p[2] = (uint8_t)(p[2] + ((cb - p[2]) * a + 127) / 255);
    }
  }
}

static void drawOverlays(FlexImgEdit* e, int outW, int outH, int y0, int rows, uint8_t* dst, uint8_t* cov){
  const FlexIeState* st = &e->st;
  float sc = outPerBase(e, st, outW) * baseLong(e);   // fraccion del lado largo -> pixeles de salida
  int y1 = y0 + rows - 1;
  for(int i = st->ovStart; i < st->nOv; i++){
    const FlexIeOverlay* o = &e->ov[i];
    float r = 0.5f * o->w * sc;
    if(r < 0.5f) r = 0.5f;
    // Caja de lo que se dibuja, en la salida.
    float bxa = 1e9f, bxb = -1e9f, bya = 1e9f, byb = -1e9f;
    float P[4][2];                                     // puntos de formas/lineas en salida
    int np = 0;
    if(o->kind == FLEXIE_OV_STROKE){
      for(int k = 0; k < o->pn; k++){
        const FlexIePt* q = &e->pts[o->p0 + k];
        float u, v; flexIeBaseToOut(e, q->x, q->y, &u, &v);
        float x = u * outW, y = v * outH;
        if(x < bxa) bxa = x;
        if(x > bxb) bxb = x;
        if(y < bya) bya = y;
        if(y > byb) byb = y;
      }
    } else if(o->kind == FLEXIE_OV_TEXT){
      float u, v; flexIeBaseToOut(e, o->x0, o->y0, &u, &v);
      bxa = u * outW; bya = v * outH;
      int hpx = (int)(o->th * sc + 0.5f);
      int tw = (e->textFn && hpx > 0) ? e->textFn(e->textCtx, o->text, hpx, -1, 0, NULL, 0) : 0;
      bxb = bxa + tw; byb = bya + hpx;
      r = 0;
    } else {
      float u, v;
      flexIeBaseToOut(e, o->x0, o->y0, &u, &v); P[0][0] = u * outW; P[0][1] = v * outH;
      flexIeBaseToOut(e, o->x1, o->y1, &u, &v); P[1][0] = u * outW; P[1][1] = v * outH;
      np = 2;
      bxa = P[0][0] < P[1][0] ? P[0][0] : P[1][0]; bxb = P[0][0] < P[1][0] ? P[1][0] : P[0][0];
      bya = P[0][1] < P[1][1] ? P[0][1] : P[1][1]; byb = P[0][1] < P[1][1] ? P[1][1] : P[0][1];
      if(o->kind == FLEXIE_OV_ARROW){                  // punta: dos tramos a +-28 grados
        float dx = P[1][0] - P[0][0], dy = P[1][1] - P[0][1];
        float len = sqrtf(dx * dx + dy * dy);
        if(len > 0.5f){
          float hl = len * 0.35f, cap = 6.0f * r + 10.0f;
          if(hl > cap) hl = cap;
          float ux = dx / len, uy = dy / len, cs = cosf(0.4887f), sn = sinf(0.4887f);
          P[2][0] = P[1][0] - hl * (ux * cs - uy * sn); P[2][1] = P[1][1] - hl * (uy * cs + ux * sn);
          P[3][0] = P[1][0] - hl * (ux * cs + uy * sn); P[3][1] = P[1][1] - hl * (uy * cs - ux * sn);
          np = 4;
          for(int k = 2; k < 4; k++){
            if(P[k][0] < bxa) bxa = P[k][0];
            if(P[k][0] > bxb) bxb = P[k][0];
            if(P[k][1] < bya) bya = P[k][1];
            if(P[k][1] > byb) byb = P[k][1];
          }
        }
      }
    }
    int xa = (int)floorf(bxa - r - 1), xb = (int)ceilf(bxb + r + 1);
    int ya = (int)floorf(bya - r - 1), yb = (int)ceilf(byb + r + 1);
    if(xa < 0) xa = 0;
    if(xb > outW - 1) xb = outW - 1;
    if(ya < y0) ya = y0;
    if(yb > y1) yb = y1;
    if(xa > xb || ya > yb) continue;                   // no toca esta banda
    for(int y = ya; y <= yb; y++) memset(cov + (size_t)(y - y0) * outW + xa, 0, (size_t)(xb - xa + 1));
    switch(o->kind){
      case FLEXIE_OV_STROKE: {
        float px0 = 0, py0 = 0;
        for(int k = 0; k < o->pn; k++){
          const FlexIePt* q = &e->pts[o->p0 + k];
          float u, v; flexIeBaseToOut(e, q->x, q->y, &u, &v);
          float x = u * outW, y = v * outH;
          if(k == 0) rasterSeg(cov, outW, y0, rows, x, y, x, y, r);
          else rasterSeg(cov, outW, y0, rows, px0, py0, x, y, r);
          px0 = x; py0 = y;
        }
        break;
      }
      case FLEXIE_OV_LINE: case FLEXIE_OV_ARROW:
        rasterSeg(cov, outW, y0, rows, P[0][0], P[0][1], P[1][0], P[1][1], r);
        if(np == 4){
          rasterSeg(cov, outW, y0, rows, P[1][0], P[1][1], P[2][0], P[2][1], r);
          rasterSeg(cov, outW, y0, rows, P[1][0], P[1][1], P[3][0], P[3][1], r);
        }
        break;
      case FLEXIE_OV_RECT: {
        float x0 = bxa, x1 = bxb, yy0 = bya, yy1 = byb;
        for(int y = ya; y <= yb; y++){
          uint8_t* row = cov + (size_t)(y - y0) * outW;
          float py = y + 0.5f;
          for(int x = xa; x <= xb; x++){
            float px = x + 0.5f;
            float dx = px < x0 ? x0 - px : (px > x1 ? px - x1 : 0.0f);
            float dy = py < yy0 ? yy0 - py : (py > yy1 ? py - yy1 : 0.0f);
            float out = sqrtf(dx * dx + dy * dy);
            float in = 0.0f;
            if(out == 0.0f){
              float a = px - x0, b = x1 - px, c = py - yy0, d = yy1 - py;
              in = a < b ? a : b; if(c < in) in = c; if(d < in) in = d;
            }
            int cv = o->fill ? (out > 0 ? covOf(0.0f, out) : 255) : covOf(r, out > 0 ? out : in);
            row[x] = (uint8_t)cv;
          }
        }
        break;
      }
      case FLEXIE_OV_ELLIPSE: {
        float cx = (bxa + bxb) * 0.5f, cy = (bya + byb) * 0.5f;
        float rx = (bxb - bxa) * 0.5f, ry = (byb - bya) * 0.5f;
        if(rx < 0.5f) rx = 0.5f;
        if(ry < 0.5f) ry = 0.5f;
        float gm = sqrtf(rx * ry);
        for(int y = ya; y <= yb; y++){
          uint8_t* row = cov + (size_t)(y - y0) * outW;
          float dy = (y + 0.5f - cy) / ry;
          for(int x = xa; x <= xb; x++){
            float dx = (x + 0.5f - cx) / rx;
            float d = (sqrtf(dx * dx + dy * dy) - 1.0f) * gm;     // distancia aproximada al borde
            int cv = o->fill ? (d <= 0 ? 255 : covOf(0.0f, d)) : covOf(r, d < 0 ? -d : d);
            row[x] = (uint8_t)cv;
          }
        }
        break;
      }
      case FLEXIE_OV_TEXT: {
        if(!e->textFn) break;
        int tx = (int)floorf(bxa), ty = (int)floorf(bya), hpx = (int)(o->th * sc + 0.5f);
        int c0 = xa - tx, n = xb - xa + 1;
        for(int y = ya; y <= yb; y++){
          if(y < ty || y >= ty + hpx) continue;
          e->textFn(e->textCtx, o->text, hpx, y - ty, c0, cov + (size_t)(y - y0) * outW + xa, n);
        }
        break;
      }
    }
    blendCov(dst, cov, outW, xa, xb, ya, yb, y0, o->rgb);
  }
}

void flexIeRenderRows(FlexImgEdit* e, int outW, int outH, int y0, int rows, uint8_t* dst, uint8_t* scratch){
  if(!e || !e->base || outW <= 0 || outH <= 0 || rows <= 0) return;
  buildColor(e);
  IeSrc src = { e->base, e->W, e->H };
  if(flexIeUsesProxy(e, outW, outH)){ src.p = e->proxy; src.W = e->PW; src.H = e->PH; }
  const bool sharp = e->sharpK != 0;
  const bool vig = e->vignette;
  for(int r = 0; r < rows; r++){
    int y = y0 + r;
    float py = y + 0.5f;
    // La correspondencia es afin y alineada (giros de 90): origen y paso
    // por columna, y paso por fila para la nitidez.
    float bx0, by0, bx1, by1, bxd, byd;
    outPxToBase(e, &src, outW, outH, 0.5f, py, &bx0, &by0);
    outPxToBase(e, &src, outW, outH, 1.5f, py, &bx1, &by1);
    outPxToBase(e, &src, outW, outH, 0.5f, py + 1.0f, &bxd, &byd);
    float sx = bx1 - bx0, sy = by1 - by0;              // paso por columna
    float tx = bxd - bx0, ty = byd - by0;              // paso por fila
    // Reduciendo mucho (vista previa de una foto grande), una muestra por
    // pixel parpadea: se promedian 4.
    bool ss = fabsf(sx) + fabsf(sy) > 1.6f || fabsf(tx) + fabsf(ty) > 1.6f;
    int32_t fx = (int32_t)(bx0 * 65536.0f), fy = (int32_t)(by0 * 65536.0f);
    int32_t dfx = (int32_t)(sx * 65536.0f), dfy = (int32_t)(sy * 65536.0f);
    int32_t gfx = (int32_t)(tx * 65536.0f), gfy = (int32_t)(ty * 65536.0f);
    uint8_t* d = dst + (size_t)r * outW * 3;
    float vy = (py / outH) * 2.0f - 1.0f;
    for(int x = 0; x < outW; x++, fx += dfx, fy += dfy){
      int c[3];
      if(ss){
        int a[3], b[3], q[3], w[3];
        sampleBase(&src, fx - dfx / 4 - gfx / 4, fy - dfy / 4 - gfy / 4, a);
        sampleBase(&src, fx + dfx / 4 - gfx / 4, fy + dfy / 4 - gfy / 4, b);
        sampleBase(&src, fx - dfx / 4 + gfx / 4, fy - dfy / 4 + gfy / 4, q);
        sampleBase(&src, fx + dfx / 4 + gfx / 4, fy + dfy / 4 + gfy / 4, w);
        for(int k = 0; k < 3; k++) c[k] = (a[k] + b[k] + q[k] + w[k] + 2) >> 2;
      } else sampleBase(&src, fx, fy, c);
      if(sharp){
        int n1[3], n2[3], n3[3], n4[3];
        sampleBase(&src, fx - dfx, fy - dfy, n1);
        sampleBase(&src, fx + dfx, fy + dfy, n2);
        sampleBase(&src, fx - gfx, fy - gfy, n3);
        sampleBase(&src, fx + gfx, fy + gfy, n4);
        for(int k = 0; k < 3; k++){
          int avg = (n1[k] + n2[k] + n3[k] + n4[k] + 2) >> 2;
          c[k] = clampi(c[k] + (int)(((int64_t)(c[k] - avg) * e->sharpK) >> 12), 0, 255);
        }
      }
      int R = e->lutR[c[0]], G = e->lutG[c[1]], B = e->lutB[c[2]];
      if(e->satK != 4096){
        int Y = (R * 1225 + G * 2404 + B * 467) >> 12;
        R = Y + (((R - Y) * e->satK) >> 12);
        G = Y + (((G - Y) * e->satK) >> 12);
        B = Y + (((B - Y) * e->satK) >> 12);
      }
      if(e->matOn){
        const int32_t* m = e->mat;
        int r2 = (m[0] * R + m[1] * G + m[2] * B) >> 12;
        int g2 = (m[3] * R + m[4] * G + m[5] * B) >> 12;
        int b2 = (m[6] * R + m[7] * G + m[8] * B) >> 12;
        R = r2; G = g2; B = b2;
        if(vig){                                        // vintage: bordes mas oscuros y negros levantados
          float vx = ((x + 0.5f) / outW) * 2.0f - 1.0f;
          float k = 1.0f - 0.30f * (vx * vx + vy * vy) * 0.5f * e->st.filterAmt / 100.0f;
          R = (int)(R * k) + 10; G = (int)(G * k) + 8; B = (int)(B * k) + 6;
        }
      }
      d[x * 3 + 0] = u8(R); d[x * 3 + 1] = u8(G); d[x * 3 + 2] = u8(B);
    }
  }
  if(scratch && e->st.nOv > e->st.ovStart) drawOverlays(e, outW, outH, y0, rows, dst, scratch);
}

// -------------------------------------------------------------
//  GUARDAR
// -------------------------------------------------------------
struct IeSaveCtx {
  FlexImgEdit* e; int w, h;
  uint8_t* scratch;
  FlexIeStopFn stop; FlexIeProgressFn progress; void* cb;
};
static bool ieSrc(void* c, int y, int rows, uint8_t* dst){
  IeSaveCtx* s = (IeSaveCtx*)c;
  if(s->stop && s->stop(s->cb)) return false;
  flexIeRenderRows(s->e, s->w, s->h, y, rows, dst, s->scratch);
  if(s->progress) s->progress(s->cb, y + rows, s->h);
  return true;
}
int flexIeSave(FlexImgEdit* e, int quality, FlexJeOutFn out, void* outCtx,
               FlexIeStopFn stop, FlexIeProgressFn progress, void* cbCtx,
               FlexJpegAlloc af, FlexJpegFree ff){
  if(!e || !e->base || !out) return FLEXJE_ERR_ARG;
  int w, h; flexIeOutSize(e, &w, &h);
  IeSaveCtx s = { e, w, h, NULL, stop, progress, cbCtx };
  if(e->st.nOv > e->st.ovStart){                     // coberturas: la fuente da 16 filas como mucho (4:2:0)
    s.scratch = (uint8_t*)(af ? af((size_t)w * 16) : NULL);
    if(!s.scratch) return FLEXJE_ERR_MEMORY;
  }
  FlexJeCfg cfg;
  cfg.width = w; cfg.height = h; cfg.quality = quality;
  cfg.subsampling = FLEXJE_SUB_420; cfg.input = FLEXJE_IN_RGB888;
  int rc = flexJpegEncode(&cfg, ieSrc, &s, out, outCtx, af, ff);
  if(s.scratch && ff) ff(s.scratch);
  return rc;
}
