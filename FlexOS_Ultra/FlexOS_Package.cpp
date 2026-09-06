// #############################################################
//  FLEX OS · INSTALADOR DE .flexpkg SOBRE LittleFS
//  ------------------------------------------------------------
//  QUE HACE ESTE ARCHIVO Y QUE NO.
//    SI: montar, recuperar transacciones a medias, crear el slot
//        temporal, escribir, cambiar de versión con rename, revertir,
//        registrar la app instalada y borrarla.
//    NO: decidir si un paquete es de fiar. Eso vive en
//        FlexOS_PkgCore.cpp, que es lógica PURA y se ejercita en el PC
//        con sanitizers (tests/host/test_pkgcore.cpp). Antes estaba
//        aquí dentro, atado a LittleFS, y por tanto sin pruebas.
//
//  TRANSACCION (igual que antes, más el registro):
//    1. Validar y extraer a  /FlexApps/<id>/.stage
//    2. Comprobar el entrypoint del runtime que declare el manifest
//    3. rename(active -> .old) ; rename(.stage -> active)
//    4. Si el paso 3 falla a mitad, se restaura .old
//    5. Si sale bien, se borra .old: sólo queda la versión reciente
//  flexPkgBegin() repara al arrancar cualquier corte de corriente que
//  haya pillado la transacción por la mitad.
//
//  CARPETA PRIVADA. /FlexApps/<id>/data vive FUERA de "active", así que
//  una actualización la conserva y una desinstalación se la lleva.
// #############################################################
#include "FlexOS_Package.h"
#include "FlexOS_PkgCore.h"
#include "FlexOS_AppVM.h"
#include "FlexOS_AppGrant.h"

#include <FS.h>
#include <LittleFS.h>
#include <cJSON.h>
#include <stdlib.h>
#include <string.h>

#include "FlexOS_OTA.h"

