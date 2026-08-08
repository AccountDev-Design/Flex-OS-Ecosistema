// #############################################################
// ##  FlexOS · CAPA REAL DE SISTEMA DE ARCHIVOS (LittleFS)
// ##  Comun a las tres placas: ESP32-P4 (Ultra), ESP32-S3
// ##  (Ultra S3) y ESP32 clasico (Pro).
// #############################################################
//
//  POR QUE EXISTE ESTE ARCHIVO
//  ---------------------------
//  Hasta ahora el proyecto NO tenia sistema de archivos: ni una
//  sola llamada a LittleFS/SPIFFS. Todo lo persistente vivia en
//  NVS (Preferences), que sirve para ajustes pequenos pero no
//  para documentos del usuario (dibujos, notas) ni para poder
//  contar carpetas, medir tamanos o tener una papelera de verdad.
//
//  Aqui vive TODA la logica de archivos, en una unidad de
//  traduccion aparte y SIN nada de interfaz, por el mismo motivo
//  que FlexOS_OTA.cpp: los .ino son enormes y casi identicos
//  entre placas, asi que duplicar la logica de ficheros tres
//  veces seria garantia de que se desincronicen. Los .ino solo
//  dibujan; los bytes los mueve este modulo.
//
//  REGLA DE ORO DEL MODULO: aqui no hay ni un solo dato
//  inventado. Cada tamano, cada porcentaje y cada contador sale
//  de recorrer el sistema de archivos real. Si algo no se puede
//  saber, la funcion lo dice devolviendo false o 0, nunca un
//  valor "bonito" de relleno.
//
//  PARTICION QUE HACE FALTA
//  ------------------------
//  LittleFS se monta sobre la particion de datos etiquetada
//  "spiffs" (asi la llama el core de arduino-esp32 aunque el
//  formato sea LittleFS). Los tres sketches ya piden un esquema
//  de particion que la incluye:
//    · Ultra (P4) y Ultra S3 -> cualquier esquema con SPIFFS
//      (16 MB / 8 MB: quedan varios MB para el usuario).
//    · Pro (ESP32 clasico)   -> "Minimal SPIFFS (1.9MB APP with
//      OTA/190KB SPIFFS)" si compilas con OTA. Son 190 KB
//      reales: caben notas y dibujos por trazos de sobra, pero
//      NO mapas de bits. Ver el comentario de formato mas abajo.
//  Si la particion no existe, flexFsBegin() devuelve false y la
//  interfaz debe decir "sin almacenamiento", no fingir datos.

#pragma once
#include <Arduino.h>

// -------------------------------------------------------------
//  Limites. Deliberadamente cortos: todo va en buffers de pila o
//  estaticos, sin String dinamico, porque estas funciones las
//  llama la interfaz dentro del bucle de dibujo y una
//  fragmentacion del heap ahi se ve como un tiron en pantalla.
// -------------------------------------------------------------
#define FLEXFS_PATH_MAX   96      // ruta absoluta completa ("/Paint/Dibujo 1.fxp")
#define FLEXFS_NAME_MAX   48      // nombre suelto, sin carpeta

// Carpetas reales que crea el sistema en el primer arranque.
#define FLEXFS_DIR_PAINT  "/Paint"
#define FLEXFS_DIR_NOTAS  "/Notas"
#define FLEXFS_DIR_SYS    "/System"
#define FLEXFS_DIR_DOCS   "/Documentos"
#define FLEXFS_DIR_TRASH  "/Papelera"

// Extensiones propias.
#define FLEXFS_EXT_PAINT  ".fxp"  // dibujo de Paint (trazos serializados)
#define FLEXFS_EXT_NOTE   ".txt"  // nota de texto plano UTF-8

// Categorias de la pantalla de Almacenamiento. El reparto NO es
// decorativo: cada categoria es un conjunto concreto de carpetas
// reales, y su tamano se obtiene sumando los ficheros que hay
// dentro. Ver flexFsCatSize().
enum {
  FLEXFS_CAT_DOCS = 0,   // /Documentos + /Notas   (lo que escribe el usuario)
  FLEXFS_CAT_SYS,        // /System                (lo que escribe el sistema)
  FLEXFS_CAT_APPS,       // /Paint  + cualquier carpeta de datos de app
  FLEXFS_CAT_TRASH,      // /Papelera
  FLEXFS_CAT_N
};

// Una entrada de directorio ya leida. Se copia a un array del
// llamante: nunca se devuelve un puntero a estado interno, para
// que la interfaz pueda quedarse la lista mientras dibuja.
struct FlexFsEntry {
  char     name[FLEXFS_NAME_MAX];
  uint32_t size;      // bytes reales del fichero; para carpetas, suma recursiva
  bool     dir;
  uint16_t items;     // solo carpetas: numero real de elementos que contiene
};

