#ifndef FLEXOS_PACKAGE_H
#define FLEXOS_PACKAGE_H

#include <Arduino.h>

#define FLEXPKG_FORMAT_VERSION       1
#define FLEXPKG_MAX_PACKAGE_BYTES    (16u * 1024u * 1024u)
#define FLEXPKG_MAX_FILE_BYTES       (8u * 1024u * 1024u)
#define FLEXPKG_MAX_FILES            128
#define FLEXPKG_MAX_INSTALLED        24
#define FLEXPKG_ID_MAX               96
#define FLEXPKG_NAME_MAX             60
#define FLEXPKG_PATH_MAX             240
#define FLEXPKG_VERSION_MAX          31
#define FLEXPKG_SUMMARY_MAX          159
#define FLEXPKG_CATEGORY_MAX         39
#define FLEXPKG_RUNTIME_MAX          23
// Trailer de permisos firmados por Flex Store. Va DESPUES de la firma del
// paquete, asi que no entra en el hash firmado: por eso puede referirse al
// hash del propio paquete sin morderse la cola. 0 = el paquete no trae grant.
#define FLEXPKG_GRANT_MAX            296

enum FlexPkgErrorCode : uint8_t {
  FLEXPKG_OK = 0,
  FLEXPKG_ERR_FS,
  FLEXPKG_ERR_OPEN,
  FLEXPKG_ERR_SIZE,
  FLEXPKG_ERR_HEADER,
  FLEXPKG_ERR_MEMORY,
  FLEXPKG_ERR_JSON,
  FLEXPKG_ERR_MANIFEST,
  FLEXPKG_ERR_INDEX,
  FLEXPKG_ERR_PATH,
  FLEXPKG_ERR_HASH,
  FLEXPKG_ERR_SIGNATURE,
  FLEXPKG_ERR_DEVELOPER,
  FLEXPKG_ERR_VERSION,
  FLEXPKG_ERR_RUNTIME,
  FLEXPKG_ERR_STORAGE,
  FLEXPKG_ERR_COMMIT,
  FLEXPKG_ERR_NOT_FOUND,
  FLEXPKG_ERR_BUSY,
  FLEXPKG_ERR_CANCELLED,
  FLEXPKG_ERR_GRANT
};

// Runtime declarado por el manifest. "flex-ui-1" es el de siempre (pantallas
// declarativas) y sigue funcionando exactamente igual; "flex-app-v1" es el
// nuevo, con logica real dentro de la maquina aislada.
enum FlexPkgRuntime : uint8_t {
  FLEXPKG_RT_UI1 = 0,
  FLEXPKG_RT_APP1 = 1
};

// Estado de una app instalada, persistido en su registro.
enum FlexPkgAppState : uint8_t {
  FLEXPKG_APP_ENABLED = 0,     // se puede abrir
  FLEXPKG_APP_STOPPED = 1,     // el usuario la detuvo desde Flex OS
  FLEXPKG_APP_BLOCKED = 2      // el sistema la bloqueo (se paso de limites)
};

enum FlexPkgPermission : uint16_t {
  FLEXPERM_NETWORK       = 1u << 0,
  FLEXPERM_STORAGE_READ  = 1u << 1,
  FLEXPERM_STORAGE_WRITE = 1u << 2,
  FLEXPERM_NOTIFICATIONS = 1u << 3,
  FLEXPERM_CAMERA        = 1u << 4,
  FLEXPERM_MICROPHONE    = 1u << 5,
  FLEXPERM_LOCATION      = 1u << 6,
  FLEXPERM_CLIPBOARD     = 1u << 7
};

