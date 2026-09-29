// #############################################################
// ##  FlexOS · EDITOR DE VIDEO · implementacion
// ##  Ver FlexOS_VidEdit.h. Nada de este archivo toca Arduino, el
// ##  sistema de archivos ni la pantalla: los bytes entran por
// ##  FlexMediaIO y salen por dos funciones del llamante.
// #############################################################
#include "FlexOS_VidEdit.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

const uint16_t FLEXVE_SPEEDS[FLEXVE_SPEED_N] = { 25, 50, 75, 100, 125, 150, 200 };
const float FLEXVE_TEXT_H[3] = { 0.05f, 0.08f, 0.12f };

// -------------------------------------------------------------
//  Bytes
// -------------------------------------------------------------
static inline uint32_t rd32(const uint8_t* p){
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint16_t rd16(const uint8_t* p){ return (uint16_t)(p[0] | (p[1] << 8)); }
static inline void wr32(uint8_t* p, uint32_t v){ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static inline void wr16(uint8_t* p, uint16_t v){ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline bool fcc(const uint8_t* p, const char* s){
  return p[0] == (uint8_t)s[0] && p[1] == (uint8_t)s[1] && p[2] == (uint8_t)s[2] && p[3] == (uint8_t)s[3];
}
static inline uint32_t pad2(uint32_t n){ return n + (n & 1u); }
static inline bool isDigit(uint8_t c){ return c >= '0' && c <= '9'; }
static inline float clampf(float v, float a, float b){ return v < a ? a : (v > b ? b : v); }
static inline int   clampi(int v, int a, int b){ return v < a ? a : (v > b ? b : v); }

// Lee `n` bytes en `off` (todos o false).
static bool ioAt(const FlexMediaIO* io, uint32_t off, void* buf, uint32_t n){
  if(!io || !io->read || !io->seek) return false;
  if(!io->seek(io->ctx, off)) return false;
  uint8_t* p = (uint8_t*)buf;
  uint32_t got = 0;
  while(got < n){
    int r = io->read(io->ctx, p + got, n - got);
    if(r <= 0) return false;
    got += (uint32_t)r;
  }
  return true;
}

const char* flexVeErrStr(int err){
  switch(err){
    case FLEXVE_OK:         return "Correcto";
    case FLEXVE_ERR_ARG:    return "Par\xC3\xA1metros no v\xC3\xA1lidos";
    case FLEXVE_ERR_IO:     return "No se pudo leer el v\xC3\xAD" "deo original";
    case FLEXVE_ERR_FORMAT: return "El v\xC3\xAD" "deo est\xC3\xA1 da\xC3\xB1" "ado o incompleto";
    case FLEXVE_ERR_MEMORY: return "No hay memoria libre para este v\xC3\xAD" "deo";
    case FLEXVE_ERR_WRITE:  return "No se pudo escribir: \xC2\xBF" "almacenamiento lleno?";
    case FLEXVE_ERR_CANCEL: return "Cancelado";
    case FLEXVE_ERR_FRAME:  return "Un fotograma sale demasiado grande: elige una resoluci\xC3\xB3n menor";
    case FLEXVE_ERR_LIMIT:  return "El v\xC3\xAD" "deo editado pasar\xC3\xAD" "a del tama\xC3\xB1o m\xC3\xA1ximo: rec\xC3\xB3rtalo o baja la resoluci\xC3\xB3n";
    case FLEXVE_ERR_CODEC:  return "Solo se editan v\xC3\xAD" "deos AVI MJPEG";
    case FLEXVE_ERR_VERIFY: return "El v\xC3\xAD" "deo exportado no pasa la comprobaci\xC3\xB3n";
  }
  return "Error";
}

// #############################################################
// ##  EL ORIGINAL: cabecera, audio y recorrido de 'movi'
// #############################################################
// Formato de la PRIMERA pista de audio: recorre la cabecera (hasta 'movi') y
// lee su WAVEFORMATEX. El numero de pista es el orden de su 'strh', igual que
// en flexAviOpen.
static void probeAudioFmt(const FlexMediaIO* io, uint32_t total, FlexVeAudio* a){
  memset(a, 0, sizeof(*a));
  uint32_t p = 12;
  int streamNo = -1;
  bool curAuds = false;
  for(int guard = 0; guard < 512 && p + 8 <= total; guard++){
    uint8_t c[8];
    if(!ioAt(io, p, c, 8)) return;
    uint32_t len = rd32(c + 4);
    if(len > total) return;
    if(fcc(c, "LIST")){
      uint8_t t[4];
      if(!ioAt(io, p + 8, t, 4) || fcc(t, "movi")) return;   // la cabecera se acabo
      p += 12;
      continue;
    }
    if(fcc(c, "strh") && len >= 4){
      streamNo++;
      uint8_t v[4];
      if(!ioAt(io, p + 8, v, 4)) return;
      curAuds = fcc(v, "auds");
    } else if(fcc(c, "strf") && curAuds && a->kind == FLEXVE_AUD_NONE && len >= 16 && streamNo >= 0 && streamNo <= 99){
      uint8_t v[20]; memset(v, 0, sizeof(v));
      uint32_t n = len >= 20 ? 20u : 16u;
      if(!ioAt(io, p + 8, v, n)) return;
      uint16_t tag = rd16(v), ch = rd16(v + 2), ba = rd16(v + 12), bits = rd16(v + 14);
      uint32_t rate = rd32(v + 4);
      a->stream = (uint8_t)streamNo; a->tag = tag; a->channels = ch; a->rate = rate; a->bits = bits; a->blockAlign = ba;
      bool chOk = ch >= 1 && ch <= 2, rateOk = rate >= 4000 && rate <= 192000;
      a->kind = FLEXVE_AUD_OTHER;
      if((tag == 0x0001 || tag == 0xFFFE) && (bits == 8 || bits == 16) && chOk && rateOk){
        a->kind = FLEXVE_AUD_PCM;
        a->blockAlign = (uint16_t)(ch * (bits / 8u));
      } else if(tag == 0x0011 && bits == 4 && chOk && rateOk){
        // IMA ADPCM de Microsoft: la misma regla que flexWavParse.
        uint32_t hdr = 4u * ch;
        if(ba > hdr && ba <= 8192 && (ba % hdr) == 0){
          uint16_t expect = (uint16_t)((ba - hdr) * 8u / hdr + 1u);
          uint16_t spb = (n >= 20 && rd16(v + 16) >= 2) ? rd16(v + 18) : 0;
          if(spb == 0) spb = expect;
          if(spb == expect){ a->kind = FLEXVE_AUD_IMA; a->samplesPerBlock = spb; }
        }
      }
    }
    p += 8 + pad2(len);
  }
}

// Siguiente trozo de la pista `stream` y de la clase pedida (0 = video
// '##dc'/'##db', 1 = audio '##wb'). 1 = encontrado, 0 = no quedan (o lo que
// queda no se puede usar: un archivo cortado), <0 = error. Mismo recorrido
// acotado que el lector del reproductor: se entra en 'LIST rec ', se salta
// 'JUNK' e 'ix##', y un tamano imposible no se sigue a ciegas.
static int nextChunk(const FlexMediaIO* io, uint32_t* cursor, uint32_t end, uint8_t stream, int kind,
                     uint32_t* off, uint32_t* len){
  for(uint32_t scan = 0; ; scan++){
    if(scan > FLEXAVI_SCAN_MAX) return FLEXVE_ERR_FORMAT;
    if(*cursor > end || end - *cursor < 8) return 0;
    uint8_t c[8];
    if(!ioAt(io, *cursor, c, 8)) return FLEXVE_ERR_IO;
    uint32_t L = rd32(c + 4);
    if(fcc(c, "LIST")){ *cursor += 12; continue; }
    if(L > end || end - *cursor - 8 < L) return 0;            // no cabe: el archivo se corto aqui
    if(isDigit(c[0]) && isDigit(c[1])){
      uint8_t st = (uint8_t)((c[0] - '0') * 10 + (c[1] - '0'));
      bool match = st == stream && (kind == 0 ? (c[2] == 'd' && (c[3] == 'c' || c[3] == 'b'))
                                              : (c[2] == 'w' && c[3] == 'b'));
      if(match){
        if(off) *off = *cursor + 8;
        if(len) *len = L;
        *cursor += 8 + pad2(L);
        return 1;
      }
    } else if(L == 0) return 0;                                  // basura: no se avanza byte a byte
    *cursor += 8 + pad2(L);
  }
}

int flexVeProbe(const FlexMediaIO* io, FlexVeSource* s, FlexAviCtx* ctx, FlexVeTickFn tick, void* tickCtx){
  if(!io || !s || !ctx || !io->size) return FLEXVE_ERR_ARG;
  memset(s, 0, sizeof(*s));
  int r = flexAviOpen(ctx, io);
  if(r != FLEXAVI_OK){
    if(r == FLEXAVI_ERR_IO) return FLEXVE_ERR_IO;
    if(r == FLEXAVI_ERR_CODEC || r == FLEXAVI_ERR_NOVIDEO) return FLEXVE_ERR_CODEC;
    return FLEXVE_ERR_FORMAT;
  }
  s->fileBytes = io->size(io->ctx);
  s->w = ctx->width; s->h = ctx->height; s->usPerFrame = ctx->usPerFrame;
  s->moviStart = ctx->moviStart; s->moviEnd = ctx->moviEnd; s->videoStream = ctx->videoStream;
  if(!s->w || !s->h || s->w > FLEXJPG_MAX_DIM || s->h > FLEXJPG_MAX_DIM) return FLEXVE_ERR_FORMAT;
  if(s->usPerFrame < 1000u || s->usPerFrame > 10000000u) s->usPerFrame = 40000u;   // de 1000 a 0,1 fps
  probeAudioFmt(io, s->fileBytes, &s->aud);
  // Recorrido de 'movi': 8 bytes por trozo, nunca los datos.
  uint32_t cur = s->moviStart, end = s->moviEnd;
  for(uint32_t n = 0; ; n++){
    if(n > FLEXVE_SCAN_MAX) return FLEXVE_ERR_FORMAT;
    if((n & 127u) == 127u && tick && !tick(tickCtx)) return FLEXVE_ERR_CANCEL;
    if(cur > end || end - cur < 8) break;
    uint8_t c[8];
    if(!ioAt(io, cur, c, 8)) return FLEXVE_ERR_IO;
    uint32_t L = rd32(c + 4);
    if(fcc(c, "LIST")){ cur += 12; continue; }
    if(L > end || end - cur - 8 < L) break;
    if(isDigit(c[0]) && isDigit(c[1])){
      uint8_t st = (uint8_t)((c[0] - '0') * 10 + (c[1] - '0'));
      if(st == s->videoStream && c[2] == 'd' && (c[3] == 'c' || c[3] == 'b')){
        s->frames++;
        s->videoBytes += L;
        if(L > s->maxFrame) s->maxFrame = L;
        if(!L) s->emptyFrames++;
      } else if(s->aud.kind != FLEXVE_AUD_NONE && st == s->aud.stream && c[2] == 'w' && c[3] == 'b'){
        s->aud.chunks++;
        s->aud.bytes += L;
      }
    } else if(L == 0) break;
    cur += 8 + pad2(L);
  }
  if(!s->frames || !s->videoBytes) return FLEXVE_ERR_FORMAT;
  if(s->maxFrame > FLEXAVI_FRAME_MAX) return FLEXVE_ERR_FRAME;
  return FLEXVE_OK;
}

uint32_t flexVeSrcDurationMs(const FlexVeSource* s){
  if(!s || !s->frames) return 0;
  return (uint32_t)(((uint64_t)s->frames * s->usPerFrame) / 1000ull);
}

// #############################################################
// ##  PARAMETROS E HISTORIAL
// #############################################################
static inline uint16_t clampSpd(uint16_t s){ return s < 25 ? 25 : (s > 200 ? 200 : s); }

static void paramsIdentity(FlexVeParams* p, uint32_t frames){
  memset(p, 0, sizeof(*p));
  p->nSeg = 1; p->seg[0].a = 0; p->seg[0].b = frames;
  p->c1x = p->c1y = 1.0f;
  p->speedPct = 100; p->volPct = 100;
  p->textRgb = 0xFFFFFFu; p->textSize = 1; p->textU = 0.08f; p->textV = 0.78f;
  p->cover = FLEXVE_NO_COVER;
}
// Campo a campo (un memcmp veria el relleno o lo que queda tras el '\0').
static bool paramsEq(const FlexVeParams* a, const FlexVeParams* b){
  if(a->nSeg != b->nSeg) return false;
  for(int i = 0; i < a->nSeg && i < FLEXVE_SEG_MAX; i++) if(a->seg[i].a != b->seg[i].a || a->seg[i].b != b->seg[i].b) return false;
  if(a->rot != b->rot || a->aspect != b->aspect) return false;
  if(a->c0x != b->c0x || a->c0y != b->c0y || a->c1x != b->c1x || a->c1y != b->c1y) return false;
  if(a->speedPct != b->speedPct || a->volPct != b->volPct || a->mute != b->mute || a->filter != b->filter) return false;
  if(a->textSize != b->textSize || a->textRgb != b->textRgb || a->textU != b->textU || a->textV != b->textV) return false;
  if(strncmp(a->text, b->text, sizeof(a->text))) return false;
  return a->cover == b->cover;
}

void flexVeInit(FlexVeEdit* e, uint32_t frames, uint32_t usPerFrame, int srcW, int srcH){
  memset(e, 0, sizeof(*e));
  e->frames = frames ? frames : 1;
  e->usPerFrame = usPerFrame ? usPerFrame : 40000u;
  e->srcW = srcW > 0 ? srcW : 1; e->srcH = srcH > 0 ? srcH : 1;
  uint32_t m = 200000u / e->usPerFrame;                     // ~0,2 s
  if(m < 1) m = 1;
  if(m > e->frames) m = e->frames;
  e->minSeg = m;
  paramsIdentity(&e->p, e->frames);
  e->hist[0] = e->p;
  e->cur = e->top = 0;
}
void flexVeCommit(FlexVeEdit* e){
  if(paramsEq(&e->p, &e->hist[e->cur])) return;
  if(e->cur == FLEXVE_HIST_MAX - 1){                        // lleno: se olvida el paso mas viejo
    memmove(&e->hist[0], &e->hist[1], sizeof(FlexVeParams) * (FLEXVE_HIST_MAX - 1));
    e->cur--;
  }
  e->hist[++e->cur] = e->p;
  e->top = e->cur;
}
bool flexVeCanUndo(const FlexVeEdit* e){ return e->cur > 0; }
bool flexVeCanRedo(const FlexVeEdit* e){ return e->cur < e->top; }
bool flexVeUndo(FlexVeEdit* e){
  if(e->cur <= 0) return false;
  e->p = e->hist[--e->cur];
  return true;
}
bool flexVeRedo(FlexVeEdit* e){
  if(e->cur >= e->top) return false;
  e->p = e->hist[++e->cur];
  return true;
}
void flexVeReset(FlexVeEdit* e){
  paramsIdentity(&e->p, e->frames);
  flexVeCommit(e);
}
bool flexVeTouchesPixels(const FlexVeParams* p){
  if((p->rot & 3) || p->filter != FLEXIE_FILTER_NONE || p->text[0]) return true;
  return !(p->c0x <= 0.0f && p->c0y <= 0.0f && p->c1x >= 1.0f && p->c1y >= 1.0f);
}
bool flexVeIsIdentity(const FlexVeEdit* e){
  const FlexVeParams* p = &e->p;
  if(p->nSeg != 1 || p->seg[0].a != 0 || p->seg[0].b != e->frames) return false;
  if(flexVeTouchesPixels(p) || p->speedPct != 100 || p->volPct != 100 || p->mute) return false;
  return p->cover == FLEXVE_NO_COVER || p->cover == 0;
}

// ---- Tiempo ----
int flexVeSegAt(const FlexVeParams* p, uint32_t f){
  for(int i = 0; i < p->nSeg && i < FLEXVE_SEG_MAX; i++) if(f >= p->seg[i].a && f < p->seg[i].b) return i;
  return -1;
}
uint32_t flexVeKeptFrames(const FlexVeParams* p){
  uint32_t n = 0;
  for(int i = 0; i < p->nSeg && i < FLEXVE_SEG_MAX; i++) if(p->seg[i].b > p->seg[i].a) n += p->seg[i].b - p->seg[i].a;
  return n;
}
uint32_t flexVeFirstKept(const FlexVeParams* p){ return p->nSeg ? p->seg[0].a : 0; }
uint32_t flexVeLastKept(const FlexVeParams* p){
  if(!p->nSeg) return 0;
  uint32_t b = p->seg[p->nSeg - 1].b;
  return b ? b - 1 : 0;
}
void flexVeTrimLive(FlexVeEdit* e, int which, uint32_t f){
  FlexVeParams* p = &e->p;
  if(!p->nSeg) return;
  if(which == 0){
    FlexVeSeg* s = &p->seg[0];
    uint32_t maxA = s->b > e->minSeg ? s->b - e->minSeg : 0;
    s->a = f > maxA ? maxA : f;
  } else {
    FlexVeSeg* s = &p->seg[p->nSeg - 1];
    uint32_t minB = s->a + e->minSeg;
    if(minB > e->frames) minB = e->frames;
    if(f < minB) f = minB;
    if(f > e->frames) f = e->frames;
    s->b = f;
  }
}
bool flexVeCanSplitAt(const FlexVeEdit* e, uint32_t at){
  const FlexVeParams* p = &e->p;
  if(p->nSeg >= FLEXVE_SEG_MAX) return false;
  int i = flexVeSegAt(p, at);
  if(i < 0) return false;
  return at >= p->seg[i].a + e->minSeg && at + e->minSeg <= p->seg[i].b;
}
bool flexVeSplit(FlexVeEdit* e, uint32_t at){
  if(!flexVeCanSplitAt(e, at)) return false;
  FlexVeParams* p = &e->p;
  int i = flexVeSegAt(p, at);
  for(int k = p->nSeg; k > i + 1; k--) p->seg[k] = p->seg[k - 1];
  p->seg[i + 1].a = at; p->seg[i + 1].b = p->seg[i].b;
  p->seg[i].b = at;
  p->nSeg++;
  flexVeCommit(e);
  return true;
}
bool flexVeRemoveSeg(FlexVeEdit* e, int i){
  FlexVeParams* p = &e->p;
  if(i < 0 || i >= p->nSeg || p->nSeg <= 1) return false;
  for(int k = i; k + 1 < p->nSeg; k++) p->seg[k] = p->seg[k + 1];
  p->nSeg--;
  flexVeCommit(e);
  return true;
}

// ---- Geometria (la MISMA correspondencia que FlexOS_ImgEdit, sin volteos) ----
static void orientToSrc(int rot, float ox, float oy, float* s, float* t){
  switch(rot & 3){
    case 0:  *s = ox;        *t = oy;        break;
    case 1:  *s = oy;        *t = 1.0f - ox; break;
    case 2:  *s = 1.0f - ox; *t = 1.0f - oy; break;
    default: *s = 1.0f - oy; *t = ox;        break;
  }
}
static void srcToOrient(int rot, float s, float t, float* ox, float* oy){
  switch(rot & 3){
    case 0:  *ox = s;        *oy = t;        break;
    case 1:  *ox = 1.0f - t; *oy = s;        break;
    case 2:  *ox = 1.0f - s; *oy = 1.0f - t; break;
    default: *ox = t;        *oy = 1.0f - s; break;
  }
}
void flexVeCropLive(FlexVeEdit* e, float x0, float y0, float x1, float y1){
  if(x1 < x0){ float t = x0; x0 = x1; x1 = t; }
  if(y1 < y0){ float t = y0; y0 = y1; y1 = t; }
  x0 = clampf(x0, 0, 1); x1 = clampf(x1, 0, 1); y0 = clampf(y0, 0, 1); y1 = clampf(y1, 0, 1);
  if(x1 - x0 < FLEXIE_MIN_CROP){ x1 = clampf(x0 + FLEXIE_MIN_CROP, 0, 1); x0 = x1 - FLEXIE_MIN_CROP; }
  if(y1 - y0 < FLEXIE_MIN_CROP){ y1 = clampf(y0 + FLEXIE_MIN_CROP, 0, 1); y0 = y1 - FLEXIE_MIN_CROP; }
  e->p.c0x = x0; e->p.c0y = y0; e->p.c1x = x1; e->p.c1y = y1;
}
float flexVeAspectRatio(const FlexVeEdit* e, int aspect){
  int ow = (e->p.rot & 1) ? e->srcH : e->srcW, oh = (e->p.rot & 1) ? e->srcW : e->srcH;
  switch(aspect){
    case FLEXVE_ASP_ORIG: return (float)ow / (float)(oh > 0 ? oh : 1);
    case FLEXVE_ASP_16_9: return 16.0f / 9.0f;
    case FLEXVE_ASP_4_3:  return 4.0f / 3.0f;
    case FLEXVE_ASP_1_1:  return 1.0f;
    case FLEXVE_ASP_9_16: return 9.0f / 16.0f;
  }
  return 0.0f;
}
static void applyAspect(FlexVeEdit* e){
  float r = flexVeAspectRatio(e, e->p.aspect);
  if(r <= 0.0f) return;
  int ow = (e->p.rot & 1) ? e->srcH : e->srcW, oh = (e->p.rot & 1) ? e->srcW : e->srcH;
  // Fracciones del fotograma girado con (w*ow)/(h*oh) = r, lo mas grande posible.
  float w = 1.0f, h = (float)ow / (r * (float)oh);
  if(h > 1.0f){ h = 1.0f; w = r * (float)oh / (float)ow; }
  flexVeCropLive(e, (1.0f - w) * 0.5f, (1.0f - h) * 0.5f, (1.0f + w) * 0.5f, (1.0f + h) * 0.5f);
}
void flexVeSetAspect(FlexVeEdit* e, int aspect){
  if(aspect < 0 || aspect >= FLEXVE_ASP_N) aspect = FLEXVE_ASP_FREE;
  e->p.aspect = (uint8_t)aspect;
  applyAspect(e);
  flexVeCommit(e);
}
void flexVeSetRot(FlexVeEdit* e, int rot){
  rot &= 3;
  FlexVeParams* p = &e->p;
  if(rot == (p->rot & 3)) return;
  // El recorte gira con el contenido: mismas esquinas en el original.
  float s0, t0, s1, t1;
  orientToSrc(p->rot, p->c0x, p->c0y, &s0, &t0);
  orientToSrc(p->rot, p->c1x, p->c1y, &s1, &t1);
  p->rot = (uint8_t)rot;
  float ax, ay, bx, by;
  srcToOrient(rot, s0, t0, &ax, &ay);
  srcToOrient(rot, s1, t1, &bx, &by);
  flexVeCropLive(e, ax, ay, bx, by);
  // Con una proporcion elegida, se mantiene la proporcion de la SALIDA.
  if(p->aspect != FLEXVE_ASP_FREE) applyAspect(e);
  flexVeCommit(e);
}
void flexVeRotate(FlexVeEdit* e, int quarters){
  int r = ((int)e->p.rot + quarters) % 4;
  if(r < 0) r += 4;
  flexVeSetRot(e, r);
}

// ---- Mapa de tiempos ----
uint32_t flexVeSegOutFrames(const FlexVeParams* p, const FlexVeSeg* s){
  uint32_t n = s->b > s->a ? s->b - s->a : 0;
  if(!n) return 0;
  uint32_t spd = clampSpd(p->speedPct);
  if(spd <= 100) return n;
  return (uint32_t)(((uint64_t)n * 100u + spd - 1u) / spd);
}
uint32_t flexVeOutFrames(const FlexVeParams* p, int s0, int s1){
  uint64_t n = 0;
  if(s0 < 0) s0 = 0;
  if(s1 >= p->nSeg) s1 = p->nSeg - 1;
  for(int i = s0; i <= s1 && i < FLEXVE_SEG_MAX; i++) n += flexVeSegOutFrames(p, &p->seg[i]);
  return n > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)n;
}
uint32_t flexVeOutPeriodUs(const FlexVeParams* p, uint32_t srcUs){
  uint32_t spd = clampSpd(p->speedPct);
  if(spd >= 100) return srcUs;
  return (uint32_t)(((uint64_t)srcUs * 100u + spd / 2u) / spd);
}
uint64_t flexVeOutDurationUs(const FlexVeParams* p, uint32_t srcUs, int s0, int s1){
  return (uint64_t)flexVeOutFrames(p, s0, s1) * flexVeOutPeriodUs(p, srcUs);
}
uint32_t flexVeSrcFrameOf(const FlexVeParams* p, const FlexVeSeg* s, uint32_t k){
  uint32_t spd = clampSpd(p->speedPct);
  if(spd <= 100) return s->a + k;
  return s->a + (uint32_t)(((uint64_t)k * spd) / 100u);
}
int32_t flexVeOutFrameOf(const FlexVeParams* p, int s0, int s1, uint32_t src){
  uint32_t base = 0, spd = clampSpd(p->speedPct);
  if(s0 < 0) s0 = 0;
  if(s1 >= p->nSeg) s1 = p->nSeg - 1;
  for(int i = s0; i <= s1 && i < FLEXVE_SEG_MAX; i++){
    const FlexVeSeg* s = &p->seg[i];
    uint32_t K = flexVeSegOutFrames(p, s);
    if(!K) continue;
    if(src < s->a) return (int32_t)base;                    // quitado: el primero conservado despues
    if(src < s->b){
      uint32_t rel = src - s->a;
      uint32_t k = spd <= 100 ? rel : (uint32_t)(((uint64_t)rel * 100u + spd - 1u) / spd);
      if(k >= K) k = K - 1;
      return (int32_t)(base + k);
    }
    base += K;
  }
  return -1;
}

// ---- Region y tamano ----
static void cropNorm(const FlexVeParams* p, float* sa, float* ta, float* sb, float* tb);
void flexVeCropRect(const FlexVeParams* p, int W, int H, int* x0, int* y0, int* x1, int* y1){
  float sa, ta, sb, tb; cropNorm(p, &sa, &ta, &sb, &tb);
  int X0 = (int)floorf(sa * W + 0.001f), X1 = (int)ceilf(sb * W - 0.001f);
  int Y0 = (int)floorf(ta * H + 0.001f), Y1 = (int)ceilf(tb * H - 0.001f);
  X0 = clampi(X0, 0, W - 1); Y0 = clampi(Y0, 0, H - 1);
  X1 = clampi(X1, X0 + 1, W); Y1 = clampi(Y1, Y0 + 1, H);
  *x0 = X0; *y0 = Y0; *x1 = X1; *y1 = Y1;
}
// Recorte EXACTO en el original (normalizado, sin girar).
static void cropNorm(const FlexVeParams* p, float* sa, float* ta, float* sb, float* tb){
  float s0, t0, s1, t1;
  orientToSrc(p->rot, p->c0x, p->c0y, &s0, &t0);
  orientToSrc(p->rot, p->c1x, p->c1y, &s1, &t1);
  *sa = s0 < s1 ? s0 : s1; *sb = s0 < s1 ? s1 : s0;
  *ta = t0 < t1 ? t0 : t1; *tb = t0 < t1 ? t1 : t0;
}
// El tamano que saldra, redondeado como lo redondea el motor de pintar (no la
// region en pixeles enteros, que se redondea hacia fuera para cubrirlo).
void flexVeRegionSize(const FlexVeParams* p, int W, int H, int* rw, int* rh){
  float sa, ta, sb, tb; cropNorm(p, &sa, &ta, &sb, &tb);
  int w = (int)((sb - sa) * W + 0.5f), h = (int)((tb - ta) * H + 0.5f);
  if(w < 1) w = 1;
  if(h < 1) h = 1;
  if(p->rot & 1){ *rw = h; *rh = w; } else { *rw = w; *rh = h; }
}
bool flexVeFitShort(int rw, int rh, int shortSide, int* ow, int* oh){
  if(rw <= 0 || rh <= 0) return false;
  if(shortSide <= 0){ *ow = rw; *oh = rh; return true; }
  int sh = rw < rh ? rw : rh, lg = rw < rh ? rh : rw;
  if(shortSide > sh) return false;
  // El motor de pintar fija el LADO LARGO y saca el corto de la proporcion:
  // se pide el largo que deja el corto exactamente en `shortSide`.
  int L = (int)(((int64_t)lg * shortSide + sh / 2) / sh);
  if(L < shortSide) L = shortSide;
  if(rw < rh){ *ow = shortSide; *oh = L; } else { *ow = L; *oh = shortSide; }
  return true;
}
// Region del recorte a 1/div: [x0/div, ceil(x1/div)).
static void regionAt(int X0, int Y0, int X1, int Y1, int W, int H, int div, int* rx0, int* ry0, int* rx1, int* ry1){
  int dw = (W + div - 1) / div, dh = (H + div - 1) / div;
  *rx0 = X0 / div; *ry0 = Y0 / div;
  *rx1 = clampi((X1 + div - 1) / div, *rx0 + 1, dw);
  *ry1 = clampi((Y1 + div - 1) / div, *ry0 + 1, dh);
}
int flexVePickDiv(int rw, int rh, int needW, int needH){
  for(int d = 8; d >= 2; d >>= 1){
    if(rw / d >= needW && rh / d >= needH) return d;
  }
  return 1;
}

// #############################################################
// ##  PINTAR UN FOTOGRAMA (con el motor del editor de fotos)
// #############################################################
void flexVeSetupRender(FlexImgEdit* ie, const uint8_t* base, int bw, int bh, const FlexVeParams* p,
                       const FlexVeRegion* region, int outLong, bool cropView){
  if(!ie) return;
  if(ie->base != base || ie->W != bw || ie->H != bh) flexIeSetBase(ie, base, bw, bh);
  FlexIeState* st = &ie->st;
  memset(st, 0, sizeof(*st));
  st->c1x = st->c1y = 1.0f;
  st->filterAmt = 100;
  st->rot = (uint8_t)(p->rot & 3);
  st->filter = p->filter < FLEXIE_FILTER_N ? p->filter : (uint8_t)FLEXIE_FILTER_NONE;
  if(region && !cropView && bw > 0 && bh > 0){
    // La base es la REGION (redondeada hacia fuera): dentro de ella se recorta
    // la fraccion exacta, asi la proporcion de la salida es la elegida.
    float sa, ta, sb, tb; cropNorm(p, &sa, &ta, &sb, &tb);
    const float d = (float)region->div;
    float u0 = clampf((sa * region->srcW / d - region->rx0) / bw, 0.0f, 1.0f);
    float u1 = clampf((sb * region->srcW / d - region->rx0) / bw, 0.0f, 1.0f);
    float v0 = clampf((ta * region->srcH / d - region->ry0) / bh, 0.0f, 1.0f);
    float v1 = clampf((tb * region->srcH / d - region->ry0) / bh, 0.0f, 1.0f);
    float ax, ay, bx, by;
    srcToOrient(st->rot, u0, v0, &ax, &ay);
    srcToOrient(st->rot, u1, v1, &bx, &by);
    st->c0x = ax < bx ? ax : bx; st->c1x = ax < bx ? bx : ax;
    st->c0y = ay < by ? ay : by; st->c1y = ay < by ? by : ay;
  } else if(!cropView){ st->c0x = p->c0x; st->c0y = p->c0y; st->c1x = p->c1x; st->c1y = p->c1y; }
  st->longSide = (uint16_t)clampi(outLong, 0, 16384);
  if(p->text[0] && !cropView && ie->textFn && bw > 0 && bh > 0){
    FlexIeOverlay* o = &ie->ov[0];
    memset(o, 0, sizeof(*o));
    o->kind = FLEXIE_OV_TEXT;
    o->rgb = p->textRgb & 0xFFFFFFu;
    snprintf(o->text, sizeof(o->text), "%s", p->text);
    // Alto: fraccion del lado largo de la SALIDA -> del lado largo de la base.
    int ow = (st->rot & 1) ? bh : bw, oh = (st->rot & 1) ? bw : bh;
    float cw = (st->c1x - st->c0x) * ow, ch = (st->c1y - st->c0y) * oh;
    float cl = cw > ch ? cw : ch, bl = (float)(bw > bh ? bw : bh);
    o->th = FLEXVE_TEXT_H[p->textSize < 3 ? p->textSize : 1] * cl / bl;
    flexIeOutToBase(ie, clampf(p->textU, 0.0f, 1.0f), clampf(p->textV, 0.0f, 1.0f), &o->x0, &o->y0);
    st->nOv = 1;
  }
}

// ---- Arena ----
void flexVeArenaInit(FlexVeArena* a, void* mem, uint32_t cap){
  a->p = (uint8_t*)mem; a->cap = mem ? cap : 0; a->used = 0; a->peak = 0;
}
void* flexVeArenaAlloc(FlexVeArena* a, size_t n){
  if(!a || !a->p) return NULL;
  uint32_t off = (a->used + 15u) & ~15u;
  if(off > a->cap || n > (size_t)(a->cap - off)) return NULL;
  a->used = off + (uint32_t)n;
  if(a->used > a->peak) a->peak = a->used;
  return a->p + off;
}
void flexVeArenaReset(FlexVeArena* a){ if(a) a->used = 0; }
// Una sola tarea usa la arena a la vez (el trabajador del editor): el
// decodificador y el codificador reciben estas dos funciones.
static FlexVeArena* veCur = NULL;
static void* veAlloc(size_t n){ return veCur ? flexVeArenaAlloc(veCur, n) : NULL; }
static void  veFree(void* p){ (void)p; }                   // se vacia entera entre fotogramas

uint32_t flexVeDecodeWork(int srcW){
  uint32_t w = srcW > 0 ? (uint32_t)srcW : 1u;
  // Estado (~8 KB) + tres planos de una fila de MCU del peor submuestreo
  // (16 filas x ancho) + la fila de salida RGB888 + alineacion.
  return 16384u + 51u * (w + 16u) + 1024u;
}
uint32_t flexVeEncodeWork(int outW){
  uint32_t w = outW > 0 ? (uint32_t)outW : 1u, wp = (w + 15u) & ~15u;
  // Estado + salida (8 KB) + banda de 16 filas RGB888 + tres planos + las
  // coberturas del texto (16 filas).
  return 16384u + 48u * w + 48u * wp + 16u * w + 1024u;
}

// ---- Decodificar la region ----
struct VeDec {
  FlexJpegInfo* inf; int expW, expH, div;
  int rx0, ry0, rx1, ry1; uint8_t* dst;
  bool bad, done;
};
static bool veDecRow(void* u, int y, int w, const uint8_t* rgb){
  VeDec* c = (VeDec*)u;
  if(y == 0 && (c->inf->width != c->expW || c->inf->height != c->expH || c->inf->scaleDenom != c->div)){ c->bad = true; return false; }
  if(y < c->ry0) return true;
  if(y >= c->ry1 || c->rx1 > w){ c->done = y >= c->ry1; return false; }
  int rw = c->rx1 - c->rx0;
  memcpy(c->dst + (size_t)(y - c->ry0) * (size_t)rw * 3u, rgb + (size_t)c->rx0 * 3u, (size_t)rw * 3u);
  if(y == c->ry1 - 1){ c->done = true; return false; }       // ya esta la ultima fila: no se decodifica mas
  return true;
}
int flexVeDecode(const uint8_t* jpg, uint32_t len, int expW, int expH, int div,
                 int rx0, int ry0, int rx1, int ry1, uint8_t* dst, FlexVeArena* ar){
  if(!jpg || len < 4 || !dst || !ar || expW <= 0 || expH <= 0) return FLEXVE_ERR_ARG;
  if(div != 1 && div != 2 && div != 4 && div != 8) return FLEXVE_ERR_ARG;
  int ow = (expW + div - 1) / div, oh = (expH + div - 1) / div;
  if(rx0 < 0 || ry0 < 0 || rx1 > ow || ry1 > oh || rx0 >= rx1 || ry0 >= ry1) return FLEXVE_ERR_ARG;
  FlexJpegInfo inf; memset(&inf, 0, sizeof(inf));
  VeDec c; memset(&c, 0, sizeof(c));
  c.inf = &inf; c.expW = expW; c.expH = expH; c.div = div;
  c.rx0 = rx0; c.ry0 = ry0; c.rx1 = rx1; c.ry1 = ry1; c.dst = dst;
  flexVeArenaReset(ar);
  veCur = ar;
  int r = flexJpegDecode888(jpg, len, ow, oh, 0, &inf, veDecRow, &c, veAlloc, veFree);
  veCur = NULL;
  if(c.bad) return FLEXVE_ERR_FORMAT;
  if(c.done && (r == FLEXJPG_OK || r == FLEXJPG_ERR_ABORTED)) return FLEXVE_OK;
  if(r == FLEXJPG_ERR_MEMORY) return FLEXVE_ERR_MEMORY;
  return FLEXVE_ERR_FORMAT;
}

// ---- Miniatura ----
struct VeThumb {
  FlexJpegInfo* inf; int tw, th; uint16_t* dst;
  float cx0, cy0, cw, ch; int next; bool ready, bad; int expW, expH;
};
static bool veThumbRow(void* u, int y, int w, const uint16_t* rgb){
  VeThumb* c = (VeThumb*)u;
  if(!c->ready){
    if(c->inf->width != c->expW || c->inf->height != c->expH){ c->bad = true; return false; }
    float W = (float)c->inf->outWidth, H = (float)c->inf->outHeight;
    float k = (float)c->tw / W;
    if((float)c->th / H > k) k = (float)c->th / H;             // "cover": llena la celda y recorta lo que sobra
    c->cw = c->tw / k; c->ch = c->th / k;
    c->cx0 = (W - c->cw) * 0.5f; c->cy0 = (H - c->ch) * 0.5f;
    c->ready = true;
  }
  while(c->next < c->th){
    int sy = (int)(c->cy0 + (c->next + 0.5f) * c->ch / c->th);
    if(sy < 0) sy = 0;
    if(sy > y) return true;                                   // esta fila de la miniatura sale de una fila posterior
    uint16_t* d = c->dst + (size_t)c->next * c->tw;
    for(int tx = 0; tx < c->tw; tx++){
      int sx = (int)(c->cx0 + (tx + 0.5f) * c->cw / c->tw);
      d[tx] = rgb[sx < 0 ? 0 : (sx >= w ? w - 1 : sx)];
    }
    c->next++;
  }
  return false;                                               // hecha: no se decodifica mas
}
int flexVeThumb(const uint8_t* jpg, uint32_t len, int expW, int expH, int tw, int th, uint16_t* dst, FlexVeArena* ar){
  if(!jpg || len < 4 || !dst || !ar || tw <= 0 || th <= 0 || expW <= 0 || expH <= 0) return FLEXVE_ERR_ARG;
  // El mayor divisor que aun cubre la celda. Las medidas son las del AVI: un
  // fotograma que no las tenga se rechaza al llegar su primera fila.
  int S = 1;
  for(int d = FLEXVE_THUMB_MAX_DIV; d >= 2; d >>= 1)
    if((expW + d - 1) / d >= tw && (expH + d - 1) / d >= th){ S = d; break; }
  FlexJpegInfo inf; memset(&inf, 0, sizeof(inf));
  VeThumb c; memset(&c, 0, sizeof(c));
  c.inf = &inf; c.tw = tw; c.th = th; c.dst = dst; c.expW = expW; c.expH = expH;
  flexVeArenaReset(ar);
  veCur = ar;
  int r = flexJpegDecode(jpg, len, (expW + S - 1) / S, (expH + S - 1) / S, 0, &inf, veThumbRow, &c, veAlloc, veFree);
  veCur = NULL;
  if(!c.bad && c.next >= th) return FLEXVE_OK;
  if(r == FLEXJPG_ERR_MEMORY) return FLEXVE_ERR_MEMORY;
  return FLEXVE_ERR_FORMAT;
}

// #############################################################
// ##  EL AUDIO ORIGINAL, POR BLOQUES
// #############################################################
// Siguiente trozo de la pista de audio: deja su carga en chunkOff/chunkLeft.
static int arNextChunk(FlexVeAudioRd* r){
  uint32_t off = 0, len = 0;
  for(int guard = 0; guard < 64; guard++){
    int k = nextChunk(&r->io, &r->cursor, r->moviEnd, r->fmt.stream, 1, &off, &len);
    if(k < 0){ r->err = true; return k; }
    if(k == 0){ r->eof = true; return 0; }
    if(len){ r->chunkOff = off; r->chunkLeft = len; return 1; }
  }
  return 1;                                                   // muchos trozos vacios: se sigue en la proxima
}
// Lee hasta `n` bytes del flujo de audio (las cargas de sus trozos, en orden).
static int arRead(FlexVeAudioRd* r, uint8_t* dst, uint32_t n){
  uint32_t got = 0;
  while(got < n && !r->err){
    if(!r->chunkLeft){ if(r->eof || arNextChunk(r) <= 0) break; if(!r->chunkLeft) continue; }
    uint32_t take = n - got < r->chunkLeft ? n - got : r->chunkLeft;
    if(!ioAt(&r->io, r->chunkOff, dst + got, take)){ r->err = true; return -1; }
    r->chunkOff += take; r->chunkLeft -= take; got += take;
  }
  return r->err ? -1 : (int)got;
}
static bool arSkip(FlexVeAudioRd* r, uint64_t n){
  while(n && !r->err){
    if(!r->chunkLeft){ if(r->eof || arNextChunk(r) <= 0) break; if(!r->chunkLeft) continue; }
    uint32_t take = n < r->chunkLeft ? (uint32_t)n : r->chunkLeft;
    r->chunkOff += take; r->chunkLeft -= take; n -= take;
  }
  return !r->err;
}
// Descarta las primeras `drop` muestras de lo recien decodificado en la ventana.
static void arAppend(FlexVeAudioRd* r, uint32_t frames, uint32_t* drop){
  uint32_t ch = r->fmt.channels;
  uint32_t d = *drop < frames ? *drop : frames;
  if(d){
    int16_t* base = r->win + (size_t)r->winN * ch;
    memmove(base, base + (size_t)d * ch, (size_t)(frames - d) * ch * sizeof(int16_t));
    *drop -= d;
  }
  r->winN += frames - d;
  r->decoded += frames;
}
// Decodifica mas muestras al final de la ventana. false = no hay mas (o error).
static bool arFill(FlexVeAudioRd* r, uint32_t* drop){
  if(r->err || (r->eof && !r->chunkLeft)) return false;
  const uint32_t ch = r->fmt.channels;
  if(r->fmt.kind == FLEXVE_AUD_PCM){
    uint32_t ba = r->fmt.blockAlign, space = r->winCap - r->winN;
    uint32_t want = r->rawCap / ba;
    if(want > space) want = space;
    if(!want) return true;
    int got = arRead(r, r->raw, want * ba);
    if(got <= 0) return false;
    uint32_t frames = (uint32_t)got / ba;
    int16_t* o = r->win + (size_t)r->winN * ch;
    if(r->fmt.bits == 8){
      for(uint32_t i = 0; i < frames * ch; i++) o[i] = (int16_t)(((int)r->raw[i] - 128) * 256);
    } else {
      for(uint32_t i = 0; i < frames * ch; i++) o[i] = (int16_t)rd16(r->raw + 2 * i);
    }
    arAppend(r, frames, drop);
    return frames > 0;
  }
  // IMA ADPCM: un bloque cada vez.
  uint32_t spb = r->fmt.samplesPerBlock;
  if(r->winCap - r->winN < spb) return true;                  // sin sitio: primero se consume
  int got = arRead(r, r->raw, r->fmt.blockAlign);
  if(got <= 0) return false;
  int frames = flexImaDecodeBlock(r->raw, (size_t)got, (int)ch, r->win + (size_t)r->winN * ch, (int)spb);
  if(frames <= 0){ r->err = true; return false; }
  arAppend(r, (uint32_t)frames, drop);
  return true;
}
// Deja en la ventana las muestras s y s+1 (si existen).
static bool arEnsure(FlexVeAudioRd* r, uint64_t s){
  if(s >= r->winStart && s + 1 < r->winStart + r->winN) return true;
  if(s < r->winStart) s = r->winStart;                        // hacia atras no se va nunca
  const uint32_t ch = r->fmt.channels;
  uint64_t end = r->winStart + r->winN;
  uint32_t drop = 0;
  if(s < end){
    uint32_t keep = (uint32_t)(end - s);
    memmove(r->win, r->win + (size_t)(s - r->winStart) * ch, (size_t)keep * ch * sizeof(int16_t));
    r->winStart = s; r->winN = keep;
  } else {
    // Saltar sin decodificar lo que no se va a oir (un tramo quitado).
    uint64_t skip = s - r->decoded;
    if(r->fmt.kind == FLEXVE_AUD_PCM){
      if(!arSkip(r, skip * r->fmt.blockAlign)) return false;
      r->decoded += skip;
    } else {
      uint64_t blocks = skip / r->fmt.samplesPerBlock;
      if(!arSkip(r, blocks * r->fmt.blockAlign)) return false;
      r->decoded += blocks * r->fmt.samplesPerBlock;
      drop = (uint32_t)(s - r->decoded);
    }
    r->winStart = s; r->winN = 0;
  }
  for(int guard = 0; guard < 64 && r->winN < 2; guard++) if(!arFill(r, &drop)) break;
  // Lo que queda por detras de la ventana ya decodificado (drop sin consumir
  // porque el audio se acabo) no cambia nada: fuera de la ventana es silencio.
  return !r->err;
}
static inline int16_t arSample(const FlexVeAudioRd* r, uint64_t s, uint32_t c){
  if(s < r->winStart || s >= r->winStart + r->winN) return 0;   // mas alla del final: silencio
  return r->win[(size_t)(s - r->winStart) * r->fmt.channels + c];
}

// #############################################################
// ##  EXPORTAR
// #############################################################
static bool xPut(FlexVeExport* x, const void* d, uint32_t n){
  if(x->err) return false;
  if(n && !x->c.write(x->c.outCtx, d, n)){ x->err = FLEXVE_ERR_WRITE; return false; }
  x->pos += n;
  return true;
}
static inline uint8_t* hcc(uint8_t* q, const char* s){ memcpy(q, s, 4); return q + 4; }
static inline uint8_t* h32(uint8_t* q, uint32_t v){ wr32(q, v); return q + 4; }
static inline uint8_t* h16(uint8_t* q, uint16_t v){ wr16(q, v); return q + 2; }


// Cabecera completa con los valores ACTUALES. Mide siempre lo mismo para una
// misma exportacion (depende solo de si hay audio y portada), asi que la
// definitiva se escribe encima de la provisional al terminar.
#define VE_HDR_MAX 512
static uint32_t xHeader(const FlexVeExport* x, uint8_t* h){
  const FlexVeSource* s = x->c.src;
  const bool aud = x->audioOn, info = x->hasInfo;
  const uint16_t ch = aud ? s->aud.channels : 1;
  const uint32_t fs = aud ? s->aud.rate : 0, ba = 2u * ch;
  const uint32_t strlV = 4 + 64 + 48, strlA = 4 + 64 + 26, infoL = 4 + 16 + 12;
  const uint32_t hdrl = 4 + 64 + 8 + strlV + (aud ? 8 + strlA : 0);
  const uint32_t mEnd = x->finished ? x->moviEndPos : x->pos;
  const uint32_t movi = mEnd > x->moviList + 8 ? mEnd - (x->moviList + 8) : 4u;
  const uint32_t riff = x->pos > 8 ? x->pos - 8 : 0;
  const uint32_t per = x->periodUs ? x->periodUs : 1u;
  uint8_t* q = h;
  q = hcc(q, "RIFF"); q = h32(q, riff); q = hcc(q, "AVI ");
  q = hcc(q, "LIST"); q = h32(q, hdrl); q = hcc(q, "hdrl");
  // ---- avih ----
  q = hcc(q, "avih"); q = h32(q, 56);
  q = h32(q, per);
  uint64_t bps = (uint64_t)(x->maxV + 8u) * 1000000ull / per + (aud ? (uint64_t)fs * ba : 0);
  q = h32(q, bps > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)bps);
  q = h32(q, 0);                                        // granularidad
  q = h32(q, 0x10u | (aud ? 0x100u : 0u));              // AVIF_HASINDEX | AVIF_ISINTERLEAVED
  q = h32(q, x->vChunks);
  q = h32(q, 0);
  q = h32(q, aud ? 2u : 1u);
  q = h32(q, (x->maxV > x->maxA ? x->maxV : x->maxA) + 8u);
  q = h32(q, (uint32_t)x->outW); q = h32(q, (uint32_t)x->outH);
  for(int i = 0; i < 4; i++) q = h32(q, 0);
  // ---- pista de video: MJPEG ----
  q = hcc(q, "LIST"); q = h32(q, strlV); q = hcc(q, "strl");
  q = hcc(q, "strh"); q = h32(q, 56);
  q = hcc(q, "vids"); q = hcc(q, "MJPG"); q = h32(q, 0); q = h16(q, 0); q = h16(q, 0); q = h32(q, 0);
  q = h32(q, per); q = h32(q, 1000000u);                // fps = 1e6 / periodo
  q = h32(q, 0); q = h32(q, x->vChunks);
  q = h32(q, x->maxV + 8u); q = h32(q, 0xFFFFFFFFu); q = h32(q, 0);
  q = h16(q, 0); q = h16(q, 0); q = h16(q, (uint16_t)x->outW); q = h16(q, (uint16_t)x->outH);
  q = hcc(q, "strf"); q = h32(q, 40);
  q = h32(q, 40); q = h32(q, (uint32_t)x->outW); q = h32(q, (uint32_t)x->outH); q = h16(q, 1); q = h16(q, 24);
  q = hcc(q, "MJPG"); q = h32(q, (uint32_t)x->outW * (uint32_t)x->outH * 3u);
  for(int i = 0; i < 4; i++) q = h32(q, 0);
  // ---- pista de audio: PCM de 16 bits ----
  if(aud){
    q = hcc(q, "LIST"); q = h32(q, strlA); q = hcc(q, "strl");
    q = hcc(q, "strh"); q = h32(q, 56);
    q = hcc(q, "auds"); q = h32(q, 0); q = h32(q, 0); q = h16(q, 0); q = h16(q, 0); q = h32(q, 0);
    q = h32(q, ba); q = h32(q, fs * ba);                // muestras por segundo = rate / scale
    q = h32(q, 0); q = h32(q, (uint32_t)(x->aBytes / ba));
    q = h32(q, x->maxA); q = h32(q, 0xFFFFFFFFu); q = h32(q, ba);
    q = h16(q, 0); q = h16(q, 0); q = h16(q, 0); q = h16(q, 0);
    q = hcc(q, "strf"); q = h32(q, 18);
    q = h16(q, 1); q = h16(q, ch); q = h32(q, fs); q = h32(q, fs * ba); q = h16(q, (uint16_t)ba); q = h16(q, 16); q = h16(q, 0);
  }
  // ---- portada: el fotograma que usan las miniaturas (FlexOS_MediaThumb) ----
  if(info){
    q = hcc(q, "LIST"); q = h32(q, infoL); q = hcc(q, "INFO");
    q = hcc(q, "ISFT"); q = h32(q, 8); memcpy(q, "Flex OS", 8); q += 8;
    q = hcc(q, "IFCV"); q = h32(q, 4); q = h32(q, x->coverOut > 0 ? (uint32_t)x->coverOut : 0u);
  }
  q = hcc(q, "LIST"); q = h32(q, movi); q = hcc(q, "movi");
  return (uint32_t)(q - h);
}

// Un trozo de 'movi' (con su entrada del indice). El tope del archivo cuenta
// ya con el indice que falta por escribir.
static bool xChunk(FlexVeExport* x, bool audio, const uint8_t* d, uint32_t n){
  if(x->err) return false;
  uint64_t after = (uint64_t)x->pos + 8u + pad2(n) + 8u + 16ull * (x->idxN + 1u);
  if(x->c.maxBytes && after > x->c.maxBytes){ x->err = FLEXVE_ERR_LIMIT; return false; }
  if(x->idxN >= x->idxCap){ x->err = FLEXVE_ERR_MEMORY; return false; }
  uint8_t h[8];
  memcpy(h, audio ? "01wb" : "00dc", 4);
  wr32(h + 4, n);
  uint32_t rel = x->pos - (x->moviList + 8);            // idx1: desde el 'movi' de su LIST
  if(!xPut(x, h, 8)) return false;
  if(n && !xPut(x, d, n)) return false;
  if(n & 1u){ uint8_t z = 0; if(!xPut(x, &z, 1)) return false; }
  x->idx[2 * x->idxN] = rel;
  x->idx[2 * x->idxN + 1] = n | (audio ? 0x80000000u : 0u);
  x->idxN++;
  if(audio){ x->aChunks++; x->aBytes += n; if(n > x->maxA) x->maxA = n; }
  else     { x->vChunks++; if(n > x->maxV) x->maxV = n; }
  return true;
}

// Salida del codificador: al buffer del fotograma (crece hasta el tope del
// reproductor; lo que pase de ahi no lo podria abrir nadie en el P4).
static bool xOutCb(void* u, const uint8_t* d, size_t n){
  FlexVeExport* x = (FlexVeExport*)u;
  if((uint64_t)x->outLen + n > x->outCap){
    if((uint64_t)x->outLen + n > FLEXAVI_FRAME_MAX){ x->outOver = true; return false; }
    uint32_t cap = x->outCap * 2u;
    if(cap < x->outLen + (uint32_t)n) cap = x->outLen + (uint32_t)n;
    cap = (cap + 4095u) & ~4095u;
    if(cap > FLEXAVI_FRAME_MAX) cap = FLEXAVI_FRAME_MAX;
    uint8_t* nb = (uint8_t*)x->c.alloc(cap);
    if(!nb){ x->err = FLEXVE_ERR_MEMORY; return false; }
    if(x->outLen) memcpy(nb, x->outBuf, x->outLen);
    x->c.free(x->outBuf);
    x->outBuf = nb; x->outCap = cap;
  }
  memcpy(x->outBuf + x->outLen, d, n);
  x->outLen += (uint32_t)n;
  return true;
}

// Lleva la lectura del video hasta el fotograma `f` (incluido). La imagen
// vigente (el ultimo fotograma CON datos: un trozo vacio repite el anterior)
// queda en curOff/curLen. 1 = bien, 0 = el archivo se acabo antes, <0 = error.
static int xVideoTo(FlexVeExport* x, uint32_t f){
  const FlexVeSource* s = x->c.src;
  uint32_t n = 0;
  while(x->vFrame <= f){
    uint32_t off = 0, len = 0;
    int k = nextChunk(&x->c.io, &x->vCursor, s->moviEnd, s->videoStream, 0, &off, &len);
    if(k <= 0) return k;
    if(len){ x->curOff = off; x->curLen = len; x->haveCur = true; }
    x->vFrame++;
    // Un tramo quitado largo son muchas cabeceras seguidas: se cede y se
    // puede cancelar a mitad.
    if((++n & 255u) == 0 && x->c.tick && !x->c.tick(x->c.tickCtx)) return FLEXVE_ERR_CANCEL;
  }
  // El original empieza con trozos vacios: la imagen es el primero con datos.
  for(int guard = 0; guard < 4096 && !x->haveCur; guard++){
    uint32_t off = 0, len = 0;
    int k = nextChunk(&x->c.io, &x->vCursor, s->moviEnd, s->videoStream, 0, &off, &len);
    if(k <= 0) return k;
    x->vFrame++;
    if(len){ x->curOff = off; x->curLen = len; x->haveCur = true; }
  }
  return x->haveCur ? 1 : FLEXVE_ERR_FORMAT;
}

static bool xEmitVideo(FlexVeExport* x){
  // La misma imagen que el fotograma anterior: un trozo vacio (8 bytes), que
  // en AVI significa "repetir".
  if(x->haveOut && x->curOff == x->lastOutOff) return xChunk(x, false, NULL, 0);
  const int32_t prevReal = x->lastRealOut;
  x->lastRealOut = (int32_t)x->outDone;                 // este fotograma lleva imagen
  if(x->curLen > x->inCap){                             // el analisis ya lo dimensiono: no deberia pasar
    if(x->curLen > FLEXAVI_FRAME_MAX){ x->err = FLEXVE_ERR_FRAME; return false; }
    uint32_t cap = (x->curLen + 4095u) & ~4095u;
    if(cap > FLEXAVI_FRAME_MAX) cap = FLEXAVI_FRAME_MAX;
    uint8_t* nb = (uint8_t*)x->c.alloc(cap);
    if(!nb){ x->err = FLEXVE_ERR_MEMORY; return false; }
    x->c.free(x->inBuf);
    x->inBuf = nb; x->inCap = cap;
  }
  if(!ioAt(&x->c.io, x->curOff, x->inBuf, x->curLen)){ x->err = FLEXVE_ERR_IO; return false; }
  if(x->copy){
    if(!xChunk(x, false, x->inBuf, x->curLen)) return false;
  } else {
    const FlexVeSource* s = x->c.src;
    int r = flexVeDecode(x->inBuf, x->curLen, s->w, s->h, x->div, x->rx0, x->ry0, x->rx1, x->ry1, x->base, &x->arena);
    if(r == FLEXVE_ERR_MEMORY){ x->err = r; return false; }
    if(r != FLEXVE_OK){
      x->badFrames++;
      if(x->haveOut){ x->lastRealOut = prevReal; return xChunk(x, false, NULL, 0); }   // danado: se queda el anterior
      // Sin anterior (el primero): sale lo que haya en la base.
    }
    x->outLen = 0; x->outOver = false;
    flexVeArenaReset(&x->arena);
    veCur = &x->arena;
    int e = flexIeSave(x->c.ie, x->c.quality > 0 ? x->c.quality : FLEXVE_QUALITY, xOutCb, x,
                       NULL, NULL, NULL, veAlloc, veFree);
    veCur = NULL;
    if(x->err) return false;
    if(x->outOver){ x->err = FLEXVE_ERR_FRAME; return false; }
    if(e != FLEXJE_OK){ x->err = e == FLEXJE_ERR_MEMORY ? FLEXVE_ERR_MEMORY : FLEXVE_ERR_FORMAT; return false; }
    if(!xChunk(x, false, x->outBuf, x->outLen)) return false;
  }
  x->lastOutOff = x->curOff; x->haveOut = true;
  return true;
}

// Posicion del audio original al empezar un tramo (32.32, en muestras).
static void xAudioSeg(FlexVeExport* x){
  const FlexVeSource* s = x->c.src;
  const FlexVeSeg* sg = &x->p.seg[x->seg];
  double smp = (double)sg->a * (double)s->usPerFrame * (double)s->aud.rate / 1e6;
  x->aPos = (uint64_t)(smp * 4294967296.0);
}

// El audio que corresponde al fotograma de salida que se acaba de escribir:
// el original, a la velocidad elegida (interpolacion lineal: a 1x y sin
// cambiar el volumen sale exactamente igual) y con su volumen.
static bool xEmitAudio(FlexVeExport* x){
  const FlexVeSource* s = x->c.src;
  const uint32_t ch = s->aud.channels, fs = s->aud.rate;
  uint64_t n0 = (uint64_t)x->outDone * x->periodUs * fs / 1000000ull;
  uint64_t n1 = (uint64_t)(x->outDone + 1u) * x->periodUs * fs / 1000000ull;
  uint32_t n = (uint32_t)(n1 - n0);
  if(n > x->aOutCap) n = x->aOutCap;
  const int32_t gain = (int32_t)x->p.volPct * 32768 / 100;   // Q15
  uint8_t* o = (uint8_t*)x->aOut;
  FlexVeAudioRd* r = &x->ar;
  for(uint32_t i = 0; i < n; i++){
    uint64_t si = x->aPos >> 32;
    int32_t fr = (int32_t)((x->aPos & 0xFFFFFFFFull) >> 16);
    if(!(si >= r->winStart && si + 1 < r->winStart + r->winN) && !r->eof){
      if(!arEnsure(r, si)){ x->err = FLEXVE_ERR_IO; return false; }
    }
    for(uint32_t c = 0; c < ch; c++){
      int32_t a = arSample(r, si, c), b = arSample(r, si + 1, c);
      int32_t v = a + (int32_t)(((int64_t)(b - a) * fr) / 65536);
      v = (int32_t)(((int64_t)v * gain) / 32768);
      if(v > 32767) v = 32767;
      if(v < -32768) v = -32768;
      wr16(o, (uint16_t)(int16_t)v); o += 2;
    }
    x->aPos += x->aStep;
  }
  return xChunk(x, true, (const uint8_t*)x->aOut, n * ch * 2u);
}

bool flexVeCopyMode(const FlexVeSource* s, const FlexVeParams* p, int outW, int outH){
  if(!s || !p || flexVeTouchesPixels(p)) return false;
  return (outW <= 0 || outW >= s->w) && (outH <= 0 || outH >= s->h);
}

// Lo mismo que calcula flexVeExportBegin, sin reservar nada.
struct VePlan { int ow, oh, div, bw, bh, rx0, ry0, rx1, ry1; bool copy, audio; };
static void xPlan(const FlexVeSource* s, const FlexVeParams* p, int outW, int outH, VePlan* pl){
  memset(pl, 0, sizeof(*pl));
  int rw, rh; flexVeRegionSize(p, s->w, s->h, &rw, &rh);
  pl->ow = outW > 0 ? outW : rw; pl->oh = outH > 0 ? outH : rh;
  if(pl->ow > rw) pl->ow = rw;
  if(pl->oh > rh) pl->oh = rh;
  pl->copy = flexVeCopyMode(s, p, pl->ow, pl->oh);
  pl->audio = (s->aud.kind == FLEXVE_AUD_PCM || s->aud.kind == FLEXVE_AUD_IMA) && !p->mute && p->volPct > 0 && s->aud.chunks > 0;
  if(pl->copy) return;
  int X0, Y0, X1, Y1; flexVeCropRect(p, s->w, s->h, &X0, &Y0, &X1, &Y1);
  int needW = (p->rot & 1) ? pl->oh : pl->ow, needH = (p->rot & 1) ? pl->ow : pl->oh;
  pl->div = flexVePickDiv(X1 - X0, Y1 - Y0, needW, needH);
  regionAt(X0, Y0, X1, Y1, s->w, s->h, pl->div, &pl->rx0, &pl->ry0, &pl->rx1, &pl->ry1);
  pl->bw = pl->rx1 - pl->rx0; pl->bh = pl->ry1 - pl->ry0;
}
static uint32_t xOutCapFor(int w, int h){
  uint32_t oc = (uint32_t)w * (uint32_t)h / 2u + 32768u;
  if(oc < 65536u) oc = 65536u;
  oc = (oc + 4095u) & ~4095u;
  return oc > FLEXAVI_FRAME_MAX ? FLEXAVI_FRAME_MAX : oc;
}
static uint32_t xInCapFor(const FlexVeSource* s){
  uint32_t ic = (s->maxFrame + 4095u) & ~4095u;
  if(ic < 4096u) ic = 4096u;
  return ic > FLEXAVI_FRAME_MAX ? FLEXAVI_FRAME_MAX : ic;
}
static uint32_t xAudWinFor(const FlexVeAudio* a){
  uint32_t wc = FLEXVE_AUD_WIN_MIN;
  if(a->kind == FLEXVE_AUD_IMA && (uint32_t)a->samplesPerBlock + 2u > wc) wc = a->samplesPerBlock + 2u;
  return wc;
}

uint32_t flexVeExportWorkBytes(const FlexVeSource* s, const FlexVeParams* p, int seg0, int seg1, int outW, int outH){
  if(!s || !p) return 0;
  VePlan pl; xPlan(s, p, outW, outH, &pl);
  uint64_t b = 16384u + xInCapFor(s);
  uint32_t frames = flexVeOutFrames(p, seg0, seg1);
  b += ((uint64_t)frames * (pl.audio ? 2u : 1u) + 4u) * 8u;
  if(!pl.copy){
    b += (uint64_t)pl.bw * pl.bh * 3u;
    uint32_t aw = flexVeDecodeWork(s->w), ew = flexVeEncodeWork(pl.ow > pl.oh ? pl.ow : pl.oh);
    b += aw > ew ? aw : ew;
    b += xOutCapFor(pl.ow, pl.oh);
  }
  if(pl.audio){
    uint32_t per = flexVeOutPeriodUs(p, s->usPerFrame);
    b += 8192u + (uint64_t)xAudWinFor(&s->aud) * s->aud.channels * 2u;
    b += ((uint64_t)per * s->aud.rate / 1000000u + 4u) * s->aud.channels * 2u;
  }
  return b > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)b;
}

