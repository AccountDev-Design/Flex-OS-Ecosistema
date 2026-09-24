// #############################################################
// ##  FlexOS_FS · flujos y movimiento entre carpetas (host)
// #############################################################
//
// El codigo REAL de FlexOS_FS.cpp contra el doble de LittleFS de
// fsstub/. Cubre lo que usa Flex Web Server y la biblioteca de medios:
// escribir un archivo por trozos sin tenerlo entero en memoria, leerlo
// por trozos y a saltos, y moverlo de la carpeta temporal a la suya sin
// pisar nada. Con fallos provocados (escritura que falla a mitad).
#include <cstdio>
#include <cstring>
#include <vector>
#include "FS.h"                 // el doble (fsstub): gFs y los interruptores de fallo
#include "FlexOS_FS.h"

void fsStubReset();

static int g_run = 0, g_fail = 0;
#define CHECK(c, ...) do { g_run++; if(!(c)){ g_fail++; std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

int main(){
  std::printf("=== FlexOS_FS: flujos y movimientos ===\n");
  fsStubReset();
  CHECK(flexFsBegin(), "monta");

  std::vector<uint8_t> data(6001);
  for(size_t i = 0; i < data.size(); i++) data[i] = (uint8_t)(i * 7 + 3);

  const char* tmp = "/System/Media/tmp/up-1.part";
  FlexFsStream* w = flexFsOpenWrite(tmp);
  CHECK(w != nullptr, "abre para escribir en una carpeta que aun no existe");
  CHECK(gFs.count("/System/Media") && gFs["/System/Media"].dir && gFs.count("/System/Media/tmp") && gFs["/System/Media/tmp"].dir,
        "crea las carpetas del camino");
  CHECK(flexFsStreamWrite(w, data.data(), 1000) && flexFsStreamWrite(w, data.data() + 1000, 5000) &&
        flexFsStreamWrite(w, data.data() + 6000, 1) && flexFsStreamWrite(w, nullptr, 0), "tres trozos (y uno vacio)");
  CHECK(flexFsStreamSize(w) == 6001, "tamano mientras se escribe: %u", flexFsStreamSize(w));
  flexFsStreamClose(w);
  CHECK(flexFsSize(tmp) == 6001, "en disco, entero");

  FlexFsStream* r = flexFsOpenRead(tmp);
  CHECK(r != nullptr && flexFsStreamSize(r) == 6001, "abre para leer");
  std::vector<uint8_t> back;
  uint8_t buf[777];
  int n;
  while((n = flexFsStreamRead(r, buf, sizeof(buf))) > 0) back.insert(back.end(), buf, buf + n);
  CHECK(n == 0 && back == data, "leido a trozos de 777: identico y 0 al final");
  CHECK(flexFsStreamSeek(r, 5000) && flexFsStreamRead(r, buf, 10) == 10 && !std::memcmp(buf, data.data() + 5000, 10), "salto a 5000");
  CHECK(!flexFsStreamSeek(r, 7000), "saltar mas alla del final: no");
  flexFsStreamClose(r);

  CHECK(flexFsOpenRead("/System/Media") == nullptr, "una carpeta no se abre como archivo");
  CHECK(flexFsOpenRead("/no/existe.jpg") == nullptr, "lo que no existe: NULL");
  CHECK(flexFsOpenWrite("relativa.txt") == nullptr, "ruta sin '/' inicial: no");
  CHECK(flexFsStreamRead(nullptr, buf, 1) == -1 && !flexFsStreamWrite(nullptr, buf, 1) && !flexFsStreamSeek(nullptr, 0) &&
        flexFsStreamSize(nullptr) == 0, "flujo nulo: todo falla sin romper nada");
  flexFsStreamClose(nullptr);

  CHECK(flexFsMove(tmp, "/Imagenes/Playa.jpg"), "mueve de tmp a /Imagenes (que no existia)");
  CHECK(!flexFsExists(tmp) && flexFsSize("/Imagenes/Playa.jpg") == 6001, "el original ya no esta y el destino es el mismo archivo");
  FlexFsStream* w2 = flexFsOpenWrite("/System/Media/tmp/otra.part");
  flexFsStreamWrite(w2, data.data(), 10);
  flexFsStreamClose(w2);
  CHECK(!flexFsMove("/System/Media/tmp/otra.part", "/Imagenes/Playa.jpg"), "no pisa un archivo que ya existe");
  CHECK(flexFsSize("/Imagenes/Playa.jpg") == 6001 && flexFsSize("/System/Media/tmp/otra.part") == 10, "y los dos siguen intactos");
  CHECK(!flexFsMove("/System/Media/tmp/nada", "/Imagenes/x.jpg"), "mover lo que no existe: false");
  CHECK(flexFsMove("/Imagenes/Playa.jpg", "/Imagenes/Playa.jpg"), "a si mismo: nada que hacer");

  // Escritura que falla a mitad (sin espacio): se dice, no se finge.
  FlexFsStream* w3 = flexFsOpenWrite("/System/Media/tmp/llena.part");
  gFsFailWriteAfter = gFsWriteCount + 1;
  bool a = flexFsStreamWrite(w3, data.data(), 100);
  bool b = flexFsStreamWrite(w3, data.data(), 100);
  gFsFailWriteAfter = -1;
  flexFsStreamClose(w3);
  CHECK(a && !b, "la segunda escritura falla y se sabe");

  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
