// #############################################################
//  test_videdit.cpp  ·  el nucleo del editor de video de la Galeria
// #############################################################
//
//  El MISMO codigo que va a la placa (FlexOS_VidEdit, con FlexOS_ImgEdit,
//  el decodificador y el codificador JPEG y el lector de AVI del firmware)
//  sobre AVIs construidos aqui. Cada fotograma lleva su numero pintado (un
//  gris que se puede volver a leer despues de recodificar) y cuatro
//  cuadrantes de colores para saber hacia donde giro. Lo que importa:
//    · editar solo mueve parametros: tramos, velocidad, recorte, deshacer;
//    · sin tocar los pixeles, los fotogramas salen BYTE A BYTE iguales;
//    · recortar el tiempo, dividir, quitar partes y cambiar la velocidad
//      eligen exactamente los fotogramas que tocan;
//    · recortar, girar, filtrar y poner texto salen en los pixeles, y el
//      AVI que sale lo abre el lector del reproductor, con su indice;
//    · el audio original sale igual a 1x/100 %, a la mitad al 50 %, sin pista
//      silenciado, y remuestreado con la velocidad (PCM e IMA ADPCM); un
//      audio que el P4 no decodifica se quita, sin romper nada;
//    · la portada llega a la miniatura (FlexOS_MediaThumb);
//    · disco lleno, tope de tamano, cancelar y un fotograma danado acaban
//      en un error limpio, sin memoria viva, y exportar NO reserva memoria
//      fotograma a fotograma (la arena se reutiliza).
#include "../../FlexOS_Ultra/FlexOS_VidEdit.h"
#include "../../FlexOS_Ultra/FlexOS_MediaThumb.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

// ---- Reservas contadas (las LARGAS de la exportacion) ----
static long g_live = 0, g_allocs = 0;
static bool g_allocFail = false;
static void* tA(size_t n){ if(g_allocFail) return nullptr; void* p = std::malloc(n ? n : 1); if(p){ g_live++; g_allocs++; } return p; }
static void tF(void* p){ if(p){ g_live--; std::free(p); } }

// ---- Archivo en memoria ----
struct Mem { std::vector<uint8_t> d; uint32_t pos = 0; long failReadAt = -1; };
static int mRead(void* c, void* buf, uint32_t n){
  Mem* m = (Mem*)c;
  if(m->failReadAt >= 0 && (long)m->pos >= m->failReadAt) return -1;
  uint32_t k = m->pos >= m->d.size() ? 0 : (uint32_t)(m->d.size() - m->pos < n ? m->d.size() - m->pos : n);
  if(k) std::memcpy(buf, m->d.data() + m->pos, k);
  m->pos += k;
  return (int)k;
}
static bool mSeek(void* c, uint32_t off){ Mem* m = (Mem*)c; if(off > m->d.size()) return false; m->pos = off; return true; }
static uint32_t mSize(void* c){ return (uint32_t)((Mem*)c)->d.size(); }
static FlexMediaIO ioOf(Mem* m){ FlexMediaIO io; io.read = mRead; io.seek = mSeek; io.size = mSize; io.ctx = m; return io; }

struct Out { std::vector<uint8_t> d; uint32_t pos = 0; long failAt = -1; };
static bool oWrite(void* c, const void* p, size_t n){
  Out* o = (Out*)c;
  if(o->failAt >= 0 && (long)(o->pos + n) > o->failAt) return false;
  if(o->pos + n > o->d.size()) o->d.resize(o->pos + n);
  std::memcpy(o->d.data() + o->pos, p, n);
  o->pos += (uint32_t)n;
  return true;
}
static bool oSeek(void* c, uint32_t off){ Out* o = (Out*)c; if(off > o->d.size()) return false; o->pos = off; return true; }

// ---- Fotogramas con su numero ----
// Cuadrantes: arriba-izquierda ROJO, arriba-derecha GRIS con el numero
// (30 + 22 * (i % 10)), abajo-izquierda VERDE, abajo-derecha AZUL.
static int grayOf(int i){ return 30 + 22 * (i % 10); }
static std::vector<uint8_t> framePixels(int W, int H, int idx){
  std::vector<uint8_t> px((size_t)W * H * 3);
  for(int y = 0; y < H; y++) for(int x = 0; x < W; x++){
    uint8_t* p = &px[((size_t)y * W + x) * 3];
    bool top = y < H / 2, left = x < W / 2;
    if(top && left){ p[0] = 220; p[1] = 30; p[2] = 30; }
    else if(top){ int g = grayOf(idx); p[0] = p[1] = p[2] = (uint8_t)g; }
    else if(left){ p[0] = 30; p[1] = 200; p[2] = 40; }
    else { p[0] = 30; p[1] = 40; p[2] = 220; }
  }
  return px;
}
static bool vOut(void* c, const uint8_t* d, size_t n){ auto* v = (std::vector<uint8_t>*)c; v->insert(v->end(), d, d + n); return true; }
static std::vector<uint8_t> frameJpeg(int W, int H, int idx){
  auto px = framePixels(W, H, idx);
  std::vector<uint8_t> out;
  FlexJeCfg c; c.width = W; c.height = H; c.quality = 92; c.subsampling = FLEXJE_SUB_420; c.input = FLEXJE_IN_RGB888;
  flexJpegEncodeMem(&c, px.data(), (size_t)W * 3, vOut, &out, nullptr, nullptr);
  return out;
}
struct Img { std::vector<uint8_t> px; int w = 0, h = 0; };
static Img decodeJpeg(const uint8_t* j, size_t n){
  Img im; FlexJpegInfo inf; std::memset(&inf, 0, sizeof(inf));
  if(flexJpegProbe(j, n, &inf) != FLEXJPG_OK) return im;
  im.w = inf.width; im.h = inf.height; im.px.resize((size_t)im.w * im.h * 3);
  flexJpegDecode888(j, n, 0, 0, 0, &inf,
    [](void* u, int y, int w, const uint8_t* rgb) -> bool { Img* q = (Img*)u; std::memcpy(&q->px[(size_t)y * q->w * 3], rgb, (size_t)w * 3); return true; },
    &im, nullptr, nullptr);
  return im;
}
static const uint8_t* pix(const Img& im, int x, int y){ return &im.px[((size_t)y * im.w + x) * 3]; }
static bool isRed(const uint8_t* p){ return p[0] > 170 && p[1] < 90 && p[2] < 90; }
static bool isGreen(const uint8_t* p){ return p[1] > 150 && p[0] < 90 && p[2] < 100; }
static bool isBlue(const uint8_t* p){ return p[2] > 170 && p[0] < 90 && p[1] < 100; }
static bool isGray(const uint8_t* p){ return std::abs(p[0] - p[1]) < 12 && std::abs(p[1] - p[2]) < 12; }
static int idxOfGray(int g){ return (g - 30 + 11) / 22; }

