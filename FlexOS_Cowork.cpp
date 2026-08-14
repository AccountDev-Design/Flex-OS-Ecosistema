// #############################################################
// ##  FlexOS_Cowork.cpp  ·  MOTOR DE TRABAJOS EN SEGUNDO PLANO
// ##  Ver FlexOS_Cowork.h para el contrato. Aqui, el como.
// #############################################################

#include "FlexOS_Cowork.h"

#include <string.h>

// =============================================================
//  ESTADO DEL MOTOR
//  -------------------------------------------------------------
//  El almacen lo aporta el llamante (PSRAM en la placa, un array
//  estatico en las pruebas). El motor solo guarda el puntero, el
//  tamano y el contador de ids. Cero reservas dinamicas aqui
//  dentro: en un sistema que ya tiene la PSRAM repartida entre
//  framebuffers, quien decide donde vive esto es el .ino.
// =============================================================
static CoworkJob* gCw    = NULL;
static int        gCwCap = 0;
static int        gCwN   = 0;
static uint32_t   gCwSeq = 0;      // siguiente id; nunca vuelve atras

void coworkBegin(){
  gCwN = 0;
  // El contador de ids NO se reinicia a proposito: dentro de una
  // sesion, un id que ya se uso no se vuelve a usar aunque se vacie
  // la cola. Una notificacion en vuelo que apunte a un trabajo
  // borrado tiene que fallar al buscarlo, no encontrar OTRO trabajo
  // distinto que casualmente reciclo el numero.
  if(gCw && gCwCap > 0) memset(gCw, 0, sizeof(CoworkJob) * (size_t)gCwCap);
}

void coworkAttachStorage(CoworkJob* slots, int n){
  gCw    = slots;
  gCwCap = (slots && n > 0) ? n : 0;
  if(gCwCap > CW_MAX_JOBS) gCwCap = CW_MAX_JOBS;
  gCwN   = 0;
  if(gCw && gCwCap > 0) memset(gCw, 0, sizeof(CoworkJob) * (size_t)gCwCap);
}

bool coworkReady(){ return gCw != NULL && gCwCap > 0; }

// =============================================================
//  HUELLA DEL DOCUMENTO
// =============================================================
uint32_t coworkHash(const char* s, size_t n){
  uint32_t h = 2166136261u;                  // FNV-1a de 32 bits
  if(!s) return h;
  for(size_t i = 0; i < n && s[i]; i++){
    h ^= (uint8_t)s[i];
    h *= 16777619u;
  }
  // Un hash que salga 0 seria indistinguible de "no hay documento de
  // origen", que es lo que significa 0 en la API. Se desplaza a 1: la
  // probabilidad es de 1 entre 4.300 millones, pero la ambiguedad
  // costaria un resultado aplicado sobre un documento cambiado.
  return h ? h : 1u;
}

// =============================================================
//  UTILIDADES INTERNAS
// =============================================================
static void cwCopy(char* dst, size_t cap, const char* src){
  if(!dst || cap == 0) return;
  if(!src){ dst[0] = 0; return; }
  size_t l = strlen(src);
  if(l > cap - 1) l = cap - 1;
  memcpy(dst, src, l);
  dst[l] = 0;
}

static inline bool cwLive(const CoworkJob* j){
  return j->state != CW_CANCELLED;
}
static inline bool cwFinished(const CoworkJob* j){
  return j->state == CW_DONE || j->state == CW_ERROR || j->state == CW_CANCELLED;
}

// Quita la ranura idx compactando. Es la unica via por la que un
// trabajo desaparece de la lista.
static void cwRemove(int idx){
  if(idx < 0 || idx >= gCwN) return;
  for(int i = idx; i < gCwN - 1; i++) gCw[i] = gCw[i + 1];
  gCwN--;
  memset(&gCw[gCwN], 0, sizeof(CoworkJob));
}

// Hace sitio para uno nuevo descartando historial. Criterio, por este
// orden: primero un terminado YA VISTO (el mas antiguo), y solo si no
// hay ninguno, el terminado mas antiguo aunque no se haya visto. Un
// trabajo ACTIVO no se descarta jamas -- antes se rechaza el nuevo.
static bool cwMakeRoom(){
  if(gCwN < gCwCap) return true;
  int best = -1;
  for(int i = 0; i < gCwN; i++){
    if(!cwFinished(&gCw[i]) || !gCw[i].seen) continue;
    if(best < 0 || gCw[i].bornMs < gCw[best].bornMs) best = i;
  }
  if(best < 0) for(int i = 0; i < gCwN; i++){
    if(!cwFinished(&gCw[i])) continue;
    if(best < 0 || gCw[i].bornMs < gCw[best].bornMs) best = i;
  }
  if(best < 0) return false;                 // todo activo: la cola esta llena de verdad
  cwRemove(best);
  return true;
}

