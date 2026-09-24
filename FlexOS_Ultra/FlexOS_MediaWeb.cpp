// #############################################################
// ##  FlexOS · FLEX WEB SERVER  ·  implementacion
// ##  Ver FlexOS_MediaWeb.h. Nada de lo que llega de la red se copia
// ##  sin un limite, ninguna respuesta de exito sale antes de que el
// ##  trabajo este hecho de verdad, y ningun camino de error deja un
// ##  temporal en el disco.
// #############################################################
#include "FlexOS_MediaWeb.h"
#include "FlexOS_MediaThumb.h"
#include "FlexOS_Media.h"
#include "FlexOS_WebUI.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Lo que ve la web: fotos, videos y audio. Un dibujo de Paint (.fxp) es de la
// Galeria del P4 y fuera de el no lo abre nada, asi que para la web no existe.
#define FLEXWEB_MASK (FML_MASK(FML_K_PHOTO) | FML_MASK(FML_K_VIDEO) | FML_MASK(FML_K_AUDIO))

// -------------------------------------------------------------
//  Utilidades
// -------------------------------------------------------------
static uint32_t nowMs(FlexWebCtx* w){ return w->host.nowMs ? w->host.nowMs(w->host.ctx) : 0; }
static void yieldHost(FlexWebCtx* w){ if(w->host.yield) w->host.yield(w->host.ctx); }
static bool othersWaiting(FlexWebCtx* w){ return w->host.othersWaiting && w->host.othersWaiting(w->host.ctx); }
static void* wAlloc(FlexWebCtx* w, size_t n){ return w->alloc ? w->alloc(n) : malloc(n); }
static void wFree(FlexWebCtx* w, void* p){ if(!p) return; if(w->free) w->free(p); else free(p); }
static void wipe(void* p, size_t n){ volatile uint8_t* v = (volatile uint8_t*)p; while(n--) *v++ = 0; }
static bool eqCT(const char* a, const char* b, size_t n){
  uint8_t d = 0;
  for(size_t i = 0; i < n; i++) d |= (uint8_t)(a[i] ^ b[i]);
  return d == 0;
}
static void copyz(char* d, size_t cap, const char* s){
  if(!d || !cap) return;
  size_t i = 0;
  if(s) for(; s[i] && i + 1 < cap; i++) d[i] = s[i];
  d[i] = 0;
}

static void emit(FlexWebCtx* w, int ev, int kind, uint32_t done, uint32_t total, uint32_t id,
                 const char* name, const char* msg){
  if(!w->host.event) return;
  FlexWebXfer x;
  memset(&x, 0, sizeof(x));
  x.ev = ev; x.kind = kind; x.done = done; x.total = total; x.id = id;
  copyz(x.name, sizeof(x.name), name);
  copyz(x.msg, sizeof(x.msg), msg);
  w->host.event(w->host.ctx, &x);
}

// -------------------------------------------------------------
//  Envio
// -------------------------------------------------------------
static bool sendAll(const FlexWebConn* c, const void* p, size_t n){
  return n == 0 || c->write(c->ctx, (const uint8_t*)p, n);
}
static bool sendHead(const FlexWebConn* c, int status, const char* mime, long long len,
                     const char* extra, bool keep){
  char h[1280];
  size_t n = flexHttpHeaderEx(h, sizeof(h), status, mime, len, extra, keep ? 1 : 0);
  return n && sendAll(c, h, n);
}
static bool sendBody(const FlexWebConn* c, int status, const char* mime, const void* body, size_t len,
                     const char* extra, bool keep, bool headOnly){
  if(!sendHead(c, status, mime, (long long)len, extra, keep)) return false;
  return headOnly || sendAll(c, body, len);
}
static bool sendJson(const FlexWebConn* c, int status, const char* json, bool keep, const char* extra = NULL){
  return sendBody(c, status, "application/json; charset=utf-8", json, strlen(json), extra, keep, false);
}
// {"error":"<msg>"} (+ campos extra ya formados, sin llaves: ,"left":3)
static bool sendErr(const FlexWebConn* c, int status, const char* msg, bool keep,
                    const char* more = NULL, const char* extra = NULL){
  char esc[240];
  if(!flexMlJsonStr(msg, esc, sizeof(esc))) copyz(esc, sizeof(esc), "\"Error\"");
  char b[400];
  snprintf(b, sizeof(b), "{\"error\":%s%s}", esc, more ? more : "");
  return sendJson(c, status, b, keep, extra);
}

// -------------------------------------------------------------
//  Sesiones y codigo de acceso
// -------------------------------------------------------------
static void hexOf(const uint8_t* b, size_t n, char* out){
  static const char H[] = "0123456789abcdef";
  for(size_t i = 0; i < n; i++){ out[2 * i] = H[b[i] >> 4]; out[2 * i + 1] = H[b[i] & 15]; }
  out[2 * n] = 0;
}

void flexWebNewCode(FlexWebCtx* w){
  uint8_t r[FLEXWEB_CODE_LEN];
  if(w->host.random) w->host.random(w->host.ctx, r, sizeof(r));
  else memset(r, 7, sizeof(r));
  // Rechazo sesgado: solo se aceptan bytes < 250 para que cada cifra salga
  // equiprobable (256 no es multiplo de 10).
  for(int i = 0; i < FLEXWEB_CODE_LEN; i++){
    uint8_t v = r[i];
    int guard = 0;
    while(v >= 250 && guard++ < 16){
      if(w->host.random) w->host.random(w->host.ctx, &v, 1); else v = 0;
    }
    w->code[i] = (char)('0' + (v % 10));
  }
  w->code[FLEXWEB_CODE_LEN] = 0;
  wipe(r, sizeof(r));
}

void flexWebDropAll(FlexWebCtx* w){
  if(!w) return;
  wipe(w->sess, sizeof(w->sess));
}
void flexWebDropOwners(FlexWebCtx* w){ if(w) w->ownerEpoch++; }

void flexWebInit(FlexWebCtx* w){
  if(!w) return;
  flexWebDropAll(w);
  memset(&w->pairLim, 0, sizeof(w->pairLim));
  memset(&w->loginLim, 0, sizeof(w->loginLim));
  w->uploading = false;
  w->served = w->uploads = w->downloads = 0;
  if(!w->port) w->port = FLEXWEB_PORT;
  flexWebNewCode(w);
}

int flexWebSessionCount(const FlexWebCtx* w){
  int n = 0;
  if(w) for(int i = 0; i < FLEXWEB_SESS_N; i++) if(w->sess[i].tok[0]) n++;
  return n;
}

static FlexWebSess* sessFind(FlexWebCtx* w, const char* tok){
  if(!tok || strlen(tok) != 32) return NULL;
  uint32_t now = nowMs(w);
  FlexWebSess* found = NULL;
  for(int i = 0; i < FLEXWEB_SESS_N; i++){
    FlexWebSess* s = &w->sess[i];
    if(!s->tok[0]) continue;
    if(now - s->lastMs > FLEXWEB_SESS_IDLE_MS){ wipe(s, sizeof(*s)); continue; }
    if(eqCT(s->tok, tok, 32)) found = s;            // se recorren TODAS: tiempo constante
  }
  if(found) found->lastMs = now;
  return found;
}
static bool sessOwner(FlexWebCtx* w, FlexWebSess* s){
  if(!s || !s->owner) return false;
  uint32_t now = nowMs(w);
  if(s->ownerEpoch != w->ownerEpoch || now - s->ownerMs > FLEXWEB_OWNER_IDLE_MS){ s->owner = 0; return false; }
  s->ownerMs = now;
  return true;
}
static FlexWebSess* sessNew(FlexWebCtx* w){
  FlexWebSess* slot = NULL;
  for(int i = 0; i < FLEXWEB_SESS_N && !slot; i++) if(!w->sess[i].tok[0]) slot = &w->sess[i];
  if(!slot){                                        // la menos usada cede su sitio
    slot = &w->sess[0];
    for(int i = 1; i < FLEXWEB_SESS_N; i++) if(w->sess[i].lastMs < slot->lastMs) slot = &w->sess[i];
  }
  uint8_t r[16];
  if(w->host.random) w->host.random(w->host.ctx, r, sizeof(r)); else memset(r, 0x5A, sizeof(r));
  memset(slot, 0, sizeof(*slot));
  hexOf(r, sizeof(r), slot->tok);
  wipe(r, sizeof(r));
  slot->lastMs = nowMs(w);
  return slot;
}

