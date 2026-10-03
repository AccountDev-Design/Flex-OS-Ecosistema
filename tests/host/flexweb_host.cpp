// #############################################################
// ##  flexweb_host · Flex Web Server en el PC, con un socket de verdad
// #############################################################
//
// El nucleo del servidor (FlexOS_MediaWeb, el MISMO codigo que va al P4)
// detras de un socket TCP real y con el sistema de archivos en una carpeta.
// Existe para tests/web/e2e.test.js: un Chromium de verdad empareja, sube,
// convierte, descarga y desbloquea contra el, y la prueba mira despues lo
// que quedo en el disco con los analizadores del firmware.
//
//   flexweb_host <carpeta> [bytes_de_disco]
//     -> imprime {"port":N,"code":"123456"} y atiende hasta que se cierra stdin
//   Ordenes por stdin (una por linea):
//     dump         el catalogo en JSON (como lo ve el propietario), una linea
//     lock <id>    bloquea un elemento CON EL CODIGO DEL P4 (FlexOS_MediaStore:
//     unlock <id>  el archivo y su miniatura se mueven a/desde Protegido)
//     scan         reconcilia el catalogo con la carpeta, como al arrancar
//     jobs         hace las miniaturas pendientes, como la tarea de fondo
//   Los eventos del servidor (tarjetas de transferencia del P4) salen por
//   stderr, uno por linea, en JSON.
//
//   El catalogo es el MISMO almacen que usa la placa (FlexOS_MediaStore):
//   publicar una subida, poner una miniatura o bloquear no tiene aqui una
//   copia propia que pudiera portarse distinto.
//
//   FLEX STORAGE (opcional):
//     flexweb_host <carpeta> [bytes] --storage [--phone-port N --phone-key HEX64]
//   --storage: empareja telefonos con el nucleo de VERDAD (FlexOS_StorageCore,
//   el mismo de la placa). La "pantalla del P4" son ordenes por stdin:
//     approve | deny     decide el emparejamiento que espera
//   y por stderr sale {"pairWaiting":...,"sas":"123456"} / {"paired":...}.
//   --phone-port/--phone-key: un telefono YA emparejado en 127.0.0.1:N (el
//   servidor Kotlin de pruebas, DevServer.kt) para la pasarela /api/cloud.
//   Las copias Flex OS <-> Flex Cloud (POST /api/fs/xfer) son aqui un DOBLE DE
//   PRUEBAS: se apunta lo que pide la web (stderr {"xferReq":...}) y la prueba
//   mueve el progreso por stdin:
//     xjob <id> <fase> <bytes>   fase como FCX_* (6 hecho, 7 fallo, 8 cancelado)
//   En la placa las hace el gestor de la nube (FlexOS_Cloud), probado contra el
//   telefono de verdad en phone_e2e.
#include "../../FlexOS_Ultra/FlexOS_MediaWeb.h"
#include "../../FlexOS_Ultra/FlexOS_MediaStore.h"
#include "../../FlexOS_Ultra/FlexOS_StorageCore.h"
#include "e2e_http.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>
#include <dirent.h>

static std::string g_root;
static uint32_t g_total = 10u * 1024u * 1024u;
static FlexMlRec g_store[FML_CAP];
static FlexMediaStore g_ms;
static int g_listen = -1;

static std::string real(const char* p){ return g_root + p; }
static void mkdirs(const std::string& p){
  for(size_t i = 1; i < p.size(); i++) if(p[i] == '/'){ std::string d = p.substr(0, i); mkdir(d.c_str(), 0755); }
  mkdir(p.c_str(), 0755);
}
static uint64_t duUsed(const std::string& dir);

