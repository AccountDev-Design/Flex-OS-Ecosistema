// #############################################################
// ##  FlexOS · BIBLIOTECA DE MEDIOS · el almacen · implementacion
// ##  Ver FlexOS_MediaStore.h. Ningun camino deja un archivo del
// ##  usuario fuera de su sitio: lo que se mueve se mueve con el
// ##  cerrojo tomado, y lo que falla vuelve a donde estaba.
// #############################################################
#include "FlexOS_MediaStore.h"
#include "FlexOS_MediaThumb.h"
#include "FlexOS_Media.h"
#include "FlexOS_JPEGEnc.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void msLock(FlexMediaStore* ms){ if(ms->lock) ms->lock(ms->lockCtx); }
static void msUnlock(FlexMediaStore* ms){ if(ms->unlock) ms->unlock(ms->lockCtx); }
static uint32_t msNow(FlexMediaStore* ms){ return ms->now ? ms->now(ms->nowCtx) : 0; }
static void* msAlloc(FlexMediaStore* ms, size_t n){ return ms->alloc ? ms->alloc(n) : malloc(n); }
static void msFree(FlexMediaStore* ms, void* p){ if(!p) return; if(ms->free) ms->free(p); else free(p); }
static bool endsWith(const char* s, const char* suf){ size_t a = strlen(s), b = strlen(suf); return a >= b && !strcmp(s + a - b, suf); }
static const char* extOf(const char* path){
  const char* d = strrchr(path, '.'), *s = strrchr(path, '/');
  return (d && (!s || d > s)) ? d : "";
}
static bool tailNoCase(const char* s, const char* suf){
  size_t a = strlen(s), b = strlen(suf);
  if(!b || a < b) return false;
  for(size_t i = 0; i < b; i++){
    char x = s[a - b + i], y = suf[i];
    if(x >= 'A' && x <= 'Z') x = (char)(x + 32);
    if(y >= 'A' && y <= 'Z') y = (char)(y + 32);
    if(x != y) return false;
  }
  return true;
}
// Nombre para ensenar con la extension del archivo ("Vacaciones" ->
// "Vacaciones.jpg"). Si hay que recortar, nunca por la mitad de un caracter.
static void nameWithExt(const char* clean, const char* ext, char* out, size_t cap){
  size_t le = strlen(ext), lc = strlen(clean);
  if(tailNoCase(clean, ext) || le + 1 >= cap){ snprintf(out, cap, "%s", clean); return; }
  size_t room = cap - 1 - le;
  if(lc > room){ lc = room; while(lc && ((unsigned char)clean[lc] & 0xC0) == 0x80) lc--; }
  memcpy(out, clean, lc);
  memcpy(out + lc, ext, le + 1);
}
static bool msExists(void* ctx, const char* p){ FlexMediaStore* ms = (FlexMediaStore*)ctx; return ms->fs.exists(ms->fs.ctx, p); }
static void copyz(char* d, size_t cap, const char* s){ if(!cap) return; snprintf(d, cap, "%s", s ? s : ""); }

const char* flexMsDestDir(int kind){ return kind == FML_K_DRAW ? "/Paint" : flexMlDestDir(kind); }

void flexMsThumbPath(const FlexMlRec* r, char* out, size_t cap){
  if(r->flags & FML_R_LOCKED) snprintf(out, cap, FML_DIR_LOCKED "/%lu.t.jpg", (unsigned long)r->id);
  else                        snprintf(out, cap, FML_DIR_THUMB "/%lu.jpg", (unsigned long)r->id);
}

void flexMsInit(FlexMediaStore* ms, FlexMlRec* store, uint16_t cap){
  flexMlInit(&ms->lib, store, cap);
  ms->dirty = false; ms->scanning = false; ms->pending = 0;
}

void flexMsMakeDirs(FlexMediaStore* ms){
  static const char* const D[] = { "/System", FML_DIR_ROOT, FML_DIR_THUMB, FML_DIR_TMP, FML_DIR_LOCKED,
                                   FML_DIR_PHOTO, FML_DIR_VIDEO, FML_DIR_AUDIO };
  for(size_t i = 0; i < sizeof(D) / sizeof(D[0]); i++) if(!ms->fs.exists(ms->fs.ctx, D[i])) ms->fs.mkdir(ms->fs.ctx, D[i]);
}

// -------------------------------------------------------------
//  PERSISTENCIA
// -------------------------------------------------------------
static bool readWhole(FlexMediaStore* ms, const char* path, uint8_t* buf, uint32_t n){
  void* h = ms->fs.open(ms->fs.ctx, path, false);
  if(!h) return false;
  uint32_t got = 0;
  while(got < n){
    int r = ms->fs.read(ms->fs.ctx, h, buf + got, n - got);
    if(r <= 0) break;
    got += (uint32_t)r;
  }
  ms->fs.close(ms->fs.ctx, h);
  return got == n;
}

