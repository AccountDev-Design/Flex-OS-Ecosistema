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
//  0) De donde salen los bytes
//  ------------------------------------------------------------
//  Dos modos y UN solo decodificador:
//    · MEMORIA: los bytes estan enteros en un buffer (win == NULL). Es el
//      camino de siempre y se comporta exactamente igual que antes: la
//      "ventana" es el buffer entero y nunca se recarga.
//    · FLUJO: los bytes se piden a una funcion de lectura (un archivo de
//      LittleFS, un socket...) y viven en una ventana pequena que se va
//      rellenando. Asi se puede decodificar una foto de varios MB sin
//      tenerla entera en RAM: el pico de memoria es la ventana mas los
//      buffers de trabajo de siempre, no el tamano del archivo.
//  El lector de cabecera y el de bits solo miran [p, end) y piden mas con
//  jsNeed(); lo que hay debajo les da igual.
// -------------------------------------------------------------
#define JPG_STREAM_WIN  8192u      // ventana del modo flujo (cabe cualquier DHT/DQT/SOF/SOS real)

typedef struct {
  const uint8_t* p;          // proximo byte sin consumir
  const uint8_t* end;        // fin de lo disponible
  uint8_t*       win;        // ventana del modo flujo (NULL = modo memoria)
  size_t         winCap;
  FlexJpegReadFn rd;
  void*          rdCtx;
  bool           eof;        // el flujo ya no da mas (o fallo)
  bool           ioErr;      // el flujo devolvio un error de lectura
} JSrc;

static void jsInitMem(JSrc* s, const uint8_t* data, size_t len){
  s->p = data; s->end = data + len;
  s->win = NULL; s->winCap = 0; s->rd = NULL; s->rdCtx = NULL;
  s->eof = true; s->ioErr = false;
}
static void jsInitStream(JSrc* s, uint8_t* win, size_t cap, FlexJpegReadFn rd, void* ctx){
  s->p = win; s->end = win;
  s->win = win; s->winCap = cap; s->rd = rd; s->rdCtx = ctx;
  s->eof = false; s->ioErr = false;
}

// Deja al menos `n` bytes sin consumir en [p, end) si el origen los tiene.
// Devuelve cuantos hay de verdad (menos que n solo al final). En modo
// memoria no hay nada que recargar: devuelve lo que queda.
static size_t jsNeed(JSrc* s, size_t n){
  size_t have = (size_t)(s->end - s->p);
  if(have >= n || !s->win || s->eof) return have;
  if(n > s->winCap) n = s->winCap;
  // Lo pendiente se corre al principio de la ventana y el resto se llena.
  if(have && s->p != s->win) memmove(s->win, s->p, have);
  s->p = s->win;
  size_t used = have;
  while(used < n && !s->eof){
    int r = s->rd(s->rdCtx, s->win + used, s->winCap - used);
    if(r <= 0 || (size_t)r > s->winCap - used){ s->eof = true; if(r != 0) s->ioErr = true; break; }
    used += (size_t)r;
  }
  s->end = s->win + used;
  return used;
}
// Consume `n` bytes aunque no quepan en la ventana (segmentos que se ignoran).
static bool jsSkip(JSrc* s, size_t n){
  while(n){
    size_t have = jsNeed(s, 1);
    if(!have) return false;
    size_t k = have < n ? have : n;
    s->p += k; n -= k;
  }
  return true;
}

// -------------------------------------------------------------
//  1) Lector de bits
// -------------------------------------------------------------
typedef struct {
  JSrc*    s;
  uint32_t bitBuf;
  int      bitCnt;
  bool     marker;      // se topo con un marcador: a partir de aqui se rellena con ceros
  uint8_t  markerVal;
  bool     truncated;   // se acabo el buffer sin EOI
} BitRdr;

static void brInit(BitRdr* br, JSrc* s){
  br->s = s;
  br->bitBuf = 0; br->bitCnt = 0;
  br->marker = false; br->markerVal = 0; br->truncated = false;
}

