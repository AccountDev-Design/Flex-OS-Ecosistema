// #############################################################
// ##  FLEX OS ULTRA  ·  BIBLIOTECA DE MEDIOS  ·  el catalogo vivo
// ##  ----------------------------------------------------------
// ##  La mitad de PLACA de la biblioteca: el almacen (FlexOS_MediaStore)
// ##  sobre LittleFS y un mutex, con el catalogo en PSRAM; la tarea que
// ##  lo mantiene al dia; la cache de miniaturas y el kit de interfaz
// ##  (menu, dialogos, clave) que comparten Galeria, Multimedia y Musica.
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
#include "FlexOS_Ultra_Media.h"   // eslabon anterior de la cadena
#include "FlexOS_MediaLib.h"
#include "FlexOS_MediaStore.h"
#include "FlexOS_MediaThumb.h"

// #############################################################
// ##  QUIEN TOCA QUE
// ##  ------------------------------------------------------
// ##  Tres tareas leen o cambian el catalogo:
// ##    · loopTask (la interfaz): lo pinta, borra, bloquea, renombra.
// ##    · "flexMedia" (aqui): reconcilia con el disco, genera
// ##      miniaturas persistentes y guarda el archivo.
// ##    · "flexWeb" (Flex Web Server): registra lo que sube el movil.
// ##  TODAS pasan por gMlMux (el cerrojo de gMs). Regla: un cambio de
// ##  DISCO que afecta a un registro (mover a Protegido, borrar,
// ##  publicar una subida) se hace CON el cerrojo tomado, junto con el
// ##  cambio del registro. Asi el recorrido del disco nunca ve un archivo
// ##  "a medio mover" y no puede nacer un registro duplicado ni uno
// ##  fantasma. La prueba de host lo comprueba con hilos de verdad.
// ##
// ##  Las funciones "...Locked" dan el cerrojo por tomado. El mutex no
// ##  es recursivo a proposito: si una funcion publica llamara a otra
// ##  publica con el cerrojo tomado, la placa se quedaria parada en el
// ##  acto y se veria en la primera prueba, no en un caso raro.
// ##
// ##  LO QUE NO HACE LA INTERFAZ. Nunca decodifica una foto entera para
// ##  pintar una celda: pinta la miniatura persistente (un JPEG de
// ##  132x132, unos pocos KB). Las fotos grandes las abre la tarea de
// ##  fondo, una a una, y solo si queda PSRAM de sobra.
// #############################################################

#define ML_SAVE_DELAY_MS      1500u                 // agrupa cambios seguidos en una escritura
#define ML_WORKER_STACK       12288
#define ML_PSRAM_MARGIN       (1024u * 1024u)       // ademas de la reserva del sistema
#define ML_RETRY_MS           3000u                 // lo aplazado por memoria se reintenta tras esto
#define ML_CACHE_N            24                    // miniaturas decodificadas a la vez (132x132 RGB565 = 34 KB)
#define ML_SIDE               FLEXTH_SIDE
#define ML_MSG_N              4

static FlexMediaStore    gMs;                       // catalogo + disco (FlexOS_MediaStore)
static FlexMlRec*        gMlStore  = NULL;          // sus registros, en PSRAM
static SemaphoreHandle_t gMlMux    = NULL;
static TaskHandle_t      gMlTask   = NULL;
static bool              gMlOk     = false;         // hay catalogo (aunque este vacio)
static volatile bool     gMlScanReq  = true;        // hay que reconciliar con el disco
static volatile uint32_t gMlLoadedFrom = 0;         // 1 = archivo, 2 = .bak, 3 = .tmp, 0 = vacio

// Avisos de la tarea de fondo para la isla. La isla solo se toca desde
// loopTask, asi que la tarea deja el texto aqui y mlTick() lo entrega.
static char              gMlMsg[ML_MSG_N][2][56];     // el aviso mas largo mide 50 bytes
static volatile uint8_t  gMlMsgW = 0, gMlMsgR = 0;

static inline void mlLock(){ if(gMlMux) xSemaphoreTake(gMlMux, portMAX_DELAY); }
static inline void mlUnlock(){ if(gMlMux) xSemaphoreGive(gMlMux); }
static inline void mlWake(){ if(gMlTask) xTaskNotifyGive(gMlTask); }

// Revision del catalogo: cambia con CUALQUIER cambio que se vea.
static uint32_t mlRev(){ return flexMsRev(&gMs); }

// Copia un texto que se ENSENA en `dst` (cap bytes), recortado en un limite
// de caracter UTF-8: cortar a mitad de una "o" con tilde la pintaria como "?".
static void mlCopyText(char* dst, size_t cap, const char* src){
  if(!dst || !cap) return;
  size_t n = src ? strlen(src) : 0;
  if(n >= cap){ n = cap - 1; while(n > 0 && ((uint8_t)src[n] & 0xC0) == 0x80) n--; }
  if(n) memcpy(dst, src, n);
  dst[n] = 0;
}

static void mlPostMsg(const char* title, const char* sub){
  uint8_t w = gMlMsgW;
  if((uint8_t)(w - gMlMsgR) >= ML_MSG_N) return;           // cola llena: se pierde el aviso, no el sistema
  snprintf(gMlMsg[w % ML_MSG_N][0], sizeof(gMlMsg[0][0]), "%s", title ? title : "");
  snprintf(gMlMsg[w % ML_MSG_N][1], sizeof(gMlMsg[0][1]), "%s", sub ? sub : "");
  gMlMsgW = (uint8_t)(w + 1);
}

