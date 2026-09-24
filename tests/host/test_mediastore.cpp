// #############################################################
//  test_mediastore.cpp  ·  el almacen de la biblioteca de medios
// #############################################################
//
//  FlexOS_MediaStore es lo que la placa HACE con el disco en nombre de la
//  biblioteca: cargar y guardar el catalogo, reconciliarlo, hacer las
//  miniaturas, bloquear (mover a la carpeta protegida), borrar, renombrar,
//  sustituir un original y publicar lo que sube el movil. Aqui corre el
//  MISMO codigo contra un sistema de archivos en memoria que sabe fallar:
//  un movimiento que no se hace, una carpeta que no se deja leer, un disco
//  que se llena, un catalogo que se quedo a medio escribir.
//
//  Lo que importa es lo que NO puede pasar:
//    · perder un archivo del usuario por cualquier camino de error;
//    · que un protegido deje algo a la vista: su archivo en una carpeta
//      publica, su miniatura en la carpeta publica, su paso por la papelera;
//    · que una lectura fallida de una carpeta vacie el catalogo;
//    · un registro duplicado o fantasma cuando la interfaz, la tarea de
//      fondo y el servidor trabajan A LA VEZ (hay una prueba con hilos);
//    · tomar el cerrojo dos veces en el mismo hilo (en la placa es un mutex
//      no recursivo: seria un bloqueo mutuo) o cambiar el disco de un
//      registro sin tenerlo.

#include "../../FlexOS_Ultra/FlexOS_MediaStore.h"
#include "../../FlexOS_Ultra/FlexOS_MediaThumb.h"
#include "../../FlexOS_Ultra/FlexOS_JPEGEnc.h"
#include "../../FlexOS_Ultra/FlexOS_JPEG.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <thread>
#include <atomic>

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

// =============================================================
//  Sistema de archivos en memoria, con fallos a la carta
// =============================================================
struct MemFs {
  std::map<std::string, std::vector<uint8_t>> files;
  std::set<std::string> dirs;
  std::set<std::string> unreadable;     // list() falla aunque exista
  std::set<std::string> failMoveFrom;   // mover DESDE aqui falla
  std::set<std::string> failMoveTo;     // mover HACIA aqui falla
  std::set<std::string> failRemove;
  long failWriteAfter = -1, written = 0;
  bool failAtomic = false;
  int  atomicWrites = 0;
};
static MemFs g_fs;
static std::mutex g_fsMu;               // LittleFS tiene su propio cerrojo interno
#define FSLOCK std::lock_guard<std::mutex> fsg(g_fsMu)

// ---- el cerrojo del almacen: detecta anidamiento y cambios sin el ----
static std::mutex g_mu;
static thread_local bool t_held = false;
static std::atomic<int> g_nested{0}, g_badUnlock{0}, g_unlockedChanges{0};
static void lk(void*){ if(t_held){ g_nested++; return; } g_mu.lock(); t_held = true; }
static void ul(void*){ if(!t_held){ g_badUnlock++; return; } t_held = false; g_mu.unlock(); }
static bool isTmp(const std::string& p){ return p.rfind(FML_DIR_TMP "/", 0) == 0; }
// Un cambio de disco fuera de los temporales es el archivo (o la miniatura)
// de un registro: la regla de oro dice que va con el cerrojo tomado.
static void noteChange(const std::string& a, const std::string& b = ""){
  if(t_held) return;
  if(!isTmp(a) || (!b.empty() && !isTmp(b))) g_unlockedChanges++;
}

static std::string parentOf(const std::string& p){ size_t s = p.rfind('/'); return s == 0 || s == std::string::npos ? "/" : p.substr(0, s); }
static void addDirs(const std::string& d){ for(std::string x = d; x != "/" && !x.empty(); x = parentOf(x)) g_fs.dirs.insert(x); }
static bool dirExistsU(const std::string& d){
  if(g_fs.dirs.count(d)) return true;
  std::string pre = d + "/";
  auto it = g_fs.files.lower_bound(pre);
  return it != g_fs.files.end() && it->first.compare(0, pre.size(), pre) == 0;
}

struct H { std::string path; size_t pos = 0; };
static void* fsOpen(void*, const char* p, bool wr){
  FSLOCK;
  if(wr){ g_fs.files[p].clear(); addDirs(parentOf(p)); }
  else if(!g_fs.files.count(p)) return nullptr;
  H* h = new H; h->path = p; return h;
}
static int fsRead(void*, void* hh, uint8_t* b, size_t n){
  FSLOCK;
  H* h = (H*)hh; auto it = g_fs.files.find(h->path); if(it == g_fs.files.end()) return -1;
  auto& d = it->second; if(h->pos >= d.size()) return 0;
  size_t k = d.size() - h->pos; if(n > k) n = k; std::memcpy(b, d.data() + h->pos, n); h->pos += n; return (int)n;
}
static bool fsWrite(void*, void* hh, const uint8_t* b, size_t n){
  FSLOCK;
  H* h = (H*)hh;
  if(g_fs.failWriteAfter >= 0 && g_fs.written + (long)n > g_fs.failWriteAfter) return false;
  g_fs.written += (long)n;
  auto& d = g_fs.files[h->path];
  if(h->pos + n > d.size()) d.resize(h->pos + n);
  std::memcpy(d.data() + h->pos, b, n); h->pos += n; return true;
}
static bool fsSeek(void*, void* hh, uint32_t off){
  FSLOCK; H* h = (H*)hh; auto it = g_fs.files.find(h->path);
  if(it == g_fs.files.end() || off > it->second.size()) return false;
  h->pos = off; return true;
}
static void fsClose(void*, void* hh){ delete (H*)hh; }
static uint32_t fsSize(void*, const char* p){ FSLOCK; auto it = g_fs.files.find(p); return it == g_fs.files.end() ? 0 : (uint32_t)it->second.size(); }
static bool fsExists(void*, const char* p){ FSLOCK; return g_fs.files.count(p) || dirExistsU(p); }
static bool fsRemove(void*, const char* p){
  FSLOCK;
  noteChange(p);
  if(g_fs.failRemove.count(p)) return false;
  if(g_fs.files.erase(p)) return true;
  if(!dirExistsU(p)) return false;
  std::string pre = std::string(p) + "/";
  for(auto it = g_fs.files.lower_bound(pre); it != g_fs.files.end() && it->first.compare(0, pre.size(), pre) == 0; ) it = g_fs.files.erase(it);
  for(auto it = g_fs.dirs.begin(); it != g_fs.dirs.end(); ) it = (*it == p || it->compare(0, pre.size(), pre) == 0) ? g_fs.dirs.erase(it) : std::next(it);
  return true;
}
static bool moveU(const std::string& a, const std::string& b){
  auto it = g_fs.files.find(a);
  if(it == g_fs.files.end() || g_fs.files.count(b) || dirExistsU(b)) return false;
  if(g_fs.failMoveFrom.count(a) || g_fs.failMoveTo.count(b)) return false;
  addDirs(parentOf(b));
  g_fs.files[b] = std::move(it->second); g_fs.files.erase(a);
  return true;
}
static bool fsMove(void*, const char* a, const char* b){ FSLOCK; noteChange(a, b); return moveU(a, b); }
static bool fsTrash(void*, const char* p){
  FSLOCK; noteChange(p);
  std::string b = std::string("/Papelera/") + (strrchr(p, '/') ? strrchr(p, '/') + 1 : p);
  for(int k = 2; g_fs.files.count(b); k++) b = std::string("/Papelera/") + std::to_string(k) + "_" + (strrchr(p, '/') + 1);
  return moveU(p, b);
}
static bool fsMkdir(void*, const char* p){ FSLOCK; addDirs(p); return true; }
static int fsList(void*, const char* dir, FlexMsEntry* out, int maxn, int skip){
  FSLOCK;
  std::string d = dir;
  if(g_fs.unreadable.count(d) || !dirExistsU(d)) return -1;
  std::map<std::string, FlexMsEntry> kids;
  std::string pre = d + "/";
  auto add = [&](const std::string& full, bool isFile, uint32_t sz){
    std::string rest = full.substr(pre.size());
    size_t s = rest.find('/');
    FlexMsEntry e; std::memset(&e, 0, sizeof(e));
    if(s == std::string::npos){ snprintf(e.name, sizeof(e.name), "%s", rest.c_str()); e.size = sz; e.dir = !isFile; }
    else { snprintf(e.name, sizeof(e.name), "%s", rest.substr(0, s).c_str()); e.dir = true; }
    if(!kids.count(e.name)) kids[e.name] = e;
  };
  for(auto it = g_fs.files.lower_bound(pre); it != g_fs.files.end() && it->first.compare(0, pre.size(), pre) == 0; ++it)
    add(it->first, true, (uint32_t)it->second.size());
  for(auto& x : g_fs.dirs) if(x.compare(0, pre.size(), pre) == 0) add(x, false, 0);
  int n = 0, i = 0;
  for(auto& kv : kids){ if(i++ < skip) continue; if(n >= maxn) break; out[n++] = kv.second; }
  return n;
}
static bool fsAtomic(void*, const char* p, const void* b, size_t n){
  FSLOCK;
  if(g_fs.failAtomic) return false;
  g_fs.atomicWrites++;
  g_fs.files[p].assign((const uint8_t*)b, (const uint8_t*)b + n); addDirs(parentOf(p));
  return true;
}