// ---- Constructor de AVI ----
struct AviSpec {
  int w = 64, h = 48; uint32_t us = 100000;                   // 10 fps por defecto
  std::vector<std::vector<uint8_t>> frames;                    // vacio = trozo vacio (repetir)
  int audTag = 0, audCh = 1, audRate = 8000, audBits = 16, audBa = 2, audSpb = 0;
  std::vector<std::vector<uint8_t>> aud;                       // un trozo de audio antes de cada fotograma
  bool audioFirst = false, withIdx = true;
  const char* codec = "MJPG";
  uint32_t declared = 0;                                         // 0 = los que hay
};
static void u32(std::vector<uint8_t>& v, uint32_t x){ for(int i = 0; i < 4; i++) v.push_back((uint8_t)(x >> (8 * i))); }
static void u16(std::vector<uint8_t>& v, uint16_t x){ v.push_back((uint8_t)x); v.push_back((uint8_t)(x >> 8)); }
static void cc(std::vector<uint8_t>& v, const char* t){ for(int i = 0; i < 4; i++) v.push_back((uint8_t)t[i]); }
static std::vector<uint8_t> chunk(const char* id, const std::vector<uint8_t>& p){
  std::vector<uint8_t> v; cc(v, id); u32(v, (uint32_t)p.size()); v.insert(v.end(), p.begin(), p.end());
  if(p.size() & 1) v.push_back(0);
  return v;
}
static std::vector<uint8_t> list(const char* t, const std::vector<uint8_t>& p){
  std::vector<uint8_t> v; cc(v, "LIST"); u32(v, (uint32_t)p.size() + 4); cc(v, t); v.insert(v.end(), p.begin(), p.end());
  return v;
}
static std::vector<uint8_t> buildAvi(const AviSpec& a){
  const bool aud = a.audTag != 0;
  const int vs = (aud && a.audioFirst) ? 1 : 0, as = vs ? 0 : 1;
  uint32_t n = (uint32_t)a.frames.size(), nd = a.declared ? a.declared : n;
  std::vector<uint8_t> avih; u32(avih, a.us); u32(avih, 0); u32(avih, 0); u32(avih, 0x10); u32(avih, nd); u32(avih, 0);
  u32(avih, aud ? 2 : 1); u32(avih, 0); u32(avih, (uint32_t)a.w); u32(avih, (uint32_t)a.h); for(int i = 0; i < 4; i++) u32(avih, 0);
  std::vector<uint8_t> vstrh; cc(vstrh, "vids"); cc(vstrh, a.codec); u32(vstrh, 0); u16(vstrh, 0); u16(vstrh, 0); u32(vstrh, 0);
  u32(vstrh, 1); u32(vstrh, 1000000u / a.us); u32(vstrh, 0); u32(vstrh, nd); u32(vstrh, 65536); u32(vstrh, 0); u32(vstrh, 0);
  u32(vstrh, 0); u32(vstrh, 0);
  std::vector<uint8_t> vstrf; u32(vstrf, 40); u32(vstrf, (uint32_t)a.w); u32(vstrf, (uint32_t)a.h); u16(vstrf, 1); u16(vstrf, 24);
  cc(vstrf, a.codec); for(int i = 0; i < 5; i++) u32(vstrf, 0);
  std::vector<uint8_t> vstrl = chunk("strh", vstrh); { auto t = chunk("strf", vstrf); vstrl.insert(vstrl.end(), t.begin(), t.end()); }
  std::vector<uint8_t> astrl;
  if(aud){
    std::vector<uint8_t> h; cc(h, "auds"); u32(h, 0); u32(h, 0); u16(h, 0); u16(h, 0); u32(h, 0);
    u32(h, (uint32_t)a.audBa); u32(h, (uint32_t)(a.audRate * a.audBa)); u32(h, 0); u32(h, 0); u32(h, 4096); u32(h, 0);
    u32(h, (uint32_t)a.audBa); u32(h, 0); u32(h, 0);
    std::vector<uint8_t> f; u16(f, (uint16_t)a.audTag); u16(f, (uint16_t)a.audCh); u32(f, (uint32_t)a.audRate);
    u32(f, (uint32_t)(a.audRate * a.audBa)); u16(f, (uint16_t)a.audBa); u16(f, (uint16_t)a.audBits);
    if(a.audTag == 0x11){ u16(f, 2); u16(f, (uint16_t)a.audSpb); } else u16(f, 0);
    astrl = chunk("strh", h); auto t = chunk("strf", f); astrl.insert(astrl.end(), t.begin(), t.end());
  }
  std::vector<uint8_t> hdrl = chunk("avih", avih);
  if(aud && a.audioFirst){ auto t = list("strl", astrl); hdrl.insert(hdrl.end(), t.begin(), t.end()); }
  { auto t = list("strl", vstrl); hdrl.insert(hdrl.end(), t.begin(), t.end()); }
  if(aud && !a.audioFirst){ auto t = list("strl", astrl); hdrl.insert(hdrl.end(), t.begin(), t.end()); }
  char vid[5], aid[5];
  std::snprintf(vid, sizeof(vid), "%02ddc", vs); std::snprintf(aid, sizeof(aid), "%02dwb", as);
  std::vector<uint8_t> movi, idx1;
  for(uint32_t i = 0; i < n; i++){
    if(aud && i < a.aud.size() && !a.aud[i].empty()){
      uint32_t off = 4u + (uint32_t)movi.size();
      auto c = chunk(aid, a.aud[i]); movi.insert(movi.end(), c.begin(), c.end());
      cc(idx1, aid); u32(idx1, 0x10); u32(idx1, off); u32(idx1, (uint32_t)a.aud[i].size());
    }
    uint32_t off = 4u + (uint32_t)movi.size();
    auto c = chunk(vid, a.frames[i]); movi.insert(movi.end(), c.begin(), c.end());
    cc(idx1, vid); u32(idx1, 0x10); u32(idx1, off); u32(idx1, (uint32_t)a.frames[i].size());
  }
  std::vector<uint8_t> body = list("hdrl", hdrl);
  { auto t = list("movi", movi); body.insert(body.end(), t.begin(), t.end()); }
  if(a.withIdx){ auto t = chunk("idx1", idx1); body.insert(body.end(), t.begin(), t.end()); }
  std::vector<uint8_t> out; cc(out, "RIFF"); u32(out, (uint32_t)body.size() + 4); cc(out, "AVI ");
  out.insert(out.end(), body.begin(), body.end());
  return out;
}
static AviSpec videoSpec(int n, int W = 64, int H = 48, uint32_t us = 100000){
  AviSpec a; a.w = W; a.h = H; a.us = us;
  for(int i = 0; i < n; i++) a.frames.push_back(frameJpeg(W, H, i));
  return a;
}
// PCM de 16 bits: la muestra global k vale (k * 7) % 20000 - 10000 (y el otro canal, su negativo).
static int16_t rampAt(uint32_t k){ return (int16_t)((int)((k * 7u) % 20000u) - 10000); }
static void addPcm16(AviSpec& a, int ch){
  a.audTag = 1; a.audCh = ch; a.audBits = 16; a.audBa = 2 * ch; a.audRate = 8000;
  uint32_t per = (uint32_t)((uint64_t)a.audRate * a.us / 1000000u);
  for(size_t i = 0; i < a.frames.size(); i++){
    std::vector<uint8_t> c;
    for(uint32_t s = 0; s < per; s++){
      int16_t v = rampAt((uint32_t)i * per + s);
      u16(c, (uint16_t)v);
      if(ch == 2) u16(c, (uint16_t)(int16_t)(-v));
    }
    a.aud.push_back(c);
  }
}

// ---- Reader de lo exportado: trozos de 'movi' en orden ----
struct Ck { char id[5]; uint32_t off, len; };
static std::vector<Ck> moviChunks(const std::vector<uint8_t>& f){
  std::vector<Ck> out;
  size_t p = 12;
  while(p + 12 <= f.size()){
    if(!std::memcmp(&f[p], "LIST", 4) && !std::memcmp(&f[p + 8], "movi", 4)){
      uint32_t len = f[p + 4] | (f[p + 5] << 8) | (f[p + 6] << 16) | ((uint32_t)f[p + 7] << 24);
      size_t q = p + 12, end = p + 8 + len;
      while(q + 8 <= end && q + 8 <= f.size()){
        Ck c; std::memcpy(c.id, &f[q], 4); c.id[4] = 0;
        c.len = f[q + 4] | (f[q + 5] << 8) | (f[q + 6] << 16) | ((uint32_t)f[q + 7] << 24);
        c.off = (uint32_t)q + 8;
        out.push_back(c);
        q += 8 + c.len + (c.len & 1);
      }
      break;
    }
    if(!std::memcmp(&f[p], "LIST", 4)){ p += 12; continue; }
    uint32_t len = f[p + 4] | (f[p + 5] << 8) | (f[p + 6] << 16) | ((uint32_t)f[p + 7] << 24);
    p += 8 + len + (len & 1);
  }
  return out;
}
static std::vector<Ck> only(const std::vector<Ck>& v, const char* id){
  std::vector<Ck> o; for(auto& c : v) if(!std::strcmp(c.id, id)) o.push_back(c); return o;
}

