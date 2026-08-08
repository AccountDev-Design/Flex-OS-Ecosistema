// #############################################################
// ##  FlexOS_JPEG.cpp  ·  DECODIFICADOR JPEG BASELINE
// ##  Ver FlexOS_JPEG.h para el alcance exacto (que soporta y que
// ##  rechaza) y para el porque de escribirlo en vez de enlazar
// ##  una biblioteca.
// #############################################################
//
//  COMO ESTA ORGANIZADO
//  --------------------
//    1) Lector de bits con des-relleno 0xFF00 y deteccion de
//       marcadores (nunca lee mas alla del buffer).
//    2) Tablas Huffman: derivacion segun el Anexo F del estandar
//       mas una tabla de consulta de 8 bits que resuelve de un
//       golpe los codigos cortos (que son la inmensa mayoria).
//    3) IDCT entera 8x8 separable en coma fija de 13 bits. Es el
//       algoritmo clasico "islow": exacto dentro de la tolerancia
//       del estandar y sin una sola operacion en coma flotante,
//       que es lo que importa en el ESP32 clasico (sin FPU util
//       para esto) y lo que mantiene el coste predecible.
//    4) Bucle de MCU: decodifica una FILA DE MCU completa a los
//       buffers de componente y despues emite las filas RGB565 ya
//       escaladas. Nunca existe la imagen entera en memoria.
//
//  DECISION DE ESCALADO: en vez de cuatro IDCT distintas (1x1, 2x2,
//  4x4, 8x8) se hace SIEMPRE la IDCT completa y despues un promedio
//  de caja NxN al emitir. Cuesta algo mas de CPU en 1/4 y 1/8, pero
//  es un unico camino de codigo que se puede probar de verdad, y en
//  el caso que de verdad importa -- la pagina a 1:1 -- no cuesta
//  absolutamente nada porque S=1 tiene su propio camino directo.

#include "FlexOS_JPEG.h"
#include <string.h>
#include <stdlib.h>

// -------------------------------------------------------------
//  Utilidades
// -------------------------------------------------------------
#define JPG_DCTSIZE 64

static const uint8_t kZigZag[64] = {
   0,  1,  8, 16,  9,  2,  3, 10,
  17, 24, 32, 25, 18, 11,  4,  5,
  12, 19, 26, 33, 40, 48, 41, 34,
  27, 20, 13,  6,  7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36,
  29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46,
  53, 60, 61, 54, 47, 55, 62, 63
};