// ---- memoria contada: ninguna reserva puede quedarse viva ----
static std::atomic<long> g_live{0};
static void* tA(size_t n){ void* p = std::malloc(n); if(p) g_live++; return p; }
static void tF(void* p){ if(p){ g_live--; std::free(p); } }

// ---- dibujos de Paint: el formato real vive en FlexOS_FS ----
static int g_paintCalls = 0;
static bool paintThumb(void*, const char* path, uint16_t* px, int* w, int* h){
  g_paintCalls++;
  if(strstr(path, "roto")) return false;           // cabecera que FlexOS_FS no acepta
  for(int i = 0; i < FLEXTH_SIDE * FLEXTH_SIDE; i++) px[i] = (uint16_t)(i * 7);
  *w = 320; *h = 480;
  return true;
}
static uint32_t g_now = 1760000000u;
static uint32_t nowCb(void*){ return g_now; }

static FlexMlRec g_store[FML_CAP];
static FlexMediaStore g_ms;

static void setupStore(FlexMediaStore* ms, FlexMlRec* store, uint16_t cap){
  std::memset(ms, 0, sizeof(*ms));
  ms->fs = { fsOpen, fsRead, fsWrite, fsSeek, fsClose, fsSize, fsExists, fsRemove, fsMove, fsTrash, fsMkdir, fsList, fsAtomic, nullptr };
  ms->lock = lk; ms->unlock = ul;
  ms->alloc = tA; ms->free = tF;
  ms->now = nowCb;
  ms->paintThumb = paintThumb;
  flexMsInit(ms, store, cap);
}
static void fresh(uint16_t cap = FML_CAP){
  { FSLOCK; g_fs = MemFs(); }
  g_nested = 0; g_badUnlock = 0; g_unlockedChanges = 0; g_paintCalls = 0;
  setupStore(&g_ms, g_store, cap);
  flexMsMakeDirs(&g_ms);
}
static void put(const std::string& p, const std::vector<uint8_t>& d){ FSLOCK; g_fs.files[p] = d; addDirs(parentOf(p)); }
static bool has(const std::string& p){ FSLOCK; return g_fs.files.count(p) != 0; }
static std::vector<uint8_t> get(const std::string& p){ FSLOCK; auto it = g_fs.files.find(p); return it == g_fs.files.end() ? std::vector<uint8_t>() : it->second; }
static int countUnder(const std::string& d){ FSLOCK; int n = 0; std::string pre = d + "/"; for(auto& kv : g_fs.files) if(kv.first.compare(0, pre.size(), pre) == 0) n++; return n; }
static const FlexMlRec* rec(uint32_t id){ int i = flexMlFindId(&g_ms.lib, id); return i < 0 ? nullptr : &g_ms.lib.recs[i]; }
static const FlexMlRec* recAt(const char* path){ int i = flexMlFindPath(&g_ms.lib, path); return i < 0 ? nullptr : &g_ms.lib.recs[i]; }
static std::string thumbOf(uint32_t id){ const FlexMlRec* r = rec(id); char b[FML_PATH_MAX]; flexMsThumbPath(r, b, sizeof(b)); return b; }
static void lockRules(const char* where){
  CHECK(g_nested == 0, "%s: cerrojo tomado dos veces en el mismo hilo (%d)", where, (int)g_nested);
  CHECK(g_badUnlock == 0, "%s: soltado sin tenerlo (%d)", where, (int)g_badUnlock);
  CHECK(g_unlockedChanges == 0, "%s: %d cambios de disco de un registro sin el cerrojo", where, (int)g_unlockedChanges);
  CHECK(g_live == 0, "%s: %ld reservas vivas", where, (long)g_live);
}

// =============================================================
//  Contenido de prueba
// =============================================================
static bool outV(void* c, const uint8_t* d, size_t n){ ((std::vector<uint8_t>*)c)->insert(((std::vector<uint8_t>*)c)->end(), d, d + n); return true; }
static std::vector<uint8_t> makeJpeg(int w, int h, int seed = 0){
  std::vector<uint8_t> px((size_t)w * h * 3);
  for(int y = 0; y < h; y++) for(int x = 0; x < w; x++){
    uint8_t* p = &px[((size_t)y * w + x) * 3]; p[0] = (uint8_t)(x + seed); p[1] = (uint8_t)y; p[2] = (uint8_t)(x ^ y);
  }
  std::vector<uint8_t> o; FlexJeCfg c; c.width = w; c.height = h; c.quality = 85; c.subsampling = FLEXJE_SUB_420; c.input = FLEXJE_IN_RGB888;
  flexJpegEncodeMem(&c, px.data(), 0, outV, &o, nullptr, nullptr);
  return o;
}
static void p16(std::vector<uint8_t>& v, uint16_t x){ v.push_back((uint8_t)x); v.push_back((uint8_t)(x >> 8)); }
static void p32(std::vector<uint8_t>& v, uint32_t x){ for(int i = 0; i < 4; i++) v.push_back((uint8_t)(x >> (8 * i))); }
static void cc4(std::vector<uint8_t>& v, const char* s){ for(int i = 0; i < 4; i++) v.push_back((uint8_t)s[i]); }
static std::vector<uint8_t> makeAvi(int frames, const char* codec = "MJPG"){
  auto f = makeJpeg(320, 240, 1);
  std::vector<uint8_t> hdrl; cc4(hdrl, "hdrl");
  cc4(hdrl, "avih"); p32(hdrl, 56); p32(hdrl, 100000); for(int i = 0; i < 3; i++) p32(hdrl, 0);
  p32(hdrl, (uint32_t)frames); p32(hdrl, 0); p32(hdrl, 1); p32(hdrl, 0); p32(hdrl, 320); p32(hdrl, 240); for(int i = 0; i < 4; i++) p32(hdrl, 0);
  std::vector<uint8_t> strl; cc4(strl, "strl");
  cc4(strl, "strh"); p32(strl, 56); cc4(strl, "vids"); cc4(strl, codec); p32(strl, 0); p32(strl, 0); p32(strl, 0);
  p32(strl, 1); p32(strl, 10); p32(strl, 0); p32(strl, (uint32_t)frames); p32(strl, 0); p32(strl, 0xFFFFFFFF); p32(strl, 0); p32(strl, 0); p32(strl, 0);
  cc4(strl, "strf"); p32(strl, 40); p32(strl, 40); p32(strl, 320); p32(strl, 240); p32(strl, 0x00180001); cc4(strl, codec); for(int i = 0; i < 5; i++) p32(strl, 0);
  cc4(hdrl, "LIST"); p32(hdrl, (uint32_t)strl.size()); hdrl.insert(hdrl.end(), strl.begin(), strl.end());
  std::vector<uint8_t> movi; cc4(movi, "movi");
  for(int i = 0; i < frames; i++){ cc4(movi, "00dc"); p32(movi, (uint32_t)f.size()); movi.insert(movi.end(), f.begin(), f.end()); if(f.size() & 1) movi.push_back(0); }
  std::vector<uint8_t> body; cc4(body, "AVI ");
  cc4(body, "LIST"); p32(body, (uint32_t)hdrl.size()); body.insert(body.end(), hdrl.begin(), hdrl.end());
  cc4(body, "LIST"); p32(body, (uint32_t)movi.size()); body.insert(body.end(), movi.begin(), movi.end());
  std::vector<uint8_t> out; cc4(out, "RIFF"); p32(out, (uint32_t)body.size()); out.insert(out.end(), body.begin(), body.end());
  return out;
}
// WAV: tag 1 = PCM 16 bits; otra etiqueta = codec que el P4 no tiene.
static std::vector<uint8_t> makeWav(uint32_t rate, uint32_t frames, uint16_t tag = 1){
  std::vector<uint8_t> v; cc4(v, "RIFF"); p32(v, 36 + frames * 2); cc4(v, "WAVE");
  cc4(v, "fmt "); p32(v, 16); p16(v, tag); p16(v, 1); p32(v, rate); p32(v, rate * 2); p16(v, 2); p16(v, 16);
  cc4(v, "data"); p32(v, frames * 2);
  for(uint32_t i = 0; i < frames; i++) p16(v, (uint16_t)(i * 97));
  return v;
}
static std::vector<uint8_t> readFixture(const char* name){
  std::string p = std::string("../fixtures/") + name;
  FILE* f = std::fopen(p.c_str(), "rb");
  std::vector<uint8_t> d;
  if(!f) return d;
  int c; while((c = std::fgetc(f)) != EOF) d.push_back((uint8_t)c);
  std::fclose(f);
  return d;
}
static bool jpegDims(const std::vector<uint8_t>& d, int* w, int* h){
  FlexJpegInfo inf;
  if(d.empty() || flexJpegProbe(d.data(), d.size(), &inf) != 0) return false;
  *w = inf.width; *h = inf.height; return true;
}
static bool jpegDecodes(const std::vector<uint8_t>& d){
  if(d.empty()) return false;
  auto cb = [](void*, int, int, const uint16_t*) -> bool { return true; };
  return flexJpegDecode(d.data(), d.size(), 0, 0, 0, nullptr, cb, nullptr, nullptr, nullptr) == 0;
}
// Ejecuta todos los trabajos pendientes con memoria de sobra.
static int runAll(uint32_t memFree = 64u * 1024u * 1024u){
  FlexMsJob j; int n = 0; uint32_t after = 0;
  while(flexMsNextJob(&g_ms, after, &j)){ after = j.id; flexMsRunJob(&g_ms, &j, memFree, 1024u * 1024u); n++; }
  return n;
}