// Exporta con la configuracion de siempre. Devuelve el codigo final y deja
// el archivo en `out`.
struct Run { int rc = 0; FlexVeExport x; long allocsInSteps = 0; int steps = 0; };
static FlexImgEdit g_ie;
static int g_ticks = 0, g_cancelAt = -1;
static bool tick(void*){ g_ticks++; return !(g_cancelAt >= 0 && g_ticks > g_cancelAt); }
static Run* doExport(Mem* src, const FlexVeSource* s, const FlexVeParams* p, Out* out, int seg0 = 0, int seg1 = -1,
                     int ow = 0, int oh = 0, uint32_t maxBytes = 0){
  static Run r; r.rc = 0; r.allocsInSteps = 0; r.steps = 0;
  FlexVeExportCfg c; std::memset(&c, 0, sizeof(c));
  c.io = ioOf(src); c.src = s; c.params = p; c.seg0 = seg0; c.seg1 = seg1 < 0 ? p->nSeg - 1 : seg1;
  c.write = oWrite; c.seek = oSeek; c.outCtx = out; c.outW = ow; c.outH = oh; c.maxBytes = maxBytes;
  c.ie = &g_ie; c.alloc = tA; c.free = tF; c.tick = tick; c.tickCtx = nullptr;
  g_ticks = 0;
  int rc = flexVeExportBegin(&r.x, &c);
  if(rc == FLEXVE_OK){
    long a0 = g_allocs;
    for(;;){ int k = flexVeExportStep(&r.x); r.steps++; if(k <= 0){ rc = k; break; } }
    r.allocsInSteps = g_allocs - a0;
    if(rc == 0) rc = flexVeExportFinish(&r.x);
  }
  r.rc = rc;
  return &r;
}
static FlexVeSource probeOf(Mem* m, int* rcOut = nullptr){
  FlexVeSource s; static FlexAviCtx ctx;
  FlexMediaIO io = ioOf(m);
  int rc = flexVeProbe(&io, &s, &ctx, nullptr, nullptr);
  if(rcOut) *rcOut = rc;
  return s;
}
static void initParams(FlexVeEdit* e, const FlexVeSource& s){ flexVeInit(e, s.frames, s.usPerFrame, s.w, s.h); }

// Texto de prueba: cada letra es una caja llena (el editor de fotos usa la misma idea).
static int boxText(void*, const char* s, int hpx, int row, int col0, uint8_t* cov, int covW){
  int cw = hpx / 2 + 1, n = (int)std::strlen(s), w = n * cw;
  if(row < 0) return w;
  for(int i = 0; i < covW; i++){ int c = col0 + i; cov[i] = (c >= 0 && c < w && row > 0 && row < hpx - 1) ? 255 : 0; }
  return w;
}

// #############################################################
static void testParams(){
  std::printf("-- parametros, historial, tramos y velocidad --\n");
  static FlexVeEdit e;
  flexVeInit(&e, 100, 40000, 640, 480);                          // 25 fps
  CHECK(e.minSeg == 5 && e.p.nSeg == 1 && e.p.seg[0].a == 0 && e.p.seg[0].b == 100 && flexVeIsIdentity(&e),
        "recien abierto: un tramo entero y nada que exportar (minSeg %u)", e.minSeg);
  CHECK(!flexVeCanUndo(&e) && !flexVeCanRedo(&e), "nada que deshacer");
  // Recortar arrastrando: sin paso hasta soltar, y sin cruzar el minimo.
  flexVeTrimLive(&e, 0, 20); flexVeTrimLive(&e, 1, 70);
  CHECK(e.p.seg[0].a == 20 && e.p.seg[0].b == 70 && !flexVeCanUndo(&e), "arrastrar los extremos no crea pasos");
  flexVeCommit(&e);
  CHECK(flexVeCanUndo(&e) && flexVeKeptFrames(&e.p) == 50 && !flexVeIsIdentity(&e), "al soltar, UN paso: 50 fotogramas");
  flexVeTrimLive(&e, 0, 99);
  CHECK(e.p.seg[0].a == 65, "el principio no pasa del final menos 0,2 s (%u)", e.p.seg[0].a);
  flexVeTrimLive(&e, 1, 10);
  CHECK(e.p.seg[0].b == 70, "el final no baja del principio mas 0,2 s (%u)", e.p.seg[0].b);
  flexVeTrimLive(&e, 0, 20); flexVeCommit(&e);
  // Dividir y quitar.
  CHECK(!flexVeCanSplitAt(&e, 22) && !flexVeCanSplitAt(&e, 68) && !flexVeCanSplitAt(&e, 80), "no se divide pegado a un borde ni fuera");
  CHECK(flexVeSplit(&e, 40) && e.p.nSeg == 2 && e.p.seg[0].b == 40 && e.p.seg[1].a == 40 && e.p.seg[1].b == 70, "dividir en 40");
  CHECK(flexVeSplit(&e, 55) && e.p.nSeg == 3, "y en 55: tres tramos");
  CHECK(flexVeSegAt(&e.p, 45) == 1 && flexVeSegAt(&e.p, 10) == -1 && flexVeSegAt(&e.p, 69) == 2, "cada fotograma en su tramo");
  CHECK(flexVeRemoveSeg(&e, 1) && e.p.nSeg == 2 && flexVeKeptFrames(&e.p) == 35 && flexVeSegAt(&e.p, 45) == -1,
        "quitar el del medio: 35 fotogramas, el hueco ya no existe");
  CHECK(!flexVeRemoveSeg(&e, 7) && e.p.nSeg == 2, "un tramo que no existe no se quita");
  int steps = e.cur;
  CHECK(flexVeUndo(&e) && e.p.nSeg == 3 && flexVeUndo(&e) && e.p.nSeg == 2 && flexVeRedo(&e) && e.p.nSeg == 3,
        "deshacer y rehacer recorren los pasos (%d)", steps);
  flexVeRedo(&e);
  { FlexVeEdit* q = &e; while(q->p.nSeg > 1) flexVeRemoveSeg(q, 0); }
  CHECK(e.p.nSeg == 1 && !flexVeRemoveSeg(&e, 0), "siempre queda un tramo");
  for(int i = 0; i < 20; i++) flexVeSplit(&e, e.p.seg[e.p.nSeg - 1].a + 6);
  CHECK(e.p.nSeg <= FLEXVE_SEG_MAX, "como mucho %d tramos (%d)", FLEXVE_SEG_MAX, e.p.nSeg);
  flexVeReset(&e);
  CHECK(flexVeIsIdentity(&e) && flexVeCanUndo(&e), "restablecer vuelve al original y se puede deshacer");
  for(int i = 0; i < 60; i++){ e.p.speedPct = (uint16_t)(i % 2 ? 50 : 200); flexVeCommit(&e); }
  CHECK(e.cur == FLEXVE_HIST_MAX - 1, "el historial tiene tope (%d pasos)", e.cur);
  flexVeCommit(&e);
  int c0 = e.cur; flexVeCommit(&e);
  CHECK(e.cur == c0, "confirmar sin cambios no crea un paso");

  // Velocidad: fotogramas y duracion.
  FlexVeParams p; std::memset(&p, 0, sizeof(p)); p.nSeg = 1; p.seg[0].a = 10; p.seg[0].b = 30;
  p.speedPct = 100; CHECK(flexVeOutFrames(&p, 0, 0) == 20 && flexVeOutPeriodUs(&p, 40000) == 40000, "1x: 20 fotogramas a 25 fps");
  p.speedPct = 200; CHECK(flexVeOutFrames(&p, 0, 0) == 10 && flexVeSrcFrameOf(&p, &p.seg[0], 3) == 16, "2x: 10 fotogramas, uno de cada dos");
  p.speedPct = 150; CHECK(flexVeOutFrames(&p, 0, 0) == 14 && flexVeSrcFrameOf(&p, &p.seg[0], 13) == 29, "1,5x: 14 fotogramas, el ultimo es el 29");
  p.speedPct = 125; CHECK(flexVeOutFrames(&p, 0, 0) == 16 && flexVeSrcFrameOf(&p, &p.seg[0], 15) < 30, "1,25x: 16, sin salirse del tramo");
  p.speedPct = 50; CHECK(flexVeOutFrames(&p, 0, 0) == 20 && flexVeOutPeriodUs(&p, 40000) == 80000, "0,5x: los 20 fotogramas, cada uno el doble");
  p.speedPct = 25; CHECK(flexVeOutDurationUs(&p, 40000, 0, 0) == 20ull * 160000, "0,25x: cuatro veces mas largo");
  p.speedPct = 75; CHECK(flexVeOutPeriodUs(&p, 40000) == 53333, "0,75x: 53,3 ms por fotograma");
  p.speedPct = 200;
  CHECK(flexVeOutFrameOf(&p, 0, 0, 13) == 2 && flexVeOutFrameOf(&p, 0, 0, 5) == 0 && flexVeOutFrameOf(&p, 0, 0, 40) == -1,
        "fotograma de salida de un fotograma del original (portada)");
}

