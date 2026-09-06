// #############################################################
//  test_apphost.cpp  ·  gestor de aplicaciones flex-app-v1
// #############################################################
//
//  Aqui se comprueba lo que hace que Flex OS no pierda el control:
//    1) Estados y transiciones del ciclo de vida.
//    2) PERMISO POR LLAMADA: sin grant valido, un servicio
//       privilegiado devuelve "no disponible" TODAS las veces.
//    3) Presupuesto: agotarlo cede; insistir detiene la app por
//       seguridad, con motivo, y el sistema sigue vivo.
//    4) Cancelacion en cada fase: arranque, ejecucion, suspension y
//       cierre. Siempre se sueltan los recursos.
//    5) Orientacion: se restaura SIEMPRE, por cualquier salida.
//    6) Almacenamiento aislado entre dos apps, con cuota y nombres
//       saneados.
//    7) Sin PSRAM y sin sensor: se dice "no disponible", no se
//       inventa un numero.
//    8) 25 aperturas y cierres sin fugas detectables.
//    9) Vectores dorados de la tabla de syscalls (el SDK escribe
//       estos mismos numeros).

#include "vmimage.h"
#include "pkgbuild.h"
#include "../../FlexOS_AppHost.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <map>

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

// =============================================================
//  Anfitrion simulado
// =============================================================
struct HostState {
  uint64_t nowUs = 1000000;
  uint32_t caps = FLEXAPP_CAP_LANDSCAPE | FLEXAPP_CAP_STORAGE | FLEXAPP_CAP_NOTIFICATIONS;
  bool landscape = false, exclusive = false;
  int landscapeCalls = 0, exclusiveCalls = 0;
  int drawCalls = 0, presentCalls = 0, notifyCalls = 0;
  int reserves = 0, releases = 0;
  int32_t nextHandle = 1;
  std::map<int32_t, uint32_t> live;             // handle -> KB
  bool psramAvailable = true;
  uint32_t psramFreeKb = 16000;
  bool tempSensor = false;
  int32_t tempValue = 41250;
  int touchCount = -1;                          // <0 = el panel no da dato fiable
  FlexAppPoint touch[FLEXAPP_TOUCH_POINTS];
  std::string appDir;                           // "carpeta privada" de la app activa
  std::map<std::string, std::string> files;     // "dir/nombre" -> contenido
  std::string lastText, lastNav, lastLog;
  int stressSlices = 0;
  bool stressAvailable = true;
  // Contadores de recursos, para la prueba de fugas.
  int openResources() const { return (int)live.size() + (landscape ? 1 : 0) + (exclusive ? 1 : 0); }
};
static HostState H;

static uint64_t hNow(void*){ return H.nowUs; }
static void hClear(void*, uint16_t){ H.drawCalls++; }
static void hFillRect(void*, int, int, int, int, uint16_t, int){ H.drawCalls++; }
static void hRect(void*, int, int, int, int, uint16_t){ H.drawCalls++; }
static void hLine(void*, int, int, int, int, uint16_t){ H.drawCalls++; }
static void hRound(void*, int, int, int, int, int, uint16_t){ H.drawCalls++; }
static void hPixel(void*, int, int, uint16_t){ H.drawCalls++; }
static void hText(void*, const char* t, int, int, int, uint16_t){ H.drawCalls++; H.lastText = t ? t : ""; }
static int  hTextW(void*, const char* t, int size){ return (int)strlen(t ? t : "") * 6 * size; }
static void hClip(void*, int, int, int, int){}
static void hBlit(void*, const uint8_t*, int, int, int, int, int, int, int, int){ H.drawCalls++; }
static void hPresent(void*){ H.presentCalls++; }
static int  hCanvasW(void*){ return H.landscape ? 800 : 480; }
static int  hCanvasH(void*){ return H.landscape ? 480 : 800; }
static void hWidget(void*, int, const char* t, int, int, int, int, int){ H.drawCalls++; H.lastText = t ? t : ""; }
static void hNavTitle(void*, const char* t){ H.lastNav = t ? t : ""; }
static void hLog(void*, const char* t){ H.lastLog = t ? t : ""; }
static bool hNotify(void*, const char*, const char*){ H.notifyCalls++; return true; }
static int  hOsInfo(void*, int which, char* out, int cap){
  const char* s = which == 0 ? "1.0.0" : "Flex OS Ultra P4";
  int n = (int)strlen(s);
  if(n > cap) n = cap;
  memcpy(out, s, n);
  return n;
}
static uint32_t hCaps(void*){ return H.caps; }
static int hScreenW(void*){ return 480; }
static int hScreenH(void*){ return 800; }
static bool hSetLandscape(void*, bool on){
  H.landscapeCalls++;
  if(!(H.caps & FLEXAPP_CAP_LANDSCAPE)) return false;
  H.landscape = on;
  return true;
}
static bool hSetExclusive(void*, bool on){ H.exclusiveCalls++; H.exclusive = on; return true; }

static int hStoreWrite(void*, const char* name, const uint8_t* d, uint32_t n){
  H.files[H.appDir + "/" + name] = std::string((const char*)d, n);
  return 1;
}
static int hStoreRead(void*, const char* name, uint8_t* out, uint32_t cap){
  auto it = H.files.find(H.appDir + "/" + name);
  if(it == H.files.end()) return -1;
  uint32_t n = (uint32_t)it->second.size();
  if(n > cap) n = cap;
  memcpy(out, it->second.data(), n);
  return (int)n;
}
static int hStoreSize(void*, const char* name){
  auto it = H.files.find(H.appDir + "/" + name);
  return it == H.files.end() ? -1 : (int)it->second.size();
}
static int hStoreDelete(void*, const char* name){ return H.files.erase(H.appDir + "/" + name) ? 1 : 0; }
static uint32_t hStoreUsed(void*){
  uint32_t n = 0;
  for(const auto& kv : H.files)
    if(kv.first.compare(0, H.appDir.size() + 1, H.appDir + "/") == 0) n += (uint32_t)kv.second.size();
  return n;
}
static int hTouchPoints(void*, FlexAppPoint* out, int maxn){
  if(H.touchCount < 0) return -1;
  int n = H.touchCount < maxn ? H.touchCount : maxn;
  for(int i = 0; i < n; i++) out[i] = H.touch[i];
  return n;
}
static int32_t hPerf(void*, int32_t field){
  switch(field){
    case FLEXAPP_PERF_LOOP_RATE: return 240;
    case FLEXAPP_PERF_CPU_MHZ: return 360;
    default: return FLEXAPP_NO_VALUE;
  }
}
static int32_t hPsramKb(void*, int which){
  if(!H.psramAvailable) return FLEXAPP_NO_VALUE;
  return which == 1 ? 32768 : (int32_t)H.psramFreeKb;
}
static int32_t hReserve(void*, uint32_t kb){
  if(!H.psramAvailable || kb > H.psramFreeKb) return 0;
  H.reserves++;
  H.psramFreeKb -= kb;
  int32_t h = H.nextHandle++;
  H.live[h] = kb;
  return h;
}
static bool hRelease(void*, int32_t h){
  auto it = H.live.find(h);
  if(it == H.live.end()) return false;
  H.psramFreeKb += it->second;
  H.live.erase(it);
  H.releases++;
  return true;
}
static bool hPoke(void*, int32_t h, uint32_t off, int32_t){ return H.live.count(h) && off < H.live[h] * 256; }
static bool hPeek(void*, int32_t h, uint32_t off, int32_t* out){
  if(!H.live.count(h) || off >= H.live[h] * 256) return false;
  *out = 0;
  return true;
}
static int32_t hTemp(void*){ return H.tempSensor ? H.tempValue : FLEXAPP_NO_VALUE; }
static int32_t hStress(void*, int32_t, uint32_t budgetUs, uint64_t* acc){
  if(!H.stressAvailable) return -1;
  H.stressSlices++;
  H.nowUs += budgetUs;          // la rodaja consume tiempo REAL
  *acc += 1000;
  return 1;
}

