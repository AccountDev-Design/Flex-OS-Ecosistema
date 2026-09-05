// #############################################################
//  test_media.cpp  ·  pruebas de host de FlexOS_Media.cpp
// #############################################################
//
//  Se compila y ejecuta en el PC. El modulo bajo prueba es
//  EXACTAMENTE el que va a la placa: FlexOS_Media.cpp no toca
//  Arduino ni el sistema de archivos, todo entra por FlexMediaIO
//  (bytes) y FlexMediaVolume (directorios). Aqui esos dos
//  interfaces se rellenan con memoria y con un arbol de mentira, de
//  modo que se puede ejercitar lo que en la placa seria "meter una
//  tarjeta con 4.000 fotos y sacarla a mitad del recorrido".
//
//  Que se comprueba:
//    1) CLASIFICACION: lo soportado, lo no soportado con su motivo
//       concreto, y los casos borde (".jpg" a secas, sin extension,
//       mayusculas, extension enganosa).
//    2) AVI: cabecera completa, dimensiones, fps, duracion; lectura
//       secuencial de todos los fotogramas con sus bytes exactos;
//       trozos de audio intercalados que hay que saltar; relleno de
//       alineacion a 2 bytes; 'LIST rec ' de los AVI entrelazados.
//    3) AVI: busqueda con idx1 (indice disperso) y sin idx1.
//    4) AVI: rechazo limpio de lo que no es MJPEG, de lo truncado,
//       de lo que no es RIFF y de un fotograma que no cabe.
//    5) WAV: PCM de 8 y 16 bits, mono y estereo, duracion, data
//       truncado, y rechazo de audio comprimido.
//    6) INDICE: construccion por lotes (el resultado no depende del
//       presupuesto), subcarpetas, limite de profundidad, ocultos,
//       tope de capacidad, ruta demasiado larga, directorio
//       ilegible y -- el importante -- el volumen que DESAPARECE a
//       mitad del recorrido sin perder lo ya indexado.

#include "../../FlexOS_Media.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>

// ---- mini framework (mismo estilo que el resto de la bateria) ----
static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

// =============================================================
//  FlexMediaIO sobre un bloque de memoria
// =============================================================
struct MemIO {
  std::vector<uint8_t> data;
  uint32_t pos = 0;
  int  reads = 0;          // cuantas lecturas se han pedido (coste)
  bool dead  = false;      // simula "la tarjeta se fue"
};
static int memRead(void* c, void* buf, uint32_t n){
  MemIO* m = (MemIO*)c;
  if(m->dead) return -1;
  m->reads++;
  if(m->pos >= m->data.size()) return 0;
  uint32_t left = (uint32_t)m->data.size() - m->pos;
  if(n > left) n = left;
  std::memcpy(buf, m->data.data() + m->pos, n);
  m->pos += n;
  return (int)n;
}
static bool memSeek(void* c, uint32_t off){
  MemIO* m = (MemIO*)c;
  if(m->dead) return false;
  if(off > m->data.size()) return false;
  m->pos = off; return true;
}
static uint32_t memSize(void* c){ return (uint32_t)((MemIO*)c)->data.size(); }
static FlexMediaIO ioOf(MemIO* m){
  FlexMediaIO io; io.read = memRead; io.seek = memSeek; io.size = memSize; io.ctx = m;
  return io;
}

// =============================================================
//  CONSTRUCTOR DE AVI
//  ------------------------------------------------------------
//  Escribe un AVI valido byte a byte. Hace falta uno propio (y no
//  un fichero grabado) para poder producir a voluntad los casos que
//  importan y que casi nunca aparecen en un fichero de ejemplo:
//  fotogramas de tamano IMPAR (que obligan al byte de relleno),
//  pista de audio intercalada, ausencia de idx1 y agrupacion en
//  'LIST rec '.
// =============================================================
struct AviBuild {
  std::vector<uint8_t> b;
  std::vector<std::vector<uint8_t>> frames;   // lo que se escribio, para comparar
  std::vector<uint32_t> idxOff, idxLen;
  uint32_t moviStart = 0;

  void u32(std::vector<uint8_t>& v, uint32_t x){
    v.push_back((uint8_t)(x & 0xFF)); v.push_back((uint8_t)((x >> 8) & 0xFF));
    v.push_back((uint8_t)((x >> 16) & 0xFF)); v.push_back((uint8_t)((x >> 24) & 0xFF));
  }
  void u16(std::vector<uint8_t>& v, uint16_t x){
    v.push_back((uint8_t)(x & 0xFF)); v.push_back((uint8_t)((x >> 8) & 0xFF));
  }
  void cc(std::vector<uint8_t>& v, const char* s){ for(int i = 0; i < 4; i++) v.push_back((uint8_t)s[i]); }
};