// Un fichero grande localizado por flexFsLargest(): necesita la
// ruta completa porque puede estar en cualquier carpeta.
struct FlexFsBig {
  char     path[FLEXFS_PATH_MAX];
  uint32_t size;
};

// -------------------------------------------------------------
//  Montaje y estado
// -------------------------------------------------------------
// Monta LittleFS (formateando SOLO si el montaje falla porque la
// particion esta virgen) y crea las carpetas del sistema si no
// existen. Devuelve false si no hay particion o no se pudo
// montar: en ese caso TODAS las demas funciones devuelven cero /
// false, nunca datos inventados.
bool     flexFsBegin();
bool     flexFsReady();
// Motivo legible del ultimo fallo, para poder ensenarlo en la
// pantalla de Almacenamiento en vez de un numero vacio.
const char* flexFsError();

// -------------------------------------------------------------
//  Consulta
// -------------------------------------------------------------
// Lista un directorio. Ordena carpetas primero y luego por
// nombre (orden estable para que la rejilla no baile entre
// repintados). Devuelve cuantas entradas ha escrito en `out`.
int      flexFsList(const char* dir, FlexFsEntry* out, int maxn);

// Numero REAL de elementos directos de un directorio (recorre el
// directorio y cuenta; no hay ningun contador cacheado que se
// pueda desincronizar).
int      flexFsCount(const char* dir);

// Suma recursiva de los bytes de un directorio.
uint32_t flexFsDirSize(const char* dir);

// Tamano de la particion y bytes ocupados, tal cual los reporta
// LittleFS.
uint32_t flexFsTotalBytes();
uint32_t flexFsUsedBytes();

bool     flexFsExists(const char* path);
bool     flexFsIsDir(const char* path);
uint32_t flexFsSize(const char* path);

// Bytes reales de una categoria de la pantalla de Almacenamiento.
uint32_t flexFsCatSize(int cat);

// Los `maxn` ficheros mas grandes del sistema de archivos entero,
// ordenados de mayor a menor. Recorre TODAS las carpetas.
int      flexFsLargest(FlexFsBig* out, int maxn);

// -------------------------------------------------------------
//  Operaciones (todas tocan el fichero real)
// -------------------------------------------------------------
bool     flexFsMkdir(const char* path);

// Borrado definitivo. Si `path` es una carpeta, se vacia primero
// (recursivo). No pasa por la papelera: es el "borrar de verdad".
bool     flexFsDelete(const char* path);

// Renombra dentro de la misma carpeta. `newName` es solo el
// nombre, sin ruta. Conserva la extension si `newName` no trae
// una (para que renombrar un dibujo no lo deje ilegible).
bool     flexFsRename(const char* path, const char* newName);

// Mueve a /Papelera. El nombre original con su ruta se codifica
// en el nombre del fichero de la papelera sustituyendo '/' por
// '@' ("/Paint/Dibujo 1.fxp" -> "/Papelera/@Paint@Dibujo 1.fxp"),
// de modo que la restauracion es exacta y no hace falta un
// fichero de indice aparte que se pueda quedar desincronizado.
bool     flexFsTrash(const char* path);

// Devuelve en `out` la ruta original legible de un elemento de la
// papelera (deshace la codificacion de arriba).
bool     flexFsTrashOrigin(const char* trashName, char* out, size_t n);

// Restaura un elemento de la papelera a su ruta original. Si ya
// existe algo con ese nombre, restaura con un sufijo libre.
bool     flexFsRestore(const char* trashName);

// Vacia la papelera del todo (borrado definitivo).
bool     flexFsEmptyTrash();

// -------------------------------------------------------------
//  Nombres
// -------------------------------------------------------------
// Compone "<dir>/<base> <n><ext>" con el primer `n` LIBRE de
// verdad (mira el directorio real). Es lo que hace que los
// titulos "Dibujo N" / "Sin titulo N" no esten inventados: N es
// el primer hueco disponible en la carpeta, no un contador en
// RAM que se reinicia al apagar.
bool     flexFsNewName(const char* dir, const char* base, const char* ext,
                       char* out, size_t n);

// Quita la extension de un nombre (para mostrar el titulo).
void     flexFsStem(const char* name, char* out, size_t n);

// Formatea bytes como "53 KB" / "1.2 MB" (unidades reales).
void     flexFsFmtSize(uint32_t bytes, char* out, size_t n);

// -------------------------------------------------------------
//  NOTAS (texto plano)
// -------------------------------------------------------------
// Lee hasta `n-1` bytes del fichero a `out` (siempre terminado en
// 0). Devuelve los bytes leidos, o -1 si no se pudo abrir.
int      flexFsReadText(const char* path, char* out, size_t n);
bool     flexFsWriteText(const char* path, const char* txt);