// -------------------------------------------------------------
//  EL ALMACEN SOBRE LITTLEFS
//  ------------------------------------------------------------
//  Lo que se hace con el disco (reconciliar, miniaturas, bloquear, borrar,
//  renombrar, reemplazar, publicar subidas) vive en FlexOS_MediaStore y se
//  prueba en el PC con fallos provocados y con hilos a la vez
//  (tests/host/test_mediastore.cpp). Aqui solo se le da el disco, el
//  cerrojo, la memoria y el reloj de la placa.
// -------------------------------------------------------------
static void* msfOpen(void*, const char* p, bool w){ return w ? (void*)flexFsOpenWrite(p) : (void*)flexFsOpenRead(p); }
static int msfRead(void*, void* h, uint8_t* b, size_t n){ return flexFsStreamRead((FlexFsStream*)h, b, n); }
static bool msfWrite(void*, void* h, const uint8_t* b, size_t n){ return flexFsStreamWrite((FlexFsStream*)h, b, n); }
static bool msfSeek(void*, void* h, uint32_t off){ return flexFsStreamSeek((FlexFsStream*)h, off); }
static void msfClose(void*, void* h){ flexFsStreamClose((FlexFsStream*)h); }
static uint32_t msfSize(void*, const char* p){ return flexFsSize(p); }
static bool msfExists(void*, const char* p){ return flexFsExists(p); }
static bool msfRemove(void*, const char* p){ return flexFsDelete(p); }
static bool msfMove(void*, const char* a, const char* b){ return flexFsMove(a, b); }
static bool msfTrash(void*, const char* p){ return flexFsTrash(p); }
static bool msfMkdir(void*, const char* p){ return flexFsMkdir(p); }
static int msfList(void*, const char* dir, FlexMsEntry* out, int maxn, int skip){
  FlexFsEntry e[8];
  int n = flexFsListFrom(dir, e, maxn < 8 ? maxn : 8, skip);
  for(int i = 0; i < n; i++){
    // FLEXFS_NAME_MAX (48) < 64: el nombre siempre cabe; la copia se acota al
    // campo de origen para que el limite quede escrito (y el compilador lo vea).
    snprintf(out[i].name, sizeof(out[i].name), "%.*s", (int)sizeof(e[i].name) - 1, e[i].name);
    out[i].size = e[i].size; out[i].dir = e[i].dir;
  }
  return n;
}
static bool msfAtomic(void*, const char* p, const void* b, size_t n){ return flexFsWriteBinAtomic(p, b, n); }
static void mlLockCb(void*){ mlLock(); }
static void mlUnlockCb(void*){ mlUnlock(); }
static uint32_t mlNowCb(void*){ return clkNowUtc(); }

// Dibujo de Paint (.fxp) -> lienzo RGB565 cuadrado sobre blanco. El formato
// y su reproductor viven en FlexOS_FS: la miniatura es el mismo dibujo.
struct MlPaintCtx { uint16_t* px; int ox, oy; };
static void mlPaintSeg(int x0, int y0, int x1, int y1, uint16_t color, int radius, void* user){
  (void)radius;
  MlPaintCtx* c = (MlPaintCtx*)user;
  int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for(int guard = 0; guard < 4096; guard++){
    int x = c->ox + x0, y = c->oy + y0;
    if((unsigned)x < ML_SIDE && (unsigned)y < ML_SIDE) c->px[y * ML_SIDE + x] = color;
    if(x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if(e2 >= dy){ err += dy; x0 += sx; }
    if(e2 <= dx){ err += dx; y0 += sy; }
  }
}
static bool mlPaintThumb(void*, const char* path, uint16_t* px, int* w, int* h){
  FlexPaintHdr hd;
  if(!flexPaintHeader(path, &hd) || !hd.w || !hd.h) return false;
  for(int i = 0; i < ML_SIDE * ML_SIDE; i++) px[i] = 0xFFFF;
  int fw, fh; mediaFitBox(hd.w, hd.h, ML_SIDE, ML_SIDE, fw, fh);
  MlPaintCtx pc = { px, (ML_SIDE - fw) / 2, (ML_SIDE - fh) / 2 };
  bool ok = flexPaintReplay(path, (float)fw / (float)hd.w, 0, 0, mlPaintSeg, &pc);
  *w = hd.w; *h = hd.h;
  return ok;
}

// -------------------------------------------------------------
//  LA TAREA DE FONDO: reconciliar, miniaturas y guardar
// -------------------------------------------------------------
static void mlYield(void*){ vTaskDelay(1); }       // el recorrido nunca acapara la CPU

static void mlTask(void*){
  flexMsCleanTmp(&gMs);                            // subidas y guardados que no terminaron
  uint32_t lastId = 0, dirtySince = 0, saveFails = 0, lastWarn = 0;
  bool deferred = false;
  for(;;){
    if(gMlScanReq){
      gMlScanReq = false;
      if(!flexMsScan(&gMs, mlYield, NULL))
        Serial.println(F("[medios] una carpeta no se pudo leer: no se retira nada del catalogo"));
      lastId = 0; deferred = false;
    }
    // Miniaturas: un pase por orden de id. Lo que no cabe en memoria se
    // aplaza y se reintenta al acabar el pase, tras una pausa.
    FlexMsJob job;
    bool worked = false;
    uint32_t idleMs = 5000;
    if(flexMsNextJob(&gMs, lastId, &job)){
      if(flexMsRunJob(&gMs, &job, memFreePsram(), FLEXMEM_RESERVE_BYTES + ML_PSRAM_MARGIN) == FLEXMS_JOB_RETRY) deferred = true;
      lastId = job.id;
      worked = true;
    } else if(lastId){
      lastId = 0;
      if(deferred){ deferred = false; idleMs = ML_RETRY_MS; }
      else worked = true;                          // otro pase por si algo volvio a necesitarla
    }
    // Guardado agrupado; si la flash falla, se espacia el reintento.
    if(gMs.dirty){ if(!dirtySince) dirtySince = millis() | 1u; }
    else dirtySince = 0;
    if(dirtySince && millis() - dirtySince >= ML_SAVE_DELAY_MS * (1u + (saveFails < 20 ? saveFails : 20))){
      if(flexMsSave(&gMs)) saveFails = 0;
      else {
        saveFails++;
        if(!lastWarn || millis() - lastWarn > 60000u){
          lastWarn = millis() | 1u;
          mlPostMsg("Biblioteca", "No se pudo guardar el cat\xC3\xA1logo (\xC2\xBF" "memoria llena?)");
        }
      }
      dirtySince = 0;
    }
    // Con trabajo, una pausa corta (la interfaz manda); sin trabajo, se
    // duerme hasta que alguien avise o pase un rato.
    if(worked) vTaskDelay(pdMS_TO_TICKS(5));
    else ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(dirtySince ? 400 : idleMs));
  }
}

