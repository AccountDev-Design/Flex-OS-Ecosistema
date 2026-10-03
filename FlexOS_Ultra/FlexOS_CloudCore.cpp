// #############################################################
//  FLEX CLOUD · NUCLEO PORTABLE · implementacion
//  Ver FlexOS_CloudCore.h. Sin Arduino, sin red, sin flash.
// #############################################################
#include "FlexOS_CloudCore.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <cJSON.h>

#if defined(__has_include)
#  if __has_include(<mbedtls/sha256.h>)
#    define FCL_SHA_MBEDTLS 1
#  elif __has_include(<openssl/sha.h>)
#    define FCL_SHA_OPENSSL 1
#  endif
#endif
#if FCL_SHA_MBEDTLS
#  include "mbedtls/sha256.h"
#  include "mbedtls/version.h"
typedef mbedtls_sha256_context FclShaCtx;
#elif FCL_SHA_OPENSSL
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#  include <openssl/sha.h>
typedef SHA256_CTX FclShaCtx;
#else
#  error "FlexOS_CloudCore: falta un backend de SHA-256 (mbedTLS u OpenSSL)"
#endif

static_assert(sizeof(FclShaCtx) <= sizeof(((FclSha*)0)->opaque), "FclSha demasiado pequeno para el contexto SHA-256");

// =====================================================================
//  Utilidades de texto
// =====================================================================
void fclCopyUtf8(char* out, size_t cap, const char* in){
  if(!out || !cap) return;
  if(!in){ out[0] = 0; return; }
  size_t n = strlen(in);
  if(n >= cap){
    n = cap - 1;
    // No partir un caracter: retroceder hasta un byte que no sea de continuacion.
    while(n > 0 && ((unsigned char)in[n] & 0xC0) == 0x80) n--;
  }
  memcpy(out, in, n);
  out[n] = 0;
}

bool fclUrlEncode(const char* in, char* out, size_t cap){
  static const char hx[] = "0123456789ABCDEF";
  size_t o = 0;
  for(const unsigned char* p = (const unsigned char*)(in ? in : ""); *p; p++){
    bool safe = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
                *p == '-' || *p == '_' || *p == '.' || *p == '~';
    if(safe){ if(o + 1 >= cap) return false; out[o++] = (char)*p; }
    else { if(o + 3 >= cap) return false; out[o++] = '%'; out[o++] = hx[*p >> 4]; out[o++] = hx[*p & 15]; }
  }
  if(o >= cap) return false;
  out[o] = 0;
  return true;
}

bool fclJsonEscape(const char* in, char* out, size_t cap){
  size_t o = 0;
  for(const unsigned char* p = (const unsigned char*)(in ? in : ""); *p; p++){
    char esc = 0;
    if(*p == '"') esc = '"'; else if(*p == '\\') esc = '\\';
    else if(*p == '\n') esc = 'n'; else if(*p == '\r') esc = 'r'; else if(*p == '\t') esc = 't';
    if(esc){ if(o + 2 >= cap) return false; out[o++] = '\\'; out[o++] = esc; continue; }
    if(*p < 0x20){ if(o + 6 >= cap) return false; o += (size_t)snprintf(out + o, cap - o, "\\u%04x", *p); continue; }
    if(o + 1 >= cap) return false;
    out[o++] = (char)*p;
  }
  if(o >= cap) return false;
  out[o] = 0;
  return true;
}

void fclFmtBytes(uint64_t n, char* out, size_t cap){
  if(n < 1024){ snprintf(out, cap, "%u B", (unsigned)n); return; }
  static const char* U[] = { "KB", "MB", "GB", "TB" };
  double v = (double)n / 1024.0; int i = 0;
  while(v >= 1024.0 && i < 3){ v /= 1024.0; i++; }
  char num[24];
  if(v >= 100.0) snprintf(num, sizeof(num), "%.0f", v);
  else           snprintf(num, sizeof(num), "%.1f", v);
  for(char* c = num; *c; c++) if(*c == '.') *c = ',';            // como el resto del sistema
  size_t L = strlen(num);
  if(L > 2 && num[L - 2] == ',' && num[L - 1] == '0') num[L - 2] = 0;
  snprintf(out, cap, "%s %s", num, U[i]);
}

uint32_t fclBackoffMs(uint8_t failures){
  if(!failures) return 0;
  uint32_t ms = 2000u;
  for(uint8_t i = 1; i < failures && ms < 60000u; i++) ms *= 2u;
  return ms > 60000u ? 60000u : ms;
}

uint32_t fclChunkFor(uint64_t size){
  uint32_t chunk = 256u * 1024u;
  if(size > (uint64_t)chunk * FCL_PARTS_MAX){
    uint64_t c = (size + FCL_PARTS_MAX - 1) / FCL_PARTS_MAX;
    c = (c + 65535u) & ~(uint64_t)65535u;
    if(c > 64u * 1024u * 1024u) c = 64u * 1024u * 1024u;
    chunk = (uint32_t)c;
  }
  return chunk;
}

void fclLocalName(const char* cloudName, char* out, size_t cap){
  if(!out || cap < 2) return;
  const char* src = (cloudName && cloudName[0]) ? cloudName : "archivo";
  // Separar la extension (si es corta y razonable).
  const char* dot = strrchr(src, '.');
  size_t extLen = (dot && dot != src && strlen(dot) <= 10 && !strchr(dot, ' ')) ? strlen(dot) : 0;
  size_t stemLen = strlen(src) - extLen;
  char stem[FCL_NAME_MAX];
  size_t s = 0;
  for(size_t i = 0; i < stemLen && s + 1 < sizeof(stem); i++){
    unsigned char c = (unsigned char)src[i];
    if(c < 0x20 || c == 0x7F || strchr("/\\:*?\"<>|", c)) c = '_';
    stem[s++] = (char)c;
  }
  stem[s] = 0;
  // Sin espacios ni puntos al principio o al final (nombres invisibles o
  // que algunos sistemas recortan).
  char* b = stem;
  while(*b == ' ' || *b == '.') b++;
  size_t L = strlen(b);
  while(L && (b[L - 1] == ' ' || b[L - 1] == '.')) b[--L] = 0;
  if(!*b) b = (char*)"archivo";
  char ext[16] = "";
  for(size_t i = 0; i < extLen && i + 1 < sizeof(ext); i++){
    unsigned char c = (unsigned char)dot[i];
    ext[i] = (c < 0x20 || strchr("/\\:*?\"<>| ", c)) ? '_' : (char)c;
    ext[i + 1] = 0;
  }
  size_t room = cap - 1 - strlen(ext);
  if(room < 4){ ext[0] = 0; room = cap - 1; }
  char cut[FCL_NAME_MAX];
  fclCopyUtf8(cut, room + 1 < sizeof(cut) ? room + 1 : sizeof(cut), b);
  // Recortar puede dejar un espacio final: fuera tambien.
  L = strlen(cut);
  while(L && (cut[L - 1] == ' ' || cut[L - 1] == '.')) cut[--L] = 0;
  if(!L) snprintf(cut, sizeof(cut), "archivo");
  snprintf(out, cap, "%s%s", cut, ext);
}

