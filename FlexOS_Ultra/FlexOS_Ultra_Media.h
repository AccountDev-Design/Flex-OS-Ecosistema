// #############################################################
// ##  FLEX OS ULTRA  ·  NUCLEO DE MEDIOS  ·  indice compartido
// ##  ----------------------------------------------------------
// ##  Lo que comparten Galeria, Multimedia y el Explorador:
// ##  clasificacion de ficheros e indice LittleFS por lotes.
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
// ##    · almacenamiento interno LittleFS,
// ##    · un lector de ficheros unico,
// ##    · el indice de medios, que se construye por lotes desde
// ##      el bucle principal y nunca bloquea,
// ##    · la cache de miniaturas,
// ##    · la orientacion Auto/Vertical/Horizontal,
// ##    · y el puente con la isla de notificaciones.
// ##
// ##  POR QUE AQUI Y NO EN CADA APP. Antes de esto, la Galeria
// ##  tenia su propio recorrido de carpetas y su propio
// ##  decodificador de miniatura, y Multimedia una fuente
// ##  sintetica sin relacion con ninguna de las dos. Mantener eso
// ##  duplicado haria divergir ambas apps. Aqui hay UN
// ##  indice y UN lector.
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
//  ALMACENAMIENTO INTERNO
// -------------------------------------------------------------
static bool mediaVolReady(const char* path){
  (void)path;
  return flexFsReady();
}

static const char* mediaVolName(const char* path){
  (void)path;
  return "Memoria interna";
}

static int mediaList(const char* dir, FlexFsEntry* out, int maxn){
  return flexFsReady() ? flexFsList(dir, out, maxn) : -1;
}

// -------------------------------------------------------------
//  LECTOR UNIFICADO (MediaStream + FlexMediaIO)
// -------------------------------------------------------------
static void mediaStreamClose(MediaStream* s){
  if(!s) return;
  if(s->f){ flexFsStreamClose(s->f); s->f = NULL; }
  s->kind = MSTREAM_NONE;
  s->path[0] = 0;
  s->pos = s->size = s->fpos = 0;
}

static bool mediaStreamOpen(MediaStream* s, const char* path){
  if(!s || !path) return false;
  mediaStreamClose(s);
  if(!flexFsReady()) return false;
  s->f = flexFsOpenRead(path);
  if(!s->f) return false;
  s->kind = MSTREAM_INT;
  s->size = flexFsStreamSize(s->f);
  snprintf(s->path, sizeof(s->path), "%s", path);
  s->pos = s->fpos = 0;
  if(!s->size){ mediaStreamClose(s); return false; }
  return true;
}

static inline bool mediaStreamOpenOk(const MediaStream* s){
  return s && s->kind == MSTREAM_INT && s->f;
}

static int mediaIoRead(void* c, void* buf, uint32_t n){
  MediaStream* s = (MediaStream*)c;
  if(!s || n == 0) return 0;
  if(s->kind != MSTREAM_INT || !s->f) return -1;
  // Solo se mueve el flujo si la lectura no sigue donde quedo la anterior:
  // la lectura secuencial (cabecera y datos del fotograma) no paga seeks.
  if(s->fpos != s->pos){
    if(!flexFsStreamSeek(s->f, s->pos)){ s->fpos = 0xFFFFFFFFu; return -1; }
    s->fpos = s->pos;
  }
  int r = flexFsStreamRead(s->f, buf, n);
  if(r > 0){ s->pos += (uint32_t)r; s->fpos = s->pos; }
  else s->fpos = 0xFFFFFFFFu;                  // estado desconocido: la proxima lectura lo recoloca
  return r;
}
static bool mediaIoSeek(void* c, uint32_t off){
  MediaStream* s = (MediaStream*)c;
  if(!s) return false;
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
  return flexFsReadBin(path, buf, cap);
}

static uint32_t mediaFileSize(const char* path){
  return flexFsSize(path);
}

// -------------------------------------------------------------
//  RESERVA PARA MEDIOS
//  ------------------------------------------------------------
//  Todo va a PSRAM. Si la PSRAM no da (llena o troceada), SOLO lo
//  pequeno puede caer a la RAM interna, y nunca dejandola por debajo
//  de un suelo: de ella viven la Wi-Fi (esp-hosted), lwIP y las pilas
//  de las tareas, y el P4 arranca con ~84 KB libres. Antes caia
//  CUALQUIER tamano -- un buffer de decodificacion de 60 KB en mitad de
//  una transferencia podia comerse la RAM de la red y tumbar el
//  sistema lejos de aqui. Quien pide algo grande recibe NULL y lo
//  gestiona (todos los llamantes ya lo comprueban).
// -------------------------------------------------------------
#define MEDIA_INTERNAL_MAX    (4u * 1024u)      // lo mas grande que puede caer a la RAM interna
#define MEDIA_INTERNAL_FLOOR  (48u * 1024u)     // RAM interna que nunca se toca
static void* mediaAlloc(size_t n){
  void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(p || n == 0 || n > MEDIA_INTERNAL_MAX) return p;
  if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < n + MEDIA_INTERNAL_FLOOR) return NULL;
  return heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
static void mediaFree(void* p){ if(p) heap_caps_free(p); }

// (El indice por recorrido de carpetas que vivia aqui se retiro: Galeria,
// Multimedia y Musica leen el catalogo de la biblioteca de medios,
// FlexOS_Ultra_MediaLib.h, que reconcilia en su propia tarea de fondo.)

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