static FlexAppHostApi api(){
  FlexAppHostApi a;
  memset(&a, 0, sizeof(a));
  a.nowUs = hNow;
  a.gfxClear = hClear; a.gfxFillRect = hFillRect; a.gfxRect = hRect; a.gfxLine = hLine;
  a.gfxRoundRect = hRound; a.gfxPixel = hPixel; a.gfxText = hText; a.gfxTextWidth = hTextW;
  a.gfxClip = hClip; a.gfxBlit = hBlit; a.gfxPresent = hPresent;
  a.gfxCanvasW = hCanvasW; a.gfxCanvasH = hCanvasH;
  a.uiWidget = hWidget; a.uiNavTitle = hNavTitle;
  a.logLine = hLog; a.notify = hNotify; a.osInfo = hOsInfo; a.caps = hCaps;
  a.screenW = hScreenW; a.screenH = hScreenH;
  a.setLandscape = hSetLandscape; a.setExclusive = hSetExclusive;
  a.storeWrite = hStoreWrite; a.storeRead = hStoreRead; a.storeSize = hStoreSize;
  a.storeDelete = hStoreDelete; a.storeUsed = hStoreUsed;
  a.touchPoints = hTouchPoints;
  a.perfMetric = hPerf; a.psramKb = hPsramKb; a.psramReserve = hReserve;
  a.psramRelease = hRelease; a.psramPoke = hPoke; a.psramPeek = hPeek;
  a.tempMilliC = hTemp; a.cpuStressSlice = hStress;
  return a;
}

// =============================================================
//  Regiones y lanzamiento
// =============================================================
static pkgb::Key gStore, gDev;

struct Launcher {
  std::vector<uint8_t> image;
  uint8_t mem[8192];
  int32_t globals[FLEXVM_MAX_GLOBALS];
  int32_t stack[FLEXVM_MAX_STACK];
  int32_t locals[FLEXVM_MAX_FRAMESLOTS];
  FlexVmFrame frames[FLEXVM_MAX_CALLDEPTH];
  uint8_t scratch[8192];
  pkgb::Bytes grant;
  uint8_t pkgHash[32];

  FlexAppLaunch make(const char* id, const char* ver, uint32_t code,
                     uint32_t manifestPerms, bool withGrant){
    for(int i = 0; i < 32; i++) pkgHash[i] = (uint8_t)(i + code);
    FlexAppLaunch L;
    memset(&L, 0, sizeof(L));
    L.packageId = id; L.versionName = ver; L.versionCode = code;
    memcpy(L.packageSha256, pkgHash, 32);
    pkgb::Bytes fp = pkgb::sha256(gDev.pub);
    memcpy(L.developerKeySha256, fp.data(), 32);
    L.manifestPermissions = manifestPerms;
    L.nowEpoch = 1780000000ull;
    L.image = image.data(); L.imageLen = (uint32_t)image.size();
    L.trustedPub = gStore.pub.data();
    if(withGrant){
      pkgb::GrantSpec g;
      g.packageId = id; g.versionName = ver; g.versionCode = code;
      g.packageSha256 = pkgb::Bytes(pkgHash, pkgHash + 32);
      g.developerFingerprint = gDev.fingerprint();
      g.mask = manifestPerms;
      grant = pkgb::buildGrant(g, gStore);
      L.grant = grant.data(); L.grantLen = (uint32_t)grant.size();
    }
    L.mem = mem; L.memBytes = sizeof(mem);
    L.globals = globals; L.globalCount = FLEXVM_MAX_GLOBALS;
    L.stack = stack; L.stackSlots = FLEXVM_MAX_STACK;
    L.locals = locals; L.frameSlots = FLEXVM_MAX_FRAMESLOTS;
    L.frames = frames; L.callDepth = FLEXVM_MAX_CALLDEPTH;
    L.scratch = scratch; L.scratchLen = sizeof(scratch);
    L.storageQuota = 4096;
    return L;
  }
};

// Imagen: onStart llama a `sys` con los argumentos dados y guarda el
// resultado en la global 0; onTick no hace nada; onEvent guarda el tipo.
static std::vector<uint8_t> imageCallSys(uint16_t sysId, const std::vector<int32_t>& args){
  Image im;
  im.globals = 4; im.stack = 32; im.frames = 32; im.calldepth = 8; im.mem = 512;
  im.funcs.push_back({0, 0, 0});          // onStart
  im.h[FLEXVM_H_START] = 0;
  for(int32_t a : args) im.i32(a);
  im.u16(OP_SYS, sysId);
  im.u16(OP_ST_GLOBAL, 0);
  im.b(OP_RET);
  uint32_t tickOff = (uint32_t)im.here();
  im.funcs.push_back({tickOff, 1, 0});    // onTick
  im.h[FLEXVM_H_TICK] = 1;
  im.b(OP_RET);
  uint32_t evOff = (uint32_t)im.here();
  im.funcs.push_back({evOff, 3, 0});      // onEvent
  im.h[FLEXVM_H_EVENT] = 2;
  im.u8(OP_LD_LOCAL, 0); im.u16(OP_ST_GLOBAL, 1);
  im.u8(OP_LD_LOCAL, 1); im.u16(OP_ST_GLOBAL, 2);
  im.b(OP_RET);
  uint32_t stopOff = (uint32_t)im.here();
  im.funcs.push_back({stopOff, 1, 0});    // onStop
  im.h[FLEXVM_H_STOP] = 3;
  im.u8(OP_LD_LOCAL, 0); im.u16(OP_ST_GLOBAL, 3);
  im.b(OP_RET);
  return im.build();
}

