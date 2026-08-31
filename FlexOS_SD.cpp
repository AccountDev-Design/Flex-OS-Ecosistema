// #############################################################
// ##  FlexOS · TARJETA microSD · SDMMC NATIVO 4 BITS
// ##  Implementacion para ESP32-P4 (placa JC4880P443 V1.0)
// #############################################################
//
//  Aqui no hay ni un dato inventado: capacidad, tipo de tarjeta,
//  numero de entradas y tamanos salen de preguntarselo al volumen
//  montado. Cuando algo no se puede saber, se devuelve 0 / false y
//  el estado dice por que.

#include "FlexOS_SD.h"
#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <new>

// -------------------------------------------------------------
//  PINES. Constantes con nombre y en un solo sitio: si algun dia
//  cambia la revision de placa, se cambia aqui y no hay una segunda
//  copia en el .ino que se quede vieja.
//  (Orden verificado en el esquema; ver la cabecera del .h, en
//  especial la nota sobre DATA2=GPIO41 y DATA3=GPIO42.)
// -------------------------------------------------------------
#define SDPIN_CLK    43
#define SDPIN_CMD    44
#define SDPIN_D0     39
#define SDPIN_D1     40
#define SDPIN_D2     41
#define SDPIN_D3     42

// Periodo de reintento tras retirar una tarjeta. Solo se usa mientras
// NO hay un volumen montado: una tarjeta ya montada nunca se toca por
// temporizador. En esta placa DATA3 ocupa el contacto CD y no existe
// una linea de deteccion independiente; abrir la raiz cada pocos
// segundos provoca PANIC en algunas combinaciones P4/core/tarjeta.
#define FLEXSD_POLL_MS       2000
// Tras un fallo de montaje se espera mas: reintentar cada 2 s un
// montaje que tarda ~200 ms en fallar si que se notaria.
#define FLEXSD_RETRY_MS      6000
#define FLEXSD_MAXOPEN       6

// -------------------------------------------------------------
//  Estado del modulo
// -------------------------------------------------------------
static bool          sdHwReady   = false;   // pines SDMMC configurados
static bool          sdDriverTouched = false; // begin/end ya tocaron el host
static bool          sdMounted   = false;
static int           sdState     = FLEXSD_ABSENT;
static const char*   sdErr       = "Sin tarjeta insertada";
static uint32_t      sdGen       = 1;       // nunca 0: 0 = "sin generacion"
static bool          sdOneBit    = false;   // se cayo a 1 bit para poder montar
static bool          sdEverMounted = false; // hubo una tarjeta valida en esta sesion
static unsigned long sdNextPoll  = 0;
static bool          sdBusy      = false;
static uint64_t      sdTotal     = 0, sdUsed = 0;
static bool          sdUsageOk   = false;

// -------------------------------------------------------------
//  Utilidades de ruta
// -------------------------------------------------------------
bool flexSdIsSdPath(const char* path){
  if(!path) return false;
  if(strncmp(path, FLEXSD_MOUNT, FLEXSD_MOUNT_LEN) != 0) return false;
  char c = path[FLEXSD_MOUNT_LEN];
  return c == 0 || c == '/';
}

// Solo se acepta una ruta que este dentro del punto de montaje. Sin
// esto, un fallo de composicion de cadena en la interfaz acabaria
// abriendo "/DCIM" en LittleFS.
static bool sdPathOk(const char* path){
  return sdMounted && flexSdIsSdPath(path);
}

