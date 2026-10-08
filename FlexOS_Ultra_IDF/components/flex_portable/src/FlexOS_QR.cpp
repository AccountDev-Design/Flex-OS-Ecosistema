// #############################################################
//  FLEX OS · CODIFICADOR QR  ·  implementacion
//  ------------------------------------------------------------
//  Ver FlexOS_QR.h. Las tablas son las del estandar ISO/IEC 18004 y
//  estan escritas aqui version a version, sin generarlas: son datos
//  fijos y una tabla mal generada da un codigo ilegible que compila
//  igual de bien.
//
//  Ni una reserva de memoria. Los buffers de trabajo son locales y su
//  tamano viene de la version 10, que es el techo del modulo: 346
//  codewords en total. Con eso el marco de pila mas grande de este
//  archivo se queda muy por debajo del techo de tests/host/check_stack.py.
// #############################################################
#include "FlexOS_QR.h"
#include <string.h>

// -------------------------------------------------------------
//  TABLAS DEL ESTANDAR
//  ------------------------------------------------------------
//  Por version (1..10) y nivel (M, L, H, Q -- el orden del estandar):
//  codewords de correccion por bloque, bloques y datos del grupo 1, y
//  bloques y datos del grupo 2.
// -------------------------------------------------------------
typedef struct { uint8_t ecc, b1, d1, b2, d2; } QrRow;
static const QrRow QR_TAB[FLEXQR_MAX_VERSION][4] = {
  /* v1  */ { {10,1,16,0,0}, { 7,1,19,0,0}, {17,1, 9,0,0}, {13,1,13,0,0} },
  /* v2  */ { {16,1,28,0,0}, {10,1,34,0,0}, {28,1,16,0,0}, {22,1,22,0,0} },
  /* v3  */ { {26,1,44,0,0}, {15,1,55,0,0}, {22,2,13,0,0}, {18,2,17,0,0} },
  /* v4  */ { {18,2,32,0,0}, {20,1,80,0,0}, {16,4, 9,0,0}, {26,2,24,0,0} },
  /* v5  */ { {24,2,43,0,0}, {26,1,108,0,0},{22,2,11,2,12},{18,2,15,2,16} },
  /* v6  */ { {16,4,27,0,0}, {18,2,68,0,0}, {28,4,15,0,0}, {24,4,19,0,0} },
  /* v7  */ { {18,4,31,0,0}, {20,2,78,0,0}, {26,4,13,1,14},{18,2,14,4,15} },
  /* v8  */ { {22,2,38,2,39},{24,2,97,0,0}, {26,4,14,2,15},{22,4,18,2,19} },
  /* v9  */ { {22,3,36,2,37},{30,2,116,0,0},{24,4,12,4,13},{20,4,16,4,17} },
  /* v10 */ { {26,4,43,1,44},{18,2,68,2,69},{28,6,15,2,16},{24,6,19,2,20} },
};
// Total de codewords (datos + correccion) de cada version.
static const uint16_t QR_TOTAL[FLEXQR_MAX_VERSION] = {
  26, 44, 70, 100, 134, 172, 196, 242, 292, 346
};
// Centros de los patrones de alineacion.
static const uint8_t QR_ALIGN[FLEXQR_MAX_VERSION][4] = {
  {0,0,0,0}, {6,18,0,0}, {6,22,0,0}, {6,26,0,0}, {6,30,0,0},
  {6,34,0,0}, {6,22,38,0}, {6,24,42,0}, {6,26,46,0}, {6,28,50,0},
};

int flexQrBlockInfo(int version, int ecc, FlexQrBlocks* out){
  if(!out || version < FLEXQR_MIN_VERSION || version > FLEXQR_MAX_VERSION) return FLEXQR_E_BADARG;
  if(ecc < 0 || ecc > 3) return FLEXQR_E_BADARG;
  const QrRow* r = &QR_TAB[version - 1][ecc];
  out->eccPerBlock = r->ecc;
  out->blocks1 = r->b1; out->data1 = r->d1;
  out->blocks2 = r->b2; out->data2 = r->d2;
  out->totalCodewords = QR_TOTAL[version - 1];
  out->dataCodewords = r->b1 * r->d1 + r->b2 * r->d2;
  return FLEXQR_OK;
}