static void testGeometry(){
  std::printf("-- recorte, giro, proporciones y tamanos --\n");
  static FlexVeEdit e;
  flexVeInit(&e, 50, 40000, 640, 360);
  flexVeCropLive(&e, 0.0f, 0.0f, 0.5f, 0.5f); flexVeCommit(&e);
  int x0, y0, x1, y1; flexVeCropRect(&e.p, 640, 360, &x0, &y0, &x1, &y1);
  CHECK(x0 == 0 && y0 == 0 && x1 == 320 && y1 == 180, "recorte arriba-izquierda: 320x180 del original");
  flexVeRotate(&e, 1);
  flexVeCropRect(&e.p, 640, 360, &x0, &y0, &x1, &y1);
  CHECK(x0 == 0 && y0 == 0 && x1 == 320 && y1 == 180 && e.p.rot == 1, "al girar, el recorte se va con el contenido");
  int rw, rh; flexVeRegionSize(&e.p, 640, 360, &rw, &rh);
  CHECK(rw == 180 && rh == 320, "y su tamano ya girado es 180x320");
  // Mismo mapeo que el motor del editor de fotos.
  { static FlexImgEdit ie; std::vector<uint8_t> b(640 * 360 * 3);
    flexIeInit(&ie, b.data(), 640, 360);
    flexVeSetupRender(&ie, b.data(), 640, 360, &e.p, nullptr, 0, false);
    float s, t; flexIeOutToBase(&ie, 0.0f, 0.0f, &s, &t);
    float s2, t2; flexIeOutToBase(&ie, 1.0f, 1.0f, &s2, &t2);
    float a0 = s < s2 ? s : s2, a1 = s < s2 ? s2 : s, b0 = t < t2 ? t : t2, b1 = t < t2 ? t2 : t;
    CHECK(std::fabs(a0 * 640 - x0) < 1 && std::fabs(a1 * 640 - x1) < 1 && std::fabs(b0 * 360 - y0) < 1 && std::fabs(b1 * 360 - y1) < 1,
          "la region coincide con la del editor de fotos (FlexOS_ImgEdit)");
    int ow, oh; flexIeOutSize(&ie, &ow, &oh);
    CHECK(ow == 180 && oh == 320, "y el motor pinta 180x320 (%dx%d)", ow, oh); }
  flexVeReset(&e);
  flexVeSetAspect(&e, FLEXVE_ASP_1_1);
  flexVeRegionSize(&e.p, 640, 360, &rw, &rh);
  CHECK(rw == 360 && rh == 360 && std::fabs(e.p.c0x - 0.21875f) < 0.001f, "1:1 centrado y lo mas grande posible (%dx%d)", rw, rh);
  flexVeSetAspect(&e, FLEXVE_ASP_9_16);
  flexVeRegionSize(&e.p, 640, 360, &rw, &rh);
  CHECK(std::abs(rw * 16 - rh * 9) <= 16 && rh == 360, "9:16 sobre un video horizontal (%dx%d)", rw, rh);
  { int X0, Y0, X1, Y1; flexVeCropRect(&e.p, 640, 360, &X0, &Y0, &X1, &Y1);
    CHECK(X1 - X0 >= rw && X0 <= 219 && X1 >= 421, "la region en pixeles enteros CUBRE el recorte (%d..%d)", X0, X1); }
  flexVeSetRot(&e, 1);
  flexVeRegionSize(&e.p, 640, 360, &rw, &rh);
  CHECK(std::abs(rw * 16 - rh * 9) <= 16, "al girar se mantiene la proporcion elegida (%dx%d)", rw, rh);
  flexVeSetAspect(&e, FLEXVE_ASP_ORIG);
  flexVeRegionSize(&e.p, 640, 360, &rw, &rh);
  CHECK(rw == 360 && rh == 640, "Original girado: el fotograma entero (%dx%d)", rw, rh);
  // Tamanos de salida por lado corto.
  int ow, oh;
  CHECK(flexVeFitShort(1920, 1080, 720, &ow, &oh) && ow == 1280 && oh == 720, "720p de 1920x1080: 1280x720");
  CHECK(flexVeFitShort(1080, 1920, 480, &ow, &oh) && ow == 480 && oh == 853, "480p de un vertical 1080x1920: 480x853 (%dx%d)", ow, oh);
  CHECK(!flexVeFitShort(640, 480, 720, &ow, &oh), "no se amplia: 720p de un 640x480 no se ofrece");
  CHECK(flexVeFitShort(641, 481, 0, &ow, &oh) && ow == 641 && oh == 481, "Original = el tamano del recorte, tal cual");
  CHECK(flexVePickDiv(1920, 1080, 852, 480) == 2 && flexVePickDiv(1920, 1080, 1280, 720) == 1 && flexVePickDiv(1920, 1080, 200, 100) == 8,
        "divisor de decodificacion: el mayor que aun cubre la salida");
}

static void testProbe(){
  std::printf("-- analisis del original: video, audio y archivos raros --\n");
  int rc;
  { AviSpec a = videoSpec(6); Mem m; m.d = buildAvi(a); FlexVeSource s = probeOf(&m, &rc);
    CHECK(rc == FLEXVE_OK && s.frames == 6 && s.w == 64 && s.h == 48 && s.usPerFrame == 100000 && s.aud.kind == FLEXVE_AUD_NONE,
          "AVI MJPEG sin audio: 6 fotogramas de 64x48 a 10 fps"); }
  { AviSpec a = videoSpec(5); addPcm16(a, 2); Mem m; m.d = buildAvi(a); FlexVeSource s = probeOf(&m, &rc);
    CHECK(rc == FLEXVE_OK && s.aud.kind == FLEXVE_AUD_PCM && s.aud.channels == 2 && s.aud.stream == 1 && s.aud.chunks == 5 &&
          s.aud.bytes == 5u * 800u * 4u, "con audio PCM estereo: pista 1, 5 trozos"); }
  { AviSpec a = videoSpec(5); addPcm16(a, 1); a.audioFirst = true; Mem m; m.d = buildAvi(a); FlexVeSource s = probeOf(&m, &rc);
    CHECK(rc == FLEXVE_OK && s.videoStream == 1 && s.aud.stream == 0 && s.frames == 5 && s.aud.chunks == 5,
          "el audio puede ir primero (pista 0) y el video segundo"); }
  { AviSpec a = videoSpec(4); a.audTag = 0x55; a.audBits = 0; a.audBa = 1; for(int i = 0; i < 4; i++) a.aud.push_back(std::vector<uint8_t>(417, 0xFF));
    Mem m; m.d = buildAvi(a); FlexVeSource s = probeOf(&m, &rc);
    CHECK(rc == FLEXVE_OK && s.aud.kind == FLEXVE_AUD_OTHER && s.aud.tag == 0x55, "audio MP3 dentro del AVI: se reconoce y se marca como no editable"); }
  { AviSpec a = videoSpec(4); a.frames[2].clear(); Mem m; m.d = buildAvi(a); FlexVeSource s = probeOf(&m, &rc);
    CHECK(rc == FLEXVE_OK && s.frames == 4 && s.emptyFrames == 1, "un trozo vacio cuenta como fotograma (repite el anterior)"); }
  { AviSpec a = videoSpec(6); a.declared = 10; a.withIdx = false; Mem m; m.d = buildAvi(a); FlexVeSource s = probeOf(&m, &rc);
    CHECK(rc == FLEXVE_OK && s.frames == 6, "grabacion cortada (declara 10, hay 6): cuenta los que existen de verdad (%u)", s.frames); }
  { AviSpec a = videoSpec(3); a.codec = "H264"; Mem m; m.d = buildAvi(a); probeOf(&m, &rc);
    CHECK(rc == FLEXVE_ERR_CODEC, "AVI con otro codec: no se edita (%d)", rc); }
  { Mem m; m.d.assign(4096, 0x5A); probeOf(&m, &rc); CHECK(rc == FLEXVE_ERR_FORMAT, "basura: formato no valido"); }
  { AviSpec a = videoSpec(3); Mem m; m.d = buildAvi(a); m.failReadAt = 300; probeOf(&m, &rc);
    CHECK(rc == FLEXVE_ERR_IO || rc == FLEXVE_ERR_FORMAT, "un error de lectura no se toma por un video (%d)", rc); }
}

