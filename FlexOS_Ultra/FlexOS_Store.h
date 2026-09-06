#ifndef FLEXOS_STORE_H
#define FLEXOS_STORE_H

#include <Arduino.h>
#include "FlexOS_Package.h"

#define FLEXSTORE_MAX_CATALOG 24

enum FlexStoreState : uint8_t {
  FLEXSTORE_IDLE = 0,
  FLEXSTORE_LOADING,
  FLEXSTORE_READY,
  FLEXSTORE_DOWNLOADING,
  FLEXSTORE_INSTALLING,
  FLEXSTORE_SUCCESS,
  FLEXSTORE_ERROR,
  FLEXSTORE_CANCELLED
};

struct FlexStoreItem {
  char recordId[40];
  char packageId[FLEXPKG_ID_MAX + 1];
  char name[FLEXPKG_NAME_MAX + 1];
  char summary[FLEXPKG_SUMMARY_MAX + 1];
  char category[FLEXPKG_CATEGORY_MAX + 1];
  char versionName[FLEXPKG_VERSION_MAX + 1];
  uint32_t versionCode;
  char minFlexOS[FLEXPKG_VERSION_MAX + 1];
  uint32_t packageBytes;
  char packageSha256[65];
  char downloadUrl[320];
  char permissionGrantUrl[320];
  uint16_t ratingX100;
  uint32_t ratingCount;
};

void flexStoreBegin();
bool flexStoreRefresh();
bool flexStoreInstall(int catalogIndex);
void flexStoreCancel();

FlexStoreState flexStoreState();
uint8_t flexStoreProgress();
const char* flexStoreStage();
const char* flexStoreError();
int flexStoreCatalogCount();
bool flexStoreCatalogItem(int index, FlexStoreItem* out);
bool flexStoreHasUpdate(const FlexStoreItem* item, FlexPkgInfo* installed = nullptr);

// Identificador del paquete que se esta descargando o instalando AHORA, o
// cadena vacia si no hay ninguno. Lo usa la Caja de aplicaciones para marcar
// esa app como "actualizando" sin tocar el sistema de archivos ni adivinarlo
// por el estado global de la tienda.
void flexStoreBusyPackage(char* out, size_t outSize);

#endif