static bool loadFrom(FlexMediaStore* ms, const char* path){
  uint32_t sz = ms->fs.size(ms->fs.ctx, path);
  if(sz < FML_HDR_BYTES || sz > FML_HDR_BYTES + (uint32_t)ms->lib.cap * sizeof(FlexMlRec)) return false;
  uint8_t* buf = (uint8_t*)msAlloc(ms, sz);
  if(!buf) return false;
  bool ok = readWhole(ms, path, buf, sz) && flexMlDeserialize(&ms->lib, buf, sz) == FML_OK;
  msFree(ms, buf);
  return ok;
}

int flexMsLoad(FlexMediaStore* ms){
  msLock(ms);
  int from = 0;
  // Tres generaciones: la buena; la anterior que deja la escritura atomica
  // si se fue la luz en el cambio; y la nueva que no llego a renombrarse.
  if(loadFrom(ms, FML_LIB_PATH)) from = 1;
  else if(loadFrom(ms, FML_LIB_PATH ".bak")) from = 2;
  else if(loadFrom(ms, FML_LIB_PATH ".tmp")) from = 3;
  if(from > 1) ms->dirty = true;                  // se reescribe la buena
  msUnlock(ms);
  return from;
}

bool flexMsSave(FlexMediaStore* ms){
  // Se copia con el cerrojo y se escribe SIN el: nadie espera a la flash.
  msLock(ms);
  size_t n = flexMlSerializedSize(&ms->lib);
  uint8_t* buf = (uint8_t*)msAlloc(ms, n);
  size_t w = buf ? flexMlSerialize(&ms->lib, buf, n) : 0;
  ms->dirty = false;
  msUnlock(ms);
  bool ok = buf && w == n && ms->fs.writeAtomic(ms->fs.ctx, FML_LIB_PATH, buf, n);
  msFree(ms, buf);
  if(!ok){ msLock(ms); ms->dirty = true; msUnlock(ms); }
  return ok;
}

void flexMsCleanTmp(FlexMediaStore* ms){
  FlexMsEntry e[8];
  for(int guard = 0; guard < 64; guard++){
    int n = ms->fs.list(ms->fs.ctx, FML_DIR_TMP, e, 8, 0);
    if(n <= 0) break;
    int del = 0;
    for(int i = 0; i < n; i++){
      char p[FML_PATH_MAX];
      if(snprintf(p, sizeof(p), FML_DIR_TMP "/%s", e[i].name) >= (int)sizeof(p)) continue;
      if(ms->fs.remove(ms->fs.ctx, p)) del++;
    }
    if(!del) break;
  }
}

// -------------------------------------------------------------
//  RECONCILIACION
// -------------------------------------------------------------
// Donde se aparta el original mientras se sustituye (junto a el, para que
// un corte de luz no lo deje en la carpeta de temporales, que se vacia).
#define MS_ORIG_SUFFIX ".fxorig"

static const char* const MS_ROOTS[] = {
  FML_DIR_PHOTO, FML_DIR_VIDEO, FML_DIR_AUDIO, "/Documentos", "/Paint", "/Camara", "/Descargas"
};

static int kindFromName(const char* name){
  if(endsWith(name, ".fxp") || endsWith(name, ".FXP")) return FML_K_DRAW;
  return flexMlKindFromExt(name);
}

static bool scanDir(FlexMediaStore* ms, const char* dir, bool locked, int depth, uint32_t now,
                    void (*yieldFn)(void*), void* yctx){
  FlexMsEntry e[8];
  for(int skip = 0; ; ){
    int n = ms->fs.list(ms->fs.ctx, dir, e, 8, skip);
    if(n < 0) return !ms->fs.exists(ms->fs.ctx, dir);  // no existe: vacia; existe e ilegible: error
    if(n == 0) return true;
    skip += n;
    for(int i = 0; i < n; i++){
      char p[FML_PATH_MAX];
      if(snprintf(p, sizeof(p), "%s/%s", dir, e[i].name) >= (int)sizeof(p)) continue;
      if(e[i].dir){
        if(!locked && depth < 2) scanDir(ms, p, false, depth + 1, now, yieldFn, yctx);
        continue;
      }
      if(locked && endsWith(e[i].name, ".t.jpg")) continue;    // miniaturas de los protegidos
      uint32_t size = e[i].size;
      if(endsWith(e[i].name, MS_ORIG_SUFFIX)){
        // Resto de un "reemplazar original" cortado (ver flexMsReplace): si
        // la version nueva llego a su sitio, el apartado sobra; si no, el
        // original vuelve. Nunca se pierde la foto del usuario. Solo con
        // nombre de medio delante: un archivo ajeno que acabe igual no se toca.
        char orig[FML_PATH_MAX];
        copyz(orig, sizeof(orig), p);
        p[strlen(p) - (sizeof(MS_ORIG_SUFFIX) - 1)] = 0; // p = la ruta del original
        const char* bn = strrchr(p, '/');
        if(kindFromName(bn ? bn + 1 : p) == FML_K_NONE) continue;
        bool back = false;
        msLock(ms);
        if(ms->fs.exists(ms->fs.ctx, orig)){            // un reemplazo en curso ya lo resolvio
          if(ms->fs.exists(ms->fs.ctx, p)) ms->fs.remove(ms->fs.ctx, orig);
          else back = ms->fs.move(ms->fs.ctx, orig, p);
        }
        msUnlock(ms);
        if(!back) continue;
        // Recuperado: se registra ya (no estaba en el listado).
      }
      const char* nm = strrchr(p, '/');
      int kind = kindFromName(nm ? nm + 1 : p);
      if(kind == FML_K_NONE) continue;
      msLock(ms);
      // Uno que no estaba se confirma con el cerrojo tomado: la interfaz
      // pudo borrarlo o moverlo entre el listado y ahora.
      if(flexMlFindPath(&ms->lib, p) >= 0 || ms->fs.exists(ms->fs.ctx, p)){
        uint32_t rev0 = ms->lib.rev;
        flexMlScanMerge(&ms->lib, p, size, kind, locked, now);
        if(ms->lib.rev != rev0) ms->dirty = true;
      }
      msUnlock(ms);
    }
    if(yieldFn) yieldFn(yctx);
  }
}