// ---- limitador de intentos ----
static uint32_t limWait(FlexWebCtx* w, FlexWebLimiter* l){
  if(!l->untilMs) return 0;
  int32_t d = (int32_t)(l->untilMs - nowMs(w));
  if(d <= 0){ l->untilMs = 0; return 0; }
  return (uint32_t)d;
}
static void limFail(FlexWebCtx* w, FlexWebLimiter* l){
  if(l->fails < 250) l->fails++;
  if(l->fails >= FLEXWEB_TRIES){
    int k = l->fails - FLEXWEB_TRIES;
    if(k > 5) k = 5;
    l->untilMs = nowMs(w) + (30000u << k);          // 30 s, 1 min, 2 min... 16 min
    if(!l->untilMs) l->untilMs = 1;
  }
}
static void limOk(FlexWebLimiter* l){ l->fails = 0; l->untilMs = 0; }
static int  limLeft(const FlexWebLimiter* l){ return l->fails >= FLEXWEB_TRIES ? 0 : FLEXWEB_TRIES - l->fails; }

// -------------------------------------------------------------
//  Tipos MIME y nombres de descarga
// -------------------------------------------------------------
const char* flexWebMimeOf(int fmt){
  switch(fmt){
    case FML_F_JPEG: case FML_F_JPEG_PROG: return "image/jpeg";
    case FML_F_PNG:  return "image/png";
    case FML_F_GIF:  return "image/gif";
    case FML_F_BMP:  return "image/bmp";
    case FML_F_WEBP: return "image/webp";
    case FML_F_HEIC: return "image/heic";
    case FML_F_AVIF: return "image/avif";
    case FML_F_AVI_MJPEG: case FML_F_AVI_OTHER: return "video/x-msvideo";
    case FML_F_MP4:  return "video/mp4";
    case FML_F_MOV:  return "video/quicktime";
    case FML_F_WEBM: return "video/webm";
    case FML_F_MKV:  return "video/x-matroska";
    case FML_F_WAV_PCM: case FML_F_WAV_ADPCM: case FML_F_WAV_OTHER: return "audio/wav";
    case FML_F_MP3:  return "audio/mpeg";
    case FML_F_AAC:  return "audio/aac";
    case FML_F_M4A:  return "audio/mp4";
    case FML_F_FLAC: return "audio/flac";
    case FML_F_OGG:  return "audio/ogg";
  }
  return "application/octet-stream";
}

// Nombre con el que se descarga: el ORIGINAL del usuario, pero con la
// extension del formato real (una foto que llego como HEIC y se guardo
// convertida es un .jpg, y descargarla como .heic la haria ilegible).
static void downloadName(const FlexMlRec* r, char* out, size_t cap){
  char base[FML_NAME_MAX];
  copyz(base, sizeof(base), r->name[0] ? r->name : (strrchr(r->path, '/') ? strrchr(r->path, '/') + 1 : r->path));
  if(r->flags & FML_R_LOCKED && !strcmp(base, "Elemento protegido")) snprintf(base, sizeof(base), "Flex %lu", (unsigned long)r->id);
  char* dot = strrchr(base, '.');
  if(dot && dot != base) *dot = 0;
  const char* ext = flexMlFmtExt(r->fmt);
  if(!ext[0]){ const char* pd = strrchr(r->path, '.'); ext = pd ? pd : ""; }
  snprintf(out, cap, "%s%s", base, ext);
}

// -------------------------------------------------------------
//  Fecha MS-DOS (ZIP)
// -------------------------------------------------------------
void flexWebDosTime(uint32_t epoch, uint16_t* dosTime, uint16_t* dosDate){
  if(epoch < 315532800u) epoch = 315532800u;         // ZIP empieza en 1980
  uint32_t days = epoch / 86400u, rem = epoch % 86400u;
  // Dias -> fecha civil (algoritmo de Howard Hinnant, sin tablas).
  int64_t z = (int64_t)days + 719468;
  int64_t era = z / 146097;
  uint32_t doe = (uint32_t)(z - era * 146097);
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t y = (int64_t)yoe + era * 400;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t mp = (5 * doy + 2) / 153;
  uint32_t d = doy - (153 * mp + 2) / 5 + 1;
  uint32_t m = mp < 10 ? mp + 3 : mp - 9;
  if(m <= 2) y++;
  if(y > 2107) y = 2107;
  if(dosDate) *dosDate = (uint16_t)(((uint32_t)(y - 1980) << 9) | (m << 5) | d);
  if(dosTime) *dosTime = (uint16_t)(((rem / 3600u) << 11) | (((rem % 3600u) / 60u) << 5) | ((rem % 60u) / 2u));
}

// -------------------------------------------------------------
//  Peticion en curso
// -------------------------------------------------------------
struct Req {
  FlexHttpReqEx r;
  const uint8_t* pre;     // cuerpo que ya vino con la cabecera
  size_t   preN;
  size_t   preUsed;       // cuanto de 'pre' era cuerpo de ESTA peticion
  bool     keep;
  bool     head;
  FlexWebSess* s;
  bool     owner;
};

// Lee un cuerpo PEQUENO entero (clave, codigo). -1 si no llega o no cabe.
static int readSmallBody(const FlexWebConn* c, Req* q, char* out, size_t cap){
  if(q->r.contentLength < 0 || q->r.chunked) return -1;
  size_t need = (size_t)q->r.contentLength;
  if(need + 1 > cap) return -1;
  size_t got = q->preN < need ? q->preN : need;
  memcpy(out, q->pre, got);
  q->preUsed = got;
  while(got < need){
    int n = c->read(c->ctx, (uint8_t*)out + got, need - got, FLEXWEB_BODY_TIMEOUT_MS);
    if(n <= 0) return -1;
    got += (size_t)n;
  }
  out[need] = 0;
  return (int)need;
}

static bool idFromPath(const char* path, const char* prefix, uint32_t* id){
  size_t pl = strlen(prefix);
  if(strncmp(path, prefix, pl)) return false;
  return flexHttpParseU32(path + pl, id) && *id;
}

// -------------------------------------------------------------
//  RUTAS PUBLICAS
// -------------------------------------------------------------
static const char kCsp[] =
  "Content-Security-Policy: default-src 'self'; img-src 'self' blob: data:; media-src 'self' blob:; "
  "style-src 'self'; script-src 'self'; connect-src 'self'; worker-src 'self' blob:; "
  "base-uri 'none'; form-action 'none'; frame-ancestors 'none'\r\n";

static void handleStatic(const FlexWebConn* c, Req* q, const char* mime, const char* body, size_t len){
  q->keep = sendBody(c, 200, mime, body, len, kCsp, q->keep, q->head) && q->keep;
}

static void handleHello(FlexWebCtx* w, const FlexWebConn* c, Req* q){
  char b[256];
  int lt = w->host.lockType ? w->host.lockType(w->host.ctx) : 0;
  snprintf(b, sizeof(b), "{\"paired\":%d,\"owner\":%d,\"lockType\":%d,\"up\":%d,\"dev\":\"Flex OS Ultra\"}",
           q->s ? 1 : 0, q->owner ? 1 : 0, lt, w->allowUpload ? 1 : 0);
  q->keep = sendJson(c, 200, b, q->keep) && q->keep;
}

static void handlePair(FlexWebCtx* w, const FlexWebConn* c, Req* q){
  char body[FLEXWEB_SMALL_BODY];
  uint32_t wait = limWait(w, &w->pairLim);
  if(readSmallBody(c, q, body, sizeof(body)) < 0){ q->keep = false; sendErr(c, 400, "Petici\xC3\xB3n no v\xC3\xA1lida", false); return; }
  if(wait){
    char more[48], ra[48];
    snprintf(more, sizeof(more), ",\"wait\":%lu", (unsigned long)((wait + 999) / 1000));
    snprintf(ra, sizeof(ra), "Retry-After: %lu\r\n", (unsigned long)((wait + 999) / 1000));
    q->keep = sendErr(c, 429, "Demasiados intentos. Espera antes de volver a probar", q->keep, more, ra) && q->keep;
    return;
  }
  char code[16];
  flexHttpQueryGet(body, "code", code, sizeof(code));
  wipe(body, sizeof(body));
  bool ok = strlen(code) == FLEXWEB_CODE_LEN && eqCT(code, w->code, FLEXWEB_CODE_LEN);
  wipe(code, sizeof(code));
  if(!ok){
    limFail(w, &w->pairLim);
    char more[24]; snprintf(more, sizeof(more), ",\"left\":%d", limLeft(&w->pairLim));
    q->keep = sendErr(c, 403, "C\xC3\xB3" "digo incorrecto", q->keep, more) && q->keep;
    return;
  }
  limOk(&w->pairLim);
  FlexWebSess* s = sessNew(w);
  char ck[128];
  snprintf(ck, sizeof(ck), "Set-Cookie: " FLEXHTTP_SESS_COOKIE "=%s; Path=/; HttpOnly; SameSite=Strict\r\n", s->tok);
  q->keep = sendJson(c, 200, "{\"ok\":1}", q->keep, ck) && q->keep;
  emit(w, FLEXWEB_EV_PAIRED, 0, 0, 0, 0, "", "M\xC3\xB3vil conectado");
}

