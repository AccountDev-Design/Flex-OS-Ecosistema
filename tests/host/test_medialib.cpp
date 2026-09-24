// #############################################################
//  test_medialib.cpp  ·  pruebas de host de FlexOS_MediaLib.cpp
// #############################################################
//
//  El catalogo es la unica fuente de datos de Galeria, Multimedia,
//  Musica y Flex Web Server, y decide tres cosas que no pueden quedarse
//  en "parece que funciona":
//    1) que formato es DE VERDAD un fichero que llega del movil (la
//       extension miente: un .jpg puede ser un HEIC renombrado);
//    2) que nombre recibe en el disco (nada de rutas, nada de "..",
//       nada de nombres ocultos, nada que parta un caracter);
//    3) que se le ensena de un elemento BLOQUEADO a quien no se ha
//       autenticado: su existencia, y nada mas.
//  Mas la persistencia: un catalogo danado en la flash no puede cargarse
//  "a medias" -- o entra entero y valido, o no entra.
//
//  El CRC se compara contra zlib y el JSON se valida con cJSON: las dos
//  referencias son independientes del codigo bajo prueba.

#include "../../FlexOS_Ultra/FlexOS_MediaLib.h"
#include "vendor/cJSON/cJSON.h"
#include <zlib.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <set>

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

static std::vector<uint8_t> readFile(const char* p){
  std::vector<uint8_t> v;
  FILE* f = std::fopen(p, "rb");
  if(!f) return v;
  uint8_t b[4096]; size_t n;
  while((n = std::fread(b, 1, sizeof(b), f)) > 0) v.insert(v.end(), b, b + n);
  std::fclose(f);
  return v;
}

