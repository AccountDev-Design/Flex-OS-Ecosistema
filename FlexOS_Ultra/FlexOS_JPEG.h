// #############################################################
// ##  FlexOS_JPEG.h  ·  DECODIFICADOR JPEG BASELINE PORTABLE
// ##  Comun a las tres placas: ESP32-P4 (Ultra), ESP32-S3
// ##  (Ultra S3) y ESP32 clasico (Pro).
// #############################################################
//
//  POR QUE EXISTE ESTE ARCHIVO
//  ---------------------------
//  El navegador de FlexOS recibe la pagina ya rasterizada desde el
//  servicio remoto. Ese transporte tiene que ser una imagen
//  COMPRIMIDA (un frame de 480x800 en RGB565 son 768 KB: ni cabe en
//  RAM ni pasa por Wi-Fi a un ritmo usable), y el unico formato que
//  se puede decodificar en un microcontrolador con unas decenas de
//  KB de memoria y sin coma flotante es JPEG baseline.
//
//  Hasta ahora el proyecto NO tenia ningun decodificador de imagen:
//  ni una llamada a tjpgd, JPEGDEC o esp_jpeg. Se escribe aqui, sin
//  dependencias externas, por tres motivos concretos:
//
//    1) PORTABILIDAD REAL. esp32-camera (que trae jpg2rgb565) solo
//       esta garantizado en las placas con soporte de camara; el
//       tjpgd de ROM no expone la misma API en las tres familias.
//       Este fichero compila igual en P4, S3, ESP32 clasico y en un
//       PC de escritorio (de ahi que se pueda PROBAR de verdad).
//    2) MEMORIA ACOTADA. Decodifica por FILAS DE MCU y entrega el
//       resultado con un callback fila a fila, asi que nunca existe
//       la imagen entera descomprimida en memoria. Es lo que permite
//       pintar una pagina de 480x800 con ~20 KB de trabajo.
//    3) ESCALADO EN LA DECODIFICACION. Puede entregar la imagen a
//       1/1, 1/2, 1/4 o 1/8 sin construir antes la version grande,
//       que es justo lo que necesita una miniatura o una foto que
//       llega mas grande que el hueco donde va.
//
//  QUE SOPORTA Y QUE NO (sin promesas de mas)
//  ------------------------------------------
//    SI  · JPEG baseline secuencial (SOF0) y secuencial extendido
//          de 8 bits (SOF1), que es lo que produce el servicio.
//        · 1 componente (escala de grises) y 3 componentes YCbCr.
//        · Submuestreo 4:4:4, 4:2:2, 4:4:0 y 4:2:0 (factores 1 o 2).
//        · Marcadores de reinicio (DRI/RSTn).
//        · Tablas Huffman y de cuantizacion multiples.
//    NO  · Progresivo (SOF2), aritmetico, 12 bits, CMYK/YCCK,
//          jerarquico o sin perdidas. Devuelve
//          FLEXJPG_ERR_UNSUPPORTED y el llamante muestra un error
//          claro: NO se intenta adivinar el contenido.
//
//  El servicio remoto tiene prohibido emitir progresivo justamente
//  por esto (ver server/README.md, opcion `progressive: false`).

#ifndef FLEXOS_JPEG_H
#define FLEXOS_JPEG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// -------------------------------------------------------------
//  Codigos de resultado. Nunca se devuelve "casi bien": si algo no
//  cuadra, la decodificacion para y el llamante decide que pintar.
// -------------------------------------------------------------
enum {
  FLEXJPG_OK = 0,
  FLEXJPG_ERR_ARG = -1,          // parametros invalidos
  FLEXJPG_ERR_TRUNCATED = -2,    // se acabaron los bytes antes de tiempo
  FLEXJPG_ERR_BADMARKER = -3,    // no es un JPEG o la cabecera esta corrupta
  FLEXJPG_ERR_UNSUPPORTED = -4,  // progresivo, 12 bits, CMYK, factores >2...
  FLEXJPG_ERR_MEMORY = -5,       // no hay RAM para los buffers de trabajo
  FLEXJPG_ERR_TOOBIG = -6,       // supera los limites de dimensiones pedidos
  FLEXJPG_ERR_HUFFMAN = -7,      // codigo Huffman invalido (datos corruptos)
  FLEXJPG_ERR_ABORTED = -8       // el callback pidio parar
};

// Limite duro de seguridad: una cabecera manipulada no puede hacer
// que se reserven cientos de MB. 4096 px de lado cubre de sobra
// cualquier cosa que el servicio pueda enviar a estas pantallas.
#define FLEXJPG_MAX_DIM        4096
// Numero maximo de componentes que se aceptan (1 = gris, 3 = YCbCr).
#define FLEXJPG_MAX_COMPONENTS 3

typedef struct {
  int width, height;      // dimensiones REALES de la imagen (SOF)
  int outWidth, outHeight;// dimensiones que se van a entregar (ya escaladas)
  int components;         // 1 o 3
  int scaleDenom;         // 1, 2, 4 u 8 -- divisor aplicado
  bool progressive;       // true si era progresivo (y por tanto no se decodifico)
} FlexJpegInfo;

// Callback de salida. Se llama UNA VEZ por fila de la imagen ya
// escalada, en orden de arriba a abajo, con `w` pixeles RGB565.
// El puntero `rgb` solo es valido durante la llamada: el llamante
// debe copiar lo que necesite. Devolver false ABORTA la
// decodificacion (util para cancelar al cambiar de pagina).
typedef bool (*FlexJpegRowCb)(void* user, int y, int w, const uint16_t* rgb);

// Reserva/liberacion. Se pasan desde fuera para que cada placa
// decida de donde salen los buffers (PSRAM en Ultra/S3, heap
// interno en Pro). Si se pasan NULL se usan malloc/free.
typedef void* (*FlexJpegAlloc)(size_t n);
typedef void  (*FlexJpegFree)(void* p);

// -------------------------------------------------------------
//  Lee SOLO la cabecera (SOI..SOF) y rellena `info`. No decodifica
//  ni reserva memoria: sirve para decidir por adelantado si una
//  imagen cabe, o para rechazarla sin gastar nada.
// -------------------------------------------------------------
int flexJpegProbe(const uint8_t* data, size_t len, FlexJpegInfo* info);

// -------------------------------------------------------------
//  Decodifica `data` entregando el resultado fila a fila.
//
//    maxW, maxH : caja de destino. El decodificador elige el mayor
//                 divisor (1, 2, 4, 8) necesario para que la salida
//                 quepa. <= 0 en ambos = sin restriccion (1:1).
//    maxPixels  : tope de pixeles de SALIDA. Cero = sin tope. Es la
//                 defensa contra una imagen enorme que si "cabe"
//                 tras escalar pero costaria segundos de CPU.
//    info       : opcional, se rellena antes de la primera fila.
//
//  Memoria de trabajo: se reserva al entrar y se libera SIEMPRE al
//  salir, tambien en los caminos de error. Ronda
//  (anchoMCU * 8 * vmax) bytes por componente mas ~5 KB fijos.
// -------------------------------------------------------------
int flexJpegDecode(const uint8_t* data, size_t len,
                   int maxW, int maxH, uint32_t maxPixels,
                   FlexJpegInfo* info,
                   FlexJpegRowCb cb, void* user,
                   FlexJpegAlloc af, FlexJpegFree ff);

// Texto corto y estable para un codigo de error (para la interfaz).
const char* flexJpegErrStr(int err);

#ifdef __cplusplus
}
#endif

#endif // FLEXOS_JPEG_H