// Construye un AVI. `codec` = 'MJPG' para el caso bueno.
// withIdx  : escribe la tabla idx1.
// withAudio: intercala un trozo '01wb' entre fotogramas.
// withRec  : envuelve cada instante en 'LIST rec '.
static std::vector<uint8_t> makeAvi(int nFrames, uint16_t w, uint16_t h,
                                    const char* codec, bool withIdx,
                                    bool withAudio, bool withRec,
                                    AviBuild* outInfo = nullptr,
                                    uint32_t usPerFrame = 40000){
  AviBuild A;
  // --- fotogramas de prueba: tamano variable (algunos IMPARES a
  //     proposito) y contenido reconocible ---
  for(int i = 0; i < nFrames; i++){
    uint32_t len = 17u + (uint32_t)i * 3u;         // 17, 20, 23... impares y pares
    std::vector<uint8_t> f(len);
    for(uint32_t k = 0; k < len; k++) f[k] = (uint8_t)(i * 31 + k);
    f[0] = 0xFF; f[1] = 0xD8;                      // aspecto de JPEG
    A.frames.push_back(f);
  }

  // ---- hdrl ----
  std::vector<uint8_t> avih;
  A.u32(avih, usPerFrame);      // dwMicroSecPerFrame
  A.u32(avih, 0);               // dwMaxBytesPerSec
  A.u32(avih, 0);               // dwPaddingGranularity
  A.u32(avih, 0x10);            // dwFlags (HASINDEX)
  A.u32(avih, (uint32_t)nFrames);// dwTotalFrames
  A.u32(avih, 0);               // dwInitialFrames
  A.u32(avih, withAudio ? 2 : 1);// dwStreams
  A.u32(avih, 0);               // dwSuggestedBufferSize
  A.u32(avih, w);               // dwWidth
  A.u32(avih, h);               // dwHeight
  for(int i = 0; i < 4; i++) A.u32(avih, 0);       // dwReserved[4]

  std::vector<uint8_t> strhV;
  A.cc(strhV, "vids"); A.cc(strhV, codec);
  A.u32(strhV, 0); A.u16(strhV, 0); A.u16(strhV, 0);   // flags, prio, lang
  A.u32(strhV, 0);                                     // dwInitialFrames
  A.u32(strhV, 1);                                     // dwScale
  A.u32(strhV, 1000000u / usPerFrame);                 // dwRate -> fps
  A.u32(strhV, 0);                                     // dwStart
  A.u32(strhV, (uint32_t)nFrames);                     // dwLength
  A.u32(strhV, 4096);                                  // dwSuggestedBufferSize
  A.u32(strhV, 0); A.u32(strhV, 0);                    // quality, sampleSize
  A.u32(strhV, 0); A.u32(strhV, 0);                    // rcFrame

  std::vector<uint8_t> strfV;                          // BITMAPINFOHEADER
  A.u32(strfV, 40); A.u32(strfV, w); A.u32(strfV, h);
  A.u16(strfV, 1);  A.u16(strfV, 24);
  A.cc(strfV, codec);
  for(int i = 0; i < 5; i++) A.u32(strfV, 0);

  auto chunk = [&](const char* id, const std::vector<uint8_t>& p){
    std::vector<uint8_t> v; A.cc(v, id); A.u32(v, (uint32_t)p.size());
    v.insert(v.end(), p.begin(), p.end());
    if(p.size() & 1) v.push_back(0);
    return v;
  };
  auto list = [&](const char* type, const std::vector<uint8_t>& p){
    std::vector<uint8_t> v; A.cc(v, "LIST"); A.u32(v, (uint32_t)p.size() + 4);
    A.cc(v, type); v.insert(v.end(), p.begin(), p.end());
    return v;
  };

  std::vector<uint8_t> strlV = chunk("strh", strhV);
  { auto t = chunk("strf", strfV); strlV.insert(strlV.end(), t.begin(), t.end()); }
  std::vector<uint8_t> hdrl = chunk("avih", avih);
  { auto t = list("strl", strlV); hdrl.insert(hdrl.end(), t.begin(), t.end()); }
  if(withAudio){
    std::vector<uint8_t> strhA;
    A.cc(strhA, "auds"); A.u32(strhA, 1);
    A.u32(strhA, 0); A.u16(strhA, 0); A.u16(strhA, 0);
    A.u32(strhA, 0); A.u32(strhA, 1); A.u32(strhA, 8000);
    A.u32(strhA, 0); A.u32(strhA, 8000); A.u32(strhA, 1024);
    A.u32(strhA, 0); A.u32(strhA, 1); A.u32(strhA, 0); A.u32(strhA, 0);
    std::vector<uint8_t> strlA = chunk("strh", strhA);
    { std::vector<uint8_t> f(16, 0); auto t = chunk("strf", f); strlA.insert(strlA.end(), t.begin(), t.end()); }
    auto t = list("strl", strlA); hdrl.insert(hdrl.end(), t.begin(), t.end());
  }
  std::vector<uint8_t> hdrlL = list("hdrl", hdrl);

  // ---- movi ----
  std::vector<uint8_t> movi;              // contenido DESPUES de 'movi'
  std::vector<uint32_t> relOff, relLen;   // desplazamientos como los de idx1
  for(int i = 0; i < nFrames; i++){
    std::vector<uint8_t> group;           // lo que va dentro de 'rec ' (o suelto)
    // El desplazamiento de idx1 se mide desde el fourcc 'movi', que
    // esta 4 bytes antes del contenido.
    uint32_t here = 4u + (uint32_t)movi.size() + (withRec ? 12u : 0u);
    auto vc = chunk("00dc", A.frames[i]);
    group.insert(group.end(), vc.begin(), vc.end());
    relOff.push_back(here);
    relLen.push_back((uint32_t)A.frames[i].size());
    if(withAudio){
      std::vector<uint8_t> a(9, 0x5A);    // impar: fuerza el byte de relleno
      auto ac = chunk("01wb", a);
      group.insert(group.end(), ac.begin(), ac.end());
    }
    if(withRec){ auto r = list("rec ", group); movi.insert(movi.end(), r.begin(), r.end()); }
    else        movi.insert(movi.end(), group.begin(), group.end());
  }
  std::vector<uint8_t> moviL = list("movi", movi);

  // ---- idx1 ----
  std::vector<uint8_t> idx1;
  if(withIdx){
    for(int i = 0; i < nFrames; i++){
      A.cc(idx1, "00dc"); A.u32(idx1, 0x10);      // AVIIF_KEYFRAME
      A.u32(idx1, relOff[i]); A.u32(idx1, relLen[i]);
    }
  }

  // ---- RIFF ----
  std::vector<uint8_t> body;
  body.insert(body.end(), hdrlL.begin(), hdrlL.end());
  size_t moviAt = body.size();
  body.insert(body.end(), moviL.begin(), moviL.end());
  if(withIdx){ auto t = chunk("idx1", idx1); body.insert(body.end(), t.begin(), t.end()); }

  std::vector<uint8_t> out;
  A.cc(out, "RIFF"); A.u32(out, (uint32_t)body.size() + 4); A.cc(out, "AVI ");
  out.insert(out.end(), body.begin(), body.end());

  if(outInfo){ *outInfo = A; outInfo->moviStart = (uint32_t)(12 + moviAt + 12); }
  return out;
}

