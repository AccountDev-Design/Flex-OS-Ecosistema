// #############################################################
// ##  FLEX OS ULTRA  ·  NUCLEO DE MEDIOS  ·  indice compartido
// ##  ----------------------------------------------------------
// ##  Lo que comparten Galeria, Multimedia, Almacenamiento y el Explorador:
// ##  montaje de la microSD, clasificacion de ficheros e indice por lotes
// ##  (mediaStorageTick / mediaIndexTick).
// ##
// ##  COMO ENCAJA ESTE ARCHIVO
// ##  ----------------------------------------------------------
// ##  Es una PARTE del sketch FlexOS_Ultra.ino, no una unidad de
// ##  traduccion independiente. FlexOS_Ultra.ino lo incluye en el
// ##  orden que fija la cadena de cabeceras (cada modulo incluye al
// ##  anterior), asi que todo el sistema sigue compilandose como UN
// ##  SOLO archivo, exactamente igual que antes de separarlo.
// ##
// ##  Consecuencias practicas, y son las que mantienen esto seguro:
// ##    · Las variables globales se DEFINEN una sola vez, aqui, en el
// ##      modulo al que pertenecen. No hace falta `extern` ni existe
// ##      el riesgo de una definicion duplicada en el enlazado.
// ##    · El ORDEN de definicion es el mismo que tenia el .ino: una
// ##      funcion `static` solo se puede llamar despues de definirse,
// ##      y esa relacion se conserva modulo a modulo.
// ##    · La cadena de includes es LINEAL (Types -> ... -> Recovery),
// ##      asi que no hay dependencias circulares posibles.
// ##    · No lo incluyas por tu cuenta desde otro sitio: el punto de
// ##      entrada del sistema es siempre FlexOS_Ultra.ino.
// #############################################################
#pragma once
#include "FlexOS_Ultra_QuickPanelEdit.h"   // eslabon anterior de la cadena

// #############################################################
// ##  NUCLEO DE MEDIOS DE FLEX OS
// ##  ------------------------------------------------------
// ##  Todo lo que Galeria, Multimedia, Almacenamiento y el
// ##  Explorador comparten, en UN solo sitio:
// ##
// ##    · los dos volumenes (interno y tarjeta) vistos igual,
// ##    · un lector de ficheros unico para los dos,
// ##    · el indice de medios, que se construye por lotes desde
// ##      el bucle principal y nunca bloquea,
// ##    · la cache de miniaturas,
// ##    · la orientacion Auto/Vertical/Horizontal,
// ##    · y el puente con la isla de notificaciones.
// ##
// ##  POR QUE AQUI Y NO EN CADA APP. Antes de esto, la Galeria
// ##  tenia su propio recorrido de carpetas y su propio
// ##  decodificador de miniatura, y Multimedia una fuente
// ##  sintetica sin relacion con ninguna de las dos. Con dos
// ##  volumenes y una tarjeta que puede irse en cualquier
// ##  momento, mantener eso duplicado seria garantizar que una de
// ##  las copias se olvida de comprobar la retirada. Aqui hay UN
// ##  indice, UN lector y UNA politica de invalidacion.
// #############################################################

// -------------------------------------------------------------
//  Puente con la isla de notificaciones
//  ------------------------------------------------------------
//  notifPush vive mucho mas abajo (necesita la geometria de la
//  isla), asi que se declara aqui. Se reutiliza la cola que ya
//  existe en vez de crear una segunda: dos capas escribiendo en la
//  misma banda de bbuf es exactamente el fallo que documenta la
//  cabecera de la isla.
// -------------------------------------------------------------
static void notifPush(const DetectedModule* m);

// AVISO DEL SISTEMA POR LA ISLA. Misma cola y misma banda que el resto: no se
// crea una segunda capa de notificaciones (dos compositores sobre las mismas
// filas de bbuf es el fallo que documenta la cabecera de la isla).
static void sysNotify(const char* title, const char* sub){
  DetectedModule m;
  memset(&m, 0, sizeof(m));
  m.type    = MOD_UNKNOWN;
  m.i2cAddr = 0;
  m.active  = true;
  m.detectedAt = millis();
  snprintf(m.name, sizeof(m.name), "%s", title ? title : "");
  snprintf(m.sub,  sizeof(m.sub),  "%s", sub   ? sub   : "");
  notifPush(&m);
}

static void mediaNotify(ModuleType t, const char* title, const char* sub){
  DetectedModule m;
  memset(&m, 0, sizeof(m));
  m.type    = t;
  m.i2cAddr = 0;                 // no es un dispositivo del bus
  m.active  = true;
  m.detectedAt = millis();
  snprintf(m.name, sizeof(m.name), "%s", title ? title : "");
  snprintf(m.sub,  sizeof(m.sub),  "%s", sub   ? sub   : "");
  notifPush(&m);
}