// =============================================================
static void testPersistence(){
  std::printf("-- catalogo: guardar, cargar y sus tres generaciones --\n");
  fresh();
  put(FML_DIR_PHOTO "/a.jpg", makeJpeg(64, 48));
  put(FML_DIR_AUDIO "/b.wav", makeWav(8000, 800));
  CHECK(flexMsScan(&g_ms, nullptr, nullptr), "el recorrido lee todo");
  CHECK(g_ms.lib.n == 2 && g_ms.dirty, "dos registros y cambios sin guardar (n=%d)", g_ms.lib.n);
  CHECK(flexMsSave(&g_ms) && !g_ms.dirty && has(FML_LIB_PATH), "guardado");
  uint32_t idA = recAt(FML_DIR_PHOTO "/a.jpg")->id;

  static FlexMlRec st2[FML_CAP]; FlexMediaStore m2;
  setupStore(&m2, st2, FML_CAP);
  CHECK(flexMsLoad(&m2) == 1 && m2.lib.n == 2 && !m2.dirty, "se carga el bueno");
  CHECK(flexMlFindId(&m2.lib, idA) >= 0, "con los mismos ids");

  // El bueno danado: se cae a .bak, y se marca para reescribir.
  auto good = get(FML_LIB_PATH);
  auto bad = good; bad[40] ^= 0x5A;
  put(FML_LIB_PATH, bad);
  put(FML_LIB_PATH ".bak", good);
  setupStore(&m2, st2, FML_CAP);
  CHECK(flexMsLoad(&m2) == 2 && m2.lib.n == 2 && m2.dirty, "danado: la generacion anterior (.bak) y a reescribir");
  // Ni bueno ni .bak: el .tmp que no llego a renombrarse.
  { FSLOCK; g_fs.files.erase(FML_LIB_PATH ".bak"); }
  put(FML_LIB_PATH ".tmp", good);
  setupStore(&m2, st2, FML_CAP);
  CHECK(flexMsLoad(&m2) == 3 && m2.lib.n == 2, "y si no, el .tmp");
  // Nada legible: vacio, nunca medio catalogo.
  { FSLOCK; g_fs.files.erase(FML_LIB_PATH ".tmp"); }
  setupStore(&m2, st2, FML_CAP);
  CHECK(flexMsLoad(&m2) == 0 && m2.lib.n == 0, "nada legible: vacio");
  // Un catalogo que dice tener mas registros de los que caben no se lee.
  put(FML_LIB_PATH, good);
  static FlexMlRec st1[1]; FlexMediaStore m1;
  setupStore(&m1, st1, 1);
  CHECK(flexMsLoad(&m1) == 0 && m1.lib.n == 0, "mas registros que capacidad: no se carga a medias");

  // Guardar que falla: sigue sucio (se reintentara).
  flexMlTouch(&g_ms.lib); g_ms.dirty = true;
  g_fs.failAtomic = true;
  CHECK(!flexMsSave(&g_ms) && g_ms.dirty, "si la escritura falla, sigue pendiente");
  g_fs.failAtomic = false;
  CHECK(flexMsSave(&g_ms) && !g_ms.dirty, "y a la siguiente se guarda");
  lockRules("persistencia");
}

static void testCleanTmp(){
  std::printf("-- temporales abandonados --\n");
  fresh();
  for(int i = 0; i < 21; i++) put(FML_DIR_TMP "/up-" + std::to_string(i) + ".part", {1, 2, 3});
  put(FML_DIR_PHOTO "/keep.jpg", makeJpeg(16, 16));
  flexMsCleanTmp(&g_ms);
  CHECK(countUnder(FML_DIR_TMP) == 0, "no queda ningun temporal (%d)", countUnder(FML_DIR_TMP));
  CHECK(has(FML_DIR_PHOTO "/keep.jpg"), "y no se toca nada fuera de su carpeta");
  lockRules("temporales");
}

