// #############################################################
// ##  mediacheck · lo que la web genera, juzgado por el FIRMWARE
// #############################################################
//
// La web del movil (FlexOS_Ultra/webui/app.js) fabrica AVI MJPEG, WAV
// PCM y WAV IMA ADPCM en el navegador. Que Node diga que "parecen" bien
// no vale: el unico juez que importa es el codigo que corre en el P4.
// Esta herramienta pasa esos archivos por FlexOS_MediaLib, FlexOS_Media,
// FlexOS_JPEG y FlexOS_MediaThumb, exactamente como hace Flex Web Server
// al recibirlos, y responde en JSON. La usa tests/web/webui.test.js.
//
//   mediacheck sniff <archivo>                 formato real (flexMlSniff)
//   mediacheck avi   <archivo>                 abre, lee y DECODIFICA cada fotograma
//   mediacheck wav   <archivo> <salida.raw>    analiza y decodifica a PCM 16 LE
//   mediacheck jpeg  <w> <h> <semilla> <salida.jpg>   JPEG de prueba (codificador del firmware)
//   mediacheck thumb <archivo.jpg>             miniatura como la de una subida
#include "../../FlexOS_Ultra/FlexOS_MediaLib.h"
#include "../../FlexOS_Ultra/FlexOS_Media.h"
#include "../../FlexOS_Ultra/FlexOS_JPEG.h"
#include "../../FlexOS_Ultra/FlexOS_JPEGEnc.h"
#include "../../FlexOS_Ultra/FlexOS_MediaThumb.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static std::vector<uint8_t> readAll(const char* p){
  std::vector<uint8_t> v;
  FILE* f = std::fopen(p, "rb");
  if(!f){ std::printf("{\"ok\":0,\"error\":\"no se puede abrir\"}\n"); std::exit(2); }
  uint8_t b[65536]; size_t n;
  while((n = std::fread(b, 1, sizeof(b), f)) > 0) v.insert(v.end(), b, b + n);
  std::fclose(f);
  return v;
}

struct Mem { const std::vector<uint8_t>* v; uint32_t pos; };
static int memRead(void* c, void* buf, uint32_t n){
  Mem* m = (Mem*)c;
  uint32_t left = (uint32_t)m->v->size() - m->pos;
  if(n > left) n = left;
  std::memcpy(buf, m->v->data() + m->pos, n);
  m->pos += n;
  return (int)n;
}
static bool memSeek(void* c, uint32_t off){ Mem* m = (Mem*)c; if(off > m->v->size()) return false; m->pos = off; return true; }
static uint32_t memSize(void* c){ return (uint32_t)((Mem*)c)->v->size(); }

static bool sinkNull(void*, const uint8_t*, size_t){ return true; }
static bool rowNull(void*, int, int, const uint16_t*){ return true; }

static int cmdSniff(const char* path){
  auto v = readAll(path);
  int kind = FML_K_NONE;
  // Flex Web Server decide con los primeros 512 bytes: se le da lo mismo.
  int f = flexMlSniff(v.data(), v.size() < 512 ? v.size() : 512, &kind);
  std::printf("{\"ok\":1,\"fmt\":\"%s\",\"kind\":\"%s\",\"playable\":%d}\n", flexMlFmtName(f), flexMlKindName(kind),
              flexMlFmtPlayable(f) ? 1 : 0);
  return 0;
}

static int cmdAvi(const char* path){
  auto v = readAll(path);
  Mem m = { &v, 0 };
  FlexMediaIO io; io.read = memRead; io.seek = memSeek; io.size = memSize; io.ctx = &m;
  static FlexAviCtx a;
  int rc = flexAviOpen(&a, &io);
  if(rc != FLEXAVI_OK){ std::printf("{\"ok\":0,\"error\":\"%s\"}\n", flexAviErrStr(rc)); return 1; }
  std::vector<uint8_t> buf(FLEXTH_AVI_FRAME);
  int n = 0, decoded = 0, bad = 0;
  uint32_t fno = 0, maxLen = 0;
  for(;;){
    int r = flexAviReadFrame(&a, buf.data(), (uint32_t)buf.size(), &fno);
    if(r == FLEXAVI_ERR_EOF) break;
    if(r < 0){ std::printf("{\"ok\":0,\"error\":\"%s\",\"frame\":%d}\n", flexAviErrStr(r), n); return 1; }
    n++;
    if((uint32_t)r > maxLen) maxLen = (uint32_t)r;
    FlexJpegInfo inf;
    int d = flexJpegDecode(buf.data(), (size_t)r, 0, 0, 0, &inf, rowNull, nullptr, std::malloc, std::free);
    if(d == FLEXJPG_OK && inf.width == a.width && inf.height == a.height) decoded++; else bad++;
  }
  Mem m2 = { &v, 0 };
  FlexMediaIO io2 = io; io2.ctx = &m2;
  int tw = 0, th = 0; uint32_t dur = 0;
  int t = flexThumbFromAvi(&io2, FLEXTH_SIDE, FLEXTH_QUALITY, sinkNull, nullptr, &tw, &th, &dur, std::malloc, std::free);
  std::printf("{\"ok\":1,\"w\":%u,\"h\":%u,\"frames\":%d,\"declared\":%u,\"us\":%u,\"dur\":%u,\"decoded\":%d,\"bad\":%d,"
              "\"maxFrame\":%u,\"index\":%d,\"thumb\":%d,\"codec\":\"%s\"}\n",
              a.width, a.height, n, a.frames, a.usPerFrame, flexAviDurationMs(&a), decoded, bad, maxLen,
              a.idxFromFile ? 1 : 0, t == FLEXTH_OK ? 1 : 0, a.codec);
  return 0;
}