static void testCopyExport(){
  std::printf("-- exportar sin tocar pixeles: copia exacta, tramos y velocidad --\n");
  AviSpec a = videoSpec(20);
  a.frames[7].clear();                                           // un "repetir" en medio
  Mem m; m.d = buildAvi(a);
  int rc; FlexVeSource s = probeOf(&m, &rc);
  static FlexVeEdit e; initParams(&e, s);
  flexVeTrimLive(&e, 0, 4); flexVeTrimLive(&e, 1, 12); flexVeCommit(&e);
  Out o; long live0 = g_live;
  Run* r = doExport(&m, &s, &e.p, &o);
  CHECK(r->rc == FLEXVE_OK && r->x.copy && r->x.outTotal == 8, "recortar 4..12 a 1x: copia de 8 fotogramas (rc %d)", r->rc);
  auto ck = only(moviChunks(o.d), "00dc");
  bool same = ck.size() == 8;
  for(size_t i = 0; same && i < ck.size(); i++){
    const auto& f = a.frames[4 + i];
    if(4 + i == 7) same = ck[i].len == 0;                        // la repeticion sigue siendo repeticion
    else same = ck[i].len == f.size() && !std::memcmp(&o.d[ck[i].off], f.data(), f.size());
  }
  CHECK(same, "los fotogramas salen BYTE A BYTE iguales (y el vacio sigue vacio)");
  CHECK(r->allocsInSteps == 0, "exportar no reserva memoria fotograma a fotograma (%ld)", r->allocsInSteps);
  flexVeExportFree(&r->x);
  CHECK(g_live == live0, "al terminar no queda memoria viva (%ld)", g_live - live0);
  // El lector del reproductor lo abre: medidas, fotogramas, indice, fps.
  { Mem mo; mo.d = o.d; FlexMediaIO io = ioOf(&mo); static FlexAviCtx c;
    CHECK(flexAviOpen(&c, &io) == FLEXAVI_OK && c.frames == 8 && c.width == 64 && c.height == 48 && c.usPerFrame == 100000 && c.idxFromFile,
          "el reproductor lo abre: 8 fotogramas, 64x48, 10 fps, con indice");
    CHECK(flexVeVerify(&io, (uint32_t)o.d.size(), 8, 64, 48, false, &c, tA, tF) == FLEXVE_OK, "y pasa la comprobacion");
    uint8_t buf[4096]; uint32_t fn = 0;
    CHECK(flexAviSeekFrame(&c, 5) == 5 && flexAviReadFrame(&c, buf, sizeof(buf), &fn) == (int)a.frames[9].size() && fn == 5,
          "buscar por el indice llega al fotograma 5 (el 9 del original)"); }
  // Dividir y quitar el medio: se juntan los tramos que quedan.
  flexVeSplit(&e, 7); flexVeSplit(&e, 10); flexVeRemoveSeg(&e, 1);   // quedan 4..7 y 10..12
  Out o2; r = doExport(&m, &s, &e.p, &o2);
  auto ck2 = only(moviChunks(o2.d), "00dc");
  bool okJoin = r->rc == FLEXVE_OK && ck2.size() == 5;
  const int want[5] = { 4, 5, 6, 10, 11 };
  for(int i = 0; okJoin && i < 5; i++) okJoin = ck2[i].len == a.frames[want[i]].size() && !std::memcmp(&o2.d[ck2[i].off], a.frames[want[i]].data(), ck2[i].len);
  CHECK(okJoin, "quitar el tramo del medio: 4,5,6 + 10,11 (%zu fotogramas)", ck2.size());
  flexVeExportFree(&r->x);
  // Partes por separado: cada tramo, su archivo.
  Out o3; r = doExport(&m, &s, &e.p, &o3, 1, 1);
  auto ck3 = only(moviChunks(o3.d), "00dc");
  CHECK(r->rc == FLEXVE_OK && ck3.size() == 2 && ck3[0].len == a.frames[10].size(), "exportar solo la segunda parte: 10 y 11");
  flexVeExportFree(&r->x);
  // Velocidad 2x: uno de cada dos, mismos fps. 0,5x: todos, a la mitad de fps.
  flexVeReset(&e); e.p.speedPct = 200; flexVeCommit(&e);
  Out o4; r = doExport(&m, &s, &e.p, &o4);
  auto ck4 = only(moviChunks(o4.d), "00dc");
  CHECK(r->rc == FLEXVE_OK && ck4.size() == 10 && ck4[3].len == a.frames[6].size() && !std::memcmp(&o4.d[ck4[3].off], a.frames[6].data(), ck4[3].len),
        "2x: 10 fotogramas (0,2,4,6...)");
  flexVeExportFree(&r->x);
  e.p.speedPct = 50; flexVeCommit(&e);
  Out o5; r = doExport(&m, &s, &e.p, &o5);
  { Mem mo; mo.d = o5.d; FlexMediaIO io = ioOf(&mo); static FlexAviCtx c;
    CHECK(r->rc == FLEXVE_OK && flexAviOpen(&c, &io) == FLEXAVI_OK && c.frames == 20 && c.usPerFrame == 200000,
          "0,5x: los 20 fotogramas a 5 fps (%u us)", c.usPerFrame); }
  flexVeExportFree(&r->x);
  // 1,5x con un "repetir": la imagen del fotograma elegido es la del ultimo con datos.
  e.p.speedPct = 150; flexVeCommit(&e);
  Out o6; r = doExport(&m, &s, &e.p, &o6);
  auto ck6 = only(moviChunks(o6.d), "00dc");
  // Salida k -> original floor(1,5k): 0,1,3,4,6,7(vacio: imagen del 6),9...
  CHECK(r->rc == FLEXVE_OK && ck6.size() == 14 && ck6[5].len == 0 && ck6[4].len == a.frames[6].size(),
        "el vacio del original repite la imagen buena de antes, no la de otro fotograma");
  flexVeExportFree(&r->x);
  CHECK(g_live == live0, "ninguna exportacion deja memoria viva");
}