// Mismo comportamiento que el lector de siempre, byte a byte: un 0xFF
// seguido de 0x00 es un 0xFF de datos; seguido de otra cosa es un
// marcador y el cursor se queda SOBRE el 0xFF. Se miran los dos bytes
// antes de consumir, asi que en modo flujo el 0xFF nunca se pierde en
// una recarga de la ventana.
static void brFill(BitRdr* br){
  while(br->bitCnt <= 24){
    uint8_t b = 0;
    if(!br->marker){
      JSrc* s = br->s;
      size_t have = (size_t)(s->end - s->p);
      if(have < 2 && s->win) have = jsNeed(s, 2);
      if(have == 0){ br->truncated = true; br->marker = true; br->markerVal = 0xD9; }
      else {
        b = s->p[0];
        if(b == 0xFF){
          uint8_t b2 = (have >= 2) ? s->p[1] : 0xD9;
          if(b2 == 0x00){
            s->p += 2;                     // relleno de byte: el 0xFF es dato
          } else {
            br->marker = true; br->markerVal = b2; b = 0;   // p se queda sobre el 0xFF del marcador
          }
        } else s->p++;
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
  JSrc* s = br->s;
  int guard = 0;
  for(;;){
    size_t have = (size_t)(s->end - s->p);
    if(have < 2 && s->win) have = jsNeed(s, 2);
    if(have < 2 || guard >= 256) break;
    if(s->p[0] == 0xFF && s->p[1] >= 0xD0 && s->p[1] <= 0xD7){
      s->p += 2; br->marker = false; br->markerVal = 0; return true;
    }
    s->p++; guard++;
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

// Tablas Huffman ESTANDAR (ITU-T T.81, anexo K.3). Un fotograma MJPEG de
// camara (el formato "AVI1" de las webcams USB, de muchas camaras IP y de la
// ESP32-CAM) NO lleva segmento DHT: el estandar de MJPEG da por hechas estas
// tablas. Sin ellas cada codigo se leia contra una tabla vacia y el video
// salia como basura o se rechazaba como danado. libjpeg-turbo hace lo mismo:
// solo para las tablas 0 (luminancia) y 1 (crominancia).
static const uint8_t kStdDcBits[2][17] = {
  { 0, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 }
};
static const uint8_t kStdDcVals[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
static const uint8_t kStdAcBits[2][17] = {
  { 0, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d },
  { 0, 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 }
};
static const uint8_t kStdAcVals[2][162] = {
  { 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
    0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa },
  { 0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
    0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
    0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa }
};

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

// ARITMETICA ENVOLVENTE. Los pasos intermedios van en uint32_t y se vuelven a
// int32_t solo para el descalado. Con datos validos da EXACTAMENTE los mismos
// bits que la cuenta con signo (el complemento a dos es el mismo); con datos
// danados (coeficientes y cuantizadores absurdos) la cuenta con signo se
// desbordaba, y en C++ eso es comportamiento indefinido: el compilador puede
// suponer que no ocurre. Ahora es una simple vuelta modular y la salida se
// recorta a 0..255 como siempre: un fotograma roto es solo un fotograma feo.
typedef uint32_t jw;
static inline jw jwOf(int32_t x){ return (jw)x; }
static inline int32_t jdesc(jw x, int n){ return (int32_t)(x + ((jw)1 << (n - 1))) >> n; }
#define JWC(k) ((jw)(int32_t)(k))

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
      int32_t dc = (int32_t)((jwOf(in[0]) * q[0]) << PASS1_BITS);
      for(int r = 0; r < 8; r++) ws[r * 8 + c] = dc;
      continue;
    }
    jw z1, z2, z3, z4, z5, t0, t1, t2, t3, t10, t11, t12, t13;

    z2 = jwOf(in[16]) * q[16];
    z3 = jwOf(in[48]) * q[48];
    z1 = (z2 + z3) * JWC(FIX_0_541196100);
    t2 = z1 + z3 * JWC(-FIX_1_847759065);
    t3 = z1 + z2 * JWC(FIX_0_765366865);

    z2 = jwOf(in[0])  * q[0];
    z3 = jwOf(in[32]) * q[32];
    t0 = (z2 + z3) << CONST_BITS;
    t1 = (z2 - z3) << CONST_BITS;

    t10 = t0 + t3; t13 = t0 - t3;
    t11 = t1 + t2; t12 = t1 - t2;

    t0 = jwOf(in[56]) * q[56];
    t1 = jwOf(in[40]) * q[40];
    t2 = jwOf(in[24]) * q[24];
    t3 = jwOf(in[8])  * q[8];

    z1 = t0 + t3; z2 = t1 + t2; z3 = t0 + t2; z4 = t1 + t3;
    z5 = (z3 + z4) * JWC(FIX_1_175875602);

    t0 *= JWC(FIX_0_298631336); t1 *= JWC(FIX_2_053119869);
    t2 *= JWC(FIX_3_072711026); t3 *= JWC(FIX_1_501321110);
    z1 *= JWC(-FIX_0_899976223); z2 *= JWC(-FIX_2_562915447);
    z3 *= JWC(-FIX_1_961570560); z4 *= JWC(-FIX_0_390180644);
    z3 += z5; z4 += z5;
    t0 += z1 + z3; t1 += z2 + z4; t2 += z2 + z3; t3 += z1 + z4;

    ws[0 * 8 + c] = jdesc(t10 + t3, CONST_BITS - PASS1_BITS);
    ws[7 * 8 + c] = jdesc(t10 - t3, CONST_BITS - PASS1_BITS);
    ws[1 * 8 + c] = jdesc(t11 + t2, CONST_BITS - PASS1_BITS);
    ws[6 * 8 + c] = jdesc(t11 - t2, CONST_BITS - PASS1_BITS);
    ws[2 * 8 + c] = jdesc(t12 + t1, CONST_BITS - PASS1_BITS);
    ws[5 * 8 + c] = jdesc(t12 - t1, CONST_BITS - PASS1_BITS);
    ws[3 * 8 + c] = jdesc(t13 + t0, CONST_BITS - PASS1_BITS);
    ws[4 * 8 + c] = jdesc(t13 - t0, CONST_BITS - PASS1_BITS);
  }

  // ---- Pase 2: filas ----
  for(int r = 0; r < 8; r++){
    const int32_t* w = ws + r * 8;
    uint8_t* o = out + (size_t)r * stride;
    jw z1, z2, z3, z4, z5, t0, t1, t2, t3, t10, t11, t12, t13;

    z2 = jwOf(w[2]); z3 = jwOf(w[6]);
    z1 = (z2 + z3) * JWC(FIX_0_541196100);
    t2 = z1 + z3 * JWC(-FIX_1_847759065);
    t3 = z1 + z2 * JWC(FIX_0_765366865);

    t0 = (jwOf(w[0]) + jwOf(w[4])) << CONST_BITS;
    t1 = (jwOf(w[0]) - jwOf(w[4])) << CONST_BITS;

    t10 = t0 + t3; t13 = t0 - t3;
    t11 = t1 + t2; t12 = t1 - t2;

    t0 = jwOf(w[7]); t1 = jwOf(w[5]); t2 = jwOf(w[3]); t3 = jwOf(w[1]);
    z1 = t0 + t3; z2 = t1 + t2; z3 = t0 + t2; z4 = t1 + t3;
    z5 = (z3 + z4) * JWC(FIX_1_175875602);

    t0 *= JWC(FIX_0_298631336); t1 *= JWC(FIX_2_053119869);
    t2 *= JWC(FIX_3_072711026); t3 *= JWC(FIX_1_501321110);
    z1 *= JWC(-FIX_0_899976223); z2 *= JWC(-FIX_2_562915447);
    z3 *= JWC(-FIX_1_961570560); z4 *= JWC(-FIX_0_390180644);
    z3 += z5; z4 += z5;
    t0 += z1 + z3; t1 += z2 + z4; t2 += z2 + z3; t3 += z1 + z4;

    o[0] = (uint8_t)jclamp255(jdesc(t10 + t3, CONST_BITS + PASS1_BITS + 3) + 128);
    o[7] = (uint8_t)jclamp255(jdesc(t10 - t3, CONST_BITS + PASS1_BITS + 3) + 128);
    o[1] = (uint8_t)jclamp255(jdesc(t11 + t2, CONST_BITS + PASS1_BITS + 3) + 128);
    o[6] = (uint8_t)jclamp255(jdesc(t11 - t2, CONST_BITS + PASS1_BITS + 3) + 128);
    o[2] = (uint8_t)jclamp255(jdesc(t12 + t1, CONST_BITS + PASS1_BITS + 3) + 128);
    o[5] = (uint8_t)jclamp255(jdesc(t12 - t1, CONST_BITS + PASS1_BITS + 3) + 128);
    o[3] = (uint8_t)jclamp255(jdesc(t13 + t0, CONST_BITS + PASS1_BITS + 3) + 128);
    o[4] = (uint8_t)jclamp255(jdesc(t13 - t0, CONST_BITS + PASS1_BITS + 3) + 128);
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

// Segmentos cuyo contenido se lee (el resto -- APPn, COM... -- se salta sin
// guardarlo, asi que en modo flujo un EXIF de 64 KB no ocupa ventana).
static inline bool jpegSegNeeded(uint8_t m){
  return m == 0xC0 || m == 0xC1 || m == 0xC2 || m == 0xC4 || m == 0xDB || m == 0xDD || m == 0xDA;
}
// Recorre los segmentos hasta SOS. Devuelve >= 0 con el origen colocado en
// el primer byte de datos entropicos (o justo tras el SOF si headerOnly), o
// un codigo de error negativo.
static int jpegParseHeaders(JDec* d, JSrc* src, bool headerOnly){
  if(jsNeed(src, 4) < 4 || src->p[0] != 0xFF || src->p[1] != 0xD8) return FLEXJPG_ERR_BADMARKER;
  src->p += 2;
  bool haveSOF = false;

  for(;;){
    if(jsNeed(src, 2) < 2) return FLEXJPG_ERR_TRUNCATED;
    if(src->p[0] != 0xFF){ src->p++; continue; }           // resincroniza sobre relleno
    uint8_t m = src->p[1];
    src->p += 2;
    if(m == 0xFF){ src->p--; continue; }                   // 0xFF de relleno
    if(m == 0x01 || (m >= 0xD0 && m <= 0xD7)) continue;    // sin payload
    if(m == 0xD9) return FLEXJPG_ERR_TRUNCATED;            // EOI antes de SOS
    if(jsNeed(src, 2) < 2) return FLEXJPG_ERR_TRUNCATED;
    int seglen = rd16(src->p);
    if(seglen < 2) return FLEXJPG_ERR_TRUNCATED;
    const bool need = jpegSegNeeded(m);
    if(need && jsNeed(src, (size_t)seglen) < (size_t)seglen){
      // En modo flujo un segmento util mas grande que la ventana no es un
      // JPEG real (el mayor posible, un DHT con 8 tablas, son 2.186 B).
      return (src->win && (size_t)seglen > src->winCap) ? FLEXJPG_ERR_UNSUPPORTED : FLEXJPG_ERR_TRUNCATED;
    }
    // En memoria se comprueba que el segmento este entero ANTES de mirarlo,
    // como siempre: un archivo cortado a mitad de segmento es "truncado".
    if(!src->win && (size_t)(src->end - src->p) < (size_t)seglen) return FLEXJPG_ERR_TRUNCATED;
    const uint8_t* seg = need ? src->p + 2 : NULL;
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
        if(headerOnly){ src->p += seglen; return 0; }
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
        src->p += seglen;                                  // el origen queda en el primer byte de datos
        return 0;
      }
      default:
        break;                                             // APPn, COM, etc: se ignoran
    }
    if(need) src->p += seglen;
    else if(!jsSkip(src, (size_t)seglen)) return FLEXJPG_ERR_TRUNCATED;
  }
}

// -------------------------------------------------------------
//  API: solo cabecera
// -------------------------------------------------------------
// -------------------------------------------------------------
//  EL ESTADO DEL DECODIFICADOR NO VA EN LA PILA. NUNCA.
//  ------------------------------------------------------------
//  `sizeof(JDec)` son 8240 bytes: las ocho tablas de Huffman (4 DC +
//  4 AC) ocupan 944 B cada una porque llevan una tabla rapida de 256
//  entradas para resolver los codigos cortos en un solo acceso.
//
//  En el PC eso da exactamente igual: la pila de un proceso son megas,
//  y por eso las pruebas de host decodificaban una imagen de 480x800
//  sin inmutarse. En la placa NO: el dibujo corre en el `loopTask` de
//  arduino-esp32, cuya pila son 8192 BYTES. Declarar `JDec d;` ahi
//  desbordaba la pila entera en cuanto llegaba el primer frame de
//  verdad -- antes incluso de decodificar un solo bloque.
//
//  Sintoma exacto en la ESP32-P4: la pantalla se llena de basura de un
//  solo color y acto seguido salta "PANIC / fatal exception / core
//  dump" y el sistema se reinicia. No es la URL, ni la Wi-Fi, ni el
//  token, ni el JPEG: es la pila.
//
//  Asi que el estado va al monton (heap), con el mismo asignador que
//  ya se usa para los planos de muestras. Son unos 8 KB temporales por
//  banda, y como todas las reservas son del MISMO tamano el asignador
//  reutiliza el mismo bloque una y otra vez: no fragmenta.
//
//  Regla para el futuro: en este fichero, nada de mas de ~256 bytes se
//  declara como local.
// -------------------------------------------------------------
static void* jdefAlloc(size_t n){ return malloc(n); }
static void  jdefFree(void* p){ free(p); }

static int jpegProbeSrc(JSrc* src, FlexJpegInfo* info, FlexJpegAlloc af, FlexJpegFree ff){
  JDec* d = (JDec*)af(sizeof(JDec));
  if(!d) return FLEXJPG_ERR_MEMORY;
  memset(d, 0, sizeof(*d));
  int r = jpegParseHeaders(d, src, true);
  memset(info, 0, sizeof(*info));
  info->progressive = d->progressive;
  info->width = d->width; info->height = d->height;
  info->outWidth = d->width; info->outHeight = d->height;
  info->components = d->ncomp; info->scaleDenom = 1;
  ff(d);
  return r < 0 ? r : FLEXJPG_OK;
}

int flexJpegProbe(const uint8_t* data, size_t len, FlexJpegInfo* info){
  if(!data || len < 4 || !info) return FLEXJPG_ERR_ARG;
  JSrc src; jsInitMem(&src, data, len);
  return jpegProbeSrc(&src, info, jdefAlloc, jdefFree);
}

int flexJpegProbeStream(FlexJpegReadFn rd, void* rdCtx, FlexJpegInfo* info,
                        FlexJpegAlloc af, FlexJpegFree ff){
  if(!rd || !info) return FLEXJPG_ERR_ARG;
  if(!af) af = jdefAlloc;
  if(!ff) ff = jdefFree;
  uint8_t* win = (uint8_t*)af(JPG_STREAM_WIN);
  if(!win){ memset(info, 0, sizeof(*info)); return FLEXJPG_ERR_MEMORY; }
  JSrc src; jsInitStream(&src, win, JPG_STREAM_WIN, rd, rdCtx);
  int r = jpegProbeSrc(&src, info, af, ff);
  ff(win);
  return r;
}

// -------------------------------------------------------------
//  API: decodificacion completa
//  ------------------------------------------------------------
//  UN solo cuerpo para las dos salidas. La plantilla decide en tiempo
//  de COMPILACION si cada pixel se empaqueta en RGB565 (pantalla) o se
//  entrega en RGB888 (editor, que no puede perder precision antes de
//  volver a codificar): ni una comparacion por pixel en el camino de
//  siempre, y ni una linea de la decodificacion duplicada.
// -------------------------------------------------------------
template <bool RGB888>
static inline void jput(uint16_t* o565, uint8_t* o888, int ox, int r, int g, int b){
  if(RGB888){ uint8_t* p = o888 + (size_t)ox * 3; p[0] = (uint8_t)r; p[1] = (uint8_t)g; p[2] = (uint8_t)b; }
  else o565[ox] = jrgb565(r, g, b);
}

template <bool RGB888>
static int jpegDecodeT(JSrc* src,
                       int maxW, int maxH, uint32_t maxPixels, FlexJpegScaleFn pick,
                       FlexJpegInfo* infoOut,
                       FlexJpegRowCb cb, FlexJpegRow888Cb cb888, void* user,
                       FlexJpegAlloc af, FlexJpegFree ff){
  // Ver el bloque de arriba: 8240 bytes al monton, jamas a la pila.
  JDec* dp = (JDec*)af(sizeof(JDec));
  if(!dp) return FLEXJPG_ERR_MEMORY;
  memset(dp, 0, sizeof(*dp));
  JDec& d = *dp;
  int sosEnd = jpegParseHeaders(&d, src, false);
  if(sosEnd < 0){ ff(dp); return sosEnd; }

  // ---- Divisor de escala ----
  // Lo decide el llamante si ha dado `pick` (ya conoce las medidas reales);
  // si no, el menor que hace que la salida quepa en maxW x maxH / maxPixels.
  int S = 1;
  if(pick){
    S = pick(user, d.width, d.height);
    if(S <= 0){ ff(dp); return FLEXJPG_ERR_ABORTED; }
    if(S != 1 && S != 2 && S != 4 && S != 8){ ff(dp); return FLEXJPG_ERR_ARG; }
  } else for(;;){
    int ow = (d.width + S - 1) / S, oh = (d.height + S - 1) / S;
    bool fits = true;
    if(maxW > 0 && ow > maxW) fits = false;
    if(maxH > 0 && oh > maxH) fits = false;
    if(maxPixels && (uint32_t)ow * (uint32_t)oh > maxPixels) fits = false;
    if(fits) break;
    if(S >= 8){ ff(dp); return FLEXJPG_ERR_TOOBIG; }
    S <<= 1;
  }
  int outW = (d.width + S - 1) / S;
  int outH = (d.height + S - 1) / S;

  for(int c = 0; c < d.ncomp; c++){
    JComp* cm = &d.comp[c];
    if(!d.quantSet[cm->tq]){ ff(dp); return FLEXJPG_ERR_BADMARKER; }
    // Sin DHT (MJPEG de camara): las tablas estandar del anexo K. Solo
    // existen para las tablas 0 y 1; pedir otra sin definirla es un error.
    if(!d.hdc[cm->td].present){
      if(cm->td > 1){ ff(dp); return FLEXJPG_ERR_BADMARKER; }
      huffBuild(&d.hdc[cm->td], kStdDcBits[cm->td], kStdDcVals, 12);
    }
    if(!d.hac[cm->ta].present){
      if(cm->ta > 1){ ff(dp); return FLEXJPG_ERR_BADMARKER; }
      huffBuild(&d.hac[cm->ta], kStdAcBits[cm->ta], kStdAcVals[cm->ta], 162);
    }
  }

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
  uint8_t*  out888 = NULL;
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
  if(RGB888){
    out888 = (uint8_t*)af((size_t)outW * 3u);
    if(!out888){ rc = FLEXJPG_ERR_MEMORY; goto done; }
  } else {
    outRow = (uint16_t*)af((size_t)outW * sizeof(uint16_t));
    if(!outRow){ rc = FLEXJPG_ERR_MEMORY; goto done; }
  }

  {
    BitRdr br; brInit(&br, src);
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
              // Se guarda ya reducido a 16 bits: coef[0] sale identico y un
              // flujo danado no puede desbordar el int a fuerza de sumar.
              cm->dcPred = (int16_t)(uint16_t)(cm->dcPred + diff);
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
              jput<RGB888>(outRow, out888, ox, Y, Y, Y);
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
              jput<RGB888>(outRow, out888, ox, jclamp255(r), jclamp255(g), jclamp255(b));
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
            if(d.ncomp == 1){ jput<RGB888>(outRow, out888, ox, Y, Y, Y); continue; }
            int Cb = sumCb / n - 128, Cr = sumCr / n - 128;
            int r = Y + ((91881 * Cr) >> 16);
            int g = Y - ((22554 * Cb + 46802 * Cr) >> 16);
            int b = Y + ((116130 * Cb) >> 16);
            jput<RGB888>(outRow, out888, ox, jclamp255(r), jclamp255(g), jclamp255(b));
          }
        }

        bool more = RGB888 ? cb888(user, oy, outW, out888) : cb(user, oy, outW, outRow);
        if(!more){ rc = FLEXJPG_ERR_ABORTED; goto done; }
      }
    }
  }