static uint32_t rng = 0x12345678u;
static uint32_t rnd(){ rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

// =============================================================
static void testCrc(){
  std::printf("-- CRC-32 contra zlib --\n");
  CHECK(flexMlCrc32(0, "", 0) == 0, "CRC de nada es 0");
  CHECK(flexMlCrc32(0, "123456789", 9) == 0xCBF43926u, "vector estandar 123456789");
  for(int t = 0; t < 50; t++){
    std::vector<uint8_t> b(1 + rnd() % 5000);
    for(auto& x : b) x = (uint8_t)rnd();
    uint32_t mine = flexMlCrc32(0, b.data(), b.size());
    uint32_t ref  = (uint32_t)crc32(0, b.data(), (uInt)b.size());
    CHECK(mine == ref, "CRC igual a zlib (%zu bytes)", b.size());
    // Encadenado: por trozos da lo mismo que de una vez.
    size_t cut = b.size() / 3;
    uint32_t c = flexMlCrc32(0, b.data(), cut);
    c = flexMlCrc32(c, b.data() + cut, b.size() - cut);
    CHECK(c == ref, "CRC encadenado por trozos");
  }
  CHECK(flexMlHash("") != 0 && flexMlHash("/a") != flexMlHash("/b"), "hash no nulo y distingue");
}

// =============================================================
static std::vector<uint8_t> riffAvi(const char* handler){
  std::vector<uint8_t> v(200, 0);
  memcpy(&v[0], "RIFF", 4); memcpy(&v[8], "AVI ", 4);
  memcpy(&v[12], "LIST", 4); memcpy(&v[20], "hdrl", 4);
  memcpy(&v[24], "avih", 4); v[28] = 56;
  size_t p = 24 + 8 + 56;
  memcpy(&v[p], "LIST", 4); memcpy(&v[p + 8], "strl", 4);
  memcpy(&v[p + 12], "strh", 4); v[p + 16] = 56;
  memcpy(&v[p + 20], "vids", 4); memcpy(&v[p + 24], handler, 4);
  return v;
}
static std::vector<uint8_t> riffWav(uint16_t tag, uint16_t ch, uint16_t bits){
  std::vector<uint8_t> v(64, 0);
  memcpy(&v[0], "RIFF", 4); memcpy(&v[8], "WAVE", 4);
  memcpy(&v[12], "fmt ", 4); v[16] = 16;
  v[20] = (uint8_t)tag; v[21] = (uint8_t)(tag >> 8);
  v[22] = (uint8_t)ch;
  v[24] = 0x44; v[25] = 0xAC;                // 44100
  v[34] = (uint8_t)bits;
  memcpy(&v[36], "data", 4);
  return v;
}
static int sniffOf(const std::vector<uint8_t>& v, int* kind = nullptr){
  int k = -1;
  int f = flexMlSniff(v.data(), v.size(), &k);
  if(kind) *kind = k;
  return f;
}
static std::vector<uint8_t> bytes(const char* s, size_t n){ return std::vector<uint8_t>(s, s + n); }

static void testSniff(){
  std::printf("-- formato real por firma --\n");
  int k;
  auto base = readFile("../fixtures/grad420.jpg");
  CHECK(!base.empty(), "fixture grad420.jpg");
  CHECK(sniffOf(base, &k) == FML_F_JPEG && k == FML_K_PHOTO, "JPEG baseline");
  auto prog = readFile("../fixtures/progressive.jpg");
  CHECK(sniffOf(prog, &k) == FML_F_JPEG_PROG && k == FML_K_PHOTO, "JPEG progresivo detectado por su SOF2");
  CHECK(!flexMlFmtPlayable(FML_F_JPEG_PROG) && flexMlWhyUnplayable(FML_F_JPEG_PROG), "progresivo: no reproducible y con motivo");
  // Solo la cabecera (sin llegar al SOF): hipotesis JPEG.
  CHECK(sniffOf(std::vector<uint8_t>(base.begin(), base.begin() + 4)) == FML_F_JPEG, "JPEG corto: hipotesis");
  CHECK(sniffOf(bytes("\x89PNG\r\n\x1a\n\0\0\0\rIHDR", 16), &k) == FML_F_PNG && k == FML_K_PHOTO, "PNG");
  CHECK(sniffOf(bytes("GIF89a\x01\0\x01\0", 10)) == FML_F_GIF, "GIF");
  { std::vector<uint8_t> b(32, 0); b[0] = 'B'; b[1] = 'M'; b[10] = 54; CHECK(sniffOf(b) == FML_F_BMP, "BMP"); }
  { std::vector<uint8_t> b(32, 0); memcpy(&b[0], "RIFF", 4); memcpy(&b[8], "WEBP", 4);
    CHECK(sniffOf(b, &k) == FML_F_WEBP && k == FML_K_PHOTO, "WebP"); }
  CHECK(sniffOf(riffAvi("MJPG"), &k) == FML_F_AVI_MJPEG && k == FML_K_VIDEO, "AVI MJPEG");
  CHECK(sniffOf(riffAvi("mjpg")) == FML_F_AVI_MJPEG, "AVI mjpg en minusculas");
  CHECK(sniffOf(riffAvi("H264"), &k) == FML_F_AVI_OTHER && k == FML_K_VIDEO, "AVI H.264: video, no reproducible");
  { std::vector<uint8_t> b(32, 0); memcpy(&b[4], "ftypisom", 8);
    CHECK(sniffOf(b, &k) == FML_F_MP4 && k == FML_K_VIDEO, "MP4 (isom)"); }
  { std::vector<uint8_t> b(32, 0); memcpy(&b[4], "ftypqt  ", 8); CHECK(sniffOf(b) == FML_F_MOV, "MOV"); }
  { std::vector<uint8_t> b(32, 0); memcpy(&b[4], "ftypheic", 8);
    CHECK(sniffOf(b, &k) == FML_F_HEIC && k == FML_K_PHOTO, "HEIC es foto, aunque vaya en ftyp"); }
  { std::vector<uint8_t> b(32, 0); memcpy(&b[4], "ftypM4A ", 8);
    CHECK(sniffOf(b, &k) == FML_F_M4A && k == FML_K_AUDIO, "M4A es audio"); }
  { std::vector<uint8_t> b(40, 0); b[0] = 0x1A; b[1] = 0x45; b[2] = 0xDF; b[3] = 0xA3; memcpy(&b[20], "webm", 4);
    CHECK(sniffOf(b) == FML_F_WEBM, "WebM"); b[20] = 'x'; CHECK(sniffOf(b) == FML_F_MKV, "MKV"); }
  CHECK(sniffOf(riffWav(1, 2, 16), &k) == FML_F_WAV_PCM && k == FML_K_AUDIO, "WAV PCM 16");
  CHECK(sniffOf(riffWav(1, 1, 8)) == FML_F_WAV_PCM, "WAV PCM 8");
  CHECK(sniffOf(riffWav(0x11, 1, 4)) == FML_F_WAV_ADPCM, "WAV IMA ADPCM");
  CHECK(sniffOf(riffWav(0x55, 2, 0)) == FML_F_WAV_OTHER, "WAV con MP3 dentro: no reproducible");
  CHECK(sniffOf(riffWav(1, 2, 24)) == FML_F_WAV_OTHER, "WAV PCM 24 bits: no reproducible");
  CHECK(sniffOf(bytes("ID3\x04\0\0\0\0\0\0", 10), &k) == FML_F_MP3 && k == FML_K_AUDIO, "MP3 con ID3");
  CHECK(sniffOf(bytes("\xFF\xFB\x90\x44\0\0", 6)) == FML_F_MP3, "MP3 por sincronia de trama");
  CHECK(sniffOf(bytes("\xFF\xF1\x50\x80\0\0", 6)) == FML_F_AAC, "AAC ADTS");
  CHECK(sniffOf(bytes("fLaC\0\0\0\x22", 8)) == FML_F_FLAC, "FLAC");
  CHECK(sniffOf(bytes("OggS\0\x02", 6)) == FML_F_OGG, "OGG");
  CHECK(sniffOf(bytes("FXP1\x10\0\x10\0", 8), &k) == FML_F_FXP && k == FML_K_DRAW, "dibujo de Paint");
  CHECK(sniffOf(bytes("Hola, esto es texto", 19), &k) == FML_F_UNKNOWN && k == FML_K_NONE, "texto: desconocido");
  CHECK(sniffOf(bytes("\xFF\xD8", 2)) == FML_F_UNKNOWN, "dos bytes no bastan");
  CHECK(flexMlSniff(nullptr, 10, &k) == FML_F_UNKNOWN, "puntero nulo");
  // La extension es solo una pista, y las mayusculas no la engañan.
  CHECK(flexMlKindFromExt("a.JPG") == FML_K_PHOTO && flexMlKindFromExt("b.Mp4") == FML_K_VIDEO, "extension sin mayusculas");
  CHECK(flexMlKindFromExt("c.mp3") == FML_K_AUDIO && flexMlKindFromExt("d.fxp") == FML_K_DRAW, "audio y dibujo por extension");
  CHECK(flexMlKindFromExt("sin") == FML_K_NONE && flexMlKindFromExt("x.") == FML_K_NONE, "sin extension");
  // Reproducible de verdad: exactamente lo que el P4 abre.
  CHECK(flexMlFmtPlayable(FML_F_JPEG) && flexMlFmtPlayable(FML_F_AVI_MJPEG) &&
        flexMlFmtPlayable(FML_F_WAV_PCM) && flexMlFmtPlayable(FML_F_WAV_ADPCM) &&
        flexMlFmtPlayable(FML_F_FXP), "lo que el P4 abre");
  const int no[] = { FML_F_PNG, FML_F_HEIC, FML_F_MP4, FML_F_MOV, FML_F_MP3, FML_F_M4A, FML_F_AVI_OTHER, FML_F_WEBP };
  for(int f : no) CHECK(!flexMlFmtPlayable(f) && flexMlWhyUnplayable(f) && flexMlWhyUnplayable(f)[0],
                        "%s: no reproducible y con motivo", flexMlFmtName(f));
  // Ruido: nunca se sale del buffer (ASan lo diria).
  for(int t = 0; t < 3000; t++){
    std::vector<uint8_t> b(rnd() % 600);
    for(auto& x : b) x = (uint8_t)rnd();
    if(b.size() >= 4 && (t & 3) == 0) memcpy(&b[0], "RIFF", 4);
    if(b.size() >= 3 && (t & 3) == 1){ b[0] = 0xFF; b[1] = 0xD8; b[2] = 0xFF; }
    int kk; int f = flexMlSniff(b.data(), b.size(), &kk);
    CHECK(f >= 0 && f < FML_F_COUNT, "ruido: formato dentro del rango");
  }
}

// =============================================================
static bool existsSet(void* ctx, const char* p){ return ((std::set<std::string>*)ctx)->count(p) != 0; }

static void testNames(){
  std::printf("-- nombres en disco y rutas --\n");
  char o[64];
  flexMlSafeStem("IMG_1234.JPG", o, sizeof(o));                 CHECK(!strcmp(o, "IMG_1234"), "IMG_1234 -> '%s'", o);
  flexMlSafeStem("Canci\xC3\xB3n de verano \xC3\xB1" "and\xC3\xBA.mp3", o, sizeof(o));
  CHECK(!strcmp(o, "Cancion de verano nandu"), "acentos transliterados -> '%s'", o);
  flexMlSafeStem("../../etc/passwd", o, sizeof(o));             CHECK(!strcmp(o, "passwd"), "sin ruta -> '%s'", o);
  flexMlSafeStem("C:\\fotos\\playa.jpeg", o, sizeof(o));        CHECK(!strcmp(o, "playa"), "ruta Windows -> '%s'", o);
  flexMlSafeStem(".oculto", o, sizeof(o));                      CHECK(o[0] != '.' && o[0], "nunca oculto -> '%s'", o);
  flexMlSafeStem("..", o, sizeof(o));                           CHECK(!strcmp(o, "Archivo"), "'..' -> '%s'", o);
  flexMlSafeStem("   ", o, sizeof(o));                          CHECK(!strcmp(o, "Archivo"), "vacio -> '%s'", o);
  flexMlSafeStem("\xE6\x97\xA5\xE6\x9C\xAC.jpg", o, sizeof(o)); CHECK(!strcmp(o, "Archivo"), "no latino -> '%s'", o);
  flexMlSafeStem("a  <b>  |c|.jpg", o, sizeof(o));              CHECK(!strchr(o, '<') && !strchr(o, '|') && !strstr(o, "  "), "raros fuera -> '%s'", o);
  flexMlSafeStem("foto.final.jpeg", o, sizeof(o));              CHECK(!strcmp(o, "foto.final"), "puntos internos -> '%s'", o);
  flexMlSafeStem("Stra\xC3\x9F" "e", o, sizeof(o));             CHECK(!strcmp(o, "Strasse"), "eszett -> '%s'", o);
  flexMlSafeStem("0123456789012345678901234567890123456789.jpg", o, sizeof(o));
  CHECK(strlen(o) <= FML_STEM_MAX, "recortado a %d (%zu)", FML_STEM_MAX, strlen(o));
  char tiny[4]; flexMlSafeStem("abcdef", tiny, sizeof(tiny)); CHECK(!strcmp(tiny, "abc"), "buffer pequeno respetado");
  for(int t = 0; t < 2000; t++){
    char in[80]; size_t n = rnd() % 79;
    for(size_t i = 0; i < n; i++) in[i] = (char)(1 + rnd() % 255);
    in[n] = 0;
    flexMlSafeStem(in, o, sizeof(o));
    bool ok = o[0] && o[0] != '.' && strlen(o) <= FML_STEM_MAX;
    for(const char* p = o; *p; p++){
      char c = *p;
      if(!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == ' ' || c == '_' || c == '-' || c == '.' || c == '(' || c == ')')) ok = false;
    }
    CHECK(ok, "ruido: raiz ASCII segura '%s'", o);
  }

  // Nombre para ensenar: UTF-8 valido, sin controles, sin partir caracteres.
  flexMlCleanName("Fot\xC3\xB3\x01 \xFF" "bien/mal", o, sizeof(o));
  CHECK(!strcmp(o, "Fot\xC3\xB3 ?bien_mal"), "limpio -> '%s'", o);
  char c5[6]; flexMlCleanName("\xC3\xB1\xC3\xB1\xC3\xB1", c5, sizeof(c5));
  CHECK(strlen(c5) == 4, "no parte un caracter de dos bytes (%zu)", strlen(c5));

  CHECK(flexMlPathSafe("/Imagenes/IMG_1.jpg"), "ruta normal");
  const char* bad[] = { "", "rel/a.jpg", "/a/../b", "/a/./b", "/a//b", "/a/", "/a\\b", "/a\x01", "/..", "/" };
  for(const char* b : bad) CHECK(!flexMlPathSafe(b), "ruta rechazada: '%s'", b);
  std::string lng = "/" + std::string(FML_PATH_MAX, 'a');
  CHECK(!flexMlPathSafe(lng.c_str()), "ruta demasiado larga");

  std::set<std::string> fs;
  char p[FML_PATH_MAX];
  CHECK(flexMlUniquePath("/Imagenes", "IMG", ".jpg", existsSet, &fs, p, sizeof(p)) && !strcmp(p, "/Imagenes/IMG.jpg"), "libre: %s", p);
  fs.insert(p);
  CHECK(flexMlUniquePath("/Imagenes", "IMG", ".jpg", existsSet, &fs, p, sizeof(p)) && !strcmp(p, "/Imagenes/IMG (2).jpg"), "ocupado: %s", p);
  for(int k = 2; k <= 99; k++){ char q[80]; snprintf(q, sizeof(q), "/Imagenes/IMG (%d).jpg", k); fs.insert(q); }
  CHECK(!flexMlUniquePath("/Imagenes", "IMG", ".jpg", existsSet, &fs, p, sizeof(p)), "sin hueco en 99 intentos: no inventa");
  CHECK(!strcmp(flexMlDestDir(FML_K_PHOTO), FML_DIR_PHOTO) && !strcmp(flexMlDestDir(FML_K_VIDEO), FML_DIR_VIDEO) &&
        !strcmp(flexMlDestDir(FML_K_AUDIO), FML_DIR_AUDIO), "destino por clase");
  CHECK(flexMlDestDir(FML_K_DRAW)[0] == 0, "un dibujo no llega por la red");
}

