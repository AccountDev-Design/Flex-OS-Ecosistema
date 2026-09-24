// #############################################################
//  test_jpegenc.cpp  ·  codificador JPEG (FlexOS_JPEGEnc) y salida
//  RGB888 del decodificador (FlexOS_JPEG)
// #############################################################
//
//  EL CODIFICADOR se comprueba con el UNICO juez que importa en la placa:
//  el decodificador del propio firmware. Cada imagen que se codifica aqui
//  se vuelve a decodificar con FlexOS_JPEG y se mide cuanto se parece al
//  original (PSNR). Ademas:
//    · la estructura del flujo: SOI al principio, EOI al final y ni un
//      marcador 0xFF sin su 0x00 dentro de los datos (un 0xFF suelto es
//      lo que hace que un visor corte la imagen por la mitad);
//    · bordes que no son multiplo de 8/16, 1x1, gris, 4:4:4 y 4:2:0;
//    · entrada RGB565 == la misma imagen en RGB888 (bit a bit);
//    · fallos del destino, de la fuente y de la memoria en CADA reserva,
//      sin fugas;
//    · ruido a calidad 100, que recorre casi todos los simbolos de las
//      tablas Huffman: un simbolo sin codigo romperia el flujo.
//
//  LA SALIDA RGB888 DEL DECODIFICADOR se compara contra las mismas
//  referencias de libjpeg-turbo que usa test_jpeg (ficheros .rgb), y ademas
//  se comprueba que la salida RGB565 de siempre es EXACTAMENTE la RGB888
//  empaquetada: las dos salen del mismo cuerpo y no pueden divergir.

#include "../../FlexOS_Ultra/FlexOS_JPEG.h"
#include "../../FlexOS_Ultra/FlexOS_JPEGEnc.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

// ---- reservador que cuenta (y que puede fallar en la reserva N) ----
static long g_live = 0;
static int  g_allocN = 0, g_failAt = -1;
static void* tAlloc(size_t n){
  if(g_failAt >= 0 && g_allocN++ == g_failAt) return nullptr;
  void* p = std::malloc(n);
  if(p) g_live++;
  return p;
}
static void tFree(void* p){ if(p){ g_live--; std::free(p); } }

