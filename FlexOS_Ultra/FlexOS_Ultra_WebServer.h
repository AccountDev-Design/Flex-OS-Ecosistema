// #############################################################
// ##  FLEX OS ULTRA  ·  FLEX WEB SERVER  ·  la mitad de placa
// ##  ----------------------------------------------------------
// ##  Sockets, LittleFS, la tarea del servidor y la hoja "Conectar
// ##  con el movil". Lo que el servidor DECIDE vive en FlexOS_MediaWeb
// ##  (portable, probado de extremo a extremo en el PC); aqui solo se
// ##  le da una red, un disco y un catalogo.
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
#include "FlexOS_Ultra_System.h"   // eslabon anterior de la cadena
#include "FlexOS_MediaWeb.h"
#include "FlexOS_QR.h"

// #############################################################
// ##  QUIEN HACE QUE
// ##  ------------------------------------------------------
// ##  · Tarea "flexWeb" (nucleo 1, prioridad 1, como el resto de la
// ##    red): escucha en el 8080 y atiende UNA conexion cada vez con
// ##    flexWebServeConn(). Nunca toca la pantalla.
// ##  · loopTask: enciende y apaga el servidor, pinta la hoja y
// ##    entrega los avisos (webTick). Nunca toca un socket.
// ##  Entre los dos, solo: gWebEv (cola de eventos de transferencia) y
// ##  unas pocas banderas. El catalogo es el almacen de las apps (gMs),
// ##  con su propio cerrojo.
// ##
// ##  SEGURIDAD. El servidor SOLO arranca si el usuario lo enciende en
// ##  la hoja, y se apaga si se va la Wi-Fi. Cada arranque genera un
// ##  codigo nuevo. Al bloquearse el P4, ninguna sesion conserva el
// ##  acceso al contenido protegido (flexWebDropOwners).
// #############################################################
// 14 KB: lo mas hondo de esta tarea es validar una foto recien subida
// (decodificador + codificador de la miniatura) con LittleFS o el socket por
// debajo. Medido en el PC (-fstack-usage, sin inline) esa cadena ronda los
// 6-7 KB; con 12 KB el margen para LittleFS, lwIP y newlib era justo. La
// tarea solo existe con el servidor encendido, y mlStackCheck avisa por
// Serie si alguna vez el margen baja de 2 KB.
#define WEB_TASK_STACK   14336
#define WEB_EV_N         24

enum { WEBS_OFF = 0, WEBS_NOWIFI, WEBS_STARTING, WEBS_ON, WEBS_FAIL };

static FlexWebCtx        gWebCtx;
static TaskHandle_t      gWebTask   = NULL;
static volatile bool     gWebWant   = false;     // el usuario lo quiere encendido
static volatile bool     gWebStop   = false;     // la tarea debe salir
static volatile uint8_t  gWebState  = WEBS_OFF;
static uint8_t*          gWebHdr    = NULL;
static uint8_t*          gWebIo     = NULL;
static char              gWebUrl[64] = "";
static bool              gWebLockSeen = false;   // ya se soltaron los propietarios en este bloqueo

// ---- Cola de eventos: la escribe la tarea del servidor, la lee loopTask ----
static FlexWebXfer*      gWebEv = NULL;          // WEB_EV_N plazas en PSRAM (4 KB que la RAM interna no necesita)
static volatile uint8_t  gWebEvW = 0, gWebEvR = 0;

// ---- Sistema de archivos: los mismos adaptadores que el almacen ----
static uint32_t wfsTotal(void*){ return flexFsTotalBytes(); }
static uint32_t wfsFree(void*){ uint32_t t = flexFsTotalBytes(), u = flexFsUsedBytes(); return u >= t ? 0 : t - u; }

// ---- Anfitrion: el catalogo es el MISMO almacen que usan las apps ----
static int whSnapshot(void*, FlexMlRec* d, int cap, uint32_t* rev){ return flexMsSnapshot(&gMs, d, cap, rev); }
static bool whGet(void*, uint32_t id, FlexMlRec* o){ return flexMsGet(&gMs, id, o); }
static uint32_t whRev(void*){ return flexMsRev(&gMs); }
static uint32_t whDup(void*, uint32_t size, uint32_t crc){ return flexMsFindDup(&gMs, size, crc); }
// Publica una subida ya comprobada: la mueve a su carpeta y la registra,
// con el cerrojo tomado de principio a fin (el recorrido del disco nunca
// ve el archivo "sin registro" ni puede registrarlo dos veces).
static uint32_t whCommit(void*, const FlexWebUpload* up, char* why, size_t cap){
  FlexMsUpload u;
  u.tmpPath = up->tmpPath; u.thumbTmp = up->thumbTmp;
  u.name = up->name; u.title = up->title; u.artist = up->artist; u.album = up->album;
  u.kind = up->kind; u.fmt = up->fmt; u.playable = up->playable;
  u.size = up->size; u.crc = up->crc; u.created = up->created; u.durMs = up->durMs; u.w = up->w; u.h = up->h;
  uint32_t id = flexMsCommitUpload(&gMs, &u, why, cap);
  mlBurstNote();                                  // puede venir otra: el guardado espera a la rafaga
  mlWake();                                       // guardar el catalogo
  return id;
}
// Miniatura que aporta el movil (ya normalizada por el servidor a 132x132).
static bool whSetThumb(void*, uint32_t id, const char* tmp){
  bool ok = flexMsSetExtThumb(&gMs, id, tmp);
  mlWake();
  return ok;
}
static void whThumbPath(void*, const FlexMlRec* r, char* out, size_t cap){ flexMsThumbPath(r, out, cap); }
// La clave del sistema, comprobada contra su hash SIN tocar la verificacion
// que la pantalla pudiera tener a medias (ver flexLockVerifyAlone).
static bool whVerify(void*, const char* secret){ return flexLockVerifyAlone(secret); }
static int whLockType(void*){ return gLockType; }
static uint32_t whNow(void*){ return millis(); }
static uint32_t whEpoch(void*){ return clkNowUtc(); }
static void whRandom(void*, uint8_t* out, size_t n){ flexLockRandomBytes(out, n); }
// Cola de un solo productor y un solo consumidor: esta tarea SOLO mueve
// gWebEvW y loopTask SOLO mueve gWebEvR. Un PROGRESO se puede perder (llega
// otro); por eso deja libres las ultimas plazas para los inicios y finales.
#define WEB_EV_KEEP 6
// El evento se copia ANTES de publicar el indice (release) y loopTask lo lee
// DESPUES de ver el indice (acquire): sin eso, el otro nucleo podia leer una
// plaza a medio copiar.
static void whEvent(void*, const FlexWebXfer* x){
  if(x->ev == FLEXWEB_EV_UP_START || x->ev == FLEXWEB_EV_UP_PROGRESS || x->ev == FLEXWEB_EV_UP_CHECK)
    mlBurstNote();                                // hay una subida en curso (ver mlSaveDue)
  if(!gWebEv) return;                             // webStart no deja arrancar sin ella
  uint8_t w = gWebEvW, used = (uint8_t)(w - __atomic_load_n(&gWebEvR, __ATOMIC_ACQUIRE));
  bool progress = x->ev == FLEXWEB_EV_UP_PROGRESS || x->ev == FLEXWEB_EV_DL_PROGRESS;
  if(used >= WEB_EV_N || (progress && used >= WEB_EV_N - WEB_EV_KEEP)) return;
  gWebEv[w % WEB_EV_N] = *x;
  __atomic_store_n(&gWebEvW, (uint8_t)(w + 1), __ATOMIC_RELEASE);
}
static void whYield(void*){ vTaskDelay(1); }
// Validar una subida es trabajo pesado: se espera su turno (la tarea de
// miniaturas o el visor lo sueltan en segundos). Solo si en un minuto no
// llega, la subida se rechaza con 503 y el movil la reintenta sola.
static bool whHeavyBegin(void*){ return mediaHeavyBegin(60000); }
static void whHeavyEnd(void*){ mediaHeavyEnd(); }