static void testScan(){
  std::printf("-- reconciliacion con el disco --\n");
  fresh();
  put(FML_DIR_PHOTO "/a.jpg", makeJpeg(64, 48));
  put(FML_DIR_PHOTO "/viaje/b.jpg", makeJpeg(64, 48, 3));
  put(FML_DIR_PHOTO "/viaje/dia2/c.jpg", makeJpeg(64, 48, 4));
  put(FML_DIR_PHOTO "/viaje/dia2/hondo/d.jpg", makeJpeg(64, 48, 5));   // mas alla de dos niveles
  put(FML_DIR_PHOTO "/notas.txt", {'h', 'o', 'l', 'a'});
  put(FML_DIR_VIDEO "/v.avi", makeAvi(3));
  put(FML_DIR_AUDIO "/s.wav", makeWav(8000, 400));
  put("/Paint/dibujo.fxp", {'F', 'X', 'P', '1', 0, 0, 0, 0});
  put("/Descargas/bajada.jpg", makeJpeg(32, 32));
  put(FML_DIR_LOCKED "/77.jpg", makeJpeg(40, 40));
  put(FML_DIR_LOCKED "/77.t.jpg", makeJpeg(8, 8));
  put("/Otra/fuera.jpg", makeJpeg(8, 8));                          // no es carpeta de medios
  CHECK(flexMsScan(&g_ms, nullptr, nullptr), "recorrido completo");
  CHECK(recAt(FML_DIR_PHOTO "/a.jpg") && recAt(FML_DIR_PHOTO "/viaje/b.jpg") && recAt(FML_DIR_PHOTO "/viaje/dia2/c.jpg"),
        "fotos de la raiz y de dos niveles de subcarpetas");
  CHECK(!recAt(FML_DIR_PHOTO "/viaje/dia2/hondo/d.jpg"), "no baja mas de dos niveles");
  CHECK(!recAt(FML_DIR_PHOTO "/notas.txt") && !recAt("/Otra/fuera.jpg"), "ni otras extensiones ni otras carpetas");
  const FlexMlRec* v = recAt(FML_DIR_VIDEO "/v.avi");
  const FlexMlRec* s = recAt(FML_DIR_AUDIO "/s.wav");
  const FlexMlRec* d = recAt("/Paint/dibujo.fxp");
  CHECK(v && v->kind == FML_K_VIDEO && s && s->kind == FML_K_AUDIO && d && d->kind == FML_K_DRAW, "clases por carpeta y extension");
  CHECK(recAt("/Descargas/bajada.jpg") != nullptr, "tambien Descargas");
  const FlexMlRec* L = recAt(FML_DIR_LOCKED "/77.jpg");
  CHECK(L && (L->flags & FML_R_LOCKED) && !strcmp(L->name, "Elemento protegido"),
        "lo de la carpeta protegida vuelve BLOQUEADO y sin nombre");
  CHECK(!recAt(FML_DIR_LOCKED "/77.t.jpg"), "su miniatura no es un elemento");
  int n0 = g_ms.lib.n; uint32_t rev0 = g_ms.lib.rev;
  g_ms.dirty = false;
  CHECK(flexMsScan(&g_ms, nullptr, nullptr) && g_ms.lib.n == n0 && g_ms.lib.rev == rev0 && !g_ms.dirty,
        "un segundo recorrido no cambia nada (n %d->%d)", n0, g_ms.lib.n);

  // Un archivo que se va: se retira con su miniatura.
  uint32_t idA = recAt(FML_DIR_PHOTO "/a.jpg")->id;
  runAll();
  CHECK(has(thumbOf(idA)), "a.jpg tiene miniatura");
  std::string tpA = thumbOf(idA);
  { FSLOCK; g_fs.files.erase(FML_DIR_PHOTO "/a.jpg"); }
  CHECK(flexMsScan(&g_ms, nullptr, nullptr) && !rec(idA) && !has(tpA), "borrado por fuera: fuera del catalogo y sin miniatura");

  // Una carpeta que EXISTE y no se deja leer: no se retira NADA.
  uint32_t idV = recAt(FML_DIR_VIDEO "/v.avi")->id;
  { FSLOCK; g_fs.files.erase(FML_DIR_PHOTO "/viaje/b.jpg"); }
  g_fs.unreadable.insert(FML_DIR_VIDEO);
  int before = g_ms.lib.n;
  CHECK(!flexMsScan(&g_ms, nullptr, nullptr), "el recorrido avisa de la carpeta ilegible");
  CHECK(g_ms.lib.n == before && rec(idV) && recAt(FML_DIR_PHOTO "/viaje/b.jpg"),
        "y no retira nada, ni lo que falta de verdad (n %d->%d)", before, g_ms.lib.n);
  g_fs.unreadable.clear();
  CHECK(flexMsScan(&g_ms, nullptr, nullptr) && !recAt(FML_DIR_PHOTO "/viaje/b.jpg") && rec(idV),
        "con todo legible, se retira solo lo que falta");
  // Una carpeta que no existe no es un error.
  { FSLOCK; g_fs.dirs.erase("/Camara"); }
  CHECK(flexMsScan(&g_ms, nullptr, nullptr), "una carpeta que no existe no es un error");

  // Un archivo cuyo tamano cambia (un dibujo que crece): miniatura de nuevo.
  runAll();
  const FlexMlRec* dr = recAt("/Paint/dibujo.fxp");
  uint8_t tv = dr->thumbVer;
  put("/Paint/dibujo.fxp", {'F', 'X', 'P', '1', 1, 2, 3, 4, 5, 6});
  flexMsScan(&g_ms, nullptr, nullptr);
  dr = recAt("/Paint/dibujo.fxp");
  CHECK(dr && (dr->flags & FML_R_NEED_THUMB) && dr->thumbVer != tv && dr->size == 10, "contenido cambiado: miniatura otra vez");

  // Lotes: el recorrido cede entre lotes y no se pierde nada con muchas entradas.
  fresh();
  for(int i = 0; i < 50; i++) put(FML_DIR_PHOTO "/f" + std::to_string(100 + i) + ".jpg", {0xFF, 0xD8, 0xFF, 0xE0});
  int yields = 0;
  flexMsScan(&g_ms, [](void* c){ (*(int*)c)++; }, &yields);
  CHECK(g_ms.lib.n == 50 && yields >= 6, "50 archivos en lotes (n=%d, cesiones=%d)", g_ms.lib.n, yields);
  lockRules("reconciliacion");
}

static void testJobs(){
  std::printf("-- miniaturas y metadatos (tarea de fondo) --\n");
  fresh();
  auto jpg = makeJpeg(640, 480);
  put(FML_DIR_PHOTO "/foto.jpg", jpg);
  auto prog = readFixture("progressive.jpg");
  CHECK(!prog.empty(), "fixture progressive.jpg");
  put(FML_DIR_PHOTO "/prog.jpg", prog);
  auto broken = jpg; broken.resize(broken.size() / 3);
  put(FML_DIR_PHOTO "/rota.jpg", broken);
  put(FML_DIR_PHOTO "/imagen.png", {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13});
  put(FML_DIR_VIDEO "/clip.avi", makeAvi(20));
  put(FML_DIR_VIDEO "/h264.avi", makeAvi(5, "H264"));
  put(FML_DIR_AUDIO "/tono.wav", makeWav(8000, 16000));
  put(FML_DIR_AUDIO "/mulaw.wav", makeWav(8000, 800, 7));
  put("/Paint/lienzo.fxp", {'F', 'X', 'P', '1', 9, 9, 9, 9});
  put("/Paint/roto.fxp", {'F', 'X', 'P', '1', 0, 0});
  flexMsScan(&g_ms, nullptr, nullptr);

  // Sin memoria: se deja para luego, sin tocar nada.
  FlexMsJob j;
  uint32_t idF = recAt(FML_DIR_PHOTO "/foto.jpg")->id;
  CHECK(flexMsNextJob(&g_ms, 0, &j) && g_ms.pending == 10, "diez trabajos pendientes (%d)", (int)g_ms.pending);
  j.id = 0;
  for(uint32_t after = 0; flexMsNextJob(&g_ms, after, &j) && j.id != idF; after = j.id){}
  CHECK(j.id == idF, "se encuentra el trabajo de la foto");
  CHECK(flexMsRunJob(&g_ms, &j, 200u * 1024u, 1024u * 1024u) == FLEXMS_JOB_RETRY, "memoria justa: se reintenta");
  CHECK((rec(idF)->flags & FML_R_NEED_THUMB) && countUnder(FML_DIR_TMP) == 0, "sigue pendiente y sin temporales");

  int ran = runAll();
  CHECK(ran == 10 && g_ms.pending == 0, "todos hechos (%d)", ran);
  CHECK(countUnder(FML_DIR_TMP) == 0, "ningun temporal de miniatura se queda");

  const FlexMlRec* f = rec(idF);
  CHECK(f->fmt == FML_F_JPEG && (f->flags & FML_R_PLAYABLE) && (f->flags & FML_R_THUMB) && f->w == 640 && f->h == 480 &&
        f->state == FML_S_READY, "foto: formato real, dimensiones, miniatura");
  auto th = get(thumbOf(idF)); int tw = 0, thh = 0;
  CHECK(jpegDims(th, &tw, &thh) && tw == FLEXTH_SIDE && thh == FLEXTH_SIDE && jpegDecodes(th),
        "la miniatura es un JPEG cuadrado que el firmware decodifica (%dx%d)", tw, thh);

  const FlexMlRec* p = recAt(FML_DIR_PHOTO "/prog.jpg");
  CHECK(p && p->fmt == FML_F_JPEG_PROG && !(p->flags & FML_R_PLAYABLE) && (p->flags & FML_R_THUMB_FAIL) &&
        p->err == FML_E_UNSUPPORTED && p->state == FML_S_READY, "progresivo: se conserva, no se abre");
  const FlexMlRec* b = recAt(FML_DIR_PHOTO "/rota.jpg");
  CHECK(b && b->state == FML_S_ERROR && b->err == FML_E_CORRUPT && !(b->flags & FML_R_PLAYABLE),
        "JPEG cortado: danado, nunca 'listo'");
  const FlexMlRec* png = recAt(FML_DIR_PHOTO "/imagen.png");
  CHECK(png && png->fmt == FML_F_PNG && !(png->flags & FML_R_PLAYABLE) && (png->flags & FML_R_THUMB_FAIL), "PNG: guardado sin abrir");
  const FlexMlRec* v = recAt(FML_DIR_VIDEO "/clip.avi");
  CHECK(v && v->fmt == FML_F_AVI_MJPEG && (v->flags & FML_R_THUMB) && v->w == 320 && v->h == 240 && v->durMs == 2000,
        "AVI MJPEG: primer fotograma, 320x240, 2 s (dur=%u)", v ? v->durMs : 0);
  const FlexMlRec* h = recAt(FML_DIR_VIDEO "/h264.avi");
  CHECK(h && h->fmt == FML_F_AVI_OTHER && !(h->flags & FML_R_PLAYABLE) && h->err == FML_E_UNSUPPORTED, "AVI con H.264: no reproducible");
  const FlexMlRec* w = recAt(FML_DIR_AUDIO "/tono.wav");
  CHECK(w && w->fmt == FML_F_WAV_PCM && (w->flags & FML_R_PLAYABLE) && w->durMs == 2000 && (w->flags & FML_R_THUMB_FAIL),
        "WAV PCM: 2 s, sin imagen (dur=%u)", w ? w->durMs : 0);
  const FlexMlRec* mu = recAt(FML_DIR_AUDIO "/mulaw.wav");
  CHECK(mu && mu->fmt == FML_F_WAV_OTHER && !(mu->flags & FML_R_PLAYABLE), "WAV con otro codec: no reproducible");
  const FlexMlRec* dr = recAt("/Paint/lienzo.fxp");
  CHECK(dr && (dr->flags & FML_R_THUMB) && dr->w == 320 && dr->h == 480 && g_paintCalls == 2, "dibujo de Paint: miniatura por su lector");
  const FlexMlRec* rt = recAt("/Paint/roto.fxp");
  CHECK(rt && rt->state == FML_S_ERROR && rt->err == FML_E_CORRUPT && !(rt->flags & FML_R_PLAYABLE), "dibujo ilegible: danado");

  // Foto por encima del tope del visor: no se decodifica.
  fresh();
  std::vector<uint8_t> huge = makeJpeg(64, 64); huge.resize(FML_LIMIT_PHOTO + 10, 0);
  put(FML_DIR_PHOTO "/enorme.jpg", huge);
  flexMsScan(&g_ms, nullptr, nullptr); runAll();
  const FlexMlRec* e = recAt(FML_DIR_PHOTO "/enorme.jpg");
  CHECK(e && !(e->flags & FML_R_PLAYABLE) && (e->flags & FML_R_THUMB_FAIL) && e->state == FML_S_READY,
        "foto mayor que el tope: guardada, sin abrir");

  // El resultado ya no es de ese registro: se descarta.
  fresh();
  put(FML_DIR_PHOTO "/x.jpg", makeJpeg(100, 80));
  flexMsScan(&g_ms, nullptr, nullptr);
  CHECK(flexMsNextJob(&g_ms, 0, &j), "trabajo");
  CHECK(flexMsDelete(&g_ms, j.id), "se borra antes de hacerlo");
  CHECK(flexMsRunJob(&g_ms, &j, 64u << 20, 1u << 20) == FLEXMS_JOB_GONE && countUnder(FML_DIR_THUMB) == 0 &&
        countUnder(FML_DIR_TMP) == 0, "borrado entretanto: nada se escribe");
  put(FML_DIR_PHOTO "/y.jpg", makeJpeg(100, 80));
  flexMsScan(&g_ms, nullptr, nullptr);
  CHECK(flexMsNextJob(&g_ms, 0, &j), "otro trabajo");
  char why[96];
  CHECK(flexMsSetLock(&g_ms, j.id, true, why, sizeof(why)), "se bloquea antes de hacerlo");
  CHECK(flexMsRunJob(&g_ms, &j, 64u << 20, 1u << 20) == FLEXMS_JOB_GONE && countUnder(FML_DIR_THUMB) == 0,
        "bloqueado entretanto: ninguna miniatura a la vista");
  CHECK(!flexMsNextJob(&g_ms, 0, &j), "un protegido no genera trabajos");
  // El archivo desaparece por fuera antes de leerlo: no es "danado".
  put(FML_DIR_PHOTO "/z.jpg", makeJpeg(50, 50));
  flexMsScan(&g_ms, nullptr, nullptr);
  CHECK(flexMsNextJob(&g_ms, 0, &j), "trabajo de z");
  { FSLOCK; g_fs.files.erase(FML_DIR_PHOTO "/z.jpg"); }
  CHECK(flexMsRunJob(&g_ms, &j, 64u << 20, 1u << 20) == FLEXMS_JOB_GONE && rec(j.id)->state != FML_S_ERROR,
        "desaparecido: lo retirara la reconciliacion, no se marca danado");

  // Disco lleno al escribir la miniatura: la foto sigue bien.
  fresh();
  put(FML_DIR_PHOTO "/w.jpg", makeJpeg(300, 200));
  flexMsScan(&g_ms, nullptr, nullptr);
  g_fs.failWriteAfter = 100;
  runAll();
  g_fs.failWriteAfter = -1;
  const FlexMlRec* wr = recAt(FML_DIR_PHOTO "/w.jpg");
  CHECK(wr && (wr->flags & FML_R_PLAYABLE) && (wr->flags & FML_R_THUMB_FAIL) && wr->state == FML_S_READY &&
        countUnder(FML_DIR_TMP) == 0 && countUnder(FML_DIR_THUMB) == 0, "miniatura sin sitio: la foto no se da por danada");
  lockRules("trabajos");
}

