// #############################################################
// ##  FlexOS · MINIATURAS DE LA BIBLIOTECA DE MEDIOS
// ##  Portable: compila igual en el P4 y en un PC.
// #############################################################
//
//  QUE HACE
//  --------
//  Una miniatura CUADRADA de `side` pixeles, recortada al centro ("cover")
//  y sin deformar: exactamente lo que pinta una celda de la rejilla de la
//  Galeria. Sale en JPEG (FlexOS_JPEGEnc): 3-6 KB en la flash en vez de
//  34 KB en RGB565, y se vuelve a pintar en un par de milisegundos.
//
//  Y VALIDA. Generar la miniatura de un JPEG obliga a decodificarlo ENTERO
//  (a escala reducida, pero recorriendo todo su flujo entropico). Si la
//  foto que llega del movil esta danada o usa algo que el P4 no sabe leer,
//  se sabe AQUI, antes de publicarla en la biblioteca -- no la primera vez
//  que alguien intenta abrirla.
//
//  POR QUE ES PORTABLE. Lo usan dos tareas distintas de la placa (la del
//  servidor web al recibir y la de fondo de la biblioteca con lo que ya
//  estaba en el disco) y se prueba entero en el PC con sanitizers.

#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "FlexOS_JPEG.h"
#include "FlexOS_JPEGEnc.h"
#include "FlexOS_Media.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLEXTH_SIDE       132      // = celda de la rejilla de la Galeria (3 columnas en 480 px)
#define FLEXTH_QUALITY     82
#define FLEXTH_AVI_FRAME  (192u * 1024u)   // buffer inicial si el AVI no declara su mayor fotograma
#define FLEXTH_AVI_TRIES   4               // fotogramas con imagen que se prueban para la miniatura

enum {
  FLEXTH_OK          =  0,
  FLEXTH_ERR_ARG     = -1,
  FLEXTH_ERR_MEMORY  = -2,
  FLEXTH_ERR_DECODE  = -3,     // datos danados: el JPEG no se puede leer entero
  FLEXTH_ERR_UNSUP   = -4,     // JPEG progresivo / AVI sin MJPEG: el P4 no lo abre
  FLEXTH_ERR_WRITE   = -5,     // el destino no acepto la miniatura
  FLEXTH_ERR_IO      = -6      // no se pudo leer el archivo
};

// Recorte centrado que cubre un cuadrado: la mayor region de proporcion 1:1
// dentro de w x h.
void flexThumbCoverRect(int w, int h, int* x0, int* y0, int* cw, int* ch);

// Miniatura desde un JPEG en memoria. `srcW/srcH` (opcionales) reciben las
// dimensiones reales de la foto.
int  flexThumbFromJpeg(const uint8_t* jpg, size_t len, int side, int quality,
                       FlexJeOutFn out, void* outCtx, int* srcW, int* srcH,
                       FlexJpegAlloc af, FlexJpegFree ff);

// La misma miniatura LEYENDO el JPEG por trozos (un archivo de LittleFS): la
// foto nunca esta entera en RAM. Es lo que usan el servidor web al recibir
// y la tarea de fondo con lo que ya estaba en el disco. Tambien VALIDA: el
// flujo entropico se recorre entero. Un fallo de lectura da FLEXTH_ERR_IO.
int  flexThumbFromJpegStream(FlexJpegReadFn rd, void* rdCtx, int side, int quality,
                             FlexJeOutFn out, void* outCtx, int* srcW, int* srcH,
                             FlexJpegAlloc af, FlexJpegFree ff);

// Miniatura desde pixeles ya en memoria (RGB888 o RGB565, stride en bytes).
int  flexThumbFromPixels(const void* px, int w, int h, size_t stride, int input,
                         int side, int quality, FlexJeOutFn out, void* outCtx,
                         FlexJpegAlloc af, FlexJpegFree ff);

// Miniatura del PRIMER fotograma de un AVI MJPEG. Rellena ancho, alto y
// duracion del video (opcionales).
int  flexThumbFromAvi(const FlexMediaIO* io, int side, int quality,
                      FlexJeOutFn out, void* outCtx, int* w, int* h, uint32_t* durMs,
                      FlexJpegAlloc af, FlexJpegFree ff);

const char* flexThumbErrStr(int err);

#ifdef __cplusplus
}
#endif
