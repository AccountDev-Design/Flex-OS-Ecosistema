// #############################################################
// ##  FlexOS · TARJETA microSD  ·  SDMMC NATIVO 4 BITS
// ##  Placa JC4880P443 V1.0 (ESP32-P4)
// #############################################################
//
//  POR QUE ESTE MODULO EXISTE APARTE DE FlexOS_FS
//  ----------------------------------------------
//  FlexOS_FS es la particion INTERNA (LittleFS): siempre esta,
//  siempre monta y nunca desaparece a media lectura. La microSD es
//  lo contrario: puede no estar, puede llegar despues de arrancar,
//  puede irse mientras se reproduce un video y puede traer un
//  sistema de archivos que no sabemos leer. Meter esos estados
//  dentro de FlexOS_FS obligaria a que cada llamada suya
//  contemplara "y si el volumen se evaporo", que es justo lo que
//  hace fragil un modulo que hoy es simple y correcto.
//
//  Los dos volumenes se presentan por separado en Almacenamiento
//  ("Memoria interna" y "Tarjeta SD") y NO se mezclan: nada de
//  esta unidad de traduccion toca LittleFS.
//
//  CABLEADO REAL (verificado en el esquema JC4880P443_V1.0, hoja
//  del modulo JC-ESP32P4-M3 y hoja del conector TF_Card J1)
//  ------------------------------------------------------------
//    pin 64 -> GPIO44 -> SD_CMD    -> J1.3 CMD
//    pin 63 -> GPIO43 -> SD_CLK    -> J1.5 CLK
//    pin 62 -> GPIO42 -> SD_DATA3  -> J1.2 CD/DATA3
//    pin 61 -> GPIO41 -> SD_DATA2  -> J1.1 DATA2
//    pin 60 -> GPIO40 -> SD_DATA1  -> J1.8 DATA1
//    pin 59 -> GPIO39 -> SD_DATA0  -> J1.7 DATA0
//
//  ATENCION AL ORDEN DE DATA2/DATA3. En el esquema, DATA2 es
//  GPIO41 y DATA3 es GPIO42, no al reves. Se deja escrito aqui
//  porque es el error tipico al transcribir la tabla, y en modo de
//  4 bits intercambiarlos no da un fallo limpio: la tarjeta
//  responde a los comandos (que van por CMD) y luego los datos
//  salen corruptos de forma intermitente, que es mucho peor de
//  diagnosticar que un montaje que falla.
//
//  ALIMENTACION. TF_VCC sale de ESP_LDO_VO4 (pin 58) a traves del
//  P-MOSFET Q1 (AO3401), cuya puerta cuelga de R13 (10K a GND):
//  con la puerta baja el MOSFET conduce, asi que la tarjeta tiene
//  tension SIEMPRE que el LDO interno 4 del P4 este levantado.
//  R10, que llevaria GPIO45 a esa puerta, esta marcada NC en esta
//  revision: GPIO45 NO alimenta la tarjeta y no se toca.
//  El ejemplo Arduino oficial de esta misma placa configura los
//  pines y llama SD_MMC.begin() sin adquirir ese LDO manualmente;
//  se sigue esa ruta para que el propio driver del core gestione el
//  slot 0 y su control de potencia.
//
//  DETECCION DE PRESENCIA. La placa NO tiene linea util de
//  insercion: el pin CD del conector es CD/DATA3 y aqui se usa
//  como DATA3. Por seguridad, una tarjeta MONTADA no se abre por
//  temporizador: tanto abrir la raiz como repetir SD_MMC.begin()
//  provocan PANIC con algunas combinaciones P4/core/tarjeta.
//    · Mientras no esta montada, solo una accion explicita de una
//      pantalla pide a flexSdTick que intente el montaje.
//    · Mientras esta montada, solo una operacion real solicitada por
//      el usuario toca el bus. Si falla, el volumen se invalida y se
//      notifica la retirada; en video ocurre en la misma lectura.
//  No hay ningun bucle que lea la tarjeta continuamente.

#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "FlexOS_FS.h"        // FlexFsEntry / FLEXFS_PATH_MAX: un solo tipo de entrada

// -------------------------------------------------------------
//  Punto de montaje. Toda ruta de la tarjeta empieza por aqui, y
//  ese prefijo es lo que distingue un fichero de la SD de uno de
//  la memoria interna en una ruta suelta. Es deliberado que sea
//  una sola cadena: la interfaz nunca tiene que llevar un "flag de
//  volumen" al lado de cada ruta, que es como se acaban abriendo
//  ficheros en el volumen equivocado.
// -------------------------------------------------------------
#define FLEXSD_MOUNT      "/sdcard"
#define FLEXSD_MOUNT_LEN  7