// =============================================================
//  1) CLASIFICACION
// =============================================================
static void testClassify(){
  std::printf("[media] clasificacion y motivos\n");

  CHECK(flexMediaClassify("foto.jpg")   == FLEXMED_PHOTO, "jpg");
  CHECK(flexMediaClassify("FOTO.JPEG")  == FLEXMED_PHOTO, "JPEG en mayusculas");
  CHECK(flexMediaClassify("clip.avi")   == FLEXMED_VIDEO, "avi");
  CHECK(flexMediaClassify("CLIP.AVI")   == FLEXMED_VIDEO, "AVI en mayusculas");
  CHECK(flexMediaClassify("son.wav")    == FLEXMED_AUDIO, "wav");
  CHECK(flexMediaClassify("dib.fxp")    == FLEXMED_DRAW,  "fxp");

  // No soportados: cada uno con su motivo, no un mensaje generico.
  CHECK(flexMediaClassify("v.mp4")  == FLEXMED_UNSUP, "mp4 debe ser NO soportado");
  CHECK(flexMediaClassify("v.mkv")  == FLEXMED_UNSUP, "mkv");
  CHECK(flexMediaClassify("a.mp3")  == FLEXMED_UNSUP, "mp3");
  CHECK(flexMediaClassify("i.png")  == FLEXMED_UNSUP, "png");
  CHECK(flexMediaClassify("i.HEIC") == FLEXMED_UNSUP, "heic");

  const char* why = flexMediaUnsupportedReason("pelicula.mp4");
  CHECK(why && std::strstr(why, "H.264"), "el motivo del mp4 debe nombrar H.264: '%s'", why ? why : "(null)");
  CHECK(flexMediaUnsupportedReason("foto.jpg") == NULL, "un jpg no tiene motivo de rechazo");
  const char* whyPng = flexMediaUnsupportedReason("captura.png");
  CHECK(whyPng && std::strstr(whyPng, "JPEG"), "el motivo del png debe decir que solo hay JPEG: '%s'",
        whyPng ? whyPng : "(null)");

  // Casos borde: no debe confundirse un nombre que TERMINA en algo
  // parecido con la extension de verdad.
  CHECK(flexMediaClassify(".jpg")        == FLEXMED_NONE, "'.jpg' a secas no es una foto");
  CHECK(flexMediaClassify("sinpunto")    == FLEXMED_NONE, "sin extension");
  CHECK(flexMediaClassify("")            == FLEXMED_NONE, "cadena vacia");
  CHECK(flexMediaClassify("archivo.txt") == FLEXMED_NONE, "txt no es medio");
  CHECK(flexMediaClassify("no.jpgx")     == FLEXMED_NONE, "jpgx no es jpg");

  char e[8];
  flexMediaExt("Foto.JPEG", e, sizeof(e));
  CHECK(std::strcmp(e, "jpeg") == 0, "extension en minusculas, dio '%s'", e);
  flexMediaExt("sinpunto", e, sizeof(e));
  CHECK(e[0] == 0, "sin extension -> cadena vacia");

}

// =============================================================
//  2) AVI: cabecera y lectura secuencial
// =============================================================
static void testAviBasics(){
  std::printf("[media] AVI MJPEG: cabecera y lectura secuencial\n");

  AviBuild info;
  MemIO m; m.data = makeAvi(6, 320, 240, "MJPG", true, false, false, &info);
  FlexMediaIO io = ioOf(&m);

  FlexAviCtx a;
  int r = flexAviOpen(&a, &io);
  CHECK(r == FLEXAVI_OK, "abrir AVI valido dio %d (%s)", r, flexAviErrStr(r));
  CHECK(a.width == 320 && a.height == 240, "dimensiones %ux%u", a.width, a.height);
  CHECK(a.frames == 6, "fotogramas %u", a.frames);
  CHECK(a.usPerFrame == 40000, "us/frame %u", a.usPerFrame);
  CHECK(flexAviDurationMs(&a) == 240, "duracion %u ms (6 x 40 ms)", flexAviDurationMs(&a));
  CHECK(a.idxFromFile, "con idx1 debe haber indice para buscar");

  // Todos los fotogramas, con sus BYTES exactos: es lo que prueba
  // que el relleno de alineacion se maneja bien (los tamanos de
  // prueba alternan par e impar a proposito).
  uint8_t buf[512];
  for(int i = 0; i < 6; i++){
    uint32_t fn = 0xFFFFFFFF;
    int n = flexAviReadFrame(&a, buf, sizeof(buf), &fn);
    CHECK(n == (int)info.frames[i].size(), "fotograma %d: %d bytes, esperaba %zu",
          i, n, info.frames[i].size());
    CHECK(fn == (uint32_t)i, "numero de fotograma %u != %d", fn, i);
    if(n > 0)
      CHECK(std::memcmp(buf, info.frames[i].data(), (size_t)n) == 0,
            "contenido del fotograma %d no coincide", i);
  }
  int end = flexAviReadFrame(&a, buf, sizeof(buf), NULL);
  CHECK(end == FLEXAVI_ERR_EOF, "tras el ultimo debe dar EOF, dio %d", end);
}

