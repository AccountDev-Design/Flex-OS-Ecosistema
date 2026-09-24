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
//     lock <id>    bloquea un elemento como lo haria el P4 (miniatura a Protegido)
//     unlock <id>
//   Los eventos del servidor (tarjetas de transferencia del P4) salen por
//   stderr, uno por linea, en JSON.
#include "../../FlexOS_Ultra/FlexOS_MediaWeb.h"
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

static std::string g_root;
static uint32_t g_total = 10u * 1024u * 1024u;
static FlexMlRec g_store[FML_CAP];
static FlexMlLib g_lib;
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
#include <dirent.h>
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

// ---------------- anfitrion: catalogo, clave, tiempo, azar ----------------
static bool existsCb(void*, const char* p){ struct stat st; return stat(real(p).c_str(), &st) == 0; }
static void thumbPathOf(const FlexMlRec* r, char* out, size_t cap){
  if(r->flags & FML_R_LOCKED) std::snprintf(out, cap, FML_DIR_LOCKED "/%lu.t.jpg", (unsigned long)r->id);
  else std::snprintf(out, cap, FML_DIR_THUMB "/%lu.jpg", (unsigned long)r->id);
}
static int hSnapshot(void*, FlexMlRec* d, int cap, uint32_t* rev){
  int n = g_lib.n < cap ? g_lib.n : cap;
  std::memcpy(d, g_lib.recs, sizeof(FlexMlRec) * (size_t)n);
  *rev = g_lib.rev;
  return n;
}
static bool hGet(void*, uint32_t id, FlexMlRec* o){ int i = flexMlFindId(&g_lib, id); if(i < 0) return false; *o = g_lib.recs[i]; return true; }
static uint32_t hRev(void*){ return g_lib.rev; }
static uint32_t hDup(void*, uint32_t size, uint32_t crc){ int i = flexMlFindDup(&g_lib, size, crc); return i < 0 ? 0 : g_lib.recs[i].id; }
static uint32_t hCommit(void*, const FlexWebUpload* up, char* why, size_t cap){
  char stem[FML_STEM_MAX + 1], dest[FML_PATH_MAX];
  flexMlSafeStem(up->name, stem, sizeof(stem));
  const char* ext = flexMlFmtExt(up->fmt);
  if(!flexMlUniquePath(flexMlDestDir(up->kind), stem, ext, existsCb, nullptr, dest, sizeof(dest))){
    std::snprintf(why, cap, "Sin nombre libre"); return 0;
  }
  mkdirs(real(flexMlDestDir(up->kind)));
  if(std::rename(real(up->tmpPath).c_str(), real(dest).c_str())){ std::snprintf(why, cap, "No se pudo mover"); return 0; }
  FlexMlRec r; std::memset(&r, 0, sizeof(r));
  r.kind = (uint8_t)up->kind; r.fmt = (uint8_t)up->fmt; r.size = up->size; r.crc = up->crc;
  r.created = up->created; r.w = up->w; r.h = up->h; r.durMs = up->durMs; r.origin = FML_O_WEB;
  r.flags = (uint16_t)((up->playable ? FML_R_PLAYABLE : 0) | (up->thumbTmp[0] ? FML_R_THUMB : 0));
  std::snprintf(r.path, sizeof(r.path), "%s", dest);
  std::snprintf(r.name, sizeof(r.name), "%s", up->name);
  std::snprintf(r.title, sizeof(r.title), "%s", up->title);
  std::snprintf(r.artist, sizeof(r.artist), "%s", up->artist);
  std::snprintf(r.album, sizeof(r.album), "%s", up->album);
  int i = flexMlAdd(&g_lib, &r, (uint32_t)std::time(nullptr));
  if(i < 0){ std::rename(real(dest).c_str(), real(up->tmpPath).c_str()); std::snprintf(why, cap, "Catalogo lleno"); return 0; }
  if(up->thumbTmp[0]){
    char tp[FML_PATH_MAX]; thumbPathOf(&g_lib.recs[i], tp, sizeof(tp));
    mkdirs(real(FML_DIR_THUMB));
    std::rename(real(up->thumbTmp).c_str(), real(tp).c_str());
  }
  return g_lib.recs[i].id;
}
static bool hSetThumb(void*, uint32_t id, const char* tmp){
  int i = flexMlFindId(&g_lib, id);
  if(i < 0) return false;
  char tp[FML_PATH_MAX]; thumbPathOf(&g_lib.recs[i], tp, sizeof(tp));
  mkdirs(real(FML_DIR_THUMB));
  if(std::rename(real(tmp).c_str(), real(tp).c_str())) return false;
  g_lib.recs[i].flags |= FML_R_THUMB | FML_R_EXT_THUMB;
  g_lib.recs[i].thumbVer++;
  g_lib.rev++;
  return true;
}
static void hThumbPath(void*, const FlexMlRec* r, char* out, size_t cap){ thumbPathOf(r, out, cap); }
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

static void command(char* line){
  char* nl = std::strchr(line, '\n'); if(nl) *nl = 0;
  if(!std::strcmp(line, "dump")){
    static char buf[1 << 20];
    size_t w = 0;
    struct S { char* b; size_t* w; };
    S s = { buf, &w };
    flexMlJsonLibrary(&g_lib, true, FML_MASK_ALL, nullptr, [](void* c, const char* t, size_t n) -> bool {
      S* s = (S*)c; if(*s->w + n >= sizeof(buf)) return false; std::memcpy(s->b + *s->w, t, n); *s->w += n; return true; }, &s);
    buf[w] = 0;
    std::printf("%s\n", buf);
    std::fflush(stdout);
    return;
  }
  unsigned long id = 0;
  bool lock = std::sscanf(line, "lock %lu", &id) == 1;
  if(lock || std::sscanf(line, "unlock %lu", &id) == 1){
    int i = flexMlFindId(&g_lib, (uint32_t)id);
    if(i >= 0){
      char a[FML_PATH_MAX], b[FML_PATH_MAX];
      thumbPathOf(&g_lib.recs[i], a, sizeof(a));
      flexMlSetLocked(&g_lib, i, lock);
      thumbPathOf(&g_lib.recs[i], b, sizeof(b));
      mkdirs(real(lock ? FML_DIR_LOCKED : FML_DIR_THUMB));
      std::rename(real(a).c_str(), real(b).c_str());
    }
    std::printf("{\"ok\":%d}\n", i >= 0 ? 1 : 0);
    std::fflush(stdout);
  }
}

int main(int argc, char** argv){
  if(argc < 2){ std::fprintf(stderr, "uso: flexweb_host <carpeta> [bytes]\n"); return 2; }
  g_root = argv[1];
  if(argc >= 3) g_total = (uint32_t)std::strtoul(argv[2], nullptr, 10);
  mkdirs(g_root);
  for(const char* d : { FML_DIR_PHOTO, FML_DIR_VIDEO, FML_DIR_AUDIO, FML_DIR_THUMB, FML_DIR_TMP, FML_DIR_LOCKED }) mkdirs(real(d));
  flexMlInit(&g_lib, g_store, FML_CAP);

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
      FlexWebConn conn = { cRead, cWrite, &fd };
      flexWebServeConn(&w, &conn, hdr, io);
      close(fd);
    }
  }
  return 0;
}