static void testRenderExport(){
  std::printf("-- exportar recodificando: recorte, giro, filtro, texto y resolucion --\n");
  const int W = 128, H = 96;
  AviSpec a = videoSpec(6, W, H);
  Mem m; m.d = buildAvi(a);
  int rc; FlexVeSource s = probeOf(&m, &rc);
  flexIeInit(&g_ie, nullptr, 0, 0);
  flexIeSetTextFn(&g_ie, boxText, nullptr);
  static FlexVeEdit e; initParams(&e, s);
  long live0 = g_live;
  // Girar 90: 96x128, el rojo (arriba-izq) pasa a arriba-derecha.
  flexVeRotate(&e, 1);
  Out o; Run* r = doExport(&m, &s, &e.p, &o);
  auto ck = only(moviChunks(o.d), "00dc");
  Img f0 = ck.empty() ? Img() : decodeJpeg(&o.d[ck[2].off], ck[2].len);
  CHECK(r->rc == FLEXVE_OK && !r->x.copy && f0.w == 96 && f0.h == 128, "girar 90: sale 96x128 (%dx%d)", f0.w, f0.h);
  CHECK(f0.w == 96 && isRed(pix(f0, 72, 32)) && isGreen(pix(f0, 24, 32)) && isBlue(pix(f0, 24, 96)) && isGray(pix(f0, 72, 96)),
        "y cada cuadrante esta donde debe (rojo arriba a la derecha)");
  CHECK(f0.w == 96 && idxOfGray(pix(f0, 72, 96)[0]) == 2, "el fotograma 2 del original es el 2 de la salida");
  CHECK(r->allocsInSteps == 0, "recodificar tampoco reserva por fotograma: decodificar y codificar usan la arena (%ld)", r->allocsInSteps);
  CHECK(r->x.arena.peak > 0 && r->x.arena.peak <= r->x.arena.cap, "la arena cubrio lo que pidieron (%u de %u)", r->x.arena.peak, r->x.arena.cap);
  { Mem mo; mo.d = o.d; FlexMediaIO io = ioOf(&mo); static FlexAviCtx c;
    CHECK(flexVeVerify(&io, (uint32_t)o.d.size(), 6, 96, 128, false, &c, tA, tF) == FLEXVE_OK, "el AVI girado pasa la comprobacion"); }
  flexVeExportFree(&r->x);
  CHECK(g_ie.base == nullptr, "al soltar, el motor ya no apunta a la base de la exportacion");
  // Recortar: la mitad izquierda (rojo arriba, verde abajo).
  flexVeReset(&e);
  flexVeCropLive(&e, 0.0f, 0.0f, 0.5f, 1.0f); flexVeCommit(&e);
  Out o2; r = doExport(&m, &s, &e.p, &o2);
  ck = only(moviChunks(o2.d), "00dc");
  Img f1 = ck.empty() ? Img() : decodeJpeg(&o2.d[ck[0].off], ck[0].len);
  CHECK(r->rc == FLEXVE_OK && f1.w == 64 && f1.h == 96 && isRed(pix(f1, 32, 20)) && isGreen(pix(f1, 32, 76)),
        "recorte de la mitad izquierda: 64x96, rojo y verde (%dx%d)", f1.w, f1.h);
  CHECK(r->x.bw == 64 && r->x.bh == 96, "solo se guarda la region del recorte (%dx%d)", r->x.bw, r->x.bh);
  flexVeExportFree(&r->x);
  // Blanco y negro.
  flexVeReset(&e); e.p.filter = FLEXIE_FILTER_BW; flexVeCommit(&e);
  Out o3; r = doExport(&m, &s, &e.p, &o3);
  ck = only(moviChunks(o3.d), "00dc");
  Img f2 = ck.empty() ? Img() : decodeJpeg(&o3.d[ck[0].off], ck[0].len);
  CHECK(r->rc == FLEXVE_OK && f2.w == W && isGray(pix(f2, 20, 20)) && isGray(pix(f2, 100, 70)), "filtro B/N: el rojo y el azul salen grises");
  flexVeExportFree(&r->x);
  // Texto: una caja blanca donde se puso.
  flexVeReset(&e);
  std::snprintf(e.p.text, sizeof(e.p.text), "HOLA"); e.p.textRgb = 0xFFFFFF; e.p.textSize = 2; e.p.textU = 0.1f; e.p.textV = 0.6f;
  flexVeCommit(&e);
  Out o4; r = doExport(&m, &s, &e.p, &o4);
  ck = only(moviChunks(o4.d), "00dc");
  Img f3 = ck.empty() ? Img() : decodeJpeg(&o4.d[ck[0].off], ck[0].len);
  int th = (int)(FLEXVE_TEXT_H[2] * W + 0.5f);
  const uint8_t* tp = f3.w ? pix(f3, (int)(0.1f * W) + 3, (int)(0.6f * H) + th / 2) : nullptr;
  CHECK(r->rc == FLEXVE_OK && tp && tp[0] > 200 && tp[1] > 200 && tp[2] > 200, "el texto sale donde se coloco");
  CHECK(f3.w && isGreen(pix(f3, 10, 90)), "y el resto del fotograma sigue igual");
  flexVeExportFree(&r->x);
  // Resolucion: 48p de un 128x96 -> 64x48, decodificando a 1/2.
  flexVeReset(&e); e.p.filter = FLEXIE_FILTER_WARM; flexVeCommit(&e);
  int ow, oh; flexVeFitShort(W, H, 48, &ow, &oh);
  Out o5; r = doExport(&m, &s, &e.p, &o5, 0, -1, ow, oh);
  ck = only(moviChunks(o5.d), "00dc");
  Img f4 = ck.empty() ? Img() : decodeJpeg(&o5.d[ck[0].off], ck[0].len);
  CHECK(r->rc == FLEXVE_OK && r->x.div == 2 && f4.w == 64 && f4.h == 48, "bajar a 48p: 64x48, decodificando a la mitad (%dx%d, 1/%d)", f4.w, f4.h, r->x.div);
  flexVeExportFree(&r->x);
  CHECK(g_live == live0, "ninguna exportacion recodificada deja memoria viva");
}

static void testAudio(){
  std::printf("-- audio original: igual, volumen, silencio, velocidad, IMA y MP3 --\n");
  AviSpec a = videoSpec(10);                                      // 10 fps, 800 muestras por fotograma
  addPcm16(a, 2);
  Mem m; m.d = buildAvi(a);
  int rc; FlexVeSource s = probeOf(&m, &rc);
  static FlexVeEdit e; initParams(&e, s);
  flexVeTrimLive(&e, 0, 3); flexVeTrimLive(&e, 1, 7); flexVeCommit(&e);
  auto collect = [](const std::vector<uint8_t>& f){
    std::vector<int16_t> v;
    for(auto& c : only(moviChunks(f), "01wb")) for(uint32_t i = 0; i + 1 < c.len; i += 2) v.push_back((int16_t)(f[c.off + i] | (f[c.off + i + 1] << 8)));
    return v;
  };
  Out o; Run* r = doExport(&m, &s, &e.p, &o);
  auto pcm = collect(o.d);
  bool exact = r->rc == FLEXVE_OK && pcm.size() == 4u * 800u * 2u;
  for(size_t i = 0; exact && i < pcm.size() / 2; i++) exact = pcm[2 * i] == rampAt(2400 + (uint32_t)i) && pcm[2 * i + 1] == (int16_t)(-rampAt(2400 + (uint32_t)i));
  CHECK(exact, "1x al 100 %%: el audio del tramo sale EXACTO (%zu muestras)", pcm.size() / 2);
  { Mem mo; mo.d = o.d; FlexMediaIO io = ioOf(&mo); static FlexAviCtx c;
    CHECK(flexVeVerify(&io, (uint32_t)o.d.size(), 4, 64, 48, true, &c, tA, tF) == FLEXVE_OK, "con su pista PCM de 16 bits");
    FlexVeSource s2; static FlexAviCtx c2; FlexMediaIO io2 = ioOf(&mo);
    CHECK(flexVeProbe(&io2, &s2, &c2, nullptr, nullptr) == FLEXVE_OK && s2.aud.kind == FLEXVE_AUD_PCM && s2.aud.channels == 2 && s2.aud.rate == 8000,
          "y el editor la vuelve a leer como PCM estereo a 8 kHz"); }
  flexVeExportFree(&r->x);
  e.p.volPct = 50; flexVeCommit(&e);
  Out o2; r = doExport(&m, &s, &e.p, &o2);
  pcm = collect(o2.d);
  bool half = pcm.size() == 4u * 800u * 2u;
  for(size_t i = 0; half && i < pcm.size() / 2; i++) half = std::abs(pcm[2 * i] - rampAt(2400 + (uint32_t)i) / 2) <= 1;
  CHECK(half, "al 50 %%: la mitad");
  flexVeExportFree(&r->x);
  e.p.volPct = 100; e.p.mute = 1; flexVeCommit(&e);
  Out o3; r = doExport(&m, &s, &e.p, &o3);
  { Mem mo; mo.d = o3.d; FlexMediaIO io = ioOf(&mo); static FlexAviCtx c;
    CHECK(r->rc == FLEXVE_OK && only(moviChunks(o3.d), "01wb").empty() && flexVeVerify(&io, (uint32_t)o3.d.size(), 4, 64, 48, false, &c, tA, tF) == FLEXVE_OK,
          "silenciado: el video sale SIN pista de audio"); }
  flexVeExportFree(&r->x);
  e.p.mute = 0; e.p.speedPct = 200; flexVeCommit(&e);
  Out o4; r = doExport(&m, &s, &e.p, &o4);
  pcm = collect(o4.d);
  bool dec = r->rc == FLEXVE_OK && pcm.size() == 2u * 800u * 2u;
  for(size_t i = 0; dec && i < pcm.size() / 2; i++) dec = pcm[2 * i] == rampAt(2400 + 2 * (uint32_t)i);
  CHECK(dec, "2x: la mitad de muestras (una de cada dos del original, sin mezclar)");
  flexVeExportFree(&r->x);
  e.p.speedPct = 50; flexVeCommit(&e);
  Out o5; r = doExport(&m, &s, &e.p, &o5);
  pcm = collect(o5.d);
  bool str = r->rc == FLEXVE_OK && pcm.size() == 4u * 1600u * 2u;
  for(size_t i = 0; str && i + 2 < pcm.size() / 2; i += 2){
    int a0 = rampAt(2400 + (uint32_t)i / 2), a1 = rampAt(2400 + (uint32_t)i / 2 + 1);
    str = pcm[2 * i] == a0 && std::abs(pcm[2 * i + 2] - (a0 + (a1 - a0) / 2)) <= 1;
  }
  CHECK(str, "0,5x: el doble de muestras, con las intermedias interpoladas (%zu)", pcm.size() / 2);
  flexVeExportFree(&r->x);
  // Dos tramos: el audio del hueco no se oye.
  flexVeReset(&e); flexVeTrimLive(&e, 1, 10); flexVeSplit(&e, 3); flexVeSplit(&e, 6); flexVeRemoveSeg(&e, 1);
  Out o6; r = doExport(&m, &s, &e.p, &o6);
  pcm = collect(o6.d);
  CHECK(r->rc == FLEXVE_OK && pcm.size() == 7u * 800u * 2u && pcm[2 * 2400] == rampAt(4800) && pcm[2 * 2399] == rampAt(2399),
        "quitar un tramo quita tambien su audio (y el siguiente empieza justo donde toca)");
  flexVeExportFree(&r->x);

  // IMA ADPCM mono: bloques de 256 bytes (505 muestras), como los de un WAV.
  { AviSpec b = videoSpec(6); b.audTag = 0x11; b.audCh = 1; b.audBits = 4; b.audBa = 256; b.audSpb = 505; b.audRate = 8000;
    std::vector<uint8_t> all;
    for(int blk = 0; blk < 10; blk++){
      std::vector<uint8_t> bl(256);
      bl[0] = (uint8_t)(blk * 100); bl[1] = 0; bl[2] = (uint8_t)(20 + blk); bl[3] = 0;   // predictor, indice
      for(int i = 4; i < 256; i++) bl[i] = (uint8_t)((i * 37 + blk * 11) & 0xFF);
      all.insert(all.end(), bl.begin(), bl.end());
    }
    for(int i = 0; i < 6; i++){                                   // trozos que NO coinciden con los bloques
      size_t a0 = all.size() * i / 6, a1 = all.size() * (i + 1) / 6;
      b.aud.push_back(std::vector<uint8_t>(all.begin() + a0, all.begin() + a1));
    }
    Mem mb; mb.d = buildAvi(b);
    FlexVeSource sb = probeOf(&mb, &rc);
    CHECK(rc == FLEXVE_OK && sb.aud.kind == FLEXVE_AUD_IMA && sb.aud.samplesPerBlock == 505, "IMA ADPCM dentro del AVI");
    std::vector<int16_t> ref(10 * 505);
    for(int blk = 0; blk < 10; blk++) flexImaDecodeBlock(&all[blk * 256], 256, 1, &ref[blk * 505], 505);
    static FlexVeEdit eb; initParams(&eb, sb);
    Out ob; r = doExport(&mb, &sb, &eb.p, &ob);
    auto got = collect(ob.d);
    bool same = r->rc == FLEXVE_OK && got.size() == 6u * 800u;
    for(size_t i = 0; same && i < got.size(); i++) same = got[i] == (i < ref.size() ? ref[i] : 0);
    CHECK(same, "se decodifica por bloques aunque crucen trozos, igual que el reproductor de Musica (%zu)", got.size());
    flexVeExportFree(&r->x);
    // Empezando a mitad (salta bloques sin decodificarlos).
    flexVeTrimLive(&eb, 0, 2); flexVeCommit(&eb);
    Out ob2; r = doExport(&mb, &sb, &eb.p, &ob2);
    got = collect(ob2.d);
    same = r->rc == FLEXVE_OK && got.size() == 4u * 800u;
    for(size_t i = 0; same && i < got.size(); i++) same = got[i] == (1600 + i < ref.size() ? ref[1600 + i] : 0);
    CHECK(same, "empezar en el fotograma 2 salta los bloques de antes y cae en la muestra 1600");
    flexVeExportFree(&r->x); }
  // MP3: no se toca, no se inventa, y el video sale igual.
  { AviSpec c = videoSpec(4); c.audTag = 0x55; c.audBits = 0; c.audBa = 1; for(int i = 0; i < 4; i++) c.aud.push_back(std::vector<uint8_t>(417, 0xFF));
    Mem mc; mc.d = buildAvi(c);
    FlexVeSource sc = probeOf(&mc, &rc);
    static FlexVeEdit ec; initParams(&ec, sc);
    Out oc; r = doExport(&mc, &sc, &ec.p, &oc);
    CHECK(r->rc == FLEXVE_OK && r->x.audioDropped && !r->x.audioOn && only(moviChunks(oc.d), "01wb").empty() &&
          only(moviChunks(oc.d), "00dc").size() == 4, "audio MP3: el video sale sin el (y la interfaz lo avisa antes)");
    flexVeExportFree(&r->x); }
}