static void testAviAudioAndRec(){
  std::printf("[media] AVI: audio intercalado y 'LIST rec '\n");

  // Con audio: los trozos '01wb' (de tamano IMPAR) tienen que
  // saltarse sin desalinear el recorrido.
  {
    AviBuild info;
    MemIO m; m.data = makeAvi(5, 160, 120, "MJPG", true, true, false, &info);
    FlexMediaIO io = ioOf(&m);
    FlexAviCtx a;
    CHECK(flexAviOpen(&a, &io) == FLEXAVI_OK, "abrir con pista de audio");
    uint8_t buf[512];
    for(int i = 0; i < 5; i++){
      int n = flexAviReadFrame(&a, buf, sizeof(buf), NULL);
      CHECK(n == (int)info.frames[i].size(), "con audio, fotograma %d dio %d", i, n);
      if(n > 0) CHECK(std::memcmp(buf, info.frames[i].data(), (size_t)n) == 0,
                      "con audio, contenido del fotograma %d", i);
    }
    CHECK(flexAviReadFrame(&a, buf, sizeof(buf), NULL) == FLEXAVI_ERR_EOF, "EOF con audio");
  }
  // Entrelazado en 'LIST rec ': hay que ENTRAR en la lista, no
  // saltarla, o no se ve ni un fotograma.
  {
    AviBuild info;
    MemIO m; m.data = makeAvi(4, 160, 120, "MJPG", true, true, true, &info);
    FlexMediaIO io = ioOf(&m);
    FlexAviCtx a;
    CHECK(flexAviOpen(&a, &io) == FLEXAVI_OK, "abrir entrelazado");
    uint8_t buf[512];
    int got = 0;
    for(int i = 0; i < 4; i++){
      int n = flexAviReadFrame(&a, buf, sizeof(buf), NULL);
      if(n > 0){ got++;
        CHECK(std::memcmp(buf, info.frames[i].data(), (size_t)n) == 0,
              "rec: contenido del fotograma %d", i); }
    }
    CHECK(got == 4, "en 'LIST rec ' deben salir los 4 fotogramas, salieron %d", got);
  }
}

static void testAviSeek(){
  std::printf("[media] AVI: busqueda con y sin idx1\n");

  // --- con idx1 ---
  {
    AviBuild info;
    MemIO m; m.data = makeAvi(40, 320, 240, "MJPG", true, false, false, &info);
    FlexMediaIO io = ioOf(&m);
    FlexAviCtx a;
    CHECK(flexAviOpen(&a, &io) == FLEXAVI_OK, "abrir para buscar");

    uint8_t buf[1024];
    int landed = flexAviSeekFrame(&a, 25);
    CHECK(landed == 25, "buscar el 25 dejo en %d", landed);
    uint32_t fn = 0;
    int n = flexAviReadFrame(&a, buf, sizeof(buf), &fn);
    CHECK(fn == 25, "tras buscar, el fotograma entregado es el %u", fn);
    CHECK(n == (int)info.frames[25].size() &&
          std::memcmp(buf, info.frames[25].data(), (size_t)n) == 0,
          "el contenido del fotograma 25 es el suyo");

    // Buscar hacia ATRAS tiene que funcionar igual (es lo que hace
    // el boton de retroceder).
    landed = flexAviSeekFrame(&a, 3);
    CHECK(landed == 3, "buscar hacia atras dejo en %d", landed);
    n = flexAviReadFrame(&a, buf, sizeof(buf), &fn);
    CHECK(fn == 3 && n == (int)info.frames[3].size() &&
          std::memcmp(buf, info.frames[3].data(), (size_t)n) == 0,
          "contenido del fotograma 3 tras retroceder");

    // Pasarse del final se recorta al ultimo, no explota.
    landed = flexAviSeekFrame(&a, 9999);
    CHECK(landed == 39, "buscar mas alla del final deja en el ultimo (%d)", landed);
  }

  // --- sin idx1: solo hacia delante, y NUNCA se salta contenido ---
  {
    AviBuild info;
    MemIO m; m.data = makeAvi(20, 320, 240, "MJPG", false, false, false, &info);
    FlexMediaIO io = ioOf(&m);
    FlexAviCtx a;
    CHECK(flexAviOpen(&a, &io) == FLEXAVI_OK, "abrir sin idx1");
    CHECK(!a.idxFromFile, "sin idx1 no debe haber indice");

    uint8_t buf[1024]; uint32_t fn = 0;
    int landed = flexAviSeekFrame(&a, 10);
    CHECK(landed == 10, "sin idx1, avanzar al 10 dejo en %d", landed);
    int n = flexAviReadFrame(&a, buf, sizeof(buf), &fn);
    CHECK(fn == 10 && n == (int)info.frames[10].size() &&
          std::memcmp(buf, info.frames[10].data(), (size_t)n) == 0,
          "sin idx1, el fotograma 10 es el correcto");
  }
}