// =============================================================
static FlexMlRec mk(const char* path, int kind, uint32_t created = 0){
  FlexMlRec r; memset(&r, 0, sizeof(r));
  snprintf(r.path, sizeof(r.path), "%s", path);
  const char* b = strrchr(path, '/');
  snprintf(r.name, sizeof(r.name), "%s", b ? b + 1 : path);
  r.kind = (uint8_t)kind; r.fmt = FML_F_JPEG; r.created = created; r.size = 100;
  return r;
}

static void testCatalog(){
  std::printf("-- catalogo: ids, busqueda, bloqueo --\n");
  static FlexMlRec store[8];
  FlexMlLib lib; flexMlInit(&lib, store, 8);
  FlexMlRec a = mk("/Imagenes/a.jpg", FML_K_PHOTO);
  int ia = flexMlAdd(&lib, &a, 1000);
  CHECK(ia == 0 && lib.recs[0].id == 1 && lib.recs[0].added == 1000, "primer id = 1, added = ahora");
  CHECK(flexMlAdd(&lib, &a, 1001) == FML_ERR_EXISTS, "misma ruta dos veces: no");
  FlexMlRec bad = mk("/Imagenes/../x.jpg", FML_K_PHOTO);
  CHECK(flexMlAdd(&lib, &bad, 1) == FML_ERR_ARG, "ruta peligrosa: no");
  FlexMlRec nok = mk("/Imagenes/n.jpg", 9);
  CHECK(flexMlAdd(&lib, &nok, 1) == FML_ERR_ARG, "clase invalida: no");
  FlexMlRec b = mk("/Videos/b.avi", FML_K_VIDEO); b.crc = 0xABCD; b.size = 555;
  int ib = flexMlAdd(&lib, &b, 1002);
  CHECK(lib.recs[ib].id == 2, "ids crecientes");
  CHECK(flexMlFindId(&lib, 2) == ib && flexMlFindPath(&lib, "/Videos/b.avi") == ib, "busqueda por id y por ruta");
  CHECK(flexMlFindDup(&lib, 555, 0xABCD) == ib && flexMlFindDup(&lib, 555, 0) < 0, "duplicado exacto (CRC conocido)");
  uint32_t rev = lib.rev;
  CHECK(flexMlRemoveAt(&lib, ia) && lib.n == 1 && lib.rev != rev, "borrar sube la revision");
  FlexMlRec c = mk("/Imagenes/c.jpg", FML_K_PHOTO);
  int ic = flexMlAdd(&lib, &c, 1003);
  CHECK(lib.recs[ic].id == 3, "un id borrado NO se reutiliza");
  CHECK(!flexMlSetPath(&lib, ic, "/Videos/b.avi"), "renombrar encima de otro: no");
  CHECK(flexMlSetPath(&lib, ic, "/Imagenes/c2.jpg") && flexMlFindPath(&lib, "/Imagenes/c2.jpg") == ic, "renombrar");
  uint8_t tv = lib.recs[ic].thumbVer; rev = lib.rev;
  CHECK(flexMlSetLocked(&lib, ic, true) && (lib.recs[ic].flags & FML_R_LOCKED), "bloquear");
  CHECK(lib.recs[ic].thumbVer != tv && lib.rev != rev, "bloquear invalida la miniatura cacheada y avisa");
  rev = lib.rev;
  CHECK(flexMlSetLocked(&lib, ic, true) && lib.rev == rev, "bloquear lo bloqueado no cambia nada");
  CHECK(flexMlSetLocked(&lib, ic, false) && !(lib.recs[ic].flags & FML_R_LOCKED), "desbloquear");
  for(int i = 0; i < 10; i++){ char q[40]; snprintf(q, sizeof(q), "/Imagenes/z%d.jpg", i);
    FlexMlRec z = mk(q, FML_K_PHOTO); flexMlAdd(&lib, &z, 5); }
  CHECK(lib.n == 8, "lleno a 8");
  FlexMlRec y = mk("/Imagenes/lleno.jpg", FML_K_PHOTO);
  CHECK(flexMlAdd(&lib, &y, 1) == FML_ERR_FULL, "catalogo lleno: lo dice");
}