// -------------------------------------------------------------
//  RUTAS CON SESION
// -------------------------------------------------------------
static void handleLogin(FlexWebCtx* w, const FlexWebConn* c, Req* q){
  char body[FLEXWEB_SMALL_BODY];
  int n = readSmallBody(c, q, body, sizeof(body));
  if(n < 0){ q->keep = false; sendErr(c, 400, "Petici\xC3\xB3n no v\xC3\xA1lida", false); return; }
  int lt = w->host.lockType ? w->host.lockType(w->host.ctx) : 0;
  if(lt == 0){
    wipe(body, sizeof(body));
    // Sin clave en el sistema no hay con que comprobar al propietario: lo
    // protegido sigue oculto aqui (en el P4 lo abre quien tiene el aparato).
    q->keep = sendErr(c, 409, "Flex OS no tiene PIN ni contrase\xC3\xB1" "a: configura uno en Seguridad para ver lo protegido", q->keep) && q->keep;
    return;
  }
  uint32_t wait = limWait(w, &w->loginLim);
  if(wait){
    wipe(body, sizeof(body));
    char more[48], ra[48];
    snprintf(more, sizeof(more), ",\"wait\":%lu", (unsigned long)((wait + 999) / 1000));
    snprintf(ra, sizeof(ra), "Retry-After: %lu\r\n", (unsigned long)((wait + 999) / 1000));
    q->keep = sendErr(c, 429, "Demasiados intentos. Espera antes de volver a probar", q->keep, more, ra) && q->keep;
    return;
  }
  char secret[72];
  flexHttpQueryGet(body, "secret", secret, sizeof(secret));
  wipe(body, sizeof(body));
  bool ok = secret[0] && w->host.verifySecret && w->host.verifySecret(w->host.ctx, secret);
  wipe(secret, sizeof(secret));
  if(!ok){
    limFail(w, &w->loginLim);
    char more[24]; snprintf(more, sizeof(more), ",\"left\":%d", limLeft(&w->loginLim));
    q->keep = sendErr(c, 401, lt == 1 ? "PIN incorrecto" : "Contrase\xC3\xB1" "a incorrecta", q->keep, more) && q->keep;
    emit(w, FLEXWEB_EV_OWNER_FAIL, 0, 0, 0, 0, "", "Intento de acceso fallido desde el m\xC3\xB3vil");
    return;
  }
  limOk(&w->loginLim);
  q->s->owner = 1;
  q->s->ownerEpoch = w->ownerEpoch;
  q->s->ownerMs = nowMs(w);
  q->keep = sendJson(c, 200, "{\"owner\":1}", q->keep) && q->keep;
  emit(w, FLEXWEB_EV_OWNER, 0, 0, 0, 0, "", "Acceso al contenido protegido desde el m\xC3\xB3vil");
}

static void handleLogout(FlexWebCtx* w, const FlexWebConn* c, Req* q, bool unpair){
  char body[FLEXWEB_SMALL_BODY];
  if(q->r.contentLength > 0 && readSmallBody(c, q, body, sizeof(body)) < 0){ q->keep = false; return; }
  if(unpair){ wipe(q->s, sizeof(*q->s)); q->s = NULL; }
  else { q->s->owner = 0; }
  const char* ck = unpair ? "Set-Cookie: " FLEXHTTP_SESS_COOKIE "=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict\r\n" : NULL;
  q->keep = sendJson(c, 200, "{\"ok\":1}", q->keep, ck) && q->keep;
  (void)w;
}

// ---- JSON por trozos ----
struct ChunkSink { const FlexWebConn* c; uint8_t* buf; size_t n, cap; bool ok; };
static bool chunkFlush(ChunkSink* k){
  if(!k->ok || k->n == 0) return k->ok;
  char h[16];
  size_t hl = flexHttpChunkHead(h, sizeof(h), k->n);
  k->ok = sendAll(k->c, h, hl) && sendAll(k->c, k->buf, k->n) && sendAll(k->c, "\r\n", 2);
  k->n = 0;
  return k->ok;
}
static bool chunkAdd(void* ctx, const char* s, size_t n){
  ChunkSink* k = (ChunkSink*)ctx;
  while(n && k->ok){
    size_t room = k->cap - k->n;
    size_t t = n < room ? n : room;
    memcpy(k->buf + k->n, s, t);
    k->n += t; s += t; n -= t;
    if(k->n == k->cap) chunkFlush(k);
  }
  return k->ok;
}

static void handleLibrary(FlexWebCtx* w, const FlexWebConn* c, Req* q, uint8_t* io){
  char v[16];
  uint32_t since = 0, rev = w->host.rev ? w->host.rev(w->host.ctx) : 0;
  int lt = w->host.lockType ? w->host.lockType(w->host.ctx) : 0;
  if(flexHttpQueryGet(q->r.query, "since", v, sizeof(v)) && flexHttpParseU32(v, &since) && since == rev){
    // Tambien el nivel de propietario: puede caducar o caer con el bloqueo
    // del P4 sin que cambie el catalogo, y la web tiene que enterarse.
    char b[80]; snprintf(b, sizeof(b), "{\"rev\":%lu,\"same\":1,\"owner\":%d}", (unsigned long)rev, q->owner ? 1 : 0);
    q->keep = sendJson(c, 200, b, q->keep) && q->keep;
    return;
  }
  FlexMlRec* recs = (FlexMlRec*)wAlloc(w, sizeof(FlexMlRec) * FML_CAP);
  if(!recs){ q->keep = sendErr(c, 503, "Memoria insuficiente en Flex OS", q->keep) && q->keep; return; }
  int n = w->host.snapshot ? w->host.snapshot(w->host.ctx, recs, FML_CAP, &rev) : 0;
  FlexMlLib lib;
  flexMlInit(&lib, recs, FML_CAP);
  lib.n = (uint16_t)(n < 0 ? 0 : n);
  lib.rev = rev;
  char head[320];
  uint32_t fr = w->fs.freeBytes ? w->fs.freeBytes(w->fs.ctx) : 0, tot = w->fs.totalBytes ? w->fs.totalBytes(w->fs.ctx) : 0;
  snprintf(head, sizeof(head),
           "\"rev\":%lu,\"owner\":%d,\"lockType\":%d,\"free\":%lu,\"total\":%lu,\"reserve\":%lu,\"up\":%d,"
           "\"lim\":{\"photo\":%lu,\"video\":%lu,\"audio\":%lu}",
           (unsigned long)rev, q->owner ? 1 : 0, lt, (unsigned long)fr, (unsigned long)tot,
           (unsigned long)FML_RESERVE_BYTES, w->allowUpload ? 1 : 0,
           (unsigned long)FML_LIMIT_PHOTO, (unsigned long)FML_LIMIT_VIDEO, (unsigned long)FML_LIMIT_AUDIO);
  bool ok = sendHead(c, 200, "application/json; charset=utf-8", -1, NULL, q->keep);
  ChunkSink k = { c, io, 0, FLEXWEB_IO_BUF, ok };
  if(ok) flexMlJsonLibrary(&lib, q->owner, FLEXWEB_MASK, head, chunkAdd, &k);
  if(k.ok) chunkFlush(&k);
  if(k.ok) k.ok = sendAll(c, "0\r\n\r\n", 5);
  wFree(w, recs);
  q->keep = k.ok && q->keep;
}

static void handleStatus(FlexWebCtx* w, const FlexWebConn* c, Req* q){
  char b[160];
  uint32_t rev = w->host.rev ? w->host.rev(w->host.ctx) : 0;
  uint32_t fr = w->fs.freeBytes ? w->fs.freeBytes(w->fs.ctx) : 0, tot = w->fs.totalBytes ? w->fs.totalBytes(w->fs.ctx) : 0;
  snprintf(b, sizeof(b), "{\"rev\":%lu,\"free\":%lu,\"total\":%lu,\"busy\":%d,\"owner\":%d}",
           (unsigned long)rev, (unsigned long)fr, (unsigned long)tot, w->uploading ? 1 : 0, q->owner ? 1 : 0);
  q->keep = sendJson(c, 200, b, q->keep) && q->keep;
}

