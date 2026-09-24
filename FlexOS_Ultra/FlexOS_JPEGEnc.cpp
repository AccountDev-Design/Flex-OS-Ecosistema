// #############################################################
// ##  FlexOS_JPEGEnc.cpp  ·  codificador JPEG baseline
// ##  Ver FlexOS_JPEGEnc.h. Sin estado global mutable: se puede
// ##  llamar a la vez desde la tarea de fondo de la biblioteca y desde
// ##  la del sistema sin pisarse.
// #############################################################
#include "FlexOS_JPEGEnc.h"
#include <string.h>
#include <stdlib.h>

// ---- Orden zigzag: posicion k del flujo -> indice natural (fila*8+col) ----
static const uint8_t kNat[64] = {
   0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
  12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
};

// ---- Cuantizacion del anexo K (orden natural) ----
static const uint8_t kQLum[64] = {
  16, 11, 10, 16,  24,  40,  51,  61,  12, 12, 14, 19,  26,  58,  60,  55,
  14, 13, 16, 24,  40,  57,  69,  56,  14, 17, 22, 29,  51,  87,  80,  62,
  18, 22, 37, 56,  68, 109, 103,  77,  24, 35, 55, 64,  81, 104, 113,  92,
  49, 64, 78, 87, 103, 121, 120, 101,  72, 92, 95, 98, 112, 100, 103,  99
};
static const uint8_t kQChr[64] = {
  17, 18, 24, 47, 99, 99, 99, 99,  18, 21, 26, 66, 99, 99, 99, 99,
  24, 26, 56, 99, 99, 99, 99, 99,  47, 66, 99, 99, 99, 99, 99, 99,
  99, 99, 99, 99, 99, 99, 99, 99,  99, 99, 99, 99, 99, 99, 99, 99,
  99, 99, 99, 99, 99, 99, 99, 99,  99, 99, 99, 99, 99, 99, 99, 99
};