// ---------------- sistema de archivos (una carpeta del PC) ----------------
static void* fsOpen(void*, const char* p, bool wr){
  std::string r = real(p);
  if(wr){ size_t s = r.rfind('/'); if(s != std::string::npos) mkdirs(r.substr(0, s)); }
  return std::fopen(r.c_str(), wr ? "wb" : "rb");
}
static int fsRead(void*, void* h, uint8_t* b, size_t n){ size_t k = std::fread(b, 1, n, (FILE*)h); return k ? (int)k : (std::ferror((FILE*)h) ? -1 : 0); }
static bool fsWrite(void*, void* h, const uint8_t* b, size_t n){
  if(duUsed(g_root) + n > g_total) return false;                  // disco "lleno" como la particion del P4
  return std::fwrite(b, 1, n, (FILE*)h) == n;
}
static bool fsSeek(void*, void* h, uint32_t off){ return std::fseek((FILE*)h, (long)off, SEEK_SET) == 0; }
static void fsClose(void*, void* h){ std::fclose((FILE*)h); }
static uint32_t fsSize(void*, const char* p){ struct stat st; return stat(real(p).c_str(), &st) == 0 && S_ISREG(st.st_mode) ? (uint32_t)st.st_size : 0; }
static bool fsRemove(void*, const char* p){ return unlink(real(p).c_str()) == 0; }
static uint64_t duUsed(const std::string& dir){
  uint64_t n = 0;
  DIR* d = opendir(dir.c_str());
  if(!d) return 0;
  while(dirent* e = readdir(d)){
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    std::string p = dir + "/" + e->d_name;
    struct stat st;
    if(stat(p.c_str(), &st)) continue;
    if(S_ISDIR(st.st_mode)) n += duUsed(p); else n += ((uint64_t)st.st_size + 4095) / 4096 * 4096;
  }
  closedir(d);
  return n;
}
static uint32_t fsFree(void*){ uint64_t u = duUsed(g_root); return u >= g_total ? 0 : (uint32_t)(g_total - u); }
static uint32_t fsTotal(void*){ return g_total; }

// ---------------- lo que el almacen necesita ademas ----------------
static bool fsExists(void*, const char* p){ struct stat st; return stat(real(p).c_str(), &st) == 0; }
static bool fsMove(void*, const char* a, const char* b){
  std::string ra = real(a), rb = real(b);
  struct stat st;
  if(stat(ra.c_str(), &st) || !stat(rb.c_str(), &st)) return false;       // no existe / no se pisa
  size_t sl = rb.rfind('/'); if(sl != std::string::npos) mkdirs(rb.substr(0, sl));
  return std::rename(ra.c_str(), rb.c_str()) == 0;
}
static bool fsMkdir(void*, const char* p){ mkdirs(real(p)); return true; }
static int fsList(void*, const char* dir, FlexMsEntry* out, int maxn, int skip){
  DIR* d = opendir(real(dir).c_str());
  if(!d) return -1;
  std::vector<std::string> names;
  while(dirent* e = readdir(d)) if(std::strcmp(e->d_name, ".") && std::strcmp(e->d_name, "..")) names.push_back(e->d_name);
  closedir(d);
  std::sort(names.begin(), names.end());                                // orden estable, como LittleFS
  int n = 0;
  for(size_t i = (size_t)skip; i < names.size() && n < maxn; i++){
    struct stat st;
    std::string rp = real(dir) + "/" + names[i];
    if(stat(rp.c_str(), &st)) continue;
    std::memset(&out[n], 0, sizeof(out[n]));
    std::snprintf(out[n].name, sizeof(out[n].name), "%s", names[i].c_str());
    out[n].dir = S_ISDIR(st.st_mode);
    out[n].size = out[n].dir ? 0 : (uint32_t)st.st_size;
    n++;
  }
  return n;
}
static bool fsAtomic(void*, const char* p, const void* b, size_t n){
  std::string t = real(p) + ".tmp";
  FILE* f = std::fopen(t.c_str(), "wb");
  if(!f) return false;
  bool ok = std::fwrite(b, 1, n, f) == n;
  ok = (std::fclose(f) == 0) && ok;
  return ok && std::rename(t.c_str(), real(p).c_str()) == 0;
}
static uint32_t nowEpoch(void*){ return (uint32_t)std::time(nullptr); }

