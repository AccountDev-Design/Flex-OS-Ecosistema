// #############################################################
//  Dobles de los modulos hermanos, para el enlazado de la prueba
//  ------------------------------------------------------------
//  FlexOS_Ultra.ino llama a tres modulos que viven en sus propios
//  .cpp: OTA, sistema de archivos y navegador. Esas unidades ya
//  tienen sus propias pruebas de host (test_app, test_bridge,
//  test_net, test_browser, test_jpeg); aqui solo hace falta que el
//  enlazador encuentre los simbolos para poder EJECUTAR las
//  comprobaciones del reloj que viven en el propio sketch.
//
//  Cada doble devuelve el valor "no disponible" del modulo real,
//  que es el mismo camino que toma la placa si el subsistema no
//  arranca. Nada de esto se compila para la placa.
// #############################################################
#include "Arduino.h"
#include "FlexOS_OTA.h"
#include "FlexOS_FS.h"
#include "FlexOS_Browser.h"
#include "FlexOS_Vault.h"

// ---- OTA ----
void        flexOtaBegin(){}
bool        flexOtaBusy(){ return false; }
bool        flexOtaOwnsScreen(){ return false; }
bool        flexOtaOverlayActive(){ return false; }
void        flexOtaRender(){}
void        flexOtaOpenSettings(){}
bool        flexOtaHandleTouch(FlexOtaTouch*){ return false; }
const char* flexOtaStatusText(){ return "Sin comprobar"; }

// ---- Sistema de archivos ----
bool        flexFsBegin(){ return false; }
// -------------------------------------------------------------
//  SISTEMA DE ARCHIVOS FALSO, PEQUENO Y CONTROLABLE
//  -------------------------------------------------------------
//  Los dobles de abajo devolvian siempre "no montado", que basta para
//  enlazar pero no para PROBAR la busqueda de archivos: sin ficheros no
//  hay nada que encontrar, ni limite que alcanzar, ni carpeta prohibida
//  que evitar.
//
//  Esto es un sistema de archivos de mentira de veinte lineas: una lista
//  de rutas con su contenido, que la prueba rellena a mano. No pretende
//  parecerse a LittleFS -- de eso ya se encarga la placa --, solo dar de
//  comer al codigo que se quiere probar.
// -------------------------------------------------------------
struct FakeFile { char path[64]; char body[256]; unsigned size; };
FakeFile gFakeFs[32];
int      gFakeFsN     = 0;
bool     gFakeFsReady = false;
void fakeFsReset(){ gFakeFsN = 0; gFakeFsReady = false; }
void fakeFsAdd(const char* path, const char* body, unsigned size){
  if(gFakeFsN >= 32) return;
  snprintf(gFakeFs[gFakeFsN].path, sizeof(gFakeFs[0].path), "%s", path);
  snprintf(gFakeFs[gFakeFsN].body, sizeof(gFakeFs[0].body), "%s", body ? body : "");
  gFakeFs[gFakeFsN].size = size ? size : (unsigned)strlen(body ? body : "");
  gFakeFsN++;
  gFakeFsReady = true;
}