static void testAviRejects(){
  std::printf("[media] AVI: rechazo limpio de lo que no se puede reproducir\n");

  // Codec que no es MJPEG -> se dice, no se intenta.
  {
    MemIO m; m.data = makeAvi(4, 320, 240, "H264", true, false, false);
    FlexMediaIO io = ioOf(&m);
    FlexAviCtx a;
    int r = flexAviOpen(&a, &io);
    CHECK(r == FLEXAVI_ERR_CODEC, "AVI con H264 debe dar ERR_CODEC, dio %d", r);
    CHECK(std::strstr(flexAviErrStr(r), "MJPEG") != NULL, "el motivo debe nombrar MJPEG");
  }
  // No es RIFF.
  {
    MemIO m; m.data.assign(200, 0x41);
    FlexMediaIO io = ioOf(&m);
    FlexAviCtx a;
    CHECK(flexAviOpen(&a, &io) == FLEXAVI_ERR_FORMAT, "basura -> ERR_FORMAT");
  }
  // Fichero cortado por la mitad: sin 'movi' completo.
  {
    MemIO m; m.data = makeAvi(8, 320, 240, "MJPG", true, false, false);
    m.data.resize(m.data.size() / 3);
    FlexMediaIO io = ioOf(&m);
    FlexAviCtx a;
    int r = flexAviOpen(&a, &io);
    CHECK(r == FLEXAVI_ERR_FORMAT || r == FLEXAVI_OK,
          "un AVI truncado debe abrir o rechazarse, nunca colgarse (dio %d)", r);
    if(r == FLEXAVI_OK){
      // Si abrio, leer hasta el final tiene que terminar SIEMPRE.
      uint8_t buf[1024];
      int guard = 0, n;
      while((n = flexAviReadFrame(&a, buf, sizeof(buf), NULL)) >= 0 && ++guard < 1000){}
      CHECK(guard < 1000, "leer un AVI truncado debe terminar, no dar vueltas");
    }
  }
  // Demasiado pequeno para tener cabecera.
  {
    MemIO m; m.data.assign(10, 0);
    FlexMediaIO io = ioOf(&m);
    FlexAviCtx a;
    CHECK(flexAviOpen(&a, &io) == FLEXAVI_ERR_FORMAT, "fichero minusculo -> ERR_FORMAT");
  }
  // Un fotograma que no cabe en el buffer: se avisa y se PASA al
  // siguiente, no se entrega medio JPEG.
  {
    AviBuild info;
    MemIO m; m.data = makeAvi(3, 320, 240, "MJPG", true, false, false, &info);
    FlexMediaIO io = ioOf(&m);
    FlexAviCtx a;
    CHECK(flexAviOpen(&a, &io) == FLEXAVI_OK, "abrir para probar buffer corto");
    uint8_t tiny[4];
    int r = flexAviReadFrame(&a, tiny, sizeof(tiny), NULL);
    CHECK(r == FLEXAVI_ERR_TOOBIG, "buffer corto -> ERR_TOOBIG, dio %d", r);
    // ...y el siguiente sigue siendo legible con un buffer normal.
    uint8_t buf[512];
    uint32_t fn = 0;
    int n = flexAviReadFrame(&a, buf, sizeof(buf), &fn);
    CHECK(n == (int)info.frames[1].size() && fn == 1,
          "tras un fotograma que no cabia, el siguiente se lee (n=%d fn=%u)", n, fn);
  }
  // El medio DESAPARECE a mitad (la tarjeta se saca): toda lectura
  // posterior tiene que devolver error, nunca datos viejos.
  {
    MemIO m; m.data = makeAvi(10, 320, 240, "MJPG", true, false, false);
    FlexMediaIO io = ioOf(&m);
    FlexAviCtx a;
    CHECK(flexAviOpen(&a, &io) == FLEXAVI_OK, "abrir antes de sacar la tarjeta");
    uint8_t buf[512];
    CHECK(flexAviReadFrame(&a, buf, sizeof(buf), NULL) > 0, "primer fotograma antes de sacarla");
    m.dead = true;                             // <- se retira la tarjeta
    int r = flexAviReadFrame(&a, buf, sizeof(buf), NULL);
    CHECK(r < 0, "con el medio muerto la lectura debe fallar, dio %d", r);
    CHECK(flexAviSkipFrame(&a) < 0, "saltar tambien debe fallar");
    CHECK(flexAviSeekFrame(&a, 5) < 0, "buscar tambien debe fallar");
  }
}

