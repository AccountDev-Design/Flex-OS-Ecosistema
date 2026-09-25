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

int main(){
  std::printf("=== FlexOS · Flex Web Server de extremo a extremo ===\n");
  testPublicAndPairing();
  testUploads();
  testVideoAndLibrary();
  testLocked();
  testBigAndHeavy();
  testFuzz();
  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
