// #############################################################
//  test_mediathumb.cpp  ·  miniaturas de la biblioteca de medios
// #############################################################
//
//  Una miniatura es lo que el usuario ve de cada foto en la rejilla, asi
//  que se comprueba lo que se VE:
//    · recorte centrado sin deformar (una foto apaisada no se aplasta: se
//      recorta), con los colores en su sitio;
//    · parecido medible (PSNR) contra una referencia calculada aqui, a
//      resolucion completa, por un camino distinto;
//    · fotos enormes (se decodifica a escala, no a 12 MP) y diminutas (se
//      amplian sin romperse);
//    · lo que NO se puede leer se dice: JPEG truncado -> danado,
//      progresivo -> formato no admitido. Es la validacion de la subida;
//    · primer fotograma de un AVI MJPEG construido aqui;
//    · sin fugas aunque falle la memoria en cualquier reserva.

#include "../../FlexOS_Ultra/FlexOS_MediaThumb.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

static long g_live = 0; static int g_n = 0, g_failAt = -1;
static void* tA(size_t n){ if(g_failAt >= 0 && g_n++ == g_failAt) return nullptr; void* p = std::malloc(n); if(p) g_live++; return p; }
static void tF(void* p){ if(p){ g_live--; std::free(p); } }

struct Out { std::vector<uint8_t> b; };
static bool outW(void* c, const uint8_t* d, size_t n){ ((Out*)c)->b.insert(((Out*)c)->b.end(), d, d + n); return true; }

struct Img { int w = 0, h = 0; std::vector<uint8_t> px; };
static bool rowCb(void* u, int y, int w, const uint8_t* rgb){
  Img* m = (Img*)u; m->w = w; m->h = y + 1; m->px.resize((size_t)(y + 1) * w * 3);
  std::memcpy(&m->px[(size_t)y * w * 3], rgb, (size_t)w * 3); return true;
}
static Img decode(const std::vector<uint8_t>& j){
  Img m; flexJpegDecode888(j.data(), j.size(), 0, 0, 0, NULL, rowCb, &m, NULL, NULL); return m;
}
static std::vector<uint8_t> encodeRgb(const std::vector<uint8_t>& px, int w, int h, int q = 92){
  Out o; FlexJeCfg c; c.width = w; c.height = h; c.quality = q; c.subsampling = FLEXJE_SUB_420; c.input = FLEXJE_IN_RGB888;
  flexJpegEncodeMem(&c, px.data(), 0, outW, &o, NULL, NULL);
  return o.b;
}
static double psnr(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b){
  if(a.size() != b.size() || a.empty()) return 0;
  double se = 0; for(size_t i = 0; i < a.size(); i++){ double d = (double)a[i] - b[i]; se += d * d; }
  double mse = se / a.size(); return mse < 1e-9 ? 99 : 10 * std::log10(65025.0 / mse);
}
static std::vector<uint8_t> scene(int w, int h){
  // Izquierda roja, derecha azul, un cuadrado verde en el centro y un
  // degradado suave por encima: bordes y zonas lisas a la vez.
  std::vector<uint8_t> v((size_t)w * h * 3);
  for(int y = 0; y < h; y++) for(int x = 0; x < w; x++){
    uint8_t* p = &v[((size_t)y * w + x) * 3];
    bool left = x < w / 2;
    p[0] = left ? 210 : 30; p[1] = (uint8_t)(40 + 60 * y / h); p[2] = left ? 30 : 210;
    if(std::abs(x - w / 2) < h / 8 && std::abs(y - h / 2) < h / 8){ p[0] = 20; p[1] = 200; p[2] = 40; }
  }
  return v;
}
// Referencia por OTRO camino: decodificar entero, recortar y promediar en
// coma flotante.
static std::vector<uint8_t> refThumb(const Img& full, int side){
  int s = full.w < full.h ? full.w : full.h, x0 = (full.w - s) / 2, y0 = (full.h - s) / 2;
  std::vector<uint8_t> o((size_t)side * side * 3);
  for(int oy = 0; oy < side; oy++) for(int ox = 0; ox < side; ox++){
    double a0 = (double)ox * s / side, a1 = (double)(ox + 1) * s / side;
    double b0 = (double)oy * s / side, b1 = (double)(oy + 1) * s / side;
    double acc[3] = { 0, 0, 0 }, n = 0;
    for(int y = (int)b0; y < (int)std::ceil(b1); y++) for(int x = (int)a0; x < (int)std::ceil(a1); x++){
      const uint8_t* p = &full.px[((size_t)(y0 + y) * full.w + x0 + x) * 3];
      for(int k = 0; k < 3; k++) acc[k] += p[k];
      n++;
    }
    for(int k = 0; k < 3; k++) o[((size_t)oy * side + ox) * 3 + k] = (uint8_t)(acc[k] / n + 0.5);
  }
  return o;
}

