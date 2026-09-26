// #############################################################
//  Dobles de los modulos hermanos, para el enlazado de la prueba
//  ------------------------------------------------------------
//  FlexOS_Ultra.ino llama a varios modulos que viven en sus propios
//  .cpp: OTA, sistema de archivos, navegador, boveda y clima. Esas
//  unidades ya
//  tienen sus propias pruebas de host (test_app, test_bridge,
//  test_net, test_browser, test_jpeg); aqui solo hace falta que el
//  enlazador encuentre los simbolos para poder EJECUTAR las
//  comprobaciones del reloj que viven en el propio sketch.
//
//  Cada doble devuelve el valor "no disponible" del modulo real,
//  que es el mismo camino que toma la placa si el subsistema no
//  arranca. Nada de esto se compila para la placa.
// #############################################################
// Antes que Arduino.h: sus macros min/max rompen las cabeceras de la STL.
#include <map>
#include <string>
#include <vector>
#include "Arduino.h"
#include "FlexOS_OTA.h"
#include "FlexOS_FS.h"
#include "FlexOS_Browser.h"
#include "FlexOS_Passcode.h"
#include "FlexOS_Weather.h"
#include "FlexOS_Audio.h"
#include "FlexOS_BNO085.h"
#include <string.h>

// ---- OTA ----
void        flexOtaBegin(){}
bool        flexOtaBusy(){ return false; }
// Conmutable desde las pruebas: hace falta para comprobar que el editor del
// Panel Rapido no se abre mientras la pantalla es del OTA.
bool        gFlexOtaOwns = false;
bool        flexOtaOwnsScreen(){ return gFlexOtaOwns; }
bool        flexOtaOverlayActive(){ return false; }
void        flexOtaRender(){}
void        flexOtaOpenSettings(){}
bool        flexOtaHandleTouch(FlexOtaTouch*){ return false; }
const char* flexOtaStatusText(){ return "Sin comprobar"; }
// Version local: la usa el subtitulo del control "Actualizaciones" del Panel Rapido.
const char* flexOtaLocalVersion(){ return "1.0.0"; }

