// #############################################################
//  test_appgrant.cpp  ·  pruebas de host de los permisos firmados
// #############################################################
//
//  Aqui se comprueba lo que el enunciado exige campo a campo: un grant
//  vale SOLO para la app, la version, el paquete y el desarrollador a
//  los que se emitio, y SOLO si lo firmo la clave pinneada de Flex
//  Store. Cualquier otra combinacion se deniega de forma limpia.
//
//  Las claves son EFIMERAS: se generan aqui y no se escriben en disco.

#include "../../FlexOS_Ultra/FlexOS_AppGrant.h"
#include "pkgbuild.h"

#include <cstdio>
#include <cstring>

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

static pkgb::Key gStore;     // clave "de Flex Store" (la pinneada)
static pkgb::Key gOther;     // otra clave cualquiera
static pkgb::Key gDev;       // clave del desarrollador

static const char* kId = "com.flexos.contador";
static const char* kVer = "1.2.0";
static const uint32_t kCode = 7;

static pkgb::Bytes pkgHash(){
  pkgb::Bytes h(32);
  for(int i = 0; i < 32; i++) h[i] = (uint8_t)(i * 7 + 1);
  return h;
}

static pkgb::GrantSpec baseSpec(){
  pkgb::GrantSpec g;
  g.packageId = kId;
  g.versionName = kVer;
  g.versionCode = kCode;
  g.packageSha256 = pkgHash();
  g.developerFingerprint = gDev.fingerprint();
  g.mask = FLEXPERM_SYS_STORAGE_APP | FLEXPERM_SYS_DISPLAY_LANDSCAPE;
  return g;
}

static FlexGrantExpect baseExpect(){
  FlexGrantExpect e;
  memset(&e, 0, sizeof(e));
  e.packageId = kId;
  e.versionName = kVer;
  e.versionCode = kCode;
  pkgb::Bytes h = pkgHash();
  memcpy(e.packageSha256, h.data(), 32);
  pkgb::Bytes fp = pkgb::sha256(gDev.pub);
  memcpy(e.developerKeySha256, fp.data(), 32);
  e.manifestRequested = FLEXPERM_SYS_ALL;
  e.nowEpoch = 1780000000ull;
  return e;
}

static FlexGrantStatus check(const pkgb::Bytes& blob, const FlexGrantExpect& e,
                             FlexGrantResult* out = nullptr){
  FlexGrantResult r;
  return flexGrantCheck(blob.data(), (uint32_t)blob.size(), &e, gStore.pub.data(), out ? out : &r);
}

// =============================================================
static void testValid(){
  std::printf("-- grant valido --\n");
  pkgb::Bytes g = pkgb::buildGrant(baseSpec(), gStore);
  FlexGrantExpect e = baseExpect();
  FlexGrantResult r;
  CHECK(check(g, e, &r) == FLEXGRANT_OK, "un grant coherente y bien firmado se acepta");
  CHECK(r.granted == (FLEXPERM_SYS_STORAGE_APP | FLEXPERM_SYS_DISPLAY_LANDSCAPE),
        "se conceden exactamente los permisos del grant");
  CHECK(r.windowChecked == 1, "sin ventana declarada, no hay nada que comprobar");
  CHECK(g.size() == FLEXGRANT_BYTES, "el bloque mide lo que dice el formato");
}

static void testAbsent(){
  std::printf("-- sin grant: la app funciona, sin privilegios --\n");
  FlexGrantExpect e = baseExpect();
  FlexGrantResult r;
  CHECK(flexGrantCheck(nullptr, 0, &e, gStore.pub.data(), &r) == FLEXGRANT_ABSENT,
        "no traer grant no es un error");
  CHECK(r.granted == 0, "y no concede nada");
}

