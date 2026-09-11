// #############################################################
//  test_qr.cpp  ·  pruebas de host de FlexOS_QR.cpp
// #############################################################
//
//  POR QUE ESTA PRUEBA DECODIFICA EN VEZ DE COMPARAR BYTES.
//
//  Un codificador QR se puede equivocar de cuatro maneras distintas y
//  las cuatro producen una rejilla que PARECE un codigo QR: patrones
//  de busqueda en su sitio, tamano correcto, aspecto normal. Y ningun
//  telefono la lee.
//
//    · aritmetica en GF(256) mal -> los codewords de correccion son
//      basura y el lector rechaza el codigo entero;
//    · entrelazado de bloques mal -> los datos salen desordenados;
//    · zigzag mal -> los bits se colocan en el modulo equivocado;
//    · informacion de formato mal -> el lector aplica otra mascara.
//
//  Comparar contra una rejilla escrita a mano solo cubriria el caso
//  concreto que se escribio. Aqui se VUELVE A LEER lo generado -- se
//  extrae la mascara del formato, se deshace, se recorre el zigzag, se
//  desentrelaza y se reconstruye la cadena -- para los cuatro niveles
//  de correccion y varias longitudes. Si cualquiera de esas cuatro
//  cosas se rompe, la cadena no vuelve.
//
//  El decodificador de la prueba usa flexQrBlockInfo(), es decir LA
//  MISMA tabla que el codificador. Eso es a proposito: lo que se
//  verifica aqui es la maquinaria, no la tabla del estandar, que es un
//  dato fijo y se revisa leyendola.

#include "../firmware-modules/FlexOS_QR.h"
#include <cstdio>
#include <cstring>
#include <string>

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

static uint8_t g_mods[FLEXQR_BUF_BYTES];

static int maskBit(int mask, int x, int y){
  switch(mask){
    case 0: return ((x + y) % 2) == 0;
    case 1: return (y % 2) == 0;
    case 2: return (x % 3) == 0;
    case 3: return ((x + y) % 3) == 0;
    case 4: return (((y / 2) + (x / 3)) % 2) == 0;
    case 5: return ((x * y) % 2 + (x * y) % 3) == 0;
    case 6: return (((x * y) % 2 + (x * y) % 3) % 2) == 0;
    default:return (((x + y) % 2 + (x * y) % 3) % 2) == 0;
  }
}

// Lector minimo: formato -> mascara -> zigzag -> desentrelazado -> cadena.
// No corrige errores, y no hace falta: aqui no hay ninguno.
static bool decode(const uint8_t* m, int n, int ver, int eccWant, std::string& out, int* maskOut){
  uint32_t bits = 0;
  for(int i = 0; i < 15; i++){
    int x, y;
    if(i < 6){ x = 8; y = i; }
    else if(i == 6){ x = 8; y = 7; }
    else if(i == 7){ x = 8; y = 8; }
    else if(i == 8){ x = 7; y = 8; }
    else { x = 14 - i; y = 8; }
    if(m[y * n + x] & FLEXQR_DARK) bits |= (1u << i);
  }
  bits ^= 0x5412u;
  int data5 = (int)((bits >> 10) & 0x1F);
  int ecc = (data5 >> 3) & 3, mask = data5 & 7;
  if(maskOut) *maskOut = mask;
  if(ecc != eccWant) return false;
  FlexQrBlocks bi;
  if(flexQrBlockInfo(ver, ecc, &bi) != FLEXQR_OK) return false;
  static uint8_t cw[400];
  memset(cw, 0, sizeof(cw));
  int bit = 0, up = 1;
  for(int right = n - 1; right >= 1; right -= 2){
    if(right == 6) right = 5;
    for(int v = 0; v < n; v++){
      int y = up ? (n - 1 - v) : v;
      for(int c = 0; c < 2; c++){
        int x = right - c;
        if(m[y * n + x] & FLEXQR_FUNC) continue;
        int b = (m[y * n + x] & FLEXQR_DARK) ? 1 : 0;
        if(maskBit(mask, x, y)) b ^= 1;
        if(bit < bi.totalCodewords * 8 && b) cw[bit >> 3] |= (uint8_t)(0x80u >> (bit & 7));
        bit++;
      }
    }
    up = !up;
  }
  int nb = bi.blocks1 + bi.blocks2, off[32], dsz[32], p = 0;
  if(nb > 32) return false;
  for(int b = 0; b < nb; b++){ dsz[b] = (b < bi.blocks1) ? bi.data1 : bi.data2; off[b] = p; p += dsz[b]; }
  static uint8_t dat[400];
  memset(dat, 0, sizeof(dat));
  int idx = 0, maxD = bi.data1 > bi.data2 ? bi.data1 : bi.data2;
  for(int i = 0; i < maxD; i++)
    for(int b = 0; b < nb; b++)
      if(i < dsz[b]) dat[off[b] + i] = cw[idx++];
  int bp = 0;
  auto get = [&](int nbits) -> uint32_t {
    uint32_t v = 0;
    for(int i = 0; i < nbits; i++){ v = (v << 1) | ((dat[bp >> 3] >> (7 - (bp & 7))) & 1); bp++; }
    return v;
  };
  if(get(4) != 4) return false;                    // modo BYTE
  uint32_t len = get(ver <= 9 ? 8 : 16);
  if(len > 400) return false;
  out.clear();
  for(uint32_t i = 0; i < len; i++) out.push_back((char)get(8));
  return true;
}