// -------------------------------------------------------------
//  ARRANQUE Y TICK
// -------------------------------------------------------------
static void mlBegin(){
  if(gMlOk) return;
  if(!flexFsReady()){ Serial.println(F("[medios] sin almacenamiento: biblioteca desactivada")); return; }
  if(!gMlMux) gMlMux = xSemaphoreCreateMutex();
  if(!gMlStore) gMlStore = (FlexMlRec*)mediaAlloc(sizeof(FlexMlRec) * FML_CAP);
  if(!gMlMux || !gMlStore){
    Serial.println(F("[medios] sin memoria para el catalogo"));
    mediaFree(gMlStore); gMlStore = NULL;
    return;
  }
  memset(&gMs, 0, sizeof(gMs));
  gMs.fs = { msfOpen, msfRead, msfWrite, msfSeek, msfClose, msfSize, msfExists, msfRemove, msfMove, msfTrash,
             msfMkdir, msfList, msfAtomic, NULL };
  gMs.lock = mlLockCb; gMs.unlock = mlUnlockCb;
  gMs.alloc = mediaAlloc; gMs.free = mediaFree;
  gMs.now = mlNowCb;
  gMs.paintThumb = mlPaintThumb;
  flexMsInit(&gMs, gMlStore, FML_CAP);
  flexMsMakeDirs(&gMs);
  gMlLoadedFrom = (uint32_t)flexMsLoad(&gMs);      // el bueno, su .bak o su .tmp
  Serial.printf("[medios] catalogo: %u elementos (%s)\n", (unsigned)gMs.lib.n,
                gMlLoadedFrom == 1 ? "archivo" : gMlLoadedFrom ? "copia de respaldo" : "nuevo");
  gMlOk = true;
  gMlScanReq = true;                               // el disco manda: se reconcilia siempre al arrancar
  if(xTaskCreatePinnedToCore(mlTask, "flexMedia", ML_WORKER_STACK, NULL, 1, &gMlTask, 1) != pdPASS){
    gMlTask = NULL;
    Serial.println(F("[medios] no se pudo crear la tarea de fondo"));
  }
}

// Algo cambio en el disco por fuera de la biblioteca (Paint guardo un
// dibujo, se vacio la papelera): reconciliar en cuanto se pueda.
static void mlRequestScan(){ gMlScanReq = true; mlWake(); }

// Entrega en la isla los avisos que dejo la tarea de fondo. Va en loop():
// la isla solo se toca desde loopTask.
static void mlTick(){
  while(gMlMsgR != gMlMsgW){
    uint8_t r = gMlMsgR;
    sysNotify(gMlMsg[r % ML_MSG_N][0], gMlMsg[r % ML_MSG_N][1]);
    gMlMsgR = (uint8_t)(r + 1);
  }
}

// -------------------------------------------------------------
//  CACHE DE MINIATURAS (interfaz)
//  ------------------------------------------------------------
//  Compartida por Galeria, Multimedia y Musica. La clave es (id,
//  version): un cambio de miniatura (bloqueo, archivo sustituido,
//  edicion) sube la version y la entrada vieja deja de servir sola.
//  Un elemento bloqueado NUNCA entra: mlThumbDrop() borra sus pixeles.
// -------------------------------------------------------------
#define MLT_EMPTY 0
#define MLT_OK    1
#define MLT_FAIL  2
struct MlThumb { uint32_t id; uint8_t ver; uint8_t state; uint16_t* px; uint32_t useMs, pass; };
static MlThumb   gMlCache[ML_CACHE_N];
static uint32_t  gMlPass = 0;
static uint8_t*  gMlJpgBuf = NULL;                 // lectura del JPEG de la miniatura (48 KB, a demanda)
static MlThumb*  gMlDecDst = NULL;

static void mlThumbFreeSlot(MlThumb* t){
  if(t->px){ memset(t->px, 0, (size_t)ML_SIDE * ML_SIDE * 2); mediaFree(t->px); t->px = NULL; }
  t->id = 0; t->ver = 0; t->state = MLT_EMPTY; t->useMs = 0; t->pass = 0;
}
// Suelta la cache entera (al cerrar las apps o con poca memoria).
static size_t mlThumbDropAll(){
  size_t live = 0;
  for(int i = 0; i < ML_CACHE_N; i++){ if(gMlCache[i].px) live++; mlThumbFreeSlot(&gMlCache[i]); }
  if(gMlJpgBuf){ mediaFree(gMlJpgBuf); gMlJpgBuf = NULL; }
  return live;
}
// Olvida (y borra los pixeles de) la miniatura de un elemento.
static void mlThumbDrop(uint32_t id){
  for(int i = 0; i < ML_CACHE_N; i++) if(gMlCache[i].id == id) mlThumbFreeSlot(&gMlCache[i]);
}
static void mlThumbNewPass(){ gMlPass++; }

static bool mlThumbRow(void* user, int y, int w, const uint16_t* rgb){
  (void)user;
  MlThumb* t = gMlDecDst;
  if(!t || !t->px || y >= ML_SIDE) return false;
  memcpy(t->px + (size_t)y * ML_SIDE, rgb, (size_t)(w > ML_SIDE ? ML_SIDE : w) * 2);
  return true;
}