// =============================================================
static void testScan(){
  std::printf("-- reconciliacion con el disco --\n");
  static FlexMlRec store[16];
  FlexMlLib lib; flexMlInit(&lib, store, 16);
  flexMlScanBegin(&lib);
  int i1 = flexMlScanMerge(&lib, "/Imagenes/uno.jpg", 1000, FML_K_PHOTO, false, 50);
  int i2 = flexMlScanMerge(&lib, "/Paint/Dibujo 1.fxp", 300, FML_K_DRAW, false, 50);
  int i3 = flexMlScanMerge(&lib, "/Videos/clip.mp4", 9000, FML_K_VIDEO, false, 50);
  CHECK(i1 >= 0 && i2 >= 0 && i3 >= 0 && lib.n == 3, "tres descubiertos");
  CHECK((lib.recs[i1].flags & FML_R_PLAYABLE) && (lib.recs[i1].flags & FML_R_NEED_THUMB), "JPEG: reproducible y pide miniatura");
  CHECK(!(lib.recs[i3].flags & FML_R_PLAYABLE) && lib.recs[i3].err == FML_E_UNSUPPORTED, "MP4: guardado pero no reproducible");
  CHECK(!strcmp(lib.recs[i1].name, "uno.jpg"), "nombre desde la ruta");
  uint32_t id1 = lib.recs[i1].id;
  // Segunda pasada: uno cambia de tamano, otro desaparece.
  lib.recs[i1].flags = (uint16_t)((lib.recs[i1].flags | FML_R_THUMB) & ~FML_R_NEED_THUMB);
  uint8_t tv = lib.recs[i1].thumbVer;
  flexMlScanBegin(&lib);
  int j1 = flexMlScanMerge(&lib, "/Imagenes/uno.jpg", 2000, FML_K_PHOTO, false, 60);
  flexMlScanMerge(&lib, "/Paint/Dibujo 1.fxp", 300, FML_K_DRAW, false, 60);
  CHECK(j1 == i1 && lib.recs[j1].id == id1, "el mismo archivo conserva su media_id");
  CHECK(lib.recs[j1].size == 2000 && (lib.recs[j1].flags & FML_R_NEED_THUMB) && !(lib.recs[j1].flags & FML_R_THUMB) &&
        lib.recs[j1].thumbVer != tv && lib.recs[j1].modified == 60, "contenido cambiado: miniatura de nuevo");
  int u = flexMlNextUnseen(&lib, 0);
  CHECK(u == i3 && flexMlNextUnseen(&lib, u + 1) < 0, "solo el que no aparecio es candidato a faltar");
  // Recuperacion desde la carpeta protegida sin catalogo.
  int ip = flexMlScanMerge(&lib, FML_DIR_LOCKED "/17.jpg", 800, FML_K_PHOTO, true, 70);
  CHECK(ip >= 0 && (lib.recs[ip].flags & FML_R_LOCKED) && !strstr(lib.recs[ip].name, "17"),
        "lo de Protegido nace bloqueado y con el nombre oculto");
  CHECK(flexMlScanMerge(&lib, "/Imagenes/x.txt", 1, FML_K_NONE, false, 1) == FML_ERR_KIND, "sin clase: no entra");
}

