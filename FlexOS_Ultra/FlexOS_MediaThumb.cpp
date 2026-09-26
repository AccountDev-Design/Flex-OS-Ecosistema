// #############################################################
// ##  FlexOS · MINIATURAS  ·  implementacion
// ##  Ver FlexOS_MediaThumb.h. Sin estado global: lo llaman a la vez
// ##  la tarea del servidor web y la de fondo de la biblioteca.
// #############################################################
#include "FlexOS_MediaThumb.h"
#include <string.h>
#include <stdlib.h>

void flexThumbCoverRect(int w, int h, int* x0, int* y0, int* cw, int* ch){
  int s = w < h ? w : h;
  if(s < 1) s = 1;
  if(cw) *cw = s;
  if(ch) *ch = s;
  if(x0) *x0 = (w - s) / 2;
  if(y0) *y0 = (h - s) / 2;
}

// Lee un pixel de la fuente (RGB888 o RGB565) como tres bytes.
static inline void px888(const uint8_t* row, int x, int input, int* r, int* g, int* b){
  if(input == FLEXJE_IN_RGB565){
    uint16_t v = (uint16_t)(row[(size_t)x * 2] | (row[(size_t)x * 2 + 1] << 8));
    int R = (v >> 11) & 31, G = (v >> 5) & 63, B = v & 31;
    *r = (R << 3) | (R >> 2); *g = (G << 2) | (G >> 4); *b = (B << 3) | (B >> 2);
  } else {
    const uint8_t* p = row + (size_t)x * 3;
    *r = p[0]; *g = p[1]; *b = p[2];
  }
}

// Region cuadrada s x s (origen x0,y0) de la fuente -> side x side RGB888.
// Reducir: media de caja (cada pixel de salida promedia TODOS los que cubre,
// no toma uno: sin eso una foto con textura fina da una miniatura con
// muare). Ampliar (fuente mas pequena que la celda): bilineal.
static void resampleSquare(const uint8_t* src, size_t stride, int input, int x0, int y0, int s,
                           uint8_t* dst, int side){
  if(s >= side){
    for(int oy = 0; oy < side; oy++){
      int ys = (int)((int64_t)oy * s / side), ye = (int)((int64_t)(oy + 1) * s / side);
      if(ye <= ys) ye = ys + 1;
      for(int ox = 0; ox < side; ox++){
        int xs = (int)((int64_t)ox * s / side), xe = (int)((int64_t)(ox + 1) * s / side);
        if(xe <= xs) xe = xs + 1;
        uint32_t sr = 0, sg = 0, sb = 0, n = 0;
        for(int y = ys; y < ye; y++){
          const uint8_t* row = src + (size_t)(y0 + y) * stride;
          for(int x = xs; x < xe; x++){
            int r, g, b; px888(row, x0 + x, input, &r, &g, &b);
            sr += (uint32_t)r; sg += (uint32_t)g; sb += (uint32_t)b; n++;
          }
        }
        uint8_t* d = dst + ((size_t)oy * side + ox) * 3;
        d[0] = (uint8_t)((sr + n / 2) / n); d[1] = (uint8_t)((sg + n / 2) / n); d[2] = (uint8_t)((sb + n / 2) / n);
      }
    }
    return;
  }
  // Bilineal en coma fija 16.16.
  for(int oy = 0; oy < side; oy++){
    int32_t fy = (int32_t)(((int64_t)(2 * oy + 1) * s * 32768) / side) - 32768;
    if(fy < 0) fy = 0;
    int yA = fy >> 16, yB = yA + 1 < s ? yA + 1 : s - 1, wy = fy & 0xFFFF;
    for(int ox = 0; ox < side; ox++){
      int32_t fx = (int32_t)(((int64_t)(2 * ox + 1) * s * 32768) / side) - 32768;
      if(fx < 0) fx = 0;
      int xA = fx >> 16, xB = xA + 1 < s ? xA + 1 : s - 1, wx = fx & 0xFFFF;
      int c[4][3];
      px888(src + (size_t)(y0 + yA) * stride, x0 + xA, input, &c[0][0], &c[0][1], &c[0][2]);
      px888(src + (size_t)(y0 + yA) * stride, x0 + xB, input, &c[1][0], &c[1][1], &c[1][2]);
      px888(src + (size_t)(y0 + yB) * stride, x0 + xA, input, &c[2][0], &c[2][1], &c[2][2]);
      px888(src + (size_t)(y0 + yB) * stride, x0 + xB, input, &c[3][0], &c[3][1], &c[3][2]);
      uint8_t* d = dst + ((size_t)oy * side + ox) * 3;
      for(int k = 0; k < 3; k++){
        int64_t top = (int64_t)c[0][k] * (65536 - wx) + (int64_t)c[1][k] * wx;
        int64_t bot = (int64_t)c[2][k] * (65536 - wx) + (int64_t)c[3][k] * wx;
        int64_t v = (top * (65536 - wy) + bot * wy + ((int64_t)1 << 31)) >> 32;
        d[k] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
      }
    }
  }
}