// ---- Sistema de archivos ----
// DISCO EN MEMORIA, APAGADO POR DEFECTO. Las baterias que ya existian
// esperan "sin almacenamiento" y con gTestMemFs = false todo sigue fallando
// limpio, igual que antes. Una prueba que necesita archivos de verdad (el
// editor de la Galeria: abrir un JPEG, guardar una copia, reemplazar el
// original) lo enciende y mete ella misma los archivos. Mismas reglas que
// FlexOS_FS en la placa: mover no pisa un destino que ya existe, abrir para
// escribir crea o vacia, y escribir falla si no cabe (disco lleno).
bool gTestMemFs = false;
std::map<std::string, std::vector<uint8_t>> gTestFiles;
uint32_t gTestFsCap = 16u << 20;               // capacidad de la particion en memoria
long     gTestFsFailWriteAt = -1;              // >= 0: una escritura que pase de aqui falla
long     gTestFsWritten = 0;
struct FlexFsStream { std::string path; uint32_t pos; };
static uint32_t memFsUsed(){ uint32_t u = 0; for(auto& kv : gTestFiles) u += (uint32_t)kv.second.size(); return u; }
static bool memFsHas(const char* p){ return gTestMemFs && p && gTestFiles.count(p); }
bool        flexFsBegin(){ return false; }
// El almacenamiento tambien se puede mover desde las pruebas: por defecto sigue
// "no disponible", que es lo que esperaban las baterias que ya existian.
bool        gTestFsReady = false;
bool        flexFsReady(){ return gTestFsReady; }
bool        flexFsIsDir(const char*){ return false; }
uint32_t    flexFsSize(const char* p){ return memFsHas(p) ? (uint32_t)gTestFiles[p].size() : 0; }
bool        flexFsExists(const char* p){ return memFsHas(p); }
const char* flexFsError(){ return "no montado"; }
// Cuantas veces se pidio el espacio usado: en la placa es RECORRER LittleFS
// entero, asi que las pruebas de trabajo periodico lo cuentan.
unsigned gTestFsUsedCalls = 0;
uint32_t    flexFsUsedBytes(){ gTestFsUsedCalls++; return gTestMemFs ? memFsUsed() : 0; }
uint32_t    flexFsTotalBytes(){ return gTestMemFs ? gTestFsCap : 0; }
uint32_t    flexFsDirSize(const char*){ return 0; }
uint32_t    flexFsCatSize(int){ return 0; }
int         flexFsListFrom(const char* dir, FlexFsEntry* out, int maxn, int skip);
// Con el disco en memoria, lista de verdad (la Papelera del kit la usa).
int         flexFsList(const char* d, FlexFsEntry* o, int m){
  if(!gTestMemFs) return 0;
  int n = flexFsListFrom(d, o, m, 0);
  return n < 0 ? 0 : n;
}
int         flexFsListFrom(const char* dir, FlexFsEntry* out, int maxn, int skip){
  if(!gTestMemFs || !dir) return -1;
  std::string d = dir; if(d.empty() || d.back() != '/') d += '/';
  int n = 0, seen = 0;
  for(auto& kv : gTestFiles){
    if(kv.first.compare(0, d.size(), d) || kv.first.find('/', d.size()) != std::string::npos) continue;
    if(seen++ < skip) continue;
    if(n >= maxn) break;
    memset(&out[n], 0, sizeof(out[n]));
    snprintf(out[n].name, sizeof(out[n].name), "%s", kv.first.c_str() + d.size());
    out[n].size = (uint32_t)kv.second.size();
    n++;
  }
  return n;
}
int         flexFsLargest(FlexFsBig*, int){ return 0; }
int         flexFsReadText(const char*, char* out, size_t n){ if(n) out[0] = 0; return -1; }
bool        flexFsWriteText(const char*, const char*){ return false; }
int         flexFsReadBin(const char* p, void* b, size_t n){
  if(!memFsHas(p)) return -1;
  auto& f = gTestFiles[p];
  size_t k = f.size() < n ? f.size() : n;
  if(k) memcpy(b, f.data(), k);
  return (int)k;
}
bool        flexFsWriteBin(const char*, const void*, size_t){ return false; }
bool        flexFsDelete(const char* p){ return memFsHas(p) && gTestFiles.erase(p) == 1; }
bool        flexFsRename(const char*, const char*){ return false; }
// Con el disco en memoria, a "/Papelera" con la ruta de origen en el nombre
// (como el de verdad, sin su codificacion exacta). Sin el, falla limpio.
bool        flexFsTrash(const char* p){
  if(!memFsHas(p) || !strncmp(p, "/Papelera", 9)) return false;
  std::string dst = "/Papelera/";
  for(const char* c = p + 1; *c; c++) dst += (*c == '/') ? '!' : *c;
  while(gTestFiles.count(dst)) dst += "~";
  gTestFiles[dst] = std::move(gTestFiles[p]);
  gTestFiles.erase(p);
  return true;
}
bool        flexFsRestore(const char*){ return false; }
bool        flexFsEmptyTrash(){ return false; }
bool        flexFsTrashOrigin(const char*, char* out, size_t n){ if(n) out[0] = 0; return false; }
void        flexFsFmtSize(uint32_t, char* out, size_t n){ if(n) snprintf(out, n, "0 B"); }
void        flexFsStem(const char*, char* out, size_t n){ if(n) out[0] = 0; }
bool        flexFsNewName(const char*, const char*, const char*, char* out, size_t n){ if(n) out[0] = 0; return false; }
bool        flexPaintCreate(const char*, uint16_t, uint16_t){ return false; }
bool        flexPaintAppend(const char*, uint16_t, uint8_t, const int16_t*, uint16_t){ return false; }
bool        flexPaintHeader(const char*, FlexPaintHdr*){ return false; }
bool        flexPaintReplay(const char*, float, int, int, FlexPaintSegCb, void*){ return false; }
bool        flexPaintUndo(const char*){ return false; }
bool        flexPaintClear(const char*){ return false; }