// ---- Tablas Huffman estandar (anexo K.3) ----
static const uint8_t kDcLumBits[16] = { 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
static const uint8_t kDcChrBits[16] = { 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 };
static const uint8_t kDcVals[12]    = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
static const uint8_t kAcLumBits[16] = { 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d };
static const uint8_t kAcLumVals[162] = {
  0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
  0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
  0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
  0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
  0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
  0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
  0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
  0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
  0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
  0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
  0xf9, 0xfa
};
static const uint8_t kAcChrBits[16] = { 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 };
static const uint8_t kAcChrVals[162] = {
  0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
  0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
  0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
  0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
  0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
  0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
  0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
  0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
  0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
  0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
  0xf9, 0xfa
};

// Factores de escala del DCT de Arai-Agui-Nakajima: cos(k*pi/16)*sqrt(2), k>0.
static const float kAan[8] = {
  1.0f, 1.387039845f, 1.306562965f, 1.175875602f, 1.0f, 0.785694958f, 0.541196100f, 0.275899379f
};

#define FLEXJE_OBUF 4096

struct HuffEnc { uint16_t code[256]; uint8_t len[256]; };

struct JEnc {
  FlexJeOutFn out; void* octx;
  uint8_t* obuf; size_t on;
  bool     werr;
  uint32_t bitBuf; int bitCnt;
  HuffEnc  dcL, acL, dcC, acC;
  uint8_t  qL[64], qC[64];      // natural
  float    fdL[64], fdC[64];    // divisores con la escala AAN (natural)
  int      dcPred[3];
};

static void huffBuild(HuffEnc* h, const uint8_t* bits, const uint8_t* vals){
  memset(h, 0, sizeof(*h));
  uint32_t code = 0; int k = 0;
  for(int l = 1; l <= 16; l++){
    for(int i = 0; i < bits[l - 1]; i++){ h->code[vals[k]] = (uint16_t)code; h->len[vals[k]] = (uint8_t)l; code++; k++; }
    code <<= 1;
  }
}

static void quantScale(const uint8_t* base, int quality, uint8_t* out){
  if(quality < 1) quality = 1;
  if(quality > 100) quality = 100;
  int scale = quality < 50 ? 5000 / quality : 200 - quality * 2;
  for(int i = 0; i < 64; i++){
    long t = ((long)base[i] * scale + 50) / 100;
    if(t < 1) t = 1;
    if(t > 255) t = 255;
    out[i] = (uint8_t)t;
  }
}
static void divisors(const uint8_t* q, float* fd){
  for(int r = 0; r < 8; r++)
    for(int c = 0; c < 8; c++)
      fd[r * 8 + c] = 1.0f / ((float)q[r * 8 + c] * kAan[r] * kAan[c] * 8.0f);
}

// ---- salida ----
static void flushOut(JEnc* e){
  if(e->on && !e->werr && !e->out(e->octx, e->obuf, e->on)) e->werr = true;
  e->on = 0;
}
static inline void putByte(JEnc* e, uint8_t b){
  if(e->on >= FLEXJE_OBUF) flushOut(e);
  e->obuf[e->on++] = b;
}
static void put16(JEnc* e, unsigned v){ putByte(e, (uint8_t)(v >> 8)); putByte(e, (uint8_t)v); }
// MSB primero, con relleno 0xFF -> 0xFF 0x00. len <= 16: el acumulador no
// pasa nunca de 7 + 16 bits.
static inline void putBits(JEnc* e, uint32_t code, int len){
  if(len <= 0) return;
  e->bitBuf = (e->bitBuf << len) | (code & ((1u << len) - 1u));
  e->bitCnt += len;
  while(e->bitCnt >= 8){
    uint8_t b = (uint8_t)(e->bitBuf >> (e->bitCnt - 8));
    putByte(e, b);
    if(b == 0xFF) putByte(e, 0x00);
    e->bitCnt -= 8;
  }
  e->bitBuf &= (1u << e->bitCnt) - 1u;
}
static void flushBits(JEnc* e){
  if(e->bitCnt > 0) putBits(e, 0x7F, 8 - e->bitCnt);     // relleno con unos
  e->bitBuf = 0; e->bitCnt = 0;
}

// ---- DCT directa (AAN, coma flotante: el P4 tiene FPU de simple precision) ----
static void fdct8x8(float* d){
  for(int pass = 0; pass < 2; pass++){
    for(int i = 0; i < 8; i++){
      float* p = pass == 0 ? d + i * 8 : d + i;
      const int s = pass == 0 ? 1 : 8;
      float t0 = p[0 * s] + p[7 * s], t7 = p[0 * s] - p[7 * s];
      float t1 = p[1 * s] + p[6 * s], t6 = p[1 * s] - p[6 * s];
      float t2 = p[2 * s] + p[5 * s], t5 = p[2 * s] - p[5 * s];
      float t3 = p[3 * s] + p[4 * s], t4 = p[3 * s] - p[4 * s];
      float t10 = t0 + t3, t13 = t0 - t3, t11 = t1 + t2, t12 = t1 - t2;
      p[0 * s] = t10 + t11;
      p[4 * s] = t10 - t11;
      float z1 = (t12 + t13) * 0.707106781f;
      p[2 * s] = t13 + z1;
      p[6 * s] = t13 - z1;
      t10 = t4 + t5; t11 = t5 + t6; t12 = t6 + t7;
      float z5 = (t10 - t12) * 0.382683433f;
      float z2 = 0.541196100f * t10 + z5;
      float z4 = 1.306562965f * t12 + z5;
      float z3 = t11 * 0.707106781f;
      float z11 = t7 + z3, z13 = t7 - z3;
      p[5 * s] = z13 + z2;
      p[3 * s] = z13 - z2;
      p[1 * s] = z11 + z4;
      p[7 * s] = z11 - z4;
    }
  }
}

static inline int nbitsOf(int v){ if(v < 0) v = -v; int n = 0; while(v){ n++; v >>= 1; } return n; }

// Codifica un bloque de 64 muestras YA desplazadas (-128), orden natural.
static void encodeBlock(JEnc* e, float* blk, const float* fd, int* dcPred,
                        const HuffEnc* dc, const HuffEnc* ac){
  fdct8x8(blk);
  int q[64];
  for(int i = 0; i < 64; i++){
    float v = blk[i] * fd[i];
    int r = (int)(v < 0 ? v - 0.5f : v + 0.5f);
    // Baseline de 8 bits: un coeficiente AC cabe en 10 bits de magnitud.
    if(r > 1023) r = 1023;
    if(r < -1023) r = -1023;
    q[i] = r;
  }
  int diff = q[0] - *dcPred;
  *dcPred = q[0];
  int nb = nbitsOf(diff);
  putBits(e, dc->code[nb], dc->len[nb]);
  if(nb) putBits(e, (uint32_t)(diff < 0 ? diff - 1 : diff), nb);
  int run = 0;
  for(int k = 1; k < 64; k++){
    int v = q[kNat[k]];
    if(v == 0){ run++; continue; }
    while(run > 15){ putBits(e, ac->code[0xF0], ac->len[0xF0]); run -= 16; }
    nb = nbitsOf(v);
    int sym = (run << 4) | nb;
    putBits(e, ac->code[sym], ac->len[sym]);
    putBits(e, (uint32_t)(v < 0 ? v - 1 : v), nb);
    run = 0;
  }
  if(run > 0) putBits(e, ac->code[0x00], ac->len[0x00]);   // EOB
}

// ---- cabeceras ----
static void writeDqt(JEnc* e, int id, const uint8_t* q){
  putByte(e, (uint8_t)id);
  for(int k = 0; k < 64; k++) putByte(e, q[kNat[k]]);
}
static void writeDht(JEnc* e, int cls, int id, const uint8_t* bits, const uint8_t* vals){
  int n = 0; for(int i = 0; i < 16; i++) n += bits[i];
  putByte(e, (uint8_t)((cls << 4) | id));
  for(int i = 0; i < 16; i++) putByte(e, bits[i]);
  for(int i = 0; i < n; i++) putByte(e, vals[i]);
}
static int dhtLen(const uint8_t* bits){ int n = 0; for(int i = 0; i < 16; i++) n += bits[i]; return 17 + n; }

static void writeHeaders(JEnc* e, int w, int h, int sub){
  const bool gray = (sub == FLEXJE_GRAY);
  putByte(e, 0xFF); putByte(e, 0xD8);                       // SOI
  // APP0 JFIF 1.01, pixel cuadrado.
  static const uint8_t app0[] = { 0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0x00, 0x01, 0x01,
                                  0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00 };
  for(size_t i = 0; i < sizeof(app0); i++) putByte(e, app0[i]);
  // DQT
  putByte(e, 0xFF); putByte(e, 0xDB); put16(e, 2 + 65 * (gray ? 1 : 2));
  writeDqt(e, 0, e->qL);
  if(!gray) writeDqt(e, 1, e->qC);
  // SOF0
  int nc = gray ? 1 : 3;
  putByte(e, 0xFF); putByte(e, 0xC0); put16(e, 8 + 3 * nc);
  putByte(e, 8); put16(e, (unsigned)h); put16(e, (unsigned)w); putByte(e, (uint8_t)nc);
  putByte(e, 1); putByte(e, gray ? 0x11 : (sub == FLEXJE_SUB_420 ? 0x22 : 0x11)); putByte(e, 0);
  if(!gray){
    putByte(e, 2); putByte(e, 0x11); putByte(e, 1);
    putByte(e, 3); putByte(e, 0x11); putByte(e, 1);
  }
  // DHT
  int len = 2 + dhtLen(kDcLumBits) + dhtLen(kAcLumBits);
  if(!gray) len += dhtLen(kDcChrBits) + dhtLen(kAcChrBits);
  putByte(e, 0xFF); putByte(e, 0xC4); put16(e, (unsigned)len);
  writeDht(e, 0, 0, kDcLumBits, kDcVals);
  writeDht(e, 1, 0, kAcLumBits, kAcLumVals);
  if(!gray){
    writeDht(e, 0, 1, kDcChrBits, kDcVals);
    writeDht(e, 1, 1, kAcChrBits, kAcChrVals);
  }
  // SOS
  putByte(e, 0xFF); putByte(e, 0xDA); put16(e, 6 + 2 * nc); putByte(e, (uint8_t)nc);
  putByte(e, 1); putByte(e, 0x00);
  if(!gray){ putByte(e, 2); putByte(e, 0x11); putByte(e, 3); putByte(e, 0x11); }
  putByte(e, 0); putByte(e, 63); putByte(e, 0);
}

// ---- conversion de color (JFIF, coma fija de 16 bits) ----
static inline uint8_t clamp8(int v){ return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }
static inline void rgbToYcc(int r, int g, int b, uint8_t* y, uint8_t* cb, uint8_t* cr){
  *y  = clamp8((19595 * r + 38470 * g + 7471 * b + 32768) >> 16);
  *cb = clamp8((-11058 * r - 21710 * g + 32768 * b + (128 << 16) + 32768) >> 16);
  *cr = clamp8((32768 * r - 27439 * g - 5329 * b + (128 << 16) + 32768) >> 16);
}

int flexJpegEncode(const FlexJeCfg* cfg, FlexJeSrcFn src, void* srcCtx,
                   FlexJeOutFn out, void* outCtx, FlexJpegAlloc af, FlexJpegFree ff){
  if(!cfg || !src || !out) return FLEXJE_ERR_ARG;
  const int w = cfg->width, h = cfg->height;
  if(w <= 0 || h <= 0 || w > FLEXJE_MAX_DIM || h > FLEXJE_MAX_DIM) return FLEXJE_ERR_ARG;
  const int sub = (cfg->subsampling == FLEXJE_SUB_444 || cfg->subsampling == FLEXJE_GRAY)
                  ? cfg->subsampling : FLEXJE_SUB_420;
  const bool gray = (sub == FLEXJE_GRAY);
  const int bpp = cfg->input == FLEXJE_IN_RGB565 ? 2 : 3;
  if(!af) af = malloc;
  if(!ff) ff = free;

  const int mcuW = (sub == FLEXJE_SUB_420) ? 16 : 8;
  const int mcuH = mcuW;
  const int mcusX = (w + mcuW - 1) / mcuW;
  const int wPad = mcusX * mcuW;

  // Todo lo de trabajo, al monton del llamante. Nada grande en la pila.
  JEnc* e = (JEnc*)af(sizeof(JEnc));
  uint8_t* obuf  = (uint8_t*)af(FLEXJE_OBUF);
  uint8_t* strip = (uint8_t*)af((size_t)mcuH * (size_t)w * (size_t)bpp);
  uint8_t* pY    = (uint8_t*)af((size_t)mcuH * (size_t)wPad);
  uint8_t* pCb   = gray ? NULL : (uint8_t*)af((size_t)mcuH * (size_t)wPad);
  uint8_t* pCr   = gray ? NULL : (uint8_t*)af((size_t)mcuH * (size_t)wPad);
  int rc = FLEXJE_OK;
  if(!e || !obuf || !strip || !pY || (!gray && (!pCb || !pCr))){ rc = FLEXJE_ERR_MEMORY; goto done; }

  memset(e, 0, sizeof(*e));
  e->out = out; e->octx = outCtx; e->obuf = obuf;
  huffBuild(&e->dcL, kDcLumBits, kDcVals);
  huffBuild(&e->acL, kAcLumBits, kAcLumVals);
  huffBuild(&e->dcC, kDcChrBits, kDcVals);
  huffBuild(&e->acC, kAcChrBits, kAcChrVals);
  quantScale(kQLum, cfg->quality, e->qL);
  quantScale(kQChr, cfg->quality, e->qC);
  divisors(e->qL, e->fdL);
  divisors(e->qC, e->fdC);
  writeHeaders(e, w, h, sub);

  {
    float blk[64];
    for(int y0 = 0; y0 < h && !e->werr; y0 += mcuH){
      int rows = h - y0 < mcuH ? h - y0 : mcuH;
      if(!src(srcCtx, y0, rows, strip)){ rc = FLEXJE_ERR_SOURCE; goto done; }
      // Banda -> planos Y/Cb/Cr a resolucion completa. Las filas y columnas
      // de relleno repiten el borde: es lo que evita el halo gris en el
      // borde de una imagen que no es multiplo de 8/16.
      for(int r = 0; r < mcuH; r++){
        const uint8_t* s = strip + (size_t)(r < rows ? r : rows - 1) * (size_t)w * (size_t)bpp;
        uint8_t* yy = pY + (size_t)r * wPad;
        uint8_t* cb = gray ? NULL : pCb + (size_t)r * wPad;
        uint8_t* cr = gray ? NULL : pCr + (size_t)r * wPad;
        for(int x = 0; x < wPad; x++){
          int sx = x < w ? x : w - 1;
          int R, G, B;
          if(bpp == 3){ const uint8_t* p = s + (size_t)sx * 3; R = p[0]; G = p[1]; B = p[2]; }
          else {
            uint16_t v = (uint16_t)(s[(size_t)sx * 2] | (s[(size_t)sx * 2 + 1] << 8));
            R = (v >> 11) & 31; G = (v >> 5) & 63; B = v & 31;
            R = (R << 3) | (R >> 2); G = (G << 2) | (G >> 4); B = (B << 3) | (B >> 2);
          }
          if(gray){ yy[x] = clamp8((19595 * R + 38470 * G + 7471 * B + 32768) >> 16); continue; }
          rgbToYcc(R, G, B, yy + x, cb + x, cr + x);
        }
      }
      for(int mx = 0; mx < mcusX && !e->werr; mx++){
        const int x0 = mx * mcuW;
        // Luminancia: 1 bloque (8x8) o 4 (16x16).
        const int nb = (sub == FLEXJE_SUB_420) ? 2 : 1;
        for(int by = 0; by < nb; by++)
          for(int bx = 0; bx < nb; bx++){
            for(int r = 0; r < 8; r++){
              const uint8_t* p = pY + (size_t)(by * 8 + r) * wPad + x0 + bx * 8;
              for(int c = 0; c < 8; c++) blk[r * 8 + c] = (float)p[c] - 128.0f;
            }
            encodeBlock(e, blk, e->fdL, &e->dcPred[0], &e->dcL, &e->acL);
          }
        if(gray) continue;
        // Crominancia: 4:4:4 directa; 4:2:0 como media de 2x2.
        for(int ch = 0; ch < 2; ch++){
          const uint8_t* pl = ch == 0 ? pCb : pCr;
          for(int r = 0; r < 8; r++)
            for(int c = 0; c < 8; c++){
              if(sub == FLEXJE_SUB_420){
                const uint8_t* p = pl + (size_t)(r * 2) * wPad + x0 + c * 2;
                int s = p[0] + p[1] + p[wPad] + p[wPad + 1];
                blk[r * 8 + c] = (float)((s + 2) >> 2) - 128.0f;
              } else {
                blk[r * 8 + c] = (float)pl[(size_t)r * wPad + x0 + c] - 128.0f;
              }
            }
          encodeBlock(e, blk, e->fdC, &e->dcPred[1 + ch], &e->dcC, &e->acC);
        }
      }
    }
  }
  if(!e->werr){
    flushBits(e);
    putByte(e, 0xFF); putByte(e, 0xD9);                      // EOI
    flushOut(e);
  }
  if(e->werr) rc = FLEXJE_ERR_WRITE;

done:
  if(pCr) ff(pCr);
  if(pCb) ff(pCb);
  if(pY) ff(pY);
  if(strip) ff(strip);
  if(obuf) ff(obuf);
  if(e) ff(e);
  return rc;
}

struct MemSrc { const uint8_t* px; size_t stride; size_t rowBytes; };
static bool memSrcRows(void* c, int y, int rows, uint8_t* dst){
  MemSrc* m = (MemSrc*)c;
  for(int r = 0; r < rows; r++)
    memcpy(dst + (size_t)r * m->rowBytes, m->px + (size_t)(y + r) * m->stride, m->rowBytes);
  return true;
}

int flexJpegEncodeMem(const FlexJeCfg* cfg, const void* pixels, size_t stride,
                      FlexJeOutFn out, void* outCtx, FlexJpegAlloc af, FlexJpegFree ff){
  if(!cfg || !pixels) return FLEXJE_ERR_ARG;
  MemSrc m;
  m.px = (const uint8_t*)pixels;
  m.rowBytes = (size_t)cfg->width * (cfg->input == FLEXJE_IN_RGB565 ? 2u : 3u);
  m.stride = stride ? stride : m.rowBytes;
  if(m.stride < m.rowBytes) return FLEXJE_ERR_ARG;
  return flexJpegEncode(cfg, memSrcRows, &m, out, outCtx, af, ff);
}

const char* flexJpegEncErrStr(int err){
  switch(err){
    case FLEXJE_OK:         return "OK";
    case FLEXJE_ERR_ARG:    return "Parametros no validos";
    case FLEXJE_ERR_MEMORY: return "Sin memoria para codificar";
    case FLEXJE_ERR_WRITE:  return "No se pudo escribir la imagen";
    case FLEXJE_ERR_SOURCE: return "La imagen de origen no se pudo leer";
  }
  return "Error";
}