done:
  if(outRow) ff(outRow);
  if(out888) ff(out888);
  for(int c = 0; c < FLEXJPG_MAX_COMPONENTS; c++) if(planes[c]) ff(planes[c]);
  ff(dp);
  return rc;
}

int flexJpegDecode(const uint8_t* data, size_t len,
                   int maxW, int maxH, uint32_t maxPixels,
                   FlexJpegInfo* infoOut,
                   FlexJpegRowCb cb, void* user,
                   FlexJpegAlloc af, FlexJpegFree ff){
  if(!data || len < 4 || !cb) return FLEXJPG_ERR_ARG;
  if(!af) af = jdefAlloc;
  if(!ff) ff = jdefFree;
  JSrc src; jsInitMem(&src, data, len);
  return jpegDecodeT<false>(&src, maxW, maxH, maxPixels, NULL, infoOut, cb, NULL, user, af, ff);
}

int flexJpegDecode888(const uint8_t* data, size_t len,
                      int maxW, int maxH, uint32_t maxPixels,
                      FlexJpegInfo* infoOut,
                      FlexJpegRow888Cb cb, void* user,
                      FlexJpegAlloc af, FlexJpegFree ff){
  if(!data || len < 4 || !cb) return FLEXJPG_ERR_ARG;
  if(!af) af = jdefAlloc;
  if(!ff) ff = jdefFree;
  JSrc src; jsInitMem(&src, data, len);
  return jpegDecodeT<true>(&src, maxW, maxH, maxPixels, NULL, infoOut, NULL, cb, user, af, ff);
}