static void testJpeg(){
  std::printf("-- desde un JPEG: recorte centrado, colores y parecido --\n");
  auto src = scene(400, 300);
  auto jpg = encodeRgb(src, 400, 300);
  Out t; int w = 0, h = 0;
  long live = g_live;
  int rc = flexThumbFromJpeg(jpg.data(), jpg.size(), FLEXTH_SIDE, FLEXTH_QUALITY, outW, &t, &w, &h, tA, tF);
  CHECK(rc == FLEXTH_OK && w == 400 && h == 300, "miniatura (rc=%d, %dx%d)", rc, w, h);
  CHECK(g_live == live, "sin fugas");
  Img th = decode(t.b);
  CHECK(th.w == FLEXTH_SIDE && th.h == FLEXTH_SIDE, "cuadrada de %d (%dx%d)", FLEXTH_SIDE, th.w, th.h);
  // Recorte 300x300 centrado: la columna 30 cae en la mitad roja y la 110
  // en la azul; el centro, en el cuadrado verde.
  const uint8_t* L = &th.px[((size_t)20 * th.w + 30) * 3];
  const uint8_t* R = &th.px[((size_t)20 * th.w + 110) * 3];
  const uint8_t* C = &th.px[((size_t)66 * th.w + 66) * 3];
  CHECK(L[0] > 150 && L[2] < 90, "izquierda roja (%d,%d,%d)", L[0], L[1], L[2]);
  CHECK(R[2] > 150 && R[0] < 90, "derecha azul (%d,%d,%d)", R[0], R[1], R[2]);
  CHECK(C[1] > 150 && C[0] < 90, "centro verde (%d,%d,%d)", C[0], C[1], C[2]);
  Img full = decode(jpg);
  double p = psnr(th.px, refThumb(full, FLEXTH_SIDE));
  CHECK(p >= 28.0, "parecido con la referencia: %.1f dB", p);
  std::printf("   400x300 -> %dx%d en %zu bytes, %.1f dB frente a la referencia\n", th.w, th.h, t.b.size(), p);

  // Vertical: tambien cuadrada, sin aplastar.
  auto v = encodeRgb(scene(240, 520), 240, 520);
  Out tv; CHECK(flexThumbFromJpeg(v.data(), v.size(), FLEXTH_SIDE, 82, outW, &tv, &w, &h, tA, tF) == FLEXTH_OK, "vertical");
  Img tvi = decode(tv.b);
  CHECK(tvi.w == FLEXTH_SIDE && tvi.h == FLEXTH_SIDE, "vertical -> cuadrada");

  // Enorme: se decodifica a 1/8 (256x192 de trabajo), no a 2048x1536.
  auto big = encodeRgb(scene(2048, 1536), 2048, 1536, 85);
  Out tb; live = g_live;
  CHECK(flexThumbFromJpeg(big.data(), big.size(), FLEXTH_SIDE, 82, outW, &tb, &w, &h, tA, tF) == FLEXTH_OK && w == 2048,
        "2048x1536 -> miniatura");
  CHECK(g_live == live, "enorme: sin fugas");
  // Diminuta: se amplia.
  auto sm = encodeRgb(scene(60, 40), 60, 40);
  Out ts; CHECK(flexThumbFromJpeg(sm.data(), sm.size(), FLEXTH_SIDE, 82, outW, &ts, &w, &h, tA, tF) == FLEXTH_OK, "60x40 se amplia");
  Img tsi = decode(ts.b);
  CHECK(tsi.w == FLEXTH_SIDE, "diminuta -> %d", tsi.w);
}