uint32_t flexVeEstimateBytes(const FlexVeSource* s, const FlexVeParams* p, int seg0, int seg1, int outW, int outH){
  if(!s || !p || !s->frames) return 0;
  VePlan pl; xPlan(s, p, outW, outH, &pl);
  uint32_t frames = flexVeOutFrames(p, seg0, seg1);
  uint32_t real = s->frames > s->emptyFrames ? s->frames - s->emptyFrames : 1u;
  double avg = (double)s->videoBytes / (double)real;
  double vid;
  if(pl.copy) vid = avg * 1.1 * frames;
  else {
    double bpp = avg / ((double)s->w * s->h);
    if(bpp < 0.12) bpp = 0.12;
    double per = (double)pl.ow * pl.oh * bpp * 1.4;
    if(per > FLEXAVI_FRAME_MAX) per = FLEXAVI_FRAME_MAX;
    vid = per * frames;
  }
  double aud = 0;
  if(pl.audio) aud = (double)flexVeOutDurationUs(p, s->usPerFrame, seg0, seg1) / 1e6 * s->aud.rate * s->aud.channels * 2.0;
  double t = 1024.0 + vid + aud + 24.0 * frames * (pl.audio ? 2 : 1);
  return t > 4.0e9 ? 0xFFFFFFFFu : (uint32_t)t;
}