// La miniatura de `r` ya decodificada (132x132 RGB565), o NULL. Decodifica
// como mucho *budget en este repintado; si no le toca, *more = true.
// Hay que llamarla con el cerrojo del catalogo tomado (lee el registro).
static const uint16_t* mlThumbGetLocked(const FlexMlRec* r, int* budget, bool* more){
  if(!r || (r->flags & FML_R_LOCKED) || !(r->flags & FML_R_THUMB)) return NULL;
  MlThumb* hit = NULL;
  for(int i = 0; i < ML_CACHE_N && !hit; i++)
    if(gMlCache[i].state != MLT_EMPTY && gMlCache[i].id == r->id && gMlCache[i].ver == r->thumbVer) hit = &gMlCache[i];
  if(hit){
    hit->useMs = millis(); hit->pass = gMlPass;
    return hit->state == MLT_OK ? hit->px : NULL;
  }
  if(!budget || *budget <= 0){ if(more) *more = true; return NULL; }
  (*budget)--;
  // Ranura: libre, o la menos usada de las que NO se han pintado ya en
  // este repintado (si no, la cache se pisaria a si misma).
  MlThumb* slot = NULL;
  for(int i = 0; i < ML_CACHE_N; i++){
    MlThumb* t = &gMlCache[i];
    if(t->state == MLT_EMPTY){ slot = t; break; }
    if(t->pass == gMlPass) continue;
    if(!slot || t->useMs < slot->useMs) slot = t;
  }
  if(!slot) return NULL;
  if(!slot->px) slot->px = (uint16_t*)mediaAlloc((size_t)ML_SIDE * ML_SIDE * 2);
  if(!gMlJpgBuf) gMlJpgBuf = (uint8_t*)mediaAlloc(FML_LIMIT_THUMB);
  if(!slot->px || !gMlJpgBuf) return NULL;
  slot->id = r->id; slot->ver = r->thumbVer; slot->useMs = millis(); slot->pass = gMlPass;
  slot->state = MLT_FAIL;
  char tp[FML_PATH_MAX];
  flexMsThumbPath(r, tp, sizeof(tp));
  int n = flexFsReadBin(tp, gMlJpgBuf, FML_LIMIT_THUMB);
  if(n > 0){
    for(int i = 0; i < ML_SIDE * ML_SIDE; i++) slot->px[i] = 0x18C3;
    gMlDecDst = slot;
    if(flexJpegDecode(gMlJpgBuf, (size_t)n, ML_SIDE, ML_SIDE, 0, NULL, mlThumbRow, NULL, mediaAlloc, mediaFree) == FLEXJPG_OK)
      slot->state = MLT_OK;
    gMlDecDst = NULL;
  }
  return slot->state == MLT_OK ? slot->px : NULL;
}

// -------------------------------------------------------------
//  CAMBIOS DESDE LA INTERFAZ
//  ------------------------------------------------------------
//  El almacen hace el cambio (cerrojo, disco y registro juntos); aqui se
//  suelta ademas la miniatura de la RAM y se despierta a la tarea de
//  fondo (guardar, rehacer miniaturas). Devuelven false con el motivo en
//  `why`, para ensenarlo tal cual.
// -------------------------------------------------------------
static bool mlGet(uint32_t id, FlexMlRec* out){ return flexMsGet(&gMs, id, out); }

// Quien tiene ABIERTO un archivo de la biblioteca durante mucho rato (Musica
// mientras suena) lo suelta ANTES de que se mueva, se borre o se sustituya.
// Un solo oyente: solo Musica reproduce de continuo.
static void (*gMlBeforeChange)(uint32_t id) = NULL;
static inline void mlBeforeChange(uint32_t id){ if(gMlBeforeChange) gMlBeforeChange(id); }

// Borrado DEFINITIVO (con su miniatura). Lo protegido tambien: pero solo
// despues de que la interfaz haya pedido la clave del sistema.
static bool mlDelete(uint32_t id){
  mlBeforeChange(id);
  bool ok = flexMsDelete(&gMs, id);
  mlThumbDrop(id);
  mlWake();
  return ok;
}

// A la papelera (recuperable desde el Explorador). NUNCA un protegido: la
// papelera es publica y se veria su nombre y su contenido.
static bool mlTrash(uint32_t id){
  mlBeforeChange(id);
  bool ok = flexMsTrash(&gMs, id);
  mlThumbDrop(id);
  mlWake();
  return ok;
}

// Pone o quita el candado. Bloquear MUEVE el original y su miniatura a la
// carpeta protegida (el nombre en el disco pasa a ser el id: ni el
// Explorador, ni Almacenamiento, ni el selector de fondos pueden llegar a
// el ni ver como se llamaba). Desbloquear lo devuelve a su carpeta con su
// nombre, sin pisar nada. Solo se llama DESPUES de verificar la clave.
static bool mlSetLock(uint32_t id, bool lock, char* why, size_t whyCap){
  mlBeforeChange(id);
  bool ok = flexMsSetLock(&gMs, id, lock, why, whyCap);
  mlThumbDrop(id);                                 // ni un pixel suyo se queda en RAM
  mlWake();
  return ok;
}

// Renombra (nombre que se ENSENA y nombre en el disco). Un protegido solo
// cambia el nombre del catalogo: su archivo sigue llamandose por su id.
static bool mlRename(uint32_t id, const char* newName, char* why, size_t whyCap){
  mlBeforeChange(id);
  bool ok = flexMsRename(&gMs, id, newName, why, whyCap);
  mlWake();
  return ok;
}

// Registra un archivo NUEVO que ya esta completo en `tmp` (el editor,
// "guardar como copia"). Lo mueve a la carpeta de su clase y devuelve el
// id, o 0 con el motivo (y `tmp` sigue donde estaba).
static uint32_t mlAddFile(const char* tmp, int kind, const char* shownName, uint8_t origin, uint32_t parent,
                          char* why, size_t whyCap){
  uint32_t id = flexMsAddFile(&gMs, tmp, kind, shownName, origin, parent, why, whyCap);
  mlWake();
  return id;
}