namespace {

static const char* ROOT_DIR = "/FlexApps";
static const char* MANIFEST_FILE = "/.manifest.json";
static const char* INDEX_FILE = "/.index.json";
static const char* HASH_FILE = "/.pkghash";
static const char* GRANT_FILE = "/.grant";
static const char* STATE_FILE = "/.state";
static const char* STAGE_NAME = "/.stage";
static const char* ACTIVE_NAME = "/active";
static const char* OLD_NAME = "/.old";
static const char* DATA_NAME = "/data";

static volatile bool gBusy = false;
static volatile bool gCancel = false;
static FlexPkgErrorCode gErr = FLEXPKG_OK;
// Revision del registro de apps instaladas. Sube UNA vez por operacion que
// pueda cambiar la lista; nunca por lectura. Ver flexPkgRevision().
static uint32_t gRevision = 1;
static inline void bumpRevision(){ gRevision++; }
static char gErrText[128] = "Correcto";

static void setError(FlexPkgErrorCode code, const char* text){
  gErr = code;
  snprintf(gErrText, sizeof(gErrText), "%s", text ? text : "Error de paquete");
}

static bool readExact(File& f, void* dst, size_t n){
  uint8_t* p = static_cast<uint8_t*>(dst);
  size_t done = 0;
  while(done < n){
    size_t got = f.read(p + done, n - done);
    if(got == 0) return false;
    done += got;
  }
  return true;
}

static bool mkdirOne(const char* path){
  return LittleFS.exists(path) || LittleFS.mkdir(path);
}

static bool mkdirs(const char* path){
  if(!path || path[0] != '/') return false;
  char tmp[FLEXPKG_PATH_MAX + FLEXPKG_ID_MAX + 40];
  size_t n = strlen(path);
  if(n >= sizeof(tmp)) return false;
  memcpy(tmp, path, n + 1);
  for(size_t i = 1; i < n; i++){
    if(tmp[i] == '/'){
      tmp[i] = 0;
      if(!mkdirOne(tmp)) return false;
      tmp[i] = '/';
    }
  }
  return mkdirOne(tmp);
}

static const char* baseName(const char* path){
  const char* p = path ? strrchr(path, '/') : nullptr;
  return p ? p + 1 : (path ? path : "");
}

static bool removeTree(const char* path, uint8_t depth = 0){
  if(!path || !path[0] || !strcmp(path, "/") || depth > 12) return false;
  File f = LittleFS.open(path, "r");
  if(!f) return !LittleFS.exists(path);
  if(!f.isDirectory()){
    f.close();
    return LittleFS.remove(path);
  }
  File e = f.openNextFile();
  while(e){
    char child[FLEXPKG_PATH_MAX + FLEXPKG_ID_MAX + 48];
    snprintf(child, sizeof(child), "%s/%s", path, baseName(e.name()));
    bool dir = e.isDirectory();
    e.close();
    if(dir) removeTree(child, depth + 1);
    else LittleFS.remove(child);
    e = f.openNextFile();
  }
  f.close();
  return LittleFS.rmdir(path);
}

static uint32_t dirBytes(const char* path, uint8_t depth = 0){
  if(depth > 12) return 0;
  File f = LittleFS.open(path, "r");
  if(!f) return 0;
  if(!f.isDirectory()){
    uint32_t n = (uint32_t)f.size();
    f.close();
    return n;
  }
  uint32_t total = 0;
  File e = f.openNextFile();
  while(e){
    char child[FLEXPKG_PATH_MAX + FLEXPKG_ID_MAX + 48];
    snprintf(child, sizeof(child), "%s/%s", path, baseName(e.name()));
    bool dir = e.isDirectory();
    uint32_t n = dir ? 0 : (uint32_t)e.size();
    e.close();
    total += dir ? dirBytes(child, depth + 1) : n;
    e = f.openNextFile();
  }
  f.close();
  return total;
}

static bool appPaths(const char* id, char* root, char* active, char* stage, char* old, size_t cap){
  if(!flexPkgCoreSafeId(id)) return false;
  snprintf(root, cap, "%s/%s", ROOT_DIR, id);
  snprintf(active, cap, "%s%s", root, ACTIVE_NAME);
  snprintf(stage, cap, "%s%s", root, STAGE_NAME);
  snprintf(old, cap, "%s%s", root, OLD_NAME);
  return true;
}

static bool writeBlob(const char* dir, const char* suffix, const void* data, size_t n){
  char path[FLEXPKG_PATH_MAX + FLEXPKG_ID_MAX + 48];
  snprintf(path, sizeof(path), "%s%s", dir, suffix);
  File f = LittleFS.open(path, "w");
  if(!f) return false;
  bool ok = (n == 0) || (f.write((const uint8_t*)data, n) == n);
  f.close();
  if(!ok) LittleFS.remove(path);
  return ok;
}

static uint32_t readBlob(const char* dir, const char* suffix, void* out, uint32_t cap){
  char path[FLEXPKG_PATH_MAX + FLEXPKG_ID_MAX + 48];
  snprintf(path, sizeof(path), "%s%s", dir, suffix);
  File f = LittleFS.open(path, "r");
  if(!f) return 0;
  uint32_t n = (uint32_t)f.size();
  if(n == 0 || n > cap){ f.close(); return 0; }
  bool ok = readExact(f, out, n);
  f.close();
  return ok ? n : 0;
}

// ---- Lector del paquete (LittleFS) -------------------------------------
struct FileReader {
  File f;
  uint32_t pos = 0;
};

static bool fileRead(void* user, uint32_t off, void* dst, uint32_t n){
  FileReader* r = (FileReader*)user;
  if(!r->f) return false;
  if(r->pos != off){
    if(!r->f.seek(off, SeekSet)) return false;
    r->pos = off;
  }
  if(!readExact(r->f, dst, n)) return false;
  r->pos += n;
  return true;
}

// ---- Sumidero de extracción (LittleFS) ---------------------------------
struct StageSink {
  char stage[FLEXPKG_PATH_MAX + FLEXPKG_ID_MAX + 48];
  File out;
};

static bool ensureParent(const char* path){
  char dir[FLEXPKG_PATH_MAX + FLEXPKG_ID_MAX + 48];
  size_t n = strlen(path);
  if(n >= sizeof(dir)) return false;
  memcpy(dir, path, n + 1);
  char* slash = strrchr(dir, '/');
  if(!slash || slash == dir) return true;
  *slash = 0;
  return mkdirs(dir);
}

static bool sinkBegin(void* user, const char* rel){
  StageSink* s = (StageSink*)user;
  if(s->out) s->out.close();
  char dst[FLEXPKG_PATH_MAX + FLEXPKG_ID_MAX + 48];
  int n = snprintf(dst, sizeof(dst), "%s/%s", s->stage, rel);
  if(n <= 0 || (size_t)n >= sizeof(dst)) return false;
  if(!ensureParent(dst)) return false;
  s->out = LittleFS.open(dst, "w");
  return (bool)s->out;
}

static bool sinkWrite(void* user, const uint8_t* data, uint32_t n){
  StageSink* s = (StageSink*)user;
  return s->out && s->out.write(data, n) == n;
}

static bool sinkEnd(void* user, bool ok){
  StageSink* s = (StageSink*)user;
  if(s->out) s->out.close();
  return ok;
}

// ---- Comprobación del entrypoint según el runtime -----------------------
// No basta con que el archivo exista: se abre y se mira que sea del formato
// que dice el manifest. Un paquete cuyo entry no arranca no se activa.
static bool smokeEntrypoint(const char* stage, const FlexPkgInfo& info){
  char path[FLEXPKG_PATH_MAX + FLEXPKG_ID_MAX + 48];
  snprintf(path, sizeof(path), "%s/%s", stage, info.entry);
  File f = LittleFS.open(path, "r");
  if(!f) return false;
  size_t n = f.size();
  if(n < 2){ f.close(); return false; }

  if(info.runtime == FLEXPKG_RT_APP1){
    // flex-app-v1: cabecera FLXB v1 y tamaño coherente con lo que declara.
    if(n < 48 || n > (size_t)FLEXVM_MAX_CODE + FLEXVM_MAX_CONST + 64u * 1024u){ f.close(); return false; }
    uint8_t h[48];
    bool ok = readExact(f, h, sizeof(h));
    f.close();
    if(!ok) return false;
    if(memcmp(h, "FLXB", 4) != 0) return false;
    uint16_t ver = (uint16_t)h[4] | ((uint16_t)h[5] << 8);
    if(ver != FLEXVM_FORMAT_VERSION) return false;
    uint32_t codeLen  = (uint32_t)h[8]  | ((uint32_t)h[9] << 8)  | ((uint32_t)h[10] << 16) | ((uint32_t)h[11] << 24);
    uint32_t constLen = (uint32_t)h[12] | ((uint32_t)h[13] << 8) | ((uint32_t)h[14] << 16) | ((uint32_t)h[15] << 24);
    uint32_t memBytes = (uint32_t)h[16] | ((uint32_t)h[17] << 8) | ((uint32_t)h[18] << 16) | ((uint32_t)h[19] << 24);
    uint16_t funcN    = (uint16_t)h[28] | ((uint16_t)h[29] << 8);
    uint64_t need = 48ull + (uint64_t)funcN * 8ull + constLen + codeLen;
    if(need != (uint64_t)n) return false;
    // El manifest manda sobre la memoria lineal: si el bytecode pide más de
    // lo declarado, el paquete miente y no se instala.
    if(memBytes > (uint32_t)info.memoryKB * 1024u) return false;
    return true;
  }

  // flex-ui-1: exactamente la comprobación de siempre.
  if(n > (size_t)info.memoryKB * 1024u){ f.close(); return false; }
  char* raw = (char*)malloc(n + 1);
  if(!raw){ f.close(); return false; }
  bool ok = readExact(f, raw, n); f.close(); raw[n] = 0;
  cJSON* root = ok ? cJSON_ParseWithLength(raw, n) : nullptr;
  if(!root || !cJSON_IsObject(root)) ok = false;
  if(ok){
    cJSON* schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
    cJSON* start = cJSON_GetObjectItemCaseSensitive(root, "startScreen");
    cJSON* screens = cJSON_GetObjectItemCaseSensitive(root, "screens");
    ok = cJSON_IsNumber(schema) && schema->valuedouble == 1.0 && cJSON_IsString(start) &&
         cJSON_IsArray(screens) && cJSON_GetArraySize(screens) > 0;
  }
  if(root) cJSON_Delete(root);
  free(raw);
  return ok;
}

// ---- Lectura del manifest ya instalado ----------------------------------
static bool readManifestAt(const char* activeRoot, FlexPkgInfo* out){
  char path[FLEXPKG_PATH_MAX + FLEXPKG_ID_MAX + 48];
  snprintf(path, sizeof(path), "%s%s", activeRoot, MANIFEST_FILE);
  File f = LittleFS.open(path, "r");
  if(!f) return false;
  size_t n = f.size();
  if(n < 2 || n > 32u * 1024u){ f.close(); return false; }
  char* raw = (char*)malloc(n + 1);
  if(!raw){ f.close(); return false; }
  bool ok = readExact(f, raw, n); f.close(); raw[n] = 0;
  ok = ok && flexPkgCoreParseManifestBuffer(raw, (uint32_t)n, out);
  free(raw);
  if(!ok) return false;
  out->installedBytes = dirBytes(activeRoot);
  // Registro: hash del paquete, grant y estado. Ausentes = app instalada por
  // una versión anterior del firmware: sigue abriéndose, sin privilegios.
  char hex[65] = "";
  uint32_t hn = readBlob(activeRoot, HASH_FILE, hex, 64);
  if(hn == 64){ hex[64] = 0; memcpy(out->packageSha256, hex, 65); }
  else out->packageSha256[0] = 0;
  char gpath[FLEXPKG_PATH_MAX + FLEXPKG_ID_MAX + 48];
  snprintf(gpath, sizeof(gpath), "%s%s", activeRoot, GRANT_FILE);
  out->grantLen = 0;
  { File g = LittleFS.open(gpath, "r");
    if(g){ if((uint32_t)g.size() == FLEXGRANT_BYTES) out->grantLen = FLEXGRANT_BYTES; g.close(); } }
  return true;
}

// ---- Estado persistido de la app ---------------------------------------
static uint8_t readState(const char* root){
  char c = 0;
  if(readBlob(root, STATE_FILE, &c, 1) != 1) return FLEXPKG_APP_ENABLED;
  if(c == '1') return FLEXPKG_APP_STOPPED;
  if(c == '2') return FLEXPKG_APP_BLOCKED;
  return FLEXPKG_APP_ENABLED;
}

static bool writeState(const char* root, uint8_t state){
  char c = (state == FLEXPKG_APP_STOPPED) ? '1' : (state == FLEXPKG_APP_BLOCKED ? '2' : '0');
  return writeBlob(root, STATE_FILE, &c, 1);
}

// ---- Motor común de inspección/instalación ------------------------------
static bool progressBridge(uint8_t pct, const char* stage, void* user);

struct ProgressWrap { FlexPkgProgressFn cb; void* user; };

static bool progressBridge(uint8_t pct, const char* stage, void* user){
  if(gCancel) return false;
  ProgressWrap* w = (ProgressWrap*)user;
  if(w && w->cb && !w->cb(pct, stage, w->user)){ gCancel = true; return false; }
  return true;
}

static bool runPackage(const char* packagePath, FlexPkgInfo* out, bool install,
                       const uint8_t* externalGrant, uint32_t externalGrantLen,
                       const uint8_t* trustedStorePublicKey, uint64_t nowEpoch,
                       FlexPkgProgressFn cb, void* user){
  if(gBusy){ setError(FLEXPKG_ERR_BUSY, "El instalador ya esta ocupado"); return false; }
  gBusy = true; gCancel = false; setError(FLEXPKG_OK, "Correcto");

  FileReader fr;
  fr.f = LittleFS.open(packagePath, "r");
  if(!fr.f){ gBusy = false; setError(FLEXPKG_ERR_OPEN, "No se pudo abrir el .flexpkg"); return false; }

  FlexPkgReader reader{ fileRead, (uint32_t)fr.f.size(), &fr };
  ProgressWrap wrap{ cb, user };

  // Los datos grandes van al heap: FlexPkgCoreOut lleva el grant entero y no
  // tiene por que vivir en la pila de la tarea de interfaz.
  FlexPkgCoreOut* core = (FlexPkgCoreOut*)malloc(sizeof(FlexPkgCoreOut));
  if(!core){ fr.f.close(); gBusy = false; setError(FLEXPKG_ERR_MEMORY, "Sin memoria para validar el paquete"); return false; }

  char root[360] = {0}, active[360] = {0}, stage[360] = {0}, old[360] = {0};
  StageSink sink;
  FlexPkgSink sinkApi{ sinkBegin, sinkWrite, sinkEnd, &sink };
  bool ok = true;

  // PASO 1: vistazo al manifest (cabecera + manifest, que van al principio).
  // Sirve UNICAMENTE para saber el id de la app y poder decidir donde
  // extraer y si la actualizacion esta permitida. Nada de esto concede
  // confianza: el paquete entero se verifica despues, hash a hash y firma
  // incluida, en la unica pasada que escribe.
  if(install){
    FlexPkgInfo peek;
    FlexPkgErrorCode prc = flexPkgCorePeek(&reader, &peek, gErrText, sizeof(gErrText), FLEXOS_FW_VERSION);
    if(prc != FLEXPKG_OK){ gErr = prc; ok = false; }
    if(ok){
      appPaths(peek.id, root, active, stage, old, sizeof(root));
      FlexPkgInfo current;
      if(readManifestAt(active, &current)){
        if(strcmp(current.developerKeySha256, peek.developerKeySha256)){
          setError(FLEXPKG_ERR_DEVELOPER, "La actualizacion usa otra clave de desarrollador"); ok = false;
        } else if(peek.versionCode <= current.versionCode){
          setError(FLEXPKG_ERR_VERSION, "La version instalada es igual o mas reciente"); ok = false;
        }
      }
    }
    if(ok){
      uint64_t needed = (uint64_t)reader.size + 8192u;
      uint64_t freeBytes = (uint64_t)LittleFS.totalBytes() - LittleFS.usedBytes();
      if(needed > freeBytes){ setError(FLEXPKG_ERR_STORAGE, "No hay espacio para validar la version nueva"); ok = false; }
    }
    if(ok && !mkdirs(root)){ setError(FLEXPKG_ERR_STORAGE, "No se pudo crear la carpeta de la app"); ok = false; }
    if(ok){
      removeTree(stage);
      if(!mkdirs(stage)){ setError(FLEXPKG_ERR_STORAGE, "No se pudo crear el slot temporal"); ok = false; }
      else snprintf(sink.stage, sizeof(sink.stage), "%s", stage);
    }
  }

  // PASO 2: validacion COMPLETA. Es la que manda: cabecera, JSON canonico,
  // rutas, offsets, SHA-256 por archivo, SHA-256 global, firma ECDSA y huella
  // del desarrollador. Si escribe, escribe solo en el slot temporal.
  if(ok){
    fr.pos = 0xFFFFFFFFu;
    FlexPkgErrorCode rc = flexPkgCoreRun(&reader, install ? &sinkApi : nullptr, core,
                                         progressBridge, &wrap,
                                         gErrText, sizeof(gErrText), FLEXOS_FW_VERSION);
    if(rc != FLEXPKG_OK){ gErr = rc; ok = false; }
  }

  // Flex Store entrega el grant como recurso separado: el paquete del
  // desarrollador permanece inmutable y conserva el SHA-256 publicado en el
  // catalogo. Antes de escribir nada permanente se ata criptograficamente el
  // grant al hash firmado, manifest, version y clave que acabamos de validar.
  if(ok && externalGrantLen){
    if(!install || !externalGrant || externalGrantLen != FLEXGRANT_BYTES ||
       !trustedStorePublicKey){
      setError(FLEXPKG_ERR_GRANT, "El permiso descargado tiene un formato invalido");
      ok = false;
    } else if(core->grantLen){
      setError(FLEXPKG_ERR_GRANT, "El paquete ya contiene otro permiso firmado");
      ok = false;
    } else {
      FlexGrantExpect expected;
      memset(&expected, 0, sizeof(expected));
      expected.packageId = core->info.id;
      expected.versionName = core->info.versionName;
      expected.versionCode = core->info.versionCode;
      memcpy(expected.packageSha256, core->signedHash, sizeof(expected.packageSha256));
      expected.manifestRequested = core->info.systemPermissions;
      expected.nowEpoch = nowEpoch;
      if(!flexPkgCoreHexToBytes(core->info.developerKeySha256,
                               expected.developerKeySha256,
                               sizeof(expected.developerKeySha256))){
        setError(FLEXPKG_ERR_GRANT, "La identidad del desarrollador no se pudo comprobar");
        ok = false;
      } else {
        FlexGrantResult result;
        FlexGrantStatus grantStatus = flexGrantCheck(externalGrant, externalGrantLen,
                                                     &expected, trustedStorePublicKey,
                                                     &result);
        if(grantStatus != FLEXGRANT_OK){
          char detail[128];
          snprintf(detail, sizeof(detail), "Permiso de sistema rechazado: %s",
                   flexGrantStatusText((uint8_t)grantStatus));
          setError(FLEXPKG_ERR_GRANT, detail);
          ok = false;
        } else {
          memcpy(core->grant, externalGrant, FLEXGRANT_BYTES);
          core->grantLen = FLEXGRANT_BYTES;
          core->info.grantLen = FLEXGRANT_BYTES;
        }
      }
    }
  }

  if(ok && install){
    // Manifest, indice, hash y grant quedan junto a los archivos de la app.
    // El grant se guarda TAL CUAL: su firma se vuelve a comprobar en cada
    // arranque de la app, no se da por buena porque se instalara un dia.
    char* manifestRaw = (char*)malloc(core->manifestLen + 1);
    char* indexRaw = (char*)malloc(core->indexLen + 1);
    bool wrote = manifestRaw && indexRaw &&
                 fileRead(&fr, 64, manifestRaw, core->manifestLen) &&
                 fileRead(&fr, 64 + core->manifestLen, indexRaw, core->indexLen) &&
                 writeBlob(stage, MANIFEST_FILE, manifestRaw, core->manifestLen) &&
                 writeBlob(stage, INDEX_FILE, indexRaw, core->indexLen) &&
                 writeBlob(stage, HASH_FILE, core->info.packageSha256, 64);
    if(wrote && core->grantLen) wrote = writeBlob(stage, GRANT_FILE, core->grant, core->grantLen);
    free(manifestRaw); free(indexRaw);
    if(!wrote){ setError(FLEXPKG_ERR_STORAGE, "No se pudo guardar el registro de la app"); ok = false; }
    else if(!smokeEntrypoint(stage, core->info)){
      setError(FLEXPKG_ERR_RUNTIME,
               core->info.runtime == FLEXPKG_RT_APP1
                 ? "El bytecode flex-app-v1 no es valido"
                 : "El entrypoint flex-ui-1 no puede iniciarse");
      ok = false;
    } else {
      core->info.installedBytes = dirBytes(stage);
    }
  }

  if(ok && install){
    removeTree(old);
    bool hadActive = LittleFS.exists(active);
    if(hadActive && !LittleFS.rename(active, old)){
      setError(FLEXPKG_ERR_COMMIT, "No se pudo preparar la actualizacion"); ok = false;
    }
    if(ok && !LittleFS.rename(stage, active)){
      if(hadActive) LittleFS.rename(old, active);       // reversion: se conserva la anterior
      setError(FLEXPKG_ERR_COMMIT, "No se pudo activar la version nueva"); ok = false;
    }
    if(ok){
      removeTree(old);                                  // solo queda la version mas reciente
      char dataDir[360];
      snprintf(dataDir, sizeof(dataDir), "%s%s", root, DATA_NAME);
      mkdirs(dataDir);                                  // carpeta privada, persiste entre versiones
      // Una version nueva vuelve a estar habilitada: si el usuario la habia
      // detenido, actualizar es una decision explicita de volver a usarla.
      writeState(root, FLEXPKG_APP_ENABLED);
      core->info.state = FLEXPKG_APP_ENABLED;
      if(cb) cb(100, "Aplicacion instalada", user);
    }
  }

  if(!ok && install && stage[0]) removeTree(stage);      // temporales fuera SIEMPRE
  if(out && ok) *out = core->info;
  free(core);
  fr.f.close();
  gBusy = false;
  return ok;
}

} // namespace