int flexQrAlignPositions(int version, int* out, int maxn){
  if(!out || version < FLEXQR_MIN_VERSION || version > FLEXQR_MAX_VERSION) return 0;
  int n = 0;
  for(int i = 0; i < 4 && n < maxn; i++){
    if(QR_ALIGN[version - 1][i] == 0 && i > 0) break;
    if(QR_ALIGN[version - 1][i] == 0) continue;
    out[n++] = QR_ALIGN[version - 1][i];
  }
  return n;
}

// -------------------------------------------------------------
//  GF(256) con el polinomio 0x11D del estandar.
//  Las tablas se construyen una sola vez, en la primera llamada.
// -------------------------------------------------------------
static uint8_t gfExp[512], gfLog[256];
static int gfReady = 0;
static void gfInit(){
  if(gfReady) return;
  int x = 1;
  for(int i = 0; i < 255; i++){
    gfExp[i] = (uint8_t)x;
    gfLog[x] = (uint8_t)i;
    x <<= 1;
    if(x & 0x100) x ^= 0x11D;
  }
  for(int i = 255; i < 512; i++) gfExp[i] = gfExp[i - 255];
  gfReady = 1;
}
static inline uint8_t gfMul(uint8_t a, uint8_t b){
  if(!a || !b) return 0;
  return gfExp[gfLog[a] + gfLog[b]];
}

// Polinomio generador de grado 'n'.
static void rsGenerator(int n, uint8_t* g){
  memset(g, 0, (size_t)n + 1);
  g[0] = 1;
  int len = 1;
  for(int i = 0; i < n; i++){
    // g(x) = g(x) * (x - a^i). Se recorre de atras adelante para poder
    // hacerlo EN EL SITIO sin un segundo buffer.
    g[len] = 0;
    for(int j = len; j > 0; j--) g[j] = (uint8_t)(g[j - 1] ^ gfMul(g[j], gfExp[i]));
    g[0] = gfMul(g[0], gfExp[i]);
    len++;
  }
}

// Resto de dividir el bloque de datos por el generador: son los
// codewords de correccion.
static void rsEncode(const uint8_t* data, int dlen, int ecclen, uint8_t* out){
  uint8_t gen[31];
  if(ecclen > 30) ecclen = 30;
  rsGenerator(ecclen, gen);
  memset(out, 0, (size_t)ecclen);
  for(int i = 0; i < dlen; i++){
    uint8_t factor = (uint8_t)(data[i] ^ out[0]);
    memmove(out, out + 1, (size_t)ecclen - 1);
    out[ecclen - 1] = 0;
    if(factor) for(int j = 0; j < ecclen; j++) out[j] ^= gfMul(gen[ecclen - 1 - j], factor);
  }
}

// -------------------------------------------------------------
//  REJILLA
// -------------------------------------------------------------
typedef struct { uint8_t* m; int n; } QrGrid;
static inline void qrSet(QrGrid* g, int x, int y, int dark, int func){
  if(x < 0 || y < 0 || x >= g->n || y >= g->n) return;
  uint8_t v = (uint8_t)((dark ? FLEXQR_DARK : 0) | (func ? FLEXQR_FUNC : 0));
  g->m[y * g->n + x] = v;
}
static inline int qrIsFunc(const QrGrid* g, int x, int y){
  if(x < 0 || y < 0 || x >= g->n || y >= g->n) return 1;
  return (g->m[y * g->n + x] & FLEXQR_FUNC) != 0;
}
static inline int qrDark(const QrGrid* g, int x, int y){
  if(x < 0 || y < 0 || x >= g->n || y >= g->n) return 0;
  return (g->m[y * g->n + x] & FLEXQR_DARK) != 0;
}

static void qrFinder(QrGrid* g, int cx, int cy){
  for(int dy = -4; dy <= 4; dy++){
    for(int dx = -4; dx <= 4; dx++){
      int x = cx + dx, y = cy + dy;
      if(x < 0 || y < 0 || x >= g->n || y >= g->n) continue;
      int a = dx < 0 ? -dx : dx, b = dy < 0 ? -dy : dy;
      int r = a > b ? a : b;
      qrSet(g, x, y, (r != 2 && r != 4), 1);
    }
  }
}