// =====================================================================
//  JSON
// =====================================================================
static cJSON* parseRoot(const char* body, size_t len){
  if(!body || !len) return nullptr;
  return cJSON_ParseWithLength(body, len);
}
static const char* jstr(const cJSON* o, const char* k){
  const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
  return cJSON_IsString(v) && v->valuestring ? v->valuestring : nullptr;
}
static double jnum(const cJSON* o, const char* k, double d){
  const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
  return cJSON_IsNumber(v) ? v->valuedouble : d;
}
static uint64_t jbytes(const cJSON* o, const char* k){
  double d = jnum(o, k, 0);
  return (d > 0 && d < 9.0e15) ? (uint64_t)d : 0;
}
static bool jbool(const cJSON* o, const char* k){ return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, k)); }
static void jcopy(const cJSON* o, const char* k, char* out, size_t cap){ fclCopyUtf8(out, cap, jstr(o, k)); }

uint8_t fclKindOf(const char* kind){
  if(!kind) return FCL_K_OTHER;
  if(!strcmp(kind, "folder")) return FCL_K_FOLDER;
  if(!strcmp(kind, "photo")) return FCL_K_PHOTO;
  if(!strcmp(kind, "video")) return FCL_K_VIDEO;
  if(!strcmp(kind, "audio")) return FCL_K_AUDIO;
  if(!strcmp(kind, "document")) return FCL_K_DOCUMENT;
  if(!strcmp(kind, "archive")) return FCL_K_ARCHIVE;
  return FCL_K_OTHER;
}

// Un identificador de la API: solo [a-z0-9_]. Lo demas no se acepta (sale
// en rutas de la URL y en nombres de archivo temporales).
static bool safeId(const char* s){
  if(!s || !*s || strlen(s) >= FCL_ID_MAX) return false;
  for(const char* p = s; *p; p++) if(!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) return false;
  return true;
}
static bool hex64(const char* s){
  if(!s || strlen(s) != 64) return false;
  for(const char* p = s; *p; p++) if(!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) return false;
  return true;
}

static bool readItem(const cJSON* o, FclItem* it){
  if(!cJSON_IsObject(o)) return false;
  memset(it, 0, sizeof(*it));
  const char* type = jstr(o, "type");
  const char* id = jstr(o, "id");
  if(!type || !safeId(id)) return false;
  it->isFolder = !strcmp(type, "folder");
  if(!it->isFolder && strcmp(type, "file")) return false;
  snprintf(it->id, sizeof(it->id), "%s", id);
  const char* pid = jstr(o, "parentId");
  if(safeId(pid)) snprintf(it->parentId, sizeof(it->parentId), "%s", pid);
  jcopy(o, "name", it->name, sizeof(it->name));
  if(!it->name[0]) snprintf(it->name, sizeof(it->name), "(sin nombre)");
  it->updatedAt = (int64_t)jnum(o, "updatedAt", 0);
  it->deletedAt = (int64_t)jnum(o, "deletedAt", 0);
  if(it->isFolder){
    it->kind = FCL_K_FOLDER;
    double ic = jnum(o, "itemCount", 0);
    it->itemCount = ic > 0 ? (ic > 65535 ? 65535 : (uint16_t)ic) : 0;
    return true;
  }
  it->size = jbytes(o, "size");
  jcopy(o, "mime", it->mime, sizeof(it->mime));
  it->kind = fclKindOf(jstr(o, "kind"));
  if(it->kind == FCL_K_FOLDER) it->kind = FCL_K_OTHER;
  const char* sha = jstr(o, "sha256");
  if(hex64(sha)) snprintf(it->sha256, sizeof(it->sha256), "%s", sha);
  it->hasThumb = jbool(o, "hasThumbnail");
  const char* src = jstr(o, "source");
  it->fromDevice = src && !strcmp(src, "device");
  const cJSON* m = cJSON_GetObjectItemCaseSensitive(o, "metadata");
  if(cJSON_IsObject(m)){
    double w = jnum(m, "width", 0), h = jnum(m, "height", 0), d = jnum(m, "durationMs", 0);
    it->width = (w > 0 && w < 65536) ? (uint16_t)w : 0;
    it->height = (h > 0 && h < 65536) ? (uint16_t)h : 0;
    it->durationMs = (d > 0 && d < 4.0e9) ? (uint32_t)d : 0;
  }
  return true;
}

bool fclParseError(const char* body, size_t len, char* code, size_t codeCap, char* msg, size_t msgCap){
  if(code && codeCap) code[0] = 0;
  if(msg && msgCap) msg[0] = 0;
  cJSON* r = parseRoot(body, len);
  if(!r) return false;
  const cJSON* e = cJSON_GetObjectItemCaseSensitive(r, "error");
  bool ok = cJSON_IsObject(e) && jstr(e, "code");
  if(ok){
    if(code) fclCopyUtf8(code, codeCap, jstr(e, "code"));
    if(msg) fclCopyUtf8(msg, msgCap, jstr(e, "message"));
  }
  cJSON_Delete(r);
  return ok;
}