bool flexMsScan(FlexMediaStore* ms, void (*yieldFn)(void*), void* yctx){
  ms->scanning = true;
  uint32_t now = msNow(ms);
  msLock(ms);
  flexMlScanBegin(&ms->lib);
  msUnlock(ms);
  bool ok = true;
  for(size_t r = 0; r < sizeof(MS_ROOTS) / sizeof(MS_ROOTS[0]); r++) ok = scanDir(ms, MS_ROOTS[r], false, 0, now, yieldFn, yctx) && ok;
  ok = scanDir(ms, FML_DIR_LOCKED, true, 0, now, yieldFn, yctx) && ok;
  // Lo no visto que ya no esta se retira (con su miniatura). Solo si TODO se
  // leyo bien: una lectura fallida no puede borrar la biblioteca.
  if(ok){
    msLock(ms);
    int removed = 0;
    for(int i = flexMlNextUnseen(&ms->lib, 0); i >= 0; ){
      FlexMlRec* r = &ms->lib.recs[i];
      if(ms->fs.exists(ms->fs.ctx, r->path)){
        r->flags |= FML_R_SEEN;
        i = flexMlNextUnseen(&ms->lib, i + 1);
        continue;
      }
      char tp[FML_PATH_MAX];
      flexMsThumbPath(r, tp, sizeof(tp));
      ms->fs.remove(ms->fs.ctx, tp);
      flexMlRemoveAt(&ms->lib, i);
      removed++;
      i = flexMlNextUnseen(&ms->lib, i);
    }
    if(removed) ms->dirty = true;
    msUnlock(ms);
  }
  ms->scanning = false;
  return ok;
}

// -------------------------------------------------------------
//  MINIATURAS Y METADATOS
// -------------------------------------------------------------
struct MsIo { FlexMediaStore* ms; void* h; uint32_t pos, size; };
static int msioRead(void* c, void* buf, uint32_t n){
  MsIo* f = (MsIo*)c;
  if(!f->ms->fs.seek(f->ms->fs.ctx, f->h, f->pos)) return -1;
  int r = f->ms->fs.read(f->ms->fs.ctx, f->h, (uint8_t*)buf, n);
  if(r > 0) f->pos += (uint32_t)r;
  return r;
}
static bool msioSeek(void* c, uint32_t off){ MsIo* f = (MsIo*)c; if(off > f->size) return false; f->pos = off; return true; }
static uint32_t msioSize(void* c){ return ((MsIo*)c)->size; }
struct MsOut { FlexMediaStore* ms; void* h; bool ok; };
static bool msOutWrite(void* c, const uint8_t* d, size_t n){
  MsOut* o = (MsOut*)c;
  if(o->ok) o->ok = o->ms->fs.write(o->ms->fs.ctx, o->h, d, n);
  return o->ok;
}

bool flexMsNextJob(FlexMediaStore* ms, uint32_t afterId, FlexMsJob* job){
  bool found = false;
  uint16_t pend = 0;
  msLock(ms);
  for(int i = 0; i < ms->lib.n; i++){
    const FlexMlRec* r = &ms->lib.recs[i];
    if(!(r->flags & FML_R_NEED_THUMB) || (r->flags & FML_R_LOCKED)) continue;
    pend++;
    if(found || r->id <= afterId) continue;
    job->id = r->id; job->size = r->size; job->kind = r->kind;
    copyz(job->path, sizeof(job->path), r->path);
    found = true;
  }
  ms->pending = pend;
  msUnlock(ms);
  return found;
}

struct MsProbe { int fmt, rc; uint16_t w, h; uint32_t dur; bool playable, thumb, retry; };