// -------------------------------------------------------------
//  VOLUMENES
//  ------------------------------------------------------------
//  Una ruta lleva su volumen dentro: si empieza por /sdcard es de
//  la tarjeta y si no, de la particion interna. No hay una bandera
//  aparte que se pueda desincronizar de la ruta.
// -------------------------------------------------------------
static inline bool mediaIsSd(const char* path){ return flexSdIsSdPath(path); }

static bool mediaVolReady(const char* path){
  return mediaIsSd(path) ? flexSdReady() : flexFsReady();
}

// Nombre corto del volumen, para la ficha de detalles y la Galeria.
static const char* mediaVolName(const char* path){
  return mediaIsSd(path) ? "Tarjeta SD" : "Memoria interna";
}

// Listado uniforme. Devuelve -1 si el volumen no esta disponible,
// para que el llamante distinga "carpeta vacia" de "no hay tarjeta".
static int mediaList(const char* dir, FlexFsEntry* out, int maxn){
  if(mediaIsSd(dir)) return flexSdReady() ? flexSdList(dir, out, maxn) : -1;
  return flexFsReady() ? flexFsList(dir, out, maxn) : -1;
}

// -------------------------------------------------------------
//  LECTOR UNIFICADO (MediaStream + FlexMediaIO)
// -------------------------------------------------------------
static void mediaStreamClose(MediaStream* s){
  if(!s) return;
  if(s->kind == MSTREAM_SD) flexSdClose(&s->sd);
  s->kind = MSTREAM_NONE;
  s->path[0] = 0;
  s->pos = s->size = 0;
}

static bool mediaStreamOpen(MediaStream* s, const char* path){
  if(!s || !path) return false;
  mediaStreamClose(s);
  if(mediaIsSd(path)){
    if(!flexSdReady()) return false;
    if(!flexSdOpen(&s->sd, path)) return false;
    s->kind = MSTREAM_SD;
    s->size = flexSdFileSize(&s->sd);
  } else {
    if(!flexFsReady() || !flexFsExists(path)) return false;
    s->kind = MSTREAM_INT;
    s->size = flexFsSize(path);
    snprintf(s->path, sizeof(s->path), "%s", path);
  }
  s->pos = 0;
  return s->size > 0;
}

static inline bool mediaStreamOpenOk(const MediaStream* s){
  if(!s || s->kind == MSTREAM_NONE) return false;
  if(s->kind == MSTREAM_SD) return flexSdIsOpen(&s->sd);
  return true;
}

static int mediaIoRead(void* c, void* buf, uint32_t n){
  MediaStream* s = (MediaStream*)c;
  if(!s || n == 0) return 0;
  if(s->kind == MSTREAM_SD){
    int r = flexSdRead(&s->sd, buf, n);          // -1 si la tarjeta ya no esta
    if(r > 0) s->pos += (uint32_t)r;
    return r;
  }
  if(s->kind == MSTREAM_INT){
    int r = flexFsReadAt(s->path, s->pos, buf, n);
    if(r > 0) s->pos += (uint32_t)r;
    return r;
  }
  return -1;
}
static bool mediaIoSeek(void* c, uint32_t off){
  MediaStream* s = (MediaStream*)c;
  if(!s) return false;
  if(s->kind == MSTREAM_SD) return flexSdSeek(&s->sd, off);
  if(s->kind == MSTREAM_INT){
    if(off > s->size) return false;
    s->pos = off; return true;
  }
  return false;
}
static uint32_t mediaIoSize(void* c){
  MediaStream* s = (MediaStream*)c;
  return s ? s->size : 0;
}
static void mediaBindIO(FlexMediaIO* io, MediaStream* s){
  io->read = mediaIoRead; io->seek = mediaIoSeek; io->size = mediaIoSize; io->ctx = s;
}

// Lee un fichero entero a un buffer que pone el llamante. Devuelve
// los bytes leidos o -1. Es para ficheros PEQUENOS (una foto que ya
// se comprobo que cabe); lo grande va por MediaStream.
static int mediaReadWhole(const char* path, uint8_t* buf, uint32_t cap){
  if(!path || !buf) return -1;
  if(mediaIsSd(path)) return flexSdReadBin(path, buf, cap);
  return flexFsReadBin(path, buf, cap);
}

static uint32_t mediaFileSize(const char* path){
  if(mediaIsSd(path)) return (uint32_t)flexSdSize(path);
  return flexFsSize(path);
}