// ---- Navegador ----
void flexBrVersionGuard_v4_copia_los_4_ficheros_del_navegador(void){}
// flexBrSource* NO se doblan: son NUCLEO PURO de FlexOS_Browser.cpp y
// entran de verdad en el enlace (abajo, en la regla de test_ino). Lo
// que si necesita doble es el ACCESOR de ajustes, porque vive en
// FlexOS_BrowserApp.cpp, que aqui no se compila.
// Los ajustes del doble arrancan en sus valores de fabrica: fuente
// Automatico y sin ningun servidor configurado. Es a proposito -- asi
// la seccion Navegador de Flex Phone se ejercita en el caso "sin
// fuente disponible", que es el que tiene que decir la verdad.
static BrSettings gStubBrSettings;
static bool gStubBrSettingsInit = false;
const BrSettings* flexBrowserSettings(){
  if(!gStubBrSettingsInit){ flexBrSettingsDefaults(&gStubBrSettings); gStubBrSettingsInit = true; }
  return &gStubBrSettings;
}

// El doble SI guarda la fuente: la seccion Navegador de Flex Phone la
// cambia, y un doble que ignorara el cambio haria pasar la compilacion
// de algo que en la placa no seleccionaria nada.
void flexBrowserSetSource(uint8_t src){
  if(src >= BRSRC_N) return;
  if(!gStubBrSettingsInit){ flexBrSettingsDefaults(&gStubBrSettings); gStubBrSettingsInit = true; }
  gStubBrSettings.source = src;
}

void flexBrowserBegin(){}
void flexBrowserEnter(){}
void flexBrowserTick(){}
void flexBrowserExit(){}
// Multitarea: suspender/reanudar sin reiniciar la sesion. Aqui son dobles,
// como el resto del navegador; su comportamiento real lo prueba test_app.
void flexBrowserSuspend(){}
void flexBrowserResume(){}
bool flexBrowserActive(){ return false; }
size_t flexBrowserReleaseVisualCache(){ return 0; }
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


// Sistema de archivos: lectura parcial y limpieza de restos.
// FlexOS_Passcode.cpp entra en el enlace como CODIGO REAL (ver el Makefile),
// asi que la clave del sistema NO lleva doble: el sketch se enlaza contra el
// mismo PBKDF2 a plazos que corre en la placa.
// Con el disco en memoria (gTestMemFs) lee de verdad: es lo que usa el lector
// de video del visor (MediaStream). Sin el, falla limpio como sin montar.
int      flexFsReadAt(const char* p, uint32_t off, void* b, size_t n){
  if(!memFsHas(p)) return -1;
  auto& f = gTestFiles[p];
  if(off >= f.size()) return 0;
  size_t k = f.size() - off < n ? f.size() - off : n;
  memcpy(b, f.data() + off, k);
  return (int)k;
}
// Flujos y movimientos (subidas del movil, biblioteca de medios). Sin
// sistema de archivos de verdad aqui: todo falla limpio, como sin montar.
FlexFsStream* flexFsOpenRead(const char* p){ return memFsHas(p) ? new FlexFsStream{ p, 0 } : nullptr; }
FlexFsStream* flexFsOpenWrite(const char* p){
  if(!gTestMemFs || !p || p[0] != '/') return nullptr;
  gTestFiles[p].clear();                        // crea o vacia, como LittleFS con "w"
  return new FlexFsStream{ p, 0 };
}
int      flexFsStreamRead(FlexFsStream* s, void* b, size_t n){
  if(!s || !gTestFiles.count(s->path)) return -1;
  auto& f = gTestFiles[s->path];
  size_t k = s->pos >= f.size() ? 0 : (f.size() - s->pos < n ? f.size() - s->pos : n);
  if(k) memcpy(b, f.data() + s->pos, k);
  s->pos += (uint32_t)k;
  return (int)k;
}
bool     flexFsStreamWrite(FlexFsStream* s, const void* b, size_t n){
  if(!s || !gTestFiles.count(s->path)) return false;
  if(memFsUsed() + n > gTestFsCap) return false;                          // disco lleno
  if(gTestFsFailWriteAt >= 0 && gTestFsWritten + (long)n > gTestFsFailWriteAt) return false;
  auto& f = gTestFiles[s->path];
  if(s->pos + n > f.size()) f.resize(s->pos + n);
  memcpy(f.data() + s->pos, b, n);
  s->pos += (uint32_t)n; gTestFsWritten += (long)n;
  return true;
}
bool     flexFsStreamSeek(FlexFsStream* s, uint32_t off){
  if(!s || !gTestFiles.count(s->path) || off > gTestFiles[s->path].size()) return false;
  s->pos = off; return true;
}
uint32_t flexFsStreamSize(FlexFsStream* s){ return (s && gTestFiles.count(s->path)) ? (uint32_t)gTestFiles[s->path].size() : 0; }
void     flexFsStreamClose(FlexFsStream* s){ delete s; }
bool     flexFsMove(const char* a, const char* b){
  if(!gTestMemFs || !a || !b || b[0] != '/') return false;
  if(!strcmp(a, b)) return true;
  if(!gTestFiles.count(a) || gTestFiles.count(b)) return false;          // no se pisa nada
  gTestFiles[b] = std::move(gTestFiles[a]);
  gTestFiles.erase(a);
  return true;
}
bool     flexFsPurgeLegacyVault(){ return false; }