static int encodeSquare(const uint8_t* rgb, int side, int quality, FlexJeOutFn out, void* outCtx,
                        FlexJpegAlloc af, FlexJpegFree ff){
  FlexJeCfg c;
  c.width = side; c.height = side; c.quality = quality;
  c.subsampling = FLEXJE_SUB_420; c.input = FLEXJE_IN_RGB888;
  int rc = flexJpegEncodeMem(&c, rgb, (size_t)side * 3, out, outCtx, af, ff);
  if(rc == FLEXJE_OK) return FLEXTH_OK;
  if(rc == FLEXJE_ERR_MEMORY) return FLEXTH_ERR_MEMORY;
  if(rc == FLEXJE_ERR_WRITE) return FLEXTH_ERR_WRITE;
  return FLEXTH_ERR_ARG;
}

int flexThumbFromPixels(const void* px, int w, int h, size_t stride, int input,
                        int side, int quality, FlexJeOutFn out, void* outCtx,
                        FlexJpegAlloc af, FlexJpegFree ff){
  if(!px || w <= 0 || h <= 0 || side <= 0 || side > 1024 || !out) return FLEXTH_ERR_ARG;
  if(!af) af = malloc;
  if(!ff) ff = free;
  size_t minStride = (size_t)w * (input == FLEXJE_IN_RGB565 ? 2u : 3u);
  if(stride == 0) stride = minStride;
  if(stride < minStride) return FLEXTH_ERR_ARG;
  int x0, y0, s;
  flexThumbCoverRect(w, h, &x0, &y0, &s, NULL);
  uint8_t* sq = (uint8_t*)af((size_t)side * side * 3);
  if(!sq) return FLEXTH_ERR_MEMORY;
  resampleSquare((const uint8_t*)px, stride, input, x0, y0, s, sq, side);
  int rc = encodeSquare(sq, side, quality, out, outCtx, af, ff);
  ff(sq);
  return rc;
}

