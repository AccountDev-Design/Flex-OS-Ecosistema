// #############################################################
// ##  FlexOS · BIBLIOTECA DE MEDIOS  ·  el catalogo compartido
// ##  Portable: compila igual en el P4 y en un PC.
// #############################################################
//
//  QUE ES
//  ------
//  La UNICA fuente de datos de medios de Flex OS. Galeria, Multimedia,
//  Musica y Flex Web Server leen de aqui; ninguna de ellas tiene su
//  propia lista. Un elemento tiene un `media_id` que no cambia nunca
//  (ni al renombrarlo, ni al bloquearlo, ni al editarlo), y todo lo que
//  se sabe de el vive en su registro:
//
//    nombre original · ruta interna segura · tipo · formato REAL
//    detectado por firma · tamano · fechas · dimensiones · duracion ·
//    bloqueo · miniatura · estado de procesamiento · error
//
//  POR QUE ES PORTABLE
//  -------------------
//  Porque decide cosas que no pueden quedarse en "parece que funciona":
//  que formato es de verdad un fichero que llega por la red, que nombre
//  se le da en disco, si un catalogo leido de la flash esta sano, y que
//  se le ensena de un elemento bloqueado a quien no se ha autenticado.
//  Todo eso se ejercita en el PC con sanitizers
//  (tests/host/test_medialib.cpp), igual que FlexOS_Media o FlexOS_Mem.
//
//  QUE NO HACE
//  -----------
//  No toca LittleFS, ni la pantalla, ni la red, ni reserva memoria: el
//  almacen de registros lo pone quien llama (en PSRAM, en la placa). El
//  sketch (FlexOS_Ultra_MediaLib.h) lo carga, lo guarda y lo reconcilia
//  con lo que hay de verdad en el sistema de archivos.

#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// -------------------------------------------------------------
//  Limites. Fijos y con motivo.
// -------------------------------------------------------------
#define FML_CAP            384      // el mismo tope que el indice por escaneo
#define FML_PATH_MAX       112      // = FLEXMED_PATH_MAX
#define FML_NAME_MAX        64      // nombre original, UTF-8 (recortado sin partir caracteres)
#define FML_TITLE_MAX       48
#define FML_ARTIST_MAX      32
#define FML_ALBUM_MAX       32
#define FML_STEM_MAX        28      // raiz del nombre EN DISCO (LittleFS de Flex OS: 48 por nombre)

// Presupuestos por defecto de una subida. Configurables: son defines y
// no constantes enterradas en el codigo del servidor.
#define FML_LIMIT_PHOTO   (6u * 1024u * 1024u)    // el mismo tope que el visor
#define FML_LIMIT_VIDEO   (8u * 1024u * 1024u)
#define FML_LIMIT_AUDIO   (8u * 1024u * 1024u)
#define FML_LIMIT_THUMB   (48u * 1024u)           // miniatura que aporta el navegador
#define FML_RESERVE_BYTES (512u * 1024u)          // lo que nunca se consume de la particion

// Carpetas de destino. /Imagenes es la misma que ya lee el selector de
// fondos: una foto que llega del movil puede ser fondo sin moverla.
#define FML_DIR_PHOTO   "/Imagenes"
#define FML_DIR_VIDEO   "/Videos"
#define FML_DIR_AUDIO   "/Musica"
#define FML_DIR_ROOT    "/System/Media"
#define FML_DIR_THUMB   "/System/Media/th"
#define FML_DIR_TMP     "/System/Media/tmp"
// Defensa en profundidad del bloqueo: un elemento bloqueado se MUEVE aqui
// con un nombre neutro. El candado lo decide el catalogo; esta carpeta
// garantiza que, aunque el catalogo se perdiera, lo que hay dentro se
// vuelve a registrar BLOQUEADO y con su nombre original oculto.
#define FML_DIR_LOCKED  "/System/Media/Protegido"
#define FML_LIB_PATH    "/System/Media/library.fml"

// -------------------------------------------------------------
//  Clase de medio (a que app va)
// -------------------------------------------------------------
enum {
  FML_K_NONE  = 0,
  FML_K_PHOTO = 1,     // Galeria + Multimedia
  FML_K_VIDEO = 2,     // Galeria + Multimedia
  FML_K_AUDIO = 3,     // Musica
  FML_K_DRAW  = 4      // dibujo de Paint (.fxp): Galeria + Multimedia
};
#define FML_MASK(k)       (1u << (k))
#define FML_MASK_VISUAL   (FML_MASK(FML_K_PHOTO) | FML_MASK(FML_K_VIDEO) | FML_MASK(FML_K_DRAW))
#define FML_MASK_AUDIO    (FML_MASK(FML_K_AUDIO))
#define FML_MASK_ALL      (FML_MASK_VISUAL | FML_MASK_AUDIO)