// ---- Clima (motor meteorologico) ----
// El doble devuelve "sin datos", que es EXACTAMENTE el camino que toma la
// placa cuando aun no hay descarga valida: asi la prueba recorre el estado
// vacio de la app y de los dos widgets. El motor de verdad tiene su propia
// bateria (test_weather).
void               flexWeatherBegin(){}
void               flexWeatherTick(bool){}
void               flexWeatherRefresh(bool){}
void               flexWeatherSetClock(uint32_t){}
// DOBLE CONTROLABLE. Por defecto sigue siendo "sin datos" -- el camino que
// toma la placa cuando aun no hay descarga valida, y el que recorrian todas las
// pruebas que ya existian --, pero una prueba puede rellenar gTestWx y encender
// gTestWxOn para ejercitar la app con un pronostico de verdad: las tarjetas, el
// horario, los siete dias y la escena. Sin esto no se puede medir en el PC lo
// que de verdad cuesta un cuadro de Clima.
FlexWeather gTestWx;
bool        gTestWxOn = false;
const FlexWeather* flexWeatherData(){ return gTestWxOn ? &gTestWx : nullptr; }
uint32_t           flexWeatherGen(){ return 0; }
uint8_t            flexWeatherStatus(){ return WXS_NEVER; }
uint8_t            flexWeatherError(){ return WXE_NONE; }
bool               flexWeatherBusy(){ return false; }
int32_t            flexWeatherAgeSec(){ return -1; }
int32_t            flexWeatherNowLocal(){ return 0; }
uint8_t            flexWeatherLocCount(){ return 0; }
const FlexWxLoc*   flexWeatherLocAt(uint8_t){ return nullptr; }
int8_t             flexWeatherLocSel(){ return -1; }
void               flexWeatherLocSelect(uint8_t){}
bool               flexWeatherLocAdd(const FlexWxLoc*){ return false; }
void               flexWeatherLocRemove(uint8_t){}
void               flexWeatherSearch(const char*, int){}
uint8_t            flexWeatherSearchState(){ return WXQ_IDLE; }
uint8_t            flexWeatherSearchCount(){ return 0; }
const FlexWxLoc*   flexWeatherSearchAt(uint8_t){ return nullptr; }
void               flexWeatherSearchClear(){}
uint8_t            flexWeatherVisual(int){ return WXV_CLOUDY; }
const char*        flexWeatherCondName(int, int){ return "Nublado"; }
const char*        flexWeatherErrorText(uint8_t, int){ return ""; }
void               flexWeatherDescribe(char* out, size_t n, int){ if(out && n) out[0] = 0; }