// Envia `len` bytes de un archivo desde `off`. false si algo fallo (y
// entonces la conexion no se puede reutilizar: el Content-Length ya salio).
static bool streamFile(FlexWebCtx* w, const FlexWebConn* c, const char* path, uint32_t off, uint32_t len,
                       uint8_t* io, int evKind, const char* evName, uint32_t evTotal){
  void* h = w->fs.open(w->fs.ctx, path, false);
  if(!h) return false;
  bool ok = (off == 0) || w->fs.seek(w->fs.ctx, h, off);
  uint32_t sent = 0, lastEv = 0;
  while(ok && sent < len){
    size_t want = len - sent < FLEXWEB_IO_BUF ? len - sent : FLEXWEB_IO_BUF;
    int n = w->fs.read(w->fs.ctx, h, io, want);
    if(n <= 0){ ok = false; break; }
    if(!sendAll(c, io, (size_t)n)){ ok = false; break; }
    sent += (uint32_t)n;
    if(evKind >= 0 && sent - lastEv >= 64u * 1024u){
      lastEv = sent;
      emit(w, FLEXWEB_EV_DL_PROGRESS, evKind, sent, evTotal, 0, evName, "");
    }
    yieldHost(w);
  }
  w->fs.close(w->fs.ctx, h);
  return ok;
}

static bool accessOk(const FlexMlRec* r, bool owner){ return !(r->flags & FML_R_LOCKED) || owner; }

static bool webRec(FlexWebCtx* w, uint32_t id, FlexMlRec* r){
  return w->host.getRec && w->host.getRec(w->host.ctx, id, r) && (FLEXWEB_MASK & FML_MASK(r->kind));
}
static const char* kLockedMsg = "Contenido protegido: desbloqu\xC3\xA9" "alo con el PIN o la contrase\xC3\xB1" "a de Flex OS";

static void handleThumbGet(FlexWebCtx* w, const FlexWebConn* c, Req* q, uint32_t id, uint8_t* io){
  FlexMlRec r;
  if(!webRec(w, id, &r)){ q->keep = sendErr(c, 404, "No existe", q->keep) && q->keep; return; }
  if(!accessOk(&r, q->owner)){ q->keep = sendErr(c, 403, kLockedMsg, q->keep, ",\"lock\":1") && q->keep; return; }
  char path[FML_PATH_MAX] = "";
  if((r.flags & FML_R_THUMB) && w->host.thumbPath) w->host.thumbPath(w->host.ctx, &r, path, sizeof(path));
  uint32_t sz = path[0] ? w->fs.size(w->fs.ctx, path) : 0;
  if(!sz){ q->keep = sendErr(c, 404, "Sin miniatura", q->keep) && q->keep; return; }
  const char* cache = (r.flags & FML_R_LOCKED) ? "Cache-Control: no-store\r\n"
                                                : "Cache-Control: private, max-age=31536000, immutable\r\n";
  if(!sendHead(c, 200, "image/jpeg", sz, cache, q->keep)){ q->keep = false; return; }
  if(q->head) return;
  if(!streamFile(w, c, path, 0, sz, io, -1, "", 0)) q->keep = false;
}

static void handleFile(FlexWebCtx* w, const FlexWebConn* c, Req* q, uint32_t id, uint8_t* io){
  FlexMlRec r;
  if(!webRec(w, id, &r)){ q->keep = sendErr(c, 404, "No existe", q->keep) && q->keep; return; }
  if(!accessOk(&r, q->owner)){ q->keep = sendErr(c, 403, kLockedMsg, q->keep, ",\"lock\":1") && q->keep; return; }
  uint32_t size = w->fs.size(w->fs.ctx, r.path);
  if(!size){ q->keep = sendErr(c, 404, "El archivo ya no est\xC3\xA1 en la memoria de Flex OS", q->keep) && q->keep; return; }
  uint32_t a = 0, z = size - 1;
  int status = 200;
  if(q->r.hasRange){
    if(q->r.rangeStart < 0){                          // "los ultimos N"
      uint32_t k = (uint32_t)q->r.rangeEnd;
      if(k == 0){ a = size; } else { if(k > size) k = size; a = size - k; }
    } else {
      a = (uint32_t)q->r.rangeStart;
      if(q->r.rangeEnd >= 0 && (uint32_t)q->r.rangeEnd < z) z = (uint32_t)q->r.rangeEnd;
    }
    if(a >= size){
      char cr[64]; snprintf(cr, sizeof(cr), "Content-Range: bytes */%lu\r\n", (unsigned long)size);
      q->keep = sendBody(c, 416, "text/plain", "", 0, cr, q->keep, false) && q->keep;
      return;
    }
    status = 206;
  }
  char dl[8] = "";
  flexHttpQueryGet(q->r.query, "dl", dl, sizeof(dl));
  char nm[FML_NAME_MAX + 8];
  downloadName(&r, nm, sizeof(nm));
  char extra[640];
  size_t e = flexHttpDisposition(extra, sizeof(extra), nm, dl[0] == '1' ? 0 : 1);
  if(!e){ copyz(extra, sizeof(extra), "Content-Disposition: attachment\r\n"); e = strlen(extra); }
  int m = snprintf(extra + e, sizeof(extra) - e, "Accept-Ranges: bytes\r\n");
  if(m > 0) e += (size_t)m;
  if(status == 206){
    m = snprintf(extra + e, sizeof(extra) - e, "Content-Range: bytes %lu-%lu/%lu\r\n",
                 (unsigned long)a, (unsigned long)z, (unsigned long)size);
    if(m > 0) e += (size_t)m;
  }
  uint32_t len = z - a + 1;
  if(!sendHead(c, status, flexWebMimeOf(r.fmt), len, extra, q->keep)){ q->keep = false; return; }
  if(q->head) return;
  emit(w, FLEXWEB_EV_DL_START, r.kind, 0, len, r.id, nm, "");
  if(!streamFile(w, c, r.path, a, len, io, r.kind, nm, len)){
    q->keep = false;
    emit(w, FLEXWEB_EV_DL_FAIL, r.kind, 0, len, r.id, nm, "La descarga se interrumpi\xC3\xB3");
    return;
  }
  w->downloads++;
  emit(w, FLEXWEB_EV_DL_DONE, r.kind, len, len, r.id, nm, "");
}

// -------------------------------------------------------------
//  ZIP (almacenado, por flujo, con descriptor de datos)
// -------------------------------------------------------------
struct ZipEnt { char path[FML_PATH_MAX]; char name[FML_NAME_MAX + 16]; uint32_t size, crc, off; uint16_t t, d; };