// =============================================================
//  ENCOLAR
// =============================================================
uint32_t coworkSubmit(uint8_t kind, const char* title,
                      const char* in, size_t inLen,
                      const char* src, uint32_t srcHash,
                      uint32_t budgetMs, uint32_t nowMs){
  if(!coworkReady()) return 0;
  if(kind >= CW_KIND_N) return 0;
  if(inLen > CW_IN_MAX - 1) return 0;        // demasiado grande: se dice, no se trunca en silencio
  if(!cwMakeRoom()) return 0;

  CoworkJob* j = &gCw[gCwN++];
  memset(j, 0, sizeof(*j));
  j->id       = ++gCwSeq;
  j->kind     = kind;
  j->state    = CW_QUEUED;
  j->progress = 0xFF;                        // hasta que alguien pueda calcularlo de verdad
  j->bornMs   = nowMs;
  j->budgetMs = budgetMs ? budgetMs : CW_DEFAULT_BUDGET_MS;
  j->srcHash  = srcHash;
  cwCopy(j->title, sizeof(j->title), title);
  cwCopy(j->src,   sizeof(j->src),   src);
  if(in && inLen > 0){
    memcpy(j->in, in, inLen);
    j->in[inLen] = 0;
    j->inLen = (uint16_t)inLen;
  } else {
    j->in[0] = 0;
    j->inLen = 0;
  }
  return j->id;
}

// =============================================================
//  CONSULTA
// =============================================================
int coworkCount(){ return gCwN; }

CoworkJob* coworkAt(int i){
  if(!coworkReady() || i < 0 || i >= gCwN) return NULL;
  return &gCw[i];
}

CoworkJob* coworkFind(uint32_t id){
  if(!coworkReady() || id == 0) return NULL;
  for(int i = 0; i < gCwN; i++) if(gCw[i].id == id) return &gCw[i];
  return NULL;
}

int coworkActiveCount(){
  int n = 0;
  for(int i = 0; i < gCwN; i++) if(!cwFinished(&gCw[i])) n++;
  return n;
}

int coworkUnseenCount(){
  int n = 0;
  for(int i = 0; i < gCwN; i++) if(gCw[i].state == CW_DONE && !gCw[i].seen) n++;
  return n;
}

const char* coworkStateName(uint8_t st){
  switch(st){
    case CW_QUEUED:    return "En cola";
    case CW_PREP:      return "Preparando";
    case CW_WORKING:   return "Trabajando";
    case CW_WAITING:   return "Esperando servidor";
    case CW_PAUSED:    return "Pausado";
    case CW_DONE:      return "Terminado";
    case CW_ERROR:     return "Error";
    case CW_CANCELLED: return "Cancelado";
    default:           return "Desconocido";
  }
}

const char* coworkKindName(uint8_t kind){
  switch(kind){
    case CW_KIND_CORRECT:   return "Corregir";
    case CW_KIND_SUMMARY:   return "Resumir";
    case CW_KIND_TRANSLATE: return "Traducir";
    case CW_KIND_TONE:      return "Cambiar tono";
    case CW_KIND_EXPLAIN:   return "Explicar error";
    case CW_KIND_ANALYZE:   return "Analizar";
    case CW_KIND_IMAGE:     return "Analizar imagen";
    case CW_KIND_FIND:      return "Buscar archivos";
    default:                return "Tarea";
  }
}

CoworkJob* coworkNextForNetwork(uint32_t nowMs){
  if(!coworkReady()) return NULL;
  // UNA sola operacion pesada de red a la vez: si ya hay alguien
  // trabajando o esperando al servidor, no se saca a nadie mas.
  for(int i = 0; i < gCwN; i++)
    if(gCw[i].state == CW_WORKING || gCw[i].state == CW_WAITING || gCw[i].state == CW_PREP) return NULL;
  CoworkJob* best = NULL;
  for(int i = 0; i < gCwN; i++){
    CoworkJob* j = &gCw[i];
    if(j->state != CW_QUEUED) continue;
    if(j->retryAtMs && (int32_t)(nowMs - j->retryAtMs) < 0) continue;   // aun en su espera
    if(!best || j->bornMs < best->bornMs) best = j;                     // el mas antiguo primero
  }
  return best;
}

