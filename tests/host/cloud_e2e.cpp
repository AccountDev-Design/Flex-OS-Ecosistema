// #############################################################
//  FLEX CLOUD · PUNTA A PUNTA: el gestor del P4 contra el servidor REAL
//  ------------------------------------------------------------
//  test_cloud.cpp usa un servidor simulado en C++. Este usa el DE VERDAD
//  (cloud/ de Flex-Developer-Studio, Node) arrancado en local por
//  cloud_e2e.sh: si el contrato de la API cambia en un lado y no en el otro,
//  falla aqui.
//
//  Las peticiones del modulo (https://...) se reenvian por un puente HTTP a
//  127.0.0.1; el registro de netstub sigue viendo la URL https y el TLS que
//  pidio el modulo, asi que la auditoria de seguridad es la misma.
//
//  La parte de Flex Account (el codigo de enlace firmado) la sigue haciendo
//  un servidor simulado, como en test_account.cpp: el servicio de cuentas
//  real no forma parte de este repositorio. La huella de la credencial se
//  registra en el servidor de Flex Cloud con su ruta de desarrollo, que es
//  lo que hace Flex Account al aprobar el codigo.
// #############################################################
#include "netstub.h"
#include "FS.h"
#include "FlexOS_Account.h"
#include "FlexOS_Cloud.h"
#include "FlexOS_CloudTLS.h"
#include "FlexOS_FS.h"
#include "FlexOS_MediaLib.h"
#include "FlexOS_JPEGEnc.h"
#include "pkgbuild.h"
#include "e2e_http.h"
#include <cJSON.h>
#include <string>
#include <vector>
#include <map>
#include <functional>

void fsStubReset();

static int gChecks = 0, gFails = 0;
#define CHECK(cond, msg) do { gChecks++; if(!(cond)){ gFails++; printf("  FALLO: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } else if(getenv("E2E_VERBOSE")) printf("  ok: %s\n", msg); } while(0)

static const std::string HOST = "https://flex-developer-studio.ralvarezsantos980.chatgpt.site";
static const std::string CODE_URL = HOST + "/api/devices/code";
static std::string gLocal;          // http://127.0.0.1:PUERTO
static int gPort = 0;

// ----------------------------------------------------------- puente HTTP
// (e2e_http.h: el mismo puente que phone_e2e.cpp)
static std::string lower(std::string s){ return e2eLower(s); }
static Raw httpRaw(const std::string& method, const std::string& path, const std::map<std::string, std::string>& hdrs, const std::string& body){
  return e2eHttp(gPort, method, path, hdrs, body);
}

// ------------------------------------------------- Flex Account simulado
static pkgb::Key gKey;
static const char* KEY_ID = "69b91cf7a9246cc2084dda73ccef77b2dc1a372b18fec19b49cf980efd68af69";
static std::string pendingHash;
static std::string b64url(const pkgb::Bytes& b){
  static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out; uint32_t acc = 0; int bits = 0;
  for(uint8_t c : b){ acc = (acc << 8) | c; bits += 8; while(bits >= 6){ bits -= 6; out += t[(acc >> bits) & 63]; } }
  if(bits) out += t[(acc << (6 - bits)) & 63];
  return out;
}
static std::string envelope(const std::string& payload){
  pkgb::Bytes p(payload.begin(), payload.end());
  pkgb::Bytes sig = gKey.sign(pkgb::sha256(p));
  return std::string("{\"schema\":2,\"algorithm\":\"ES256\",\"keyId\":\"") + KEY_ID + "\",\"payload\":\"" + b64url(p) +
         "\",\"signature\":\"" + b64url(sig) + "\"}";
}
static std::string sha256hex(const std::string& s){ return pkgb::hex(pkgb::sha256((const uint8_t*)s.data(), s.size())); }

// Fallos a proposito sobre el servidor real (cortes, red caida).
static std::function<bool(const NetRequest&, NetResponse&)> gFault;
static int gForwarded = 0;