// -------------------------------------------------------------------------
bool flexPkgBegin(){
  setError(FLEXPKG_OK, "Correcto");
  // FlexOS_FS ya monto LittleFS con la etiqueta real de la particion
  // (spiffs/littlefs/ffat/storage). Volver a llamar begin() aqui podria
  // intentar la etiqueta por defecto y desmontar una particion valida.
  if(LittleFS.totalBytes() == 0){ setError(FLEXPKG_ERR_FS, "LittleFS no esta disponible"); return false; }
  if(!mkdirOne(ROOT_DIR)){ setError(FLEXPKG_ERR_FS, "No se pudo crear /FlexApps"); return false; }
  File root = LittleFS.open(ROOT_DIR, "r");
  if(!root || !root.isDirectory()){ if(root) root.close(); return false; }
  File e = root.openNextFile();
  while(e){
    if(e.isDirectory()){
      char id[FLEXPKG_ID_MAX + 1];
      snprintf(id, sizeof(id), "%s", baseName(e.name()));
      char appRoot[360], active[360], stage[360], old[360];
      e.close();
      if(appPaths(id, appRoot, active, stage, old, sizeof(appRoot))){
        removeTree(stage);
        if(!LittleFS.exists(active) && LittleFS.exists(old)) LittleFS.rename(old, active);
        if(LittleFS.exists(active)) removeTree(old);
      }
    } else e.close();
    e = root.openNextFile();
  }
  root.close();
  // La recuperacion pudo activar una version que estaba a medio cambiar o
  // retirar un stage huerfano: la lista de apps de despues del arranque no
  // tiene por que ser la de antes.
  bumpRevision();
  return true;
}

