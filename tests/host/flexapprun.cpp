// #############################################################
//  flexapprun  ·  ejecuta un .flxb con el GESTOR DEL FIRMWARE
//  ------------------------------------------------------------
//  Carga un bytecode flex-app-v1 y lo hace pasar por el mismo
//  FlexAppManager que corre en el ESP32-P4, con un anfitrion de
//  mentira que anota lo que la app pide (dibujo, almacenamiento,
//  orientacion...) y un reloj virtual.
//
//  Sirve para probar una app SIN placa, y es lo que usa
//  sdk/test/sdk.test.js para comprobar que el ejemplo del SDK hace de
//  verdad lo que dice: contar, cronometrar, pausar y guardar.
//
//  Uso:
//    flexapprun <app.flxb> [--perms <mascara>] [--nogrant] [--json]
//
//  Guion fijo: arranque, 3 ticks, toque en el boton, 3 ticks, un
//  segundo de reloj, pausa, reanudacion, cierre.
// #############################################################
#include "pkgbuild.h"
#include "../../FlexOS_Ultra/FlexOS_AppHost.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <map>
#include <vector>

// ---- Anfitrion de mentira ----------------------------------------------
static uint64_t gNowUs = 1000000;
static int gDraw = 0, gPresent = 0, gNotify = 0;
static bool gLandscape = false, gExclusive = false;
static std::map<std::string, std::string> gFiles;
static std::string gNav, gLastLog;

static uint64_t hNow(void*){ return gNowUs; }
static void hClear(void*, uint16_t){ gDraw++; }
static void hFillRect(void*, int, int, int, int, uint16_t, int){ gDraw++; }
static void hRect(void*, int, int, int, int, uint16_t){ gDraw++; }
static void hLine(void*, int, int, int, int, uint16_t){ gDraw++; }
static void hRound(void*, int, int, int, int, int, uint16_t){ gDraw++; }
static void hPixel(void*, int, int, uint16_t){ gDraw++; }
static void hText(void*, const char*, int, int, int, uint16_t){ gDraw++; }
static int  hTextW(void*, const char* t, int size){ return (int)strlen(t ? t : "") * 6 * size; }
static void hClip(void*, int, int, int, int){}
static void hBlit(void*, const uint8_t*, int, int, int, int, int, int, int, int){ gDraw++; }
static void hPresent(void*){ gPresent++; }
static int  hCanvasW(void*){ return gLandscape ? 800 : 480; }
static int  hCanvasH(void*){ return gLandscape ? 480 : 648; }
static void hWidget(void*, int, const char*, int, int, int, int, int){ gDraw++; }
static void hNav(void*, const char* t){ gNav = t ? t : ""; }
static void hLog(void*, const char* t){ gLastLog = t ? t : ""; }
static bool hNotify(void*, const char*, const char*){ gNotify++; return true; }
static int  hOsInfo(void*, int which, char* out, int cap){
  const char* s = which == 0 ? "1.0.0" : "Flex OS Ultra (host)";
  int n = (int)strlen(s); if(n > cap) n = cap;
  memcpy(out, s, (size_t)n); return n;
}
static uint32_t hCaps(void*){
  return FLEXAPP_CAP_LANDSCAPE | FLEXAPP_CAP_STORAGE | FLEXAPP_CAP_NOTIFICATIONS | FLEXAPP_CAP_PSRAM;
}
static int hScreenW(void*){ return 480; }
static int hScreenH(void*){ return 800; }
static bool hLand(void*, bool on){ gLandscape = on; return true; }
static bool hExcl(void*, bool on){ gExclusive = on; return true; }
static int hWrite(void*, const char* name, const uint8_t* d, uint32_t n){
  gFiles[name] = std::string((const char*)d, n); return 1;
}
static int hRead(void*, const char* name, uint8_t* out, uint32_t cap){
  auto it = gFiles.find(name);
  if(it == gFiles.end()) return -1;
  uint32_t n = (uint32_t)it->second.size();
  if(n > cap) n = cap;
  memcpy(out, it->second.data(), n);
  return (int)n;
}
static int hSize(void*, const char* name){
  auto it = gFiles.find(name);
  return it == gFiles.end() ? -1 : (int)it->second.size();
}
static int hDel(void*, const char* name){ return gFiles.erase(name) ? 1 : 0; }
static uint32_t hUsed(void*){
  uint32_t n = 0;
  for(const auto& kv : gFiles) n += (uint32_t)kv.second.size();
  return n;
}
static int hTouch(void*, FlexAppPoint*, int){ return -1; }     // el host no tiene panel
static int32_t hPerf(void*, int32_t f){ return f == FLEXAPP_PERF_LOOP_RATE ? 240 : FLEXAPP_NO_VALUE; }
static int32_t hPsram(void*, int which){ return which == 1 ? 32768 : 16000; }
static int32_t hReserve(void*, uint32_t){ return 0; }
static bool hRelease(void*, int32_t){ return false; }
static bool hPoke(void*, int32_t, uint32_t, int32_t){ return false; }
static bool hPeek(void*, int32_t, uint32_t, int32_t*){ return false; }
static int32_t hTemp(void*){ return FLEXAPP_NO_VALUE; }        // sin sensor real
static int32_t hStress(void*, int32_t, uint32_t us, uint64_t* acc){ gNowUs += us; if(acc) *acc += 1; return 1; }