static MsProbe probeFile(FlexMediaStore* ms, const FlexMsJob* job, const char* tmp, uint32_t memFree, uint32_t memReserve){
  MsProbe pr; memset(&pr, 0, sizeof(pr));
  uint8_t head[512];
  int hn = -1;
  void* h = ms->fs.open(ms->fs.ctx, job->path, false);
  if(h){ hn = ms->fs.read(ms->fs.ctx, h, head, sizeof(head)); ms->fs.close(ms->fs.ctx, h); }
  if(hn <= 0){ pr.rc = FLEXTH_ERR_IO; pr.fmt = FML_F_UNKNOWN; return pr; }
  int sk = FML_K_NONE;
  pr.fmt = flexMlSniff(head, (size_t)hn, &sk);
  pr.playable = flexMlFmtPlayable(pr.fmt);
  int ow = 0, oh = 0;
  if(pr.fmt == FML_F_JPEG || pr.fmt == FML_F_JPEG_PROG){
    // Una foto se decodifica ENTERA: es lo que la valida. Solo si sobra
    // memoria; si no, se deja para luego en vez de apretar al sistema.
    if(job->size > FML_LIMIT_PHOTO){ pr.rc = FLEXTH_ERR_ARG; pr.playable = false; return pr; }
    if(memFree < job->size || memFree - job->size < memReserve){ pr.retry = true; return pr; }
    uint8_t* buf = (uint8_t*)msAlloc(ms, job->size);
    if(!buf){ pr.retry = true; return pr; }
    MsOut o = { ms, NULL, true };
    if(readWhole(ms, job->path, buf, job->size) && (o.h = ms->fs.open(ms->fs.ctx, tmp, true)) != NULL){
      pr.rc = flexThumbFromJpeg(buf, job->size, FLEXTH_SIDE, FLEXTH_QUALITY, msOutWrite, &o, &ow, &oh, ms->alloc, ms->free);
      ms->fs.close(ms->fs.ctx, o.h);
      if(pr.rc == FLEXTH_OK && !o.ok) pr.rc = FLEXTH_ERR_WRITE;
    } else pr.rc = FLEXTH_ERR_IO;
    msFree(ms, buf);
    if(pr.rc == FLEXTH_ERR_UNSUP){ pr.fmt = FML_F_JPEG_PROG; pr.playable = false; }
    if(pr.rc == FLEXTH_ERR_MEMORY) pr.retry = true;
  } else if(pr.fmt == FML_F_AVI_MJPEG || pr.fmt == FML_F_AVI_OTHER){
    MsIo f = { ms, ms->fs.open(ms->fs.ctx, job->path, false), 0, job->size };
    MsOut o = { ms, f.h ? ms->fs.open(ms->fs.ctx, tmp, true) : NULL, true };
    if(f.h && o.h){
      FlexMediaIO io; io.read = msioRead; io.seek = msioSeek; io.size = msioSize; io.ctx = &f;
      pr.rc = flexThumbFromAvi(&io, FLEXTH_SIDE, FLEXTH_QUALITY, msOutWrite, &o, &ow, &oh, &pr.dur, ms->alloc, ms->free);
      if(pr.rc == FLEXTH_OK && !o.ok) pr.rc = FLEXTH_ERR_WRITE;
    } else pr.rc = FLEXTH_ERR_IO;
    if(o.h) ms->fs.close(ms->fs.ctx, o.h);
    if(f.h) ms->fs.close(ms->fs.ctx, f.h);
    if(pr.rc == FLEXTH_ERR_UNSUP){ pr.fmt = FML_F_AVI_OTHER; pr.playable = false; }
    if(pr.rc == FLEXTH_ERR_MEMORY) pr.retry = true;
  } else if(pr.fmt == FML_F_WAV_PCM || pr.fmt == FML_F_WAV_ADPCM || pr.fmt == FML_F_WAV_OTHER){
    MsIo f = { ms, ms->fs.open(ms->fs.ctx, job->path, false), 0, job->size };
    int wr = FLEXWAV_ERR_IO;
    FlexWavInfo wi;
    if(f.h){
      FlexMediaIO io; io.read = msioRead; io.seek = msioSeek; io.size = msioSize; io.ctx = &f;
      wr = flexWavParse(&io, &wi);
      ms->fs.close(ms->fs.ctx, f.h);
    }
    if(wr == FLEXWAV_OK) pr.dur = flexWavDurationMs(&wi);
    else if(wr == FLEXWAV_ERR_CODEC){ pr.playable = false; pr.fmt = FML_F_WAV_OTHER; }
    else { pr.playable = false; pr.rc = FLEXTH_ERR_DECODE; return pr; }
    pr.rc = FLEXTH_ERR_UNSUP;                        // el audio no tiene imagen propia
  } else if(pr.fmt == FML_F_FXP && job->kind == FML_K_DRAW){
    pr.rc = FLEXTH_ERR_UNSUP;
    pr.playable = true;
    if(ms->paintThumb){
      uint16_t* px = (uint16_t*)msAlloc(ms, (size_t)FLEXTH_SIDE * FLEXTH_SIDE * 2);
      if(!px){ pr.retry = true; return pr; }
      if(ms->paintThumb(ms->paintCtx, job->path, px, &ow, &oh)){
        MsOut o = { ms, ms->fs.open(ms->fs.ctx, tmp, true), true };
        pr.rc = o.h ? flexThumbFromPixels(px, FLEXTH_SIDE, FLEXTH_SIDE, FLEXTH_SIDE * 2, FLEXJE_IN_RGB565,
                                          FLEXTH_SIDE, FLEXTH_QUALITY, msOutWrite, &o, ms->alloc, ms->free)
                    : FLEXTH_ERR_WRITE;
        if(o.h) ms->fs.close(ms->fs.ctx, o.h);
        if(pr.rc == FLEXTH_OK && !o.ok) pr.rc = FLEXTH_ERR_WRITE;
      } else {
        pr.rc = FLEXTH_ERR_DECODE;                 // Paint tampoco podria abrirlo
        pr.playable = false;
      }
      msFree(ms, px);
    }
  } else {
    pr.rc = FLEXTH_ERR_UNSUP;                        // PNG, MP4, MP3...: se conserva, no se abre aqui
  }
  if(ow > 0 && oh > 0 && ow < 65536 && oh < 65536){ pr.w = (uint16_t)ow; pr.h = (uint16_t)oh; }
  pr.thumb = pr.rc == FLEXTH_OK;
  return pr;
}