static void testLock(){
  std::printf("-- bloquear y desbloquear: el archivo y su miniatura se mueven --\n");
  fresh();
  auto jpg = makeJpeg(200, 150);
  put(FML_DIR_PHOTO "/Playa 2024.jpg", jpg);
  flexMsScan(&g_ms, nullptr, nullptr); runAll();
  uint32_t id = recAt(FML_DIR_PHOTO "/Playa 2024.jpg")->id;
  std::string pubThumb = thumbOf(id);
  CHECK(has(pubThumb), "miniatura publica antes");
  uint8_t tv = rec(id)->thumbVer; uint32_t rv = g_ms.lib.rev;
  char why[96];
  CHECK(flexMsSetLock(&g_ms, id, true, why, sizeof(why)), "bloquear: %s", why);
  std::string lp = FML_DIR_LOCKED "/" + std::to_string(id) + ".jpg";
  CHECK(has(lp) && get(lp) == jpg && !has(FML_DIR_PHOTO "/Playa 2024.jpg"), "el archivo pasa, intacto, a la carpeta protegida con nombre neutro");
  CHECK(!has(pubThumb) && has(FML_DIR_LOCKED "/" + std::to_string(id) + ".t.jpg"), "y su miniatura sale de la carpeta publica");
  const FlexMlRec* r = rec(id);
  CHECK((r->flags & FML_R_LOCKED) && r->thumbVer != tv && g_ms.lib.rev != rv && !strcmp(r->path, lp.c_str()),
        "registro: bloqueado, misma id, nueva version de miniatura");
  CHECK(!strcmp(r->name, "Playa 2024.jpg"), "el nombre original se conserva en el catalogo");
  CHECK(flexMsSetLock(&g_ms, id, true, why, sizeof(why)) && has(lp), "bloquear dos veces no mueve nada");
  int n0 = g_ms.lib.n;
  flexMsScan(&g_ms, nullptr, nullptr);
  CHECK(g_ms.lib.n == n0 && (rec(id)->flags & FML_R_LOCKED), "el recorrido no lo duplica");
  CHECK(!flexMsTrash(&g_ms, id) && has(lp), "un protegido NUNCA va a la papelera (publica)");
  CHECK(!flexMsSetExtThumb(&g_ms, id, FML_DIR_TMP "/nada.jpg"), "ni recibe miniatura de fuera");

  // Desbloquear: vuelve con su nombre; si esta ocupado, con otro libre.
  put(FML_DIR_PHOTO "/Playa 2024.jpg", makeJpeg(10, 10));      // alguien ocupo el nombre
  CHECK(flexMsSetLock(&g_ms, id, false, why, sizeof(why)), "desbloquear: %s", why);
  r = rec(id);
  CHECK(!(r->flags & FML_R_LOCKED) && !strcmp(r->path, FML_DIR_PHOTO "/Playa 2024 (2).jpg") && get(r->path) == jpg,
        "vuelve a su carpeta sin pisar a nadie (%s)", r->path);
  CHECK(has(thumbOf(id)) && countUnder(FML_DIR_LOCKED) == 0, "con su miniatura, y la carpeta protegida queda vacia");

  // El movimiento falla: nada cambia y se dice por que.
  g_fs.failMoveFrom.insert(r->path);
  std::string keep = r->path;
  CHECK(!flexMsSetLock(&g_ms, id, true, why, sizeof(why)) && why[0], "si no se puede mover, no se bloquea (%s)", why);
  r = rec(id);
  CHECK(!(r->flags & FML_R_LOCKED) && has(keep) && keep == r->path, "y todo sigue donde estaba");
  g_fs.failMoveFrom.clear();

  // La miniatura no puede acompanarle: se borra, no se queda a la vista.
  g_fs.failMoveFrom.insert(thumbOf(id));
  std::string pt = thumbOf(id);
  CHECK(flexMsSetLock(&g_ms, id, true, why, sizeof(why)), "bloquear con la miniatura atascada");
  CHECK(!has(pt), "la miniatura publica NO se queda");
  g_fs.failMoveFrom.clear();
  CHECK(flexMsSetLock(&g_ms, id, false, why, sizeof(why)), "desbloquear");
  r = rec(id);
  CHECK((r->flags & FML_R_NEED_THUMB) && !(r->flags & FML_R_THUMB), "al volver se rehace");
  runAll();
  CHECK(has(thumbOf(id)) && (rec(id)->flags & FML_R_THUMB), "y se rehace");

  // Catalogo perdido: lo protegido vuelve protegido.
  CHECK(flexMsSetLock(&g_ms, id, true, why, sizeof(why)), "bloquear otra vez");
  flexMlClear(&g_ms.lib);
  flexMsScan(&g_ms, nullptr, nullptr);
  const FlexMlRec* back = recAt(lp.c_str());
  CHECK(back && (back->flags & FML_R_LOCKED) && !strcmp(back->name, "Elemento protegido"),
        "sin catalogo, lo de la carpeta protegida vuelve bloqueado y sin nombre");
  CHECK(!flexMsSetLock(&g_ms, 999999, true, why, sizeof(why)) && why[0], "un id que no existe: no (%s)", why);
  lockRules("bloqueo");
}