static void qrFunctionPatterns(QrGrid* g, int version){
  int n = g->n;
  qrFinder(g, 3, 3);
  qrFinder(g, n - 4, 3);
  qrFinder(g, 3, n - 4);
  for(int i = 8; i < n - 8; i++){          // patrones de sincronizacion
    qrSet(g, i, 6, (i % 2) == 0, 1);
    qrSet(g, 6, i, (i % 2) == 0, 1);
  }
  int pos[4], np = flexQrAlignPositions(version, pos, 4);
  for(int a = 0; a < np; a++){
    for(int b = 0; b < np; b++){
      int cx = pos[a], cy = pos[b];
      // Las tres esquinas las ocupan los patrones de busqueda.
      if((cx <= 8 && cy <= 8) || (cx <= 8 && cy >= n - 9) || (cx >= n - 9 && cy <= 8)) continue;
      for(int dy = -2; dy <= 2; dy++)
        for(int dx = -2; dx <= 2; dx++){
          int r = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
          qrSet(g, cx + dx, cy + dy, r != 1, 1);
        }
    }
  }
  // Espacio de la informacion de formato (se rellena al final) y el
  // modulo oscuro obligatorio.
  for(int i = 0; i < 9; i++){
    if(i != 6) qrSet(g, i, 8, 0, 1);
    if(i != 6) qrSet(g, 8, i, 0, 1);
  }
  for(int i = 0; i < 8; i++){
    qrSet(g, n - 1 - i, 8, 0, 1);
    qrSet(g, 8, n - 1 - i, 0, 1);
  }
  qrSet(g, 8, n - 8, 1, 1);                 // modulo oscuro
  if(version >= 7){                         // espacio de la informacion de version
    for(int i = 0; i < 6; i++)
      for(int j = 0; j < 3; j++){
        qrSet(g, n - 11 + j, i, 0, 1);
        qrSet(g, i, n - 11 + j, 0, 1);
      }
  }
}

// BCH(15,5) de la informacion de formato y BCH(18,6) de la de version.
static uint32_t qrBch(uint32_t data, uint32_t gen, int genBits){
  uint32_t v = data << genBits;
  int top = 0;
  for(uint32_t t = gen; t; t >>= 1) top++;
  for(int i = 31; i >= (int)0; i--){
    if(v & (1u << i)){
      int shift = i - (top - 1);
      if(shift < 0) break;
      v ^= gen << shift;
    }
  }
  return v;
}

static void qrPutFormat(QrGrid* g, int ecc, int mask){
  int n = g->n;
  uint32_t data = (uint32_t)((ecc << 3) | mask);
  uint32_t bits = ((data << 10) | qrBch(data, 0x537, 10)) ^ 0x5412u;
  for(int i = 0; i < 15; i++){
    int bit = (bits >> i) & 1;
    // Copia 1: alrededor del patron de busqueda superior izquierdo.
    int x1, y1;
    if(i < 6){ x1 = 8; y1 = i; }
    else if(i == 6){ x1 = 8; y1 = 7; }
    else if(i == 7){ x1 = 8; y1 = 8; }
    else if(i == 8){ x1 = 7; y1 = 8; }
    else { x1 = 14 - i; y1 = 8; }
    qrSet(g, x1, y1, bit, 1);
    // Copia 2: repartida entre los otros dos patrones.
    int x2, y2;
    if(i < 8){ x2 = n - 1 - i; y2 = 8; }
    else { x2 = 8; y2 = n - 15 + i; }
    qrSet(g, x2, y2, bit, 1);
  }
}

static void qrPutVersion(QrGrid* g, int version){
  if(version < 7) return;
  int n = g->n;
  uint32_t bits = ((uint32_t)version << 12) | qrBch((uint32_t)version, 0x1F25, 12);
  for(int i = 0; i < 18; i++){
    int bit = (bits >> i) & 1;
    int a = i / 3, b = i % 3;
    qrSet(g, n - 11 + b, a, bit, 1);
    qrSet(g, a, n - 11 + b, bit, 1);
  }
}

static inline int qrMaskBit(int mask, int x, int y){
  switch(mask){
    case 0: return ((x + y) % 2) == 0;
    case 1: return (y % 2) == 0;
    case 2: return (x % 3) == 0;
    case 3: return ((x + y) % 3) == 0;
    case 4: return (((y / 2) + (x / 3)) % 2) == 0;
    case 5: return ((x * y) % 2 + (x * y) % 3) == 0;
    case 6: return (((x * y) % 2 + (x * y) % 3) % 2) == 0;
    default:return (((x + y) % 2 + (x * y) % 3) % 2) == 0;
  }
}