static void testTampered(){
  std::printf("-- alterado: cada campo, por separado --\n");
  FlexGrantExpect e = baseExpect();

  { // un bit cambiado en la mascara
    pkgb::Bytes g = pkgb::buildGrant(baseSpec(), gStore);
    g[224] ^= 0x04;   // añade system.psram.measure a mano
    FlexGrantStatus s = check(g, e);
    CHECK(s == FLEXGRANT_ERR_PERMS || s == FLEXGRANT_ERR_SIGNATURE,
          "tocar la mascara rompe el contador o la firma (fue %s)", flexGrantStatusText(s));
  }
  { // mascara y contador manipulados a la vez: cae la firma
    pkgb::GrantSpec sp = baseSpec();
    sp.mask |= FLEXPERM_SYS_CPU_STRESS;
    pkgb::Bytes good = pkgb::buildGrant(sp, gStore);
    pkgb::Bytes bad = pkgb::buildGrant(baseSpec(), gStore);
    memcpy(bad.data() + 224, good.data() + 224, 4);
    memcpy(bad.data() + 10, good.data() + 10, 2);
    CHECK(check(bad, e) == FLEXGRANT_ERR_SIGNATURE,
          "cuadrar mascara y contador a mano no cuela: la firma no cuadra");
  }
  { // un byte cualquiera de la zona firmada
    pkgb::Bytes g = pkgb::buildGrant(baseSpec(), gStore);
    g[100] ^= 0x01;
    CHECK(check(g, e) == FLEXGRANT_ERR_PACKAGE, "cambiar el id lo delata antes de la firma");
  }
  { // firma revuelta
    pkgb::Bytes g = pkgb::buildGrant(baseSpec(), gStore);
    g[240] ^= 0xFF;
    CHECK(check(g, e) == FLEXGRANT_ERR_SIGNATURE, "una firma alterada se rechaza");
  }
  { // longitud
    pkgb::Bytes g = pkgb::buildGrant(baseSpec(), gStore);
    g.pop_back();
    CHECK(check(g, e) == FLEXGRANT_ERR_SIZE, "un grant truncado se rechaza por tamano");
  }
  { // magia y version
    pkgb::Bytes g = pkgb::buildGrant(baseSpec(), gStore);
    g[3] = 'X';
    CHECK(check(g, e) == FLEXGRANT_ERR_MAGIC, "sin FLXG no hay grant");
    pkgb::GrantSpec sp = baseSpec(); sp.formatVersion = 2;
    CHECK(check(pkgb::buildGrant(sp, gStore), e) == FLEXGRANT_ERR_MAGIC, "otra version del formato se rechaza");
  }
  { // proposito
    pkgb::GrantSpec sp = baseSpec(); sp.purpose = 9;
    CHECK(check(pkgb::buildGrant(sp, gStore), e) == FLEXGRANT_ERR_PURPOSE,
          "un grant con otro proposito no abre permisos de app");
  }
}

static void testWrongTarget(){
  std::printf("-- de otra app, otra version, otro paquete, otro desarrollador --\n");
  FlexGrantExpect e = baseExpect();

  { pkgb::GrantSpec sp = baseSpec(); sp.packageId = "com.flexos.otra";
    CHECK(check(pkgb::buildGrant(sp, gStore), e) == FLEXGRANT_ERR_PACKAGE, "grant de OTRA app"); }
  { pkgb::GrantSpec sp = baseSpec(); sp.versionName = "1.3.0";
    CHECK(check(pkgb::buildGrant(sp, gStore), e) == FLEXGRANT_ERR_VERSION, "grant de OTRO nombre de version"); }
  { pkgb::GrantSpec sp = baseSpec(); sp.versionCode = kCode + 1;
    CHECK(check(pkgb::buildGrant(sp, gStore), e) == FLEXGRANT_ERR_VERSION, "grant de OTRO codigo de version"); }
  { pkgb::GrantSpec sp = baseSpec(); sp.packageSha256[5] ^= 0xFF;
    CHECK(check(pkgb::buildGrant(sp, gStore), e) == FLEXGRANT_ERR_HASH, "grant de OTRO paquete (hash distinto)"); }
  { pkgb::GrantSpec sp = baseSpec(); sp.developerFingerprint = gOther.fingerprint();
    CHECK(check(pkgb::buildGrant(sp, gStore), e) == FLEXGRANT_ERR_DEVELOPER, "grant de OTRO desarrollador"); }
  { // firmado por una clave que no es la de Flex Store
    CHECK(check(pkgb::buildGrant(baseSpec(), gOther), e) == FLEXGRANT_ERR_SIGNATURE,
          "firmado por OTRA clave: la clave pinneada es la unica que vale"); }
  { // el id correcto pero con relleno sucio detras
    pkgb::Bytes g = pkgb::buildGrant(baseSpec(), gStore);
    g[96 + strlen(kId)] = 'x';
    CHECK(check(g, e) == FLEXGRANT_ERR_PACKAGE, "el relleno del id tiene que estar limpio");
  }
}