// ---- Conexion (WiFiClient) ----
static WiFiServer* gWebSrv = NULL;
static bool whOthers(void*){ return gWebSrv && gWebSrv->hasClient(); }
static int  wcRead(void* ctx, uint8_t* buf, size_t n, uint32_t ms);
static bool wcWrite(void* ctx, const uint8_t* b, size_t n);

// #############################################################
// ##  FLEX STORAGE en el servidor de siempre (docs/FLEX-STORAGE.md)
// ##  ------------------------------------------------------
// ##  Lo que el servidor decide vive en FlexOS_MediaWeb (probado en el PC);
// ##  aqui solo se le da: una conexion con el telefono emparejado
// ##  (FlexOS_StorageLink), el emparejamiento, las transferencias del gestor
// ##  de la nube y las operaciones de la biblioteca hechas en loopTask.
// #############################################################
// Pasarela /api/cloud: una conexion TCP con el telefono por peticion.
static bool whCloudOpen(void*, FlexWebConn* up, char* host, size_t hc, char* bearer, size_t bc, char* why, size_t wc){
  if(!flexStorageCopyBearer(bearer, bc, why, wc)) return false;     // puede abrir sesion (red, segundos como mucho)
  FlexStorageInfo si; flexStorageInfo(&si);
  IPAddress ip;
  if(si.state != FSP_READY || !si.port || !ip.fromString(si.ip)){ memset(bearer, 0, bc); snprintf(why, wc, "no_phone"); return false; }
  WiFiClient* cli = new WiFiClient();
  if(!cli->connect(ip, si.port, 4000)){
    delete cli; memset(bearer, 0, bc);
    flexStorageNoteResult(-1);
    snprintf(why, wc, "network");
    return false;
  }
  cli->setNoDelay(true);
  up->read = wcRead; up->write = wcWrite; up->ctx = cli; up->peer = NULL;
  snprintf(host, hc, "%s:%u", si.ip, (unsigned)si.port);
  return true;
}
static void whCloudClose(void*, FlexWebConn* up){
  WiFiClient* c = (WiFiClient*)up->ctx;
  if(c){ c->stop(); delete c; up->ctx = NULL; }
}
static void whCloudResult(void*, int st){
  flexStorageNoteResult(st);
  if(st == 401) flexStorageSessionRejected();     // la sesion caduco: la siguiente abre otra
}
// Origen del telefono para la CSP: sus fotos y videos se ven DIRECTAMENTE
// desde el (enlaces firmados de 15 min), sin pasar los bytes por el P4.
static void whPhoneOrigin(void*, char* out, size_t cap){
  FlexStorageInfo si; flexStorageInfo(&si);
  if(si.state == FSP_READY && si.ip[0] && si.port) snprintf(out, cap, "http://%s:%u", si.ip, (unsigned)si.port);
  else if(cap) out[0] = 0;
}
// Emparejamiento: lo decide FlexOS_StorageCore; la aprobacion es de la pantalla.
static bool whPhoneOffer(void*, char out[FST_HEX32]){ return flexStorageOffer(out); }
static int whPhonePair(void*, const FstPairReq* rq, const char* peer, char* json, size_t cap, uint32_t* retryS){
  return flexStoragePairBegin(rq, peer, json, cap, retryS);
}
static int whPhonePoll(void*, const char* id, const char* proof, char* json, size_t cap){ return flexStoragePairPoll(id, proof, json, cap); }