// =============================================================
static void testSyscallTable(){
  std::printf("-- vectores dorados de la tabla de syscalls --\n");
  struct { uint16_t id; uint8_t argc; const char* name; uint32_t perm; } golden[] = {
    { 0x0002, 5, "gfx.fill_rect",   0 },
    { 0x0007, 6, "gfx.text",        0 },
    { 0x000D, 7, "gfx.blit_scaled", 0 },
    { 0x0014, 0, "gfx.present",     0 },
    { 0x0024, 7, "ui.button",       0 },
    { 0x0030, 0, "time.now_ms",     0 },
    { 0x0033, 3, "time.timer_set",  0 },
    { 0x0040, 4, "store.write",     FLEXPERM_SYS_STORAGE_APP },
    { 0x0056, 4, "sys.notify",      0 },
    { 0x0060, 1, "display.landscape", FLEXPERM_SYS_DISPLAY_LANDSCAPE },
    { 0x0070, 1, "perf.metric",     FLEXPERM_SYS_PERF_METRICS },
    { 0x0074, 1, "mem.reserve",     FLEXPERM_SYS_PSRAM_MEASURE },
    { 0x0078, 0, "temp.milli_c",    FLEXPERM_SYS_TEMPERATURE_READ },
    { 0x0079, 2, "cpu.stress_begin", FLEXPERM_SYS_CPU_STRESS },
    { 0x007E, 1, "bench.begin",     FLEXPERM_SYS_BENCHMARK_RUN }
  };
  for(const auto& g : golden){
    CHECK(flexAppSyscallArity(g.id) == g.argc, "aridad de %s", g.name);
    CHECK(!strcmp(flexAppSyscallName(g.id), g.name), "nombre de 0x%04X", g.id);
    CHECK(flexAppSyscallPermission(g.id) == g.perm, "permiso de %s", g.name);
  }
  CHECK(flexAppSyscallArity(0xFFFF) == 0xFF, "una syscall inexistente no tiene aridad");
  CHECK(flexAppSyscallPermission(0x0001) == 0, "dibujar en el propio lienzo no pide permiso");
}

// =============================================================
static void testLifecycle(){
  std::printf("-- ciclo de vida y estados --\n");
  H = HostState(); H.appDir = "app1";
  FlexAppHostApi a = api();
  FlexAppManager m;
  flexAppInit(&m, &a, nullptr);
  CHECK(flexAppState(&m) == FLEXAPP_ST_INSTALLED, "nace instalada");

  Launcher L;
  L.image = imageCallSys(FLEXSYS_SCREEN_W, {});
  FlexAppLaunch launch = L.make("com.flexos.demo", "1.0.0", 1, 0, false);
  CHECK(flexAppStart(&m, &launch), "arranca: %s", flexAppReason(&m));
  CHECK(flexAppState(&m) == FLEXAPP_ST_RUNNING, "queda en ejecucion");
  CHECK(m.vm.globals[0] == 480, "onStart llamo a sys.screen_w y guardo 480");

  flexAppPostEvent(&m, FLEXAPP_EV_TOUCH_TAP, 100, 200);
  flexAppTick(&m);
  CHECK(m.vm.globals[1] == FLEXAPP_EV_TOUCH_TAP, "onEvent recibio el tipo");
  CHECK(m.vm.globals[2] == 100, "y la coordenada x");
  CHECK(m.events == 1, "se contabilizo un evento");

  uint32_t ticksBefore = m.ticks;
  flexAppSuspend(&m);
  CHECK(flexAppState(&m) == FLEXAPP_ST_SUSPENDED, "suspender la deja en segundo plano");
  flexAppTick(&m);
  CHECK(m.ticks == ticksBefore, "suspendida NO recibe ticks (%u -> %u)", ticksBefore, m.ticks);
  flexAppResume(&m);
  CHECK(flexAppState(&m) == FLEXAPP_ST_RUNNING, "reanudar la devuelve a ejecucion");
  flexAppTick(&m);
  CHECK(m.vm.globals[1] == FLEXAPP_EV_PAUSE || m.vm.globals[1] == FLEXAPP_EV_RESUME,
        "la app recibe pausa/reanudacion");

  flexAppStop(&m, FLEXAPP_STOP_USER);
  CHECK(flexAppState(&m) == FLEXAPP_ST_STOPPED, "parar la deja detenida");
  CHECK(!strcmp(flexAppReason(&m), "Cerrada por el usuario"), "con el motivo: %s", flexAppReason(&m));
  flexAppStop(&m, FLEXAPP_STOP_USER);
  CHECK(flexAppState(&m) == FLEXAPP_ST_STOPPED, "parar dos veces no rompe nada");
  flexAppTick(&m);
  CHECK(flexAppState(&m) == FLEXAPP_ST_STOPPED, "una app parada no ejecuta");

  // Bytecode invalido: no arranca y lo dice.
  Launcher bad;
  bad.image = imageCallSys(FLEXSYS_SCREEN_W, {});
  bad.image[4] = 9;                    // version de formato imposible
  FlexAppManager m2;
  flexAppInit(&m2, &a, nullptr);
  FlexAppLaunch l2 = bad.make("com.flexos.demo", "1.0.0", 1, 0, false);
  CHECK(!flexAppStart(&m2, &l2), "un bytecode invalido no arranca");
  CHECK(flexAppState(&m2) == FLEXAPP_ST_ERROR, "y queda en error");
  CHECK(strlen(flexAppReason(&m2)) > 0, "con un motivo legible: %s", flexAppReason(&m2));
}