static void testDeleteTrashRename(){
  std::printf("-- borrar, papelera y renombrar --\n");
  fresh();
  put(FML_DIR_PHOTO "/a.jpg", makeJpeg(60, 60));
  put(FML_DIR_PHOTO "/b.jpg", makeJpeg(60, 60, 2));
  put(FML_DIR_AUDIO "/c.wav", makeWav(8000, 100));
  flexMsScan(&g_ms, nullptr, nullptr); runAll();
  uint32_t a = recAt(FML_DIR_PHOTO "/a.jpg")->id, b = recAt(FML_DIR_PHOTO "/b.jpg")->id, c = recAt(FML_DIR_AUDIO "/c.wav")->id;
  std::string ta = thumbOf(a);

  g_fs.failRemove.insert(FML_DIR_PHOTO "/a.jpg");
  CHECK(!flexMsDelete(&g_ms, a) && rec(a) && has(FML_DIR_PHOTO "/a.jpg"), "si no se puede borrar, el registro se queda");
  g_fs.failRemove.clear();
  CHECK(flexMsDelete(&g_ms, a) && !rec(a) && !has(FML_DIR_PHOTO "/a.jpg") && !has(ta), "borrar: archivo, miniatura y registro");
  CHECK(!flexMsDelete(&g_ms, a), "borrar dos veces: no");

  std::string tb = thumbOf(b);
  CHECK(flexMsTrash(&g_ms, b) && !rec(b) && has("/Papelera/b.jpg") && !has(tb), "a la papelera: el archivo se conserva alli");
  FlexMsFs keepFs = g_ms.fs; g_ms.fs.trash = nullptr;
  CHECK(!flexMsTrash(&g_ms, c) && rec(c), "sin papelera: no se hace nada");
  g_ms.fs = keepFs;

  // Renombrar.
  put(FML_DIR_PHOTO "/d.jpg", makeJpeg(30, 30));
  put(FML_DIR_PHOTO "/Vacaciones.jpg", makeJpeg(30, 30, 9));
  flexMsScan(&g_ms, nullptr, nullptr);
  uint32_t d = recAt(FML_DIR_PHOTO "/d.jpg")->id;
  char why[96];
  CHECK(flexMsRename(&g_ms, d, "Vacaciones", why, sizeof(why)), "renombrar: %s", why);
  CHECK(!strcmp(rec(d)->path, FML_DIR_PHOTO "/Vacaciones (2).jpg") && !strcmp(rec(d)->name, "Vacaciones.jpg"),
        "sin pisar al que ya se llamaba asi (%s | %s)", rec(d)->path, rec(d)->name);
  CHECK(flexMsRename(&g_ms, d, "Vacaciones (2)", why, sizeof(why)) && !strcmp(rec(d)->path, FML_DIR_PHOTO "/Vacaciones (2).jpg"),
        "el mismo nombre no se convierte en '(2) (2)' (%s)", rec(d)->path);
  CHECK(!flexMsRename(&g_ms, d, "   ", why, sizeof(why)) && why[0], "vacio: no (%s)", why);
  CHECK(flexMsRename(&g_ms, d, "../../etc/passwd", why, sizeof(why)) && strstr(rec(d)->path, FML_DIR_PHOTO "/") == rec(d)->path &&
        !strstr(rec(d)->path, ".."), "un nombre con ruta no sale de su carpeta (%s)", rec(d)->path);
  CHECK(flexMsRename(&g_ms, d, "Año nuevo en Málaga", why, sizeof(why)) && !strcmp(rec(d)->name, "Año nuevo en Málaga.jpg") &&
        !strcmp(rec(d)->path, FML_DIR_PHOTO "/Ano nuevo en Malaga.jpg"), "UTF-8 para ensenar, ASCII seguro en disco (%s)", rec(d)->path);
  CHECK(!flexMsRename(&g_ms, 424242, "x", why, sizeof(why)), "un id que no existe: no");
  // Un protegido solo cambia el nombre que se ensena.
  CHECK(flexMsSetLock(&g_ms, d, true, why, sizeof(why)), "bloquear");
  std::string lp = rec(d)->path;
  CHECK(flexMsRename(&g_ms, d, "Secreto", why, sizeof(why)) && rec(d)->path == lp && !strcmp(rec(d)->name, "Secreto.jpg"),
        "protegido: su archivo sigue llamandose por su id");
  CHECK(flexMsSetLock(&g_ms, d, false, why, sizeof(why)) && !strcmp(rec(d)->path, FML_DIR_PHOTO "/Secreto.jpg"),
        "y al desbloquear sale con el nombre nuevo (%s)", rec(d)->path);
  CHECK(flexMsDelete(&g_ms, d) && !has(FML_DIR_PHOTO "/Secreto.jpg"), "borrar un (ex)protegido");
  // Borrar un protegido: se va de la carpeta protegida.
  put(FML_DIR_PHOTO "/p.jpg", makeJpeg(30, 30));
  flexMsScan(&g_ms, nullptr, nullptr); runAll();
  uint32_t pid = recAt(FML_DIR_PHOTO "/p.jpg")->id;
  CHECK(flexMsSetLock(&g_ms, pid, true, why, sizeof(why)), "bloquear p");
  CHECK(flexMsDelete(&g_ms, pid) && countUnder(FML_DIR_LOCKED) == 0, "borrar un protegido no deja nada en su carpeta");
  lockRules("borrar/renombrar");
}