static void testInvalid(){
  std::printf("-- lo que no se puede leer, se dice --\n");
  auto jpg = encodeRgb(scene(320, 240), 320, 240);
  std::vector<uint8_t> cut(jpg.begin(), jpg.begin() + (long)jpg.size() / 2);
  Out t; long live = g_live;
  int rc = flexThumbFromJpeg(cut.data(), cut.size(), FLEXTH_SIDE, 82, outW, &t, NULL, NULL, tA, tF);
  CHECK(rc == FLEXTH_ERR_DECODE && g_live == live, "JPEG truncado a la mitad: danado (rc=%d)", rc);
  FILE* f = std::fopen("../fixtures/progressive.jpg", "rb");
  std::vector<uint8_t> pj; uint8_t b[4096]; size_t n;
  if(f){ while((n = std::fread(b, 1, sizeof(b), f)) > 0) pj.insert(pj.end(), b, b + n); std::fclose(f); }
  CHECK(!pj.empty(), "fixture progresivo");
  rc = flexThumbFromJpeg(pj.data(), pj.size(), FLEXTH_SIDE, 82, outW, &t, NULL, NULL, tA, tF);
  CHECK(rc == FLEXTH_ERR_UNSUP, "progresivo: formato que el P4 no abre (rc=%d)", rc);
  const char junk[] = "esto no es un jpeg de ninguna manera";
  CHECK(flexThumbFromJpeg((const uint8_t*)junk, sizeof(junk), FLEXTH_SIDE, 82, outW, &t, NULL, NULL, tA, tF) == FLEXTH_ERR_DECODE,
        "basura: danado");
  CHECK(flexThumbFromJpeg(jpg.data(), jpg.size(), 0, 82, outW, &t, NULL, NULL, tA, tF) == FLEXTH_ERR_ARG, "lado 0");
  for(int k = 0; k < 12; k++){
    Out o; g_n = 0; g_failAt = k; live = g_live;
    rc = flexThumbFromJpeg(jpg.data(), jpg.size(), FLEXTH_SIDE, 82, outW, &o, NULL, NULL, tA, tF);
    g_failAt = -1;
    CHECK(g_live == live, "sin memoria en la reserva %d: sin fugas (rc=%d)", k, rc);
  }
}