// -------------------------------------------------------------
//  RESERVA PARA MEDIOS
//  ------------------------------------------------------------
//  Todo lo grande va a PSRAM. Si no hay, se cae al heap interno,
//  pero solo para lo pequeno: las funciones que piden cientos de KB
//  comprueban el resultado y desactivan su funcion en vez de dejar
//  la placa sin memoria.
// -------------------------------------------------------------
static void* mediaAlloc(size_t n){
  void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : malloc(n);
}
static void mediaFree(void* p){ if(p) heap_caps_free(p); }

// -------------------------------------------------------------
//  INDICE DE MEDIOS
//  ------------------------------------------------------------
//  CAPACIDAD. 384 elementos a 120 bytes son ~46 KB de PSRAM. Es un
//  tope real, no "infinito": una tarjeta con 5.000 fotos no cabe
//  entera y la interfaz lo DICE (ixFull), en vez de ensenar 384 y
//  callar que hay mas.
//
//  CUANDO SE CONSTRUYE. Nunca al arrancar (nadie ha pedido ver la
//  galeria todavia) sino al abrir Galeria, Multimedia o
//  Almacenamiento, y al cambiar la tarjeta. Se avanza en el tick
//  del bucle principal con un presupuesto pequeno.
// -------------------------------------------------------------
#define MEDIA_INDEX_CAP    384
#define MEDIA_STEP_BUDGET   12     // entradas de directorio por vuelta de loop()

static FlexMediaIndex  gMedIx;
static FlexMediaItem*  gMedStore   = NULL;
static bool            gMedInited  = false;
static uint32_t        gMedScanGen = 0;      // generacion de SD del indice actual
static bool            gMedNotifyDone = false;

// ---- Puente de volumen para el indexador ----
// Traduce FlexFsEntry -> FlexMediaDirent. El indexador nunca ve las
// estructuras del sistema de archivos.
// Los dos volumenes entregan sus entradas por el MISMO camino: en
// orden fisico y saltando las ya vistas. Es lo que permite recorrer
// una carpeta de miles de fotos en lotes de ocho sin releerla entera
// en cada lote y sin un tope silencioso de elementos.
#define MED_BATCH 8
static int medListInt(void* ctx, const char* dir, FlexMediaDirent* out, int maxn, int skip){
  (void)ctx;
  if(!flexFsReady()) return -1;
  FlexFsEntry e[MED_BATCH];
  int want = maxn < MED_BATCH ? maxn : MED_BATCH;
  int n = flexFsListFrom(dir, e, want, skip);
  if(n < 0) return -1;
  for(int i = 0; i < n; i++){
    snprintf(out[i].name, FLEXMED_NAME_MAX, "%s", e[i].name);
    out[i].size = e[i].size;
    out[i].dir  = e[i].dir;
  }
  return n;
}
static bool medAliveInt(void* ctx){ (void)ctx; return flexFsReady(); }

static int medListSd(void* ctx, const char* dir, FlexMediaDirent* out, int maxn, int skip){
  (void)ctx;
  if(!flexSdReady()) return -1;
  FlexFsEntry e[MED_BATCH];
  int want = maxn < MED_BATCH ? maxn : MED_BATCH;
  int n = flexSdListFrom(dir, e, want, skip);
  if(n < 0) return -1;
  for(int i = 0; i < n; i++){
    snprintf(out[i].name, FLEXMED_NAME_MAX, "%s", e[i].name);
    out[i].size = e[i].size;
    out[i].dir  = e[i].dir;
  }
  return n;
}
static bool medAliveSd(void* ctx){ (void)ctx; return flexSdReady(); }

// Carpetas que se recorren. Las de Flex OS van primero para que lo
// propio aparezca antes; despues las carpetas de siempre de una
// tarjeta, que se INDEXAN pero no se tocan: ni se crean, ni se
// mueven ficheros, ni se escribe nada dentro.
static void mediaIndexSetRoots(){
  gMedIx.rootN = 0;
  flexMediaIndexAddRoot(&gMedIx, FLEXFS_DIR_DOCS,   FLEXMED_VOL_INT);
  flexMediaIndexAddRoot(&gMedIx, FLEXFS_DIR_PAINT,  FLEXMED_VOL_INT);
  if(flexSdReady()){
    flexMediaIndexAddRoot(&gMedIx, FLEXSD_DIR_PHOTOS, FLEXMED_VOL_SD);
    flexMediaIndexAddRoot(&gMedIx, FLEXSD_DIR_VIDEOS, FLEXMED_VOL_SD);
    flexMediaIndexAddRoot(&gMedIx, FLEXSD_DIR_AUDIO,  FLEXMED_VOL_SD);
    flexMediaIndexAddRoot(&gMedIx, "/sdcard/DCIM",     FLEXMED_VOL_SD);
    flexMediaIndexAddRoot(&gMedIx, "/sdcard/Download", FLEXMED_VOL_SD);
    flexMediaIndexAddRoot(&gMedIx, "/sdcard/Pictures", FLEXMED_VOL_SD);
    flexMediaIndexAddRoot(&gMedIx, "/sdcard/Movies",   FLEXMED_VOL_SD);
    flexMediaIndexAddRoot(&gMedIx, "/sdcard/Music",    FLEXMED_VOL_SD);
  }
}