// =============================================================
static void testPermissionsPerCall(){
  std::printf("-- permiso POR LLAMADA --\n");
  FlexAppHostApi a = api();

  // 1) El manifest DECLARA psram.measure pero no hay grant: denegado.
  {
    H = HostState(); H.appDir = "app1";
    FlexAppManager m;
    flexAppInit(&m, &a, nullptr);
    Launcher L;
    L.image = imageCallSys(FLEXSYS_PSRAM_FREE, {});
    FlexAppLaunch l = L.make("com.flexos.bench", "1.0.0", 1, FLEXPERM_SYS_PSRAM_MEASURE, false);
    CHECK(flexAppStart(&m, &l), "la app arranca igual, sin privilegios");
    CHECK(m.granted == 0, "no se concede nada sin grant");
    CHECK(m.vm.globals[0] == FLEXAPP_NO_VALUE, "y la medida sale como 'no disponible'");
    CHECK(H.reserves == 0, "no se llego a tocar el servicio");
    flexAppStop(&m, FLEXAPP_STOP_USER);
  }
  // 2) Con grant valido: concedido.
  {
    H = HostState(); H.appDir = "app1";
    FlexAppManager m;
    flexAppInit(&m, &a, nullptr);
    Launcher L;
    L.image = imageCallSys(FLEXSYS_PSRAM_FREE, {});
    FlexAppLaunch l = L.make("com.flexos.bench", "1.0.0", 1, FLEXPERM_SYS_PSRAM_MEASURE, true);
    CHECK(flexAppStart(&m, &l), "arranca con grant");
    CHECK(m.granted == FLEXPERM_SYS_PSRAM_MEASURE, "y se concede exactamente lo pedido");
    CHECK(m.vm.globals[0] == 16000, "la medida real llega a la app");
    flexAppStop(&m, FLEXAPP_STOP_USER);
    CHECK(m.granted == 0, "al parar, los permisos dejan de existir");
  }
  // 3) Grant de OTRA version: denegado, la app sigue funcionando.
  {
    H = HostState(); H.appDir = "app1";
    FlexAppManager m;
    flexAppInit(&m, &a, nullptr);
    Launcher L;
    L.image = imageCallSys(FLEXSYS_PSRAM_FREE, {});
    FlexAppLaunch l = L.make("com.flexos.bench", "1.0.0", 1, FLEXPERM_SYS_PSRAM_MEASURE, true);
    // Se altera el codigo de version DENTRO del grant ya firmado.
    L.grant[12] = 99;
    CHECK(flexAppStart(&m, &l), "arranca aunque el grant no valga");
    CHECK(m.granted == 0, "pero sin conceder nada");
    CHECK(m.grantStatus == FLEXGRANT_ERR_VERSION || m.grantStatus == FLEXGRANT_ERR_SIGNATURE,
          "y con el motivo del rechazo (%s)", flexGrantStatusText(m.grantStatus));
    CHECK(m.vm.globals[0] == FLEXAPP_NO_VALUE, "el servicio sigue cerrado");
    flexAppStop(&m, FLEXAPP_STOP_USER);
  }
  // 4) Un permiso NO se hereda por tener otro.
  {
    H = HostState(); H.appDir = "app1";
    FlexAppManager m;
    flexAppInit(&m, &a, nullptr);
    Launcher L;
    L.image = imageCallSys(FLEXSYS_TEMP_MILLIC, {});
    FlexAppLaunch l = L.make("com.flexos.bench", "1.0.0", 1, FLEXPERM_SYS_PSRAM_MEASURE, true);
    CHECK(flexAppStart(&m, &l), "arranca con psram.measure");
    CHECK(flexAppHasPermission(&m, FLEXPERM_SYS_PSRAM_MEASURE), "tiene psram.measure");
    CHECK(!flexAppHasPermission(&m, FLEXPERM_SYS_TEMPERATURE_READ), "pero NO temperature.read");
    CHECK(m.vm.globals[0] == FLEXAPP_NO_VALUE, "y la temperatura sale denegada");
    flexAppStop(&m, FLEXAPP_STOP_USER);
  }
}

// =============================================================
static void testNoHardware(){
  std::printf("-- sin PSRAM y sin sensor: 'no disponible', no un numero inventado --\n");
  FlexAppHostApi a = api();
  {
    H = HostState(); H.appDir = "app1"; H.psramAvailable = false;
    FlexAppManager m;
    flexAppInit(&m, &a, nullptr);
    Launcher L;
    L.image = imageCallSys(FLEXSYS_PSRAM_FREE, {});
    FlexAppLaunch l = L.make("com.flexos.bench", "1.0.0", 1, FLEXPERM_SYS_PSRAM_MEASURE, true);
    CHECK(flexAppStart(&m, &l), "arranca sin PSRAM");
    CHECK(m.vm.globals[0] == FLEXAPP_NO_VALUE, "la medida de PSRAM dice 'no disponible'");
    flexAppStop(&m, FLEXAPP_STOP_USER);
  }
  {
    H = HostState(); H.appDir = "app1"; H.psramAvailable = false;
    FlexAppManager m;
    flexAppInit(&m, &a, nullptr);
    Launcher L;
    L.image = imageCallSys(FLEXSYS_PSRAM_RESERVE, { 1024 });
    FlexAppLaunch l = L.make("com.flexos.bench", "1.0.0", 1, FLEXPERM_SYS_PSRAM_MEASURE, true);
    CHECK(flexAppStart(&m, &l), "arranca");
    CHECK(m.vm.globals[0] == 0, "reservar sin PSRAM devuelve 0 limpiamente");
    flexAppStop(&m, FLEXAPP_STOP_USER);
  }
  {
    H = HostState(); H.appDir = "app1"; H.tempSensor = false;
    FlexAppManager m;
    flexAppInit(&m, &a, nullptr);
    Launcher L;
    L.image = imageCallSys(FLEXSYS_TEMP_MILLIC, {});
    FlexAppLaunch l = L.make("com.flexos.bench", "1.0.0", 1, FLEXPERM_SYS_TEMPERATURE_READ, true);
    CHECK(flexAppStart(&m, &l), "arranca con permiso de temperatura");
    CHECK(m.vm.globals[0] == FLEXAPP_NO_VALUE, "sin sensor real: 'no disponible'");
    flexAppStop(&m, FLEXAPP_STOP_USER);
  }
  {
    H = HostState(); H.appDir = "app1"; H.tempSensor = true; H.tempValue = 42500;
    FlexAppManager m;
    flexAppInit(&m, &a, nullptr);
    Launcher L;
    L.image = imageCallSys(FLEXSYS_TEMP_MILLIC, {});
    FlexAppLaunch l = L.make("com.flexos.bench", "1.0.0", 1, FLEXPERM_SYS_TEMPERATURE_READ, true);
    CHECK(flexAppStart(&m, &l), "arranca");
    CHECK(m.vm.globals[0] == 42500, "con sensor real, el valor real");
    flexAppStop(&m, FLEXAPP_STOP_USER);
  }
  {
    // El panel no reporta multitactil fiable: NO se inventan puntos.
    H = HostState(); H.appDir = "app1"; H.touchCount = -1;
    FlexAppManager m;
    flexAppInit(&m, &a, nullptr);
    Launcher L;
    L.image = imageCallSys(FLEXSYS_INPUT_POINTS, {});
    FlexAppLaunch l = L.make("com.flexos.bench", "1.0.0", 1, 0, false);
    CHECK(flexAppStart(&m, &l), "arranca");
    CHECK(m.vm.globals[0] == FLEXAPP_NO_VALUE, "sin dato fiable del panel: 'no disponible'");
    flexAppStop(&m, FLEXAPP_STOP_USER);

    H.touchCount = 2;
    H.touch[0] = { 10, 20, 1 }; H.touch[1] = { 30, 40, 2 };
    FlexAppManager m2;
    flexAppInit(&m2, &a, nullptr);
    Launcher L2;
    L2.image = imageCallSys(FLEXSYS_INPUT_POINTS, {});
    FlexAppLaunch l2 = L2.make("com.flexos.bench", "1.0.0", 1, 0, false);
    CHECK(flexAppStart(&m2, &l2), "arranca");
    CHECK(m2.vm.globals[0] == 2, "con dato fiable, el numero real de contactos");
    flexAppStop(&m2, FLEXAPP_STOP_USER);
  }
}