// Sustituye el CONTENIDO de un elemento por `tmp` (el editor, "reemplazar
// original"). El original se aparta junto a si mismo, el nuevo ocupa su
// sitio y solo entonces se borra el apartado; si algo falla, el original
// vuelve, y si se va la luz a mitad lo resuelve el recorrido del arranque.
static bool mlReplaceFile(uint32_t id, const char* tmp, char* why, size_t whyCap){
  mlBeforeChange(id);
  bool ok = flexMsReplace(&gMs, id, tmp, why, whyCap);
  mlThumbDrop(id);
  mlWake();
  return ok;
}


// #############################################################
// ##  KIT DE INTERFAZ DE MEDIOS  ·  Galeria, Multimedia y Musica
// ##  ------------------------------------------------------
// ##  Un solo menu contextual, un solo dialogo y una sola ruta hacia
// ##  la clave del sistema para las tres apps: si el menu de la Galeria
// ##  y el de Musica se dibujaran por separado acabarian pareciendose
// ##  "casi", que es peor que distintos.
// #############################################################
enum { MA_NONE = 0, MA_OPEN, MA_SELECT, MA_LOCK, MA_UNLOCK, MA_RENAME, MA_EDIT, MA_TRASH, MA_DELETE,
       MA_CONNECT, MA_TRASHBIN, MA_INFO };
#define MM_MAX    8
#define MM_W      300
#define MM_RH     50
#define MM_PAD    10
#define MM_ANIM_MS 140

// ---- Candado dibujado (no hay imagen: vectorial, como el resto) ----
static void mlPadlock(int cx, int cy, int s, uint16_t col, bool open){
  // s = medio ancho del cuerpo.
  int bw = 2 * s, bh = s * 3 / 2, bx = cx - s, by = cy - bh / 4;
  fillRoundRect(bx, by, bw, bh, s / 3 + 1, col);
  float r = s * 0.62f, th = s * 0.22f + 0.8f;
  float sx = open ? cx + s * 0.55f : (float)cx;
  // Arco del asa con segmentos cortos (media circunferencia).
  float px0 = sx - r, py0 = (float)by;
  for(int k = 1; k <= 8; k++){
    float a = 3.14159f * (1.0f - k / 8.0f);
    float x1 = sx + r * cosf(a), y1 = by - s * 0.35f - r * sinf(a);
    if(k == 1) strokeSegAA(px0, py0, px0, by - s * 0.35f, th, col);
    strokeSegAA(px0, k == 1 ? by - s * 0.35f : py0, x1, y1, th, col);
    px0 = x1; py0 = y1;
  }
  if(!open) strokeSegAA(sx + r, by - s * 0.35f, sx + r, (float)by, th, col);
}

// ---- Iconos del menu ----
static void mmGlyph(int act, int cx, int cy, uint16_t col){
  uint16_t red = rgb565(228, 60, 60);
  switch(act){
    case MA_SELECT:
      drawCircle(cx, cy, 11, col); drawCircle(cx, cy, 10, col);
      strokeSegAA(cx - 5, cy, cx - 1, cy + 5, 1.8f, col);
      strokeSegAA(cx - 1, cy + 5, cx + 6, cy - 5, 1.8f, col);
      break;
    case MA_LOCK:   mlPadlock(cx, cy + 3, 8, col, false); break;
    case MA_UNLOCK: mlPadlock(cx - 2, cy + 3, 8, col, true); break;
    case MA_RENAME:
      drawRoundRect(cx - 14, cy - 7, 22, 14, 3, col);
      fillRect(cx + 10, cy - 11, 2, 22, col);
      fillRect(cx + 7, cy - 11, 8, 2, col); fillRect(cx + 7, cy + 9, 8, 2, col);
      break;
    case MA_EDIT:                                          // tres reguladores
      for(int i = 0; i < 3; i++){
        int y = cy - 8 + i * 8, kx = cx - 8 + ((i * 7) % 16);
        strokeSegAA(cx - 12, y, cx + 12, y, 1.4f, col);
        fillCircle(kx, y, 3, col);
      }
      break;
    case MA_TRASH: case MA_TRASHBIN:
      fillRect(cx - 10, cy - 12, 20, 3, col); fillRect(cx - 4, cy - 16, 8, 3, col);
      drawRoundRect(cx - 8, cy - 8, 16, 20, 3, col);
      fillRect(cx - 3, cy - 4, 2, 12, col); fillRect(cx + 1, cy - 4, 2, 12, col);
      break;
    case MA_DELETE:
      fillRect(cx - 10, cy - 12, 20, 3, red); fillRect(cx - 4, cy - 16, 8, 3, red);
      fillRoundRect(cx - 8, cy - 8, 16, 20, 3, red);
      fillRect(cx - 3, cy - 4, 2, 12, 0xFFFF); fillRect(cx + 1, cy - 4, 2, 12, 0xFFFF);
      break;
    case MA_CONNECT:                                       // movil con ondas
      drawRoundRect(cx - 12, cy - 12, 14, 24, 3, col);
      fillRect(cx - 8, cy + 7, 6, 2, col);
      for(int k = 0; k < 2; k++){
        int r = 6 + k * 5;
        for(int a = -3; a <= 3; a++){
          float t0 = a * 0.22f, t1 = (a + 1) * 0.22f;
          if(a == 3) break;
          strokeSegAA(cx + 4 + r * cosf(t0), cy + r * sinf(t0), cx + 4 + r * cosf(t1), cy + r * sinf(t1), 1.3f, col);
        }
      }
      break;
    default:                                               // informacion
      drawCircle(cx, cy, 11, col); fillRect(cx - 1, cy - 2, 3, 9, col); fillRect(cx - 1, cy - 7, 3, 3, col);
      break;
  }
}