int flexMsRunJob(FlexMediaStore* ms, const FlexMsJob* job, uint32_t memFree, uint32_t memReserve){
  char tmp[FML_PATH_MAX];
  snprintf(tmp, sizeof(tmp), FML_DIR_TMP "/th-%lu.jpg", (unsigned long)job->id);
  ms->fs.remove(ms->fs.ctx, tmp);
  MsProbe pr = probeFile(ms, job, tmp, memFree, memReserve);
  if(pr.retry){ ms->fs.remove(ms->fs.ctx, tmp); return FLEXMS_JOB_RETRY; }
  msLock(ms);
  int i = flexMlFindId(&ms->lib, job->id);
  // Si mientras tanto se borro, se bloqueo o se cambio el archivo, el
  // resultado ya no es de este registro. Y si el archivo ya no esta, no es
  // que este danado: la reconciliacion retirara el registro.
  if(i < 0 || strcmp(ms->lib.recs[i].path, job->path) || ms->lib.recs[i].size != job->size ||
     (ms->lib.recs[i].flags & FML_R_LOCKED) || !(ms->lib.recs[i].flags & FML_R_NEED_THUMB) ||
     (pr.rc == FLEXTH_ERR_IO && !ms->fs.exists(ms->fs.ctx, job->path))){
    msUnlock(ms);
    ms->fs.remove(ms->fs.ctx, tmp);
    return FLEXMS_JOB_GONE;
  }
  FlexMlRec* r = &ms->lib.recs[i];
  if(pr.fmt != FML_F_UNKNOWN) r->fmt = (uint8_t)pr.fmt;
  if(pr.w){ r->w = pr.w; r->h = pr.h; }
  if(pr.dur) r->durMs = pr.dur;
  r->flags &= (uint16_t)~(FML_R_NEED_THUMB | FML_R_PLAYABLE);
  if(pr.playable) r->flags |= FML_R_PLAYABLE;
  r->err = pr.playable ? FML_E_NONE : FML_E_UNSUPPORTED;
  if(pr.rc == FLEXTH_ERR_DECODE || pr.rc == FLEXTH_ERR_IO){
    r->state = FML_S_ERROR; r->err = FML_E_CORRUPT; r->flags &= (uint16_t)~FML_R_PLAYABLE;
  } else r->state = FML_S_READY;
  if(pr.thumb){
    char tp[FML_PATH_MAX];
    flexMsThumbPath(r, tp, sizeof(tp));
    ms->fs.remove(ms->fs.ctx, tp);                  // una miniatura vieja no se queda con la ruta
    if(ms->fs.move(ms->fs.ctx, tmp, tp)){ r->flags |= FML_R_THUMB; r->flags &= (uint16_t)~FML_R_THUMB_FAIL; }
    else r->flags |= FML_R_THUMB_FAIL;
  } else if(!(r->flags & FML_R_EXT_THUMB)){
    r->flags |= FML_R_THUMB_FAIL;
  }
  r->thumbVer++;
  ms->lib.rev++;
  ms->dirty = true;
  msUnlock(ms);
  ms->fs.remove(ms->fs.ctx, tmp);
  return FLEXMS_JOB_DONE;
}

// -------------------------------------------------------------
//  CONSULTAS
// -------------------------------------------------------------
bool flexMsGet(FlexMediaStore* ms, uint32_t id, FlexMlRec* out){
  msLock(ms);
  int i = flexMlFindId(&ms->lib, id);
  if(i >= 0 && out) *out = ms->lib.recs[i];
  msUnlock(ms);
  return i >= 0;
}
uint32_t flexMsRev(FlexMediaStore* ms){ msLock(ms); uint32_t r = ms->lib.rev; msUnlock(ms); return r; }
int flexMsSnapshot(FlexMediaStore* ms, FlexMlRec* dst, int cap, uint32_t* rev){
  msLock(ms);
  int n = ms->lib.n < cap ? ms->lib.n : cap;
  if(n > 0) memcpy(dst, ms->lib.recs, sizeof(FlexMlRec) * (size_t)n);
  if(rev) *rev = ms->lib.rev;
  msUnlock(ms);
  return n;
}
uint32_t flexMsFindDup(FlexMediaStore* ms, uint32_t size, uint32_t crc){
  msLock(ms);
  int i = flexMlFindDup(&ms->lib, size, crc);
  uint32_t id = i >= 0 ? ms->lib.recs[i].id : 0;
  msUnlock(ms);
  return id;
}