// =============================================================
//  CONTROL
// =============================================================
bool coworkCancel(uint32_t id){
  CoworkJob* j = coworkFind(id);
  if(!j) return false;
  if(j->state == CW_DONE || j->state == CW_CANCELLED) return false;
  j->state    = CW_CANCELLED;
  j->progress = 0xFF;
  // El snapshot se borra AQUI, no al eliminar el trabajo: cancelar
  // significa que ese contenido ya no hace falta, y si venia de Flex
  // Vault no puede quedarse en memoria esperando a que alguien limpie
  // el historial. Ver la regla de "no conservar copias temporales".
  memset(j->in, 0, sizeof(j->in));
  j->inLen = 0;
  return true;
}

bool coworkPause(uint32_t id, uint8_t why){
  CoworkJob* j = coworkFind(id);
  if(!j) return false;
  if(cwFinished(j) || j->state == CW_PAUSED) return false;
  j->state    = CW_PAUSED;
  j->pauseWhy = why;
  return true;
}

bool coworkResume(uint32_t id){
  CoworkJob* j = coworkFind(id);
  if(!j || j->state != CW_PAUSED) return false;
  j->state    = CW_QUEUED;
  j->pauseWhy = CW_PAUSE_NONE;
  return true;
}

bool coworkMarkSeen(uint32_t id){
  CoworkJob* j = coworkFind(id);
  if(!j) return false;
  j->seen = true;
  return true;
}

bool coworkDelete(uint32_t id){
  if(!coworkReady()) return false;
  for(int i = 0; i < gCwN; i++){
    if(gCw[i].id != id) continue;
    // Borrar de verdad: el contenido se pisa antes de soltar la
    // ranura. Un resultado puede venir de un documento privado.
    memset(&gCw[i], 0, sizeof(CoworkJob));
    cwRemove(i);
    return true;
  }
  return false;
}

void coworkPauseAll(uint8_t why){
  for(int i = 0; i < gCwN; i++){
    CoworkJob* j = &gCw[i];
    if(cwFinished(j) || j->state == CW_PAUSED) continue;
    j->state    = CW_PAUSED;
    j->pauseWhy = why;
  }
}

void coworkResumeAll(uint8_t why){
  for(int i = 0; i < gCwN; i++){
    CoworkJob* j = &gCw[i];
    // Solo los pausados POR ESE motivo: si el usuario paro una tarea
    // a mano, cerrar la camara no se la reanuda por la espalda.
    if(j->state != CW_PAUSED || j->pauseWhy != why) continue;
    j->state    = CW_QUEUED;
    j->pauseWhy = CW_PAUSE_NONE;
  }
}

bool coworkAnyPaused(uint8_t why){
  for(int i = 0; i < gCwN; i++)
    if(gCw[i].state == CW_PAUSED && gCw[i].pauseWhy == why) return true;
  return false;
}

// =============================================================
//  TRANSICIONES QUE DISPARA EL TRABAJADOR
// =============================================================
void coworkBeginWork(uint32_t id, uint32_t nowMs){
  CoworkJob* j = coworkFind(id);
  if(!j || cwFinished(j) || j->state == CW_PAUSED) return;
  if(j->startMs == 0) j->startMs = nowMs;
  j->state    = CW_WORKING;
  j->progress = 0;
}

void coworkWaitServer(uint32_t id, uint32_t nowMs){
  CoworkJob* j = coworkFind(id);
  if(!j || cwFinished(j) || j->state == CW_PAUSED) return;
  if(j->startMs == 0) j->startMs = nowMs;
  j->state = CW_WAITING;
}

void coworkProgress(uint32_t id, uint8_t pct){
  CoworkJob* j = coworkFind(id);
  if(!j || cwFinished(j)) return;
  if(pct != 0xFF && pct > 100) pct = 100;
  j->progress = pct;
}

void coworkFinish(uint32_t id, const char* out, size_t outLen,
                  uint32_t srcHashNow, bool needsConfirm, uint32_t nowMs){
  CoworkJob* j = coworkFind(id);
  if(!j || j->state == CW_CANCELLED) return;
  if(outLen > CW_OUT_MAX - 1) outLen = CW_OUT_MAX - 1;
  if(out && outLen > 0){ memcpy(j->out, out, outLen); j->out[outLen] = 0; j->outLen = (uint16_t)outLen; }
  else { j->out[0] = 0; j->outLen = 0; }

  // LA COMPROBACION QUE IMPORTA. El snapshot se tomo con srcHash; si
  // el documento de origen ya no tiene esa huella, el usuario escribio
  // encima mientras Cowork trabajaba. El resultado NO se pierde -- se
  // marca, y la UI ofrece comparar en vez de aplicar a ciegas.
  j->srcChanged   = (j->srcHash != 0 && srcHashNow != 0 && srcHashNow != j->srcHash);
  j->needsConfirm = needsConfirm || j->srcChanged;
  j->state        = CW_DONE;
  j->progress     = 100;
  j->err          = CW_ERR_NONE;
  j->err_[0]      = 0;
  j->seen         = false;
  (void)nowMs;

  // El snapshot ya no hace falta: lo que se conserva es el RESULTADO.
  // Soltarlo aqui es lo que evita que un texto de Flex Vault siga en
  // memoria despues de terminar (ver la regla de privacidad).
  memset(j->in, 0, sizeof(j->in));
  j->inLen = 0;
}