// ---- JPEG: se decodifica SOLO el recorte, a la escala justa ----
// Todo sale del MISMO camino: el de flujo. Una foto que ya esta en memoria
// (la miniatura que aporta el navegador, el fotograma de un AVI) se lee con
// un lector de memoria; una que esta en el disco se lee por trozos y nunca
// entera en RAM (una foto de 5 MB ya no son 5 MB de PSRAM solo para hacer
// su miniatura de 132 px).
struct CropSink { uint8_t* buf; int x0, y0, s, w; };
struct ThJob {
  CropSink cs;                 // PRIMER campo: cropRow recibe este puntero
  int side;
  FlexJpegAlloc af;
  int srcW, srcH;
  bool noMem;
};
static bool cropRow(void* u, int y, int w, const uint8_t* rgb){
  CropSink* c = (CropSink*)u;
  c->w = w;
  if(y < c->y0) return true;
  if(y >= c->y0 + c->s) return true;          // se sigue hasta el final: eso VALIDA el resto del flujo
  int n = c->s;
  if(c->x0 + n > w) n = w - c->x0;
  if(n > 0) memcpy(c->buf + (size_t)(y - c->y0) * c->s * 3, rgb + (size_t)c->x0 * 3, (size_t)n * 3);
  return true;
}
// Con las medidas REALES ya leidas de la cabecera: la mayor division (8, 4,
// 2) que aun deja los DOS lados >= side. La miniatura se promedia desde una
// imagen algo mayor que ella, nunca desde la foto entera (una de 12 MP
// costaria 12 MP de trabajo) ni desde una mas pequena (saldria borrosa).
static int thPick(void* u, int width, int height){
  ThJob* t = (ThJob*)u;
  t->srcW = width; t->srcH = height;
  int d = 1;
  for(int k = 8; k >= 2; k >>= 1)
    if((width + k - 1) / k >= t->side && (height + k - 1) / k >= t->side){ d = k; break; }
  int dw = (width + d - 1) / d, dh = (height + d - 1) / d;
  flexThumbCoverRect(dw, dh, &t->cs.x0, &t->cs.y0, &t->cs.s, NULL);
  t->cs.w = 0;
  t->cs.buf = (uint8_t*)t->af((size_t)t->cs.s * t->cs.s * 3);
  if(!t->cs.buf){ t->noMem = true; return 0; }     // cancela sin decodificar nada
  memset(t->cs.buf, 0, (size_t)t->cs.s * t->cs.s * 3);
  return d;
}

int flexThumbFromJpegStream(FlexJpegReadFn rd, void* rdCtx, int side, int quality,
                            FlexJeOutFn out, void* outCtx, int* srcW, int* srcH,
                            FlexJpegAlloc af, FlexJpegFree ff){
  if(!rd || side <= 0 || side > 1024 || !out) return FLEXTH_ERR_ARG;
  if(!af) af = malloc;
  if(!ff) ff = free;
  ThJob t;
  memset(&t, 0, sizeof(t));
  t.side = side; t.af = af;
  int rc = flexJpegDecode888Stream(rd, rdCtx, 0, 0, 0, thPick, NULL, cropRow, &t, af, ff);
  if(srcW && t.srcW) *srcW = t.srcW;
  if(srcH && t.srcH) *srcH = t.srcH;
  if(rc != FLEXJPG_OK){
    if(t.cs.buf) ff(t.cs.buf);
    if(t.noMem || rc == FLEXJPG_ERR_MEMORY) return FLEXTH_ERR_MEMORY;
    if(rc == FLEXJPG_ERR_UNSUPPORTED) return FLEXTH_ERR_UNSUP;
    if(rc == FLEXJPG_ERR_IO) return FLEXTH_ERR_IO;
    return FLEXTH_ERR_DECODE;
  }
  uint8_t* sq = (uint8_t*)af((size_t)side * side * 3);
  if(!sq){ ff(t.cs.buf); return FLEXTH_ERR_MEMORY; }
  resampleSquare(t.cs.buf, (size_t)t.cs.s * 3, FLEXJE_IN_RGB888, 0, 0, t.cs.s, sq, side);
  ff(t.cs.buf);
  rc = encodeSquare(sq, side, quality, out, outCtx, af, ff);
  ff(sq);
  return rc;
}

struct ThMem { const uint8_t* p; size_t n, pos; };
static int thMemRead(void* c, uint8_t* buf, size_t n){
  ThMem* m = (ThMem*)c;
  size_t left = m->n - m->pos;
  if(n > left) n = left;
  memcpy(buf, m->p + m->pos, n);
  m->pos += n;
  return (int)n;
}

int flexThumbFromJpeg(const uint8_t* jpg, size_t len, int side, int quality,
                      FlexJeOutFn out, void* outCtx, int* srcW, int* srcH,
                      FlexJpegAlloc af, FlexJpegFree ff){
  if(!jpg || len < 4 || side <= 0 || side > 1024 || !out) return FLEXTH_ERR_ARG;
  ThMem m = { jpg, len, 0 };
  return flexThumbFromJpegStream(thMemRead, &m, side, quality, out, outCtx, srcW, srcH, af, ff);
}