// =============================================================
static void testViews(){
  std::printf("-- vistas: filtro y orden --\n");
  static FlexMlRec store[16];
  FlexMlLib lib; flexMlInit(&lib, store, 16);
  FlexMlRec r;
  r = mk("/Imagenes/vieja.jpg", FML_K_PHOTO, 100);  flexMlAdd(&lib, &r, 900);
  r = mk("/Imagenes/nueva.jpg", FML_K_PHOTO, 300);  flexMlAdd(&lib, &r, 901);
  r = mk("/Videos/medio.avi", FML_K_VIDEO, 200);    flexMlAdd(&lib, &r, 902);
  r = mk("/Musica/zeta.wav", FML_K_AUDIO); snprintf(r.title, sizeof(r.title), "Zeta"); flexMlAdd(&lib, &r, 903);
  r = mk("/Musica/alfa.wav", FML_K_AUDIO); snprintf(r.title, sizeof(r.title), "alfa"); flexMlAdd(&lib, &r, 904);
  r = mk("/Paint/sin fecha.fxp", FML_K_DRAW);       flexMlAdd(&lib, &r, 150);   // added 150, created 0
  uint16_t vs[16], as[16];
  FlexMlView v, a;
  flexMlViewInit(&v, vs, 16, FML_MASK_VISUAL, FML_SORT_NEWEST);
  flexMlViewInit(&a, as, 16, FML_MASK_AUDIO, FML_SORT_TITLE);
  CHECK(flexMlViewSync(&v, &lib, false) && v.n == 4, "Galeria: fotos, video y dibujo, sin audio (%u)", v.n);
  CHECK(!strcmp(lib.recs[v.idx[0]].name, "nueva.jpg") && !strcmp(lib.recs[v.idx[1]].name, "medio.avi") &&
        !strcmp(lib.recs[v.idx[2]].name, "sin fecha.fxp") && !strcmp(lib.recs[v.idx[3]].name, "vieja.jpg"),
        "orden: lo mas nuevo primero (sin fecha de captura usa la de entrada)");
  CHECK(flexMlViewSync(&a, &lib, false) && a.n == 2, "Musica: solo audio");
  CHECK(!strcmp(lib.recs[a.idx[0]].title, "alfa") && !strcmp(lib.recs[a.idx[1]].title, "Zeta"), "Musica por titulo, sin mayusculas");
  CHECK(!flexMlViewSync(&v, &lib, false), "sin cambios no se reconstruye");
  flexMlTouch(&lib);
  CHECK(flexMlViewSync(&v, &lib, false), "con cambios si");
  CHECK(flexMlViewFindId(&v, &lib, lib.recs[v.idx[2]].id) == 2, "posicion de un id en la vista");
}

