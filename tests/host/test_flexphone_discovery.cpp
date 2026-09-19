// #############################################################
//  test_flexphone_discovery.cpp  ·  los bytes del descubrimiento
// #############################################################
//
//  POR QUE EXISTE ESTE FICHERO
//  ---------------------------
//  El emparejamiento fallaba con el sintoma mas dificil de todos:
//  "el reloj no aparece en la lista" -- sin ningun error, ni en el
//  telefono ni en el puerto serie. Cuando el fallo esta en un
//  desplazamiento mal contado dentro de un paquete UDP, no hay nada
//  que leer: el paquete sale, llega, y el otro lado simplemente no
//  lo reconoce.
//
//  Estos cuatro paquetes estan escritos DOS VECES -- aqui y en
//  android/.../protocol/Discovery.kt --, asi que la unica forma de
//  asegurar que encajan es fijar los bytes y compararlos en los dos
//  lados. Los VECTORES DE ABAJO son los mismos que comprueba
//  DiscoveryTest.kt.
//
//  La otra mitad de la bateria es hostil: paquetes cortados, que
//  mienten sobre su propia longitud, o con campos imposibles. Todo
//  esto viene de la red y puede estar hecho a mano.

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

#include "../../FlexOS_Ultra/FlexOS_FlexPhone_Discovery.h"

// Los mismos huecos que usa el firmware en FlexOS_FlexPhone_WiFi.h.
// Se repiten aqui a proposito: la cabecera de arriba es PURA y no
// arrastra ni FlexOS_FlexAuth.h ni FlexOS_FlexPhone.h, que si son del
// sketch. Si alla se encogieran, la prueba seguiria comprobando lo
// que importa -- que nada se sale del hueco que se le da.
#define FLXA_ID_MAX       32
#define FLP_DEVNAME_MAX   32

// Vuelca un paquete en hexadecimal, para poder pegarlo en el otro lado.
static void hex(char* out, size_t outN, const uint8_t* p, int n){
  size_t at = 0;
  for(int i = 0; i < n && at + 3 < outN; i++) at += (size_t)snprintf(out + at, outN - at, "%02X", p[i]);
  out[at] = 0;
}

static bool hexEq(const uint8_t* p, int n, const char* want){
  char got[256];
  hex(got, sizeof(got), p, n);
  if(strcmp(got, want) == 0) return true;
  std::printf("    obtenido %s\n    esperado %s\n", got, want);
  return false;
}