static void testPermissionRules(){
  std::printf("-- reglas de permisos --\n");
  FlexGrantExpect e = baseExpect();

  { pkgb::GrantSpec sp = baseSpec(); sp.mask = 0;
    CHECK(check(pkgb::buildGrant(sp, gStore), e) == FLEXGRANT_ERR_PERMS, "un grant que no concede nada no tiene sentido"); }
  { pkgb::GrantSpec sp = baseSpec(); sp.mask = 0x80000000u;
    CHECK(check(pkgb::buildGrant(sp, gStore), e) == FLEXGRANT_ERR_PERMS, "un permiso que no existe se rechaza"); }
  { pkgb::GrantSpec sp = baseSpec(); sp.countOverride = 5;
    CHECK(check(pkgb::buildGrant(sp, gStore), e) == FLEXGRANT_ERR_PERMS, "mascara y contador tienen que cuadrar"); }
  { // el grant no puede conceder mas de lo que el manifest declara pedir
    FlexGrantExpect e2 = baseExpect();
    e2.manifestRequested = FLEXPERM_SYS_STORAGE_APP;     // el manifest solo pedia almacenamiento
    CHECK(check(pkgb::buildGrant(baseSpec(), gStore), e2) == FLEXGRANT_ERR_MANIFEST,
          "conceder mas de lo declarado se rechaza");
  }
  { // nombres canonicos <-> bits
    CHECK(flexSysPermissionBit("benchmark.run") == FLEXPERM_SYS_BENCHMARK_RUN, "benchmark.run");
    CHECK(flexSysPermissionBit("system.cpu.stress") == FLEXPERM_SYS_CPU_STRESS, "system.cpu.stress");
    CHECK(flexSysPermissionBit("system.psram.measure") == FLEXPERM_SYS_PSRAM_MEASURE, "system.psram.measure");
    CHECK(flexSysPermissionBit("system.temperature.read") == FLEXPERM_SYS_TEMPERATURE_READ, "system.temperature.read");
    CHECK(flexSysPermissionBit("system.performance.metrics") == FLEXPERM_SYS_PERF_METRICS, "system.performance.metrics");
    CHECK(flexSysPermissionBit("display.landscape") == FLEXPERM_SYS_DISPLAY_LANDSCAPE, "display.landscape");
    CHECK(flexSysPermissionBit("display.exclusive") == FLEXPERM_SYS_DISPLAY_EXCLUSIVE, "display.exclusive");
    CHECK(flexSysPermissionBit("storage.app") == FLEXPERM_SYS_STORAGE_APP, "storage.app");
    CHECK(flexSysPermissionBit("network") == 0, "network NO es un permiso de sistema de esta version");
    CHECK(flexSysPermissionBit("camera") == 0, "camera tampoco");
    CHECK(flexSysPermissionBit("bluetooth") == 0, "bluetooth tampoco");
    CHECK(flexSysPermissionBit("") == 0, "la cadena vacia no concede nada");
    CHECK(!strcmp(flexSysPermissionName(FLEXPERM_SYS_CPU_STRESS), "system.cpu.stress"), "bit -> nombre");
  }
}

static void testWindow(){
  std::printf("-- ventana de validez --\n");
  FlexGrantExpect e = baseExpect();

  { pkgb::GrantSpec sp = baseSpec(); sp.notBefore = 1000; sp.notAfter = 2000;
    FlexGrantExpect e2 = e; e2.nowEpoch = 1500;
    FlexGrantResult r;
    CHECK(check(pkgb::buildGrant(sp, gStore), e2, &r) == FLEXGRANT_OK && r.windowChecked == 1,
          "dentro de la ventana, y comprobado");
    e2.nowEpoch = 2500;
    CHECK(check(pkgb::buildGrant(sp, gStore), e2) == FLEXGRANT_ERR_WINDOW, "despues de notAfter, denegado");
    e2.nowEpoch = 500;
    CHECK(check(pkgb::buildGrant(sp, gStore), e2) == FLEXGRANT_ERR_WINDOW, "antes de notBefore, denegado");
    // Reloj no fiable: no se puede demostrar vigencia, asi que falla cerrado.
    e2.nowEpoch = 0;
    FlexGrantResult r2;
    CHECK(check(pkgb::buildGrant(sp, gStore), e2, &r2) == FLEXGRANT_ERR_WINDOW && r2.granted == 0,
          "sin hora fiable un grant temporal no obtiene privilegios");
  }
  { pkgb::GrantSpec sp = baseSpec(); sp.notBefore = 3000; sp.notAfter = 2000;
    CHECK(check(pkgb::buildGrant(sp, gStore), e) == FLEXGRANT_ERR_WINDOW, "una ventana imposible se rechaza"); }
}

// =============================================================
int main(){
  std::printf("\n=== FlexOS · permisos de sistema con grant firmado ===\n");
  gStore.generate();
  gOther.generate();
  gDev.generate();

  testValid();
  testAbsent();
  testTampered();
  testWrongTarget();
  testPermissionRules();
  testWindow();

  gStore.free_(); gOther.free_(); gDev.free_();
  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