// Que errores merecen otro intento. Un fallo de configuracion o un
// permiso denegado no se arreglan repitiendo: reintentarlos solo
// gastaria bateria y radio para volver a fallar igual.
static bool cwRetryable(uint8_t e){
  return e == CW_ERR_NONET || e == CW_ERR_HTTP || e == CW_ERR_TIMEOUT || e == CW_ERR_MEM;
}

void coworkFail(uint32_t id, uint8_t errCode, const char* msg, uint32_t nowMs){
  CoworkJob* j = coworkFind(id);
  if(!j || j->state == CW_CANCELLED) return;
  j->tries++;
  cwCopy(j->err_, sizeof(j->err_), msg);
  j->err = errCode;
  if(cwRetryable(errCode) && j->tries < CW_MAX_TRIES){
    uint32_t wait = CW_BACKOFF_BASE_MS;
    for(uint8_t k = 1; k < j->tries; k++) wait *= CW_BACKOFF_FACTOR;
    j->state     = CW_QUEUED;
    j->retryAtMs = nowMs + wait;
    j->progress  = 0xFF;
    // EL PRESUPUESTO ES POR INTENTO, y por eso el cronometro se pone
    // a cero aqui. Sin esta linea, un trabajo que agota su tiempo
    // maximo vuelve a la cola con startMs viejo: el siguiente
    // coworkPump ve otra vez el presupuesto pasado y lo vuelve a
    // matar en el acto, asi que los tres intentos se quemarian
    // seguidos sin llegar a salir ni una vez mas. El resultado seria
    // un "reintento" que en realidad nunca reintenta nada.
    j->startMs = 0;
    return;
  }
  j->state    = CW_ERROR;
  j->progress = 0xFF;
  j->seen     = false;
  memset(j->in, 0, sizeof(j->in));
  j->inLen = 0;
}

// =============================================================
//  AVANCE
// =============================================================
int coworkPump(uint32_t nowMs){
  if(!coworkReady()) return 0;
  int changed = 0;
  for(int i = 0; i < gCwN; i++){
    CoworkJob* j = &gCw[i];
    if(cwFinished(j) || j->state == CW_PAUSED) continue;

    // TIEMPO MAXIMO. Se cuenta desde que el trabajo EMPEZO, no desde
    // que se encolo: un trabajo que lleva media hora en cola porque no
    // habia red no ha gastado su presupuesto -- no ha trabajado.
    if(j->startMs != 0 && (uint32_t)(nowMs - j->startMs) > j->budgetMs){
      coworkFail(j->id, CW_ERR_TIMEOUT, "Se agoto el tiempo maximo", nowMs);
      changed++;
      continue;
    }
    // Fin de una espera de reintento: vuelve a estar listo para salir.
    if(j->state == CW_QUEUED && j->retryAtMs && (int32_t)(nowMs - j->retryAtMs) >= 0){
      j->retryAtMs = 0;
      changed++;
    }
  }
  return changed;
}