// -------------------------------------------------------------
//  CAMBIOS
// -------------------------------------------------------------
bool flexMsDelete(FlexMediaStore* ms, uint32_t id){
  msLock(ms);
  int i = flexMlFindId(&ms->lib, id);
  bool ok = false;
  if(i >= 0){
    FlexMlRec* r = &ms->lib.recs[i];
    char tp[FML_PATH_MAX];
    flexMsThumbPath(r, tp, sizeof(tp));
    if(!ms->fs.exists(ms->fs.ctx, r->path) || ms->fs.remove(ms->fs.ctx, r->path)){
      ms->fs.remove(ms->fs.ctx, tp);
      flexMlRemoveAt(&ms->lib, i);
      ms->dirty = true;
      ok = true;
    }
  }
  msUnlock(ms);
  return ok;
}

bool flexMsTrash(FlexMediaStore* ms, uint32_t id){
  if(!ms->fs.trash) return false;
  msLock(ms);
  int i = flexMlFindId(&ms->lib, id);
  bool ok = false;
  // La papelera es publica: un protegido NUNCA va ahi (se veria su nombre
  // y su contenido desde el Explorador).
  if(i >= 0 && !(ms->lib.recs[i].flags & FML_R_LOCKED) && ms->fs.trash(ms->fs.ctx, ms->lib.recs[i].path)){
    char tp[FML_PATH_MAX];
    flexMsThumbPath(&ms->lib.recs[i], tp, sizeof(tp));
    ms->fs.remove(ms->fs.ctx, tp);
    flexMlRemoveAt(&ms->lib, i);
    ms->dirty = true;
    ok = true;
  }
  msUnlock(ms);
  return ok;
}

bool flexMsSetLock(FlexMediaStore* ms, uint32_t id, bool lock, char* why, size_t whyCap){
  if(why && whyCap) why[0] = 0;
  msLock(ms);
  int i = flexMlFindId(&ms->lib, id);
  if(i < 0){ msUnlock(ms); if(why) snprintf(why, whyCap, "Ya no existe"); return false; }
  FlexMlRec* r = &ms->lib.recs[i];
  if(((r->flags & FML_R_LOCKED) != 0) == lock){ msUnlock(ms); return true; }
  char dst[FML_PATH_MAX], oldTp[FML_PATH_MAX], newTp[FML_PATH_MAX];
  const char* ext = extOf(r->path);
  if(lock){
    // En la carpeta protegida el archivo se llama por su id: ni el nombre
    // se ve desde fuera.
    snprintf(dst, sizeof(dst), FML_DIR_LOCKED "/%lu%s", (unsigned long)r->id, ext);
  } else {
    char stem[FML_STEM_MAX + 1];
    flexMlSafeStem(r->name[0] ? r->name : r->path, stem, sizeof(stem));
    if(!flexMlUniquePath(flexMsDestDir(r->kind), stem, ext, msExists, ms, dst, sizeof(dst))){
      msUnlock(ms); if(why) snprintf(why, whyCap, "No hay un nombre libre en su carpeta"); return false;
    }
  }
  if(!ms->fs.move(ms->fs.ctx, r->path, dst)){
    msUnlock(ms); if(why) snprintf(why, whyCap, "No se pudo mover el archivo"); return false;
  }
  flexMsThumbPath(r, oldTp, sizeof(oldTp));
  flexMlSetPath(&ms->lib, i, dst);
  flexMlSetLocked(&ms->lib, i, lock);
  flexMsThumbPath(&ms->lib.recs[i], newTp, sizeof(newTp));
  ms->fs.remove(ms->fs.ctx, newTp);               // nada ajeno ocupa su sitio
  if(ms->fs.exists(ms->fs.ctx, oldTp) && !ms->fs.move(ms->fs.ctx, oldTp, newTp)){
    // La miniatura no pudo acompanarle: se BORRA (nunca se queda a la
    // vista la de un protegido) y se rehara cuando haga falta.
    ms->fs.remove(ms->fs.ctx, oldTp);
  }
  r = &ms->lib.recs[i];
  if(!ms->fs.exists(ms->fs.ctx, newTp) && (r->flags & FML_R_THUMB)){
    // Sin miniatura en su sitio: al volver a la vista se hace de nuevo (y
    // si el P4 no sabe hacerla, la tarea lo anotara como fallo).
    r->flags &= (uint16_t)~(FML_R_THUMB | FML_R_EXT_THUMB);
    r->flags |= FML_R_NEED_THUMB;
  }
  ms->dirty = true;
  msUnlock(ms);
  return true;
}