struct FlexPkgInfo {
  char id[FLEXPKG_ID_MAX + 1];
  char name[FLEXPKG_NAME_MAX + 1];
  char versionName[FLEXPKG_VERSION_MAX + 1];
  uint32_t versionCode;
  char minFlexOS[FLEXPKG_VERSION_MAX + 1];
  char entry[FLEXPKG_PATH_MAX + 1];
  char summary[FLEXPKG_SUMMARY_MAX + 1];
  char category[FLEXPKG_CATEGORY_MAX + 1];
  char developerKeySha256[65];
  uint16_t permissions;
  uint16_t memoryKB;
  uint16_t storageKB;
  uint16_t fileCount;
  uint32_t payloadBytes;
  uint32_t installedBytes;
  // ---- Campos anadidos AL FINAL, a proposito ----
  // Van los ultimos para que ninguna estructura ya inicializada por posicion
  // (las pruebas, los dobles de host, el puente de Flex Store) tenga que
  // tocarse, y para que un memset previo siga dejandolos en el valor neutro.
  uint8_t  runtime;              // FlexPkgRuntime
  uint8_t  state;                // FlexPkgAppState
  uint32_t systemPermissions;    // lo que el manifest DECLARA pedir (no concede nada)
  uint32_t instrPerTick;         // 0 = valor por defecto del gestor
  uint32_t usPerTick;
  uint32_t drawPerFrame;
  char     packageSha256[65];    // hash firmado del paquete instalado
  uint32_t grantLen;             // bytes del grant guardado (0 = sin permisos)
};

typedef bool (*FlexPkgProgressFn)(uint8_t percent, const char* stage, void* user);

// Mount recovery and registry scan. Call after flexFsBegin().
bool flexPkgBegin();

// Full cryptographic validation without modifying the installed app.
bool flexPkgInspect(const char* packagePath, FlexPkgInfo* out,
                    FlexPkgProgressFn progress = nullptr, void* user = nullptr);

// Transactional install/update. The active version is replaced only after the
// complete new package, entrypoint and developer identity have been verified.
bool flexPkgInstall(const char* packagePath, FlexPkgInfo* out,
                    FlexPkgProgressFn progress = nullptr, void* user = nullptr);

// Instalacion desde Flex Store cuando el grant se entrega como un recurso
// firmado separado. El grant se valida contra el contenido, desarrollador,
// version y permisos del paquete ANTES de activar la nueva version.
bool flexPkgInstallWithGrant(const char* packagePath,
                             const uint8_t* grant, uint32_t grantLen,
                             const uint8_t trustedStorePublicKey[65],
                             uint64_t nowEpoch,
                             FlexPkgInfo* out,
                             FlexPkgProgressFn progress = nullptr,
                             void* user = nullptr);

bool flexPkgUninstall(const char* packageId);

// REVISION DEL REGISTRO. Contador que sube UNA vez cada vez que el conjunto de
// apps instaladas puede haber cambiado: al recuperar el almacenamiento en el
// arranque, al instalar o actualizar, al desinstalar y al detener o reactivar
// una app. No toca el sistema de archivos: leerlo cuesta lo que leer un entero.
//
// Es la senal de invalidacion de cache para todo el que muestre apps instaladas
// (la Caja de aplicaciones, Flex Store). Comparar este numero por cuadro es
// gratis; releer /FlexApps por cuadro no lo seria.
uint32_t flexPkgRevision();

// Lee el grant firmado que se instalo con la app. Devuelve los bytes escritos
// (0 = la app no trae permisos de sistema).
uint32_t flexPkgGrant(const char* packageId, uint8_t* out, uint32_t cap);

// Carpeta PRIVADA de la app: /FlexApps/<id>/data. Vive FUERA de "active", asi
// que sobrevive a una actualizacion y desaparece con la desinstalacion.
bool flexPkgDataDir(const char* packageId, char* out, size_t outSize);
bool flexPkgDataEnsure(const char* packageId);
uint32_t flexPkgDataBytes(const char* packageId);

// Detener / reactivar una app instalada (queda anotado en su registro).
bool flexPkgSetState(const char* packageId, FlexPkgAppState state);
int  flexPkgList(FlexPkgInfo* out, int maxItems);
bool flexPkgGet(const char* packageId, FlexPkgInfo* out);
bool flexPkgEntryPath(const char* packageId, char* out, size_t outSize);
bool flexPkgActiveRoot(const char* packageId, char* out, size_t outSize);

FlexPkgErrorCode flexPkgErrorCode();
const char* flexPkgError();
bool flexPkgBusy();
void flexPkgCancel();

#endif