bool        flexFsReady(){ return gFakeFsReady; }
bool        flexFsIsDir(const char*){ return false; }
uint32_t    flexFsSize(const char*){ return 0; }
bool        flexFsExists(const char*){ return false; }
const char* flexFsError(){ return "no montado"; }
uint32_t    flexFsUsedBytes(){ return 0; }
uint32_t    flexFsTotalBytes(){ return 0; }
uint32_t    flexFsDirSize(const char*){ return 0; }
uint32_t    flexFsCatSize(int){ return 0; }
int         flexFsList(const char* dir, FlexFsEntry* out, int maxn){
  if(!dir || !out || maxn <= 0) return 0;
  size_t dl = strlen(dir);
  int n = 0;
  for(int i = 0; i < gFakeFsN && n < maxn; i++){
    const char* p = gFakeFs[i].path;
    if(strncmp(p, dir, dl) != 0 || p[dl] != '/') continue;
    if(strchr(p + dl + 1, '/')) continue;              // sin subcarpetas
    snprintf(out[n].name, sizeof(out[n].name), "%s", p + dl + 1);
    out[n].size  = gFakeFs[i].size;
    out[n].dir   = false;
    out[n].items = 0;
    n++;
  }
  return n;
}
int         flexFsLargest(FlexFsBig*, int){ return 0; }
int         flexFsReadText(const char* path, char* out, size_t n){
  if(n) out[0] = 0;
  if(!path || !out || n == 0) return -1;
  for(int i = 0; i < gFakeFsN; i++){
    if(strcmp(gFakeFs[i].path, path)) continue;
    size_t l = strlen(gFakeFs[i].body);
    if(l > n - 1) l = n - 1;
    memcpy(out, gFakeFs[i].body, l); out[l] = 0;
    return (int)l;
  }
  return -1;
}
bool        flexFsWriteText(const char* path, const char* txt){
  if(!gFakeFsReady || !path) return false;
  for(int i = 0; i < gFakeFsN; i++){
    if(strcmp(gFakeFs[i].path, path)) continue;
    snprintf(gFakeFs[i].body, sizeof(gFakeFs[0].body), "%s", txt ? txt : "");
    gFakeFs[i].size = (unsigned)strlen(gFakeFs[i].body);
    return true;
  }
  fakeFsAdd(path, txt, 0);
  return true;
}
int         flexFsReadBin(const char*, void*, size_t){ return -1; }
bool        flexFsWriteBin(const char*, const void*, size_t){ return false; }
bool        flexFsDelete(const char*){ return false; }
bool        flexFsRename(const char*, const char*){ return false; }
bool        flexFsTrash(const char*){ return false; }
bool        flexFsRestore(const char*){ return false; }
bool        flexFsEmptyTrash(){ return false; }
bool        flexFsTrashOrigin(const char*, char* out, size_t n){ if(n) out[0] = 0; return false; }
void        flexFsFmtSize(uint32_t, char* out, size_t n){ if(n) snprintf(out, n, "0 B"); }
void        flexFsStem(const char*, char* out, size_t n){ if(n) out[0] = 0; }
bool        flexFsNewName(const char* dir, const char* base, const char* ext, char* out, size_t n){
  if(n) out[0] = 0;
  if(!gFakeFsReady || !dir || !base || !ext || n < 8) return false;
  snprintf(out, n, "%s/%s%s", dir, base, ext);
  return true;
}
bool        flexPaintCreate(const char*, uint16_t, uint16_t){ return false; }
bool        flexPaintAppend(const char*, uint16_t, uint8_t, const int16_t*, uint16_t){ return false; }
bool        flexPaintHeader(const char*, FlexPaintHdr*){ return false; }
bool        flexPaintReplay(const char*, float, int, int, FlexPaintSegCb, void*){ return false; }
bool        flexPaintUndo(const char*){ return false; }
bool        flexPaintClear(const char*){ return false; }

// ---- Navegador ----
void flexBrVersionGuard_v4_copia_los_4_ficheros_del_navegador(void){}
void flexBrowserBegin(){}
void flexBrowserEnter(){}
void flexBrowserTick(){}
void flexBrowserExit(){}
bool flexBrowserWantsClose(){ return false; }
bool flexBrowserHandleSystemBack(){ return false; }
void flexBrowserForceRepaint(){}
bool flexBrowserRepaintedFull(){ return false; }
void flexBrowserCancelDrag(){}
bool flexBrowserKeyboardOpen(){ return false; }
const char* flexBrowserEditText(){ return ""; }
const char* flexBrowserEditLabel(){ return ""; }
void flexBrowserKeyText(const char*){}
void flexBrowserKeyBackspace(){}
void flexBrowserKeyEnter(){}
void flexBrowserKeyCancel(){}