int main(int argc, char** argv){
  const char* path = nullptr;
  uint32_t perms = FLEXPERM_SYS_STORAGE_APP;
  bool withGrant = true, asJson = false;
  for(int i = 1; i < argc; i++){
    if(!strcmp(argv[i], "--json")) asJson = true;
    else if(!strcmp(argv[i], "--nogrant")) withGrant = false;
    else if(!strcmp(argv[i], "--perms") && i + 1 < argc) perms = (uint32_t)strtoul(argv[++i], nullptr, 0);
    else path = argv[i];
  }
  if(!path){ std::fprintf(stderr, "uso: flexapprun <app.flxb> [--perms N] [--nogrant] [--json]\n"); return 2; }

  FILE* f = fopen(path, "rb");
  if(!f){ std::fprintf(stderr, "no se puede abrir %s\n", path); return 2; }
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> image((size_t)(n > 0 ? n : 0));
  if(n <= 0 || fread(image.data(), 1, (size_t)n, f) != (size_t)n){ fclose(f); return 2; }
  fclose(f);

  pkgb::Key store, dev;
  store.generate(); dev.generate();

  const char* id = "com.flexos.ejemplo.contador";
  const char* ver = "1.0.0";
  uint32_t code = 1;
  uint8_t pkgHash[32];
  for(int i = 0; i < 32; i++) pkgHash[i] = (uint8_t)(i * 3 + 11);

  pkgb::Bytes grant;
  if(withGrant && perms){
    pkgb::GrantSpec g;
    g.packageId = id; g.versionName = ver; g.versionCode = code;
    g.packageSha256 = pkgb::Bytes(pkgHash, pkgHash + 32);
    g.developerFingerprint = dev.fingerprint();
    g.mask = perms;
    grant = pkgb::buildGrant(g, store);
  }

  static uint8_t     mem[256 * 1024];
  static int32_t     globals[FLEXVM_MAX_GLOBALS];
  static int32_t     stack[FLEXVM_MAX_STACK];
  static int32_t     locals[FLEXVM_MAX_FRAMESLOTS];
  static FlexVmFrame frames[FLEXVM_MAX_CALLDEPTH];
  static uint8_t     scratch[FLEXVM_MAX_CODE / 8 + 64];

  FlexAppHostApi api;
  memset(&api, 0, sizeof(api));
  api.nowUs = hNow;
  api.gfxClear = hClear; api.gfxFillRect = hFillRect; api.gfxRect = hRect; api.gfxLine = hLine;
  api.gfxRoundRect = hRound; api.gfxPixel = hPixel; api.gfxText = hText; api.gfxTextWidth = hTextW;
  api.gfxClip = hClip; api.gfxBlit = hBlit; api.gfxPresent = hPresent;
  api.gfxCanvasW = hCanvasW; api.gfxCanvasH = hCanvasH;
  api.uiWidget = hWidget; api.uiNavTitle = hNav;
  api.logLine = hLog; api.notify = hNotify; api.osInfo = hOsInfo; api.caps = hCaps;
  api.screenW = hScreenW; api.screenH = hScreenH;
  api.setLandscape = hLand; api.setExclusive = hExcl;
  api.storeWrite = hWrite; api.storeRead = hRead; api.storeSize = hSize;
  api.storeDelete = hDel; api.storeUsed = hUsed;
  api.touchPoints = hTouch;
  api.perfMetric = hPerf; api.psramKb = hPsram; api.psramReserve = hReserve;
  api.psramRelease = hRelease; api.psramPoke = hPoke; api.psramPeek = hPeek;
  api.tempMilliC = hTemp; api.cpuStressSlice = hStress;

  FlexAppManager m;
  flexAppInit(&m, &api, nullptr);

  FlexAppLaunch L;
  memset(&L, 0, sizeof(L));
  L.packageId = id; L.versionName = ver; L.versionCode = code;
  memcpy(L.packageSha256, pkgHash, 32);
  pkgb::Bytes fp = pkgb::sha256(dev.pub);
  memcpy(L.developerKeySha256, fp.data(), 32);
  L.manifestPermissions = perms;
  L.nowEpoch = 1780000000ull;
  L.image = image.data(); L.imageLen = (uint32_t)image.size();
  L.grant = grant.empty() ? nullptr : grant.data();
  L.grantLen = (uint32_t)grant.size();
  L.trustedPub = store.pub.data();
  L.mem = mem; L.memBytes = sizeof(mem);
  L.globals = globals; L.globalCount = FLEXVM_MAX_GLOBALS;
  L.stack = stack; L.stackSlots = FLEXVM_MAX_STACK;
  L.locals = locals; L.frameSlots = FLEXVM_MAX_FRAMESLOTS;
  L.frames = frames; L.callDepth = FLEXVM_MAX_CALLDEPTH;
  L.scratch = scratch; L.scratchLen = sizeof(scratch);
  L.storageQuota = 16 * 1024;

  bool started = flexAppStart(&m, &L);
  int32_t g0 = 0, g1 = 0;
  int drawsAfterTap = 0;
  // Se anotan ANTES de parar: al parar, los permisos dejan de existir a
  // proposito, y el informe tiene que decir lo que la app tuvo mientras corria.
  uint32_t granted = m.granted;
  uint8_t grantStatus = m.grantStatus;

  if(started){
    for(int i = 0; i < 3; i++){ gNowUs += 16000; flexAppTick(&m); }
    int before = gDraw;
    // Toque en el boton "Sumar 1" (y = 350 cae dentro de 320..384).
    flexAppPostEvent(&m, FLEXAPP_EV_TOUCH_TAP, 100, 350);
    gNowUs += 16000; flexAppTick(&m);
    drawsAfterTap = gDraw - before;
    for(int i = 0; i < 3; i++){ gNowUs += 16000; flexAppTick(&m); }
    // Un segundo de reloj: el temporizador de la app tiene que dispararse.
    gNowUs += 1100000;
    flexAppTick(&m);
    flexAppTick(&m);
    flexAppSuspend(&m);
    flexAppTick(&m);            // suspendida: no ejecuta
    flexAppResume(&m);
    gNowUs += 16000; flexAppTick(&m);
    gNowUs += 16000; flexAppTick(&m);
    g0 = m.vm.globals[0];
    g1 = m.vm.globals[1];
    flexAppStop(&m, FLEXAPP_STOP_USER);
  }

  std::string saved;
  for(const auto& kv : gFiles) saved += (saved.empty() ? "" : ",") + kv.first;

  if(asJson){
    std::printf("{\"started\":%s,\"state\":\"%s\",\"reason\":\"%s\",\"granted\":%u,"
                "\"grantStatus\":%u,\"ticks\":%u,\"events\":%u,\"draws\":%d,\"presents\":%d,"
                "\"drawsAfterTap\":%d,\"navTitle\":\"%s\",\"files\":\"%s\",\"fileBytes\":%u,"
                "\"global0\":%d,\"global1\":%d,\"instrTotal\":%llu,\"drawDropped\":%u}\n",
                started ? "true" : "false", flexAppStateName(m.state), flexAppReason(&m),
                (unsigned)granted, (unsigned)grantStatus, (unsigned)m.ticks, (unsigned)m.events,
                gDraw, gPresent, drawsAfterTap, gNav.c_str(), saved.c_str(), (unsigned)hUsed(nullptr),
                (int)g0, (int)g1, (unsigned long long)m.instrTotal, (unsigned)m.drawDropped);
  } else {
    std::printf("arranque      %s (%s)\n", started ? "si" : "NO", flexAppReason(&m));
    std::printf("permisos      0x%08X (grant: %s)\n", (unsigned)granted, flexGrantStatusText(grantStatus));
    std::printf("titulo        %s\n", gNav.c_str());
    std::printf("ticks         %u   eventos %u\n", (unsigned)m.ticks, (unsigned)m.events);
    std::printf("dibujo        %d comandos, %d cuadros publicados (%u descartados)\n",
                gDraw, gPresent, (unsigned)m.drawDropped);
    std::printf("instrucciones %llu\n", (unsigned long long)m.instrTotal);
    std::printf("estado app    contador=%d  segundos=%d\n", (int)g0, (int)g1);
    std::printf("archivos      %s (%u bytes)\n", saved.empty() ? "(ninguno)" : saved.c_str(),
                (unsigned)hUsed(nullptr));
    std::printf("estado final  %s\n", flexAppStateName(m.state));
  }
  store.free_(); dev.free_();
  return started ? 0 : 1;
}