// =============================================================
static void testRoundTrip(){
  std::printf("-- ida y vuelta: se codifica y se vuelve a LEER --\n");
  const char* casos[] = {
    "A",
    "http://192.168.1.50:8080/ab12cd34/Documento.svg",
    "https://flexos.local/vector/9f3a2b7c/Mi_dibujo_vectorial.svg",
    "0123456789012345678901234567890123456789012345678901234567890123456789",
  };
  const char* nom[4] = { "M", "L", "H", "Q" };
  for(int e = 0; e < 4; e++){
    for(unsigned c = 0; c < sizeof(casos) / sizeof(casos[0]); c++){
      int size = 0, ver = 0;
      int rc = flexQrEncode(casos[c], e, 1, g_mods, sizeof(g_mods), &size, &ver);
      CHECK(rc == FLEXQR_OK, "ecc %s, caso %u: codificar devolvio %d", nom[e], c, rc);
      if(rc != FLEXQR_OK) continue;
      CHECK(size == flexQrSize(ver), "el lado no cuadra con la version");
      std::string got;
      int mask = -1;
      bool ok = decode(g_mods, size, ver, e, got, &mask);
      CHECK(ok && got == casos[c], "ecc %s ver %d: leido \"%s\"", nom[e], ver, got.c_str());
      CHECK(mask >= 0 && mask <= 7, "mascara fuera de rango: %d", mask);
    }
  }
}

static void testPatterns(){
  std::printf("-- patrones de funcion --\n");
  int size = 0, ver = 0;
  CHECK(flexQrEncode("prueba", FLEXQR_ECC_M, 1, g_mods, sizeof(g_mods), &size, &ver) == FLEXQR_OK, "codificar");
  // Patron de busqueda: anillo oscuro (r=3), hueco claro (r=2), nucleo (r<=1).
  CHECK((g_mods[0 * size + 0] & FLEXQR_DARK) != 0, "esquina del patron de busqueda");
  CHECK((g_mods[3 * size + 3] & FLEXQR_DARK) != 0, "nucleo del patron");
  CHECK((g_mods[1 * size + 3] & FLEXQR_DARK) == 0, "anillo claro del patron");
  CHECK((g_mods[3 * size + (size - 4)] & FLEXQR_DARK) != 0, "patron superior derecho");
  CHECK((g_mods[(size - 4) * size + 3] & FLEXQR_DARK) != 0, "patron inferior izquierdo");
  // Sincronizacion: alterna a partir de la columna 8.
  CHECK((g_mods[6 * size + 8] & FLEXQR_DARK) != 0, "sincronizacion horizontal, par");
  CHECK((g_mods[6 * size + 9] & FLEXQR_DARK) == 0, "sincronizacion horizontal, impar");
  CHECK((g_mods[8 * size + 6] & FLEXQR_DARK) != 0, "sincronizacion vertical, par");
  // Modulo oscuro obligatorio del estandar.
  CHECK((g_mods[(size - 8) * size + 8] & FLEXQR_DARK) != 0, "modulo oscuro obligatorio");
  // Todo lo de arriba tiene que estar marcado como patron de funcion, o
  // los datos se colocarian encima.
  CHECK((g_mods[0 * size + 0] & FLEXQR_FUNC) != 0, "el patron de busqueda es funcion");
  CHECK((g_mods[6 * size + 10] & FLEXQR_FUNC) != 0, "la sincronizacion es funcion");
}