static bool mediaIndexBegin(){
  if(!gMedInited){
    gMedStore = (FlexMediaItem*)mediaAlloc(sizeof(FlexMediaItem) * MEDIA_INDEX_CAP);
    if(!gMedStore){
      // Sin memoria para el indice no se finge una galeria vacia: se
      // deja el indice a capacidad 0 y las pantallas lo dicen.
      flexMediaIndexInit(&gMedIx, NULL, 0);
      gMedInited = true;
      return false;
    }
    flexMediaIndexInit(&gMedIx, gMedStore, MEDIA_INDEX_CAP);
    gMedInited = true;
  }
  gMedIx.volInt.list = medListInt; gMedIx.volInt.alive = medAliveInt; gMedIx.volInt.ctx = NULL;
  gMedIx.volSd.list  = medListSd;  gMedIx.volSd.alive  = medAliveSd;  gMedIx.volSd.ctx  = NULL;
  return gMedStore != NULL;
}

// Relanza el recorrido desde cero. Se llama al abrir una app de
// medios si la tarjeta cambio, y al cambiar la tarjeta.
static void mediaIndexRescan(){
  if(!mediaIndexBegin()) return;
  mediaIndexSetRoots();
  gMedScanGen    = flexSdGeneration();
  gMedNotifyDone = false;
  flexMediaIndexStart(&gMedIx, gMedScanGen);
}

// Marca el indice como caducado SIN recorrer nada: la proxima vez
// que se abra Galeria o Multimedia se reconstruye. La llaman las apps
// que CREAN o BORRAN archivos (Paint al guardar un dibujo, la
// papelera al vaciarse), que es lo que hace que lo que Flex OS
// produce aparezca en la Galeria sin que nadie tenga que refrescar.
static void mediaIndexInvalidate(){
  if(gMedInited) gMedIx.state = FLEXMED_SCAN_IDLE;
}

// ¿Hace falta reindexar? Solo si nunca se hizo o si la tarjeta que
// hay ahora no es la que se indexo.
static void mediaIndexEnsure(){
  if(!gMedInited || gMedScanGen != flexSdGeneration()
     || gMedIx.state == FLEXMED_SCAN_IDLE)
    mediaIndexRescan();
}

static inline bool mediaIndexBusy(){ return gMedIx.state == FLEXMED_SCAN_RUNNING; }

// Progreso REAL para la barra: entradas ya miradas. No se puede dar
// un porcentaje honesto (no se sabe cuantas hay hasta terminar), asi
// que se ensena el contador y no una barra que miente.
static inline uint32_t mediaIndexSeen(){ return gMedIx.seen; }
static inline int      mediaIndexN(){ return (int)gMedIx.n; }

// Un paso del recorrido. Lo llama loop(). No hace NADA si no hay un
// recorrido en curso, asi que el coste en reposo es una comparacion.
static void mediaIndexTick(){
  if(gMedIx.state != FLEXMED_SCAN_RUNNING) return;
  flexMediaIndexStep(&gMedIx, MEDIA_STEP_BUDGET);
  if(gMedIx.state == FLEXMED_SCAN_DONE && !gMedNotifyDone){
    gMedNotifyDone = true;
    // Solo se avisa si de verdad hay algo que contar y si la tarjeta
    // participo: un indice de dos dibujos internos no merece un aviso.
    int nsd = flexMediaIndexCount(&gMedIx, 0, FLEXMED_VOL_SD);
    if(nsd > 0){
      char sub[40];
      snprintf(sub, sizeof(sub), "%d en la tarjeta, %d en total",
               nsd, (int)gMedIx.n);
      mediaNotify(MOD_SDCARD, "Galer" "\xC3\xAD" "a actualizada", sub);
    }
  }
}

