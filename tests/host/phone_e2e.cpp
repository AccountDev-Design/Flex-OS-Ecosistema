// #############################################################
//  FLEX STORAGE · PUNTA A PUNTA: el P4 contra el servidor REAL del telefono
//  ------------------------------------------------------------
//  test_cloud.cpp enfrenta el gestor de Flex Cloud con destino telefono a un
//  telefono simulado en C++. Este lo enfrenta al DE VERDAD: CloudServer.kt
//  del modulo :storage de Flex Phone (Kotlin, JVM), arrancado por
//  phone_e2e.sh ya emparejado con este P4 y con la clave que se le pasa.
//
//  Lo que se ejecuta aqui es el codigo de la placa: FlexOS_StorageLink
//  (sesion con reto-respuesta y autenticacion mutua), FlexOS_StorageCore
//  (HMAC) y FlexOS_Cloud (subidas por partes con SHA-256, descargas
//  verificadas, streaming por rangos, operaciones, cuota) sobre netstub,
//  cuyas peticiones se reenvian por un puente HTTP al servidor Kotlin. Si el
//  contrato cambia en un lado y no en el otro, falla aqui.
// #############################################################
#include "netstub.h"
#include "FS.h"
#include "FlexOS_Account.h"
#include "FlexOS_Cloud.h"
#include "FlexOS_FS.h"
#include "FlexOS_MediaLib.h"
#include "FlexOS_JPEGEnc.h"
#include "FlexOS_StorageCore.h"
#include "FlexOS_StorageLink.h"
#include "pkgbuild.h"
#include "e2e_http.h"
#include <cJSON.h>
#include <string>
#include <vector>
#include <functional>
#include <map>
#include <unistd.h>
#include <sys/stat.h>

void fsStubReset();

static int gChecks = 0, gFails = 0;
#define CHECK(cond, msg) do { gChecks++; if(!(cond)){ gFails++; printf("  FALLO: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } else if(getenv("E2E_VERBOSE")) printf("  ok: %s\n", msg); } while(0)

static int gPort = 0;
static std::string gBase;                       // http://127.0.0.1:PUERTO
static std::string gKeyHex;
static std::function<bool(const NetRequest&, NetResponse&)> gFault;
static int gForwarded = 0, gToPhoneHttps = 0, gElsewhere = 0;

// ------------------------------------------------- puente al telefono
static NetResponse bridge(const NetRequest& rq){
  NetResponse rs;
  if(gFault && gFault(rq, rs)) return rs;
  if(rq.url.compare(0, gBase.size(), gBase)){ gElsewhere++; rs.status = HTTPC_ERROR_CONNECTION_REFUSED; return rs; }
  if(rq.https) gToPhoneHttps++;
  gForwarded++;
  Raw r = e2eHttp(gPort, rq.method, rq.url.substr(gBase.size()), rq.headers, rq.body);
  if(r.status < 0){ rs.status = HTTPC_ERROR_CONNECTION_REFUSED; return rs; }
  rs.status = r.status; rs.body = r.body; rs.headers = r.headers;
  return rs;
}