static inline int jclamp255(int v){ return v < 0 ? 0 : (v > 255 ? 255 : v); }
static inline uint16_t jrgb565(int r, int g, int b){
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// -------------------------------------------------------------
//  1) Lector de bits
// -------------------------------------------------------------
typedef struct {
  const uint8_t* p;
  const uint8_t* end;
  uint32_t bitBuf;
  int      bitCnt;
  bool     marker;      // se topo con un marcador: a partir de aqui se rellena con ceros
  uint8_t  markerVal;
  bool     truncated;   // se acabo el buffer sin EOI
} BitRdr;

static void brInit(BitRdr* br, const uint8_t* p, const uint8_t* end){
  br->p = p; br->end = end;
  br->bitBuf = 0; br->bitCnt = 0;
  br->marker = false; br->markerVal = 0; br->truncated = false;
}

static void brFill(BitRdr* br){
  while(br->bitCnt <= 24){
    uint8_t b = 0;
    if(!br->marker){
      if(br->p >= br->end){ br->truncated = true; br->marker = true; br->markerVal = 0xD9; }
      else {
        b = *br->p++;
        if(b == 0xFF){
          uint8_t b2 = (br->p < br->end) ? *br->p : 0xD9;
          if(b2 == 0x00){
            br->p++;                       // relleno de byte: el 0xFF es dato
          } else {
            br->p--;                       // deja p sobre el 0xFF del marcador
            br->marker = true; br->markerVal = b2; b = 0;
          }
        }
      }
    }
    br->bitBuf = (br->bitBuf << 8) | b;
    br->bitCnt += 8;
  }
}

static inline int brGetBits(BitRdr* br, int n){
  if(n <= 0) return 0;
  if(br->bitCnt < n) brFill(br);
  int v = (int)((br->bitBuf >> (br->bitCnt - n)) & ((1u << n) - 1u));
  br->bitCnt -= n;
  return v;
}

static inline int brPeek8(BitRdr* br){
  if(br->bitCnt < 8) brFill(br);
  return (int)((br->bitBuf >> (br->bitCnt - 8)) & 0xFF);
}

// Salta al siguiente marcador RSTn y limpia el estado de bits. El
// codificador rellena con unos el ultimo byte antes del marcador, asi
// que los bits que quedaran en el buffer son basura y hay que tirarlos.
static bool brRestart(BitRdr* br){
  br->bitBuf = 0; br->bitCnt = 0;
  int guard = 0;
  while(br->p + 1 < br->end && guard < 256){
    if(br->p[0] == 0xFF && br->p[1] >= 0xD0 && br->p[1] <= 0xD7){
      br->p += 2; br->marker = false; br->markerVal = 0; return true;
    }
    br->p++; guard++;
  }
  return false;
}

// -------------------------------------------------------------
//  2) Tablas Huffman
// -------------------------------------------------------------
typedef struct {
  bool     present;
  uint8_t  vals[256];
  int32_t  mincode[17];
  int32_t  maxcode[18];   // -1 = no hay codigos de esa longitud
  int16_t  valptr[17];
  uint8_t  lutLen[256];   // 0 = no resuelto por la tabla rapida
  uint8_t  lutVal[256];
} HuffTbl;

static void huffBuild(HuffTbl* h, const uint8_t* bits /*17*/, const uint8_t* vals, int nvals){
  memcpy(h->vals, vals, (size_t)nvals);
  memset(h->lutLen, 0, sizeof(h->lutLen));

  int code = 0, k = 0;
  for(int l = 1; l <= 16; l++){
    h->valptr[l] = (int16_t)k;
    h->mincode[l] = code;
    int n = bits[l];
    if(n == 0){ h->maxcode[l] = -1; code <<= 1; continue; }
    // Tabla rapida: todos los codigos de <= 8 bits se resuelven sin bucle.
    if(l <= 8){
      for(int i = 0; i < n; i++){
        int c = code + i;
        int lo = c << (8 - l), hi = lo + (1 << (8 - l));
        for(int idx = lo; idx < hi && idx < 256; idx++){
          h->lutLen[idx] = (uint8_t)l;
          h->lutVal[idx] = vals[k + i];
        }
      }
    }
    k += n; code += n;
    h->maxcode[l] = code - 1;
    code <<= 1;
  }
  h->maxcode[17] = 0x7FFFFFFF;
  h->present = true;
}

static int huffDecode(BitRdr* br, const HuffTbl* h){
  int look = brPeek8(br);
  uint8_t l = h->lutLen[look];
  if(l){ br->bitCnt -= l; return h->lutVal[look]; }
  int code = 0;
  for(int len = 1; len <= 16; len++){
    code = (code << 1) | brGetBits(br, 1);
    if(h->maxcode[len] >= 0 && code <= h->maxcode[len])
      return h->vals[h->valptr[len] + (code - h->mincode[len])];
  }
  return -1;   // codigo invalido -> datos corruptos
}

static inline int huffExtend(int v, int t){
  return (t == 0) ? 0 : (v < (1 << (t - 1)) ? v - (1 << t) + 1 : v);
}

// -------------------------------------------------------------
//  3) IDCT entera 8x8 (coma fija 13 bits, algoritmo "islow")
// -------------------------------------------------------------
#define CONST_BITS 13
#define PASS1_BITS 2
#define FIX_0_298631336  2446
#define FIX_0_390180644  3196
#define FIX_0_541196100  4433
#define FIX_0_765366865  6270
#define FIX_0_899976223  7373
#define FIX_1_175875602  9633
#define FIX_1_501321110 12299
#define FIX_1_847759065 15137
#define FIX_1_961570560 16069
#define FIX_2_053119869 16819
#define FIX_2_562915447 20995
#define FIX_3_072711026 25172
#define JDESCALE(x, n)  (((x) + (1 << ((n) - 1))) >> (n))

// coef: 64 coeficientes en orden NATURAL. quant: 64 valores en orden
// NATURAL. out: destino de 8x8 muestras (0..255) con paso `stride`.
static void idct8x8(const int16_t* coef, const uint16_t* quant,
                    uint8_t* out, int stride){
  int32_t ws[64];

  // ---- Pase 1: columnas ----
  for(int c = 0; c < 8; c++){
    const int16_t* in = coef + c;
    const uint16_t* q = quant + c;
    if(in[8] == 0 && in[16] == 0 && in[24] == 0 && in[32] == 0 &&
       in[40] == 0 && in[48] == 0 && in[56] == 0){
      // Multiplicacion y no desplazamiento: in[0] puede ser NEGATIVO y
      // desplazar a la izquierda un negativo es comportamiento indefinido
      // en C++ (lo detecta -fsanitize=undefined). El compilador genera el
      // mismo shift para una potencia de dos.
      int32_t dc = ((int32_t)in[0] * q[0]) * (1 << PASS1_BITS);
      for(int r = 0; r < 8; r++) ws[r * 8 + c] = dc;
      continue;
    }
    int32_t z1, z2, z3, z4, z5, t0, t1, t2, t3, t10, t11, t12, t13;

    z2 = (int32_t)in[16] * q[16];
    z3 = (int32_t)in[48] * q[48];
    z1 = (z2 + z3) * FIX_0_541196100;
    t2 = z1 + z3 * (-FIX_1_847759065);
    t3 = z1 + z2 * FIX_0_765366865;

    z2 = (int32_t)in[0]  * q[0];
    z3 = (int32_t)in[32] * q[32];
    t0 = (z2 + z3) * (1 << CONST_BITS);
    t1 = (z2 - z3) * (1 << CONST_BITS);

    t10 = t0 + t3; t13 = t0 - t3;
    t11 = t1 + t2; t12 = t1 - t2;

    t0 = (int32_t)in[56] * q[56];
    t1 = (int32_t)in[40] * q[40];
    t2 = (int32_t)in[24] * q[24];
    t3 = (int32_t)in[8]  * q[8];

    z1 = t0 + t3; z2 = t1 + t2; z3 = t0 + t2; z4 = t1 + t3;
    z5 = (z3 + z4) * FIX_1_175875602;

    t0 *= FIX_0_298631336; t1 *= FIX_2_053119869;
    t2 *= FIX_3_072711026; t3 *= FIX_1_501321110;
    z1 *= -FIX_0_899976223; z2 *= -FIX_2_562915447;
    z3 *= -FIX_1_961570560; z4 *= -FIX_0_390180644;
    z3 += z5; z4 += z5;
    t0 += z1 + z3; t1 += z2 + z4; t2 += z2 + z3; t3 += z1 + z4;

    ws[0 * 8 + c] = JDESCALE(t10 + t3, CONST_BITS - PASS1_BITS);
    ws[7 * 8 + c] = JDESCALE(t10 - t3, CONST_BITS - PASS1_BITS);
    ws[1 * 8 + c] = JDESCALE(t11 + t2, CONST_BITS - PASS1_BITS);
    ws[6 * 8 + c] = JDESCALE(t11 - t2, CONST_BITS - PASS1_BITS);
    ws[2 * 8 + c] = JDESCALE(t12 + t1, CONST_BITS - PASS1_BITS);
    ws[5 * 8 + c] = JDESCALE(t12 - t1, CONST_BITS - PASS1_BITS);
    ws[3 * 8 + c] = JDESCALE(t13 + t0, CONST_BITS - PASS1_BITS);
    ws[4 * 8 + c] = JDESCALE(t13 - t0, CONST_BITS - PASS1_BITS);
  }

  // ---- Pase 2: filas ----
  for(int r = 0; r < 8; r++){
    const int32_t* w = ws + r * 8;
    uint8_t* o = out + (size_t)r * stride;
    int32_t z1, z2, z3, z4, z5, t0, t1, t2, t3, t10, t11, t12, t13;

    z2 = w[2]; z3 = w[6];
    z1 = (z2 + z3) * FIX_0_541196100;
    t2 = z1 + z3 * (-FIX_1_847759065);
    t3 = z1 + z2 * FIX_0_765366865;

    t0 = (w[0] + w[4]) * (1 << CONST_BITS);
    t1 = (w[0] - w[4]) * (1 << CONST_BITS);

    t10 = t0 + t3; t13 = t0 - t3;
    t11 = t1 + t2; t12 = t1 - t2;

    t0 = w[7]; t1 = w[5]; t2 = w[3]; t3 = w[1];
    z1 = t0 + t3; z2 = t1 + t2; z3 = t0 + t2; z4 = t1 + t3;
    z5 = (z3 + z4) * FIX_1_175875602;

    t0 *= FIX_0_298631336; t1 *= FIX_2_053119869;
    t2 *= FIX_3_072711026; t3 *= FIX_1_501321110;
    z1 *= -FIX_0_899976223; z2 *= -FIX_2_562915447;
    z3 *= -FIX_1_961570560; z4 *= -FIX_0_390180644;
    z3 += z5; z4 += z5;
    t0 += z1 + z3; t1 += z2 + z4; t2 += z2 + z3; t3 += z1 + z4;

    o[0] = (uint8_t)jclamp255((int)JDESCALE(t10 + t3, CONST_BITS + PASS1_BITS + 3) + 128);
    o[7] = (uint8_t)jclamp255((int)JDESCALE(t10 - t3, CONST_BITS + PASS1_BITS + 3) + 128);
    o[1] = (uint8_t)jclamp255((int)JDESCALE(t11 + t2, CONST_BITS + PASS1_BITS + 3) + 128);
    o[6] = (uint8_t)jclamp255((int)JDESCALE(t11 - t2, CONST_BITS + PASS1_BITS + 3) + 128);
    o[2] = (uint8_t)jclamp255((int)JDESCALE(t12 + t1, CONST_BITS + PASS1_BITS + 3) + 128);
    o[5] = (uint8_t)jclamp255((int)JDESCALE(t12 - t1, CONST_BITS + PASS1_BITS + 3) + 128);
    o[3] = (uint8_t)jclamp255((int)JDESCALE(t13 + t0, CONST_BITS + PASS1_BITS + 3) + 128);
    o[4] = (uint8_t)jclamp255((int)JDESCALE(t13 - t0, CONST_BITS + PASS1_BITS + 3) + 128);
  }
}

// -------------------------------------------------------------
//  Estado del decodificador
// -------------------------------------------------------------
typedef struct {
  uint8_t  id, h, v, tq;
  uint8_t  td, ta;
  int      dcPred;
  uint8_t* pix;      // muestras de UNA fila de MCU
  int      stride;   // ancho del buffer en muestras
  int      rows;     // 8 * v
  int      shx, shy; // desplazamiento para pasar de coord. de imagen a coord. de este componente
} JComp;

typedef struct {
  uint16_t quant[4][64];        // ya des-zigzagueadas (orden natural)
  bool     quantSet[4];
  HuffTbl  hdc[4], hac[4];
  JComp    comp[FLEXJPG_MAX_COMPONENTS];
  int      ncomp;
  int      width, height;
  int      hmax, vmax;
  int      mcuW, mcuH, mcusX, mcusY;
  int      restartInterval;
  bool     progressive;
} JDec;

// -------------------------------------------------------------
//  Lectura de la cabecera
// -------------------------------------------------------------
static inline int rd16(const uint8_t* p){ return (p[0] << 8) | p[1]; }

// Recorre los segmentos hasta SOS. Devuelve el offset del primer byte
// de datos entropicos, o un codigo de error negativo.
static int jpegParseHeaders(JDec* d, const uint8_t* data, size_t len, bool headerOnly){
  size_t i = 0;
  if(len < 4 || data[0] != 0xFF || data[1] != 0xD8) return FLEXJPG_ERR_BADMARKER;
  i = 2;
  bool haveSOF = false;

  while(i + 1 < len){
    if(data[i] != 0xFF){ i++; continue; }                 // resincroniza sobre relleno
    uint8_t m = data[i + 1];
    i += 2;
    if(m == 0xFF){ i--; continue; }                        // 0xFF de relleno
    if(m == 0x01 || (m >= 0xD0 && m <= 0xD7)) continue;    // sin payload
    if(m == 0xD9) return FLEXJPG_ERR_TRUNCATED;            // EOI antes de SOS
    if(i + 2 > len) return FLEXJPG_ERR_TRUNCATED;
    int seglen = rd16(data + i);
    if(seglen < 2 || i + (size_t)seglen > len) return FLEXJPG_ERR_TRUNCATED;
    const uint8_t* seg = data + i + 2;
    int segn = seglen - 2;

    switch(m){
      case 0xC0: case 0xC1: {                              // SOF0 / SOF1 (baseline / ext. secuencial)
        if(segn < 6) return FLEXJPG_ERR_TRUNCATED;
        if(seg[0] != 8) return FLEXJPG_ERR_UNSUPPORTED;    // solo 8 bits por muestra
        d->height = rd16(seg + 1);
        d->width  = rd16(seg + 3);
        d->ncomp  = seg[5];
        if(d->width <= 0 || d->height <= 0) return FLEXJPG_ERR_BADMARKER;
        if(d->width > FLEXJPG_MAX_DIM || d->height > FLEXJPG_MAX_DIM) return FLEXJPG_ERR_TOOBIG;
        if(d->ncomp != 1 && d->ncomp != 3) return FLEXJPG_ERR_UNSUPPORTED;
        if(segn < 6 + 3 * d->ncomp) return FLEXJPG_ERR_TRUNCATED;
        d->hmax = d->vmax = 1;
        for(int c = 0; c < d->ncomp; c++){
          const uint8_t* cp = seg + 6 + 3 * c;
          d->comp[c].id = cp[0];
          d->comp[c].h  = (uint8_t)(cp[1] >> 4);
          d->comp[c].v  = (uint8_t)(cp[1] & 0x0F);
          d->comp[c].tq = cp[2];
          if(d->comp[c].h != 1 && d->comp[c].h != 2) return FLEXJPG_ERR_UNSUPPORTED;
          if(d->comp[c].v != 1 && d->comp[c].v != 2) return FLEXJPG_ERR_UNSUPPORTED;
          if(d->comp[c].tq > 3) return FLEXJPG_ERR_BADMARKER;
          if(d->comp[c].h > d->hmax) d->hmax = d->comp[c].h;
          if(d->comp[c].v > d->vmax) d->vmax = d->comp[c].v;
        }
        haveSOF = true;
        if(headerOnly) return (int)(i + seglen);
        break;
      }
      case 0xC2:                                           // SOF2 = progresivo
        d->progressive = true;
        // Se leen las dimensiones para poder informar, pero no se decodifica.
        if(segn >= 6){ d->height = rd16(seg + 1); d->width = rd16(seg + 3); d->ncomp = seg[5]; }
        return FLEXJPG_ERR_UNSUPPORTED;
      case 0xC3: case 0xC5: case 0xC6: case 0xC7:          // jerarquico / sin perdidas
      case 0xC9: case 0xCA: case 0xCB: case 0xCD: case 0xCE: case 0xCF:
        return FLEXJPG_ERR_UNSUPPORTED;

      case 0xC4: {                                         // DHT
        int p = 0;
        while(p < segn){
          if(p + 17 > segn) return FLEXJPG_ERR_TRUNCATED;
          int tc = seg[p] >> 4, th = seg[p] & 0x0F;
          if(tc > 1 || th > 3) return FLEXJPG_ERR_BADMARKER;
          uint8_t bits[17]; bits[0] = 0;
          int total = 0;
          for(int l = 1; l <= 16; l++){ bits[l] = seg[p + l]; total += bits[l]; }
          if(total > 256 || p + 17 + total > segn) return FLEXJPG_ERR_TRUNCATED;
          huffBuild(tc == 0 ? &d->hdc[th] : &d->hac[th], bits, seg + p + 17, total);
          p += 17 + total;
        }
        break;
      }
      case 0xDB: {                                         // DQT
        int p = 0;
        while(p < segn){
          int pq = seg[p] >> 4, tq = seg[p] & 0x0F;
          if(tq > 3) return FLEXJPG_ERR_BADMARKER;
          p++;
          int n = pq ? 128 : 64;
          if(p + n > segn) return FLEXJPG_ERR_TRUNCATED;
          for(int k = 0; k < 64; k++){
            int v = pq ? rd16(seg + p + 2 * k) : seg[p + k];
            d->quant[tq][kZigZag[k]] = (uint16_t)v;
          }
          d->quantSet[tq] = true;
          p += n;
        }
        break;
      }
      case 0xDD:                                           // DRI
        if(segn < 2) return FLEXJPG_ERR_TRUNCATED;
        d->restartInterval = rd16(seg);
        break;

      case 0xDA: {                                         // SOS -> empiezan los datos
        if(!haveSOF) return FLEXJPG_ERR_BADMARKER;
        if(segn < 1) return FLEXJPG_ERR_TRUNCATED;
        int ns = seg[0];
        if(ns != d->ncomp) return FLEXJPG_ERR_UNSUPPORTED;  // escaneos no entrelazados: fuera de alcance
        if(segn < 1 + 2 * ns + 3) return FLEXJPG_ERR_TRUNCATED;
        for(int s = 0; s < ns; s++){
          int cid = seg[1 + 2 * s], tt = seg[2 + 2 * s];
          int ci = -1;
          for(int c = 0; c < d->ncomp; c++) if(d->comp[c].id == cid){ ci = c; break; }
          if(ci < 0) return FLEXJPG_ERR_BADMARKER;
          d->comp[ci].td = (uint8_t)(tt >> 4);
          d->comp[ci].ta = (uint8_t)(tt & 0x0F);
          if(d->comp[ci].td > 3 || d->comp[ci].ta > 3) return FLEXJPG_ERR_BADMARKER;
        }
        return (int)(i + seglen);
      }
      default:
        break;                                             // APPn, COM, etc: se ignoran
    }
    i += seglen;
  }
  return FLEXJPG_ERR_TRUNCATED;
}

// -------------------------------------------------------------
//  API: solo cabecera
// -------------------------------------------------------------
int flexJpegProbe(const uint8_t* data, size_t len, FlexJpegInfo* info){
  if(!data || len < 4 || !info) return FLEXJPG_ERR_ARG;
  JDec d; memset(&d, 0, sizeof(d));
  int r = jpegParseHeaders(&d, data, len, true);
  memset(info, 0, sizeof(*info));
  info->progressive = d.progressive;
  info->width = d.width; info->height = d.height;
  info->outWidth = d.width; info->outHeight = d.height;
  info->components = d.ncomp; info->scaleDenom = 1;
  return r < 0 ? r : FLEXJPG_OK;
}

// -------------------------------------------------------------
//  API: decodificacion completa
// -------------------------------------------------------------
static void* jdefAlloc(size_t n){ return malloc(n); }
static void  jdefFree(void* p){ free(p); }

int flexJpegDecode(const uint8_t* data, size_t len,
                   int maxW, int maxH, uint32_t maxPixels,
                   FlexJpegInfo* infoOut,
                   FlexJpegRowCb cb, void* user,
                   FlexJpegAlloc af, FlexJpegFree ff){
  if(!data || len < 4 || !cb) return FLEXJPG_ERR_ARG;
  if(!af) af = jdefAlloc;
  if(!ff) ff = jdefFree;

  JDec d; memset(&d, 0, sizeof(d));
  int sosEnd = jpegParseHeaders(&d, data, len, false);
  if(sosEnd < 0) return sosEnd;

  // ---- Divisor de escala: el menor que hace que quepa ----
  int S = 1;
  for(;;){
    int ow = (d.width + S - 1) / S, oh = (d.height + S - 1) / S;
    bool fits = true;
    if(maxW > 0 && ow > maxW) fits = false;
    if(maxH > 0 && oh > maxH) fits = false;
    if(maxPixels && (uint32_t)ow * (uint32_t)oh > maxPixels) fits = false;
    if(fits) break;
    if(S >= 8) return FLEXJPG_ERR_TOOBIG;
    S <<= 1;
  }
  int outW = (d.width + S - 1) / S;
  int outH = (d.height + S - 1) / S;

  for(int c = 0; c < d.ncomp; c++)
    if(!d.quantSet[d.comp[c].tq]) return FLEXJPG_ERR_BADMARKER;

  d.mcuW  = 8 * d.hmax;  d.mcuH = 8 * d.vmax;
  d.mcusX = (d.width  + d.mcuW - 1) / d.mcuW;
  d.mcusY = (d.height + d.mcuH - 1) / d.mcuH;

  if(infoOut){
    infoOut->width = d.width; infoOut->height = d.height;
    infoOut->outWidth = outW; infoOut->outHeight = outH;
    infoOut->components = d.ncomp; infoOut->scaleDenom = S;
    infoOut->progressive = false;
  }

  // ---- Buffers de trabajo ----
  // Un buffer de muestras por componente que cubre UNA fila de MCU
  // completa, mas una fila de salida RGB565. Nada mas: no existe en
  // ningun momento la imagen entera descomprimida.
  uint8_t*  planes[FLEXJPG_MAX_COMPONENTS] = { 0, 0, 0 };
  uint16_t* outRow = NULL;
  int rc = FLEXJPG_OK;

  for(int c = 0; c < d.ncomp; c++){
    d.comp[c].stride = d.mcusX * 8 * d.comp[c].h;
    d.comp[c].rows   = 8 * d.comp[c].v;
    d.comp[c].shx    = (d.hmax / d.comp[c].h) == 2 ? 1 : 0;
    d.comp[c].shy    = (d.vmax / d.comp[c].v) == 2 ? 1 : 0;
    size_t need = (size_t)d.comp[c].stride * (size_t)d.comp[c].rows;
    planes[c] = (uint8_t*)af(need);
    if(!planes[c]){ rc = FLEXJPG_ERR_MEMORY; goto done; }
    d.comp[c].pix = planes[c];
    d.comp[c].dcPred = 0;
  }
  outRow = (uint16_t*)af((size_t)outW * sizeof(uint16_t));
  if(!outRow){ rc = FLEXJPG_ERR_MEMORY; goto done; }

  {
    BitRdr br; brInit(&br, data + sosEnd, data + len);
    int16_t coef[64];
    int mcuSinceRestart = 0;
    const long totalMcus = (long)d.mcusX * (long)d.mcusY;

    for(int my = 0; my < d.mcusY; my++){
      // ---------- Decodifica una fila de MCU ----------
      for(int mx = 0; mx < d.mcusX; mx++){
        if(d.restartInterval && mcuSinceRestart == d.restartInterval){
          if(!brRestart(&br)){ rc = FLEXJPG_ERR_TRUNCATED; goto done; }
          for(int c = 0; c < d.ncomp; c++) d.comp[c].dcPred = 0;
          mcuSinceRestart = 0;
        }
        mcuSinceRestart++;

        for(int c = 0; c < d.ncomp; c++){
          JComp* cm = &d.comp[c];
          for(int by = 0; by < cm->v; by++){
            for(int bx = 0; bx < cm->h; bx++){
              memset(coef, 0, sizeof(coef));
              // DC
              int t = huffDecode(&br, &d.hdc[cm->td]);
              if(t < 0 || t > 16){ rc = FLEXJPG_ERR_HUFFMAN; goto done; }
              int diff = huffExtend(brGetBits(&br, t), t);
              cm->dcPred += diff;
              coef[0] = (int16_t)cm->dcPred;
              // AC
              for(int k = 1; k < 64; ){
                int rs = huffDecode(&br, &d.hac[cm->ta]);
                if(rs < 0){ rc = FLEXJPG_ERR_HUFFMAN; goto done; }
                int r = rs >> 4, s = rs & 15;
                if(s == 0){
                  if(r != 15) break;      // EOB
                  k += 16; continue;      // ZRL
                }
                k += r;
                if(k > 63) break;         // flujo corrupto: se corta el bloque
                coef[kZigZag[k]] = (int16_t)huffExtend(brGetBits(&br, s), s);
                k++;
              }
              uint8_t* dst = cm->pix
                           + (size_t)(by * 8) * cm->stride
                           + (size_t)(mx * cm->h + bx) * 8;
              idct8x8(coef, d.quant[cm->tq], dst, cm->stride);
            }
          }
        }
        // Se acabaron los bytes antes de tiempo: se corta con error en vez
        // de pintar el relleno de ceros como si fuera imagen. El lector de
        // bits va hasta 4 bytes por delante de lo consumido, y una MCU
        // cuesta siempre mas de 32 bits, asi que detectar el final durante
        // la ULTIMA MCU es normal y no se considera truncamiento.
        if(br.truncated && ((long)my * d.mcusX + mx) < totalMcus - 1){
          rc = FLEXJPG_ERR_TRUNCATED; goto done;
        }
      }

      // ---------- Emite las filas de salida de esta banda ----------
      int bandY0 = my * d.mcuH;                 // primera fila de imagen de la banda
      int rowsInBand = d.mcuH / S;              // S divide siempre a mcuH (8 o 16)
      for(int rIdx = 0; rIdx < rowsInBand; rIdx++){
        int oy = my * rowsInBand + rIdx;
        if(oy >= outH) break;
        int sy0 = oy * S - bandY0;              // fila dentro de la banda

        if(S == 1){
          const JComp* cy = &d.comp[0];
          const uint8_t* rowY = cy->pix + (size_t)sy0 * cy->stride;
          if(d.ncomp == 1){
            for(int ox = 0; ox < outW; ox++){
              int Y = rowY[ox];
              outRow[ox] = jrgb565(Y, Y, Y);
            }
          } else {
            const JComp* cb1 = &d.comp[1];
            const JComp* cr1 = &d.comp[2];
            const uint8_t* rowCb = cb1->pix + (size_t)(sy0 >> cb1->shy) * cb1->stride;
            const uint8_t* rowCr = cr1->pix + (size_t)(sy0 >> cr1->shy) * cr1->stride;
            int shb = cb1->shx, shr = cr1->shx;
            for(int ox = 0; ox < outW; ox++){
              int Y  = rowY[ox];
              int Cb = rowCb[ox >> shb] - 128;
              int Cr = rowCr[ox >> shr] - 128;
              // ITU-R BT.601 en coma fija 16 bits (sin flotantes).
              int r = Y + ((91881 * Cr) >> 16);
              int g = Y - ((22554 * Cb + 46802 * Cr) >> 16);
              int b = Y + ((116130 * Cb) >> 16);
              outRow[ox] = jrgb565(jclamp255(r), jclamp255(g), jclamp255(b));
            }
          }
        } else {
          // Promedio de caja SxS. Los bordes se recortan contra el tamano
          // real de la imagen para no promediar el relleno de la ultima MCU.
          int syLim = d.height - bandY0; if(syLim > d.mcuH) syLim = d.mcuH;
          for(int ox = 0; ox < outW; ox++){
            int sx0 = ox * S;
            int sumY = 0, sumCb = 0, sumCr = 0, n = 0;
            for(int dy = 0; dy < S; dy++){
              int sy = sy0 + dy;
              if(sy >= syLim) break;
              const JComp* cy = &d.comp[0];
              const uint8_t* rowY = cy->pix + (size_t)sy * cy->stride;
              const uint8_t* rowCb = NULL; const uint8_t* rowCr = NULL;
              int shb = 0, shr = 0;
              if(d.ncomp == 3){
                const JComp* c1 = &d.comp[1]; const JComp* c2 = &d.comp[2];
                rowCb = c1->pix + (size_t)(sy >> c1->shy) * c1->stride; shb = c1->shx;
                rowCr = c2->pix + (size_t)(sy >> c2->shy) * c2->stride; shr = c2->shx;
              }
              for(int dx = 0; dx < S; dx++){
                int sx = sx0 + dx;
                if(sx >= d.width) break;
                sumY += rowY[sx];
                if(rowCb){ sumCb += rowCb[sx >> shb]; sumCr += rowCr[sx >> shr]; }
                n++;
              }
            }
            if(n == 0) n = 1;
            int Y = sumY / n;
            if(d.ncomp == 1){ outRow[ox] = jrgb565(Y, Y, Y); continue; }
            int Cb = sumCb / n - 128, Cr = sumCr / n - 128;
            int r = Y + ((91881 * Cr) >> 16);
            int g = Y - ((22554 * Cb + 46802 * Cr) >> 16);
            int b = Y + ((116130 * Cb) >> 16);
            outRow[ox] = jrgb565(jclamp255(r), jclamp255(g), jclamp255(b));
          }
        }

        if(!cb(user, oy, outW, outRow)){ rc = FLEXJPG_ERR_ABORTED; goto done; }
      }
    }
  }

done:
  if(outRow) ff(outRow);
  for(int c = 0; c < FLEXJPG_MAX_COMPONENTS; c++) if(planes[c]) ff(planes[c]);
  return rc;
}

const char* flexJpegErrStr(int err){
  switch(err){
    case FLEXJPG_OK:              return "OK";
    case FLEXJPG_ERR_ARG:         return "parametros invalidos";
    case FLEXJPG_ERR_TRUNCATED:   return "imagen incompleta";
    case FLEXJPG_ERR_BADMARKER:   return "cabecera JPEG corrupta";
    case FLEXJPG_ERR_UNSUPPORTED: return "formato JPEG no soportado";
    case FLEXJPG_ERR_MEMORY:      return "sin memoria para decodificar";
    case FLEXJPG_ERR_TOOBIG:      return "imagen demasiado grande";
    case FLEXJPG_ERR_HUFFMAN:     return "datos de imagen corruptos";
    case FLEXJPG_ERR_ABORTED:     return "decodificacion cancelada";
    default:                      return "error desconocido";
  }
}