static const char* mmLabel(int act){
  switch(act){
    case MA_SELECT:   return "Seleccionar";
    case MA_LOCK:     return gLockType == 2 ? "Bloquear con contrase\xC3\xB1" "a" : "Bloquear con PIN";
    case MA_UNLOCK:   return "Desbloquear";
    case MA_RENAME:   return "Renombrar";
    case MA_EDIT:     return "Editar";
    case MA_TRASH:    return "Mover a la papelera";
    case MA_DELETE:   return "Borrar";
    case MA_CONNECT:  return "Conectar con el m\xC3\xB3vil";
    case MA_TRASHBIN: return "Papelera";
  }
  return "Detalles";
}

static bool     mmOn = false;
static uint8_t  mmAct[MM_MAX], mmN = 0;
static int      mmAx = 0, mmAy = 0;
static uint32_t mmT0 = 0;
static bool     mmAnimDone = true;

// Siempre DENTRO de la pantalla (y del area de la app): si no cabe debajo
// del dedo se sube, si no cabe a la derecha se corre a la izquierda.
static void mmGeom(int &x, int &y, int &w, int &h){
  w = MM_W; h = mmN * MM_RH + 2 * MM_PAD;
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  if(w > bw - 16) w = bw - 16;
  x = mmAx - w / 2; y = mmAy;
  if(x + w > bx + bw - 8) x = bx + bw - 8 - w;
  if(x < bx + 8) x = bx + 8;
  if(y + h > by + bh - 8) y = by + bh - 8 - h;
  if(y < by + 8) y = by + 8;
}

// Dibuja el menu con `frac` (0..1) de su altura: se DESPLIEGA desde arriba.
// Cada cuadro cubre al anterior (mismo ancho, misma parte de arriba), asi
// que no hace falta restaurar lo de debajo.
static void mmDraw(float frac){
  int x, y, w, h; mmGeom(x, y, w, h);
  int hh = (int)(h * frac);
  if(hh < 24) hh = 24;
  if(hh > h) hh = h;
  int ox0 = gClipY0, ox1 = gClipY1;
  gClipY0 = y; gClipY1 = y + hh - 1;
  if(uiGlass) drawLiquidGlassPanel(x, y, w, hh, 18, rgb565(210, 214, 222));
  else        fillRoundRect(x, y, w, hh, 18, rgb565(206, 210, 218));
  for(int i = 0; i < mmN; i++){
    int ry = y + MM_PAD + i * MM_RH;
    if(ry + MM_RH / 2 > y + hh) break;
    uint16_t tc = mmAct[i] == MA_DELETE ? rgb565(200, 40, 40) : rgb565(16, 18, 24);
    drawTextClip(x + 18, ry + (MM_RH - uiLineH(2)) / 2, mmLabel(mmAct[i]), 2, tc, x + w - 50);
    mmGlyph(mmAct[i], x + w - 30, ry + MM_RH / 2, mmAct[i] == MA_DELETE ? rgb565(200, 40, 40) : rgb565(40, 44, 56));
    if(i + 1 < mmN) fillRect(x + 16, ry + MM_RH - 1, w - 32, 1, rgb565(176, 180, 192));
  }
  gClipY0 = ox0; gClipY1 = ox1;
  flxFlush(y - 2, y + hh + 2);
}

static void mmOpen(int ax, int ay, const uint8_t* acts, int n){
  mmN = 0;
  for(int i = 0; i < n && mmN < MM_MAX; i++) if(acts[i]) mmAct[mmN++] = acts[i];
  if(!mmN) return;
  mmAx = ax; mmAy = ay; mmOn = true; mmT0 = millis(); mmAnimDone = false;
  setBuf(fb);
  mmDraw(0.15f);
}
// Avanza la animacion de apertura. La llama el tick de la app mientras
// el menu esta abierto.
static void mmAnimTick(){
  if(!mmOn || mmAnimDone) return;
  uint32_t e = millis() - mmT0;
  float t = e >= MM_ANIM_MS ? 1.0f : e / (float)MM_ANIM_MS;
  float k = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);       // salida suave
  setBuf(fb);
  mmDraw(0.15f + 0.85f * k);
  if(t >= 1.0f) mmAnimDone = true;
}
// MA_* elegida, 0 si el toque cayo dentro sin elegir nada, -1 si fuera.
static int mmHit(int px, int py){
  int x, y, w, h; mmGeom(x, y, w, h);
  if(px < x || px > x + w || py < y || py > y + h) return -1;
  int i = (py - y - MM_PAD) / MM_RH;
  if(i < 0 || i >= mmN) return 0;
  return mmAct[i];
}

// ---- Dialogo (aviso o confirmacion) ----
static bool mmDlgOn = false;
static char mmDlgTitle[64], mmDlgText[320], mmDlgA[32], mmDlgB[24];
static bool mmDlgDanger = false;