int flexVeExportBegin(FlexVeExport* x, const FlexVeExportCfg* cfg){
  if(!x) return FLEXVE_ERR_ARG;
  memset(x, 0, sizeof(*x));
  if(!cfg || !cfg->src || !cfg->params || !cfg->write || !cfg->seek || !cfg->alloc || !cfg->free ||
     !cfg->io.read || !cfg->io.seek) return x->err = FLEXVE_ERR_ARG;
  x->c = *cfg;
  x->p = *cfg->params;
  const FlexVeSource* s = cfg->src;
  FlexVeParams* p = &x->p;
  if(!s->frames || !s->w || !s->h || !s->usPerFrame || p->nSeg < 1 || p->nSeg > FLEXVE_SEG_MAX) return x->err = FLEXVE_ERR_ARG;
  for(int i = 0; i < p->nSeg; i++){
    if(p->seg[i].b > s->frames) p->seg[i].b = s->frames;
    if(p->seg[i].a >= p->seg[i].b) return x->err = FLEXVE_ERR_ARG;
    if(i && p->seg[i].a < p->seg[i - 1].b) return x->err = FLEXVE_ERR_ARG;
  }
  if(cfg->seg0 < 0 || cfg->seg1 >= p->nSeg || cfg->seg0 > cfg->seg1) return x->err = FLEXVE_ERR_ARG;
  p->speedPct = clampSpd(p->speedPct);
  if(p->volPct > 100) p->volPct = 100;
  x->spd = p->speedPct;
  x->periodUs = flexVeOutPeriodUs(p, s->usPerFrame);
  x->outTotal = flexVeOutFrames(p, cfg->seg0, cfg->seg1);
  if(!x->outTotal) return x->err = FLEXVE_ERR_ARG;
  if(x->outTotal > FLEXVE_OUT_FRAMES_MAX) return x->err = FLEXVE_ERR_LIMIT;
  x->seg = cfg->seg0;
  x->coverOut = p->cover == FLEXVE_NO_COVER ? -1 : flexVeOutFrameOf(p, cfg->seg0, cfg->seg1, p->cover);
  x->hasInfo = x->coverOut > 0;
  x->lastRealOut = 0;
  x->audioDropped = s->aud.kind == FLEXVE_AUD_OTHER;

  VePlan pl; xPlan(s, p, cfg->outW, cfg->outH, &pl);
  x->audioOn = pl.audio;
  x->copy = pl.copy;
  if(x->copy){ x->outW = s->w; x->outH = s->h; }
  else {
    if(!cfg->ie) return x->err = FLEXVE_ERR_ARG;
    x->div = pl.div; x->rx0 = pl.rx0; x->ry0 = pl.ry0; x->rx1 = pl.rx1; x->ry1 = pl.ry1;
    x->bw = pl.bw; x->bh = pl.bh;
    x->base = (uint8_t*)cfg->alloc((size_t)x->bw * x->bh * 3u);
    if(!x->base) return x->err = FLEXVE_ERR_MEMORY;
    memset(x->base, 0, (size_t)x->bw * x->bh * 3u);        // negro si el primero estuviera danado
    uint32_t aw = flexVeDecodeWork(s->w), ew = flexVeEncodeWork(pl.ow > pl.oh ? pl.ow : pl.oh);
    uint32_t cap = aw > ew ? aw : ew;
    x->arenaMem = (uint8_t*)cfg->alloc(cap);
    if(!x->arenaMem) return x->err = FLEXVE_ERR_MEMORY;
    flexVeArenaInit(&x->arena, x->arenaMem, cap);
    FlexVeRegion rg = { s->w, s->h, x->div, x->rx0, x->ry0 };
    flexVeSetupRender(cfg->ie, x->base, x->bw, x->bh, p, &rg, pl.ow > pl.oh ? pl.ow : pl.oh, false);
    flexIeOutSize(cfg->ie, &x->outW, &x->outH);
    x->outCap = xOutCapFor(x->outW, x->outH);
    x->outBuf = (uint8_t*)cfg->alloc(x->outCap);
    if(!x->outBuf) return x->err = FLEXVE_ERR_MEMORY;
  }
  x->inCap = xInCapFor(s);
  x->inBuf = (uint8_t*)cfg->alloc(x->inCap);
  if(!x->inBuf) return x->err = FLEXVE_ERR_MEMORY;
  x->idxCap = x->outTotal * (x->audioOn ? 2u : 1u) + 4u;
  x->idx = (uint32_t*)cfg->alloc((size_t)x->idxCap * 8u);
  if(!x->idx) return x->err = FLEXVE_ERR_MEMORY;
  if(x->audioOn){
    FlexVeAudioRd* r = &x->ar;
    r->io = cfg->io; r->fmt = s->aud;
    r->cursor = s->moviStart; r->moviEnd = s->moviEnd;
    const uint32_t ch = s->aud.channels;
    r->rawCap = s->aud.kind == FLEXVE_AUD_IMA ? s->aud.blockAlign : (4096u / s->aud.blockAlign) * s->aud.blockAlign;
    if(r->rawCap < s->aud.blockAlign) r->rawCap = s->aud.blockAlign;
    r->raw = (uint8_t*)cfg->alloc(r->rawCap);
    r->winCap = xAudWinFor(&s->aud);
    r->win = (int16_t*)cfg->alloc((size_t)r->winCap * ch * 2u);
    x->aOutCap = (uint32_t)(((uint64_t)x->periodUs * s->aud.rate + 999999u) / 1000000u) + 2u;
    x->aOut = (int16_t*)cfg->alloc((size_t)x->aOutCap * ch * 2u);
    if(!r->raw || !r->win || !x->aOut) return x->err = FLEXVE_ERR_MEMORY;
    x->aStep = ((uint64_t)x->spd << 32) / 100u;
    xAudioSeg(x);
  }
  uint8_t h[VE_HDR_MAX];
  x->hdrBytes = xHeader(x, h);
  x->moviList = x->hdrBytes - 12;
  x->hdrBytes = xHeader(x, h);                            // mismo tamano; 'movi' ya sabe donde esta
  if(!xPut(x, h, x->hdrBytes)) return x->err;
  x->vCursor = s->moviStart;
  x->vFrame = 0;
  return FLEXVE_OK;
}