// -------------------------------------------------------------
//  Formato REAL, detectado por la firma de los bytes. La extension
//  solo se usa como pista cuando no hay bytes que mirar.
// -------------------------------------------------------------
enum {
  FML_F_UNKNOWN = 0,
  FML_F_JPEG,          // baseline o secuencial extendido (el P4 lo abre)
  FML_F_JPEG_PROG,     // progresivo: el P4 NO lo decodifica
  FML_F_PNG, FML_F_GIF, FML_F_BMP, FML_F_WEBP, FML_F_HEIC, FML_F_AVIF,
  FML_F_AVI_MJPEG,     // el unico video que el P4 reproduce
  FML_F_AVI_OTHER,
  FML_F_MP4, FML_F_MOV, FML_F_WEBM, FML_F_MKV,
  FML_F_WAV_PCM,       // PCM entero 8/16 bits
  FML_F_WAV_ADPCM,     // IMA ADPCM de 4 bits (lo convierte la web; el P4 lo reproduce)
  FML_F_WAV_OTHER,
  FML_F_MP3, FML_F_AAC, FML_F_M4A, FML_F_FLAC, FML_F_OGG,
  FML_F_FXP,           // dibujo de Paint
  FML_F_COUNT
};

// -------------------------------------------------------------
//  Estado de un elemento y codigo de error
// -------------------------------------------------------------
enum { FML_S_READY = 0, FML_S_PROCESSING = 1, FML_S_ERROR = 2 };
enum {
  FML_E_NONE = 0,
  FML_E_UNSUPPORTED,   // guardado, pero el P4 no lo abre (MP4, HEIC, MP3...)
  FML_E_CORRUPT,       // la firma dice una cosa y el contenido no se puede leer
  FML_E_THUMB,         // no se pudo generar la miniatura
  FML_E_MISSING        // el archivo ya no esta en el disco
};

// Origen del elemento.
enum { FML_O_LOCAL = 0, FML_O_WEB = 1, FML_O_EDIT = 2 };

// Banderas del registro.
#define FML_R_LOCKED      0x0001u   // protegido con la seguridad del sistema
#define FML_R_PLAYABLE    0x0002u   // el P4 lo puede abrir / reproducir
#define FML_R_THUMB       0x0004u   // hay miniatura persistente valida
#define FML_R_THUMB_FAIL  0x0008u   // se intento y no se pudo: no se reintenta en bucle
#define FML_R_NEED_THUMB  0x0010u   // hay que (re)generarla
#define FML_R_EDITED      0x0020u   // sale del editor
#define FML_R_EXT_THUMB   0x0040u   // la miniatura la aporto el navegador (formato no decodificable)
#define FML_R_SEEN        0x0080u   // interno: visto en el recorrido en curso

// -------------------------------------------------------------
//  EL REGISTRO. POD de tamano fijo: se guarda tal cual en la flash
//  (P4 y PC son little-endian, y la prueba de host lo comprueba).
// -------------------------------------------------------------
typedef struct {
  uint32_t id;          // media_id: unico, creciente, nunca se reutiliza
  uint32_t size;        // bytes del archivo
  uint32_t crc;         // CRC-32 del contenido (0 = no se sabe)
  uint32_t created;     // epoca UTC, s: fecha del original (0 = no se sabe)
  uint32_t added;       // epoca UTC, s: cuando entro en la biblioteca
  uint32_t modified;    // epoca UTC, s: ultimo cambio (edicion)
  uint32_t durMs;       // audio y video
  uint32_t pathHash;    // FNV-1a de path: busqueda sin strcmp de 112 bytes
  uint32_t parent;      // id del que sale (copia editada) o 0
  uint16_t w, h;        // pixeles (foto, video, dibujo)
  uint16_t flags;       // FML_R_*
  uint8_t  kind;        // FML_K_*
  uint8_t  fmt;         // FML_F_*
  uint8_t  state;       // FML_S_*
  uint8_t  err;         // FML_E_*
  uint8_t  thumbVer;    // sube cada vez que cambia la miniatura (cache de la web)
  uint8_t  origin;      // FML_O_*
  char     path[FML_PATH_MAX];     // ruta interna (ASCII seguro)
  char     name[FML_NAME_MAX];     // nombre original tal como llego (UTF-8)
  char     title[FML_TITLE_MAX];   // audio: titulo de las etiquetas
  char     artist[FML_ARTIST_MAX];
  char     album[FML_ALBUM_MAX];
} FlexMlRec;

typedef struct {
  FlexMlRec* recs;      // almacen del llamante
  uint16_t   cap;
  uint16_t   n;
  uint32_t   nextId;    // el proximo id que se dara
  uint32_t   rev;       // sube en CADA cambio: la UI y la web repintan por esto
} FlexMlLib;

