// #############################################################
//  test_fuzzmedia.cpp  ·  archivos DANADOS contra los lectores del firmware
// #############################################################
//
//  Un video o una foto llegan por el movil, por la web o de una camara, y
//  pueden venir cortados, corrompidos o hechos a proposito. Lo que se exige
//  aqui a EXACTAMENTE el codigo que va a la placa (FlexOS_JPEG.cpp,
//  FlexOS_Media.cpp y FlexOS_MediaThumb.cpp):
//
//    · Ningun acceso fuera de limites ni comportamiento indefinido. Se
//      compila con ASan y UBSan SIN recuperacion: el primer aviso aborta la
//      prueba (asi fallaba la IDCT, que desbordaba un int con coeficientes
//      absurdos).
//    · Trabajo ACOTADO por llamada: ninguna llamada al demultiplexor puede
//      leer mas de FLEXAVI_SCAN_MAX + FLEXAVI_SEEK_SKIPS cabeceras. Un bucle
//      sin fin en el hilo de la interfaz es un reinicio por watchdog, y uno
//      en la tarea de medios deja la biblioteca parada.
//    · Las filas que entrega el decodificador caen dentro de la imagen.
//
//  Es determinista (semilla fija): si algo falla, falla siempre igual.
//  Las semillas son las fotos de ../fixtures y AVI construidos aqui con
//  esas fotos como fotogramas (con y sin idx1, con audio, con trozos vacios).
// #############################################################
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#include "../../FlexOS_Ultra/FlexOS_JPEG.h"
#include "../../FlexOS_Ultra/FlexOS_Media.h"
#include "../../FlexOS_Ultra/FlexOS_MediaThumb.h"

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(){ rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)rs; }
static uint32_t rn(uint32_t n){ return n ? rnd() % n : 0; }

static std::vector<uint8_t> readFile(const char* p){
  std::vector<uint8_t> v; FILE* f = std::fopen(p, "rb"); if(!f) return v;
  std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
  v.resize((size_t)n); if(std::fread(v.data(), 1, (size_t)n, f) != (size_t)n) v.clear(); std::fclose(f); return v;
}

// Mutaciones tipicas de un archivo roto: bits sueltos, bytes al azar,
// cortes, marcadores inventados, tramos borrados o insertados, longitudes
// pequenas y enteros de 32 bits arbitrarios (tamanos de trozo absurdos).
static void mutate(std::vector<uint8_t>& d){
  int k = 1 + (int)rn(6);
  for(int i = 0; i < k && !d.empty(); i++){
    switch(rn(9)){
      case 0: d[rn((uint32_t)d.size())] ^= (uint8_t)(1u << rn(8)); break;
      case 1: d[rn((uint32_t)d.size())] = (uint8_t)rnd(); break;
      case 2: d.resize(rn((uint32_t)d.size()) + 1); break;
      case 3: { size_t p = rn((uint32_t)d.size()); d.insert(d.begin() + (long)p, (uint8_t)0xFF);
                d.insert(d.begin() + (long)p + 1, (uint8_t)(0xC0 + rn(64))); } break;
      case 4: { size_t p = rn((uint32_t)d.size()); if(p + 4 <= d.size()){ d[p] = 0xFF; d[p + 1] = (uint8_t)rnd(); } } break;
      case 5: { size_t p = rn((uint32_t)d.size()), n = rn(64); if(p + n <= d.size()) d.erase(d.begin() + (long)p, d.begin() + (long)(p + n)); } break;
      case 6: { size_t p = rn((uint32_t)d.size()), n = rn(64); for(size_t j = 0; j < n; j++) d.insert(d.begin() + (long)p, (uint8_t)rnd()); } break;
      case 7: { size_t p = rn((uint32_t)d.size()); if(p + 2 <= d.size()){ d[p] = (uint8_t)rn(3); d[p + 1] = (uint8_t)rnd(); } } break;
      case 8: { size_t p = rn((uint32_t)d.size()); if(p + 4 <= d.size()){ uint32_t v = rnd(); std::memcpy(&d[p], &v, 4); } } break;
    }
  }
}

