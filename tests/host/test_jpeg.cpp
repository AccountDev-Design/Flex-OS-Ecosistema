// #############################################################
//  test_jpeg.cpp  ·  pruebas de host del decodificador JPEG
//  de Flex OS (FlexOS_JPEG.cpp).
// #############################################################
//
//  Se compila y ejecuta en el PC (ver tests/host/Makefile). El
//  decodificador es EXACTAMENTE el mismo fichero que compila el
//  firmware -- no hay una version "de prueba" distinta.
//
//  Que se comprueba:
//    1) Salida 1:1 contra la referencia de libjpeg-turbo, en
//       4:4:4, 4:2:0, 4:2:2, 4:4:0, escala de grises y dimensiones
//       impares (ultima MCU recortada).
//    2) Marcadores de reinicio: DRI=3 y DRI=1, con predictores DC
//       que se reinician en cada intervalo.
//    3) Escalado 1/2, 1/4 y 1/8 (dimensiones y contenido coherente
//       con un promedio de caja de la referencia).
//    4) Rechazo limpio: progresivo, cabecera rota, datos truncados,
//       Huffman corrupto, imagen mayor que el limite.
//    5) Cancelacion desde el callback y ausencia de fugas.

#include "../../FlexOS_JPEG.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <pthread.h>

// ---- mini framework ----
static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

// ---- contador de reservas, para detectar fugas ----
static long g_allocs = 0;
static void* tAlloc(size_t n){ g_allocs++; return std::malloc(n); }
static void  tFree(void* p){ if(p){ g_allocs--; std::free(p); } }

static std::string fixtureDir(){
  const char* env = std::getenv("FLEXOS_FIXTURES");
  if(env && *env) return std::string(env);
  return std::string("../fixtures");
}

static bool readFile(const std::string& path, std::vector<uint8_t>& out){
  FILE* f = std::fopen(path.c_str(), "rb");
  if(!f) return false;
  std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
  out.resize((size_t)n);
  size_t rd = n > 0 ? std::fread(out.data(), 1, (size_t)n, f) : 0;
  std::fclose(f);
  return rd == (size_t)n;
}

// ---- receptor de filas ----
struct Sink {
  int w = 0, h = 0;
  std::vector<uint16_t> px;
  int rowsSeen = 0;
  int lastY = -1;
  bool orderOk = true;
  int abortAfter = -1;     // -1 = nunca aborta
};
static bool sinkRow(void* user, int y, int w, const uint16_t* rgb){
  Sink* s = (Sink*)user;
  if(y != s->lastY + 1) s->orderOk = false;   // las filas llegan en orden estricto
  s->lastY = y;
  if(s->w == 0){ s->w = w; }
  if((int)s->px.size() < (y + 1) * w) s->px.resize((size_t)(y + 1) * w, 0);
  std::memcpy(&s->px[(size_t)y * w], rgb, (size_t)w * 2);
  s->rowsSeen++;
  s->h = y + 1;
  if(s->abortAfter >= 0 && s->rowsSeen > s->abortAfter) return false;
  return true;
}

static inline void unpack565(uint16_t c, int& r, int& g, int& b){
  r = ((c >> 11) & 0x1F) * 255 / 31;
  g = ((c >> 5)  & 0x3F) * 255 / 63;
  b = ( c        & 0x1F) * 255 / 31;
}