// Resultados.
enum {
  FML_OK = 0,
  FML_ERR_ARG     = -1,
  FML_ERR_FULL    = -2,   // no caben mas elementos
  FML_ERR_EXISTS  = -3,   // esa ruta ya esta en el catalogo
  FML_ERR_FORMAT  = -4,   // fichero de catalogo con otro formato o version
  FML_ERR_CRC     = -5,   // fichero de catalogo danado
  FML_ERR_RECORD  = -6,   // un registro no pasa la validacion
  FML_ERR_TOOBIG  = -7,
  FML_ERR_NOSPACE = -8,
  FML_ERR_KIND    = -9
};

// -------------------------------------------------------------
//  CRC-32 (IEEE 802.3, el de zlib y el de ZIP). Semantica de zlib:
//  se empieza con 0 y se encadena: crc = flexMlCrc32(crc, buf, n).
// -------------------------------------------------------------
uint32_t flexMlCrc32(uint32_t crc, const void* data, size_t n);
// FNV-1a de 32 bits (nunca devuelve 0).
uint32_t flexMlHash(const char* s);

// -------------------------------------------------------------
//  FORMATOS
// -------------------------------------------------------------
// Detecta el formato real mirando los primeros bytes (con 512 basta para
// todo lo de aqui; con menos se hace lo que se puede). `kindOut` recibe
// la clase (FML_K_*). Devuelve FML_F_UNKNOWN si no reconoce la firma.
int         flexMlSniff(const uint8_t* head, size_t n, int* kindOut);
// Clase por extension: SOLO como pista (listados del disco sin abrir).
int         flexMlKindFromExt(const char* name);
int         flexMlKindOfFmt(int fmt);
// true si el P4 sabe abrir/reproducir ese formato.
bool        flexMlFmtPlayable(int fmt);
const char* flexMlFmtName(int fmt);       // "JPEG", "HEIC"...
const char* flexMlFmtExt(int fmt);        // ".jpg", ".heic"... ("" si no hay)
const char* flexMlKindName(int kind);     // "photo", "video", "audio", "draw"
int         flexMlKindParse(const char* s);
// Motivo legible (espanol) de por que el P4 no abre ese formato, o NULL.
const char* flexMlWhyUnplayable(int fmt);

// -------------------------------------------------------------
//  NOMBRES Y RUTAS
// -------------------------------------------------------------
// Raiz segura para el disco a partir de un nombre que viene de fuera
// (UTF-8, cualquier cosa): translitera las letras latinas acentuadas,
// deja [A-Za-z0-9 ._()-], junta espacios, quita la extension y recorta a
// FML_STEM_MAX. Nunca devuelve vacio ("Archivo"), nunca un nombre que
// empiece por '.' y nunca "." o "..".
size_t      flexMlSafeStem(const char* in, char* out, size_t cap);
// Copia un nombre para MOSTRARLO: UTF-8 valido, sin controles, recortado
// sin partir un caracter. Los bytes invalidos se sustituyen por '?'.
size_t      flexMlCleanName(const char* in, char* out, size_t cap);
// Carpeta de destino de una clase ("" si no tiene).
const char* flexMlDestDir(int kind);
// true si la ruta es absoluta, sin "..", sin '\\' ni controles y cabe.
bool        flexMlPathSafe(const char* path);
// "<dir>/<stem><ext>", y si ya existe "<dir>/<stem> (2)<ext>"... hasta 99.
// `exists` pregunta al sistema de archivos del llamante.
typedef bool (*FlexMlExistsFn)(void* ctx, const char* path);
bool        flexMlUniquePath(const char* dir, const char* stem, const char* ext,
                             FlexMlExistsFn exists, void* ctx, char* out, size_t cap);
// Nombre para ensenar: titulo (audio), o nombre original, o el de la ruta.
void        flexMlDisplayName(const FlexMlRec* r, char* out, size_t cap);

// -------------------------------------------------------------
//  CATALOGO
// -------------------------------------------------------------
void flexMlInit(FlexMlLib* lib, FlexMlRec* store, uint16_t cap);
void flexMlClear(FlexMlLib* lib);
int  flexMlFindId(const FlexMlLib* lib, uint32_t id);
int  flexMlFindPath(const FlexMlLib* lib, const char* path);
// Duplicado exacto: mismo tamano y mismo CRC (con CRC conocido).
int  flexMlFindDup(const FlexMlLib* lib, uint32_t size, uint32_t crc);
// Anade un registro. Se le asigna id, pathHash y added (si venia a 0).
// Devuelve el indice o FML_ERR_*.
int  flexMlAdd(FlexMlLib* lib, const FlexMlRec* proto, uint32_t now);
bool flexMlRemoveAt(FlexMlLib* lib, int idx);
// Cambia la ruta (renombrar, mover a Protegido y de vuelta).
bool flexMlSetPath(FlexMlLib* lib, int idx, const char* path);
// Pone/quita el candado. Al PONERLO se descarta la miniatura persistente
// (el llamante borra el archivo) y se sube thumbVer: ninguna cache puede
// seguir sirviendo la imagen vieja con la misma version.
bool flexMlSetLocked(FlexMlLib* lib, int idx, bool on);
// Marca un cambio que la UI y la web tienen que ver.
void flexMlTouch(FlexMlLib* lib);