static NetResponse bridge(const NetRequest& rq){
  NetResponse rs;
  if(gFault && gFault(rq, rs)) return rs;
  if(!rq.url.compare(0, CODE_URL.size(), CODE_URL)){
    if(rq.method == "POST"){
      cJSON* j = cJSON_Parse(rq.body.c_str()); cJSON* h = cJSON_GetObjectItem(j, "tokenHash");
      pendingHash = cJSON_IsString(h) ? h->valuestring : ""; cJSON_Delete(j);
      rs.status = 201;
      rs.body = envelope("{\"schema\":1,\"purpose\":\"flex-device-code\",\"code\":\"FLX7Q2\",\"tokenHash\":\"" + pendingHash +
                         "\",\"activationUrl\":\"" + HOST + "/activate?code=FLX7Q2\"}");
      return rs;
    }
    rs.body = envelope("{\"schema\":1,\"purpose\":\"flex-device-status\",\"state\":\"approved\",\"flexAddress\":\"ana.p4@flex\",\"displayName\":\"Ana\"}");
    return rs;
  }
  if(rq.url.compare(0, HOST.size(), HOST)){ rs.status = HTTPC_ERROR_CONNECTION_REFUSED; return rs; }
  gForwarded++;
  Raw r = httpRaw(rq.method, rq.url.substr(HOST.size()), rq.headers, rq.body);
  if(r.status < 0){ rs.status = HTTPC_ERROR_CONNECTION_REFUSED; return rs; }
  rs.status = r.status; rs.body = r.body; rs.headers = r.headers;
  return rs;
}

// ----------------------------------------------------------- utilidades
static void step(unsigned long ms = 20){ flexAccountTestStep(); flexCloudTestStep(); netstubAdvance(ms); }
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
static void putLocal(const char* path, const std::string& data){
  FlexFsStream* f = flexFsOpenWrite(path);
  if(f){ flexFsStreamWrite(f, data.data(), data.size()); flexFsStreamClose(f); }
}
static std::string localFile(const std::string& path){ auto it = gFs.find(path); return it == gFs.end() ? std::string("<no>") : std::string(it->second.data.begin(), it->second.data.end()); }
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