// =============================================================
static void testPersist(){
  std::printf("-- persistencia: todo o nada --\n");
  static FlexMlRec s1[FML_CAP], s2[FML_CAP];
  FlexMlLib a; flexMlInit(&a, s1, FML_CAP);
  for(int i = 0; i < 40; i++){
    char p[48]; snprintf(p, sizeof(p), "/Imagenes/f%02d.jpg", i);
    FlexMlRec r = mk(p, i % 3 == 2 ? FML_K_AUDIO : FML_K_PHOTO, 1000 + i);
    snprintf(r.name, sizeof(r.name), "F\xC3\xB3to %d", i);
    r.crc = (uint32_t)rnd(); r.w = 100 + i; r.h = 50; r.flags = (i % 5 == 0) ? FML_R_LOCKED : 0;
    flexMlAdd(&a, &r, 2000);
  }
  std::vector<uint8_t> buf(flexMlSerializedSize(&a));
  CHECK(buf.size() == FML_HDR_BYTES + 40 * sizeof(FlexMlRec), "tamano serializado");
  CHECK(flexMlSerialize(&a, buf.data(), buf.size()) == buf.size(), "serializa");
  CHECK(flexMlSerialize(&a, buf.data(), buf.size() - 1) == 0, "buffer corto: 0, sin escribir de mas");
  FlexMlLib b; flexMlInit(&b, s2, FML_CAP);
  CHECK(flexMlDeserialize(&b, buf.data(), buf.size()) == FML_OK && b.n == 40 && b.nextId == a.nextId, "ida y vuelta");
  bool same = true;
  for(int i = 0; i < 40; i++) if(memcmp(&a.recs[i], &b.recs[i], sizeof(FlexMlRec))) same = false;
  CHECK(same, "registros identicos tras releer");
  CHECK(b.rev != a.rev, "cargar cuenta como cambio");

  // Un bit cambiado: CRC.
  auto bad = buf; bad[FML_HDR_BYTES + 77] ^= 0x10;
  FlexMlLib c; static FlexMlRec s3[FML_CAP]; flexMlInit(&c, s3, FML_CAP);
  FlexMlRec keep = mk("/Imagenes/previo.jpg", FML_K_PHOTO); flexMlAdd(&c, &keep, 1);
  CHECK(flexMlDeserialize(&c, bad.data(), bad.size()) == FML_ERR_CRC, "un bit cambiado: CRC");
  CHECK(c.n == 1 && !strcmp(c.recs[0].path, "/Imagenes/previo.jpg"), "el catalogo anterior queda intacto");
  CHECK(flexMlDeserialize(&c, buf.data(), buf.size() - 3) == FML_ERR_FORMAT, "truncado: formato");
  auto mg = buf; mg[0] = 'X';
  CHECK(flexMlDeserialize(&c, mg.data(), mg.size()) == FML_ERR_FORMAT, "otra firma: formato");
  // Registro invalido con el CRC rehecho (lo que haria alguien que escribe a mano).
  auto rv = buf;
  FlexMlRec* r0 = (FlexMlRec*)(rv.data() + FML_HDR_BYTES);
  snprintf(r0->path, sizeof(r0->path), "/Imagenes/../../x");
  uint32_t crc = flexMlCrc32(0, rv.data() + FML_HDR_BYTES, 40 * sizeof(FlexMlRec));
  memcpy(rv.data() + 20, &crc, 4);
  CHECK(flexMlDeserialize(&c, rv.data(), rv.size()) == FML_ERR_RECORD, "ruta peligrosa dentro: se rechaza entero");
  auto dup = buf;
  FlexMlRec* d1 = (FlexMlRec*)(dup.data() + FML_HDR_BYTES + sizeof(FlexMlRec));
  d1->id = ((FlexMlRec*)(dup.data() + FML_HDR_BYTES))->id;
  crc = flexMlCrc32(0, dup.data() + FML_HDR_BYTES, 40 * sizeof(FlexMlRec)); memcpy(dup.data() + 20, &crc, 4);
  CHECK(flexMlDeserialize(&c, dup.data(), dup.size()) == FML_ERR_RECORD, "ids repetidos: se rechaza");
  auto sm = buf; FlexMlLib t; static FlexMlRec s4[8]; flexMlInit(&t, s4, 8);
  CHECK(flexMlDeserialize(&t, sm.data(), sm.size()) == FML_ERR_FULL, "no cabe en el almacen: lo dice");
  CHECK(c.n == 1, "y sigue intacto tras todos los intentos");
  // Ruido con el CRC rehecho: o entra valido o no entra, nunca se rompe.
  int oks = 0;
  for(int it = 0; it < 400; it++){
    auto z = buf;
    int flips = 1 + (int)(rnd() % 6);
    for(int f = 0; f < flips; f++) z[FML_HDR_BYTES + rnd() % (z.size() - FML_HDR_BYTES)] = (uint8_t)rnd();
    crc = flexMlCrc32(0, z.data() + FML_HDR_BYTES, 40 * sizeof(FlexMlRec)); memcpy(z.data() + 20, &crc, 4);
    FlexMlLib q; static FlexMlRec s5[FML_CAP]; flexMlInit(&q, s5, FML_CAP);
    if(flexMlDeserialize(&q, z.data(), z.size()) == FML_OK){
      oks++;
      for(int i = 0; i < q.n; i++) CHECK(flexMlPathSafe(q.recs[i].path) && q.recs[i].id, "ruido aceptado: sigue siendo valido");
    }
  }
  std::printf("   ruido: %d de 400 catalogos alterados seguian siendo validos\n", oks);
}

