#ifndef FLEXOS_TRUSTEDKEYS_H
#define FLEXOS_TRUSTEDKEYS_H
// #############################################################
//  FLEX OS · CLAVES PUBLICAS PINNEADAS
//  ------------------------------------------------------------
//  UNA sola definición para todo el firmware. Antes esta clave vivía
//  dentro de FlexOS_Store.cpp; ahora la comparten el catálogo de Flex
//  Store y los GRANTS de permisos, y una constante duplicada en dos
//  archivos es exactamente la clase de cosa que se desincroniza sin
//  que nadie lo note.
//
//  La clave privada correspondiente vive ÚNICAMENTE como secreto del
//  backend de Flex Developer Studio. El firmware sólo verifica.
// #############################################################
#include <stdint.h>

// Clave publica estable de Flex Store (ECDSA P-256, SEC1 sin comprimir).
// Firma: (a) cada respuesta /api/catalog, (b) cada grant de permisos FLXG.
static const uint8_t FLEX_STORE_PUBLIC_KEY[65] = {
  0x04,0x70,0x9d,0xf4,0x80,0xde,0x8d,0x66,0x05,0x38,0x6b,0x01,0xb3,0xf8,0x9f,0x20,
  0xf7,0x29,0x08,0x7f,0x76,0xff,0x76,0xfd,0x43,0x9d,0x50,0xa8,0x30,0x9e,0xac,0xc7,
  0x60,0xf5,0x4d,0x60,0xbb,0x85,0xde,0xa2,0xd3,0x79,0x8e,0x15,0xab,0xad,0x89,0xd5,
  0x97,0xa4,0xc0,0x7c,0xfa,0x66,0xc1,0x70,0x6d,0xe6,0x48,0xb3,0x8e,0x40,0x28,0x2c,
  0x05
};
static const char* FLEX_STORE_KEY_ID =
  "69b91cf7a9246cc2084dda73ccef77b2dc1a372b18fec19b49cf980efd68af69";

#endif
