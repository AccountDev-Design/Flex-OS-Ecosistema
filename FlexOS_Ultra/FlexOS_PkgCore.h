#ifndef FLEXOS_PKGCORE_H
#define FLEXOS_PKGCORE_H
// #############################################################
//  FLEX OS · NUCLEO DE VALIDACION DE .flexpkg  (FLXP v1)
//  ------------------------------------------------------------
//  QUE ES. Toda la parte de "¿me puedo fiar de estos bytes?" del
//  instalador de Flex Store, SIN sistema de archivos:
//    · cabecera y coherencia de tamaños,
//    · manifest e índice JSON canónicos,
//    · reglas de identificador, ruta, versión y límites,
//    · offsets contiguos, rutas duplicadas y tamaños,
//    · SHA-256 por archivo y SHA-256 global,
//    · firma ECDSA P-256 y huella de la clave del desarrollador,
//    · trailer de grant (permisos firmados por Flex Store).
//
//  POR QUE VIVE APARTE. Antes esto estaba dentro de FlexOS_Package.cpp,
//  atado a LittleFS, y por tanto NO se podía probar en el PC: la parte
//  del firmware que decide si un paquete descargado de internet es de
//  fiar era justamente la que no tenía batería de pruebas. Aquí recibe
//  los bytes por un LECTOR abstracto y los entrega por un SUMIDERO
//  abstracto, así que en la placa son LittleFS y en el PC memoria — el
//  MISMO código, la misma decisión.
//
//  El único apoyo externo es cJSON (que el core de ESP32 ya trae) y el
//  backend criptográfico de FlexOS_AppGrant.
// #############################################################
#include <stdint.h>
#include <stddef.h>
#include "FlexOS_Package.h"

// Lector de los bytes del paquete. `read` devuelve false si no puede servir
// EXACTAMENTE n bytes desde off.
struct FlexPkgReader {
  bool (*read)(void* user, uint32_t off, void* dst, uint32_t n);
  uint32_t size;
  void* user;
};

// Sumidero de extracción. NULL en `begin` => sólo inspeccionar (no se escribe
// nada). Las rutas que llegan a `begin` ya pasaron safePath().
struct FlexPkgSink {
  bool (*begin)(void* user, const char* relPath);
  bool (*write)(void* user, const uint8_t* data, uint32_t n);
  bool (*end)(void* user, bool ok);
  void* user;
};

// Resultado ampliado de una validación.
struct FlexPkgCoreOut {
  FlexPkgInfo info;
  uint8_t  signedHash[32];     // SHA-256(manifest || index || payload): la identidad del paquete
  uint8_t  grant[FLEXPKG_GRANT_MAX];
  uint32_t grantLen;           // 0 = el paquete no trae permisos firmados
  uint32_t manifestLen, indexLen, payloadLen;
};

// Valida (y opcionalmente extrae) un paquete completo.
// `sink` puede ser NULL para inspeccionar sin escribir.
// `errText` recibe un motivo legible en español.
FlexPkgErrorCode flexPkgCoreRun(const FlexPkgReader* rd, const FlexPkgSink* sink,
                                FlexPkgCoreOut* out,
                                FlexPkgProgressFn progress, void* progressUser,
                                char* errText, size_t errCap,
                                const char* firmwareVersion);

// Lee SOLO la cabecera y el manifest (que van al principio del archivo) para
// conocer el id, la version y el runtime ANTES de tocar el almacenamiento.
// No valida hashes ni firma: eso es trabajo de flexPkgCoreRun, que se ejecuta
// despues sobre el paquete entero. Sirve para saber DONDE extraer.
FlexPkgErrorCode flexPkgCorePeek(const FlexPkgReader* rd, FlexPkgInfo* out,
                                 char* errText, size_t errCap,
                                 const char* firmwareVersion);

// Reglas sueltas, expuestas porque las usan el instalador y las pruebas.
bool flexPkgCoreSafeId(const char* s);
bool flexPkgCoreSafePath(const char* s);
bool flexPkgCoreSemver(const char* s);
int  flexPkgCoreSemverCompare(const char* a, const char* b);
// Lee y valida un manifest suelto (el que quedó instalado en `active`).
bool flexPkgCoreParseManifestBuffer(const char* raw, uint32_t len, FlexPkgInfo* out);
void flexPkgCoreBytesToHex(const uint8_t* in, size_t n, char* out);
bool flexPkgCoreHexToBytes(const char* hex, uint8_t* out, size_t n);

#endif