bool flexPkgInspect(const char* packagePath, FlexPkgInfo* out, FlexPkgProgressFn cb, void* user){
  return runPackage(packagePath, out, false, nullptr, 0, nullptr, 0, cb, user);
}

bool flexPkgInstall(const char* packagePath, FlexPkgInfo* out, FlexPkgProgressFn cb, void* user){
  bool ok = runPackage(packagePath, out, true, nullptr, 0, nullptr, 0, cb, user);
  // Solo cuenta la instalacion que TERMINO. Una que falla revierte y deja el
  // registro exactamente como estaba: subir la revision ahi obligaria a releer
  // /FlexApps para descubrir que no habia cambiado nada.
  if(ok) bumpRevision();
  return ok;
}

bool flexPkgInstallWithGrant(const char* packagePath,
                             const uint8_t* grant, uint32_t grantLen,
                             const uint8_t trustedStorePublicKey[65],
                             uint64_t nowEpoch,
                             FlexPkgInfo* out,
                             FlexPkgProgressFn cb, void* user){
  bool ok = runPackage(packagePath, out, true, grant, grantLen,
                       trustedStorePublicKey, nowEpoch, cb, user);
  if(ok) bumpRevision();
  return ok;
}

bool flexPkgUninstall(const char* packageId){
  if(gBusy){ setError(FLEXPKG_ERR_BUSY, "El instalador ya esta ocupado"); return false; }
  char root[360], active[360], stage[360], old[360];
  if(!appPaths(packageId, root, active, stage, old, sizeof(root)) || !LittleFS.exists(root)){
    setError(FLEXPKG_ERR_NOT_FOUND, "Aplicacion no encontrada"); return false;
  }
  // Se lleva TODO: version activa, temporales, registro y carpeta privada.
  if(!removeTree(root)){ setError(FLEXPKG_ERR_STORAGE, "No se pudo eliminar la aplicacion"); return false; }
  setError(FLEXPKG_OK, "Correcto");
  bumpRevision();
  return true;
}