static int mmWrap(int x, int y, int w, const char* s, int size, uint16_t col, bool draw){
  // Ajuste por palabras, con '\n' como salto forzado; devuelve la altura usada.
  int lh = uiLineH(size) + 6, cy = y;
  char line[96]; int li = 0;
  const char* p = s;
  while(*p){
    const char* q = p;
    while(*q && *q != ' ' && *q != '\n') q++;
    int wl = (int)(q - p);
    char cand[96];
    if(wl && li + (li ? 1 : 0) + wl < (int)sizeof(cand)){
      snprintf(cand, sizeof(cand), "%.*s%s%.*s", li, line, li ? " " : "", wl, p);
      if(li && textW(cand, size) > w){
        line[li] = 0; if(draw) drawTextC(x + w / 2, cy, line, size, col); cy += lh;
        li = snprintf(line, sizeof(line), "%.*s", wl, p);
      } else { li = snprintf(line, sizeof(line), "%s", cand); }
    }
    if(*q == '\n'){                                  // salto forzado
      line[li] = 0; if(draw && li) drawTextC(x + w / 2, cy, line, size, col); cy += lh;
      li = 0;
    }
    p = *q ? q + 1 : q;
  }
  if(li){ line[li] = 0; if(draw) drawTextC(x + w / 2, cy, line, size, col); cy += lh; }
  return cy - y;
}
static void mmDlgGeom(int &x, int &y, int &w, int &h){
  w = SCR_W - 64; x = 32;
  h = 118 + mmWrap(0, 0, w - 48, mmDlgText, 1, 0, false) + 70;
  y = (SCR_H - h) / 2;
}
static void mmDlgDraw(){
  int x, y, w, h; mmDlgGeom(x, y, w, h);
  setBuf(fb);
  if(uiGlass) drawLiquidGlassPanel(x, y, w, h, 24, rgb565(60, 64, 88));
  else        fillRoundRect(x, y, w, h, 24, rgb565(34, 38, 50));
  drawTextC(SCR_W / 2, y + 24, mmDlgTitle, 2, rgb565(255, 255, 255));
  mmWrap(x + 24, y + 66, w - 48, mmDlgText, 1, rgb565(190, 196, 212), true);
  int by = y + h - 76;
  if(mmDlgB[0]){
    int bw = (w - 48) / 2;
    fillRoundRect(x + 16, by, bw, 56, 16, rgb565(70, 74, 90));
    drawTextC(x + 16 + bw / 2, by + 18, mmDlgB, 2, rgb565(240, 242, 248));
    fillRoundRect(x + 32 + bw, by, bw, 56, 16, mmDlgDanger ? rgb565(220, 70, 70) : TH_PRIM);
    drawTextC(x + 32 + bw + bw / 2, by + 18, mmDlgA, 2, rgb565(255, 255, 255));
  } else {
    fillRoundRect(x + 16, by, w - 32, 56, 16, TH_PRIM);
    drawTextC(SCR_W / 2, by + 18, mmDlgA, 2, rgb565(255, 255, 255));
  }
  flxFlush(y - 2, y + h + 2);
}
// btnB vacio = un solo boton.
static void mmDlgOpen(const char* title, const char* text, const char* btnA, const char* btnB, bool danger){
  snprintf(mmDlgTitle, sizeof(mmDlgTitle), "%s", title ? title : "");
  snprintf(mmDlgText, sizeof(mmDlgText), "%s", text ? text : "");
  snprintf(mmDlgA, sizeof(mmDlgA), "%s", btnA ? btnA : "Aceptar");
  snprintf(mmDlgB, sizeof(mmDlgB), "%s", btnB ? btnB : "");
  mmDlgDanger = danger;
  mmDlgOn = true;
  mmDlgDraw();
}
// 0 abierto, 1 boton principal, -1 cancelado (boton secundario o fuera).
static int mmDlgTick(){
  if(!T.tap) return 0;
  int x, y, w, h; mmDlgGeom(x, y, w, h);
  int by = y + h - 76;
  if(T.y >= by && T.y <= by + 56){
    if(mmDlgB[0]){
      int bw = (w - 48) / 2;
      if(T.x >= x + 16 && T.x <= x + 16 + bw){ mmDlgOn = false; return -1; }
      if(T.x >= x + 32 + bw && T.x <= x + 32 + 2 * bw){ mmDlgOn = false; return 1; }
    } else if(T.x >= x + 16 && T.x <= x + w - 16){ mmDlgOn = false; return 1; }
  }
  if(T.x < x || T.x > x + w || T.y < y || T.y > y + h){ mmDlgOn = false; return -1; }
  return 0;
}

// Sin PIN ni contrasena no se bloquea nada: se dice y se ofrece el camino.
static void mediaNoLockDialog(){
  mmDlgOpen("Primero, un bloqueo",
            "Para bloquear archivos, configura primero un PIN o una contrase\xC3\xB1" "a en Seguridad.",
            "Ir a Seguridad", "Ahora no", false);
}

// -------------------------------------------------------------
//  LA CLAVE DEL SISTEMA PARA LOS MEDIOS PROTEGIDOS
//  ------------------------------------------------------------
//  No hay un PIN de la Galeria: se usa la MISMA pantalla y la MISMA
//  verificacion que el bloqueo del sistema (lsuStartVerifyFor), con su
//  limitador de intentos. Al acertar (o cancelar) se vuelve a la app que
//  lo pidio y ella hace la accion con la lista que dejo apartada.
// -------------------------------------------------------------
// -------------------------------------------------------------
//  TABLAS DE TRABAJO DE LAS APPS DE MEDIOS, EN PSRAM
//  ------------------------------------------------------------
//  Listas de ids (seleccion, destino de una accion, lo que espera a la clave)
//  y las vistas del catalogo de Galeria, Multimedia y Musica: ~7 KB que NO
//  necesitan RAM interna. La interna del P4 va justa (de ella salen Wi-Fi,
//  lwIP, TLS y las pilas de las tareas; la compilacion real para esp32p4 la
//  deja por debajo de 90 KB libres) y su .bss no puede ir a PSRAM en este
//  SDK (CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY esta apagado). Se
//  reservan UNA vez, la primera vez que hacen falta. Sin PSRAM la capacidad
//  es 0: las listas salen vacias y nada escribe fuera.
// -------------------------------------------------------------
struct MlTables {
  uint32_t sel[FML_CAP];          // seleccion multiple del kit
  uint32_t ids[FML_CAP];          // ids sobre los que actua una accion
  uint32_t authIds[FML_CAP];      // los que esperan a la clave del sistema
  uint16_t galView[FML_CAP], vidView[FML_CAP], musView[FML_CAP];
};
static MlTables* gMlT = NULL;
static MlTables* mlTables(){
  if(!gMlT){
    gMlT = (MlTables*)mediaAlloc(sizeof(MlTables));
    if(gMlT) memset(gMlT, 0, sizeof(*gMlT));
  }
  return gMlT;
}

typedef void (*MediaAuthDone)(bool ok, int act, const uint32_t* ids, int n);
static struct {
  uint8_t act; int8_t app; uint16_t n;
  uint32_t* ids;                  // mlTables()->authIds (NULL sin PSRAM: n = 0)
  MediaAuthDone done;
} gMediaAuth;