bool flexMsRename(FlexMediaStore* ms, uint32_t id, const char* newName, char* why, size_t whyCap){
  if(why && whyCap) why[0] = 0;
  char clean[FML_NAME_MAX];
  flexMlCleanName(newName ? newName : "", clean, sizeof(clean));
  // Sin espacios sobrantes a los lados.
  size_t L = strlen(clean);
  while(L && clean[L - 1] == ' ') clean[--L] = 0;
  if(!clean[0]){ if(why) snprintf(why, whyCap, "El nombre no puede estar vac\xC3\xAD" "o"); return false; }
  msLock(ms);
  int i = flexMlFindId(&ms->lib, id);
  if(i < 0){ msUnlock(ms); if(why) snprintf(why, whyCap, "Ya no existe"); return false; }
  FlexMlRec* r = &ms->lib.recs[i];
  char ext[16];                                   // COPIA: la ruta cambia aqui abajo
  copyz(ext, sizeof(ext), extOf(r->path));
  if(!(r->flags & FML_R_LOCKED)){
    char stem[FML_STEM_MAX + 1], dst[FML_PATH_MAX] = "", dir[FML_PATH_MAX], same[FML_PATH_MAX];
    flexMlSafeStem(clean, stem, sizeof(stem));
    copyz(dir, sizeof(dir), r->path);
    char* sl = strrchr(dir, '/');
    if(sl) *sl = 0;
    if(snprintf(same, sizeof(same), "%s/%s%s", dir, stem, ext) >= (int)sizeof(same)) same[0] = 0;
    if(!strcmp(same, r->path)) copyz(dst, sizeof(dst), same);
    else if(!flexMlUniquePath(dir[0] ? dir : "/", stem, ext, msExists, ms, dst, sizeof(dst))) dst[0] = 0;
    if(!dst[0] || (strcmp(dst, r->path) && !ms->fs.move(ms->fs.ctx, r->path, dst))){
      msUnlock(ms); if(why) snprintf(why, whyCap, "No se pudo renombrar el archivo"); return false;
    }
    flexMlSetPath(&ms->lib, i, dst);
  }
  // Un protegido solo cambia el nombre que se ensena: su archivo sigue
  // llamandose por su id.
  nameWithExt(clean, ext, r->name, sizeof(r->name));
  if(r->kind == FML_K_AUDIO && r->title[0]) flexMlCleanName(clean, r->title, sizeof(r->title));
  ms->lib.rev++;
  ms->dirty = true;
  msUnlock(ms);
  return true;
}

uint32_t flexMsAddFile(FlexMediaStore* ms, const char* tmp, int kind, const char* shownName,
                       uint8_t origin, char* why, size_t whyCap){
  if(why && whyCap) why[0] = 0;
  char stem[FML_STEM_MAX + 1], dst[FML_PATH_MAX];
  flexMlSafeStem(shownName, stem, sizeof(stem));
  msLock(ms);
  if(!flexMlUniquePath(flexMsDestDir(kind), stem, extOf(tmp), msExists, ms, dst, sizeof(dst)) ||
     !ms->fs.move(ms->fs.ctx, tmp, dst)){
    msUnlock(ms); if(why) snprintf(why, whyCap, "No se pudo guardar en su carpeta"); return 0;
  }
  FlexMlRec p; memset(&p, 0, sizeof(p));
  copyz(p.path, sizeof(p.path), dst);
  flexMlCleanName(shownName, p.name, sizeof(p.name));
  uint32_t now = msNow(ms);
  p.kind = (uint8_t)kind; p.size = ms->fs.size(ms->fs.ctx, dst); p.origin = origin;
  p.created = now; p.state = FML_S_READY; p.flags = FML_R_NEED_THUMB;
  int at = flexMlAdd(&ms->lib, &p, now);
  uint32_t id = at >= 0 ? ms->lib.recs[at].id : 0;
  if(!id){ ms->fs.move(ms->fs.ctx, dst, tmp); if(why) snprintf(why, whyCap, "La biblioteca est\xC3\xA1 llena"); }
  else ms->dirty = true;
  msUnlock(ms);
  return id;
}

bool flexMsReplace(FlexMediaStore* ms, uint32_t id, const char* tmp, char* why, size_t whyCap){
  if(why && whyCap) why[0] = 0;
  msLock(ms);
  int i = flexMlFindId(&ms->lib, id);
  if(i < 0){ msUnlock(ms); if(why) snprintf(why, whyCap, "El original ya no existe"); return false; }
  FlexMlRec* r = &ms->lib.recs[i];
  char path[FML_PATH_MAX], old[FML_PATH_MAX];
  copyz(path, sizeof(path), r->path);
  if(snprintf(old, sizeof(old), "%s" MS_ORIG_SUFFIX, path) >= (int)sizeof(old)){
    msUnlock(ms); if(why) snprintf(why, whyCap, "Ruta demasiado larga"); return false;
  }
  // Orden seguro: el original se aparta JUNTO A SI MISMO (fuera de la
  // carpeta de temporales, que se vacia al arrancar), el nuevo ocupa su
  // sitio y solo entonces se borra el apartado. Un corte de luz a mitad lo
  // resuelve la reconciliacion (MS_ORIG_SUFFIX).
  ms->fs.remove(ms->fs.ctx, old);
  if(!ms->fs.move(ms->fs.ctx, path, old)){ msUnlock(ms); if(why) snprintf(why, whyCap, "No se pudo apartar el original"); return false; }
  if(!ms->fs.move(ms->fs.ctx, tmp, path)){
    ms->fs.move(ms->fs.ctx, old, path);           // el original vuelve: nada se pierde
    msUnlock(ms); if(why) snprintf(why, whyCap, "No se pudo guardar la versi\xC3\xB3n editada"); return false;
  }
  ms->fs.remove(ms->fs.ctx, old);
  char tp[FML_PATH_MAX];
  flexMsThumbPath(r, tp, sizeof(tp));
  ms->fs.remove(ms->fs.ctx, tp);
  r->size = ms->fs.size(ms->fs.ctx, path);
  r->crc = 0;
  r->modified = msNow(ms);
  r->origin = FML_O_EDIT;
  r->flags |= FML_R_EDITED | FML_R_NEED_THUMB;
  r->flags &= (uint16_t)~(FML_R_THUMB | FML_R_THUMB_FAIL | FML_R_EXT_THUMB);
  r->thumbVer++;
  ms->lib.rev++;
  ms->dirty = true;
  msUnlock(ms);
  return true;
}