int flexPkgList(FlexPkgInfo* out, int maxItems){
  if(!out || maxItems <= 0 || !LittleFS.exists(ROOT_DIR)) return 0;
  int n = 0;
  File root = LittleFS.open(ROOT_DIR, "r");
  if(!root || !root.isDirectory()){ if(root) root.close(); return 0; }
  File e = root.openNextFile();
  while(e && n < maxItems){
    if(e.isDirectory()){
      char appRoot[360], active[360];
      snprintf(appRoot, sizeof(appRoot), "%s/%s", ROOT_DIR, baseName(e.name()));
      snprintf(active, sizeof(active), "%s%s", appRoot, ACTIVE_NAME);
      e.close();
      if(readManifestAt(active, &out[n])){
        out[n].state = readState(appRoot);
        n++;
      }
    } else e.close();
    e = root.openNextFile();
  }
  if(e) e.close();
  root.close();
  return n;
}

bool flexPkgGet(const char* packageId, FlexPkgInfo* out){
  if(!out || !flexPkgCoreSafeId(packageId)) return false;
  char appRoot[360], active[360];
  snprintf(appRoot, sizeof(appRoot), "%s/%s", ROOT_DIR, packageId);
  snprintf(active, sizeof(active), "%s%s", appRoot, ACTIVE_NAME);
  if(!readManifestAt(active, out)) return false;
  out->state = readState(appRoot);
  return true;
}

