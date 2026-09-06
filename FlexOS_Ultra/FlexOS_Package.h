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
  FLEXPKG_ERR_CANCELLED
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

bool flexPkgUninstall(const char* packageId);
int  flexPkgList(FlexPkgInfo* out, int maxItems);
bool flexPkgGet(const char* packageId, FlexPkgInfo* out);
bool flexPkgEntryPath(const char* packageId, char* out, size_t outSize);
bool flexPkgActiveRoot(const char* packageId, char* out, size_t outSize);

FlexPkgErrorCode flexPkgErrorCode();
const char* flexPkgError();
bool flexPkgBusy();
void flexPkgCancel();

#endif