// -------------------------------------------------------------
//  BINARIO (registros de tamano fijo)
//  ------------------------------------------------------------
//  Lo usa el navegador para el historial y los favoritos: son
//  estructuras con bytes 0 dentro, asi que las funciones de texto de
//  arriba no sirven. Se ponen aqui, y no en el .ino, por la misma
//  regla de siempre: LittleFS solo se toca desde este modulo.
// -------------------------------------------------------------
// Lee hasta `n` bytes. Devuelve los leidos, o -1 si no se pudo abrir.
int      flexFsReadBin(const char* path, void* buf, size_t n);
// Escribe `n` bytes reemplazando el fichero. false si no se escribio todo.
bool     flexFsWriteBin(const char* path, const void* buf, size_t n);

// -------------------------------------------------------------
//  PAINT (dibujo vectorial por trazos)
//  ------------------------------------------------------------
//  POR QUE TRAZOS Y NO UN MAPA DE BITS
//  El lienzo de Paint mide 464x584 px en la pantalla de 480x800.
//  Guardado como bitmap RGB565 sin comprimir son 464*584*2 =
//  542 KB POR DIBUJO. En la particion de datos del Pro (190 KB)
//  no cabe ni uno, y en la del Ultra caben cuatro o cinco antes
//  de llenarla. Comprimirlo (RLE/PNG) obligaria a decodificar
//  entero para pintar la miniatura de la galeria.
//  Un trazo, en cambio, ocupa 5 bytes de cabecera + 4 bytes por
//  punto: un garabato largo de 300 puntos son ~1.2 KB, y un
//  dibujo completo como los de las capturas ronda los 3-8 KB.
//  Ademas se redibuja a CUALQUIER escala sin perder calidad, que
//  es justo lo que necesita la miniatura de la galeria (se
//  reproducen los mismos trazos con un factor de escala), y
//  permite guardar mientras se dibuja, trazo a trazo, sin
//  reescribir el fichero entero.
//  Contrapartida honesta: no se puede importar una foto a un
//  .fxp. Cuando haga falta eso, sera otro formato, no este.
// -------------------------------------------------------------
#define FLEXPAINT_MAGIC0  'F'
#define FLEXPAINT_MAGIC1  'X'
#define FLEXPAINT_MAGIC2  'P'
#define FLEXPAINT_MAGIC3  '1'

// Cabecera del fichero (12 bytes, sin relleno: todos los campos
// estan alineados a su tamano natural).
struct FlexPaintHdr {
  uint8_t  magic[4];
  uint16_t w, h;        // tamano del lienzo con el que se dibujo
  uint16_t strokes;     // numero de trazos guardados
  uint16_t reserved;
};

// Cabecera de un trazo, seguida de `pts` pares int16 (x, y).
struct FlexPaintStroke {
  uint16_t color;       // RGB565 real, el mismo que usa el motor grafico
  uint8_t  size;        // radio del pincel en px
  uint8_t  flags;       // reservado (0)
  uint16_t pts;
};

// Crea un .fxp vacio (0 trazos) con el tamano de lienzo indicado.
bool     flexPaintCreate(const char* path, uint16_t w, uint16_t h);

// Anade un trazo AL FINAL del fichero y actualiza el contador de
// la cabecera. Se llama al levantar el dedo, asi que el dibujo
// queda en disco aunque se corte la corriente a mitad de sesion.
bool     flexPaintAppend(const char* path, uint16_t color, uint8_t size,
                         const int16_t* xy, uint16_t pts);

// Lee la cabecera. false si el fichero no existe o no es un .fxp.
bool     flexPaintHeader(const char* path, FlexPaintHdr* out);

// Recorre los trazos del fichero llamando a `cb` por cada
// segmento (x0,y0)-(x1,y1) ya escalado por `sc` y desplazado a
// (ox,oy). Sirve TANTO para pintar el lienzo a tamano real
// (sc=1) como para la miniatura de la galeria (sc<1): la
// miniatura es el mismo dibujo, no una imagen aparte.
typedef void (*FlexPaintSegCb)(int x0, int y0, int x1, int y1,
                               uint16_t color, int radius, void* user);
bool     flexPaintReplay(const char* path, float sc, int ox, int oy,
                         FlexPaintSegCb cb, void* user);

// Borra el ultimo trazo (deshacer real: trunca el fichero).
bool     flexPaintUndo(const char* path);

// Vacia el dibujo (0 trazos) conservando el fichero y su nombre.
bool     flexPaintClear(const char* path);