// =============================================================
static void testBudget(){
  std::printf("-- presupuesto: ceder si, colgarse no --\n");
  FlexAppHostApi a = api();
  H = HostState(); H.appDir = "app1";

  // onTick con un bucle infinito de verdad.
  Image im;
  im.globals = 2; im.stack = 32; im.frames = 32; im.calldepth = 8; im.mem = 256;
  im.funcs.push_back({0, 1, 0});
  im.h[FLEXVM_H_TICK] = 0;
  size_t loop = im.here();
  size_t j = im.here(); im.u16(OP_JMP, 0);
  im.patchRel(j, loop);

  FlexAppManager m;
  flexAppInit(&m, &a, nullptr);
  Launcher L;
  L.image = im.build();
  FlexAppLaunch l = L.make("com.flexos.bucle", "1.0.0", 1, 0, false);
  CHECK(flexAppStart(&m, &l), "una app con un bucle infinito arranca (no tiene onStart)");
  CHECK(flexAppState(&m) == FLEXAPP_ST_RUNNING, "y queda en ejecucion");

  int ticks = 0;
  while(flexAppState(&m) == FLEXAPP_ST_RUNNING && ticks < 500){
    H.nowUs += 16000;              // un cuadro de sistema
    flexAppTick(&m);
    ticks++;
  }
  CHECK(flexAppState(&m) == FLEXAPP_ST_STOPPED_SECURITY,
        "el sistema acaba deteniendola por seguridad (estado %s)", flexAppStateName(m.state));
  CHECK(m.stopReason == FLEXAPP_STOP_BUDGET, "y el motivo es el presupuesto");
  CHECK(strstr(flexAppReason(&m), "control") != nullptr, "con un motivo claro: %s", flexAppReason(&m));
  CHECK(ticks > 1, "hizo falta mas de un tick: se le dio la oportunidad de ceder (%d)", ticks);
  CHECK(ticks <= FLEXAPP_MAX_OVERRUN_TICKS + 3, "pero no eterna (%d ticks)", ticks);
  CHECK(H.openResources() == 0, "y al detenerla no quedo ningun recurso suyo");

  // Cesion voluntaria: NO se penaliza.
  H = HostState(); H.appDir = "app1";
  Image good;
  good.globals = 2; good.stack = 32; good.frames = 32; good.calldepth = 8; good.mem = 256;
  good.funcs.push_back({0, 1, 0});
  good.h[FLEXVM_H_TICK] = 0;
  good.b(OP_YIELD);
  good.b(OP_RET);
  FlexAppManager m2;
  flexAppInit(&m2, &a, nullptr);
  Launcher L2;
  L2.image = good.build();
  FlexAppLaunch l2 = L2.make("com.flexos.cede", "1.0.0", 1, 0, false);
  CHECK(flexAppStart(&m2, &l2), "arranca la app que cede");
  for(int i = 0; i < 200; i++){ H.nowUs += 16000; flexAppTick(&m2); }
  CHECK(flexAppState(&m2) == FLEXAPP_ST_RUNNING, "ceder cortesmente no la detiene");
  flexAppStop(&m2, FLEXAPP_STOP_USER);
}

// =============================================================
static void testOrientation(){
  std::printf("-- orientacion: se restaura SIEMPRE --\n");
  FlexAppHostApi a = api();

  auto runAndStop = [&](FlexAppStopReason why, const char* what){
    H = HostState(); H.appDir = "app1";
    FlexAppManager m;
    flexAppInit(&m, &a, nullptr);
    Launcher L;
    L.image = imageCallSys(FLEXSYS_DISPLAY_LANDSCAPE, { 1 });
    FlexAppLaunch l = L.make("com.flexos.bench", "1.0.0", 1,
                             FLEXPERM_SYS_DISPLAY_LANDSCAPE, true);
    CHECK(flexAppStart(&m, &l), "arranca (%s)", what);
    CHECK(H.landscape, "la app puso el horizontal (%s)", what);
    flexAppStop(&m, why);
    CHECK(!H.landscape, "y al salir por '%s' se restauro el vertical", what);
    CHECK(H.openResources() == 0, "sin recursos colgando (%s)", what);
  };
  runAndStop(FLEXAPP_STOP_USER, "usuario");
  runAndStop(FLEXAPP_STOP_SYSTEM, "sistema");
  runAndStop(FLEXAPP_STOP_CANCELLED, "cancelacion");
  runAndStop(FLEXAPP_STOP_UNINSTALL, "desinstalacion");

  // Suspender tambien devuelve la pantalla.
  {
    H = HostState(); H.appDir = "app1";
    FlexAppManager m;
    flexAppInit(&m, &a, nullptr);
    Launcher L;
    L.image = imageCallSys(FLEXSYS_DISPLAY_LANDSCAPE, { 1 });
    FlexAppLaunch l = L.make("com.flexos.bench", "1.0.0", 1, FLEXPERM_SYS_DISPLAY_LANDSCAPE, true);
    CHECK(flexAppStart(&m, &l), "arranca");
    CHECK(H.landscape, "horizontal puesto");
    flexAppSuspend(&m);
    CHECK(!H.landscape, "al pasar a segundo plano se devuelve el vertical");
    flexAppResume(&m);
    CHECK(H.landscape, "y al volver se recupera la peticion de la app");
    flexAppStop(&m, FLEXAPP_STOP_USER);
    CHECK(!H.landscape, "cerrar restaura");
  }
  // Sin permiso, no hay horizontal.
  {
    H = HostState(); H.appDir = "app1";
    FlexAppManager m;
    flexAppInit(&m, &a, nullptr);
    Launcher L;
    L.image = imageCallSys(FLEXSYS_DISPLAY_LANDSCAPE, { 1 });
    FlexAppLaunch l = L.make("com.flexos.bench", "1.0.0", 1, FLEXPERM_SYS_DISPLAY_LANDSCAPE, false);
    CHECK(flexAppStart(&m, &l), "arranca sin grant");
    CHECK(!H.landscape, "sin permiso no se cambia la orientacion");
    CHECK(H.landscapeCalls == 0, "y ni siquiera se llama al servicio");
    flexAppStop(&m, FLEXAPP_STOP_USER);
  }
  // El equipo dice que no puede: la app se entera.
  {
    H = HostState(); H.appDir = "app1"; H.caps = FLEXAPP_CAP_STORAGE;
    FlexAppManager m;
    flexAppInit(&m, &a, nullptr);
    Launcher L;
    L.image = imageCallSys(FLEXSYS_DISPLAY_LANDSCAPE, { 1 });
    FlexAppLaunch l = L.make("com.flexos.bench", "1.0.0", 1, FLEXPERM_SYS_DISPLAY_LANDSCAPE, true);
    CHECK(flexAppStart(&m, &l), "arranca");
    CHECK(m.vm.globals[0] == 0, "el servicio devuelve 0 si el equipo no puede");
    CHECK(!H.landscape, "y la pantalla no cambia");
    flexAppStop(&m, FLEXAPP_STOP_USER);
  }
}