static void whEsc(const char* in, char* out, size_t cap){ if(!fclJsonEscape(in ? in : "", out, cap) && cap) out[0] = 0; }
static bool whStorageJson(void*, char* out, size_t cap){
  FlexStorageInfo si; flexStorageInfo(&si);
  FlexCloudStatus cs; flexCloudStatus(&cs);
  const char* st = si.state == FSP_READY ? "ready" : si.state == FSP_OFF ? "off" : si.state == FSP_REJECTED ? "rejected" : "none";
  char nm[2 * FST_NAME_MAX], md[2 * FST_MODEL_MAX], nt[200];
  whEsc(si.name, nm, sizeof(nm)); whEsc(si.model, md, sizeof(md)); whEsc(cs.netText, nt, sizeof(nt));
  long ok = si.lastOkAgeS == 0xFFFFFFFFu ? -1L : (long)si.lastOkAgeS;
  int n = snprintf(out, cap,
    "\"phone\":{\"state\":\"%s\",\"name\":\"%s\",\"model\":\"%s\",\"ip\":\"%s\",\"port\":%u,\"reachable\":%u,\"lastOkS\":%ld,\"pairWaiting\":%u},"
    "\"cloud\":{\"dest\":\"%s\",\"net\":%u,\"netText\":\"%s\",\"active\":%u,\"quota\":",
    st, nm, md, si.ip, (unsigned)si.port, (unsigned)si.reachable, ok, (unsigned)si.pairWaiting,
    flexCloudDest() == FCD_PHONE ? "phone" : "internet", (unsigned)cs.net, nt, (unsigned)cs.activeXfers);
  if(n < 0 || (size_t)n >= cap) return false;
  int m;
  if(cs.quotaValid){
    const FclQuota& q = cs.quota;
    m = snprintf(out + n, cap - (size_t)n,
      "{\"total\":%llu,\"used\":%llu,\"reserved\":%llu,\"trash\":%llu,\"available\":%llu,\"permille\":%u,\"state\":\"%s\"}}",
      (unsigned long long)q.totalBytes, (unsigned long long)q.usedBytes, (unsigned long long)q.reservedBytes,
      (unsigned long long)q.trashBytes, (unsigned long long)q.availableBytes, (unsigned)q.permille,
      q.state == FCL_Q_FULL ? "full" : q.state == FCL_Q_LOW ? "low" : "ok");
  } else m = snprintf(out + n, cap - (size_t)n, "null}");
  return m > 0 && (size_t)m < cap - (size_t)n;
}
// Las transferencias del gestor de la nube, con la MISMA frase que ensena el P4.
static bool whXfersJson(void*, char* out, size_t cap){
  FlexCloudXfer x[FLEX_CLOUD_XFERS];
  int n = flexCloudXfers(x, FLEX_CLOUD_XFERS);
  size_t w = 0;
  if(cap < 3) return false;
  out[w++] = '[';
  for(int i = 0; i < n; i++){
    char line[120], nm[2 * 64], ln[2 * 120];
    fclXferLine(x[i].phase, x[i].type, x[i].done, x[i].size, x[i].bytesPerSec, x[i].retryInMs, x[i].error, line, sizeof(line));
    whEsc(x[i].name, nm, sizeof(nm)); whEsc(line, ln, sizeof(ln));
    int k = snprintf(out + w, cap - w, "%s{\"id\":%lu,\"up\":%u,\"phase\":%u,\"name\":\"%s\",\"size\":%llu,\"done\":%llu,\"rate\":%lu,\"text\":\"%s\",\"move\":%u}",
                     i ? "," : "", (unsigned long)x[i].id, x[i].type == FCL_JOB_UPLOAD ? 1u : 0u, (unsigned)x[i].phase, nm,
                     (unsigned long long)x[i].size, (unsigned long long)x[i].done, (unsigned long)x[i].bytesPerSec, ln,
                     (x[i].flags & (FCL_JF_FREE_LOCAL | FCL_JF_MOVE_REMOTE)) ? 1u : 0u);
    if(k < 0 || (size_t)k >= cap - w - 2) break;
    w += (size_t)k;
  }
  out[w++] = ']'; out[w] = 0;
  return true;
}
static int whXferOp(void*, const FlexWebXferReq* rq, char* msg, size_t cap){
  if((rq->op == FLEXWEB_X_UP || rq->op == FLEXWEB_X_DOWN) && (flexCloudDest() != FCD_PHONE || !flexStoragePhoneUsable())){
    snprintf(msg, cap, "Conecta tu tel\xC3\xA9" "fono para usar Flex Cloud");
    return 409;
  }
  switch(rq->op){
    case FLEXWEB_X_UP: {
      FlexMlRec r;
      if(!flexMsGet(&gMs, rq->id, &r)){ snprintf(msg, cap, "Ya no existe"); return 404; }
      if(r.flags & FML_R_LOCKED){ snprintf(msg, cap, "Contenido protegido"); return 403; }
      // Mover = subir, que el telefono CONFIRME la misma huella, releer el
      // original... y solo entonces borrarlo (ckFreeLocal, en loopTask).
      uint32_t job = flexCloudUpload(r.path, r.name[0] ? r.name : NULL, rq->folder, r.id, rq->move ? FCL_JF_FREE_LOCAL : 0);
      if(!job){ snprintf(msg, cap, "No se pudo poner en la cola"); return 503; }
      snprintf(msg, cap, "%s", rq->move ? "Moviendo a Flex Cloud" : "Copiando a Flex Cloud");
      return 202;
    }
    case FLEXWEB_X_DOWN: {
      FclItem it; memset(&it, 0, sizeof(it));
      snprintf(it.id, sizeof(it.id), "%s", rq->file);
      fclCopyUtf8(it.name, sizeof(it.name), rq->name);
      snprintf(it.sha256, sizeof(it.sha256), "%s", rq->sha);
      it.size = rq->size;
      // La copia se VERIFICA contra la huella; mover deja el original en la
      // papelera de Flex Cloud solo despues de colocarla (ckPlaceDownload).
      uint32_t job = flexCloudDownload(&it, (uint8_t)(FCL_JF_TO_LIBRARY | (rq->move ? FCL_JF_MOVE_REMOTE : 0)));
      if(!job){ snprintf(msg, cap, "No se pudo poner en la cola"); return 503; }
      snprintf(msg, cap, "%s", rq->move ? "Moviendo a Flex OS" : "Copiando a Flex OS");
      return 202;
    }
    case FLEXWEB_X_CANCEL: if(flexCloudCancel(rq->id)){ snprintf(msg, cap, "Cancelada"); return 202; } snprintf(msg, cap, "Ya no est\xC3\xA1 en curso"); return 404;
    case FLEXWEB_X_RETRY:  if(flexCloudRetry(rq->id)){ snprintf(msg, cap, "Reintentando"); return 202; } snprintf(msg, cap, "No se puede reintentar"); return 409;
    case FLEXWEB_X_CLEAR:  flexCloudClearFinished(); snprintf(msg, cap, "Lista limpia"); return 202;
  }
  snprintf(msg, cap, "Operaci\xC3\xB3n no v\xC3\xA1lida");
  return 400;
}