// =============================================================
struct Sink { std::string s; };
static bool sinkAdd(void* c, const char* p, size_t n){ ((Sink*)c)->s.append(p, n); return true; }

static void testJson(){
  std::printf("-- JSON para la web: lo que ve cada uno --\n");
  static FlexMlRec store[8];
  FlexMlLib lib; flexMlInit(&lib, store, 8);
  FlexMlRec r = mk("/Imagenes/playa.jpg", FML_K_PHOTO, 1700000000);
  snprintf(r.name, sizeof(r.name), "Playa \"<script>\" \\ \xC3\xB1");
  r.flags = FML_R_THUMB | FML_R_PLAYABLE; r.thumbVer = 3; r.w = 1600; r.h = 1200;
  flexMlAdd(&lib, &r, 1);
  FlexMlRec s = mk("/Musica/c.wav", FML_K_AUDIO);
  snprintf(s.title, sizeof(s.title), "Canci\xC3\xB3n"); snprintf(s.artist, sizeof(s.artist), "Grupo");
  s.flags = FML_R_LOCKED | FML_R_THUMB; s.size = 12345;
  int is = flexMlAdd(&lib, &s, 1);
  char out[1024];
  size_t n = flexMlJsonItem(&lib.recs[is], false, out, sizeof(out));
  char want[64]; snprintf(want, sizeof(want), "{\"id\":%lu,\"lock\":1}", (unsigned long)lib.recs[is].id);
  CHECK(n && !strcmp(out, want), "bloqueado sin sesion: SOLO id y candado -> %s", out);
  CHECK(!strstr(out, "Canci") && !strstr(out, "12345") && !strstr(out, "audio") && !strstr(out, "\"t\""),
        "ni titulo, ni tamano, ni tipo, ni miniatura");
  n = flexMlJsonItem(&lib.recs[is], true, out, sizeof(out));
  CHECK(n && strstr(out, "\"lock\":1") && strstr(out, "Canci") && strstr(out, "\"ar\":\"Grupo\""), "propietario: todo, con candado");
  Sink sk;
  CHECK(flexMlJsonLibrary(&lib, false, FML_MASK_ALL, "\"rev\":7", sinkAdd, &sk), "biblioteca");
  cJSON* j = cJSON_Parse(sk.s.c_str());
  CHECK(j != nullptr, "JSON valido: %s", sk.s.c_str());
  if(j){
    CHECK(cJSON_GetObjectItemCaseSensitive(j, "rev") && cJSON_GetObjectItemCaseSensitive(j, "rev")->valueint == 7, "cabecera");
    cJSON* it = cJSON_GetObjectItemCaseSensitive(j, "items");
    CHECK(cJSON_GetArraySize(it) == 2, "dos elementos");
    cJSON* e0 = cJSON_GetArrayItem(it, 0);
    cJSON* nm = cJSON_GetObjectItemCaseSensitive(e0, "n");
    CHECK(nm && cJSON_IsString(nm) && !strcmp(nm->valuestring, "Playa \"<script>\" \\ \xC3\xB1"), "escapado y vuelto a leer igual");
    CHECK(!strstr(sk.s.c_str(), "<script>"), "ni un '<' literal en la salida");
    cJSON_Delete(j);
  }
  Sink sa;
  flexMlJsonLibrary(&lib, true, FML_MASK_AUDIO, nullptr, sinkAdd, &sa);
  cJSON* ja = cJSON_Parse(sa.s.c_str());
  CHECK(ja && cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(ja, "items")) == 1, "filtro por clase");
  cJSON_Delete(ja);
  char tiny[8]; CHECK(flexMlJsonStr("hola mundo", tiny, sizeof(tiny)) == 0, "no cabe: 0, sin desbordar");
}