int flexVeExportStep(FlexVeExport* x){
  if(!x) return FLEXVE_ERR_ARG;
  if(x->err) return x->err;
  if(x->finished) return 0;
  if(x->outDone >= x->outTotal){ x->finished = true; x->moviEndPos = x->pos; return 0; }
  if(x->c.tick && !x->c.tick(x->c.tickCtx)) return x->err = FLEXVE_ERR_CANCEL;
  const FlexVeSeg* sg = &x->p.seg[x->seg];
  uint32_t f = flexVeSrcFrameOf(&x->p, sg, x->kInSeg);
  int r = xVideoTo(x, f);
  if(r < 0) return x->err = r;
  if(r == 0){                                            // el original se acaba antes (grabacion cortada)
    if(!x->outDone) return x->err = FLEXVE_ERR_FORMAT;
    x->outTotal = x->outDone;
    x->finished = true; x->moviEndPos = x->pos;
    return 0;
  }
  if(!xEmitVideo(x)) return x->err ? x->err : (x->err = FLEXVE_ERR_WRITE);
  if(x->audioOn && !xEmitAudio(x)) return x->err ? x->err : (x->err = FLEXVE_ERR_WRITE);
  // La portada cayo en un fotograma repetido: la imagen que se ve ahi es la
  // del ultimo con datos, y es la que tiene que usar la miniatura.
  if((int32_t)x->outDone == x->coverOut && x->lastRealOut != x->coverOut) x->coverOut = x->lastRealOut;
  x->outDone++;
  if(++x->kInSeg >= flexVeSegOutFrames(&x->p, sg)){
    x->seg++; x->kInSeg = 0;
    if(x->audioOn && x->seg <= x->c.seg1) xAudioSeg(x);
  }
  if(x->outDone >= x->outTotal){ x->finished = true; x->moviEndPos = x->pos; return 0; }
  return 1;
}