// Carpetas propias de Flex OS dentro de la tarjeta. Se crean solo
// cuando hacen falta (al guardar algo), NUNCA al montar: montar una
// tarjeta ajena no debe escribir en ella.
#define FLEXSD_DIR_FLEXOS "/sdcard/FlexOS"
#define FLEXSD_DIR_MEDIA  "/sdcard/FlexOS/Media"
#define FLEXSD_DIR_PHOTOS "/sdcard/FlexOS/Media/Photos"
#define FLEXSD_DIR_VIDEOS "/sdcard/FlexOS/Media/Videos"
#define FLEXSD_DIR_AUDIO  "/sdcard/FlexOS/Media/Audio"

// -------------------------------------------------------------
//  Estados. Son los que la interfaz tiene que saber distinguir
//  para decir algo util; no hay un "error generico" cajon de
//  sastre porque "no se pudo" no le dice nada a nadie.
// -------------------------------------------------------------
enum {
  FLEXSD_ABSENT = 0,   // no hay tarjeta (o no responde a los comandos)
  FLEXSD_READY,        // montada y utilizable
  FLEXSD_ERR_MOUNT,    // la tarjeta responde pero no se pudo montar
  FLEXSD_ERR_FS,       // sistema de archivos no compatible (no FAT)
  FLEXSD_ERR_IO,       // fallo de lectura/escritura sobre tarjeta montada
  FLEXSD_ERR_HW        // el controlador SDMMC no arranco (pines/driver)
};

// -------------------------------------------------------------
//  Ciclo de vida
// -------------------------------------------------------------
// Prepara el controlador (pines SDMMC). Se llama una vez en setup()
// antes del primer y unico intento automatico de montaje. Devuelve false si el
// hardware no se pudo preparar (estado FLEXSD_ERR_HW), en cuyo caso
// las demas funciones devuelven cero/false para siempre.
bool        flexSdBegin();

// Intenta montar AHORA. Libera cualquier montaje anterior antes de
// intentarlo, para que un reintento tras un fallo parta de un estado
// limpio y no de medio driver colgado. Devuelve true si quedo
// montada. Es reentrante: si ya estaba montada no hace nada y
// devuelve true.
bool        flexSdMount();

// Desmonta y suelta el driver. Sube la generacion (ver abajo), asi
// que cualquier fichero abierto queda invalidado por construccion.
void        flexSdUnmount();

bool        flexSdReady();
int         flexSdState();

// Motivo legible del estado actual. Siempre devuelve algo que se
// puede ensenar tal cual en pantalla; nunca NULL.
const char* flexSdError();

// -------------------------------------------------------------
//  GENERACION DE MONTAJE
//  ------------------------------------------------------------
//  Sube en CADA montaje y en CADA desmontaje. Quien tenga un
//  fichero abierto, un indice construido o una posicion de
//  reproduccion guarda la generacion con la que lo obtuvo y la
//  compara antes de usarlo: si no coincide, la tarjeta que hay
//  ahora no es la de entonces (o no hay ninguna) y lo que tenia en
//  la mano ya no vale. Es lo que impide leer con un descriptor de
//  una tarjeta que ya se saco.
// -------------------------------------------------------------
uint32_t    flexSdGeneration();

// -------------------------------------------------------------
//  SONDEO
// -------------------------------------------------------------
// Llamar desde loop(). No toca SDMMC salvo que una pantalla haya llamado
// antes a flexSdPoke(). Devuelve true si el estado CAMBIO en esta llamada.
// Nunca intenta montar mientras flexSdBusySet(true) este activo.
bool        flexSdTick();

// Si no hay volumen, pide que el proximo flexSdTick() intente montar.
// Con una tarjeta ya montada no hace
// ninguna lectura. La llaman Almacenamiento, Galeria y Multimedia.
void        flexSdPoke();

// Aplaza una peticion durante una operacion sensible al tiempo (un
// fotograma de video). No desactiva la deteccion PASIVA: si una
// lectura falla, la retirada se detecta igual y al instante.
void        flexSdBusySet(bool busy);
// Lectura pura del mismo indicador. La usa el diagnostico de Wi-Fi/SD para
// poder decir si la tarjeta estaba ocupada en el instante del fallo.
bool        flexSdBusyGet();

