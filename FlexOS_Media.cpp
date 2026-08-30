// #############################################################
// ##  FlexOS · MEDIOS · implementacion portable
// #############################################################
//
//  Sin Arduino, sin sistema de archivos y sin pantalla: todo entra
//  por FlexMediaIO / FlexMediaVolume. Esa es la razon de que las
//  pruebas del PC ejerciten EXACTAMENTE este codigo y no una
//  version parecida.

#include "FlexOS_Media.h"
#include <string.h>
#include <stdio.h>

// -------------------------------------------------------------
//  Utilidades
// -------------------------------------------------------------
static char lower1(char c){ return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

// Compara la cola de `name` con `ext` sin distinguir mayusculas.
static bool extIs(const char* name, const char* ext){
  if(!name || !ext) return false;
  size_t ln = strlen(name), le = strlen(ext);
  if(ln <= le) return false;                 // "  .jpg" si, ".jpg" solo no
  const char* a = name + ln - le;
  for(size_t k = 0; k < le; k++)
    if(lower1(a[k]) != lower1(ext[k])) return false;
  return true;
}

void flexMediaExt(const char* name, char* out, size_t n){
  if(!out || n == 0) return;
  out[0] = 0;
  if(!name) return;
  const char* d = strrchr(name, '.');
  if(!d || !d[1]) return;
  size_t k = 0;
  for(const char* p = d + 1; *p && k + 1 < n; p++) out[k++] = lower1(*p);
  out[k] = 0;
}

bool flexMediaPathIsSd(const char* path){
  if(!path) return false;
  if(strncmp(path, "/sdcard", 7) != 0) return false;
  return path[7] == 0 || path[7] == '/';
}

// -------------------------------------------------------------
//  CLASIFICACION
//  ------------------------------------------------------------
//  La tabla es explicita a proposito. Un "else -> intentalo y a ver"
//  es justo lo que produce pantallas en negro y cuelgues: si un
//  formato no esta escrito aqui como soportado, NO se abre.
// -------------------------------------------------------------
struct UnsupEntry { const char* ext; const char* why; };

static const UnsupEntry UNSUP[] = {
  // --- video que la gente tiene de verdad en la tarjeta ---
  { ".mp4",  "MP4/H.264: esta placa no tiene decodificador de video" },
  { ".m4v",  "MP4/H.264: esta placa no tiene decodificador de video" },
  { ".mov",  "MOV/H.264: esta placa no tiene decodificador de video" },
  { ".mkv",  "MKV: contenedor no soportado" },
  { ".webm", "WebM/VP8-VP9: sin decodificador" },
  { ".3gp",  "3GP/H.263: sin decodificador" },
  { ".flv",  "FLV: contenedor no soportado" },
  { ".wmv",  "WMV: contenedor no soportado" },
  { ".mpg",  "MPEG-1/2: sin decodificador" },
  { ".mpeg", "MPEG-1/2: sin decodificador" },
  { ".ts",   "MPEG-TS: sin decodificador" },
  // --- audio comprimido ---
  { ".mp3",  "MP3: sin decodificador de audio comprimido" },
  { ".aac",  "AAC: sin decodificador de audio comprimido" },
  { ".m4a",  "M4A/AAC: sin decodificador de audio comprimido" },
  { ".flac", "FLAC: sin decodificador de audio comprimido" },
  { ".ogg",  "OGG/Vorbis: sin decodificador de audio comprimido" },
  { ".opus", "Opus: sin decodificador de audio comprimido" },
  { ".wma",  "WMA: sin decodificador de audio comprimido" },
  // --- imagen que no es JPEG ---
  { ".png",  "PNG: solo se decodifica JPEG" },
  { ".gif",  "GIF: solo se decodifica JPEG" },
  { ".bmp",  "BMP: solo se decodifica JPEG" },
  { ".webp", "WebP: solo se decodifica JPEG" },
  { ".heic", "HEIC: solo se decodifica JPEG" },
  { ".heif", "HEIF: solo se decodifica JPEG" },
  { ".tif",  "TIFF: solo se decodifica JPEG" },
  { ".tiff", "TIFF: solo se decodifica JPEG" },
  { ".raw",  "RAW: solo se decodifica JPEG" },
  { ".dng",  "DNG: solo se decodifica JPEG" },
};
static const int UNSUP_N = (int)(sizeof(UNSUP) / sizeof(UNSUP[0]));

int flexMediaClassify(const char* name){
  if(!name || !name[0]) return FLEXMED_NONE;
  if(extIs(name, ".jpg") || extIs(name, ".jpeg")) return FLEXMED_PHOTO;
  if(extIs(name, ".avi"))                        return FLEXMED_VIDEO;
  if(extIs(name, ".wav"))                        return FLEXMED_AUDIO;
  if(extIs(name, ".fxp"))                        return FLEXMED_DRAW;
  for(int i = 0; i < UNSUP_N; i++)
    if(extIs(name, UNSUP[i].ext)) return FLEXMED_UNSUP;
  return FLEXMED_NONE;
}

const char* flexMediaUnsupportedReason(const char* name){
  if(!name) return "Archivo sin nombre";
  int k = flexMediaClassify(name);
  if(k != FLEXMED_UNSUP) return NULL;
  for(int i = 0; i < UNSUP_N; i++)
    if(extIs(name, UNSUP[i].ext)) return UNSUP[i].why;
  return "Formato no soportado";
}

// -------------------------------------------------------------
//  Lectura primitiva sobre FlexMediaIO
// -------------------------------------------------------------
static bool ioReadAt(const FlexMediaIO* io, uint32_t off, void* buf, uint32_t n){
  if(!io || !io->read || !io->seek) return false;
  if(!io->seek(io->ctx, off)) return false;
  uint8_t* p = (uint8_t*)buf;
  uint32_t got = 0;
  while(got < n){
    int r = io->read(io->ctx, p + got, n - got);
    if(r <= 0) return false;
    got += (uint32_t)r;
  }
  return true;
}
static inline uint32_t rd32(const uint8_t* p){
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint16_t rd16(const uint8_t* p){
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static inline bool fourcc(const uint8_t* p, const char* s){
  return p[0] == (uint8_t)s[0] && p[1] == (uint8_t)s[1]
      && p[2] == (uint8_t)s[2] && p[3] == (uint8_t)s[3];
}

// -------------------------------------------------------------
//  AVI · analisis de cabecera
//  ------------------------------------------------------------
//  Estructura que se recorre (solo lo que hace falta):
//
//    'RIFF' tam 'AVI '
//      LIST tam 'hdrl'
//        'avih' 56   -> us por fotograma, ancho, alto, total
//        LIST tam 'strl'
//          'strh' 56 -> tipo de pista ('vids') y codec ('MJPG')
//          'strf' .. -> BITMAPINFOHEADER (ancho/alto reales)
//      LIST tam 'movi'   <- los fotogramas
//      'idx1' tam        <- tabla de posiciones (opcional)
//
//  Todo trozo RIFF esta alineado a 2 bytes: si el tamano es impar
//  hay un byte de relleno detras. Olvidar ese detalle es el fallo
//  clasico que hace que el recorrido se desalinee a mitad del
//  fichero y empiece a leer basura.
// -------------------------------------------------------------
static inline uint32_t pad2(uint32_t n){ return n + (n & 1u); }

const char* flexAviErrStr(int err){
  switch(err){
    case FLEXAVI_OK:          return "correcto";
    case FLEXAVI_ERR_IO:      return "no se pudo leer el archivo";
    case FLEXAVI_ERR_FORMAT:  return "el AVI esta danado o incompleto";
    case FLEXAVI_ERR_CODEC:   return "AVI sin video MJPEG: no se puede reproducir";
    case FLEXAVI_ERR_NOVIDEO: return "el AVI no tiene pista de video";
    case FLEXAVI_ERR_EOF:     return "fin del video";
    case FLEXAVI_ERR_TOOBIG:  return "un fotograma no cabe en memoria";
    default:                  return "error desconocido";
  }
}

// Construye la muestra dispersa a partir de idx1. Lee la tabla por
// bloques de tamano fijo: nunca esta entera en memoria.
static void aviBuildSparseIndex(FlexAviCtx* a, uint32_t idxOff, uint32_t idxBytes){
  const uint32_t entries = idxBytes / 16u;
  if(entries == 0) return;
  uint32_t stride = entries / FLEXAVI_IDX_MAX;
  if(stride == 0) stride = 1;

  uint8_t blk[16 * 16];                       // 16 entradas por lectura: 256 B
  uint32_t done = 0, videoSeen = 0;
  a->idxN = 0;
  while(done < entries && a->idxN < FLEXAVI_IDX_MAX){
    uint32_t batch = entries - done;
    if(batch > 16) batch = 16;
    if(!ioReadAt(&a->io, idxOff + done * 16u, blk, batch * 16u)) break;
    for(uint32_t i = 0; i < batch && a->idxN < FLEXAVI_IDX_MAX; i++){
      const uint8_t* e = blk + i * 16u;
      // ckid "##dc"/"##db": pista de video. El primer par de bytes
      // es el numero de pista en ASCII.
      bool isVideo = (e[2] == 'd' && (e[3] == 'c' || e[3] == 'b'))
                  && (e[0] == (uint8_t)('0' + a->videoStream / 10))
                  && (e[1] == (uint8_t)('0' + a->videoStream % 10));
      if(!isVideo) continue;
      if((videoSeen % stride) == 0){
        // dwChunkOffset es relativo al inicio de 'movi' (a su campo
        // de datos) en la inmensa mayoria de los AVI; algunos
        // codificadores lo escriben absoluto. Se distingue mirando
        // si en esa posicion hay una cabecera de trozo valida.
        uint32_t rel = rd32(e + 8);
        a->idxOff[a->idxN]   = rel;           // se resuelve al usarlo
        a->idxFrame[a->idxN] = videoSeen;
        a->idxN++;
      }
      videoSeen++;
    }
    done += batch;
  }
  a->idxFromFile = (a->idxN > 0);
}

int flexAviOpen(FlexAviCtx* a, const FlexMediaIO* io){
  if(!a || !io || !io->read || !io->seek || !io->size) return FLEXAVI_ERR_IO;
  memset(a, 0, sizeof(*a));
  a->io = *io;
  a->videoStream = 0;

  const uint32_t total = io->size(io->ctx);
  if(total < 64) return FLEXAVI_ERR_FORMAT;

  uint8_t h[12];
  if(!ioReadAt(&a->io, 0, h, 12)) return FLEXAVI_ERR_IO;
  if(!fourcc(h, "RIFF") || !fourcc(h + 8, "AVI ")) return FLEXAVI_ERR_FORMAT;

  uint32_t idx1Off = 0, idx1Len = 0;
  bool haveAvih = false, haveVideo = false, codecOk = false;
  int  streamNo  = -1;                        // pista que se esta describiendo

  // Recorrido de los trozos de primer nivel.
  uint32_t p = 12;
  while(p + 8 <= total){
    uint8_t c[8];
    if(!ioReadAt(&a->io, p, c, 8)) break;
    const uint32_t len = rd32(c + 4);
    if(len > total) break;                    // cabecera corrupta: se para

    if(fourcc(c, "LIST")){
      uint8_t t[4];
      if(!ioReadAt(&a->io, p + 8, t, 4)) break;
      if(fourcc(t, "movi")){
        a->moviStart = p + 12;
        a->moviEnd   = p + 8 + len;
        if(a->moviEnd > total) a->moviEnd = total;
        p = p + 8 + pad2(len);                // 'movi' no se recorre aqui
        continue;
      }
      // hdrl y strl se ABREN (se entra dentro), por eso solo se
      // avanzan 12 bytes y no el trozo entero.
      p += 12;
      continue;
    }

    if(fourcc(c, "avih") && len >= 40){
      uint8_t v[40];
      if(ioReadAt(&a->io, p + 8, v, 40)){
        a->usPerFrame = rd32(v + 0);
        a->frames     = rd32(v + 16);
        a->width      = (uint16_t)rd32(v + 32);
        a->height     = (uint16_t)rd32(v + 36);
        haveAvih = true;
      }
    } else if(fourcc(c, "strh") && len >= 40){
      streamNo++;
      uint8_t v[40];
      if(ioReadAt(&a->io, p + 8, v, 40)){
        if(fourcc(v, "vids") && !haveVideo){
          haveVideo = true;
          a->videoStream = (uint8_t)(streamNo < 0 ? 0 : streamNo);
          memcpy(a->codec, v + 4, 4);
          a->codec[4] = 0;
          // MJPG / mjpg / JPEG / dmb1 son los identificadores que
          // usan de verdad las camaras y ffmpeg para MJPEG.
          char cc[5];
          for(int i = 0; i < 4; i++) cc[i] = lower1(a->codec[i]);
          cc[4] = 0;
          codecOk = (!strcmp(cc, "mjpg") || !strcmp(cc, "jpeg")
                  || !strcmp(cc, "dmb1") || !strcmp(cc, "mjpa"));
          // dwScale/dwRate: mas fiable que avih cuando existe.
          uint32_t scale = rd32(v + 20), rate = rd32(v + 24);
          if(rate && scale){
            uint64_t us = (uint64_t)scale * 1000000ull / rate;
            if(us > 0 && us < 1000000ull) a->usPerFrame = (uint32_t)us;
          }
          uint32_t length = rd32(v + 32);
          if(length) a->frames = length;
          a->maxFrameBytes = rd32(v + 36);
        }
      }
    } else if(fourcc(c, "strf") && haveVideo && a->width == 0 && len >= 16){
      uint8_t v[16];
      if(ioReadAt(&a->io, p + 8, v, 16)){
        a->width  = (uint16_t)rd32(v + 4);
        a->height = (uint16_t)rd32(v + 8);
      }
    } else if(fourcc(c, "idx1")){
      idx1Off = p + 8;
      idx1Len = len;
    }
    p = p + 8 + pad2(len);
  }

  if(!haveAvih || a->moviEnd <= a->moviStart) return FLEXAVI_ERR_FORMAT;
  if(!haveVideo)                              return FLEXAVI_ERR_NOVIDEO;
  if(!codecOk)                                return FLEXAVI_ERR_CODEC;
  if(a->usPerFrame == 0) a->usPerFrame = 40000;    // 25 fps si el fichero calla

  if(idx1Off && idx1Len >= 16) aviBuildSparseIndex(a, idx1Off, idx1Len);

  a->cursor  = a->moviStart;
  a->frameNo = 0;
  return FLEXAVI_OK;
}

uint32_t flexAviDurationMs(const FlexAviCtx* a){
  if(!a || !a->frames || !a->usPerFrame) return 0;
  return (uint32_t)(((uint64_t)a->frames * a->usPerFrame) / 1000ull);
}

// ¿Hay en `off` una cabecera de trozo con aspecto de valida?
// Sirve para resolver si los desplazamientos de idx1 son relativos
// a 'movi' o absolutos, sin tener que fiarse del codificador.
static bool aviChunkHere(FlexAviCtx* a, uint32_t off){
  if(off + 8 > a->moviEnd) return false;
  uint8_t c[8];
  if(!ioReadAt(&a->io, off, c, 8)) return false;
  if(c[0] < '0' || c[0] > '9' || c[1] < '0' || c[1] > '9') return false;
  uint32_t len = rd32(c + 4);
  return off + 8 + len <= a->moviEnd + 8;
}

// Traduce una posicion de idx1 a un desplazamiento absoluto.
static uint32_t aviResolveIdx(FlexAviCtx* a, uint32_t rel){
  // Caso normal: relativo al campo de datos de 'movi' menos 4 (los
  // desplazamientos de idx1 se miden desde el 'movi' del LIST).
  uint32_t cand = a->moviStart - 4 + rel;
  if(aviChunkHere(a, cand)) return cand;
  if(aviChunkHere(a, rel))  return rel;       // codificador que lo escribe absoluto
  cand = a->moviStart + rel;
  if(aviChunkHere(a, cand)) return cand;
  return 0;
}

// Avanza el cursor hasta la siguiente cabecera de trozo de VIDEO.
// Devuelve el tamano del trozo y deja `dataOff` en su primer byte.
// FLEXAVI_ERR_EOF cuando se acaba 'movi'.
static int aviNextVideoChunk(FlexAviCtx* a, uint32_t* dataOff, uint32_t* lenOut){
  for(;;){
    if(a->cursor + 8 > a->moviEnd) return FLEXAVI_ERR_EOF;
    uint8_t c[8];
    if(!ioReadAt(&a->io, a->cursor, c, 8)) return FLEXAVI_ERR_IO;
    const uint32_t len = rd32(c + 4);

    // Un 'LIST' 'rec ' agrupa los trozos de un mismo instante: se
    // entra dentro en vez de saltarlo, o se perderian los
    // fotogramas de los AVI entrelazados.
    if(fourcc(c, "LIST")){ a->cursor += 12; continue; }

    const bool isChunk = (c[0] >= '0' && c[0] <= '9' && c[1] >= '0' && c[1] <= '9');
    if(!isChunk){
      // Basura o relleno ('JUNK'): se salta el trozo entero si el
      // tamano es creible, y si no se aborta (no se avanza a ciegas
      // byte a byte por un fichero de megabytes).
      if(len == 0 || a->cursor + 8 + len > a->moviEnd) return FLEXAVI_ERR_FORMAT;
      a->cursor += 8 + pad2(len);
      continue;
    }
    const bool video = (c[2] == 'd' && (c[3] == 'c' || c[3] == 'b'))
                    && c[0] == (uint8_t)('0' + a->videoStream / 10)
                    && c[1] == (uint8_t)('0' + a->videoStream % 10);
    if(a->cursor + 8 + len > a->moviEnd + 2) return FLEXAVI_ERR_FORMAT;
    if(video){
      if(dataOff) *dataOff = a->cursor + 8;
      if(lenOut)  *lenOut  = len;
      a->cursor += 8 + pad2(len);
      return FLEXAVI_OK;
    }
    a->cursor += 8 + pad2(len);               // audio u otra pista: se salta
  }
}

int flexAviReadFrame(FlexAviCtx* a, void* buf, uint32_t bufCap, uint32_t* frameOut){
  if(!a || !buf) return FLEXAVI_ERR_IO;
  uint32_t off = 0, len = 0;
  int r = aviNextVideoChunk(a, &off, &len);
  if(r != FLEXAVI_OK) return r;
  if(len == 0)        return FLEXAVI_ERR_FORMAT;
  if(len > bufCap){
    // No se lee a medias: entregar medio JPEG haria que el
    // decodificador pintase basura. Se salta el fotograma y se dice.
    if(frameOut) *frameOut = a->frameNo;
    a->frameNo++;
    return FLEXAVI_ERR_TOOBIG;
  }
  if(!ioReadAt(&a->io, off, buf, len)) return FLEXAVI_ERR_IO;
  if(frameOut) *frameOut = a->frameNo;
  a->frameNo++;
  return (int)len;
}

int flexAviSkipFrame(FlexAviCtx* a){
  if(!a) return FLEXAVI_ERR_IO;
  int r = aviNextVideoChunk(a, NULL, NULL);
  if(r != FLEXAVI_OK) return r;
  a->frameNo++;
  return FLEXAVI_OK;
}

int flexAviSeekFrame(FlexAviCtx* a, uint32_t frame){
  if(!a) return FLEXAVI_ERR_IO;
  if(a->frames && frame >= a->frames) frame = a->frames ? a->frames - 1 : 0;

  // Punto de partida: la muestra anterior mas cercana, o el
  // principio de 'movi'. Nunca se salta hacia delante a ciegas.
  uint32_t startOff = a->moviStart, startFrame = 0;
  if(a->idxFromFile){
    for(int i = (int)a->idxN - 1; i >= 0; i--){
      if(a->idxFrame[i] <= frame){
        uint32_t abs = aviResolveIdx(a, a->idxOff[i]);
        if(abs){ startOff = abs; startFrame = a->idxFrame[i]; }
        break;
      }
    }
  } else if(a->frameNo <= frame){
    // Sin idx1 solo se puede ir hacia delante desde donde estamos:
    // rebobinar al principio para adelantar seria absurdo.
    startOff = a->cursor; startFrame = a->frameNo;
  }

  a->cursor  = startOff;
  a->frameNo = startFrame;
  // Avance por cabeceras: 8 bytes leidos por fotograma saltado, sin
  // tocar los datos comprimidos.
  int guard = 0;
  while(a->frameNo < frame){
    int r = flexAviSkipFrame(a);
    if(r != FLEXAVI_OK) return r;
    if(++guard > 100000) break;               // corta un fichero circular corrupto
  }
  return (int)a->frameNo;
}

// -------------------------------------------------------------
//  WAV
// -------------------------------------------------------------
int flexWavParse(const FlexMediaIO* io, FlexWavInfo* w){
  if(!io || !w || !io->read || !io->seek || !io->size) return FLEXWAV_ERR_IO;
  memset(w, 0, sizeof(*w));
  const uint32_t total = io->size(io->ctx);
  if(total < 44) return FLEXWAV_ERR_FORMAT;

  uint8_t h[12];
  if(!ioReadAt(io, 0, h, 12)) return FLEXWAV_ERR_IO;
  if(!fourcc(h, "RIFF") || !fourcc(h + 8, "WAVE")) return FLEXWAV_ERR_FORMAT;

  bool haveFmt = false;
  uint32_t p = 12;
  while(p + 8 <= total){
    uint8_t c[8];
    if(!ioReadAt(io, p, c, 8)) break;
    uint32_t len = rd32(c + 4);
    if(fourcc(c, "fmt ") && len >= 16){
      uint8_t v[16];
      if(!ioReadAt(io, p + 8, v, 16)) return FLEXWAV_ERR_IO;
      uint16_t tag = rd16(v);
      w->channels   = rd16(v + 2);
      w->sampleRate = rd32(v + 4);
      w->bits       = rd16(v + 14);
      // 1 = PCM entero. 0xFFFE (extensible) se acepta solo si los
      // bits y canales son los de un PCM normal; cualquier otro tag
      // es audio comprimido y aqui no hay decodificador.
      if(tag != 1 && tag != 0xFFFE) return FLEXWAV_ERR_CODEC;
      if(w->bits != 8 && w->bits != 16) return FLEXWAV_ERR_CODEC;
      if(w->channels == 0 || w->channels > 2) return FLEXWAV_ERR_CODEC;
      if(w->sampleRate < 4000 || w->sampleRate > 192000) return FLEXWAV_ERR_FORMAT;
      haveFmt = true;
    } else if(fourcc(c, "data")){
      if(!haveFmt) return FLEXWAV_ERR_FORMAT;
      w->dataStart = p + 8;
      uint32_t avail = total - w->dataStart;
      w->dataBytes  = (len <= avail) ? len : avail;   // fichero truncado: lo que haya
      return FLEXWAV_OK;
    }
    if(len == 0) break;
    p = p + 8 + pad2(len);
  }
  return FLEXWAV_ERR_FORMAT;
}

uint32_t flexWavDurationMs(const FlexWavInfo* w){
  if(!w || !w->sampleRate || !w->channels || !w->bits) return 0;
  uint32_t frameBytes = (uint32_t)w->channels * (w->bits / 8u);
  if(!frameBytes) return 0;
  return (uint32_t)(((uint64_t)(w->dataBytes / frameBytes) * 1000ull) / w->sampleRate);
}

// -------------------------------------------------------------
//  INDICE INCREMENTAL
//  ------------------------------------------------------------
//  El recorrido es una pila de como mucho FLEXMED_DEPTH_MAX
//  niveles, cada uno con su contador de "entradas ya consumidas".
//  No hay recursion: la pila es explicita y de tamano fijo, asi que
//  ni se desborda ni gasta pila de la tarea de dibujo.
//
//  Cada llamada a Step consume como mucho `budget` entradas y
//  vuelve. El estado que hace falta para continuar cabe entero en
//  el struct, asi que se puede parar y seguir en cualquier punto,
//  incluido a mitad de una carpeta de 4.000 ficheros.
// -------------------------------------------------------------
#define FLEXMED_BATCH 8      // entradas por lectura de directorio

void flexMediaIndexInit(FlexMediaIndex* ix, FlexMediaItem* store, uint16_t cap){
  if(!ix) return;
  memset(ix, 0, sizeof(*ix));
  ix->items = store;
  ix->cap   = store ? cap : 0;
  ix->state = FLEXMED_SCAN_IDLE;
  ix->depth = -1;
}

void flexMediaIndexAddRoot(FlexMediaIndex* ix, const char* path, uint8_t vol){
  if(!ix || !path || ix->rootN >= FLEXMED_ROOTS_MAX) return;
  ix->roots[ix->rootN].path = path;
  ix->roots[ix->rootN].vol  = vol;
  ix->rootN++;
}

void flexMediaIndexStart(FlexMediaIndex* ix, uint32_t sdGen){
  if(!ix) return;
  ix->n     = 0;
  ix->full  = false;
  ix->rootI = 0;
  ix->depth = -1;
  ix->seen  = 0;
  ix->sdGen = sdGen;
  ix->state = ix->rootN ? FLEXMED_SCAN_RUNNING : FLEXMED_SCAN_DONE;
  memset(ix->stackSkip, 0, sizeof(ix->stackSkip));
}

void flexMediaIndexAbort(FlexMediaIndex* ix){
  if(!ix) return;
  ix->state = FLEXMED_SCAN_ABORTED;
  ix->depth = -1;
}

void flexMediaIndexDropSd(FlexMediaIndex* ix){
  if(!ix || !ix->items) return;
  uint16_t w = 0;
  for(uint16_t i = 0; i < ix->n; i++){
    if(ix->items[i].vol == FLEXMED_VOL_SD) continue;
    if(w != i) ix->items[w] = ix->items[i];
    w++;
  }
  ix->n = w;
  ix->full = false;
  // Si el recorrido estaba dentro de la tarjeta, se corta: seguir
  // pidiendo directorios a un volumen que ya no esta solo produce
  // errores.
  if(ix->state == FLEXMED_SCAN_RUNNING && ix->rootI < ix->rootN
     && ix->roots[ix->rootI].vol == FLEXMED_VOL_SD){
    ix->depth = -1;
  }
}

static const FlexMediaVolume* volOf(FlexMediaIndex* ix, uint8_t vol){
  return (vol == FLEXMED_VOL_SD) ? &ix->volSd : &ix->volInt;
}

// Empuja un directorio en la pila. false si ya no cabe mas hondo.
static bool pushDir(FlexMediaIndex* ix, const char* path){
  if(ix->depth + 1 >= FLEXMED_DEPTH_MAX) return false;
  ix->depth++;
  snprintf(ix->stackPath[ix->depth], FLEXMED_PATH_MAX, "%s", path);
  ix->stackSkip[ix->depth] = 0;
  return true;
}

static void addItem(FlexMediaIndex* ix, const char* dir, const FlexMediaDirent* e,
                    int kind, uint8_t vol){
  if(ix->n >= ix->cap){ ix->full = true; return; }
  FlexMediaItem* it = &ix->items[ix->n];
  // Una ruta que no cabe entera se DESCARTA en vez de guardarse
  // truncada: una ruta truncada no abre el fichero, abre otro (o
  // ninguno), y eso es peor que no tenerlo en la lista.
  int need = snprintf(it->path, FLEXMED_PATH_MAX, "%s/%s", dir, e->name);
  if(need < 0 || need >= FLEXMED_PATH_MAX) return;
  it->size = e->size;
  it->kind = (uint8_t)kind;
  it->vol  = vol;
  ix->n++;
}

int flexMediaIndexStep(FlexMediaIndex* ix, int budget){
  if(!ix) return FLEXMED_SCAN_IDLE;
  if(ix->state != FLEXMED_SCAN_RUNNING) return ix->state;
  if(budget <= 0) budget = 1;

  while(budget > 0){
    // ---- entre carpetas: abrir la siguiente raiz ----
    if(ix->depth < 0){
      if(ix->rootI >= ix->rootN){ ix->state = FLEXMED_SCAN_DONE; return ix->state; }
      const FlexMediaRoot* r = &ix->roots[ix->rootI];
      const FlexMediaVolume* v = volOf(ix, r->vol);
      // Un volumen que no esta no es un error: sencillamente no
      // aporta nada al indice y se pasa a la siguiente raiz.
      if(!v->list || (v->alive && !v->alive(v->ctx))){ ix->rootI++; continue; }
      if(!pushDir(ix, r->path)){ ix->rootI++; continue; }
      continue;
    }

    const uint8_t vol = ix->roots[ix->rootI].vol;
    const FlexMediaVolume* v = volOf(ix, vol);
    if(v->alive && !v->alive(v->ctx)){
      // El volumen desaparecio a mitad del recorrido (la tarjeta se
      // saco). Se abandona ESA raiz, no el indice entero: lo ya
      // encontrado en la memoria interna sigue valiendo.
      ix->depth = -1;
      ix->rootI++;
      continue;
    }

    FlexMediaDirent ent[FLEXMED_BATCH];
    const char* dir = ix->stackPath[ix->depth];
    int want = budget < FLEXMED_BATCH ? budget : FLEXMED_BATCH;
    int got  = v->list(v->ctx, dir, ent, want, (int)ix->stackSkip[ix->depth]);

    if(got <= 0){
      // 0 = carpeta terminada; -1 = no se pudo abrir. En los dos
      // casos se sube un nivel: un directorio ilegible no puede
      // parar el indice entero.
      ix->depth--;
      if(ix->depth < 0) ix->rootI++;
      if(got < 0) budget--;                   // el intento tambien cuesta
      continue;
    }

    // CUENTA EXACTA DE LO CONSUMIDO. stackSkip se sube por lo que
    // se ha mirado DE VERDAD, no por el tamano del lote: si en
    // mitad del lote aparece una subcarpeta se entra en ella y el
    // resto del lote se vuelve a pedir al volver. Sumar `got` de
    // golpe aqui haria que los ficheros que van detras de una
    // subcarpeta dentro del mismo lote no se indexaran nunca.
    int used = 0;
    bool descended = false;
    for(int i = 0; i < got; i++){
      used++;
      ix->seen++;
      budget--;
      if(ent[i].name[0] == '.') continue;     // ocultos y ".", ".."
      if(ent[i].dir){
        char sub[FLEXMED_PATH_MAX];
        int need = snprintf(sub, sizeof(sub), "%s/%s", dir, ent[i].name);
        // Si no cabe mas hondo (o la ruta no cabe) la subcarpeta se
        // ignora: el limite de profundidad esta documentado y es
        // preferible a una pila sin tope.
        if(need > 0 && need < (int)sizeof(sub) && ix->depth + 1 < FLEXMED_DEPTH_MAX){
          ix->stackSkip[ix->depth] += (uint32_t)used;   // lo del padre, ya visto
          pushDir(ix, sub);
          descended = true;
          break;
        }
        continue;
      }
      int kind = flexMediaClassify(ent[i].name);
      if(kind == FLEXMED_PHOTO || kind == FLEXMED_VIDEO
      || kind == FLEXMED_AUDIO || kind == FLEXMED_DRAW)
        addItem(ix, dir, &ent[i], kind, vol);
    }
    if(!descended) ix->stackSkip[ix->depth] += (uint32_t)used;
  }
  return ix->state;
}

static bool itemMatches(const FlexMediaItem* it, int kind, int vol){
  if(kind && it->kind != (uint8_t)kind) return false;
  if(vol >= 0 && it->vol != (uint8_t)vol) return false;
  return true;
}

int flexMediaIndexCount(const FlexMediaIndex* ix, int kind, int vol){
  if(!ix || !ix->items) return 0;
  int c = 0;
  for(uint16_t i = 0; i < ix->n; i++) if(itemMatches(&ix->items[i], kind, vol)) c++;
  return c;
}

int flexMediaIndexNth(const FlexMediaIndex* ix, int kind, int vol, int nth){
  if(!ix || !ix->items || nth < 0) return -1;
  for(uint16_t i = 0; i < ix->n; i++){
    if(!itemMatches(&ix->items[i], kind, vol)) continue;
    if(nth-- == 0) return (int)i;
  }
  return -1;
}