int flexVeExportFinish(FlexVeExport* x){
  if(!x) return FLEXVE_ERR_ARG;
  if(x->err) return x->err;
  if(!x->finished) return x->err = FLEXVE_ERR_ARG;
  if(x->c.maxBytes && (uint64_t)x->pos + 8u + 16ull * x->idxN > x->c.maxBytes) return x->err = FLEXVE_ERR_LIMIT;
  uint8_t h[8];
  memcpy(h, "idx1", 4);
  wr32(h + 4, 16u * x->idxN);
  if(!xPut(x, h, 8)) return x->err;
  uint8_t e[16 * 32];
  for(uint32_t i = 0; i < x->idxN; ){
    uint32_t n = 0;
    for(; n < 32 && i < x->idxN; n++, i++){
      uint8_t* q = e + 16 * n;
      uint32_t sz = x->idx[2 * i + 1];
      memcpy(q, (sz & 0x80000000u) ? "01wb" : "00dc", 4);
      wr32(q + 4, 0x10u);                                  // AVIIF_KEYFRAME: todo MJPEG lo es
      wr32(q + 8, x->idx[2 * i]);
      wr32(q + 12, sz & 0x7FFFFFFFu);
    }
    if(!xPut(x, e, 16 * n)) return x->err;
  }
  uint8_t hd[VE_HDR_MAX];
  uint32_t hb = xHeader(x, hd);
  if(hb != x->hdrBytes) return x->err = FLEXVE_ERR_ARG;
  if(!x->c.seek(x->c.outCtx, 0) || !x->c.write(x->c.outCtx, hd, hb)) return x->err = FLEXVE_ERR_WRITE;
  return FLEXVE_OK;
}