// =============================================================
static void testStorageIsolation(){
  std::printf("-- almacenamiento privado: aislado, con cuota y nombres saneados --\n");
  FlexAppHostApi a = api();
  H = HostState();

  // Programa: escribe "hola" en el archivo cuyo nombre esta en las constantes.
  auto writer = [&](const char* name, const char* data, int32_t dataLen) {
    Image im;
    im.globals = 4; im.stack = 32; im.frames = 32; im.calldepth = 8; im.mem = 512;
    for(const char* p = name; *p; p++) im.konst.push_back((uint8_t)*p);
    uint32_t nameOff = 0, nameLen = (uint32_t)strlen(name);
    uint32_t dataOff = 32;
    im.funcs.push_back({0, 0, 0});
    im.h[FLEXVM_H_START] = 0;
    // memoria[32..] = data
    for(int i = 0; i < dataLen; i++){
      im.i32((int32_t)(dataOff + i));
      im.i32((int32_t)(uint8_t)data[i]);
      im.b(OP_ST8);
    }
    im.i32((int32_t)nameOff); im.i32((int32_t)nameLen);
    im.i32((int32_t)dataOff); im.i32(dataLen);
    im.u16(OP_SYS, FLEXSYS_STORE_WRITE);
    im.u16(OP_ST_GLOBAL, 0);
    im.b(OP_RET);
    return im.build();
  };

  // App 1 escribe en su carpeta.
  H.appDir = "app1";
  FlexAppManager m1;
  flexAppInit(&m1, &a, nullptr);
  Launcher L1;
  L1.image = writer("datos.bin", "uno", 3);
  FlexAppLaunch l1 = L1.make("com.flexos.uno", "1.0.0", 1, FLEXPERM_SYS_STORAGE_APP, true);
  CHECK(flexAppStart(&m1, &l1), "la app 1 arranca");
  CHECK(m1.vm.globals[0] == 1, "y escribe en su carpeta");
  CHECK(H.files.count("app1/datos.bin") == 1, "el archivo esta en app1");
  flexAppStop(&m1, FLEXAPP_STOP_USER);

  // App 2 escribe un archivo con EL MISMO nombre: va a OTRA carpeta.
  H.appDir = "app2";
  FlexAppManager m2;
  flexAppInit(&m2, &a, nullptr);
  Launcher L2;
  L2.image = writer("datos.bin", "dos", 3);
  FlexAppLaunch l2 = L2.make("com.flexos.dos", "1.0.0", 1, FLEXPERM_SYS_STORAGE_APP, true);
  CHECK(flexAppStart(&m2, &l2), "la app 2 arranca");
  CHECK(H.files.count("app2/datos.bin") == 1, "su archivo va a app2");
  CHECK(H.files["app1/datos.bin"] == "uno", "y el de la app 1 sigue intacto");
  CHECK(H.files["app2/datos.bin"] == "dos", "cada una ve el suyo");
  flexAppStop(&m2, FLEXAPP_STOP_USER);

  // Nombres peligrosos: rechazados.
  const char* nasty[] = { "../fuera.bin", "/etc/passwd", "..", ".oculto", "con espacio" };
  for(const char* n : nasty){
    H = HostState(); H.appDir = "app3";
    FlexAppManager m;
    flexAppInit(&m, &a, nullptr);
    Launcher L;
    L.image = writer(n, "x", 1);
    FlexAppLaunch l = L.make("com.flexos.tres", "1.0.0", 1, FLEXPERM_SYS_STORAGE_APP, true);
    CHECK(flexAppStart(&m, &l), "arranca con nombre '%s'", n);
    CHECK(m.vm.globals[0] == 0, "el nombre '%s' se rechaza", n);
    CHECK(H.files.empty(), "y no se escribe nada con '%s'", n);
    flexAppStop(&m, FLEXAPP_STOP_USER);
  }

  // Sin permiso storage.app: no se escribe.
  {
    H = HostState(); H.appDir = "app4";
    FlexAppManager m;
    flexAppInit(&m, &a, nullptr);
    Launcher L;
    L.image = writer("datos.bin", "no", 2);
    FlexAppLaunch l = L.make("com.flexos.cuatro", "1.0.0", 1, FLEXPERM_SYS_STORAGE_APP, false);
    CHECK(flexAppStart(&m, &l), "arranca sin grant");
    CHECK(m.vm.globals[0] == FLEXAPP_NO_VALUE, "escribir sin permiso devuelve 'no disponible'");
    CHECK(H.files.empty(), "y no toca el almacenamiento");
    flexAppStop(&m, FLEXAPP_STOP_USER);
  }

  // Cuota: no se pasa.
  {
    H = HostState(); H.appDir = "app5";
    FlexAppManager m;
    flexAppInit(&m, &a, nullptr);
    Launcher L;
    std::string big(400, 'A');
    L.image = writer("grande.bin", big.c_str(), 400);
    FlexAppLaunch l = L.make("com.flexos.cinco", "1.0.0", 1, FLEXPERM_SYS_STORAGE_APP, true);
    l.storageQuota = 100;              // cuota MENOR que lo que quiere escribir
    CHECK(flexAppStart(&m, &l), "arranca con cuota pequena");
    CHECK(m.vm.globals[0] == 0, "pasarse de cuota devuelve 0");
    CHECK(H.files.empty(), "y no se escribe nada");
    flexAppStop(&m, FLEXAPP_STOP_USER);
  }
}

// =============================================================
static void testLeaks(){
  std::printf("-- 25 aperturas y cierres seguidos --\n");
  FlexAppHostApi a = api();
  H = HostState(); H.appDir = "app1";

  // Un programa que se agarra a TODO: horizontal, exclusiva y PSRAM.
  Image im;
  im.globals = 4; im.stack = 32; im.frames = 32; im.calldepth = 8; im.mem = 512;
  im.funcs.push_back({0, 0, 0});
  im.h[FLEXVM_H_START] = 0;
  im.i32(1); im.u16(OP_SYS, FLEXSYS_DISPLAY_LANDSCAPE); im.b(OP_DROP);
  im.i32(1); im.u16(OP_SYS, FLEXSYS_DISPLAY_EXCLUSIVE); im.b(OP_DROP);
  im.i32(64); im.u16(OP_SYS, FLEXSYS_PSRAM_RESERVE); im.u16(OP_ST_GLOBAL, 0);
  im.i32(64); im.u16(OP_SYS, FLEXSYS_PSRAM_RESERVE); im.u16(OP_ST_GLOBAL, 1);
  im.b(OP_RET);
  std::vector<uint8_t> image = im.build();

  uint32_t freeBefore = H.psramFreeKb;
  for(int i = 0; i < 25; i++){
    FlexAppManager m;
    flexAppInit(&m, &a, nullptr);
    Launcher L;
    L.image = image;
    uint32_t perms = FLEXPERM_SYS_DISPLAY_LANDSCAPE | FLEXPERM_SYS_DISPLAY_EXCLUSIVE |
                     FLEXPERM_SYS_PSRAM_MEASURE;
    FlexAppLaunch l = L.make("com.flexos.ciclos", "1.0.0", 1, perms, true);
    CHECK(flexAppStart(&m, &l), "ciclo %d: arranca", i);
    if(i == 0){
      CHECK(H.landscape && H.exclusive, "la app toma la pantalla");
      CHECK(H.live.size() == 2, "y dos reservas de PSRAM");
    }
    H.nowUs += 16000;
    flexAppTick(&m);
    // Se cierra de una forma distinta en cada vuelta: usuario, sistema,
    // cancelacion... todas tienen que soltar lo mismo.
    FlexAppStopReason why = (i % 3 == 0) ? FLEXAPP_STOP_USER
                          : (i % 3 == 1) ? FLEXAPP_STOP_SYSTEM : FLEXAPP_STOP_CANCELLED;
    flexAppStop(&m, why);
    CHECK(H.openResources() == 0, "ciclo %d: no queda nada abierto", i);
    CHECK(H.psramFreeKb == freeBefore, "ciclo %d: la PSRAM vuelve a su sitio", i);
  }
  CHECK(H.reserves == H.releases, "tantas reservas como liberaciones (%d/%d)", H.reserves, H.releases);
  CHECK(H.reserves == 50, "dos reservas por ciclo, 25 ciclos");
  std::printf("   %d reservas, %d liberaciones, %d KB libres al final\n",
              H.reserves, H.releases, (int)H.psramFreeKb);
}