int flexThumbFromAvi(const FlexMediaIO* io, int side, int quality,
                     FlexJeOutFn out, void* outCtx, int* w, int* h, uint32_t* durMs,
                     FlexJpegAlloc af, FlexJpegFree ff){
  if(!io || !out) return FLEXTH_ERR_ARG;
  if(!af) af = malloc;
  if(!ff) ff = free;
  // El contexto del AVI lleva el indice disperso (4 KB): al monton, jamas
  // a la pila de una tarea.
  FlexAviCtx* a = (FlexAviCtx*)af(sizeof(FlexAviCtx));
  if(!a) return FLEXTH_ERR_MEMORY;
  int rc = flexAviOpen(a, io);
  if(rc != FLEXAVI_OK){
    ff(a);
    if(rc == FLEXAVI_ERR_CODEC || rc == FLEXAVI_ERR_NOVIDEO) return FLEXTH_ERR_UNSUP;
    return rc == FLEXAVI_ERR_IO ? FLEXTH_ERR_IO : FLEXTH_ERR_DECODE;
  }
  if(w) *w = a->width;
  if(h) *h = a->height;
  if(durMs) *durMs = flexAviDurationMs(a);
  // Buffer del fotograma: lo que declara el propio AVI (dwSuggestedBufferSize)
  // si es creible, y si no FLEXTH_AVI_FRAME. Si un fotograma pide mas, crece
  // hasta FLEXAVI_FRAME_MAX, el MISMO tope del reproductor: un video de
  // camara con fotogramas de 300 KB ya no se clasifica como "no reproducible"
  // por un limite que el reproductor tampoco tiene.
  uint32_t cap = FLEXTH_AVI_FRAME;
  if(a->maxFrameBytes >= 4096u && a->maxFrameBytes <= FLEXAVI_FRAME_MAX) cap = (a->maxFrameBytes + 4095u) & ~4095u;
  uint8_t* fb = (uint8_t*)af(cap);
  if(!fb){ ff(a); return FLEXTH_ERR_MEMORY; }
  // El primer fotograma CON imagen que se deje leer: los vacios (repeticion
  // del anterior) no tienen nada que mostrar, y uno danado al principio no
  // condena un video cuyo resto esta bien. Como mucho FLEXTH_AVI_TRIES.
  rc = FLEXTH_ERR_DECODE;
  bool grown = false;
  int empties = 0;
  for(int tries = 0; tries < FLEXTH_AVI_TRIES && empties < 256; ){
    int n = flexAviReadFrame(a, fb, cap, NULL);
    if(n == FLEXAVI_ERR_TOOBIG){
      uint32_t need = a->needBytes;
      if(need > FLEXAVI_FRAME_MAX || grown){ rc = FLEXTH_ERR_UNSUP; break; }
      ff(fb);
      cap = (need + 4095u) & ~4095u;
      fb = (uint8_t*)af(cap);
      if(!fb){ ff(a); return FLEXTH_ERR_MEMORY; }
      grown = true;
      continue;                                 // el mismo fotograma, ya cabe
    }
    grown = false;
    if(n == 0){ empties++; continue; }          // vacio: no es un intento (pero se acotan)
    if(n < 0){ rc = n == FLEXAVI_ERR_IO ? FLEXTH_ERR_IO : FLEXTH_ERR_DECODE; break; }
    tries++;
    rc = flexThumbFromJpeg(fb, (size_t)n, side, quality, out, outCtx, NULL, NULL, af, ff);
    if(rc != FLEXTH_ERR_DECODE) break;          // hecho, o un motivo que no arregla el siguiente
  }
  ff(fb);
  ff(a);
  return rc;
}

const char* flexThumbErrStr(int err){
  switch(err){
    case FLEXTH_OK:         return "OK";
    case FLEXTH_ERR_ARG:    return "Parametros no validos";
    case FLEXTH_ERR_MEMORY: return "Memoria insuficiente";
    case FLEXTH_ERR_DECODE: return "El archivo esta danado";
    case FLEXTH_ERR_UNSUP:  return "Formato que esta placa no abre";
    case FLEXTH_ERR_WRITE:  return "No se pudo guardar la miniatura";
    case FLEXTH_ERR_IO:     return "No se pudo leer el archivo";
  }
  return "Error";
}