// Comparacion de nombres sin distinguir mayusculas. Propia a
// proposito: strcasecmp es POSIX y el proyecto ya evita depender de
// ella (misma razon que en galExtIs del sketch).
static int sdNameCmp(const char* a, const char* b){
  for(;; a++, b++){
    char ca = *a, cb = *b;
    if(ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
    if(cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
    if(ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
    if(!ca) return 0;
  }
}

static const char* baseNameOf(const char* p){
  if(!p) return "";
  const char* s = strrchr(p, '/');
  return s ? s + 1 : p;
}

static void sdSetGone(){
  if(sdMounted){
    SD_MMC.end();
    sdMounted = false;
    sdGen++;                      // invalida descriptores e indices
  }
  sdState   = FLEXSD_ABSENT;
  sdErr     = "Tarjeta retirada";
  sdUsageOk = false;
  sdTotal = sdUsed = 0;
  sdNextPoll = millis() + FLEXSD_POLL_MS;
}

// Una operacion real que falla invalida el volumen sin hacer una
// segunda operacion de "comprobacion" sobre la raiz. Esa segunda
// apertura era precisamente la ruta que producia reinicios PANIC en
// placa. Las ausencias normales se filtran con exists() antes de
// llegar aqui; por tanto esto representa un fallo real de E/S.
static void sdIoFailed(){
  if(!sdMounted) return;
  sdSetGone();
  sdErr = "La tarjeta dejo de responder";
}

// -------------------------------------------------------------
//  Arranque del hardware
// -------------------------------------------------------------
bool flexSdBegin(){
  if(sdHwReady) return true;

  // El ejemplo Arduino del fabricante de ESTA placa solo configura
  // estos pines y deja que SD_MMC.begin() haga su secuencia nativa
  // para el LDO 4/slot 0. No se adquiere el LDO por separado: hacerlo
  // antes del driver puede dejarle un controlador de potencia ajeno.
  // Pines SDMMC nativos de 4 bits: ni SPI, ni CS, ni SD.begin().
  if(!SD_MMC.setPins(SDPIN_CLK, SDPIN_CMD, SDPIN_D0, SDPIN_D1, SDPIN_D2, SDPIN_D3)){
    sdState = FLEXSD_ERR_HW;
    sdErr   = "No se pudo configurar el bus SDMMC";
    Serial.println(F("[SD] ERROR: setPins fallo"));
    return false;
  }

  sdHwReady  = true;
  sdState    = FLEXSD_ABSENT;
  sdErr      = "Sin tarjeta insertada";
  sdNextPoll = 0;                       // el primer tick ya intenta montar
  return true;
}

// -------------------------------------------------------------
//  Montaje
//  ------------------------------------------------------------
//  Antes de cada intento se llama a SD_MMC.end(): un montaje
//  anterior a medias deja el driver tomado y el siguiente begin()
//  fallaria por eso y no por la tarjeta. Empezar limpio es lo que
//  hace que reinsertar la tarjeta funcione sin reiniciar.
// -------------------------------------------------------------
static bool sdTryBegin(bool oneBit){
  // IMPORTANTE: el PRIMER intento NO puede empezar con SD_MMC.end(). El
  // ejemplo mp3_player del fabricante hace exactamente setPins() -> begin().
  // En algunas versiones del core P4, end() antes del primer begin() limpia
  // la configuracion de slot que setPins() acaba de instalar y hace que una
  // tarjeta valida parezca ausente. Para reintentos si se limpia el driver,
  // pero se vuelven a declarar los seis pines antes de tocar begin().
  if(sdDriverTouched) SD_MMC.end();
  if(!SD_MMC.setPins(SDPIN_CLK, SDPIN_CMD, SDPIN_D0, SDPIN_D1, SDPIN_D2, SDPIN_D3)){
    sdState = FLEXSD_ERR_HW;
    sdErr = "No se pudo reconfigurar el bus SDMMC";
    return false;
  }
  sdDriverTouched = true;

  // La primera ruta es IDENTICA al ejemplo mp3_player del fabricante:
  // setPins() seguido de SD_MMC.begin() sin argumentos. Su punto de montaje
  // por defecto es /sdcard, que coincide con FLEXSD_MOUNT.
  if(!oneBit) return SD_MMC.begin();

  // Solo si la ruta oficial de 4 bits falla se intenta diagnostico a
  // 1 bit. Conserva FAT sin formateo y el mismo punto de montaje.
  return SD_MMC.begin(FLEXSD_MOUNT, true, false, SDMMC_FREQ_DEFAULT,
                      FLEXSD_MAXOPEN);
}

bool flexSdMount(){
  if(!sdHwReady && !flexSdBegin()) return false;
  if(sdMounted) return true;

  bool ok = false;
  sdOneBit = false;

  // Primero la secuencia Arduino oficial del fabricante en 4 bits.
  // El intento de 1 bit es solo un diagnostico posterior.
  if(sdTryBegin(false))                  ok = true;
  else if(sdTryBegin(true))              { ok = true; sdOneBit = true; }

  if(!ok){
    SD_MMC.end();
    sdMounted = false;
    // LIMITE HONESTO DEL API: SD_MMC.begin() devuelve el mismo
    // false cuando no hay tarjeta y cuando la hay pero su sistema
    // de archivos no se reconoce (el core descarta esa distincion
    // antes de devolver). Asi que no se inventa un estado: si nunca
    // hubo tarjeta valida se dice "no hay o no se puede leer" y se
    // nombra el formato que si funciona; si la habia, se dice que
    // dejo de responder, que eso si lo sabemos.
    if(sdEverMounted){
      sdState = FLEXSD_ERR_MOUNT;
      sdErr   = "La tarjeta dejo de responder";
    } else {
      sdState = FLEXSD_ABSENT;
      sdErr   = "Sin tarjeta, o formato no compatible (usa FAT32)";
    }
    sdNextPoll = millis() + FLEXSD_RETRY_MS;
    return false;
  }

  uint8_t ct = SD_MMC.cardType();
  if(ct == CARD_NONE || ct == CARD_UNKNOWN){
    // Monto pero el tipo no es utilizable: eso si es un montaje malo.
    SD_MMC.end();
    sdMounted = false;
    sdState   = FLEXSD_ERR_MOUNT;
    sdErr     = "Tarjeta no reconocida";
    sdNextPoll = millis() + FLEXSD_RETRY_MS;
    return false;
  }

  sdMounted     = true;
  sdEverMounted = true;
  sdGen++;                              // tarjeta nueva: todo lo anterior caduca
  sdState  = FLEXSD_READY;
  sdErr    = sdOneBit ? "Montada en 1 bit (revisa el bus de datos)" : "Tarjeta lista";
  sdUsageOk = false;                    // se calcula a peticion, no aqui
  sdNextPoll = millis() + FLEXSD_POLL_MS;
  Serial.printf("[SD] montada  tipo=%u  bus=%s\n",
                (unsigned)ct, sdOneBit ? "1bit" : "4bit");
  return true;
}

void flexSdUnmount(){
  if(!sdMounted) return;
  SD_MMC.end();
  sdMounted = false;
  sdGen++;
  sdState   = FLEXSD_ABSENT;
  sdErr     = "Tarjeta expulsada";
  sdUsageOk = false;
  sdTotal = sdUsed = 0;
}

bool        flexSdReady()      { return sdMounted; }
int         flexSdState()      { return sdState; }
const char* flexSdError()      { return sdErr ? sdErr : ""; }
uint32_t    flexSdGeneration() { return sdGen; }
void        flexSdPoke()       { sdNextPoll = 0; }
void        flexSdBusySet(bool b){ sdBusy = b; }

// -------------------------------------------------------------
//  Sondeo. La inmensa mayoria de las vueltas de loop() salen por
//  la primera comparacion sin tocar nada.
// -------------------------------------------------------------
bool flexSdTick(){
  if(!sdHwReady) return false;
  if(sdBusy)     return false;              // fotograma de video en curso

  // Sin linea CD independiente no existe una comprobacion inocua de
  // presencia. Mientras esta montada solo las operaciones solicitadas
  // por el usuario acceden al bus; su fallo activa sdIoFailed(). Esto
  // elimina por completo la apertura periodica que causaba PANIC.
  if(sdMounted) return false;

  unsigned long now = millis();
  if((long)(now - sdNextPoll) < 0) return false;
  sdNextPoll = now + FLEXSD_POLL_MS;

  int before = sdState;
  flexSdMount();                            // insercion/reconexion en caliente
  return sdState != before;
}

// -------------------------------------------------------------
//  Capacidad
//  ------------------------------------------------------------
//  usedBytes() recorre la tabla de asignacion de FAT: en una
//  tarjeta de 64 GB puede costar cientos de milisegundos. Por eso
//  se calcula solo cuando se pide (al abrir Almacenamiento) y se
//  cachea hasta que cambie la generacion. Ninguna pantalla que
//  dibuje en bucle lo llama.
// -------------------------------------------------------------
void flexSdRefreshUsage(){
  if(!sdMounted){ sdTotal = sdUsed = 0; sdUsageOk = false; return; }
  sdTotal = SD_MMC.totalBytes();
  sdUsed  = SD_MMC.usedBytes();
  if(sdTotal == 0){ sdUsageOk = false; sdIoFailed(); return; }
  if(sdUsed > sdTotal) sdUsed = sdTotal;
  sdUsageOk = true;
}

static void sdUsageEnsure(){ if(!sdUsageOk) flexSdRefreshUsage(); }

uint64_t flexSdTotalBytes(){ if(!sdMounted) return 0; sdUsageEnsure(); return sdTotal; }
uint64_t flexSdUsedBytes (){ if(!sdMounted) return 0; sdUsageEnsure(); return sdUsed;  }
uint64_t flexSdFreeBytes (){
  if(!sdMounted) return 0;
  sdUsageEnsure();
  return sdTotal > sdUsed ? sdTotal - sdUsed : 0;
}

const char* flexSdCardTypeName(){
  if(!sdMounted) return "-";
  switch(SD_MMC.cardType()){
    case CARD_MMC:  return "MMC";
    case CARD_SD:   return "SDSC";
    case CARD_SDHC: return "SDHC/SDXC";
    default:        return "Desconocida";
  }
}

// El core solo monta FAT (FAT12/16/32 y exFAT si esta compilado).
// Se deduce del tamano del volumen, que es como lo decide la propia
// especificacion SD al formatear: por encima de 32 GB la tarjeta
// viene en exFAT de fabrica. Si no se pudo leer la capacidad no se
// adivina: se dice "FAT".
const char* flexSdFsName(){
  if(!sdMounted) return "-";
  sdUsageEnsure();
  if(!sdUsageOk)                     return "FAT";
  if(sdTotal >  (64ULL << 30))       return "exFAT";
  if(sdTotal >  (32ULL << 30))       return "exFAT";
  if(sdTotal >= (2ULL  << 30))       return "FAT32";
  return "FAT16";
}

// -------------------------------------------------------------
//  Listado
//  ------------------------------------------------------------
//  DOS funciones a proposito:
//
//  · flexSdListFrom() entrega las entradas en el ORDEN FISICO del
//    directorio y permite saltar las primeras N. Ese orden es
//    estable mientras no se toque la carpeta, que es exactamente lo
//    que necesita el indexador para recorrer 900 fotos en lotes de
//    16 sin reservar sitio para 900 ni repetir ni saltarse ninguna.
//    NO ordena: ordenar cada lote daria un orden global incoherente.
//
//  · flexSdList() lee de una vez y SI ordena (carpetas primero,
//    luego por nombre), que es lo que quiere el explorador.
//
//  El campo `items` de las carpetas se deja en 0xFFFF = DESCONOCIDO.
//  Contar los elementos de cada subcarpeta obliga a abrirlas todas,
//  y en una tarjeta con DCIM de miles de ficheros eso convierte
//  entrar en una carpeta en una espera de segundos. La interfaz
//  ensena "Carpeta" en vez de un numero: preferimos no dar el dato
//  a darlo mal o a costa de bloquear la pantalla.
// -------------------------------------------------------------
#define FLEXSD_ITEMS_UNKNOWN 0xFFFF

int flexSdListFrom(const char* dir, FlexFsEntry* out, int maxn, int skip){
  if(!out || maxn <= 0) return 0;
  if(!sdPathOk(dir)) return -1;

  // DCIM, Pictures, Movies, etc. son raices OPCIONALES. Una tarjeta
  // vacia no debe convertir cada carpeta ausente en un error ni
  // disparar verificaciones adicionales del volumen.
  if(!SD_MMC.exists(dir)) return 0;

  File d = SD_MMC.open(dir);
  if(!d || !d.isDirectory()){
    if(d) d.close();
    sdIoFailed();
    return -1;
  }
  int seen = 0, n = 0;
  for(;;){
    File e = d.openNextFile();
    if(!e) break;
    if(seen++ < skip){ e.close(); continue; }
    const char* nm = baseNameOf(e.name());
    if(nm[0]){
      snprintf(out[n].name, FLEXFS_NAME_MAX, "%s", nm);
      out[n].dir   = e.isDirectory();
      out[n].size  = out[n].dir ? 0 : (uint32_t)e.size();
      out[n].items = out[n].dir ? FLEXSD_ITEMS_UNKNOWN : 0;
      n++;
    }
    e.close();
    if(n >= maxn) break;
  }
  d.close();
  return n;
}

int flexSdList(const char* dir, FlexFsEntry* out, int maxn){
  int n = flexSdListFrom(dir, out, maxn, 0);
  if(n <= 1) return n;
  // Insercion: n esta acotado por maxn (24-64 en las pantallas), asi
  // que el coste es irrelevante frente a la lectura del directorio.
  for(int i = 1; i < n; i++){
    FlexFsEntry k = out[i];
    int j = i - 1;
    while(j >= 0){
      bool after = (out[j].dir != k.dir) ? (!out[j].dir && k.dir)
                                         : (sdNameCmp(out[j].name, k.name) > 0);
      if(!after) break;
      out[j + 1] = out[j];
      j--;
    }
    out[j + 1] = k;
  }
  return n;
}

bool flexSdExists(const char* path){
  if(!sdPathOk(path)) return false;
  return SD_MMC.exists(path);
}

bool flexSdIsDir(const char* path){
  if(!sdPathOk(path)) return false;
  File f = SD_MMC.open(path);
  if(!f) return false;
  bool d = f.isDirectory();
  f.close();
  return d;
}

uint64_t flexSdSize(const char* path){
  if(!sdPathOk(path)) return 0;
  File f = SD_MMC.open(path);
  if(!f) return 0;
  uint64_t s = f.isDirectory() ? 0 : (uint64_t)f.size();
  f.close();
  return s;
}

// -------------------------------------------------------------
//  Carpetas de Flex OS. Solo dentro de /FlexOS: este modulo no
//  crea, mueve ni borra nada del usuario (DCIM, Download, ...).
// -------------------------------------------------------------
bool flexSdMkdirFlexOS(const char* path){
  if(!sdMounted || !path) return false;
  if(strncmp(path, FLEXSD_DIR_FLEXOS, strlen(FLEXSD_DIR_FLEXOS)) != 0) return false;
  char tmp[FLEXFS_PATH_MAX];
  snprintf(tmp, sizeof(tmp), "%s", path);
  // Crea nivel a nivel: mkdir de FAT no crea padres.
  for(char* p = tmp + FLEXSD_MOUNT_LEN + 1; *p; p++){
    if(*p != '/') continue;
    *p = 0;
    if(!SD_MMC.exists(tmp) && !SD_MMC.mkdir(tmp)){ *p = '/'; sdIoFailed(); return false; }
    *p = '/';
  }
  if(SD_MMC.exists(tmp)) return true;
  if(SD_MMC.mkdir(tmp)) return true;
  sdIoFailed();
  return false;
}

bool flexSdEnsureMediaDirs(){
  if(!sdMounted) return false;
  return flexSdMkdirFlexOS(FLEXSD_DIR_PHOTOS)
      && flexSdMkdirFlexOS(FLEXSD_DIR_VIDEOS)
      && flexSdMkdirFlexOS(FLEXSD_DIR_AUDIO);
}

// -------------------------------------------------------------
//  Descriptor abierto
//  ------------------------------------------------------------
//  El File del core se reserva con new/delete UNA vez por fichero
//  abierto (no por lectura ni por fotograma). Un reproductor tiene
//  exactamente uno vivo, y flexSdClose lo suelta en todos los
//  caminos de salida.
// -------------------------------------------------------------
bool flexSdIsOpen(const FlexSdFile* f){
  return f && f->h && f->gen == sdGen;
}

bool flexSdOpen(FlexSdFile* f, const char* path){
  if(!f) return false;
  flexSdClose(f);
  if(!sdPathOk(path)) return false;
  if(!SD_MMC.exists(path)) return false;
  File h = SD_MMC.open(path, FILE_READ);
  if(!h){ sdIoFailed(); return false; }
  if(h.isDirectory()){ h.close(); return false; }
  File* keep = new (std::nothrow) File(h);
  if(!keep){ h.close(); return false; }
  f->h    = (void*)keep;
  f->gen  = sdGen;
  f->size = (uint32_t)keep->size();
  f->pos  = 0;
  return true;
}

void flexSdClose(FlexSdFile* f){
  if(!f) return;
  if(f->h){
    File* h = (File*)f->h;
    // Si la tarjeta ya se fue, el descriptor no es valido: se
    // destruye el objeto sin llamar a close() sobre un volumen
    // desmontado.
    if(f->gen == sdGen && sdMounted) h->close();
    delete h;
  }
  f->h = NULL; f->gen = 0; f->size = 0; f->pos = 0;
}

int flexSdRead(FlexSdFile* f, void* buf, uint32_t n){
  if(!f || !f->h || !buf) return -1;
  if(f->gen != sdGen || !sdMounted) return -1;   // tarjeta cambiada o ausente
  if(n == 0) return 0;
  File* h = (File*)f->h;
  int rd = h->read((uint8_t*)buf, n);
  // Una lectura corta ANTES del final del fichero no es un fin de
  // fichero: es que la tarjeta ha dejado de contestar. Distinguirlo
  // importa porque el core devuelve 0 (no -1) cuando se saca la
  // tarjeta. Sin esto el reproductor interpretaria la retirada como
  // fin normal del fichero y seguiria conservando un volumen invalido.
  if(rd < 0 || (rd == 0 && f->pos < f->size)){ sdIoFailed(); return -1; }
  f->pos += (uint32_t)rd;
  return rd;
}

bool flexSdSeek(FlexSdFile* f, uint32_t off){
  if(!f || !f->h) return false;
  if(f->gen != sdGen || !sdMounted) return false;
  if(off > f->size) return false;
  File* h = (File*)f->h;
  if(!h->seek(off)){ sdIoFailed(); return false; }
  f->pos = off;
  return true;
}

uint32_t flexSdTell(const FlexSdFile* f){ return f ? f->pos : 0; }
uint32_t flexSdFileSize(const FlexSdFile* f){ return f ? f->size : 0; }

// -------------------------------------------------------------
//  Lectura/escritura de una pieza completa. Para ficheros
//  pequenos (indices, miniaturas). Los grandes van por descriptor.
// -------------------------------------------------------------
int flexSdReadBin(const char* path, void* buf, size_t n){
  if(!sdPathOk(path) || !buf) return -1;
  if(!SD_MMC.exists(path)) return -1;
  File h = SD_MMC.open(path, FILE_READ);
  if(!h){ sdIoFailed(); return -1; }
  if(h.isDirectory()){ h.close(); return -1; }
  int rd = h.read((uint8_t*)buf, n);
  h.close();
  if(rd < 0){ sdIoFailed(); return -1; }
  return rd;
}

int flexSdWriteBin(const char* path, const void* buf, size_t n){
  if(!sdPathOk(path) || !buf) return FLEXSD_W_ERR;
  // "Tarjeta llena" se comprueba ANTES de abrir: si se descubriera
  // a mitad de la escritura quedaria un fichero truncado en la
  // tarjeta del usuario.
  if(flexSdFreeBytes() < (uint64_t)n + 65536ULL) return FLEXSD_W_FULL;
  File h = SD_MMC.open(path, FILE_WRITE);
  if(!h){ sdIoFailed(); return FLEXSD_W_ERR; }
  size_t wr = h.write((const uint8_t*)buf, n);
  h.close();
  sdUsageOk = false;                     // lo que habia cacheado ya no vale
  if(wr != n){ sdIoFailed(); return FLEXSD_W_FULL; }
  return FLEXSD_W_OK;
}