// Recorrido en zigzag: dos columnas a la vez, de derecha a izquierda,
// saltando la columna 6 (la del patron de sincronizacion vertical).
static void qrPlaceData(QrGrid* g, const uint8_t* cw, int cwLen, int mask){
  int n = g->n;
  int bit = 0;
  int total = cwLen * 8;
  int up = 1;
  for(int right = n - 1; right >= 1; right -= 2){
    if(right == 6) right = 5;
    for(int v = 0; v < n; v++){
      int y = up ? (n - 1 - v) : v;
      for(int c = 0; c < 2; c++){
        int x = right - c;
        if(qrIsFunc(g, x, y)) continue;
        int b = 0;
        if(bit < total) b = (cw[bit >> 3] >> (7 - (bit & 7))) & 1;
        bit++;
        if(qrMaskBit(mask, x, y)) b ^= 1;
        qrSet(g, x, y, b, 0);
      }
    }
    up = !up;
  }
}

// Penalizacion del estandar: las cuatro reglas. Elegir la mascara con
// menos penalizacion es lo que evita que salgan bandas o bloques
// grandes que confunden al lector del telefono.
static int qrPenalty(const QrGrid* g){
  int n = g->n, score = 0;
  for(int y = 0; y < n; y++){
    int run = 1;
    for(int x = 1; x < n; x++){
      if(qrDark(g, x, y) == qrDark(g, x - 1, y)) run++;
      else { if(run >= 5) score += 3 + (run - 5); run = 1; }
    }
    if(run >= 5) score += 3 + (run - 5);
  }
  for(int x = 0; x < n; x++){
    int run = 1;
    for(int y = 1; y < n; y++){
      if(qrDark(g, x, y) == qrDark(g, x, y - 1)) run++;
      else { if(run >= 5) score += 3 + (run - 5); run = 1; }
    }
    if(run >= 5) score += 3 + (run - 5);
  }
  for(int y = 0; y + 1 < n; y++)
    for(int x = 0; x + 1 < n; x++){
      int a = qrDark(g, x, y);
      if(a == qrDark(g, x + 1, y) && a == qrDark(g, x, y + 1) && a == qrDark(g, x + 1, y + 1))
        score += 3;
    }
  static const int PAT[7] = { 1, 0, 1, 1, 1, 0, 1 };
  for(int y = 0; y < n; y++)
    for(int x = 0; x < n; x++){
      if(x + 7 <= n){
        int ok = 1;
        for(int k = 0; k < 7; k++) if(qrDark(g, x + k, y) != PAT[k]){ ok = 0; break; }
        if(ok){
          int before = 1, after = 1;
          for(int k = 1; k <= 4; k++) if(qrDark(g, x - k, y)) before = 0;
          for(int k = 0; k < 4; k++) if(qrDark(g, x + 7 + k, y)) after = 0;
          if(before || after) score += 40;
        }
      }
      if(y + 7 <= n){
        int ok = 1;
        for(int k = 0; k < 7; k++) if(qrDark(g, x, y + k) != PAT[k]){ ok = 0; break; }
        if(ok){
          int before = 1, after = 1;
          for(int k = 1; k <= 4; k++) if(qrDark(g, x, y - k)) before = 0;
          for(int k = 0; k < 4; k++) if(qrDark(g, x, y + 7 + k)) after = 0;
          if(before || after) score += 40;
        }
      }
    }
  int dark = 0;
  for(int y = 0; y < n; y++) for(int x = 0; x < n; x++) if(qrDark(g, x, y)) dark++;
  int pct = (dark * 100) / (n * n);
  int dev = pct > 50 ? pct - 50 : 50 - pct;
  score += (dev / 5) * 10;
  return score;
}