static void put16(uint8_t* p, uint16_t v){ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t* p, uint32_t v){ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static void handleZip(FlexWebCtx* w, const FlexWebConn* c, Req* q, uint8_t* io){
  char ids[FLEXHTTP_QUERY_MAX];
  if(!flexHttpQueryGet(q->r.query, "ids", ids, sizeof(ids)) || !ids[0]){
    q->keep = sendErr(c, 400, "Sin elementos que descargar", q->keep) && q->keep; return;
  }
  ZipEnt* ent = (ZipEnt*)wAlloc(w, sizeof(ZipEnt) * FLEXWEB_ZIP_MAX);
  if(!ent){ q->keep = sendErr(c, 503, "Memoria insuficiente en Flex OS", q->keep) && q->keep; return; }
  int n = 0;
  const char* p = ids;
  while(*p){
    char tok[12]; size_t k = 0;
    while(*p && *p != ',' && k + 1 < sizeof(tok)) tok[k++] = *p++;
    tok[k] = 0;
    while(*p && *p != ',') p++;
    if(*p == ',') p++;
    uint32_t id;
    if(!flexHttpParseU32(tok, &id) || !id){ wFree(w, ent); q->keep = sendErr(c, 400, "Lista de elementos no v\xC3\xA1lida", q->keep) && q->keep; return; }
    bool dup = false;
    for(int i = 0; i < n; i++) if(ent[i].off == id) dup = true;   // 'off' guarda el id hasta el recuento
    if(dup) continue;
    if(n >= FLEXWEB_ZIP_MAX){ wFree(w, ent); q->keep = sendErr(c, 413, "Como m\xC3\xA1ximo 64 elementos por descarga", q->keep) && q->keep; return; }
    FlexMlRec r;
    if(!webRec(w, id, &r)){ wFree(w, ent); q->keep = sendErr(c, 404, "Un elemento ya no existe", q->keep) && q->keep; return; }
    if(!accessOk(&r, q->owner)){ wFree(w, ent); q->keep = sendErr(c, 403, kLockedMsg, q->keep, ",\"lock\":1") && q->keep; return; }
    ZipEnt* z = &ent[n];
    memset(z, 0, sizeof(*z));
    copyz(z->path, sizeof(z->path), r.path);
    z->size = w->fs.size(w->fs.ctx, r.path);
    if(!z->size){ wFree(w, ent); q->keep = sendErr(c, 404, "Un elemento ya no est\xC3\xA1 en la memoria", q->keep) && q->keep; return; }
    downloadName(&r, z->name, sizeof(z->name));
    // Nombres repetidos dentro del ZIP: "foto.jpg", "foto (2).jpg"...
    for(int dupN = 2; dupN < 100; dupN++){
      bool clash = false;
      for(int i = 0; i < n; i++) if(!strcmp(ent[i].name, z->name)) clash = true;
      if(!clash) break;
      char base[FML_NAME_MAX + 16];
      downloadName(&r, base, sizeof(base));
      char* dot = strrchr(base, '.');
      char ext[16] = "";
      if(dot){ copyz(ext, sizeof(ext), dot); *dot = 0; }
      snprintf(z->name, sizeof(z->name), "%.40s (%d)%s", base, dupN, ext);
    }
    flexWebDosTime(r.created ? r.created : r.added, &z->t, &z->d);
    z->off = id;
    n++;
  }
  if(n == 0){ wFree(w, ent); q->keep = sendErr(c, 400, "Sin elementos que descargar", q->keep) && q->keep; return; }
  // Tamano EXACTO antes de mandar un byte: Content-Length de verdad, sin
  // chunked, para que el movil ensene una barra real.
  uint64_t total = 22;
  for(int i = 0; i < n; i++){
    size_t nl = strlen(ent[i].name);
    total += 30 + nl + ent[i].size + 16;             // local + datos + descriptor
    total += 46 + nl;                                 // directorio central
  }
  if(total > 0xFFFFFFF0ull){ wFree(w, ent); q->keep = sendErr(c, 413, "Descarga demasiado grande", q->keep) && q->keep; return; }
  char extra[256];
  char zn[64];
  snprintf(zn, sizeof(zn), "Flex OS (%d archivo%s).zip", n, n == 1 ? "" : "s");
  size_t e = flexHttpDisposition(extra, sizeof(extra), zn, 0);
  if(!e) copyz(extra, sizeof(extra), "Content-Disposition: attachment\r\n");
  if(!sendHead(c, 200, "application/zip", (long long)total, extra, false)){ wFree(w, ent); q->keep = false; return; }
  q->keep = false;                                    // pase lo que pase, despues se cierra
  if(q->head){ wFree(w, ent); return; }
  emit(w, FLEXWEB_EV_DL_START, 0, 0, (uint32_t)total, 0, zn, "");
  uint32_t off = 0, sentData = 0;
  bool ok = true;
  uint8_t hdr[64];
  for(int i = 0; i < n && ok; i++){
    ZipEnt* z = &ent[i];
    size_t nl = strlen(z->name);
    z->off = off;
    put32(hdr, 0x04034b50u); put16(hdr + 4, 20); put16(hdr + 6, 0x0808); put16(hdr + 8, 0);
    put16(hdr + 10, z->t); put16(hdr + 12, z->d); put32(hdr + 14, 0); put32(hdr + 18, 0); put32(hdr + 22, 0);
    put16(hdr + 26, (uint16_t)nl); put16(hdr + 28, 0);
    ok = sendAll(c, hdr, 30) && sendAll(c, z->name, nl);
    off += 30 + (uint32_t)nl;
    void* h = ok ? w->fs.open(w->fs.ctx, z->path, false) : NULL;
    if(!h) ok = false;
    uint32_t crc = 0, got = 0;
    while(ok && got < z->size){
      size_t want = z->size - got < FLEXWEB_IO_BUF ? z->size - got : FLEXWEB_IO_BUF;
      int r = w->fs.read(w->fs.ctx, h, io, want);
      if(r <= 0){ ok = false; break; }
      crc = flexMlCrc32(crc, io, (size_t)r);
      if(!sendAll(c, io, (size_t)r)){ ok = false; break; }
      got += (uint32_t)r; sentData += (uint32_t)r;
      yieldHost(w);
    }
    if(h) w->fs.close(w->fs.ctx, h);
    if(!ok) break;
    z->crc = crc;
    off += z->size;
    put32(hdr, 0x08074b50u); put32(hdr + 4, crc); put32(hdr + 8, z->size); put32(hdr + 12, z->size);
    ok = sendAll(c, hdr, 16);
    off += 16;
    emit(w, FLEXWEB_EV_DL_PROGRESS, 0, off, (uint32_t)total, 0, zn, "");
  }
  uint32_t cdStart = off, cdSize = 0;
  for(int i = 0; i < n && ok; i++){
    ZipEnt* z = &ent[i];
    size_t nl = strlen(z->name);
    put32(hdr, 0x02014b50u); put16(hdr + 4, 20); put16(hdr + 6, 20); put16(hdr + 8, 0x0808); put16(hdr + 10, 0);
    put16(hdr + 12, z->t); put16(hdr + 14, z->d); put32(hdr + 16, z->crc); put32(hdr + 20, z->size);
    put32(hdr + 24, z->size); put16(hdr + 28, (uint16_t)nl); put16(hdr + 30, 0); put16(hdr + 32, 0);
    put16(hdr + 34, 0); put16(hdr + 36, 0); put32(hdr + 38, 0); put32(hdr + 42, z->off);
    ok = sendAll(c, hdr, 46) && sendAll(c, z->name, nl);
    cdSize += 46 + (uint32_t)nl;
  }
  if(ok){
    put32(hdr, 0x06054b50u); put16(hdr + 4, 0); put16(hdr + 6, 0); put16(hdr + 8, (uint16_t)n);
    put16(hdr + 10, (uint16_t)n); put32(hdr + 12, cdSize); put32(hdr + 16, cdStart); put16(hdr + 20, 0);
    ok = sendAll(c, hdr, 22);
  }
  wFree(w, ent);
  if(ok){ w->downloads++; emit(w, FLEXWEB_EV_DL_DONE, 0, (uint32_t)total, (uint32_t)total, 0, zn, ""); }
  else emit(w, FLEXWEB_EV_DL_FAIL, 0, sentData, (uint32_t)total, 0, zn, "La descarga se interrumpi\xC3\xB3");
}

// -------------------------------------------------------------
//  SUBIDA
// -------------------------------------------------------------
struct FsIo { FlexWebCtx* w; void* h; uint32_t pos, size; };
static int fsioRead(void* c, void* buf, uint32_t n){
  FsIo* f = (FsIo*)c;
  if(!f->w->fs.seek(f->w->fs.ctx, f->h, f->pos)) return -1;
  int r = f->w->fs.read(f->w->fs.ctx, f->h, (uint8_t*)buf, n);
  if(r > 0) f->pos += (uint32_t)r;
  return r;
}
static bool fsioSeek(void* c, uint32_t off){ FsIo* f = (FsIo*)c; if(off > f->size) return false; f->pos = off; return true; }
static uint32_t fsioSize(void* c){ return ((FsIo*)c)->size; }

struct FsOut { FlexWebCtx* w; void* h; bool ok; };
static bool fsOutWrite(void* c, const uint8_t* d, size_t n){
  FsOut* o = (FsOut*)c;
  if(o->ok) o->ok = o->w->fs.write(o->w->fs.ctx, o->h, d, n);
  return o->ok;
}

static void tmpName(FlexWebCtx* w, const char* pre, const char* ext, char* out, size_t cap){
  uint8_t r[4];
  if(w->host.random) w->host.random(w->host.ctx, r, sizeof(r)); else memset(r, 0, sizeof(r));
  char hx[9]; hexOf(r, 4, hx);
  snprintf(out, cap, FML_DIR_TMP "/%s-%s%s", pre, hx, ext);
}

// Genera la miniatura en un temporal. Devuelve el FLEXTH_* del intento.
static int thumbToTmp(FlexWebCtx* w, char* thumbTmp, size_t cap, int kind, int fmt,
                      const char* dataPath, uint32_t size, int* ow, int* oh, uint32_t* dur){
  tmpName(w, "th", ".jpg", thumbTmp, cap);
  FsOut o = { w, w->fs.open(w->fs.ctx, thumbTmp, true), true };
  if(!o.h){ thumbTmp[0] = 0; return FLEXTH_ERR_WRITE; }
  int rc = FLEXTH_ERR_ARG;
  if(fmt == FML_F_JPEG || fmt == FML_F_JPEG_PROG){
    uint8_t* buf = (uint8_t*)wAlloc(w, size);
    if(!buf) rc = FLEXTH_ERR_MEMORY;
    else {
      void* h = w->fs.open(w->fs.ctx, dataPath, false);
      uint32_t got = 0;
      while(h && got < size){
        int r = w->fs.read(w->fs.ctx, h, buf + got, size - got);
        if(r <= 0) break;
        got += (uint32_t)r;
      }
      if(h) w->fs.close(w->fs.ctx, h);
      rc = (got == size) ? flexThumbFromJpeg(buf, size, FLEXTH_SIDE, FLEXTH_QUALITY, fsOutWrite, &o, ow, oh,
                                             w->alloc, w->free)
                         : FLEXTH_ERR_IO;
      wFree(w, buf);
    }
  } else if(fmt == FML_F_AVI_MJPEG || fmt == FML_F_AVI_OTHER){
    FsIo f = { w, w->fs.open(w->fs.ctx, dataPath, false), 0, size };
    if(!f.h) rc = FLEXTH_ERR_IO;
    else {
      FlexMediaIO io; io.read = fsioRead; io.seek = fsioSeek; io.size = fsioSize; io.ctx = &f;
      rc = flexThumbFromAvi(&io, FLEXTH_SIDE, FLEXTH_QUALITY, fsOutWrite, &o, ow, oh, dur, w->alloc, w->free);
      w->fs.close(w->fs.ctx, f.h);
    }
  }
  (void)kind;
  w->fs.close(w->fs.ctx, o.h);
  if(rc != FLEXTH_OK || !o.ok){
    w->fs.remove(w->fs.ctx, thumbTmp);
    thumbTmp[0] = 0;
    if(rc == FLEXTH_OK) rc = FLEXTH_ERR_WRITE;
  }
  return rc;
}

static void upFail(FlexWebCtx* w, const FlexWebConn* c, Req* q, int status, const char* msg,
                   const char* tmp, const char* thumbTmp, const char* name, int kind, bool reply){
  if(tmp && tmp[0]) w->fs.remove(w->fs.ctx, tmp);
  if(thumbTmp && thumbTmp[0]) w->fs.remove(w->fs.ctx, thumbTmp);
  emit(w, FLEXWEB_EV_UP_FAIL, kind, 0, 0, 0, name, msg);
  if(reply) sendErr(c, status, msg, false);
  q->keep = false;
}

static void handleUpload(FlexWebCtx* w, const FlexWebConn* c, Req* q, uint8_t* io){
  q->keep = false;          // tras una subida, se cierra siempre: ningun camino de error deja bytes a medias
  if(!w->allowUpload){ sendErr(c, 403, "Las subidas est\xC3\xA1n desactivadas en Flex OS", false); return; }
  if(w->uploading){ sendErr(c, 503, "Ya hay una subida en curso", false); return; }
  if(q->r.chunked || q->r.contentLength < 0){ sendErr(c, 411, "Falta la longitud del archivo", false); return; }

  char v[FML_NAME_MAX * 3];
  FlexWebUpload up;
  memset(&up, 0, sizeof(up));
  up.kind = flexHttpQueryGet(q->r.query, "kind", v, sizeof(v)) ? flexMlKindParse(v) : FML_K_NONE;
  if(up.kind == FML_K_NONE){ sendErr(c, 400, "Tipo de archivo no indicado", false); return; }
  if(!flexHttpQueryGet(q->r.query, "name", v, sizeof(v)) || !v[0]){ sendErr(c, 400, "Falta el nombre del archivo", false); return; }
  flexMlCleanName(v, up.name, sizeof(up.name));
  if(!flexHttpQueryGet(q->r.query, "size", v, sizeof(v)) || !flexHttpParseU32(v, &up.size) ||
     (long long)up.size != q->r.contentLength){
    sendErr(c, 400, "El tama\xC3\xB1o declarado no coincide con lo que se env\xC3\xAD" "a", false); return;
  }
  if(!flexHttpQueryGet(q->r.query, "crc", v, sizeof(v)) || !flexHttpParseU32(v, &up.crc)){
    sendErr(c, 400, "Falta la suma de comprobaci\xC3\xB3n del archivo", false); return;
  }
  uint32_t u;
  if(flexHttpQueryGet(q->r.query, "created", v, sizeof(v)) && flexHttpParseU32(v, &u)) up.created = u;
  if(flexHttpQueryGet(q->r.query, "w", v, sizeof(v)) && flexHttpParseU32(v, &u) && u < 65536) up.w = (uint16_t)u;
  if(flexHttpQueryGet(q->r.query, "h", v, sizeof(v)) && flexHttpParseU32(v, &u) && u < 65536) up.h = (uint16_t)u;
  if(flexHttpQueryGet(q->r.query, "dur", v, sizeof(v)) && flexHttpParseU32(v, &u)) up.durMs = u;
  if(flexHttpQueryGet(q->r.query, "title", v, sizeof(v))) flexMlCleanName(v, up.title, sizeof(up.title));
  if(flexHttpQueryGet(q->r.query, "artist", v, sizeof(v))) flexMlCleanName(v, up.artist, sizeof(up.artist));
  if(flexHttpQueryGet(q->r.query, "album", v, sizeof(v))) flexMlCleanName(v, up.album, sizeof(up.album));

  char why[128];
  uint32_t fr = w->fs.freeBytes ? w->fs.freeBytes(w->fs.ctx) : 0;
  int chk = flexMlCheckUpload(up.kind, up.size, fr, why, sizeof(why));
  if(chk != FML_OK){ sendErr(c, chk == FML_ERR_NOSPACE ? 507 : 413, why, false); return; }

  w->uploading = true;
  tmpName(w, "up", ".part", up.tmpPath, sizeof(up.tmpPath));
  emit(w, FLEXWEB_EV_UP_START, up.kind, 0, up.size, 0, up.name, "");
  void* h = w->fs.open(w->fs.ctx, up.tmpPath, true);
  if(!h){
    upFail(w, c, q, 500, "No se pudo crear el archivo en la memoria de Flex OS", NULL, NULL, up.name, up.kind, true);
    w->uploading = false; return;
  }
  // ---- recepcion, con el progreso en bytes REALES escritos ----
  uint8_t head[512];
  size_t headN = 0;
  uint32_t got = 0, crc = 0, lastEv = 0, lastEvMs = nowMs(w);
  bool wok = true;
  int lost = 0;                                   // 0 bien, 1 plazo vencido, 2 conexion cerrada
  size_t pre = q->preN < up.size ? q->preN : up.size;
  q->preUsed = pre;
  const uint8_t* src = q->pre;
  size_t srcN = pre;
  for(;;){
    if(srcN){
      if(headN < sizeof(head)){ size_t t = sizeof(head) - headN; if(t > srcN) t = srcN; memcpy(head + headN, src, t); headN += t; }
      crc = flexMlCrc32(crc, src, srcN);
      if(!w->fs.write(w->fs.ctx, h, src, srcN)){ wok = false; break; }
      got += (uint32_t)srcN;
      uint32_t now = nowMs(w);
      if(got - lastEv >= 64u * 1024u || now - lastEvMs >= 250u || got == up.size){
        lastEv = got; lastEvMs = now;
        emit(w, FLEXWEB_EV_UP_PROGRESS, up.kind, got, up.size, 0, up.name, "");
      }
      yieldHost(w);
    }
    if(got >= up.size) break;
    size_t want = up.size - got < FLEXWEB_IO_BUF ? up.size - got : FLEXWEB_IO_BUF;
    int n = c->read(c->ctx, io, want, FLEXWEB_BODY_TIMEOUT_MS);
    if(n == 0){ lost = 1; break; }
    if(n < 0){ lost = 2; break; }
    src = io; srcN = (size_t)n;
  }
  w->fs.close(w->fs.ctx, h);
  if(!wok){
    upFail(w, c, q, 507, "No se pudo escribir en la memoria de Flex OS (\xC2\xBFsin espacio?)", up.tmpPath, NULL, up.name, up.kind, true);
    w->uploading = false; return;
  }
  if(lost){
    upFail(w, c, q, 408, lost == 1 ? "La transferencia se interrumpi\xC3\xB3: no llegaron datos en 15 s"
                                   : "El m\xC3\xB3vil se desconect\xC3\xB3 a mitad de la transferencia",
           up.tmpPath, NULL, up.name, up.kind, lost == 1);
    w->uploading = false; return;
  }
  emit(w, FLEXWEB_EV_UP_CHECK, up.kind, got, up.size, 0, up.name, "Comprobando");
  // ---- integridad: tamano en disco y CRC de extremo a extremo ----
  if(w->fs.size(w->fs.ctx, up.tmpPath) != up.size){
    upFail(w, c, q, 500, "El archivo no qued\xC3\xB3 completo en la memoria", up.tmpPath, NULL, up.name, up.kind, true);
    w->uploading = false; return;
  }
  if(crc != up.crc){
    upFail(w, c, q, 400, "El archivo lleg\xC3\xB3 da\xC3\xB1" "ado (la suma de comprobaci\xC3\xB3n no coincide)", up.tmpPath, NULL, up.name, up.kind, true);
    w->uploading = false; return;
  }
  // ---- formato REAL ----
  int skind = FML_K_NONE;
  up.fmt = flexMlSniff(head, headN, &skind);
  if(up.fmt == FML_F_UNKNOWN){
    upFail(w, c, q, 415, "Formato no reconocido", up.tmpPath, NULL, up.name, up.kind, true);
    w->uploading = false; return;
  }
  if(skind != up.kind){
    char m[128];
    snprintf(m, sizeof(m), "El contenido no es %s: es %s",
             up.kind == FML_K_PHOTO ? "una foto" : up.kind == FML_K_VIDEO ? "un v\xC3\xAD" "deo" : "audio",
             flexMlFmtName(up.fmt));
    upFail(w, c, q, 415, m, up.tmpPath, NULL, up.name, up.kind, true);
    w->uploading = false; return;
  }
  // ---- duplicado exacto ----
  uint32_t dup = w->host.findDup ? w->host.findDup(w->host.ctx, up.size, up.crc) : 0;
  if(dup){
    w->fs.remove(w->fs.ctx, up.tmpPath);
    char b[96];
    snprintf(b, sizeof(b), "{\"id\":%lu,\"dup\":1}", (unsigned long)dup);
    sendJson(c, 200, b, false);
    emit(w, FLEXWEB_EV_UP_DONE, up.kind, up.size, up.size, dup, up.name, "Ya estaba en Flex OS");
    w->uploading = false; return;
  }
  // ---- validacion profunda + miniatura ----
  up.playable = flexMlFmtPlayable(up.fmt);
  int ow = 0, oh = 0; uint32_t dur = 0;
  if(up.fmt == FML_F_JPEG || up.fmt == FML_F_JPEG_PROG || up.fmt == FML_F_AVI_MJPEG || up.fmt == FML_F_AVI_OTHER){
    int tr = thumbToTmp(w, up.thumbTmp, sizeof(up.thumbTmp), up.kind, up.fmt, up.tmpPath, up.size, &ow, &oh, &dur);
    if(tr == FLEXTH_ERR_DECODE || tr == FLEXTH_ERR_IO){
      upFail(w, c, q, 422, up.kind == FML_K_PHOTO ? "La imagen est\xC3\xA1 da\xC3\xB1" "ada o incompleta" : "El v\xC3\xAD" "deo est\xC3\xA1 da\xC3\xB1" "ado o incompleto",
             up.tmpPath, up.thumbTmp, up.name, up.kind, true);
      w->uploading = false; return;
    }
    if(tr == FLEXTH_ERR_MEMORY){
      upFail(w, c, q, 503, "Memoria insuficiente en Flex OS para comprobar el archivo; vuelve a intentarlo",
             up.tmpPath, up.thumbTmp, up.name, up.kind, true);
      w->uploading = false; return;
    }
    if(tr == FLEXTH_ERR_UNSUP){
      // Se guarda como ORIGINAL: el P4 no lo abre (progresivo, AVI con otro
      // codec) y lo dice, pero el archivo es del usuario y se conserva.
      up.playable = false;
      if(up.fmt == FML_F_JPEG) up.fmt = FML_F_JPEG_PROG;
      if(up.fmt == FML_F_AVI_MJPEG) up.fmt = FML_F_AVI_OTHER;
    }
    if(ow > 0 && oh > 0 && ow < 65536 && oh < 65536){ up.w = (uint16_t)ow; up.h = (uint16_t)oh; }
    if(dur) up.durMs = dur;
  } else if(up.kind == FML_K_AUDIO && (up.fmt == FML_F_WAV_PCM || up.fmt == FML_F_WAV_ADPCM || up.fmt == FML_F_WAV_OTHER)){
    FsIo f = { w, w->fs.open(w->fs.ctx, up.tmpPath, false), 0, up.size };
    int wr = FLEXWAV_ERR_IO;
    FlexWavInfo wi;
    if(f.h){
      FlexMediaIO io2; io2.read = fsioRead; io2.seek = fsioSeek; io2.size = fsioSize; io2.ctx = &f;
      wr = flexWavParse(&io2, &wi);
      w->fs.close(w->fs.ctx, f.h);
    }
    if(wr == FLEXWAV_OK){ up.durMs = flexWavDurationMs(&wi); }
    else if(wr == FLEXWAV_ERR_CODEC){ up.playable = false; up.fmt = FML_F_WAV_OTHER; }
    else {
      upFail(w, c, q, 422, "El audio est\xC3\xA1 da\xC3\xB1" "ado o incompleto", up.tmpPath, NULL, up.name, up.kind, true);
      w->uploading = false; return;
    }
  }
  // ---- publicar ----
  uint32_t id = w->host.commit ? w->host.commit(w->host.ctx, &up, why, sizeof(why)) : 0;
  if(!id){
    upFail(w, c, q, 500, why[0] ? why : "No se pudo guardar en la biblioteca", up.tmpPath, up.thumbTmp, up.name, up.kind, true);
    w->uploading = false; return;
  }
  w->uploads++;
  char b[320], wesc[160] = "";
  const char* whyNot = up.playable ? NULL : flexMlWhyUnplayable(up.fmt);
  if(whyNot){ char e[140]; if(flexMlJsonStr(whyNot, e, sizeof(e))) snprintf(wesc, sizeof(wesc), ",\"why\":%s", e); }
  snprintf(b, sizeof(b), "{\"id\":%lu,\"p\":%d,\"thumb\":%d%s}", (unsigned long)id, up.playable ? 1 : 0,
           up.thumbTmp[0] ? 1 : 0, wesc);
  sendJson(c, 201, b, false);
  emit(w, FLEXWEB_EV_UP_DONE, up.kind, up.size, up.size, id, up.name, up.playable ? "" : whyNot);
  w->uploading = false;
}

// Miniatura que aporta el navegador para lo que el P4 no sabe abrir (un MP4,
// un HEIC guardado como original, la portada de una cancion). Se vuelve a
// generar con el decodificador y el codificador del sistema: lo que se guarda
// es SIEMPRE un JPEG cuadrado de 132 px hecho aqui, nunca los bytes tal cual.
static void handleThumbPost(FlexWebCtx* w, const FlexWebConn* c, Req* q, uint32_t id, uint8_t* io){
  q->keep = false;
  FlexMlRec r;
  if(!webRec(w, id, &r)){ sendErr(c, 404, "No existe", false); return; }
  if(r.flags & FML_R_LOCKED){ sendErr(c, 423, "Elemento protegido", false); return; }
  bool deviceMakesIt = (r.fmt == FML_F_JPEG || r.fmt == FML_F_AVI_MJPEG || r.fmt == FML_F_FXP);
  if(deviceMakesIt || ((r.flags & FML_R_THUMB) && !(r.flags & FML_R_EXT_THUMB))){
    sendErr(c, 409, "Flex OS ya genera la miniatura de este archivo", false); return;
  }
  if(q->r.chunked || q->r.contentLength <= 0 || q->r.contentLength > (long long)FML_LIMIT_THUMB){
    sendErr(c, 413, "Miniatura no v\xC3\xA1lida", false); return;
  }
  size_t len = (size_t)q->r.contentLength;
  uint8_t* buf = (uint8_t*)wAlloc(w, len);
  if(!buf){ sendErr(c, 503, "Memoria insuficiente", false); return; }
  size_t got = q->preN < len ? q->preN : len;
  memcpy(buf, q->pre, got);
  q->preUsed = got;
  while(got < len){
    int n = c->read(c->ctx, buf + got, len - got, FLEXWEB_BODY_TIMEOUT_MS);
    if(n <= 0){ wFree(w, buf); return; }
    got += (size_t)n;
  }
  char tmp[FML_PATH_MAX];
  tmpName(w, "tb", ".jpg", tmp, sizeof(tmp));
  FsOut o = { w, w->fs.open(w->fs.ctx, tmp, true), true };
  int rc = FLEXTH_ERR_WRITE;
  if(o.h){
    rc = flexThumbFromJpeg(buf, len, FLEXTH_SIDE, FLEXTH_QUALITY, fsOutWrite, &o, NULL, NULL, w->alloc, w->free);
    w->fs.close(w->fs.ctx, o.h);
    if(rc == FLEXTH_OK && !o.ok) rc = FLEXTH_ERR_WRITE;
  }
  wFree(w, buf);
  (void)io;
  if(rc != FLEXTH_OK){
    w->fs.remove(w->fs.ctx, tmp);
    sendErr(c, rc == FLEXTH_ERR_MEMORY ? 503 : 422, "La miniatura no es un JPEG v\xC3\xA1lido", false);
    return;
  }
  if(!w->host.setThumb || !w->host.setThumb(w->host.ctx, id, tmp)){
    w->fs.remove(w->fs.ctx, tmp);
    sendErr(c, 500, "No se pudo guardar la miniatura", false);
    return;
  }
  sendJson(c, 200, "{\"ok\":1}", false);
}

// -------------------------------------------------------------
//  DESPACHO
// -------------------------------------------------------------
static void route(FlexWebCtx* w, const FlexWebConn* c, Req* q, uint8_t* io){
  const char* p = q->r.path;
  const int m = q->r.method;
  if(!flexHttpHostOk(q->r.host, w->ip, w->port)){ q->keep = false; sendErr(c, 403, "Host no v\xC3\xA1lido", false); return; }
  q->head = (m == FLEXHTTP_M_HEAD);
  bool get = (m == FLEXHTTP_M_GET || m == FLEXHTTP_M_HEAD);
  bool post = (m == FLEXHTTP_M_POST);
  if(!get && !post){
    q->keep = false;
    sendErr(c, 405, "M\xC3\xA9todo no permitido", false, NULL, "Allow: GET, HEAD, POST\r\n");
    return;
  }
  // Un cuerpo en una peticion que no lo espera no se puede saltar sin
  // leerlo: se cierra despues de responder.
  if(get && (q->r.contentLength > 0 || q->r.chunked)) q->keep = false;

  // ---- publico ----
  if(get && (!strcmp(p, "/") || !strcmp(p, "/index.html"))){
    handleStatic(c, q, "text/html; charset=utf-8", FLEXWEB_INDEX_HTML, sizeof(FLEXWEB_INDEX_HTML) - 1); return;
  }
  if(get && !strcmp(p, "/app.js")){
    handleStatic(c, q, "text/javascript; charset=utf-8", FLEXWEB_APP_JS, sizeof(FLEXWEB_APP_JS) - 1); return;
  }
  if(get && !strcmp(p, "/app.css")){
    handleStatic(c, q, "text/css; charset=utf-8", FLEXWEB_APP_CSS, sizeof(FLEXWEB_APP_CSS) - 1); return;
  }
  if(strncmp(p, "/api/", 5)){ q->keep = sendErr(c, 404, "Aqu\xC3\xAD no hay nada", q->keep) && q->keep; return; }

  // Toda peticion que cambia algo tiene que venir de la app (X-Flex): un
  // formulario de otra pagina no puede poner esa cabecera.
  if(post && !q->r.xflex){ q->keep = false; sendErr(c, 403, "Petici\xC3\xB3n no permitida", false); return; }

  q->s = sessFind(w, q->r.session);
  q->owner = q->s ? sessOwner(w, q->s) : false;

  if(get && !strcmp(p, "/api/hello")){ handleHello(w, c, q); return; }
  if(post && !strcmp(p, "/api/pair")){ handlePair(w, c, q); return; }

  if(!q->s){
    if(post) q->keep = false;
    sendErr(c, 401, "Conecta primero con el c\xC3\xB3" "digo que aparece en Flex OS", q->keep, ",\"pair\":1");
    return;
  }
  uint32_t id;
  if(get && !strcmp(p, "/api/library")){ handleLibrary(w, c, q, io); return; }
  if(get && !strcmp(p, "/api/status")){ handleStatus(w, c, q); return; }
  if(get && !strcmp(p, "/api/zip")){ handleZip(w, c, q, io); return; }
  if(get && idFromPath(p, "/api/thumb/", &id)){ handleThumbGet(w, c, q, id, io); return; }
  if(get && idFromPath(p, "/api/file/", &id)){ handleFile(w, c, q, id, io); return; }
  if(post && !strcmp(p, "/api/upload")){ handleUpload(w, c, q, io); return; }
  if(post && idFromPath(p, "/api/thumb/", &id)){ handleThumbPost(w, c, q, id, io); return; }
  if(post && !strcmp(p, "/api/login")){ handleLogin(w, c, q); return; }
  if(post && !strcmp(p, "/api/logout")){ handleLogout(w, c, q, false); return; }
  if(post && !strcmp(p, "/api/unpair")){ handleLogout(w, c, q, true); return; }
  if(post) q->keep = false;
  sendErr(c, 404, "Aqu\xC3\xAD no hay nada", q->keep);
}

int flexWebServeConn(FlexWebCtx* w, const FlexWebConn* c, uint8_t* hdrBuf, uint8_t* io){
  if(!w || !c || !hdrBuf || !io) return 0;
  int served = 0;
  size_t have = 0;
  for(;;){
    Req q;
    memset(&q, 0, sizeof(q));
    FlexHttpReqEx* rq = &q.r;
    int st = have ? flexHttpParseEx((const char*)hdrBuf, have, rq) : 0;
    while(st == 0){
      if(have >= FLEXWEB_HDR_BUF - 1){ st = -1; break; }
      int n = 0;
      if(!have){
        // Sin un solo byte de la peticion: conexion recien abierta (los
        // navegadores abren alguna "por si acaso" y no mandan nada) u ociosa
        // entre peticiones (keep-alive). Se espera a trozos y, si otra
        // conexion aguarda turno, esta cede el sitio en vez de agotar el
        // plazo: un unico hilo sirviendo y ninguna pagina esperando segundos
        // por una conexion que el navegador no esta usando.
        // Primero se mira si ya llego algo: una peticion que esta en camino
        // nunca se pierde por ceder el turno.
        const uint32_t limit = served ? FLEXWEB_KEEP_IDLE_MS : FLEXWEB_HDR_TIMEOUT_MS;
        for(uint32_t waited = 0; n == 0 && waited < limit; waited += FLEXWEB_IDLE_SLICE_MS){
          n = c->read(c->ctx, hdrBuf, FLEXWEB_HDR_BUF - 1, FLEXWEB_IDLE_SLICE_MS);
          if(n == 0 && othersWaiting(w)) return served;
        }
      } else {
        n = c->read(c->ctx, hdrBuf + have, FLEXWEB_HDR_BUF - 1 - have, FLEXWEB_HDR_TIMEOUT_MS);
      }
      if(n <= 0){
        // Ociosa entre peticiones: se cierra sin ruido. A mitad de una
        // cabecera: 408 si el otro lado sigue ahi.
        if(have && n == 0) sendErr(c, 408, "La petici\xC3\xB3n no lleg\xC3\xB3 entera", false);
        return served;
      }
      have += (size_t)n;
      st = flexHttpParseEx((const char*)hdrBuf, have, rq);
    }
    if(st < 0){ sendErr(c, 400, "Petici\xC3\xB3n no v\xC3\xA1lida", false); return served; }
    served++;
    w->served++;
    q.pre = hdrBuf + rq->headerLen;
    q.preN = have - rq->headerLen;
    // Con alguien esperando, esta respuesta es la ultima de la conexion.
    q.keep = rq->keepAlive != 0 && !othersWaiting(w);
    route(w, c, &q, io);
    if(!q.keep) return served;
    // Lo que sobra en el buffer tras el cuerpo de ESTA peticion es el
    // principio de la siguiente (keep-alive con envio anticipado).
    size_t used = rq->headerLen + q.preUsed;
    size_t left = have > used ? have - used : 0;
    if(left) memmove(hdrBuf, hdrBuf + used, left);
    have = left;
    yieldHost(w);
  }
}