// ---- Flex Package / Flex Store / Flex UI Runtime / Flex Account ----
// Los cuatro modulos que Flex Store necesita para ENLAZAR. Se anadieron al
// sketch en da688a0 y 4741867 sin sus dobles, y desde entonces `make` no
// llegaba a construir test_ino: solo pasaba `make ino` (sintaxis). Estos
// dobles devuelven el camino "modulo no disponible", igual que el resto del
// fichero, y ademas dejan que la prueba CONTROLE sus entradas y OBSERVE lo
// que el puente les pide. Eso ultimo es lo que permite verificar de verdad
// que un toque sobre una fila FILTRADA instala el indice REAL del catalogo y
// abre el identificador REAL, y no el de la posicion tocada.
//
// La criptografia, LittleFS y TLS de verdad viven en los .cpp, que solo se
// compilan para la placa: aqui no se simulan ni se dan por probados.
#include "FlexOS_Package.h"
#include "FlexOS_Runtime.h"
#include "FlexOS_Store.h"
#include "FlexOS_Account.h"

FlexStoreItem  gStubCatalog[FLEXSTORE_MAX_CATALOG];
int            gStubCatalogN = 0;
FlexPkgInfo    gStubInstalled[FLEXPKG_MAX_INSTALLED];
int            gStubInstalledN = 0;
int            gStubInstallIndex = -1;                      // ultimo flexStoreInstall()
char           gStubRuntimeId[FLEXPKG_ID_MAX + 1] = "";     // ultimo flexRuntimeLoad()
char           gStubUninstallId[FLEXPKG_ID_MAX + 1] = "";   // ultimo flexPkgUninstall()
int            gStubPkgListCalls = 0;                       // relecturas de la lista instalada
uint32_t       gStubPkgRevision = 1;                        // revision del registro (ver flexPkgRevision)
int            gStubCatalogItemCalls = 0;                   // copias de FlexStoreItem servidas
bool           gStubRuntimeOk = true;
bool           gStubStoreCancelled = false;
FlexStoreState gStubStoreState = FLEXSTORE_READY;
uint8_t        gStubStoreProgress = 100;
bool           gStubAccountLinked = false;
FlexAccountSnapshot gStubAccountSnap;

// -- Flex Package --
bool flexPkgBegin(){ return false; }
bool flexPkgInspect(const char*, FlexPkgInfo*, FlexPkgProgressFn, void*){ return false; }
bool flexPkgInstall(const char*, FlexPkgInfo*, FlexPkgProgressFn, void*){ return false; }
bool flexPkgUninstall(const char* packageId){
  snprintf(gStubUninstallId, sizeof(gStubUninstallId), "%s", packageId ? packageId : "");
  for(int i = 0; i < gStubInstalledN; i++){
    if(strcmp(gStubInstalled[i].id, gStubUninstallId)) continue;
    for(int j = i; j + 1 < gStubInstalledN; j++) gStubInstalled[j] = gStubInstalled[j + 1];
    gStubInstalledN--; gStubPkgRevision++; return true;
  }
  return false;
}
// Igual que en la placa: solo sube cuando la lista PUDO cambiar de verdad.
uint32_t flexPkgRevision(){ return gStubPkgRevision; }
int flexPkgList(FlexPkgInfo* out, int maxItems){
  gStubPkgListCalls++;
  int n = 0;
  for(int i = 0; i < gStubInstalledN && n < maxItems; i++) out[n++] = gStubInstalled[i];
  return n;
}
bool flexPkgGet(const char* packageId, FlexPkgInfo* out){
  if(!packageId || !out) return false;
  for(int i = 0; i < gStubInstalledN; i++)
    if(!strcmp(gStubInstalled[i].id, packageId)){ *out = gStubInstalled[i]; return true; }
  return false;
}
bool flexPkgEntryPath(const char*, char* out, size_t n){ if(out && n) out[0] = 0; return false; }
// Registro ampliado: grant firmado, carpeta privada y estado detenido/activo.
// Los dobles devuelven "no hay nada", que es el mismo camino que toma la placa
// cuando la app no trae permisos o el almacenamiento no esta montado.
uint32_t flexPkgGrant(const char*, uint8_t*, uint32_t){ return 0; }
bool flexPkgDataDir(const char*, char* out, size_t n){ if(out && n) out[0] = 0; return false; }
bool flexPkgDataEnsure(const char*){ return false; }
uint32_t flexPkgDataBytes(const char*){ return 0; }
bool flexPkgSetState(const char*, FlexPkgAppState){ return false; }
bool flexPkgActiveRoot(const char*, char* out, size_t n){ if(out && n) out[0] = 0; return false; }
FlexPkgErrorCode flexPkgErrorCode(){ return FLEXPKG_ERR_FS; }
const char* flexPkgError(){ return "sin almacenamiento"; }
bool flexPkgBusy(){ return false; }
void flexPkgCancel(){}