int flexQrEncode(const char* text, int ecc, int minVersion,
                 uint8_t* modules, size_t cap, int* size, int* version){
  if(!text || !modules) return FLEXQR_E_BADARG;
  if(ecc < 0 || ecc > 3) return FLEXQR_E_BADARG;
  if(minVersion < FLEXQR_MIN_VERSION) minVersion = FLEXQR_MIN_VERSION;
  if(minVersion > FLEXQR_MAX_VERSION) return FLEXQR_E_BADARG;
  gfInit();
  size_t len = strlen(text);
  if(len == 0) return FLEXQR_E_BADARG;
  if(len > 0xFFFF) return FLEXQR_E_TOOLONG;

  // 1) Version mas pequena en la que quepa.
  int ver = -1;
  FlexQrBlocks bi;
  for(int v = minVersion; v <= FLEXQR_MAX_VERSION; v++){
    if(flexQrBlockInfo(v, ecc, &bi) != FLEXQR_OK) continue;
    int ccBits = (v <= 9) ? 8 : 16;
    long need = 4 + ccBits + 8 * (long)len;
    if(need <= (long)bi.dataCodewords * 8){ ver = v; break; }
  }
  if(ver < 0) return FLEXQR_E_TOOLONG;
  flexQrBlockInfo(ver, ecc, &bi);
  int n = flexQrSize(ver);
  if(cap < (size_t)n * n) return FLEXQR_E_NOMEM;

  // 2) Flujo de bits: modo BYTE, longitud, datos, terminador y relleno.
  // 400 bytes cubren la version 10 con correccion L (274 codewords de
  // datos), que es el techo de este modulo.
  uint8_t data[400];
  memset(data, 0, sizeof(data));
  int bitPos = 0;
  #define QRPUT(val, bits) do { \
      for(int _b = (bits) - 1; _b >= 0; _b--){ \
        if(bitPos >= (int)sizeof(data) * 8) break; \
        if(((val) >> _b) & 1) data[bitPos >> 3] |= (uint8_t)(0x80u >> (bitPos & 7)); \
        bitPos++; \
      } } while(0)
  QRPUT(4u, 4);                                   // modo BYTE
  QRPUT((uint32_t)len, (ver <= 9) ? 8 : 16);
  for(size_t i = 0; i < len; i++) QRPUT((uint32_t)(uint8_t)text[i], 8);
  int cap8 = bi.dataCodewords * 8;
  for(int i = 0; i < 4 && bitPos < cap8; i++) QRPUT(0u, 1);   // terminador
  while(bitPos % 8) QRPUT(0u, 1);
  #undef QRPUT
  int dlen = bitPos / 8;
  // Relleno alterno del estandar hasta llenar la capacidad de datos.
  for(int i = dlen; i < bi.dataCodewords; i++)
    data[i] = ((i - dlen) % 2) ? 0x11 : 0xEC;

  // 3) Correccion por bloques y ENTRELAZADO. El entrelazado es lo que
  //    hace que una mancha o un reflejo se reparta entre bloques en vez
  //    de destrozar uno solo: sin el, la correccion no sirve de nada.
  uint8_t cw[400];
  memset(cw, 0, sizeof(cw));
  int nBlocks = bi.blocks1 + bi.blocks2;
  int off[32], dsz[32];
  if(nBlocks > 32) return FLEXQR_E_BADARG;
  int p = 0;
  for(int b = 0; b < nBlocks; b++){
    dsz[b] = (b < bi.blocks1) ? bi.data1 : bi.data2;
    off[b] = p; p += dsz[b];
  }
  uint8_t eccBuf[32 * 32];
  memset(eccBuf, 0, sizeof(eccBuf));
  for(int b = 0; b < nBlocks; b++)
    rsEncode(data + off[b], dsz[b], bi.eccPerBlock, eccBuf + b * 32);
  int out = 0;
  int maxD = bi.data1 > bi.data2 ? bi.data1 : bi.data2;
  for(int i = 0; i < maxD; i++)
    for(int b = 0; b < nBlocks; b++)
      if(i < dsz[b]) cw[out++] = data[off[b] + i];
  for(int i = 0; i < bi.eccPerBlock; i++)
    for(int b = 0; b < nBlocks; b++)
      cw[out++] = eccBuf[b * 32 + i];

  // 4) Rejilla, mascara y formato. Se prueban las ocho mascaras y se
  //    conserva la de menor penalizacion, como manda el estandar.
  QrGrid g; g.m = modules; g.n = n;
  int best = -1, bestScore = 0;
  for(int mask = 0; mask < 8; mask++){
    memset(modules, 0, (size_t)n * n);
    qrFunctionPatterns(&g, ver);
    qrPutVersion(&g, ver);
    qrPutFormat(&g, ecc, mask);
    qrPlaceData(&g, cw, out, mask);
    int sc = qrPenalty(&g);
    if(best < 0 || sc < bestScore){ best = mask; bestScore = sc; }
  }
  memset(modules, 0, (size_t)n * n);
  qrFunctionPatterns(&g, ver);
  qrPutVersion(&g, ver);
  qrPutFormat(&g, ecc, best);
  qrPlaceData(&g, cw, out, best);

  if(size) *size = n;
  if(version) *version = ver;
  return FLEXQR_OK;
}