bool flexPkgActiveRoot(const char* packageId, char* out, size_t outSize){
  if(!out || outSize == 0 || !flexPkgCoreSafeId(packageId)) return false;
  int n = snprintf(out, outSize, "%s/%s%s", ROOT_DIR, packageId, ACTIVE_NAME);
  return n > 0 && (size_t)n < outSize && LittleFS.exists(out);
}

bool flexPkgEntryPath(const char* packageId, char* out, size_t outSize){
  FlexPkgInfo info; char root[360];
  if(!out || !flexPkgGet(packageId, &info) || !flexPkgActiveRoot(packageId, root, sizeof(root))) return false;
  int n = snprintf(out, outSize, "%s/%s", root, info.entry);
  return n > 0 && (size_t)n < outSize && LittleFS.exists(out);
}

uint32_t flexPkgGrant(const char* packageId, uint8_t* out, uint32_t cap){
  char active[360];
  if(!out || cap < FLEXGRANT_BYTES || !flexPkgActiveRoot(packageId, active, sizeof(active))) return 0;
  return readBlob(active, GRANT_FILE, out, cap);
}

bool flexPkgDataDir(const char* packageId, char* out, size_t outSize){
  if(!out || outSize == 0 || !flexPkgCoreSafeId(packageId)) return false;
  int n = snprintf(out, outSize, "%s/%s%s", ROOT_DIR, packageId, DATA_NAME);
  return n > 0 && (size_t)n < outSize;
}