// -------------------------------------------------------------
//  TICK DE ALMACENAMIENTO EXTRAIBLE
//  ------------------------------------------------------------
//  Lo llama loop() en cada vuelta. Su coste en reposo es una
//  comparacion de millis() dentro de flexSdTick(): no lee la tarjeta
//  continuamente ni recorre nada.
//
//  Cuando el estado CAMBIA hace las tres cosas que hay que hacer y
//  no una mas: avisar por la isla, invalidar lo que ya no vale y
//  volver a indexar. Que este en un solo sitio es lo que garantiza
//  que meter o sacar la tarjeta se comporte igual venga de donde
//  venga -- Galeria, Multimedia, Almacenamiento o el escritorio.
// -------------------------------------------------------------
static void mediaStorageTick(){
  // SDMMC vive en el slot dedicado 0; esp-hosted/C6 usa el slot 1. Esa
  // separacion se fija en build_opt.h, por lo que el sondeo de la tarjeta no
  // depende del estado del Wi-Fi ni lo bloquea.
  if(flexSdTick()){
    if(flexSdReady()){
      char sub[40];
      char tot[24];
      flexFsFmtSize((uint32_t)(flexSdTotalBytes() / 1048576ull), tot, sizeof(tot));
      snprintf(sub, sizeof(sub), "%s · %s", flexSdCardTypeName(), flexSdFsName());
      mediaNotify(MOD_SDCARD, "Tarjeta SD lista", sub);
      mediaIndexRescan();                 // hay contenido nuevo que mirar
    } else {
      // La tarjeta se fue: lo suyo deja de existir en el indice al
      // instante. No se espera a que alguien abra la Galeria y se
      // encuentre con rutas que ya no llevan a ningun sitio.
      flexMediaIndexDropSd(&gMedIx);
      const char* why = flexSdError();
      mediaNotify(MOD_SDCARD, "Tarjeta SD retirada", why);
    }
  }
  mediaIndexTick();
}

// -------------------------------------------------------------
//  ORIENTACION
//  ------------------------------------------------------------
//  Tres modos, tal cual los pide el sistema: Auto, Vertical y
//  Horizontal. Lo importante es que NO hay un segundo motor de
//  rotacion: se usa el mismo gLand + putPhys que Modo PC y Juegos,
//  con el MISMO mapeo de tactil (lx = T.y, ly = SCR_W-1-T.x). Un
//  segundo motor seria la forma segura de acabar viendo la imagen
//  girada y el dedo respondiendo en vertical.
//
//  El modo manual se aplica al archivo ACTUAL y vuelve a Auto al
//  abrir otro, que es lo que se pidio: girar una foto concreta no
//  debe cambiar como se abren las siguientes.
// -------------------------------------------------------------
static uint8_t gMediaOriMode = MORI_AUTO;   // eleccion del usuario para el archivo actual
static bool    gMediaSquareLand = false;    // ultima orientacion usada con un archivo cuadrado

// Orientacion EFECTIVA de un medio de w x h. Devuelve true = horizontal.
static bool mediaOriLandscape(int w, int h){
  if(gMediaOriMode == MORI_PORT) return false;
  if(gMediaOriMode == MORI_LAND) return true;
  if(w <= 0 || h <= 0) return false;            // sin dimensiones: vertical
  if(w > h) return true;
  if(h > w) return false;
  return gMediaSquareLand;                      // cuadrado: la ultima de la sesion
}

// Coordenadas logicas del lienzo segun la orientacion en curso.
static inline int mediaCanvasW(bool land){ return land ? LW : SCR_W; }
static inline int mediaCanvasH(bool land){ return land ? LH : SCR_H; }

// Toque en coordenadas del lienzo logico. UN solo sitio hace la
// conversion: si el tactil y el dibujo se desalinean, es aqui y en
// ningun otro lado.
static inline void mediaTouchXY(bool land, int &x, int &y){
  if(land){ x = T.y; y = (SCR_W - 1) - T.x; }
  else    { x = T.x; y = T.y; }
}

// Ajuste PROPORCIONAL de un contenido de sw x sh dentro de una caja.
// Nunca deforma: sobra caja por un lado, no se estira la imagen.
static void mediaFitBox(int sw, int sh, int bw, int bh, int &ow, int &oh){
  if(sw <= 0 || sh <= 0 || bw <= 0 || bh <= 0){ ow = bw; oh = bh; return; }
  // Se compara sw/sh contra bw/bh con productos cruzados: sin coma
  // flotante y sin perder precision con imagenes grandes.
  if((int64_t)sw * bh > (int64_t)bw * sh){ ow = bw; oh = (int)((int64_t)sh * bw / sw); }
  else                                   { oh = bh; ow = (int)((int64_t)sw * bh / sh); }
  if(ow < 1) ow = 1;
  if(oh < 1) oh = 1;
}