// -------------------------------------------------------------
//  LO QUE SUBE EL MOVIL
// -------------------------------------------------------------
uint32_t flexMsCommitUpload(FlexMediaStore* ms, const struct FlexMsUpload* up, char* why, size_t whyCap){
  if(why && whyCap) why[0] = 0;
  char stem[FML_STEM_MAX + 1], dst[FML_PATH_MAX];
  flexMlSafeStem(up->name, stem, sizeof(stem));
  const char* ext = flexMlFmtExt(up->fmt);
  msLock(ms);
  if(!flexMlUniquePath(flexMsDestDir(up->kind), stem, ext, msExists, ms, dst, sizeof(dst))){
    msUnlock(ms); if(why) snprintf(why, whyCap, "No hay un nombre libre en su carpeta"); return 0;
  }
  if(!ms->fs.move(ms->fs.ctx, up->tmpPath, dst)){
    msUnlock(ms); if(why) snprintf(why, whyCap, "No se pudo mover el archivo a su carpeta"); return 0;
  }
  FlexMlRec r; memset(&r, 0, sizeof(r));
  copyz(r.path, sizeof(r.path), dst);
  copyz(r.name, sizeof(r.name), up->name);
  copyz(r.title, sizeof(r.title), up->title);
  copyz(r.artist, sizeof(r.artist), up->artist);
  copyz(r.album, sizeof(r.album), up->album);
  r.kind = (uint8_t)up->kind; r.fmt = (uint8_t)up->fmt; r.size = up->size; r.crc = up->crc;
  r.created = up->created; r.w = up->w; r.h = up->h; r.durMs = up->durMs;
  r.origin = FML_O_WEB; r.state = FML_S_READY;
  bool hasThumb = up->thumbTmp && up->thumbTmp[0];
  r.flags = (uint16_t)((up->playable ? FML_R_PLAYABLE : 0) | (hasThumb ? FML_R_THUMB : FML_R_THUMB_FAIL));
  if(!up->playable) r.err = FML_E_UNSUPPORTED;
  int at = flexMlAdd(&ms->lib, &r, msNow(ms));
  if(at < 0){
    ms->fs.move(ms->fs.ctx, dst, up->tmpPath);     // al temporal: el servidor lo borra
    msUnlock(ms); if(why) snprintf(why, whyCap, "La biblioteca est\xC3\xA1 llena"); return 0;
  }
  uint32_t id = ms->lib.recs[at].id;
  if(hasThumb){
    char tp[FML_PATH_MAX];
    flexMsThumbPath(&ms->lib.recs[at], tp, sizeof(tp));
    ms->fs.remove(ms->fs.ctx, tp);
    if(!ms->fs.move(ms->fs.ctx, up->thumbTmp, tp)){
      ms->lib.recs[at].flags &= (uint16_t)~FML_R_THUMB;
      ms->lib.recs[at].flags |= FML_R_NEED_THUMB;
    }
  }
  ms->dirty = true;
  msUnlock(ms);
  return id;
}

bool flexMsSetExtThumb(FlexMediaStore* ms, uint32_t id, const char* tmp){
  msLock(ms);
  int i = flexMlFindId(&ms->lib, id);
  bool ok = false;
  if(i >= 0 && !(ms->lib.recs[i].flags & FML_R_LOCKED)){
    char tp[FML_PATH_MAX];
    flexMsThumbPath(&ms->lib.recs[i], tp, sizeof(tp));
    ms->fs.remove(ms->fs.ctx, tp);
    if(ms->fs.move(ms->fs.ctx, tmp, tp)){
      FlexMlRec* r = &ms->lib.recs[i];
      r->flags |= FML_R_THUMB | FML_R_EXT_THUMB;
      r->flags &= (uint16_t)~(FML_R_THUMB_FAIL | FML_R_NEED_THUMB);
      r->thumbVer++;
      ms->lib.rev++;
      ms->dirty = true;
      ok = true;
    }
  }
  msUnlock(ms);
  return ok;
}