// =============================================================
//  PERSISTENCIA
//  -------------------------------------------------------------
//  Formato: cabecera (magic, version, contador) y luego un registro
//  por trabajo TERMINADO con longitudes explicitas. Se escriben
//  campo a campo y no un memcpy de la estructura entera a proposito:
//  volcar la struct grabaria tambien el relleno del compilador y
//  ataria el fichero a la disposicion de memoria de ESTA
//  compilacion. Un cambio de orden de campos manana dejaria el
//  historial ilegible sin que nadie se diera cuenta.
// =============================================================
static void cwPut32(uint8_t* p, uint32_t v){
  p[0] = (uint8_t)(v      ); p[1] = (uint8_t)(v >>  8);
  p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint32_t cwGet32(const uint8_t* p){
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

size_t coworkExport(uint8_t* out, size_t n){
  if(!out || n < 12 || !coworkReady()) return 0;
  size_t o = 0;
  cwPut32(out + o, CW_BLOB_MAGIC);   o += 4;
  cwPut32(out + o, CW_BLOB_VERSION); o += 4;
  size_t cntAt = o;                  o += 4;      // el contador se rellena al final
  uint32_t cnt = 0;

  for(int i = 0; i < gCwN; i++){
    const CoworkJob* j = &gCw[i];
    if(j->state != CW_DONE) continue;             // solo lo terminado sobrevive al reinicio
    size_t tl = strlen(j->title), sl = strlen(j->src), ol = j->outLen;
    size_t need = 4 + 1 + 1 + 1 + 2 + tl + 2 + sl + 2 + ol;
    if(o + need > n) break;                       // no cabe mas: se corta en registro entero
    cwPut32(out + o, j->id); o += 4;
    out[o++] = j->kind;
    out[o++] = (uint8_t)(j->seen ? 1 : 0);
    out[o++] = (uint8_t)((j->srcChanged ? 1 : 0) | (j->needsConfirm ? 2 : 0));
    out[o++] = (uint8_t)(tl      ); out[o++] = (uint8_t)(tl >> 8);
    memcpy(out + o, j->title, tl); o += tl;
    out[o++] = (uint8_t)(sl      ); out[o++] = (uint8_t)(sl >> 8);
    memcpy(out + o, j->src, sl);   o += sl;
    out[o++] = (uint8_t)(ol      ); out[o++] = (uint8_t)(ol >> 8);
    memcpy(out + o, j->out, ol);   o += ol;
    cnt++;
    if(cnt >= CW_HIST_MAX) break;
  }
  cwPut32(out + cntAt, cnt);
  return o;
}

bool coworkImport(const uint8_t* blob, size_t n){
  if(!coworkReady() || !blob || n < 12) return false;
  if(cwGet32(blob) != CW_BLOB_MAGIC) return false;
  if(cwGet32(blob + 4) != CW_BLOB_VERSION) return false;
  uint32_t cnt = cwGet32(blob + 8);
  size_t o = 12;
  uint32_t maxId = gCwSeq;

  for(uint32_t k = 0; k < cnt && gCwN < gCwCap; k++){
    if(o + 7 > n) return false;
    uint32_t id = cwGet32(blob + o); o += 4;
    uint8_t kind  = blob[o++];
    uint8_t seen  = blob[o++];
    uint8_t flags = blob[o++];
    if(kind >= CW_KIND_N) return false;

    // Cada longitud se valida CONTRA LO QUE QUEDA del blob antes de
    // leer un solo byte. Este fichero lo escribimos nosotros, pero
    // tambien puede llegar truncado por un corte de corriente a mitad
    // de escritura -- y un largo de 65535 leido de ahi seria una
    // lectura fuera de rango en un buffer de 1 KB.
    if(o + 2 > n) return false;
    size_t tl = (size_t)blob[o] | ((size_t)blob[o + 1] << 8); o += 2;
    if(tl >= CW_TITLE_MAX || o + tl > n) return false;
    const uint8_t* title = blob + o; o += tl;

    if(o + 2 > n) return false;
    size_t sl = (size_t)blob[o] | ((size_t)blob[o + 1] << 8); o += 2;
    if(sl >= CW_SRC_MAX || o + sl > n) return false;
    const uint8_t* src = blob + o; o += sl;

    if(o + 2 > n) return false;
    size_t ol = (size_t)blob[o] | ((size_t)blob[o + 1] << 8); o += 2;
    if(ol >= CW_OUT_MAX || o + ol > n) return false;
    const uint8_t* outp = blob + o; o += ol;

    CoworkJob* j = &gCw[gCwN++];
    memset(j, 0, sizeof(*j));
    j->id           = id;
    j->kind         = kind;
    j->state        = CW_DONE;
    j->progress     = 100;
    j->seen         = (seen != 0);
    j->srcChanged   = (flags & 1) != 0;
    j->needsConfirm = (flags & 2) != 0;
    memcpy(j->title, title, tl); j->title[tl] = 0;
    memcpy(j->src,   src,   sl); j->src[sl]   = 0;
    memcpy(j->out,   outp,  ol); j->out[ol]   = 0;
    j->outLen = (uint16_t)ol;
    if(id > maxId) maxId = id;
  }
  // El contador de ids se coloca por encima del mayor id restaurado:
  // sin esto, el primer trabajo de la sesion nueva reutilizaria un id
  // que ya esta en el historial y coworkFind() devolveria el trabajo
  // equivocado.
  gCwSeq = maxId;
  return true;
}