// ----------------------------------------------------------- utilidades
static void step(unsigned long ms = 20){ flexCloudTestStep(); netstubAdvance(ms); }
static bool pumpUntil(std::function<bool()> cond, int max = 20000){ for(int i = 0; i < max; i++){ if(cond()) return true; step(); } return cond(); }
static std::vector<FlexCloudEvent> gEv;
static void drain(){ FlexCloudEvent e; while(flexCloudPollEvent(&e)) gEv.push_back(e); }
static const FlexCloudEvent* findEv(uint8_t kind, uint32_t op){ for(auto& e : gEv) if(e.kind == kind && e.opId == op) return &e; return nullptr; }
static const FlexCloudEvent* waitAny(uint32_t op, uint8_t ok, uint8_t bad, int max = 40000){
  pumpUntil([&]{ drain(); return findEv(ok, op) || findEv(bad, op); }, max);
  const FlexCloudEvent* e = findEv(ok, op);
  if(!e){ const FlexCloudEvent* b = findEv(bad, op); if(b) printf("   (fallo: %s · %s)\n", b->code, b->text); }
  return e;
}
static std::string pattern(size_t n, uint32_t seed){
  std::string s(n, '\0'); uint32_t x = seed * 747796405u + 2891336453u;
  for(size_t i = 0; i < n; i++){ x ^= x << 13; x ^= x >> 17; x ^= x << 5; s[i] = (char)x; }
  return s;
}
static std::string sha256hex(const std::string& s){ return pkgb::hex(pkgb::sha256((const uint8_t*)s.data(), s.size())); }
static void putLocal(const char* path, const std::string& data){
  FlexFsStream* f = flexFsOpenWrite(path);
  if(f){ flexFsStreamWrite(f, data.data(), data.size()); flexFsStreamClose(f); }
}
static std::string localFile(const std::string& path){ auto it = gFs.find(path); return it == gFs.end() ? std::string("<no>") : std::string(it->second.data.begin(), it->second.data.end()); }
// La foto para el visor llega a un buffer de RAM (nunca a la flash): se espera, se copia y se suelta.
static bool viewPhoto(const FclItem& it, std::string& out, std::string* err = nullptr){
  uint32_t op = flexCloudViewStart(&it);
  if(!op) return false;
  pumpUntil([&]{ return flexCloudViewState(op, nullptr, nullptr, nullptr, 0) != FCV_FETCHING; }, 4000);
  char e[96] = ""; uint8_t st = flexCloudViewState(op, nullptr, nullptr, e, sizeof(e));
  if(err) *err = e;
  if(st != FCV_READY) return false;
  uint32_t n = 0; uint8_t* p = flexCloudViewTake(op, &n);
  if(!p) return false;
  out.assign((const char*)p, n); flexCloudViewFree(p);
  return true;
}
static bool listReady(){ FlexCloudListInfo l; flexCloudListInfo(&l); return l.state == FCL_LIST_READY || l.state == FCL_LIST_ERROR; }
static std::vector<FclItem> listNow(uint8_t view, const char* folder){
  flexCloudRequestList(view, folder, nullptr);
  pumpUntil(listReady, 400);
  FlexCloudListInfo l; flexCloudListInfo(&l);
  std::vector<FclItem> v((size_t)(l.count > 0 ? l.count : 0));
  if(l.count > 0) flexCloudListCopy(v.data(), 0, l.count);
  return v;
}
static const FclItem* byName(const std::vector<FclItem>& v, const char* name){ for(auto& i : v) if(!strcmp(i.name, name)) return &i; return nullptr; }
static FlexCloudStatus status(){ FlexCloudStatus s; flexCloudStatus(&s); return s; }


// ------------------------------------------------- lo multimedia que prepara el telefono
static std::string readReal(const std::string& path){
  std::string out; FILE* f = fopen(path.c_str(), "rb"); if(!f) return out;
  char b[65536]; size_t n; while((n = fread(b, 1, sizeof(b), f)) > 0) out.append(b, n);
  fclose(f); return out;
}
static void writeReal(const std::string& path, const std::string& data){ FILE* f = fopen(path.c_str(), "wb"); if(f){ fwrite(data.data(), 1, data.size(), f); fclose(f); } }
// Lee TODO lo que el P4 reproduciria por rangos (lo que lee el visor o Musica), pidiendo los bloques como lo hace la cache.
static bool streamAll(const FclItem& item, std::string& out){
  out.clear();
  if(!flexCloudStreamOpen(&item)) return false;
  uint32_t size = flexCloudStreamSize();
  uint8_t buf[16384]; uint32_t off = 0; int spins = 0;
  while(off < size){
    uint32_t want = size - off < sizeof(buf) ? size - off : (uint32_t)sizeof(buf);
    int r = flexCloudStreamRead(off, buf, want);
    if(r < 0){ if(spins++ > 200000) break; flexCloudTestStreamStep(); netstubAdvance(2); usleep(100); continue; }
    if(r == 0) break;
    out.append((const char*)buf, (size_t)r); off += (uint32_t)r; spins = 0;
  }
  flexCloudStreamClose();
  return off == size;
}