static bool readQuota(const cJSON* q, FclQuota* out){
  if(!cJSON_IsObject(q)) return false;
  memset(out, 0, sizeof(*out));
  out->totalBytes = jbytes(q, "totalBytes");
  if(!out->totalBytes) return false;
  out->usedBytes = jbytes(q, "usedBytes");
  out->reservedBytes = jbytes(q, "reservedBytes");
  out->trashBytes = jbytes(q, "trashBytes");
  out->availableBytes = jbytes(q, "availableBytes");
  uint64_t committed = out->usedBytes + out->reservedBytes;
  uint64_t pm = committed >= out->totalBytes ? 1000 : (committed * 1000 + out->totalBytes - 1) / out->totalBytes;
  if(committed > 0 && pm == 0) pm = 1;
  out->permille = (uint16_t)(pm > 1000 ? 1000 : pm);
  const char* st = jstr(q, "state");
  out->state = (st && !strcmp(st, "full")) ? FCL_Q_FULL : (st && !strcmp(st, "low")) ? FCL_Q_LOW : FCL_Q_OK;
  jcopy(q, "plan", out->plan, sizeof(out->plan));
  return true;
}

bool fclParseQuota(const char* body, size_t len, FclQuota* q){
  cJSON* r = parseRoot(body, len);
  if(!r) return false;
  bool ok = readQuota(cJSON_GetObjectItemCaseSensitive(r, "quota"), q);
  cJSON_Delete(r);
  return ok;
}

bool fclParseMe(const char* body, size_t len, char* address, size_t addrCap, char* displayName, size_t nameCap, FclQuota* q){
  cJSON* r = parseRoot(body, len);
  if(!r) return false;
  const cJSON* a = cJSON_GetObjectItemCaseSensitive(r, "account");
  bool ok = cJSON_IsObject(a) && jstr(a, "flexAddress");
  if(ok){
    if(address) jcopy(a, "flexAddress", address, addrCap);
    if(displayName) jcopy(a, "displayName", displayName, nameCap);
    if(q && !readQuota(cJSON_GetObjectItemCaseSensitive(r, "quota"), q)) memset(q, 0, sizeof(*q));
  }
  cJSON_Delete(r);
  return ok;
}

