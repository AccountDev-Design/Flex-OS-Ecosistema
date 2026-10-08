#ifndef FLEXOS_APPGRANT_H
#define FLEXOS_APPGRANT_H
// #############################################################
//  FLEX OS · PERMISOS DE SISTEMA CON GRANT FIRMADO  (FLXG v1)
//  ------------------------------------------------------------
//  EL PRINCIPIO. El manifest de una app DECLARA lo que le gustaría
//  poder hacer. El grant CONCEDE. Son cosas distintas y este módulo
//  es la frontera entre las dos: un permiso privilegiado que sólo
//  aparece en el manifest NO existe.
//
//  El grant lo emite Flex Developer Studio y lo firma la clave
//  PINNEADA de Flex Store (la misma que ya valida el catálogo, ver
//  FlexOS_TrustedKeys.h). Ata, en un solo bloque firmado:
//      · el propósito del grant,
//      · el packageId,
//      · la versión (nombre y código),
//      · el SHA-256 del paquete instalado,
//      · el SHA-256 de la clave del desarrollador,
//      · la máscara de permisos concedidos,
//      · la ventana de validez, si la lleva.
//  Si CUALQUIERA de esos campos no coincide con la app que se está
//  arrancando, se deniega. No hay concesión parcial por proximidad.
//
//  POR QUE BINARIO Y NO JSON. Un bloque de tamaño fijo se firma y se
//  comprueba byte a byte, sin canonicalización, sin parser y sin
//  ambigüedades de codificación. La superficie de ataque de un grant
//  es entonces exactamente cero antes de llegar a la firma.
//
//  Es lógica PURA salvo por SHA-256/ECDSA, que salen del backend
//  criptográfico (mbedTLS en la placa, OpenSSL en el PC). Se prueba en
//  el PC con sanitizers: tests/host/test_appgrant.cpp.
// #############################################################
#include <stdint.h>
#include <stddef.h>

// ---- Disposición del bloque FLXG v1 ------------------------------------
//   0    4  "FLXG"
//   4    2  formatVersion = 1
//   6    2  flags = 0
//   8    2  purpose
//  10    2  permissionCount   (= popcount(permissionMask))
//  12    4  versionCode
//  16    8  notBefore (epoch s, LE; 0 = sin límite inferior)
//  24    8  notAfter  (epoch s, LE; 0 = sin límite superior)
//  32   32  packageSha256      (bytes crudos)
//  64   32  developerKeySha256 (bytes crudos)
//  96   96  packageId   (ASCII, relleno a cero)
// 192   32  versionName (ASCII, relleno a cero)
// 224    4  permissionMask
// 228    4  reservado = 0
//          -- hasta aquí es lo que se firma (232 bytes) --
// 232   64  firma ECDSA P-256 (r || s)
#define FLEXGRANT_SIGNED_BYTES  232u
#define FLEXGRANT_BYTES         296u
#define FLEXGRANT_FORMAT_VERSION 1
#define FLEXGRANT_PURPOSE_APP    1u    // permisos de sistema de una app de Flex Store

#define FLEXGRANT_ID_BYTES       96u
#define FLEXGRANT_VERNAME_BYTES  32u

// ---- Permisos de SISTEMA (los del grant) --------------------------------
// No se mezclan con FlexPkgPermission (los del manifest de flex-ui-1): esos
// describen la app, éstos abren servicios del firmware.
enum FlexSysPermission : uint32_t {
  FLEXPERM_SYS_BENCHMARK_RUN      = 1u << 0,   // "benchmark.run"
  FLEXPERM_SYS_CPU_STRESS         = 1u << 1,   // "system.cpu.stress"
  FLEXPERM_SYS_PSRAM_MEASURE      = 1u << 2,   // "system.psram.measure"
  FLEXPERM_SYS_TEMPERATURE_READ   = 1u << 3,   // "system.temperature.read"
  FLEXPERM_SYS_PERF_METRICS       = 1u << 4,   // "system.performance.metrics"
  FLEXPERM_SYS_DISPLAY_LANDSCAPE  = 1u << 5,   // "display.landscape"
  FLEXPERM_SYS_DISPLAY_EXCLUSIVE  = 1u << 6,   // "display.exclusive"
  FLEXPERM_SYS_STORAGE_APP        = 1u << 7    // "storage.app"
};
#define FLEXPERM_SYS_ALL  0x000000FFu

// Nombre canónico <-> bit. Devuelve 0 si el nombre no es un permiso conocido.
uint32_t    flexSysPermissionBit(const char* name);
const char* flexSysPermissionName(uint32_t bit);

enum FlexGrantStatus : uint8_t {
  FLEXGRANT_OK = 0,
  FLEXGRANT_ABSENT,        // no hay grant: la app funciona, sin privilegios
  FLEXGRANT_ERR_SIZE,      // longitud distinta de FLEXGRANT_BYTES
  FLEXGRANT_ERR_MAGIC,     // magia, versión, flags o reservados inválidos
  FLEXGRANT_ERR_PURPOSE,   // el grant no es para permisos de app
  FLEXGRANT_ERR_PACKAGE,   // packageId distinto del de la app instalada
  FLEXGRANT_ERR_VERSION,   // versionName / versionCode distintos
  FLEXGRANT_ERR_HASH,      // SHA-256 del paquete instalado distinto
  FLEXGRANT_ERR_DEVELOPER, // SHA-256 de la clave del desarrollador distinto
  FLEXGRANT_ERR_PERMS,     // permisos desconocidos, o máscara y contador que no cuadran
  FLEXGRANT_ERR_MANIFEST,  // concede algo que el manifest ni siquiera pedía
  FLEXGRANT_ERR_WINDOW,    // fuera de ventana o sin reloj fiable para comprobarla
  FLEXGRANT_ERR_SIGNATURE  // la firma no es de la clave pinneada de Flex Store
};

// Lo que el firmware sabe de la app instalada y contra lo que hay que
// comprobar el grant. Todo campo es obligatorio.
struct FlexGrantExpect {
  const char* packageId;
  const char* versionName;
  uint32_t    versionCode;
  uint8_t     packageSha256[32];
  uint8_t     developerKeySha256[32];
  uint32_t    manifestRequested;  // permisos que el manifest declara pedir
  uint64_t    nowEpoch;           // 0 = el reloj todavía no es fiable
};

struct FlexGrantResult {
  uint32_t granted;        // máscara realmente concedida (0 si status != OK)
  uint64_t notBefore;
  uint64_t notAfter;
  uint8_t  windowChecked;  // 1 en todo grant aceptado; 0 si status != OK
};

// Comprueba un grant COMPLETO. `trustedPub` son los 65 bytes de la clave
// pública pinneada (formato SEC1 sin comprimir, 0x04 || X || Y).
//
// EL ORDEN IMPORTA: primero lo barato y lo que ata identidad (magia, propósito,
// paquete, versión, hashes, permisos, ventana) y sólo al final la firma. Así un
// grant de otra app se rechaza sin gastar una verificación ECDSA, y ningún
// campo se da por bueno "porque la firma cuadra".
FlexGrantStatus flexGrantCheck(const uint8_t* blob, uint32_t len,
                               const FlexGrantExpect* expect,
                               const uint8_t trustedPub[65],
                               FlexGrantResult* out);

const char* flexGrantStatusText(uint8_t status);

// SHA-256 de un buffer con el backend disponible. Se expone porque el
// instalador y las pruebas necesitan la MISMA función que usa el grant.
bool flexGrantSha256(const uint8_t* data, size_t len, uint8_t out[32]);

#endif