static uint32_t rng = 0xC0FFEEu;
static uint32_t rnd(){ rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static bool readFile(const std::string& p, std::vector<uint8_t>& v){
  FILE* f = std::fopen(p.c_str(), "rb");
  if(!f) return false;
  v.clear(); uint8_t b[4096]; size_t n;
  while((n = std::fread(b, 1, sizeof(b), f)) > 0) v.insert(v.end(), b, b + n);
  std::fclose(f);
  return true;
}

// ---- destino en memoria (que puede fallar tras N bytes) ----
struct Out { std::vector<uint8_t> b; long failAfter = -1; };
static bool outWrite(void* c, const uint8_t* d, size_t n){
  Out* o = (Out*)c;
  if(o->failAfter >= 0 && (long)(o->b.size() + n) > o->failAfter) return false;
  o->b.insert(o->b.end(), d, d + n);
  return true;
}

// ---- decodificacion a RGB888 ----
struct Img { int w = 0, h = 0; std::vector<uint8_t> px; bool ordered = true; int next = 0; };
static bool row888(void* u, int y, int w, const uint8_t* rgb){
  Img* m = (Img*)u;
  if(y != m->next) m->ordered = false;
  m->next = y + 1;
  if(m->w == 0) m->w = w;
  if((int)m->px.size() < (y + 1) * w * 3) m->px.resize((size_t)(y + 1) * w * 3);
  std::memcpy(&m->px[(size_t)y * w * 3], rgb, (size_t)w * 3);
  m->h = y + 1;
  return true;
}
struct Img565 { int w = 0, h = 0; std::vector<uint16_t> px; };
static bool row565(void* u, int y, int w, const uint16_t* rgb){
  Img565* m = (Img565*)u;
  m->w = w; m->h = y + 1;
  m->px.resize((size_t)(y + 1) * w);
  std::memcpy(&m->px[(size_t)y * w], rgb, (size_t)w * 2);
  return true;
}

static double psnr(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b){
  if(a.size() != b.size() || a.empty()) return 0;
  double se = 0;
  for(size_t i = 0; i < a.size(); i++){ double d = (double)a[i] - (double)b[i]; se += d * d; }
  double mse = se / (double)a.size();
  return mse <= 1e-12 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

// Estructura del flujo: SOI..EOI y relleno correcto de los 0xFF.
static bool streamOk(const std::vector<uint8_t>& j, std::string* why){
  if(j.size() < 4 || j[0] != 0xFF || j[1] != 0xD8){ *why = "sin SOI"; return false; }
  if(j[j.size() - 2] != 0xFF || j[j.size() - 1] != 0xD9){ *why = "sin EOI"; return false; }
  // Localiza el SOS y recorre los datos entropicos.
  size_t i = 2;
  while(i + 4 <= j.size()){
    if(j[i] != 0xFF){ *why = "cabecera rota"; return false; }
    uint8_t m = j[i + 1];
    size_t len = ((size_t)j[i + 2] << 8) | j[i + 3];
    if(m == 0xDA){ i += 2 + len; break; }
    i += 2 + len;
  }
  for(; i + 2 < j.size(); i++){
    if(j[i] == 0xFF && j[i + 1] != 0x00){ *why = "0xFF sin relleno en los datos"; return false; }
    if(j[i] == 0xFF) i++;
  }
  return true;
}

static std::vector<uint8_t> makeImage(int w, int h, int kind){
  std::vector<uint8_t> v((size_t)w * h * 3);
  for(int y = 0; y < h; y++) for(int x = 0; x < w; x++){
    uint8_t* p = &v[((size_t)y * w + x) * 3];
    switch(kind){
      case 0:   // degradado suave (el caso tipico de un cielo)
        p[0] = (uint8_t)(x * 255 / (w > 1 ? w - 1 : 1));
        p[1] = (uint8_t)(y * 255 / (h > 1 ? h - 1 : 1));
        p[2] = (uint8_t)((x + y) * 255 / (w + h > 2 ? w + h - 2 : 1));
        break;
      case 1:   // bordes duros de color saturado
        p[0] = ((x / 8 + y / 8) & 1) ? 230 : 20;
        p[1] = (x % 13 < 6) ? 200 : 40;
        p[2] = (y % 11 < 5) ? 10 : 240;
        break;
      default:  // ruido
        p[0] = (uint8_t)rnd(); p[1] = (uint8_t)rnd(); p[2] = (uint8_t)rnd();
        break;
    }
  }
  return v;
}

static int encode(const std::vector<uint8_t>& px, int w, int h, int q, int sub, Out& o){
  FlexJeCfg c; c.width = w; c.height = h; c.quality = q; c.subsampling = sub; c.input = FLEXJE_IN_RGB888;
  return flexJpegEncodeMem(&c, px.data(), (size_t)w * 3, outWrite, &o, tAlloc, tFree);
}

// =============================================================
static void testRoundTrip(){
  std::printf("-- codificar y volver a leer con el decodificador del firmware --\n");
  struct Case { int w, h, kind, q, sub; double minPsnr; };
  const Case cs[] = {
    { 64, 48,   0, 90, FLEXJE_SUB_420, 38.0 },
    { 64, 48,   0, 90, FLEXJE_SUB_444, 40.0 },
    { 61, 45,   0, 90, FLEXJE_SUB_420, 36.0 },   // bordes no multiplo de 16
    { 17, 33,   0, 85, FLEXJE_SUB_444, 36.0 },
    { 1, 1,     0, 90, FLEXJE_SUB_420, 30.0 },
    { 132, 132, 0, 82, FLEXJE_SUB_420, 36.0 },   // miniatura de la Galeria
    { 640, 480, 0, 92, FLEXJE_SUB_420, 40.0 },   // guardado del editor
    { 128, 96,  1, 90, FLEXJE_SUB_444, 28.0 },   // bordes duros: 4:4:4 los respeta
    // En 4:2:0 el croma va a media resolucion: un borde de color de 5-6 px
    // no sobrevive. No es un fallo del codificador: libjpeg-turbo, con la
    // MISMA imagen y la misma calidad, da 17,8 dB (comprobado con Pillow).
    { 128, 96,  1, 90, FLEXJE_SUB_420, 16.0 },
    { 96, 64,   2, 100, FLEXJE_SUB_444, 30.0 },  // ruido a calidad maxima: todas las tablas
    { 96, 64,   2, 10, FLEXJE_SUB_420, 5.0 },    // calidad minima: se decodifica, sin mas
  };
  for(const Case& k : cs){
    auto src = makeImage(k.w, k.h, k.kind);
    Out o;
    long live = g_live;
    int rc = encode(src, k.w, k.h, k.q, k.sub, o);
    CHECK(rc == FLEXJE_OK, "%dx%d q%d: rc=%d", k.w, k.h, k.q, rc);
    CHECK(g_live == live, "%dx%d: sin fugas", k.w, k.h);
    std::string why;
    CHECK(streamOk(o.b, &why), "%dx%d: flujo %s", k.w, k.h, why.c_str());
    FlexJpegInfo inf;
    CHECK(flexJpegProbe(o.b.data(), o.b.size(), &inf) == FLEXJPG_OK && inf.width == k.w && inf.height == k.h &&
          !inf.progressive && inf.components == 3, "%dx%d: cabecera leida por el firmware", k.w, k.h);
    Img d;
    int dr = flexJpegDecode888(o.b.data(), o.b.size(), 0, 0, 0, NULL, row888, &d, tAlloc, tFree);
    CHECK(dr == FLEXJPG_OK && d.w == k.w && d.h == k.h && d.ordered, "%dx%d: el firmware lo decodifica (rc=%d)", k.w, k.h, dr);
    double p = psnr(src, d.px);
    CHECK(p >= k.minPsnr, "%dx%d q%d %s: PSNR %.1f dB (minimo %.1f)", k.w, k.h, k.q,
          k.sub == FLEXJE_SUB_420 ? "4:2:0" : "4:4:4", p, k.minPsnr);
    std::printf("   %4dx%-4d %-6s q%-3d %-5s %6zu B  PSNR %5.1f dB\n", k.w, k.h,
                k.kind == 0 ? "suave" : k.kind == 1 ? "bordes" : "ruido", k.q,
                k.sub == FLEXJE_SUB_420 ? "4:2:0" : "4:4:4", o.b.size(), p);
  }
  // Gris.
  auto src = makeImage(40, 24, 0);
  for(size_t i = 0; i < src.size(); i += 3) src[i + 1] = src[i + 2] = src[i];
  Out o;
  FlexJeCfg c; c.width = 40; c.height = 24; c.quality = 90; c.subsampling = FLEXJE_GRAY; c.input = FLEXJE_IN_RGB888;
  CHECK(flexJpegEncodeMem(&c, src.data(), 0, outWrite, &o, tAlloc, tFree) == FLEXJE_OK, "gris");
  FlexJpegInfo inf;
  CHECK(flexJpegProbe(o.b.data(), o.b.size(), &inf) == FLEXJPG_OK && inf.components == 1, "gris: un componente");
  Img d; flexJpegDecode888(o.b.data(), o.b.size(), 0, 0, 0, NULL, row888, &d, tAlloc, tFree);
  CHECK(psnr(src, d.px) >= 38.0, "gris: PSNR %.1f", psnr(src, d.px));
  // Tamano razonable de una miniatura (132x132 de una foto real).
  std::vector<uint8_t> jpg, ref;
  if(readFile("../fixtures/page480.rgb", ref)){
    std::vector<uint8_t> th((size_t)132 * 132 * 3);
    for(int y = 0; y < 132; y++) for(int x = 0; x < 132; x++)
      std::memcpy(&th[((size_t)y * 132 + x) * 3], &ref[((size_t)(y * 6) * 480 + x * 3) * 3], 3);
    Out t; encode(th, 132, 132, 82, FLEXJE_SUB_420, t);
    CHECK(t.b.size() > 600 && t.b.size() < 16 * 1024, "miniatura de 132x132: %zu bytes", t.b.size());
    std::printf("   miniatura 132x132 de una pagina real: %zu bytes\n", t.b.size());
  }
}

static void test565(){
  std::printf("-- entrada RGB565 == la misma imagen en RGB888 --\n");
  const int w = 50, h = 37;
  std::vector<uint16_t> p565((size_t)w * h);
  std::vector<uint8_t> p888((size_t)w * h * 3);
  for(size_t i = 0; i < p565.size(); i++){
    uint16_t v = (uint16_t)rnd();
    p565[i] = v;
    int r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
    p888[i * 3 + 0] = (uint8_t)((r << 3) | (r >> 2));
    p888[i * 3 + 1] = (uint8_t)((g << 2) | (g >> 4));
    p888[i * 3 + 2] = (uint8_t)((b << 3) | (b >> 2));
  }
  FlexJeCfg c; c.width = w; c.height = h; c.quality = 80; c.subsampling = FLEXJE_SUB_420;
  Out a, b;
  c.input = FLEXJE_IN_RGB565;
  CHECK(flexJpegEncodeMem(&c, p565.data(), 0, outWrite, &a, tAlloc, tFree) == FLEXJE_OK, "desde RGB565");
  c.input = FLEXJE_IN_RGB888;
  CHECK(flexJpegEncodeMem(&c, p888.data(), 0, outWrite, &b, tAlloc, tFree) == FLEXJE_OK, "desde RGB888");
  CHECK(a.b == b.b, "bit a bit iguales (%zu / %zu bytes)", a.b.size(), b.b.size());
}

static bool srcFailAt3(void* c, int y, int rows, uint8_t* dst){
  (void)rows; (void)dst; return y < 3 * 16 ? (std::memset(dst, 128, (size_t)rows * 64 * 3), true) : false;
  (void)c;
}

static void testFailures(){
  std::printf("-- fallos: destino, fuente y memoria, sin fugas --\n");
  auto src = makeImage(64, 256, 0);
  Out full; encode(src, 64, 256, 85, FLEXJE_SUB_420, full);
  for(long cut : { 0L, 100L, 600L, (long)full.b.size() - 1 }){
    Out o; o.failAfter = cut;
    long live = g_live;
    int rc = encode(src, 64, 256, 85, FLEXJE_SUB_420, o);
    CHECK(rc == FLEXJE_ERR_WRITE && g_live == live, "destino que falla tras %ld bytes: rc=%d", cut, rc);
  }
  {
    Out o; long live = g_live;
    FlexJeCfg c; c.width = 64; c.height = 256; c.quality = 85; c.subsampling = FLEXJE_SUB_420; c.input = FLEXJE_IN_RGB888;
    int rc = flexJpegEncode(&c, srcFailAt3, nullptr, outWrite, &o, tAlloc, tFree);
    CHECK(rc == FLEXJE_ERR_SOURCE && g_live == live, "fuente que se corta: rc=%d", rc);
  }
  for(int k = 0; k < 8; k++){
    Out o; long live = g_live;
    g_allocN = 0; g_failAt = k;
    int rc = encode(src, 64, 256, 85, FLEXJE_SUB_420, o);
    g_failAt = -1;
    if(k < 6) CHECK(rc == FLEXJE_ERR_MEMORY, "sin memoria en la reserva %d: rc=%d", k, rc);
    CHECK(g_live == live, "sin memoria en la reserva %d: sin fugas", k);
  }
  FlexJeCfg c; c.width = 0; c.height = 10; c.quality = 80; c.subsampling = 0; c.input = 0;
  Out o;
  CHECK(flexJpegEncodeMem(&c, src.data(), 0, outWrite, &o, tAlloc, tFree) == FLEXJE_ERR_ARG, "ancho 0");
  c.width = FLEXJE_MAX_DIM + 1; c.height = 1;
  CHECK(flexJpegEncodeMem(&c, src.data(), 0, outWrite, &o, tAlloc, tFree) == FLEXJE_ERR_ARG, "demasiado grande");
}

// =============================================================
static void testDecode888(){
  std::printf("-- salida RGB888 del decodificador contra libjpeg-turbo --\n");
  const char* names[] = { "grad444", "grad420", "odd444", "odd420", "gray", "restart", "samp422", "samp440", "rst1", "page240" };
  for(const char* nm : names){
    std::vector<uint8_t> jpg, ref;
    if(!readFile(std::string("../fixtures/") + nm + ".jpg", jpg) ||
       !readFile(std::string("../fixtures/") + nm + ".rgb", ref)){ CHECK(false, "faltan los ficheros de %s", nm); continue; }
    Img d; FlexJpegInfo inf;
    long live = g_live;
    int rc = flexJpegDecode888(jpg.data(), jpg.size(), 0, 0, 0, &inf, row888, &d, tAlloc, tFree);
    CHECK(rc == FLEXJPG_OK && g_live == live, "%s: rc=%d", nm, rc);
    if(rc != FLEXJPG_OK) continue;
    // Referencia en RGB (el .rgb de gris tambien viene en tres canales).
    size_t n = (size_t)d.w * d.h;
    std::vector<uint8_t> refRgb(n * 3);
    bool grayRef = (ref.size() == n);
    for(size_t i = 0; i < n; i++)
      for(int c = 0; c < 3; c++) refRgb[i * 3 + c] = grayRef ? ref[i] : ref[i * 3 + c];
    double sum = 0; int worst = 0;
    for(size_t i = 0; i < n * 3; i++){ int e = std::abs((int)d.px[i] - (int)refRgb[i]); sum += e; if(e > worst) worst = e; }
    double mean = sum / (double)(n * 3);
    // Mismas razones que en test_jpeg: sin submuestreo manda el redondeo
    // de la IDCT entera; con croma submuestreado, libjpeg-turbo interpola
    // ("fancy upsampling") y aqui se replica la muestra vecina.
    bool sub = !strcmp(nm, "grad420") || !strcmp(nm, "odd420") || !strcmp(nm, "page240") ||
               !strcmp(nm, "samp422") || !strcmp(nm, "samp440");
    double limMean = sub ? 3.0 : 1.2;
    int limWorst = sub ? 72 : 8;
    CHECK(mean <= limMean && worst <= limWorst, "%s: error medio %.2f max %d niveles (limites %.1f / %d)",
          nm, mean, worst, limMean, limWorst);
    std::printf("   %-8s %3dx%-3d  error medio %.2f  max %2d  (niveles de 8 bits)\n", nm, d.w, d.h, mean, worst);
    // La salida de siempre es EXACTAMENTE esta, empaquetada.
    Img565 s; flexJpegDecode(jpg.data(), jpg.size(), 0, 0, 0, NULL, row565, &s, tAlloc, tFree);
    bool same = (s.w == d.w && s.h == d.h);
    for(size_t i = 0; same && i < n; i++){
      const uint8_t* p = &d.px[i * 3];
      uint16_t want = (uint16_t)(((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3));
      if(s.px[i] != want) same = false;
    }
    CHECK(same, "%s: RGB565 == RGB888 empaquetado", nm);
  }
  // Escalado tambien en RGB888.
  std::vector<uint8_t> jpg;
  if(readFile("../fixtures/page480.jpg", jpg)){
    Img d; FlexJpegInfo inf;
    int rc = flexJpegDecode888(jpg.data(), jpg.size(), 120, 200, 0, &inf, row888, &d, tAlloc, tFree);
    CHECK(rc == FLEXJPG_OK && inf.scaleDenom == 4 && d.w == 120 && d.h == 200, "escalado 1/4 en RGB888");
  }
  Img d;
  CHECK(flexJpegDecode888(jpg.data(), jpg.size(), 0, 0, 0, NULL, nullptr, &d, tAlloc, tFree) == FLEXJPG_ERR_ARG,
        "sin callback: argumento invalido");
}

int main(){
  std::printf("=== FlexOS · codificador JPEG y salida RGB888 ===\n");
  testRoundTrip();
  test565();
  testFailures();
  testDecode888();
  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