// El telefono emparejado, como lo dejaria el emparejamiento en la NVS.
static void writePhone(const char* keyHex){
  FstPhone ph; memset(&ph, 0, sizeof(ph));
  ph.valid = 1; ph.enabled = 1;
  snprintf(ph.id, sizeof(ph.id), "a55-e2e"); snprintf(ph.name, sizeof(ph.name), "Galaxy A55 5G"); snprintf(ph.model, sizeof(ph.model), "SM-A556B");
  fstUnhex(keyHex, ph.key, 32);
  snprintf(ph.ip, sizeof(ph.ip), "127.0.0.1"); ph.port = (uint16_t)gPort;
  uint8_t buf[FST_PHONE_BLOB_MAX]; size_t n = fstPhoneEncode(&ph, buf, sizeof(buf));
  Preferences pr; pr.begin("flexstor", false); pr.putBytes("phone", buf, n); pr.end();
}
static void reboot(){
  flexCloudTestPowerOff();
  flexStorageTestReset();
  flexStorageBegin();
  flexCloudBegin();
  gEv.clear();
}

int main(){
  const char* port = getenv("PHONE_E2E_PORT");
  const char* key = getenv("PHONE_E2E_KEY");
  uint64_t quota = getenv("PHONE_E2E_QUOTA") ? strtoull(getenv("PHONE_E2E_QUOTA"), nullptr, 10) : 0;
  if(!port || !key || strlen(key) != 64 || !quota){ printf("phone_e2e: faltan PHONE_E2E_PORT, PHONE_E2E_KEY y PHONE_E2E_QUOTA (usa phone_e2e.sh)\n"); return 2; }
  gPort = atoi(port); gKeyHex = key; gBase = "http://127.0.0.1:" + std::to_string(gPort);
  printf("=== FlexOS · Flex Storage: el P4 contra el servidor REAL del telefono (%s) ===\n", gBase.c_str());
  netstubNvsWipe(); netstubReset(); fsStubReset();
  flexFsBegin(); flexFsMkdir("/System");
  gNetHandler = bridge; gNetWifi = true;
  writePhone(key);
  reboot();
  CHECK(flexCloudDest() == FCD_PHONE && flexStoragePhoneUsable(), "destino: el telefono emparejado");

  // ---- sesion mutua y cuota
  flexCloudSetActive(true);
  CHECK(pumpUntil([]{ return status().net == FCN_ONLINE && status().quotaValid; }, 4000), "ONLINE: sesion abierta con el telefono REAL");
  FlexCloudStatus s = status();
  CHECK(s.quota.totalBytes == quota, "la cuota la decide el telefono (la logica que le fija phone_e2e.sh)");
  CHECK(!strcmp(s.address, "Galaxy A55 5G"), "la tarjeta nombra al telefono");
  FlexStorageInfo si; flexStorageInfo(&si);
  CHECK(si.reachable && si.state == FSP_READY, "Flex Storage: conectado");

  // ---- carpeta Unicode y lista
  const char* folderName = "Viaje a Espa\xC3\xB1" "a \xE2\x80\x94 \xE6\x97\x85\xE8\xA1\x8C \xF0\x9F\x8C\x8D";
  uint32_t op = flexCloudMkdir("root", folderName);
  const FlexCloudEvent* e = waitAny(op, FCE_OP_DONE, FCE_NONE, 400);
  CHECK(e && e->ok, "carpeta creada en el telefono");
  std::vector<FclItem> root = listNow(FCL_VIEW_FOLDER, "root");
  const FclItem* folder = byName(root, folderName);
  CHECK(folder && folder->isFolder, "la carpeta aparece con su nombre EXACTO");
  std::string folderId = folder ? folder->id : "";

  // ---- subida por partes (con un corte de red a mitad) y "liberar espacio"
  std::string data = pattern(3 * 1024 * 1024 + 4321, 77);
  putLocal("/Fotos/viaje.avi", data);
  int puts = 0, cut = 1;
  gFault = [&](const NetRequest& rq, NetResponse& rs){
    if(rq.method == "PUT" && rq.url.find("/parts/") != std::string::npos){ puts++; if(cut && puts == 6){ cut = 0; rs.status = HTTPC_ERROR_CONNECTION_LOST; return true; } }
    return false;
  };
  uint32_t up = flexCloudUpload("/Fotos/viaje.avi", "Viaje \xC3\x91" "and\xC3\xBA.avi", folderId.c_str(), 0, FCL_JF_FREE_LOCAL);
  CHECK(up >= 0x40000000u, "trabajo del telefono (numeracion propia)");
  e = waitAny(up, FCE_UPLOAD_DONE, FCE_UPLOAD_FAILED, 60000);
  CHECK(e != nullptr, "subida terminada contra el telefono real");
  CHECK(e && e->sha256 == sha256hex(data), "el telefono confirma la MISMA huella que el original");
  CHECK(e && (e->flags & FCL_JF_FREE_LOCAL), "y solo entonces se ofrece liberar espacio");
  CHECK(puts <= 13 + 1, "13 partes + el reintento de la cortada");
  CHECK(localFile("/Fotos/viaje.avi") == data, "el original intacto");
  gFault = nullptr;

  std::vector<FclItem> inFolder = listNow(FCL_VIEW_FOLDER, folderId.c_str());
  const FclItem* it = byName(inFolder, "Viaje \xC3\x91" "and\xC3\xBA.avi");
  CHECK(it && it->size == data.size() && it->sha256 == sha256hex(data) && it->kind == FCL_K_VIDEO, "en la carpeta: tamano, huella y tipo");
  CHECK(it && it->fromDevice, "marcada como subida desde un Flex OS");
  FclItem item; memset(&item, 0, sizeof(item)); if(it) item = *it;

  // ---- descarga con corte y reanudacion por Range
  std::vector<std::string> ranges;
  int dcut = 1;
  gFault = [&](const NetRequest& rq, NetResponse& rs){
    if(rq.url.find("/download/") == std::string::npos) return false;
    auto r = rq.headers.find("range"); if(r != rq.headers.end()) ranges.push_back(r->second);
    if(dcut){ dcut = 0; rs = bridge(rq); rs.cutAt = 1000000; return true; }
    return false;
  };
  uint32_t dl = flexCloudDownload(&item, FCL_JF_TO_LIBRARY);
  e = waitAny(dl, FCE_DOWNLOAD_DONE, FCE_DOWNLOAD_FAILED, 60000);
  CHECK(e && localFile(e->localPath) == data, "descargada byte a byte tras un corte");
  CHECK(ranges.size() == 1 && ranges[0] == "bytes=1000000-", "reanudada con Range desde el byte exacto (If-Range con la huella)");
  CHECK(e && std::string(e->localPath).rfind("/System/Cloud/pdl/", 0) == 0, "en la carpeta de descargas del telefono");
  gFault = nullptr;

  // ---- streaming por rangos: lecturas en cualquier punto
  CHECK(flexCloudStreamOpen(&item), "streaming abierto desde el telefono");
  uint8_t buf[8192];
  bool same = true; int spins = 0;
  const uint32_t offs[] = { 0, 1234567, (uint32_t)data.size() - 8192, 2u * 1024 * 1024 + 17, 65535 };
  for(uint32_t off : offs){
    flexCloudStreamSeek(off);
    int r;
    while((r = flexCloudStreamRead(off, buf, sizeof(buf))) < 0 && spins++ < 50000){ flexCloudTestStreamStep(); netstubAdvance(2); }
    if(r != (int)sizeof(buf) || memcmp(buf, data.data() + off, sizeof(buf))) same = false;
  }
  CHECK(same, "5 saltos: los bytes del streaming son los del archivo (206 + Content-Range del telefono)");
  flexCloudStreamClose();

  // ---- miniatura (objeto aparte) y foto original para el visor
  std::vector<uint16_t> px(320 * 240, 0x07E0);
  FlexJeCfg cfg = { 320, 240, 85, FLEXJE_SUB_420, FLEXJE_IN_RGB565 };
  std::string jpg;
  flexJpegEncodeMem(&cfg, px.data(), 640, [](void* c, const uint8_t* d, size_t n){ ((std::string*)c)->append((const char*)d, n); return true; }, &jpg, nullptr, nullptr);
  putLocal(FML_DIR_THUMB "/42.jpg", jpg);
  std::string photo = pattern(150000, 5);
  putLocal("/Fotos/verde.jpg", photo);
  uint32_t up2 = flexCloudUpload("/Fotos/verde.jpg", "verde.jpg", "root", 42, 0);
  e = waitAny(up2, FCE_UPLOAD_DONE, FCE_UPLOAD_FAILED, 20000);
  CHECK(e != nullptr, "foto de la Galeria subida al telefono");
  std::string photoId = e ? e->fileId : "";
  flexCloudWantThumb(photoId.c_str());
  uint32_t g0 = flexCloudThumbGen();
  pumpUntil([&]{ return flexCloudThumbGen() != g0; }, 400);
  uint16_t center = 0;
  bool drawn = flexCloudThumbDraw(photoId.c_str(), [](const uint16_t* p, int side, void* u){ *(uint16_t*)u = p[side * side / 2 + side / 2]; }, &center);
  CHECK(drawn && (center & 0x07E0) >= 0x0700 && (center & 0xF800) < 0x1000, "la miniatura vuelve del telefono y es verde");
  std::vector<FclItem> rootNow = listNow(FCL_VIEW_FOLDER, "root");
  const FclItem* ph = byName(rootNow, "verde.jpg");
  CHECK(ph && ph->hasThumb && ph->size == photo.size(), "el ORIGINAL conserva su tamano; la miniatura es aparte");
  FclItem phi; memset(&phi, 0, sizeof(phi)); if(ph) phi = *ph;
  { std::string pv; CHECK(viewPhoto(phi, pv) && pv == photo, "el visor recibe la foto ORIGINAL (a la RAM)"); }

  // ---- renombrar, papelera, restaurar y borrar para siempre
  op = flexCloudRename(&phi, "verde (copia de seguridad).jpg");
  e = waitAny(op, FCE_OP_DONE, FCE_NONE, 400);
  CHECK(e && e->ok, "renombrada");
  rootNow = listNow(FCL_VIEW_FOLDER, "root");
  ph = byName(rootNow, "verde (copia de seguridad).jpg");
  CHECK(ph != nullptr, "con su nombre nuevo");
  if(ph) phi = *ph;
  op = flexCloudTrash(&phi);
  e = waitAny(op, FCE_OP_DONE, FCE_NONE, 400);
  CHECK(e && e->ok, "a la papelera del telefono");
  std::vector<FclItem> trash = listNow(FCL_VIEW_TRASH, nullptr);
  CHECK(byName(trash, "verde (copia de seguridad).jpg") != nullptr, "en la papelera");
  op = flexCloudRestore(&phi);
  e = waitAny(op, FCE_OP_DONE, FCE_NONE, 400);
  CHECK(e && e->ok && byName(listNow(FCL_VIEW_FOLDER, "root"), "verde (copia de seguridad).jpg"), "restaurada");
  uint64_t used0 = status().quota.usedBytes;
  op = flexCloudTrash(&phi); waitAny(op, FCE_OP_DONE, FCE_NONE, 400);
  op = flexCloudDeleteForever(&phi);
  e = waitAny(op, FCE_OP_DONE, FCE_NONE, 400);
  CHECK(e && e->ok, "borrada para siempre");
  pumpUntil([&]{ return status().quota.usedBytes < used0; }, 400);
  CHECK(status().quota.usedBytes + photo.size() <= used0 + 4096, "el espacio vuelve a la cuota del telefono");


  // ---- LO MULTIMEDIA QUE EL TELEFONO PREPARA PARA EL P4 (analiza, convierte y sirve /files/:id/playable)
  if(const char* mdir = getenv("PHONE_E2E_MEDIA_DIR")){
    printf("-- multimedia: el telefono analiza y convierte; el P4 lo abre con SU codigo --\n");
    std::string dir = mdir;
    const char* names[] = { "m_h264.mp4", "m_native.avi", "m_png.png", "m_prog.jpg", "m_big.jpg", "m_wav24.wav", "m_ima.wav", "m_aac.m4a", "m_cut.jpg", "m_h264.avi" };
    std::map<std::string, std::string> orig;
    for(const char* nm : names){
      std::string d = readReal(dir + "/" + nm);
      CHECK(!d.empty(), nm);
      orig[nm] = d;
      std::string lp = std::string("/Fotos/") + nm;
      putLocal(lp.c_str(), d);
      uint32_t u = flexCloudUpload(lp.c_str(), nm, "root", 0, 0);
      const FlexCloudEvent* ue = waitAny(u, FCE_UPLOAD_DONE, FCE_UPLOAD_FAILED, 40000);
      CHECK(ue != nullptr, "subido al telefono");
    }
    // El telefono trabaja en SU hilo (tiempo real): se espera a que ninguno quede en cola.
    std::vector<FclItem> ml;
    bool settledAll = false;
    for(int t = 0; t < 400 && !settledAll; t++){
      ml = listNow(FCL_VIEW_FOLDER, "root");
      settledAll = true; int seen = 0;
      for(const char* nm : names){
        const FclItem* x = byName(ml, nm);
        if(!x) { settledAll = false; continue; }
        seen++;
        if(x->playState == FCL_PS_UNKNOWN || x->playState == FCL_PS_PENDING || x->playState == FCL_PS_PREPARING) settledAll = false;
      }
      if(!settledAll) usleep(50000);
    }
    CHECK(settledAll, "el telefono dejo todos preparados (o dicho por que no)");
    auto get = [&](const char* nm) -> FclItem { const FclItem* x = byName(ml, nm); FclItem z; memset(&z, 0, sizeof(z)); if(x) z = *x; return z; };
    const char* why = nullptr;
    std::string got;
    std::string outDir = dir + "/out";
    mkdir(outDir.c_str(), 0755);

    // vídeo H.264: el P4 no lo decodifica; el telefono lo hizo AVI MJPEG y el P4 lo lee por /playable con rangos
    FclItem h264 = get("m_h264.mp4");
    CHECK(h264.playState == FCL_PS_READY && h264.kind == FCL_K_VIDEO, "MP4 H.264: el telefono lo dejo READY");
    CHECK(fclOpenAction(&h264, &why) == FCL_OPEN_STREAM, "el MP4 se abre en el reproductor de video (antes: 'solo AVI MJPEG')");
    CHECK(h264.size == orig["m_h264.mp4"].size() && h264.playSize != h264.size, "el original conserva su tamano; se lee la version");
    CHECK(h264.hasThumb, "miniatura generada por el telefono");
    CHECK(streamAll(h264, got) && got.size() == h264.playSize, "streaming de la version: todos los bytes");
    CHECK(got.compare(0, 4, "RIFF") == 0 && got.compare(8, 4, "AVI ") == 0, "es un AVI");
    CHECK(sha256hex(got) == h264.playSha, "con el SHA-256 que el telefono anuncia");
    writeReal(outDir + "/h264.avi", got);

    // AVI MJPEG ya compatible: NO se convierte; se lee el original
    FclItem nat = get("m_native.avi");
    CHECK(nat.playState == FCL_PS_NATIVE && fclOpenAction(&nat, &why) == FCL_OPEN_STREAM, "AVI MJPEG compatible: native, sin conversion");
    CHECK(streamAll(nat, got) && got == orig["m_native.avi"], "se reproduce el ORIGINAL byte a byte");
    CHECK(nat.hasThumb, "y tiene miniatura");
    writeReal(outDir + "/native.avi", got);

    // foto PNG y JPEG progresivo: el visor recibe un JPEG baseline ligero
    FclItem png = get("m_png.png");
    CHECK(png.playState == FCL_PS_READY && fclOpenAction(&png, &why) == FCL_OPEN_PHOTO, "PNG: READY y se abre como foto");
    std::string pv;
    bool gotPng = viewPhoto(png, pv);
    CHECK(gotPng && pv.size() > 3 && (uint8_t)pv[0] == 0xFF && (uint8_t)pv[1] == 0xD8 && sha256hex(pv) == png.playSha, "el visor recibe un JPEG, con la huella de la version (a la RAM)");
    writeReal(outDir + "/png.jpg", pv);
    FclItem prog = get("m_prog.jpg");
    CHECK(prog.playState == FCL_PS_READY, "JPEG progresivo: el telefono lo paso a baseline");
    pv.clear();
    CHECK(viewPhoto(prog, pv) && !pv.empty(), "la foto progresiva se ve");
    writeReal(outDir + "/prog.jpg", pv);
    FclItem big = get("m_big.jpg");
    CHECK(big.playState == FCL_PS_READY && big.playSize < big.size, "foto grande: vista previa ligera, mucho menos que el original");
    std::string pvBig;
    CHECK(viewPhoto(big, pvBig) && pvBig.size() == big.playSize, "el visor descarga SOLO la vista previa");

    // audio: WAV de 24 bits y AAC pasan a WAV IMA; el WAV IMA ya valia
    FclItem w24 = get("m_wav24.wav");
    CHECK(w24.playState == FCL_PS_READY && fclOpenAction(&w24, &why) == FCL_OPEN_AUDIO, "WAV 24 bits: READY y suena en Musica");
    CHECK(streamAll(w24, got) && got.size() == w24.playSize && got.compare(8, 4, "WAVE") == 0, "streaming del WAV preparado");
    writeReal(outDir + "/wav24.wav", got);
    FclItem aac = get("m_aac.m4a");
    CHECK(aac.playState == FCL_PS_READY && fclOpenAction(&aac, &why) == FCL_OPEN_AUDIO, "AAC: el telefono lo paso a WAV");
    CHECK(streamAll(aac, got), "streaming del audio convertido");
    writeReal(outDir + "/aac.wav", got);
    FclItem ima = get("m_ima.wav");
    CHECK(ima.playState == FCL_PS_NATIVE && fclOpenAction(&ima, &why) == FCL_OPEN_AUDIO, "WAV IMA: native");

    // lo roto o imposible se dice, aunque la extension diga lo contrario
    FclItem cut = get("m_cut.jpg");
    CHECK(cut.playState == FCL_PS_CORRUPT && fclOpenAction(&cut, &why) == FCL_OPEN_MENU && why && strstr(why, "cortado"), "JPEG cortado: no se intenta y se dice por que");
    FclItem fake = get("m_h264.avi");
    CHECK(fake.playState == FCL_PS_UNSUPPORTED && fclOpenAction(&fake, &why) == FCL_OPEN_MENU && why && strstr(why, "h264"), "'.avi' con H.264: no se reproduce y se dice");

    // el original sigue siendo descargable tal cual
    uint32_t dlo = flexCloudDownload(&h264, FCL_JF_TO_LIBRARY);
    e = waitAny(dlo, FCE_DOWNLOAD_DONE, FCE_DOWNLOAD_FAILED, 60000);
    CHECK(e && localFile(e->localPath) == orig["m_h264.mp4"], "'Descargar a Flex OS' trae el ORIGINAL, no la version");

    // se limpia: la cuota vuelve (versiones incluidas)
    ml = listNow(FCL_VIEW_FOLDER, "root");
    for(const char* nm : names){ FclItem x = get(nm); if(x.id[0]){ uint32_t d = flexCloudDeleteForever(&x); waitAny(d, FCE_OP_DONE, FCE_NONE, 400); } }
    CHECK(pumpUntil([&]{ return status().quota.usedBytes <= used0 + 4096; }, 2000), "la cuota vuelve al punto de partida: los originales Y sus versiones se borraron");
  }

  // ---- la cuota del telefono es real: lo que no cabe no entra
  uint64_t avail = status().quota.availableBytes;
  if(avail < 8u * 1024 * 1024){
    std::string big = pattern((size_t)avail + 4096, 6);
    putLocal("/grande.bin", big);
    uint32_t upBig = flexCloudUpload("/grande.bin", nullptr, "root", 0, 0);
    pumpUntil([&]{ drain(); return findEv(FCE_UPLOAD_FAILED, upBig) || findEv(FCE_UPLOAD_DONE, upBig); }, 40000);
    const FlexCloudEvent* f = findEv(FCE_UPLOAD_FAILED, upBig);
    CHECK(f && !strcmp(f->code, "quota_exceeded") && strstr(f->text, "tel"), "sin sitio: 'No queda espacio en Flex Cloud del telefono'");
  } else CHECK(false, "phone_e2e.sh deberia fijar una cuota pequena");

  // ---- el telefono reinicia sus sesiones: token rechazado -> sesion nueva, sola
  int sessionsBefore = 0;
  for(auto& r : gNetLog) if(r.url.find("/api/fs/session") != std::string::npos) sessionsBefore++;
  int forged = 1;
  gFault = [&](const NetRequest& rq, NetResponse& rs){
    if(forged && rq.url.find("/api/cloud/files") != std::string::npos){
      forged = 0;
      NetRequest r2 = rq; r2.headers["authorization"] = "Bearer 000000000000000000000000000000000000000000000000";
      rs = bridge(r2);
      return true;
    }
    return false;
  };
  std::vector<FclItem> again = listNow(FCL_VIEW_FOLDER, "root");
  int sessionsAfter = 0;
  for(auto& r : gNetLog) if(r.url.find("/api/fs/session") != std::string::npos) sessionsAfter++;
  FlexCloudListInfo li; flexCloudListInfo(&li);
  CHECK(li.state == FCL_LIST_READY && !again.empty(), "el 401 REAL del telefono se resuelve solo: la lista llega");
  CHECK(sessionsAfter == sessionsBefore + 1, "con UNA sesion nueva");
  gFault = nullptr;

  // ---- telefono fuera de la Wi-Fi: "Telefono desconectado" y vuelve solo
  gFault = [&](const NetRequest&, NetResponse& rs){ rs.status = HTTPC_ERROR_CONNECTION_REFUSED; rs.latencyMs = 4000; return true; };
  flexCloudRefresh();
  pumpUntil([]{ return status().net == FCN_UNAVAILABLE; }, 400);
  CHECK(status().net == FCN_UNAVAILABLE && strstr(status().netText, "desconectado"), "Telefono desconectado");
  gFault = nullptr;
  CHECK(pumpUntil([]{ return status().net == FCN_ONLINE; }, 8000), "vuelve solo");

  // ---- apagado del P4: la sesion no se guarda (se abre otra) y todo sigue
  reboot();
  flexCloudSetActive(true);
  CHECK(pumpUntil([]{ return status().net == FCN_ONLINE; }, 4000), "tras reiniciar el P4: sesion nueva y ONLINE");

  // ---- clave equivocada: el telefono real no la acepta y el P4 no insiste
  char wrong[65]; snprintf(wrong, sizeof(wrong), "%s", key); wrong[0] = wrong[0] == 'a' ? 'b' : 'a';
  writePhone(wrong);
  reboot();
  flexCloudSetActive(true);
  CHECK(pumpUntil([]{ return status().net == FCN_AUTH; }, 4000), "clave que no cuadra: 'vuelve a emparejar el telefono'");
  int sess = 0; for(auto& r : gNetLog) if(r.url.find("/api/fs/session") != std::string::npos) sess++;
  for(int i = 0; i < 3000; i++) step();
  int sess2 = 0; for(auto& r : gNetLog) if(r.url.find("/api/fs/session") != std::string::npos) sess2++;
  CHECK(sess2 == sess, "y no insiste");

  CHECK(gToPhoneHttps == 0 && gElsewhere == 0, "todo fue al telefono por la red local (nada a Internet)");
  bool keyLeak = false;
  for(auto& r : gNetLog){
    std::string all = r.url + r.body; for(auto& h : r.headers) all += h.second;
    if(all.find(key) != std::string::npos) keyLeak = true;
  }
  CHECK(!keyLeak, "la clave del emparejamiento no viajo NUNCA");
  printf("   %d peticiones reenviadas al telefono real\n", gForwarded);
  printf("=== %d comprobaciones, %d fallos ===\n", gChecks, gFails);
  return gFails ? 1 : 0;
}
