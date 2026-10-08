#pragma once
// #############################################################
//  FLEX OS · CODIFICADOR QR  (FlexOS_QR.h/.cpp)
//  ------------------------------------------------------------
//  QUE ES. Un codificador de codigos QR completo (modo BYTE,
//  versiones 1 a 10, los cuatro niveles de correccion), escrito para
//  Flex OS. Es un MODULO DEL SISTEMA, no una pieza privada de una app:
//  cualquier parte del sistema que necesite ensenar un enlace en
//  pantalla lo usa desde aqui.
//
//  POR QUE EXISTE. Se busco en todo el repositorio y no habia ninguno.
//  Lo mas parecido, isqrt32(), es una raiz cuadrada entera del motor
//  grafico y no tiene nada que ver.
//
//  POR QUE VIVE FUERA DEL SKETCH. Es logica PURA -- entra texto, sale
//  una rejilla de modulos -- y por tanto se compila y se ejercita EN EL
//  PC con sanitizers (tests/host/test_qr.cpp), igual que FlexOS_Media,
//  FlexOS_Mem o FlexOS_FlexLink. Aqui hay aritmetica en GF(256),
//  entrelazado de bloques y colocacion en zigzag: tres sitios donde un
//  indice mal calculado produce un codigo que "parece un QR" y que
//  ningun telefono lee. Eso solo se caza con una prueba que lo VUELVA
//  A LEER, y esa prueba corre en el PC.
//
//  NO RESERVA MEMORIA. El anfitrion entrega el buffer de modulos.
//
//  QUE NO HACE: modo numerico, alfanumerico y kanji (comprimen mas,
//  pero el caso de uso del sistema es una URL, que es byte puro), ni
//  versiones por encima de la 10. Una URL local
//  ("http://192.168.1.50:8080/ab12cd34/Documento.svg") son unos 48
//  bytes: cabe de sobra en la version 4 con correccion M.
// #############################################################
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEXQR_MIN_VERSION  1
#define FLEXQR_MAX_VERSION 10
#define FLEXQR_MAX_SIZE    (17 + 4 * FLEXQR_MAX_VERSION)   /* 57 modulos de lado */
#define FLEXQR_BUF_BYTES   (FLEXQR_MAX_SIZE * FLEXQR_MAX_SIZE)

// Niveles de correccion de errores, en el orden del ESTANDAR (no en el
// de "menos a mas"): asi el valor es directamente el que va en la
// informacion de formato.
enum { FLEXQR_ECC_M = 0, FLEXQR_ECC_L = 1, FLEXQR_ECC_H = 2, FLEXQR_ECC_Q = 3 };

enum {
  FLEXQR_OK = 0,
  FLEXQR_E_BADARG,      // argumento invalido
  FLEXQR_E_TOOLONG,     // el texto no cabe ni en la version 10
  FLEXQR_E_NOMEM        // el buffer de modulos se queda corto
};

// Cada byte del buffer es un modulo:
//   bit 0 -> 1 = oscuro
//   bit 1 -> 1 = patron de funcion (no se enmascara ni lleva datos)
#define FLEXQR_DARK  0x01
#define FLEXQR_FUNC  0x02

// Codifica 'text' (terminado en cero) en 'modules'. Elige la version mas
// pequena en la que quepa, a partir de 'minVersion'. Devuelve FLEXQR_OK y
// escribe el lado en '*size' y la version en '*version'.
int flexQrEncode(const char* text, int ecc, int minVersion,
                 uint8_t* modules, size_t cap, int* size, int* version);

// Lado de la rejilla de una version.
static inline int flexQrSize(int version){ return 17 + 4 * version; }

// Estructura de bloques de una version y nivel. La necesita el
// codificador y tambien la prueba de host, que la usa para VOLVER A LEER
// el codigo generado: si las dos leyeran tablas distintas, la prueba no
// probaria nada.
typedef struct {
  int eccPerBlock;      // codewords de correccion por bloque
  int blocks1, data1;   // bloques del grupo 1 y sus codewords de datos
  int blocks2, data2;   // grupo 2 (0 si no hay)
  int totalCodewords;   // datos + correccion
  int dataCodewords;    // solo datos
} FlexQrBlocks;
int flexQrBlockInfo(int version, int ecc, FlexQrBlocks* out);

// Posiciones de los centros de los patrones de alineacion. Devuelve
// cuantas escribio (0 para la version 1).
int flexQrAlignPositions(int version, int* out, int maxn);

#ifdef __cplusplus
}
#endif