void flexVeExportFree(FlexVeExport* x){
  if(!x) return;
  // El motor de pintar apunta a la base que se va: se le quita antes.
  if(x->c.ie && x->base && x->c.ie->base == x->base) flexIeSetBase(x->c.ie, NULL, 0, 0);
  FlexJpegFree f = x->c.free;
  if(f){
    f(x->inBuf); f(x->outBuf); f(x->base); f(x->arenaMem); f(x->idx);
    f(x->ar.raw); f(x->ar.win); f(x->aOut);
  }
  x->inBuf = x->outBuf = x->base = x->arenaMem = NULL;
  x->idx = NULL; x->ar.raw = NULL; x->ar.win = NULL; x->aOut = NULL;
  x->inCap = x->outCap = x->idxCap = 0;
  flexVeArenaInit(&x->arena, NULL, 0);
}

int flexVeExportProgress(const FlexVeExport* x){
  if(!x || !x->outTotal) return 0;
  uint64_t v = (uint64_t)x->outDone * 1000u / x->outTotal;
  return v > 1000 ? 1000 : (int)v;
}

// #############################################################
// ##  COMPROBAR LO ESCRITO
// #############################################################
struct VeRd { const FlexMediaIO* io; uint32_t pos, end; };
static int veRd(void* c, uint8_t* buf, size_t n){
  VeRd* r = (VeRd*)c;
  if(r->pos >= r->end) return 0;
  uint32_t k = (uint32_t)(n < (size_t)(r->end - r->pos) ? n : (size_t)(r->end - r->pos));
  if(!ioAt(r->io, r->pos, buf, k)) return -1;
  r->pos += k;
  return (int)k;
}
int flexVeVerify(const FlexMediaIO* io, uint32_t fileBytes, uint32_t expFrames, int expW, int expH,
                 bool expAudio, FlexAviCtx* ctx, FlexJpegAlloc af, FlexJpegFree ff){
  if(!io || !io->size || !ctx || !expFrames) return FLEXVE_ERR_ARG;
  if(io->size(io->ctx) != fileBytes) return FLEXVE_ERR_VERIFY;
  if(flexAviOpen(ctx, io) != FLEXAVI_OK) return FLEXVE_ERR_VERIFY;
  if(ctx->width != expW || ctx->height != expH || ctx->frames != expFrames || !ctx->idxFromFile) return FLEXVE_ERR_VERIFY;
  FlexVeAudio a; probeAudioFmt(io, fileBytes, &a);
  if(expAudio ? (a.kind != FLEXVE_AUD_PCM || a.bits != 16) : (a.kind != FLEXVE_AUD_NONE)) return FLEXVE_ERR_VERIFY;
  // El primer fotograma con datos es un JPEG de las medidas esperadas.
  uint32_t cur = ctx->moviStart, off = 0, len = 0;
  bool found = false;
  for(uint32_t i = 0; i < expFrames && i < 4096u && !found; i++){
    int k = nextChunk(io, &cur, ctx->moviEnd, ctx->videoStream, 0, &off, &len);
    if(k <= 0) return FLEXVE_ERR_VERIFY;
    found = len > 0;
  }
  if(!found) return FLEXVE_ERR_VERIFY;
  VeRd rd = { io, off, off + len };
  FlexJpegInfo inf; memset(&inf, 0, sizeof(inf));
  if(flexJpegProbeStream(veRd, &rd, &inf, af, ff) != FLEXJPG_OK || inf.progressive ||
     inf.width != expW || inf.height != expH) return FLEXVE_ERR_VERIFY;
  // Y el ultimo se alcanza por el indice.
  if(flexAviSeekFrame(ctx, expFrames - 1) != (int)(expFrames - 1)) return FLEXVE_ERR_VERIFY;
  if(flexAviSkipFrame(ctx) != FLEXAVI_OK) return FLEXVE_ERR_VERIFY;
  return FLEXVE_OK;
}
