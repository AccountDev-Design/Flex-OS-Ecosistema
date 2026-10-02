// #############################################################
//  test_mediaweb.cpp  ·  Flex Web Server de extremo a extremo
// #############################################################
//
//  El servidor ENTERO (FlexOS_MediaWeb + protocolo + catalogo +
//  miniaturas) contra un movil simulado que habla HTTP de verdad, un
//  sistema de archivos en memoria que sabe llenarse y un anfitrion que
//  hace lo mismo que la placa al publicar un archivo.
//
//  Lo que importa aqui no es "responde 200", es lo que el servidor se
//  NIEGA a hacer:
//    · publicar un archivo que no llego entero, que llego danado (CRC),
//      cuyo contenido no es lo que dice, o que no se puede decodificar;
//    · dejar un temporal en el disco por cualquier camino de error
//      (desconexion, silencio, disco lleno, JPEG roto);
//    · ensenar a alguien sin sesion de propietario NADA de un elemento
//      bloqueado: ni su tipo, ni su miniatura, ni su contenido, ni dentro
//      de un ZIP;
//    · aceptar el codigo o la clave sin limite de intentos;
//    · conservar el nivel de propietario cuando el P4 se bloquea.
//  El ZIP se valida con el modulo zipfile de Python: una referencia que no
//  comparte ni una linea con el codigo bajo prueba.

#include "../../FlexOS_Ultra/FlexOS_MediaWeb.h"
#include "../../FlexOS_Ultra/FlexOS_MediaStore.h"
#include "../../FlexOS_Ultra/FlexOS_MediaThumb.h"
#include "../../FlexOS_Ultra/FlexOS_JPEGEnc.h"
#include "../../FlexOS_Ultra/FlexOS_WebUI.h"
#include "vendor/cJSON/cJSON.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <functional>

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

// =============================================================
//  Sistema de archivos en memoria
// =============================================================
struct MemFs {
  std::map<std::string, std::vector<uint8_t>> files;
  uint32_t total = 10u * 1024u * 1024u;
  long failWriteAfter = -1;          // bytes que se aceptan antes de "disco lleno"
  long written = 0;
  uint32_t used() const { uint32_t n = 0; for(auto& kv : files) n += (uint32_t)kv.second.size(); return n; }
  int tmpCount() const { int n = 0; for(auto& kv : files) if(kv.first.rfind(FML_DIR_TMP "/", 0) == 0) n++; return n; }
};
static MemFs g_fs;
struct H { std::string path; size_t pos = 0; bool wr = false; };
static void* fsOpen(void*, const char* p, bool wr){
  if(wr){ g_fs.files[p].clear(); }
  else if(!g_fs.files.count(p)) return nullptr;
  H* h = new H; h->path = p; h->wr = wr; return h;
}
static int fsRead(void*, void* hh, uint8_t* b, size_t n){
  H* h = (H*)hh; auto it = g_fs.files.find(h->path); if(it == g_fs.files.end()) return -1;
  auto& d = it->second; if(h->pos >= d.size()) return 0;
  size_t k = d.size() - h->pos; if(n > k) n = k; std::memcpy(b, d.data() + h->pos, n); h->pos += n; return (int)n;
}
static bool fsWrite(void*, void* hh, const uint8_t* b, size_t n){
  H* h = (H*)hh;
  if(g_fs.failWriteAfter >= 0 && g_fs.written + (long)n > g_fs.failWriteAfter) return false;
  g_fs.written += (long)n;
  auto& d = g_fs.files[h->path];
  if(h->pos + n > d.size()) d.resize(h->pos + n);
  std::memcpy(d.data() + h->pos, b, n); h->pos += n; return true;
}
static bool fsSeek(void*, void* hh, uint32_t off){ H* h = (H*)hh; if(off > g_fs.files[h->path].size()) return false; h->pos = off; return true; }
static void fsClose(void*, void* hh){ delete (H*)hh; }
static uint32_t fsSize(void*, const char* p){ auto it = g_fs.files.find(p); return it == g_fs.files.end() ? 0 : (uint32_t)it->second.size(); }
static bool fsRemove(void*, const char* p){ return g_fs.files.erase(p) > 0; }
static uint32_t fsFree(void*){ uint32_t u = g_fs.used(); return u >= g_fs.total ? 0 : g_fs.total - u; }
static uint32_t fsTotal(void*){ return g_fs.total; }
static bool fsRename(const char* a, const char* b){
  auto it = g_fs.files.find(a); if(it == g_fs.files.end() || g_fs.files.count(b)) return false;
  g_fs.files[b] = it->second; g_fs.files.erase(a); return true;
}

// =============================================================
//  Anfitrion: el MISMO almacen que usa la placa (FlexOS_MediaStore)
//  publica las subidas, pone las miniaturas y bloquea.
// =============================================================
static FlexMlRec g_store[FML_CAP];
static FlexMediaStore g_mst;
static FlexMlLib& g_lib = g_mst.lib;
static uint32_t g_ms = 1000;
static int g_lockType = 1;
static int g_verifyCalls = 0;
static uint32_t g_rng = 0xA5A5A5A5u;
static std::vector<FlexWebXfer> g_ev;

static bool fsExists(void*, const char* p){
  if(g_fs.files.count(p)) return true;
  std::string pre = std::string(p) + "/";
  auto it = g_fs.files.lower_bound(pre);
  return it != g_fs.files.end() && it->first.compare(0, pre.size(), pre) == 0;
}
static bool fsMove(void*, const char* a, const char* b){ return fsRename(a, b); }
static bool fsMkdir(void*, const char*){ return true; }
static int fsList(void*, const char*, FlexMsEntry*, int, int){ return -1; }   // aqui no se reconcilia
static bool fsAtomic(void*, const char* p, const void* b, size_t n){ g_fs.files[p].assign((const uint8_t*)b, (const uint8_t*)b + n); return true; }
static uint32_t msNowCb(void*){ return 1700000000u; }
static void thumbPathOf(const FlexMlRec* r, char* out, size_t cap){ flexMsThumbPath(r, out, cap); }
static int hSnapshot(void*, FlexMlRec* d, int cap, uint32_t* rev){ return flexMsSnapshot(&g_mst, d, cap, rev); }
static bool hGet(void*, uint32_t id, FlexMlRec* o){ return flexMsGet(&g_mst, id, o); }
static uint32_t hRev(void*){ return flexMsRev(&g_mst); }
static uint32_t hDup(void*, uint32_t size, uint32_t crc){ return flexMsFindDup(&g_mst, size, crc); }
static uint32_t hCommit(void*, const FlexWebUpload* up, char* why, size_t cap){
  FlexMsUpload u;
  u.tmpPath = up->tmpPath; u.thumbTmp = up->thumbTmp;
  u.name = up->name; u.title = up->title; u.artist = up->artist; u.album = up->album;
  u.kind = up->kind; u.fmt = up->fmt; u.playable = up->playable;
  u.size = up->size; u.crc = up->crc; u.created = up->created; u.durMs = up->durMs; u.w = up->w; u.h = up->h;
  return flexMsCommitUpload(&g_mst, &u, why, cap);
}
static bool hSetThumb(void*, uint32_t id, const char* tmp){ return flexMsSetExtThumb(&g_mst, id, tmp); }
static void hThumbPath(void*, const FlexMlRec* r, char* out, size_t cap){ flexMsThumbPath(r, out, cap); }
static bool hVerify(void*, const char* s){ g_verifyCalls++; return !strcmp(s, "2468"); }
static int hLockType(void*){ return g_lockType; }
static uint32_t hNow(void*){ return g_ms; }
static uint32_t hEpoch(void*){ return 1760000000u; }
static void hRand(void*, uint8_t* o, size_t n){ for(size_t i = 0; i < n; i++){ g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; o[i] = (uint8_t)g_rng; } }
static void hEvent(void*, const FlexWebXfer* x){ g_ev.push_back(*x); }
static bool g_others = false;
static bool hOthers(void*){ return g_others; }

static long g_live = 0;
static void* tA(size_t n){ void* p = std::malloc(n); if(p) g_live++; return p; }
static void tF(void* p){ if(p){ g_live--; std::free(p); } }

static FlexWebCtx g_w;
static uint8_t g_hdr[FLEXWEB_HDR_BUF], g_io[FLEXWEB_IO_BUF];

static void setup(){
  g_fs = MemFs();
  g_fs.files[FML_DIR_TMP "/.keep"];       // solo para que existan las carpetas en el listado
  g_fs.files.erase(FML_DIR_TMP "/.keep");
  std::memset(&g_mst, 0, sizeof(g_mst));
  g_mst.fs = { fsOpen, fsRead, fsWrite, fsSeek, fsClose, fsSize, fsExists, fsRemove, fsMove, nullptr, fsMkdir, fsList, fsAtomic, nullptr };
  g_mst.now = msNowCb;
  flexMsInit(&g_mst, g_store, FML_CAP);
  g_ms = 1000; g_lockType = 1; g_verifyCalls = 0; g_ev.clear();
  std::memset(&g_w, 0, sizeof(g_w));
  g_w.fs = { fsOpen, fsRead, fsWrite, fsSeek, fsClose, fsSize, fsRemove, fsFree, fsTotal, nullptr };
  g_w.host = { hSnapshot, hGet, hRev, hDup, hCommit, hSetThumb, hThumbPath, hVerify, hLockType,
               hNow, hEpoch, hRand, hEvent, nullptr, hOthers, nullptr };
  g_others = false;
  g_w.alloc = tA; g_w.free = tF;
  snprintf(g_w.ip, sizeof(g_w.ip), "192.168.1.50");
  g_w.port = 8080; g_w.allowUpload = true;
  flexWebInit(&g_w);
}

// =============================================================
//  El movil simulado
// =============================================================
struct Client {
  std::string in;           // lo que el movil envia
  size_t pos = 0;
  size_t chunk = 100000;    // trocea la entrega (TCP)
  long hangAfter = -1;      // tras N bytes: silencio (plazo vencido)
  long closeAfter = -1;     // tras N bytes: se cierra
  std::string out;          // lo que responde el servidor
  int reads = 0;            // llamadas a read (trozos de espera incluidos)
};
static int cRead(void* c, uint8_t* b, size_t n, uint32_t){
  Client* k = (Client*)c;
  k->reads++;
  if(k->closeAfter >= 0 && (long)k->pos >= k->closeAfter) return -1;
  if(k->hangAfter >= 0 && (long)k->pos >= k->hangAfter) return 0;
  if(k->pos >= k->in.size()) return -1;
  size_t t = k->in.size() - k->pos;
  if(t > n) t = n;
  if(t > k->chunk) t = k->chunk;
  if(k->closeAfter >= 0 && (long)(k->pos + t) > k->closeAfter) t = (size_t)k->closeAfter - k->pos;
  if(k->hangAfter >= 0 && (long)(k->pos + t) > k->hangAfter) t = (size_t)k->hangAfter - k->pos;
  std::memcpy(b, k->in.data() + k->pos, t); k->pos += t; return (int)t;
}
static bool cWrite(void* c, const uint8_t* b, size_t n){ ((Client*)c)->out.append((const char*)b, n); return true; }