// Borrar y renombrar desde la web se hace en loopTask: es quien sabe si esa
// cancion esta sonando o esa foto abierta (mlBeforeChange) y quien suelta sus
// miniaturas de la RAM. Un solo hueco (el servidor atiende de una en una); la
// tarea del servidor espera el resultado con un plazo y, si loopTask no lo
// recogio a tiempo, lo retira sin que llegue a hacerse.
enum { WLO_IDLE = 0, WLO_PENDING, WLO_RUNNING, WLO_DONE };
enum { WLO_REMOVE = 1, WLO_RENAME };
struct WebLibOp { volatile uint8_t state; uint8_t op; uint32_t id; char name[FML_NAME_MAX]; bool ok; char why[96]; };
static WebLibOp gWebLibOp;
static bool webLibRun(uint8_t op, uint32_t id, const char* name, char* why, size_t cap){
  gWebLibOp.op = op; gWebLibOp.id = id; gWebLibOp.ok = false; gWebLibOp.why[0] = 0;
  snprintf(gWebLibOp.name, sizeof(gWebLibOp.name), "%s", name ? name : "");
  __atomic_store_n(&gWebLibOp.state, (uint8_t)WLO_PENDING, __ATOMIC_RELEASE);
  uint32_t t0 = millis();
  for(;;){
    uint8_t st = __atomic_load_n(&gWebLibOp.state, __ATOMIC_ACQUIRE);
    if(st == WLO_DONE) break;
    if(st == WLO_PENDING && (millis() - t0 > 3000 || gWebStop)){
      uint8_t exp = WLO_PENDING;
      if(__atomic_compare_exchange_n(&gWebLibOp.state, &exp, (uint8_t)WLO_IDLE, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)){
        snprintf(why, cap, "Flex OS est\xC3\xA1 ocupado; vuelve a intentarlo");
        return false;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  bool ok = gWebLibOp.ok;
  snprintf(why, cap, "%s", gWebLibOp.why);
  __atomic_store_n(&gWebLibOp.state, (uint8_t)WLO_IDLE, __ATOMIC_RELEASE);
  return ok;
}
static bool whRemoveRec(void*, uint32_t id, char* why, size_t cap){ return webLibRun(WLO_REMOVE, id, NULL, why, cap); }
static bool whRenameRec(void*, uint32_t id, const char* name, char* why, size_t cap){ return webLibRun(WLO_RENAME, id, name, why, cap); }

static int wcRead(void* ctx, uint8_t* buf, size_t n, uint32_t ms){
  WiFiClient* c = (WiFiClient*)ctx;
  uint32_t t0 = millis();
  for(;;){
    int a = c->available();
    if(a > 0){
      int k = c->read(buf, n < (size_t)a ? n : (size_t)a);
      return k > 0 ? k : -1;
    }
    if(!c->connected()) return -1;
    if(gWebStop) return -1;
    if(millis() - t0 >= ms) return 0;
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}
// El write real puede aceptar MENOS de lo pedido con el socket vivo: se
// insiste mientras el otro lado siga ahi (y no se da por muerta la conexion
// por una escritura corta).
static bool wcWrite(void* ctx, const uint8_t* b, size_t n){
  WiFiClient* c = (WiFiClient*)ctx;
  uint32_t t0 = millis();
  while(n){
    size_t k = c->write(b, n);
    if(k == 0){
      if(!c->connected() || gWebStop || millis() - t0 > 10000) return false;
      vTaskDelay(pdMS_TO_TICKS(2));
      continue;
    }
    b += k; n -= k; t0 = millis();
  }
  return true;
}

static void webTask(void*){
  gWebSrv = new WiFiServer(FLEXWEB_PORT);
  gWebSrv->begin();
  gWebSrv->setNoDelay(true);
  gWebState = WEBS_ON;
  Serial.printf("[web] Flex Web Server en %s\n", gWebUrl);
  uint32_t stackLow = 0xFFFFFFFFu;
  while(!gWebStop){
    mlStackCheck("web", &stackLow);
    if(!gNetOnline) break;
    if(gWebSrv->hasClient()){
      WiFiClient c = gWebSrv->accept();
      if(c){
        // La IP de quien llama: Flex Storage solo empareja telefonos de la red local.
        char peer[16];
        snprintf(peer, sizeof(peer), "%s", c.remoteIP().toString().c_str());
        FlexWebConn conn = { wcRead, wcWrite, &c, peer };
        flexWebServeConn(&gWebCtx, &conn, gWebHdr, gWebIo);
        c.stop();
      }
    } else vTaskDelay(pdMS_TO_TICKS(20));
  }
  gWebSrv->end();
  delete gWebSrv;
  gWebSrv = NULL;
  gWebState = gNetOnline ? WEBS_OFF : WEBS_NOWIFI;
  Serial.println(F("[web] Flex Web Server parado"));
  gWebTask = NULL;
  vTaskDelete(NULL);
}

// ---- Encender / apagar (desde loopTask) ----
static bool webStart(){
  if(gWebTask || !gMlOk) return false;
  if(!gNetOnline || !wifiConnIP[0]){ gWebState = WEBS_NOWIFI; return false; }
  if(!gWebHdr) gWebHdr = (uint8_t*)mediaAlloc(FLEXWEB_HDR_BUF);
  if(!gWebIo)  gWebIo  = (uint8_t*)mediaAlloc(FLEXWEB_IO_BUF);
  if(!gWebEv)  gWebEv  = (FlexWebXfer*)mediaAlloc(sizeof(FlexWebXfer) * WEB_EV_N);
  if(!gWebHdr || !gWebIo || !gWebEv){ gWebState = WEBS_FAIL; return false; }
  memset(&gWebCtx, 0, sizeof(gWebCtx));
  gWebCtx.fs = { msfOpen, msfRead, msfWrite, msfSeek, msfClose, msfSize, msfRemove, wfsFree, wfsTotal, NULL };
  // La segunda y la tercera linea son Flex Storage: el MISMO servidor y el
  // mismo QR (docs/FLEX-STORAGE.md).
  gWebCtx.host = { whSnapshot, whGet, whRev, whDup, whCommit, whSetThumb, whThumbPath, whVerify, whLockType,
                   whNow, whEpoch, whRandom, whEvent, whYield, whOthers, NULL, whHeavyBegin, whHeavyEnd,
                   whCloudOpen, whCloudClose, whCloudResult, whPhoneOrigin, whPhoneOffer, whPhonePair, whPhonePoll,
                   whStorageJson, whXfersJson, whXferOp, whRemoveRec, whRenameRec };
  gWebCtx.alloc = mediaAlloc; gWebCtx.free = mediaFree;
  snprintf(gWebCtx.ip, sizeof(gWebCtx.ip), "%s", wifiConnIP);
  gWebCtx.port = FLEXWEB_PORT;
  gWebCtx.allowUpload = true;
  flexWebInit(&gWebCtx);                          // codigo NUEVO en cada arranque
  snprintf(gWebUrl, sizeof(gWebUrl), "http://%s:%d/", wifiConnIP, FLEXWEB_PORT);
  gWebStop = false;
  gWebState = WEBS_STARTING;
  if(xTaskCreatePinnedToCore(webTask, "flexWeb", WEB_TASK_STACK, NULL, 1, &gWebTask, 1) != pdPASS){
    gWebTask = NULL; gWebState = WEBS_FAIL; return false;
  }
  return true;
}
static void webStop(){
  gWebStop = true;                                // la tarea sale en su proxima vuelta
  if(!gWebTask) gWebState = gNetOnline ? WEBS_OFF : WEBS_NOWIFI;
}

// -------------------------------------------------------------
//  HOJA "CONECTAR CON EL MOVIL"
// -------------------------------------------------------------
#define WEB_CARDS 3
struct WebCard { bool on; bool up; int kind; uint32_t done, total, doneMs; uint8_t state; char name[48]; char msg[64]; };
enum { WCS_RUN = 0, WCS_CHECK, WCS_OK, WCS_FAIL };
static WebCard gWebCards[WEB_CARDS];
static bool    webSheetOn = false;
static void  (*webSheetBack)() = NULL;            // repinta la app al cerrar
static uint32_t gWebSheetMs = 0;
static uint8_t* gWebQr = NULL;                     // modulos del QR (a demanda)
static int      gWebQrSize = 0;
static char     gWebQrFor[80] = "";

static WebCard* webCardFor(const char* name, bool up){
  for(int i = 0; i < WEB_CARDS; i++) if(gWebCards[i].on && gWebCards[i].up == up && !strcmp(gWebCards[i].name, name)) return &gWebCards[i];
  // Nueva: la ranura libre o la terminada mas vieja.
  WebCard* c = NULL;
  for(int i = 0; i < WEB_CARDS && !c; i++) if(!gWebCards[i].on) c = &gWebCards[i];
  for(int i = 0; i < WEB_CARDS && !c; i++) if(gWebCards[i].state >= WCS_OK) c = &gWebCards[i];
  if(!c) c = &gWebCards[0];
  memset(c, 0, sizeof(*c));
  c->on = true; c->up = up;
  snprintf(c->name, sizeof(c->name), "%s", name);
  return c;
}

static void webQrEnsure(){
  char txt[80];
  snprintf(txt, sizeof(txt), "%s?k=%s", gWebUrl, gWebCtx.code);
  if(gWebQr && !strcmp(txt, gWebQrFor)) return;
  if(!gWebQr) gWebQr = (uint8_t*)mediaAlloc(FLEXQR_BUF_BYTES);
  int ver = 0;
  gWebQrSize = 0;
  if(gWebQr && flexQrEncode(txt, FLEXQR_ECC_M, 1, gWebQr, FLEXQR_BUF_BYTES, &gWebQrSize, &ver) == FLEXQR_OK)
    snprintf(gWebQrFor, sizeof(gWebQrFor), "%s", txt);
  else gWebQrSize = 0;
}

static void webSheetGeom(int &x, int &y, int &w, int &h){ uiBox(x, y, w, h); }

// -------------------------------------------------------------
//  REPINTADO LOCALIZADO DE LA HOJA
//  ------------------------------------------------------------
//  Antes la hoja entera (con el QR) se repintaba y se publicaba cada
//  400 ms mientras estaba abierta, cambiara algo o no: una subida DMA de
//  toda la ventana dos veces y media por segundo, tambien en reposo y
//  durante las transferencias. Ahora la hoja tiene dos zonas:
//    · FIJA: cabecera, interruptor, QR, direccion y codigo. Solo se
//      repinta si cambia lo que la decide (estado del servidor, codigo,
//      tema).
//    · VIVA: moviles conectados, contadores y tarjetas de progreso. Se
//      repinta ELLA SOLA, y solo cuando su contenido cambio de verdad.
//  Cada zona tiene una huella (FNV-1a) de lo que pinta; comparar huellas
//  cuesta unos pocos cientos de bytes de lectura, no un repintado.
// -------------------------------------------------------------
static int      gWebLiveY = -1;                    // arriba de la zona viva (-1 = pintar entera)
static uint32_t gWebStaticSig = 0, gWebLiveSig = 0;

static uint32_t webFnv(uint32_t h, const void* p, size_t n){
  const uint8_t* b = (const uint8_t*)p;
  while(n--) h = (h ^ *b++) * 16777619u;
  return h;
}
static uint32_t webStaticSig(){
  uint32_t h = 2166136261u;
  uint8_t k[3] = { gWebState, (uint8_t)uiGlass, (uint8_t)gDark };
  h = webFnv(h, k, sizeof(k));
  if(gWebState == WEBS_ON){ h = webFnv(h, gWebCtx.code, strlen(gWebCtx.code)); h = webFnv(h, gWebUrl, strlen(gWebUrl)); }
  return h;
}
static uint32_t webLiveSig(){
  uint32_t h = 2166136261u;
  if(gWebState == WEBS_ON){
    uint32_t v[3] = { (uint32_t)flexWebSessionCount(&gWebCtx), gWebCtx.uploads, gWebCtx.downloads };
    h = webFnv(h, v, sizeof(v));
    // Flex Cloud en el telefono: la linea de estado tambien es zona viva.
    FlexStorageInfo si; flexStorageInfo(&si);
    uint8_t k[2] = { si.state, si.reachable };
    h = webFnv(h, k, sizeof(k));
    h = webFnv(h, si.name, strlen(si.name));
  }
  for(int i = 0; i < WEB_CARDS; i++){
    const WebCard* c = &gWebCards[i];
    if(!c->on) continue;
    uint32_t v[4] = { (uint32_t)i | ((uint32_t)c->state << 8) | ((uint32_t)c->up << 16), c->done, c->total, 0 };
    h = webFnv(h, v, sizeof(v));
    h = webFnv(h, c->name, strlen(c->name));
    h = webFnv(h, c->msg, strlen(c->msg));
  }
  return h;
}

// Zona VIVA, desde `cy` hasta abajo de la hoja. No vuelca.
static void webSheetDrawLive(int x, int y, int w, int h, int cy){
  int pad = uiPad();
  if(gWebState == WEBS_ON){
    int ns = flexWebSessionCount(&gWebCtx);
    char st[80];
    snprintf(st, sizeof(st), "%d m\xC3\xB3vil%s conectado%s  \xC2\xB7  %lu recibidos  \xC2\xB7  %lu enviados",
             ns, ns == 1 ? "" : "es", ns == 1 ? "" : "s", (unsigned long)gWebCtx.uploads, (unsigned long)gWebCtx.downloads);
    drawTextC(x + w / 2, cy, st, 1, TH_TXT2);
    cy += 20;
    // Flex Cloud en el telefono (Flex Storage): se activa desde esta misma web.
    FlexStorageInfo si; flexStorageInfo(&si);
    char fc[96];
    if(si.state == FSP_READY) snprintf(fc, sizeof(fc), "Flex Cloud: %s \xC2\xB7 %s", si.name[0] ? si.name : "tel\xC3\xA9" "fono", si.reachable ? "conectado" : "sin respuesta");
    else if(si.state == FSP_REJECTED) snprintf(fc, sizeof(fc), "Flex Cloud: vuelve a emparejar el tel\xC3\xA9" "fono desde la web");
    else if(si.state == FSP_OFF) snprintf(fc, sizeof(fc), "Flex Cloud: tel\xC3\xA9" "fono en pausa (Almacenamiento)");
    else snprintf(fc, sizeof(fc), "Flex Cloud: act\xC3\xADvalo desde la web en tu tel\xC3\xA9" "fono");
    drawTextC(x + w / 2, cy, fc, 1, si.state == FSP_READY && si.reachable ? TH_OK : TH_TXT2);
    cy += 22;
    // Acciones
    int bw = (w - 2 * pad - 12) / 2;
    fillRoundRect(x + pad, cy, bw, 44, 14, TH_SURF2);
    drawTextC(x + pad + bw / 2, cy + 14, "C\xC3\xB3" "digo nuevo", 2, TH_TXT);
    fillRoundRect(x + pad + bw + 12, cy, bw, 44, 14, TH_SURF2);
    drawTextC(x + pad + bw + 12 + bw / 2, cy + 14, "Desconectar m\xC3\xB3viles", 1, TH_TXT);
    cy += 58;
  } else {
    drawTextC(x + w / 2, cy + 10, "Sube fotos, v\xC3\xAD" "deos y m\xC3\xBAsica desde el navegador", 1, TH_TXT2);
    drawTextC(x + w / 2, cy + 28, "del m\xC3\xB3vil, en la misma red Wi-Fi. Lo que Flex OS no", 1, TH_TXT2);
    drawTextC(x + w / 2, cy + 46, "puede abrir se convierte en el propio m\xC3\xB3vil.", 1, TH_TXT2);
    cy += 80;
  }
  // Tarjetas de transferencia (progreso REAL, en bytes)
  for(int i = 0; i < WEB_CARDS; i++){
    WebCard* c = &gWebCards[i];
    if(!c->on || cy + 64 > y + h) continue;
    if(uiGlass) drawGlassCardFlat(x + pad, cy, w - 2 * pad, 58, 14, TH_GLASS, WIN_BG);
    else        fillRoundRect(x + pad, cy, w - 2 * pad, 58, 14, TH_SURF);
    drawTextClip(x + pad + 14, cy + 8, c->name, 2, TH_TXT, x + w - pad - 70);
    char line[80];
    if(c->state == WCS_RUN && c->total){
      char a[16], b[16]; flexFsFmtSize(c->done, a, sizeof(a)); flexFsFmtSize(c->total, b, sizeof(b));
      snprintf(line, sizeof(line), "%s %s de %s", c->up ? "Recibiendo" : "Enviando", a, b);
    } else if(c->state == WCS_CHECK) snprintf(line, sizeof(line), "Comprobando el archivo...");
    else snprintf(line, sizeof(line), "%s", c->msg);
    drawTextClip(x + pad + 14, cy + 30, line, 1, c->state == WCS_FAIL ? TH_ERR : TH_TXT2, x + w - pad - 20);
    int pw = w - 2 * pad - 28, pct = c->total ? (int)((uint64_t)c->done * 100 / c->total) : 0;
    if(c->state >= WCS_CHECK) pct = 100;
    fillRoundRect(x + pad + 14, cy + 46, pw, 5, 2, TH_SURF2);
    fillRoundRect(x + pad + 14, cy + 46, pw * pct / 100, 5, 2, c->state == WCS_FAIL ? TH_ERR : c->state == WCS_OK ? TH_OK : TH_PRIM);
    cy += 66;
  }
}

// Hoja ENTERA (al abrirla, al tocarla y cuando cambia su parte fija).
static void webSheetRender(){
  int x, y, w, h; webSheetGeom(x, y, w, h);
  setBuf(fb);
  fillRect(x, y, w, h, WIN_BG);
  int pad = uiPad();
  strokeSegAA(x + 30, y + 30, x + 18, y + 22, 2.4f, TH_TXT);      // atras
  strokeSegAA(x + 18, y + 22, x + 30, y + 14, 2.4f, TH_TXT);
  drawText(x + 48, y + 12, "Conectar con el m\xC3\xB3vil", 3, TH_TXT);

  // Interruptor
  bool on = gWebState == WEBS_ON || gWebState == WEBS_STARTING;
  int sy = y + 56;
  if(uiGlass) drawGlassCardFlat(x + pad, sy, w - 2 * pad, 58, 16, TH_GLASS, WIN_BG);
  else        fillRoundRect(x + pad, sy, w - 2 * pad, 58, 16, TH_SURF);
  drawText(x + pad + 16, sy + 10, "Flex Web Server", 2, TH_TXT);
  const char* sub = gWebState == WEBS_ON ? "Activo en esta red Wi-Fi"
                  : gWebState == WEBS_STARTING ? "Arrancando..."
                  : gWebState == WEBS_NOWIFI ? "Sin Wi-Fi: con\xC3\xA9" "ctate primero a una red"
                  : gWebState == WEBS_FAIL ? "No se pudo arrancar (memoria)" : "Apagado";
  drawText(x + pad + 16, sy + 34, sub, 1, gWebState == WEBS_NOWIFI || gWebState == WEBS_FAIL ? TH_WARN : TH_TXT2);
  int kx = x + w - pad - 70, ky = sy + 15;
  fillRoundRect(kx, ky, 54, 28, 14, on ? TH_PRIM : TH_MUTE);
  fillCircle(on ? kx + 40 : kx + 14, ky + 14, 11, rgb565(255, 255, 255));

  int cy = sy + 76;
  if(gWebState == WEBS_ON){
    // QR con el codigo dentro: escanear es emparejar.
    webQrEnsure();
    int qs = gWebQrSize, mod = qs ? (w - 2 * pad - 160) / (qs + 8) : 0;
    if(mod > 7) mod = 7;
    if(mod < 3) mod = 3;
    int side = (qs + 8) * mod, qx = x + (w - side) / 2;
    fillRoundRect(qx, cy, side, side, 12, rgb565(255, 255, 255));
    for(int r = 0; r < qs; r++) for(int c = 0; c < qs; c++)
      if(gWebQr[r * qs + c] & FLEXQR_DARK) fillRect(qx + (c + 4) * mod, cy + (r + 4) * mod, mod, mod, rgb565(0, 0, 0));
    cy += side + 14;
    drawTextC(x + w / 2, cy, gWebUrl, 2, TH_TXT);
    cy += 30;
    drawTextC(x + w / 2, cy, "C\xC3\xB3" "digo de acceso", 1, TH_TXT2);
    char code[16];
    snprintf(code, sizeof(code), "%c%c%c %c%c%c", gWebCtx.code[0], gWebCtx.code[1], gWebCtx.code[2],
             gWebCtx.code[3], gWebCtx.code[4], gWebCtx.code[5]);
    drawTextC(x + w / 2, cy + 16, code, 5, TH_TXT);
    cy += 70;
  }
  gWebLiveY = cy;
  webSheetDrawLive(x, y, w, h, cy);
  flxFlush(WIN_TOP, WIN_BOT);
  gWebSheetMs = millis();
  gWebStaticSig = webStaticSig();
  gWebLiveSig = webLiveSig();
}

// Solo la zona VIVA: se borra su fondo liso, se repinta y se publica ESA
// banda. Las tarjetas son vidrio sobre fondo plano (drawGlassCardFlat), asi
// que repintarlas sobre WIN_BG da siempre los mismos pixeles: nada se apila.
static void webSheetRenderLive(){
  int x, y, w, h; webSheetGeom(x, y, w, h);
  if(gWebLiveY < y || gWebLiveY >= y + h){ webSheetRender(); return; }
  setBuf(fb);
  fillRect(x, gWebLiveY, w, y + h - gWebLiveY, WIN_BG);
  webSheetDrawLive(x, y, w, h, gWebLiveY);
  flxFlush(gWebLiveY, y + h - 1);
  gWebSheetMs = millis();
  gWebLiveSig = webLiveSig();
}

// Repinta SOLO si algo cambio, y solo la zona que cambio.
static void webSheetRefresh(){
  if(!webSheetOn) return;
  if(gWebLiveY < 0 || webStaticSig() != gWebStaticSig){ webSheetRender(); return; }
  if(webLiveSig() != gWebLiveSig) webSheetRenderLive();
}

static void webSheetOpen(){
  webSheetOn = true;
  if(gWebState == WEBS_NOWIFI && gNetOnline) gWebState = WEBS_OFF;
  webSheetRender();
}
// Cierra la hoja SIN repintar a nadie (la app pasa a segundo plano). El
// servidor sigue como estaba: la hoja solo es la ventana para verlo.
static void webSheetDismiss(){
  webSheetOn = false;
  gWebLiveY = -1;                                  // la proxima vez se pinta entera
  if(gWebQr){ mediaFree(gWebQr); gWebQr = NULL; gWebQrFor[0] = 0; }
}
static void webSheetClose(){
  webSheetDismiss();
  if(webSheetBack) webSheetBack();
}
// Para el kit de listas de medios (Galeria, Multimedia, Musica): abre la
// hoja y, al cerrarla, repinta la app que la abrio.
static void webSheetOpenFor(void (*back)()){ webSheetBack = back; webSheetOpen(); }
static bool webSheetIsOpen(){ return webSheetOn; }

// La hoja se lleva los toques mientras esta abierta. true = la hoja esta
// abierta (la app no debe procesar nada mas este cuadro).
static bool webSheetTick(){
  if(!webSheetOn) return false;
  // Estado y progreso vivos: se MIRA cada 250 ms y solo se repinta lo que
  // cambio (ver REPINTADO LOCALIZADO DE LA HOJA).
  if(millis() - gWebSheetMs >= 250){ gWebSheetMs = millis(); webSheetRefresh(); }
  if(!T.tap) return true;
  int x, y, w, h; webSheetGeom(x, y, w, h);
  int pad = uiPad();
  if(T.y < y + 48 && T.x < x + 60){ webSheetClose(); return true; }
  int sy = y + 56;
  if(T.y >= sy && T.y <= sy + 58){
    bool on = gWebState == WEBS_ON || gWebState == WEBS_STARTING;
    if(on){ gWebWant = false; webStop(); }
    else { gWebWant = true; webStart(); }
    webSheetRender();
    return true;
  }
  if(gWebState == WEBS_ON){
    // Las dos acciones, abajo del QR: su posicion depende del QR, asi que se
    // recalcula como en el dibujo.
    int qs = gWebQrSize, mod = qs ? (w - 2 * pad - 160) / (qs + 8) : 0;
    if(mod > 7) mod = 7;
    if(mod < 3) mod = 3;
    int by = sy + 76 + (qs + 8) * mod + 14 + 30 + 70 + 22;
    if(T.y >= by && T.y <= by + 44){
      int bw = (w - 2 * pad - 12) / 2;
      if(T.x < x + pad + bw){ flexWebNewCode(&gWebCtx); sysNotify("Flex Web Server", "C\xC3\xB3" "digo nuevo: el anterior ya no sirve"); }
      else { flexWebDropAll(&gWebCtx); sysNotify("Flex Web Server", "M\xC3\xB3viles desconectados"); }
      webSheetRender();
    }
  }
  return true;
}

// -------------------------------------------------------------
//  TICK (loop): avisos, tarjetas, Wi-Fi y bloqueo
// -------------------------------------------------------------
// Lo que la web pidio hacer en la biblioteca (ver webLibRun).
static void webLibTick(){
  uint8_t exp = WLO_PENDING;
  if(!__atomic_compare_exchange_n(&gWebLibOp.state, &exp, (uint8_t)WLO_RUNNING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
  bool ok = false;
  char why[96] = "";
  FlexMlRec r;
  if(!mlGet(gWebLibOp.id, &r)) snprintf(why, sizeof(why), "Ya no existe");
  else if(gWebLibOp.op == WLO_REMOVE){
    // A la papelera (recuperable desde Archivos). Un protegido nunca va ahi
    // (la papelera es publica): se borra, y la web solo llega aqui con el
    // nivel de propietario.
    ok = (r.flags & FML_R_LOCKED) ? mlDelete(r.id) : mlTrash(r.id);
    if(!ok) snprintf(why, sizeof(why), "No se pudo eliminar");
  } else ok = mlRename(r.id, gWebLibOp.name, why, sizeof(why));
  gWebLibOp.ok = ok;
  snprintf(gWebLibOp.why, sizeof(gWebLibOp.why), "%s", why);
  __atomic_store_n(&gWebLibOp.state, (uint8_t)WLO_DONE, __ATOMIC_RELEASE);
}

static void webTick(){
  webLibTick();
  // El P4 se bloquea: ninguna sesion conserva el contenido protegido.
  bool locked = (gState == ST_LOCK);
  if(locked && !gWebLockSeen){ flexWebDropOwners(&gWebCtx); gWebLockSeen = true; }
  if(!locked) gWebLockSeen = false;
  // Wi-Fi: sin red se para; si vuelve y el usuario lo tenia encendido, arranca.
  if(gWebTask && !gNetOnline) webStop();
  if(!gWebTask && gWebWant && gNetOnline && wifiConnIP[0] && gWebState != WEBS_FAIL){
    static uint32_t lastTry = 0;
    if(!lastTry || millis() - lastTry > 5000){ lastTry = millis() | 1u; webStart(); }
  }
  if(!gWebTask && !gWebWant && gWebState == WEBS_ON) gWebState = WEBS_OFF;
  // Eventos de la tarea del servidor.
  bool changed = false;
  while(gWebEv && gWebEvR != __atomic_load_n(&gWebEvW, __ATOMIC_ACQUIRE)){
    FlexWebXfer x = gWebEv[gWebEvR % WEB_EV_N];
    __atomic_store_n(&gWebEvR, (uint8_t)(gWebEvR + 1), __ATOMIC_RELEASE);
    changed = true;
    switch(x.ev){
      case FLEXWEB_EV_UP_START: case FLEXWEB_EV_UP_PROGRESS: {
        WebCard* c = webCardFor(x.name, true);
        c->kind = x.kind; c->done = x.done; c->total = x.total; c->state = WCS_RUN;
        break;
      }
      case FLEXWEB_EV_UP_CHECK: { WebCard* c = webCardFor(x.name, true); c->state = WCS_CHECK; c->done = c->total; break; }
      case FLEXWEB_EV_UP_DONE: {
        WebCard* c = webCardFor(x.name, true);
        c->state = WCS_OK; c->done = c->total = x.total ? x.total : c->total;
        mlCopyText(c->msg, sizeof(c->msg), x.msg[0] ? x.msg : x.kind == FML_K_AUDIO ? "Guardado en M\xC3\xBAsica" : "Guardado en Galer\xC3\xAD" "a y Multimedia");
        char t[64]; snprintf(t, sizeof(t), "Recibido: %.40s", x.name);
        if(!webSheetOn) sysNotify("Flex Web Server", t);
        break;
      }
      case FLEXWEB_EV_UP_FAIL: {
        WebCard* c = webCardFor(x.name, true);
        c->state = WCS_FAIL; mlCopyText(c->msg, sizeof(c->msg), x.msg);
        char t[64]; snprintf(t, sizeof(t), "No se recibi\xC3\xB3 %.36s", x.name);
        sysNotify(t, x.msg);
        break;
      }
      case FLEXWEB_EV_DL_START: case FLEXWEB_EV_DL_PROGRESS: {
        WebCard* c = webCardFor(x.name, false);
        c->done = x.done; c->total = x.total; c->state = WCS_RUN;
        break;
      }
      case FLEXWEB_EV_DL_DONE: { WebCard* c = webCardFor(x.name, false); c->state = WCS_OK; c->done = c->total; snprintf(c->msg, sizeof(c->msg), "Enviado al m\xC3\xB3vil"); break; }
      case FLEXWEB_EV_DL_FAIL: { WebCard* c = webCardFor(x.name, false); c->state = WCS_FAIL; mlCopyText(c->msg, sizeof(c->msg), x.msg); break; }
      case FLEXWEB_EV_PAIRED:     sysNotify("Flex Web Server", "Un m\xC3\xB3vil se ha conectado"); break;
      case FLEXWEB_EV_OWNER:      sysNotify("Contenido protegido", "Se ha abierto desde un m\xC3\xB3vil con tu clave"); break;
      case FLEXWEB_EV_OWNER_FAIL: sysNotify("Contenido protegido", "Intento fallido desde un m\xC3\xB3vil"); break;
    }
  }
  (void)changed;
}