// ---- Reconciliacion con el disco -----------------------------
void flexMlScanBegin(FlexMlLib* lib);
// Un archivo que el recorrido encontro. Si ya estaba, se marca visto y se
// actualiza su tamano (un tamano distinto = contenido cambiado: miniatura
// de nuevo). Si no estaba, se registra. `lockedDir` = viene de
// FML_DIR_LOCKED, y entonces nace BLOQUEADO. Devuelve el indice o <0.
int  flexMlScanMerge(FlexMlLib* lib, const char* path, uint32_t size,
                     int kind, bool lockedDir, uint32_t now);
// Siguiente registro NO visto desde `from` (los candidatos a comprobar si
// su archivo sigue existiendo). -1 si no hay mas.
int  flexMlNextUnseen(const FlexMlLib* lib, int from);

// -------------------------------------------------------------
//  VISTAS: la lista que pinta cada app, ya filtrada y ordenada.
// -------------------------------------------------------------
enum { FML_SORT_NEWEST = 0, FML_SORT_TITLE = 1, FML_SORT_OLDEST = 2 };
typedef struct {
  uint16_t* idx;        // indices dentro de lib->recs
  uint16_t  cap, n;
  uint32_t  rev;        // rev del catalogo con la que se construyo
  uint32_t  mask;       // FML_MASK(...)
  int       sort;
} FlexMlView;
void flexMlViewInit(FlexMlView* v, uint16_t* store, uint16_t cap, uint32_t mask, int sort);
// Reconstruye si el catalogo cambio (o si force). Devuelve true si cambio.
bool flexMlViewSync(FlexMlView* v, const FlexMlLib* lib, bool force);
int  flexMlViewFindId(const FlexMlView* v, const FlexMlLib* lib, uint32_t id);

// -------------------------------------------------------------
//  PERSISTENCIA
//  Cabecera de 32 bytes + n registros. CRC-32 de los registros en la
//  cabecera. Un solo registro que no valide invalida el fichero entero:
//  el llamante cae a la generacion anterior, nunca a medio catalogo.
// -------------------------------------------------------------
#define FML_FILE_MAGIC   "FML1"
#define FML_FILE_VER     1
#define FML_HDR_BYTES    32
size_t flexMlSerializedSize(const FlexMlLib* lib);
size_t flexMlSerialize(const FlexMlLib* lib, uint8_t* out, size_t cap);
int    flexMlDeserialize(FlexMlLib* lib, const uint8_t* in, size_t len);

// -------------------------------------------------------------
//  JSON PARA LA WEB
//  Se escribe por trozos a traves de `sink`. Un elemento bloqueado sin
//  sesion de propietario sale SOLO como {"id":N,"lock":1}: ni tipo, ni
//  nombre, ni tamano, ni fecha, ni miniatura.
// -------------------------------------------------------------
typedef bool (*FlexMlSink)(void* ctx, const char* s, size_t n);
// Escapa una cadena UTF-8 para JSON (con comillas). Devuelve bytes
// escritos o 0 si no cabe.
size_t flexMlJsonStr(const char* in, char* out, size_t cap);
// Un elemento. Devuelve bytes o 0 si no cabe.
size_t flexMlJsonItem(const FlexMlRec* r, bool owner, char* out, size_t cap);
// La biblioteca entera (solo las clases de `mask`), dentro de un objeto
// con los datos de `head` (texto JSON ya formado, sin llaves, o NULL).
bool   flexMlJsonLibrary(const FlexMlLib* lib, bool owner, uint32_t mask,
                         const char* head, FlexMlSink sink, void* ctx);

// -------------------------------------------------------------
//  SUBIDAS: la decision previa, antes de aceptar un solo byte.
// -------------------------------------------------------------
uint32_t flexMlLimitFor(int kind);
// FML_OK, FML_ERR_KIND, FML_ERR_TOOBIG o FML_ERR_NOSPACE. `why` recibe el
// motivo en espanol para ensenarlo tal cual.
int      flexMlCheckUpload(int kind, uint32_t size, uint32_t freeBytes,
                           char* why, size_t whyCap);

#ifdef __cplusplus
}
#endif