static void testPixels(){
  std::printf("-- desde pixeles en memoria (editor y dibujos) --\n");
  auto src = scene(300, 200);
  Out a; CHECK(flexThumbFromPixels(src.data(), 300, 200, 0, FLEXJE_IN_RGB888, FLEXTH_SIDE, 82, outW, &a, tA, tF) == FLEXTH_OK, "RGB888");
  std::vector<uint16_t> p565(300 * 200);
  for(size_t i = 0; i < p565.size(); i++){
    const uint8_t* p = &src[i * 3];
    p565[i] = (uint16_t)(((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3));
  }
  Out b; CHECK(flexThumbFromPixels(p565.data(), 300, 200, 0, FLEXJE_IN_RGB565, FLEXTH_SIDE, 82, outW, &b, tA, tF) == FLEXTH_OK, "RGB565");
  Img ia = decode(a.b), ib = decode(b.b);
  CHECK(ia.w == FLEXTH_SIDE && ib.w == FLEXTH_SIDE && psnr(ia.px, ib.px) > 30.0, "las dos entradas dan casi lo mismo (%.1f dB)", psnr(ia.px, ib.px));
  Out c; CHECK(flexThumbFromPixels(src.data(), 300, 200, 100, FLEXJE_IN_RGB888, FLEXTH_SIDE, 82, outW, &c, tA, tF) == FLEXTH_ERR_ARG,
               "stride menor que la fila: argumento invalido");
}

// ---- AVI MJPEG construido aqui ----
static void p32(std::vector<uint8_t>& v, uint32_t x){ for(int i = 0; i < 4; i++) v.push_back((uint8_t)(x >> (8 * i))); }
static void p16(std::vector<uint8_t>& v, uint16_t x){ v.push_back((uint8_t)x); v.push_back((uint8_t)(x >> 8)); }
static void cc(std::vector<uint8_t>& v, const char* s){ for(int i = 0; i < 4; i++) v.push_back((uint8_t)s[i]); }
static std::vector<uint8_t> makeAvi(const std::vector<std::vector<uint8_t>>& frames, int w, int h, uint32_t usPerFrame){
  std::vector<uint8_t> avih; p32(avih, usPerFrame); p32(avih, 0); p32(avih, 0); p32(avih, 0x10);
  p32(avih, (uint32_t)frames.size()); p32(avih, 0); p32(avih, 1); p32(avih, 0); p32(avih, (uint32_t)w); p32(avih, (uint32_t)h);
  for(int i = 0; i < 4; i++) p32(avih, 0);
  std::vector<uint8_t> strh; cc(strh, "vids"); cc(strh, "MJPG"); p32(strh, 0); p16(strh, 0); p16(strh, 0); p32(strh, 0);
  p32(strh, usPerFrame); p32(strh, 1000000); p32(strh, 0); p32(strh, (uint32_t)frames.size()); p32(strh, 0); p32(strh, 0xFFFFFFFF); p32(strh, 0);
  for(int i = 0; i < 4; i++) p16(strh, 0);
  std::vector<uint8_t> strf; p32(strf, 40); p32(strf, (uint32_t)w); p32(strf, (uint32_t)h); p16(strf, 1); p16(strf, 24); cc(strf, "MJPG");
  p32(strf, (uint32_t)(w * h * 3)); for(int i = 0; i < 4; i++) p32(strf, 0);
  std::vector<uint8_t> strl; cc(strl, "strl");
  cc(strl, "strh"); p32(strl, (uint32_t)strh.size()); strl.insert(strl.end(), strh.begin(), strh.end());
  cc(strl, "strf"); p32(strl, (uint32_t)strf.size()); strl.insert(strl.end(), strf.begin(), strf.end());
  std::vector<uint8_t> hdrl; cc(hdrl, "hdrl");
  cc(hdrl, "avih"); p32(hdrl, (uint32_t)avih.size()); hdrl.insert(hdrl.end(), avih.begin(), avih.end());
  cc(hdrl, "LIST"); p32(hdrl, (uint32_t)strl.size()); hdrl.insert(hdrl.end(), strl.begin(), strl.end());
  std::vector<uint8_t> movi; cc(movi, "movi");
  std::vector<uint8_t> idx;
  for(auto& f : frames){
    uint32_t off = (uint32_t)movi.size();
    cc(movi, "00dc"); p32(movi, (uint32_t)f.size()); movi.insert(movi.end(), f.begin(), f.end());
    if(f.size() & 1) movi.push_back(0);
    cc(idx, "00dc"); p32(idx, 0x10); p32(idx, off); p32(idx, (uint32_t)f.size());
  }
  std::vector<uint8_t> body; cc(body, "AVI ");
  cc(body, "LIST"); p32(body, (uint32_t)hdrl.size()); body.insert(body.end(), hdrl.begin(), hdrl.end());
  cc(body, "LIST"); p32(body, (uint32_t)movi.size()); body.insert(body.end(), movi.begin(), movi.end());
  cc(body, "idx1"); p32(body, (uint32_t)idx.size()); body.insert(body.end(), idx.begin(), idx.end());
  std::vector<uint8_t> out; cc(out, "RIFF"); p32(out, (uint32_t)body.size()); out.insert(out.end(), body.begin(), body.end());
  return out;
}
struct Mem { std::vector<uint8_t> d; uint32_t pos = 0; };
static int mRead(void* c, void* b, uint32_t n){ Mem* m = (Mem*)c; if(m->pos >= m->d.size()) return 0;
  uint32_t k = (uint32_t)m->d.size() - m->pos; if(n > k) n = k; std::memcpy(b, &m->d[m->pos], n); m->pos += n; return (int)n; }
static bool mSeek(void* c, uint32_t o){ Mem* m = (Mem*)c; if(o > m->d.size()) return false; m->pos = o; return true; }
static uint32_t mSize(void* c){ return (uint32_t)((Mem*)c)->d.size(); }

static void testAvi(){
  std::printf("-- primer fotograma de un AVI MJPEG --\n");
  std::vector<std::vector<uint8_t>> frames;
  for(int i = 0; i < 5; i++) frames.push_back(encodeRgb(scene(320, 180), 320, 180, 70));
  Mem m; m.d = makeAvi(frames, 320, 180, 66666);
  FlexMediaIO io; io.read = mRead; io.seek = mSeek; io.size = mSize; io.ctx = &m;
  Out t; int w = 0, h = 0; uint32_t dur = 0;
  long live = g_live;
  int rc = flexThumbFromAvi(&io, FLEXTH_SIDE, 82, outW, &t, &w, &h, &dur, tA, tF);
  CHECK(rc == FLEXTH_OK && w == 320 && h == 180, "AVI: miniatura y dimensiones (rc=%d %dx%d)", rc, w, h);
  CHECK(dur >= 330 && dur <= 336, "AVI: duracion 5 x 66,7 ms = %u ms", dur);
  CHECK(g_live == live, "AVI: sin fugas");
  Img ti = decode(t.b);
  CHECK(ti.w == FLEXTH_SIDE, "AVI: cuadrada");
  // Otro codec: se dice.
  Mem m2; m2.d = m.d;
  for(size_t i = 0; i + 8 < m2.d.size(); i++) if(!std::memcmp(&m2.d[i], "vidsMJPG", 8)){ std::memcpy(&m2.d[i + 4], "H264", 4); break; }
  io.ctx = &m2;
  CHECK(flexThumbFromAvi(&io, FLEXTH_SIDE, 82, outW, &t, &w, &h, &dur, tA, tF) == FLEXTH_ERR_UNSUP, "AVI H.264: no se abre");
}

int main(){
  std::printf("=== FlexOS · miniaturas de la biblioteca de medios ===\n");
  testJpeg();
  testInvalid();
  testPixels();
  testAvi();
  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