// ---------------- anfitrion: el catalogo es el almacen ----------------
static int hSnapshot(void*, FlexMlRec* d, int cap, uint32_t* rev){ return flexMsSnapshot(&g_ms, d, cap, rev); }
static bool hGet(void*, uint32_t id, FlexMlRec* o){ return flexMsGet(&g_ms, id, o); }
static uint32_t hRev(void*){ return flexMsRev(&g_ms); }
static uint32_t hDup(void*, uint32_t size, uint32_t crc){ return flexMsFindDup(&g_ms, size, crc); }
static uint32_t hCommit(void*, const FlexWebUpload* up, char* why, size_t cap){
  FlexMsUpload u;
  u.tmpPath = up->tmpPath; u.thumbTmp = up->thumbTmp;
  u.name = up->name; u.title = up->title; u.artist = up->artist; u.album = up->album;
  u.kind = up->kind; u.fmt = up->fmt; u.playable = up->playable;
  u.size = up->size; u.crc = up->crc; u.created = up->created; u.durMs = up->durMs; u.w = up->w; u.h = up->h;
  return flexMsCommitUpload(&g_ms, &u, why, cap);
}
static bool hSetThumb(void*, uint32_t id, const char* tmp){ return flexMsSetExtThumb(&g_ms, id, tmp); }
static void hThumbPath(void*, const FlexMlRec* r, char* out, size_t cap){ flexMsThumbPath(r, out, cap); }
static bool hVerify(void*, const char* s){ return !std::strcmp(s, "2468"); }
static int hLockType(void*){ return 1; }
static uint32_t hNow(void*){
  using namespace std::chrono;
  return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
static uint32_t hEpoch(void*){ return (uint32_t)std::time(nullptr); }
static void hRand(void*, uint8_t* o, size_t n){
  FILE* f = std::fopen("/dev/urandom", "rb");
  if(!f || std::fread(o, 1, n, f) != n) for(size_t i = 0; i < n; i++) o[i] = (uint8_t)std::rand();
  if(f) std::fclose(f);
}
static void hEvent(void*, const FlexWebXfer* x){
  std::fprintf(stderr, "{\"ev\":%d,\"kind\":%d,\"done\":%u,\"total\":%u,\"id\":%u}\n", x->ev, x->kind, x->done, x->total, x->id);
}
// Otra conexion esperando... o una orden de la prueba por stdin: la prueba
// no tiene por que esperar a que el navegador suelte una conexion ociosa.
static bool hOthers(void*){
  pollfd p[2] = { { g_listen, POLLIN, 0 }, { 0, POLLIN, 0 } };
  return poll(p, 2, 0) > 0;
}

// ---------------- conexion ----------------
static int cRead(void* c, uint8_t* b, size_t n, uint32_t ms){
  int fd = *(int*)c;
  pollfd p = { fd, POLLIN, 0 };
  int r = poll(&p, 1, (int)ms);
  if(r == 0) return 0;
  if(r < 0) return -1;
  ssize_t k = recv(fd, b, n, 0);
  return k > 0 ? (int)k : -1;
}
static bool cWrite(void* c, const uint8_t* b, size_t n){
  int fd = *(int*)c;
  while(n){
    ssize_t k = send(fd, b, n, MSG_NOSIGNAL);
    if(k <= 0) return false;
    b += k; n -= (size_t)k;
  }
  return true;
}

// ---------------- FLEX STORAGE (con --storage) ----------------
static bool g_storage = false;
static FstCore g_core;
static char g_token[FST_TOKEN_MAX] = "";
static void coreRand(void*, uint8_t* o, size_t n){ hRand(nullptr, o, n); }
static bool hOffer(void*, char out[FST_HEX32]){ return fstOfferNew(&g_core, hNow(nullptr), coreRand, nullptr, out); }
static int hPair(void*, const FstPairReq* rq, const char* peer, char* json, size_t cap, uint32_t* retry){
  int st = fstPairBeginHttp(&g_core, hNow(nullptr), rq, peer, coreRand, nullptr, json, cap, retry);
  if(st == 202) std::fprintf(stderr, "{\"pairWaiting\":%d,\"sas\":\"%s\",\"name\":\"%s\"}\n",
                             g_core.pair.needsApproval ? 1 : 0, g_core.pair.sas, g_core.pair.phoneId);
  return st;
}
static int hPoll(void*, const char* id, const char* proof, char* json, size_t cap){
  bool persist = false;
  int st = fstPairPollHttp(&g_core, hNow(nullptr), id, proof, json, cap, &persist);
  if(persist){
    g_token[0] = 0;                               // telefono nuevo: sesion nueva
    std::fprintf(stderr, "{\"paired\":\"%s\",\"ip\":\"%s\",\"port\":%u}\n", g_core.phone.id, g_core.phone.ip, (unsigned)g_core.phone.port);
  }
  return st;
}
// Sesion con el telefono (reto-respuesta con autenticacion mutua, como
// FlexOS_StorageLink) por sockets de verdad.
static bool phoneSession(){
  Raw ch = e2eHttp(g_core.phone.port, "GET", "/api/fs/challenge", {}, "");
  char nonce[FST_HEX32];
  if(ch.status != 200 || !fstParseChallenge(ch.body.data(), ch.body.size(), nonce)) return false;
  char body[256];
  if(!fstSessionBody(&g_core, nonce, body, sizeof(body))) return false;
  Raw ss = e2eHttp(g_core.phone.port, "POST", "/api/fs/session", {{"Content-Type", "application/json"}}, body);
  uint32_t exp = 0;
  return ss.status == 200 && fstParseSession(ss.body.data(), ss.body.size(), g_core.phone.key, nonce, g_token, &exp, nullptr, 0);
}
static bool hCloudOpen(void*, FlexWebConn* up, char* host, size_t hc, char* bearer, size_t bc, char* why, size_t wc){
  if(!g_core.phone.valid || !g_core.phone.enabled){ std::snprintf(why, wc, "no_phone"); return false; }
  if(!g_token[0] && !phoneSession()){ std::snprintf(why, wc, "network"); return false; }
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a; std::memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET; a.sin_port = htons(g_core.phone.port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if(fd < 0 || connect(fd, (sockaddr*)&a, sizeof(a))){ if(fd >= 0) close(fd); std::snprintf(why, wc, "network"); return false; }
  up->read = cRead; up->write = cWrite; up->ctx = new int(fd); up->peer = nullptr;
  std::snprintf(host, hc, "%s:%u", g_core.phone.ip, (unsigned)g_core.phone.port);
  std::snprintf(bearer, bc, "%s", g_token);
  return true;
}
static void hCloudClose(void*, FlexWebConn* up){ int* fd = (int*)up->ctx; if(fd){ close(*fd); delete fd; up->ctx = nullptr; } }
static void hCloudResult(void*, int st){ if(st == 401) g_token[0] = 0; }
static void hOrigin(void*, char* out, size_t cap){
  if(g_core.phone.valid) std::snprintf(out, cap, "http://%s:%u", g_core.phone.ip, (unsigned)g_core.phone.port);
  else if(cap) out[0] = 0;
}
static bool hStorage(void*, char* out, size_t cap){
  bool ok = g_core.phone.valid;
  int n = std::snprintf(out, cap,
    "\"phone\":{\"state\":\"%s\",\"name\":\"%s\",\"model\":\"%s\",\"ip\":\"%s\",\"port\":%u,\"reachable\":%d,\"lastOkS\":-1,\"pairWaiting\":%d},"
    "\"cloud\":{\"dest\":\"%s\",\"net\":%d,\"netText\":\"\",\"active\":0,\"quota\":null}",
    ok ? "ready" : "none", g_core.phone.name, g_core.phone.model, g_core.phone.ip, (unsigned)g_core.phone.port, g_token[0] ? 1 : 0,
    g_core.pair.state == FSTP_PENDING ? 1 : 0, ok ? "phone" : "internet", ok ? 3 : 0);
  return n > 0 && (size_t)n < cap;
}
// ---- copias Flex OS <-> Flex Cloud: doble de pruebas (ver la cabecera) ----
struct SimXfer { uint32_t id; bool up, move; uint8_t phase; char name[256]; unsigned long long size, done; };
static std::vector<SimXfer> g_xf;
static uint32_t g_xfSeq = 0x40000100u;
static void jsonEsc(const char* in, char* out, size_t cap){
  size_t w = 0;
  for(const unsigned char* p = (const unsigned char*)in; *p && w + 7 < cap; p++){
    if(*p == '"' || *p == '\\'){ out[w++] = '\\'; out[w++] = (char)*p; }
    else if(*p < 0x20) w += (size_t)std::snprintf(out + w, cap - w, "\\u%04x", *p);
    else out[w++] = (char)*p;
  }
  out[w] = 0;
}
static bool hXfersJson(void*, char* out, size_t cap){
  size_t w = 0;
  out[w++] = '[';
  for(size_t i = 0; i < g_xf.size(); i++){
    const SimXfer& x = g_xf[i];
    char nm[600]; jsonEsc(x.name, nm, sizeof(nm));
    const char* txt = x.phase == 6 ? "Hecho" : x.phase == 7 ? "No se pudo completar" : x.phase == 8 ? "Cancelado" : x.phase == 2 ? "En curso" : "En cola";
    int k = std::snprintf(out + w, cap - w, "%s{\"id\":%lu,\"up\":%d,\"phase\":%u,\"name\":\"%s\",\"size\":%llu,\"done\":%llu,\"rate\":0,\"text\":\"%s\",\"move\":%d}",
                          i ? "," : "", (unsigned long)x.id, x.up ? 1 : 0, (unsigned)x.phase, nm, x.size, x.done, txt, x.move ? 1 : 0);
    if(k < 0 || (size_t)k >= cap - w - 2) break;
    w += (size_t)k;
  }
  out[w++] = ']'; out[w] = 0;
  return true;
}
static int hXferOp(void*, const FlexWebXferReq* rq, char* msg, size_t cap){
  if(rq->op == FLEXWEB_X_UP || rq->op == FLEXWEB_X_DOWN){
    SimXfer x; std::memset(&x, 0, sizeof(x));
    x.id = ++g_xfSeq; x.up = rq->op == FLEXWEB_X_UP; x.move = rq->move;
    if(x.up){
      FlexMlRec r;
      if(!flexMsGet(&g_ms, rq->id, &r)){ std::snprintf(msg, cap, "Ya no existe"); return 404; }
      std::snprintf(x.name, sizeof(x.name), "%s", r.name); x.size = r.size;
      std::fprintf(stderr, "{\"xferReq\":{\"op\":\"up\",\"id\":%lu,\"folder\":\"%s\",\"move\":%d,\"job\":%lu}}\n",
                   (unsigned long)rq->id, rq->folder, rq->move ? 1 : 0, (unsigned long)x.id);
    } else {
      std::snprintf(x.name, sizeof(x.name), "%s", rq->name); x.size = rq->size;
      char nm[600]; jsonEsc(rq->name, nm, sizeof(nm));
      std::fprintf(stderr, "{\"xferReq\":{\"op\":\"down\",\"file\":\"%s\",\"name\":\"%s\",\"size\":%llu,\"sha\":\"%s\",\"move\":%d,\"job\":%lu}}\n",
                   rq->file, nm, (unsigned long long)rq->size, rq->sha, rq->move ? 1 : 0, (unsigned long)x.id);
    }
    g_xf.push_back(x);
    std::snprintf(msg, cap, "%s", x.up ? (x.move ? "Moviendo a Flex Cloud" : "Copiando a Flex Cloud") : (x.move ? "Moviendo a Flex OS" : "Copiando a Flex OS"));
    return 202;
  }
  if(rq->op == FLEXWEB_X_CLEAR){
    g_xf.erase(std::remove_if(g_xf.begin(), g_xf.end(), [](const SimXfer& x){ return x.phase >= 6; }), g_xf.end());
    std::snprintf(msg, cap, "Lista limpia");
    return 202;
  }
  for(SimXfer& x : g_xf) if(x.id == rq->id){
    std::fprintf(stderr, "{\"xferReq\":{\"op\":\"%s\",\"job\":%lu}}\n", rq->op == FLEXWEB_X_CANCEL ? "cancel" : "retry", (unsigned long)x.id);
    if(rq->op == FLEXWEB_X_CANCEL && x.phase < 6){ x.phase = 8; std::snprintf(msg, cap, "Cancelada"); return 202; }
    if(rq->op == FLEXWEB_X_RETRY && x.phase == 7){ x.phase = 0; x.done = 0; std::snprintf(msg, cap, "Reintentando"); return 202; }
    std::snprintf(msg, cap, "No se puede"); return 409;
  }
  std::snprintf(msg, cap, "Ya no est\xC3\xA1 en curso");
  return 404;
}

static bool hRemoveRec(void*, uint32_t id, char* why, size_t cap){ bool ok = flexMsDelete(&g_ms, id); if(!ok) std::snprintf(why, cap, "No se pudo"); return ok; }
static bool hRenameRec(void*, uint32_t id, const char* name, char* why, size_t cap){ return flexMsRename(&g_ms, id, name, why, cap); }

static void command(char* line){
  char* nl = std::strchr(line, '\n'); if(nl) *nl = 0;
  if(g_storage && (!std::strcmp(line, "approve") || !std::strcmp(line, "deny"))){
    bool ok = fstPairDecide(&g_core, !std::strcmp(line, "approve"));
    std::printf("{\"ok\":%d}\n", ok ? 1 : 0);
    std::fflush(stdout);
    return;
  }
  unsigned long xid = 0, xdone = 0; unsigned xph = 0;
  if(g_storage && std::sscanf(line, "xjob %lu %u %lu", &xid, &xph, &xdone) == 3){
    bool ok = false;
    for(SimXfer& x : g_xf) if(x.id == xid){ x.phase = (uint8_t)xph; x.done = xdone; ok = true; }
    std::printf("{\"ok\":%d}\n", ok ? 1 : 0);
    std::fflush(stdout);
    return;
  }
  if(!std::strcmp(line, "dump")){
    static char buf[1 << 20];
    size_t w = 0;
    struct S { char* b; size_t* w; };
    S s = { buf, &w };
    flexMlJsonLibrary(&g_ms.lib, true, FML_MASK_ALL, nullptr, [](void* c, const char* t, size_t n) -> bool {
      S* s = (S*)c; if(*s->w + n >= sizeof(buf)) return false; std::memcpy(s->b + *s->w, t, n); *s->w += n; return true; }, &s);
    buf[w] = 0;
    std::printf("%s\n", buf);
    std::fflush(stdout);
    return;
  }
  if(!std::strcmp(line, "scan")){
    bool ok = flexMsScan(&g_ms, nullptr, nullptr);
    std::printf("{\"ok\":%d,\"n\":%d}\n", ok ? 1 : 0, (int)g_ms.lib.n);
    std::fflush(stdout);
    return;
  }
  if(!std::strcmp(line, "jobs")){
    FlexMsJob j; uint32_t after = 0; int n = 0;
    while(flexMsNextJob(&g_ms, after, &j)){ after = j.id; flexMsRunJob(&g_ms, &j, 64u << 20, 1u << 20); n++; }
    std::printf("{\"ok\":1,\"jobs\":%d}\n", n);
    std::fflush(stdout);
    return;
  }
  unsigned long id = 0;
  bool lock = std::sscanf(line, "lock %lu", &id) == 1;
  if(lock || std::sscanf(line, "unlock %lu", &id) == 1){
    char why[96] = "";
    bool ok = flexMsSetLock(&g_ms, (uint32_t)id, lock, why, sizeof(why));
    FlexMlRec r;
    bool have = flexMsGet(&g_ms, (uint32_t)id, &r);
    std::printf("{\"ok\":%d,\"path\":\"%s\"}\n", ok ? 1 : 0, have ? r.path : "");
    std::fflush(stdout);
  }
}

int main(int argc, char** argv){
  if(argc < 2){ std::fprintf(stderr, "uso: flexweb_host <carpeta> [bytes] [--storage [--phone-port N --phone-key HEX64]]\n"); return 2; }
  g_root = argv[1];
  int phonePort = 0; const char* phoneKey = nullptr;
  for(int i = 2; i < argc; i++){
    if(!std::strcmp(argv[i], "--storage")) g_storage = true;
    else if(!std::strcmp(argv[i], "--phone-port") && i + 1 < argc) phonePort = std::atoi(argv[++i]);
    else if(!std::strcmp(argv[i], "--phone-key") && i + 1 < argc) phoneKey = argv[++i];
    else g_total = (uint32_t)std::strtoul(argv[i], nullptr, 10);
  }
  if(g_storage){
    // El mismo id de P4 que DevServer.kt espera en las pruebas (y que netstub da a la placa).
    fstInit(&g_core, "flexos-a1b2c3d4e5f6", "Flex OS Ultra (pruebas)");
    if(phonePort > 0 && phoneKey && fstUnhex(phoneKey, g_core.phone.key, 32)){
      g_core.phone.valid = 1; g_core.phone.enabled = 1;
      std::snprintf(g_core.phone.id, sizeof(g_core.phone.id), "a55-e2e");
      std::snprintf(g_core.phone.name, sizeof(g_core.phone.name), "Galaxy A55 5G");
      std::snprintf(g_core.phone.model, sizeof(g_core.phone.model), "SM-A556B");
      std::snprintf(g_core.phone.ip, sizeof(g_core.phone.ip), "127.0.0.1");
      g_core.phone.port = (uint16_t)phonePort;
    }
  }
  mkdirs(g_root);
  std::memset(&g_ms, 0, sizeof(g_ms));
  g_ms.fs = { fsOpen, fsRead, fsWrite, fsSeek, fsClose, fsSize, fsExists, fsRemove, fsMove, nullptr, fsMkdir, fsList, fsAtomic, nullptr };
  g_ms.now = nowEpoch;                          // un solo hilo: sin cerrojo
  flexMsInit(&g_ms, g_store, FML_CAP);
  flexMsMakeDirs(&g_ms);
  flexMsLoad(&g_ms);

  g_listen = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1; setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in a; std::memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
  if(bind(g_listen, (sockaddr*)&a, sizeof(a)) || listen(g_listen, 16)){ std::perror("socket"); return 1; }
  socklen_t al = sizeof(a); getsockname(g_listen, (sockaddr*)&a, &al);
  int port = ntohs(a.sin_port);

  static FlexWebCtx w;
  std::memset(&w, 0, sizeof(w));
  w.fs = { fsOpen, fsRead, fsWrite, fsSeek, fsClose, fsSize, fsRemove, fsFree, fsTotal, nullptr };
  w.host = { hSnapshot, hGet, hRev, hDup, hCommit, hSetThumb, hThumbPath, hVerify, hLockType,
             hNow, hEpoch, hRand, hEvent, nullptr, hOthers, nullptr };
  if(g_storage){
    w.host.cloudOpen = hCloudOpen; w.host.cloudClose = hCloudClose; w.host.cloudResult = hCloudResult; w.host.phoneOrigin = hOrigin;
    w.host.phoneOffer = hOffer; w.host.phonePair = hPair; w.host.phonePoll = hPoll; w.host.storageJson = hStorage;
    w.host.removeRec = hRemoveRec; w.host.renameRec = hRenameRec;
    // Copias Flex OS <-> Flex Cloud: el doble de pruebas de arriba (en la placa,
    // el gestor de la nube; ver test_cloud.cpp y phone_e2e.sh).
    w.host.xfersJson = hXfersJson; w.host.xferOp = hXferOp;
  }
  std::snprintf(w.ip, sizeof(w.ip), "127.0.0.1");
  w.port = port;
  w.allowUpload = true;
  flexWebInit(&w);
  std::printf("{\"port\":%d,\"code\":\"%s\"}\n", port, w.code);
  std::fflush(stdout);

  static uint8_t hdr[FLEXWEB_HDR_BUF], io[FLEXWEB_IO_BUF];
  for(;;){
    pollfd p[2] = { { g_listen, POLLIN, 0 }, { 0, POLLIN, 0 } };
    if(poll(p, 2, -1) < 0) break;
    if(p[1].revents){
      char line[256];
      if(!std::fgets(line, sizeof(line), stdin)) break;              // stdin cerrado: fin
      command(line);
      continue;
    }
    if(p[0].revents & POLLIN){
      int fd = accept(g_listen, nullptr, nullptr);
      if(fd < 0) continue;
      FlexWebConn conn = { cRead, cWrite, &fd, "127.0.0.1" };
      flexWebServeConn(&w, &conn, hdr, io);
      close(fd);
    }
  }
  return 0;
}
