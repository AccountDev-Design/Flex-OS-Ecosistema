// #############################################################
// ##  FlexOS · BIBLIOTECA DE MEDIOS  ·  implementacion
// ##  Ver FlexOS_MediaLib.h. Regla del archivo: nada que venga de
// ##  fuera (un nombre del movil, un catalogo leido de la flash) se
// ##  copia sin un limite explicito, y toda cadena de salida acaba en
// ##  cero aunque se haya recortado.
// #############################################################
#include "FlexOS_MediaLib.h"
#include <string.h>
#include <stdio.h>

// El registro se guarda tal cual en la flash: su tamano forma parte del
// formato del fichero. Si alguien anade un campo, esto no compila y el
// numero de version del fichero tiene que subir con el.
static_assert(sizeof(FlexMlRec) == 336, "FlexMlRec forma parte del formato de library.fml");

// -------------------------------------------------------------
//  CRC-32 y hash
// -------------------------------------------------------------
// Tabla constante (polinomio reflejado 0xEDB88320). Constante y no
// construida en el primer uso: la llaman a la vez la tarea del servidor
// web y la del sistema, y una tabla perezosa seria una carrera.
static const uint32_t kCrcTab[256] = {
  0x00000000u, 0x77073096u, 0xEE0E612Cu, 0x990951BAu, 0x076DC419u, 0x706AF48Fu,
  0xE963A535u, 0x9E6495A3u, 0x0EDB8832u, 0x79DCB8A4u, 0xE0D5E91Eu, 0x97D2D988u,
  0x09B64C2Bu, 0x7EB17CBDu, 0xE7B82D07u, 0x90BF1D91u, 0x1DB71064u, 0x6AB020F2u,
  0xF3B97148u, 0x84BE41DEu, 0x1ADAD47Du, 0x6DDDE4EBu, 0xF4D4B551u, 0x83D385C7u,
  0x136C9856u, 0x646BA8C0u, 0xFD62F97Au, 0x8A65C9ECu, 0x14015C4Fu, 0x63066CD9u,
  0xFA0F3D63u, 0x8D080DF5u, 0x3B6E20C8u, 0x4C69105Eu, 0xD56041E4u, 0xA2677172u,
  0x3C03E4D1u, 0x4B04D447u, 0xD20D85FDu, 0xA50AB56Bu, 0x35B5A8FAu, 0x42B2986Cu,
  0xDBBBC9D6u, 0xACBCF940u, 0x32D86CE3u, 0x45DF5C75u, 0xDCD60DCFu, 0xABD13D59u,
  0x26D930ACu, 0x51DE003Au, 0xC8D75180u, 0xBFD06116u, 0x21B4F4B5u, 0x56B3C423u,
  0xCFBA9599u, 0xB8BDA50Fu, 0x2802B89Eu, 0x5F058808u, 0xC60CD9B2u, 0xB10BE924u,
  0x2F6F7C87u, 0x58684C11u, 0xC1611DABu, 0xB6662D3Du, 0x76DC4190u, 0x01DB7106u,
  0x98D220BCu, 0xEFD5102Au, 0x71B18589u, 0x06B6B51Fu, 0x9FBFE4A5u, 0xE8B8D433u,
  0x7807C9A2u, 0x0F00F934u, 0x9609A88Eu, 0xE10E9818u, 0x7F6A0DBBu, 0x086D3D2Du,
  0x91646C97u, 0xE6635C01u, 0x6B6B51F4u, 0x1C6C6162u, 0x856530D8u, 0xF262004Eu,
  0x6C0695EDu, 0x1B01A57Bu, 0x8208F4C1u, 0xF50FC457u, 0x65B0D9C6u, 0x12B7E950u,
  0x8BBEB8EAu, 0xFCB9887Cu, 0x62DD1DDFu, 0x15DA2D49u, 0x8CD37CF3u, 0xFBD44C65u,
  0x4DB26158u, 0x3AB551CEu, 0xA3BC0074u, 0xD4BB30E2u, 0x4ADFA541u, 0x3DD895D7u,
  0xA4D1C46Du, 0xD3D6F4FBu, 0x4369E96Au, 0x346ED9FCu, 0xAD678846u, 0xDA60B8D0u,
  0x44042D73u, 0x33031DE5u, 0xAA0A4C5Fu, 0xDD0D7CC9u, 0x5005713Cu, 0x270241AAu,
  0xBE0B1010u, 0xC90C2086u, 0x5768B525u, 0x206F85B3u, 0xB966D409u, 0xCE61E49Fu,
  0x5EDEF90Eu, 0x29D9C998u, 0xB0D09822u, 0xC7D7A8B4u, 0x59B33D17u, 0x2EB40D81u,
  0xB7BD5C3Bu, 0xC0BA6CADu, 0xEDB88320u, 0x9ABFB3B6u, 0x03B6E20Cu, 0x74B1D29Au,
  0xEAD54739u, 0x9DD277AFu, 0x04DB2615u, 0x73DC1683u, 0xE3630B12u, 0x94643B84u,
  0x0D6D6A3Eu, 0x7A6A5AA8u, 0xE40ECF0Bu, 0x9309FF9Du, 0x0A00AE27u, 0x7D079EB1u,
  0xF00F9344u, 0x8708A3D2u, 0x1E01F268u, 0x6906C2FEu, 0xF762575Du, 0x806567CBu,
  0x196C3671u, 0x6E6B06E7u, 0xFED41B76u, 0x89D32BE0u, 0x10DA7A5Au, 0x67DD4ACCu,
  0xF9B9DF6Fu, 0x8EBEEFF9u, 0x17B7BE43u, 0x60B08ED5u, 0xD6D6A3E8u, 0xA1D1937Eu,
  0x38D8C2C4u, 0x4FDFF252u, 0xD1BB67F1u, 0xA6BC5767u, 0x3FB506DDu, 0x48B2364Bu,
  0xD80D2BDAu, 0xAF0A1B4Cu, 0x36034AF6u, 0x41047A60u, 0xDF60EFC3u, 0xA867DF55u,
  0x316E8EEFu, 0x4669BE79u, 0xCB61B38Cu, 0xBC66831Au, 0x256FD2A0u, 0x5268E236u,
  0xCC0C7795u, 0xBB0B4703u, 0x220216B9u, 0x5505262Fu, 0xC5BA3BBEu, 0xB2BD0B28u,
  0x2BB45A92u, 0x5CB36A04u, 0xC2D7FFA7u, 0xB5D0CF31u, 0x2CD99E8Bu, 0x5BDEAE1Du,
  0x9B64C2B0u, 0xEC63F226u, 0x756AA39Cu, 0x026D930Au, 0x9C0906A9u, 0xEB0E363Fu,
  0x72076785u, 0x05005713u, 0x95BF4A82u, 0xE2B87A14u, 0x7BB12BAEu, 0x0CB61B38u,
  0x92D28E9Bu, 0xE5D5BE0Du, 0x7CDCEFB7u, 0x0BDBDF21u, 0x86D3D2D4u, 0xF1D4E242u,
  0x68DDB3F8u, 0x1FDA836Eu, 0x81BE16CDu, 0xF6B9265Bu, 0x6FB077E1u, 0x18B74777u,
  0x88085AE6u, 0xFF0F6A70u, 0x66063BCAu, 0x11010B5Cu, 0x8F659EFFu, 0xF862AE69u,
  0x616BFFD3u, 0x166CCF45u, 0xA00AE278u, 0xD70DD2EEu, 0x4E048354u, 0x3903B3C2u,
  0xA7672661u, 0xD06016F7u, 0x4969474Du, 0x3E6E77DBu, 0xAED16A4Au, 0xD9D65ADCu,
  0x40DF0B66u, 0x37D83BF0u, 0xA9BCAE53u, 0xDEBB9EC5u, 0x47B2CF7Fu, 0x30B5FFE9u,
  0xBDBDF21Cu, 0xCABAC28Au, 0x53B39330u, 0x24B4A3A6u, 0xBAD03605u, 0xCDD70693u,
  0x54DE5729u, 0x23D967BFu, 0xB3667A2Eu, 0xC4614AB8u, 0x5D681B02u, 0x2A6F2B94u,
  0xB40BBE37u, 0xC30C8EA1u, 0x5A05DF1Bu, 0x2D02EF8Du,
};