static void mediaAfterVerify(bool ok){
  MediaAuthDone cb = gMediaAuth.done;
  gMediaAuth.done = NULL;
  gState = ST_APP;
  if(gMediaAuth.app >= 0 && gMediaAuth.app < APP_N) gAppId = gMediaAuth.app;
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, WIN_BG);
  appDrawChrome(gAppId);
  if(!(APP_REG[gAppId].flags & APP_CUSTOM_HEADER)) appDrawHeader(gAppId);
  if(cb) cb(ok, gMediaAuth.act, gMediaAuth.ids, gMediaAuth.n);
  flxFlushAll();
  gMediaAuth.n = 0;
}

// Pide la clave y luego llama a `done`. Sin clave configurada en el
// sistema no hay nada que comprobar (el dueno la quito): se sigue.
static void mediaAuthRequest(int act, const uint32_t* ids, int n, MediaAuthDone done){
  MlTables* t = mlTables();
  int cap = t ? FML_CAP : 0;
  if(n < 0) n = 0;
  if(n > cap) n = cap;
  gMediaAuth.ids = t ? t->authIds : NULL;
  gMediaAuth.act = (uint8_t)act; gMediaAuth.app = (int8_t)gAppId; gMediaAuth.n = (uint16_t)n;
  if(n) memcpy(gMediaAuth.ids, ids, sizeof(uint32_t) * (size_t)n);
  gMediaAuth.done = done;
  if(gLockType == 0){ mediaAfterVerify(true); return; }
  lsuStartVerifyFor(LSU_AFTER_MEDIA, gAppId);
}

// ---- Vuelco de una miniatura (132x132) en una celda ----------------
// Centrada y recortada a la celda, respetando el recorte activo. Las
// esquinas se redondean contra `bg` para que la foto no asome por fuera
// de la tarjeta.
static void mlBlitThumb(const uint16_t* src, int x, int y, int w, int h, int rad, uint16_t bg){
  if(!src || w <= 0 || h <= 0) return;
  int sx0 = (ML_SIDE - w) / 2, sy0 = (ML_SIDE - h) / 2;
  int dx0 = x, dy0 = y, cw = w, ch = h;
  if(sx0 < 0){ dx0 -= sx0; cw = ML_SIDE; sx0 = 0; }
  if(sy0 < 0){ dy0 -= sy0; ch = ML_SIDE; sy0 = 0; }
  for(int ry = 0; ry < ch; ry++){
    int dy = dy0 + ry;
    if(dy < gClipY0 || dy > gClipY1 || dy < 0 || dy >= SCR_H) continue;
    const uint16_t* row = src + (size_t)(sy0 + ry) * ML_SIDE + sx0;
    if(gLand){ for(int rx = 0; rx < cw; rx++) px(dx0 + rx, dy, row[rx]); continue; }
    int dx = dx0, n = cw;
    if(dx < gClipX0){ row += gClipX0 - dx; n -= gClipX0 - dx; dx = gClipX0; }
    if(dx + n > gClipX1 + 1) n = gClipX1 + 1 - dx;
    if(dx < 0){ row -= dx; n += dx; dx = 0; }
    if(dx + n > SCR_W) n = SCR_W - dx;
    if(n > 0) memcpy(gBuf + (size_t)dy * SCR_W + dx, row, (size_t)n * 2);
  }
  if(rad <= 1 || cw != w || ch != h) return;
  // Esquinas: lo que queda fuera del cuarto de circulo vuelve al fondo.
  for(int j = 0; j < rad; j++) for(int i = 0; i < rad; i++){
    int ddx = rad - i, ddy = rad - j;
    if(ddx * ddx + ddy * ddy <= rad * rad) continue;
    px(x + i, y + j, bg); px(x + w - 1 - i, y + j, bg);
    px(x + i, y + h - 1 - j, bg); px(x + w - 1 - i, y + h - 1 - j, bg);
  }
}

// Miniatura a un TERCIO (132 -> 44 px, media de cada 3x3) para las filas de
// Multimedia y Musica. Se escribe directa al lienzo, sin reservar nada; las
// esquinas redondeadas no se pintan (se ve la tarjeta de debajo).
#define ML_SMALL (ML_SIDE / 3)
static void mlBlitThumbSmall(const uint16_t* src, int x, int y, int rad){
  if(!src) return;
  for(int ry = 0; ry < ML_SMALL; ry++){
    int dy = y + ry;
    if(dy < gClipY0 || dy > gClipY1) continue;
    for(int rx = 0; rx < ML_SMALL; rx++){
      int dx = x + rx;
      if(dx < gClipX0 || dx > gClipX1) continue;
      int cx = rx < rad ? rad - rx : (rx >= ML_SMALL - rad ? rx - (ML_SMALL - 1 - rad) : 0);
      int cy = ry < rad ? rad - ry : (ry >= ML_SMALL - rad ? ry - (ML_SMALL - 1 - rad) : 0);
      if(cx && cy && cx * cx + cy * cy > rad * rad) continue;
      const uint16_t* p = src + (size_t)(ry * 3) * ML_SIDE + (size_t)rx * 3;
      uint32_t r = 0, g = 0, b = 0;
      for(int k = 0; k < 3; k++, p += ML_SIDE)
        for(int j = 0; j < 3; j++){ uint16_t c = p[j]; r += c >> 11; g += (c >> 5) & 63u; b += c & 31u; }
      px(dx, dy, (uint16_t)(((r / 9) << 11) | ((g / 9) << 5) | (b / 9)));
    }
  }
}

// Duracion "m:ss" para las insignias.
static void mlFmtDur(uint32_t ms, char* out, size_t n){
  uint32_t s = (ms + 500) / 1000;
  if(s >= 3600) snprintf(out, n, "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60), (unsigned long)(s % 60));
  else snprintf(out, n, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}
