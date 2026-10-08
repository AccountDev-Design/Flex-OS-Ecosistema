// #############################################################
// ##  FlexOS_JPEGEnc.h  ·  CODIFICADOR JPEG BASELINE PORTABLE
// ##  Portable: compila igual en el P4 y en un PC.
// #############################################################
//
//  POR QUE EXISTE
//  --------------
//  Hasta ahora Flex OS solo sabia LEER JPEG (FlexOS_JPEG). Dos cosas
//  nuevas necesitan ESCRIBIRLO:
//    1) las miniaturas persistentes de la biblioteca de medios: una
//       miniatura de 132x132 en RGB565 son 34 KB; en JPEG, 3-6 KB, y se
//       decodifica en un par de milisegundos en vez de volver a leer una
//       foto de varios megas en cada visita a la Galeria;
//    2) el editor de imagenes de la Galeria, que tiene que guardar lo
//       editado en el mismo formato que el P4 sabe abrir.
//
//  QUE PRODUCE, Y NADA MAS
//  -----------------------
//    · JFIF baseline secuencial (SOF0), 8 bits, YCbCr (o gris).
//    · Submuestreo 4:2:0 (fotos, lo normal en un movil) o 4:4:4.
//    · Tablas de cuantizacion del anexo K escaladas por calidad (la
//      misma formula que libjpeg) y tablas Huffman estandar.
//  Es exactamente lo que FlexOS_JPEG decodifica, y la prueba de host lo
//  comprueba decodificando cada salida con ese decodificador.
//
//  MEMORIA
//  -------
//  Por FILAS DE MCU: la imagen entera no existe nunca aqui dentro. La
//  fuente entrega 8 o 16 filas cada vez y la salida se entrega a trozos
//  de 4 KB por el callback. Para 2048 px de ancho son ~110 KB de trabajo,
//  todos pedidos con el reservador del llamante (PSRAM en la placa).

#ifndef FLEXOS_JPEGENC_H
#define FLEXOS_JPEGENC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "FlexOS_JPEG.h"      // FlexJpegAlloc / FlexJpegFree: el mismo contrato

#ifdef __cplusplus
extern "C" {
#endif

enum {
  FLEXJE_OK          =  0,
  FLEXJE_ERR_ARG     = -1,
  FLEXJE_ERR_MEMORY  = -2,
  FLEXJE_ERR_WRITE   = -3,     // el destino no acepto los bytes (disco lleno, desconexion)
  FLEXJE_ERR_SOURCE  = -4      // la fuente no entrego filas (o pidio parar)
};

enum { FLEXJE_SUB_420 = 0, FLEXJE_SUB_444 = 1, FLEXJE_GRAY = 2 };
enum { FLEXJE_IN_RGB888 = 0, FLEXJE_IN_RGB565 = 1 };

#define FLEXJE_MAX_DIM 8192

typedef struct {
  int width, height;
  int quality;        // 1..100 (se acota)
  int subsampling;    // FLEXJE_SUB_* / FLEXJE_GRAY
  int input;          // FLEXJE_IN_*
} FlexJeCfg;

// La fuente rellena `rows` filas empezando en `y` en `dst` (stride =
// width * 3 para RGB888, width * 2 para RGB565). false = parar.
typedef bool (*FlexJeSrcFn)(void* ctx, int y, int rows, uint8_t* dst);
// El destino recibe los bytes del JPEG en orden. false = no se pudo.
typedef bool (*FlexJeOutFn)(void* ctx, const uint8_t* data, size_t n);

int  flexJpegEncode(const FlexJeCfg* cfg, FlexJeSrcFn src, void* srcCtx,
                    FlexJeOutFn out, void* outCtx, FlexJpegAlloc af, FlexJpegFree ff);

// Comodidad: la imagen ya esta en memoria (stride en bytes).
int  flexJpegEncodeMem(const FlexJeCfg* cfg, const void* pixels, size_t stride,
                       FlexJeOutFn out, void* outCtx, FlexJpegAlloc af, FlexJpegFree ff);

const char* flexJpegEncErrStr(int err);

#ifdef __cplusplus
}
#endif

#endif // FLEXOS_JPEGENC_H