// ---- Flex Vault (Carpeta segura) ----
// Dobles del modulo real (FlexOS_Vault.cpp), que tiene su propia prueba
// con criptografia y NVS de verdad: tests/host/test_vault. Aqui solo
// hacen falta los simbolos para poder ENLAZAR y ejecutar las
// comprobaciones que viven dentro del sketch.
//
// Cada doble devuelve el estado "boveda no configurada / sin
// almacenamiento", que es el mismo camino que toma la placa cuando el
// usuario todavia no ha creado su Flex Vault. Asi las pantallas del
// sketch se ejercitan por su rama honesta y no por una inventada.
bool     flexVaultBegin(){ return false; }
bool     flexVaultExists(){ return false; }
bool     flexVaultUnlocked(){ return false; }
int      flexVaultLockType(){ return FLEXVAULT_LOCK_NONE; }
int      flexVaultSecretLen(){ return 0; }
uint32_t flexVaultAutoLockMs(){ return 60000; }
void     flexVaultSetAutoLockMs(uint32_t){}
uint32_t flexVaultLastAccess(){ return 0; }
int      flexVaultFails(){ return 0; }
uint32_t flexVaultWaitMs(){ return 0; }
uint32_t flexVaultUsedBytes(){ return 0; }
int      flexVaultCount(int){ return 0; }
const char* flexVaultError(){ return "sin almacenamiento"; }
int      flexVaultCreate(const char*, int){ return FXV_ERR_IO; }
int      flexVaultUnlock(const char*){ return FXV_ERR_STATE; }
void     flexVaultLock(int){}
int      flexVaultChangeSecret(const char*, const char*, int){ return FXV_ERR_STATE; }
int      flexVaultList(int, FlexVaultItem*, int){ return 0; }
int      flexVaultListFor(int, int, FlexVaultItem*, int){ return 0; }
int      flexVaultCountFor(int, int){ return 0; }
bool     flexVaultGet(uint16_t, FlexVaultItem*){ return false; }
bool     flexVaultImport(const char*, int, int){ return false; }
bool     flexVaultExport(uint16_t, char* out, size_t n){ if(out && n) out[0] = 0; return false; }
bool     flexVaultDelete(uint16_t){ return false; }
bool     flexVaultRename(uint16_t, const char*){ return false; }
bool     flexVaultCreateItem(const char*, int, int, uint16_t*){ return false; }
bool     flexVaultWrite(uint16_t, const void*, size_t){ return false; }
int      flexVaultRead(uint16_t, void*, size_t){ return -1; }
bool     flexVaultReadStream(uint16_t, FlexVaultChunkCb, void*){ return false; }
bool     flexVaultAppSupported(int appId){ return appId == 1 || appId == 3 || appId == 5; }
const char* flexVaultAppReason(int appId){ return flexVaultAppSupported(appId) ? 0 : "Esta app aun no es compatible con Carpeta segura"; }
bool     flexVaultAppForbidden(int appId){ return appId == 12 || appId == 4; }
bool     flexVaultAppAdded(int){ return false; }
bool     flexVaultAppAdd(int){ return false; }
bool     flexVaultAppRemove(int, int){ return false; }
bool     flexVaultAppLocked(int){ return false; }
void     flexVaultAppSetLocked(int, bool){}
uint32_t flexVaultAppBytes(int){ return 0; }
uint32_t flexVaultAppLast(int){ return 0; }
void     flexVaultAppTouch(int){}
int      flexVaultLogRead(FlexVaultLog*, int){ return 0; }
void     flexVaultLogAdd(uint8_t, int8_t){}
void     flexVaultSetClock(uint32_t){}
uint32_t flexVaultNow(){ return 0; }
void     flexVaultRandomBytes(void* out, size_t n){ if(out) memset(out, 0, n); }
void     flexVaultKdf(const char*, const uint8_t*, size_t, uint32_t, uint8_t* out, size_t n){ if(out) memset(out, 0, n); }
bool     flexVaultEqualCT(const void* a, const void* b, size_t n){ return memcmp(a, b, n) == 0; }
void     flexVaultWipe(void* p, size_t n){ if(p) memset(p, 0, n); }

// El bloqueo del sistema con hash y sal tambien vive en ese modulo.
// El doble se comporta como "sin clave configurada".
int      flexLockType(){ return 0; }
int      flexLockLen(){ return 0; }
bool     flexLockSet(const char*, int){ return false; }
bool     flexLockVerify(const char*){ return false; }
bool     flexLockClear(){ return false; }
int      flexLockMigrate(){ return 0; }

// Sistema de archivos: piezas nuevas que necesita la boveda.
int      flexFsReadAt(const char*, uint32_t, void*, size_t){ return -1; }
bool     flexFsAppendBin(const char*, const void*, size_t){ return false; }
bool     flexFsVaultInit(){ return false; }
bool     flexFsPrivExists(const char*){ return false; }
uint32_t flexFsPrivSize(const char*){ return 0; }
int      flexFsPrivRead(const char*, uint32_t, void*, size_t){ return -1; }
bool     flexFsPrivAppend(const char*, const void*, size_t){ return false; }
bool     flexFsPrivWrite(const char*, const void*, size_t){ return false; }
bool     flexFsPrivDelete(const char*){ return false; }
uint32_t flexFsPrivDirSize(const char*){ return 0; }
bool     flexFsIsVaultPath(const char* path){
  if(!path || path[0] != '/') return false;
  size_t vl = strlen(FLEXFS_DIR_VAULT);
  if(strncmp(path, FLEXFS_DIR_VAULT, vl) != 0) return false;
  return path[vl] == 0 || path[vl] == '/';
}
bool     flexPaintReplayMem(const void*, size_t, float, int, int, FlexPaintSegCb, void*){ return false; }
bool     flexPaintHeaderMem(const void*, size_t, FlexPaintHdr*){ return false; }