struct Resp { int status = 0; std::map<std::string, std::string> h; std::string body; };
static std::string lower(std::string s){ for(auto& ch : s) ch = (char)tolower((unsigned char)ch); return s; }
static std::vector<Resp> parseAll(const std::string& raw){
  std::vector<Resp> v; size_t p = 0;
  while(p < raw.size()){
    size_t e = raw.find("\r\n\r\n", p); if(e == std::string::npos) break;
    Resp r; std::string head = raw.substr(p, e - p); p = e + 4;
    size_t ln = head.find("\r\n");
    std::string sl = head.substr(0, ln);
    r.status = std::atoi(sl.c_str() + 9);
    size_t q = ln == std::string::npos ? head.size() : ln + 2;
    while(q < head.size()){
      size_t le = head.find("\r\n", q); if(le == std::string::npos) le = head.size();
      std::string line = head.substr(q, le - q); q = le + 2;
      size_t c = line.find(':'); if(c == std::string::npos) continue;
      std::string k = lower(line.substr(0, c)), val = line.substr(c + 1);
      while(!val.empty() && val[0] == ' ') val.erase(0, 1);
      r.h[k] = val;
    }
    if(r.h.count("content-length")){
      size_t n = (size_t)std::atol(r.h["content-length"].c_str());
      r.body = raw.substr(p, n); p += n;
    } else if(r.h.count("transfer-encoding")){
      for(;;){
        size_t le = raw.find("\r\n", p); if(le == std::string::npos) break;
        size_t n = std::strtoul(raw.substr(p, le - p).c_str(), nullptr, 16); p = le + 2;
        if(n == 0){ p += 2; break; }
        r.body += raw.substr(p, n); p += n + 2;
      }
    }
    v.push_back(r);
  }
  return v;
}

static std::string g_cookie;
static std::string req(const char* method, const std::string& path, const std::string& body = "",
                       const char* extraHdr = "", bool xflex = true, const char* host = "192.168.1.50:8080"){
  std::string r = std::string(method) + " " + path + " HTTP/1.1\r\nHost: " + host + "\r\n";
  if(!g_cookie.empty()) r += "Cookie: " FLEXHTTP_SESS_COOKIE "=" + g_cookie + "\r\n";
  if(xflex) r += "X-Flex: 1\r\n";
  if(!body.empty() || !strcmp(method, "POST")) r += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  r += extraHdr;
  r += "Connection: close\r\n\r\n";
  return r + body;
}
static Resp run(const std::string& in, Client* keep = nullptr){
  Client c; c.in = in;
  if(keep){ c.chunk = keep->chunk; c.hangAfter = keep->hangAfter; c.closeAfter = keep->closeAfter; }
  FlexWebConn cn = { cRead, cWrite, &c };
  flexWebServeConn(&g_w, &cn, g_hdr, g_io);
  if(keep) keep->out = c.out;
  auto v = parseAll(c.out);
  return v.empty() ? Resp() : v[0];
}
static cJSON* js(const Resp& r){ return cJSON_Parse(r.body.c_str()); }
static int jint(cJSON* j, const char* k){ cJSON* x = cJSON_GetObjectItemCaseSensitive(j, k); return x ? x->valueint : -9999; }

static bool pair(){
  Resp r = run(req("POST", "/api/pair", std::string("code=") + g_w.code));
  if(r.status != 200) return false;
  std::string sc = r.h["set-cookie"];
  size_t a = sc.find("fxs="), b = sc.find(';', a);
  g_cookie = sc.substr(a + 4, b - a - 4);
  return g_cookie.size() == 32;
}

// ---- contenido de prueba ----
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
static std::string urlenc(const std::string& s){
  std::string o; char b[4];
  for(unsigned char ch : s){ if(isalnum(ch) || ch == '.' || ch == '-' || ch == '_') o += (char)ch; else { snprintf(b, 4, "%%%02X", ch); o += b; } }
  return o;
}
static std::string upPath(const char* kind, const std::string& name, const std::vector<uint8_t>& d, uint32_t crc, const std::string& more = ""){
  return std::string("/api/upload?kind=") + kind + "&name=" + urlenc(name) + "&size=" + std::to_string(d.size()) +
         "&crc=" + std::to_string(crc) + more;
}
static std::string S(const std::vector<uint8_t>& v){ return std::string((const char*)v.data(), v.size()); }

// =============================================================
static void testPublicAndPairing(){
  std::printf("-- pagina publica, emparejamiento y limites --\n");
  setup(); g_cookie.clear();
  Resp r = run(req("GET", "/"));
  CHECK(r.status == 200 && r.h["content-type"].find("text/html") == 0 && r.h.count("content-security-policy"), "pagina con CSP");
  CHECK(r.body == std::string(FLEXWEB_INDEX_HTML) && r.body.find("<script src=\"/app.js\">") != std::string::npos,
        "sirve la web de webui/ (la generada, no un marcador)");
  Resp rj = run(req("GET", "/app.js")), rc = run(req("GET", "/app.css"));
  CHECK(rj.status == 200 && rj.h["content-type"].find("text/javascript") == 0 && rj.body == std::string(FLEXWEB_APP_JS) &&
        rc.status == 200 && rc.h["content-type"].find("text/css") == 0 && rc.body == std::string(FLEXWEB_APP_CSS),
        "app.js y app.css con su tipo y enteros (%zu + %zu bytes)", rj.body.size(), rc.body.size());
  r = run(req("GET", "/api/library"));
  cJSON* j = js(r);
  CHECK(r.status == 401 && jint(j, "pair") == 1, "sin emparejar: 401 y pide el codigo");
  cJSON_Delete(j);
  CHECK(run(req("GET", "/api/library", "", "", true, "evil.example")).status == 403, "Host ajeno: 403 (DNS rebinding)");
  CHECK(run(req("POST", "/api/pair", std::string("code=") + g_w.code, "", false)).status == 403, "sin X-Flex: 403 (CSRF)");
  CHECK(strlen(g_w.code) == 6, "codigo de 6 cifras: %s", g_w.code);
  for(int i = 0; i < 5; i++){
    r = run(req("POST", "/api/pair", "code=000000"));
    j = js(r);
    CHECK(r.status == 403 && jint(j, "left") == 4 - i, "codigo malo %d: quedan %d", i + 1, jint(j, "left"));
    cJSON_Delete(j);
  }
  r = run(req("POST", "/api/pair", std::string("code=") + g_w.code));
  CHECK(r.status == 429 && r.h.count("retry-after"), "tras 5 fallos, ni el bueno entra: 429");
  g_ms += 31000;
  CHECK(pair(), "pasado el plazo, el codigo bueno empareja");
  r = run(req("GET", "/api/hello"));
  j = js(r);
  CHECK(r.status == 200 && jint(j, "paired") == 1 && jint(j, "owner") == 0 && jint(j, "lockType") == 1, "hola: emparejado, sin propietario");
  cJSON_Delete(j);
  CHECK(r.h.count("set-cookie") == 0, "ninguna cookie en una respuesta normal");
  // Seis ranuras: la septima desplaza a la mas vieja.
  std::string first = g_cookie;
  for(int i = 0; i < FLEXWEB_SESS_N; i++){ g_ms += 10; std::string keep = g_cookie; pair(); }
  std::string last = g_cookie;
  g_cookie = first;
  CHECK(run(req("GET", "/api/status")).status == 401, "la sesion mas vieja cedio su sitio");
  g_cookie = last;
  CHECK(run(req("GET", "/api/status")).status == 200, "la nueva vale");
  g_ms += FLEXWEB_SESS_IDLE_MS + 1;
  CHECK(run(req("GET", "/api/status")).status == 401, "30 min sin uso: caduca");
  CHECK(run(req("BREW", "/api/x", "")).status == 405, "metodo raro: 405");
  CHECK(run("GARBAGE\r\n\r\n").status == 400, "peticion rota: 400");
}

static int evCount(int ev){ int n = 0; for(auto& e : g_ev) if(e.ev == ev) n++; return n; }