bool flexPkgDataEnsure(const char* packageId){
  char dir[360];
  if(!flexPkgDataDir(packageId, dir, sizeof(dir))) return false;
  return mkdirs(dir);
}

uint32_t flexPkgDataBytes(const char* packageId){
  char dir[360];
  if(!flexPkgDataDir(packageId, dir, sizeof(dir))) return 0;
  return dirBytes(dir);
}

bool flexPkgSetState(const char* packageId, FlexPkgAppState state){
  char root[360];
  if(!flexPkgCoreSafeId(packageId)) return false;
  snprintf(root, sizeof(root), "%s/%s", ROOT_DIR, packageId);
  if(!LittleFS.exists(root)){ setError(FLEXPKG_ERR_NOT_FOUND, "Aplicacion no encontrada"); return false; }
  if(!writeState(root, (uint8_t)state)){ setError(FLEXPKG_ERR_STORAGE, "No se pudo guardar el estado"); return false; }
  setError(FLEXPKG_OK, "Correcto");
  // Detener o reactivar no cambia QUE apps hay, pero si cambia si se pueden
  // abrir: quien cachee la lista tiene que volver a leer el estado.
  bumpRevision();
  return true;
}

uint32_t flexPkgRevision(){ return gRevision; }

FlexPkgErrorCode flexPkgErrorCode(){ return gErr; }
const char* flexPkgError(){ return gErrText; }
bool flexPkgBusy(){ return gBusy; }
void flexPkgCancel(){ gCancel = true; }