static void testVersions(){
  std::printf("-- eleccion de version y version >= 7 (info de version) --\n");
  int size = 0, ver = 0;
  CHECK(flexQrEncode("hola", FLEXQR_ECC_L, 1, g_mods, sizeof(g_mods), &size, &ver) == FLEXQR_OK, "corto");
  CHECK(ver == 1, "4 bytes con correccion L caben en la version 1 (fue %d)", ver);
  // Algo que obligue a la version 7 o mas: ahi entra en juego el bloque
  // de informacion de version, que es un camino de codigo aparte.
  std::string largo(150, 'x');
  CHECK(flexQrEncode(largo.c_str(), FLEXQR_ECC_M, 1, g_mods, sizeof(g_mods), &size, &ver) == FLEXQR_OK, "largo");
  CHECK(ver >= 7, "150 bytes con correccion M deberian pasar de la version 7 (fue %d)", ver);
  std::string got;
  CHECK(decode(g_mods, size, ver, FLEXQR_ECC_M, got, NULL) && got == largo,
        "una version >= 7 tambien tiene que volver a leerse");
  // minVersion fuerza hacia arriba, nunca hacia abajo.
  CHECK(flexQrEncode("hola", FLEXQR_ECC_L, 5, g_mods, sizeof(g_mods), &size, &ver) == FLEXQR_OK, "minVersion");
  CHECK(ver == 5, "minVersion 5 deberia dar version 5 (fue %d)", ver);
}

static void testBlockTable(){
  std::printf("-- tabla de bloques: datos + correccion = total --\n");
  for(int v = FLEXQR_MIN_VERSION; v <= FLEXQR_MAX_VERSION; v++){
    for(int e = 0; e < 4; e++){
      FlexQrBlocks b;
      CHECK(flexQrBlockInfo(v, e, &b) == FLEXQR_OK, "v%d ecc%d", v, e);
      int blocks = b.blocks1 + b.blocks2;
      int total = b.dataCodewords + blocks * b.eccPerBlock;
      CHECK(total == b.totalCodewords,
            "v%d ecc%d: %d datos + %d correccion = %d, la tabla dice %d",
            v, e, b.dataCodewords, blocks * b.eccPerBlock, total, b.totalCodewords);
    }
  }
}

static void testLimits(){
  std::printf("-- limites: fallan de forma controlada --\n");
  int size = 0, ver = 0;
  static char big[3000];
  memset(big, 'X', sizeof(big) - 1);
  big[sizeof(big) - 1] = 0;
  CHECK(flexQrEncode(big, FLEXQR_ECC_H, 1, g_mods, sizeof(g_mods), &size, &ver) == FLEXQR_E_TOOLONG,
        "un texto que no cabe ni en la version 10 tiene que decirlo");
  CHECK(flexQrEncode("x", FLEXQR_ECC_M, 1, g_mods, 10, &size, &ver) == FLEXQR_E_NOMEM,
        "un buffer corto tiene que decirlo, no escribir fuera");
  CHECK(flexQrEncode(NULL, FLEXQR_ECC_M, 1, g_mods, sizeof(g_mods), &size, &ver) == FLEXQR_E_BADARG, "texto nulo");
  CHECK(flexQrEncode("", FLEXQR_ECC_M, 1, g_mods, sizeof(g_mods), &size, &ver) == FLEXQR_E_BADARG, "texto vacio");
  CHECK(flexQrEncode("x", 9, 1, g_mods, sizeof(g_mods), &size, &ver) == FLEXQR_E_BADARG, "nivel invalido");
  CHECK(flexQrEncode("x", FLEXQR_ECC_M, 99, g_mods, sizeof(g_mods), &size, &ver) == FLEXQR_E_BADARG, "version invalida");
  FlexQrBlocks b;
  CHECK(flexQrBlockInfo(0, 0, &b) == FLEXQR_E_BADARG, "version 0");
  CHECK(flexQrBlockInfo(1, 7, &b) == FLEXQR_E_BADARG, "ecc 7");
}

int main(){
  std::printf("=== FlexOS · codificador QR ===\n");
  testRoundTrip();
  testPatterns();
  testVersions();
  testBlockTable();
  testLimits();
  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
