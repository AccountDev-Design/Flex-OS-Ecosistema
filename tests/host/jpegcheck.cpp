// #############################################################
//  jpegcheck  ·  decodifica un JPEG con el decodificador DEL
//  FIRMWARE y dice si vale.
// #############################################################
//
//  PARA QUE SIRVE
//  --------------
//  Cierra el circulo entre el servicio y el dispositivo. La prueba de
//  extremo a extremo (server/test/e2e.test.js) levanta el servicio de
//  verdad, navega a una pagina de verdad y guarda las bandas JPEG que
//  el servicio envia. Despues las pasa por ESTE programa, que usa
//  EXACTAMENTE el mismo FlexOS_JPEG.cpp que compila el firmware.
//
//  Si el servicio emitiera progresivo, un submuestreo raro o una
//  imagen mas grande de la cuenta, aqui saltaria -- en vez de
//  descubrirlo en la placa con una pantalla en blanco.
//
//  Uso:  jpegcheck fichero.jpg [anchoMax] [altoMax]
//  Salida (una linea):  OK w h comp escala suma
//  Codigo de salida: 0 si decodifica, 1 si no.

#include "../../FlexOS_JPEG.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

struct Acc { unsigned long long sum = 0; int rows = 0; int w = 0; };

static bool rowCb(void* user, int y, int w, const uint16_t* rgb){
  Acc* a = (Acc*)user;
  a->rows++; a->w = w;
  for(int i = 0; i < w; i++) a->sum = a->sum * 1000003ull + rgb[i];
  (void)y;
  return true;
}

int main(int argc, char** argv){
  if(argc < 2){ std::fprintf(stderr, "uso: jpegcheck fichero.jpg [maxW] [maxH]\n"); return 2; }
  int maxW = argc > 2 ? atoi(argv[2]) : 0;
  int maxH = argc > 3 ? atoi(argv[3]) : 0;

  FILE* f = std::fopen(argv[1], "rb");
  if(!f){ std::fprintf(stderr, "no se pudo abrir %s\n", argv[1]); return 2; }
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> buf((size_t)(n > 0 ? n : 0));
  if(n > 0 && std::fread(buf.data(), 1, (size_t)n, f) != (size_t)n){
    std::fclose(f); std::fprintf(stderr, "lectura incompleta\n"); return 2;
  }
  std::fclose(f);

  Acc acc;
  FlexJpegInfo info;
  int rc = flexJpegDecode(buf.data(), buf.size(), maxW, maxH, 0, &info, rowCb, &acc, nullptr, nullptr);
  if(rc != FLEXJPG_OK){
    std::printf("ERROR %d %s\n", rc, flexJpegErrStr(rc));
    return 1;
  }
  if(acc.rows != info.outHeight){
    std::printf("ERROR filas %d, esperadas %d\n", acc.rows, info.outHeight);
    return 1;
  }
  std::printf("OK %d %d %d %d %llu\n", info.outWidth, info.outHeight,
              info.components, info.scaleDenom, acc.sum);
  return 0;
}