int main(){
  std::printf("\n=== FlexOS · bytes del descubrimiento de Flex Phone ===\n");

  // ---------------------------------------------------------
  //  1) Vectores fijos. Si uno cambia, cambia el protocolo.
  // ---------------------------------------------------------
  {
    std::printf("[vectores] los mismos bytes que DiscoveryTest.kt\n");
    uint8_t b[128];

    // El reloj pregunta. "FLEXPHONE?" = 464C455850484F4E453F
    int n = fpdBuildProbe(b, sizeof(b), 2);
    CHECK(n == 11, "la sonda mide %d B, esperaba 11", n);
    CHECK(hexEq(b, n, "464C455850484F4E453F02"), "sonda del reloj distinta");

    // El telefono pregunta. "FLEXOS?" = 464C45584F533F
    n = fpdBuildAsk(b, sizeof(b), 2, 47820, "Galaxy A55");
    CHECK(hexEq(b, n, "464C45584F533F02CCBA0A47616C61787920413535"),
          "sonda del telefono distinta");

    // El reloj contesta, sin emparejamiento en curso.
    n = fpdBuildAnswer(b, sizeof(b), 2, 0, "flexos-1", "Flex OS Ultra");
    CHECK(hexEq(b, n, "464C45584F5321020008666C65786F732D310D466C6578204F5320556C747261"),
          "respuesta del reloj distinta");

    // Y con el codigo en pantalla: cambia UN bit.
    n = fpdBuildAnswer(b, sizeof(b), 2, FLPW_FLAG_PAIRING, "flexos-1", "Flex OS Ultra");
    CHECK(b[FLPW_ASK_LEN + 1] == 0x01, "la bandera de emparejamiento no viaja");
  }

  // ---------------------------------------------------------
  //  2) Ida y vuelta
  // ---------------------------------------------------------
  {
    std::printf("[ida y vuelta] lo que se escribe es lo que se lee\n");
    uint8_t b[128];
    int n = fpdBuildAsk(b, sizeof(b), FLNK_VERSION, 47820, "Galaxy A55");
    uint8_t ver = 0; uint16_t port = 0; char name[FLP_DEVNAME_MAX];
    CHECK(fpdParseAsk(b, n, &ver, &port, name, sizeof(name)), "no se reconocio la sonda");
    CHECK(ver == FLNK_VERSION, "version %u", ver);
    CHECK(port == 47820, "puerto %u", port);
    CHECK(strcmp(name, "Galaxy A55") == 0, "nombre \"%s\"", name);

    n = fpdBuildAnswer(b, sizeof(b), FLNK_VERSION, FLPW_FLAG_PAIRING, "flexos-1", "Flex OS Ultra");
    uint8_t flags = 0; char id[FLXA_ID_MAX]; char nm[FLP_DEVNAME_MAX];
    CHECK(fpdParseAnswer(b, n, &ver, &flags, id, sizeof(id), nm, sizeof(nm)),
          "no se reconocio la respuesta");
    CHECK(flags == FLPW_FLAG_PAIRING, "banderas %u", flags);
    CHECK(strcmp(id, "flexos-1") == 0, "identificador \"%s\"", id);
    CHECK(strcmp(nm, "Flex OS Ultra") == 0, "nombre \"%s\"", nm);

    n = fpdBuildProbe(b, sizeof(b), FLNK_VERSION);
    CHECK(fpdIsProbe(b, n), "no se reconocio la sonda del reloj");
    CHECK(!fpdIsAsk(b, n), "una sonda del reloj se tomo por una del telefono");
    CHECK(!fpdIsAnswer(b, n), "una sonda del reloj se tomo por una respuesta");
  }

  // ---------------------------------------------------------
  //  3) No se confunden entre si
  // ---------------------------------------------------------
  {
    std::printf("[marcas] cada paquete solo lo reconoce quien debe\n");
    uint8_t ask[64], ans[64], reply[64];
    const int na = fpdBuildAsk(ask, sizeof(ask), 2, 1, "x");
    const int nn = fpdBuildAnswer(ans, sizeof(ans), 2, 0, "a", "b");
    // La respuesta del telefono se construye a mano: la escribe Android.
    int nr = 0;
    memcpy(reply, FLPW_REPLY, FLPW_PROBE_LEN); nr = FLPW_PROBE_LEN;
    reply[nr++] = 2; reply[nr++] = 0xCC; reply[nr++] = 0xBA; reply[nr++] = 0;   // 47820, byte bajo primero

    CHECK(fpdIsAsk(ask, na) && !fpdIsAnswer(ask, na) && !fpdIsReply(ask, na),
          "\"FLEXOS?\" se confunde con otro paquete");
    CHECK(fpdIsAnswer(ans, nn) && !fpdIsAsk(ans, nn) && !fpdIsReply(ans, nn),
          "\"FLEXOS!\" se confunde con otro paquete");
    CHECK(fpdIsReply(reply, nr) && !fpdIsAsk(reply, nr) && !fpdIsAnswer(reply, nr),
          "\"FLEXPHONE!\" se confunde con otro paquete");

    uint16_t port = 0;
    CHECK(fpdParseReply(reply, nr, nullptr, &port, nullptr, 0), "no se leyo la respuesta");
    CHECK(port == 47820, "puerto %u, esperaba 47820", port);
  }

  // ---------------------------------------------------------
  //  4) PAQUETES HOSTILES
  // ---------------------------------------------------------
  {
    std::printf("[hostil] paquetes cortados, mentirosos e imposibles\n");
    uint8_t b[128];
    const int full = fpdBuildAnswer(b, sizeof(b), 2, 0, "flexos-1", "Flex OS Ultra");

    // Cortado en TODAS las posiciones posibles: ninguna puede leer
    // fuera del buffer ni devolver basura como si fuera un nombre.
    for(int cut = 0; cut <= full; cut++){
      uint8_t ver = 0xEE, flags = 0xEE;
      char id[FLXA_ID_MAX], nm[FLP_DEVNAME_MAX];
      const bool ok = fpdParseAnswer(b, cut, &ver, &flags, id, sizeof(id), nm, sizeof(nm));
      if(ok){
        // Si dice que lo entendio, las cadenas TIENEN que estar
        // terminadas y ser prefijos de las de verdad.
        CHECK(strlen(id) <= strlen("flexos-1"), "identificador mas largo que el original con %d B", cut);
        CHECK(strlen(nm) <= strlen("Flex OS Ultra"), "nombre mas largo que el original con %d B", cut);
        CHECK(strncmp(id, "flexos-1", strlen(id)) == 0, "identificador inventado con %d B", cut);
      }
    }

    // Un paquete que MIENTE: dice que el nombre mide 200 bytes.
    uint8_t liar[32];
    int n = 0;
    memcpy(liar, FLPW_ANS, FLPW_ASK_LEN); n = FLPW_ASK_LEN;
    liar[n++] = 2; liar[n++] = 0;
    liar[n++] = 200;                      // longitud imposible
    liar[n++] = 'A'; liar[n++] = 'B';
    char id[FLXA_ID_MAX], nm[FLP_DEVNAME_MAX];
    CHECK(fpdParseAnswer(liar, n, nullptr, nullptr, id, sizeof(id), nm, sizeof(nm)),
          "no se leyo un paquete mentiroso");
    CHECK(strcmp(id, "AB") == 0, "se copio mas de lo que habia: \"%s\"", id);
    CHECK(nm[0] == 0, "se invento un nombre: \"%s\"", nm);

    // Un nombre mas largo que el hueco de destino: se corta, no se
    // desborda. (ASan falla la bateria entera si no fuera asi.)
    char largo[FLPW_NAME_MAX + 1];
    memset(largo, 'z', sizeof(largo) - 1);
    largo[sizeof(largo) - 1] = 0;
    n = fpdBuildAnswer(b, sizeof(b), 2, 0, largo, largo);
    char chico[8];
    CHECK(fpdParseAnswer(b, n, nullptr, nullptr, chico, sizeof(chico), nm, sizeof(nm)),
          "no se leyo un paquete con nombres largos");
    CHECK(strlen(chico) == sizeof(chico) - 1, "no se lleno el hueco pequeno (%zu)", strlen(chico));
    CHECK(strcmp(nm, largo) == 0, "el segundo campo se descoloco tras cortar el primero");
  }

  // ---------------------------------------------------------
  //  5) Argumentos imposibles
  // ---------------------------------------------------------
  {
    std::printf("[bordes] nulos y huecos que no dan\n");
    uint8_t b[4];
    CHECK(fpdBuildProbe(nullptr, 100, 2) == 0, "acepto un destino nulo");
    CHECK(fpdBuildProbe(b, sizeof(b), 2) == 0, "escribio en un hueco que no daba");
    CHECK(fpdBuildAsk(b, sizeof(b), 2, 1, "x") == 0, "escribio una sonda que no cabia");
    CHECK(fpdBuildAnswer(b, sizeof(b), 2, 0, "x", "y") == 0, "escribio una respuesta que no cabia");
    CHECK(!fpdIsAsk(nullptr, 100), "reconocio un paquete nulo");
    CHECK(!fpdParseAsk(nullptr, 100, nullptr, nullptr, nullptr, 0), "leyo un paquete nulo");

    // Sin nombre: sigue siendo un paquete valido.
    uint8_t c[64];
    const int n = fpdBuildAsk(c, sizeof(c), 2, 47820, nullptr);
    uint16_t port = 0; char nm[8];
    CHECK(fpdParseAsk(c, n, nullptr, &port, nm, sizeof(nm)), "una sonda sin nombre se rechazo");
    CHECK(port == 47820, "puerto %u", port);
    CHECK(nm[0] == 0, "se invento un nombre");
  }

  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