// =============================================================
static void testUpload(){
  std::printf("-- decision previa de una subida --\n");
  char why[128];
  CHECK(flexMlCheckUpload(FML_K_PHOTO, 900000, 8u << 20, why, sizeof(why)) == FML_OK, "foto normal");
  CHECK(flexMlCheckUpload(FML_K_PHOTO, 7u << 20, 11u << 20, why, sizeof(why)) == FML_ERR_TOOBIG && strstr(why, "fotos"),
        "foto de 7 MB: %s", why);
  CHECK(flexMlCheckUpload(FML_K_VIDEO, 3u << 20, 2u << 20, why, sizeof(why)) == FML_ERR_NOSPACE && strstr(why, "espacio"),
        "sin espacio: %s", why);
  CHECK(flexMlCheckUpload(FML_K_AUDIO, 1u << 20, (1u << 20) + FML_RESERVE_BYTES, why, sizeof(why)) == FML_ERR_NOSPACE,
        "la reserva no se toca");
  CHECK(flexMlCheckUpload(FML_K_DRAW, 10, 1u << 20, why, sizeof(why)) == FML_ERR_KIND, "un dibujo no se sube");
  CHECK(flexMlCheckUpload(FML_K_PHOTO, 0, 1u << 20, why, sizeof(why)) == FML_ERR_ARG, "vacio: no");
  CHECK(flexMlKindParse("photo") == FML_K_PHOTO && flexMlKindParse("draw") == FML_K_NONE && flexMlKindParse(nullptr) == FML_K_NONE,
        "clase desde la peticion");
}

int main(){
  std::printf("=== FlexOS · biblioteca de medios (catalogo compartido) ===\n");
  const uint16_t one = 1;
  CHECK(*(const uint8_t*)&one == 1, "little-endian (el formato del fichero lo da por hecho)");
  testCrc();
  testSniff();
  testNames();
  testCatalog();
  testScan();
  testViews();
  testPersist();
  testJson();
  testUpload();
  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