int fclParseList(const char* body, size_t len, FclItem* items, int cap, char* cursor, size_t cursorCap,
                 bool* more, FclCrumb* crumbs, int crumbCap, int* nCrumbs){
  if(cursor && cursorCap) cursor[0] = 0;
  if(more) *more = false;
  if(nCrumbs) *nCrumbs = 0;
  cJSON* r = parseRoot(body, len);
  if(!r) return -1;
  const cJSON* arr = cJSON_GetObjectItemCaseSensitive(r, "items");
  if(!cJSON_IsArray(arr)){ cJSON_Delete(r); return -1; }
  int n = 0;
  const cJSON* el = nullptr;
  cJSON_ArrayForEach(el, arr){
    if(n >= cap) break;
    if(readItem(el, &items[n])) n++;
  }
  const char* nc = jstr(r, "nextCursor");
  if(nc && cursor && strlen(nc) < cursorCap){
    bool clean = true;
    for(const char* p = nc; *p; p++) if(!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')) clean = false;
    if(clean){ snprintf(cursor, cursorCap, "%s", nc); if(more) *more = true; }
  }
  const cJSON* folder = cJSON_GetObjectItemCaseSensitive(r, "folder");
  const cJSON* path = cJSON_IsObject(folder) ? cJSON_GetObjectItemCaseSensitive(folder, "path") : nullptr;
  if(cJSON_IsArray(path) && crumbs && nCrumbs){
    int total = cJSON_GetArraySize(path);
    int skip = total > crumbCap ? total - crumbCap : 0;          // los niveles mas cercanos
    int i = 0, k = 0;
    cJSON_ArrayForEach(el, path){
      if(i++ < skip) continue;
      const char* id = jstr(el, "id");
      if(!safeId(id) || k >= crumbCap) continue;
      snprintf(crumbs[k].id, sizeof(crumbs[k].id), "%s", id);
      jcopy(el, "name", crumbs[k].name, sizeof(crumbs[k].name));
      k++;
    }
    *nCrumbs = k;
  }
  cJSON_Delete(r);
  return n;
}

bool fclParseItem(const char* body, size_t len, FclItem* it){
  cJSON* r = parseRoot(body, len);
  if(!r) return false;
  static const char* keys[] = { "file", "folder", "item" };
  bool ok = false;
  for(const char* k : keys){
    const cJSON* o = cJSON_GetObjectItemCaseSensitive(r, k);
    if(cJSON_IsObject(o) && readItem(o, it)){ ok = true; break; }
  }
  // {upload:{file:{...}}} (respuesta de completar)
  if(!ok){
    const cJSON* u = cJSON_GetObjectItemCaseSensitive(r, "upload");
    const cJSON* f = cJSON_IsObject(u) ? cJSON_GetObjectItemCaseSensitive(u, "file") : nullptr;
    ok = f && readItem(f, it);
  }
  cJSON_Delete(r);
  return ok;
}

bool fclUploadHasPart(const FclUpload* u, uint32_t n){
  if(!u || n == 0 || n > FCL_PARTS_MAX) return false;
  return (u->parts[(n - 1) / 32] >> ((n - 1) % 32)) & 1u;
}

bool fclParseUpload(const char* body, size_t len, FclUpload* u){
  cJSON* r = parseRoot(body, len);
  if(!r) return false;
  const cJSON* o = cJSON_GetObjectItemCaseSensitive(r, "upload");
  bool ok = false;
  if(cJSON_IsObject(o) && safeId(jstr(o, "uploadId"))){
    memset(u, 0, sizeof(*u));
    snprintf(u->uploadId, sizeof(u->uploadId), "%s", jstr(o, "uploadId"));
    jcopy(o, "state", u->state, sizeof(u->state));
    u->size = jbytes(o, "size");
    u->receivedBytes = jbytes(o, "receivedBytes");
    double cs = jnum(o, "chunkSize", 0), tp = jnum(o, "totalParts", 0);
    u->chunkSize = (cs > 0 && cs <= 256.0 * 1024 * 1024) ? (uint32_t)cs : 0;
    u->totalParts = (tp >= 0 && tp <= 1000000) ? (uint32_t)tp : 0;
    u->resumed = jbool(o, "resumed");
    const char* fid = jstr(o, "fileId");
    if(safeId(fid)) snprintf(u->fileId, sizeof(u->fileId), "%s", fid);
    const cJSON* parts = cJSON_GetObjectItemCaseSensitive(o, "receivedParts");
    const cJSON* el = nullptr;
    if(cJSON_IsArray(parts)) cJSON_ArrayForEach(el, parts){
      if(!cJSON_IsNumber(el)) continue;
      double d = el->valuedouble;
      if(d >= 1 && d <= FCL_PARTS_MAX && d == (double)(uint32_t)d){
        uint32_t n = (uint32_t)d;
        if(!fclUploadHasPart(u, n)){ u->parts[(n - 1) / 32] |= 1u << ((n - 1) % 32); u->receivedCount++; }
      }
    }
    const cJSON* f = cJSON_GetObjectItemCaseSensitive(o, "file");
    if(cJSON_IsObject(f)){
      const char* sha = jstr(f, "sha256");
      if(hex64(sha)) snprintf(u->sha256, sizeof(u->sha256), "%s", sha);
      const char* id = jstr(f, "id");
      if(safeId(id) && !u->fileId[0]) snprintf(u->fileId, sizeof(u->fileId), "%s", id);
    }
    // Un cuerpo que mienta (mas partes que el plan, tamanos imposibles) no se acepta.
    ok = u->chunkSize && u->totalParts <= FCL_PARTS_MAX && u->receivedCount <= u->totalParts &&
         (uint64_t)u->chunkSize * (u->totalParts ? u->totalParts - 1 : 0) <= u->size;
    if(u->size == 0) ok = u->chunkSize != 0;
  }
  cJSON_Delete(r);
  return ok;
}

const char* fclErrorText(const char* code){
  if(!code || !*code) return "No se pudo completar la operaci\xC3\xB3n";
  struct { const char* c; const char* t; } T[] = {
    { "network",            "Sin conexi\xC3\xB3n con Flex Cloud" },
    { "tls",                "No se pudo verificar el certificado del servidor" },
    { "no_wifi",            "Esperando conexi\xC3\xB3n Wi-Fi" },
    { "no_account",         "Vincula tu Flex Account para usar Flex Cloud" },
    { "auth_required",      "Vuelve a vincular tu Flex Account" },
    { "device_revoked",     "Este dispositivo ya no est\xC3\xA1 vinculado" },
    { "token_expired",      "La sesi\xC3\xB3n caduc\xC3\xB3: vuelve a vincular" },
    { "account_unavailable","Flex Account no responde ahora" },
    { "quota_exceeded",     "No queda espacio en tu Flex Cloud" },
    { "checksum_mismatch",  "Los datos llegaron da\xC3\xB1" "ados; se reintenta" },
    { "part_conflict",      "El archivo cambi\xC3\xB3 mientras se sub\xC3\xAD" "a" },
    { "local_changed",      "El archivo cambi\xC3\xB3 mientras se sub\xC3\xAD" "a" },
    { "name_conflict",      "Ya existe un elemento con ese nombre" },
    { "name_invalid",       "Ese nombre no es v\xC3\xA1lido" },
    { "not_found",          "Ya no existe en la nube" },
    { "file_too_large",     "El archivo es demasiado grande" },
    { "upload_expired",     "La subida caduc\xC3\xB3; empieza de nuevo" },
    { "rate_limited",       "Demasiadas solicitudes; espera un momento" },
    { "no_space_local",     "No hay espacio en el dispositivo" },
    { "local_missing",      "El archivo local ya no existe" },
    { "local_io",           "No se pudo leer el archivo local" },
    { "server",             "Flex Cloud no responde; se reintentar\xC3\xA1" },
    { "bad_response",       "Respuesta inesperada de Flex Cloud" },
    { "no_memory",          "No hay memoria libre ahora" },
    { "cancelled",          "Cancelado" },
    { "dest_changed",       "Flex Cloud cambi\xC3\xB3 de destino; vuelve a intentarlo" },
    { "no_phone",           "Empareja un tel\xC3\xA9" "fono para usar Flex Cloud" },
    { "phone_rejected",     "El tel\xC3\xA9" "fono ya no reconoce este Flex OS: vuelve a emparejarlo" },
  };
  for(auto& e : T) if(!strcmp(code, e.c)) return e.t;
  return "No se pudo completar la operaci\xC3\xB3n";
}

// Flex Cloud en el TELEFONO (Flex Storage): las mismas respuestas de la API,
// pero quien no contesta es el telefono y no hay cuenta que revincular.
const char* fclPhoneErrorText(const char* code){
  if(code && *code){
    struct { const char* c; const char* t; } T[] = {
      { "network",        "Tel\xC3\xA9" "fono desconectado" },
      { "server",         "El tel\xC3\xA9" "fono no responde; se reintentar\xC3\xA1" },
      { "no_account",     "Empareja un tel\xC3\xA9" "fono para usar Flex Cloud" },
      { "auth_required",  "Renovando la sesi\xC3\xB3n con el tel\xC3\xA9" "fono" },
      { "token_expired",  "Renovando la sesi\xC3\xB3n con el tel\xC3\xA9" "fono" },
      { "device_revoked", "El tel\xC3\xA9" "fono ya no reconoce este Flex OS: vuelve a emparejarlo" },
      { "quota_exceeded", "No queda espacio en Flex Cloud del tel\xC3\xA9" "fono" },
      { "bad_response",   "Respuesta inesperada del tel\xC3\xA9" "fono" },
      { "not_found",      "Ya no existe en el tel\xC3\xA9" "fono" },
      { "server_busy",    "El tel\xC3\xA9" "fono est\xC3\xA1 ocupado; se reintentar\xC3\xA1" },
    };
    for(auto& e : T) if(!strcmp(code, e.c)) return e.t;
  }
  return fclErrorText(code);
}

// =====================================================================
//  SHA-256
// =====================================================================
void fclShaStart(FclSha* s){
  FclShaCtx* c = (FclShaCtx*)s->opaque;
#if FCL_SHA_MBEDTLS
  mbedtls_sha256_init(c);
#  if MBEDTLS_VERSION_NUMBER >= 0x03000000
  mbedtls_sha256_starts(c, 0);
#  else
  mbedtls_sha256_starts_ret(c, 0);
#  endif
#else
  SHA256_Init(c);
#endif
}
void fclShaUpdate(FclSha* s, const void* data, size_t n){
  FclShaCtx* c = (FclShaCtx*)s->opaque;
#if FCL_SHA_MBEDTLS
#  if MBEDTLS_VERSION_NUMBER >= 0x03000000
  mbedtls_sha256_update(c, (const unsigned char*)data, n);
#  else
  mbedtls_sha256_update_ret(c, (const unsigned char*)data, n);
#  endif
#else
  SHA256_Update(c, data, n);
#endif
}
void fclShaFinishHex(FclSha* s, char out[FCL_SHA_HEX]){
  FclShaCtx* c = (FclShaCtx*)s->opaque;
  uint8_t d[32];
#if FCL_SHA_MBEDTLS
#  if MBEDTLS_VERSION_NUMBER >= 0x03000000
  mbedtls_sha256_finish(c, d);
#  else
  mbedtls_sha256_finish_ret(c, d);
#  endif
  mbedtls_sha256_free(c);
#else
  SHA256_Final(d, c);
#endif
  static const char h[] = "0123456789abcdef";
  for(int i = 0; i < 32; i++){ out[2 * i] = h[d[i] >> 4]; out[2 * i + 1] = h[d[i] & 15]; }
  out[64] = 0;
}
void fclShaHex(const void* data, size_t n, char out[FCL_SHA_HEX]){
  FclSha s; fclShaStart(&s); fclShaUpdate(&s, data, n); fclShaFinishHex(&s, out);
}

// =====================================================================
//  Diario de trabajos
// =====================================================================
static const char JOURNAL_MAGIC[4] = { 'F', 'C', 'J', '1' };
static const uint16_t JOURNAL_VERSION = 1;

static uint32_t crc32(const uint8_t* p, size_t n){
  uint32_t c = 0xFFFFFFFFu;
  for(size_t i = 0; i < n; i++){
    c ^= p[i];
    for(int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

struct W { uint8_t* b; size_t cap, n; bool ok;
  void u8(uint8_t v){ if(n + 1 > cap){ ok = false; return; } b[n++] = v; }
  void u32(uint32_t v){ for(int i = 0; i < 4; i++) u8((uint8_t)(v >> (8 * i))); }
  void u64(uint64_t v){ u32((uint32_t)v); u32((uint32_t)(v >> 32)); }
  void str(const char* s, size_t field){ size_t L = strnlen(s, field - 1); u8((uint8_t)(L & 0xFF)); u8((uint8_t)(L >> 8)); for(size_t i = 0; i < L; i++) u8((uint8_t)s[i]); }
};
struct R { const uint8_t* b; size_t len, n; bool ok;
  uint8_t u8(){ if(n + 1 > len){ ok = false; return 0; } return b[n++]; }
  uint32_t u32(){ uint32_t v = 0; for(int i = 0; i < 4; i++) v |= (uint32_t)u8() << (8 * i); return v; }
  uint64_t u64(){ uint64_t lo = u32(); return lo | ((uint64_t)u32() << 32); }
  void str(char* out, size_t field){
    size_t L = u8();
    L |= (size_t)u8() << 8;
    if(L >= field){ ok = false; out[0] = 0; return; }
    for(size_t i = 0; i < L; i++) out[i] = (char)u8();
    out[L] = 0;
    if(memchr(out, 0, L)) ok = false;
  }
};

void fclJournalInit(FclJournal* j){ memset(j, 0, sizeof(*j)); j->nextId = 1; }

static void encodeJob(W& w, const FclJob& x){
  w.u8(x.state); w.u8(x.type); w.u8(x.flags); w.u8(x.attempts);
  w.u32(x.id); w.u32(x.mlId); w.u64(x.size);
  w.str(x.localPath, sizeof(x.localPath)); w.str(x.name, sizeof(x.name)); w.str(x.parentId, sizeof(x.parentId));
  w.str(x.remoteId, sizeof(x.remoteId)); w.str(x.fileId, sizeof(x.fileId)); w.str(x.sha256, sizeof(x.sha256));
  w.str(x.error, sizeof(x.error));
}
static bool decodeJob(R& r, FclJob& x){
  memset(&x, 0, sizeof(x));
  x.state = r.u8(); x.type = r.u8(); x.flags = r.u8(); x.attempts = r.u8();
  x.id = r.u32(); x.mlId = r.u32(); x.size = r.u64();
  r.str(x.localPath, sizeof(x.localPath)); r.str(x.name, sizeof(x.name)); r.str(x.parentId, sizeof(x.parentId));
  r.str(x.remoteId, sizeof(x.remoteId)); r.str(x.fileId, sizeof(x.fileId)); r.str(x.sha256, sizeof(x.sha256));
  r.str(x.error, sizeof(x.error));
  return r.ok && x.state <= FCL_JOB_CANCELLED && (x.state == FCL_JOB_FREE || x.type == FCL_JOB_UPLOAD || x.type == FCL_JOB_DOWNLOAD);
}

size_t fclJournalMaxBytes(){
  // cabecera + por registro: longitud(2) + campos + crc(4)
  size_t rec = 4 + 4 + 4 + 8 + 2 * 7 + FCL_PATH_MAX + FCL_NAME_MAX + FCL_ID_MAX * 3 + FCL_SHA_HEX + FCL_MSG_MAX;
  return 4 + 2 + 2 + 4 + FCL_JOBS_MAX * (2 + rec + 4);
}

size_t fclJournalEncode(const FclJournal* j, uint8_t* buf, size_t cap){
  W w{ buf, cap, 0, true };
  for(char c : JOURNAL_MAGIC) w.u8((uint8_t)c);
  w.u8(JOURNAL_VERSION & 0xFF); w.u8(JOURNAL_VERSION >> 8);
  w.u8(FCL_JOBS_MAX); w.u8(0);
  w.u32(j->nextId);
  for(int i = 0; i < FCL_JOBS_MAX && w.ok; i++){
    size_t lenAt = w.n;
    w.u8(0); w.u8(0);
    size_t start = w.n;
    encodeJob(w, j->jobs[i]);
    if(!w.ok) break;
    size_t L = w.n - start;
    buf[lenAt] = (uint8_t)(L & 0xFF); buf[lenAt + 1] = (uint8_t)(L >> 8);
    w.u32(crc32(buf + start, L));
  }
  return w.ok ? w.n : 0;
}

bool fclJournalDecode(FclJournal* j, const uint8_t* buf, size_t len, bool* damaged){
  fclJournalInit(j);
  if(damaged) *damaged = false;
  if(!buf || len < 12 || memcmp(buf, JOURNAL_MAGIC, 4)){ if(damaged && len) *damaged = true; return len == 0; }
  R r{ buf, len, 4, true };
  // Un byte por sentencia: en `a() | (a() << 8)` el orden de las dos
  // llamadas no esta definido en C++, y GCC lee primero la de la derecha.
  uint16_t ver = r.u8();
  ver = (uint16_t)(ver | (r.u8() << 8));
  uint8_t count = r.u8(); r.u8();
  uint32_t nextId = r.u32();
  if(ver != JOURNAL_VERSION || count != FCL_JOBS_MAX){ if(damaged) *damaged = true; return true; }
  j->nextId = nextId ? nextId : 1;
  for(int i = 0; i < FCL_JOBS_MAX; i++){
    size_t L = r.u8(); L |= (size_t)r.u8() << 8;
    if(!r.ok || r.n + L + 4 > len){ if(damaged) *damaged = true; break; }
    const uint8_t* rec = buf + r.n;
    uint32_t want = 0;
    for(int k = 0; k < 4; k++) want |= (uint32_t)buf[r.n + L + k] << (8 * k);
    if(crc32(rec, L) != want){ if(damaged) *damaged = true; r.n += L + 4; continue; }
    R rr{ rec, L, 0, true };
    FclJob x;
    if(decodeJob(rr, x) && rr.n == L) j->jobs[i] = x;
    else if(damaged) *damaged = true;
    r.n += L + 4;
  }
  // Un trabajo a medias de un arranque anterior vuelve a la cola: la
  // reanudacion se hace con el servidor (las partes recibidas las sabe el).
  for(int i = 0; i < FCL_JOBS_MAX; i++) if(j->jobs[i].state == FCL_JOB_ACTIVE) j->jobs[i].state = FCL_JOB_QUEUED;
  for(int i = 0; i < FCL_JOBS_MAX; i++) if(j->jobs[i].id >= j->nextId) j->nextId = j->jobs[i].id + 1;
  return true;
}

FclJob* fclJournalAlloc(FclJournal* j){
  FclJob* best = nullptr;
  for(int i = 0; i < FCL_JOBS_MAX; i++){
    FclJob* x = &j->jobs[i];
    if(x->state == FCL_JOB_FREE){ best = x; break; }
    bool finished = (x->state == FCL_JOB_DONE && (x->flags & FCL_JF_DELIVERED)) || x->state == FCL_JOB_FAILED ||
                    (x->state == FCL_JOB_CANCELLED && !(x->flags & FCL_JF_ABORT));
    if(finished && (!best || x->id < best->id)) best = x;
  }
  if(!best) return nullptr;
  memset(best, 0, sizeof(*best));
  best->id = j->nextId++;
  return best;
}

FclJob* fclJournalFind(FclJournal* j, uint32_t id){
  for(int i = 0; i < FCL_JOBS_MAX; i++) if(j->jobs[i].state != FCL_JOB_FREE && j->jobs[i].id == id) return &j->jobs[i];
  return nullptr;
}

FclJob* fclJournalNext(FclJournal* j){
  FclJob* best = nullptr;
  for(int i = 0; i < FCL_JOBS_MAX; i++){
    FclJob* x = &j->jobs[i];
    if((x->state == FCL_JOB_QUEUED || x->state == FCL_JOB_ACTIVE) && (!best || x->id < best->id)) best = x;
  }
  return best;
}

int fclJournalCount(const FclJournal* j, uint8_t state){
  int n = 0;
  for(int i = 0; i < FCL_JOBS_MAX; i++) if(j->jobs[i].state == state) n++;
  return n;
}

// =====================================================================
//  Cache de bloques
// =====================================================================
void fclCacheInit(FclCache* c, uint8_t* arena, uint32_t blockSize, uint16_t nBlocks, uint32_t fileSize){
  memset(c, 0, sizeof(*c));
  c->arena = arena;
  c->blockSize = blockSize ? blockSize : 65536u;
  c->nBlocks = nBlocks > FCL_CACHE_BLOCKS_MAX ? FCL_CACHE_BLOCKS_MAX : nBlocks;
  c->fileSize = fileSize;
  // Por delante caben todos los bloques menos dos (uno para lo que se pida
  // fuera de orden y otro para lo que queda justo detras).
  uint32_t n = c->nBlocks > 2 ? c->nBlocks - 2u : 1u;
  c->ahead = n * c->blockSize;
  c->behind = c->blockSize;
}

static int findBlock(const FclCache* c, uint32_t boff, uint8_t state){
  for(int i = 0; i < c->nBlocks; i++)
    if(c->blocks[i].state == state && c->blocks[i].off == boff) return i;
  return -1;
}
static bool present(const FclCache* c, uint32_t boff){
  for(int i = 0; i < c->nBlocks; i++)
    if(c->blocks[i].state != FCL_B_EMPTY && c->blocks[i].off == boff) return true;
  return false;
}

bool fclCacheReady(const FclCache* c, uint32_t off, uint32_t len){
  if(!c->arena || off >= c->fileSize) return off >= c->fileSize && c->arena;
  if(len == 0) return true;
  uint64_t end = (uint64_t)off + len;
  if(end > c->fileSize) end = c->fileSize;
  for(uint64_t b = (off / c->blockSize) * (uint64_t)c->blockSize; b < end; b += c->blockSize)
    if(findBlock(c, (uint32_t)b, FCL_B_READY) < 0) return false;
  return true;
}

int fclCacheRead(FclCache* c, uint32_t off, void* buf, uint32_t n){
  if(!c->arena) return -1;
  if(off >= c->fileSize) return 0;
  if(n == 0) return 0;
  if((uint64_t)off + n > c->fileSize) n = c->fileSize - off;
  c->readPos = off;
  // Primero se comprueba que esta TODO: nunca se entrega media lectura.
  uint32_t end = off + n;
  for(uint32_t b = (off / c->blockSize) * c->blockSize; b < end; b += c->blockSize){
    if(findBlock(c, b, FCL_B_READY) < 0){
      c->wantSet = true; c->wantOff = b; c->misses++;
      return -1;
    }
    if(b > 0xFFFFFFFFu - c->blockSize) break;
  }
  uint8_t* dst = (uint8_t*)buf;
  uint32_t p = off;
  while(p < end){
    uint32_t b = (p / c->blockSize) * c->blockSize;
    int s = findBlock(c, b, FCL_B_READY);
    uint32_t inBlk = p - b;
    uint32_t take = c->blocks[s].len > inBlk ? c->blocks[s].len - inBlk : 0;
    if(take > end - p) take = end - p;
    if(!take){ c->wantSet = true; c->wantOff = b; c->misses++; return -1; }   // bloque corto: no deberia pasar
    memcpy(dst, c->arena + (size_t)s * c->blockSize + inBlk, take);
    dst += take; p += take;
    c->blocks[s].lru = ++c->tick;
  }
  c->hits++;
  return (int)n;
}

void fclCachePin(FclCache* c, uint32_t off, uint32_t len){ c->pinSet = len > 0; c->pinOff = off; c->pinLen = len; }
void fclCacheUnpin(FclCache* c){ c->pinSet = false; }
void fclCacheSeek(FclCache* c, uint32_t pos){ c->readPos = pos; c->wantSet = false; }

static bool inPin(const FclCache* c, uint32_t boff){
  if(!c->pinSet) return false;
  uint64_t pe = (uint64_t)c->pinOff + c->pinLen;
  return (uint64_t)boff + c->blockSize > c->pinOff && boff < pe;
}
static bool protectedBlock(const FclCache* c, uint32_t boff){
  if(inPin(c, boff)) return true;
  uint64_t lo = c->readPos > c->behind ? c->readPos - c->behind : 0;
  uint64_t hi = (uint64_t)c->readPos + c->ahead;
  return (uint64_t)boff + c->blockSize > lo && boff < hi;
}

int fclCacheNextFetch(FclCache* c, uint32_t* off, uint32_t* len){
  if(!c->arena || !c->fileSize) return -1;
  const uint32_t bs = c->blockSize;
  bool urgent = false;
  uint32_t target = 0;
  bool found = false;
  // 1) Lo que el lector pidio y no habia.
  if(c->wantSet && !present(c, c->wantOff) && c->wantOff < c->fileSize){ target = c->wantOff; found = true; urgent = true; }
  // 2) Un rango que hace falta entero (indice del AVI para buscar).
  if(!found && c->pinSet){
    uint64_t pe = (uint64_t)c->pinOff + c->pinLen;
    if(pe > c->fileSize) pe = c->fileSize;
    for(uint64_t b = (c->pinOff / bs) * (uint64_t)bs; b < pe; b += bs)
      if(!present(c, (uint32_t)b)){ target = (uint32_t)b; found = true; urgent = true; break; }
  }
  // 3) Por delante de la lectura.
  if(!found){
    uint64_t hi = (uint64_t)c->readPos + c->ahead;
    if(hi > c->fileSize) hi = c->fileSize;
    for(uint64_t b = (c->readPos / bs) * (uint64_t)bs; b < hi; b += bs)
      if(!present(c, (uint32_t)b)){ target = (uint32_t)b; found = true; break; }
  }
  if(!found) return -1;
  // Hueco: uno vacio; si no, el menos util que no este protegido.
  int slot = -1;
  for(int i = 0; i < c->nBlocks && slot < 0; i++) if(c->blocks[i].state == FCL_B_EMPTY) slot = i;
  if(slot < 0){
    uint32_t bestLru = 0xFFFFFFFFu;
    for(int i = 0; i < c->nBlocks; i++){
      const FclBlock& b = c->blocks[i];
      if(b.state != FCL_B_READY || protectedBlock(c, b.off)) continue;
      if(b.lru < bestLru){ bestLru = b.lru; slot = i; }
    }
  }
  if(slot < 0 && urgent){
    // Lo que se necesita YA manda sobre la lectura adelantada: se sacrifica
    // el bloque listo mas lejano por delante (nunca uno fijado).
    uint32_t far = 0;
    for(int i = 0; i < c->nBlocks; i++){
      const FclBlock& b = c->blocks[i];
      if(b.state != FCL_B_READY || inPin(c, b.off)) continue;
      uint32_t d = b.off > c->readPos ? b.off - c->readPos : c->readPos - b.off;
      if(d >= far){ far = d; slot = i; }
    }
  }
  if(slot < 0) return -1;
  FclBlock& b = c->blocks[slot];
  b.state = FCL_B_LOADING;
  b.off = target;
  b.len = (c->fileSize - target) < bs ? c->fileSize - target : bs;
  b.lru = ++c->tick;
  if(c->wantSet && c->wantOff == target) c->wantSet = false;
  *off = b.off; *len = b.len;
  return slot;
}

uint8_t* fclCacheSlot(FclCache* c, int slot){
  if(!c->arena || slot < 0 || slot >= c->nBlocks) return nullptr;
  return c->arena + (size_t)slot * c->blockSize;
}
void fclCacheCommit(FclCache* c, int slot){
  if(slot < 0 || slot >= c->nBlocks || c->blocks[slot].state != FCL_B_LOADING) return;
  c->blocks[slot].state = FCL_B_READY;
  c->blocks[slot].lru = ++c->tick;
  c->fetched++;
}
void fclCacheAbort(FclCache* c, int slot){
  if(slot < 0 || slot >= c->nBlocks) return;
  c->blocks[slot].state = FCL_B_EMPTY;
}

uint32_t fclCacheContiguous(const FclCache* c, uint32_t pos){
  if(!c->arena || pos >= c->fileSize) return 0;
  uint32_t b = (pos / c->blockSize) * c->blockSize, total = 0;
  for(;;){
    int s = findBlock(c, b, FCL_B_READY);
    if(s < 0) break;
    uint32_t end = b + c->blocks[s].len;
    total = end - pos;
    if(end >= c->fileSize || c->blocks[s].len < c->blockSize) break;
    b += c->blockSize;
  }
  return total;
}

#if FCL_SHA_OPENSSL
#  pragma GCC diagnostic pop
#endif

// =====================================================================
//  TEXTOS Y DECISIONES DE LA INTERFAZ
// =====================================================================
void fclQuotaLine(const FclQuota* q, char* out, size_t cap){
  if(!out || !cap) return;
  if(!q || !q->totalBytes){ snprintf(out, cap, "Espacio no disponible"); return; }
  char u[24], t[24];
  fclFmtBytes(q->usedBytes + q->reservedBytes, u, sizeof(u));
  fclFmtBytes(q->totalBytes, t, sizeof(t));
  unsigned pct = (q->permille + 5u) / 10u;
  if(pct == 0 && q->permille > 0) pct = 1;                      // algo ocupado nunca es "0 %"
  if(pct > 100) pct = 100;
  snprintf(out, cap, "%s de %s \xC2\xB7 %u %%", u, t, pct);
}

void fclQuotaHint(const FclQuota* q, char* out, size_t cap){
  if(!out || !cap) return;
  if(!q || !q->totalBytes){ out[0] = 0; return; }
  uint64_t taken = q->usedBytes + q->reservedBytes;
  uint64_t left = q->totalBytes > taken ? q->totalBytes - taken : 0;
  // Lo que queda DE VERDAD: si el servidor dice que hay menos disponible (el
  // telefono de Flex Storage con menos libre que lo que falta de la cuota),
  // manda eso. 0 = el servidor no lo dio (o esta lleno: lo dice `state`).
  if(q->availableBytes && q->availableBytes < left) left = q->availableBytes;
  char l[24]; fclFmtBytes(left, l, sizeof(l));
  if(q->state == FCL_Q_FULL || !left) snprintf(out, cap, "Flex Cloud est\xC3\xA1 lleno");
  else if(q->state == FCL_Q_LOW) snprintf(out, cap, "Espacio casi lleno: quedan %s", l);
  else snprintf(out, cap, "Quedan %s", l);
}

static bool endsWithCi(const char* s, const char* ext){
  size_t a = strlen(s), b = strlen(ext);
  if(a < b) return false;
  for(size_t i = 0; i < b; i++){
    char c = s[a - b + i];
    if(c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if(c != ext[i]) return false;
  }
  return true;
}

int fclOpenAction(const FclItem* it, const char** why){
  if(why) *why = nullptr;
  if(!it) return FCL_OPEN_MENU;
  if(it->isFolder) return FCL_OPEN_FOLDER;
  const char* n = it->name;
  if(endsWithCi(n, ".jpg") || endsWithCi(n, ".jpeg")){
    if(it->size > 8ull * 1024 * 1024){ if(why) *why = "Es demasiado grande para abrirla aqu\xC3\xAD: desc\xC3\xA1rgala o \xC3\xA1" "brela en la web"; return FCL_OPEN_MENU; }
    return FCL_OPEN_PHOTO;
  }
  if(endsWithCi(n, ".avi")){
    if(it->size > 0xFFFFFFF0ull){ if(why) *why = "Es demasiado grande para este dispositivo"; return FCL_OPEN_MENU; }
    return FCL_OPEN_STREAM;
  }
  if(it->kind == FCL_K_PHOTO){ if(why) *why = "Este formato de foto se ve en la web de Flex Cloud"; return FCL_OPEN_MENU; }
  if(it->kind == FCL_K_VIDEO){ if(why) *why = "Este v\xC3\xAD" "deo no se reproduce en este dispositivo (solo AVI MJPEG). \xC3\x81" "brelo en la web"; return FCL_OPEN_MENU; }
  if(it->kind == FCL_K_AUDIO){ if(why) *why = "Desc\xC3\xA1rgalo para escucharlo en M\xC3\xBAsica"; return FCL_OPEN_MENU; }
  return FCL_OPEN_MENU;
}

// Dias desde 1970-01-01 -> fecha civil (algoritmo de H. Hinnant, sin tablas).
static void civilFromDays(int64_t z, int& y, unsigned& m, unsigned& d){
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t yy = (int64_t)yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y = (int)(yy + (m <= 2));
}

void fclFmtDate(int64_t ms, char* out, size_t cap){
  if(!out || !cap) return;
  out[0] = 0;
  if(ms <= 0) return;
  int64_t days = ms / 86400000LL;
  int y; unsigned m, d;
  civilFromDays(days, y, m, d);
  snprintf(out, cap, "%02u/%02u/%04d", d, m, y);
}

void fclItemSub(const FclItem* it, char* out, size_t cap){
  if(!out || !cap) return;
  if(!it){ out[0] = 0; return; }
  if(it->isFolder){ snprintf(out, cap, "Carpeta"); return; }
  char sz[24], dt[16];
  fclFmtBytes(it->size, sz, sizeof(sz));
  fclFmtDate(it->updatedAt, dt, sizeof(dt));
  if(dt[0]) snprintf(out, cap, "%s \xC2\xB7 %s", sz, dt);
  else snprintf(out, cap, "%s", sz);
}

void fclXferLine(uint8_t phase, uint8_t type, uint64_t done, uint64_t size, uint32_t bytesPerSec,
                 uint32_t retryInMs, const char* error, char* out, size_t cap){
  if(!out || !cap) return;
  bool up = type == FCL_JOB_UPLOAD;
  char d[24], t[24], r[24];
  fclFmtBytes(done > size ? size : done, d, sizeof(d));
  fclFmtBytes(size, t, sizeof(t));
  switch(phase){
    case FCX_QUEUED:      snprintf(out, cap, "En cola \xC2\xB7 %s", t); break;
    case FCX_PREPARING:   snprintf(out, cap, up ? "Comprobando el archivo..." : "Preparando..."); break;
    case FCX_RUNNING:
      if(bytesPerSec){ fclFmtBytes(bytesPerSec, r, sizeof(r)); snprintf(out, cap, "%s \xC2\xB7 %s de %s \xC2\xB7 %s/s", up ? "Subiendo" : "Descargando", d, t, r); }
      else snprintf(out, cap, "%s \xC2\xB7 %s de %s", up ? "Subiendo" : "Descargando", d, t);
      break;
    case FCX_VERIFYING:   snprintf(out, cap, "Verificando integridad..."); break;
    case FCX_WAITING_NET:
      // Con un motivo (la cuenta ya no sirve) se dice ESE, no un "esperando
      // conexion" que nunca se va a cumplir.
      if(error && error[0]) snprintf(out, cap, "%s", error);
      else snprintf(out, cap, "Esperando conexi\xC3\xB3n \xC2\xB7 %s de %s", d, t);
      break;
    case FCX_RETRYING: {
      uint32_t s = (retryInMs + 999u) / 1000u;
      if(s) snprintf(out, cap, "Reintento en %lu s \xC2\xB7 %s de %s", (unsigned long)s, d, t);
      else snprintf(out, cap, "Reintentando \xC2\xB7 %s de %s", d, t);
      break;
    }
    case FCX_DONE:        snprintf(out, cap, "%s \xC2\xB7 %s", up ? "Subido" : "Descargado", t); break;
    case FCX_CANCELLED:   snprintf(out, cap, "Cancelado"); break;
    default:              snprintf(out, cap, "%s", error && error[0] ? error : "No se pudo completar"); break;
  }
}