static int cmdWav(const char* path, const char* out){
  auto v = readAll(path);
  Mem m = { &v, 0 };
  FlexMediaIO io; io.read = memRead; io.seek = memSeek; io.size = memSize; io.ctx = &m;
  FlexWavInfo w;
  int rc = flexWavParse(&io, &w);
  if(rc != FLEXWAV_OK){ std::printf("{\"ok\":0,\"error\":%d}\n", rc); return 1; }
  std::vector<int16_t> pcm;
  const uint8_t* d = v.data() + w.dataStart;
  uint32_t len = w.dataBytes;
  if(w.format == FLEXWAV_FMT_PCM && w.bits == 16){
    for(uint32_t i = 0; i + 1 < len; i += 2) pcm.push_back((int16_t)(d[i] | (d[i + 1] << 8)));
  } else if(w.format == FLEXWAV_FMT_PCM && w.bits == 8){
    for(uint32_t i = 0; i < len; i++) pcm.push_back((int16_t)((d[i] - 128) << 8));
  } else if(w.format == FLEXWAV_FMT_IMA){
    std::vector<int16_t> blk((size_t)w.samplesPerBlock * w.channels);
    for(uint32_t o = 0; o < len; o += w.blockAlign){
      uint32_t n = len - o < w.blockAlign ? len - o : w.blockAlign;
      int f = flexImaDecodeBlock(d + o, n, w.channels, blk.data(), w.samplesPerBlock);
      if(f < 0){ std::printf("{\"ok\":0,\"error\":\"bloque IMA %u no valido\"}\n", o / w.blockAlign); return 1; }
      pcm.insert(pcm.end(), blk.begin(), blk.begin() + (size_t)f * w.channels);
    }
    if(w.frames && pcm.size() > (size_t)w.frames * w.channels) pcm.resize((size_t)w.frames * w.channels);
  }
  FILE* f = std::fopen(out, "wb");
  if(f){ std::fwrite(pcm.data(), 2, pcm.size(), f); std::fclose(f); }
  std::printf("{\"ok\":1,\"rate\":%u,\"ch\":%u,\"bits\":%u,\"format\":%u,\"blockAlign\":%u,\"spb\":%u,\"frames\":%u,"
              "\"dur\":%u,\"samples\":%zu}\n", w.sampleRate, w.channels, w.bits, w.format, w.blockAlign,
              w.samplesPerBlock, w.frames, flexWavDurationMs(&w), pcm.size());
  return 0;
}

static bool outFile(void* c, const uint8_t* d, size_t n){ return std::fwrite(d, 1, n, (FILE*)c) == n; }

static int cmdJpeg(int w, int h, int seed, const char* out){
  std::vector<uint8_t> px((size_t)w * h * 3);
  for(int y = 0; y < h; y++) for(int x = 0; x < w; x++){
    uint8_t* p = &px[((size_t)y * w + x) * 3];
    p[0] = (uint8_t)(x * 255 / (w > 1 ? w - 1 : 1));
    p[1] = (uint8_t)(y * 255 / (h > 1 ? h - 1 : 1));
    p[2] = (uint8_t)((x + y + seed * 37) & 255);
  }
  FILE* f = std::fopen(out, "wb");
  if(!f) return 1;
  FlexJeCfg cfg; std::memset(&cfg, 0, sizeof(cfg));
  cfg.width = w; cfg.height = h; cfg.quality = 70; cfg.subsampling = FLEXJE_SUB_420; cfg.input = FLEXJE_IN_RGB888;
  int rc = flexJpegEncodeMem(&cfg, px.data(), w * 3, outFile, f, std::malloc, std::free);
  std::fclose(f);
  std::printf("{\"ok\":%d}\n", rc == FLEXJE_OK ? 1 : 0);
  return rc == FLEXJE_OK ? 0 : 1;
}

static int cmdThumb(const char* path){
  auto v = readAll(path);
  int w = 0, h = 0;
  int rc = flexThumbFromJpeg(v.data(), v.size(), FLEXTH_SIDE, FLEXTH_QUALITY, sinkNull, nullptr, &w, &h, std::malloc, std::free);
  std::printf("{\"ok\":%d,\"w\":%d,\"h\":%d,\"error\":\"%s\"}\n", rc == FLEXTH_OK ? 1 : 0, w, h, flexThumbErrStr(rc));
  return 0;
}

int main(int argc, char** argv){
  if(argc >= 3 && !std::strcmp(argv[1], "sniff")) return cmdSniff(argv[2]);
  if(argc >= 3 && !std::strcmp(argv[1], "avi")) return cmdAvi(argv[2]);
  if(argc >= 4 && !std::strcmp(argv[1], "wav")) return cmdWav(argv[2], argv[3]);
  if(argc >= 6 && !std::strcmp(argv[1], "jpeg")) return cmdJpeg(std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]), argv[5]);
  if(argc >= 3 && !std::strcmp(argv[1], "thumb")) return cmdThumb(argv[2]);
  std::fprintf(stderr, "uso: mediacheck sniff|avi|wav|jpeg|thumb ...\n");
  return 2;
}