// ---- JPEG ----
struct Sink { int w, h; long rows; int div; bool bad; };
static bool rowCb(void* u, int y, int w, const uint16_t* rgb){
  Sink* s = (Sink*)u; s->rows++;
  if(y < 0 || y >= s->h || w > s->w) s->bad = true;
  volatile uint16_t acc = 0; for(int i = 0; i < w; i++) acc ^= rgb[i]; (void)acc;   // ASan: toda la fila es legible
  return true;
}
static bool row888(void* u, int y, int w, const uint8_t* rgb){
  Sink* s = (Sink*)u; s->rows++;
  if(y < 0 || y >= s->h || w > s->w) s->bad = true;
  volatile uint8_t acc = 0; for(int i = 0; i < w * 3; i++) acc ^= rgb[i]; (void)acc;
  return true;
}
struct Mem { const uint8_t* p; size_t n, pos; int chunk; };
static int memRd(void* c, uint8_t* buf, size_t n){
  Mem* m = (Mem*)c; size_t left = m->n - m->pos;
  if(m->chunk > 0 && n > (size_t)m->chunk) n = (size_t)m->chunk;
  if(n > left) n = left;
  std::memcpy(buf, m->p + m->pos, n); m->pos += n; return (int)n;
}
static int pickFn(void* u, int w, int h){
  Sink* s = (Sink*)u; int S = s->div;
  int ow = (w + S - 1) / S, oh = (h + S - 1) / S;
  if((long)ow * oh > 4000000L) return 0;
  s->w = ow; s->h = oh;
  return S;
}
static int g_rowsOut = 0;
static void fuzzJpegOne(const std::vector<uint8_t>& d){
  for(int k = 0; k < 2; k++){
    int mw = k ? 64 + (int)rn(900) : 0, mh = k ? 64 + (int)rn(900) : 0;
    FlexJpegInfo inf; std::memset(&inf, 0, sizeof(inf));
    if(flexJpegProbe(d.data(), d.size(), &inf) != FLEXJPG_OK) continue;
    if((long)inf.width * inf.height > 16000000L) continue;
    Sink s = { inf.width, inf.height, 0, 1, false };
    flexJpegDecode(d.data(), d.size(), mw, mh, 0, &inf, rowCb, &s, nullptr, nullptr);
    if(s.bad) g_rowsOut++;
  }
  {
    Sink s = { 0, 0, 0, 1 << rn(4), false };
    Mem m = { d.data(), d.size(), 0, (int)(1 + rn(4096)) };
    flexJpegDecodeStream(memRd, &m, 0, 0, 0, pickFn, nullptr, rowCb, &s, nullptr, nullptr);
    if(s.bad) g_rowsOut++;
  }
  {
    Sink s = { 0, 0, 0, 1 << rn(4), false };
    Mem m = { d.data(), d.size(), 0, (int)(1 + rn(9000)) };
    flexJpegDecode888Stream(memRd, &m, 0, 0, 0, pickFn, nullptr, row888, &s, nullptr, nullptr);
    if(s.bad) g_rowsOut++;
  }
}