// -- Flex Store --
void flexStoreBegin(){}
bool flexStoreRefresh(){ return false; }
bool flexStoreInstall(int catalogIndex){ gStubInstallIndex = catalogIndex; return true; }
void flexStoreCancel(){ gStubStoreCancelled = true; }
FlexStoreState flexStoreState(){ return gStubStoreState; }
uint8_t flexStoreProgress(){ return gStubStoreProgress; }
const char* flexStoreStage(){ return "Listo"; }
const char* flexStoreError(){ return "sin conexion"; }
int flexStoreCatalogCount(){ return gStubCatalogN; }
bool flexStoreCatalogItem(int index, FlexStoreItem* out){
  if(!out || index < 0 || index >= gStubCatalogN) return false;
  gStubCatalogItemCalls++; *out = gStubCatalog[index]; return true;
}
// Paquete que la tienda esta descargando o instalando ahora mismo. Las pruebas
// lo fijan para comprobar el estado "actualizando" de la Caja de aplicaciones.
char gStubStoreBusyId[FLEXPKG_ID_MAX + 1] = "";
int            gStubStoreBusyCalls = 0;   // consultas desde el pintado (ver testCajaDescargadasRegresion)
void flexStoreBusyPackage(char* out, size_t outSize){
  gStubStoreBusyCalls++;
  if(!out || outSize == 0) return;
  out[0] = 0;
  if(gStubStoreState != FLEXSTORE_DOWNLOADING && gStubStoreState != FLEXSTORE_INSTALLING) return;
  snprintf(out, outSize, "%s", gStubStoreBusyId);
}

bool flexStoreHasUpdate(const FlexStoreItem* item, FlexPkgInfo* installed){
  if(!item || !item->packageId[0]) return false;
  FlexPkgInfo local;
  if(!flexPkgGet(item->packageId, &local)) return false;
  if(installed) *installed = local;
  return item->versionCode > local.versionCode;
}

// -- Flex UI Runtime --
bool flexRuntimeLoad(const char* packageId, FlexRuntimeApp* app){
  snprintf(gStubRuntimeId, sizeof(gStubRuntimeId), "%s", packageId ? packageId : "");
  if(app){ memset(app, 0, sizeof(*app)); app->loaded = gStubRuntimeOk; }
  return gStubRuntimeOk;
}
bool flexRuntimeNavigate(FlexRuntimeApp*, const char*){ return false; }
bool flexRuntimeHit(const FlexRuntimeApp*, int, int, FlexUiAction* action){
  if(action){ action->type = FLEXUI_ACTION_NONE; action->value[0] = 0; }
  return false;
}
void flexRuntimeUnload(FlexRuntimeApp* app){ if(app) memset(app, 0, sizeof(*app)); }
const char* flexRuntimeError(){ return "paquete no disponible"; }

// -- Flex Account --
void flexAccountBegin(){}
bool flexAccountRequestCode(const char*){ return false; }
void flexAccountCancel(){}
bool flexAccountLinked(){ return gStubAccountLinked; }
FlexAccountState flexAccountState(){ return gStubAccountSnap.state; }
void flexAccountSnapshot(FlexAccountSnapshot* out){
  if(!out) return;
  *out = gStubAccountSnap;
  out->linked = gStubAccountLinked;
}
bool flexAccountCopyBearer(char* out, size_t n){ if(out && n) out[0] = 0; return false; }
void flexAccountForgetLocal(){}