static void testUploads(){
  std::printf("-- subidas: solo se publica lo que llego entero y se puede leer --\n");
  setup(); g_cookie.clear(); pair();
  auto jpg = makeJpeg(640, 480);
  uint32_t crc = flexMlCrc32(0, jpg.data(), jpg.size());
  Client c; c.chunk = 777;                                   // TCP trocea: 777 bytes por lectura
  long live = g_live;
  Resp r = run(req("POST", upPath("photo", "Playa \xC3\xB1 1.jpg", jpg, crc, "&created=1690000000"), S(jpg)), &c);
  cJSON* j = js(r);
  CHECK(r.status == 201 && jint(j, "id") == 1 && jint(j, "p") == 1 && jint(j, "thumb") == 1, "foto publicada (%d) %s", r.status, r.body.c_str());
  cJSON_Delete(j);
  CHECK(g_live == live, "sin fugas de memoria");
  CHECK(g_fs.files.count("/Imagenes/Playa n 1.jpg") && S(g_fs.files["/Imagenes/Playa n 1.jpg"]) == S(jpg), "en /Imagenes, byte a byte");
  CHECK(g_fs.files.count(FML_DIR_THUMB "/1.jpg"), "con su miniatura");
  CHECK(g_fs.tmpCount() == 0, "ningun temporal");
  const FlexMlRec& rec = g_lib.recs[0];
  CHECK(rec.kind == FML_K_PHOTO && rec.fmt == FML_F_JPEG && rec.w == 640 && rec.h == 480 && (rec.flags & FML_R_PLAYABLE) &&
        !strcmp(rec.name, "Playa \xC3\xB1 1.jpg") && rec.created == 1690000000u && rec.crc == crc, "registro completo");
  CHECK(evCount(FLEXWEB_EV_UP_START) == 1 && evCount(FLEXWEB_EV_UP_DONE) == 1 && evCount(FLEXWEB_EV_UP_CHECK) == 1, "eventos de la tarjeta");
  uint32_t maxDone = 0; for(auto& e : g_ev) if(e.ev == FLEXWEB_EV_UP_PROGRESS && e.done > maxDone) maxDone = e.done;
  CHECK(maxDone == jpg.size(), "progreso en bytes reales hasta el total (%u)", maxDone);
  // Miniatura servida y valida.
  Resp t = run(req("GET", "/api/thumb/1?v=0"));
  FlexJpegInfo inf;
  CHECK(t.status == 200 && flexJpegProbe((const uint8_t*)t.body.data(), t.body.size(), &inf) == FLEXJPG_OK &&
        inf.width == FLEXTH_SIDE && inf.height == FLEXTH_SIDE && t.h["cache-control"].find("immutable") != std::string::npos,
        "miniatura 132x132 cacheable por version");

  r = run(req("POST", upPath("photo", "otra.jpg", jpg, crc), S(jpg)));
  j = js(r);
  CHECK(r.status == 200 && jint(j, "dup") == 1 && jint(j, "id") == 1 && g_lib.n == 1, "duplicado exacto: no se guarda dos veces");
  cJSON_Delete(j);

  auto bad = jpg; bad[bad.size() / 2] ^= 0x55;
  r = run(req("POST", upPath("photo", "rota.jpg", bad, crc), S(bad)));
  CHECK(r.status == 400 && r.body.find("comprobaci\xC3\xB3n") != std::string::npos && g_lib.n == 1 && g_fs.tmpCount() == 0,
        "CRC distinto: danado, nada publicado ni temporal");

  std::vector<uint8_t> mp4(4000, 0x11); std::memcpy(&mp4[4], "ftypisom", 8);
  uint32_t c4 = flexMlCrc32(0, mp4.data(), mp4.size());
  r = run(req("POST", upPath("photo", "falsa.jpg", mp4, c4), S(mp4)));
  CHECK(r.status == 415 && r.body.find("MP4") != std::string::npos && g_fs.tmpCount() == 0, "dice foto y es MP4: 415");

  Client cut; cut.closeAfter = 2000;                         // cabecera + un trozo del cuerpo
  auto big = makeJpeg(800, 600, 3);
  uint32_t cb = flexMlCrc32(0, big.data(), big.size());
  g_ev.clear();
  run(req("POST", upPath("photo", "cortada.jpg", big, cb), S(big)), &cut);
  CHECK(g_lib.n == 1 && g_fs.tmpCount() == 0 && evCount(FLEXWEB_EV_UP_FAIL) == 1, "desconexion a mitad: nada publicado y temporal borrado");
  Client hang; hang.hangAfter = 3000;
  r = run(req("POST", upPath("photo", "colgada.jpg", big, cb), S(big)), &hang);
  CHECK(r.status == 408 && g_fs.tmpCount() == 0 && !g_w.uploading, "silencio de 15 s: 408, temporal borrado y servidor libre");

  g_fs.failWriteAfter = g_fs.written + 5000;
  r = run(req("POST", upPath("photo", "llena.jpg", big, cb), S(big)));
  CHECK(r.status == 507 && g_fs.tmpCount() == 0, "disco lleno a mitad: 507 y temporal borrado");
  g_fs.failWriteAfter = -1;

  std::vector<uint8_t> huge(1, 0);
  std::string hp = std::string("/api/upload?kind=video&name=x.avi&size=") + std::to_string(FML_LIMIT_VIDEO + 1) + "&crc=1";
  r = run(std::string("POST ") + hp + " HTTP/1.1\r\nHost: 192.168.1.50\r\nCookie: fxs=" + g_cookie +
          "\r\nX-Flex: 1\r\nContent-Length: " + std::to_string(FML_LIMIT_VIDEO + 1) + "\r\n\r\n");
  CHECK(r.status == 413 && r.body.find("MB") != std::string::npos, "demasiado grande: 413 antes de recibir nada");
  // Libre = la reserva + la mitad del archivo: el archivo cabria "a secas",
  // pero no sin comerse la reserva que nunca se toca.
  g_fs.total = g_fs.used() + FML_RESERVE_BYTES + (uint32_t)big.size() / 2;
  uint16_t nBefore = g_lib.n;
  r = run(req("POST", upPath("photo", "sinsitio.jpg", big, cb), S(big)));
  CHECK(r.status == 507 && r.body.find("espacio") != std::string::npos && g_lib.n == nBefore,
        "sin espacio (reserva incluida): 507 con el motivo (%d)", r.status);
  g_fs.total = 10u * 1024u * 1024u;

  auto trunc = std::vector<uint8_t>(big.begin(), big.begin() + (long)big.size() * 6 / 10);
  uint32_t ct = flexMlCrc32(0, trunc.data(), trunc.size());
  nBefore = g_lib.n;
  r = run(req("POST", upPath("photo", "incompleta.jpg", trunc, ct), S(trunc)));
  CHECK(r.status == 422 && g_lib.n == nBefore && g_fs.tmpCount() == 0, "JPEG que no se puede decodificar: 422, sin temporales (%d)", r.status);

  // Progresivo guardado como ORIGINAL (no reproducible) + miniatura del navegador.
  FILE* f = std::fopen("../fixtures/progressive.jpg", "rb");
  std::vector<uint8_t> pj; uint8_t b[4096]; size_t n;
  if(f){ while((n = std::fread(b, 1, sizeof(b), f)) > 0) pj.insert(pj.end(), b, b + n); std::fclose(f); }
  uint32_t cp = flexMlCrc32(0, pj.data(), pj.size());
  r = run(req("POST", upPath("photo", "progresiva.jpg", pj, cp), S(pj)));
  j = js(r);
  int pid = jint(j, "id");
  CHECK(r.status == 201 && jint(j, "p") == 0 && jint(j, "thumb") == 0 && cJSON_GetObjectItemCaseSensitive(j, "why"),
        "progresivo: se guarda, se dice por que no se abre");
  cJSON_Delete(j);
  auto poster = makeJpeg(300, 200, 9);
  r = run(req("POST", "/api/thumb/" + std::to_string(pid), S(poster)));
  int pi = flexMlFindId(&g_lib, (uint32_t)pid);
  CHECK(r.status == 200 && pi >= 0 && (g_lib.recs[pi].flags & FML_R_EXT_THUMB), "miniatura del navegador aceptada y normalizada");
  Resp tt = run(req("GET", "/api/thumb/" + std::to_string(pid)));
  CHECK(tt.status == 200 && flexJpegProbe((const uint8_t*)tt.body.data(), tt.body.size(), &inf) == FLEXJPG_OK &&
        inf.width == FLEXTH_SIDE, "la guardada es un JPEG de 132 hecho por Flex OS");
  r = run(req("POST", "/api/thumb/1", S(poster)));
  CHECK(r.status == 409, "a una foto que Flex OS sabe leer no se le cambia la miniatura desde fuera");
  std::string junk(3000, 'x');
  r = run(req("POST", "/api/thumb/" + std::to_string(pid), junk));
  CHECK(r.status == 422 && g_fs.tmpCount() == 0, "miniatura que no es JPEG: 422");

  // Audio WAV PCM y original MP3 (no reproducible).
  std::vector<uint8_t> wav(44 + 22050 * 2, 0);
  std::memcpy(&wav[0], "RIFF", 4); uint32_t rs = (uint32_t)wav.size() - 8; std::memcpy(&wav[4], &rs, 4);
  std::memcpy(&wav[8], "WAVEfmt ", 8); uint32_t fl = 16; std::memcpy(&wav[16], &fl, 4);
  uint16_t tag = 1, ch = 1, bits = 16, ba = 2; uint32_t sr = 22050, br = 44100;
  std::memcpy(&wav[20], &tag, 2); std::memcpy(&wav[22], &ch, 2); std::memcpy(&wav[24], &sr, 4); std::memcpy(&wav[28], &br, 4);
  std::memcpy(&wav[32], &ba, 2); std::memcpy(&wav[34], &bits, 2); std::memcpy(&wav[36], "data", 4);
  uint32_t dl = 22050 * 2; std::memcpy(&wav[40], &dl, 4);
  uint32_t cw = flexMlCrc32(0, wav.data(), wav.size());
  r = run(req("POST", upPath("audio", "Cancion.wav", wav, cw, "&title=Mi%20canci%C3%B3n&artist=Grupo"), S(wav)));
  j = js(r);
  int aid = jint(j, "id");
  CHECK(r.status == 201 && jint(j, "p") == 1, "WAV PCM: reproducible");
  cJSON_Delete(j);
  int ai = flexMlFindId(&g_lib, (uint32_t)aid);
  CHECK(ai >= 0 && g_lib.recs[ai].durMs == 1000 && !strcmp(g_lib.recs[ai].title, "Mi canci\xC3\xB3n") &&
        !strncmp(g_lib.recs[ai].path, "/Musica/", 8), "a /Musica, con duracion medida y titulo");
  std::vector<uint8_t> mp3(5000, 0x33); std::memcpy(&mp3[0], "ID3\x04", 4);
  uint32_t cm = flexMlCrc32(0, mp3.data(), mp3.size());
  r = run(req("POST", upPath("audio", "tema.mp3", mp3, cm), S(mp3)));
  j = js(r);
  CHECK(r.status == 201 && jint(j, "p") == 0 && r.body.find("MP3") != std::string::npos, "MP3 original: se guarda y se dice que no suena aqui");
  cJSON_Delete(j);
  CHECK(g_fs.tmpCount() == 0, "ningun temporal en toda la bateria de subidas");
  g_w.allowUpload = false;
  CHECK(run(req("POST", upPath("photo", "no.jpg", jpg, crc), S(jpg))).status == 403, "subidas desactivadas: 403");
  g_w.allowUpload = true;
}

// ---- AVI construido aqui (el mismo formato que genera la web) ----
static void p32(std::vector<uint8_t>& v, uint32_t x){ for(int i = 0; i < 4; i++) v.push_back((uint8_t)(x >> (8 * i))); }
static void cc4(std::vector<uint8_t>& v, const char* s){ for(int i = 0; i < 4; i++) v.push_back((uint8_t)s[i]); }
static std::vector<uint8_t> makeAvi(int frames){
  auto f = makeJpeg(320, 240, 1);
  std::vector<uint8_t> hdrl; cc4(hdrl, "hdrl");
  cc4(hdrl, "avih"); p32(hdrl, 56); p32(hdrl, 100000); for(int i = 0; i < 3; i++) p32(hdrl, 0);
  p32(hdrl, (uint32_t)frames); p32(hdrl, 0); p32(hdrl, 1); p32(hdrl, 0); p32(hdrl, 320); p32(hdrl, 240); for(int i = 0; i < 4; i++) p32(hdrl, 0);
  std::vector<uint8_t> strl; cc4(strl, "strl");
  cc4(strl, "strh"); p32(strl, 56); cc4(strl, "vids"); cc4(strl, "MJPG"); p32(strl, 0); p32(strl, 0); p32(strl, 0);
  p32(strl, 1); p32(strl, 10); p32(strl, 0); p32(strl, (uint32_t)frames); p32(strl, 0); p32(strl, 0xFFFFFFFF); p32(strl, 0); p32(strl, 0); p32(strl, 0);
  cc4(strl, "strf"); p32(strl, 40); p32(strl, 40); p32(strl, 320); p32(strl, 240); p32(strl, 0x00180001); cc4(strl, "MJPG"); for(int i = 0; i < 5; i++) p32(strl, 0);
  cc4(hdrl, "LIST"); p32(hdrl, (uint32_t)strl.size()); hdrl.insert(hdrl.end(), strl.begin(), strl.end());
  std::vector<uint8_t> movi; cc4(movi, "movi");
  for(int i = 0; i < frames; i++){ cc4(movi, "00dc"); p32(movi, (uint32_t)f.size()); movi.insert(movi.end(), f.begin(), f.end()); if(f.size() & 1) movi.push_back(0); }
  std::vector<uint8_t> body; cc4(body, "AVI ");
  cc4(body, "LIST"); p32(body, (uint32_t)hdrl.size()); body.insert(body.end(), hdrl.begin(), hdrl.end());
  cc4(body, "LIST"); p32(body, (uint32_t)movi.size()); body.insert(body.end(), movi.begin(), movi.end());
  std::vector<uint8_t> out; cc4(out, "RIFF"); p32(out, (uint32_t)body.size()); out.insert(out.end(), body.begin(), body.end());
  return out;
}