int main(){
  const char* url = getenv("FLEX_CLOUD_E2E_URL");
  if(!url || strncmp(url, "http://127.0.0.1:", 17)){ printf("cloud_e2e: falta FLEX_CLOUD_E2E_URL=http://127.0.0.1:PUERTO (usa cloud_e2e.sh)\n"); return 2; }
  gLocal = url; gPort = atoi(url + 17);
  printf("=== FlexOS · Flex Cloud del P4 contra el servidor REAL (%s) ===\n", url);
  gKey.generate();
  flexAccountTestSetKey(gKey.pub.data());
  netstubNvsWipe(); netstubReset(); fsStubReset();
  flexFsBegin(); flexFsMkdir("/System");
  gNetHandler = bridge; gNetWifi = true;
  flexAccountBegin(); flexCloudBegin();
  flexAccountTestPowerCycle(); flexCloudTestPowerCycle();

  // ---- enlace de la cuenta y alta de la credencial en Flex Cloud
  CHECK(flexAccountRequestCode("Flex OS Ultra"), "enlace pedido");
  flexAccountTestStep();
  char bearer[64] = "";
  CHECK(flexAccountCopyBearer(bearer, sizeof(bearer)), "credencial del dispositivo");
  Raw login = httpRaw("POST", "/api/cloud/dev/login", {{"Content-Type", "application/json"}, {"X-Flex-Cloud", "1"}},
                      "{\"flexAddress\":\"ana.p4@flex\",\"displayName\":\"Ana\"}");
  std::string cookie = login.headers["set-cookie"].substr(0, login.headers["set-cookie"].find(';'));
  CHECK(login.status == 200 && !cookie.empty(), "sesion web de desarrollo");
  Raw reg = httpRaw("POST", "/api/cloud/dev/devices", {{"Content-Type", "application/json"}, {"X-Flex-Cloud", "1"}, {"Cookie", cookie}},
                    "{\"tokenHash\":\"" + sha256hex(bearer) + "\",\"label\":\"Flex OS Ultra (e2e)\"}");
  CHECK(reg.status == 200, "huella de la credencial registrada en la cuenta (lo que hace Flex Account al aprobar)");
  memset(bearer, 0, sizeof(bearer));
  flexAccountRequestValidation();

  // ---- estado y cuota
  flexCloudSetActive(true);
  CHECK(pumpUntil([]{ return status().net == FCN_ONLINE && status().quotaValid; }, 4000), "ONLINE con la cuota del servidor");
  FlexCloudStatus s = status();
  CHECK(s.quota.totalBytes == 5ull * 1024 * 1024 * 1024, "5 GB del plan inicial (lo decide el servidor)");
  CHECK(!strcmp(s.address, "ana.p4@flex"), "la cuenta es la de Flex Account");

  // ---- carpeta Unicode y lista
  const char* folderName = "Viaje a Espa\xC3\xB1" "a \xE2\x80\x94 \xE6\x97\x85\xE8\xA1\x8C \xF0\x9F\x8C\x8D";
  uint32_t op = flexCloudMkdir("root", folderName);
  const FlexCloudEvent* e = waitAny(op, FCE_OP_DONE, FCE_NONE, 400);
  CHECK(e && e->ok, "carpeta creada");
  std::vector<FclItem> root = listNow(FCL_VIEW_FOLDER, "root");
  const FclItem* folder = byName(root, folderName);
  CHECK(folder && folder->isFolder, "la carpeta aparece con su nombre EXACTO");
  std::string folderId = folder ? folder->id : "";

  // ---- subida de 3 MB con apagado a mitad y Wi-Fi caido
  std::string data = pattern(3 * 1024 * 1024 + 4321, 77);
  putLocal("/Fotos/viaje.avi", data);
  uint32_t up = flexCloudUpload("/Fotos/viaje.avi", "Viaje \xC3\x91" "and\xC3\xBA.avi", folderId.c_str(), 0, FCL_JF_FREE_LOCAL);
  int puts = 0;
  gFault = [&](const NetRequest& rq, NetResponse&){ if(rq.method == "PUT" && rq.url.find("/parts/") != std::string::npos) puts++; return false; };
  CHECK(pumpUntil([&]{ return puts >= 5; }, 4000), "van 5 partes");
  flexAccountTestPowerCycle(); flexCloudTestPowerCycle(); gEv.clear();
  gNetWifi = false;
  for(int i = 0; i < 100; i++) step();
  gNetWifi = true;
  // Una parte se corta a mitad por la red: se reintenta.
  int cut = 1;
  gFault = [&](const NetRequest& rq, NetResponse& rs){
    if(rq.method == "PUT" && rq.url.find("/parts/") != std::string::npos){ puts++; if(cut){ cut = 0; rs.status = HTTPC_ERROR_CONNECTION_LOST; return true; } }
    return false;
  };
  e = waitAny(up, FCE_UPLOAD_DONE, FCE_UPLOAD_FAILED, 60000);
  CHECK(e != nullptr, "subida terminada contra el servidor real");
  CHECK(e && e->sha256 == sha256hex(data), "el servidor confirma la MISMA huella que el original");
  CHECK(e && (e->flags & FCL_JF_FREE_LOCAL), "y entonces (solo entonces) se ofrece liberar espacio");
  CHECK(puts <= 13 + 1 + 1, "tras el apagado solo se enviaron las partes que faltaban");
  CHECK(localFile("/Fotos/viaje.avi") == data, "el original intacto");
  std::string fileId = e ? e->fileId : "";
  gFault = nullptr;

  // ---- la lista la trae con sus metadatos
  std::vector<FclItem> inFolder = listNow(FCL_VIEW_FOLDER, folderId.c_str());
  const FclItem* it = byName(inFolder, "Viaje \xC3\x91" "and\xC3\xBA.avi");
  CHECK(it && it->size == data.size() && it->sha256 == sha256hex(data) && it->kind == FCL_K_VIDEO, "en la carpeta: tamano, huella y tipo");
  CHECK(it && it->fromDevice, "marcada como subida desde un Flex OS Ultra");
  FlexCloudListInfo li; flexCloudListInfo(&li);
  CHECK(li.nCrumbs >= 1 && !strcmp(li.crumbs[li.nCrumbs - 1].name, folderName), "migas con el nombre Unicode");
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
  CHECK(ranges.size() == 1 && ranges[0] == "bytes=1000000-", "reanudada con Range (If-Range con la huella)");
  gFault = nullptr;

  // ---- streaming: lecturas en cualquier punto, iguales al original
  CHECK(flexCloudStreamOpen(&item), "streaming abierto");
  uint8_t buf[8192];
  bool same = true; int spins = 0;
  const uint32_t offs[] = { 0, 1234567, (uint32_t)data.size() - 8192, 2u * 1024 * 1024 + 17, 65535 };
  for(uint32_t off : offs){
    flexCloudStreamSeek(off);
    int r;
    while((r = flexCloudStreamRead(off, buf, sizeof(buf))) < 0 && spins++ < 50000){ flexCloudTestStreamStep(); netstubAdvance(2); }
    if(r != (int)sizeof(buf) || memcmp(buf, data.data() + off, sizeof(buf))) same = false;
  }
  CHECK(same, "5 saltos: los bytes del streaming son los del archivo");
  flexCloudStreamClose();

  // ---- miniatura subida por el P4 y leida de vuelta
  std::vector<uint16_t> px(320 * 240, 0x07E0);
  FlexJeCfg cfg = { 320, 240, 85, FLEXJE_SUB_420, FLEXJE_IN_RGB565 };
  std::string jpg;
  flexJpegEncodeMem(&cfg, px.data(), 640, [](void* c, const uint8_t* d, size_t n){ ((std::string*)c)->append((const char*)d, n); return true; }, &jpg, nullptr, nullptr);
  putLocal(FML_DIR_THUMB "/42.jpg", jpg);
  std::string photo = pattern(150000, 5);
  putLocal("/Fotos/verde.jpg", photo);
  uint32_t up2 = flexCloudUpload("/Fotos/verde.jpg", "verde.jpg", "root", 42, 0);
  e = waitAny(up2, FCE_UPLOAD_DONE, FCE_UPLOAD_FAILED, 20000);
  CHECK(e != nullptr, "foto de la Galeria subida");
  std::string photoId = e ? e->fileId : "";
  for(auto& r : gNetLog) if(r.url.find("/thumbnail") != std::string::npos && getenv("E2E_VERBOSE")) printf("   %s %s (%zu bytes, ct=%s)\n", r.method.c_str(), r.url.c_str(), r.body.size(), r.headers.count("content-type") ? r.headers.at("content-type").c_str() : "-");
  flexCloudWantThumb(photoId.c_str());
  uint32_t g0 = flexCloudThumbGen();
  size_t nlog0 = gNetLog.size();
  pumpUntil([&]{ return flexCloudThumbGen() != g0; }, 400);
  if(getenv("E2E_VERBOSE")) for(size_t k = nlog0; k < gNetLog.size(); k++) printf("   tras pedirla: %s %s\n", gNetLog[k].method.c_str(), gNetLog[k].url.c_str());
  if(getenv("E2E_VERBOSE")) printf("   gen %u -> %u, net %d\n", g0, flexCloudThumbGen(), status().net);
  if(getenv("E2E_VERBOSE")){
    Raw t = httpRaw("GET", "/api/cloud/files/" + photoId + "/thumbnail", {{"Cookie", cookie}}, "");
    printf("   GET miniatura: %d, %zu bytes (jpeg subido: %zu)\n", t.status, t.body.size(), jpg.size());
  }
  uint16_t center = 0;
  bool drawn = flexCloudThumbDraw(photoId.c_str(), [](const uint16_t* p, int side, void* u){ *(uint16_t*)u = p[side * side / 2 + side / 2]; }, &center);
  CHECK(drawn && (center & 0x07E0) >= 0x0700 && (center & 0xF800) < 0x1000, "la miniatura (objeto aparte) vuelve y es verde");
  std::vector<FclItem> rootNow = listNow(FCL_VIEW_FOLDER, "root");
  const FclItem* ph = byName(rootNow, "verde.jpg");
  CHECK(ph && ph->hasThumb && ph->size == photo.size(), "el ORIGINAL sigue con su tamano; la miniatura es aparte");
  FclItem phi; memset(&phi, 0, sizeof(phi)); if(ph) phi = *ph;
  op = flexCloudFetchForView(&phi);
  e = waitAny(op, FCE_VIEW_READY, FCE_VIEW_FAILED, 4000);
  CHECK(e && localFile(e->localPath) == photo, "el visor recibe la foto ORIGINAL (sin recomprimir)");

  // ---- cancelar suelta la reserva en el servidor (se mira en el propio servidor)
  auto serverReserved = [&]() -> long long {
    Raw m = httpRaw("GET", "/api/cloud/me", {{"Cookie", cookie}}, "");
    cJSON* j = cJSON_Parse(m.body.c_str());
    cJSON* q = cJSON_GetObjectItem(j, "quota");
    cJSON* r = q ? cJSON_GetObjectItem(q, "reservedBytes") : nullptr;
    long long v = cJSON_IsNumber(r) ? (long long)r->valuedouble : -1;
    cJSON_Delete(j);
    return v;
  };
  putLocal("/big.bin", pattern(2 * 1024 * 1024, 9));
  puts = 0;
  // Las partes a partir de la 3 "no llegan": la subida queda a medias.
  gFault = [&](const NetRequest& rq, NetResponse& rs){
    if(rq.method == "PUT" && rq.url.find("/parts/") != std::string::npos){ puts++; if(puts > 2){ rs.status = HTTPC_ERROR_CONNECTION_LOST; return true; } }
    return false;
  };
  uint32_t up3 = flexCloudUpload("/big.bin", nullptr, "root", 0, 0);
  CHECK(pumpUntil([&]{ return puts >= 3; }, 4000), "subida a mitad");
  CHECK(serverReserved() == 2 * 1024 * 1024, "el servidor reservo el espacio de la subida");
  gFault = nullptr;
  flexCloudCancel(up3);
  int tries = 0;
  bool released = false;
  while(!released && tries++ < 200){ for(int k = 0; k < 50; k++) step(); released = serverReserved() == 0; }
  CHECK(released, "cancelada: el servidor solto la reserva (DELETE /uploads/:id)");

  // ---- papelera, restaurar, borrar para siempre (la cuota vuelve)
  uint64_t used0 = status().quota.usedBytes;
  op = flexCloudTrash(&phi);
  CHECK(waitAny(op, FCE_OP_DONE, FCE_NONE, 400) && findEv(FCE_OP_DONE, op)->ok, "a la papelera");
  std::vector<FclItem> trash = listNow(FCL_VIEW_TRASH, nullptr);
  CHECK(byName(trash, "verde.jpg") != nullptr, "aparece en la papelera");
  op = flexCloudRestore(&phi);
  CHECK(waitAny(op, FCE_OP_DONE, FCE_NONE, 400) && findEv(FCE_OP_DONE, op)->ok, "restaurada");
  op = flexCloudTrash(&phi); waitAny(op, FCE_OP_DONE, FCE_NONE, 400);
  op = flexCloudDeleteForever(&phi);
  CHECK(waitAny(op, FCE_OP_DONE, FCE_NONE, 400) && findEv(FCE_OP_DONE, op)->ok, "borrada para siempre");
  CHECK(pumpUntil([&]{ return status().quota.usedBytes == used0 - photo.size(); }, 4000), "la cuota recupera su tamano");

  // ---- un id de otra cuenta no sirve (el servidor decide el propietario)
  Raw other = httpRaw("POST", "/api/cloud/dev/login", {{"Content-Type", "application/json"}, {"X-Flex-Cloud", "1"}}, "{\"flexAddress\":\"otra@flex\"}");
  std::string cookie2 = other.headers["set-cookie"].substr(0, other.headers["set-cookie"].find(';'));
  Raw peek = httpRaw("GET", "/api/cloud/download/" + fileId, {{"Cookie", cookie2}}, "");
  CHECK(peek.status == 404, "otra cuenta no puede bajar el archivo del P4 conociendo su id");

  // ---- auditoria de todo lo que salio por la red
  bool tls = true, leak = false, accountParam = false;
  char b2[64] = ""; flexAccountCopyBearer(b2, sizeof(b2));
  for(auto& r : gNetLog){
    if(r.headers.count("authorization") && (!r.https || r.tlsInsecure || r.tlsCa != flexCloudRootCA())) tls = false;
    if(r.url.find(b2) != std::string::npos || r.body.find(b2) != std::string::npos) leak = true;
    std::string l = lower(r.url + r.body);
    if(l.find("accountid") != std::string::npos || l.find("account_id") != std::string::npos) accountParam = true;
  }
  CHECK(tls, "toda peticion con credencial: HTTPS con la CA de Flex Cloud");
  CHECK(!leak, "la credencial nunca en URL ni cuerpo");
  CHECK(!accountParam, "el P4 nunca envia un id de cuenta");
  printf("   %d peticiones reenviadas al servidor real\n", gForwarded);
  gKey.free_();
  printf("=== %d comprobaciones, %d fallos ===\n", gChecks, gFails);
  return gFails ? 1 : 0;
}