static void testCover(){
  std::printf("-- portada: la miniatura usa el fotograma elegido --\n");
  AviSpec a = videoSpec(12);
  Mem m; m.d = buildAvi(a);
  int rc; FlexVeSource s = probeOf(&m, &rc);
  static FlexVeEdit e; initParams(&e, s);
  flexVeTrimLive(&e, 0, 2); e.p.cover = 7; flexVeCommit(&e);
  Out o; Run* r = doExport(&m, &s, &e.p, &o);
  CHECK(r->rc == FLEXVE_OK && r->x.coverOut == 5, "la portada (original 7) es el fotograma 5 de la salida");
  flexVeExportFree(&r->x);
  Mem mo; mo.d = o.d; FlexMediaIO io = ioOf(&mo);
  static FlexAviCtx c;
  CHECK(flexAviOpen(&c, &io) == FLEXAVI_OK && c.cover == 5 && c.frames == 10, "el lector del reproductor lee la portada (IFCV) y el video sigue igual");
  std::vector<uint8_t> th;
  int tw = 0, tth = 0; uint32_t dur = 0;
  int trc = flexThumbFromAvi(&io, 32, 90, vOut, &th, &tw, &tth, &dur, nullptr, nullptr);
  Img ti = th.empty() ? Img() : decodeJpeg(th.data(), th.size());
  // La miniatura es cuadrada y recortada al centro: el gris del numero queda arriba a la derecha.
  CHECK(trc == FLEXTH_OK && ti.w == 32 && idxOfGray(pix(ti, 26, 6)[0]) == 7, "la miniatura sale del fotograma 7 (el elegido)");
  // Sin portada: la de siempre (el primero).
  e.p.cover = FLEXVE_NO_COVER; flexVeCommit(&e);
  Out o2; r = doExport(&m, &s, &e.p, &o2);
  flexVeExportFree(&r->x);
  Mem m2; m2.d = o2.d; FlexMediaIO io2 = ioOf(&m2);
  th.clear();
  flexThumbFromAvi(&io2, 32, 90, vOut, &th, &tw, &tth, &dur, nullptr, nullptr);
  ti = th.empty() ? Img() : decodeJpeg(th.data(), th.size());
  CHECK(ti.w == 32 && idxOfGray(pix(ti, 26, 6)[0]) == 2, "sin portada elegida, la miniatura es la del primer fotograma");
  // La portada en un fotograma repetido apunta al ultimo con imagen.
  AviSpec b = videoSpec(8); b.frames[5].clear();
  Mem mb; mb.d = buildAvi(b);
  FlexVeSource sb = probeOf(&mb, &rc);
  static FlexVeEdit eb; initParams(&eb, sb); eb.p.cover = 5; flexVeCommit(&eb);
  Out ob; r = doExport(&mb, &sb, &eb.p, &ob);
  CHECK(r->rc == FLEXVE_OK && r->x.coverOut == 4, "portada sobre un fotograma que repite: se usa el 4, que es el que se ve");
  flexVeExportFree(&r->x);
}