// ---- AVI ----
static void put32(std::vector<uint8_t>& v, uint32_t x){ for(int i = 0; i < 4; i++) v.push_back((uint8_t)(x >> (8 * i))); }
static void put16(std::vector<uint8_t>& v, uint16_t x){ v.push_back((uint8_t)x); v.push_back((uint8_t)(x >> 8)); }
static void putS(std::vector<uint8_t>& v, const char* s){ for(int i = 0; i < 4; i++) v.push_back((uint8_t)s[i]); }
static std::vector<uint8_t> makeAvi(const std::vector<std::vector<uint8_t>>& frames, int w, int h, int fps, bool idx, bool audio){
  std::vector<uint8_t> movi, index;
  uint32_t maxF = 0;
  for(size_t i = 0; i < frames.size(); i++){
    uint32_t off = (uint32_t)movi.size() + 4;
    putS(movi, "00dc"); put32(movi, (uint32_t)frames[i].size());
    movi.insert(movi.end(), frames[i].begin(), frames[i].end());
    if(frames[i].size() & 1) movi.push_back(0);
    putS(index, "00dc"); put32(index, 0x10); put32(index, off); put32(index, (uint32_t)frames[i].size());
    if(frames[i].size() > maxF) maxF = (uint32_t)frames[i].size();
    if(audio){
      uint32_t ao = (uint32_t)movi.size() + 4;
      putS(movi, "01wb"); put32(movi, 100); for(int j = 0; j < 100; j++) movi.push_back((uint8_t)j);
      putS(index, "01wb"); put32(index, 0); put32(index, ao); put32(index, 100);
    }
  }
  std::vector<uint8_t> hdrl;
  putS(hdrl, "avih"); put32(hdrl, 56);
  put32(hdrl, 1000000u / (uint32_t)fps); put32(hdrl, maxF * (uint32_t)fps); put32(hdrl, 0); put32(hdrl, 0x10);
  put32(hdrl, (uint32_t)frames.size()); put32(hdrl, 0); put32(hdrl, audio ? 2 : 1); put32(hdrl, maxF + 8);
  put32(hdrl, (uint32_t)w); put32(hdrl, (uint32_t)h); for(int i = 0; i < 4; i++) put32(hdrl, 0);
  std::vector<uint8_t> strl;
  putS(strl, "strh"); put32(strl, 56);
  putS(strl, "vids"); putS(strl, "MJPG"); put32(strl, 0); put16(strl, 0); put16(strl, 0); put32(strl, 0);
  put32(strl, 1); put32(strl, (uint32_t)fps); put32(strl, 0); put32(strl, (uint32_t)frames.size()); put32(strl, maxF + 8);
  put32(strl, 0); put32(strl, 0); put16(strl, 0); put16(strl, 0); put16(strl, (uint16_t)w); put16(strl, (uint16_t)h);
  putS(strl, "strf"); put32(strl, 40);
  put32(strl, 40); put32(strl, (uint32_t)w); put32(strl, (uint32_t)h); put16(strl, 1); put16(strl, 24); putS(strl, "MJPG");
  put32(strl, (uint32_t)(w * h * 3)); for(int i = 0; i < 4; i++) put32(strl, 0);
  std::vector<uint8_t> strlList; putS(strlList, "LIST"); put32(strlList, (uint32_t)strl.size() + 4); putS(strlList, "strl");
  strlList.insert(strlList.end(), strl.begin(), strl.end());
  hdrl.insert(hdrl.end(), strlList.begin(), strlList.end());
  std::vector<uint8_t> out;
  putS(out, "RIFF"); put32(out, 0); putS(out, "AVI ");
  putS(out, "LIST"); put32(out, (uint32_t)hdrl.size() + 4); putS(out, "hdrl"); out.insert(out.end(), hdrl.begin(), hdrl.end());
  putS(out, "LIST"); put32(out, (uint32_t)movi.size() + 4); putS(out, "movi"); out.insert(out.end(), movi.begin(), movi.end());
  if(idx){ putS(out, "idx1"); put32(out, (uint32_t)index.size()); out.insert(out.end(), index.begin(), index.end()); }
  uint32_t riff = (uint32_t)out.size() - 8; std::memcpy(&out[4], &riff, 4);
  return out;
}
struct AMem { const uint8_t* p; uint32_t n, pos; long reads; };
static int aRead(void* c, void* buf, uint32_t n){
  AMem* m = (AMem*)c; m->reads++;
  if(m->pos > m->n) return -1;
  uint32_t left = m->n - m->pos; if(n > left) n = left;
  std::memcpy(buf, m->p + m->pos, n); m->pos += n; return (int)n;
}
static bool aSeek(void* c, uint32_t off){ AMem* m = (AMem*)c; if(off > m->n) return false; m->pos = off; return true; }
static uint32_t aSize(void* c){ return ((AMem*)c)->n; }