// =============================================================
static void testCancelPhases(){
  std::printf("-- cancelacion en cada fase --\n");
  FlexAppHostApi a = api();

  // Parar desde INSTALLED (nunca arrancada).
  {
    H = HostState(); H.appDir = "app1";
    FlexAppManager m;
    flexAppInit(&m, &a, nullptr);
    flexAppStop(&m, FLEXAPP_STOP_CANCELLED);
    CHECK(flexAppState(&m) == FLEXAPP_ST_STOPPED, "parar una app que nunca arranco no rompe");
    CHECK(H.openResources() == 0, "y no deja nada");
  }
  // Parar mientras un manejador esta A MEDIAS.
  {
    H = HostState(); H.appDir = "app1";
    Image im;
    im.globals = 2; im.stack = 32; im.frames = 32; im.calldepth = 8; im.mem = 256;
    im.funcs.push_back({0, 1, 0});
    im.h[FLEXVM_H_TICK] = 0;
    size_t loop = im.here();
    size_t j = im.here(); im.u16(OP_JMP, 0);
    im.patchRel(j, loop);
    FlexAppManager m;
    flexAppInit(&m, &a, nullptr);
    Launcher L;
    L.image = im.build();
    FlexAppLaunch l = L.make("com.flexos.medias", "1.0.0", 1, 0, false);
    CHECK(flexAppStart(&m, &l), "arranca");
    H.nowUs += 16000;
    flexAppTick(&m);
    CHECK(m.vm.running == 1, "hay un manejador a medias");
    flexAppStop(&m, FLEXAPP_STOP_SYSTEM);
    CHECK(flexAppState(&m) == FLEXAPP_ST_STOPPED, "se puede parar igual");
    CHECK(m.vm.running == 0, "y el manejador se corta");
    CHECK(H.openResources() == 0, "sin recursos colgando");
  }
  // Suspender con un manejador a medias.
  {
    H = HostState(); H.appDir = "app1";
    Image im;
    im.globals = 2; im.stack = 32; im.frames = 32; im.calldepth = 8; im.mem = 256;
    im.funcs.push_back({0, 1, 0});
    im.h[FLEXVM_H_TICK] = 0;
    size_t loop = im.here();
    size_t j = im.here(); im.u16(OP_JMP, 0);
    im.patchRel(j, loop);
    FlexAppManager m;
    flexAppInit(&m, &a, nullptr);
    Launcher L;
    L.image = im.build();
    FlexAppLaunch l = L.make("com.flexos.medias", "1.0.0", 1, 0, false);
    flexAppStart(&m, &l);
    H.nowUs += 16000;
    flexAppTick(&m);
    flexAppSuspend(&m);
    CHECK(m.vm.running == 0, "suspender aborta el manejador a medias");
    CHECK(flexAppState(&m) == FLEXAPP_ST_SUSPENDED, "y la deja suspendida");
    flexAppStop(&m, FLEXAPP_STOP_USER);
    CHECK(H.openResources() == 0, "cerrar despues sigue limpiando");
  }
  // La app pide cerrarse sola.
  {
    H = HostState(); H.appDir = "app1";
    Image im;
    im.globals = 2; im.stack = 32; im.frames = 32; im.calldepth = 8; im.mem = 256;
    im.funcs.push_back({0, 1, 0});
    im.h[FLEXVM_H_TICK] = 0;
    im.u16(OP_SYS, FLEXSYS_EXIT); im.b(OP_DROP); im.b(OP_RET);
    FlexAppManager m;
    flexAppInit(&m, &a, nullptr);
    Launcher L;
    L.image = im.build();
    FlexAppLaunch l = L.make("com.flexos.salir", "1.0.0", 1, 0, false);
    CHECK(flexAppStart(&m, &l), "arranca");
    H.nowUs += 16000;
    flexAppTick(&m);
    CHECK(flexAppState(&m) == FLEXAPP_ST_STOPPED, "sys.exit cierra la app de forma limpia");
  }
}

// =============================================================
static void testDrawBudget(){
  std::printf("-- limite de comandos por cuadro --\n");
  FlexAppHostApi a = api();
  H = HostState(); H.appDir = "app1";

  Image im;
  im.globals = 2; im.stack = 64; im.frames = 32; im.calldepth = 8; im.mem = 256;
  im.funcs.push_back({0, 1, 0});
  im.h[FLEXVM_H_TICK] = 0;
  for(int i = 0; i < 200; i++){
    im.i32(0); im.i32(0); im.i32(10); im.i32(10); im.i32(0xFFFF);
    im.u16(OP_SYS, FLEXSYS_GFX_FILL_RECT);
    im.b(OP_DROP);
  }
  im.u16(OP_SYS, FLEXSYS_GFX_PRESENT); im.b(OP_DROP);
  im.b(OP_RET);

  FlexAppManager m;
  flexAppInit(&m, &a, nullptr);
  Launcher L;
  L.image = im.build();
  FlexAppLaunch l = L.make("com.flexos.dibujo", "1.0.0", 1, 0, false);
  l.drawPerFrame = 50;                 // techo pequeno a proposito
  CHECK(flexAppStart(&m, &l), "arranca");
  H.nowUs += 16000;
  flexAppTick(&m);
  CHECK(H.drawCalls <= 50, "no se dibujan mas comandos de los permitidos (%d)", H.drawCalls);
  CHECK(m.drawDropped > 0, "y los de mas quedan contados (%u)", m.drawDropped);
  CHECK(H.presentCalls == 1, "el cuadro se publica igual");
  CHECK(flexAppState(&m) == FLEXAPP_ST_RUNNING, "pasarse dibujando NO mata la app");
  flexAppStop(&m, FLEXAPP_STOP_USER);
}