// =============================================================
//  5) WAV
// =============================================================
static void pushU32(std::vector<uint8_t>& v, uint32_t x){
  v.push_back((uint8_t)(x & 0xFF)); v.push_back((uint8_t)((x >> 8) & 0xFF));
  v.push_back((uint8_t)((x >> 16) & 0xFF)); v.push_back((uint8_t)((x >> 24) & 0xFF));
}
static void pushU16(std::vector<uint8_t>& v, uint16_t x){
  v.push_back((uint8_t)(x & 0xFF)); v.push_back((uint8_t)((x >> 8) & 0xFF));
}
static void pushCC(std::vector<uint8_t>& v, const char* s){
  for(int i = 0; i < 4; i++) v.push_back((uint8_t)s[i]);
}
static std::vector<uint8_t> makeWav(uint16_t tag, uint16_t ch, uint32_t rate,
                                    uint16_t bits, uint32_t dataBytes,
                                    bool truncateData = false){
  std::vector<uint8_t> fmt;
  pushU16(fmt, tag); pushU16(fmt, ch); pushU32(fmt, rate);
  pushU32(fmt, rate * ch * (bits / 8));       // byte rate
  pushU16(fmt, (uint16_t)(ch * (bits / 8)));  // block align
  pushU16(fmt, bits);

  std::vector<uint8_t> body;
  pushCC(body, "fmt "); pushU32(body, (uint32_t)fmt.size());
  body.insert(body.end(), fmt.begin(), fmt.end());
  pushCC(body, "data"); pushU32(body, dataBytes);
  uint32_t actual = truncateData ? dataBytes / 2 : dataBytes;
  body.insert(body.end(), actual, 0x20);

  std::vector<uint8_t> out;
  pushCC(out, "RIFF"); pushU32(out, (uint32_t)body.size() + 4); pushCC(out, "WAVE");
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

static void testWav(){
  std::printf("[media] WAV PCM\n");

  { // 16 bits estereo 44100
    MemIO m; m.data = makeWav(1, 2, 44100, 16, 44100 * 2 * 2);   // 1 s
    FlexMediaIO io = ioOf(&m);
    FlexWavInfo w;
    int r = flexWavParse(&io, &w);
    CHECK(r == FLEXWAV_OK, "WAV PCM16 estereo dio %d", r);
    CHECK(w.channels == 2 && w.bits == 16 && w.sampleRate == 44100, "campos del fmt");
    uint32_t ms = flexWavDurationMs(&w);
    CHECK(ms >= 995 && ms <= 1005, "duracion %u ms, esperaba ~1000", ms);
  }
  { // 8 bits mono
    MemIO m; m.data = makeWav(1, 1, 8000, 8, 8000);
    FlexMediaIO io = ioOf(&m);
    FlexWavInfo w;
    CHECK(flexWavParse(&io, &w) == FLEXWAV_OK, "WAV PCM8 mono");
    CHECK(flexWavDurationMs(&w) == 1000, "duracion PCM8 mono");
  }
  { // data declarado mas grande que el fichero: se usa lo que HAY
    MemIO m; m.data = makeWav(1, 1, 8000, 16, 16000, true);
    FlexMediaIO io = ioOf(&m);
    FlexWavInfo w;
    CHECK(flexWavParse(&io, &w) == FLEXWAV_OK, "WAV con data truncado debe abrir");
    CHECK(w.dataBytes <= 8000, "dataBytes se recorta a lo real (%u)", w.dataBytes);
  }
  { // comprimido -> se dice que no, no se intenta
    MemIO m; m.data = makeWav(0x0011, 1, 8000, 4, 4000);   // IMA ADPCM
    FlexMediaIO io = ioOf(&m);
    FlexWavInfo w;
    CHECK(flexWavParse(&io, &w) == FLEXWAV_ERR_CODEC, "ADPCM -> ERR_CODEC");
  }
  { // 24 bits: PCM pero no soportado
    MemIO m; m.data = makeWav(1, 2, 48000, 24, 4800);
    FlexMediaIO io = ioOf(&m);
    FlexWavInfo w;
    CHECK(flexWavParse(&io, &w) == FLEXWAV_ERR_CODEC, "PCM 24 bits -> ERR_CODEC");
  }
  { // no es RIFF
    MemIO m; m.data.assign(100, 7);
    FlexMediaIO io = ioOf(&m);
    FlexWavInfo w;
    CHECK(flexWavParse(&io, &w) == FLEXWAV_ERR_FORMAT, "basura -> ERR_FORMAT");
  }
}

// =============================================================
//  6) INDICE INCREMENTAL
//  ------------------------------------------------------------
//  Volumen de mentira: un mapa de "ruta -> entradas". Cuenta las
//  llamadas para poder comprobar que el recorrido avanza de verdad
//  y que respeta el presupuesto.
// =============================================================
struct FakeEntry { std::string name; uint32_t size; bool dir; };
struct FakeVol {
  std::map<std::string, std::vector<FakeEntry>> tree;
  bool alive = true;
  int  listCalls = 0;
  int  dieAfterCalls = -1;      // -1 = no muere
  std::string unreadable;       // un directorio que falla al abrirse
};
static int fakeList(void* c, const char* dir, FlexMediaDirent* out, int maxn, int skip){
  FakeVol* v = (FakeVol*)c;
  v->listCalls++;
  if(v->dieAfterCalls >= 0 && v->listCalls > v->dieAfterCalls) v->alive = false;
  if(!v->alive) return -1;
  if(!v->unreadable.empty() && v->unreadable == dir) return -1;
  auto it = v->tree.find(dir);
  if(it == v->tree.end()) return -1;
  int n = 0;
  for(size_t i = (size_t)skip; i < it->second.size() && n < maxn; i++, n++){
    std::snprintf(out[n].name, FLEXMED_NAME_MAX, "%s", it->second[i].name.c_str());
    out[n].size = it->second[i].size;
    out[n].dir  = it->second[i].dir;
  }
  return n;
}
static bool fakeAlive(void* c){ return ((FakeVol*)c)->alive; }
static FlexMediaVolume volOf(FakeVol* v){
  FlexMediaVolume mv; mv.list = fakeList; mv.alive = fakeAlive; mv.ctx = v;
  return mv;
}

// Recorre hasta el final con un presupuesto dado. Devuelve las
// vueltas que hicieron falta (para comprobar que el trabajo se
// reparte y no se hace todo de golpe).
static int runToEnd(FlexMediaIndex* ix, int budget, int maxSteps = 20000){
  int steps = 0;
  while(flexMediaIndexStep(ix, budget) == FLEXMED_SCAN_RUNNING && steps < maxSteps) steps++;
  return steps;
}

static void testIndexBasics(){
  std::printf("[media] indice: recorrido por lotes\n");

  FakeVol volume;
  volume.tree["/Documentos"] = {
    {"a.jpg", 100, false}, {"b.JPG", 200, false}, {"notas.txt", 10, false},
    {"c.avi", 5000, false}, {"peli.mp4", 90000, false}, {"DCIM", 0, true}
  };
  volume.tree["/Paint"] = { {"dibujo.fxp", 300, false} };
  volume.tree["/Documentos/DCIM"] = {
    {"100APPLE", 0, true}, {"x.jpg", 400, false}, {"y.wav", 800, false}
  };
  volume.tree["/Documentos/DCIM/100APPLE"] = {
    {"IMG_0001.JPG", 1000, false}, {"IMG_0002.JPG", 1100, false},
    {".oculto.jpg",  1200, false}
  };

  // El resultado NO puede depender del presupuesto: con lotes de 1,
  // de 3 o de 100 tiene que salir exactamente lo mismo. Es la
  // propiedad que hace seguro llamarlo desde el tick de dibujo.
  int lastN = -1;
  for(int budget : {1, 2, 3, 7, 100}){
    FlexMediaItem store[64];
    FlexMediaIndex ix;
    flexMediaIndexInit(&ix, store, 64);
    ix.volume = volOf(&volume);
    flexMediaIndexAddRoot(&ix, "/Documentos");
    flexMediaIndexAddRoot(&ix, "/Paint");
    flexMediaIndexStart(&ix);
    int steps = runToEnd(&ix, budget);
    CHECK(ix.state == FLEXMED_SCAN_DONE, "presupuesto %d: debe terminar", budget);

    if(lastN < 0) lastN = ix.n;
    CHECK((int)ix.n == lastN, "presupuesto %d dio %u elementos, antes %d",
          budget, ix.n, lastN);

    // Lo esperado: a.jpg, b.JPG, c.avi (mp4 y txt fuera),
    // dibujo.fxp, x.jpg, y.wav, IMG_0001, IMG_0002 (el oculto no).
    CHECK(ix.n == 8, "presupuesto %d: esperaba 8 elementos, hay %u", budget, ix.n);
    CHECK(flexMediaIndexCount(&ix, FLEXMED_PHOTO) == 5,
          "fotos: %d", flexMediaIndexCount(&ix, FLEXMED_PHOTO));
    CHECK(flexMediaIndexCount(&ix, FLEXMED_VIDEO) == 1, "videos");
    CHECK(flexMediaIndexCount(&ix, FLEXMED_AUDIO) == 1, "audios");
    CHECK(flexMediaIndexCount(&ix, FLEXMED_DRAW)  == 1, "dibujos");

    // Con lotes de 1 tiene que hacer falta MAS de una vuelta: si no,
    // es que el presupuesto no se esta respetando.
    if(budget == 1) CHECK(steps > 8, "con presupuesto 1 deben hacer falta varias vueltas (%d)", steps);

    // Ninguna ruta puede estar truncada ni mal compuesta.
    for(uint16_t i = 0; i < ix.n; i++){
      CHECK(store[i].path[0] == '/', "ruta absoluta: '%s'", store[i].path);
      CHECK(std::strstr(store[i].path, "//") == NULL, "ruta con doble barra: '%s'", store[i].path);
    }
    // Y la subcarpeta se recorrio de verdad.
    bool found = false;
    for(uint16_t i = 0; i < ix.n; i++)
      if(std::strcmp(store[i].path, "/Documentos/DCIM/100APPLE/IMG_0002.JPG") == 0) found = true;
    CHECK(found, "presupuesto %d: falta el fichero de la subcarpeta", budget);
  }
}

static void testIndexSiblingsAfterSubdir(){
  std::printf("[media] indice: hermanos DETRAS de una subcarpeta\n");
  // Este es el caso que rompe un indexador escrito a la ligera: si
  // al bajar a una subcarpeta se da por consumido el lote entero,
  // los ficheros que van DESPUES de la subcarpeta no se indexan
  // nunca. Se prueba con la subcarpeta la primera, en medio y con
  // varios presupuestos.
  FakeVol volume;
  volume.tree["/Media"] = {
    {"sub", 0, true},
    {"a.jpg", 1, false}, {"b.jpg", 2, false}, {"c.jpg", 3, false},
    {"sub2", 0, true},
    {"d.jpg", 4, false}, {"e.jpg", 5, false}
  };
  volume.tree["/Media/sub"]  = { {"s1.jpg", 1, false} };
  volume.tree["/Media/sub2"] = { {"s2.jpg", 1, false} };

  for(int budget : {1, 2, 4, 8, 64}){
    FlexMediaItem store[32];
    FlexMediaIndex ix;
    flexMediaIndexInit(&ix, store, 32);
    ix.volume = volOf(&volume);
    flexMediaIndexAddRoot(&ix, "/Media");
    flexMediaIndexStart(&ix);
    runToEnd(&ix, budget);
    CHECK(ix.n == 7, "presupuesto %d: esperaba 7 (5 sueltos + 2 de subcarpetas), hay %u",
          budget, ix.n);
    for(const char* want : {"/Media/a.jpg", "/Media/e.jpg",
                            "/Media/sub/s1.jpg", "/Media/sub2/s2.jpg"}){
      bool ok = false;
      for(uint16_t i = 0; i < ix.n; i++) if(!std::strcmp(store[i].path, want)) ok = true;
      CHECK(ok, "presupuesto %d: falta %s", budget, want);
    }
  }
}

static void testIndexEdges(){
  std::printf("[media] indice: capacidad, profundidad, rutas largas y errores\n");

  { // Se llena: se marca `full`, no se desborda el array.
    FakeVol volume;
    std::vector<FakeEntry> many;
    for(int i = 0; i < 50; i++){
      char n[32]; std::snprintf(n, sizeof(n), "f%02d.jpg", i);
      many.push_back({n, 10, false});
    }
    volume.tree["/Media"] = many;
    FlexMediaItem store[8];
    FlexMediaIndex ix;
    flexMediaIndexInit(&ix, store, 8);
    ix.volume = volOf(&volume);
    flexMediaIndexAddRoot(&ix, "/Media");
    flexMediaIndexStart(&ix);
    runToEnd(&ix, 4);
    CHECK(ix.n == 8,  "no puede pasar de la capacidad (n=%u)", ix.n);
    CHECK(ix.full,    "debe quedar marcado que hay mas de lo que cabe");
    CHECK(ix.state == FLEXMED_SCAN_DONE, "aun lleno, el recorrido termina");
  }
  { // Mas hondo que FLEXMED_DEPTH_MAX: se ignora, no se cuelga.
    FakeVol volume;
    volume.tree["/Media"]       = { {"n1", 0, true} };
    volume.tree["/Media/n1"]    = { {"n2", 0, true}, {"ok.jpg", 1, false} };
    volume.tree["/Media/n1/n2"] = { {"n3", 0, true}, {"ok2.jpg", 1, false} };
    volume.tree["/Media/n1/n2/n3"] = { {"hondo.jpg", 1, false} };
    FlexMediaItem store[16];
    FlexMediaIndex ix;
    flexMediaIndexInit(&ix, store, 16);
    ix.volume = volOf(&volume);
    flexMediaIndexAddRoot(&ix, "/Media");
    flexMediaIndexStart(&ix);
    runToEnd(&ix, 3);
    CHECK(ix.state == FLEXMED_SCAN_DONE, "el limite de profundidad no puede colgar el recorrido");
    CHECK(ix.n == 2, "hasta el nivel permitido: esperaba 2, hay %u", ix.n);
  }
  { // Nombre tan largo que la ruta no cabe: se descarta, NO se
    //  guarda truncada (una ruta truncada abre otro fichero).
    FakeVol volume;
    std::string big(FLEXMED_NAME_MAX - 6, 'z');
    volume.tree["/" + std::string(60, 'q')] = { {big + ".jpg", 1, false} };
    FlexMediaItem store[8];
    FlexMediaIndex ix;
    flexMediaIndexInit(&ix, store, 8);
    ix.volume = volOf(&volume);
    static std::string root = "/" + std::string(60, 'q');
    flexMediaIndexAddRoot(&ix, root.c_str());
    flexMediaIndexStart(&ix);
    runToEnd(&ix, 4);
    for(uint16_t i = 0; i < ix.n; i++)
      CHECK(std::strlen(store[i].path) < FLEXMED_PATH_MAX, "ruta dentro del limite");
    CHECK(ix.state == FLEXMED_SCAN_DONE, "una ruta larga no puede parar el recorrido");
  }
  { // Un directorio ilegible no puede tumbar el indice entero.
    FakeVol volume;
    volume.tree["/Media"]     = { {"malo", 0, true}, {"bien.jpg", 1, false} };
    volume.tree["/Media/malo"]= { {"x.jpg", 1, false} };
    volume.unreadable = "/Media/malo";
    FlexMediaItem store[8];
    FlexMediaIndex ix;
    flexMediaIndexInit(&ix, store, 8);
    ix.volume = volOf(&volume);
    flexMediaIndexAddRoot(&ix, "/Media");
    flexMediaIndexStart(&ix);
    runToEnd(&ix, 2);
    CHECK(ix.state == FLEXMED_SCAN_DONE, "un directorio ilegible no para el indice");
    CHECK(ix.n == 1, "lo legible si se indexa (n=%u)", ix.n);
  }
  { // Raiz que no existe: se pasa a la siguiente.
    FakeVol volume;
    volume.tree["/Media"] = { {"ok.jpg", 1, false} };
    FlexMediaItem store[8];
    FlexMediaIndex ix;
    flexMediaIndexInit(&ix, store, 8);
    ix.volume = volOf(&volume);
    flexMediaIndexAddRoot(&ix, "/NoExiste");
    flexMediaIndexAddRoot(&ix, "/Media");
    flexMediaIndexStart(&ix);
    runToEnd(&ix, 4);
    CHECK(ix.state == FLEXMED_SCAN_DONE && ix.n == 1,
          "una raiz ausente se salta (estado=%d n=%u)", ix.state, ix.n);
  }
}

static void testIndexVolumeUnavailable(){
  std::printf("[media] indice: LittleFS deja de estar disponible a mitad del recorrido\n");

  FakeVol volume;
  std::vector<FakeEntry> many;
  for(int i = 0; i < 60; i++){
    char n[32]; std::snprintf(n, sizeof(n), "f%02d.jpg", i);
    many.push_back({n, 10, false});
  }
  volume.tree["/Documentos"] = many;
  volume.dieAfterCalls = 3;

  FlexMediaItem store[128];
  FlexMediaIndex ix;
  flexMediaIndexInit(&ix, store, 128);
  ix.volume = volOf(&volume);
  flexMediaIndexAddRoot(&ix, "/Documentos");
  flexMediaIndexStart(&ix);
  runToEnd(&ix, 2);

  CHECK(ix.state == FLEXMED_SCAN_DONE,
        "perder LittleFS debe terminar el recorrido, no colgarlo");
  CHECK(flexMediaIndexCount(&ix, 0) < 60,
        "no puede haber indexado el arbol entero si el volumen fallo");
}

static void testIndexNth(){
  std::printf("[media] indice: seleccion por filtro (lo que usan las pestanas)\n");
  FakeVol volume;
  volume.tree["/Media"] = {
    {"1.jpg", 1, false}, {"2.avi", 2, false}, {"3.jpg", 3, false},
    {"4.avi", 4, false}, {"5.wav", 5, false}
  };
  FlexMediaItem store[16];
  FlexMediaIndex ix;
  flexMediaIndexInit(&ix, store, 16);
  ix.volume = volOf(&volume);
  flexMediaIndexAddRoot(&ix, "/Media");
  flexMediaIndexStart(&ix);
  runToEnd(&ix, 8);

  CHECK(flexMediaIndexCount(&ix, FLEXMED_VIDEO) == 2, "dos videos");
  int i0 = flexMediaIndexNth(&ix, FLEXMED_VIDEO, 0);
  int i1 = flexMediaIndexNth(&ix, FLEXMED_VIDEO, 1);
  CHECK(i0 >= 0 && i1 >= 0 && i0 != i1, "los dos videos son elementos distintos");
  CHECK(store[i0].kind == FLEXMED_VIDEO && store[i1].kind == FLEXMED_VIDEO, "y son videos");
  CHECK(flexMediaIndexNth(&ix, FLEXMED_VIDEO, 2) == -1, "no hay un tercero");
  CHECK(flexMediaIndexNth(&ix, FLEXMED_AUDIO, 1) == -1,
        "no hay un segundo audio en este arbol");
}

// =============================================================
int main(){
  std::printf("\n=== FlexOS · medios: clasificacion, AVI/MJPEG, WAV e indice ===\n");
  testClassify();
  testAviBasics();
  testAviAudioAndRec();
  testAviSeek();
  testAviRejects();
  testWav();
  testIndexBasics();
  testIndexSiblingsAfterSubdir();
  testIndexEdges();
  testIndexVolumeUnavailable();
  testIndexNth();
  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