static long g_worstReads = 0, g_overBudget = 0, g_overRead = 0;
static void fuzzAviOne(const std::vector<uint8_t>& d){
  AMem m = { d.data(), (uint32_t)d.size(), 0, 0 };
  FlexMediaIO io = { aRead, aSeek, aSize, &m };
  static FlexAviCtx a;
  if(flexAviOpen(&a, &io) != FLEXAVI_OK) return;
  std::vector<uint8_t> buf(64 * 1024);
  // Tope de lecturas por llamada: el recorrido de trozos ajenos, el de
  // fotogramas al buscar, la muestra de idx1 (una lectura por 32 entradas)
  // y un margen para la cabecera y los datos.
  const long budget = (long)FLEXAVI_SCAN_MAX + (long)FLEXAVI_SEEK_SKIPS * 2 + (long)(d.size() / (32 * 16)) + 64;
  for(int i = 0; i < 300; i++){
    int op = (int)rn(10);
    long r0 = m.reads;
    int r;
    if(op < 6){
      uint32_t fn;
      r = flexAviReadFrame(&a, buf.data(), (uint32_t)buf.size(), &fn);
      if(r > (int)buf.size()) g_overRead++;
      if(r == FLEXAVI_ERR_TOOBIG) flexAviSkipFrame(&a);
    }
    else if(op < 8) r = flexAviSkipFrame(&a);
    else r = flexAviSeekFrame(&a, rn(a.frames + 20));
    long used = m.reads - r0;
    if(used > g_worstReads) g_worstReads = used;
    if(used > budget) g_overBudget++;
    if(r == FLEXAVI_ERR_EOF && rn(3) == 0) flexAviSeekFrame(&a, 0);
    (void)flexAviDurationMs(&a);
  }
}
// La miniatura es lo que corre en la tarea de medios al indexar y en la
// validacion de una subida: tampoco puede colgarse ni leer fuera.
static bool thNull(void*, const uint8_t*, size_t){ return true; }
static void fuzzThumbOne(const std::vector<uint8_t>& d){
  AMem m = { d.data(), (uint32_t)d.size(), 0, 0 };
  FlexMediaIO io = { aRead, aSeek, aSize, &m };
  int w = 0, h = 0; uint32_t dur = 0;
  flexThumbFromAvi(&io, FLEXTH_SIDE, FLEXTH_QUALITY, thNull, nullptr, &w, &h, &dur, nullptr, nullptr);
  if(m.reads > g_worstReads) g_worstReads = m.reads;
}

int main(int argc, char** argv){
  const char* dir = argc > 1 ? argv[1] : "../fixtures";
  long iters = argc > 2 ? std::atol(argv[2]) : 1500;
  std::printf("=== FlexOS · archivos danados contra JPEG, AVI y miniaturas ===\n");
  const char* names[] = { "grad420.jpg", "grad444.jpg", "gray.jpg", "odd420.jpg", "odd444.jpg", "page240.jpg",
                          "page480.jpg", "restart.jpg", "rst1.jpg", "samp422.jpg", "samp440.jpg", "progressive.jpg" };
  std::vector<std::vector<uint8_t>> seeds;
  for(const char* n : names){ std::string p = std::string(dir) + "/" + n; auto v = readFile(p.c_str()); if(!v.empty()) seeds.push_back(v); }
  CHECK(seeds.size() >= 10, "semillas JPEG: %zu (faltan ficheros en %s)", seeds.size(), dir);
  if(seeds.empty()){ std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail); return 1; }

  for(long i = 0; i < iters; i++){
    std::vector<uint8_t> d = seeds[rn((uint32_t)seeds.size())];
    mutate(d);
    fuzzJpegOne(d);
  }
  CHECK(g_rowsOut == 0, "JPEG: %d decodificaciones entregaron filas fuera de la imagen", g_rowsOut);
  std::printf("  JPEG: %ld archivos danados, ni un acceso fuera ni un desbordamiento\n", iters);

  std::vector<std::vector<uint8_t>> fr;
  for(size_t i = 0; i + 1 < seeds.size(); i++) fr.push_back(seeds[i]);   // sin el progresivo
  long aviN = 0;
  for(int variant = 0; variant < 4; variant++){
    std::vector<std::vector<uint8_t>> frames;
    for(int k = 0; k < 30; k++) frames.push_back(fr[(size_t)k % fr.size()]);
    if(variant == 3){ frames[5].clear(); frames[6].clear(); }           // trozos vacios (00dc de 0 bytes)
    auto base = makeAvi(frames, 64, 48, 12, variant != 1, variant == 2);
    for(long i = 0; i < iters / 4; i++){
      std::vector<uint8_t> d = base;
      if(i) mutate(d);
      fuzzAviOne(d);
      if((i & 7) == 0) fuzzThumbOne(d);
      aviN++;
    }
  }
  CHECK(g_overRead == 0, "AVI: %ld lecturas devolvieron mas que el buffer", g_overRead);
  CHECK(g_overBudget == 0, "AVI: %ld llamadas pasaron del tope de trabajo (peor: %ld lecturas)", g_overBudget, g_worstReads);
  std::printf("  AVI: %ld archivos danados (y sus miniaturas); peor llamada: %ld lecturas\n", aviN, g_worstReads);
  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