static void testVideoAndLibrary(){
  std::printf("-- video, biblioteca y descargas --\n");
  setup(); g_cookie.clear(); pair();
  auto avi = makeAvi(20);
  uint32_t ca = flexMlCrc32(0, avi.data(), avi.size());
  Resp r = run(req("POST", upPath("video", "Clip.avi", avi, ca), S(avi)));
  cJSON* j = js(r);
  CHECK(r.status == 201 && jint(j, "p") == 1 && jint(j, "thumb") == 1, "AVI MJPEG: reproducible y con miniatura del primer fotograma");
  cJSON_Delete(j);
  CHECK(g_lib.recs[0].w == 320 && g_lib.recs[0].h == 240 && g_lib.recs[0].durMs == 2000 &&
        !strncmp(g_lib.recs[0].path, "/Videos/", 8), "a /Videos, 320x240, 2 s");
  auto jpg = makeJpeg(200, 150, 5);
  run(req("POST", upPath("photo", "foto.jpg", jpg, flexMlCrc32(0, jpg.data(), jpg.size())), S(jpg)));
  r = run(req("GET", "/api/library"));
  j = js(r);
  CHECK(r.status == 200 && r.h["transfer-encoding"] == "chunked" && j, "biblioteca por trozos y JSON valido");
  cJSON* items = cJSON_GetObjectItemCaseSensitive(j, "items");
  CHECK(cJSON_GetArraySize(items) == 2 && jint(j, "rev") == (int)g_lib.rev && jint(j, "free") > 0, "dos elementos, revision y espacio");
  int rev = jint(j, "rev");
  cJSON_Delete(j);
  r = run(req("GET", "/api/library?since=" + std::to_string(rev)));
  j = js(r);
  CHECK(jint(j, "same") == 1 && jint(j, "owner") == 0 && r.body.size() < 64, "sin cambios: respuesta minima (con el nivel)");
  cJSON_Delete(j);

  // Un dibujo de Paint esta en el catalogo (Galeria) pero no existe para la web.
  FlexMlRec d; std::memset(&d, 0, sizeof(d));
  d.kind = FML_K_DRAW; d.fmt = FML_F_FXP; d.size = 64; d.state = FML_S_READY;
  std::snprintf(d.path, sizeof(d.path), "/Imagenes/boceto.fxp");
  std::snprintf(d.name, sizeof(d.name), "boceto.fxp");
  g_fs.files["/Imagenes/boceto.fxp"] = std::vector<uint8_t>(64, 7);
  int di = flexMlAdd(&g_lib, &d, 1700000000u);
  uint32_t did = di >= 0 ? g_lib.recs[di].id : 0;
  r = run(req("GET", "/api/library"));
  j = js(r);
  CHECK(did && cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(j, "items")) == 2 && r.body.find("boceto") == std::string::npos,
        "el dibujo no sale en la lista de la web");
  cJSON_Delete(j);
  std::string ds = std::to_string(did);
  CHECK(run(req("GET", "/api/file/" + ds)).status == 404 && run(req("GET", "/api/thumb/" + ds)).status == 404 &&
        run(req("GET", "/api/zip?ids=" + ds)).status == 404, "ni se descarga ni tiene miniatura por la web");
  flexMlRemoveAt(&g_lib, di);
  g_fs.files.erase("/Imagenes/boceto.fxp");

  // Descarga entera, con nombre UTF-8, y rangos.
  r = run(req("GET", "/api/file/2?dl=1"));
  CHECK(r.status == 200 && S(jpg) == r.body && r.h["content-type"] == "image/jpeg" &&
        r.h["content-disposition"].find("attachment") == 0 && r.h["accept-ranges"] == "bytes", "descarga completa");
  r = run(req("GET", "/api/file/2", "", "Range: bytes=10-19\r\n"));
  CHECK(r.status == 206 && r.body == S(jpg).substr(10, 10) && r.h["content-range"] == "bytes 10-19/" + std::to_string(jpg.size()), "rango 10-19");
  r = run(req("GET", "/api/file/2", "", "Range: bytes=-7\r\n"));
  CHECK(r.status == 206 && r.body == S(jpg).substr(jpg.size() - 7), "ultimos 7 bytes");
  r = run(req("GET", "/api/file/2", "", "Range: bytes=999999-\r\n"));
  CHECK(r.status == 416 && r.h["content-range"] == "bytes */" + std::to_string(jpg.size()), "rango fuera: 416");
  r = run(req("HEAD", "/api/file/1"));
  CHECK(r.status == 200 && r.body.empty() && r.h["content-length"] == std::to_string(avi.size()), "HEAD: solo cabeceras");
  CHECK(run(req("GET", "/api/file/99")).status == 404, "id que no existe: 404");

  // Keep-alive: dos peticiones en la misma conexion, la segunda llega pegada.
  std::string two = "GET /api/status HTTP/1.1\r\nHost: 192.168.1.50\r\nCookie: fxs=" + g_cookie + "\r\n\r\n" +
                    "GET /api/hello HTTP/1.1\r\nHost: 192.168.1.50\r\nCookie: fxs=" + g_cookie + "\r\nConnection: close\r\n\r\n";
  Client k; run(two, &k);
  auto rs = parseAll(k.out);
  CHECK(rs.size() == 2 && rs[0].status == 200 && rs[1].status == 200 && rs[1].body.find("paired") != std::string::npos,
        "dos peticiones en una conexion");
  CHECK(lower(rs[0].h["connection"]) == "keep-alive", "sin nadie esperando: la conexion se reutiliza");
  // Otra conexion espera turno: la respuesta avisa de que es la ultima y la
  // segunda peticion ya no se atiende por aqui (el navegador la repite en la
  // conexion nueva).
  g_others = true;
  Client k2; run(two, &k2);
  auto rs2 = parseAll(k2.out);
  CHECK(rs2.size() == 1 && rs2[0].status == 200 && lower(rs2[0].h["connection"]) == "close",
        "con otra conexion esperando: Connection: close y se cede el turno");
  // Conexion abierta "por si acaso" (los navegadores lo hacen) sin mandar
  // nada: con otra esperando se suelta en el primer trozo de espera; sola,
  // agota su plazo. En ningun caso se contesta nada (no hubo peticion).
  Client idle; idle.in = ""; idle.hangAfter = 0;
  FlexWebConn c1 = { cRead, cWrite, &idle };
  CHECK(flexWebServeConn(&g_w, &c1, g_hdr, g_io) == 0 &&
        idle.out.empty() && idle.reads == 1, "conexion vacia con otra esperando: se suelta tras un trozo (%d lecturas)", idle.reads);
  g_others = false;
  Client idle2; idle2.in = ""; idle2.hangAfter = 0;
  FlexWebConn c2 = { cRead, cWrite, &idle2 };
  CHECK(flexWebServeConn(&g_w, &c2, g_hdr, g_io) == 0 && idle2.out.empty() &&
        idle2.reads == (int)(FLEXWEB_HDR_TIMEOUT_MS / FLEXWEB_IDLE_SLICE_MS), "conexion vacia y sola: agota los 5 s sin responder (%d trozos)", idle2.reads);

  // ZIP validado por Python.
  r = run(req("GET", "/api/zip?ids=1,2,2"));
  CHECK(r.status == 200 && r.h["content-type"] == "application/zip" &&
        std::to_string(r.body.size()) == r.h["content-length"], "ZIP con Content-Length exacto (%zu)", r.body.size());
  FILE* zf = std::fopen("build/test_mediaweb.zip", "wb");
  if(zf){ std::fwrite(r.body.data(), 1, r.body.size(), zf); std::fclose(zf); }
  std::string py = "python3 -c \"import zipfile,sys;z=zipfile.ZipFile('build/test_mediaweb.zip');"
                   "assert z.testzip() is None;n=z.namelist();assert len(n)==2,n;"
                   "a=open('build/zip_a.bin','wb').write(z.read(n[0]));b=open('build/zip_b.bin','wb').write(z.read(n[1]));"
                   "print('|'.join(n))\" > build/zip_names.txt";
  int rc = std::system(py.c_str());
  CHECK(rc == 0, "zipfile de Python abre el ZIP y todos los CRC cuadran");
  FILE* nf = std::fopen("build/zip_names.txt", "rb"); char names[256] = ""; if(nf){ size_t q = std::fread(names, 1, 255, nf); names[q] = 0; std::fclose(nf); }
  CHECK(strstr(names, "Clip.avi") && strstr(names, "foto.jpg"), "nombres originales dentro del ZIP: %s", names);
  auto readB = [](const char* p){ std::vector<uint8_t> v; FILE* f = std::fopen(p, "rb"); uint8_t b[4096]; size_t n; if(f){ while((n = std::fread(b, 1, 4096, f)) > 0) v.insert(v.end(), b, b + n); std::fclose(f);} return v; };
  CHECK(readB("build/zip_a.bin") == avi && readB("build/zip_b.bin") == jpg, "contenido identico al guardado");
  CHECK(run(req("GET", "/api/zip?ids=1,x")).status == 400 && run(req("GET", "/api/zip?ids=77")).status == 404, "lista mala / id inexistente");
}