static void testAddReplace(){
  std::printf("-- guardar como copia y reemplazar el original --\n");
  fresh();
  auto orig = makeJpeg(120, 90, 1);
  put(FML_DIR_PHOTO "/foto.jpg", orig);
  flexMsScan(&g_ms, nullptr, nullptr); runAll();
  uint32_t id = recAt(FML_DIR_PHOTO "/foto.jpg")->id;
  char why[96];

  // Copia: nombre seguro y libre, id nueva, miniatura pendiente.
  auto edited = makeJpeg(120, 90, 7);
  put(FML_DIR_TMP "/ed-1.jpg", edited);
  uint32_t cp = flexMsAddFile(&g_ms, FML_DIR_TMP "/ed-1.jpg", FML_K_PHOTO, "foto.jpg", FML_O_EDIT, id, why, sizeof(why));
  CHECK(cp && cp != id && !strcmp(rec(cp)->path, FML_DIR_PHOTO "/foto (2).jpg") && get(rec(cp)->path) == edited,
        "copia: id nueva y nombre libre (%s)", cp ? rec(cp)->path : why);
  CHECK((rec(cp)->flags & FML_R_NEED_THUMB) && rec(cp)->origin == FML_O_EDIT && get(FML_DIR_PHOTO "/foto.jpg") == orig,
        "el original ni se toca");
  CHECK(rec(cp)->parent == id && rec(id)->parent == 0, "la copia sabe de quien sale (parent)");
  runAll();
  CHECK(has(thumbOf(cp)), "y su miniatura se hace");

  // Reemplazar: misma id, contenido nuevo, miniatura de nuevo, nada apartado.
  auto v2 = makeJpeg(90, 120, 3);
  put(FML_DIR_TMP "/ed-2.jpg", v2);
  uint8_t tv = rec(id)->thumbVer;
  CHECK(flexMsReplace(&g_ms, id, FML_DIR_TMP "/ed-2.jpg", why, sizeof(why)), "reemplazar: %s", why);
  const FlexMlRec* r = rec(id);
  CHECK(!strcmp(r->path, FML_DIR_PHOTO "/foto.jpg") && get(r->path) == v2 && r->size == v2.size() && r->crc == 0,
        "misma ruta y misma id, contenido nuevo");
  CHECK((r->flags & FML_R_EDITED) && (r->flags & FML_R_NEED_THUMB) && !(r->flags & FML_R_THUMB) && r->thumbVer != tv &&
        !has(FML_DIR_PHOTO "/foto.jpg.fxorig") && !has(FML_DIR_TMP "/ed-2.jpg"), "editado, miniatura pendiente, nada apartado");
  runAll();
  int w = 0, h = 0;
  CHECK(rec(id)->w == 90 && rec(id)->h == 120 && jpegDims(get(thumbOf(id)), &w, &h), "y se rehace con las medidas nuevas");

  // El nuevo no puede ocupar su sitio: el original vuelve intacto.
  auto before = get(FML_DIR_PHOTO "/foto.jpg");
  put(FML_DIR_TMP "/ed-3.jpg", makeJpeg(50, 50));
  g_fs.failMoveFrom.insert(FML_DIR_TMP "/ed-3.jpg");
  CHECK(!flexMsReplace(&g_ms, id, FML_DIR_TMP "/ed-3.jpg", why, sizeof(why)) && why[0], "si falla (%s)", why);
  CHECK(get(FML_DIR_PHOTO "/foto.jpg") == before && !has(FML_DIR_PHOTO "/foto.jpg.fxorig") && !(rec(id)->flags & FML_R_NEED_THUMB),
        "...el original sigue ahi, entero, y el registro igual");
  g_fs.failMoveFrom.clear();
  // No se puede ni apartar el original: tampoco se toca nada.
  g_fs.failMoveFrom.insert(FML_DIR_PHOTO "/foto.jpg");
  CHECK(!flexMsReplace(&g_ms, id, FML_DIR_TMP "/ed-3.jpg", why, sizeof(why)) && get(FML_DIR_PHOTO "/foto.jpg") == before &&
        has(FML_DIR_TMP "/ed-3.jpg"), "sin apartar el original no hay reemplazo");
  g_fs.failMoveFrom.clear();

  // Corte de luz a mitad (lo que queda en el disco en cada punto).
  //  a) original apartado, el nuevo aun no: vuelve el original.
  { FSLOCK; moveU(FML_DIR_PHOTO "/foto.jpg", FML_DIR_PHOTO "/foto.jpg.fxorig"); }
  flexMsScan(&g_ms, nullptr, nullptr);
  CHECK(get(FML_DIR_PHOTO "/foto.jpg") == before && !has(FML_DIR_PHOTO "/foto.jpg.fxorig") && rec(id),
        "corte tras apartar: el original vuelve y conserva su id");
  //  b) el nuevo ya en su sitio y el apartado sin borrar: sobra el apartado.
  put(FML_DIR_PHOTO "/foto.jpg.fxorig", orig);
  flexMsScan(&g_ms, nullptr, nullptr);
  CHECK(get(FML_DIR_PHOTO "/foto.jpg") == before && !has(FML_DIR_PHOTO "/foto.jpg.fxorig"), "corte al final: se queda la version nueva");
  //  c) sin catalogo: el original recuperado se registra en el mismo recorrido.
  { FSLOCK; moveU(FML_DIR_PHOTO "/foto.jpg", FML_DIR_PHOTO "/foto.jpg.fxorig"); }
  flexMlClear(&g_ms.lib);
  flexMsScan(&g_ms, nullptr, nullptr);
  CHECK(recAt(FML_DIR_PHOTO "/foto.jpg") && has(FML_DIR_PHOTO "/foto.jpg"), "recuperado y registrado a la vez");
  //  d) un archivo AJENO que acaba igual y no es un medio no se toca.
  put(FML_DIR_PHOTO "/notas.fxorig", {1, 2, 3});
  flexMsScan(&g_ms, nullptr, nullptr);
  CHECK(has(FML_DIR_PHOTO "/notas.fxorig"), "un archivo ajeno con esa terminacion se respeta");

  // Catalogo lleno: la copia vuelve al temporal (y quien llama lo borra).
  fresh(1);
  put(FML_DIR_PHOTO "/uno.jpg", makeJpeg(20, 20));
  flexMsScan(&g_ms, nullptr, nullptr);
  put(FML_DIR_TMP "/ed-9.jpg", makeJpeg(20, 20, 5));
  CHECK(!flexMsAddFile(&g_ms, FML_DIR_TMP "/ed-9.jpg", FML_K_PHOTO, "dos.jpg", FML_O_EDIT, 0, why, sizeof(why)) && why[0] &&
        has(FML_DIR_TMP "/ed-9.jpg") && !has(FML_DIR_PHOTO "/dos.jpg"), "catalogo lleno: no se publica (%s)", why);
  lockRules("copias/reemplazo");
}

static void testCommitUpload(){
  std::printf("-- publicar lo que sube el movil --\n");
  fresh();
  auto jpg = makeJpeg(800, 600);
  put(FML_DIR_TMP "/up-1", jpg);
  put(FML_DIR_TMP "/up-1.th", makeJpeg(FLEXTH_SIDE, FLEXTH_SIDE));
  FlexMsUpload u; std::memset(&u, 0, sizeof(u));
  u.tmpPath = FML_DIR_TMP "/up-1"; u.thumbTmp = FML_DIR_TMP "/up-1.th";
  u.name = "IMG_2024 ñ.jpg"; u.title = ""; u.artist = ""; u.album = "";
  u.kind = FML_K_PHOTO; u.fmt = FML_F_JPEG; u.playable = true; u.size = (uint32_t)jpg.size();
  u.crc = flexMlCrc32(0, jpg.data(), jpg.size()); u.created = 1700000000u; u.w = 800; u.h = 600;
  char why[96];
  uint32_t id = flexMsCommitUpload(&g_ms, &u, why, sizeof(why));
  CHECK(id != 0, "publicada (%s)", why);
  const FlexMlRec* r = rec(id);
  CHECK(r && !strcmp(r->path, FML_DIR_PHOTO "/IMG_2024 n.jpg") && get(r->path) == jpg && !strcmp(r->name, "IMG_2024 ñ.jpg"),
        "en su carpeta con nombre seguro, y el original para ensenar (%s)", r ? r->path : "-");
  CHECK((r->flags & FML_R_THUMB) && (r->flags & FML_R_PLAYABLE) && r->origin == FML_O_WEB && r->crc == u.crc &&
        has(thumbOf(id)) && countUnder(FML_DIR_TMP) == 0, "con miniatura y sin temporales");
  CHECK(flexMsFindDup(&g_ms, u.size, u.crc) == id, "el duplicado exacto se reconoce");

  // Mismo nombre: no se pisa.
  put(FML_DIR_TMP "/up-2", jpg);
  u.tmpPath = FML_DIR_TMP "/up-2"; u.thumbTmp = "";
  uint32_t id2 = flexMsCommitUpload(&g_ms, &u, why, sizeof(why));
  CHECK(id2 && !strcmp(rec(id2)->path, FML_DIR_PHOTO "/IMG_2024 n (2).jpg") && (rec(id2)->flags & FML_R_THUMB_FAIL),
        "mismo nombre: '(2)', y sin miniatura se dice (%s)", id2 ? rec(id2)->path : why);

  // Audio: a Musica, con sus etiquetas.
  auto wav = makeWav(22050, 22050);
  put(FML_DIR_TMP "/up-3", wav);
  FlexMsUpload a; std::memset(&a, 0, sizeof(a));
  a.tmpPath = FML_DIR_TMP "/up-3"; a.thumbTmp = ""; a.name = "Canción.mp3"; a.title = "Canción"; a.artist = "Grupo"; a.album = "Disco";
  a.kind = FML_K_AUDIO; a.fmt = FML_F_WAV_PCM; a.playable = true; a.size = (uint32_t)wav.size(); a.durMs = 1000;
  uint32_t ia = flexMsCommitUpload(&g_ms, &a, why, sizeof(why));
  CHECK(ia && !strcmp(rec(ia)->path, FML_DIR_AUDIO "/Cancion.wav") && !strcmp(rec(ia)->title, "Canción") &&
        !strcmp(rec(ia)->artist, "Grupo") && rec(ia)->kind == FML_K_AUDIO, "audio: a Musica con la extension de su formato real (%s)",
        ia ? rec(ia)->path : why);

  // El movimiento falla: 0 y el temporal sigue ahi para que el servidor lo borre.
  put(FML_DIR_TMP "/up-4", jpg);
  u.tmpPath = FML_DIR_TMP "/up-4"; u.name = "otra.jpg";
  g_fs.failMoveFrom.insert(FML_DIR_TMP "/up-4");
  CHECK(!flexMsCommitUpload(&g_ms, &u, why, sizeof(why)) && why[0] && has(FML_DIR_TMP "/up-4") && !recAt(FML_DIR_PHOTO "/otra.jpg"),
        "si no se puede mover, no se publica (%s)", why);
  g_fs.failMoveFrom.clear();

  // Miniatura externa (formato que el P4 no decodifica).
  put(FML_DIR_TMP "/up-5", {0, 0, 0, 0x18, 'f', 't', 'y', 'p', 'h', 'e', 'i', 'c'});
  FlexMsUpload hc = u; hc.tmpPath = FML_DIR_TMP "/up-5"; hc.thumbTmp = ""; hc.name = "IMG.heic"; hc.fmt = FML_F_HEIC; hc.playable = false; hc.size = 12;
  uint32_t ih = flexMsCommitUpload(&g_ms, &hc, why, sizeof(why));
  CHECK(ih && !(rec(ih)->flags & FML_R_PLAYABLE) && rec(ih)->err == FML_E_UNSUPPORTED && strstr(rec(ih)->path, ".heic"),
        "HEIC: se guarda tal cual, marcado como no abrible");
  put(FML_DIR_TMP "/th-ext", makeJpeg(FLEXTH_SIDE, FLEXTH_SIDE, 3));
  uint8_t tv = rec(ih)->thumbVer;
  CHECK(flexMsSetExtThumb(&g_ms, ih, FML_DIR_TMP "/th-ext") && (rec(ih)->flags & FML_R_EXT_THUMB) && (rec(ih)->flags & FML_R_THUMB) &&
        rec(ih)->thumbVer != tv && has(thumbOf(ih)), "su miniatura la aporta el navegador");
  // Una miniatura externa sobrevive a un cambio de tamano detectado por el recorrido.
  flexMsScan(&g_ms, nullptr, nullptr);
  CHECK((rec(ih)->flags & FML_R_EXT_THUMB) && !(rec(ih)->flags & FML_R_NEED_THUMB), "y el recorrido no la descarta");

  // Catalogo lleno: el archivo vuelve al temporal.
  fresh(1);
  put(FML_DIR_TMP "/up-6", jpg);
  put(FML_DIR_TMP "/up-7", jpg);
  u.thumbTmp = ""; u.name = "a.jpg"; u.tmpPath = FML_DIR_TMP "/up-6";
  CHECK(flexMsCommitUpload(&g_ms, &u, why, sizeof(why)) != 0, "cabe uno");
  u.name = "b.jpg"; u.tmpPath = FML_DIR_TMP "/up-7";
  CHECK(!flexMsCommitUpload(&g_ms, &u, why, sizeof(why)) && has(FML_DIR_TMP "/up-7") && !has(FML_DIR_PHOTO "/b.jpg") && why[0],
        "catalogo lleno: nada publicado, el temporal queda para borrarse (%s)", why);
  lockRules("subidas");
}