// Compara la salida con la referencia de libjpeg-turbo y devuelve el error
// medio; el maximo se escribe en `worst`.
//
// LA UNIDAD DE MEDIDA ES EL PASO DE RGB565, no el nivel de 8 bits. La
// referencia se cuantiza con la MISMA formula que usa el decodificador y
// despues se comparan los codigos (0..31 en rojo/azul, 0..63 en verde). Es
// la unica metrica que dice algo util aqui: en 8 bits, un unico paso de
// diferencia en rojo ya "vale" 8, asi que un decodificador perfecto salvo
// redondeo daria numeros grandes y el test no distinguiria eso de un fallo
// de verdad. En pasos de 565, 0 = identico y 1 = el minimo representable.
static double compareRef(const Sink& s, const std::vector<uint8_t>& ref,
                         int refW, int refH, int& worst){
  worst = 0;
  double acc = 0; long n = 0;
  for(int y = 0; y < s.h && y < refH; y++){
    for(int x = 0; x < s.w && x < refW; x++){
      uint16_t got = s.px[(size_t)y * s.w + x];
      const uint8_t* p = &ref[((size_t)y * refW + x) * 3];
      uint16_t want = (uint16_t)(((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3));
      int d0 = std::abs((int)((got >> 11) & 0x1F) - (int)((want >> 11) & 0x1F));
      int d1 = std::abs((int)((got >> 5)  & 0x3F) - (int)((want >> 5)  & 0x3F));
      int d2 = std::abs((int)( got        & 0x1F) - (int)( want        & 0x1F));
      int d = d0 > d1 ? (d0 > d2 ? d0 : d2) : (d1 > d2 ? d1 : d2);
      if(d > worst) worst = d;
      acc += d; n++;
    }
  }
  return n ? acc / (double)n : 0.0;
}

struct Fixture { std::string name; int w, h, comps; };

static std::vector<Fixture> loadIndex(const std::string& dir){
  std::vector<Fixture> v;
  std::vector<uint8_t> raw;
  if(!readFile(dir + "/fixtures.txt", raw)) return v;
  std::string s((const char*)raw.data(), raw.size());
  size_t pos = 0;
  while(pos < s.size()){
    size_t nl = s.find('\n', pos);
    if(nl == std::string::npos) nl = s.size();
    std::string line = s.substr(pos, nl - pos);
    pos = nl + 1;
    if(line.empty()) continue;
    Fixture f; char nm[128] = {0};
    if(std::sscanf(line.c_str(), "%127s %d %d %d", nm, &f.w, &f.h, &f.comps) == 4){
      f.name = nm; v.push_back(f);
    }
  }
  return v;
}

// -------------------------------------------------------------
//  1) Decodificacion 1:1 contra la referencia
// -------------------------------------------------------------
static void testExact(const std::string& dir, const std::vector<Fixture>& fx){
  std::printf("[jpeg] decodificacion 1:1 contra libjpeg-turbo\n");
  for(const Fixture& f : fx){
    std::vector<uint8_t> jpg, ref;
    if(!readFile(dir + "/" + f.name + ".jpg", jpg)){ CHECK(false, "falta %s.jpg", f.name.c_str()); continue; }
    if(!readFile(dir + "/" + f.name + ".rgb", ref)){ CHECK(false, "falta %s.rgb", f.name.c_str()); continue; }

    Sink s; FlexJpegInfo info;
    long before = g_allocs;
    int rc = flexJpegDecode(jpg.data(), jpg.size(), 0, 0, 0, &info, sinkRow, &s, tAlloc, tFree);
    CHECK(rc == FLEXJPG_OK, "%s: rc=%d (%s)", f.name.c_str(), rc, flexJpegErrStr(rc));
    CHECK(g_allocs == before, "%s: fuga de %ld reservas", f.name.c_str(), g_allocs - before);
    if(rc != FLEXJPG_OK) continue;
    CHECK(info.width == f.w && info.height == f.h,
          "%s: dimensiones %dx%d, esperado %dx%d", f.name.c_str(), info.width, info.height, f.w, f.h);
    CHECK(info.scaleDenom == 1, "%s: divisor %d, esperado 1", f.name.c_str(), info.scaleDenom);
    CHECK(s.w == f.w && s.h == f.h, "%s: salida %dx%d", f.name.c_str(), s.w, s.h);
    CHECK(s.orderOk, "%s: las filas no llegaron en orden", f.name.c_str());

    int worst = 0;
    double mean = compareRef(s, ref, f.w, f.h, worst);

    // TOLERANCIAS, Y POR QUE SON DISTINTAS
    //  · Sin submuestreo de croma (4:4:4, gris, rejillas DC solidas) no hay
    //    ninguna decision de algoritmo por medio: el resultado tiene que
    //    coincidir con libjpeg-turbo salvo el redondeo de la IDCT entera.
    //  · Con croma submuestreado, libjpeg-turbo usa interpolacion
    //    triangular ("fancy upsampling") y este decodificador replica la
    //    muestra vecina, que es lo que hacen los decodificadores de
    //    microcontrolador (cuesta la mitad de CPU y no necesita una fila
    //    extra de croma en memoria). En un borde de color saturado eso son
    //    unos pocos niveles de diferencia en una franja de 1-2 px; el error
    //    MEDIO sigue siendo bajo, que es lo que se acota de verdad.
    bool subsampled = (f.name == "grad420" || f.name == "odd420" ||
                       f.name == "page480" || f.name == "page240");
    int    limWorst = subsampled ? 8 : 2;
    double limMean  = subsampled ? 0.60 : 0.30;
    CHECK(worst <= limWorst, "%s: error maximo %d pasos (limite %d)", f.name.c_str(), worst, limWorst);
    CHECK(mean <= limMean, "%s: error medio %.3f pasos (limite %.2f)", f.name.c_str(), mean, limMean);
    std::printf("   %-10s %4dx%-4d comp=%d  err medio %.3f  max %d  (pasos RGB565)\n",
                f.name.c_str(), f.w, f.h, info.components, mean, worst);
  }
}

// -------------------------------------------------------------
//  2) Escalado 1/2, 1/4, 1/8
// -------------------------------------------------------------
static void testScaling(const std::string& dir){
  std::printf("[jpeg] escalado en la decodificacion\n");
  std::vector<uint8_t> jpg, ref;
  if(!readFile(dir + "/page480.jpg", jpg) || !readFile(dir + "/page480.rgb", ref)){
    CHECK(false, "faltan los ficheros page480"); return;
  }
  const int W = 480, H = 800;
  const int denoms[3] = { 2, 4, 8 };
  for(int i = 0; i < 3; i++){
    int S = denoms[i];
    Sink s; FlexJpegInfo info;
    long before = g_allocs;
    // Se pide una caja justo del tamano que fuerza ese divisor.
    int rc = flexJpegDecode(jpg.data(), jpg.size(), W / S, H / S, 0, &info, sinkRow, &s, tAlloc, tFree);
    CHECK(rc == FLEXJPG_OK, "1/%d: rc=%d (%s)", S, rc, flexJpegErrStr(rc));
    CHECK(g_allocs == before, "1/%d: fuga de reservas", S);
    if(rc != FLEXJPG_OK) continue;
    CHECK(info.scaleDenom == S, "1/%d: divisor elegido %d", S, info.scaleDenom);
    CHECK(s.w == W / S && s.h == H / S, "1/%d: salida %dx%d, esperado %dx%d",
          S, s.w, s.h, W / S, H / S);

    // La imagen escalada tiene que parecerse al promedio de caja de la
    // referencia. Se compara asi y no pixel a pixel porque el promedio
    // de caja del decodificador opera sobre las muestras YCbCr, no sobre
    // el RGB ya convertido: son equivalentes salvo redondeo.
    int worst = 0; double acc = 0; long n = 0;
    for(int y = 0; y < s.h; y++){
      for(int x = 0; x < s.w; x++){
        int sr = 0, sg = 0, sb = 0, cnt = 0;
        for(int dy = 0; dy < S; dy++) for(int dx = 0; dx < S; dx++){
          int yy = y * S + dy, xx = x * S + dx;
          if(yy >= H || xx >= W) continue;
          const uint8_t* p = &ref[((size_t)yy * W + xx) * 3];
          sr += p[0]; sg += p[1]; sb += p[2]; cnt++;
        }
        if(!cnt) continue;
        int r, g, b; unpack565(s.px[(size_t)y * s.w + x], r, g, b);
        int d0 = std::abs(r - sr / cnt), d1 = std::abs(g - sg / cnt), d2 = std::abs(b - sb / cnt);
        int d = d0 > d1 ? (d0 > d2 ? d0 : d2) : (d1 > d2 ? d1 : d2);
        if(d > worst) worst = d;
        acc += d; n++;
      }
    }
    double mean = n ? acc / n : 0;
    CHECK(mean <= 4.0, "1/%d: error medio %.2f frente al promedio de caja", S, mean);
    std::printf("   1/%d -> %3dx%-3d  err medio %.2f  max %d\n", S, s.w, s.h, mean, worst);
  }

  // maxPixels tiene que forzar el divisor aunque la caja no lo pida.
  {
    Sink s; FlexJpegInfo info;
    int rc = flexJpegDecode(jpg.data(), jpg.size(), 0, 0, 480 * 800 / 16, &info, sinkRow, &s, tAlloc, tFree);
    CHECK(rc == FLEXJPG_OK, "maxPixels: rc=%d", rc);
    CHECK(info.scaleDenom == 4, "maxPixels: divisor %d, esperado 4", info.scaleDenom);
  }
  // Una caja imposible (mas de 8x de reduccion) se rechaza, no se aproxima.
  {
    Sink s;
    int rc = flexJpegDecode(jpg.data(), jpg.size(), 10, 10, 0, nullptr, sinkRow, &s, tAlloc, tFree);
    CHECK(rc == FLEXJPG_ERR_TOOBIG, "caja imposible: rc=%d, esperado TOOBIG", rc);
  }
}

// -------------------------------------------------------------
//  3) Entradas invalidas / hostiles
// -------------------------------------------------------------
static void testRejects(const std::string& dir){
  std::printf("[jpeg] rechazo de entradas invalidas\n");
  std::vector<uint8_t> jpg;
  Sink s;

  // Progresivo: se rechaza y ademas se informa de que lo era.
  if(readFile(dir + "/progressive.jpg", jpg)){
    FlexJpegInfo info;
    int rc = flexJpegDecode(jpg.data(), jpg.size(), 0, 0, 0, &info, sinkRow, &s, tAlloc, tFree);
    CHECK(rc == FLEXJPG_ERR_UNSUPPORTED, "progresivo: rc=%d, esperado UNSUPPORTED", rc);
    FlexJpegInfo pi;
    CHECK(flexJpegProbe(jpg.data(), jpg.size(), &pi) == FLEXJPG_ERR_UNSUPPORTED && pi.progressive,
          "probe no marco la imagen como progresiva");
  } else CHECK(false, "falta progressive.jpg");

  if(!readFile(dir + "/grad420.jpg", jpg)){ CHECK(false, "falta grad420.jpg"); return; }

  // No es un JPEG.
  {
    uint8_t junk[64]; std::memset(junk, 0x41, sizeof(junk));
    Sink t;
    int rc = flexJpegDecode(junk, sizeof(junk), 0, 0, 0, nullptr, sinkRow, &t, tAlloc, tFree);
    CHECK(rc == FLEXJPG_ERR_BADMARKER, "basura: rc=%d, esperado BADMARKER", rc);
  }
  // Buffer vacio / argumentos nulos.
  {
    Sink t;
    CHECK(flexJpegDecode(nullptr, 100, 0, 0, 0, nullptr, sinkRow, &t, tAlloc, tFree) == FLEXJPG_ERR_ARG,
          "puntero nulo no rechazado");
    CHECK(flexJpegDecode(jpg.data(), 2, 0, 0, 0, nullptr, sinkRow, &t, tAlloc, tFree) == FLEXJPG_ERR_ARG,
          "longitud minima no rechazada");
    CHECK(flexJpegDecode(jpg.data(), jpg.size(), 0, 0, 0, nullptr, nullptr, &t, tAlloc, tFree) == FLEXJPG_ERR_ARG,
          "callback nulo no rechazado");
  }
  // Truncado en cada punto: nunca debe petar ni escribir fuera.
  {
    int ok = 0, bad = 0;
    for(size_t cut = 4; cut < jpg.size(); cut += 7){
      Sink t;
      long before = g_allocs;
      int rc = flexJpegDecode(jpg.data(), cut, 0, 0, 0, nullptr, sinkRow, &t, tAlloc, tFree);
      if(rc == FLEXJPG_OK) ok++; else bad++;
      CHECK(g_allocs == before, "truncado en %zu: fuga de reservas", cut);
    }
    std::printf("   truncados probados: %d rechazados, %d completados\n", bad, ok);
    CHECK(bad > 0, "ningun truncamiento fue detectado");
  }
  // Corrupcion de los datos entropicos: puede salir imagen fea, pero
  // nunca un fallo de memoria ni un bucle infinito.
  {
    std::vector<uint8_t> c = jpg;
    long before = g_allocs;
    for(size_t i = c.size() / 2; i < c.size() - 2; i += 3) c[i] ^= 0x5A;
    Sink t;
    int rc = flexJpegDecode(c.data(), c.size(), 0, 0, 0, nullptr, sinkRow, &t, tAlloc, tFree);
    CHECK(rc == FLEXJPG_OK || rc < 0, "corrupto: rc=%d", rc);
    CHECK(g_allocs == before, "corrupto: fuga de reservas");
    std::printf("   datos corruptos -> rc=%d (%s)\n", rc, flexJpegErrStr(rc));
  }
  // Cabecera con dimensiones absurdas.
  {
    std::vector<uint8_t> c = jpg;
    // Busca el SOF0 y le pone 60000x60000.
    for(size_t i = 0; i + 9 < c.size(); i++){
      if(c[i] == 0xFF && c[i + 1] == 0xC0){
        c[i + 5] = 0xEA; c[i + 6] = 0x60; c[i + 7] = 0xEA; c[i + 8] = 0x60;
        break;
      }
    }
    Sink t;
    int rc = flexJpegDecode(c.data(), c.size(), 0, 0, 0, nullptr, sinkRow, &t, tAlloc, tFree);
    CHECK(rc == FLEXJPG_ERR_TOOBIG, "dimensiones absurdas: rc=%d, esperado TOOBIG", rc);
  }
  // Cancelacion desde el callback.
  {
    Sink t; t.abortAfter = 5;
    long before = g_allocs;
    int rc = flexJpegDecode(jpg.data(), jpg.size(), 0, 0, 0, nullptr, sinkRow, &t, tAlloc, tFree);
    CHECK(rc == FLEXJPG_ERR_ABORTED, "cancelacion: rc=%d, esperado ABORTED", rc);
    CHECK(t.rowsSeen == 6, "cancelacion: %d filas entregadas", t.rowsSeen);
    CHECK(g_allocs == before, "cancelacion: fuga de reservas");
  }
  // Fallo de reserva: se propaga como MEMORY, sin escribir nada.
  {
    struct FailAlloc {
      static void* fn(size_t){ return nullptr; }
      static void  fr(void*){}
    };
    Sink t;
    int rc = flexJpegDecode(jpg.data(), jpg.size(), 0, 0, 0, nullptr, sinkRow, &t,
                            &FailAlloc::fn, &FailAlloc::fr);
    CHECK(rc == FLEXJPG_ERR_MEMORY, "sin memoria: rc=%d, esperado MEMORY", rc);
    CHECK(t.rowsSeen == 0, "sin memoria: se entregaron filas igualmente");
  }
}

// =============================================================
//  EL DECODIFICADOR TIENE QUE CABER EN UNA PILA DE MICROCONTROLADOR
//  ------------------------------------------------------------
//  En la placa, el dibujo corre en el `loopTask` de arduino-esp32:
//  8192 BYTES de pila. En el PC la pila del proceso son megas, asi que
//  un decodificador que reserve 8 KB de estado como local pasa todas
//  las pruebas aqui y revienta la placa en el primer frame de verdad.
//  Eso fue un fallo real en la ESP32-P4: pantalla de un solo color y
//  "PANIC / fatal exception / core dump" al recibir la primera imagen.
//
//  Por eso esta prueba decodifica dentro de un hilo con una pila
//  DELIBERADAMENTE PEQUENA (16 KB, el doble de la del loopTask, para
//  dejar sitio a la cadena de llamadas del navegador). Si alguien
//  vuelve a poner una estructura grande en la pila, esto se cae aqui
//  -- que es donde tiene que caerse.
// =============================================================
struct StackProbe {
  std::string path;
  int         rc;
  long        rows;
};

static bool probeRowCb(void* user, int y, int w, const uint16_t* px){
  (void)y; (void)px;
  StackProbe* p = (StackProbe*)user;
  p->rows += w > 0 ? 1 : 0;
  return true;
}

static void* probeThread(void* arg){
  StackProbe* p = (StackProbe*)arg;
  std::vector<uint8_t> data;
  if(!readFile(p->path, data)){ p->rc = -1000; return NULL; }
  FlexJpegInfo info;
  // Igual que hace el navegador: primero sondea y despues decodifica.
  FlexJpegInfo probe;
  p->rc = flexJpegProbe(data.data(), data.size(), &probe);
  if(p->rc == FLEXJPG_OK)
    p->rc = flexJpegDecode(data.data(), data.size(), probe.width, 0,
                           (uint32_t)probe.width * (uint32_t)probe.height * 2u,
                           &info, probeRowCb, p, NULL, NULL);
  return NULL;
}

static void testSmallStack(const std::string& dir){
  std::printf("[pila] el decodificador cabe en la pila del loopTask\n");
  // La imagen mas grande de la coleccion: 480x800, la peor de todas para
  // el consumo de pila (mas MCU por fila, tablas Huffman completas).
  StackProbe p;
  p.path = dir + "/page480.jpg";
  p.rc = -999;
  p.rows = 0;

  pthread_attr_t at;
  pthread_attr_init(&at);
  // 16 KB. El loopTask de arduino-esp32 tiene 8 KB; se deja el doble
  // porque aqui abajo hay ademas el marco de pthread y el del propio
  // test.
  if(pthread_attr_setstacksize(&at, 16 * 1024) != 0){
    std::printf("   (omitida: la plataforma no deja fijar el tamano de pila)\n");
    pthread_attr_destroy(&at);
    return;
  }
  pthread_t th;
  if(pthread_create(&th, &at, probeThread, &p) != 0){
    std::printf("   (omitida: no se pudo crear el hilo de pila pequena)\n");
    pthread_attr_destroy(&at);
    return;
  }
  pthread_join(th, NULL);
  pthread_attr_destroy(&at);

  CHECK(p.rc == FLEXJPG_OK,
        "decodificar 480x800 en una pila de 16 KB devolvio %d", p.rc);
  CHECK(p.rows > 700, "solo se emitieron %ld filas", p.rows);
  std::printf("   480x800 decodificada en una pila de 16 KB: %ld filas\n", p.rows);
}

int main(int argc, char** argv){
  std::string dir = argc > 1 ? argv[1] : fixtureDir();
  std::printf("=== FlexOS · decodificador JPEG · ficheros en %s ===\n", dir.c_str());
  std::vector<Fixture> fx = loadIndex(dir);
  if(fx.empty()){
    std::printf("No se encontro fixtures.txt. Ejecuta: node tests/tools/make-fixtures.js\n");
    return 2;
  }
  testExact(dir, fx);
  testScaling(dir);
  testRejects(dir);
  testSmallStack(dir);
  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