// -- Flex FS: anadidos por "secure reset and app continuity" --
// El .ino ya llamaba a estas cuatro; sin sus dobles, test_ino no ENLAZA
// (el -fsyntax-only de `make ino` no lo detecta). Mismo criterio que el
// resto del fichero: contestan "no disponible", que es lo que ve el
// sketch cuando la particion interna no esta montada.
int      flexFsCount(const char* dir){
  if(!gTestMemFs || !dir) return 0;
  std::string d = dir; if(d.empty() || d.back() != '/') d += '/';
  int n = 0;
  for(auto& kv : gTestFiles)
    if(!kv.first.compare(0, d.size(), d) && kv.first.find('/', d.size()) == std::string::npos) n++;
  return n;
}
bool     flexFsMkdir(const char*){ return gTestMemFs; }   // en memoria no hay carpetas: siempre "existen"
bool     flexFsWriteBinAtomic(const char*, const void*, size_t){ return false; }
bool     flexFsFactoryErase(){ return false; }

// ---- Audio (FlexOS_Audio.cpp) ----
// Doble con el codec AUSENTE: es el estado en el que la interfaz NO
// debe dibujar ningun control de sonido. Que las pruebas corran por
// aqui comprueba precisamente eso.
bool        flexAudioBegin(){ return false; }
// Las capturas de Musica (INO_SHOTS) lo encienden para ver el reproductor
// completo; ninguna prueba reproduce nada.
bool        gStubAudioOk = false;
bool        flexAudioAvailable(){ return gStubAudioOk; }
const char* flexAudioError(){ return "Sin codec de audio"; }
bool        flexAudioStartPcm(uint32_t, uint16_t, uint16_t){ return false; }
bool        flexAudioStartPcmBuffered(uint32_t, uint16_t, uint16_t, uint16_t){ return false; }
uint32_t    flexAudioBufferMs(){ return 0; }
int         flexAudioWrite(const void*, size_t){ return -1; }
void        flexAudioStop(){}
bool        flexAudioPlaying(){ return false; }
void        flexAudioSetVolume(uint8_t){}
uint8_t     flexAudioVolume(){ return FLEXAUDIO_VOL_DEF; }
void        flexAudioSetMuted(bool){}
bool        flexAudioMuted(){ return false; }

// ---- IMU GY-BNO085 (FlexOS_BNO085.cpp) ----
// Doble con el modulo AUSENTE: es el estado en el que Flex Device Care
// TIENE que ensenar el requisito de hardware (con su grafico) y en el
// que la deteccion de caidas no puede activarse. Que las pruebas corran
// por aqui comprueba precisamente eso. El driver de verdad habla por el
// mismo Wire que el tactil, asi que no tiene sentido enlazarlo contra el
// bus simulado: su logica de protocolo se verifica en placa.
bool        flexBnoBegin(){ return false; }
void        flexBnoTick(uint32_t){}
void        flexBnoRescan(){}
void        flexBnoStop(){}
int         flexBnoState(){ return FLEXBNO_ST_ABSENT; }
bool        flexBnoPresent(){ return false; }
bool        flexBnoAvailable(){ return false; }
uint8_t     flexBnoChecks(){ return 0; }
uint8_t     flexBnoAddr(){ return 0; }
const char* flexBnoError(){ return "No hay ningun modulo IMU en el bus I2C"; }
uint8_t     flexBnoSwMajor(){ return 0; }
uint8_t     flexBnoSwMinor(){ return 0; }
uint32_t    flexBnoSwPart(){ return 0; }
bool        flexBnoAccel(float*){ return false; }
bool        flexBnoGyro(float*){ return false; }
bool        flexBnoMag(float*){ return false; }
bool        flexBnoQuat(float*){ return false; }
uint8_t     flexBnoAccelAcc(){ return 0xFF; }
uint8_t     flexBnoGyroAcc(){ return 0xFF; }
uint8_t     flexBnoMagAcc(){ return 0xFF; }
uint8_t     flexBnoFusionAcc(){ return 0xFF; }
uint32_t    flexBnoLastReportAge(uint32_t){ return 0xFFFFFFFFu; }
uint32_t    flexBnoReportCount(){ return 0; }
bool        flexBnoEuler(float*){ return false; }