uint32_t flexMlCrc32(uint32_t crc, const void* data, size_t n){
  const uint8_t* p = (const uint8_t*)data;
  crc = ~crc;
  if(p) for(size_t i = 0; i < n; i++) crc = kCrcTab[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
  return ~crc;
}

uint32_t flexMlHash(const char* s){
  uint32_t h = 2166136261u;
  for(const unsigned char* p = (const unsigned char*)s; p && *p; p++) h = (h ^ *p) * 16777619u;
  return h ? h : 1u;
}

// -------------------------------------------------------------
//  Utilidades pequenas
// -------------------------------------------------------------
static inline uint16_t rd16le(const uint8_t* p){ return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t rd32le(const uint8_t* p){
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void wr16le(uint8_t* p, uint16_t v){ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void wr32le(uint8_t* p, uint32_t v){
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static inline char lower1(char c){ return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static bool eqNoCase(const char* a, const char* b){
  for(;; a++, b++){
    if(lower1(*a) != lower1(*b)) return false;
    if(!*a) return true;
  }
}
static bool fourcc(const uint8_t* p, const char* s){ return !memcmp(p, s, 4); }
static bool fourccNoCase(const uint8_t* p, const char* s){
  for(int i = 0; i < 4; i++) if(lower1((char)p[i]) != lower1(s[i])) return false;
  return true;
}
// Copia acotada que SIEMPRE termina en cero.
static void copyz(char* dst, size_t cap, const char* src){
  if(!dst || cap == 0) return;
  size_t i = 0;
  if(src) for(; src[i] && i + 1 < cap; i++) dst[i] = src[i];
  dst[i] = 0;
}
static bool terminated(const char* s, size_t cap){ return memchr(s, 0, cap) != NULL; }

// -------------------------------------------------------------
//  FORMATOS
// -------------------------------------------------------------
struct FmtInfo { const char* name; const char* ext; uint8_t kind; bool playable; const char* why; };
static const FmtInfo kFmt[FML_F_COUNT] = {
  /* UNKNOWN   */ { "Desconocido", "",      FML_K_NONE,  false, "Formato desconocido" },
  /* JPEG      */ { "JPEG",        ".jpg",  FML_K_PHOTO, true,  NULL },
  /* JPEG_PROG */ { "JPEG progresivo", ".jpg", FML_K_PHOTO, false,
                    "JPEG progresivo: esta placa solo decodifica JPEG baseline" },
  /* PNG       */ { "PNG",  ".png",  FML_K_PHOTO, false, "PNG: esta placa solo decodifica JPEG" },
  /* GIF       */ { "GIF",  ".gif",  FML_K_PHOTO, false, "GIF: esta placa solo decodifica JPEG" },
  /* BMP       */ { "BMP",  ".bmp",  FML_K_PHOTO, false, "BMP: esta placa solo decodifica JPEG" },
  /* WEBP      */ { "WebP", ".webp", FML_K_PHOTO, false, "WebP: esta placa solo decodifica JPEG" },
  /* HEIC      */ { "HEIC", ".heic", FML_K_PHOTO, false, "HEIC: esta placa solo decodifica JPEG" },
  /* AVIF      */ { "AVIF", ".avif", FML_K_PHOTO, false, "AVIF: esta placa solo decodifica JPEG" },
  /* AVI_MJPEG */ { "AVI MJPEG", ".avi", FML_K_VIDEO, true, NULL },
  /* AVI_OTHER */ { "AVI", ".avi",  FML_K_VIDEO, false, "AVI sin v\xC3\xAD" "deo MJPEG: no se puede reproducir" },
  /* MP4       */ { "MP4", ".mp4",  FML_K_VIDEO, false,
                    "MP4: esta placa no tiene decodificador H.264/H.265" },
  /* MOV       */ { "MOV", ".mov",  FML_K_VIDEO, false,
                    "MOV: esta placa no tiene decodificador H.264/H.265" },
  /* WEBM      */ { "WebM", ".webm", FML_K_VIDEO, false, "WebM: esta placa no tiene decodificador VP8/VP9" },
  /* MKV       */ { "MKV", ".mkv",  FML_K_VIDEO, false, "MKV: esta placa no reproduce este contenedor" },
  /* WAV_PCM   */ { "WAV PCM", ".wav", FML_K_AUDIO, true, NULL },
  /* WAV_ADPCM */ { "WAV IMA ADPCM", ".wav", FML_K_AUDIO, true, NULL },
  /* WAV_OTHER */ { "WAV", ".wav",  FML_K_AUDIO, false,
                    "WAV comprimido: solo se reproduce PCM o IMA ADPCM" },
  /* MP3       */ { "MP3", ".mp3",  FML_K_AUDIO, false, "MP3: esta placa no tiene decodificador MP3" },
  /* AAC       */ { "AAC", ".aac",  FML_K_AUDIO, false, "AAC: esta placa no tiene decodificador AAC" },
  /* M4A       */ { "M4A", ".m4a",  FML_K_AUDIO, false, "M4A: esta placa no tiene decodificador AAC" },
  /* FLAC      */ { "FLAC", ".flac", FML_K_AUDIO, false, "FLAC: esta placa no tiene decodificador FLAC" },
  /* OGG       */ { "OGG", ".ogg",  FML_K_AUDIO, false, "OGG: esta placa no tiene decodificador Vorbis/Opus" },
  /* FXP       */ { "Dibujo", ".fxp", FML_K_DRAW, true, NULL },
};

int flexMlKindOfFmt(int fmt){ return (fmt > 0 && fmt < FML_F_COUNT) ? (int)kFmt[fmt].kind : (int)FML_K_NONE; }
bool flexMlFmtPlayable(int fmt){ return fmt > 0 && fmt < FML_F_COUNT && kFmt[fmt].playable; }
const char* flexMlFmtName(int fmt){ return (fmt >= 0 && fmt < FML_F_COUNT) ? kFmt[fmt].name : kFmt[0].name; }
const char* flexMlFmtExt(int fmt){ return (fmt >= 0 && fmt < FML_F_COUNT) ? kFmt[fmt].ext : ""; }
const char* flexMlWhyUnplayable(int fmt){
  if(fmt <= 0 || fmt >= FML_F_COUNT) return kFmt[0].why;
  return kFmt[fmt].playable ? NULL : kFmt[fmt].why;
}
const char* flexMlKindName(int kind){
  switch(kind){
    case FML_K_PHOTO: return "photo";
    case FML_K_VIDEO: return "video";
    case FML_K_AUDIO: return "audio";
    case FML_K_DRAW:  return "draw";
  }
  return "none";
}
int flexMlKindParse(const char* s){
  if(!s) return FML_K_NONE;
  if(!strcmp(s, "photo")) return FML_K_PHOTO;
  if(!strcmp(s, "video")) return FML_K_VIDEO;
  if(!strcmp(s, "audio")) return FML_K_AUDIO;
  return FML_K_NONE;                       // "draw" no llega por la red
}

int flexMlKindFromExt(const char* name){
  if(!name) return FML_K_NONE;
  const char* d = strrchr(name, '.');
  if(!d || !d[1]) return FML_K_NONE;
  static const char* PH[] = { ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp", ".heic", ".heif", ".avif" };
  static const char* VI[] = { ".avi", ".mp4", ".m4v", ".mov", ".webm", ".mkv", ".3gp" };
  static const char* AU[] = { ".wav", ".mp3", ".aac", ".m4a", ".flac", ".ogg", ".opus" };
  for(const char* e : PH) if(eqNoCase(d, e)) return FML_K_PHOTO;
  for(const char* e : VI) if(eqNoCase(d, e)) return FML_K_VIDEO;
  for(const char* e : AU) if(eqNoCase(d, e)) return FML_K_AUDIO;
  if(eqNoCase(d, ".fxp")) return FML_K_DRAW;
  return FML_K_NONE;
}

// JPEG: recorre los segmentos hasta el SOF para saber si es baseline.
// Si el SOF no cae dentro de lo que hay (EXIF de 60 KB), se devuelve
// FML_F_JPEG como hipotesis: la validacion completa se hace despues con
// el decodificador, sobre el archivo entero.
static int sniffJpeg(const uint8_t* h, size_t n){
  size_t i = 2;
  while(i + 4 <= n){
    if(h[i] != 0xFF) return FML_F_JPEG;          // flujo raro: lo decidira el decodificador
    uint8_t m = h[i + 1];
    if(m == 0xFF){ i++; continue; }              // relleno
    if(m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01){ i += 2; continue; }
    if(m == 0xC0 || m == 0xC1) return FML_F_JPEG;
    if(m == 0xC2 || m == 0xC3 || (m >= 0xC5 && m <= 0xC7) || (m >= 0xC9 && m <= 0xCB) ||
       (m >= 0xCD && m <= 0xCF)) return FML_F_JPEG_PROG;   // progresivo, sin perdidas o aritmetico
    if(m == 0xDA || m == 0xD9) return FML_F_JPEG;          // SOS sin SOF: lo decidira el decodificador
    size_t len = ((size_t)h[i + 2] << 8) | h[i + 3];
    if(len < 2) return FML_F_JPEG;
    i += 2 + len;
  }
  return FML_F_JPEG;
}

// RIFF: WEBP, AVI (con su codec) y WAVE (con su formato).
static int sniffRiff(const uint8_t* h, size_t n, int* kind){
  if(n < 12) return FML_F_UNKNOWN;
  if(fourcc(h + 8, "WEBP")){ *kind = FML_K_PHOTO; return FML_F_WEBP; }
  if(fourcc(h + 8, "AVI ")){
    *kind = FML_K_VIDEO;
    // Busca la cabecera de la pista de video ('strh' + 'vids') en lo que haya.
    for(size_t i = 12; i + 16 <= n; i++){
      if(fourcc(h + i, "strh") && fourcc(h + i + 8, "vids")){
        const uint8_t* cc = h + i + 12;
        if(fourccNoCase(cc, "mjpg") || fourccNoCase(cc, "jpeg") ||
           fourccNoCase(cc, "dmb1") || fourccNoCase(cc, "mjpa")) return FML_F_AVI_MJPEG;
        return FML_F_AVI_OTHER;
      }
    }
    return FML_F_AVI_MJPEG;                      // hipotesis: lo confirma flexAviOpen
  }
  if(fourcc(h + 8, "WAVE")){
    *kind = FML_K_AUDIO;
    size_t p = 12;
    while(p + 8 <= n){
      uint32_t len = rd32le(h + p + 4);
      if(fourcc(h + p, "fmt ") && p + 8 + 16 <= n){
        const uint8_t* v = h + p + 8;
        uint16_t tag = rd16le(v), ch = rd16le(v + 2), bits = rd16le(v + 14);
        if(tag == 0xFFFE && len >= 40 && p + 8 + 26 <= n) tag = rd16le(v + 24);   // extensible
        if(tag == 1 && (bits == 8 || bits == 16) && ch >= 1 && ch <= 2) return FML_F_WAV_PCM;
        if(tag == 0x11 && bits == 4 && ch >= 1 && ch <= 2) return FML_F_WAV_ADPCM;
        return FML_F_WAV_OTHER;
      }
      if(len > n) break;
      p += 8 + len + (len & 1u);
    }
    return FML_F_WAV_OTHER;
  }
  return FML_F_UNKNOWN;
}

int flexMlSniff(const uint8_t* h, size_t n, int* kindOut){
  int kd = FML_K_NONE;
  int f = FML_F_UNKNOWN;
  if(!h || n < 4){ if(kindOut) *kindOut = kd; return f; }
  if(n >= 3 && h[0] == 0xFF && h[1] == 0xD8 && h[2] == 0xFF){ f = sniffJpeg(h, n); }
  else if(n >= 8 && !memcmp(h, "\x89PNG\r\n\x1a\n", 8)) f = FML_F_PNG;
  else if(n >= 6 && (!memcmp(h, "GIF87a", 6) || !memcmp(h, "GIF89a", 6))) f = FML_F_GIF;
  else if(n >= 14 && h[0] == 'B' && h[1] == 'M' && rd32le(h + 6) == 0 && rd32le(h + 10) < (1u << 20))
    f = FML_F_BMP;
  else if(fourcc(h, "RIFF")) f = sniffRiff(h, n, &kd);
  else if(n >= 12 && fourcc(h + 4, "ftyp")){
    const uint8_t* b = h + 8;
    if(fourcc(b, "heic") || fourcc(b, "heix") || fourcc(b, "hevc") || fourcc(b, "hevx") ||
       fourcc(b, "heim") || fourcc(b, "heis") || fourcc(b, "mif1") || fourcc(b, "msf1")) f = FML_F_HEIC;
    else if(fourcc(b, "avif") || fourcc(b, "avis")) f = FML_F_AVIF;
    else if(fourcc(b, "M4A ") || fourcc(b, "M4B ") || fourcc(b, "M4P ")) f = FML_F_M4A;
    else if(fourcc(b, "qt  ")) f = FML_F_MOV;
    else f = FML_F_MP4;
  }
  else if(h[0] == 0x1A && h[1] == 0x45 && h[2] == 0xDF && h[3] == 0xA3){
    f = FML_F_MKV;
    for(size_t i = 4; i + 4 <= n && i < 64; i++) if(!memcmp(h + i, "webm", 4)){ f = FML_F_WEBM; break; }
  }
  else if(fourcc(h, "fLaC")) f = FML_F_FLAC;
  else if(fourcc(h, "OggS")) f = FML_F_OGG;
  else if(fourcc(h, "FXP1")) f = FML_F_FXP;
  else if(n >= 3 && h[0] == 'I' && h[1] == 'D' && h[2] == '3') f = FML_F_MP3;
  else if(h[0] == 0xFF && (h[1] & 0xF6) == 0xF0) f = FML_F_AAC;            // ADTS (capa 00)
  else if(h[0] == 0xFF && (h[1] & 0xE0) == 0xE0 && ((h[1] >> 1) & 3) == 1 &&
          ((h[1] >> 3) & 3) != 1 && (h[2] >> 4) != 15 && ((h[2] >> 2) & 3) != 3) f = FML_F_MP3;
  if(f != FML_F_UNKNOWN && kd == FML_K_NONE) kd = flexMlKindOfFmt(f);
  if(kindOut) *kindOut = kd;
  return f;
}

// -------------------------------------------------------------
//  NOMBRES Y RUTAS
// -------------------------------------------------------------
// Transliteracion de los caracteres latinos que de verdad aparecen en
// nombres de fotos y canciones (espanol, portugues, frances, italiano,
// aleman). Lo demas pasa a '_': un nombre de disco no es el sitio para
// escribir japones, y el nombre ORIGINAL se conserva igual en el catalogo.
static const char* translit(uint32_t cp){
  switch(cp){
    case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: return "A";
    case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: return "a";
    case 0xC7: return "C"; case 0xE7: return "c";
    case 0xC8: case 0xC9: case 0xCA: case 0xCB: return "E";
    case 0xE8: case 0xE9: case 0xEA: case 0xEB: return "e";
    case 0xCC: case 0xCD: case 0xCE: case 0xCF: return "I";
    case 0xEC: case 0xED: case 0xEE: case 0xEF: return "i";
    case 0xD1: return "N"; case 0xF1: return "n";
    case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: case 0xD8: return "O";
    case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: case 0xF8: return "o";
    case 0xD9: case 0xDA: case 0xDB: case 0xDC: return "U";
    case 0xF9: case 0xFA: case 0xFB: case 0xFC: return "u";
    case 0xDD: return "Y"; case 0xFD: case 0xFF: return "y";
    case 0xDF: return "ss"; case 0xC6: return "AE"; case 0xE6: return "ae";
    case 0x152: return "OE"; case 0x153: return "oe";
  }
  return NULL;
}

// Decodifica un punto de codigo UTF-8. Devuelve los bytes consumidos (>=1)
// y deja en *cp el valor, o 0xFFFD si la secuencia no es valida.
static int utf8Next(const unsigned char* s, uint32_t* cp){
  unsigned char c = s[0];
  if(c < 0x80){ *cp = c; return 1; }
  int n = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
  if(!n){ *cp = 0xFFFD; return 1; }
  uint32_t v = c & (0x7F >> n);
  for(int i = 1; i < n; i++){
    if((s[i] & 0xC0) != 0x80){ *cp = 0xFFFD; return 1; }
    v = (v << 6) | (s[i] & 0x3F);
  }
  // Formas demasiado largas y sustitutos: invalidos.
  if((n == 2 && v < 0x80) || (n == 3 && v < 0x800) || (n == 4 && v < 0x10000) ||
     (v >= 0xD800 && v <= 0xDFFF) || v > 0x10FFFF){ *cp = 0xFFFD; return 1; }
  *cp = v;
  return n;
}

size_t flexMlSafeStem(const char* in, char* out, size_t cap){
  if(!out || cap == 0) return 0;
  out[0] = 0;
  const char* s = in ? in : "";
  // Solo el nombre: cualquier resto de ruta se descarta.
  for(const char* p = s; *p; p++) if(*p == '/' || *p == '\\') s = p + 1;
  // Fin de la raiz: el ultimo '.' que no sea el primer caracter.
  size_t len = strlen(s);
  const char* dot = strrchr(s, '.');
  if(dot && dot != s) len = (size_t)(dot - s);

  size_t lim = cap - 1;
  if(lim > FML_STEM_MAX) lim = FML_STEM_MAX;
  size_t w = 0;
  bool lastSep = true;                       // evita separadores al principio
  for(size_t i = 0; i < len && w < lim; ){
    uint32_t cp;
    int k = utf8Next((const unsigned char*)s + i, &cp);
    if(i + (size_t)k > len) break;
    i += (size_t)k;
    const char* rep = NULL;
    char one[2] = { 0, 0 };
    if(cp < 0x80){
      char c = (char)cp;
      if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
         c == '-' || c == '(' || c == ')' || c == '.' || c == '_'){ one[0] = c; rep = one; }
      else if(c == ' '){ one[0] = ' '; rep = one; }
      else { one[0] = '_'; rep = one; }
    } else {
      rep = translit(cp);
      if(!rep){ one[0] = '_'; rep = one; }
    }
    for(const char* r = rep; *r && w < lim; r++){
      char c = *r;
      bool sep = (c == ' ' || c == '_');
      if(sep && lastSep) continue;           // junta separadores seguidos
      if(c == '.' && w == 0) continue;       // nunca un nombre oculto
      out[w++] = c;
      lastSep = sep;
    }
  }
  // Recorte por la derecha: separadores, puntos y guiones sueltos.
  while(w > 0 && (out[w - 1] == ' ' || out[w - 1] == '_' || out[w - 1] == '.' || out[w - 1] == '-')) w--;
  out[w] = 0;
  if(w == 0){ copyz(out, cap, "Archivo"); w = strlen(out); }
  return w;
}

size_t flexMlCleanName(const char* in, char* out, size_t cap){
  if(!out || cap == 0) return 0;
  size_t w = 0;
  const unsigned char* s = (const unsigned char*)(in ? in : "");
  while(*s){
    uint32_t cp;
    int k = utf8Next(s, &cp);
    // Bytes que no pertenecen a una secuencia valida -> '?'
    if(cp == 0xFFFD && k == 1 && s[0] >= 0x80){
      if(w + 1 >= cap) break;
      out[w++] = '?'; s++; continue;
    }
    if(cp < 0x20 || cp == 0x7F){ s += k; continue; }             // controles fuera
    if(cp == '/' || cp == '\\'){
      if(w + 1 >= cap) break;
      out[w++] = '_'; s += k; continue;
    }
    if(w + (size_t)k >= cap) break;                               // no se parte un caracter
    memcpy(out + w, s, (size_t)k);
    w += (size_t)k; s += k;
  }
  out[w] = 0;
  return w;
}

const char* flexMlDestDir(int kind){
  switch(kind){
    case FML_K_PHOTO: return FML_DIR_PHOTO;
    case FML_K_VIDEO: return FML_DIR_VIDEO;
    case FML_K_AUDIO: return FML_DIR_AUDIO;
  }
  return "";
}

bool flexMlPathSafe(const char* path){
  if(!path || path[0] != '/') return false;
  size_t n = strlen(path);
  if(n < 2 || n >= FML_PATH_MAX) return false;
  for(size_t i = 0; i < n; i++){
    unsigned char c = (unsigned char)path[i];
    if(c < 0x20 || c == 0x7F || c == '\\') return false;
    if(c == '/' && path[i + 1] == '/') return false;           // segmento vacio
  }
  if(path[n - 1] == '/') return false;
  const char* p = path;
  while(*p){
    while(*p == '/') p++;
    const char* s = p;
    while(*p && *p != '/') p++;
    size_t seg = (size_t)(p - s);
    if((seg == 1 && s[0] == '.') || (seg == 2 && s[0] == '.' && s[1] == '.')) return false;
  }
  return true;
}

bool flexMlUniquePath(const char* dir, const char* stem, const char* ext,
                      FlexMlExistsFn exists, void* ctx, char* out, size_t cap){
  if(!dir || !stem || !out || cap == 0) return false;
  if(!ext) ext = "";
  char base[FML_STEM_MAX + 1];
  copyz(base, sizeof(base), stem);
  for(int k = 1; k <= 99; k++){
    char suffix[8] = "";
    if(k > 1) snprintf(suffix, sizeof(suffix), " (%d)", k);
    // Si no cabe, se recorta la raiz (nunca la carpeta ni la extension).
    size_t fixed = strlen(dir) + 1 + strlen(suffix) + strlen(ext);
    if(fixed + 1 >= cap || fixed + 1 >= FML_PATH_MAX) return false;
    size_t room = cap - 1 - fixed;
    if(room > FML_PATH_MAX - 1 - fixed) room = FML_PATH_MAX - 1 - fixed;
    char b[FML_STEM_MAX + 1];
    copyz(b, sizeof(b), base);
    if(strlen(b) > room) b[room] = 0;
    size_t bl = strlen(b);
    while(bl > 0 && b[bl - 1] == ' ') b[--bl] = 0;
    if(bl == 0) return false;
    snprintf(out, cap, "%s/%s%s%s", dir, b, suffix, ext);
    if(!flexMlPathSafe(out)) return false;
    if(!exists || !exists(ctx, out)) return true;
  }
  return false;
}

void flexMlDisplayName(const FlexMlRec* r, char* out, size_t cap){
  if(!out || cap == 0) return;
  out[0] = 0;
  if(!r) return;
  if(r->kind == FML_K_AUDIO && r->title[0]){ copyz(out, cap, r->title); return; }
  if(r->name[0]){ copyz(out, cap, r->name); return; }
  const char* b = strrchr(r->path, '/');
  copyz(out, cap, b ? b + 1 : r->path);
}

// -------------------------------------------------------------
//  CATALOGO
// -------------------------------------------------------------
void flexMlInit(FlexMlLib* lib, FlexMlRec* store, uint16_t cap){
  if(!lib) return;
  lib->recs = store;
  lib->cap  = store ? cap : 0;
  lib->n = 0;
  lib->nextId = 1;
  lib->rev = 1;
}
void flexMlClear(FlexMlLib* lib){ if(lib){ lib->n = 0; lib->rev++; } }

int flexMlFindId(const FlexMlLib* lib, uint32_t id){
  if(!lib || !id) return -1;
  for(int i = 0; i < lib->n; i++) if(lib->recs[i].id == id) return i;
  return -1;
}
int flexMlFindPath(const FlexMlLib* lib, const char* path){
  if(!lib || !path) return -1;
  uint32_t h = flexMlHash(path);
  for(int i = 0; i < lib->n; i++)
    if(lib->recs[i].pathHash == h && !strcmp(lib->recs[i].path, path)) return i;
  return -1;
}
int flexMlFindDup(const FlexMlLib* lib, uint32_t size, uint32_t crc){
  if(!lib || !crc) return -1;
  for(int i = 0; i < lib->n; i++)
    if(lib->recs[i].size == size && lib->recs[i].crc == crc) return i;
  return -1;
}

int flexMlAdd(FlexMlLib* lib, const FlexMlRec* proto, uint32_t now){
  if(!lib || !proto || !lib->recs) return FML_ERR_ARG;
  if(proto->kind < FML_K_PHOTO || proto->kind > FML_K_DRAW) return FML_ERR_ARG;
  if(!terminated(proto->path, sizeof(proto->path)) || !flexMlPathSafe(proto->path)) return FML_ERR_ARG;
  if(lib->n >= lib->cap) return FML_ERR_FULL;
  if(flexMlFindPath(lib, proto->path) >= 0) return FML_ERR_EXISTS;
  FlexMlRec* r = &lib->recs[lib->n];
  *r = *proto;
  r->name[sizeof(r->name) - 1] = 0;
  r->title[sizeof(r->title) - 1] = 0;
  r->artist[sizeof(r->artist) - 1] = 0;
  r->album[sizeof(r->album) - 1] = 0;
  if(lib->nextId == 0) lib->nextId = 1;
  r->id = lib->nextId++;
  r->pathHash = flexMlHash(r->path);
  if(!r->added) r->added = now;
  if(r->fmt >= FML_F_COUNT) r->fmt = FML_F_UNKNOWN;
  r->flags &= (uint16_t)~FML_R_SEEN;
  lib->n++;
  lib->rev++;
  return lib->n - 1;
}

bool flexMlRemoveAt(FlexMlLib* lib, int idx){
  if(!lib || idx < 0 || idx >= lib->n) return false;
  if(idx < lib->n - 1)
    memmove(&lib->recs[idx], &lib->recs[idx + 1], sizeof(FlexMlRec) * (size_t)(lib->n - 1 - idx));
  lib->n--;
  lib->rev++;
  return true;
}

bool flexMlSetPath(FlexMlLib* lib, int idx, const char* path){
  if(!lib || idx < 0 || idx >= lib->n || !flexMlPathSafe(path)) return false;
  int other = flexMlFindPath(lib, path);
  if(other >= 0 && other != idx) return false;
  copyz(lib->recs[idx].path, sizeof(lib->recs[idx].path), path);
  lib->recs[idx].pathHash = flexMlHash(lib->recs[idx].path);
  lib->rev++;
  return true;
}

bool flexMlSetLocked(FlexMlLib* lib, int idx, bool on){
  if(!lib || idx < 0 || idx >= lib->n) return false;
  FlexMlRec* r = &lib->recs[idx];
  bool was = (r->flags & FML_R_LOCKED) != 0;
  if(was == on) return true;
  if(on) r->flags |= FML_R_LOCKED; else r->flags &= (uint16_t)~FML_R_LOCKED;
  r->thumbVer++;                           // ninguna cache sirve la version anterior
  lib->rev++;
  return true;
}

void flexMlTouch(FlexMlLib* lib){ if(lib) lib->rev++; }

void flexMlScanBegin(FlexMlLib* lib){
  if(!lib) return;
  for(int i = 0; i < lib->n; i++) lib->recs[i].flags &= (uint16_t)~FML_R_SEEN;
}

static int fmtFromExt(const char* path, int kind){
  const char* d = strrchr(path, '.');
  if(!d) return FML_F_UNKNOWN;
  if(eqNoCase(d, ".jpg") || eqNoCase(d, ".jpeg")) return FML_F_JPEG;
  if(eqNoCase(d, ".avi")) return FML_F_AVI_MJPEG;
  if(eqNoCase(d, ".wav")) return FML_F_WAV_PCM;
  if(eqNoCase(d, ".fxp")) return FML_F_FXP;
  if(eqNoCase(d, ".png")) return FML_F_PNG;
  if(eqNoCase(d, ".heic") || eqNoCase(d, ".heif")) return FML_F_HEIC;
  if(eqNoCase(d, ".webp")) return FML_F_WEBP;
  if(eqNoCase(d, ".gif")) return FML_F_GIF;
  if(eqNoCase(d, ".bmp")) return FML_F_BMP;
  if(eqNoCase(d, ".mp4") || eqNoCase(d, ".m4v")) return FML_F_MP4;
  if(eqNoCase(d, ".mov")) return FML_F_MOV;
  if(eqNoCase(d, ".webm")) return FML_F_WEBM;
  if(eqNoCase(d, ".mkv")) return FML_F_MKV;
  if(eqNoCase(d, ".mp3")) return FML_F_MP3;
  if(eqNoCase(d, ".m4a")) return FML_F_M4A;
  if(eqNoCase(d, ".aac")) return FML_F_AAC;
  if(eqNoCase(d, ".flac")) return FML_F_FLAC;
  if(eqNoCase(d, ".ogg") || eqNoCase(d, ".opus")) return FML_F_OGG;
  (void)kind;
  return FML_F_UNKNOWN;
}

int flexMlScanMerge(FlexMlLib* lib, const char* path, uint32_t size,
                    int kind, bool lockedDir, uint32_t now){
  if(!lib || !path) return FML_ERR_ARG;
  int i = flexMlFindPath(lib, path);
  if(i >= 0){
    FlexMlRec* r = &lib->recs[i];
    r->flags |= FML_R_SEEN;
    if(r->size != size){
      // El contenido cambio por debajo (un dibujo de Paint que crece, un
      // archivo sustituido): la miniatura y el CRC ya no valen.
      r->size = size;
      r->crc = 0;
      r->modified = now;
      if(!(r->flags & FML_R_EXT_THUMB)){
        r->flags &= (uint16_t)~(FML_R_THUMB | FML_R_THUMB_FAIL);
        r->flags |= FML_R_NEED_THUMB;
      }
      r->thumbVer++;
      lib->rev++;
    }
    return i;
  }
  if(kind < FML_K_PHOTO || kind > FML_K_DRAW) return FML_ERR_KIND;
  FlexMlRec p;
  memset(&p, 0, sizeof(p));
  copyz(p.path, sizeof(p.path), path);
  p.size = size;
  p.kind = (uint8_t)kind;
  p.fmt  = (uint8_t)fmtFromExt(path, kind);
  p.state = FML_S_READY;
  p.origin = FML_O_LOCAL;
  p.flags = FML_R_NEED_THUMB;
  if(flexMlFmtPlayable(p.fmt)) p.flags |= FML_R_PLAYABLE;
  else p.err = FML_E_UNSUPPORTED;
  if(lockedDir){
    // Recuperado de la carpeta protegida sin catalogo: nace bloqueado y
    // sin nombre (el nombre del disco es neutro a proposito).
    p.flags |= FML_R_LOCKED;
    copyz(p.name, sizeof(p.name), "Elemento protegido");
  } else {
    const char* b = strrchr(path, '/');
    flexMlCleanName(b ? b + 1 : path, p.name, sizeof(p.name));
  }
  int at = flexMlAdd(lib, &p, now);
  if(at >= 0) lib->recs[at].flags |= FML_R_SEEN;
  return at;
}

int flexMlNextUnseen(const FlexMlLib* lib, int from){
  if(!lib) return -1;
  if(from < 0) from = 0;
  for(int i = from; i < lib->n; i++) if(!(lib->recs[i].flags & FML_R_SEEN)) return i;
  return -1;
}

// -------------------------------------------------------------
//  VISTAS
// -------------------------------------------------------------
void flexMlViewInit(FlexMlView* v, uint16_t* store, uint16_t cap, uint32_t mask, int sort){
  if(!v) return;
  v->idx = store; v->cap = store ? cap : 0; v->n = 0; v->rev = 0; v->mask = mask; v->sort = sort;
}

static int cmpTitle(const FlexMlRec* a, const FlexMlRec* b){
  char x[FML_NAME_MAX], y[FML_NAME_MAX];
  flexMlDisplayName(a, x, sizeof(x));
  flexMlDisplayName(b, y, sizeof(y));
  for(size_t i = 0;; i++){
    char c = lower1(x[i]), d = lower1(y[i]);
    if(c != d) return (unsigned char)c < (unsigned char)d ? -1 : 1;
    if(!c) break;
  }
  return a->id < b->id ? -1 : (a->id > b->id ? 1 : 0);
}
static int cmpNewest(const FlexMlRec* a, const FlexMlRec* b){
  uint32_t ka = a->created ? a->created : a->added;
  uint32_t kb = b->created ? b->created : b->added;
  if(ka != kb) return ka > kb ? -1 : 1;
  return a->id > b->id ? -1 : (a->id < b->id ? 1 : 0);
}
static int viewCmp(const FlexMlLib* lib, int sort, uint16_t ia, uint16_t ib){
  const FlexMlRec* a = &lib->recs[ia];
  const FlexMlRec* b = &lib->recs[ib];
  if(sort == FML_SORT_TITLE)  return cmpTitle(a, b);
  if(sort == FML_SORT_OLDEST) return -cmpNewest(a, b);
  return cmpNewest(a, b);
}

bool flexMlViewSync(FlexMlView* v, const FlexMlLib* lib, bool force){
  if(!v || !lib) return false;
  if(!force && v->rev == lib->rev) return false;
  v->n = 0;
  for(int i = 0; i < lib->n && v->n < v->cap; i++)
    if(v->mask & FML_MASK(lib->recs[i].kind)) v->idx[v->n++] = (uint16_t)i;
  // Insercion: n <= 384 y solo corre cuando el catalogo cambia. No hay
  // qsort porque su comparador no puede llevar contexto sin una global,
  // y dos tareas ordenando a la vez se pisarian esa global.
  for(int i = 1; i < v->n; i++){
    uint16_t key = v->idx[i];
    int j = i - 1;
    while(j >= 0 && viewCmp(lib, v->sort, v->idx[j], key) > 0){ v->idx[j + 1] = v->idx[j]; j--; }
    v->idx[j + 1] = key;
  }
  v->rev = lib->rev;
  return true;
}

int flexMlViewFindId(const FlexMlView* v, const FlexMlLib* lib, uint32_t id){
  if(!v || !lib) return -1;
  for(int i = 0; i < v->n; i++) if(v->idx[i] < lib->n && lib->recs[v->idx[i]].id == id) return i;
  return -1;
}

// -------------------------------------------------------------
//  PERSISTENCIA
// -------------------------------------------------------------
size_t flexMlSerializedSize(const FlexMlLib* lib){
  return lib ? FML_HDR_BYTES + (size_t)lib->n * sizeof(FlexMlRec) : 0;
}

size_t flexMlSerialize(const FlexMlLib* lib, uint8_t* out, size_t cap){
  if(!lib || !out) return 0;
  size_t need = flexMlSerializedSize(lib);
  if(cap < need) return 0;
  memset(out, 0, FML_HDR_BYTES);
  memcpy(out, FML_FILE_MAGIC, 4);
  wr16le(out + 4, FML_FILE_VER);
  wr16le(out + 6, (uint16_t)sizeof(FlexMlRec));
  wr32le(out + 8, lib->n);
  wr32le(out + 12, lib->nextId);
  wr32le(out + 16, lib->rev);
  uint8_t* body = out + FML_HDR_BYTES;
  for(int i = 0; i < lib->n; i++){
    FlexMlRec r = lib->recs[i];
    r.flags &= (uint16_t)~FML_R_SEEN;      // lo transitorio no se guarda
    memcpy(body + (size_t)i * sizeof(FlexMlRec), &r, sizeof(FlexMlRec));
  }
  wr32le(out + 20, flexMlCrc32(0, body, (size_t)lib->n * sizeof(FlexMlRec)));
  return need;
}

static bool recValid(const FlexMlRec* r, uint32_t nextId){
  if(!r->id || r->id >= nextId) return false;
  if(r->kind < FML_K_PHOTO || r->kind > FML_K_DRAW) return false;
  if(r->fmt >= FML_F_COUNT || r->state > FML_S_ERROR) return false;
  if(!terminated(r->path, sizeof(r->path)) || !terminated(r->name, sizeof(r->name)) ||
     !terminated(r->title, sizeof(r->title)) || !terminated(r->artist, sizeof(r->artist)) ||
     !terminated(r->album, sizeof(r->album))) return false;
  if(!flexMlPathSafe(r->path)) return false;
  return true;
}

int flexMlDeserialize(FlexMlLib* lib, const uint8_t* in, size_t len){
  if(!lib || !in || !lib->recs) return FML_ERR_ARG;
  if(len < FML_HDR_BYTES || memcmp(in, FML_FILE_MAGIC, 4)) return FML_ERR_FORMAT;
  if(rd16le(in + 4) != FML_FILE_VER || rd16le(in + 6) != sizeof(FlexMlRec)) return FML_ERR_FORMAT;
  uint32_t n = rd32le(in + 8), nextId = rd32le(in + 12), rev = rd32le(in + 16), crc = rd32le(in + 20);
  if(n > lib->cap) return FML_ERR_FULL;
  if(len != FML_HDR_BYTES + (size_t)n * sizeof(FlexMlRec)) return FML_ERR_FORMAT;
  const uint8_t* body = in + FML_HDR_BYTES;
  if(flexMlCrc32(0, body, (size_t)n * sizeof(FlexMlRec)) != crc) return FML_ERR_CRC;
  if(nextId == 0) return FML_ERR_RECORD;
  // Todo o nada: se valida TODO antes de tocar el catalogo del llamante.
  for(uint32_t i = 0; i < n; i++){
    FlexMlRec r;
    memcpy(&r, body + (size_t)i * sizeof(FlexMlRec), sizeof(r));
    if(!recValid(&r, nextId)) return FML_ERR_RECORD;
    for(uint32_t j = 0; j < i; j++){
      FlexMlRec o;
      memcpy(&o, body + (size_t)j * sizeof(FlexMlRec), sizeof(o));
      if(o.id == r.id || !strcmp(o.path, r.path)) return FML_ERR_RECORD;
    }
  }
  for(uint32_t i = 0; i < n; i++){
    FlexMlRec* r = &lib->recs[i];
    memcpy(r, body + (size_t)i * sizeof(FlexMlRec), sizeof(*r));
    r->pathHash = flexMlHash(r->path);     // no se confia en el guardado
    r->flags &= (uint16_t)~FML_R_SEEN;
  }
  lib->n = (uint16_t)n;
  lib->nextId = nextId;
  lib->rev = rev + 1;                      // lo cargado cuenta como un cambio
  return FML_OK;
}

// -------------------------------------------------------------
//  JSON
// -------------------------------------------------------------
size_t flexMlJsonStr(const char* in, char* out, size_t cap){
  if(!out || cap < 3) return 0;
  size_t w = 0;
  out[w++] = '"';
  const unsigned char* s = (const unsigned char*)(in ? in : "");
  while(*s){
    uint32_t cp;
    int k = utf8Next(s, &cp);
    char esc[8];
    const char* rep = NULL;
    size_t rl = 0;
    if(cp == 0xFFFD && k == 1 && s[0] >= 0x80){ rep = "?"; rl = 1; }
    else if(cp == '"'){ rep = "\\\""; rl = 2; }
    else if(cp == '\\'){ rep = "\\\\"; rl = 2; }
    else if(cp < 0x20 || cp == 0x7F || cp == '<' || cp == '>' || cp == '&'){
      snprintf(esc, sizeof(esc), "\\u%04x", (unsigned)cp); rep = esc; rl = 6;
    }
    if(rep){
      if(w + rl + 2 > cap) return 0;
      memcpy(out + w, rep, rl); w += rl; s += k;
      continue;
    }
    if(w + (size_t)k + 2 > cap) return 0;
    memcpy(out + w, s, (size_t)k); w += (size_t)k; s += k;
  }
  if(w + 2 > cap) return 0;
  out[w++] = '"';
  out[w] = 0;
  return w;
}

static const char* stateName(int st){
  return st == FML_S_PROCESSING ? "proc" : st == FML_S_ERROR ? "err" : "ready";
}

// Anade texto a out[w..] sin salirse. Devuelve false si no cabe.
static bool put(char* out, size_t cap, size_t* w, const char* s){
  size_t n = strlen(s);
  if(*w + n + 1 > cap) return false;
  memcpy(out + *w, s, n); *w += n; out[*w] = 0;
  return true;
}
static bool putStrField(char* out, size_t cap, size_t* w, const char* key, const char* val){
  if(!put(out, cap, w, key)) return false;
  size_t n = flexMlJsonStr(val, out + *w, cap - *w);
  if(!n) return false;
  *w += n;
  return true;
}

size_t flexMlJsonItem(const FlexMlRec* r, bool owner, char* out, size_t cap){
  if(!r || !out || cap < 16) return 0;
  size_t w = 0;
  char num[224];
  bool locked = (r->flags & FML_R_LOCKED) != 0;
  if(locked && !owner){
    // Lo UNICO que sabe de un protegido quien no se ha autenticado: que
    // existe. Ni que clase de medio es.
    int n = snprintf(out, cap, "{\"id\":%lu,\"lock\":1}", (unsigned long)r->id);
    return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
  }
  snprintf(num, sizeof(num), "{\"id\":%lu,\"k\":\"%s\",\"s\":%lu,\"c\":%lu,\"a\":%lu,\"m\":%lu,"
           "\"w\":%u,\"h\":%u,\"d\":%lu,\"st\":\"%s\",\"e\":%u,\"p\":%u,\"lock\":%u",
           (unsigned long)r->id, flexMlKindName(r->kind), (unsigned long)r->size,
           (unsigned long)r->created, (unsigned long)r->added, (unsigned long)r->modified,
           (unsigned)r->w, (unsigned)r->h, (unsigned long)r->durMs, stateName(r->state),
           (unsigned)r->err, (r->flags & FML_R_PLAYABLE) ? 1u : 0u, locked ? 1u : 0u);
  if(!put(out, cap, &w, num)) return 0;
  if(r->flags & FML_R_THUMB){
    snprintf(num, sizeof(num), ",\"t\":%u", (unsigned)r->thumbVer);
    if(!put(out, cap, &w, num)) return 0;
  }
  if(!putStrField(out, cap, &w, ",\"fmt\":", flexMlFmtName(r->fmt))) return 0;
  char dn[FML_NAME_MAX];
  flexMlDisplayName(r, dn, sizeof(dn));
  if(!putStrField(out, cap, &w, ",\"n\":", dn)) return 0;
  if(!putStrField(out, cap, &w, ",\"fn\":", r->name)) return 0;
  if(r->kind == FML_K_AUDIO){
    if(!putStrField(out, cap, &w, ",\"ar\":", r->artist)) return 0;
    if(!putStrField(out, cap, &w, ",\"al\":", r->album)) return 0;
  }
  if(!put(out, cap, &w, "}")) return 0;
  return w;
}

bool flexMlJsonLibrary(const FlexMlLib* lib, bool owner, uint32_t mask,
                       const char* head, FlexMlSink sink, void* ctx){
  if(!lib || !sink) return false;
  char buf[1024];
  if(!sink(ctx, "{", 1)) return false;
  if(head && head[0]){
    if(!sink(ctx, head, strlen(head))) return false;
    if(!sink(ctx, ",", 1)) return false;
  }
  if(!sink(ctx, "\"items\":[", 9)) return false;
  bool first = true;
  for(int i = 0; i < lib->n; i++){
    const FlexMlRec* r = &lib->recs[i];
    if(!(mask & FML_MASK(r->kind))) continue;
    size_t n = flexMlJsonItem(r, owner, buf, sizeof(buf));
    if(!n) n = (size_t)snprintf(buf, sizeof(buf), "{\"id\":%lu}", (unsigned long)r->id);
    if(!first && !sink(ctx, ",", 1)) return false;
    if(!sink(ctx, buf, n)) return false;
    first = false;
  }
  return sink(ctx, "]}", 2);
}

// -------------------------------------------------------------
//  SUBIDAS
// -------------------------------------------------------------
uint32_t flexMlLimitFor(int kind){
  switch(kind){
    case FML_K_PHOTO: return FML_LIMIT_PHOTO;
    case FML_K_VIDEO: return FML_LIMIT_VIDEO;
    case FML_K_AUDIO: return FML_LIMIT_AUDIO;
  }
  return 0;
}

// "1,5 MB" / "820 KB": las cifras que ve el usuario, con coma decimal.
static void fmtSize(uint32_t b, char* out, size_t n){
  if(b >= 1024u * 1024u){
    uint32_t t = (uint32_t)(((uint64_t)b * 10u + 524288u) / 1048576u);
    snprintf(out, n, "%lu,%lu MB", (unsigned long)(t / 10u), (unsigned long)(t % 10u));
  } else {
    snprintf(out, n, "%lu KB", (unsigned long)((b + 1023u) / 1024u));
  }
}

int flexMlCheckUpload(int kind, uint32_t size, uint32_t freeBytes, char* why, size_t whyCap){
  char a[24], b[24];
  if(why && whyCap) why[0] = 0;
  if(kind != FML_K_PHOTO && kind != FML_K_VIDEO && kind != FML_K_AUDIO){
    if(why) snprintf(why, whyCap, "Tipo de archivo no admitido");
    return FML_ERR_KIND;
  }
  uint32_t lim = flexMlLimitFor(kind);
  if(size == 0){
    if(why) snprintf(why, whyCap, "El archivo est\xC3\xA1 vac\xC3\xADo");
    return FML_ERR_ARG;
  }
  if(size > lim){
    fmtSize(size, a, sizeof(a)); fmtSize(lim, b, sizeof(b));
    if(why) snprintf(why, whyCap, "Pesa %s y el l\xC3\xADmite para %s es %s", a,
                     kind == FML_K_PHOTO ? "fotos" : kind == FML_K_VIDEO ? "v\xC3\xAD" "deos" : "audio", b);
    return FML_ERR_TOOBIG;
  }
  // LittleFS guarda por bloques de 4 KB y apunta metadatos: se pide un
  // 3 % y 16 KB de margen, mas la reserva que nunca se toca.
  uint64_t need = (uint64_t)size + size / 32u + 16u * 1024u + FML_RESERVE_BYTES;
  if(need > freeBytes){
    fmtSize(size, a, sizeof(a));
    uint32_t usable = freeBytes > FML_RESERVE_BYTES ? freeBytes - FML_RESERVE_BYTES : 0;
    fmtSize(usable, b, sizeof(b));
    if(why) snprintf(why, whyCap, "Sin espacio: hacen falta %s y quedan %s libres", a, b);
    return FML_ERR_NOSPACE;
  }
  return FML_OK;
}