// =============================================================
static void testBlitAndFailedStart(){
  std::printf("-- sprites acotados y arranque fallido que no deja nada --\n");
  FlexAppHostApi a = api();

  { // Un blit escalado a un tamano imposible NO se dibuja.
    H = HostState(); H.appDir = "app1";
    Image im;
    im.globals = 2; im.stack = 32; im.frames = 32; im.calldepth = 8; im.mem = 4096;
    im.funcs.push_back({0, 1, 0});
    im.h[FLEXVM_H_TICK] = 0;
    // sprite 8x8 (128 B en memoria) escalado a 4000x4000 pixeles de salida
    im.i32(0); im.i32(8); im.i32(8); im.i32(0); im.i32(0); im.i32(4000); im.i32(4000);
    im.u16(OP_SYS, FLEXSYS_GFX_BLIT_SCALED); im.b(OP_DROP);
    // ...y el mismo sprite a su tamano natural, que SI se dibuja
    im.i32(0); im.i32(8); im.i32(8); im.i32(0); im.i32(0);
    im.u16(OP_SYS, FLEXSYS_GFX_BLIT); im.b(OP_DROP);
    im.b(OP_RET);
    FlexAppManager m;
    flexAppInit(&m, &a, nullptr);
    Launcher L;
    L.image = im.build();
    FlexAppLaunch l = L.make("com.flexos.sprite", "1.0.0", 1, 0, false);
    CHECK(flexAppStart(&m, &l), "arranca");
    H.nowUs += 16000;
    flexAppTick(&m);
    CHECK(H.drawCalls == 1, "solo se dibuja el blit razonable (%d)", H.drawCalls);
    CHECK(flexAppState(&m) == FLEXAPP_ST_RUNNING, "y pedir lo imposible no mata la app");
    flexAppStop(&m, FLEXAPP_STOP_USER);
  }
  { // Un sprite cuyos pixeles no caben en la memoria de la app: no se dibuja.
    H = HostState(); H.appDir = "app1";
    Image im;
    im.globals = 2; im.stack = 32; im.frames = 32; im.calldepth = 8; im.mem = 256;
    im.funcs.push_back({0, 1, 0});
    im.h[FLEXVM_H_TICK] = 0;
    im.i32(0); im.i32(64); im.i32(64); im.i32(0); im.i32(0);   // 64*64*2 = 8192 B > 256
    im.u16(OP_SYS, FLEXSYS_GFX_BLIT); im.b(OP_DROP);
    im.b(OP_RET);
    FlexAppManager m;
    flexAppInit(&m, &a, nullptr);
    Launcher L;
    L.image = im.build();
    FlexAppLaunch l = L.make("com.flexos.sprite", "1.0.0", 1, 0, false);
    CHECK(flexAppStart(&m, &l), "arranca");
    H.nowUs += 16000;
    flexAppTick(&m);
    CHECK(H.drawCalls == 0, "un sprite que no cabe en su memoria no se dibuja");
    flexAppStop(&m, FLEXAPP_STOP_USER);
  }
  { // onStart toma la pantalla y DESPUES revienta: no puede quedarse nada.
    H = HostState(); H.appDir = "app1";
    Image im;
    im.globals = 2; im.stack = 32; im.frames = 32; im.calldepth = 8; im.mem = 256;
    im.funcs.push_back({0, 0, 0});
    im.h[FLEXVM_H_START] = 0;
    im.i32(1); im.u16(OP_SYS, FLEXSYS_DISPLAY_LANDSCAPE); im.b(OP_DROP);
    im.i32(1); im.u16(OP_SYS, FLEXSYS_DISPLAY_EXCLUSIVE); im.b(OP_DROP);
    im.i32(256); im.u16(OP_SYS, FLEXSYS_PSRAM_RESERVE); im.b(OP_DROP);
    im.i32(1); im.i32(0); im.b(OP_DIV); im.b(OP_DROP);      // division por cero
    im.b(OP_RET);
    FlexAppManager m;
    flexAppInit(&m, &a, nullptr);
    Launcher L;
    L.image = im.build();
    uint32_t perms = FLEXPERM_SYS_DISPLAY_LANDSCAPE | FLEXPERM_SYS_DISPLAY_EXCLUSIVE |
                     FLEXPERM_SYS_PSRAM_MEASURE;
    FlexAppLaunch l = L.make("com.flexos.mala", "1.0.0", 1, perms, true);
    CHECK(!flexAppStart(&m, &l), "una app que revienta en onStart no arranca");
    CHECK(flexAppState(&m) == FLEXAPP_ST_ERROR, "y queda en error");
    CHECK(H.openResources() == 0, "sin dejar pantalla, orientacion ni PSRAM tomadas");
    CHECK(H.reserves == H.releases, "toda reserva se devolvio (%d/%d)", H.reserves, H.releases);
  }
}

// =============================================================
static void testStressSlices(){
  std::printf("-- carga de CPU por rodajas, cancelable --\n");
  FlexAppHostApi a = api();
  H = HostState(); H.appDir = "app1";

  Image im;
  im.globals = 4; im.stack = 32; im.frames = 32; im.calldepth = 8; im.mem = 256;
  im.funcs.push_back({0, 0, 0});
  im.h[FLEXVM_H_START] = 0;
  im.i32(0); im.i32(4);
  im.u16(OP_SYS, FLEXSYS_CPU_STRESS_BEGIN); im.u16(OP_ST_GLOBAL, 0);
  im.b(OP_RET);
  uint32_t tickOff = (uint32_t)im.here();
  im.funcs.push_back({tickOff, 1, 0});
  im.h[FLEXVM_H_TICK] = 1;
  im.u16(OP_SYS, FLEXSYS_CPU_STRESS_SLICE); im.u16(OP_ST_GLOBAL, 1);
  im.b(OP_RET);

  FlexAppManager m;
  flexAppInit(&m, &a, nullptr);
  Launcher L;
  L.image = im.build();
  FlexAppLaunch l = L.make("com.flexos.stress", "1.0.0", 1, FLEXPERM_SYS_CPU_STRESS, true);
  CHECK(flexAppStart(&m, &l), "arranca con permiso de carga");
  CHECK(m.vm.globals[0] == 1, "la sesion de carga se acepta");
  for(int i = 0; i < 10; i++){ H.nowUs += 16000; flexAppTick(&m); }
  CHECK(H.stressSlices == 4, "se ejecutan EXACTAMENTE las rodajas pedidas (%d)", H.stressSlices);
  CHECK(m.vm.globals[1] == -1, "y al acabar la app se entera");
  CHECK(flexAppState(&m) == FLEXAPP_ST_RUNNING, "la interfaz sigue viva durante todo");
  flexAppStop(&m, FLEXAPP_STOP_USER);
  CHECK(m.stressActive == 0, "al cerrar, la carga se cancela");
}

// =============================================================
int main(){
  std::printf("\n=== FlexOS · gestor de aplicaciones flex-app-v1 ===\n");
  gStore.generate(); gDev.generate();

  testSyscallTable();
  testLifecycle();
  testPermissionsPerCall();
  testNoHardware();
  testBudget();
  testOrientation();
  testStorageIsolation();
  testLeaks();
  testCancelPhases();
  testDrawBudget();
  testBlitAndFailedStart();
  testStressSlices();

  gStore.free_(); gDev.free_();
  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