static void testLocked(){
  std::printf("-- contenido bloqueado: solo con la clave del sistema --\n");
  setup(); g_cookie.clear(); pair();
  auto jpg = makeJpeg(320, 240, 7);
  run(req("POST", upPath("photo", "Secreta.jpg", jpg, flexMlCrc32(0, jpg.data(), jpg.size())), S(jpg)));
  auto jpg2 = makeJpeg(320, 240, 8);
  run(req("POST", upPath("photo", "Normal.jpg", jpg2, flexMlCrc32(0, jpg2.data(), jpg2.size())), S(jpg2)));
  // El P4 la bloquea con su almacen: el archivo y la miniatura se mudan a
  // Protegido (nombre neutro) y el catalogo lo marca.
  FlexMlRec& sec = g_lib.recs[0];
  char oldTp[FML_PATH_MAX]; thumbPathOf(&sec, oldTp, sizeof(oldTp));
  std::string oldPath = sec.path;
  char why[96];
  CHECK(flexMsSetLock(&g_mst, sec.id, true, why, sizeof(why)), "el P4 la bloquea (%s)", why);
  char newTp[FML_PATH_MAX]; thumbPathOf(&sec, newTp, sizeof(newTp));
  CHECK(!g_fs.files.count(oldPath) && !g_fs.files.count(oldTp) && g_fs.files.count(sec.path) && g_fs.files.count(newTp) &&
        std::string(sec.path) == FML_DIR_LOCKED "/1.jpg", "archivo y miniatura en la carpeta protegida (%s)", sec.path);
  Resp r = run(req("GET", "/api/library"));
  cJSON* j = js(r);
  cJSON* it0 = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(j, "items"), 0);
  CHECK(it0 && cJSON_GetArraySize(it0) == 2 && jint(it0, "lock") == 1 && jint(it0, "id") == 1, "sin propietario: solo {id, lock}");
  CHECK(r.body.find("Secreta") == std::string::npos, "ni el nombre aparece en la respuesta");
  cJSON_Delete(j);
  CHECK(run(req("GET", "/api/thumb/1")).status == 403, "miniatura bloqueada: 403");
  CHECK(run(req("GET", "/api/file/1")).status == 403, "archivo bloqueado: 403");
  CHECK(run(req("GET", "/api/zip?ids=2,1")).status == 403, "ZIP con uno bloqueado: 403 entero");
  CHECK(run(req("GET", "/api/zip?ids=2")).status == 200, "el normal si se descarga");
  CHECK(run(req("POST", "/api/thumb/1", S(jpg))).status == 423, "no se puede cambiar su miniatura");

  r = run(req("POST", "/api/login", "secret=1111"));
  j = js(r);
  CHECK(r.status == 401 && jint(j, "left") == 4 && r.body.find("PIN") != std::string::npos, "PIN malo: 401, quedan 4");
  cJSON_Delete(j);
  r = run(req("POST", "/api/login", "secret=2468"));
  CHECK(r.status == 200, "PIN bueno: propietario");
  r = run(req("GET", "/api/library"));
  CHECK(r.body.find("Secreta") != std::string::npos && r.body.find("\"lock\":1") != std::string::npos, "propietario: lo ve, marcado como bloqueado");
  std::string since = "/api/library?since=" + std::to_string(g_lib.rev);
  r = run(req("GET", since));
  j = js(r);
  CHECK(jint(j, "same") == 1 && jint(j, "owner") == 1, "sin cambios, pero la respuesta corta dice que es propietario");
  cJSON_Delete(j);
  r = run(req("GET", "/api/file/1"));
  CHECK(r.status == 200 && r.body == S(jpg), "propietario: descarga");
  r = run(req("GET", "/api/thumb/1"));
  CHECK(r.status == 200 && r.h["cache-control"] == "no-store", "miniatura de un bloqueado: nunca a la cache");
  flexWebDropOwners(&g_w);                                   // el P4 se bloquea
  r = run(req("GET", since));
  j = js(r);
  CHECK(jint(j, "same") == 1 && jint(j, "owner") == 0, "el P4 se bloquea: la web se entera aunque el catalogo no cambie");
  cJSON_Delete(j);
  CHECK(run(req("GET", "/api/file/1")).status == 403, "el P4 se bloquea: se acabo el propietario");
  run(req("POST", "/api/login", "secret=2468"));
  CHECK(run(req("GET", "/api/file/1")).status == 200, "de nuevo con el PIN");
  g_ms += FLEXWEB_OWNER_IDLE_MS + 1;
  CHECK(run(req("GET", "/api/file/1")).status == 403, "5 min sin usarlo: caduca");
  for(int i = 0; i < 5; i++) run(req("POST", "/api/login", "secret=0000"));
  int calls = g_verifyCalls;
  r = run(req("POST", "/api/login", "secret=2468"));
  CHECK(r.status == 429 && g_verifyCalls == calls, "tras 5 fallos: 429 y la clave ni se comprueba");
  g_lockType = 0;
  g_ms += 20u * 60u * 1000u;
  CHECK(run(req("POST", "/api/login", "secret=2468")).status == 409, "sin PIN ni contrasena en Flex OS: 409");
  r = run(req("POST", "/api/unpair"));
  CHECK(r.status == 200 && run(req("GET", "/api/status")).status == 401, "olvidar este movil");
}