// -------------------------------------------------------------
//  Capacidad. Son bytes REALES leidos del volumen montado; sin
//  tarjeta devuelven 0 y la interfaz debe decir "sin tarjeta", no
//  pintar una barra al 0%.
//
//  usedBytes es CARO en FAT (recorre la tabla de asignacion), asi
//  que se cachea y solo se recalcula cuando cambia la generacion o
//  cuando se pide explicitamente con flexSdRefreshUsage().
// -------------------------------------------------------------
uint64_t    flexSdTotalBytes();
uint64_t    flexSdUsedBytes();
uint64_t    flexSdFreeBytes();
void        flexSdRefreshUsage();

// Nombre del tipo de tarjeta ("SDHC/SDXC", "SDSC", "MMC") y del
// sistema de archivos ("FAT32", "exFAT", "FAT16"). Cadenas
// estaticas; nunca NULL.
const char* flexSdCardTypeName();
const char* flexSdFsName();

// -------------------------------------------------------------
//  Consulta. Las rutas se dan SIEMPRE completas y con el prefijo
//  del punto de montaje ("/sdcard/DCIM"). Una ruta sin ese prefijo
//  se rechaza: es la unica forma de que no se pueda escribir en la
//  tarjeta creyendo que se escribe en la memoria interna.
// -------------------------------------------------------------
bool        flexSdIsSdPath(const char* path);

// Lista un directorio con el mismo orden estable que flexFsList
// (carpetas primero, luego por nombre). Devuelve el numero de
// entradas escritas, o -1 si no se pudo abrir (y en ese caso ya ha
// disparado la verificacion de presencia).
//
// `skip` salta las primeras N entradas SIN copiarlas: es lo que
// permite al indexador recorrer una carpeta de 900 fotos en lotes
// pequenos sin reservar sitio para las 900.
int         flexSdListFrom(const char* dir, FlexFsEntry* out, int maxn, int skip);
int         flexSdList(const char* dir, FlexFsEntry* out, int maxn);

bool        flexSdExists(const char* path);
bool        flexSdIsDir(const char* path);
uint64_t    flexSdSize(const char* path);

// Crea la ruta completa (todos los niveles que falten). Solo acepta
// rutas dentro de FLEXSD_DIR_FLEXOS: este modulo no crea carpetas
// sueltas en la raiz de una tarjeta que es del usuario.
bool        flexSdMkdirFlexOS(const char* path);

// Crea el arbol /FlexOS/Media/{Photos,Videos,Audio}. Se llama solo
// cuando se va a GUARDAR algo, no al montar.
bool        flexSdEnsureMediaDirs();

// -------------------------------------------------------------
//  LECTURA POR BLOQUES CON DESCRIPTOR ABIERTO
//  ------------------------------------------------------------
//  Reabrir el fichero en cada lectura (que es lo que hace
//  flexFsReadAt) cuesta una busqueda de directorio por llamada. En
//  un video eso son dos accesos de directorio por fotograma, y a
//  25 fps se nota. Aqui el descriptor se abre UNA vez y se
//  mantiene, con su generacion pegada: si la tarjeta se va, la
//  siguiente lectura devuelve -1 y el reproductor cierra.
//
//  El struct es opaco a proposito (un puntero y dos enteros): el
//  llamante lo declara en su propio estado estatico, no hay
//  reserva dinamica por fichero.
// -------------------------------------------------------------
struct FlexSdFile {
  void*    h;        // File* del core; NULL = cerrado
  uint32_t gen;      // generacion de montaje con la que se abrio
  uint32_t size;     // tamano en el momento de abrir
  uint32_t pos;      // desplazamiento actual
};

bool        flexSdOpen(FlexSdFile* f, const char* path);
void        flexSdClose(FlexSdFile* f);
bool        flexSdIsOpen(const FlexSdFile* f);
// Devuelve bytes leidos (0 = fin de fichero) o -1 si hubo error o
// la tarjeta ya no es la misma.
int         flexSdRead(FlexSdFile* f, void* buf, uint32_t n);
bool        flexSdSeek(FlexSdFile* f, uint32_t off);
uint32_t    flexSdTell(const FlexSdFile* f);
uint32_t    flexSdFileSize(const FlexSdFile* f);

// -------------------------------------------------------------
//  Escritura. Existe solo para lo que Flex OS genera (capturas,
//  indices). NUNCA formatea, nunca borra nada del usuario y
//  distingue "tarjeta llena" de "error de escritura", porque son
//  dos mensajes distintos para quien lo lee.
// -------------------------------------------------------------
enum { FLEXSD_W_OK = 0, FLEXSD_W_ERR = -1, FLEXSD_W_FULL = -2, FLEXSD_W_RO = -3 };
int         flexSdWriteBin(const char* path, const void* buf, size_t n);
int         flexSdReadBin(const char* path, void* buf, size_t n);