static void testFailures(){
  std::printf("-- fallos: disco lleno, tope, cancelar, danado y sin memoria --\n");
  AviSpec a = videoSpec(10, 96, 64);
  Mem m; m.d = buildAvi(a);
  int rc; FlexVeSource s = probeOf(&m, &rc);
  static FlexVeEdit e; initParams(&e, s);
  long live0 = g_live;
  Out o; o.failAt = 3000;
  Run* r = doExport(&m, &s, &e.p, &o);
  CHECK(r->rc == FLEXVE_ERR_WRITE, "disco lleno a mitad: error de escritura (%d)", r->rc);
  flexVeExportFree(&r->x);
  Out o2; r = doExport(&m, &s, &e.p, &o2, 0, -1, 0, 0, 4000);
  CHECK(r->rc == FLEXVE_ERR_LIMIT && o2.d.size() <= 4000, "pasarse del tope: se para ANTES de escribirlo (%zu B)", o2.d.size());
  flexVeExportFree(&r->x);
  g_cancelAt = 3;
  Out o3; r = doExport(&m, &s, &e.p, &o3);
  CHECK(r->rc == FLEXVE_ERR_CANCEL && r->x.outDone <= 3, "cancelar entre fotogramas (%d hechos)", r->x.outDone);
  flexVeExportFree(&r->x);
  g_cancelAt = -1;
  // Un fotograma danado en mitad al recodificar: se repite el anterior.
  AviSpec b = videoSpec(6, 96, 64);
  b.frames[3].assign(b.frames[3].size(), 0x77);
  Mem mb; mb.d = buildAvi(b);
  FlexVeSource sb = probeOf(&mb, &rc);
  flexIeInit(&g_ie, nullptr, 0, 0);
  static FlexVeEdit eb; initParams(&eb, sb); eb.p.filter = FLEXIE_FILTER_COOL; flexVeCommit(&eb);
  Out ob; r = doExport(&mb, &sb, &eb.p, &ob);
  auto ck = only(moviChunks(ob.d), "00dc");
  CHECK(r->rc == FLEXVE_OK && r->x.badFrames == 1 && ck.size() == 6 && ck[3].len == 0, "fotograma danado: se queda el anterior y se cuenta");
  flexVeExportFree(&r->x);
  // Sin memoria para empezar: nada escrito mas alla de nada, nada vivo.
  g_allocFail = true;
  Out o4; r = doExport(&mb, &sb, &eb.p, &o4);
  g_allocFail = false;
  CHECK(r->rc == FLEXVE_ERR_MEMORY && o4.d.empty(), "sin memoria al empezar: error claro y el archivo ni se empieza");
  flexVeExportFree(&r->x);
  // Lectura que falla a mitad.
  Mem mf; mf.d = mb.d; mf.failReadAt = (long)mf.d.size() / 2;
  Out o5; r = doExport(&mf, &sb, &eb.p, &o5);
  CHECK(r->rc == FLEXVE_ERR_IO || r->rc == FLEXVE_ERR_FORMAT, "el original deja de leerse: error, no basura (%d)", r->rc);
  flexVeExportFree(&r->x);
  CHECK(g_live == live0, "ningun fallo deja memoria viva (%ld)", g_live - live0);
  // La comprobacion rechaza lo que no cuadra.
  Out ok; r = doExport(&m, &s, &e.p, &ok); flexVeExportFree(&r->x);
  { Mem mo; mo.d = ok.d; FlexMediaIO io = ioOf(&mo); static FlexAviCtx c;
    bool good = flexVeVerify(&io, (uint32_t)ok.d.size(), 10, 96, 64, false, &c, tA, tF) == FLEXVE_OK;
    bool frames = flexVeVerify(&io, (uint32_t)ok.d.size(), 9, 96, 64, false, &c, tA, tF) != FLEXVE_OK;
    bool dims = flexVeVerify(&io, (uint32_t)ok.d.size(), 10, 64, 64, false, &c, tA, tF) != FLEXVE_OK;
    bool aud = flexVeVerify(&io, (uint32_t)ok.d.size(), 10, 96, 64, true, &c, tA, tF) != FLEXVE_OK;
    mo.d.resize(mo.d.size() - 40);
    bool cut = flexVeVerify(&io, (uint32_t)ok.d.size(), 10, 96, 64, false, &c, tA, tF) != FLEXVE_OK;
    CHECK(good && frames && dims && aud && cut, "la comprobacion rechaza fotogramas, medidas, audio o un archivo cortado"); }
  CHECK(g_live == live0, "comprobar no deja memoria viva");
}

static void testDecodeAndThumb(){
  std::printf("-- decodificar una region y miniaturas, con la arena --\n");
  auto j = frameJpeg(160, 120, 4);
  std::vector<uint8_t> mem(flexVeDecodeWork(160));
  FlexVeArena ar; flexVeArenaInit(&ar, mem.data(), (uint32_t)mem.size());
  std::vector<uint8_t> dst(80 * 60 * 3);
  int rc = flexVeDecode(j.data(), (uint32_t)j.size(), 160, 120, 2, 0, 0, 80, 60, dst.data(), &ar);
  CHECK(rc == FLEXVE_OK && ar.peak <= ar.cap, "a 1/2 con la arena justa (%u de %u)", ar.peak, ar.cap);
  CHECK(dst[(10 * 80 + 10) * 3] > 170 && std::abs((int)dst[(10 * 80 + 60) * 3] - grayOf(4)) < 10, "rojo y el gris del numero en su sitio");
  std::vector<uint8_t> reg(40 * 30 * 3);
  rc = flexVeDecode(j.data(), (uint32_t)j.size(), 160, 120, 2, 40, 0, 80, 30, reg.data(), &ar);
  CHECK(rc == FLEXVE_OK && std::abs((int)reg[(10 * 40 + 20) * 3] - grayOf(4)) < 10, "solo la region pedida (el cuadrante del numero)");
  rc = flexVeDecode(j.data(), (uint32_t)j.size(), 200, 120, 2, 0, 0, 80, 60, dst.data(), &ar);
  CHECK(rc == FLEXVE_ERR_FORMAT, "un fotograma con otras medidas se rechaza");
  std::vector<uint8_t> tiny(64);
  FlexVeArena t2; flexVeArenaInit(&t2, tiny.data(), (uint32_t)tiny.size());
  rc = flexVeDecode(j.data(), (uint32_t)j.size(), 160, 120, 2, 0, 0, 80, 60, dst.data(), &t2);
  CHECK(rc == FLEXVE_ERR_MEMORY, "arena pequena: sin memoria, sin reventar");
  std::vector<uint16_t> th(40 * 30);
  rc = flexVeThumb(j.data(), (uint32_t)j.size(), 160, 120, 40, 30, th.data(), &ar);
  uint16_t tl = th[5 * 40 + 5], br = th[25 * 40 + 35];
  CHECK(rc == FLEXVE_OK && (tl >> 11) > 22 && (br & 31) > 22, "miniatura 40x30: rojo arriba a la izquierda, azul abajo a la derecha");
  std::vector<uint8_t> bad(j.begin(), j.begin() + j.size() / 3);
  rc = flexVeThumb(bad.data(), (uint32_t)bad.size(), 160, 120, 40, 30, th.data(), &ar);
  CHECK(rc == FLEXVE_ERR_FORMAT, "un fotograma cortado no da miniatura (ni revienta)");
}

static void testEstimates(){
  std::printf("-- memoria y espacio calculados ANTES de exportar --\n");
  AviSpec a = videoSpec(10, 128, 96);
  addPcm16(a, 1);
  Mem m; m.d = buildAvi(a);
  int rc; FlexVeSource s = probeOf(&m, &rc);
  static FlexVeEdit e; initParams(&e, s);
  e.p.rot = 1; flexVeCommit(&e);
  flexIeInit(&g_ie, nullptr, 0, 0);
  uint32_t work = flexVeExportWorkBytes(&s, &e.p, 0, 0, 0, 0);
  uint32_t est = flexVeEstimateBytes(&s, &e.p, 0, 0, 0, 0);
  long before = g_allocs;
  Out o; Run* r = doExport(&m, &s, &e.p, &o);
  (void)before;
  CHECK(r->rc == FLEXVE_OK && est >= o.d.size(), "el espacio estimado cubre lo escrito (%u >= %zu)", est, o.d.size());
  uint64_t used = (uint64_t)r->x.inCap + r->x.outCap + (uint64_t)r->x.bw * r->x.bh * 3 + r->x.arena.cap + (uint64_t)r->x.idxCap * 8 +
                  r->x.ar.rawCap + (uint64_t)r->x.ar.winCap * 2 + (uint64_t)r->x.aOutCap * 2;
  CHECK(work >= used, "la memoria calculada cubre lo que se reservo (%u >= %llu)", work, (unsigned long long)used);
  flexVeExportFree(&r->x);
}

int main(){
  std::printf("=== FlexOS · editor de video (nucleo) ===\n");
  testParams();
  testGeometry();
  testProbe();
  testDecodeAndThumb();
  testCopyExport();
  testRenderExport();
  testAudio();
  testCover();
  testFailures();
  testEstimates();
  CHECK(g_live == 0, "al acabar no queda ninguna reserva viva (%ld)", g_live);
  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