// =============================================================
//  ARCHIVOS DE VARIOS MB, VARIOS SEGUIDOS, Y TRABAJO PESADO EN FILA
//  -------------------------------------------------------------
//  El fallo que se reporto: fotos de ~2 MB o mas, o 2-3 fotos seguidas,
//  podian reiniciar Flex OS. Una de las causas estaba aqui: validar una foto
//  reservaba el archivo ENTERO en la memoria de la tarea del servidor. Lo
//  que se exige ahora:
//    · 3 y 5 MB de foto se validan y publican con TODAS las reservas por
//      debajo de 256 KB (ninguna del tamano del archivo);
//    · tres fotos seguidas: tres publicadas, sin temporales ni fugas;
//    · audio y video de varios MB: publicados, con su duracion;
//    · la validacion pide turno de trabajo pesado (una vez, y lo suelta);
//      si no se lo dan, 503 con un texto que el movil sabe reintentar y
//      nada queda a medias.
// =============================================================
static size_t g_bigA = 0;
static void* tABig(size_t n){ if(n > g_bigA) g_bigA = n; return tA(n); }
static int g_heavyIn = 0, g_heavyOut = 0; static bool g_heavyOk = true;
static bool hHeavyBegin(void*){ g_heavyIn++; return g_heavyOk; }
static void hHeavyEnd(void*){ g_heavyOut++; }
// Foto con mucho detalle (ruido con estructura): a calidad alta pesa MB de verdad.
static std::vector<uint8_t> makeBigJpeg(int w, int h, int q, uint32_t seed){
  std::vector<uint8_t> px((size_t)w * h * 3);
  for(int y = 0; y < h; y++) for(int x = 0; x < w; x++){
    seed = seed * 1664525u + 1013904223u;
    uint8_t* p = &px[((size_t)y * w + x) * 3];
    p[0] = (uint8_t)((x * 3) ^ (seed >> 24)); p[1] = (uint8_t)((y * 5) + (seed >> 16)); p[2] = (uint8_t)(seed >> 8);
  }
  std::vector<uint8_t> o; FlexJeCfg c; c.width = w; c.height = h; c.quality = q; c.subsampling = FLEXJE_SUB_444; c.input = FLEXJE_IN_RGB888;
  flexJpegEncodeMem(&c, px.data(), 0, outV, &o, nullptr, nullptr);
  return o;
}
static void testBigAndHeavy(){
  std::printf("-- varios MB, varios seguidos y trabajo pesado en fila --\n");
  setup(); g_cookie.clear(); pair();
  g_w.alloc = tABig;
  g_w.host.heavyBegin = hHeavyBegin; g_w.host.heavyEnd = hHeavyEnd;
  g_heavyIn = g_heavyOut = 0; g_heavyOk = true;
  g_fs.total = 40u * 1024u * 1024u;

  const struct { int w, h, q; double minMB; } fotos[] = { { 1100, 900, 97, 2.5 }, { 1600, 1200, 97, 4.5 } };
  for(auto& f : fotos){
    auto jpg = makeBigJpeg(f.w, f.h, f.q, (uint32_t)f.w);
    double mb = jpg.size() / 1048576.0;
    CHECK(mb >= f.minMB && jpg.size() <= FML_LIMIT_PHOTO, "foto de prueba de %.1f MB (se esperaban >= %.1f)", mb, f.minMB);
    uint32_t crc = flexMlCrc32(0, jpg.data(), jpg.size());
    Client c; c.chunk = 16384;
    long live = g_live; g_bigA = 0; int n0 = g_lib.n, h0 = g_heavyIn;
    Resp r = run(req("POST", upPath("photo", "grande.jpg", jpg, crc), S(jpg)), &c);
    CHECK(r.status == 201 && g_lib.n == n0 + 1, "foto de %.1f MB publicada (%d)", mb, r.status);
    CHECK(g_bigA < 256u * 1024u, "foto de %.1f MB: la mayor reserva son %zu B (nada del tamano del archivo)", mb, g_bigA);
    CHECK(g_live == live && g_fs.tmpCount() == 0, "foto de %.1f MB: sin fugas ni temporales", mb);
    CHECK(g_heavyIn == h0 + 1 && g_heavyOut == g_heavyIn, "la validacion pide turno una vez y lo suelta");
    std::printf("   foto de %.2f MB validada; mayor reserva %zu B\n", mb, g_bigA);
  }

  // Tres fotos seguidas (lo que manda el movil cuando se eligen varias).
  {
    int n0 = g_lib.n; long live = g_live;
    for(int k = 0; k < 3; k++){
      auto jpg = makeBigJpeg(900, 700, 95, 77u + (uint32_t)k);
      uint32_t crc = flexMlCrc32(0, jpg.data(), jpg.size());
      Client c; c.chunk = 4096 + 1000 * k;
      Resp r = run(req("POST", upPath("photo", std::string("serie ") + char('1' + k) + ".jpg", jpg, crc), S(jpg)), &c);
      CHECK(r.status == 201, "foto %d de 3 publicada (%d)", k + 1, r.status);
    }
    CHECK(g_lib.n == n0 + 3 && g_fs.tmpCount() == 0 && g_live == live, "tres seguidas: tres en la biblioteca, sin temporales ni fugas");
  }

  // Audio de varios MB (WAV PCM de 4 MB): duracion real, sin reservas grandes.
  {
    uint32_t rate = 22050, bytes = 4u * 1024u * 1024u;
    std::vector<uint8_t> w; auto p32 = [&](uint32_t x){ for(int i = 0; i < 4; i++) w.push_back((uint8_t)(x >> (8 * i))); };
    auto p16 = [&](uint16_t x){ w.push_back((uint8_t)x); w.push_back((uint8_t)(x >> 8)); };
    w.insert(w.end(), { 'R', 'I', 'F', 'F' }); p32(36 + bytes); w.insert(w.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
    p32(16); p16(1); p16(1); p32(rate); p32(rate * 2); p16(2); p16(16);
    w.insert(w.end(), { 'd', 'a', 't', 'a' }); p32(bytes);
    for(uint32_t i = 0; i < bytes; i++) w.push_back((uint8_t)(i * 37));
    uint32_t crc = flexMlCrc32(0, w.data(), w.size());
    Client c; c.chunk = 16384; g_bigA = 0; long live = g_live;
    Resp r = run(req("POST", upPath("audio", "tema.wav", w, crc), S(w)), &c);
    const FlexMlRec* last = g_lib.n ? &g_lib.recs[g_lib.n - 1] : nullptr;
    CHECK(r.status == 201 && last && last->kind == FML_K_AUDIO && last->durMs > 90000, "audio de 4 MB publicado con su duracion (%d)", r.status);
    CHECK(g_bigA < 256u * 1024u && g_live == live && g_fs.tmpCount() == 0, "audio de 4 MB: sin reservas grandes ni fugas (%zu B)", g_bigA);
  }

  // Sin turno de trabajo pesado: 503 reintentable y nada a medias.
  {
    g_heavyOk = false;
    auto jpg = makeJpeg(320, 240, 5);
    uint32_t crc = flexMlCrc32(0, jpg.data(), jpg.size());
    int n0 = g_lib.n; int in0 = g_heavyIn, out0 = g_heavyOut;
    Resp r = run(req("POST", upPath("photo", "espera.jpg", jpg, crc), S(jpg)));
    CHECK(r.status == 503 && r.body.find("en curso") != std::string::npos, "sin turno: 503 que el movil reintenta (%d %s)", r.status, r.body.c_str());
    CHECK(g_lib.n == n0 && g_fs.tmpCount() == 0 && !g_w.uploading, "sin turno: nada publicado, ningun temporal, servidor libre");
    CHECK(g_heavyIn == in0 + 1 && g_heavyOut == out0, "sin turno: no se suelta lo que no se tomo");
    g_heavyOk = true;
  }
  g_w.alloc = tA;
}

// =============================================================
//  FLEX STORAGE: emparejamiento, resumen, transferencias, biblioteca
//  y la pasarela /api/cloud hacia el telefono (simulado aqui).
// =============================================================
// ---- el telefono, detras de la pasarela ----
struct Up {
  std::string sent;                 // lo que el P4 le manda
  std::string resp;                 // lo que contesta
  size_t pos = 0;
  bool answered = false, closed = false;
  size_t chunk = 1460;              // como llegaria por TCP
};
static std::vector<Up*> g_ups;
static std::function<std::string(const std::string&)> g_phone;   // peticion completa -> respuesta
static std::string g_openWhy, g_token = "00112233445566778899aabbccddeeff0011223344556677";
static std::vector<int> g_results;
static int upRead(void* c, uint8_t* b, size_t n, uint32_t){
  Up* u = (Up*)c;
  if(!u->answered){ u->answered = true; u->resp = g_phone ? g_phone(u->sent) : std::string(); }
  if(u->pos >= u->resp.size()) return -1;
  size_t t = u->resp.size() - u->pos; if(t > n) t = n; if(t > u->chunk) t = u->chunk;
  std::memcpy(b, u->resp.data() + u->pos, t); u->pos += t; return (int)t;
}
static bool upWrite(void* c, const uint8_t* b, size_t n){ ((Up*)c)->sent.append((const char*)b, n); return true; }
static bool hCloudOpen(void*, FlexWebConn* up, char* host, size_t hc, char* bearer, size_t bc, char* why, size_t wc){
  if(!g_openWhy.empty()){ snprintf(why, wc, "%s", g_openWhy.c_str()); return false; }
  Up* u = new Up(); g_ups.push_back(u);
  up->read = upRead; up->write = upWrite; up->ctx = u;
  snprintf(host, hc, "192.168.1.77:47830");
  snprintf(bearer, bc, "%s", g_token.c_str());
  return true;
}
static void hCloudClose(void*, FlexWebConn* up){ ((Up*)up->ctx)->closed = true; }
static void hCloudResult(void*, int st){ g_results.push_back(st); }
static std::string g_origin = "http://192.168.1.77:47830";
static void hOrigin(void*, char* out, size_t cap){ snprintf(out, cap, "%s", g_origin.c_str()); }
// ---- emparejamiento: lo que llega al nucleo ----
static std::string g_pairSeen, g_pairPeer, g_pollSeen;
static int g_pairStatus = 202; static uint32_t g_pairRetry = 0;
static bool hOffer(void*, char out[FST_HEX32]){ snprintf(out, FST_HEX32, "%s", "0123456789abcdef0123456789abcdef"); return true; }
static int hPair(void*, const FstPairReq* rq, const char* peer, char* json, size_t cap, uint32_t* retryS){
  g_pairSeen = std::string(rq->offer) + "|" + rq->pid + "|" + rq->name + "|" + rq->model + "|" + rq->port + "|" + rq->pub + "|" +
               (rq->kp4 ? rq->kp4 : "<null>") + "|" + (rq->known ? rq->known : "<null>");
  g_pairPeer = peer;
  if(retryS) *retryS = g_pairRetry;
  snprintf(json, cap, g_pairStatus == 202 ? "{\"ok\":1,\"pairId\":\"abcd\"}" : "{\"error\":\"no\"}");
  return g_pairStatus;
}
static int hPoll(void*, const char* id, const char* proof, char* json, size_t cap){
  g_pollSeen = std::string(id) + "|" + proof;
  snprintf(json, cap, "{\"ok\":1,\"state\":\"pending\"}");
  return 200;
}
// ---- resumen, transferencias y biblioteca ----
static bool hStorage(void*, char* out, size_t cap){
  snprintf(out, cap, "\"phone\":{\"state\":\"ready\",\"name\":\"Galaxy A55\"},\"cloud\":{\"dest\":\"phone\",\"quota\":{\"total\":5368709120}}");
  return true;
}
static bool hXfers(void*, char* out, size_t cap){ snprintf(out, cap, "[{\"id\":7,\"phase\":2}]"); return true; }
static std::vector<FlexWebXferReq> g_xops;
static int hXferOp(void*, const FlexWebXferReq* rq, char* msg, size_t cap){ g_xops.push_back(*rq); snprintf(msg, cap, "En cola"); return 202; }
static std::vector<uint32_t> g_removed; static std::string g_renamed;
static bool hRemove(void*, uint32_t id, char* why, size_t cap){ g_removed.push_back(id); return flexMsDelete(&g_mst, id); }
static bool hRename(void*, uint32_t id, const char* name, char* why, size_t cap){ g_renamed = std::to_string(id) + ":" + name; return flexMsRename(&g_mst, id, name, why, cap); }

static void withStorage(){
  g_w.host.cloudOpen = hCloudOpen; g_w.host.cloudClose = hCloudClose; g_w.host.cloudResult = hCloudResult; g_w.host.phoneOrigin = hOrigin;
  g_w.host.phoneOffer = hOffer; g_w.host.phonePair = hPair; g_w.host.phonePoll = hPoll;
  g_w.host.storageJson = hStorage; g_w.host.xfersJson = hXfers; g_w.host.xferOp = hXferOp;
  g_w.host.removeRec = hRemove; g_w.host.renameRec = hRename;
  for(Up* u : g_ups) delete u;
  g_ups.clear(); g_results.clear(); g_xops.clear(); g_removed.clear();
  g_openWhy.clear(); g_phone = nullptr; g_origin = "http://192.168.1.77:47830";
  g_pairStatus = 202; g_pairRetry = 0;
}
// Peticion desde OTRA maquina de la red (la app del telefono).
static Resp runFrom(const char* peer, const std::string& in){
  Client c; c.in = in;
  FlexWebConn cn = { cRead, cWrite, &c, peer };
  flexWebServeConn(&g_w, &cn, g_hdr, g_io);
  auto v = parseAll(c.out);
  return v.empty() ? Resp() : v[0];
}
static std::string ok200(const std::string& body, const char* type = "application/json", const std::string& extra = ""){
  return "HTTP/1.1 200 OK\r\nContent-Type: " + std::string(type) + "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n" + extra + "\r\n" + body;
}

static void testStorageRoutes(){
  std::printf("-- Flex Storage: sin sus funciones, el servidor es el de siempre --\n");
  setup(); g_cookie.clear();
  CHECK(pair(), "sesion");
  CHECK(run(req("POST", "/api/fs/phone/pair", "offer=x")).status == 404, "sin Flex Storage: no hay emparejamiento");
  CHECK(run(req("GET", "/api/cloud/me")).status == 404, "ni pasarela");
  CHECK(run(req("PUT", "/api/cloud/x", "1")).status == 405 && run(req("PATCH", "/api/library", "{}")).status == 405, "PUT/PATCH: 405 como antes");
  Resp r = run(req("GET", "/"));
  CHECK(r.h["content-security-policy"].find("media-src 'self' blob:;") != std::string::npos, "CSP de siempre");

  std::printf("-- Flex Storage: emparejamiento (lo pide la app del telefono, sin sesion web) --\n");
  withStorage();
  r = run(req("GET", "/"));
  CHECK(r.h["content-security-policy"].find("media-src 'self' blob: http://192.168.1.77:47830;") != std::string::npos &&
        r.h["content-security-policy"].find("img-src 'self' blob: data: http://192.168.1.77:47830;") != std::string::npos,
        "CSP: SOLO el origen del telefono entra en img-src/media-src");
  g_origin = "http://evil.example;script-src *";
  r = run(req("GET", "/"));
  CHECK(r.h["content-security-policy"].find("evil") == std::string::npos && r.h["content-security-policy"].find("script-src 'self';") != std::string::npos,
        "un origen raro no entra en la CSP");
  g_origin = "http://192.168.1.77:47830";
  Resp o = run(req("POST", "/api/fs/phone/offer"));
  cJSON* j = js(o);
  cJSON* link = cJSON_GetObjectItem(j, "link");
  CHECK(o.status == 200 && cJSON_IsString(link) && !strcmp(link->valuestring, "flexstorage://attach?h=192.168.1.50%3A8080&o=0123456789abcdef0123456789abcdef"),
        "oferta para el navegador con el enlace que abre la app");
  cJSON_Delete(j);
  std::string saved = g_cookie; g_cookie.clear();
  CHECK(run(req("POST", "/api/fs/phone/offer")).status == 401, "sin sesion web no hay oferta");
  std::string form = "offer=0123456789abcdef0123456789abcdef&pid=a55-1&name=Galaxy%20A55%20de%20%C3%91and%C3%BA&model=SM-A556B&port=47830&pub=04ab";
  r = runFrom("192.168.1.77", req("POST", "/api/fs/phone/pair", form));
  CHECK(r.status == 202 && r.body.find("pairId") != std::string::npos, "la app empieza sin sesion web: 202");
  CHECK(g_pairSeen == "0123456789abcdef0123456789abcdef|a55-1|Galaxy A55 de \xC3\x91" "and\xC3\xBA|SM-A556B|47830|04ab|<null>|<null>",
        "campos del formulario des-escapados (%s)", g_pairSeen.c_str());
  CHECK(g_pairPeer == "192.168.1.77", "con la IP de quien lo pide (el telefono)");
  runFrom("192.168.1.77", req("POST", "/api/fs/phone/pair", form + "&kp4=flexos-1&known=aa"));
  CHECK(g_pairSeen.find("|flexos-1|aa") != std::string::npos, "telefono conocido: kp4 y known");
  CHECK(runFrom("192.168.1.77", req("POST", "/api/fs/phone/pair", form, "", false)).status == 403, "sin X-Flex: 403");
  CHECK(runFrom("192.168.1.77", req("POST", "/api/fs/phone/pair", form, "", true, "evil.example")).status == 403, "Host ajeno: 403");
  g_pairStatus = 429; g_pairRetry = 30;
  r = runFrom("192.168.1.77", req("POST", "/api/fs/phone/pair", form));
  CHECK(r.status == 429 && r.h["retry-after"] == "30", "demasiadas ofertas falsas: 429 + Retry-After");
  g_pairStatus = 202; g_pairRetry = 0;
  r = runFrom("192.168.1.77", req("POST", "/api/fs/phone/pair/0123456789abcdef0123456789abcdef", "proof=" + std::string(64, 'a')));
  CHECK(r.status == 200 && g_pollSeen == "0123456789abcdef0123456789abcdef|" + std::string(64, 'a'), "sondeo con su prueba");
  CHECK(runFrom("192.168.1.77", req("POST", "/api/fs/phone/pair/NOHEX", "proof=a")).status == 401, "id que no es el de un emparejamiento: no es una ruta publica");
  std::string big = "offer=" + std::string(3000, 'a');
  CHECK(runFrom("192.168.1.77", req("POST", "/api/fs/phone/pair", big)).status == 400, "cuerpo enorme: 400");
  g_cookie = saved;

  std::printf("-- Flex Storage: resumen (lo protegido no se cuenta por clase) --\n");
  std::vector<uint8_t> jpg = makeJpeg(64, 48);
  uint32_t crc = flexMlCrc32(0, jpg.data(), jpg.size());
  r = run(req("POST", upPath("photo", "a.jpg", jpg, crc), S(jpg)));
  CHECK(r.status == 201, "foto subida");
  std::vector<uint8_t> jpg2 = makeJpeg(80, 60, 9);
  uint32_t crc2 = flexMlCrc32(0, jpg2.data(), jpg2.size());
  r = run(req("POST", upPath("photo", "b.jpg", jpg2, crc2), S(jpg2)));
  j = js(r); uint32_t idB = (uint32_t)jint(j, "id"); cJSON_Delete(j);
  char why[64]; CHECK(flexMsSetLock(&g_mst, idB, true, why, sizeof(why)), "una bloqueada");
  r = run(req("GET", "/api/fs/overview"));
  j = js(r);
  cJSON* loc = cJSON_GetObjectItem(j, "local");
  CHECK(r.status == 200 && loc && jint(cJSON_GetObjectItem(loc, "photo"), "n") == 1 && jint(cJSON_GetObjectItem(loc, "protected"), "n") == 1 &&
        jint(loc, "total") == (int)g_fs.total, "Flex OS: 1 foto a la vista y 1 protegida aparte");
  cJSON* ph = cJSON_GetObjectItem(j, "phone");
  CHECK(ph && cJSON_IsString(cJSON_GetObjectItem(ph, "name")) && cJSON_GetObjectItem(j, "cloud"), "telefono y Flex Cloud del anfitrion");
  cJSON_Delete(j);

  std::printf("-- Flex Storage: transferencias Flex OS <-> Flex Cloud --\n");
  r = run(req("GET", "/api/fs/xfers"));
  CHECK(r.status == 200 && r.body == "{\"items\":[{\"id\":7,\"phase\":2}]}", "lista de transferencias");
  uint32_t idA = 0;
  { FlexMlRec recs[8]; uint32_t rv; int n = flexMsSnapshot(&g_mst, recs, 8, &rv); for(int i = 0; i < n; i++) if(!(recs[i].flags & FML_R_LOCKED)) idA = recs[i].id; }
  r = run(req("POST", "/api/fs/xfer", "op=up&id=" + std::to_string(idA) + "&folder=fld_abcdefgh2345&move=1"));
  CHECK(r.status == 202 && g_xops.size() == 1 && g_xops[0].op == FLEXWEB_X_UP && g_xops[0].id == idA && g_xops[0].move &&
        !strcmp(g_xops[0].folder, "fld_abcdefgh2345"), "subir y mover: a la cola del gestor");
  CHECK(run(req("POST", "/api/fs/xfer", "op=up&id=" + std::to_string(idB))).status == 403, "lo protegido no sale desde la web");
  CHECK(run(req("POST", "/api/fs/xfer", "op=up&id=" + std::to_string(idA) + "&folder=../x")).status == 400, "carpeta rara: 400");
  std::string sha(64, 'b');
  r = run(req("POST", "/api/fs/xfer", "op=down&file=fil_abcdefgh2345abcd&name=Viaje%20%C3%B1.avi&size=1048576&sha=" + sha));
  CHECK(r.status == 202 && g_xops.size() == 2 && g_xops[1].op == FLEXWEB_X_DOWN && !strcmp(g_xops[1].name, "Viaje \xC3\xB1.avi") &&
        g_xops[1].size == 1048576 && !strcmp(g_xops[1].sha, sha.c_str()) && !g_xops[1].move, "bajar: nombre, tamano y huella");
  CHECK(run(req("POST", "/api/fs/xfer", "op=down&file=fil_abcdefgh2345abcd&name=x&size=10&sha=XYZ")).status == 400, "sin huella valida no se baja nada");
  CHECK(run(req("POST", "/api/fs/xfer", "op=down&file=../../etc&name=x&size=10&sha=" + sha)).status == 400, "id de archivo raro: 400");
  CHECK(run(req("POST", "/api/fs/xfer", "op=cancel&job=9")).status == 202 && g_xops.back().op == FLEXWEB_X_CANCEL && g_xops.back().id == 9, "cancelar");
  CHECK(run(req("POST", "/api/fs/xfer", "op=boom")).status == 400, "operacion desconocida");
  CHECK(run(req("POST", "/api/fs/xfer", "op=clear", "", false)).status == 403, "sin X-Flex: 403");

  std::printf("-- Flex Storage: borrar y renombrar en Flex OS --\n");
  r = run(req("POST", "/api/local/rename", "id=" + std::to_string(idA) + "&name=Playa%20%C3%91and%C3%BA"));
  CHECK(r.status == 200 && g_renamed == std::to_string(idA) + ":Playa \xC3\x91" "and\xC3\xBA", "renombrar");
  CHECK(run(req("POST", "/api/local/rename", "id=" + std::to_string(idA) + "&name=%20%20")).status == 400, "nombre vacio: 400");
  CHECK(run(req("POST", "/api/local/rename", "id=" + std::to_string(idB) + "&name=x")).status == 403, "un protegido no, sin la clave");
  r = run(req("POST", "/api/local/delete", "ids=" + std::to_string(idB)));
  CHECK(r.status == 403 && g_removed.empty(), "borrar un protegido sin la clave: 403 y no se toca");
  r = run(req("POST", "/api/local/delete", "ids=999," + std::to_string(idA)));
  j = js(r);
  CHECK(r.status == 200 && jint(j, "ok") == 1 && g_removed.size() == 1 && g_removed[0] == idA, "borrar el suyo (lo que no existe se ignora)");
  cJSON_Delete(j);
}

static void testCloudProxy(){
  std::printf("-- Flex Storage: pasarela /api/cloud (el token del telefono nunca llega al navegador) --\n");
  setup(); g_cookie.clear(); withStorage();
  CHECK(pair(), "sesion");
  std::string list = "{\"ok\":true,\"items\":[],\"nextCursor\":null}";
  g_phone = [&](const std::string&){ return ok200(list); };
  Resp r = run(req("GET", "/api/cloud/files?parentId=root&limit=40"));
  CHECK(r.status == 200 && r.body == list && r.h["content-type"] == "application/json", "lista del telefono, tal cual");
  CHECK(g_ups.size() == 1, "una conexion con el telefono");
  const std::string& sent = g_ups[0]->sent;
  CHECK(sent.rfind("GET /api/cloud/files?parentId=root&limit=40 HTTP/1.1\r\n", 0) == 0, "misma ruta y consulta");
  CHECK(sent.find("Host: 192.168.1.77:47830\r\n") != std::string::npos && sent.find("Authorization: Bearer " + g_token + "\r\n") != std::string::npos,
        "con el Host y el token del TELEFONO");
  CHECK(sent.find("fxs=") == std::string::npos && sent.find(g_cookie) == std::string::npos, "la cookie del P4 no viaja al telefono");
  CHECK(r.body.find(g_token) == std::string::npos && r.h["set-cookie"].empty(), "ni el token vuelve al navegador");
  CHECK(g_ups[0]->closed && g_results.size() == 1 && g_results[0] == 200, "conexion cerrada y resultado anotado");

  std::string savedCookie = g_cookie; g_cookie.clear();
  size_t opens = g_ups.size();
  CHECK(run(req("GET", "/api/cloud/me")).status == 401 && g_ups.size() == opens, "sin sesion web: 401 y el telefono ni se entera");
  g_cookie = savedCookie;
  CHECK(run(req("PUT", "/api/cloud/uploads/upl_1/parts/1", "abc", "", false)).status == 403 && g_ups.size() == opens, "PUT sin X-Flex: 403");

  // Una parte de 1 MB: a trozos, sin guardarla, con su huella.
  std::string part(1024 * 1024, '\0');
  for(size_t i = 0; i < part.size(); i++) part[i] = (char)(i * 131 + 7);
  std::string partSha(64, 'c');
  std::string got;
  g_phone = [&](const std::string& in){
    size_t he = in.find("\r\n\r\n");
    got = in.substr(he + 4);
    return ok200("{\"ok\":true,\"partNumber\":1}");
  };
  long live0 = g_live;
  r = run(req("PUT", "/api/cloud/uploads/upl_1/parts/1", part, ("X-Part-SHA256: " + partSha + "\r\nContent-Type: application/octet-stream\r\n").c_str()));
  CHECK(r.status == 200 && got == part, "1 MB llega al telefono byte a byte");
  const std::string& ps = g_ups.back()->sent;
  CHECK(ps.find("Content-Length: 1048576\r\n") != std::string::npos && ps.find("X-Part-SHA256: " + partSha + "\r\n") != std::string::npos &&
        ps.find("Content-Type: application/octet-stream\r\n") != std::string::npos, "con su longitud, su tipo y su huella");
  CHECK(g_live == live0, "sin una sola reserva de memoria: por el buffer de siempre");

  // PATCH y DELETE: la API de Flex Cloud entera.
  std::string pbody;
  g_phone = [&](const std::string& in){ pbody = in.substr(in.find("\r\n\r\n") + 4); return ok200("{\"ok\":true}"); };
  r = run(req("PATCH", "/api/cloud/files/fil_abcdefgh2345", "{\"name\":\"N\\u00f1\"}", "Content-Type: application/json\r\n"));
  CHECK(r.status == 200 && pbody == "{\"name\":\"N\\u00f1\"}" && g_ups.back()->sent.rfind("PATCH ", 0) == 0, "PATCH con su JSON");
  r = run(req("DELETE", "/api/cloud/files/fil_abcdefgh2345"));
  CHECK(r.status == 200 && g_ups.back()->sent.rfind("DELETE /api/cloud/files/fil_abcdefgh2345 HTTP/1.1", 0) == 0, "DELETE");

  // Rangos (miniaturas, descargas): 206 con su Content-Range.
  g_phone = [&](const std::string&){
    return std::string("HTTP/1.1 206 Partial Content\r\nContent-Type: video/x-msvideo\r\nContent-Range: bytes 100-199/1000\r\nAccept-Ranges: bytes\r\n"
                       "ETag: \"abc\"\r\nContent-Length: 100\r\n\r\n") + std::string(100, 'v');
  };
  r = run(req("GET", "/api/cloud/download/fil_abcdefgh2345", "", "Range: bytes=100-199\r\nIf-Range: \"abc\"\r\n"));
  CHECK(r.status == 206 && r.h["content-range"] == "bytes 100-199/1000" && r.h["accept-ranges"] == "bytes" && r.h["etag"] == "\"abc\"" && r.body.size() == 100,
        "206 con Content-Range, Accept-Ranges y ETag");
  CHECK(g_ups.back()->sent.find("Range: bytes=100-199\r\n") != std::string::npos && g_ups.back()->sent.find("If-Range: \"abc\"\r\n") != std::string::npos,
        "el rango y el If-Range llegan al telefono");

  // Descarga grande por la pasarela: a trozos y sin memoria extra.
  std::string bigBody(3 * 1024 * 1024 + 17, 'x');
  for(size_t i = 0; i < bigBody.size(); i += 4096) bigBody[i] = (char)i;
  g_phone = [&](const std::string&){ return ok200(bigBody, "application/octet-stream"); };
  live0 = g_live;
  r = run(req("GET", "/api/cloud/download/fil_abcdefgh2345"));
  CHECK(r.status == 200 && r.body == bigBody && g_live == live0, "3 MB del telefono al navegador a trozos, sin reservar memoria");

  // La sesion del telefono caduco: con un cuerpo pequeno, se repite UNA vez.
  int calls = 0;
  g_phone = [&](const std::string&){
    calls++;
    if(calls == 1) return std::string("HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\nContent-Length: 16\r\n\r\n{\"ok\":false,\"x\"}");
    return ok200("{\"ok\":true,\"renamed\":1}");
  };
  g_results.clear();
  r = run(req("POST", "/api/cloud/folders", "{\"name\":\"Viaje\"}", "Content-Type: application/json\r\n"));
  CHECK(r.status == 200 && calls == 2 && g_results.size() == 2 && g_results[0] == 401, "401 del telefono: sesion nueva y se repite sola");
  CHECK(g_ups[g_ups.size() - 2]->sent.find("{\"name\":\"Viaje\"}") != std::string::npos && g_ups.back()->sent.find("{\"name\":\"Viaje\"}") != std::string::npos,
        "el mismo cuerpo, las dos veces");
  // ...con una parte grande no se puede repetir: el navegador reintenta en 1 s.
  calls = 0;
  g_phone = [&](const std::string&){ calls++; return std::string("HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n"); };
  r = run(req("PUT", "/api/cloud/uploads/upl_1/parts/2", part, ("X-Part-SHA256: " + partSha + "\r\n").c_str()));
  cJSON* j = js(r);
  cJSON* er = cJSON_GetObjectItem(j, "error");
  CHECK(r.status == 503 && r.h["retry-after"] == "1" && er && !strcmp(cJSON_GetObjectItem(er, "code")->valuestring, "phone_session") && calls == 1,
        "parte grande con sesion caducada: 503 'phone_session' + Retry-After (no es la sesion del navegador)");
  cJSON_Delete(j);

  // Sin telefono, apagado o rechazado: errores con la forma de Flex Cloud.
  g_openWhy = "network";
  r = run(req("GET", "/api/cloud/me"));
  j = js(r); er = cJSON_GetObjectItem(j, "error");
  CHECK(r.status == 503 && r.h["retry-after"] == "5" && er && !strcmp(cJSON_GetObjectItem(er, "code")->valuestring, "phone_offline") &&
        strstr(cJSON_GetObjectItem(er, "message")->valuestring, "desconectado"), "Telefono desconectado (503 + Retry-After)");
  cJSON_Delete(j);
  g_openWhy = "no_phone";
  CHECK(run(req("GET", "/api/cloud/me")).status == 409, "sin telefono emparejado: 409");
  g_openWhy = "phone_rejected";
  CHECK(run(req("GET", "/api/cloud/me")).status == 403, "rechazado: 403");
  g_openWhy.clear();

  // Respuestas raras del telefono: nunca se reenvian a medias.
  g_phone = [&](const std::string&){ return std::string("BASURA\r\n\r\n"); };
  CHECK(run(req("GET", "/api/cloud/me")).status == 502, "basura: 502");
  g_phone = [&](const std::string&){ return std::string(); };
  CHECK(run(req("GET", "/api/cloud/me")).status == 504, "se corta sin contestar: 504");
  g_phone = [&](const std::string&){ return std::string("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n"); };
  CHECK(run(req("GET", "/api/cloud/me")).status == 502, "por trozos sin longitud: 502");
  size_t before = g_ups.size();
  CHECK(run(req("GET", "/api/cloud/d/firmado123456")).status == 404 && g_ups.size() == before, "los enlaces firmados no pasan por el P4");
  CHECK(run(req("GET", "/api/cloud/files%20x")).status == 400 && g_ups.size() == before, "ruta con caracteres raros: 400");
  CHECK(run(req("PUT", "/api/cloud/uploads/upl_1/parts/1", "", "Content-Length: 20000000\r\n")).status == 413, "demasiado grande: 413");

  // Keep-alive: una respuesta de la pasarela no estropea la siguiente peticion.
  g_phone = [&](const std::string&){ return ok200("{\"ok\":true,\"n\":1}"); };
  Client c;
  std::string a = req("GET", "/api/cloud/me"); a.replace(a.find("Connection: close"), 17, "Connection: keep-alive");
  c.in = a + req("GET", "/api/status");
  FlexWebConn cn = { cRead, cWrite, &c, nullptr };
  flexWebServeConn(&g_w, &cn, g_hdr, g_io);
  auto v = parseAll(c.out);
  CHECK(v.size() == 2 && v[0].status == 200 && v[0].body == "{\"ok\":true,\"n\":1}" && v[1].status == 200 && v[1].body.find("\"rev\"") != std::string::npos,
        "dos peticiones en la misma conexion: pasarela y despues /api/status");
  for(Up* u : g_ups) delete u;
  g_ups.clear();
}

static void testFuzz(){
  std::printf("-- ruido contra el servidor --\n");
  setup(); g_cookie.clear(); pair();
  uint32_t s = 0xBEEF;
  for(int it = 0; it < 1500; it++){
    std::string in;
    const char* pre[] = { "GET /api/", "POST /api/upload?kind=photo&size=10&crc=1&name=a", "HEAD /", "" };
    in = pre[it % 4];
    int n = (int)(s % 300);
    for(int i = 0; i < n; i++){ s = s * 1103515245u + 12345u; char ch = (char)(s >> 16); if((s >> 9) % 5 == 0) ch = "\r\n:=&?/ "[(s >> 3) % 8]; in += ch; }
    if(it % 3 == 0) in += "\r\n\r\n";
    Client c; c.chunk = 1 + s % 64;
    run(in, &c);
  }
  CHECK(g_fs.tmpCount() == 0 && !g_w.uploading, "1500 peticiones de ruido: sin temporales y servidor libre");
  // Lo mismo con Flex Storage: ruido hacia la pasarela, el emparejamiento y
  // las rutas nuevas, con un "telefono" que a veces contesta basura.
  withStorage();
  uint32_t t = 0xF1E2;
  g_phone = [&](const std::string&){
    t = t * 1103515245u + 12345u;
    std::string r = (t >> 8) % 2 ? std::string("HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nabcd") : std::string();
    for(int i = 0; i < (int)((t >> 4) % 200); i++){ t = t * 1103515245u + 12345u; r += (char)(t >> 16); }
    return r;
  };
  long live = g_live;
  for(int it = 0; it < 1500; it++){
    const char* pre[] = { "GET /api/cloud/", "PUT /api/cloud/uploads/x/parts/1 HTTP/1.1\r\nX-Flex: 1\r\nCookie: fxs=", "POST /api/fs/phone/pair",
                          "POST /api/fs/xfer HTTP/1.1\r\nX-Flex: 1\r\nContent-Length: 40\r\n\r\nop=", "PATCH /api/cloud/files/" };
    std::string in = pre[it % 5];
    if(it % 5 == 1) in += g_cookie + "\r\nContent-Length: 30\r\n\r\n";
    int n = (int)(s % 300);
    for(int i = 0; i < n; i++){ s = s * 1103515245u + 12345u; char ch = (char)(s >> 16); if((s >> 9) % 5 == 0) ch = "\r\n:=&?/ "[(s >> 3) % 8]; in += ch; }
    if(it % 3 == 0) in += "\r\n\r\n";
    Client c; c.chunk = 1 + s % 64;
    run(in, &c);
  }
  bool allClosed = true;
  for(Up* u : g_ups) if(!u->closed) allClosed = false;
  CHECK(allClosed && g_live == live, "1500 mas hacia Flex Storage: toda conexion con el telefono cerrada y sin fugas");
  for(Up* u : g_ups) delete u;
  g_ups.clear();
}

int main(){
  std::printf("=== FlexOS · Flex Web Server de extremo a extremo ===\n");
  testPublicAndPairing();
  testUploads();
  testVideoAndLibrary();
  testLocked();
  testBigAndHeavy();
  testStorageRoutes();
  testCloudProxy();
  testFuzz();
  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