// =============================================================
//  A LA VEZ: interfaz, tarea de fondo y servidor
// =============================================================
static void testConcurrency(){
  std::printf("-- interfaz, tarea de fondo y servidor a la vez --\n");
  fresh();
  const int N = 24;
  for(int i = 0; i < N; i++) put(FML_DIR_PHOTO "/c" + std::to_string(i) + ".jpg", makeJpeg(48, 32, i));
  flexMsScan(&g_ms, nullptr, nullptr);
  std::vector<uint32_t> ids;
  for(int i = 0; i < g_ms.lib.n; i++) ids.push_back(g_ms.lib.recs[i].id);
  std::atomic<bool> stop{false};
  std::atomic<int> scans{0}, locks{0}, jobs{0}, ups{0};
  std::thread scanner([&]{ while(!stop){ flexMsScan(&g_ms, nullptr, nullptr); scans++; } });
  std::thread worker([&]{
    while(!stop){ FlexMsJob j; uint32_t after = 0;
      while(!stop && flexMsNextJob(&g_ms, after, &j)){ after = j.id; flexMsRunJob(&g_ms, &j, 64u << 20, 1u << 20); jobs++; }
      std::this_thread::yield(); }
  });
  std::thread server([&]{
    auto jpg = makeJpeg(40, 30, 99);
    for(int k = 0; k < 30 && !stop; k++){
      std::string tp = FML_DIR_TMP "/cu-" + std::to_string(k);
      put(tp, jpg);
      FlexMsUpload u; std::memset(&u, 0, sizeof(u));
      std::string nm = "web" + std::to_string(k % 5) + ".jpg";
      u.tmpPath = tp.c_str(); u.thumbTmp = ""; u.name = nm.c_str(); u.title = ""; u.artist = ""; u.album = "";
      u.kind = FML_K_PHOTO; u.fmt = FML_F_JPEG; u.playable = true; u.size = (uint32_t)jpg.size();
      char why[96];
      if(flexMsCommitUpload(&g_ms, &u, why, sizeof(why))) ups++;
      else { FSLOCK; g_fs.files.erase(tp); }
    }
  });
  // La interfaz: bloquear, desbloquear, renombrar, borrar.
  char why[96];
  for(int round = 0; round < 60; round++){
    uint32_t id = ids[(size_t)round % ids.size()];
    FlexMlRec r;
    if(!flexMsGet(&g_ms, id, &r)) continue;
    if(round % 7 == 6){ flexMsDelete(&g_ms, id); continue; }
    if(round % 3 == 2) flexMsRename(&g_ms, id, ("renombrada " + std::to_string(round)).c_str(), why, sizeof(why));
    if(flexMsSetLock(&g_ms, id, !(r.flags & FML_R_LOCKED), why, sizeof(why))) locks++;
  }
  server.join();
  stop = true;
  scanner.join(); worker.join();
  flexMsScan(&g_ms, nullptr, nullptr);
  runAll();
  CHECK(scans > 0 && locks > 0 && jobs > 0 && ups > 0, "hubo trabajo a la vez (recorridos %d, bloqueos %d, trabajos %d, subidas %d)",
        (int)scans, (int)locks, (int)jobs, (int)ups);

  // Invariantes: ningun duplicado, ningun fantasma, ningun huerfano y
  // ningun protegido a la vista.
  std::set<std::string> paths; int dups = 0, ghosts = 0, badLock = 0, pubThumbOfLocked = 0;
  for(int i = 0; i < g_ms.lib.n; i++){
    const FlexMlRec* r = &g_ms.lib.recs[i];
    if(!paths.insert(r->path).second) dups++;
    if(!has(r->path)) ghosts++;
    bool inLocked = std::string(r->path).rfind(FML_DIR_LOCKED "/", 0) == 0;
    if(((r->flags & FML_R_LOCKED) != 0) != inLocked) badLock++;
    if((r->flags & FML_R_LOCKED) && has(FML_DIR_THUMB "/" + std::to_string(r->id) + ".jpg")) pubThumbOfLocked++;
  }
  int orphans = 0;
  {
    FSLOCK;
    for(auto& kv : g_fs.files){
      const std::string& p = kv.first;
      bool media = (p.rfind(FML_DIR_PHOTO "/", 0) == 0 || p.rfind(FML_DIR_LOCKED "/", 0) == 0) && p.size() > 4 &&
                   p.compare(p.size() - 4, 4, ".jpg") == 0 && p.find(".t.jpg") == std::string::npos;
      if(media && flexMlFindPath(&g_ms.lib, p.c_str()) < 0) orphans++;
    }
  }
  CHECK(dups == 0 && ghosts == 0 && orphans == 0, "catalogo y disco coinciden (duplicados %d, fantasmas %d, huerfanos %d)", dups, ghosts, orphans);
  CHECK(badLock == 0 && pubThumbOfLocked == 0, "bloqueado <=> en la carpeta protegida, sin miniatura publica (%d, %d)", badLock, pubThumbOfLocked);
  CHECK(countUnder(FML_DIR_TMP) == 0, "ningun temporal abandonado (%d)", countUnder(FML_DIR_TMP));
  lockRules("concurrencia");
}

int main(){
  std::printf("=== FlexOS_MediaStore ===\n");
  testPersistence();
  testCleanTmp();
  testScan();
  testJobs();
  testLock();
  testDeleteTrashRename();
  testAddReplace();
  testCommitUpload();
  testConcurrency();
  std::printf("\n%d comprobaciones, %d fallos\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