// ---- Modo flujo: la ventana se reserva aqui y se suelta SIEMPRE ----
template <bool RGB888>
static int jpegDecodeStreamT(FlexJpegReadFn rd, void* rdCtx,
                             int maxW, int maxH, uint32_t maxPixels, FlexJpegScaleFn pick,
                             FlexJpegInfo* infoOut,
                             FlexJpegRowCb cb, FlexJpegRow888Cb cb888, void* user,
                             FlexJpegAlloc af, FlexJpegFree ff){
  if(!rd || (RGB888 ? !cb888 : !cb)) return FLEXJPG_ERR_ARG;
  if(!af) af = jdefAlloc;
  if(!ff) ff = jdefFree;
  uint8_t* win = (uint8_t*)af(JPG_STREAM_WIN);
  if(!win) return FLEXJPG_ERR_MEMORY;
  JSrc src; jsInitStream(&src, win, JPG_STREAM_WIN, rd, rdCtx);
  int rc = jpegDecodeT<RGB888>(&src, maxW, maxH, maxPixels, pick, infoOut, cb, cb888, user, af, ff);
  // Un error de LECTURA no es un JPEG danado: se dice como truncado para
  // que nadie lo confunda con datos corruptos... salvo que el propio
  // decodificador ya hubiera dicho algo mas concreto.
  if(src.ioErr && rc == FLEXJPG_ERR_TRUNCATED) rc = FLEXJPG_ERR_IO;
  ff(win);
  return rc;
}

int flexJpegDecodeStream(FlexJpegReadFn rd, void* rdCtx,
                         int maxW, int maxH, uint32_t maxPixels, FlexJpegScaleFn pick,
                         FlexJpegInfo* infoOut,
                         FlexJpegRowCb cb, void* user,
                         FlexJpegAlloc af, FlexJpegFree ff){
  return jpegDecodeStreamT<false>(rd, rdCtx, maxW, maxH, maxPixels, pick, infoOut, cb, NULL, user, af, ff);
}

int flexJpegDecode888Stream(FlexJpegReadFn rd, void* rdCtx,
                            int maxW, int maxH, uint32_t maxPixels, FlexJpegScaleFn pick,
                            FlexJpegInfo* infoOut,
                            FlexJpegRow888Cb cb, void* user,
                            FlexJpegAlloc af, FlexJpegFree ff){
  return jpegDecodeStreamT<true>(rd, rdCtx, maxW, maxH, maxPixels, pick, infoOut, NULL, cb, user, af, ff);
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
    case FLEXJPG_ERR_IO:          return "no se pudo leer la imagen";
    default:                      return "error desconocido";
  }
}
